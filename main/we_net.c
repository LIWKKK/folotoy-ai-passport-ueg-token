// main/we_net.c —— STA 联网与余额刷新。
#include "we_net.h"

#include "we_http.h"
#include "we_f1_net.h"
#include "we_logic.h"
#include "we_storage.h"
#include "we_time.h"
#include "we_wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "we_net";

#define NET_CONNECTED_BIT BIT0
#define NET_FAIL_BIT      BIT1

#define CONNECT_TIMEOUT_MS 20000
// 网络任务栈。这里的峰值不是我们的代码,而是 esp_http_client + mbedtls
// TLS 握手(典型 4~8 KB),而且 F1 抓取还会在本帧里放解析用的局部变量。
// 原先 6144 从未被真正验证过:设备一直没配过 Wi-Fi,ensure_connected()
// 总是提前返回,所以 TLS 一次都没跑过 —— 配网成功那一刻才会暴露。
// 按 esp_http_client 的默认值取 10240,给 TLS 留足余量。
#define NET_TASK_STACK     10240
#define HTTP_BODY_MAX      2048

typedef struct {
    const char *url;
    we_plat_kind_t kind;
} platform_endpoint_t;

static const platform_endpoint_t ENDPOINTS[WE_PLAT_KIND_COUNT] = {
    [WE_PLAT_DEEPSEEK] = { "https://api.deepseek.com/user/balance",
                           WE_PLAT_DEEPSEEK },
    // Kimi 同时有 .cn 与 .ai 两个域名;国内设备走 .cn 更稳,失败时回落 .ai。
    [WE_PLAT_KIMI]     = { "https://api.moonshot.cn/v1/users/me/balance",
                           WE_PLAT_KIMI },
};

static const char *KIMI_FALLBACK_URL =
    "https://api.moonshot.ai/v1/users/me/balance";

static we_state_t        *s_state;
static SemaphoreHandle_t  s_lock;
static EventGroupHandle_t s_events;
static TaskHandle_t       s_task;
static esp_netif_t       *s_sta_netif;
static bool               s_sta_started;
static volatile bool      s_connected;
static volatile bool      s_refreshing;
static volatile uint32_t  s_data_version;
static void             (*s_done_cb)(void);

// ---------------------------------------------------------------------------
// Wi-Fi 事件
// ---------------------------------------------------------------------------
static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_events) xEventGroupSetBits(s_events, NET_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        if (s_events) xEventGroupSetBits(s_events, NET_CONNECTED_BIT);
    }
}

static esp_err_t sta_stack_init(void)
{
    esp_err_t err = we_wifi_ensure();
    if (err != ESP_OK) return err;

    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_FAIL;

    if (!s_events) {
        s_events = xEventGroupCreate();
        if (!s_events) return ESP_ERR_NO_MEM;
    }

    if (!s_sta_started) {
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            on_wifi_event, NULL, NULL);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            on_wifi_event, NULL, NULL);
        s_sta_started = true;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// 工作任务
// ---------------------------------------------------------------------------
static void do_refresh(void);

static void net_worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        do_refresh();
    }
}

esp_err_t we_net_init(we_state_t *state, SemaphoreHandle_t state_lock)
{
    if (!state || !state_lock) return ESP_ERR_INVALID_ARG;
    s_state = state;
    s_lock = state_lock;

    if (s_task) return ESP_OK;
    if (xTaskCreate(net_worker_task, "we_net", NET_TASK_STACK, NULL, 4,
                    &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// 连接
// ---------------------------------------------------------------------------
static esp_err_t ensure_connected(uint32_t timeout_ms)
{
    if (s_connected) return ESP_OK;

    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    char ssid[WE_SSID_BYTES] = { 0 };
    char pass[WE_PASS_BYTES] = { 0 };
    bool has_wifi = s_state->has_wifi;
    if (has_wifi) {
        snprintf(ssid, sizeof(ssid), "%s", s_state->wifi_ssid);
        snprintf(pass, sizeof(pass), "%s", s_state->wifi_pass);
    }
    if (s_lock) xSemaphoreGive(s_lock);

    if (!has_wifi || ssid[0] == '\0') {
        ESP_LOGW(TAG, "未配置 Wi-Fi,跳过联网");
        return ESP_ERR_INVALID_STATE;
    }

    // 配网热点正开着时不抢占 Wi-Fi:AP 与 STA 切来切去会打断手机配置页。
    if (we_wifi_ap_active()) {
        ESP_LOGW(TAG, "配网热点占用中,暂不联网");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sta_stack_init();
    if (err != ESP_OK) return err;

    wifi_config_t wc = { 0 };
    // Wi-Fi 字段是定长数组(ssid[32] / password[64]),用显式截断的复制,
    // 不靠 snprintf —— 后者在编译期被判定可能截断时会被 -Werror 拦下。
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    ((char *)wc.sta.ssid)[sizeof(wc.sta.ssid) - 1] = '\0';
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);
    ((char *)wc.sta.password)[sizeof(wc.sta.password) - 1] = '\0';
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    xEventGroupClearBits(s_events, NET_CONNECTED_BIT | NET_FAIL_BIT);

    // 协议栈可能已被 AP 模式 start 过,那种情况下 set_mode 才是缺的一步。
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) return err;
    err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) return err;
    err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) return err;

    EventBits_t bits = xEventGroupWaitBits(
        s_events, NET_CONNECTED_BIT | NET_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & NET_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi 已连接: %s", ssid);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Wi-Fi 连接超时: %s", ssid);
    return ESP_ERR_TIMEOUT;
}

esp_err_t we_net_connect_blocking(uint32_t timeout_ms)
{
    return ensure_connected(timeout_ms);
}

// ---------------------------------------------------------------------------
// 余额 → 花费 → Token
// ---------------------------------------------------------------------------
// 成功取到余额后,把新余额并进当日快照(差额即花费)。
static void apply_sample(we_plat_kind_t kind, int32_t cents)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int32_t delta = we_account_balance(s_state, kind, cents,
                                       we_time_today_index(), we_time_now());
    xSemaphoreGive(s_lock);
    if (delta > 0) {
        ESP_LOGI(TAG, "%s 新增花费 %ld 分", we_plat_name(kind), (long)delta);
    }
}

static int fetch_balance(const platform_endpoint_t *ep, const char *key,
                         char *body, size_t body_len)
{
    int rc = we_http_get(ep->url, key, body, body_len);
    // Kimi 主域名失败时试备用域名(国内/海外两个入口)。
    if (rc != 0 && ep->kind == WE_PLAT_KIMI) {
        ESP_LOGW(TAG, "Kimi 主域名失败,改用备用域名");
        rc = we_http_get(KIMI_FALLBACK_URL, key, body, body_len);
    }
    return rc;
}

static void refresh_platform(const platform_endpoint_t *ep)
{
    char key[WE_KEY_BYTES] = { 0 };
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    bool has_key = s_state->platforms[ep->kind].has_key;
    if (has_key)
        snprintf(key, sizeof(key), "%s", s_state->platforms[ep->kind].key);
    s_state->platforms[ep->kind].status = WE_NET_LOADING;
    s_state->platforms[ep->kind].error[0] = '\0';
    if (s_lock) xSemaphoreGive(s_lock);

    if (!has_key) return;

    static char body[HTTP_BODY_MAX];
    int rc = fetch_balance(ep, key, body, sizeof(body));

    int32_t cents = -1;
    char currency[WE_CURRENCY_BYTES] = { 0 };
    bool parsed = (rc == 0) && we_parse_balance(body, ep->kind, &cents,
                                                currency, sizeof(currency));

    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    we_platform_t *plat = &s_state->platforms[ep->kind];
    if (parsed) {
        plat->balance_cents = cents;
        snprintf(plat->currency, sizeof(plat->currency), "%s", currency);
        plat->status = WE_NET_OK;
        plat->last_ok_unix = we_time_now();
    } else {
        plat->status = WE_NET_ERROR;
        snprintf(plat->error, sizeof(plat->error), "%s",
                 rc == 0 ? "Bad response" : "Query failed");
    }
    if (s_lock) xSemaphoreGive(s_lock);

    if (parsed) apply_sample(ep->kind, cents);
}

static void do_refresh(void)
{
    s_refreshing = true;

    if (ensure_connected(CONNECT_TIMEOUT_MS) == ESP_OK) {
        we_time_sync(10000);      // 联网时顺手对时,失败不影响刷新
        for (size_t i = 0; i < WE_PLAT_KIND_COUNT; i++)
            refresh_platform(&ENDPOINTS[i]);
        // F1 与余额同一套联网窗口:连上就顺便拉一次,不额外开连接流程。
        we_f1_net_refresh();
    } else if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < WE_PLAT_KIND_COUNT; i++) {
            if (!s_state->platforms[i].has_key) continue;
            s_state->platforms[i].status = WE_NET_ERROR;
            snprintf(s_state->platforms[i].error,
                     sizeof(s_state->platforms[i].error), "Offline");
        }
        xSemaphoreGive(s_lock);
    }

    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        we_storage_save(s_state);
        xSemaphoreGive(s_lock);
    }

    s_refreshing = false;
    s_data_version++;

    if (s_done_cb) s_done_cb();
}

bool we_net_is_refreshing(void) { return s_refreshing; }
bool we_net_is_connected(void)  { return s_connected; }

void we_net_set_refresh_done_cb(void (*cb)(void)) { s_done_cb = cb; }

uint32_t we_net_data_version(void) { return s_data_version; }

void we_net_request_refresh(void)
{
    if (!s_task) return;
    xTaskNotify(s_task, 1, eSetValueWithOverwrite);
}

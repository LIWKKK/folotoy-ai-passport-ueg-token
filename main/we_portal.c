// main/we_portal.c —— 无云依赖的本地配置门户。
#include "we_portal.h"

#include "we_config.h"
#include "we_storage.h"
#include "we_time.h"
#include "we_wifi.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "we_portal";

// 热点密码固定:配置页只在设备热点内开放,用户照着屏幕输入即可。
#define PORTAL_PASSWORD "ordinarylife"
#define FORM_BODY_MAX   4096

static we_state_t *s_state;
static SemaphoreHandle_t s_lock;
static we_portal_changed_cb_t s_changed_cb;
static void *s_changed_user;
static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static bool s_wifi_started;
static char s_ssid[WE_SSID_BYTES] = "UEG-TOKEN";

static void text_copy(char *dst, size_t len, const char *src)
{
    if (!dst || len == 0) return;
    src = src ? src : "";
    size_t out = 0;
    while (*src && out + 1 < len) {
        unsigned char first = (unsigned char)*src;
        size_t bytes = first < 0x80 ? 1 :
                       (first & 0xE0) == 0xC0 ? 2 :
                       (first & 0xF0) == 0xE0 ? 3 :
                       (first & 0xF8) == 0xF0 ? 4 : 1;
        if (out + bytes >= len) break;
        bool valid = true;
        for (size_t i = 1; i < bytes; i++) {
            if (!src[i] || ((unsigned char)src[i] & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            src++;
            continue;
        }
        memcpy(dst + out, src, bytes);
        out += bytes;
        src += bytes;
    }
    dst[out] = '\0';
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    size_t out = 0;
    for (size_t i = 0; i < src_len && out + 1 < dst_len; i++) {
        if (src[i] == '+') {
            dst[out++] = ' ';
        } else if (src[i] == '%' && i + 2 < src_len) {
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
            }
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

static bool form_get(const char *body, const char *key, char *out, size_t out_len)
{
    if (!body || !key || !out || out_len == 0) return false;
    size_t key_len = strlen(key);
    const char *cursor = body;
    while (*cursor) {
        const char *end = strchr(cursor, '&');
        if (!end) end = cursor + strlen(cursor);
        const char *eq = memchr(cursor, '=', (size_t)(end - cursor));
        if (eq && (size_t)(eq - cursor) == key_len &&
            memcmp(cursor, key, key_len) == 0) {
            url_decode(out, out_len, eq + 1, (size_t)(end - eq - 1));
            return true;
        }
        cursor = *end ? end + 1 : end;
    }
    out[0] = '\0';
    return false;
}

static void send_html_escaped(httpd_req_t *req, const char *text)
{
    const char *cursor = text ? text : "";
    while (*cursor) {
        const char *escaped = NULL;
        switch (*cursor) {
        case '&': escaped = "&amp;"; break;
        case '<': escaped = "&lt;"; break;
        case '>': escaped = "&gt;"; break;
        case '"': escaped = "&quot;"; break;
        case '\'': escaped = "&#39;"; break;
        default: break;
        }
        if (escaped) {
            httpd_resp_sendstr_chunk(req, escaped);
        } else {
            char one[2] = { *cursor, '\0' };
            httpd_resp_sendstr_chunk(req, one);
        }
        cursor++;
    }
}

// 单个输入行:标签 + 文本输入框。value 会被转义,可安全放入 value=""。
static void send_field(httpd_req_t *req, const char *key, const char *label,
                       const char *value, size_t maxlen, const char *type,
                       const char *note)
{
    char line[256];
    snprintf(line, sizeof(line),
             "<div class=\"row\"><label for=\"%s\">%s</label><div>"
             "<input id=\"%s\" name=\"%s\" type=\"%s\" maxlength=\"%u\" value=\"",
             key, label, key, key, type, (unsigned)maxlen);
    httpd_resp_sendstr_chunk(req, line);
    send_html_escaped(req, value);
    httpd_resp_sendstr_chunk(req, "\">");
    if (note && *note) {
        snprintf(line, sizeof(line), "<div class=\"note\">%s</div>", note);
        httpd_resp_sendstr_chunk(req, line);
    }
    httpd_resp_sendstr_chunk(req, "</div></div>");
}

static void restart_timer_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static esp_err_t root_get(httpd_req_t *req)
{
    we_state_t snapshot;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) != pdTRUE)
        return httpd_resp_send_500(req);
    snapshot = *s_state;
    xSemaphoreGive(s_lock);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    // 配置页必须每次重取:同一地址(192.168.4.1)在不同固件间内容不同,
    // 手机浏览器缓存旧页会让用户看到上一版表单。
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>UEG 数字生命卡</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#07090c;color:#edf3f7;"
        "font:15px/1.55 -apple-system,BlinkMacSystemFont,'PingFang SC',sans-serif}"
        "header{border-top:4px solid #ff7a1a;border-bottom:1px solid #29343e;"
        "padding:18px 16px}main{max-width:720px;margin:auto;padding:0 16px 40px}"
        "h1{font-size:22px;margin:0}h2{font-size:17px;color:#ff9b50;margin:24px 0 8px}"
        ".sub{color:#8fa1af}.row{display:grid;grid-template-columns:140px 1fr;gap:12px;"
        "align-items:start;padding:9px 0;border-bottom:1px solid #1d2730}"
        "label{color:#aebbc5;padding-top:9px}"
        "input,select{width:100%;background:#111820;color:#edf3f7;border:1px solid #344451;"
        "border-radius:4px;padding:9px}"
        "button,.btn{display:inline-block;background:#ff7a1a;color:#101010;border:0;"
        "border-radius:4px;padding:11px 16px;font-weight:700;text-decoration:none;"
        "margin:16px 8px 0 0}"
        ".btn.alt{background:#25323d;color:#edf3f7}.note{color:#8fa1af;font-size:13px;"
        "margin-top:6px}"
        // 保存条常驻页顶:字段多,底部按钮在手机上要滑很久才看得到,
        // 用户会以为“无处可保存”。
        ".savebar{position:sticky;top:0;z-index:5;background:#07090c;"
        "border-bottom:1px solid #29343e;padding:12px 0;margin-bottom:10px;"
        "display:flex;align-items:center;gap:12px}"
        "@media(max-width:560px){.row{grid-template-columns:1fr}label{padding-top:0}}</style>"
        "</head><body>"
        "<header><h1>UEG 数字生命卡</h1>"
        "<div class=\"sub\">AI 余额与用量热力看板 · 本地配置</div></header><main>");

    httpd_resp_sendstr_chunk(req,
        "<form method=\"post\" action=\"/save\" id=\"cfg\">"
        "<input type=\"hidden\" name=\"epoch\" id=\"epoch\">");

    // 顶部常驻保存按钮:不依赖滚动,任何屏幕尺寸下都能看到“保存并重启”。
    httpd_resp_sendstr_chunk(req,
        "<div class=\"savebar\"><button type=\"submit\">保存并重启</button>"
        "<span class=\"sub\">填完后点这里</span></div>");

    // 身份资料(不含生活信条 —— 该字段已按需求移除)。
    httpd_resp_sendstr_chunk(req, "<h2>身份资料</h2>");
    send_field(req, "name", "姓名", snapshot.profile.name,
               WE_NAME_BYTES - 1, "text", "");
    send_field(req, "identity", "身份编号", snapshot.profile.identity,
               WE_ID_BYTES - 1, "text", "");
    send_field(req, "role", "身份角色", snapshot.profile.role,
               WE_ROLE_BYTES - 1, "text", "");

    // Wi-Fi:设备靠它联网查余额。
    httpd_resp_sendstr_chunk(req,
        "<h2>Wi-Fi</h2><p class=\"note\">设备用这组凭证连接你的路由器,"
        "向 DeepSeek / Kimi 官方接口查询余额。凭证只存在设备本地。</p>");
    send_field(req, "ssid", "Wi-Fi 名称", snapshot.wifi_ssid,
               WE_SSID_BYTES - 1, "text", "");
    send_field(req, "pass", "Wi-Fi 密码", snapshot.wifi_pass,
               WE_PASS_BYTES - 1, "password", "");

    // 平台 Key。
    httpd_resp_sendstr_chunk(req,
        "<h2>平台 Key</h2><p class=\"note\">留空表示不启用该平台。"
        "Key 只保存在设备 NVS,直接发往对应官方域名。</p>");
    for (int i = 0; i < WE_PLAT_KIND_COUNT; i++) {
        const we_platform_t *plat = &snapshot.platforms[i];
        char label[48];
        char key[16];
        snprintf(label, sizeof(label), "%s Key", we_plat_name((we_plat_kind_t)i));
        snprintf(key, sizeof(key), "key%d", i);
        send_field(req, key, label, plat->key, WE_KEY_BYTES - 1, "text",
                   i == WE_PLAT_KIMI ? "Kimi 以美元计价,看板按固定汇率折算。" : "");
    }

    // 告警阈值。
    char line[512];
    snprintf(line, sizeof(line),
             "<h2>告警</h2><div class=\"row\"><label for=\"threshold\">低余额阈值</label>"
             "<div><input id=\"threshold\" name=\"threshold\" type=\"number\" min=\"0\" "
             "max=\"999999\" value=\"%ld\"><div class=\"note\">余额低于该值(元)"
             "时状态灯转为告警色。</div></div></div>",
             (long)(snapshot.threshold_cents / 100));
    httpd_resp_sendstr_chunk(req, line);

    snprintf(line, sizeof(line),
             "<h2>当前状态</h2><p>已配置平台 %d 个。保存后设备将重启并尝试联网刷新。</p>"
             "<button type=\"submit\">保存并重启</button>"
             "</form><p class=\"note\">配置只在设备热点内传输,不经过任何第三方服务器。"
             "板载 NFC 为独立被动标签,固件无法读写。</p>"
             "<script>document.getElementById('cfg').addEventListener('submit',function(){"
             "document.getElementById('epoch').value=Math.floor(Date.now()/1000)"
             "})</script></main></body></html>",
             we_enabled_platforms(&snapshot));
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t save_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= FORM_BODY_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "表单过大");
        return ESP_FAIL;
    }
    char *body = malloc((size_t)req->content_len + 1);
    if (!body) return httpd_resp_send_500(req);
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            free(body);
            return ESP_FAIL;
        }
        received += n;
    }
    body[received] = '\0';

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        free(body);
        return httpd_resp_send_500(req);
    }

    // Key 最长,缓冲按它开。
    char value[WE_KEY_BYTES * 3];
    if (form_get(body, "name", value, sizeof(value)))
        text_copy(s_state->profile.name, sizeof(s_state->profile.name), value);
    if (form_get(body, "identity", value, sizeof(value)))
        text_copy(s_state->profile.identity, sizeof(s_state->profile.identity), value);
    if (form_get(body, "role", value, sizeof(value)))
        text_copy(s_state->profile.role, sizeof(s_state->profile.role), value);
    s_state->profile.configured = true;

    if (form_get(body, "ssid", value, sizeof(value)))
        text_copy(s_state->wifi_ssid, sizeof(s_state->wifi_ssid), value);
    if (form_get(body, "pass", value, sizeof(value)))
        text_copy(s_state->wifi_pass, sizeof(s_state->wifi_pass), value);
    s_state->has_wifi = (s_state->wifi_ssid[0] != '\0');

    for (int i = 0; i < WE_PLAT_KIND_COUNT; i++) {
        char key[16];
        snprintf(key, sizeof(key), "key%d", i);
        if (form_get(body, key, value, sizeof(value))) {
            // 换了 Key 就重置余额与游标:旧余额属于另一个账号。
            bool changed = (strcmp(s_state->platforms[i].key, value) != 0);
            text_copy(s_state->platforms[i].key,
                      sizeof(s_state->platforms[i].key), value);
            if (changed) {
                s_state->platforms[i].balance_cents = -1;
                s_state->platforms[i].today_spend_cents = 0;
                s_state->cursor_cents[i] = -1;
                s_state->cursor_unix[i] = 0;
                s_state->platforms[i].status = WE_NET_IDLE;
            }
        }
    }

    if (form_get(body, "threshold", value, sizeof(value))) {
        long yuan = strtol(value, NULL, 10);
        if (yuan < 0) yuan = 0;
        if (yuan > 999999) yuan = 999999;
        s_state->threshold_cents = (int32_t)(yuan * 100);
    }

    // 用手机时间校时:设备没有 RTC 电池,这一步让热力图有正确的"今天"。
    if (form_get(body, "epoch", value, sizeof(value))) {
        int64_t epoch = strtoll(value, NULL, 10);
        if (epoch > 1700000000) we_time_set_epoch(epoch);
    }

    we_state_sanitize(s_state);
    esp_err_t err = we_storage_save(s_state);
    xSemaphoreGive(s_lock);
    free(body);

    if (s_changed_cb) s_changed_cb(s_changed_user);
    if (err != ESP_OK) return httpd_resp_send_500(req);

    // 重启让新凭证生效,也避免 AP/STA 在运行中来回切换。
    // 必须先把响应发完再重启 —— 立刻重启会让浏览器收到空响应,
    // 用户以为没保存成功。延时 1.5 s 足够 TCP 把这一页送出去。
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_sendstr(req,
        "<!doctype html><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<body style=\"margin:0;background:#07090c;color:#edf3f7;font:15px/1.6 "
        "-apple-system,sans-serif;padding:32px\"><h1 style=\"color:#ff7a1a\">已保存</h1>"
        "<p>设备正在重启并尝试联网刷新余额。可以断开热点,直接查看设备屏幕。</p></body>");

    const esp_timer_create_args_t timer_args = {
        .callback = restart_timer_cb,
        .name = "we_restart",
    };
    esp_timer_handle_t timer = NULL;
    if (esp_timer_create(&timer_args, &timer) == ESP_OK)
        esp_timer_start_once(timer, 1500000);   // 1.5 s
    return ESP_OK;
}

static esp_err_t register_handlers(void)
{
    const httpd_uri_t handlers[] = {
        { .uri = "/", .method = HTTP_GET, .handler = root_get },
        { .uri = "/save", .method = HTTP_POST, .handler = save_post },
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(s_server, &handlers[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t we_portal_init(we_state_t *state, SemaphoreHandle_t state_lock,
                         we_portal_changed_cb_t changed_cb, void *user)
{
    if (!state || !state_lock) return ESP_ERR_INVALID_ARG;
    s_state = state;
    s_lock = state_lock;
    s_changed_cb = changed_cb;
    s_changed_user = user;

    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) == ESP_OK)
        snprintf(s_ssid, sizeof(s_ssid), "UEG-TOKEN-%02X%02X", mac[4], mac[5]);
    return ESP_OK;
}

esp_err_t we_portal_start(void)
{
    if (s_wifi_started) return ESP_OK;
    if (!s_state || !s_lock) return ESP_ERR_INVALID_STATE;

    esp_err_t err = we_wifi_ensure();
    if (err != ESP_OK) return err;

    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) return ESP_ERR_NO_MEM;

    wifi_config_t config = { 0 };
    text_copy((char *)config.ap.ssid, sizeof(config.ap.ssid), s_ssid);
    text_copy((char *)config.ap.password, sizeof(config.ap.password),
              PORTAL_PASSWORD);
    config.ap.ssid_len = strlen(s_ssid);
    config.ap.channel = 6;
    config.ap.max_connection = 2;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK) goto fail;
    // 协议栈可能已由别的模式 start 过,那种情况下 ESP_ERR_WIFI_STATE 不是错误。
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) goto fail;
    s_wifi_started = true;
    we_wifi_set_ap_active(true);

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.stack_size = 6144;
    server_config.max_uri_handlers = 4;
    err = httpd_start(&s_server, &server_config);
    if (err != ESP_OK) goto fail;
    err = register_handlers();
    if (err != ESP_OK) goto fail;

    ESP_LOGI(TAG, "配置热点已启动: %s / http://192.168.4.1", s_ssid);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "配置热点启动失败: %s", esp_err_to_name(err));
    we_portal_stop();
    return err;
}

void we_portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }
    we_wifi_set_ap_active(false);
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
}

bool we_portal_running(void) { return s_wifi_started && s_server; }
const char *we_portal_ssid(void) { return s_ssid; }
const char *we_portal_password(void) { return PORTAL_PASSWORD; }

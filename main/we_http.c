#include "we_http.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "we_http";

// 响应用调用方缓冲累积。余额响应只有几百字节,2 KB 足够;
// 超出部分直接丢弃并报错,不为异常响应撑大堆占用。
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
    bool   overflow;
} response_t;

static esp_err_t on_event(esp_http_client_event_t *evt)
{
    response_t *resp = (response_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !resp) return ESP_OK;

    if (resp->len + evt->data_len >= resp->cap) {
        resp->overflow = true;
        return ESP_OK;
    }
    memcpy(resp->buf + resp->len, evt->data, evt->data_len);
    resp->len += evt->data_len;
    resp->buf[resp->len] = '\0';
    return ESP_OK;
}

int we_http_get(const char *url, const char *bearer_token, char *out,
                size_t out_len)
{
    if (!url || !out || out_len == 0) return -1;
    out[0] = '\0';

    response_t resp = { .buf = out, .cap = out_len - 1 };
    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", bearer_token ? bearer_token : "");

    const esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 12000,
        .event_handler = on_event,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -2;

    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Accept", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "请求失败: %s", esp_err_to_name(err));
        out[0] = '\0';
        return -3;
    }
    if (resp.overflow) {
        ESP_LOGW(TAG, "响应超过 %u 字节,已丢弃", (unsigned)out_len);
        out[0] = '\0';
        return -4;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP %d", status);
        out[0] = '\0';
        return -status;
    }
    return 0;
}

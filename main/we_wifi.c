// main/we_wifi.c —— Wi-Fi 协议栈的单点初始化。
#include "we_wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <stddef.h>

static const char *TAG = "we_wifi";

static bool s_ready;
static bool s_ap_active;

esp_err_t we_wifi_ensure(void)
{
    if (s_ready) return ESP_OK;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init 失败: %s", esp_err_to_name(err));
        return err;
    }

    // 默认事件循环在别的组件里可能已经建过,那不算错误。
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "事件循环创建失败: %s", esp_err_to_name(err));
        return err;
    }

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init 失败: %s", esp_err_to_name(err));
        return err;
    }

    // 只存 RAM:凭证由本固件自己的 NVS 命名空间管理,不用 Wi-Fi 驱动的存储。
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置 Wi-Fi 存储模式失败: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    return ESP_OK;
}

bool we_wifi_ready(void) { return s_ready; }

void we_wifi_set_ap_active(bool active) { s_ap_active = active; }
bool we_wifi_ap_active(void) { return s_ap_active; }

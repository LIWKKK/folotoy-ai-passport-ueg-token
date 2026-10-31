// main/we_storage.c —— 卡片状态的 NVS blob 持久化;版本不匹配时恢复默认值。
#include "we_storage.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "we_storage";

#define WE_STORE_MAGIC 0x57454B4EUL  // "WEKN"
#define WE_STORE_VERSION 1
#define WE_NAMESPACE "we_token"
#define WE_STATE_KEY "state"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    we_state_t state;
} stored_state_t;

esp_err_t we_storage_init(void)
{
    // 不自动擦除整个 NVS:里面可能还有设备身份等其他应用的数据。
    return nvs_flash_init();
}

esp_err_t we_storage_load(we_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        we_state_defaults(state);
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    stored_state_t *stored = malloc(sizeof(*stored));
    if (!stored) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    size_t size = sizeof(*stored);
    err = nvs_get_blob(handle, WE_STATE_KEY, stored, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND || size != sizeof(*stored) ||
        stored->magic != WE_STORE_MAGIC ||
        stored->version != WE_STORE_VERSION ||
        stored->payload_size != sizeof(we_state_t)) {
        free(stored);
        // 布局变了就整块重来:NVS 里存的是用户可重填的配置,不是不可再生的数据。
        ESP_LOGW(TAG, "未找到兼容数据,使用默认配置");
        we_state_defaults(state);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        free(stored);
        return err;
    }

    *state = stored->state;
    free(stored);
    we_state_sanitize(state);
    return ESP_OK;
}

esp_err_t we_storage_save(const we_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;

    stored_state_t *stored = malloc(sizeof(*stored));
    if (!stored) return ESP_ERR_NO_MEM;
    stored->magic = WE_STORE_MAGIC;
    stored->version = WE_STORE_VERSION;
    stored->payload_size = sizeof(we_state_t);
    stored->state = *state;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        free(stored);
        return err;
    }
    err = nvs_set_blob(handle, WE_STATE_KEY, stored, sizeof(*stored));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    free(stored);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "保存失败: %s", esp_err_to_name(err));
    return err;
}

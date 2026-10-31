// main/we_storage.h —— 卡片状态的 NVS 持久化。
#pragma once

#include "esp_err.h"
#include "we_logic.h"

esp_err_t we_storage_init(void);
esp_err_t we_storage_load(we_state_t *state);
esp_err_t we_storage_save(const we_state_t *state);

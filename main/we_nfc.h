// main/we_nfc.h —— 板载独立被动 NFC 的能力边界。
// 该标签不与 ESP32 数据总线相连，固件不能读取、写入或感知手机触碰。
#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t we_nfc_init(void);
bool we_nfc_firmware_accessible(void);
const char *we_nfc_state_text(void);

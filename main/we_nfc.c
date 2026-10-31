// main/we_nfc.c —— 独立被动标签不暴露任何固件控制接口。
#include "we_nfc.h"

esp_err_t we_nfc_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool we_nfc_firmware_accessible(void)
{
    return false;
}

const char *we_nfc_state_text(void)
{
    return "被动标签 / 固件不可修改";
}

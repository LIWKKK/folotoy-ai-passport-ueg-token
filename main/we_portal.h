// main/we_portal.h —— 本地 Wi-Fi 热点与配置页面。
// 完全离线:手机连上设备热点后填 Wi-Fi 凭证、平台 Key 与身份资料,
// 提交即写入 NVS。证书与密钥不经过任何第三方服务器。
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "we_logic.h"
#include <stdbool.h>

// 保存成功后回调(由 httpd 任务上下文调用,不得阻塞)。
typedef void (*we_portal_changed_cb_t)(void *user);

esp_err_t we_portal_init(we_state_t *state, SemaphoreHandle_t state_lock,
                         we_portal_changed_cb_t changed_cb, void *user);
esp_err_t we_portal_start(void);
void we_portal_stop(void);
bool we_portal_running(void);
const char *we_portal_ssid(void);
const char *we_portal_password(void);

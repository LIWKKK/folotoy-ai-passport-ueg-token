// main/we_net.h —— 余额刷新与 STA 联网。
// 设计要点:
//   * 设备平时不联网,只有主动刷新(配网页保存后 / 连接页按 OK)时才连 STA。
//   * 所有 HTTP 都在独立工作任务里跑,按键回调只置标志并通知任务,绝不阻塞。
//   * "余额差值"记账全部走 we_logic.c 的纯函数,这里只负责取数与落盘。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "we_logic.h"

// 启动网络模块:登记共享状态与锁,建工作任务(不立即连接)。可重复调用。
esp_err_t we_net_init(we_state_t *state, SemaphoreHandle_t state_lock);

// 请求一次刷新(异步)。
void we_net_request_refresh(void);

// 是否正在刷新(UI 显示加载状态用)。
bool we_net_is_refreshing(void);

// 当前 STA 是否已连接。
bool we_net_is_connected(void);

// 连接已保存的 Wi-Fi 并(可选)对时,阻塞至多 timeout_ms。
esp_err_t we_net_connect_blocking(uint32_t timeout_ms);

// 刷新完成回调,由工作任务上下文调用。回调内不得阻塞。
// 注册方(主程序)负责加 LVGL 锁并刷新可见页面。
void we_net_set_refresh_done_cb(void (*cb)(void));

// 数据版本号:每次刷新完成后自增。页面用它判断"数据换了,需要重画",
// 比每秒无条件重画整张热力图便宜得多。
uint32_t we_net_data_version(void);

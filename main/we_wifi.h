// main/we_wifi.h —— Wi-Fi 协议栈的唯一拥有者。
//
// 卡片上有两个模块要用 Wi-Fi:配网页(AP)和余额刷新(STA)。
// esp_netif_init / 事件循环 / esp_wifi_init 对同一进程只能成功一次,
// 所以集中在这里做,其他模块一律先调 we_wifi_ensure() 再碰 esp_wifi。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// 建好 netif、默认事件循环与 esp_wifi(幂等)。返回 ESP_OK 表示已就绪。
esp_err_t we_wifi_ensure(void);

// 协议栈是否已就绪。
bool we_wifi_ready(void);

// 配网热点是否占用着 Wi-Fi(占用时 STA 刷新应跳过,避免模式互相打断)。
void we_wifi_set_ap_active(bool active);
bool we_wifi_ap_active(void);

// main/we_config.h —— 应用级常量。身份资料、Wi-Fi 与平台 Key 通过配网页修改并保存到 NVS。
#pragma once

#define WE_APP_NAME "UEG 数字生命卡"
#define WE_FW_VERSION "WE-TOKEN v1.0.0"
#define WE_TIMEZONE "CST-8"
#define WE_PORTAL_URL "http://192.168.4.1"

// 余额低于该值时状态灯转为告警橙(单位:分)。
#define WE_DEFAULT_THRESHOLD_CENTS 500

// USD 平台折算成人民币分时的固定汇率(1 USD = 7.10 CNY)。
// 固定值只求可复现,不追求实时准确;单平台场景下结果就是原值。
#define WE_USD_CNY_X100 710

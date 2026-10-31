// main/we_time.h —— 时间与日期。
// 设备平时不联网,RTC 靠内部振荡器走时,漂移可接受;
// 一旦联网就顺手 SNTP 对时。热力图按"日序号"归档,与本地时区一致。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// 初始化时区(不等同于对时)。
void we_time_init(void);

// 当前 unix 时间;未对时也返回 RTC 值。
int64_t we_time_now(void);

// RTC 是否已可信(已通过 SNTP 或手机校时设置过)。
bool we_time_is_valid(void);

// 通过 SNTP 同步,阻塞至多 timeout_ms。成功返回 true。
bool we_time_sync(uint32_t timeout_ms);

// 用手机传来的 epoch 校时(配网页表单)。
void we_time_set_epoch(int64_t epoch);

// 本地日期 → 1970-01-01 起算的日序号(热力图与按天归档用)。
int32_t we_time_today_index(void);

// 指定 unix 时间对应的日序号。
int32_t we_time_day_index(int64_t unix_now);

// "HH:MM" / "HH:MM:SS" 文本。未对时时写 "--:--"。
void we_time_format_hm(char *out, size_t out_len);
void we_time_format_hms(char *out, size_t out_len);

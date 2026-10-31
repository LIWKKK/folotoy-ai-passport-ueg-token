// main/we_f1.h —— F1 信息页的纯逻辑层:赛历 / 积分榜 / RSS 解析与北京时间换算。
// 不依赖 ESP-IDF / LVGL,可直接在主机编译测试(见 tests/test_we_f1.c)。
//
// 数据源(参考社区玩法 F1 PIT WALL,play 135):
//   * 赛历/积分榜: https://api.jolpi.ca/ergast/f1/current*.json (Ergast 兼容)
//   * 新闻 RSS   : BBC F1 / formula1.com(取标题即可)
// 各节时间在上游是 UTC,本模块统一换算成北京时间(UTC+8)再交给 UI。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WE_F1_NAME_BYTES     40
#define WE_F1_CIRCUIT_BYTES  40
#define WE_F1_COUNTRY_BYTES  24
#define WE_F1_LABEL_BYTES    16
#define WE_F1_SESSION_MAX    6
#define WE_F1_STANDING_MAX   5
#define WE_F1_TEAM_BYTES     28
#define WE_F1_DRIVER_BYTES   24
#define WE_F1_NEWS_MAX       3
#define WE_F1_HEADLINE_BYTES 72

// 北京时间 = UTC+8。固定偏移,不跟随时区库(本机时区就是 CST-8)。
#define WE_F1_BEIJING_OFFSET_SECS (8 * 3600)

// 一节(练习/排位/冲刺/正赛)。时间已折算为北京时间的日序号 + 当日秒数。
typedef struct {
    char label[WE_F1_LABEL_BYTES];
    int32_t day_index;
    int32_t secs;
    bool present;
} we_f1_session_t;

typedef struct {
    char name[WE_F1_NAME_BYTES];
    char circuit[WE_F1_CIRCUIT_BYTES];
    char country[WE_F1_COUNTRY_BYTES];
    int round;
    int32_t day_index;                      // 正赛日(北京)
    we_f1_session_t sessions[WE_F1_SESSION_MAX];
    int session_count;
    bool valid;
} we_f1_race_t;

typedef struct {
    int pos;
    char name[WE_F1_DRIVER_BYTES];
    char team[WE_F1_TEAM_BYTES];
    int points;
} we_f1_driver_t;

typedef struct {
    int pos;
    char name[WE_F1_TEAM_BYTES];
    int points;
} we_f1_team_t;

// 从赛历 JSON 里挑出今天(含)之后最近的一站,并解析各节北京时间。
// 解析失败或没有后续站点时返回 false。
bool we_f1_parse_next_race(const char *json, int32_t today_index,
                           we_f1_race_t *out);

// 解析积分榜前 max 名,返回实际条数。
int we_f1_parse_drivers(const char *json, we_f1_driver_t *out, int max);
int we_f1_parse_teams(const char *json, we_f1_team_t *out, int max);

// 从 RSS 里取前 max 条标题,返回实际条数。
int we_f1_parse_news(const char *xml, char out[][WE_F1_HEADLINE_BYTES], int max);

// 日序号 + 当秒 → "MM-DD HH:MM"。
void we_f1_format_dt(int32_t day_index, int32_t secs, char *out, size_t out_len);

// 倒计时:"3天 04:12" / "04:12:33" / "已开始"。
void we_f1_countdown(int64_t now_unix, int64_t race_unix, char *out,
                     size_t out_len);

// 北京时间某一刻的 unix 时间戳(用于倒计时比较)。
int64_t we_f1_beijing_unix(int32_t day_index, int32_t secs);

// main/we_logic.h —— 卡片的纯逻辑层:JSON 取值、余额解析、花费→Token 估算、
// 热力图分档与网格索引、日期换算、文本格式化。
// 不依赖 ESP-IDF / LVGL,可直接在主机编译测试(见 tests/test_we_logic.c)。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WE_NAME_BYTES     40
#define WE_ID_BYTES       24
#define WE_ROLE_BYTES     40
#define WE_KEY_BYTES      96
#define WE_SSID_BYTES     33
#define WE_PASS_BYTES     65
#define WE_CURRENCY_BYTES 8
#define WE_ERROR_BYTES    24

// 平台表:目前两家,留出第三格便于扩展(页面按已配置数量排布)。
#define WE_MAX_PLATFORMS 3

// 热力图:7 行(周日..周六) × 5 列(由旧到新的 5 周),只使用最近 30 天。
#define WE_HEAT_ROWS 7
#define WE_HEAT_COLS 5
#define WE_HEAT_DAYS 30

typedef enum {
    WE_PLAT_DEEPSEEK = 0,
    WE_PLAT_KIMI,
    WE_PLAT_KIND_COUNT,
} we_plat_kind_t;

typedef enum {
    WE_NET_IDLE = 0,
    WE_NET_LOADING,
    WE_NET_OK,
    WE_NET_ERROR,
} we_net_status_t;

typedef struct {
    char name[WE_NAME_BYTES];
    char identity[WE_ID_BYTES];
    char role[WE_ROLE_BYTES];
    bool configured;
} we_profile_t;

typedef struct {
    bool has_key;
    char name[WE_NAME_BYTES];
    char key[WE_KEY_BYTES];
    char currency[WE_CURRENCY_BYTES];
    int32_t balance_cents;       // -1 = 从未成功查询
    int32_t today_spend_cents;
    int64_t last_ok_unix;        // 0 = 从未成功
    we_net_status_t status;
    char error[WE_ERROR_BYTES];  // 失败短语(英文短词,直接上屏)
} we_platform_t;

// 某一天的用量快照(热力图数据源)。
typedef struct {
    int32_t day_index;      // 1970-01-01 起算的日序号
    int32_t spend_cents;    // 当日累计花费(已折算为人民币分)
    int64_t tokens;         // 当日累计估算 Token
} we_day_record_t;

typedef struct {
    we_profile_t profile;
    we_platform_t platforms[WE_PLAT_KIND_COUNT];

    // 每个平台"最近一次成功查询"的余额与时间,用于做差估算花费。
    int32_t cursor_cents[WE_PLAT_KIND_COUNT];
    int64_t cursor_unix[WE_PLAT_KIND_COUNT];

    // 按天环形缓冲。
    we_day_record_t days[WE_HEAT_DAYS];
    uint8_t days_head;
    uint8_t days_count;

    char wifi_ssid[WE_SSID_BYTES];
    char wifi_pass[WE_PASS_BYTES];
    bool has_wifi;

    int32_t threshold_cents;
} we_state_t;

void we_state_defaults(we_state_t *state);
void we_state_sanitize(we_state_t *state);

// 已配置 Key 的平台数量。
int we_enabled_platforms(const we_state_t *state);

const char *we_plat_name(we_plat_kind_t kind);
const char *we_plat_currency(we_plat_kind_t kind);

// ---------------------------------------------------------------------------
// 极简 JSON 取值(不引入 cJSON,省 Flash/RAM)
// ---------------------------------------------------------------------------
// 从 json 里找 "key" 并解析其后的数值(支持 12.34 与 "12.34" 两种写法)。
// search_from 为 NULL 时从头找;否则从该位置开始(用于限定在某个数组/对象内)。
// 返回找到的位置+1(可继续向后搜),未找到返回 NULL。
const char *we_json_number_at(const char *json, const char *search_from,
                              const char *key, double *out);

// 取一个字符串值(不展开转义,够用即止)。
bool we_json_string(const char *json, const char *key, char *out, size_t out_len);

// ---------------------------------------------------------------------------
// 余额解析:成功返回 true 并给出余额(分)与币种。
//   DeepSeek: {"is_available":true,"balance_infos":[{"currency":"CNY",
//              "total_balance":"23.50",...}]}
//   Kimi    : {"code":0,"data":{"available_balance":49.58,...}}  单位 USD
// ---------------------------------------------------------------------------
bool we_parse_balance(const char *body, we_plat_kind_t kind,
                      int32_t *cents_out, char *currency, size_t currency_len);

// 单价表(估算用,单位:分/百万 token)。数值来自公开定价的粗略加权,
// 只用于把"花费"换算成"Token 估算值",不是精确账单口径。
uint32_t we_price_cents_per_mtok(we_plat_kind_t kind);

// 花费(分)→ 估算 Token 数。
int64_t we_estimate_tokens(int32_t spend_cents, uint32_t price_cents_per_mtok);

// ---------------------------------------------------------------------------
// 花费记账:把某平台的新余额并入当日快照。
// 返回本次新增花费(分);首次查询(无游标)或余额回升(充值)时返回 0 且只更新游标。
// ---------------------------------------------------------------------------
int32_t we_account_balance(we_state_t *state, we_plat_kind_t kind,
                           int32_t new_cents, int32_t day_index, int64_t unix_now);

// 取某天的记录,没有则返回 NULL。
const we_day_record_t *we_day_get(const we_state_t *state, int32_t day_index);

// 最近 window_days 天(含今天)里有多少天有记录。
int we_days_with_usage(const we_state_t *state, int32_t today_index, int window_days);

// 最近 window_days 天的合计。tokens 用于热力图汇总行。
int64_t we_window_tokens(const we_state_t *state, int32_t today_index, int window_days);
int32_t we_window_spend(const we_state_t *state, int32_t today_index, int window_days);

// ---------------------------------------------------------------------------
// 热力图
// ---------------------------------------------------------------------------
// 某一天的档位:0 = 无记录,1..4 = 由低到高。
int we_heat_level(int64_t tokens);

// 把某个绝对日序号放进网格(列 = 周,行 = 星期,与日历对齐)。
// 返回 false 表示该日不在本网格能表达的范围内(早于最旧一列,或晚于今天)。
// 注意:7 × 5 只能表达 5 个完整周(35 天),最早几天可能落在第 6 周而不可选 ——
// 这是网格尺寸的固有限制,由调用方跳过。
bool we_heat_cell(int32_t day_index, int32_t today_index, int *row, int *col);

// 星期几,0 = 周日。1970-01-01 是周四,故基准偏移为 4。
int we_day_weekday(int32_t day_index);

// 公历日期 → 1970-01-01 起算的日序号(Howard Hinnant days_from_civil 算法)。
// 纯算术,不依赖 time.h,便于宿主测试;闰年规则已包含。
int32_t we_days_from_civil(int year, int month, int day);

// 反向:日序号 → 公历日期。
void we_civil_from_days(int32_t day_index, int *year, int *month, int *day);

// "MM-DD" 短日期,用于热力图下方的日期轴。
void we_format_date(int32_t day_index, char *out, size_t out_len);

// ---------------------------------------------------------------------------
// 文本格式化
// ---------------------------------------------------------------------------
// 数量:"830" / "12.4K" / "423K" / "1.2M"。
void we_format_count(int64_t value, char *out, size_t out_len);

// 金额(分):"23.50" / "0.87"。
void we_format_money(int32_t cents, char *out, size_t out_len);

// 余额 → 状态灯:0 正常(绿),1 偏低(橙),2 耗尽/未知(红)。
int we_balance_level(int32_t cents, int32_t threshold_cents);

// 把某平台的余额折算成人民币分,用于"总余额"这一项。
int32_t we_balance_to_cny_cents(we_plat_kind_t kind, int32_t cents);

#ifdef __cplusplus
}
#endif

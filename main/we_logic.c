// main/we_logic.c —— 卡片纯逻辑。可在主机上编译测试,不含 ESP-IDF / LVGL 依赖。
#include "we_logic.h"

#include "we_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_text(char *dst, size_t len, const char *src)
{
    if (!dst || len == 0) return;
    src = src ? src : "";
    size_t out = 0;
    while (*src && out + 1 < len) {
        unsigned char first = (unsigned char)*src;
        size_t bytes = first < 0x80 ? 1 :
                       (first & 0xE0) == 0xC0 ? 2 :
                       (first & 0xF0) == 0xE0 ? 3 :
                       (first & 0xF8) == 0xF0 ? 4 : 1;
        if (out + bytes >= len) break;
        bool valid = true;
        for (size_t i = 1; i < bytes; i++) {
            if (!src[i] || ((unsigned char)src[i] & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            src++;
            continue;
        }
        memcpy(dst + out, src, bytes);
        out += bytes;
        src += bytes;
    }
    dst[out] = '\0';
}

// ---------------------------------------------------------------------------
// 默认值与清洗
// ---------------------------------------------------------------------------
static void platform_default(we_platform_t *plat, we_plat_kind_t kind)
{
    memset(plat, 0, sizeof(*plat));
    copy_text(plat->name, sizeof(plat->name), we_plat_name(kind));
    copy_text(plat->currency, sizeof(plat->currency), we_plat_currency(kind));
    plat->balance_cents = -1;
    plat->status = WE_NET_IDLE;
}

void we_state_defaults(we_state_t *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    copy_text(state->profile.name, sizeof(state->profile.name), "未命名用户");
    copy_text(state->profile.identity, sizeof(state->profile.identity), "UEG-0000");
    copy_text(state->profile.role, sizeof(state->profile.role), "联合政府公民");
    for (int i = 0; i < WE_PLAT_KIND_COUNT; i++)
        platform_default(&state->platforms[i], (we_plat_kind_t)i);
    for (int i = 0; i < WE_PLAT_KIND_COUNT; i++)
        state->cursor_cents[i] = -1;
    state->threshold_cents = WE_DEFAULT_THRESHOLD_CENTS;
}

void we_state_sanitize(we_state_t *state)
{
    if (!state) return;
    state->profile.name[WE_NAME_BYTES - 1] = '\0';
    state->profile.identity[WE_ID_BYTES - 1] = '\0';
    state->profile.role[WE_ROLE_BYTES - 1] = '\0';
    state->wifi_ssid[WE_SSID_BYTES - 1] = '\0';
    state->wifi_pass[WE_PASS_BYTES - 1] = '\0';
    if (state->threshold_cents < 0) state->threshold_cents = 0;

    for (int i = 0; i < WE_PLAT_KIND_COUNT; i++) {
        we_platform_t *plat = &state->platforms[i];
        plat->key[WE_KEY_BYTES - 1] = '\0';
        plat->name[WE_NAME_BYTES - 1] = '\0';
        plat->currency[WE_CURRENCY_BYTES - 1] = '\0';
        plat->error[WE_ERROR_BYTES - 1] = '\0';
        if (plat->name[0] == '\0')
            copy_text(plat->name, sizeof(plat->name), we_plat_name((we_plat_kind_t)i));
        if (plat->currency[0] == '\0')
            copy_text(plat->currency, sizeof(plat->currency),
                      we_plat_currency((we_plat_kind_t)i));
        if (plat->balance_cents < -1) plat->balance_cents = -1;
        if (plat->today_spend_cents < 0) plat->today_spend_cents = 0;
        if (plat->status > WE_NET_ERROR) plat->status = WE_NET_IDLE;
        // Key 文本是唯一事实来源:有 Key 才算已配置,避免两个字段各说各话。
        plat->has_key = (plat->key[0] != '\0');
    }
    if (state->days_head >= WE_HEAT_DAYS) state->days_head = 0;
    if (state->days_count > WE_HEAT_DAYS) state->days_count = WE_HEAT_DAYS;
}

int we_enabled_platforms(const we_state_t *state)
{
    if (!state) return 0;
    int count = 0;
    for (int i = 0; i < WE_PLAT_KIND_COUNT; i++)
        if (state->platforms[i].has_key) count++;
    return count;
}

const char *we_plat_name(we_plat_kind_t kind)
{
    switch (kind) {
    case WE_PLAT_DEEPSEEK: return "DeepSeek";
    case WE_PLAT_KIMI:     return "Kimi";
    default:               return "API";
    }
}

const char *we_plat_currency(we_plat_kind_t kind)
{
    switch (kind) {
    case WE_PLAT_DEEPSEEK: return "CNY";
    case WE_PLAT_KIMI:     return "USD";
    default:               return "CNY";
    }
}

// ---------------------------------------------------------------------------
// 极简 JSON 取值
// ---------------------------------------------------------------------------
static const char *find_key(const char *from, const char *key)
{
    if (!from || !key) return NULL;

    // 只匹配 "key",避免命中 "key_suffix" 之类的子串。
    size_t key_len = strlen(key);
    for (const char *p = from; (p = strchr(p, '"')) != NULL; p++) {
        if (strncmp(p + 1, key, key_len) != 0) continue;
        if (p[1 + key_len] != '"') continue;
        return p + 1 + key_len + 1;
    }
    return NULL;
}

const char *we_json_number_at(const char *json, const char *search_from,
                              const char *key, double *out)
{
    if (!json || !key || !out) return NULL;

    const char *p = find_key(search_from ? search_from : json, key);
    if (!p) return NULL;

    while (*p && *p != ':') {
        // 允许 "key" 与 ':' 之间有空白;遇到其它 JSON 结构则放弃。
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') return NULL;
        p++;
    }
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    const bool quoted = (*p == '"');
    if (quoted) p++;

    char buffer[32];
    size_t n = 0;
    while (*p && n + 1 < sizeof(buffer)) {
        char c = *p;
        if (quoted ? (c == '"') : !(isdigit((unsigned char)c) || c == '.' ||
                                    c == '-' || c == '+' || c == 'e' || c == 'E')) {
            break;
        }
        buffer[n++] = c;
        p++;
    }
    buffer[n] = '\0';
    if (n == 0) return NULL;

    char *end = NULL;
    double value = strtod(buffer, &end);
    if (end == buffer) return NULL;

    *out = value;
    return p;
}

bool we_json_string(const char *json, const char *key, char *out, size_t out_len)
{
    if (!json || !key || !out || out_len == 0) return false;

    const char *p = find_key(json, key);
    if (!p) return false;
    while (*p && *p != ':') {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') return false;
        p++;
    }
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return false;
    p++;

    size_t n = 0;
    while (*p && *p != '"' && n + 1 < out_len) {
        if (*p == '\\' && p[1]) p++;   // 不展开转义,原样保留下一个字符
        out[n++] = *p++;
    }
    out[n] = '\0';
    return true;
}

// ---------------------------------------------------------------------------
// 余额解析
// ---------------------------------------------------------------------------
// 把十进制字面量直接换算成"分",全程整数运算。
// 不走 double:1.005 在 IEEE 双精度里是 1.00499999...,
// 先乘 100 再四舍五入会把 1.01 算成 1.00 —— 金额不能有这种误差。
// 小数超过两位时按第三位四舍五入。
static bool literal_to_cents(const char *text, size_t len, int32_t *out)
{
    if (!text || len == 0 || !out) return false;

    size_t i = 0;
    bool negative = false;
    if (text[i] == '-') { negative = true; i++; }
    else if (text[i] == '+') { i++; }

    int64_t whole = 0;
    bool seen_digit = false;
    while (i < len && isdigit((unsigned char)text[i])) {
        whole = whole * 10 + (text[i] - '0');
        seen_digit = true;
        i++;
    }

    int64_t frac = 0;
    int frac_digits = 0;
    int round_digit = 0;
    if (i < len && text[i] == '.') {
        i++;
        while (i < len && isdigit((unsigned char)text[i])) {
            if (frac_digits < 2) {
                frac = frac * 10 + (text[i] - '0');
                frac_digits++;
            } else if (frac_digits == 2) {
                round_digit = text[i] - '0';
                frac_digits++;
            } else {
                break;
            }
            seen_digit = true;
            i++;
        }
    }

    // 指数写法(1e3)不属于货币字面量,拒绝比猜错好。
    if (i < len && (text[i] == 'e' || text[i] == 'E')) return false;
    if (!seen_digit) return false;

    while (frac_digits < 2) {
        frac *= 10;
        frac_digits++;
    }

    int64_t cents = whole * 100 + frac;
    if (round_digit >= 5) cents += 1;
    if (negative) cents = -cents;
    if (cents > INT32_MAX || cents < INT32_MIN) return false;

    *out = (int32_t)cents;
    return true;
}

// 取出 "key": 后面的数值字面量(去掉包裹的引号),拷进 buf。
// 返回字面量之后的续搜位置;未找到返回 NULL。
static const char *find_number_literal(const char *json, const char *search_from,
                                       const char *key, char *buf, size_t buf_len)
{
    if (!json || !key || !buf || buf_len == 0) return NULL;

    const char *p = find_key(search_from ? search_from : json, key);
    if (!p) return NULL;

    while (*p && *p != ':') {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') return NULL;
        p++;
    }
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    const bool quoted = (*p == '"');
    if (quoted) p++;

    size_t n = 0;
    while (*p && n + 1 < buf_len) {
        char c = *p;
        if (quoted ? (c == '"') : !(isdigit((unsigned char)c) || c == '.' ||
                                     c == '-' || c == '+' || c == 'e' || c == 'E')) {
            break;
        }
        buf[n++] = c;
        p++;
    }
    buf[n] = '\0';
    if (n == 0) return NULL;
    return p;
}

bool we_parse_balance(const char *body, we_plat_kind_t kind,
                      int32_t *cents_out, char *currency, size_t currency_len)
{
    if (!body || !cents_out || !currency || currency_len == 0) return false;

    char literal[48];
    const char *anchor = body;

    switch (kind) {
    case WE_PLAT_DEEPSEEK: {
        // 只看 balance_infos 数组,并取数组内第一条记录的币种与总额。
        anchor = find_key(body, "balance_infos");
        if (!anchor) return false;
        if (!find_number_literal(body, anchor, "total_balance",
                                 literal, sizeof(literal))) {
            return false;
        }
        if (!we_json_string(anchor, "currency", currency, currency_len)) {
            snprintf(currency, currency_len, "CNY");
        }
        break;
    }
    case WE_PLAT_KIMI: {
        anchor = find_key(body, "data");
        if (!find_number_literal(anchor ? anchor : body, NULL,
                                 "available_balance", literal, sizeof(literal))) {
            return false;
        }
        snprintf(currency, currency_len, "USD");
        break;
    }
    default:
        return false;
    }

    return literal_to_cents(literal, strlen(literal), cents_out);
}

uint32_t we_price_cents_per_mtok(we_plat_kind_t kind)
{
    switch (kind) {
    // DeepSeek 以 CNY 计价:输入/输出加权后约 ¥4 / 百万 token。
    case WE_PLAT_DEEPSEEK: return 400;
    // Kimi 以 USD 计价:加权后约 $1.2 / 百万 token(按 7.1 汇率折算 ≈ ¥8.5)。
    case WE_PLAT_KIMI:     return 850;
    default:               return 400;
    }
}

int64_t we_estimate_tokens(int32_t spend_cents, uint32_t price_cents_per_mtok)
{
    if (spend_cents <= 0 || price_cents_per_mtok == 0) return 0;
    return ((int64_t)spend_cents * 1000000LL) / (int64_t)price_cents_per_mtok;
}

// ---------------------------------------------------------------------------
// 花费记账
// ---------------------------------------------------------------------------
static we_day_record_t *day_slot(we_state_t *state, int32_t day_index)
{
    for (uint8_t i = 0; i < state->days_count; i++) {
        int idx = state->days_head - 1 - i;
        while (idx < 0) idx += WE_HEAT_DAYS;
        if (state->days[idx].day_index == day_index) return &state->days[idx];
    }
    return NULL;
}

int32_t we_account_balance(we_state_t *state, we_plat_kind_t kind,
                           int32_t new_cents, int32_t day_index, int64_t unix_now)
{
    if (!state || kind < 0 || kind >= WE_PLAT_KIND_COUNT) return 0;

    int32_t delta = 0;
    int32_t previous = state->cursor_cents[kind];
    // 只在两次快照之间余额下降时记花费:首次查询没有基线,
    // 充值会让余额回升,两者都跳过,避免把充值算成负花费。
    if (previous >= 0 && new_cents >= 0 && new_cents < previous)
        delta = previous - new_cents;

    state->cursor_cents[kind] = new_cents;
    state->cursor_unix[kind] = unix_now;
    if (delta <= 0) return 0;

    we_day_record_t *record = day_slot(state, day_index);
    if (!record) {
        record = &state->days[state->days_head];
        memset(record, 0, sizeof(*record));
        record->day_index = day_index;
        state->days_head = (uint8_t)((state->days_head + 1) % WE_HEAT_DAYS);
        if (state->days_count < WE_HEAT_DAYS) state->days_count++;
    }
    record->spend_cents += delta;
    record->tokens += we_estimate_tokens(delta, we_price_cents_per_mtok(kind));
    return delta;
}

const we_day_record_t *we_day_get(const we_state_t *state, int32_t day_index)
{
    if (!state) return NULL;
    for (uint8_t i = 0; i < state->days_count; i++) {
        int idx = state->days_head - 1 - i;
        while (idx < 0) idx += WE_HEAT_DAYS;
        if (state->days[idx].day_index == day_index) return &state->days[idx];
    }
    return NULL;
}

int we_days_with_usage(const we_state_t *state, int32_t today_index, int window_days)
{
    int count = 0;
    for (int offset = 0; offset < window_days; offset++) {
        const we_day_record_t *record = we_day_get(state, today_index - offset);
        if (record && record->tokens > 0) count++;
    }
    return count;
}

static int64_t window_sum_tokens(const we_state_t *state, int32_t today_index,
                                 int window_days)
{
    int64_t total = 0;
    for (int offset = 0; offset < window_days; offset++) {
        const we_day_record_t *record = we_day_get(state, today_index - offset);
        if (record) total += record->tokens;
    }
    return total;
}

static int32_t window_sum_spend(const we_state_t *state, int32_t today_index,
                                int window_days)
{
    int64_t total = 0;
    for (int offset = 0; offset < window_days; offset++) {
        const we_day_record_t *record = we_day_get(state, today_index - offset);
        if (record) total += record->spend_cents;
    }
    return (int32_t)total;
}

int64_t we_window_tokens(const we_state_t *state, int32_t today_index, int window_days)
{
    return window_sum_tokens(state, today_index, window_days);
}

int32_t we_window_spend(const we_state_t *state, int32_t today_index, int window_days)
{
    return window_sum_spend(state, today_index, window_days);
}

// ---------------------------------------------------------------------------
// 热力图
// ---------------------------------------------------------------------------
int we_heat_level(int64_t tokens)
{
    if (tokens <= 0)      return 0;
    if (tokens < 2000)    return 1;
    if (tokens < 20000)   return 2;
    if (tokens < 100000)  return 3;
    return 4;
}

int we_day_weekday(int32_t day_index)
{
    // 1970-01-01 是星期四(=4)。负数日序号也保持非负余数。
    int32_t w = (day_index + 4) % 7;
    if (w < 0) w += 7;
    return (int)w;
}

int32_t we_days_from_civil(int year, int month, int day)
{
    // 把一月/二月挪到上一年,使闰日落在年末 —— 之后每月天数固定。
    int32_t y = (int32_t)year;
    int32_t m = (int32_t)month;
    y -= (m <= 2);
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = (uint32_t)(y - era * 400);                   // [0, 399]
    const uint32_t doy = (uint32_t)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5
                                    + day - 1);                       // [0, 365]
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;       // [0, 146096]
    return (int32_t)(era * 146097 + (int32_t)doe - 719468);
}

void we_civil_from_days(int32_t day_index, int *year, int *month, int *day)
{
    int32_t z = day_index + 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);                // [0, 146096]
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int32_t y = (int32_t)yoe + era * 400;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);     // [0, 365]
    const uint32_t mp = (5 * doy + 2) / 153;                          // [0, 11]
    const uint32_t d = doy - (153 * mp + 2) / 5 + 1;                  // [1, 31]
    const uint32_t m = mp + (mp < 10 ? 3 : -9);                       // [1, 12]

    if (year)  *year  = y + (m <= 2);
    if (month) *month = (int)m;
    if (day)   *day   = (int)d;
}

bool we_heat_cell(int32_t day_index, int32_t today_index, int *row, int *col)
{
    if (day_index > today_index) return false;
    if (today_index - day_index >= WE_HEAT_DAYS) return false;

    // 把两天各自对齐到所在周的周日,相差的整周数就是列偏移。
    const int32_t week_start_today = today_index - we_day_weekday(today_index);
    const int32_t week_start_day   = day_index   - we_day_weekday(day_index);
    const int32_t weeks_ago = (week_start_today - week_start_day) / 7;

    const int32_t c = WE_HEAT_COLS - 1 - weeks_ago;
    if (c < 0 || c >= WE_HEAT_COLS) return false;

    if (row) *row = we_day_weekday(day_index);
    if (col) *col = (int)c;
    return true;
}

void we_format_date(int32_t day_index, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    int year = 0, month = 0, day = 0;
    we_civil_from_days(day_index, &year, &month, &day);
    snprintf(out, out_len, "%02d-%02d", month, day);
}

// ---------------------------------------------------------------------------
// 文本格式化
// ---------------------------------------------------------------------------
void we_format_count(int64_t value, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    if (value < 0) value = 0;

    if (value < 1000) {
        snprintf(out, out_len, "%lld", (long long)value);
    } else if (value < 100000) {
        // 1K..99.9K 保留一位小数,便于看出日常用量的变化(如 12.4K)。
        long long whole = (long long)(value / 1000);
        long long frac = (long long)((value % 1000) / 100);
        snprintf(out, out_len, "%lld.%lldK", whole, frac);
    } else if (value < 1000000) {
        snprintf(out, out_len, "%lldK", (long long)(value / 1000));
    } else {
        long long whole = (long long)(value / 1000000);
        long long frac = (long long)((value % 1000000) / 100000);
        snprintf(out, out_len, "%lld.%lldM", whole, frac);
    }
}

void we_format_money(int32_t cents, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    bool negative = cents < 0;
    int32_t abs_cents = negative ? -cents : cents;
    snprintf(out, out_len, "%s%ld.%02ld", negative ? "-" : "",
             (long)(abs_cents / 100), (long)(abs_cents % 100));
}

int we_balance_level(int32_t cents, int32_t threshold_cents)
{
    if (cents <= 0) return 2;
    if (cents < threshold_cents) return 1;
    return 0;
}

int32_t we_balance_to_cny_cents(we_plat_kind_t kind, int32_t cents)
{
    if (cents <= 0) return cents;
    if (kind == WE_PLAT_KIMI) {
        return (int32_t)(((int64_t)cents * WE_USD_CNY_X100 + 50) / 100);
    }
    return cents;
}

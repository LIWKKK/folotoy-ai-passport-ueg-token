// tests/test_we_logic.c —— 卡片纯逻辑的宿主测试。
// 覆盖:JSON 取值、两家平台的余额解析、花费估算、热力图分档与网格索引、
// 日期换算、文本格式化。改协议或排版计算时先改这里。
#include <assert.h>
#include <string.h>

#include "we_logic.h"

// DeepSeek 真实响应形态(balance_infos 数组,金额是字符串)。
static const char *DS_BODY =
    "{\"is_available\":true,\"balance_infos\":[{\"currency\":\"CNY\","
    "\"total_balance\":\"23.50\",\"granted_balance\":\"0.00\","
    "\"topped_up_balance\":\"23.50\"}]}";

// Kimi 真实响应形态(data.available_balance 是数字)。
static const char *KIMI_BODY =
    "{\"code\":0,\"data\":{\"available_balance\":49.58,"
    "\"voucher_balance\":0,\"cash_balance\":49.58},\"scode\":\"0x0\"}";

static void test_json(void)
{
    double value = 0;
    assert(we_json_number_at("{\"a\":12.34}", NULL, "a", &value));
    assert(value > 12.33 && value < 12.35);

    // 带引号的数字也要能读。
    assert(we_json_number_at("{\"a\":\"7.5\"}", NULL, "a", &value));
    assert(value > 7.49 && value < 7.51);

    // 键名必须完整匹配,不能命中前缀相同的键。
    assert(we_json_number_at("{\"ab\":1}", NULL, "a", &value) == NULL);
    value = 0;
    assert(we_json_number_at("{\"a_b\":1}", NULL, "a", &value) == NULL);

    // 冒号前允许空白;缺值返回 NULL。
    assert(we_json_number_at("{ \"a\" : 3 }", NULL, "a", &value));
    assert(value > 2.99 && value < 3.01);
    assert(we_json_number_at("{\"a\":null}", NULL, "a", &value) == NULL);

    char text[32];
    assert(we_json_string("{\"currency\":\"CNY\"}", "currency", text, sizeof(text)));
    assert(strcmp(text, "CNY") == 0);
    assert(!we_json_string("{\"currency\":123}", "currency", text, sizeof(text)));
}

static void test_balance(void)
{
    int32_t cents = 0;
    char currency[WE_CURRENCY_BYTES];

    assert(we_parse_balance(DS_BODY, WE_PLAT_DEEPSEEK, &cents, currency,
                            sizeof(currency)));
    assert(cents == 2350);
    assert(strcmp(currency, "CNY") == 0);

    assert(we_parse_balance(KIMI_BODY, WE_PLAT_KIMI, &cents, currency,
                            sizeof(currency)));
    assert(cents == 4958);
    assert(strcmp(currency, "USD") == 0);

    // 平台与响应不匹配时不得误报成功。
    assert(!we_parse_balance(KIMI_BODY, WE_PLAT_DEEPSEEK, &cents, currency,
                             sizeof(currency)));
    assert(!we_parse_balance("{}", WE_PLAT_KIMI, &cents, currency,
                             sizeof(currency)));

    // 四舍五入到分。
    assert(we_parse_balance("{\"data\":{\"available_balance\":1.005}}",
                            WE_PLAT_KIMI, &cents, currency, sizeof(currency)));
    assert(cents == 101);
}

static void test_estimate(void)
{
    // ¥4 / 百万 token:¥1 花费 ≈ 250K token。
    assert(we_estimate_tokens(100, 400) == 250000);
    assert(we_estimate_tokens(0, 400) == 0);
    assert(we_estimate_tokens(-5, 400) == 0);
    assert(we_estimate_tokens(100, 0) == 0);
}

static void test_accounting(void)
{
    we_state_t state;
    we_state_defaults(&state);
    state.platforms[WE_PLAT_DEEPSEEK].has_key = true;

    // 首次查询只建游标,不记花费。
    assert(we_account_balance(&state, WE_PLAT_DEEPSEEK, 5000, 20000, 1000) == 0);
    assert(we_days_with_usage(&state, 20000, WE_HEAT_DAYS) == 0);

    // 余额下降 → 记差额为花费。
    assert(we_account_balance(&state, WE_PLAT_DEEPSEEK, 4600, 20000, 2000) == 400);
    const we_day_record_t *day = we_day_get(&state, 20000);
    assert(day && day->spend_cents == 400);
    // ¥4.00 / ¥4 每百万 ≈ 1M token。
    assert(day->tokens == 1000000);
    assert(we_days_with_usage(&state, 20000, WE_HEAT_DAYS) == 1);

    // 同一天继续下降 → 累加到同一条记录。
    assert(we_account_balance(&state, WE_PLAT_DEEPSEEK, 4500, 20000, 3000) == 100);
    day = we_day_get(&state, 20000);
    assert(day && day->spend_cents == 500);

    // 充值(余额回升)不计为负花费。
    assert(we_account_balance(&state, WE_PLAT_DEEPSEEK, 10000, 20000, 4000) == 0);
    day = we_day_get(&state, 20000);
    assert(day && day->spend_cents == 500);

    // 跨天写入新记录。
    assert(we_account_balance(&state, WE_PLAT_DEEPSEEK, 9000, 20001, 5000) == 1000);
    assert(we_day_get(&state, 20001) != NULL);
    assert(we_days_with_usage(&state, 20001, WE_HEAT_DAYS) == 2);
    assert(we_window_spend(&state, 20001, WE_HEAT_DAYS) == 1500);
    assert(we_window_tokens(&state, 20001, WE_HEAT_DAYS) > 0);

    // 窗口外的旧记录不计入。
    assert(we_days_with_usage(&state, 20001 + WE_HEAT_DAYS, WE_HEAT_DAYS) == 0);

    // 两个平台各自记账,互不干扰。
    we_state_t mixed;
    we_state_defaults(&mixed);
    we_account_balance(&mixed, WE_PLAT_DEEPSEEK, 1000, 20000, 0);
    we_account_balance(&mixed, WE_PLAT_KIMI, 5000, 20000, 0);
    we_account_balance(&mixed, WE_PLAT_DEEPSEEK, 900, 20000, 0);
    we_account_balance(&mixed, WE_PLAT_KIMI, 4000, 20000, 0);
    const we_day_record_t *both = we_day_get(&mixed, 20000);
    assert(both && both->spend_cents == 1100);
}

static void test_heat_level(void)
{
    assert(we_heat_level(0) == 0);
    assert(we_heat_level(-1) == 0);
    assert(we_heat_level(1) == 1);
    assert(we_heat_level(1999) == 1);
    assert(we_heat_level(2000) == 2);
    assert(we_heat_level(19999) == 2);
    assert(we_heat_level(20000) == 3);
    assert(we_heat_level(99999) == 3);
    assert(we_heat_level(100000) == 4);
    assert(we_heat_level(99999999) == 4);
}

static void test_calendar(void)
{
    // 1970-01-01 是星期四。
    assert(we_days_from_civil(1970, 1, 1) == 0);
    assert(we_day_weekday(0) == 4);

    // 闰年规则:2024-02-29 存在,2023 没有 2 月 29。
    int32_t leap = we_days_from_civil(2024, 2, 29);
    assert(leap == we_days_from_civil(2024, 2, 28) + 1);
    assert(we_days_from_civil(2024, 3, 1) == leap + 1);

    // 往返换算。
    for (int32_t day = 19000; day < 20000; day += 37) {
        int year = 0, month = 0, mday = 0;
        we_civil_from_days(day, &year, &month, &mday);
        assert(we_days_from_civil(year, month, mday) == day);
    }

    char text[16];
    we_format_date(we_days_from_civil(2026, 9, 13), text, sizeof(text));
    assert(strcmp(text, "09-13") == 0);
}

static void test_heat_grid(void)
{
    // 2026-09-13 是星期日。
    int32_t today = we_days_from_civil(2026, 9, 13);
    assert(we_day_weekday(today) == 0);

    int row = -1, col = -1;
    // 今天(周日)是本轮的第一天,落在最后一列、星期日那一行。
    assert(we_heat_cell(today, today, &row, &col));
    assert(row == 0 && col == WE_HEAT_COLS - 1);

    // 昨天(周六)属于上一周,所以列数减一、行是周六。
    assert(we_heat_cell(today - 1, today, &row, &col));
    assert(row == 6 && col == WE_HEAT_COLS - 2);

    // 同一周内更早的日子与今天同列,只是行号更小。
    // today 是周日,故本周只有今天一天 —— 没有别的同周日可测。

    // 5 列只能表达 5 个完整周,所以网格能回溯到的最早一天是本周周日往前 28 天。
    // 再往前就落到第 6 周,网格放不下 —— 30 天窗口最旧的几天会被跳过。
    int32_t oldest = today - we_day_weekday(today) - 28;
    assert(we_heat_cell(oldest, today, &row, &col));
    assert(row == 0 && col == 0);
    assert(!we_heat_cell(oldest - 1, today, &row, &col));

    // 未来日期不属于热力图。
    assert(!we_heat_cell(today + 1, today, &row, &col));

    // 网格内的日子必须落在合法行列,且不重复占位。
    int seen[WE_HEAT_ROWS][WE_HEAT_COLS];
    memset(seen, 0, sizeof(seen));
    int placed = 0;
    for (int offset = 0; offset < WE_HEAT_DAYS; offset++) {
        if (!we_heat_cell(today - offset, today, &row, &col)) continue;
        assert(row >= 0 && row < WE_HEAT_ROWS);
        assert(col >= 0 && col < WE_HEAT_COLS);
        assert(!seen[row][col]);
        seen[row][col] = 1;
        placed++;
    }
    // 5 列 × 7 行 = 35 格,30 天里最多只有一格会因跨 6 周而放不下。
    assert(placed >= WE_HEAT_DAYS - 1);
}

static void test_format(void)
{
    char text[24];
    we_format_count(830, text, sizeof(text));
    assert(strcmp(text, "830") == 0);
    we_format_count(12400, text, sizeof(text));
    assert(strcmp(text, "12.4K") == 0);
    we_format_count(423000, text, sizeof(text));
    assert(strcmp(text, "423K") == 0);
    we_format_count(1200000, text, sizeof(text));
    assert(strcmp(text, "1.2M") == 0);
    we_format_count(-5, text, sizeof(text));
    assert(strcmp(text, "0") == 0);

    we_format_money(2350, text, sizeof(text));
    assert(strcmp(text, "23.50") == 0);
    we_format_money(87, text, sizeof(text));
    assert(strcmp(text, "0.87") == 0);
    we_format_money(-150, text, sizeof(text));
    assert(strcmp(text, "-1.50") == 0);
    we_format_money(0, text, sizeof(text));
    assert(strcmp(text, "0.00") == 0);
}

static void test_level_and_fx(void)
{
    assert(we_balance_level(0, 500) == 2);
    assert(we_balance_level(-1, 500) == 2);
    assert(we_balance_level(100, 500) == 1);
    assert(we_balance_level(499, 500) == 1);
    assert(we_balance_level(500, 500) == 0);
    assert(we_balance_level(9999, 500) == 0);

    // CNY 平台不换算;USD 平台按固定汇率折算。
    assert(we_balance_to_cny_cents(WE_PLAT_DEEPSEEK, 2350) == 2350);
    assert(we_balance_to_cny_cents(WE_PLAT_KIMI, 1000) == 7100);
    // 非正值原样返回,避免把"未查询(-1)"折算成奇怪的数字。
    assert(we_balance_to_cny_cents(WE_PLAT_KIMI, -1) == -1);
}

static void test_defaults_and_sanitize(void)
{
    we_state_t state;
    we_state_defaults(&state);
    assert(!state.profile.configured);
    assert(state.threshold_cents == 500);
    assert(state.platforms[WE_PLAT_DEEPSEEK].balance_cents == -1);
    assert(state.cursor_cents[WE_PLAT_KIMI] == -1);
    assert(strcmp(state.platforms[WE_PLAT_DEEPSEEK].name, "DeepSeek") == 0);
    assert(we_enabled_platforms(&state) == 0);

    state.platforms[WE_PLAT_DEEPSEEK].has_key = true;
    state.platforms[WE_PLAT_KIMI].has_key = true;
    assert(we_enabled_platforms(&state) == 2);

    // 清洗:超界与空字段都要被修正。
    state.days_head = 255;
    state.days_count = 255;
    state.threshold_cents = -7;
    state.platforms[0].balance_cents = -99;
    state.platforms[0].today_spend_cents = -5;
    state.platforms[0].status = (we_net_status_t)99;
    state.platforms[0].name[0] = '\0';
    state.platforms[0].currency[0] = '\0';
    strcpy(state.platforms[1].key, "sk-test");
    state.platforms[1].has_key = false;
    we_state_sanitize(&state);
    assert(state.days_head == 0);
    assert(state.days_count == WE_HEAT_DAYS);
    assert(state.threshold_cents == 0);
    assert(state.platforms[0].balance_cents == -1);
    assert(state.platforms[0].today_spend_cents == 0);
    assert(state.platforms[0].status == WE_NET_IDLE);
    assert(strcmp(state.platforms[0].name, "DeepSeek") == 0);
    assert(strcmp(state.platforms[0].currency, "CNY") == 0);
    // 有 Key 文本却没有 has_key 标记时,清洗要把它纠正回来。
    assert(state.platforms[1].has_key);
}

int main(void)
{
    test_json();
    test_balance();
    test_estimate();
    test_accounting();
    test_heat_level();
    test_calendar();
    test_heat_grid();
    test_format();
    test_level_and_fx();
    test_defaults_and_sanitize();
    return 0;
}

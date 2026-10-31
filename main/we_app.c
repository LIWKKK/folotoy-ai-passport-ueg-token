// main/we_app.c —— UEG 数字生命卡:AI 余额与用量热力看板。
//
// 页面(UP/DOWN 短按循环,长按 OK 回首页):
//   P1 数字生命卡  身份资料 + 总余额概览(不含"生活信条")
//   P2 余额看板    各平台实时余额、今日花费与状态灯
//   P3 用量热力    最近 30 天逐日消耗热力格
//   P4 连接设置    Wi-Fi 配网热点开关
#include "we_config.h"
#include "we_f1_net.h"
#include "we_logic.h"
#include "we_net.h"
#include "we_nfc.h"
#include "we_portal.h"
#include "we_sound.h"
#include "we_storage.h"
#include "we_theme.h"
#include "we_time.h"

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "we_app";

typedef enum {
    PAGE_IDENTITY = 0,
    PAGE_BALANCE,
    PAGE_HEATMAP,
    PAGE_F1,
    PAGE_CONNECT,
} app_page_t;

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} key_message_t;

static we_state_t s_state;
static SemaphoreHandle_t s_state_lock;
static QueueHandle_t s_key_queue;
static lv_obj_t *s_screen;
static we_hud_t s_hud;
static volatile uint32_t s_revision = 1;
static uint32_t s_drawn_revision;
static volatile app_page_t s_page = PAGE_IDENTITY;
static volatile int s_battery = -1;
static bool s_booting = true;
static bool s_buttons_ready;
static volatile bool s_setup_visible;
static volatile bool s_auto_start_portal;
static int64_t s_boot_until_us;

#define APP_VIEW_COUNT 5

static const char *PAGE_TITLE_CN[APP_VIEW_COUNT] = {
    "数字生命卡", "余额看板", "用量热力", "F1 看板", "连接设置",
};

static const char *PAGE_TITLE_EN[APP_VIEW_COUNT] = {
    "DIGITAL LIFE", "BALANCE", "HEATMAP", "F1 PIT WALL", "CONNECT",
};

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
static lv_obj_t *label_at(lv_obj_t *parent, const char *text,
                          const lv_font_t *font, uint32_t color, int x, int y)
{
    lv_obj_t *label = we_label(parent, text, font, color);
    lv_obj_set_pos(label, x, y);
    return label;
}

// 状态灯配色:0 正常 / 1 偏低 / 2 耗尽或未知。
static uint32_t balance_color(int32_t cents, int32_t threshold)
{
    switch (we_balance_level(cents, threshold)) {
    case 0:  return WE_GREEN;
    case 1:  return WE_ORANGE_HI;
    default: return WE_RED;
    }
}

static uint32_t heat_color(int level)
{
    switch (level) {
    case 1:  return WE_HEAT_L1;
    case 2:  return WE_HEAT_L2;
    case 3:  return WE_HEAT_L3;
    case 4:  return WE_HEAT_L4;
    default: return WE_PANEL;
    }
}

// 全网折算成人民币分后的总余额;没有任何平台查到余额时返回 -1。
static int32_t total_balance_cny(const we_state_t *state)
{
    int64_t total = 0;
    bool any = false;
    for (int i = 0; i < WE_PLAT_KIND_COUNT; i++) {
        const we_platform_t *plat = &state->platforms[i];
        if (!plat->has_key || plat->balance_cents < 0) continue;
        total += we_balance_to_cny_cents((we_plat_kind_t)i, plat->balance_cents);
        any = true;
    }
    return any ? (int32_t)total : -1;
}

// ---------------------------------------------------------------------------
// P1 数字生命卡
// ---------------------------------------------------------------------------
static void draw_identity(lv_obj_t *scr, const we_state_t *state)
{
    label_at(scr, "UEG CITIZEN PROFILE", &lv_font_montserrat_14,
             WE_ORANGE, 14, 80);
    lv_obj_t *name = label_at(scr, state->profile.name, &we_font_24,
                              WE_WHITE, 14, 102);
    lv_obj_set_width(name, 164);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    // 右上角身份徽记,让姓名与下方数据形成两个视觉重点。
    we_corner_marks(scr, 184, 86, 42, 42, WE_ORANGE);
    we_block(scr, 201, 94, 8, 25, WE_ORANGE);
    we_block(scr, 193, 103, 25, 8, WE_ORANGE);

    label_at(scr, state->profile.role, &we_font_16,
             WE_ORANGE_HI, 14, 137);
    label_at(scr, state->profile.identity, &lv_font_montserrat_14,
             WE_BLUE, 14, 164);
    we_block(scr, 14, 190, 212, 1, WE_LINE);

    // 原先这里是"生活信条";按需求移除,改为一屏可见的总余额概览。
    label_at(scr, "总余额", &we_font_16, WE_GREY, 14, 200);
    int32_t total = total_balance_cny(state);
    char amount[24];
    uint32_t color = total < 0 ? WE_GREY
                              : balance_color(total, state->threshold_cents);
    if (total < 0) {
        snprintf(amount, sizeof(amount), "--");
    } else {
        char money[16];
        we_format_money(total, money, sizeof(money));
        snprintf(amount, sizeof(amount), "¥%s", money);
    }
    lv_obj_t *total_label = label_at(scr, amount, &lv_font_montserrat_20,
                                     color, 92, 194);
    (void)total_label;

    int enabled = we_enabled_platforms(state);
    char summary[48];
    if (enabled == 0) {
        snprintf(summary, sizeof(summary), "尚未配置平台 Key");
    } else {
        snprintf(summary, sizeof(summary), "%d 个平台 · USD 已按 %.2f 折算",
                 enabled, WE_USD_CNY_X100 / 100.0);
    }
    label_at(scr, summary, &we_font_16, WE_GREY, 14, 228);

    we_corner_marks(scr, 12, 252, 216, 34, WE_LINE);
    label_at(scr, "上/下翻页", &we_font_16, WE_GREY, 20, 262);
    label_at(scr, we_portal_running() ? "配网中" : "OK 刷新余额",
             &we_font_16, WE_ORANGE_HI, 104, 262);
}

// ---------------------------------------------------------------------------
// P2 余额看板
// ---------------------------------------------------------------------------
static void draw_balance(lv_obj_t *scr, const we_state_t *state)
{
    int enabled = we_enabled_platforms(state);
    if (enabled == 0) {
        label_at(scr, "请先配置平台 Key", &we_font_16, WE_GREY, 20, 110);
        label_at(scr, "进入连接设置开启热点", &we_font_16, WE_ORANGE_HI,
                 20, 142);
        label_at(scr, "再于手机网页填入 DeepSeek / Kimi Key",
                 &we_font_16, WE_GREY, 20, 168);
        return;
    }

    int row = 0;
    for (int i = 0; i < WE_PLAT_KIND_COUNT; i++) {
        const we_platform_t *plat = &state->platforms[i];
        if (!plat->has_key) continue;

        int y = 82 + row * 66;
        lv_obj_t *card = we_block(scr, 10, y, 220, 58, WE_PANEL_2);
        uint32_t color = balance_color(plat->balance_cents,
                                       state->threshold_cents);
        // 左侧状态条:余额档位一眼可见,不必读数字。
        lv_obj_set_style_border_width(card, 0, 0);
        we_block(scr, 10, y, 3, 58, color);

        label_at(scr, plat->name, &we_font_16, WE_WHITE, 22, y + 7);

        // 状态灯 + 平台状态短语。
        const char *status_text = "";
        switch (plat->status) {
        case WE_NET_LOADING: status_text = "查询中"; break;
        case WE_NET_OK:      status_text = "已同步"; break;
        case WE_NET_ERROR:   status_text = plat->error; break;
        default:             status_text = "待刷新"; break;
        }
        label_at(scr, status_text, &we_font_16,
                 plat->status == WE_NET_ERROR ? WE_RED : WE_GREY, 22, y + 30);

        char money[16];
        if (plat->balance_cents < 0) {
            snprintf(money, sizeof(money), "--");
        } else {
            we_format_money(plat->balance_cents, money, sizeof(money));
        }
        char amount[32];
        snprintf(amount, sizeof(amount), "%s %s", plat->currency, money);
        lv_obj_t *value = we_label(scr, amount, &lv_font_montserrat_20, color);
        lv_obj_align(value, LV_ALIGN_TOP_RIGHT, -18, y + 7);

        char spend[40];
        char spend_money[16];
        we_format_money(plat->today_spend_cents, spend_money, sizeof(spend_money));
        snprintf(spend, sizeof(spend), "今日 %s", spend_money);
        lv_obj_t *s = we_label(scr, spend, &lv_font_montserrat_14, WE_GREY);
        lv_obj_align(s, LV_ALIGN_TOP_RIGHT, -18, y + 34);
        row++;
    }

    label_at(scr, we_net_is_refreshing() ? "正在刷新…" : "OK 立即刷新",
             &we_font_16, WE_ORANGE_HI, 12, 278);
}

// ---------------------------------------------------------------------------
// P3 用量热力
// ---------------------------------------------------------------------------
#define HEAT_CELL_W 20
#define HEAT_CELL_H 18
#define HEAT_GAP_X  4
#define HEAT_GAP_Y  3

static void draw_heatmap(lv_obj_t *scr, const we_state_t *state)
{
    int32_t today = we_time_today_index();
    int used_days = we_days_with_usage(state, today, WE_HEAT_DAYS);
    int64_t total_tokens = we_window_tokens(state, today, WE_HEAT_DAYS);
    int32_t total_spend = we_window_spend(state, today, WE_HEAT_DAYS);

    // 汇总行:先回答"这 30 天烧了多少"。
    char count[24];
    we_format_count(total_tokens, count, sizeof(count));
    char money[16];
    we_format_money(total_spend, money, sizeof(money));

    label_at(scr, "近 30 天", &we_font_16, WE_GREY, 12, 80);
    lv_obj_t *tokens_label = we_label(scr, count, &lv_font_montserrat_20,
                                      WE_ORANGE_HI);
    lv_obj_align(tokens_label, LV_ALIGN_TOP_RIGHT, -12, 78);
    char detail[48];
    snprintf(detail, sizeof(detail), "使用 %d 天 · 花费 %s", used_days, money);
    label_at(scr, detail, &we_font_16, WE_GREY, 12, 106);

    // 7 行(周日..周六) × 5 列(由旧到新)的格子,与日历对齐。
    const int grid_w = WE_HEAT_COLS * HEAT_CELL_W + (WE_HEAT_COLS - 1) * HEAT_GAP_X;
    const int x0 = (240 - grid_w) / 2;
    const int y0 = 134;

    for (int offset = 0; offset < WE_HEAT_DAYS; offset++) {
        int32_t day = today - offset;
        int row = 0, col = 0;
        if (!we_heat_cell(day, today, &row, &col)) continue;

        const we_day_record_t *record = we_day_get(state, day);
        int level = we_heat_level(record ? record->tokens : 0);
        int x = x0 + col * (HEAT_CELL_W + HEAT_GAP_X);
        int y = y0 + row * (HEAT_CELL_H + HEAT_GAP_Y);
        lv_obj_t *cell = we_block(scr, x, y, HEAT_CELL_W, HEAT_CELL_H,
                                  heat_color(level));
        // 今天单独描边,便于在 30 格里定位。
        if (offset == 0) {
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_border_color(cell, lv_color_hex(WE_WHITE), 0);
        }
    }

    if (used_days == 0) {
        label_at(scr, "暂无用量记录", &we_font_16, WE_GREY, 80, 262);
    }

    // 图例:少 → 多。
    int legend_y = 288;
    label_at(scr, "少", &we_font_16, WE_GREY, 12, legend_y - 2);
    for (int i = 0; i < 4; i++)
        we_block(scr, 40 + i * 12, legend_y, 9, 9, heat_color(i + 1));
    label_at(scr, "多", &we_font_16, WE_GREY, 92, legend_y - 2);
}

// ---------------------------------------------------------------------------
// P4 F1 看板
// ---------------------------------------------------------------------------
static void draw_f1(lv_obj_t *scr)
{
    const we_f1_data_t *d = we_f1_net_data();

    if (!d->has_race && d->driver_count == 0 && d->news_count == 0) {
        label_at(scr, "暂无 F1 数据", &we_font_16, WE_GREY, 20, 106);
        label_at(scr, "按 OK 联网获取赛程与积分", &we_font_16,
                 WE_ORANGE_HI, 20, 138);
        label_at(scr, we_net_is_connected() ? "当前已联网" : "当前未联网",
                 &we_font_16, WE_GREY, 20, 164);
        return;
    }

    int y = 80;

    // 下一站 + 倒计时。
    if (d->has_race) {
        const we_f1_race_t *r = &d->race;
        char round_txt[16];
        snprintf(round_txt, sizeof(round_txt), "R%d", r->round);
        label_at(scr, round_txt, &lv_font_montserrat_14, WE_ORANGE, 12, y);

        lv_obj_t *name = label_at(scr, r->name, &we_font_16, WE_WHITE, 44, y - 2);
        lv_obj_set_width(name, 130);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

        // 倒计时按北京时间算:上游时间是 UTC,换算后再与本地时间比。
        // 目标是正赛**发车时刻**,不是当天零点 —— 否则全天都显示"已开始"。
        char cd[24];
        int64_t race_unix = we_f1_beijing_unix(r->day_index, 0);
        for (int i = 0; i < r->session_count; i++) {
            if (strcmp(r->sessions[i].label, "正赛") == 0) {
                race_unix = we_f1_beijing_unix(r->sessions[i].day_index,
                                               r->sessions[i].secs);
                break;
            }
        }
        we_f1_countdown(we_time_now(), race_unix, cd, sizeof(cd));
        lv_obj_t *cdl = we_label(scr, cd, &we_font_16, WE_ORANGE_HI);
        lv_obj_align(cdl, LV_ALIGN_TOP_RIGHT, -12, y - 2);
        y += 24;

        // 各节北京时间。
        for (int i = 0; i < r->session_count; i++) {
            const we_f1_session_t *s = &r->sessions[i];
            if (!s->present) continue;
            label_at(scr, s->label, &we_font_16, WE_GREY, 12, y);
            char dt[32];
            we_f1_format_dt(s->day_index, s->secs, dt, sizeof(dt));
            label_at(scr, dt, &we_font_16,
                     strcmp(s->label, "正赛") == 0 ? WE_WHITE : WE_GREY,
                     60, y);
            y += 20;
        }
        y += 4;
    }

    we_block(scr, 12, y - 4, 216, 1, WE_LINE);

    // 车手榜前 3。
    label_at(scr, "车手榜", &we_font_16, WE_ORANGE_HI, 12, y);
    y += 20;
    int drv_show = d->driver_count > 3 ? 3 : d->driver_count;
    for (int i = 0; i < drv_show; i++) {
        const we_f1_driver_t *drv = &d->drivers[i];
        char pos[8];
        snprintf(pos, sizeof(pos), "%d", drv->pos);
        label_at(scr, pos, &lv_font_montserrat_14,
                 i == 0 ? WE_YELLOW : WE_GREY, 12, y);
        lv_obj_t *nm = label_at(scr, drv->name, &we_font_16, WE_WHITE, 32, y);
        lv_obj_set_width(nm, 96);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        char pts[16];
        snprintf(pts, sizeof(pts), "%d", drv->points);
        lv_obj_t *pl = we_label(scr, pts, &we_font_16, WE_ORANGE_HI);
        lv_obj_align(pl, LV_ALIGN_TOP_RIGHT, -12, y);
        y += 20;
    }

    // 车队榜首。
    if (d->team_count > 0) {
        y += 4;
        label_at(scr, "车队榜首", &we_font_16, WE_ORANGE_HI, 12, y);
        char line[40];
        snprintf(line, sizeof(line), "%s  %d", d->teams[0].name,
                 d->teams[0].points);
        lv_obj_t *tl = we_label(scr, line, &we_font_16, WE_WHITE);
        lv_obj_align(tl, LV_ALIGN_TOP_RIGHT, -12, y);
        y += 22;
    }

    // 新闻标题(取第一条,避免页面溢出)。
    if (d->news_count > 0) {
        we_block(scr, 12, y - 2, 216, 1, WE_LINE);
        y += 6;
        lv_obj_t *news = label_at(scr, d->news[0], &we_font_16, WE_GREY, 12, y);
        lv_obj_set_width(news, 216);
        lv_label_set_long_mode(news, LV_LABEL_LONG_WRAP);
    }
}

// ---------------------------------------------------------------------------
// P5 连接设置
// ---------------------------------------------------------------------------
static void draw_connect(lv_obj_t *scr)
{
    bool running = we_portal_running();
    uint32_t color = running ? WE_GREEN : WE_GREY;
    label_at(scr, "Wi-Fi 本地配置", &we_font_16, WE_WHITE, 16, 86);
    label_at(scr, running ? "ONLINE" : "OFFLINE",
             &lv_font_montserrat_20, color, 16, 112);

    if (running) {
        label_at(scr, "热点", &we_font_16, WE_GREY, 16, 150);
        label_at(scr, we_portal_ssid(), &lv_font_montserrat_14,
                 WE_ORANGE_HI, 16, 174);
        label_at(scr, "密码", &we_font_16, WE_GREY, 16, 200);
        label_at(scr, we_portal_password(), &lv_font_montserrat_14,
                 WE_BLUE, 68, 200);
        label_at(scr, "地址  192.168.4.1", &we_font_16, WE_WHITE, 16, 228);
    } else {
        label_at(scr, "按 OK 启动配置热点", &we_font_16,
                 WE_ORANGE_HI, 16, 158);
        label_at(scr, "可填写 Wi-Fi 与平台 API Key", &we_font_16,
                 WE_GREY, 16, 186);
        label_at(scr, we_net_is_connected() ? "当前已联网" : "当前未联网",
                 &we_font_16, WE_GREY, 16, 212);
    }

    label_at(scr, "NFC", &lv_font_montserrat_14, WE_WHITE, 158, 86);
    label_at(scr, we_nfc_state_text(), &we_font_16, WE_GREY, 16, 250);
    label_at(scr, "上/下翻页 · OK 开关热点", &we_font_16,
             WE_ORANGE_HI, 16, 274);
}

// ---------------------------------------------------------------------------
// 屏幕构建
// ---------------------------------------------------------------------------
static void build_setup(void)
{
    memset(&s_hud, 0, sizeof(s_hud));
    lv_obj_t *next = we_screen_create();
    we_block(next, 0, 0, 240, 4, WE_ORANGE);
    label_at(next, "首次设置", &we_font_16, WE_ORANGE_HI, 14, 18);
    label_at(next, "FIRST SETUP", &lv_font_montserrat_14, WE_GREY, 130, 19);
    we_block(next, 14, 48, 212, 1, WE_LINE);

    label_at(next, "1  手机连接设备热点", &we_font_16, WE_WHITE, 14, 62);
    label_at(next, we_portal_ssid(), &lv_font_montserrat_14,
             WE_ORANGE_HI, 24, 89);
    label_at(next, "密码", &we_font_16, WE_GREY, 24, 114);
    label_at(next, we_portal_password(), &lv_font_montserrat_14,
             WE_BLUE, 70, 114);

    label_at(next, "2  浏览器打开", &we_font_16, WE_WHITE, 14, 150);
    label_at(next, WE_PORTAL_URL, &lv_font_montserrat_14,
             WE_GREEN, 24, 177);
    label_at(next, "3  填 Wi-Fi 与平台 Key 后保存", &we_font_16,
             WE_WHITE, 14, 212);
    label_at(next, "保存后自动重启并刷新余额", &we_font_16,
             WE_GREY, 24, 239);

    uint32_t color = we_portal_running() ? WE_GREEN : WE_RED;
    label_at(next, we_portal_running() ? "热点已启动" : "热点启动失败",
             &we_font_16, color, 14, 270);
    if (!s_buttons_ready)
        label_at(next, "BUTTON FAIL", &lv_font_montserrat_14, WE_RED, 128, 271);
    label_at(next, "OK 重试  长按 OK 暂时跳过", &we_font_16,
             WE_ORANGE_HI, 14, 296);

    lv_screen_load(next);
    if (s_screen) lv_obj_delete(s_screen);
    s_screen = next;
    s_drawn_revision = s_revision;
}

static void build_page(void)
{
    // 状态可能正被网络任务改写,先取一份快照再画。锁只在这里短暂持有,
    // 不在持锁期间碰 LVGL(绘制走 LVGL 锁,两把锁先后取、绝不嵌套)。
    we_state_t snapshot;
    if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(300)) != pdTRUE) return;
    snapshot = s_state;
    xSemaphoreGive(s_state_lock);

    app_page_t page = s_page;
    lv_obj_t *next = we_screen_create();
    memset(&s_hud, 0, sizeof(s_hud));
    we_hud_build(next, &s_hud, (uint8_t)page,
                 PAGE_TITLE_CN[page], PAGE_TITLE_EN[page]);
    if (!s_buttons_ready)
        label_at(next, "BUTTON FAIL", &lv_font_montserrat_14, WE_RED, 76, 6);

    char clock[16];
    we_time_format_hms(clock, sizeof(clock));
    lv_label_set_text(s_hud.time_label, clock);
    we_hud_set_battery(&s_hud, s_battery);

    switch (page) {
    case PAGE_IDENTITY: draw_identity(next, &snapshot); break;
    case PAGE_BALANCE:  draw_balance(next, &snapshot); break;
    case PAGE_HEATMAP:  draw_heatmap(next, &snapshot); break;
    case PAGE_F1:       draw_f1(next); break;
    case PAGE_CONNECT:  draw_connect(next); break;
    }

    lv_screen_load(next);
    if (s_screen) lv_obj_delete(s_screen);
    s_screen = next;
    s_drawn_revision = s_revision;
}

static void draw_boot(void)
{
    s_screen = we_screen_create();
    we_block(s_screen, 0, 0, 240, 4, WE_ORANGE);
    we_corner_marks(s_screen, 18, 54, 204, 190, WE_ORANGE);
    label_at(s_screen, "UEG", &lv_font_montserrat_20, WE_ORANGE, 92, 78);
    label_at(s_screen, "数字生命卡", &we_font_16, WE_WHITE, 66, 118);
    label_at(s_screen, "TOKEN BALANCE", &lv_font_montserrat_14, WE_BLUE, 67, 150);
    label_at(s_screen, "AI 余额与用量热力", &we_font_16, WE_ORANGE_HI, 54, 190);
    label_at(s_screen, WE_FW_VERSION, &lv_font_montserrat_14, WE_GREY, 61, 222);
    lv_screen_load(s_screen);
    s_boot_until_us = esp_timer_get_time() + 1400000;
}

// ---------------------------------------------------------------------------
// 按键
// ---------------------------------------------------------------------------
static app_page_t adjacent_main_page(app_page_t page, int direction)
{
    int current = (int)page;
    int next = (current + direction + WE_PAGE_COUNT) % WE_PAGE_COUNT;
    return (app_page_t)next;
}

static void handle_key(const key_message_t *message)
{
    bool portal_toggle = false;
    bool start_refresh = false;
    bool play_key = true;

    if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(500)) != pdTRUE) return;
    app_page_t page_before = s_page;
    bool setup_before = s_setup_visible;

    if (s_setup_visible) {
        if (message->button == BSP_BTN_OK && message->event == BSP_BTN_PRESS) {
            if (!we_portal_running()) portal_toggle = true;
        } else if (message->button == BSP_BTN_OK &&
                   message->event == BSP_BTN_LONG) {
            s_setup_visible = false;
            s_page = PAGE_IDENTITY;
        } else {
            play_key = false;
        }
        bool changed = (setup_before != s_setup_visible);
        xSemaphoreGive(s_state_lock);
        if (portal_toggle) we_portal_start();
        if (changed || portal_toggle) s_revision++;
        if (play_key) we_sound_play(WE_SND_KEY);
        return;
    }

    if (message->event == BSP_BTN_PRESS) {
        if (message->button == BSP_BTN_UP) {
            s_page = adjacent_main_page(s_page, -1);
        } else if (message->button == BSP_BTN_DOWN) {
            s_page = adjacent_main_page(s_page, 1);
        } else if (message->button == BSP_BTN_OK) {
            if (s_page == PAGE_CONNECT) {
                portal_toggle = true;
            } else {
                start_refresh = true;
            }
        }
    } else if (message->event == BSP_BTN_LONG &&
               message->button == BSP_BTN_OK) {
        // 长按 OK 统一回首页。
        s_page = PAGE_IDENTITY;
    } else {
        play_key = false;
    }

    bool ui_changed = (page_before != s_page);
    xSemaphoreGive(s_state_lock);

    if (portal_toggle) {
        if (we_portal_running()) we_portal_stop();
        else we_portal_start();
    }
    if (play_key) we_sound_play(WE_SND_KEY);
    if (ui_changed || portal_toggle) s_revision++;
    if (start_refresh) we_net_request_refresh();
}

static void input_task(void *arg)
{
    (void)arg;
    int tick = 0;
    for (;;) {
        key_message_t message;
        if (xQueueReceive(s_key_queue, &message, pdMS_TO_TICKS(250)) == pdTRUE)
            handle_key(&message);

        // 每 2 秒刷新一次顶栏电量与"今天"。
        if (++tick >= 8) {
            tick = 0;
            int battery = bsp_battery_soc();
            if (battery != s_battery) s_battery = battery;
        }
    }
}

static void button_callback(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    key_message_t message = { .button = button, .event = event };
    if (s_key_queue) xQueueSend(s_key_queue, &message, 0);
}

static void refresh_done(void)
{
    // 网络任务上下文:只置版本号,真正重画交给 ui_tick。
    s_revision++;
}

static void portal_changed(void *user)
{
    (void)user;
    s_setup_visible = false;
    s_page = PAGE_IDENTITY;
    s_revision++;
}

static void ui_tick(lv_timer_t *timer)
{
    (void)timer;
    if (s_booting) {
        if (esp_timer_get_time() < s_boot_until_us) return;
        s_booting = false;
        if (s_setup_visible) build_setup();
        else build_page();
        // 启动流程跑完再开 Wi-Fi,避免初始化抢占 LVGL 所需的内存与中断。
        if (s_auto_start_portal) {
            s_auto_start_portal = false;
            we_portal_start();
            s_revision++;
        }
        return;
    }

    // 页面或数据变化才重建屏幕;时间和电量每秒原位更新,避免整屏频闪。
    if (s_drawn_revision != s_revision) {
        if (s_setup_visible) build_setup();
        else build_page();
        return;
    }

    static uint8_t second_divider;
    if (++second_divider >= 4) {
        second_divider = 0;
        if (!s_setup_visible && s_hud.time_label) {
            char clock[16];
            we_time_format_hms(clock, sizeof(clock));
            lv_label_set_text(s_hud.time_label, clock);
            we_hud_set_battery(&s_hud, s_battery);
        }
    }
}

void app_main(void)
{
    we_time_init();
    ESP_LOGI(TAG, "%s 启动", WE_APP_NAME);

    if (we_storage_init() != ESP_OK || we_storage_load(&s_state) != ESP_OK) {
        ESP_LOGW(TAG, "存储不可用,使用本次运行的默认配置");
        we_state_defaults(&s_state);
    }

    // 没配 Wi-Fi 就直接进配网:否则这个固件没有可用的数据来源。
    s_setup_visible = !s_state.has_wifi;
    s_auto_start_portal = s_setup_visible;

    s_state_lock = xSemaphoreCreateMutex();
    s_key_queue = xQueueCreate(12, sizeof(key_message_t));
    if (!s_state_lock || !s_key_queue) {
        ESP_LOGE(TAG, "无法创建应用同步对象");
        return;
    }

    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示初始化失败");
        return;
    }
    bsp_display_backlight(85);
    if (bsp_battery_init() != ESP_OK)
        ESP_LOGW(TAG, "电量计不可用");

    we_sound_init();
    we_nfc_init();
    we_portal_init(&s_state, s_state_lock, portal_changed, NULL);
    we_net_init(&s_state, s_state_lock);
    we_net_set_refresh_done_cb(refresh_done);

    if (xTaskCreate(input_task, "we_input", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "输入任务创建失败");
        return;
    }
    s_buttons_ready = bsp_button_init(button_callback, NULL) == ESP_OK;
    if (!s_buttons_ready)
        ESP_LOGE(TAG, "按键初始化失败,界面将显示 BUTTON FAIL");

    if (bsp_lvgl_lock(1000)) {
        draw_boot();
        lv_timer_create(ui_tick, 250, NULL);
        bsp_lvgl_unlock();
    }
    we_sound_play(WE_SND_BOOT);
}

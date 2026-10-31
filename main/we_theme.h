// main/we_theme.h —— UEG 视觉主题:配色、中文字体、HUD 基元。
#pragma once

#include "lvgl.h"
#include <stdint.h>

// ---- UEG 配色(黑底 + 工程橙 + 警报红) ----
#define WE_BG        0x05070A   // 深空黑
#define WE_PANEL     0x0E151D   // 面板深蓝灰
#define WE_PANEL_2   0x14202C   // 次级面板
#define WE_LINE      0x223446   // 分隔线
#define WE_ORANGE    0xFF7A1A   // UEG 橙
#define WE_ORANGE_HI 0xFFB25E   // 高亮橙(等离子)
#define WE_WHITE     0xEAF2F8   // 正文白
#define WE_GREY      0x74879A   // 次要灰
#define WE_RED       0xE43B2F   // 警报红
#define WE_RED_HI    0xFF6A5E
#define WE_GREEN     0x35D99A   // 核验通过
#define WE_BLUE      0x2EA8FF   // 数据流蓝
#define WE_YELLOW    0xFFD23E

// 热力图四档:0 档(无记录)用 WE_PANEL,其余由浅到深走橙→红。
#define WE_HEAT_L1 0x3D2A18
#define WE_HEAT_L2 0x8A4A16
#define WE_HEAT_L3 0xD4701C
#define WE_HEAT_L4 0xFF9B50

// 中文字体子集由 tools/gen_we_font.sh 生成。
LV_FONT_DECLARE(we_font_16);
LV_FONT_DECLARE(we_font_24);

// 纯色矩形(无圆角、无边框、无内边距)。
lv_obj_t *we_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);

// 文本标签。
lv_obj_t *we_label(lv_obj_t *parent, const char *text,
                   const lv_font_t *font, uint32_t color);

// 建一张黑底屏(240x320)。
lv_obj_t *we_screen_create(void);

// HUD 四角的 L 形角标。
void we_corner_marks(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);

// 进度条:返回填充块句柄,we_bar_set() 按 0..100 改宽度。
lv_obj_t *we_bar_create(lv_obj_t *parent, int x, int y, int w, int h,
                        uint32_t track, uint32_t fill);
void we_bar_set(lv_obj_t *fill, int total_width, int percent);

// ---- 全局 HUD(顶栏标题/时间 + 底栏电量/页码) ----
#define WE_PAGE_COUNT 5

typedef struct {
    lv_obj_t *time_label;
    lv_obj_t *battery_label;
    lv_obj_t *dots[WE_PAGE_COUNT];
} we_hud_t;

// 在屏上建 HUD。page 为 0 基页码。
void we_hud_build(lv_obj_t *scr, we_hud_t *hud, uint8_t page,
                  const char *title_cn, const char *title_en);
void we_hud_set_time(we_hud_t *hud, int hh, int mm, int ss);
void we_hud_set_battery(we_hud_t *hud, int percent);

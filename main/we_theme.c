// main/we_theme.c —— UEG 主题实现。
#include "we_theme.h"

lv_obj_t *we_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

lv_obj_t *we_label(lv_obj_t *parent, const char *text,
                   const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

lv_obj_t *we_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(WE_BG), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    return scr;
}

void we_corner_marks(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    const int arm = 9;    // 角标臂长
    const int th  = 2;    // 线宽
    // 左上
    we_block(parent, x, y, arm, th, color);
    we_block(parent, x, y, th, arm, color);
    // 右上
    we_block(parent, x + w - arm, y, arm, th, color);
    we_block(parent, x + w - th, y, th, arm, color);
    // 左下
    we_block(parent, x, y + h - th, arm, th, color);
    we_block(parent, x, y + h - arm, th, arm, color);
    // 右下
    we_block(parent, x + w - arm, y + h - th, arm, th, color);
    we_block(parent, x + w - th, y + h - arm, th, arm, color);
}

lv_obj_t *we_bar_create(lv_obj_t *parent, int x, int y, int w, int h,
                        uint32_t track, uint32_t fill)
{
    we_block(parent, x, y, w, h, track);
    lv_obj_t *f = we_block(parent, x, y, 1, h, fill);
    return f;
}

void we_bar_set(lv_obj_t *fill, int total_width, int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    lv_obj_set_width(fill, (total_width * percent) / 100);
}

void we_hud_build(lv_obj_t *scr, we_hud_t *hud, uint8_t page,
                  const char *title_cn, const char *title_en)
{
    // 顶部橙线 + 底栏
    we_block(scr, 0, 0, 240, 2, WE_ORANGE);
    we_block(scr, 0, 298, 240, 22, WE_PANEL);
    we_block(scr, 0, 298, 240, 1, WE_LINE);

    // 顶栏左:UEG 标记
    we_block(scr, 8, 9, 8, 8, WE_ORANGE);
    lv_obj_t *ueg = we_label(scr, "UEG", &lv_font_montserrat_14, WE_WHITE);
    lv_obj_set_pos(ueg, 20, 6);
    // 顶栏右:时间
    hud->time_label = we_label(scr, "00:00:00", &lv_font_montserrat_14, WE_WHITE);
    lv_obj_align(hud->time_label, LV_ALIGN_TOP_RIGHT, -8, 6);

    // 标题行:中文标题 + 页码
    lv_obj_t *t = we_label(scr, title_cn, &we_font_16, WE_WHITE);
    lv_obj_set_pos(t, 8, 28);
    char pg[8];
    lv_snprintf(pg, sizeof(pg), "P%d/%d", page + 1, WE_PAGE_COUNT);
    lv_obj_t *pgl = we_label(scr, pg, &lv_font_montserrat_14, WE_ORANGE);
    lv_obj_align(pgl, LV_ALIGN_TOP_RIGHT, -8, 33);

    // 英文副标题
    lv_obj_t *te = we_label(scr, title_en, &lv_font_montserrat_14, WE_GREY);
    lv_obj_set_pos(te, 8, 54);
    we_block(scr, 8, 72, 224, 1, WE_LINE);

    // 底栏:电量与页码点
    hud->battery_label = we_label(scr, "BAT --%", &lv_font_montserrat_14, WE_GREY);
    lv_obj_set_pos(hud->battery_label, 6, 301);
    int dot_start = 102;
    for (int i = 0; i < WE_PAGE_COUNT; i++) {
        hud->dots[i] = we_block(scr, dot_start + i * 8, 307, 5, 5,
                                i == page ? WE_ORANGE : WE_LINE);
    }
    lv_obj_t *hint = we_label(scr, "长按OK返回", &we_font_16, WE_GREY);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_RIGHT, -6, 0);
}

void we_hud_set_time(we_hud_t *hud, int hh, int mm, int ss)
{
    if (!hud || !hud->time_label) return;
    lv_label_set_text_fmt(hud->time_label, "%02d:%02d:%02d", hh, mm, ss);
}

void we_hud_set_battery(we_hud_t *hud, int percent)
{
    if (!hud || !hud->battery_label) return;
    if (percent < 0)
        lv_label_set_text(hud->battery_label, "BAT --%");
    else
        lv_label_set_text_fmt(hud->battery_label, "BAT %d%%", percent);
}

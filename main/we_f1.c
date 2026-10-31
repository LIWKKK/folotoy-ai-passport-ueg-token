// main/we_f1.c —— F1 页纯逻辑实现。
// 与 we_logic.c 同风格:手写极简扫描,不引入 JSON/XML 库。
//
// 为什么不用 cJSON:本板 Flash 只剩几百 KB,且我们只取个位数字段。
// 代价是解析偏"脆"—— 上游字段名或结构变了要同步改这里,测试用例
// (tests/test_we_f1.c)保留了真实响应样本,改协议先改测试。
#include "we_f1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "we_logic.h"

// ---------------------------------------------------------------------------
// 极简取值
// ---------------------------------------------------------------------------

// 把一段窗口拷进 NUL 结尾的栈缓冲,便于后续用 strstr 安全扫描。
static size_t copy_span(char *dst, size_t dst_len, const char *src, size_t span)
{
    if (dst_len == 0) return 0;
    size_t n = span < dst_len - 1 ? span : dst_len - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return n;
}

static const char *key_at(const char *from, const char *key)
{
    char pat[40];
    if (snprintf(pat, sizeof(pat), "\"%s\"", key) >= (int)sizeof(pat)) return NULL;
    return from ? strstr(from, pat) : NULL;
}

// 读 "key":"value" 的字符串值(不展开转义,够用即止)。
static bool read_str(const char *json, const char *key, char *out, size_t out_len)
{
    if (out_len) out[0] = '\0';
    const char *p = key_at(json, key);
    if (!p) return false;
    p += strlen(key) + 2;
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_len) {
        if (*p == '\\' && p[1]) p++;    // 跳过转义,不还原
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

static int read_int(const char *json, const char *key, int fallback)
{
    char buf[24];
    if (!read_str(json, key, buf, sizeof(buf))) return fallback;
    return atoi(buf);
}

static bool parse_iso_date(const char *s, int *y, int *m, int *d)
{
    if (!s || strlen(s) < 10) return false;
    return sscanf(s, "%4d-%2d-%2d", y, m, d) == 3;
}

// "13:00:00Z" / "13:00" → 当日秒数。缺失时间按 0 处理。
static void parse_iso_time(const char *s, int32_t *secs)
{
    int h = 0, mi = 0, se = 0;
    *secs = 0;
    if (!s || !*s) return;
    if (sscanf(s, "%2d:%2d:%2d", &h, &mi, &se) < 2) return;
    *secs = h * 3600 + mi * 60 + se;
}

// 读一节的时间:先定位 key,再在其后 180 字节窗口内取 date/time。
static void read_session(const char *obj, const char *key, const char *label,
                         we_f1_session_t *s)
{
    s->present = false;
    const char *p = key_at(obj, key);
    if (!p) return;

    char span[192];
    copy_span(span, sizeof(span), p, 180);

    char d[24];
    if (!read_str(span, "date", d, sizeof(d))) return;
    int y, mo, da;
    if (!parse_iso_date(d, &y, &mo, &da)) return;

    char t[24];
    read_str(span, "time", t, sizeof(t));
    int32_t secs = 0;
    parse_iso_time(t, &secs);

    int32_t di = we_days_from_civil(y, mo, da);
    secs += WE_F1_BEIJING_OFFSET_SECS;
    di += secs / 86400;
    secs %= 86400;

    snprintf(s->label, sizeof(s->label), "%s", label);
    s->day_index = di;
    s->secs = secs;
    s->present = true;
}

// XML 文本:去 CDATA、还原常见实体、把换行折成空格。
// 截断必须落在 UTF-8 字符边界上:标题是英文/带重音字符的原文,按字节
// 硬切会把最后一个多字节字符砍成半个,LVGL 渲染时会出现乱码或豆腐块。
static void copy_xml_text(const char *src, size_t len, char *dst, size_t dst_len)
{
    size_t i = 0, o = 0;
    while (i < len && o + 1 < dst_len) {
        if (src[i] == '&') {
            struct { const char *ent; char ch; } ENT[] = {
                { "&amp;", '&' }, { "&quot;", '"' }, { "&apos;", '\'' },
                { "&#39;", '\'' }, { "&lt;", '<' }, { "&gt;", '>' },
            };
            bool hit = false;
            for (size_t k = 0; k < sizeof(ENT) / sizeof(ENT[0]); k++) {
                size_t el = strlen(ENT[k].ent);
                if (len - i >= el && strncmp(src + i, ENT[k].ent, el) == 0) {
                    dst[o++] = ENT[k].ch;
                    i += el;
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
        }
        unsigned char c = (unsigned char)src[i];
        size_t seq = c < 0x80 ? 1 :
                     (c & 0xE0) == 0xC0 ? 2 :
                     (c & 0xF0) == 0xE0 ? 3 :
                     (c & 0xF8) == 0xF0 ? 4 : 1;
        if (o + seq >= dst_len) break;      // 放不下就整个字符丢掉,不切一半
        if (seq == 1 && (c == '\n' || c == '\r' || c == '\t')) c = ' ';
        memcpy(dst + o, src + i, seq);
        o += seq;
        i += seq;
    }
    dst[o] = '\0';
}

// ---------------------------------------------------------------------------
// 赛历
// ---------------------------------------------------------------------------
bool we_f1_parse_next_race(const char *json, int32_t today_index,
                           we_f1_race_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!json) return false;

    const char *p = json;
    while ((p = strstr(p, "\"raceName\"")) != NULL) {
        const char *next = strstr(p + 1, "\"raceName\"");
        size_t span = next ? (size_t)(next - p) : strlen(p);

        char obj[1024];
        copy_span(obj, sizeof(obj), p, span);

        char d[24];
        int y, mo, da;
        if (!read_str(obj, "date", d, sizeof(d)) ||
            !parse_iso_date(d, &y, &mo, &da)) {
            if (!next) break;
            p = next;
            continue;
        }
        int32_t di = we_days_from_civil(y, mo, da);
        if (di < today_index) {         // 已过去的站,跳过
            if (!next) break;
            p = next;
            continue;
        }

        read_str(obj, "raceName", out->name, sizeof(out->name));
        read_str(obj, "circuitName", out->circuit, sizeof(out->circuit));
        read_str(obj, "country", out->country, sizeof(out->country));
        // round/season/url 在 raceName **之前**。不能拿固定字节数的回看窗口:
        // 真实响应里中间夹着一条很长的 url(几十字符),固定窗口会把 round 漏掉。
        // 改为回退到本对象的起始 '{' —— 该对象内 raceName 之前没有嵌套对象,
        // 所以遇到的第一 '}'... 即最近的 '{' 就是本站的开头。
        {
            const char *ostart = p;
            while (ostart > json && *ostart != '{') ostart--;
            char pre[256];
            size_t span = (size_t)(p - ostart);
            if (span >= sizeof(pre)) span = sizeof(pre) - 1;
            copy_span(pre, sizeof(pre), ostart, span);
            out->round = read_int(pre, "round", 0);
        }
        out->day_index = di;

        // 顺序即显示顺序;上游没给的节会自动跳过。
        static const struct { const char *key; const char *label; } SESS[] = {
            { "FirstPractice",    "一练" },
            { "SecondPractice",   "二练" },
            { "ThirdPractice",    "三练" },
            { "SprintQualifying", "冲刺排位" },
            { "Sprint",           "冲刺赛" },
            { "Qualifying",       "排位" },
        };
        out->session_count = 0;
        for (size_t i = 0; i < sizeof(SESS) / sizeof(SESS[0]); i++) {
            if (out->session_count >= WE_F1_SESSION_MAX - 1) break;
            we_f1_session_t s;
            read_session(obj, SESS[i].key, SESS[i].label, &s);
            if (s.present) out->sessions[out->session_count++] = s;
        }

        // 正赛本身也列一节,固定在最后。
        char rt[24];
        read_str(obj, "time", rt, sizeof(rt));
        int32_t rsec = 0;
        parse_iso_time(rt, &rsec);
        int32_t rdi = di + (rsec + WE_F1_BEIJING_OFFSET_SECS) / 86400;
        int32_t rsec_bj = (rsec + WE_F1_BEIJING_OFFSET_SECS) % 86400;
        if (out->session_count < WE_F1_SESSION_MAX) {
            we_f1_session_t *s = &out->sessions[out->session_count++];
            snprintf(s->label, sizeof(s->label), "正赛");
            s->day_index = rdi;
            s->secs = rsec_bj;
            s->present = true;
        }

        out->valid = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 积分榜
// ---------------------------------------------------------------------------
int we_f1_parse_drivers(const char *json, we_f1_driver_t *out, int max)
{
    if (!json || !out || max <= 0) return 0;
    const char *p = strstr(json, "\"DriverStandings\"");
    if (!p) return 0;

    int n = 0;
    p = strstr(p, "\"positionText\"");
    while (p && n < max) {
        const char *next = strstr(p + 1, "\"positionText\"");
        size_t span = next ? (size_t)(next - p) : strlen(p);

        char obj[512];
        copy_span(obj, sizeof(obj), p, span);

        char buf[16];
        out[n].pos = read_int(obj, "positionText", 0);
        out[n].points = read_str(obj, "points", buf, sizeof(buf)) ? atoi(buf) : 0;
        out[n].name[0] = '\0';
        out[n].team[0] = '\0';
        read_str(obj, "familyName", out[n].name, sizeof(out[n].name));
        const char *cs = strstr(obj, "\"Constructors\"");
        if (cs) read_str(cs, "name", out[n].team, sizeof(out[n].team));

        n++;
        p = next;
    }
    return n;
}

int we_f1_parse_teams(const char *json, we_f1_team_t *out, int max)
{
    if (!json || !out || max <= 0) return 0;
    const char *p = strstr(json, "\"ConstructorStandings\"");
    if (!p) return 0;

    int n = 0;
    p = strstr(p, "\"positionText\"");
    while (p && n < max) {
        const char *next = strstr(p + 1, "\"positionText\"");
        size_t span = next ? (size_t)(next - p) : strlen(p);

        char obj[512];
        copy_span(obj, sizeof(obj), p, span);

        char buf[16];
        out[n].pos = read_int(obj, "positionText", 0);
        out[n].points = read_str(obj, "points", buf, sizeof(buf)) ? atoi(buf) : 0;
        out[n].name[0] = '\0';
        read_str(obj, "name", out[n].name, sizeof(out[n].name));

        n++;
        p = next;
    }
    return n;
}

// ---------------------------------------------------------------------------
// 新闻
// ---------------------------------------------------------------------------
int we_f1_parse_news(const char *xml, char out[][WE_F1_HEADLINE_BYTES], int max)
{
    if (!xml || !out || max <= 0) return 0;

    // 从第一个 <item> 起扫,避免把频道自身的 <title> 当成新闻。
    const char *p = strstr(xml, "<item");
    if (!p) return 0;

    int n = 0;
    while (n < max && (p = strstr(p, "<title")) != NULL) {
        const char *gt = strchr(p, '>');
        if (!gt) break;
        const char *end = strstr(gt + 1, "</title>");
        if (!end) break;

        const char *s = gt + 1;
        size_t len = (size_t)(end - s);
        if (len >= 9 && strncmp(s, "<![CDATA[", 9) == 0) {
            s += 9;
            len -= 9;
            if (len >= 3 && strncmp(end - 3, "]]>", 3) == 0) len -= 3;
        }
        copy_xml_text(s, len, out[n], WE_F1_HEADLINE_BYTES);

        n++;
        p = end + 8;
    }
    return n;
}

// ---------------------------------------------------------------------------
// 时间显示
// ---------------------------------------------------------------------------
void we_f1_format_dt(int32_t day_index, int32_t secs, char *out, size_t out_len)
{
    int y, mo, d;
    we_civil_from_days(day_index, &y, &mo, &d);
    int hh = (int)(secs / 3600);
    int mm = (int)((secs % 3600) / 60);
    snprintf(out, out_len, "%02d-%02d %02d:%02d", mo, d, hh, mm);
}

void we_f1_countdown(int64_t now_unix, int64_t race_unix, char *out,
                     size_t out_len)
{
    int64_t d = race_unix - now_unix;
    if (d <= 0) {
        snprintf(out, out_len, "已开始");
        return;
    }
    int64_t days = d / 86400;
    int64_t rem = d % 86400;
    if (days > 0) {
        snprintf(out, out_len, "%d天 %02d:%02d", (int)days,
                 (int)(rem / 3600), (int)((rem % 3600) / 60));
    } else {
        snprintf(out, out_len, "%02d:%02d:%02d", (int)(rem / 3600),
                 (int)((rem % 3600) / 60), (int)(rem % 60));
    }
}

int64_t we_f1_beijing_unix(int32_t day_index, int32_t secs)
{
    return (int64_t)day_index * 86400 + secs;
}

// main/we_f1_net.c —— F1 数据抓取。
//
// 端点选择(都实测过):
//   * next.json 只返回下一站,765 B;而整季赛历 14 KB 且要先扫过 60% 才到下一站。
//   * 积分榜加 ?limit=5,只取前 5 名,响应从 10 KB 降到 2.4 KB。
//   * 新闻用 formula1.com 的 RSS(BBC 那条在开发网络不可达)。
// 本板 RAM 紧张,全部抓取复用同一块缓冲(顺序执行,不会重叠)。
#include "we_f1_net.h"

#include "we_http.h"
#include "we_time.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "we_f1_net";

#define F1_BUF_MAX 4096
#define URL_NEXT   "https://api.jolpi.ca/ergast/f1/current/next.json"
#define URL_DRV    "https://api.jolpi.ca/ergast/f1/current/driverstandings.json?limit=5"
#define URL_TEAM   "https://api.jolpi.ca/ergast/f1/current/constructorstandings.json?limit=5"
#define URL_NEWS   "https://www.formula1.com/en/latest/all.xml"

// 复用缓冲:上面四个请求依次执行,同一时刻只有一个在用。
static char s_buf[F1_BUF_MAX];
static we_f1_data_t s_data;
static volatile uint32_t s_version;

void we_f1_net_refresh(void)
{
    bool any = false;

    // 赛历:next.json 已经替我们算好"下一站",不再按本地日期过滤 ——
    // 设备时钟可能还没同步,用错日期反而会跳过正确的那一站。
    if (we_http_get(URL_NEXT, NULL, s_buf, sizeof(s_buf)) == 0) {
        we_f1_race_t race;
        if (we_f1_parse_next_race(s_buf, 0, &race)) {
            s_data.race = race;
            s_data.has_race = true;
            any = true;
        } else {
            ESP_LOGW(TAG, "赛历解析失败");
        }
    } else {
        ESP_LOGW(TAG, "赛历请求失败");
    }

    if (we_http_get(URL_DRV, NULL, s_buf, sizeof(s_buf)) == 0) {
        we_f1_driver_t drv[WE_F1_STANDING_MAX];
        int n = we_f1_parse_drivers(s_buf, drv, WE_F1_STANDING_MAX);
        if (n > 0) {
            memcpy(s_data.drivers, drv, sizeof(drv));
            s_data.driver_count = n;
            any = true;
        }
    } else {
        ESP_LOGW(TAG, "车手榜请求失败");
    }

    if (we_http_get(URL_TEAM, NULL, s_buf, sizeof(s_buf)) == 0) {
        we_f1_team_t team[WE_F1_STANDING_MAX];
        int n = we_f1_parse_teams(s_buf, team, WE_F1_STANDING_MAX);
        if (n > 0) {
            memcpy(s_data.teams, team, sizeof(team));
            s_data.team_count = n;
            any = true;
        }
    } else {
        ESP_LOGW(TAG, "车队榜请求失败");
    }

    // 新闻:4 KB 缓冲会把较长的 feed 截断,但标题在前、且解析器遇到不完整的
    // 条目会直接跳过 —— 少一条,不会出半条。
    if (we_http_get(URL_NEWS, NULL, s_buf, sizeof(s_buf)) == 0) {
        char news[WE_F1_NEWS_MAX][WE_F1_HEADLINE_BYTES];
        int n = we_f1_parse_news(s_buf, news, WE_F1_NEWS_MAX);
        if (n > 0) {
            memcpy(s_data.news, news, sizeof(news));
            s_data.news_count = n;
            any = true;
        }
    } else {
        ESP_LOGW(TAG, "新闻请求失败");
    }

    if (any) {
        s_data.fetched_unix = we_time_now();
        ESP_LOGI(TAG, "F1 数据已更新:赛历 %d 站 / 车手 %d / 车队 %d / 新闻 %d",
                 s_data.has_race ? 1 : 0, s_data.driver_count,
                 s_data.team_count, s_data.news_count);
    }
    s_version++;
}

const we_f1_data_t *we_f1_net_data(void) { return &s_data; }

uint32_t we_f1_net_version(void) { return s_version; }

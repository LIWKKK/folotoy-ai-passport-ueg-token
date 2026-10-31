// tests/test_we_f1.c —— F1 页纯逻辑的宿主测试。
// 样本取自上游真实响应(api.jolpi.ca / ergast 兼容接口),裁掉无关字段后内联,
// 这样改协议时测试会先失败,而不是上屏才发现。
//
// 注意:新闻样本是**按 BBC F1 RSS 的实际结构手写的**(该 feed 从开发机不可达,
// 无法直接抓取)。字段形态(CDATA、&amp; 实体、每 item 一个 title)与上游一致。
#include <assert.h>
#include <string.h>

#include "we_f1.h"

static const char *F1_CAL =
    "{\"MRData\": {\"RaceTable\": {\"Races\": [{\"season\": \"2026\", \"round\": \"1\", \"raceNa"
    "me\": \"Australian Grand Prix\", \"Circuit\": {\"circuitId\": \"albert_park\", \"circu"
    "itName\": \"Albert Park Grand Prix Circuit\", \"Location\": {\"locality\": \"Melbour"
    "ne\", \"country\": \"Australia\"}}, \"date\": \"2026-03-08\", \"time\": \"04:00:00Z\", \"F"
    "irstPractice\": {\"date\": \"2026-03-06\", \"time\": \"01:30:00Z\"}, \"SecondPractice\""
    ": {\"date\": \"2026-03-06\", \"time\": \"05:00:00Z\"}, \"ThirdPractice\": {\"date\": \"20"
    "26-03-07\", \"time\": \"01:30:00Z\"}, \"Qualifying\": {\"date\": \"2026-03-07\", \"time\""
    ": \"05:00:00Z\"}}, {\"season\": \"2026\", \"round\": \"14\", \"raceName\": \"Spanish Gran"
    "d Prix\", \"Circuit\": {\"circuitId\": \"madring\", \"circuitName\": \"Madring\", \"Loca"
    "tion\": {\"locality\": \"Madrid\", \"country\": \"Spain\"}}, \"date\": \"2026-09-13\", \"t"
    "ime\": \"13:00:00Z\", \"FirstPractice\": {\"date\": \"2026-09-11\", \"time\": \"11:30:00"
    "Z\"}, \"SecondPractice\": {\"date\": \"2026-09-11\", \"time\": \"15:00:00Z\"}, \"ThirdPr"
    "actice\": {\"date\": \"2026-09-12\", \"time\": \"10:30:00Z\"}, \"Qualifying\": {\"date\":"
    " \"2026-09-12\", \"time\": \"14:00:00Z\"}}]}}}";

static const char *F1_DRV =
    "{\"MRData\": {\"StandingsTable\": {\"StandingsLists\": [{\"season\": \"2026\", \"round\""
    ": \"14\", \"DriverStandings\": [{\"position\": \"1\", \"positionText\": \"1\", \"points\":"
    " \"267\", \"wins\": \"7\", \"Driver\": {\"driverId\": \"antonelli\", \"code\": \"ANT\", \"giv"
    "enName\": \"Andrea Kimi\", \"familyName\": \"Antonelli\", \"nationality\": \"Italian\"}"
    ", \"Constructors\": [{\"constructorId\": \"mercedes\", \"name\": \"Mercedes\"}]}, {\"po"
    "sition\": \"2\", \"positionText\": \"2\", \"points\": \"201\", \"wins\": \"2\", \"Driver\": {"
    "\"driverId\": \"russell\", \"code\": \"RUS\", \"givenName\": \"George\", \"familyName\": \""
    "Russell\", \"nationality\": \"British\"}, \"Constructors\": [{\"constructorId\": \"mer"
    "cedes\", \"name\": \"Mercedes\"}]}, {\"position\": \"3\", \"positionText\": \"3\", \"point"
    "s\": \"191\", \"wins\": \"1\", \"Driver\": {\"driverId\": \"hamilton\", \"code\": \"HAM\", \"g"
    "ivenName\": \"Lewis\", \"familyName\": \"Hamilton\", \"nationality\": \"British\"}, \"Co"
    "nstructors\": [{\"constructorId\": \"ferrari\", \"name\": \"Ferrari\"}]}]}]}}}";

static const char *F1_TEAM =
    "{\"MRData\": {\"StandingsTable\": {\"StandingsLists\": [{\"season\": \"2026\", \"round\""
    ": \"14\", \"ConstructorStandings\": [{\"position\": \"1\", \"positionText\": \"1\", \"poi"
    "nts\": \"468\", \"wins\": \"9\", \"Constructor\": {\"constructorId\": \"mercedes\", \"name"
    "\": \"Mercedes\", \"nationality\": \"German\"}}, {\"position\": \"2\", \"positionText\": "
    "\"2\", \"points\": \"346\", \"wins\": \"2\", \"Constructor\": {\"constructorId\": \"ferrari"
    "\", \"name\": \"Ferrari\", \"nationality\": \"Italian\"}}, {\"position\": \"3\", \"positio"
    "nText\": \"3\", \"points\": \"287\", \"wins\": \"2\", \"Constructor\": {\"constructorId\": "
    "\"mclaren\", \"name\": \"McLaren\", \"nationality\": \"British\"}}]}]}}}";

// **原封不动**的 current/next.json 真实响应(765 B,含 raceName 之前那条长 url)。
// 当初用裁剪过的小样本做测试,漏掉了 round 解析失败(整季样本每个 race 对象里
// 没有 url,而真实响应有)—— 只有照抄真响应才能覆盖到。
static const char *F1_NEXT =
    "{\"MRData\":{\"xmlns\":\"\",\"series\":\"f1\",\"url\":\"https://api.jolpi.ca/ergast/f1/cu"
    "rrent/next.json\",\"limit\":\"30\",\"offset\":\"0\",\"total\":\"1\",\"RaceTable\":{\"season\""
    ":\"2026\",\"round\":\"14\",\"Races\":[{\"season\":\"2026\",\"round\":\"14\",\"url\":\"https://e"
    "n.wikipedia.org/wiki/2026_Spanish_Grand_Prix\",\"raceName\":\"Spanish Grand Prix"
    "\",\"Circuit\":{\"circuitId\":\"madring\",\"url\":\"https://en.wikipedia.org/wiki/Madr"
    "ing\",\"circuitName\":\"Madring\",\"Location\":{\"lat\":\"40.46528\",\"long\":\"-3.61528\","
    "\"locality\":\"Madrid\",\"country\":\"Spain\"}},\"date\":\"2026-09-13\",\"time\":\"13:00:00"
    "Z\",\"FirstPractice\":{\"date\":\"2026-09-11\",\"time\":\"11:30:00Z\"},\"SecondPractice\""
    ":{\"date\":\"2026-09-11\",\"time\":\"15:00:00Z\"},\"ThirdPractice\":{\"date\":\"2026-09-1"
    "2\",\"time\":\"10:30:00Z\"},\"Qualifying\":{\"date\":\"2026-09-12\",\"time\":\"14:00:00Z\"}"
    "}]}}}";

// 2026-03-08 / 2026-09-13 的日序号(we_days_from_civil 计算,测试里写死便于固定基准)。
#define DAY_2026_09_13 20709

// BBC F1 RSS 结构样本。
static const char *F1_RSS =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><rss version=\"2.0\"><channel>"
    "<title>BBC Sport - Formula 1</title>"
    "<item><title><![CDATA[Norris wins in Spain &amp; takes the lead]]></title>"
    "<description>desc</description></item>"
    "<item><title>Verstappen fastest in opening practice</title>"
    "<description>desc</description></item>"
    "<item><title><![CDATA[Teams &amp; drivers: 2026 season preview]]></title>"
    "<description>desc</description></item>"
    "</channel></rss>";

static void test_next_race(void)
{
    we_f1_race_t race;
    // 今天 = 2026-09-13:3 月那站已过去,应当跳过并选中西班牙站。
    assert(we_f1_parse_next_race(F1_CAL, DAY_2026_09_13, &race));
    assert(race.valid);
    assert(strcmp(race.name, "Spanish Grand Prix") == 0);
    assert(strcmp(race.circuit, "Madring") == 0);
    assert(strcmp(race.country, "Spain") == 0);
    assert(race.round == 14);
    // 正赛 UTC 09-13 13:00 -> 北京 09-13 21:00
    assert(race.day_index == DAY_2026_09_13);

    // 四节练习/排位 + 正赛 = 5 节
    assert(race.session_count == 5);

    // 一练 UTC 09-11 11:30 -> 北京 09-11 19:30
    assert(race.sessions[0].present);
    assert(strcmp(race.sessions[0].label, "一练") == 0);
    assert(race.sessions[0].day_index == DAY_2026_09_13 - 2);
    assert(race.sessions[0].secs == 19 * 3600 + 30 * 60);

    // 排位 UTC 09-12 14:00 -> 北京 09-12 22:00
    assert(strcmp(race.sessions[3].label, "排位") == 0);
    assert(race.sessions[3].day_index == DAY_2026_09_13 - 1);
    assert(race.sessions[3].secs == 22 * 3600);

    // 最后一节固定是正赛,北京 21:00
    assert(strcmp(race.sessions[4].label, "正赛") == 0);
    assert(race.sessions[4].secs == 21 * 3600);

    // 真实 next.json:回归覆盖"round 取不到"的 bug。
    // 该响应里 raceName 之前夹着一条完整 url,任何固定字节数的回看窗口都会漏掉 round。
    we_f1_race_t live;
    assert(we_f1_parse_next_race(F1_NEXT, 0, &live));
    assert(live.round > 0);                 // 曾经是 0(解析窗口太短)
    assert(live.name[0] != '\0');
    assert(live.circuit[0] != '\0');
    assert(live.session_count == 5);        // 4 节 + 正赛
    assert(strcmp(live.sessions[live.session_count - 1].label, "正赛") == 0);

    // 没有后续站点(今天晚于最后一站)时应返回 false,而不是给出过期数据。
    we_f1_race_t none;
    assert(!we_f1_parse_next_race(F1_CAL, DAY_2026_09_13 + 5, &none));

    // 垃圾输入不能崩。
    assert(!we_f1_parse_next_race("not json", 0, &none));
    assert(!we_f1_parse_next_race("{}", 0, &none));
}

static void test_standings(void)
{
    we_f1_driver_t drv[WE_F1_STANDING_MAX];
    int n = we_f1_parse_drivers(F1_DRV, drv, WE_F1_STANDING_MAX);
    assert(n == 3);
    assert(drv[0].pos == 1);
    assert(strcmp(drv[0].name, "Antonelli") == 0);
    assert(strcmp(drv[0].team, "Mercedes") == 0);
    assert(drv[0].points == 267);
    assert(drv[2].pos == 3);
    assert(strcmp(drv[2].name, "Hamilton") == 0);
    assert(strcmp(drv[2].team, "Ferrari") == 0);
    assert(drv[2].points == 191);

    // max 生效:只要 2 条。
    we_f1_driver_t two[2];
    assert(we_f1_parse_drivers(F1_DRV, two, 2) == 2);

    we_f1_team_t team[WE_F1_STANDING_MAX];
    int m = we_f1_parse_teams(F1_TEAM, team, WE_F1_STANDING_MAX);
    assert(m == 3);
    assert(team[0].pos == 1);
    assert(strcmp(team[0].name, "Mercedes") == 0);
    assert(team[0].points == 468);

    // 两份榜单结构不同:车手榜不能被车队解析函数误读。
    assert(we_f1_parse_teams(F1_DRV, team, WE_F1_STANDING_MAX) == 0);
    assert(we_f1_parse_drivers("garbage", drv, WE_F1_STANDING_MAX) == 0);
}

static void test_news(void)
{
    char news[WE_F1_NEWS_MAX][WE_F1_HEADLINE_BYTES];
    // 只要 3 条里前 2 条。
    int n = we_f1_parse_news(F1_RSS, news, 2);
    assert(n == 2);
    // CDATA 去掉,实体还原成 &。
    assert(strcmp(news[0], "Norris wins in Spain & takes the lead") == 0);
    assert(strcmp(news[1], "Verstappen fastest in opening practice") == 0);

    // 频道自身的 title 不能被当成新闻。
    assert(strncmp(news[0], "BBC Sport", 9) != 0);

    // 取满 3 条时第三条同样去 CDATA + 还原实体。
    char all[WE_F1_NEWS_MAX][WE_F1_HEADLINE_BYTES];
    assert(we_f1_parse_news(F1_RSS, all, WE_F1_NEWS_MAX) == 3);
    assert(strcmp(all[2], "Teams & drivers: 2026 season preview") == 0);

    // 空/垃圾输入返回 0。
    assert(we_f1_parse_news("", news, 1) == 0);
    assert(we_f1_parse_news("<rss><channel></channel></rss>", news, 1) == 0);
}

static void test_time_format(void)
{
    char buf[32];
    // 2026-09-13 21:00
    we_f1_format_dt(DAY_2026_09_13, 21 * 3600, buf, sizeof(buf));
    assert(strcmp(buf, "09-13 21:00") == 0);
    // 个位月日要补零
    we_f1_format_dt(DAY_2026_09_13 - 2, 9 * 3600 + 5 * 60, buf, sizeof(buf));
    assert(strcmp(buf, "09-11 09:05") == 0);

    int64_t race = we_f1_beijing_unix(DAY_2026_09_13, 21 * 3600);

    // 超过一天:显示天数
    we_f1_countdown(race - (4 * 86400 + 3 * 3600 + 12 * 60), race, buf, sizeof(buf));
    assert(strcmp(buf, "4天 03:12") == 0);
    // 当天:显示时分秒
    we_f1_countdown(race - 100, race, buf, sizeof(buf));
    assert(strcmp(buf, "00:01:40") == 0);
    // 已开始/已过:不能出现负数
    we_f1_countdown(race, race, buf, sizeof(buf));
    assert(strcmp(buf, "已开始") == 0);
    we_f1_countdown(race + 500, race, buf, sizeof(buf));
    assert(strcmp(buf, "已开始") == 0);
}

int main(void)
{
    test_next_race();
    test_standings();
    test_news();
    test_time_format();
    return 0;
}

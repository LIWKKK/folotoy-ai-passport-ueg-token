// main/we_f1_net.h —— F1 数据的抓取与缓存。
// 由 we_net.c 的工作任务在联网成功后调用(同一任务上下文,不另起线程),
// 因此这里的函数是阻塞的,UI 侧只读缓存。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "we_f1.h"

typedef struct {
    we_f1_race_t race;
    bool has_race;

    we_f1_driver_t drivers[WE_F1_STANDING_MAX];
    int driver_count;
    we_f1_team_t teams[WE_F1_STANDING_MAX];
    int team_count;

    char news[WE_F1_NEWS_MAX][WE_F1_HEADLINE_BYTES];
    int news_count;

    int64_t fetched_unix;   // 0 = 从未成功抓取过
} we_f1_data_t;

// 抓取一次(阻塞)。在联网工作任务里调用;部分失败时保留上一次的对应字段。
void we_f1_net_refresh(void);

// 只读缓存(永不为 NULL,失败时为全零)。
const we_f1_data_t *we_f1_net_data(void);

// 数据版本号:每次刷新结束自增,供 UI 判断是否需要重画。
uint32_t we_f1_net_version(void);

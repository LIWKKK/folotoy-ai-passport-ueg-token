// main/we_sound.h —— 音效:全部由 ES8311 实时合成正弦 PCM,不依赖音频资源文件。
// 播放阻塞较久,统一在内部 worker 任务里执行;页面调用 we_sound_* 立即返回。
#pragma once

#include <stdbool.h>

typedef enum {
    WE_SND_NONE = 0,
    WE_SND_BOOT,       // 开机上行音阶
    WE_SND_KEY,        // 轻按键音
    WE_SND_CHECKIN,    // 核验通过(上行两音)
    WE_SND_DENY,       // 核验失败(低音)
    WE_SND_IGNITION,   // 点火轰鸣(频率下滑)
    WE_SND_QIANJIN,    // "前进三"快速上行音阶
} we_snd_t;

// 初始化(创建 worker 任务)。codec 不可用时静默降级为不发声。
void we_sound_init(void);

// 排队播放一段音效(非阻塞)。
void we_sound_play(we_snd_t snd);

// 警报循环(双音交替),start/stop 配对使用;页面退出前必须 stop。
void we_sound_alert_start(void);
void we_sound_alert_stop(void);

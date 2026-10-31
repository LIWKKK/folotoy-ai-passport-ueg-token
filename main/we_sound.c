// main/we_sound.c —— PCM 合成音效,见 we_sound.h。
#include "we_sound.h"
#include "bsp_audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "we_snd";

#define SND_RATE     16000
#define SND_CHUNK     256

static TaskHandle_t s_task;
static volatile bool s_alert;

typedef struct {
    float f0, f1;        // 起止频率(Hz),f0==f1 为纯音
    int   ms;            // 时长
    int   vol;           // 振幅 0..32767
} snd_seg_t;

static const snd_seg_t SEG_BOOT[] = {
    { 440, 440, 120, 4500 }, { 554, 554, 120, 4500 },
    { 659, 659, 120, 4500 }, { 880, 880, 220, 5500 },
};
static const snd_seg_t SEG_KEY[]     = { { 1800, 1800, 45, 2200 } };
static const snd_seg_t SEG_CHECKIN[] = { { 1046, 1046, 90, 5500 },
                                         { 1568, 1568, 210, 6000 } };
static const snd_seg_t SEG_DENY[]    = { { 240, 180, 320, 6500 } };
static const snd_seg_t SEG_IGNITION[]= { { 320, 55, 800, 9000 } };
static const snd_seg_t SEG_QIANJIN[] = {
    { 523, 523, 80, 5500 }, { 659, 659, 80, 5500 }, { 784, 784, 80, 5500 },
    { 1046, 1046, 80, 6000 }, { 1318, 1318, 260, 6500 },
};
static const snd_seg_t SEG_ALERT_A[] = { { 660, 660, 210, 5500 } };
static const snd_seg_t SEG_ALERT_B[] = { { 880, 880, 210, 5500 } };

// 播放一段(支持扫频)。在 worker 任务内调用。
static void play_segments(const snd_seg_t *segs, int n)
{
    if (bsp_audio_set_format(SND_RATE, 16, 1) != ESP_OK) return;
    bsp_audio_set_volume(70);

    int16_t *buf = malloc(SND_CHUNK * sizeof(int16_t));
    if (!buf) return;

    for (int s = 0; s < n; s++) {
        int total = SND_RATE * segs[s].ms / 1000;
        double phase = 0;
        int done = 0;
        while (done < total) {
            int cnt = total - done;
            if (cnt > SND_CHUNK) cnt = SND_CHUNK;
            for (int i = 0; i < cnt; i++) {
                double frac = (double)(done + i) / (double)total;
                double f = segs[s].f0 + (segs[s].f1 - segs[s].f0) * frac;
                phase += 2.0 * M_PI * f / SND_RATE;
                if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
                // 首尾各 8ms 淡入淡出,避免爆音
                int edge = SND_RATE * 8 / 1000;
                double g = 1.0;
                if (done + i < edge) g = (double)(done + i) / edge;
                if (done + i > total - edge) g = (double)(total - done - i) / edge;
                buf[i] = (int16_t)(sin(phase) * segs[s].vol * g);
            }
            bsp_audio_write(buf, (size_t)cnt * sizeof(int16_t));
            done += cnt;
        }
    }
    free(buf);
}

static void play_snd(we_snd_t snd)
{
    switch (snd) {
    case WE_SND_BOOT:      play_segments(SEG_BOOT, 4); break;
    case WE_SND_KEY:       play_segments(SEG_KEY, 1); break;
    case WE_SND_CHECKIN:   play_segments(SEG_CHECKIN, 2); break;
    case WE_SND_DENY:      play_segments(SEG_DENY, 1); break;
    case WE_SND_IGNITION:  play_segments(SEG_IGNITION, 1); break;
    case WE_SND_QIANJIN:   play_segments(SEG_QIANJIN, 5); break;
    default: break;
    }
}

static void sound_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_alert) {
            // 警报循环:双音交替,每段后检查停止标志。
            play_segments(SEG_ALERT_A, 1);
            if (s_alert) play_segments(SEG_ALERT_B, 1);
            continue;
        }
        uint32_t note = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &note, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (note > 0 && note < 100) play_snd((we_snd_t)note);
        }
    }
}

void we_sound_init(void)
{
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "音频 codec 不可用,音效将静默降级");
    }
    if (xTaskCreate(sound_task, "we_snd", 4096, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "音效任务创建失败");
        s_task = NULL;
    }
}

void we_sound_play(we_snd_t snd)
{
    if (s_task) xTaskNotify(s_task, (uint32_t)snd, eSetValueWithOverwrite);
}

void we_sound_alert_start(void) { s_alert = true; }
void we_sound_alert_stop(void)  { s_alert = false; }

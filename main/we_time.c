#include "we_time.h"

#include "we_config.h"
#include "we_logic.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "we_time";

// SNTP 服务器:优先国内可达的两个,互为备份。
static const char *SNTP_SERVERS[] = {
    "ntp.aliyun.com",
    "cn.pool.ntp.org",
};

static volatile bool s_synced;
static bool s_sntp_started;

void we_time_init(void)
{
    setenv("TZ", WE_TIMEZONE, 1);
    tzset();
}

int64_t we_time_now(void)
{
    return (int64_t)time(NULL);
}

bool we_time_is_valid(void)
{
    return s_synced || we_time_now() > 1700000000;
}

int32_t we_time_day_index(int64_t unix_now)
{
    struct tm tm_buf = { 0 };
    time_t seconds = (time_t)unix_now;
    localtime_r(&seconds, &tm_buf);
    return we_days_from_civil(tm_buf.tm_year + 1900, tm_buf.tm_mon + 1,
                              tm_buf.tm_mday);
}

int32_t we_time_today_index(void)
{
    return we_time_day_index(we_time_now());
}

static void on_sync(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    ESP_LOGI(TAG, "SNTP 对时完成");
}

bool we_time_sync(uint32_t timeout_ms)
{
    if (!s_sntp_started) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        for (size_t i = 0; i < sizeof(SNTP_SERVERS) / sizeof(SNTP_SERVERS[0]); i++)
            esp_sntp_setservername((u8_t)i, (char *)SNTP_SERVERS[i]);
        esp_sntp_set_time_sync_notification_cb(on_sync);
        esp_sntp_init();
        s_sntp_started = true;
    }

    const int64_t deadline_us = (int64_t)timeout_ms * 1000;
    int64_t waited_us = 0;
    while (!s_synced && waited_us < deadline_us) {
        vTaskDelay(pdMS_TO_TICKS(200));
        waited_us += 200000;
    }

    if (!s_synced) {
        ESP_LOGW(TAG, "SNTP 对时超时");
        return false;
    }
    return true;
}

void we_time_set_epoch(int64_t epoch)
{
    if (epoch <= 1700000000) return;
    struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    s_synced = true;
}

static bool local_tm(struct tm *out)
{
    time_t now = (time_t)we_time_now();
    if (now <= 1700000000) return false;
    localtime_r(&now, out);
    return true;
}

void we_time_format_hm(char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    struct tm tm_buf = { 0 };
    if (!local_tm(&tm_buf)) {
        snprintf(out, out_len, "--:--");
        return;
    }
    snprintf(out, out_len, "%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min);
}

void we_time_format_hms(char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    struct tm tm_buf = { 0 };
    if (!local_tm(&tm_buf)) {
        snprintf(out, out_len, "--:--:--");
        return;
    }
    snprintf(out, out_len, "%02d:%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min,
             tm_buf.tm_sec);
}

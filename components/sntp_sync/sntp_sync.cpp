#include "sntp_sync.hpp"
#include "esp_sntp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <sys/time.h>

static const char* TAG = "SntpSync";
static bool s_started = false;

// Reasonable epoch lower bound: 2023-01-01 00:00:00 UTC
static constexpr time_t kMinValidEpoch = 1672531200;

static void on_sync(struct timeval* tv) {
    ESP_LOGI(TAG, "SNTP sync complete — wall time %lld.%06ld",
             (long long)tv->tv_sec, (long)tv->tv_usec);
}

namespace SntpSync {

void start() {
    if (s_started) return;
    s_started = true;

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    sntp_set_time_sync_notification_cb(on_sync);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP client started (pool.ntp.org, time.cloudflare.com)");
}

bool isSynced() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec > kMinValidEpoch;
}

} // namespace SntpSync

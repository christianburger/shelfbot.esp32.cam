#include "sntp_sync.hpp"
#include <idf_c_includes.hpp>
#include "esp_sntp.h"

static const char* TAG = "SntpSync";
static bool s_started = false;

namespace SntpSync {

void start() {
    if (s_started) return;
    s_started = true;

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP client started (pool.ntp.org, time.cloudflare.com)");
}

} // namespace SntpSync

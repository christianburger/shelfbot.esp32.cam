#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# setup_shelfbot_cam_components.sh
#
# Creates the three new components (idf_includes, wifi_manager, microros_sync)
# and updates main/ files inside an existing shelfbot.esp32.cam ESP-IDF project.
#
# Usage:
#   cd /path/to/shelfbot.esp32.cam
#   bash setup_shelfbot_cam_components.sh
#
# The script is idempotent – running it twice will overwrite files but will
# not create duplicate directories.
# -----------------------------------------------------------------------------
set -euo pipefail

ROOT="$(pwd)"

# Verify we look like the right project
if [[ ! -f "$ROOT/CMakeLists.txt" ]]; then
    echo "ERROR: No CMakeLists.txt found in $(pwd)."
    echo "       Run this script from the root of shelfbot.esp32.cam."
    exit 1
fi

echo "==> Creating directory structure..."
mkdir -p "$ROOT/components/idf_includes/include"
mkdir -p "$ROOT/components/wifi_manager/include"
mkdir -p "$ROOT/components/microros_sync/include"
# main/ already exists; just ensure it's there
mkdir -p "$ROOT/main"

# =============================================================================
# components/idf_includes/CMakeLists.txt
# =============================================================================
echo "==> Writing components/idf_includes/CMakeLists.txt"
cat > "$ROOT/components/idf_includes/CMakeLists.txt" << 'EOF'
idf_component_register(
    SRCS ""
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        log
        esp_system
        esp_timer
        esp_common
        esp_event
        esp_netif
        esp_hw_support
        esp_driver_gpio
        esp_wifi
        mdns
        nvs_flash
        lwip
        micro_ros_espidf_component
        esp32-camera
)
EOF

# =============================================================================
# components/idf_includes/include/idf_c_includes.hpp
# =============================================================================
echo "==> Writing components/idf_includes/include/idf_c_includes.hpp"
cat > "$ROOT/components/idf_includes/include/idf_c_includes.hpp" << 'EOF'
#pragma once

// C headers (must be wrapped for C++ consumers)
extern "C" {
// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

// ESP-IDF core
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_psram.h"

// NVS
#include "nvs_flash.h"

// Drivers
#include "driver/gpio.h"

// Protocols
#include "mdns.h"

// Camera
#include "esp_camera.h"

// micro-ROS
#include "rcl/rcl.h"
#include "rclc/rclc.h"
#include "rclc/executor.h"
#include "rmw_microros/rmw_microros.h"
#include "rmw_microros/ping.h"
#include "std_msgs/msg/bool.h"
#include "sensor_msgs/msg/compressed_image.h"
#include "sensor_msgs/msg/camera_info.h"

} // extern "C"

// C++ standard library
#include <functional>
#include <cstdint>
#include <string>
#include <cstring>
#include <cinttypes>
#include <cmath>
#include <ctime>
#include <vector>
#include <array>
#include <memory>
#include <algorithm>
#include <mutex>
EOF

# =============================================================================
# components/wifi_manager/CMakeLists.txt
# =============================================================================
echo "==> Writing components/wifi_manager/CMakeLists.txt"
cat > "$ROOT/components/wifi_manager/CMakeLists.txt" << 'EOF'
idf_component_register(
    SRCS "wifi_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        idf_includes
        mdns
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        freertos
        log
)
EOF

# =============================================================================
# components/wifi_manager/include/wifi_manager.hpp
# =============================================================================
echo "==> Writing components/wifi_manager/include/wifi_manager.hpp"
cat > "$ROOT/components/wifi_manager/include/wifi_manager.hpp" << 'EOF'
#pragma once

#include "idf_c_includes.hpp"

// ---------------------------------------------------------------------------
// Public event-group bits
// ---------------------------------------------------------------------------
#define WM_CONNECTED_BIT    BIT0
#define WM_DISCONNECTED_BIT BIT1

// ---------------------------------------------------------------------------
// State enum
// ---------------------------------------------------------------------------
typedef enum {
    WM_STATE_IDLE       = 0,
    WM_STATE_SCANNING,
    WM_STATE_CONNECTING,
    WM_STATE_CONNECTED,
    WM_STATE_DEGRADED,
} wifi_manager_state_t;

// ---------------------------------------------------------------------------
// Info struct exposed to callers
// ---------------------------------------------------------------------------
typedef struct {
    wifi_manager_state_t state;
    char                 ssid[33];
    int8_t               rssi_dbm;
    char                 ip[16];
    uint32_t             uptime_s;
    bool                 degraded;
    uint32_t             switches;
    uint32_t             reconnects;
} wifi_manager_info_t;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t          wifi_manager_init(void);
void               wifi_manager_get_info(wifi_manager_info_t *out);
EventGroupHandle_t wifi_manager_get_event_group(void);
const char        *wifi_manager_state_str(wifi_manager_state_t s);
EOF

# =============================================================================
# components/wifi_manager/wifi_manager.cpp
# =============================================================================
echo "==> Writing components/wifi_manager/wifi_manager.cpp"
cat > "$ROOT/components/wifi_manager/wifi_manager.cpp" << 'EOF'
// wifi_manager.cpp – ported from shelfbot main firmware for shelfbot.esp32.cam
// Changes vs original:
//   • Removed state_machine / state_machine_lifecycle dependencies
//   • Credentials and tuning supplied via Kconfig (main/Kconfig.projbuild)
//   • No FastAccelStepper, sensor_manager, motor_control, or led_control deps

#include "wifi_manager.hpp"

static const char *TAG = "wifi_manager";

// ---------------------------------------------------------------------------
// Kconfig fallbacks (symbols defined in main/Kconfig.projbuild)
// ---------------------------------------------------------------------------
#ifndef CONFIG_WIFI_SSID_1
#  define CONFIG_WIFI_SSID_1 ""
#endif
#ifndef CONFIG_WIFI_PASS_1
#  define CONFIG_WIFI_PASS_1 ""
#endif
#ifndef CONFIG_WIFI_SSID_2
#  define CONFIG_WIFI_SSID_2 ""
#endif
#ifndef CONFIG_WIFI_PASS_2
#  define CONFIG_WIFI_PASS_2 ""
#endif
#ifndef CONFIG_WIFI_SSID_3
#  define CONFIG_WIFI_SSID_3 ""
#endif
#ifndef CONFIG_WIFI_PASS_3
#  define CONFIG_WIFI_PASS_3 ""
#endif
#ifndef CONFIG_WIFI_SSID_4
#  define CONFIG_WIFI_SSID_4 ""
#endif
#ifndef CONFIG_WIFI_PASS_4
#  define CONFIG_WIFI_PASS_4 ""
#endif

#ifndef CONFIG_WIFI_RSSI_WARN_THRESHOLD
#  define CONFIG_WIFI_RSSI_WARN_THRESHOLD      -75
#endif
#ifndef CONFIG_WIFI_RSSI_CRITICAL_THRESHOLD
#  define CONFIG_WIFI_RSSI_CRITICAL_THRESHOLD  -85
#endif
#ifndef CONFIG_WIFI_DEGRADED_CONFIRM_N
#  define CONFIG_WIFI_DEGRADED_CONFIRM_N       3
#endif
#ifndef CONFIG_WIFI_MONITOR_INTERVAL_MS
#  define CONFIG_WIFI_MONITOR_INTERVAL_MS      5000
#endif
#ifndef CONFIG_WIFI_RETRIES_PER_NETWORK
#  define CONFIG_WIFI_RETRIES_PER_NETWORK      3
#endif
#ifndef CONFIG_WIFI_INTER_CYCLE_DELAY_S
#  define CONFIG_WIFI_INTER_CYCLE_DELAY_S      10
#endif

#define RSSI_WARN       CONFIG_WIFI_RSSI_WARN_THRESHOLD
#define RSSI_CRITICAL   CONFIG_WIFI_RSSI_CRITICAL_THRESHOLD
#define DEGRADE_N       CONFIG_WIFI_DEGRADED_CONFIRM_N
#define MONITOR_MS      CONFIG_WIFI_MONITOR_INTERVAL_MS
#define RETRIES_PER_NET CONFIG_WIFI_RETRIES_PER_NETWORK
#define CYCLE_DELAY_S   CONFIG_WIFI_INTER_CYCLE_DELAY_S

#define CONNECT_TIMEOUT_MS  12000
#define SCAN_MAX_APS        20

#define EVT_GOT_IP       BIT2
#define EVT_DISCONNECTED BIT3

// ---------------------------------------------------------------------------
// Credential table
// ---------------------------------------------------------------------------
struct cred_t { const char *ssid; const char *pass; };

static constexpr cred_t s_creds[] = {
    { CONFIG_WIFI_SSID_1, CONFIG_WIFI_PASS_1 },
    { CONFIG_WIFI_SSID_2, CONFIG_WIFI_PASS_2 },
    { CONFIG_WIFI_SSID_3, CONFIG_WIFI_PASS_3 },
    { CONFIG_WIFI_SSID_4, CONFIG_WIFI_PASS_4 },
};

static constexpr int CRED_COUNT = static_cast<int>(std::size(s_creds));

static int valid_cred_count() {
    int v = 0;
    for (int i = 0; i < CRED_COUNT; i++)
        if (s_creds[i].ssid && s_creds[i].ssid[0] != '\0') v++;
    return v;
}

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------
static EventGroupHandle_t  s_evt      = nullptr;
static esp_netif_t        *s_netif    = nullptr;
static wifi_manager_info_t s_info     = {};
static portMUX_TYPE        s_info_mux = portMUX_INITIALIZER_UNLOCKED;

static int64_t  s_connected_at_us = 0;
static uint32_t s_switches        = 0;
static uint32_t s_reconnects      = 0;
static int      s_degrade_streak  = 0;

static wifi_manager_state_t s_current_state = WM_STATE_IDLE;

static void set_wifi_state(wifi_manager_state_t new_state) {
    if (s_current_state == new_state) return;
    s_current_state = new_state;
    ESP_LOGI(TAG, "State → %s", wifi_manager_state_str(new_state));
    portENTER_CRITICAL(&s_info_mux);
    s_info.state = new_state;
    portEXIT_CRITICAL(&s_info_mux);
}

const char *wifi_manager_state_str(wifi_manager_state_t s) {
    switch (s) {
        case WM_STATE_IDLE:       return "IDLE";
        case WM_STATE_SCANNING:   return "SCANNING";
        case WM_STATE_CONNECTING: return "CONNECTING";
        case WM_STATE_CONNECTED:  return "CONNECTED";
        case WM_STATE_DEGRADED:   return "DEGRADED";
        default:                  return "UNKNOWN";
    }
}

static void publish_info(const char *ssid, int8_t rssi, bool degraded) {
    esp_netif_ip_info_t ip = {};
    if (s_netif) esp_netif_get_ip_info(s_netif, &ip);
    char ip_str[16] = "";
    if (ip.ip.addr) snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip.ip));
    const uint32_t uptime = (s_connected_at_us > 0)
        ? static_cast<uint32_t>((esp_timer_get_time() - s_connected_at_us) / 1000000ULL) : 0;
    portENTER_CRITICAL(&s_info_mux);
    s_info.rssi_dbm   = rssi;
    s_info.degraded   = degraded;
    s_info.uptime_s   = uptime;
    s_info.switches   = s_switches;
    s_info.reconnects = s_reconnects;
    strlcpy(s_info.ip, ip_str, sizeof(s_info.ip));
    if (ssid) strlcpy(s_info.ssid, ssid, sizeof(s_info.ssid));
    portEXIT_CRITICAL(&s_info_mux);
}

// ---------------------------------------------------------------------------
// Wi-Fi event handlers
// ---------------------------------------------------------------------------
static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, const void *data) {
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *d = static_cast<const wifi_event_sta_disconnected_t *>(data);
        ESP_LOGW(TAG, "STA disconnected, reason=%d", d->reason);
        xEventGroupClearBits(s_evt, WM_CONNECTED_BIT);
        xEventGroupSetBits(s_evt, WM_DISCONNECTED_BIT | EVT_DISCONNECTED);
        set_wifi_state(WM_STATE_SCANNING);
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, const void *data) {
    if (id == IP_EVENT_STA_GOT_IP) {
        const auto *e = static_cast<const ip_event_got_ip_t *>(data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupClearBits(s_evt, WM_DISCONNECTED_BIT | EVT_DISCONNECTED);
        xEventGroupSetBits(s_evt, WM_CONNECTED_BIT | EVT_GOT_IP);
        set_wifi_state(WM_STATE_CONNECTED);
    }
}

// ---------------------------------------------------------------------------
// Scan: pick the strongest visible known AP
// ---------------------------------------------------------------------------
static int scan_pick_best() {
    constexpr wifi_scan_config_t cfg = {
        .ssid        = nullptr, .bssid       = nullptr, .channel     = 0,
        .show_hidden = false,   .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time   = {
            .active  = { .min = WIFI_ACTIVE_SCAN_MIN_DEFAULT_TIME,
                         .max = WIFI_ACTIVE_SCAN_MAX_DEFAULT_TIME },
            .passive = WIFI_PASSIVE_SCAN_DEFAULT_TIME },
        .home_chan_dwell_time = WIFI_SCAN_HOME_CHANNEL_DWELL_DEFAULT_TIME,
        .channel_bitmap       = { .ghz_2_channels = 0, .ghz_5_channels = 0 }
    };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) { ESP_LOGE(TAG, "scan_start failed"); return -1; }
    uint16_t count = SCAN_MAX_APS;
    wifi_ap_record_t aps[SCAN_MAX_APS] = {};
    esp_wifi_scan_get_ap_records(&count, aps);
    ESP_LOGI(TAG, "scan: %u APs found", (unsigned)count);
    int best_cred = -1; int8_t best_rssi = INT8_MIN;
    for (int ci = 0; ci < CRED_COUNT; ci++) {
        if (!s_creds[ci].ssid || s_creds[ci].ssid[0] == '\0') continue;
        for (uint16_t ai = 0; ai < count; ai++) {
            if (strcmp(reinterpret_cast<const char *>(aps[ai].ssid), s_creds[ci].ssid) == 0) {
                ESP_LOGI(TAG, "  found \"%s\" ch%d rssi=%d",
                         reinterpret_cast<const char *>(aps[ai].ssid), aps[ai].primary, aps[ai].rssi);
                if (aps[ai].rssi > best_rssi) { best_rssi = aps[ai].rssi; best_cred = ci; }
            }
        }
    }
    if (best_cred >= 0) ESP_LOGI(TAG, "scan winner: \"%s\" rssi=%d dBm", s_creds[best_cred].ssid, best_rssi);
    else                ESP_LOGW(TAG, "scan: no known AP visible");
    return best_cred;
}

// ---------------------------------------------------------------------------
// Connect helpers
// ---------------------------------------------------------------------------
static void disconnect_blocking() {
    xEventGroupClearBits(s_evt, EVT_DISCONNECTED | EVT_GOT_IP);
    esp_wifi_disconnect();
    xEventGroupWaitBits(s_evt, EVT_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
    xEventGroupClearBits(s_evt, EVT_DISCONNECTED | EVT_GOT_IP);
}

static esp_err_t start_connect(int ci) {
    wifi_config_t cfg = {};
    strlcpy(reinterpret_cast<char *>(cfg.sta.ssid),     s_creds[ci].ssid, sizeof(cfg.sta.ssid));
    const char *pass = s_creds[ci].pass ? s_creds[ci].pass : "";
    strlcpy(reinterpret_cast<char *>(cfg.sta.password), pass,             sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = (pass[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    ESP_LOGI(TAG, "connecting → \"%s\"", s_creds[ci].ssid);
    esp_err_t r = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (r == ESP_OK) r = esp_wifi_connect();
    return r;
}

static bool monitor_rssi() {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    const bool degraded = (ap.rssi < RSSI_WARN);
    if (!degraded) { if (s_degrade_streak > 0) ESP_LOGI(TAG, "RSSI recovered: %d dBm", ap.rssi);
                     s_degrade_streak = 0; publish_info(nullptr, ap.rssi, false); return false; }
    s_degrade_streak++;
    ESP_LOGW(TAG, "RSSI %d dBm streak=%d/%d", ap.rssi, s_degrade_streak, DEGRADE_N);
    publish_info(nullptr, ap.rssi, true);
    if (ap.rssi < RSSI_CRITICAL) { ESP_LOGE(TAG, "RSSI critical – immediate scan"); s_degrade_streak = 0; return true; }
    if (s_degrade_streak >= DEGRADE_N) { s_degrade_streak = 0; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Manager task
// ---------------------------------------------------------------------------
[[noreturn]] static void manager_task(void *) {
    ESP_LOGI(TAG, "manager task started, %d slot(s), %d configured", CRED_COUNT, valid_cred_count());
    bool no_creds_logged = false;
    int  ci = -1; bool skip_scan = false;

    while (true) {
        if (!skip_scan) {
            if (valid_cred_count() == 0) {
                if (!no_creds_logged) { ESP_LOGE(TAG, "No Wi-Fi SSIDs configured. Set CONFIG_WIFI_SSID_1 in menuconfig."); no_creds_logged = true; }
                set_wifi_state(WM_STATE_IDLE); vTaskDelay(pdMS_TO_TICKS(CYCLE_DELAY_S * 1000)); continue;
            }
            no_creds_logged = false; set_wifi_state(WM_STATE_SCANNING); publish_info(nullptr, 0, false);
            ci = scan_pick_best();
            if (ci < 0) { ESP_LOGW(TAG, "No known AP, waiting %d s", CYCLE_DELAY_S); vTaskDelay(pdMS_TO_TICKS(CYCLE_DELAY_S * 1000)); continue; }
        }
        skip_scan = false;

        set_wifi_state(WM_STATE_CONNECTING); bool connected = false; s_reconnects++;
        for (int attempt = 1; attempt <= RETRIES_PER_NET && !connected; attempt++) {
            disconnect_blocking();
            if (start_connect(ci) != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
            const EventBits_t bits = xEventGroupWaitBits(s_evt, EVT_GOT_IP | EVT_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
            if (bits & EVT_GOT_IP) connected = true;
            else { ESP_LOGW(TAG, "\"%s\" attempt %d/%d failed", s_creds[ci].ssid, attempt, RETRIES_PER_NET); vTaskDelay(pdMS_TO_TICKS(1500)); }
        }
        if (!connected) { ESP_LOGW(TAG, "\"%s\" unreachable – rescanning", s_creds[ci].ssid); vTaskDelay(pdMS_TO_TICKS(3000)); continue; }

        s_connected_at_us = esp_timer_get_time(); s_degrade_streak = 0;
        publish_info(s_creds[ci].ssid, 0, false);
        ESP_LOGI(TAG, "connected to \"%s\" (reconnect #%lu)", s_creds[ci].ssid, (unsigned long)s_reconnects);

        while (true) {
            const EventBits_t bits = xEventGroupWaitBits(s_evt, EVT_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(MONITOR_MS));
            if (bits & EVT_DISCONNECTED) { ESP_LOGW(TAG, "connection lost – rescanning"); s_connected_at_us = 0; set_wifi_state(WM_STATE_SCANNING); break; }
            if (monitor_rssi()) {
                const int better = scan_pick_best();
                if (better < 0) { ESP_LOGW(TAG, "degraded but no alternatives – staying"); continue; }
                if (better == ci) { ESP_LOGI(TAG, "\"%s\" is still best – staying", s_creds[ci].ssid); continue; }
                s_switches++;
                ESP_LOGI(TAG, "switching \"%s\" → \"%s\" (switch #%lu)", s_creds[ci].ssid, s_creds[better].ssid, (unsigned long)s_switches);
                ci = better; skip_scan = true; s_connected_at_us = 0;
                xEventGroupClearBits(s_evt, WM_CONNECTED_BIT); xEventGroupSetBits(s_evt, WM_DISCONNECTED_BIT); break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t wifi_manager_init(void) {
    s_evt = xEventGroupCreate();
    if (!s_evt) return ESP_ERR_NO_MEM;
    xEventGroupSetBits(s_evt, WM_DISCONNECTED_BIT);
    s_netif = esp_netif_create_default_wifi_sta();
    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(84));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
        reinterpret_cast<esp_event_handler_t>(on_wifi_event), nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
        reinterpret_cast<esp_event_handler_t>(on_ip_event), nullptr));
    set_wifi_state(WM_STATE_IDLE);
    const BaseType_t r = xTaskCreatePinnedToCore(manager_task, "wifi_mgr", 4096, nullptr,
        configMAX_PRIORITIES - 2, nullptr, 1);
    if (r != pdPASS) { ESP_LOGE(TAG, "failed to create manager task"); return ESP_FAIL; }
    return ESP_OK;
}

void wifi_manager_get_info(wifi_manager_info_t *out) {
    portENTER_CRITICAL(&s_info_mux); *out = s_info; portEXIT_CRITICAL(&s_info_mux);
}

EventGroupHandle_t wifi_manager_get_event_group(void) { return s_evt; }
EOF

# =============================================================================
# components/microros_sync/CMakeLists.txt
# =============================================================================
echo "==> Writing components/microros_sync/CMakeLists.txt"
cat > "$ROOT/components/microros_sync/CMakeLists.txt" << 'EOF'
idf_component_register(
    SRCS "microros_sync.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        idf_includes
        micro_ros_espidf_component
        wifi_manager
        esp32-camera
        esp_driver_gpio
        freertos
        log
        esp_timer
)
EOF

# =============================================================================
# components/microros_sync/include/microros_sync.hpp
# =============================================================================
echo "==> Writing components/microros_sync/include/microros_sync.hpp"
cat > "$ROOT/components/microros_sync/include/microros_sync.hpp" << 'EOF'
#pragma once

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// MicrorosSync – camera edition
//
// Publishers:
//   /camera/image_raw/compressed   sensor_msgs/CompressedImage  (on demand)
//   /camera/camera_info            sensor_msgs/CameraInfo       (1 Hz timer)
//
// Subscriber:
//   /camera/led                    std_msgs/Bool
//     → drives GPIO CONFIG_CAMERA_LED_GPIO (default 4, ESP32-CAM flash)
// ---------------------------------------------------------------------------
class MicrorosSync {
public:
    static MicrorosSync &getInstance();

    bool init();
    void start();

    // Call from camera_task after esp_camera_fb_get() succeeds.
    static void publishCompressedImage(const uint8_t *buf, size_t len,
                                       uint32_t frame_seq);

private:
    MicrorosSync();
    ~MicrorosSync();
};
EOF

# =============================================================================
# components/microros_sync/microros_sync.cpp
# =============================================================================
echo "==> Writing components/microros_sync/microros_sync.cpp"
cat > "$ROOT/components/microros_sync/microros_sync.cpp" << 'EOF'
// microros_sync.cpp – camera edition for shelfbot.esp32.cam
//
// Ported from the main shelfbot firmware's microros_sync component.
// Removed: motor/sensor/LED-control deps, state_machine, firmware_version.
// Added:   CompressedImage publisher, CameraInfo publisher, LED GPIO sub.

#include "microros_sync.hpp"
#include "idf_c_includes.hpp"
#include "wifi_manager.hpp"

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <std_msgs/msg/bool.h>
#include <sensor_msgs/msg/compressed_image.h>
#include <sensor_msgs/msg/camera_info.h>
#include <builtin_interfaces/msg/time.h>

static const char *TAG = "MicrorosSync";

// ---------------------------------------------------------------------------
// Kconfig fallbacks
// ---------------------------------------------------------------------------
#ifndef CONFIG_MICROROS_AGENT_MDNS_HOST
#  define CONFIG_MICROROS_AGENT_MDNS_HOST "shelfbot-agent"
#endif
#ifndef CONFIG_MICROROS_AGENT_IP
#  define CONFIG_MICROROS_AGENT_IP        ""
#endif
#ifndef CONFIG_MICROROS_AGENT_PORT
#  define CONFIG_MICROROS_AGENT_PORT      "8888"
#endif
#ifndef CONFIG_CAMERA_LED_GPIO
#  define CONFIG_CAMERA_LED_GPIO          4
#endif

#define IMAGE_BUF_BYTES (80 * 1024)

#define ROS_CHECK(call, msg) do { \
    rcl_ret_t _r = (call); \
    if (_r != RCL_RET_OK) { \
        ESP_LOGE(TAG, "%s failed: %ld (%s)", (msg), (long)_r, \
                 rcl_get_error_string().str); \
        rcl_reset_error(); \
    } \
} while (0)

// ---------------------------------------------------------------------------
// Implementation struct
// ---------------------------------------------------------------------------
struct Impl {
    rcl_node_t         node;
    rcl_allocator_t    allocator;
    rclc_support_t     support;
    rclc_executor_t    executor;

    rcl_publisher_t    image_pub;
    rcl_publisher_t    caminfo_pub;
    rcl_subscription_t led_sub;
    rcl_timer_t        caminfo_timer;

    sensor_msgs__msg__CompressedImage image_msg;
    sensor_msgs__msg__CameraInfo      caminfo_msg;
    std_msgs__msg__Bool               led_msg;

    uint8_t           *image_data;
    SemaphoreHandle_t  frame_mutex;
    SemaphoreHandle_t  frame_pending;
    TaskHandle_t       task_handle;
    bool               entities_created;
    SemaphoreHandle_t  entity_mutex;

    Impl()
        : node(rcl_get_zero_initialized_node()),
          allocator(rcl_get_default_allocator()),
          support(),
          executor(rclc_executor_get_zero_initialized_executor()),
          image_pub(rcl_get_zero_initialized_publisher()),
          caminfo_pub(rcl_get_zero_initialized_publisher()),
          led_sub(rcl_get_zero_initialized_subscription()),
          caminfo_timer(rcl_get_zero_initialized_timer()),
          image_data(nullptr),
          frame_mutex(nullptr), frame_pending(nullptr),
          task_handle(nullptr), entities_created(false), entity_mutex(nullptr)
    {
        sensor_msgs__msg__CompressedImage__init(&image_msg);
        sensor_msgs__msg__CameraInfo__init(&caminfo_msg);
        std_msgs__msg__Bool__init(&led_msg);

        image_data = static_cast<uint8_t *>(
            heap_caps_malloc(IMAGE_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!image_data)
            image_data = static_cast<uint8_t *>(malloc(IMAGE_BUF_BYTES));

        image_msg.data.data     = image_data;
        image_msg.data.capacity = IMAGE_BUF_BYTES;
        image_msg.data.size     = 0;

        static char fmt[]      = "jpeg";
        image_msg.format.data     = fmt;
        image_msg.format.size     = strlen(fmt);
        image_msg.format.capacity = sizeof(fmt);

        static char img_frame_id[] = "camera_optical_frame";
        image_msg.header.frame_id.data     = img_frame_id;
        image_msg.header.frame_id.size     = strlen(img_frame_id);
        image_msg.header.frame_id.capacity = sizeof(img_frame_id);

        // Placeholder CameraInfo (replace with real calibration values)
        caminfo_msg.width  = 800;
        caminfo_msg.height = 600;
        static char dist_model[] = "plumb_bob";
        caminfo_msg.distortion_model.data     = dist_model;
        caminfo_msg.distortion_model.size     = strlen(dist_model);
        caminfo_msg.distortion_model.capacity = sizeof(dist_model);

        static double D[5] = {0, 0, 0, 0, 0};
        caminfo_msg.d.data = D; caminfo_msg.d.size = 5; caminfo_msg.d.capacity = 5;

        // K – intrinsic (fx=fy=500, cx=400, cy=300)
        caminfo_msg.k[0] = 500.0; caminfo_msg.k[2] = 400.0;
        caminfo_msg.k[4] = 500.0; caminfo_msg.k[5] = 300.0;
        caminfo_msg.k[8] = 1.0;

        // R – identity
        caminfo_msg.r[0] = 1; caminfo_msg.r[4] = 1; caminfo_msg.r[8] = 1;

        // P – projection
        caminfo_msg.p[0]  = 500.0; caminfo_msg.p[2]  = 400.0;
        caminfo_msg.p[5]  = 500.0; caminfo_msg.p[6]  = 300.0;
        caminfo_msg.p[10] = 1.0;

        static char ci_frame_id[] = "camera_optical_frame";
        caminfo_msg.header.frame_id.data     = ci_frame_id;
        caminfo_msg.header.frame_id.size     = strlen(ci_frame_id);
        caminfo_msg.header.frame_id.capacity = sizeof(ci_frame_id);

        frame_mutex   = xSemaphoreCreateMutex();
        frame_pending = xSemaphoreCreateBinary();
        entity_mutex  = xSemaphoreCreateMutex();
    }

    ~Impl() {
        if (frame_mutex)   vSemaphoreDelete(frame_mutex);
        if (frame_pending) vSemaphoreDelete(frame_pending);
        if (entity_mutex)  vSemaphoreDelete(entity_mutex);
        if (task_handle)   vTaskDelete(task_handle);
        free(image_data);
    }
};

static Impl *g_impl = nullptr;

static inline bool lock_entity() {
    return g_impl && xSemaphoreTake(g_impl->entity_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
}
static inline void unlock_entity() { if (g_impl) xSemaphoreGive(g_impl->entity_mutex); }

// ---------------------------------------------------------------------------
// LED GPIO
// ---------------------------------------------------------------------------
static void led_init() {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << CONFIG_CAMERA_LED_GPIO);
    io.mode         = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(static_cast<gpio_num_t>(CONFIG_CAMERA_LED_GPIO), 0);
}

static void led_set(bool on) {
    gpio_set_level(static_cast<gpio_num_t>(CONFIG_CAMERA_LED_GPIO), on ? 1 : 0);
    ESP_LOGI(TAG, "Flash LED %s", on ? "ON" : "OFF");
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
static void caminfo_timer_cb(rcl_timer_t *, int64_t) {
    if (!lock_entity()) return;
    if (g_impl->entities_created) {
        int64_t now_us = esp_timer_get_time();
        g_impl->caminfo_msg.header.stamp.sec     = static_cast<int32_t>(now_us / 1000000);
        g_impl->caminfo_msg.header.stamp.nanosec = static_cast<uint32_t>((now_us % 1000000) * 1000);
        ROS_CHECK(rcl_publish(&g_impl->caminfo_pub, &g_impl->caminfo_msg, NULL), "caminfo publish");
    }
    unlock_entity();
}

static void led_cb(const void *msg) {
    led_set(static_cast<const std_msgs__msg__Bool *>(msg)->data);
}

// ---------------------------------------------------------------------------
// Entity creation / destruction
// ---------------------------------------------------------------------------
static void create_entities(Impl &impl) {
    ESP_LOGI(TAG, "Creating micro-ROS entities");
    ROS_CHECK(rclc_node_init_default(&impl.node, "shelfbot_camera", "", &impl.support), "node init");

    ROS_CHECK(rclc_publisher_init_default(&impl.image_pub, &impl.node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, CompressedImage),
        "/camera/image_raw/compressed"), "image_pub init");

    ROS_CHECK(rclc_publisher_init_default(&impl.caminfo_pub, &impl.node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, CameraInfo),
        "/camera/camera_info"), "caminfo_pub init");

    ROS_CHECK(rclc_subscription_init_default(&impl.led_sub, &impl.node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Bool),
        "/camera/led"), "led_sub init");

    ROS_CHECK(rclc_timer_init_default(&impl.caminfo_timer, &impl.support,
        RCL_MS_TO_NS(1000), caminfo_timer_cb), "caminfo timer init");

    ROS_CHECK(rclc_executor_init(&impl.executor, &impl.support.context, 2, &impl.allocator), "executor init");
    ROS_CHECK(rclc_executor_add_timer(&impl.executor, &impl.caminfo_timer), "add caminfo timer");
    ROS_CHECK(rclc_executor_add_subscription(&impl.executor, &impl.led_sub,
        &impl.led_msg, led_cb, ON_NEW_DATA), "add led sub");

    ESP_LOGI(TAG, "Entities created");
}

static void destroy_entities(Impl &impl) {
    ESP_LOGI(TAG, "Destroying entities");
    ROS_CHECK(rcl_publisher_fini(&impl.image_pub,   &impl.node), "image_pub fini");
    ROS_CHECK(rcl_publisher_fini(&impl.caminfo_pub, &impl.node), "caminfo_pub fini");
    ROS_CHECK(rcl_subscription_fini(&impl.led_sub,  &impl.node), "led_sub fini");
    ROS_CHECK(rcl_timer_fini(&impl.caminfo_timer),               "caminfo timer fini");
    ROS_CHECK(rclc_executor_fini(&impl.executor),                "executor fini");
    ROS_CHECK(rcl_node_fini(&impl.node),                         "node fini");
    ROS_CHECK(rclc_support_fini(&impl.support),                  "support fini");
}

// ---------------------------------------------------------------------------
// mDNS agent discovery
// ---------------------------------------------------------------------------
static bool query_mdns_host(const char *host_name, char *out_ip, size_t len) {
    ESP_LOGI(TAG, "mDNS query: %s.local", host_name);
    esp_ip4_addr_t addr = { 0 };
    if (mdns_query_a(host_name, 2000, &addr) != ESP_OK) {
        ESP_LOGW(TAG, "mDNS failed – trying static IP fallback");
        return false;
    }
    snprintf(out_ip, len, IPSTR, IP2STR(&addr));
    ESP_LOGI(TAG, "Agent IP (mDNS): %s", out_ip);
    return true;
}

// ---------------------------------------------------------------------------
// micro-ROS task
// ---------------------------------------------------------------------------
static void microros_task(void *arg) {
    auto *impl = static_cast<Impl *>(arg);
    if (!impl) vTaskDelete(nullptr);

    enum class State { WAIT_WIFI, DISCOVER, INIT, CONNECTED, BACKOFF } state = State::WAIT_WIFI;

    uint32_t backoff_ms = 250;
    const uint32_t MAX_BACKOFF_MS = 8000;
    uint8_t  spin_failures = 0;
    char     agent_ip[16]  = {};
    rcl_init_options_t init_opts = rcl_get_zero_initialized_init_options();

    while (true) {
        switch (state) {

        case State::WAIT_WIFI:
            ESP_LOGI(TAG, "Waiting for Wi-Fi...");
            xEventGroupWaitBits(wifi_manager_get_event_group(),
                                WM_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
            ESP_LOGI(TAG, "Wi-Fi up – starting mDNS");
            mdns_init();
            mdns_hostname_set("shelfbot-cam");
            mdns_instance_name_set("Shelfbot Camera Node");
            state = State::DISCOVER;
            break;

        case State::DISCOVER:
            if (query_mdns_host(CONFIG_MICROROS_AGENT_MDNS_HOST, agent_ip, sizeof(agent_ip))) {
                state = State::INIT;
            } else {
                const char *sip = CONFIG_MICROROS_AGENT_IP;
                if (sip && sip[0] != '\0') {
                    strlcpy(agent_ip, sip, sizeof(agent_ip));
                    ESP_LOGI(TAG, "Using static agent IP: %s", agent_ip);
                    state = State::INIT;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
            }
            break;

        case State::INIT:
            init_opts = rcl_get_zero_initialized_init_options();
            if (rcl_init_options_init(&init_opts, impl->allocator) != RCL_RET_OK) {
                ESP_LOGE(TAG, "rcl_init_options_init failed"); rcl_reset_error();
                state = State::BACKOFF; break;
            }
            rmw_uros_options_set_udp_address(agent_ip, CONFIG_MICROROS_AGENT_PORT,
                rcl_init_options_get_rmw_init_options(&init_opts));
            if (rclc_support_init_with_options(&impl->support, 0, NULL,
                                               &init_opts, &impl->allocator) == RCL_RET_OK) {
                if (lock_entity()) { create_entities(*impl); impl->entities_created = true; unlock_entity(); }
                spin_failures = 0; backoff_ms = 250; state = State::CONNECTED;
            } else {
                ESP_LOGE(TAG, "rclc_support_init_with_options failed"); rcl_reset_error();
                state = State::BACKOFF;
            }
            ROS_CHECK(rcl_init_options_fini(&init_opts), "init_opts fini");
            break;

        case State::CONNECTED:
            if (rclc_executor_spin_some(&impl->executor, RCL_MS_TO_NS(50)) != RCL_RET_OK) {
                if (++spin_failures >= 5) {
                    ESP_LOGW(TAG, "5 spin failures – disconnecting");
                    if (lock_entity()) { destroy_entities(*impl); impl->entities_created = false; unlock_entity(); }
                    state = State::BACKOFF;
                }
            } else { spin_failures = 0; }

            if (xSemaphoreTake(impl->frame_pending, 0) == pdTRUE) {
                if (lock_entity()) {
                    if (impl->entities_created)
                        ROS_CHECK(rcl_publish(&impl->image_pub, &impl->image_msg, NULL), "image publish");
                    unlock_entity();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            break;

        case State::BACKOFF:
            ESP_LOGW(TAG, "backing off %lu ms", (unsigned long)backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            backoff_ms = std::min(MAX_BACKOFF_MS, backoff_ms * 2);
            state = State::DISCOVER;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// publishCompressedImage – called from camera_task (Core 0)
// ---------------------------------------------------------------------------
void MicrorosSync::publishCompressedImage(const uint8_t *buf, size_t len, uint32_t frame_seq) {
    if (!g_impl || !g_impl->image_data) return;
    if (len > IMAGE_BUF_BYTES) {
        ESP_LOGW(TAG, "Frame too large (%u B), dropping", (unsigned)len); return;
    }
    if (xSemaphoreTake(g_impl->frame_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        ESP_LOGW(TAG, "publishCompressedImage: mutex timeout, dropping"); return;
    }
    memcpy(g_impl->image_data, buf, len);
    g_impl->image_msg.data.size = len;
    int64_t now_us = esp_timer_get_time();
    g_impl->image_msg.header.stamp.sec     = static_cast<int32_t>(now_us / 1000000);
    g_impl->image_msg.header.stamp.nanosec = static_cast<uint32_t>((now_us % 1000000) * 1000);
    (void)frame_seq;
    xSemaphoreGive(g_impl->frame_mutex);
    xSemaphoreGive(g_impl->frame_pending); // signal; only latest frame matters
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
MicrorosSync::MicrorosSync()  { g_impl = new Impl(); }
MicrorosSync::~MicrorosSync() { delete g_impl; g_impl = nullptr; }

MicrorosSync &MicrorosSync::getInstance() { static MicrorosSync instance; return instance; }

bool MicrorosSync::init() {
    led_init();
    ESP_LOGI(TAG, "MicrorosSync (camera) initialised");
    return (g_impl != nullptr && g_impl->image_data != nullptr);
}

void MicrorosSync::start() {
    if (!g_impl || g_impl->task_handle) return;
    xTaskCreatePinnedToCore(microros_task, "microros_task", 16000, g_impl,
                            configMAX_PRIORITIES - 3, &g_impl->task_handle, 0);
    ESP_LOGI(TAG, "MicrorosSync task started");
}
EOF

# =============================================================================
# main/Kconfig.projbuild
# =============================================================================
echo "==> Writing main/Kconfig.projbuild"
cat > "$ROOT/main/Kconfig.projbuild" << 'EOF'
menu "Shelfbot Camera Configuration"

    menu "Wi-Fi"
        config WIFI_SSID_1
            string "Primary Wi-Fi SSID"
            default ""
        config WIFI_PASS_1
            string "Primary Wi-Fi Password"
            default ""
        config WIFI_SSID_2
            string "Fallback Wi-Fi SSID #2"
            default ""
        config WIFI_PASS_2
            string "Fallback Wi-Fi Password #2"
            default ""
        config WIFI_SSID_3
            string "Fallback Wi-Fi SSID #3"
            default ""
        config WIFI_PASS_3
            string "Fallback Wi-Fi Password #3"
            default ""
        config WIFI_SSID_4
            string "Fallback Wi-Fi SSID #4"
            default ""
        config WIFI_PASS_4
            string "Fallback Wi-Fi Password #4"
            default ""
        config WIFI_RSSI_WARN_THRESHOLD
            int "RSSI warn threshold (dBm)"
            default -75
        config WIFI_RSSI_CRITICAL_THRESHOLD
            int "RSSI critical threshold (dBm)"
            default -85
        config WIFI_DEGRADED_CONFIRM_N
            int "Consecutive degraded samples before roaming"
            default 3
        config WIFI_MONITOR_INTERVAL_MS
            int "RSSI monitor interval (ms)"
            default 5000
        config WIFI_RETRIES_PER_NETWORK
            int "Connection attempts per network before moving on"
            default 3
        config WIFI_INTER_CYCLE_DELAY_S
            int "Delay between scan cycles when no AP found (s)"
            default 10
    endmenu

    menu "micro-ROS Agent"
        config MICROROS_AGENT_MDNS_HOST
            string "Agent mDNS hostname (without .local)"
            default "shelfbot-agent"
        config MICROROS_AGENT_IP
            string "Agent static IP (fallback if mDNS fails)"
            default ""
        config MICROROS_AGENT_PORT
            string "Agent UDP port"
            default "8888"
    endmenu

    menu "Camera"
        config CAMERA_LED_GPIO
            int "Flash LED GPIO number"
            default 4
            help
                GPIO for the onboard flash LED (GPIO 4 on AI-Thinker ESP32-CAM).
                Controlled via /camera/led ROS topic.
    endmenu

endmenu
EOF

# =============================================================================
# main/microros_sync_c.h
# =============================================================================
echo "==> Writing main/microros_sync_c.h"
cat > "$ROOT/main/microros_sync_c.h" << 'EOF'
#pragma once

/*
 * C-callable shim for MicrorosSync.
 * Include this from .c files; implementation is in microros_sync_c.cpp.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

void      microros_init(void);
void      microros_start(void);
void      microros_publish_image(const uint8_t *buf, size_t len, uint32_t seq);
esp_err_t wifi_manager_init(void);

#ifdef __cplusplus
}
#endif
EOF

# =============================================================================
# main/microros_sync_c.cpp
# =============================================================================
echo "==> Writing main/microros_sync_c.cpp"
cat > "$ROOT/main/microros_sync_c.cpp" << 'EOF'
/*
 * microros_sync_c.cpp
 * Thin C-linkage wrappers around the MicrorosSync C++ singleton.
 * Compiled as C++ but callable from plain C (main.c).
 */

#include "microros_sync_c.h"
#include "microros_sync.hpp"
#include "wifi_manager.hpp"

extern "C" {

void microros_init(void) {
    MicrorosSync::getInstance().init();
}

void microros_start(void) {
    MicrorosSync::getInstance().start();
}

void microros_publish_image(const uint8_t *buf, size_t len, uint32_t seq) {
    MicrorosSync::publishCompressedImage(buf, len, seq);
}

} // extern "C"
EOF

# =============================================================================
# main/CMakeLists.txt  (replaces the existing one)
# =============================================================================
echo "==> Writing main/CMakeLists.txt"
cat > "$ROOT/main/CMakeLists.txt" << 'EOF'
idf_component_register(
    SRCS "main.c" "microros_sync_c.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp32-camera
        esp_system
        freertos
        nvs_flash
        esp_wifi
        esp_event
        esp_timer
        esp_psram
        idf_includes
        wifi_manager
        microros_sync
        micro_ros_espidf_component
)
EOF

# =============================================================================
# main/main.c  (replaces the existing one)
# =============================================================================
echo "==> Writing main/main.c"
cat > "$ROOT/main/main.c" << 'EOF'
/*
 * main.c – shelfbot.esp32.cam
 *
 * Task layout:
 *   Core 0  camera_task      captures frames → publishCompressedImage()
 *   Core 0  microros_task    micro-ROS executor (spawned by MicrorosSync::start)
 *   Core 1  wifi_mgr         multi-AP Wi-Fi manager (spawned by wifi_manager_init)
 *
 * Removed: network_manager, controller (HTTP server), frame_queue,
 *          frame_ready_semaphore, shelfbot_camera_task, process_frame_task.
 */

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "microros_sync_c.h"

static const char *TAG = "main";

// ---------------------------------------------------------------------------
// Camera pin map – ESP32-CAM AI-Thinker module
// ---------------------------------------------------------------------------
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

static const camera_config_t camera_config = {
    .pin_pwdn      = CAM_PIN_PWDN,
    .pin_reset     = CAM_PIN_RESET,
    .pin_xclk      = CAM_PIN_XCLK,
    .pin_sccb_sda  = CAM_PIN_SIOD,
    .pin_sccb_scl  = CAM_PIN_SIOC,
    .pin_d7        = CAM_PIN_D7,
    .pin_d6        = CAM_PIN_D6,
    .pin_d5        = CAM_PIN_D5,
    .pin_d4        = CAM_PIN_D4,
    .pin_d3        = CAM_PIN_D3,
    .pin_d2        = CAM_PIN_D2,
    .pin_d1        = CAM_PIN_D1,
    .pin_d0        = CAM_PIN_D0,
    .pin_vsync     = CAM_PIN_VSYNC,
    .pin_href      = CAM_PIN_HREF,
    .pin_pclk      = CAM_PIN_PCLK,
    .xclk_freq_hz  = 20000000,
    .ledc_timer    = LEDC_TIMER_0,
    .ledc_channel  = LEDC_CHANNEL_0,
    .pixel_format  = PIXFORMAT_JPEG,
    .frame_size    = FRAMESIZE_SVGA,   // 800x600
    .jpeg_quality  = 12,
    .fb_count      = 1,
};

// ---------------------------------------------------------------------------
// Camera task – ~10 fps
// ---------------------------------------------------------------------------
static void camera_task(void *pvParameters) {
    ESP_LOGI(TAG, "Initialising camera...");
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Camera ready");

    uint32_t seq = 0;
    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            ESP_LOGD(TAG, "Frame %lu: %dx%d %zu B",
                     (unsigned long)seq, fb->width, fb->height, fb->len);
            microros_publish_image(fb->buf, fb->len, seq++);
            esp_camera_fb_return(fb);
        } else {
            ESP_LOGE(TAG, "esp_camera_fb_get() failed");
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // ~10 fps
    }
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------
void app_main(void) {
    // 1. NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. TCP/IP stack + default event loop (called once here; wifi_manager
    //    uses these but does not re-initialise them)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. Wi-Fi manager (spawns wifi_mgr task on Core 1)
    ESP_ERROR_CHECK(wifi_manager_init());

    // 4. micro-ROS sync (spawns microros_task on Core 0)
    microros_init();
    microros_start();

    // 5. Camera task (Core 0)
    xTaskCreatePinnedToCore(
        camera_task, "camera_task",
        8192, NULL,
        configMAX_PRIORITIES - 2,
        NULL, 0);

    ESP_LOGI(TAG, "app_main done – all tasks running");
}
EOF

# =============================================================================
# Summary
# =============================================================================
echo ""
echo "=================================================================="
echo " Done. Files written:"
echo "=================================================================="
find "$ROOT/components/idf_includes" \
     "$ROOT/components/wifi_manager" \
     "$ROOT/components/microros_sync" \
     "$ROOT/main/Kconfig.projbuild" \
     "$ROOT/main/microros_sync_c.h" \
     "$ROOT/main/microros_sync_c.cpp" \
     "$ROOT/main/CMakeLists.txt" \
     "$ROOT/main/main.c" \
     -type f | sort | sed 's|^'"$ROOT"'/|  |'
echo ""
echo "Next steps:"
echo "  1. cd $(basename $ROOT)"
echo "  2. git clone https://github.com/micro-ROS/micro_ros_espidf_component.git \\"
echo "         components/micro_ros_espidf_component   # if not already present"
echo "  3. idf.py menuconfig   # set Wi-Fi SSID/pass + agent IP under"
echo "                         # 'Shelfbot Camera Configuration'"
echo "  4. idf.py build flash monitor"
echo "=================================================================="

#include <idf_c_includes.hpp>
#include <wifi_manager.hpp>
#include <microros_sync.hpp>
#include <state_machine.hpp>
#include <state_machine_lifecycle.hpp>
#include <shelfbot_timestamp.hpp>

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <sensor_msgs/msg/compressed_image.h>

static const char* TAG = "MicrorosSync";

#define ROS_CHECK(call, msg) do { \
    rcl_ret_t _ret = (call); \
    if (_ret != RCL_RET_OK) { \
        ESP_LOGE(TAG, "%s failed: %ld (%s)", msg, (long)_ret, rcl_get_error_string().str); \
        rcl_reset_error(); \
    } \
} while(0)

// ---------------------------------------------------------------------------
// Time-sync configuration
// ---------------------------------------------------------------------------
static constexpr int32_t TIME_SYNC_TIMEOUT_MS  = 5000;
static constexpr uint8_t TIME_SYNC_MAX_ATTEMPTS = 3;

// How long to wait for SNTP (or any other source) to produce a valid epoch
// before falling back to monotonic stamps. 30 s is generous for a local LAN.
static constexpr uint32_t EPOCH_WAIT_TIMEOUT_MS = 30000;
static constexpr uint32_t EPOCH_POLL_MS         = 200;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static rcl_node_t          g_node;
static rcl_publisher_t     g_publisher;
static rclc_executor_t     g_executor;
static rcl_allocator_t     g_allocator;
static rclc_support_t      g_support;
static bool                g_entities_created = false;
static bool                g_time_synced      = false;
static TaskHandle_t        g_task_handle      = nullptr;
static SemaphoreHandle_t   g_mutex            = nullptr;

static sensor_msgs__msg__CompressedImage g_img_msg;
static uint8_t*            g_img_buf          = nullptr;
static size_t              g_img_buf_capacity = 0;

// ---------------------------------------------------------------------------
// Image buffer helpers
// ---------------------------------------------------------------------------
static bool alloc_image_buf(size_t size) {
    if (g_img_buf_capacity >= size) return true;
    free(g_img_buf);
    g_img_buf = static_cast<uint8_t*>(malloc(size));
    if (!g_img_buf) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for image buffer", size);
        g_img_buf_capacity = 0;
        return false;
    }
    g_img_buf_capacity = size;
    return true;
}

// ---------------------------------------------------------------------------
// Entity lifecycle
// ---------------------------------------------------------------------------
static void create_entities() {
    ESP_LOGI(TAG, "Creating micro-ROS entities");
    ROS_CHECK(rclc_node_init_default(&g_node, "shelfbot_camera", "", &g_support), "node init");

    const rosidl_message_type_support_t* ts =
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, CompressedImage);
    ROS_CHECK(rclc_publisher_init_default(&g_publisher, &g_node, ts, "/camera/compressed"),
              "publisher init");

    ROS_CHECK(rclc_executor_init(&g_executor, &g_support.context, 1, &g_allocator),
              "executor init");

    ESP_LOGI(TAG, "Entities created");
}

static void destroy_entities() {
    ESP_LOGI(TAG, "Destroying entities");
    ROS_CHECK(rcl_publisher_fini(&g_publisher, &g_node), "publisher fini");
    ROS_CHECK(rclc_executor_fini(&g_executor),           "executor fini");
    ROS_CHECK(rcl_node_fini(&g_node),                    "node fini");
    ROS_CHECK(rclc_support_fini(&g_support),             "support fini");
}

// ---------------------------------------------------------------------------
// mDNS helper
// ---------------------------------------------------------------------------
static bool query_mdns_host(const char* host_name, char* out_ip, size_t len) {
    ESP_LOGI(TAG, "Querying mDNS for %s.local", host_name);
    esp_ip4_addr_t addr;
    addr.addr = 0;
    esp_err_t err = mdns_query_a(host_name, 2000, &addr);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) ESP_LOGW(TAG, "Host not found");
        else ESP_LOGE(TAG, "mDNS query failed: %s", esp_err_to_name(err));
        return false;
    }
    snprintf(out_ip, len, IPSTR, IP2STR(&addr));
    ESP_LOGI(TAG, "Agent IP: %s", out_ip);
    return true;
}

// ---------------------------------------------------------------------------
// Clock synchronisation
//
// Strategy (two independent paths, first-one-wins):
//
//  Path A — rmw_uros_sync_session()
//    Uses the XRCE-DDS time-sync sub-protocol to call settimeofday() on the
//    ESP32.  Requires the agent to have time-sync compiled in AND the --time
//    flag (some agent builds skip the update silently even on RMW_RET_OK).
//
//  Path B — SNTP (started by SntpSync::start() in wifi_manager on_ip_event)
//    Runs independently of micro-ROS.  Usually settles in 1-3 s after Wi-Fi
//    connects.  We poll ShelfbotTimestamp::isEpochValid() to detect it.
//
// sync_time() tries path A first, then falls through to polling for path B.
// If neither produces a valid epoch within EPOCH_WAIT_TIMEOUT_MS it gives up
// and sets g_time_synced=false so the monotonic fallback stamp is used.
// ---------------------------------------------------------------------------

static bool try_agent_sync() {
    // Ping first — confirms the XRCE session is up before calling sync.
    rmw_ret_t ping = rmw_uros_ping_agent(200, 20);   // 200 ms × 20 = up to 4 s
    if (ping != RMW_RET_OK) {
        ESP_LOGW(TAG, "Agent ping failed — skipping rmw time sync");
        return false;
    }
    ESP_LOGI(TAG, "Agent ping OK — attempting rmw_uros_sync_session");

    for (uint8_t i = 1; i <= TIME_SYNC_MAX_ATTEMPTS; i++) {
        rmw_ret_t ret = rmw_uros_sync_session(TIME_SYNC_TIMEOUT_MS);
        if (ret != RMW_RET_OK) {
            ESP_LOGW(TAG, "rmw_uros_sync_session attempt %u/%u failed: %d",
                     i, TIME_SYNC_MAX_ATTEMPTS, (int)ret);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (shelfbot::ShelfbotTimestamp::isEpochValid()) {
            ESP_LOGI(TAG, "Clock synced via micro-ROS agent");
            return true;
        }
        // Agent returned OK but epoch is still ~0. Log the raw value so
        // we can tell whether the agent silently skipped the update.
        ESP_LOGW(TAG, "rmw_uros_sync_session OK but epoch still invalid "
                      "(raw=%" PRId64 " us) — agent may need --time flag",
                 shelfbot::ShelfbotTimestamp::epochMicros());
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

static bool sync_time() {
    ESP_LOGI(TAG, "Starting clock sync (agent + SNTP)");

    // Path A: micro-ROS agent sync
    if (try_agent_sync()) {
        int32_t sec; uint32_t ns;
        shelfbot::ShelfbotTimestamp::toRosTime(
            shelfbot::ShelfbotTimestamp::epochMicros(), sec, ns);
        ESP_LOGI(TAG, "Wall time after agent sync: %ld.%09lu", (long)sec, (unsigned long)ns);
        return true;
    }

    // Path B: wait for SNTP (started independently in wifi_manager on_ip_event)
    ESP_LOGI(TAG, "Agent sync did not produce valid epoch — "
                  "polling for SNTP (timeout %lu ms)", (unsigned long)EPOCH_WAIT_TIMEOUT_MS);

    const TickType_t deadline = xTaskGetTickCount() +
                                pdMS_TO_TICKS(EPOCH_WAIT_TIMEOUT_MS);
    uint32_t polls = 0;
    while (xTaskGetTickCount() < deadline) {
        if (shelfbot::ShelfbotTimestamp::isEpochValid()) {
            int32_t sec; uint32_t ns;
            shelfbot::ShelfbotTimestamp::toRosTime(
                shelfbot::ShelfbotTimestamp::epochMicros(), sec, ns);
            ESP_LOGI(TAG, "Clock synced via SNTP after %lu polls — "
                          "wall time %ld.%09lu", (unsigned long)polls, (long)sec, (unsigned long)ns);
            return true;
        }
        if (++polls % 10 == 0) {  // log every 2 s
            ESP_LOGI(TAG, "Waiting for SNTP... (%lu ms elapsed)",
                     (unsigned long)(polls * EPOCH_POLL_MS));
        }
        vTaskDelay(pdMS_TO_TICKS(EPOCH_POLL_MS));
    }

    ESP_LOGE(TAG, "Clock sync timed out — stamps will use monotonic time");
    return false;
}

// ---------------------------------------------------------------------------
// Main micro-ROS task
// ---------------------------------------------------------------------------
static void microros_task(void* /*arg*/) {

    enum class TaskState {
        WAITING_WIFI,
        DISCOVER_AGENT,
        INITIALIZING,
        TIME_SYNCING,
        CONNECTED,
        BACKING_OFF
    } state = TaskState::WAITING_WIFI;

    uint32_t backoff_ms = 250;
    constexpr uint32_t MAX_BACKOFF = 5000;
    uint8_t consecutive_spin_failures = 0;
    char agent_ip[16] = {};
    rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();

    g_allocator = rcl_get_default_allocator();

    while (true) {
        switch (state) {

            case TaskState::WAITING_WIFI: {
                ESP_LOGI(TAG, "Waiting for Wi-Fi...");
                EventGroupHandle_t wifi_evt = wifi_manager_get_event_group();
                xEventGroupWaitBits(wifi_evt, WM_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
                ESP_LOGI(TAG, "Wi-Fi ready");
                StateMachine::changeState("microros", stateToString(MicrorosState::DISCOVERING));
                state = TaskState::DISCOVER_AGENT;
                break;
            }

            case TaskState::DISCOVER_AGENT: {
                if (query_mdns_host(CONFIG_MICROROS_AGENT_HOSTNAME, agent_ip, sizeof(agent_ip))) {
                    state = TaskState::INITIALIZING;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                break;
            }

            case TaskState::INITIALIZING: {
                init_options = rcl_get_zero_initialized_init_options();
                rcl_ret_t ret = rcl_init_options_init(&init_options, g_allocator);
                if (ret != RCL_RET_OK) {
                    ESP_LOGE(TAG, "rcl_init_options_init failed: %ld (%s)",
                             (long)ret, rcl_get_error_string().str);
                    rcl_reset_error();
                    state = TaskState::BACKING_OFF;
                    break;
                }

                rmw_uros_options_set_udp_address(
                    agent_ip, "8888",
                    rcl_init_options_get_rmw_init_options(&init_options));

                ret = rclc_support_init_with_options(
                    &g_support, 0, NULL, &init_options, &g_allocator);

                ROS_CHECK(rcl_init_options_fini(&init_options), "init_options fini");

                if (ret != RCL_RET_OK) {
                    ESP_LOGE(TAG, "rclc_support_init_with_options failed: %ld (%s)",
                             (long)ret, rcl_get_error_string().str);
                    rcl_reset_error();
                    state = TaskState::BACKING_OFF;
                    break;
                }

                create_entities();

                if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
                g_entities_created = true;
                g_time_synced      = false;
                if (g_mutex) xSemaphoreGive(g_mutex);

                consecutive_spin_failures = 0;
                backoff_ms = 250;

                StateMachine::changeState("microros", stateToString(MicrorosState::TIME_SYNC));
                state = TaskState::TIME_SYNCING;
                break;
            }

            case TaskState::TIME_SYNCING: {
                bool synced = sync_time();

                if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
                g_time_synced = synced;
                if (g_mutex) xSemaphoreGive(g_mutex);

                if (!synced) {
                    ESP_LOGW(TAG, "Proceeding without valid epoch — "
                             "header stamps will use monotonic time");
                }

                StateMachine::changeState("microros", stateToString(MicrorosState::CONNECTED));
                ESP_LOGI(TAG, "micro-ROS ready (time_synced=%s)", synced ? "yes" : "no");
                state = TaskState::CONNECTED;
                break;
            }

            case TaskState::CONNECTED: {
                rcl_ret_t spin_ret = rclc_executor_spin_some(&g_executor, RCL_MS_TO_NS(100));
                if (spin_ret != RCL_RET_OK) {
                    if (++consecutive_spin_failures >= 3) {
                        ESP_LOGW(TAG, "3 consecutive spin failures – disconnecting");
                        if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
                        g_entities_created = false;
                        g_time_synced      = false;
                        if (g_mutex) xSemaphoreGive(g_mutex);
                        destroy_entities();
                        StateMachine::changeState("microros", stateToString(MicrorosState::DISCONNECTED));
                        state = TaskState::BACKING_OFF;
                    }
                } else {
                    consecutive_spin_failures = 0;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
                break;
            }

            case TaskState::BACKING_OFF: {
                ESP_LOGW(TAG, "Backing off for %lu ms", (unsigned long)backoff_ms);
                vTaskDelay(pdMS_TO_TICKS(backoff_ms));
                backoff_ms = std::min(MAX_BACKOFF, backoff_ms * 2);
                StateMachine::changeState("microros", stateToString(MicrorosState::DISCOVERING));
                state = TaskState::DISCOVER_AGENT;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
MicrorosSync& MicrorosSync::getInstance() {
    static MicrorosSync instance;
    return instance;
}

bool MicrorosSync::init() {
    if (g_mutex) return true;
    g_mutex = xSemaphoreCreateMutex();
    sensor_msgs__msg__CompressedImage__init(&g_img_msg);
    static char fmt[] = "jpeg";
    g_img_msg.format.data     = fmt;
    g_img_msg.format.size     = 4;
    g_img_msg.format.capacity = sizeof(fmt);
    StateMachine::setInitial("microros", stateToString(MicrorosState::OFF));
    ESP_LOGI(TAG, "Initialised");
    return true;
}

void MicrorosSync::start() {
    if (g_task_handle) return;
    xTaskCreatePinnedToCore(microros_task, "microros_task", 8192, nullptr,
                            5, &g_task_handle, 1);
    ESP_LOGI(TAG, "Task started");
}

// ---------------------------------------------------------------------------
// publishCompressedImage
// ---------------------------------------------------------------------------
void MicrorosSync::publishCompressedImage(const uint8_t* buf, size_t len, uint32_t seq) {
    if (len < 100) {
        ESP_LOGE(TAG, "Frame too small: %zu bytes (discarded)", len);
        return;
    }
    if (buf[0] != 0xFF || buf[1] != 0xD8) {
        ESP_LOGE(TAG, "Invalid JPEG header: 0x%02X 0x%02X (discarded)", buf[0], buf[1]);
        return;
    }
    if (buf[len-2] != 0xFF || buf[len-1] != 0xD9) {
        ESP_LOGW(TAG, "JPEG missing EOI marker (possible truncation) - len=%zu", len);
    }

    if (!g_mutex) return;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    if (!g_entities_created) {
        xSemaphoreGive(g_mutex);
        return;
    }

    if (!alloc_image_buf(len)) {
        xSemaphoreGive(g_mutex);
        return;
    }

    memcpy(g_img_buf, buf, len);
    g_img_msg.data.data     = g_img_buf;
    g_img_msg.data.size     = len;
    g_img_msg.data.capacity = g_img_buf_capacity;

    {
        int32_t  stamp_sec     = 0;
        uint32_t stamp_nanosec = 0;

        if (g_time_synced) {
            shelfbot::ShelfbotTimestamp::toRosTime(
                shelfbot::ShelfbotTimestamp::epochMicros(),
                stamp_sec, stamp_nanosec);
        } else {
            int64_t mono_us = shelfbot::ShelfbotTimestamp::monotonicMicros();
            stamp_sec     = static_cast<int32_t>(mono_us / 1000000LL);
            stamp_nanosec = static_cast<uint32_t>((mono_us % 1000000LL) * 1000LL);
            static uint32_t fallback_count = 0;
            if ((++fallback_count & 0x3F) == 1) {
                ESP_LOGW(TAG, "Publishing with monotonic stamp (clock not synced) — frame #%lu",
                         (unsigned long)seq);
            }
        }

        g_img_msg.header.stamp.sec     = stamp_sec;
        g_img_msg.header.stamp.nanosec = stamp_nanosec;
    }

    static char frame_id[] = "camera_link_optical_frame";
    g_img_msg.header.frame_id.data     = frame_id;
    g_img_msg.header.frame_id.size     = sizeof(frame_id) - 1;
    g_img_msg.header.frame_id.capacity = sizeof(frame_id);

    (void)seq;

    ROS_CHECK(rcl_publish(&g_publisher, &g_img_msg, NULL), "image publish");
    xSemaphoreGive(g_mutex);
}

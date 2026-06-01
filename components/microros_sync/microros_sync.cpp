#include <idf_c_includes.hpp>
#include <wifi_manager.hpp>
#include <microros_sync.hpp>
#include <state_machine.hpp>
#include <state_machine_lifecycle.hpp>

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
// State
// ---------------------------------------------------------------------------

static rcl_node_t          g_node;
static rcl_publisher_t     g_publisher;
static rclc_executor_t     g_executor;
static rcl_allocator_t     g_allocator;
static rclc_support_t      g_support;
static bool                g_entities_created = false;
static TaskHandle_t        g_task_handle = nullptr;
static SemaphoreHandle_t   g_mutex = nullptr;

static sensor_msgs__msg__CompressedImage g_img_msg;
static uint8_t*            g_img_buf = nullptr;
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
// Main micro-ROS task
// ---------------------------------------------------------------------------

static void microros_task(void* /*arg*/) {

    enum class TaskState {
        WAITING_WIFI,
        DISCOVER_AGENT,
        INITIALIZING,
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

            // ------------------------------------------------------------------
            case TaskState::WAITING_WIFI: {
                ESP_LOGI(TAG, "Waiting for Wi-Fi...");
                EventGroupHandle_t wifi_evt = wifi_manager_get_event_group();
                xEventGroupWaitBits(wifi_evt, WM_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
                ESP_LOGI(TAG, "Wi-Fi ready");
                StateMachine::changeState("microros", stateToString(MicrorosState::DISCOVERING));
                state = TaskState::DISCOVER_AGENT;
                break;
            }

            // ------------------------------------------------------------------
            case TaskState::DISCOVER_AGENT: {
                if (query_mdns_host(CONFIG_MICROROS_AGENT_HOSTNAME, agent_ip, sizeof(agent_ip))) {
                    state = TaskState::INITIALIZING;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                break;
            }

            // ------------------------------------------------------------------
            case TaskState::INITIALIZING: {
                // 1. Create init options
                init_options = rcl_get_zero_initialized_init_options();
                rcl_ret_t ret = rcl_init_options_init(&init_options, g_allocator);
                if (ret != RCL_RET_OK) {
                    ESP_LOGE(TAG, "rcl_init_options_init failed: %ld (%s)",
                             (long)ret, rcl_get_error_string().str);
                    rcl_reset_error();
                    state = TaskState::BACKING_OFF;
                    break;
                }

                // 2. Inject the discovered agent IP into the RMW options —
                //    this is the correct way; setenv("RMW_UXRCE_URI") does NOT work on ESP-IDF.
                rmw_uros_options_set_udp_address(
                    agent_ip, "8888",
                    rcl_init_options_get_rmw_init_options(&init_options));

                // 3. Init support with options (not the plain rclc_support_init)
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
                if (g_mutex) xSemaphoreGive(g_mutex);

                consecutive_spin_failures = 0;
                backoff_ms = 250;
                StateMachine::changeState("microros", stateToString(MicrorosState::CONNECTED));
                ESP_LOGI(TAG, "micro-ROS ready");
                state = TaskState::CONNECTED;
                break;
            }

            // ------------------------------------------------------------------
            case TaskState::CONNECTED: {
                rcl_ret_t spin_ret = rclc_executor_spin_some(&g_executor, RCL_MS_TO_NS(100));
                if (spin_ret != RCL_RET_OK) {
                    if (++consecutive_spin_failures >= 3) {
                        ESP_LOGW(TAG, "3 consecutive spin failures – disconnecting");
                        if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
                        g_entities_created = false;
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

            // ------------------------------------------------------------------
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
    if (g_mutex) return true; // already initialised
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

void MicrorosSync::publishCompressedImage(const uint8_t* buf, size_t len, uint32_t seq) {
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

  int64_t now_us = esp_timer_get_time();
  g_img_msg.header.stamp.sec     = static_cast<int32_t>(now_us / 1000000);
  g_img_msg.header.stamp.nanosec = static_cast<uint32_t>((now_us % 1000000) * 1000);

  static char frame_id[] = "camera_frame";
  g_img_msg.header.frame_id.data     = frame_id;
  g_img_msg.header.frame_id.size     = sizeof(frame_id) - 1;
  g_img_msg.header.frame_id.capacity = sizeof(frame_id);

  (void)seq;

  ROS_CHECK(rcl_publish(&g_publisher, &g_img_msg, NULL), "image publish");
  xSemaphoreGive(g_mutex);
}
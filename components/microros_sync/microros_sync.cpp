#include <idf_c_includes.hpp>
#include <wifi_manager.hpp>
#include <microros_sync.hpp>
#include <state_machine.hpp>
#include <state_machine_lifecycle.hpp>

static const char* TAG = "MicrorosSync";

static rcl_node_t          node;
static rcl_publisher_t     publisher;
static rclc_executor_t     executor;
static rcl_allocator_t     allocator;
static rclc_support_t      support;
static bool                is_initialized = false;
static TaskHandle_t        spin_task_handle = nullptr;
static sensor_msgs__msg__CompressedImage img_msg;

static void free_image_msg_data() {
    if (img_msg.data.data) {
        free((void*)img_msg.data.data);
        img_msg.data.data = nullptr;
        img_msg.data.size = 0;
        img_msg.data.capacity = 0;
    }
}

static bool allocate_image_data(size_t required_size) {
    if (img_msg.data.capacity >= required_size) return true;
    free_image_msg_data();
    uint8_t* new_buf = (uint8_t*)malloc(required_size);
    if (!new_buf) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes", required_size);
        return false;
    }
    img_msg.data.data = new_buf;
    img_msg.data.capacity = required_size;
    img_msg.data.size = 0;
    return true;
}

static bool discover_agent(const char* hostname, uint32_t timeout_ms) {
    ESP_LOGI(TAG, "mDNS query: %s.local", hostname);
    esp_ip4_addr_t agent_addr;
    agent_addr.addr = 0;
    esp_err_t err = mdns_query_a(hostname, timeout_ms, &agent_addr);
    if (err != ESP_OK || agent_addr.addr == 0) {
        ESP_LOGE(TAG, "mDNS failed for %s.local", hostname);
        return false;
    }
    char addr_str[16];
    snprintf(addr_str, sizeof(addr_str), IPSTR, IP2STR(&agent_addr));
    ESP_LOGI(TAG, "Agent IP: %s", addr_str);
    if (!rmw_uros_ping_agent(3000, 1)) {
        ESP_LOGE(TAG, "Agent not reachable at %s", addr_str);
        return false;
    }
    char uri[64];
    snprintf(uri, sizeof(uri), "udp4://" IPSTR ":8888", IP2STR(&agent_addr));
    setenv("RMW_UXRCE_URI", uri, 1);
    ESP_LOGI(TAG, "Agent discovered at %s", uri);
    return true;
}

static bool create_microros_entities() {
    allocator = rcl_get_default_allocator();
    rcl_ret_t ret = rclc_support_init(&support, 0, nullptr, &allocator);
    if (ret != RCL_RET_OK) {
        ESP_LOGE(TAG, "rclc_support_init failed: %ld", (long)ret);
        return false;
    }
    ret = rclc_node_init_default(&node, "shelfbot_camera", "", &support);
    if (ret != RCL_RET_OK) {
        ESP_LOGE(TAG, "rclc_node_init_default failed: %ld", (long)ret);
        return false;
    }
    const rosidl_message_type_support_t* type_support =
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, CompressedImage);
    ret = rclc_publisher_init_default(&publisher, &node, type_support, "/camera/compressed");
    if (ret != RCL_RET_OK) {
        ESP_LOGE(TAG, "rclc_publisher_init_default failed: %ld", (long)ret);
        return false;
    }
    ret = rclc_executor_init(&executor, &support.context, 1, &allocator);
    if (ret != RCL_RET_OK) {
        ESP_LOGE(TAG, "rclc_executor_init failed: %ld", (long)ret);
        return false;
    }
    ESP_LOGI(TAG, "micro-ROS entities created");
    return true;
}

static void spin_task(void* arg) {
    while (true) {
        rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

MicrorosSync& MicrorosSync::getInstance() {
    static MicrorosSync instance;
    return instance;
}

bool MicrorosSync::init() {
    if (is_initialized) return true;
    StateMachine::setInitial("microros", stateToString(MicrorosState::OFF));
    is_initialized = true;
    ESP_LOGI(TAG, "Initialised");
    return true;
}

void MicrorosSync::start() {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Not initialised");
        return;
    }
    EventGroupHandle_t wm_evt = wifi_manager_get_event_group();
    if (wm_evt) {
        xEventGroupWaitBits(wm_evt, WM_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
    } else {
        ESP_LOGE(TAG, "Wi-Fi event group not available");
        StateMachine::changeState("microros", stateToString(MicrorosState::ERROR));
        return;
    }
    StateMachine::changeState("microros", stateToString(MicrorosState::DISCOVERING));
    const char* agent_hostname = CONFIG_MICROROS_AGENT_HOSTNAME;
    if (!discover_agent(agent_hostname, 5000)) {
        StateMachine::changeState("microros", stateToString(MicrorosState::ERROR));
        return;
    }
    if (!create_microros_entities()) {
        StateMachine::changeState("microros", stateToString(MicrorosState::ERROR));
        return;
    }
    xTaskCreatePinnedToCore(spin_task, "uros_spin", 8192, nullptr, 2, &spin_task_handle, 1);
    StateMachine::changeState("microros", stateToString(MicrorosState::CONNECTED));
    ESP_LOGI(TAG, "micro-ROS ready");
}

void MicrorosSync::publishCompressedImage(const uint8_t* buf, size_t len, uint32_t seq) {
    if (StateMachine::getState("microros") != stateToString(MicrorosState::CONNECTED)) return;
    if (!is_initialized || !rcl_publisher_is_valid(&publisher)) return;
    if (!allocate_image_data(len)) return;
    memcpy((void*)img_msg.data.data, buf, len);
    img_msg.data.size = len;
    img_msg.format.data = (char*)"jpeg";
    img_msg.format.size = 4;
    img_msg.format.capacity = 4;
    rcl_ret_t ret = rcl_publish(&publisher, &img_msg, NULL);
    if (ret != RCL_RET_OK) {
        ESP_LOGW(TAG, "rcl_publish failed: %ld", (long)ret);
    } else {
        ESP_LOGD(TAG, "Published image seq=%lu, size=%zu", (unsigned long)seq, len);
    }
}
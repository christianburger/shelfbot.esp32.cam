#include <microros_sync.hpp>

static const auto *TAG = "MicrorosSync";

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
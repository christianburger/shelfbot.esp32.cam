#include "shelfbot_camera.h"
#include "network_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "mdns.h"
#include "rosidl_runtime_c/string_functions.h"

#include <uros_network_interfaces.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <rmw_microros/init_options.h>

#include <sensor_msgs/msg/compressed_image.h>
#include <sensor_msgs/msg/camera_info.h>
#include <std_msgs/msg/bool.h>

#define LED_GPIO_PIN 4

#define RCCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){printf("Failed status on line %d: %d. Aborting.\n",__LINE__,(int)temp_rc);vTaskDelete(NULL);}}
#define RCSOFTCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){printf("Failed status on line %d: %d. Continuing.\n",__LINE__,(int)temp_rc);}}

static const char *TAG = "shelfbot_camera";

// --- Micro-ROS entities ---
rcl_publisher_t compressed_image_publisher;
rcl_publisher_t camera_info_publisher;
rcl_subscription_t led_subscriber;
rcl_node_t node;
rcl_allocator_t allocator;
rclc_support_t support;
rclc_executor_t executor;
rcl_timer_t camera_info_timer;

// --- Messages ---
sensor_msgs__msg__CompressedImage compressed_image_msg;
sensor_msgs__msg__CameraInfo camera_info_msg;
std_msgs__msg__Bool led_msg;

// --- Externs ---
extern QueueHandle_t frame_queue;

// --- mDNS ---
static char agent_ip_str[16];
static bool query_mdns_host(const char * host_name)
{
    ESP_LOGI(TAG, "Querying for mDNS host: %s.local", host_name);
    esp_ip4_addr_t addr;
    addr.addr = 0;

    esp_err_t err = mdns_query_a(host_name, 5000,  &addr);
    if (err) {
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "mDNS host \'%s.local\' not found!\n", host_name);
            return false;
        }
        ESP_LOGE(TAG, "mDNS query failed: %s\n", esp_err_to_name(err));
        return false;
    }

    esp_ip4addr_ntoa(&addr, agent_ip_str, sizeof(agent_ip_str));
    ESP_LOGI(TAG, "mDNS host \'%s.local\' found at IP: %s\n", host_name, agent_ip_str);
    return true;
}

// --- Callbacks ---
void camera_info_timer_callback(rcl_timer_t * timer, int64_t last_call_time)
{
    (void) last_call_time;
    if (timer != NULL) {
        // Get the synchronized ROS time
        int64_t now = rmw_uros_epoch_nanos();
        camera_info_msg.header.stamp.sec = now / 1000000000;
        camera_info_msg.header.stamp.nanosec = now % 1000000000;
        RCSOFTCHECK(rcl_publish(&camera_info_publisher, &camera_info_msg, NULL));
    }
}

void led_subscription_callback(const void *msgin)
{
    const std_msgs__msg__Bool *msg = (const std_msgs__msg__Bool *)msgin;
    ESP_LOGI(TAG, "LED state received: %s", msg->data ? "ON" : "OFF");
    gpio_set_level(LED_GPIO_PIN, msg->data ? 1 : 0);
}

// --- Main Task ---
void shelfbot_camera_task(void *pvParameters) {
    ESP_LOGI(TAG, "Task started, waiting for network...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | MDNS_STARTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Network and mDNS are ready.");

    if (!query_mdns_host("gentoo-laptop")) {
        ESP_LOGE(TAG, "Could not find micro-ROS agent via mDNS, aborting.");
        vTaskDelete(NULL);
        return;
    }

    gpio_reset_pin(LED_GPIO_PIN);
    gpio_set_direction(LED_GPIO_PIN, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "Initializing micro-ROS with agent at %s:8888", agent_ip_str);

    allocator = rcl_get_default_allocator();

    rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
    RCCHECK(rcl_init_options_init(&init_options, allocator));

#ifdef CONFIG_MICRO_ROS_ESP_XRCE_DDS_MIDDLEWARE
    rmw_init_options_t* rmw_options = rcl_init_options_get_rmw_init_options(&init_options);
    RCCHECK(rmw_uros_options_set_udp_address(agent_ip_str, "8888", rmw_options));
#endif

    RCCHECK(rclc_support_init_with_options(&support, 0, NULL, &init_options, &allocator));

    RCCHECK(rclc_node_init_default(&node, "shelfbot_camera", "", &support));

    RCCHECK(rclc_publisher_init_default(
        &compressed_image_publisher,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, CompressedImage),
        "/esp32_cam/image_raw/compressed"));

    RCCHECK(rclc_publisher_init_default(
        &camera_info_publisher,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, CameraInfo),
        "/esp32_cam/camera_info"));

    RCCHECK(rclc_subscription_init_default(
        &led_subscriber,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Bool),
        "/esp32_cam/led"));

    RCCHECK(rclc_timer_init_default2(
        &camera_info_timer,
        &support,
        RCL_MS_TO_NS(1000),
        camera_info_timer_callback,
        true));

    RCCHECK(rclc_executor_init(&executor, &support.context, 2, &allocator));
    RCCHECK(rclc_executor_add_timer(&executor, &camera_info_timer));
    RCCHECK(rclc_executor_add_subscription(&executor, &led_subscriber, &led_msg, &led_subscription_callback, ON_NEW_DATA));

    // Initialize the message structures first
    sensor_msgs__msg__CompressedImage__init(&compressed_image_msg);
    sensor_msgs__msg__CameraInfo__init(&camera_info_msg);

    // Set frame_id for the camera messages
    rosidl_runtime_c__String__assign(&compressed_image_msg.header.frame_id, "camera_link");
    rosidl_runtime_c__String__assign(&camera_info_msg.header.frame_id, "camera_link");
    rosidl_runtime_c__String__assign(&compressed_image_msg.format, "jpeg");

    // Synchronize time with the agent
    ESP_LOGI(TAG, "Synchronizing time with agent...");
    RCCHECK(rmw_uros_sync_session(1000));
    ESP_LOGI(TAG, "Time synchronized");

    ESP_LOGI(TAG, "Micro-ROS initialized successfully");
    xEventGroupSetBits(s_wifi_event_group, UROS_INIT_COMPLETE_BIT);

    while (1) {
        rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10));

        camera_fb_t *fb = NULL;
        if (xQueueReceive(frame_queue, &fb, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (fb) {
                // Get the synchronized ROS time
                int64_t now = rmw_uros_epoch_nanos();
                compressed_image_msg.header.stamp.sec = now / 1000000000;
                compressed_image_msg.header.stamp.nanosec = now % 1000000000;

                compressed_image_msg.data.data = fb->buf;
                compressed_image_msg.data.size = fb->len;
                compressed_image_msg.data.capacity = fb->len;

                RCSOFTCHECK(rcl_publish(&compressed_image_publisher, &compressed_image_msg, NULL));

                esp_camera_fb_return(fb);
            }
        }
    }
    // Cleanup should be unreachable
}

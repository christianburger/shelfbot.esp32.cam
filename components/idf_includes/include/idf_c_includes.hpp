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
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_psram.h"
#include "esp_chip_info.h"
#include "lwip/inet.h"
#include "sdkconfig.h"

// NVS
#include "nvs_flash.h"

// Drivers
#include "driver/gpio.h"

// HTTP server
#include "esp_http_server.h"

// JSON
#include "cJSON.h"

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

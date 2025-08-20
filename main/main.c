
#include <stdio.h>
#include "esp_system.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "network_manager.h"
#include "esp_log.h"
#include "shelfbot_camera.h"

// Camera pins for ESP32
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0      5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,
    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    //.pixel_format = PIXFORMAT_RGB565,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_SVGA,
    .jpeg_quality = 12,
    .fb_count = 2 // Increased from 1 to 2 to allow double buffering
};

QueueHandle_t frame_queue;

static const char *TAG = "main";

void camera_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting camera initialization");
    
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }
    ESP_LOGI(TAG, "Camera initialized successfully");
    
    while(1) {
        camera_fb_t *pic = esp_camera_fb_get();
        if (pic) {
            //ESP_LOGI(TAG, "Frame captured: %dx%d", pic->width, pic->height);
            if (xQueueSend(frame_queue, &pic, 0) != pdTRUE) {
                // If the queue is full, it means consumers are not keeping up.
                // Return the frame to avoid memory leaks.
                esp_camera_fb_return(pic);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent watchdog issues if queue is full
    }
}

void app_main(void) {
    // Create frame queue for camera
    frame_queue = xQueueCreate(2, sizeof(camera_fb_t*));
    
    // Highest priority task to capture frames from the camera hardware
    xTaskCreatePinnedToCore(camera_task, "camera_task", 4096, NULL, configMAX_PRIORITIES - 1, NULL, 0);

    // Lower priority task for network operations (web server)
    xTaskCreatePinnedToCore(network_task, "network_task", NETWORK_TASK_STACK_SIZE, NULL, 5, NULL, 1);

    // Lower priority task for micro-ROS operations
    xTaskCreatePinnedToCore(shelfbot_camera_task, "shelfbot_camera_task", SHELFBOT_CAMERA_TASK_STACK_SIZE, NULL, 5, NULL, 1);
}
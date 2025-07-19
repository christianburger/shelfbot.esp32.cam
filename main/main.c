
#include <stdio.h>
#include "esp_system.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "network_manager.h"
#include "esp_log.h"

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
    .fb_count = 1
};

QueueHandle_t frame_queue;

static const char *TAG = "main";
SemaphoreHandle_t frame_ready_semaphore = NULL;

void process_frame_task(void *pvParameters) {
    camera_fb_t *pic;
    static char buf[128];  // Buffer for hex conversion
    
    while(1) {
        if(xQueueReceive(frame_queue, &pic, portMAX_DELAY)) {
            printf("<start_picture>\n");
            printf("Width: %d\n", pic->width);
            printf("Height: %d\n", pic->height);
            printf("Format: %d\n", pic->format);
            printf("Size: %zu\n", pic->len);
            
            // Process in chunks with yields
            for(size_t i = 0; i < pic->len; i += 32) {
                size_t chunk = (pic->len - i > 32) ? 32 : (pic->len - i);
                for(size_t j = 0; j < chunk; j++) {
                    //Prints the binary frame on serial.
                    //sprintf(buf + (j*2), "%02X", pic->buf[i + j]);
                }

                //Prints carriage return after each chunk of the frame.
                //printf("%s\n", buf);
                vTaskDelay(1);  // Yield to prevent watchdog trigger
            }
            
            printf("<end_picture>\n");
            esp_camera_fb_return(pic);
        }
        vTaskDelay(1);
    }
}

void camera_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting camera initialization");
    
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }
    ESP_LOGI(TAG, "Camera initialized successfully");
    
    while(1) {
        if (xSemaphoreTake(frame_ready_semaphore, portMAX_DELAY) == pdTRUE) {
            camera_fb_t *pic = esp_camera_fb_get();
            if (pic) {
                ESP_LOGI(TAG, "Frame captured: %dx%d", pic->width, pic->height);
                xQueueSend(frame_queue, &pic, portMAX_DELAY);
            }
            xSemaphoreGive(frame_ready_semaphore);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void app_main(void) {
    // Create frame queue for camera
    frame_queue = xQueueCreate(2, sizeof(camera_fb_t*));
    
    // Create frame ready semaphore
    frame_ready_semaphore = xSemaphoreCreateBinary();
    if (frame_ready_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create frame_ready_semaphore");
        return;
    }

    // Give the semaphore first time so it can be taken
    xSemaphoreGive(frame_ready_semaphore);

    // Network task on core 1 with highest priority
    xTaskCreatePinnedToCore(network_task, "network_task", NETWORK_TASK_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, NULL, 1);

    // Camera task on core 0 with medium priority
    xTaskCreatePinnedToCore(camera_task, "camera_task", 8192, NULL, configMAX_PRIORITIES - 2, NULL, 0);

    // Process frame task on core 0 with lower priority
    //xTaskCreatePinnedToCore(process_frame_task, "process_frame", 8192, NULL, configMAX_PRIORITIES - 3, NULL, 0);
}
#include "controller.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_psram.h"

static const char *TAG = "controller";
extern SemaphoreHandle_t frame_ready_semaphore;
esp_err_t status_handler(httpd_req_t *req) {
    char response[256];
    uint32_t chip_speed = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ; // Get CPU freq from sdkconfig
    
    snprintf(response, sizeof(response),
             "{\"heap\":%lu,\"tasks\":%d,\"cpu\":%lu}",
             esp_get_free_heap_size(),
             uxTaskGetNumberOfTasks(),
             chip_speed);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

esp_err_t root_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Serving root page");
    const char *response = "<h1>ESP32 Camera Server</h1>"
                          "<p><a href='/capture'>Take Photo</a></p>"
                          "<p><a href='/stream'>Start Stream</a></p>"
                          "<p><a href='/status'>System Status</a></p>"
                          "<p><a href='/hardware'>Hardware Info</a></p>";
    
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

extern QueueHandle_t frame_queue;  // Add at top of file

esp_err_t capture_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "[CAPTURE] Starting capture request");
    
    camera_fb_t *pic;
    if (xQueueReceive(frame_queue, &pic, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ESP_LOGI(TAG, "[CAPTURE] Got frame from queue");
        
        if (!pic) {
            ESP_LOGE(TAG, "[CAPTURE] Camera capture failed - null buffer");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "[CAPTURE] Frame captured: %dx%d, %d bytes",
                 pic->width, pic->height, pic->len);

        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
        esp_err_t res = httpd_resp_send(req, (const char *)pic->buf, pic->len);
        
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "[CAPTURE] Failed to send response: %d", res);
        } else {
            ESP_LOGI(TAG, "[CAPTURE] Image sent successfully");
        }

        esp_camera_fb_return(pic);
        return res;
    }

    ESP_LOGE(TAG, "[CAPTURE] Timeout waiting for frame from queue");
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

esp_err_t stream_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "[STREAM] Starting video stream");
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=123456789000000000000987654321");

    while (1) {
        camera_fb_t *pic;
        if (xQueueReceive(frame_queue, &pic, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGI(TAG, "[STREAM] Got frame from queue");
            
            if (!pic) {
                ESP_LOGE(TAG, "[STREAM] Camera capture failed - null buffer");
                break;
            }

            ESP_LOGI(TAG, "[STREAM] Frame captured: %dx%d, %d bytes",
                     pic->width, pic->height, pic->len);

            const char *head = "--123456789000000000000987654321\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
            char header_buffer[128];
            sprintf(header_buffer, head, pic->len);
            
            esp_err_t res = httpd_resp_send_chunk(req, header_buffer, strlen(header_buffer));
            if (res != ESP_OK) {
                ESP_LOGE(TAG, "[STREAM] Failed to send header chunk");
                esp_camera_fb_return(pic);
                break;
            }
            
            res = httpd_resp_send_chunk(req, (const char *)pic->buf, pic->len);
            if (res != ESP_OK) {
                ESP_LOGE(TAG, "[STREAM] Failed to send image chunk");
                esp_camera_fb_return(pic);
                break;
            }
            
            res = httpd_resp_send_chunk(req, "\r\n", 2);
            if (res != ESP_OK) {
                ESP_LOGE(TAG, "[STREAM] Failed to send boundary chunk");
                esp_camera_fb_return(pic);
                break;
            }

            esp_camera_fb_return(pic);
        } else {
            ESP_LOGE(TAG, "[STREAM] Timeout waiting for frame from queue");
            break;
        }
    }

    return ESP_OK;
}

esp_err_t hardware_info_handler(httpd_req_t *req) {
    char response[512];
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    snprintf(response, sizeof(response),
             "{"
             "\"psram_size\":%d,"
             "\"features\":{"
             "\"embedded_psram\":%s,"
             "\"wifi\":%s,"
             "\"bt\":%s,"
             "\"ble\":%s"
             "},"
             "\"cores\":%d,"
             "\"revision\":%d,"
             "\"model\":\"ESP32\""
             "}",
             esp_psram_get_size(),
             (chip_info.features & CHIP_FEATURE_EMB_PSRAM) ? "true" : "false",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "true" : "false", 
             (chip_info.features & CHIP_FEATURE_BT) ? "true" : "false",
             (chip_info.features & CHIP_FEATURE_BLE) ? "true" : "false",
             chip_info.cores,
             chip_info.revision
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

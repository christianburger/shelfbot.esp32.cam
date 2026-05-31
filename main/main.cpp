#include <wifi_manager.hpp>
#include <microros_sync.hpp>

static const char *TAG = "main";

// Camera pin map – ESP32-CAM AI-Thinker module
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
    .frame_size    = FRAMESIZE_SVGA,
    .jpeg_quality  = 12,
    .fb_count      = 1,
    .fb_location   = CAMERA_FB_IN_PSRAM,
    .grab_mode     = CAMERA_GRAB_WHEN_EMPTY,
    .sccb_i2c_port = 0
};

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
            MicrorosSync::publishCompressedImage(fb->buf, fb->len, seq++);
            esp_camera_fb_return(fb);
        } else {
            ESP_LOGE(TAG, "esp_camera_fb_get() failed");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main(void) {
    // 1. NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. TCP/IP stack + default event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. Wi‑Fi manager
    ESP_ERROR_CHECK(wifi_manager_init());

    // 4. micro‑ROS sync
    MicrorosSync::getInstance().init();
    MicrorosSync::getInstance().start();

    // 5. Camera task (Core 0)
    xTaskCreatePinnedToCore(camera_task, "camera_task",
                            8192, NULL,
                            configMAX_PRIORITIES - 2,
                            NULL, 0);

    ESP_LOGI(TAG, "app_main done – all tasks running");
}
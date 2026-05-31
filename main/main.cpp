#include <wifi_manager.hpp>
#include <microros_sync.hpp>
#include <network_manager.hpp>

static const char* TAG = "main";

// ---------------------------------------------------------------------------
// Camera pin map — ESP32-CAM AI-Thinker module
// ---------------------------------------------------------------------------

static constexpr int CAM_PIN_PWDN  = 32;
static constexpr int CAM_PIN_RESET = -1;
static constexpr int CAM_PIN_XCLK  =  0;
static constexpr int CAM_PIN_SIOD  = 26;
static constexpr int CAM_PIN_SIOC  = 27;
static constexpr int CAM_PIN_D7    = 35;
static constexpr int CAM_PIN_D6    = 34;
static constexpr int CAM_PIN_D5    = 39;
static constexpr int CAM_PIN_D4    = 36;
static constexpr int CAM_PIN_D3    = 21;
static constexpr int CAM_PIN_D2    = 19;
static constexpr int CAM_PIN_D1    = 18;
static constexpr int CAM_PIN_D0    =  5;
static constexpr int CAM_PIN_VSYNC = 25;
static constexpr int CAM_PIN_HREF  = 23;
static constexpr int CAM_PIN_PCLK  = 22;

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
    .sccb_i2c_port = 0,
};

// ---------------------------------------------------------------------------
// Shared frame queue (camera_task → Controller handlers)
// ---------------------------------------------------------------------------

static QueueHandle_t frame_queue = nullptr;

// ---------------------------------------------------------------------------
// Camera task
// ---------------------------------------------------------------------------

static void camera_task(void*) {
    ESP_LOGI(TAG, "Initialising camera...");
    const esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "Camera ready");

    uint32_t seq = 0;
    while (true) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            ESP_LOGD(TAG, "Frame %lu: %dx%d %zu B",
                     static_cast<unsigned long>(seq), fb->width, fb->height, fb->len);

            MicrorosSync::publishCompressedImage(fb->buf, fb->len, seq++);

            // Push to HTTP queue; drop frame if queue is full (non-blocking)
            if (xQueueSend(frame_queue, &fb, 0) != pdTRUE) {
                esp_camera_fb_return(fb);
            }
        } else {
            ESP_LOGE(TAG, "esp_camera_fb_get() failed");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------

extern "C" void app_main() {
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

    // 3. Frame queue — capacity 2 so HTTP handlers can drain without blocking camera
    frame_queue = xQueueCreate(2, sizeof(camera_fb_t*));
    configASSERT(frame_queue);

    // 4. Wi-Fi manager (shelfbot multi-SSID roaming)
    ESP_ERROR_CHECK(wifi_manager_init());

    // 5. micro-ROS sync
    MicrorosSync::getInstance().init();
    MicrorosSync::getInstance().start();

    // 6. Network manager (Wi-Fi STA + HTTP server with Controller handlers)
    ESP_ERROR_CHECK(NetworkManager::get_instance().init(frame_queue));

    // 7. Camera task on Core 0
    xTaskCreatePinnedToCore(camera_task, "camera_task",
                            8192, nullptr,
                            configMAX_PRIORITIES - 2,
                            nullptr, 0);

    ESP_LOGI(TAG, "app_main done — all tasks running");
}

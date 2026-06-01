#include <controller.hpp>

const char*    Controller::TAG          = "Controller";
QueueHandle_t  Controller::frame_queue_ = nullptr;

void Controller::init(QueueHandle_t frame_queue) {
    frame_queue_ = frame_queue;
}

// ---------------------------------------------------------------------------
// Root
// ---------------------------------------------------------------------------

esp_err_t Controller::root_handler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Serving root page");
    static constexpr const char* HTML =
        "<h1>ESP32 Camera Server</h1>"
        "<p><a href='/capture'>Take Photo</a></p>"
        "<p><a href='/stream'>Start Stream</a></p>"
        "<p><a href='/status'>System Status</a></p>"
        "<p><a href='/hardware'>Hardware Info</a></p>";
    return httpd_resp_send(req, HTML, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// Capture — single JPEG frame from queue
// ---------------------------------------------------------------------------

esp_err_t Controller::capture_handler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Capture request");

    if (!frame_queue_) {
        ESP_LOGE(TAG, "Frame queue not initialised");
        return httpd_resp_send_500(req);
    }

    camera_fb_t* fb = nullptr;
    if (xQueueReceive(frame_queue_, &fb, pdMS_TO_TICKS(1000)) != pdTRUE || !fb) {
        ESP_LOGE(TAG, "Timeout waiting for frame");
        return httpd_resp_send_500(req);
    }

    ESP_LOGI(TAG, "Frame captured: %dx%d, %zu B", fb->width, fb->height, fb->len);

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    const esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char*>(fb->buf), fb->len);

    esp_camera_fb_return(fb);
    return res;
}

// ---------------------------------------------------------------------------
// Stream — MJPEG multipart
// ---------------------------------------------------------------------------

esp_err_t Controller::stream_handler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Stream started");

    if (!frame_queue_) {
        ESP_LOGE(TAG, "Frame queue not initialised");
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=123456789000000000000987654321");

    static constexpr const char* PART_HDR =
        "--123456789000000000000987654321\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n\r\n";

    while (true) {
        camera_fb_t* fb = nullptr;
        if (xQueueReceive(frame_queue_, &fb, pdMS_TO_TICKS(1000)) != pdTRUE || !fb) {
            ESP_LOGE(TAG, "Stream: timeout waiting for frame");
            break;
        }

        ESP_LOGD(TAG, "Stream frame: %dx%d, %zu B", fb->width, fb->height, fb->len);

        char hdr[128];
        const int hdr_len = snprintf(hdr, sizeof(hdr), PART_HDR, fb->len);

        esp_err_t res = httpd_resp_send_chunk(req, hdr, hdr_len);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(fb->buf), fb->len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n", 2);
        }

        esp_camera_fb_return(fb);

        if (res != ESP_OK) {
            ESP_LOGI(TAG, "Stream: client disconnected");
            break;
        }
    }

    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Status — heap / task count / CPU
// ---------------------------------------------------------------------------

esp_err_t Controller::status_handler(httpd_req_t* req) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"heap\":%lu,\"tasks\":%u,\"cpu_mhz\":%u}",
             static_cast<unsigned long>(esp_get_free_heap_size()),
             static_cast<unsigned>(uxTaskGetNumberOfTasks()),
             static_cast<unsigned>(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ));

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// Hardware info — chip features / PSRAM
// ---------------------------------------------------------------------------

esp_err_t Controller::hardware_info_handler(httpd_req_t* req) {
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    char buf[512];
    snprintf(buf, sizeof(buf),
             "{"
             "\"psram_size\":%u,"
             "\"cores\":%u,"
             "\"revision\":%u,"
             "\"model\":\"ESP32\","
             "\"features\":{"
             "\"embedded_psram\":%s,"
             "\"wifi\":%s,"
             "\"bt\":%s,"
             "\"ble\":%s"
             "}"
             "}",
             static_cast<unsigned>(esp_psram_get_size()),
             static_cast<unsigned>(chip.cores),
             static_cast<unsigned>(chip.revision),
             (chip.features & CHIP_FEATURE_EMB_PSRAM) ? "true" : "false",
             (chip.features & CHIP_FEATURE_WIFI_BGN)  ? "true" : "false",
             (chip.features & CHIP_FEATURE_BT)        ? "true" : "false",
             (chip.features & CHIP_FEATURE_BLE)       ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

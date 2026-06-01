#include <camera_manager.hpp>

static const char* TAG = "CameraManager";
static CameraManager* g_instance = nullptr;

void CameraManager::internalFrameCallback(const CameraCommon::CameraFrame& frame) {
    if (!g_instance) return;
    CameraCommon::CameraFrame* copy = new CameraCommon::CameraFrame(frame);
    copy->buffer = new uint8_t[frame.length];
    memcpy(copy->buffer, frame.buffer, frame.length);
    if (xQueueSend(g_instance->getFrameQueue(), &copy, 0) != pdTRUE) {
        delete[] copy->buffer;
        delete copy;
    }
}

esp_err_t CameraManager::initialize() {
    if (initialized_) return ESP_OK;
    g_instance = this;
    frame_queue_ = xQueueCreate(2, sizeof(CameraCommon::CameraFrame*));
    if (!frame_queue_) {
        ESP_LOGE(TAG, "Failed to create frame queue");
        return ESP_ERR_NO_MEM;
    }
    
    // Create and initialize the camera control with default config
    CameraControlConfig config;
    control_ = std::make_unique<CameraControl>(config);
    esp_err_t err = control_->initialize();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera control init failed");
        return err;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "CameraManager initialized");
    return ESP_OK;
}

esp_err_t CameraManager::startCapture(CameraControl::FrameCallback callback) {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    if (callback) {
        return control_->startContinuousCapture(callback);
    } else {
        return control_->startContinuousCapture(internalFrameCallback);
    }
}

esp_err_t CameraManager::stopCapture() {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    return control_->stopContinuousCapture();
}

bool CameraManager::waitUntilReady(uint32_t timeout_ms) {
    if (!initialized_) return false;
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        if (control_->isReady()) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}
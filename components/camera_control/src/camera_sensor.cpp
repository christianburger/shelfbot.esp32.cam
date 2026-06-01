#include <camera_sensor.hpp>

static const char* TAG = "CameraSensor";

CameraSensor::CameraSensor(std::unique_ptr<ICameraDriver> driver)
    : driver_(std::move(driver)) {
    frame_queue_ = xQueueCreate(2, sizeof(CameraCommon::CameraFrame*));
}

CameraSensor::~CameraSensor() {
    stopContinuousCapture();
    if (driver_) driver_->deinit();
    if (frame_queue_) vQueueDelete(frame_queue_);
}

bool CameraSensor::initialize(const CameraCommon::CameraConfig& config) {
    if (initialized_) return true;
    config_ = config;
    const char* err = driver_->configure(config);
    if (err) {
        ESP_LOGE(TAG, "Driver configure failed: %s", err);
        return false;
    }
    err = driver_->init();
    if (err) {
        ESP_LOGE(TAG, "Driver init failed: %s", err);
        return false;
    }
    initialized_ = true;
    ESP_LOGI(TAG, "Camera sensor initialized: %s", driver_->getSensorModel());
    return true;
}

bool CameraSensor::start() {
    if (!initialized_) return false;
    const char* err = driver_->start();
    if (err) {
        ESP_LOGE(TAG, "Driver start failed: %s", err);
        return false;
    }
    running_ = true;
    return true;
}

bool CameraSensor::stop() {
    if (!running_) return true;
    const char* err = driver_->stop();
    if (err) ESP_LOGW(TAG, "Driver stop: %s", err);
    running_ = false;
    return true;
}

bool CameraSensor::capture(CameraCommon::CameraFrame& frame, uint32_t timeout_ms) {
    if (!initialized_ || !running_) return false;
    return driver_->captureFrame(frame, timeout_ms);
}

void CameraSensor::setFrameCallback(FrameCallback callback) {
    callback_ = callback;
}

bool CameraSensor::startContinuousCapture(uint32_t interval_ms) {
    if (!initialized_ || !running_) return false;
    if (capture_task_ != nullptr) return true;
    BaseType_t res = xTaskCreate(captureTask, "cam_capture", 4096, this, 5, &capture_task_);
    if (res != pdPASS) return false;
    return true;
}

bool CameraSensor::stopContinuousCapture() {
    if (capture_task_) {
        vTaskDelete(capture_task_);
        capture_task_ = nullptr;
    }
    return true;
}

bool CameraSensor::startStreaming() {
    return driver_->startStreaming();
}

bool CameraSensor::stopStreaming() {
    return driver_->stopStreaming();
}

bool CameraSensor::isStreaming() const {
    return driver_->isStreaming();
}

bool CameraSensor::isReady() const {
    return initialized_ && driver_->isReady();
}

void CameraSensor::returnFrame(CameraCommon::CameraFrame& frame) {
    driver_->returnFrame(frame);
}

void CameraSensor::captureTask(void* arg) {
    auto* sensor = static_cast<CameraSensor*>(arg);
    sensor->captureLoop();
    vTaskDelete(nullptr);
}

void CameraSensor::captureLoop() {
    while (true) {
        CameraCommon::CameraFrame frame;
        if (driver_->captureFrame(frame, 1000)) {
            if (callback_) callback_(frame);
            driver_->returnFrame(frame);
        } else {
            ESP_LOGD(TAG, "Capture failed");   // changed to debug to reduce noise
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#include <camera_control.hpp>

const char* CameraControl::TAG = "CameraControl";

CameraControl::CameraControl(const CameraControlConfig& config) : config_(config) {
    // Register this module with the state machine here so ownership is
    // fully inside the camera_control component, not in main.cpp.
    StateMachine::setInitial("camera", stateToString(CameraState::OFF));

    auto driver = create_esp32_camera_driver();
    sensor_ = std::make_unique<CameraSensor>(std::move(driver));
}

CameraControl::~CameraControl() {
    stopContinuousCapture();
    stop();
}

void CameraControl::setState(CameraState newState) {
    if (state_ == newState) return;
    const char* state_str = stateToString(newState);
    if (StateMachine::changeState("camera", state_str)) {
        state_ = newState;
        ESP_LOGI(TAG, "State transition -> %s", state_str);
    } else {
        ESP_LOGE(TAG, "Failed to transition to %s", state_str);
    }
}

esp_err_t CameraControl::initialize() {
    if (state_ != CameraState::OFF) return ESP_OK;

    setState(CameraState::INITIALIZING);
    if (!sensor_->initialize(config_.camera_config)) {
        setState(CameraState::ERROR);
        return ESP_FAIL;
    }
    if (config_.start_on_init) {
        if (start() != ESP_OK) return ESP_FAIL;
    } else {
        setState(CameraState::IDLE);
    }
    ESP_LOGI(TAG, "Camera control initialized");
    return ESP_OK;
}

esp_err_t CameraControl::start() {
    if (state_ == CameraState::RUNNING || state_ == CameraState::CAPTURING) return ESP_OK;
    if (state_ != CameraState::IDLE && state_ != CameraState::OFF && state_ != CameraState::INITIALIZING) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!sensor_->start()) {
        setState(CameraState::ERROR);
        return ESP_FAIL;
    }
    setState(CameraState::RUNNING);
    return ESP_OK;
}

esp_err_t CameraControl::stop() {
    if (state_ == CameraState::OFF) return ESP_OK;
    sensor_->stop();
    setState(CameraState::OFF);
    return ESP_OK;
}

bool CameraControl::isReady() const {
    return sensor_->isReady() && (state_ != CameraState::ERROR);
}

esp_err_t CameraControl::captureOnce(CameraCommon::CameraFrame& frame, uint32_t timeout_ms) {
    if (!isReady()) return ESP_ERR_INVALID_STATE;
    bool was_continuous = (state_ == CameraState::CAPTURING);
    if (was_continuous) stopContinuousCapture();
    bool ok = sensor_->capture(frame, timeout_ms);
    if (was_continuous) startContinuousCapture(nullptr);
    return ok ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t CameraControl::startContinuousCapture(FrameCallback callback) {
    if (!isReady()) return ESP_ERR_INVALID_STATE;
    if (state_ == CameraState::CAPTURING) return ESP_OK;
    sensor_->setFrameCallback(callback);
    if (!sensor_->startContinuousCapture(config_.capture_interval_ms)) {
        setState(CameraState::ERROR);
        return ESP_FAIL;
    }
    setState(CameraState::CAPTURING);
    return ESP_OK;
}

esp_err_t CameraControl::stopContinuousCapture() {
    if (state_ != CameraState::CAPTURING) return ESP_OK;
    sensor_->stopContinuousCapture();
    setState(CameraState::RUNNING);
    return ESP_OK;
}

esp_err_t CameraControl::startStreaming() {
    if (!isReady()) return ESP_ERR_INVALID_STATE;
    if (!sensor_->startStreaming()) return ESP_FAIL;
    setState(CameraState::STREAMING);
    return ESP_OK;
}

esp_err_t CameraControl::stopStreaming() {
    if (state_ != CameraState::STREAMING) return ESP_OK;
    sensor_->stopStreaming();
    setState(CameraState::RUNNING);
    return ESP_OK;
}

bool CameraControl::isStreaming() const {
    return state_ == CameraState::STREAMING && sensor_->isStreaming();
}

void CameraControl::returnFrame(CameraCommon::CameraFrame& frame) {
    sensor_->returnFrame(frame);
}

#pragma once
#include "camera_common.hpp"
#include "camera_sensor.hpp"
#include <state_machine.hpp>

// --- Config struct is now outside the class ---
struct CameraControlConfig {
    CameraCommon::CameraConfig camera_config;
    bool start_on_init = true;
    uint32_t capture_interval_ms = 100;
};
// -------------------------------------------

enum class CameraState : uint8_t {
    OFF,
    INITIALIZING,
    IDLE,
    RUNNING,
    CAPTURING,
    STREAMING,
    ERROR,
    COUNT
};

inline const char* stateToString(CameraState s) {
    switch (s) {
        case CameraState::OFF:          return "off";
        case CameraState::INITIALIZING: return "initializing";
        case CameraState::IDLE:         return "idle";
        case CameraState::RUNNING:      return "running";
        case CameraState::CAPTURING:    return "capturing";
        case CameraState::STREAMING:    return "streaming";
        case CameraState::ERROR:        return "error";
        default: return "unknown";
    }
}

class CameraControl {
public:
    using FrameCallback = std::function<void(const CameraCommon::CameraFrame&)>;

    explicit CameraControl(const CameraControlConfig& config = CameraControlConfig());
    ~CameraControl();

    esp_err_t initialize();
    esp_err_t start();
    esp_err_t stop();
    bool isReady() const;

    esp_err_t captureOnce(CameraCommon::CameraFrame& frame, uint32_t timeout_ms = 1000);
    esp_err_t startContinuousCapture(FrameCallback callback);
    esp_err_t stopContinuousCapture();
    esp_err_t startStreaming();
    esp_err_t stopStreaming();
    bool isStreaming() const;
    void returnFrame(CameraCommon::CameraFrame& frame);

    CameraState getState() const { return state_; }
    CameraSensor* getSensor() { return sensor_.get(); }

private:
    CameraControlConfig config_;
    std::unique_ptr<CameraSensor> sensor_;
    CameraState state_ = CameraState::OFF;

    void setState(CameraState newState);

    static const char* TAG;
};
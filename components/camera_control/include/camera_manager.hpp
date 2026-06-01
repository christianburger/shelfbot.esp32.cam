#pragma once
#include "camera_control.hpp"

class CameraManager {
public:
    static CameraManager& getInstance() {
        static CameraManager instance;
        return instance;
    }

    esp_err_t initialize();
    esp_err_t startCapture(CameraControl::FrameCallback callback = nullptr);
    esp_err_t stopCapture();
    QueueHandle_t getFrameQueue() const { return frame_queue_; }
    CameraControl* getControl() { return control_.get(); }
    bool waitUntilReady(uint32_t timeout_ms = 5000);

private:
    CameraManager() = default;
    ~CameraManager() = default;

    std::unique_ptr<CameraControl> control_;
    QueueHandle_t frame_queue_ = nullptr;
    bool initialized_ = false;

    static void internalFrameCallback(const CameraCommon::CameraFrame& frame);
};
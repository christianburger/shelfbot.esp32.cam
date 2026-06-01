#pragma once
#include "camera_driver_interface.hpp"

class CameraSensor {
public:
    using FrameCallback = std::function<void(const CameraCommon::CameraFrame&)>;

    explicit CameraSensor(std::unique_ptr<ICameraDriver> driver);
    ~CameraSensor();

    bool initialize(const CameraCommon::CameraConfig& config);
    bool start();
    bool stop();
    bool capture(CameraCommon::CameraFrame& frame, uint32_t timeout_ms = 1000);
    void setFrameCallback(FrameCallback callback);
    bool startContinuousCapture(uint32_t interval_ms = 0);
    bool stopContinuousCapture();
    bool startStreaming();
    bool stopStreaming();
    bool isStreaming() const;
    bool isReady() const;
    void returnFrame(CameraCommon::CameraFrame& frame);
    const CameraCommon::CameraConfig& getConfig() const { return config_; }

private:
    std::unique_ptr<ICameraDriver> driver_;
    CameraCommon::CameraConfig config_;
    FrameCallback callback_;
    bool initialized_ = false;
    bool running_ = false;
    bool streaming_ = false;
    TaskHandle_t capture_task_ = nullptr;
    QueueHandle_t frame_queue_ = nullptr;

    static void captureTask(void* arg);
    void captureLoop();
};
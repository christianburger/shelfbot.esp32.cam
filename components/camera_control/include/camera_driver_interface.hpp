#pragma once
#include "camera_common.hpp"
#include <memory>

class ICameraDriver {
public:
    virtual ~ICameraDriver() = default;
    virtual const char* configure(const CameraCommon::CameraConfig& config) = 0;
    virtual const char* init() = 0;
    virtual const char* start() = 0;
    virtual const char* stop() = 0;
    virtual void deinit() = 0;
    virtual bool isReady() const = 0;
    virtual bool captureFrame(CameraCommon::CameraFrame& frame, uint32_t timeout_ms) = 0;
    virtual void returnFrame(CameraCommon::CameraFrame& frame) = 0;
    virtual bool startStreaming() = 0;
    virtual bool stopStreaming() = 0;
    virtual bool isStreaming() const = 0;
    virtual const char* getSensorModel() const = 0;
};

// Factory function declared here – implemented in camera_driver.cpp
std::unique_ptr<ICameraDriver> create_esp32_camera_driver();
#include <camera_driver_interface.hpp>
#include <esp_camera.h>

class Esp32CameraDriver : public ICameraDriver {
public:
    const char* configure(const CameraCommon::CameraConfig& config) override {
        camera_config_.pin_pwdn  = config.pin_pwdn;
        camera_config_.pin_reset = config.pin_reset;
        camera_config_.pin_xclk  = config.pin_xclk;
        camera_config_.pin_sccb_sda = config.pin_sccb_sda;
        camera_config_.pin_sccb_scl = config.pin_sccb_scl;
        camera_config_.pin_d7 = config.pin_d7;
        camera_config_.pin_d6 = config.pin_d6;
        camera_config_.pin_d5 = config.pin_d5;
        camera_config_.pin_d4 = config.pin_d4;
        camera_config_.pin_d3 = config.pin_d3;
        camera_config_.pin_d2 = config.pin_d2;
        camera_config_.pin_d1 = config.pin_d1;
        camera_config_.pin_d0 = config.pin_d0;
        camera_config_.pin_vsync = config.pin_vsync;
        camera_config_.pin_href  = config.pin_href;
        camera_config_.pin_pclk  = config.pin_pclk;
        camera_config_.xclk_freq_hz = config.xclk_freq_hz;
        camera_config_.ledc_timer   = config.ledc_timer;
        camera_config_.ledc_channel = config.ledc_channel;
        camera_config_.pixel_format = static_cast<pixformat_t>(config.pixel_format);
        camera_config_.frame_size   = static_cast<framesize_t>(config.frame_size);
        camera_config_.jpeg_quality = config.jpeg_quality;
        camera_config_.fb_count     = config.fb_count;
        camera_config_.fb_location  = config.fb_location;
        camera_config_.grab_mode    = config.grab_mode;
        camera_config_.sccb_i2c_port = config.sccb_i2c_port;
        return nullptr;
    }

    const char* init() override {
        esp_err_t err = esp_camera_init(&camera_config_);
        if (err != ESP_OK) {
            static char buf[64];
            snprintf(buf, sizeof(buf), "esp_camera_init failed: 0x%x", err);
            return buf;
        }
        sensor_ = esp_camera_sensor_get();
        ready_ = true;
        return nullptr;
    }

    const char* start() override { return nullptr; }
    const char* stop() override { return nullptr; }
    void deinit() override { ready_ = false; }

    bool isReady() const override { return ready_; }

    bool captureFrame(CameraCommon::CameraFrame& frame, uint32_t timeout_ms) override {
        (void)timeout_ms;
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) return false;
        frame.buffer = fb->buf;
        frame.length = fb->len;
        frame.width = fb->width;
        frame.height = fb->height;
        frame.format = static_cast<CameraCommon::PixelFormat>(fb->format);
        frame.timestamp_us = esp_timer_get_time();
        frame.is_new = true;
        frame._internal = fb;
        return true;
    }

    void returnFrame(CameraCommon::CameraFrame& frame) override {
        if (frame._internal) {
            esp_camera_fb_return(static_cast<camera_fb_t*>(frame._internal));
            frame._internal = nullptr;
        }
    }

    bool startStreaming() override { streaming_ = true; return true; }
    bool stopStreaming() override  { streaming_ = false; return true; }
    bool isStreaming() const override { return streaming_; }

    const char* getSensorModel() const override {
        if (!sensor_) return "unknown";
        static char buf[32];
        snprintf(buf, sizeof(buf), "0x%02X", sensor_->id.PID);
        return buf;
    }

private:
    camera_config_t camera_config_ = {};
    sensor_t* sensor_ = nullptr;
    bool ready_ = false;
    bool streaming_ = false;
};

std::unique_ptr<ICameraDriver> create_esp32_camera_driver() {
    return std::make_unique<Esp32CameraDriver>();
}
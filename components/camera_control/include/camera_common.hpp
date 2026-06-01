#pragma once
#include <idf_c_includes.hpp>

namespace CameraCommon {

constexpr int NUM_CAMERAS = 1;

enum class PixelFormat : uint8_t {
    JPEG = PIXFORMAT_JPEG,
    RGB565 = PIXFORMAT_RGB565,
    YUV422 = PIXFORMAT_YUV422,
    GRAYSCALE = PIXFORMAT_GRAYSCALE
};

enum class FrameSize : uint8_t {
    QQVGA = FRAMESIZE_QQVGA,
    QVGA  = FRAMESIZE_QVGA,
    VGA   = FRAMESIZE_VGA,
    SVGA  = FRAMESIZE_SVGA,
    XGA   = FRAMESIZE_XGA,
    HD    = FRAMESIZE_HD,
    FHD   = FRAMESIZE_FHD,
};

struct CameraConfig {
    int pin_pwdn  = 32;
    int pin_reset = -1;
    int pin_xclk  = 0;
    int pin_sccb_sda = 26;
    int pin_sccb_scl = 27;
    int pin_d7 = 35;
    int pin_d6 = 34;
    int pin_d5 = 39;
    int pin_d4 = 36;
    int pin_d3 = 21;
    int pin_d2 = 19;
    int pin_d1 = 18;
    int pin_d0 = 5;
    int pin_vsync = 25;
    int pin_href  = 23;
    int pin_pclk  = 22;

    uint32_t xclk_freq_hz = 20000000;
    ledc_timer_t ledc_timer = LEDC_TIMER_0;
    ledc_channel_t ledc_channel = LEDC_CHANNEL_0;
    PixelFormat pixel_format = PixelFormat::JPEG;
    FrameSize frame_size = FrameSize::SVGA;
    uint8_t jpeg_quality = 12;
    size_t fb_count = 1;
    camera_fb_location_t fb_location = CAMERA_FB_IN_PSRAM;
    camera_grab_mode_t grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    int sccb_i2c_port = 0;
};

struct CameraFrame {
    uint8_t* buffer;
    size_t length;
    uint32_t width;
    uint32_t height;
    PixelFormat format;
    uint64_t timestamp_us;
    bool is_new;
    void* _internal = nullptr;
};

} // namespace CameraCommon
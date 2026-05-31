#pragma once

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// MicrorosSync – camera edition
//
// Publishers:
//   /camera/image_raw/compressed   sensor_msgs/CompressedImage  (on demand)
//   /camera/camera_info            sensor_msgs/CameraInfo       (1 Hz timer)
//
// Subscriber:
//   /camera/led                    std_msgs/Bool
//     → drives GPIO CONFIG_CAMERA_LED_GPIO (default 4, ESP32-CAM flash)
// ---------------------------------------------------------------------------
class MicrorosSync {
public:
    static MicrorosSync &getInstance();

    bool init();
    void start();

    // Call from camera_task after esp_camera_fb_get() succeeds.
    static void publishCompressedImage(const uint8_t *buf, size_t len,
                                       uint32_t frame_seq);

private:
    MicrorosSync();
    ~MicrorosSync();
};

/*
 * microros_sync_c.cpp
 * Thin C-linkage wrappers around the MicrorosSync C++ singleton.
 * Compiled as C++ but callable from plain C (main.c).
 */

#include "microros_sync_c.h"
#include "microros_sync.hpp"
#include "wifi_manager.hpp"

extern "C" {

void microros_init(void) {
    MicrorosSync::getInstance().init();
}

void microros_start(void) {
    MicrorosSync::getInstance().start();
}

void microros_publish_image(const uint8_t *buf, size_t len, uint32_t seq) {
    MicrorosSync::publishCompressedImage(buf, len, seq);
}

} // extern "C"

#pragma once

/*
 * C-callable shim for MicrorosSync.
 * Include this from .c files; implementation is in microros_sync_c.cpp.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

void      microros_init(void);
void      microros_start(void);
void      microros_publish_image(const uint8_t *buf, size_t len, uint32_t seq);
esp_err_t wifi_manager_init(void);

#ifdef __cplusplus
}
#endif

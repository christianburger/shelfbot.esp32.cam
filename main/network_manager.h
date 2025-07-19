#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "esp_err.h"

#define WIFI_SSID "dlink-30C0"
#define WIFI_PASS "ypics98298"
#define WIFI_RECONNECT_DELAY_MS 1000
#define NETWORK_TASK_STACK_SIZE 8192
#define NETWORK_TASK_PRIORITY 5
#define NETWORK_TASK_CORE_ID 0

// Initialize WiFi and web server
esp_err_t network_manager_init(void);

// Network task function - needs to be visible to main.c
void network_task(void *pvParameters);

#endif // NETWORK_MANAGER_H
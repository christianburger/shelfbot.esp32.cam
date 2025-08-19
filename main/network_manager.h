#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "freertos/event_groups.h"

// Event group to signal when we are connected
extern EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

#define WIFI_SSID "dlink-30C0"
#define WIFI_PASS "ypics98298"
#define NETWORK_TASK_STACK_SIZE 8192
#define NETWORK_TASK_PRIORITY 5
#define NETWORK_TASK_CORE_ID 0

// Network task function - needs to be visible to main.c
void network_task(void *pvParameters);

#endif // NETWORK_MANAGER_H
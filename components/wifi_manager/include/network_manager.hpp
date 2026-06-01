#pragma once
#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <idf_c_includes.hpp>

class NetworkManager {
public:
    static NetworkManager& get_instance() {
        static NetworkManager instance;
        return instance;
    }

    NetworkManager(const NetworkManager&)            = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // Initialise HTTP server (Wi-Fi must already be started by wifi_manager).
    // Must be called after nvs_flash_init, esp_netif_init, esp_event_loop_create_default,
    // and wifi_manager_init().
    esp_err_t init(QueueHandle_t frame_queue);

    // FreeRTOS task entry point – not used directly in current main, kept for compatibility.
    static void network_task(void* arg);

private:
    NetworkManager()  = default;
    ~NetworkManager() = default;

    static const char* TAG;

    static esp_err_t start_http_server(QueueHandle_t frame_queue);
};

#endif // NETWORK_MANAGER_HPP
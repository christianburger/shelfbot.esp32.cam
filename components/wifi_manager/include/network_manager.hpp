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

    // Initialise Wi-Fi, start HTTP server, and begin the network task.
    // Must be called after nvs_flash_init, esp_netif_init, and
    // esp_event_loop_create_default.
    esp_err_t init(QueueHandle_t frame_queue);

    // FreeRTOS task entry point — do not call directly.
    static void network_task(void* arg);

private:
    NetworkManager()  = default;
    ~NetworkManager() = default;

    static const char* TAG;

    static void wifi_event_handler(void*             arg,
                                   esp_event_base_t  event_base,
                                   int32_t           event_id,
                                   void*             event_data);

    static esp_err_t wifi_init_sta();
    static esp_err_t start_http_server(QueueHandle_t frame_queue);
};

#endif // NETWORK_MANAGER_HPP

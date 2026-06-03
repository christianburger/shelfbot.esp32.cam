idf_component_register(
    SRCS  "wifi_manager.cpp"
          "network_manager.cpp"
          "sntp_sync.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        idf_includes
        http_server
        mdns
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        freertos
        log
        state_machine
        firmware_version
        sntp_sync
        lwip
)

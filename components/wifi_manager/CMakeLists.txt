idf_component_register(
    SRCS "wifi_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        idf_includes
        mdns
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        freertos
        log
)

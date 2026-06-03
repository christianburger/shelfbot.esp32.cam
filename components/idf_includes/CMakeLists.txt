idf_component_register(
    SRCS ""
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        log
        esp_system
        esp_timer
        esp_common
        esp_event
        esp_netif
        esp_psram
        esp_hw_support
        esp_driver_gpio
        esp_wifi
        esp_http_server
        mdns
        nvs_flash
        lwip
        json
        micro_ros_espidf_component
        esp32-camera
        sntp_sync
)

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp32-camera
        esp_system
        freertos
        nvs_flash
        esp_wifi
        esp_event
        esp_timer
        esp_psram
        idf_includes
        wifi_manager
        microros_sync
        micro_ros_espidf_component
)
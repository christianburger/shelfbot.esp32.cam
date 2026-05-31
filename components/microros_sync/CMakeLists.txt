idf_component_register(
    SRCS "microros_sync.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        idf_includes
        micro_ros_espidf_component
        wifi_manager
        esp32-camera
        esp_driver_gpio
        freertos
        log
        esp_timer
)

idf_component_register(
    SRCS
        "src/camera_driver.cpp"
        "src/camera_sensor.cpp"
        "src/camera_control.cpp"
        "src/camera_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        idf_includes
        esp32-camera
        state_machine
        freertos
        esp_timer
        esp_psram
        driver
)
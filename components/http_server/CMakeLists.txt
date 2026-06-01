idf_component_register(
    SRCS
        "http_server.cpp"
        "controller.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        idf_includes
        camera_control
        esp32-camera
        esp_http_server
        json
)

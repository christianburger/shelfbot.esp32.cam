idf_component_register(SRCS "main.c" "network_manager.c" "controller.c"
                      INCLUDE_DIRS "."
                      REQUIRES esp32-camera 
                               esp_system 
                               freertos
                               nvs_flash
                               esp_wifi
                               esp_http_server
                               esp_event
                               esp_timer
                               esp_psram)
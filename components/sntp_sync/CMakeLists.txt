idf_component_register(
    SRCS "sntp_sync.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        lwip
        log
        esp_timer
)

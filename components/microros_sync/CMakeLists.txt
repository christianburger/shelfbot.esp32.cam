idf_component_register(
    SRCS "microros_sync.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        idf_includes
        state_machine
        wifi_manager
        mdns
        micro_ros_espidf_component
        shelfbot_timestamp
)

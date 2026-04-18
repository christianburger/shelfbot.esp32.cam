idf_component_register(SRCS "shelfbot_camera.c"
                      INCLUDE_DIRS "."
                      REQUIRES micro_ros_espidf_component main espressif__mdns)

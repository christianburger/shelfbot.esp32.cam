cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(camera_project)

# Resolve duplicate 64-bit atomic symbols between micro-ROS rcutils and ESP-IDF newlib
target_link_options(camera_project.elf PRIVATE -Wl,--allow-multiple-definition)

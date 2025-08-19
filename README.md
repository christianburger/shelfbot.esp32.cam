# Micro-ROS ESP32 Camera Node

**Objective:** A native Micro-ROS node for the ESP32-CAM, designed to publish compressed images directly into a ROS 2 ecosystem. This firmware makes the ESP32-CAM a first-class sensor for robotics projects.

---

## Current Status (Work in Progress)

This project is in a stable, transitional phase. The firmware currently runs both the legacy **web server** and the new **micro-ROS node** simultaneously. The micro-ROS node connects reliably to the agent, but the core functionality (publishing images) is not yet implemented.

The final goal is to decommission the web server and create a pure, high-performance micro-ROS camera node.

---

## Setup and Build

This project requires a one-time setup of the Micro-ROS build system within the ESP-IDF environment.

1.  **Clone the Project:**
    Clone this repository and its submodules. The `--recursive` flag is essential.
    ```bash
    git clone --recursive <repository_url>
    cd shelfbot.esp32.cam
    ```

2.  **Install Build Dependencies:**
    Source your ESP-IDF environment script, then use `pip` to install the necessary ROS 2 build tools into the IDF's local Python environment.
    ```bash
    . $IDF_PATH/export.sh
    pip3 install catkin_pkg lark-parser colcon-common-extensions
    ```

3.  **Configure the Project:**
    Project configuration is currently managed in source files:
    - **WiFi Credentials:** Set your SSID and password in `main/network_manager.h`.
    - **micro-ROS Agent:** The agent is discovered automatically via an mDNS query for the hostname `gentoo-laptop.local`. This can be changed in `components/shelfbot_camera/shelfbot_camera.c`.

4.  **Build and Flash:**
    The first build will take a significant amount of time as it compiles the entire Micro-ROS library from source. Subsequent builds will be much faster.
    ```bash
    idf.py build
    idf.py flash monitor
    ```

---

## Micro-ROS Interface (Under Development)

The firmware exposes the following ROS 2 topics.

### Publishers

-   **Compressed Image Publisher:**
    -   **Topic:** `/camera/image_raw/compressed`
    -   **Message Type:** `sensor_msgs/msg/CompressedImage`
    -   **Status:** **(Not Yet Implemented)** The publisher is created, but no image data is being published to this topic yet.

-   **Camera Info Publisher:**
    -   **Topic:** `/camera/camera_info`
    -   **Message Type:** `sensor_msgs/msg/CameraInfo`
    -   **Status:** Active. Publishes placeholder data periodically.

### Subscribers

-   **LED Control Subscriber:**
    -   **Topic:** `/camera/led`
    -   **Message Type:** `std_msgs/msg/Bool`
    -   **Status:** **(Not Yet Implemented)** The subscriber is created, but the callback function is a placeholder.

---

## Testing and Validation

To use this firmware, you must have a Micro-ROS agent running on your host computer.

1.  **Start the Micro-ROS Agent:**
    Run the agent in a ROS 2 environment, configured for UDP communication on port 8888. Your host computer must be discoverable via mDNS with the hostname `gentoo-laptop`.
    ```bash
    docker run -it --rm -v /dev:/dev -v /dev/shm:/dev/shm --privileged --net=host microros/micro-ros-agent:humble udp4 --port 8888
    ```

2.  **Verify Connection:**
    Monitor the ESP32's serial output. It should connect to your WiFi, discover the agent via mDNS, and print `Micro-ROS initialized successfully`. The agent's console will also show a client connection.

3.  **Verify ROS 2 Topics:**
    In a separate terminal with your ROS 2 environment sourced, you can see the node and its topics.
    -   **List topics:** `ros2 topic list`
    -   **Echo camera info:** `ros2 topic echo /camera/camera_info`

4.  **Access Legacy Web Server:**
    For a visual confirmation that the camera is working, you can access the legacy web server by navigating to the ESP32's IP address in a web browser. The video stream is available at `http://<ESP32_IP>/stream`.

---

## Hardware Support

### Supported Camera Modules
- OV2640 (Primary)
- OV7670, OV7725, NT99141, OV3660, OV5640
- GC2145, GC032A, GC0308, BF3005, BF20A6, SC030IOT

### Pin Configuration (ESP32-CAM)
```c
Power Down:  GPIO32    |  Data 4:     GPIO36
Reset:       -1 (NC)   |  Data 3:     GPIO21  
XCLK:        GPIO0     |  Data 1:     GPIO18
SIOD (SDA):  GPIO26    |  Data 0:     GPIO5
SIOC (SCL):  GPIO27    |  VSYNC:      GPIO25
Data 7:      GPIO35    |  HREF:       GPIO23
Data 6:      GPIO34    |  PCLK:       GPIO22
Data 5:      GPIO39    |  Data 2:     GPIO19
```

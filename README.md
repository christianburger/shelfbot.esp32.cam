# Micro-ROS ESP32 Camera Node

**Objective:** A native Micro-ROS node for the ESP32-CAM, designed to publish compressed images directly into a ROS 2 ecosystem. This firmware makes the ESP32-CAM a first-class sensor for robotics projects.

---

## Current Status

This project is fully functional. The firmware runs both a **web server** for simple streaming and a **micro-ROS node** that is fully integrated into a ROS 2 system. The micro-ROS node connects reliably to the agent and publishes compressed camera images and receives control commands.

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

## Micro-ROS Interface

The firmware exposes the following ROS 2 topics.

### Publishers

-   **Compressed Image Publisher:**
    -   **Topic:** `/camera/image_raw/compressed`
    -   **Message Type:** `sensor_msgs/msg/CompressedImage`
    -   **Status:** Active. Publishes JPEG frames from the camera.

-   **Camera Info Publisher:**
    -   **Topic:** `/camera/camera_info`
    -   **Message Type:** `sensor_msgs/msg/CameraInfo`
    -   **Status:** Active. Publishes placeholder data periodically.

### Subscribers

-   **LED Control Subscriber:**
    -   **Topic:** `/camera/led`
    -   **Message Type:** `std_msgs/msg/Bool`
    -   **Status:** Active. Controls the onboard LED.

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

3.  **Verify Micro-ROS Functionality:**
    In a separate terminal with your ROS 2 environment sourced, you can verify the topics and data streams.

    -   **List topics:**
        ```bash
        ros2 topic list
        ```

    -   **Check Raw Image Data:**
        To confirm that image data is being sent, you can print a single message to the terminal. This is a good way to check for a valid JPEG header.
        ```bash
        ros2 topic echo --once /camera/image_raw/compressed
        ```

    -   **Visualize the Video Stream (Recommended):**
        The best way to validate the camera is to view the video stream using `rqt_image_view`.
        
        First, install it if you haven't already:
        ```bash
        sudo apt-get update
        sudo apt-get install ros-humble-rqt-image-view
        ```
        
        Then, run the viewer:
        ```bash
        ros2 run rqt_image_view rqt_image_view
        ```
        In the GUI window, select the `/camera/image_raw/compressed` topic from the dropdown menu to see the live feed.

4.  **Access Legacy Web Server:**
    For a quick visual confirmation, you can also access the legacy web server by navigating to the ESP32's IP address in a web browser. The video stream is available at `http://<ESP32_IP>/stream`.

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


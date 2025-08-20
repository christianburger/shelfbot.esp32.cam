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

## System Architecture and Data Flow

This project integrates a resource-constrained microcontroller (ESP32-CAM) into a full-fledged ROS 2 system. This is achieved through a client-agent architecture.

-   **ESP32-CAM Firmware:** Acts as the **micro-ROS Client**. It runs the camera sensor and publishes data to the agent, while also subscribing to commands from the agent.
-   **micro-ROS Agent:** A program running on the main ROS 2 computer that acts as a **bridge**. It relays topics from the micro-ROS client onto the main ROS 2 network, and vice-versa. It is a transparent proxy.
-   **ROS 2 Application:** Any standard ROS 2 node running on the computer (e.g., `rqt_image_view`, RViz, or a custom control node).

### Data Flow Summary

| Topic Name                     | Message Type                      | Producer            | Consumer            | Purpose                                              |
| :----------------------------- | :-------------------------------- | :------------------ | :------------------ | :--------------------------------------------------- |
| `/camera/image_raw/compressed` | `sensor_msgs/msg/CompressedImage` | `ESP32-CAM`         | `ROS 2 Application` | Streams the live JPEG video frames from the camera.  |
| `/camera/camera_info`          | `sensor_msgs/msg/CameraInfo`      | `ESP32-CAM`         | `ROS 2 Application` | Provides camera calibration and metadata.            |
| `/camera/led`                  | `std_msgs/msg/Bool`               | `ROS 2 Application` | `ESP32-CAM`         | Sends a command to turn the camera's flash LED on or off. |

### Detailed Data Flow

**1. Image Stream (`/camera/image_raw/compressed`)**

*   The `ESP32-CAM` **captures** a frame from its image sensor.
*   The firmware **packages** this JPEG data into a `sensor_msgs/msg/CompressedImage` message.
*   This message is **published** over the network to the **micro-ROS Agent**.
*   The Agent then **re-publishes** it onto the main ROS 2 network.
*   A `ROS 2 Application` (like `rqt_image_view`) **subscribes** to the topic and receives the frame for display or processing.

**2. Camera Info (`/camera/camera_info`)**

*   A timer on the `ESP32-CAM` triggers periodically.
*   The firmware **creates** a `sensor_msgs/msg/CameraInfo` message containing the camera's metadata.
*   This message is **published** to the **micro-ROS Agent** and then onto the ROS 2 network.
*   A `ROS 2 Application` can **subscribe** to this topic to get the camera's parameters for tasks like image rectification.

**3. LED Control (`/camera/led`)**

*   A `ROS 2 Application` **publishes** a `std_msgs/msg/Bool` message (with a value of `true` or `false`) to the `/camera/led` topic.
*   The **micro-ROS Agent** **receives** this message and forwards it over the network to the `ESP32-CAM`.
*   The firmware on the `ESP32-CAM` **receives** the message, and its subscriber callback function is triggered.
*   The callback function then **executes** the command, turning the physical flash LED on or off.

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
# Micro-ROS ESP32 Camera Node

**Objective:** A native Micro-ROS node for the ESP32-CAM, designed to publish compressed images directly into a ROS 2 ecosystem. This firmware makes the ESP32-CAM a first-class sensor for robotics projects.

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
    . /path/to/your/esp-idf/export.sh
    pip3 install catkin_pkg lark-parser colcon-common-extensions
    ```

3.  **Configure the Project:**
    Run the ESP-IDF configuration menu.
    ```bash
    idf.py menuconfig
    ```
    Navigate to `Component config` ---> `Micro ROS configuration` and set the following:
    - **Transport:** `Use WiFi for micro-ROS transport`
    - **Agent Hostname:** `shelfbot.camera.local` (or the IP of your agent)
    - **Agent Port:** `8888`
    - **WiFi SSID & Password:** Under the `WiFi Configuration` submenu.

4.  **Build and Flash:**
    The first build will take a significant amount of time as it compiles the entire Micro-ROS library from source. Subsequent builds will be much faster.
    ```bash
    idf.py build
    idf.py flash monitor
    ```

---

## Micro-ROS Interface

The firmware exposes the following ROS 2 topics:

### Publishers

-   **Compressed Image Publisher:**
    -   **Topic:** `/camera/image_raw/compressed`
    -   **Message Type:** `sensor_msgs/msg/CompressedImage`
    -   **Purpose:** The primary output of the camera, publishing JPEG frames.

-   **Camera Info Publisher:**
    -   **Topic:** `/camera/camera_info`
    -   **Message Type:** `sensor_msgs/msg/CameraInfo`
    -   **Purpose:** Publishes camera calibration data. (Currently uses placeholder values).

### Subscribers

-   **LED Control Subscriber:**
    -   **Topic:** `/camera/led`
    -   **Message Type:** `std_msgs/msg/Bool`
    -   **Purpose:** Toggles the onboard LED flash.

---

## Testing and Validation

To use this firmware, you must have a Micro-ROS agent running on your host computer.

1.  **Start the Micro-ROS Agent:**
    Run the agent in a ROS 2 environment, configured for UDP communication on port 8888.
    ```bash
    docker run -it --rm -v /dev:/dev -v /dev/shm:/dev/shm --privileged --net=host microros/micro-ros-agent:humble udp4 --port 8888
    ```

2.  **Verify Connection:**
    Monitor the ESP32's serial output. It should connect to your WiFi and then successfully connect to the agent.

3.  **Verify ROS 2 Topics:**
    In a separate terminal with your ROS 2 environment sourced, check the topics.
    -   **List topics:** `ros2 topic list`
    -   **View image stream:** `ros2 run rqt_image_view rqt_image_view` and select the `/camera/image_raw/compressed` topic.
    -   **Test LED control:** `ros2 topic pub /camera/led std_msgs/msg/Bool "data: true"`

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

# Plan: ESP32-CAM Hybrid Web Server & Micro-ROS Firmware

**Objective:** Augment the existing web server-based ESP32-CAM firmware with a native Micro-ROS node. This will enable the camera to publish images directly into the ROS 2 ecosystem while retaining the web interface for diagnostics and direct access.

---

### Phase 1: Project Setup & Dependency Integration

**Goal:** Integrate the Micro-ROS client library into the existing ESP32-CAM ESP-IDF project.

1.  **Install Micro-ROS Build System:**
    *   Follow the official Micro-ROS documentation to install the `micro_ros_setup` tool.
    *   Use this tool to download the necessary Micro-ROS libraries and build scripts into the ESP-IDF project.

2.  **Configure Project:**
    *   Ensure PSRAM support is enabled in `idf.py menuconfig`, as this is critical for camera frame buffers.
    *   WiFi credentials are set in `main/network_manager.h`.
    *   The micro-ROS agent is discovered via mDNS.

3.  **Update `CMakeLists.txt`:**
    *   Modify the component `CMakeLists.txt` files to ensure all necessary dependencies (like `main`, `espressif__mdns`, etc.) are available to the components that require them.

---

### Phase 2: Code Refactoring - Hybrid Integration

**Goal:** Run the micro-ROS node concurrently with the existing web server, sharing a single network connection.

1.  **Create Standalone micro-ROS Component:**
    *   Encapsulate all micro-ROS logic into a new component (`shelfbot_camera`).
    *   This component will contain the task that initializes the node, publishers, and subscribers.

2.  **Synchronize Task Startup:**
    *   The main `network_task` remains in control of the WiFi connection and mDNS service initialization.
    *   Use a FreeRTOS Event Group to signal the status of the network connection.
    *   The `shelfbot_camera_task` will wait on this event group to ensure both WiFi is connected and the mDNS service is started before it attempts to discover and connect to the micro-ROS agent.

3.  **Define the Micro-ROS Communication Interface:**
    *   **Publishers:**
        *   **Compressed Image Publisher:** `/camera/image_raw/compressed` (`sensor_msgs/msg/CompressedImage`)
        *   **Camera Info Publisher:** `/camera/camera_info` (`sensor_msgs/msg/CameraInfo`)
    *   **Subscribers:**
        *   **LED Control Subscriber:** `/camera/led` (`std_msgs/msg/Bool`)

---

### Phase 3: Camera Integration & Dual Publishing

**Goal:** Modify the camera task to publish frames to both the web server and Micro-ROS.

1.  **Modify `camera_task`:**
    *   After a frame is captured, the task will perform two actions:
    *   **1. Publish to Micro-ROS:** Populate a `sensor_msgs__msg__CompressedImage` message with the frame data and use `rcl_publish()` to send it.
    *   **2. Send to Web Server:** Use the existing `xQueueSend` to pass the same frame buffer to the web server's stream handler.

2.  **Implement `camera_info` Publishing:**
    *   Create a timer that periodically publishes a `CameraInfo` message with placeholder values.

3.  **Implement LED Subscriber Callback:**
    *   Create a callback function for the `/camera/led` subscriber that uses ESP-IDF GPIO functions to control the flash LED.

---

### Phase 4: Testing and Validation

**Goal:** Verify that both the web server and the micro-ROS interface are working correctly.

1.  **Start the Micro-ROS Agent:**
    *   On the host computer, run the agent, ensuring it is on the same network and its hostname is discoverable via mDNS.
    *   `ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888`

2.  **Flash and Monitor the ESP32:**
    *   Build, flash, and monitor the firmware.
    *   Confirm that it connects to WiFi, discovers the agent, and initializes the micro-ROS node successfully.

3.  **Verify Both Interfaces:**
    *   **Web Server:** Access the video stream via `http://<ESP32_IP>/stream` in a browser.
    *   **Micro-ROS:** Use ROS 2 tools to verify the topics.
        *   `ros2 topic list`
        *   `ros2 run rqt_image_view rqt_image_view` to view the `/camera/image_raw/compressed` stream.
        *   `ros2 topic pub /camera/led std_msgs/msg/Bool "data: true"` to test LED control.

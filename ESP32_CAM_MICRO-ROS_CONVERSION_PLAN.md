# Plan: ESP32-CAM Firmware Conversion to Micro-ROS

**Objective:** Convert the existing web server-based ESP32-CAM firmware into a native Micro-ROS node. This will enable the camera to publish images directly into the ROS 2 ecosystem, making it a first-class sensor for the Shelfbot project.

---

### Phase 1: Project Setup & Dependency Integration

**Goal:** Integrate the Micro-ROS client library into the existing ESP32-CAM ESP-IDF project.

1.  **Install Micro-ROS Build System:**
    *   Follow the official Micro-ROS documentation to install the `micro_ros_setup` tool.
    *   Use this tool to download the necessary Micro-ROS libraries and build scripts into the ESP-IDF project. This is typically done by running a command like `ros2 run micro_ros_setup create_firmware_ws.sh freertos esp-idf`.

2.  **Configure Project:**
    *   Run `idf.py menuconfig`.
    *   Navigate to the "Micro-ROS" component settings.
    *   Configure the Wi-Fi SSID and password for the ESP32 to connect to.
    *   Configure the IP address and port of the computer running the Micro-ROS Agent.
    *   Ensure PSRAM support is enabled, as this is critical for camera frame buffers.

3.  **Update `CMakeLists.txt`:**
    *   Modify the main `CMakeLists.txt` to add `micro_ros_common` as a required component. This will ensure the Micro-ROS libraries are correctly linked during the build.

---

### Phase 2: Code Refactoring - From Web Server to Micro-ROS

**Goal:** Remove the existing web server and replace its functionality with a Micro-ROS node, publishers, and subscribers.

1.  **Remove Web Server Code:**
    *   Delete `network_manager.c` and `controller.c`. These files are entirely related to the HTTP server.
    *   Update `main.c` to remove all calls to `network_task` and any HTTP-related initializations.

2.  **Implement Micro-ROS Node in `main.c`:**
    *   Add the necessary Micro-ROS headers.
    *   In the `app_main` function, initialize the Micro-ROS node, executor, and transport layer (configured for Wi-Fi).
    *   Create a main application loop that spins the Micro-ROS executor (`rclc_executor_spin_some`).

3.  **Define the Micro-ROS Communication Interface:**
    *   **Publishers:**
        *   **Compressed Image Publisher:**
            *   **Topic:** `/camera/image_raw/compressed`
            *   **Message Type:** `sensor_msgs/msg/CompressedImage`
            *   **Purpose:** This will be the primary output of the camera, publishing the JPEG frames captured by the sensor.
        *   **Camera Info Publisher:**
            *   **Topic:** `/camera/camera_info`
            *   **Message Type:** `sensor_msgs/msg/CameraInfo`
            *   **Purpose:** Publishes the camera's calibration data (focal length, distortion, etc.). This is essential for many ROS perception nodes, including VSLAM. Initially, this can be populated with placeholder values.
    *   **Subscribers:**
        *   **LED Control Subscriber:**
            *   **Topic:** `/camera/led`
            *   **Message Type:** `std_msgs/msg/Bool`
            *   **Purpose:** A simple subscriber to allow the main robot to turn the ESP32-CAM's onboard LED (flash) on or off for illumination. This is a useful debugging and control feature.

---

### Phase 3: Camera Integration & Publishing

**Goal:** Modify the existing camera task to publish frames via Micro-ROS instead of sending them to the web server.

1.  **Modify `camera_task`:**
    *   Remove the use of `xQueueSend` to pass the frame buffer (`pic`).
    *   After a frame is captured (`pic = esp_camera_fb_get()`), directly use its data to populate a `sensor_msgs__msg__CompressedImage` message.
        *   The `message->data` field will be populated from `pic->buf`.
        *   The `message->data.size` will be set to `pic->len`.
        *   The `message->format` will be set to `"jpeg"`.
    *   Use `rcl_publish()` to send the compressed image message.
    *   Return the frame buffer using `esp_camera_fb_return(pic)`.

2.  **Implement `camera_info` Publishing:**
    *   Create a timer that periodically publishes a `CameraInfo` message. For now, the values can be hardcoded based on the camera's datasheet or a standard calibration.

3.  **Implement LED Subscriber Callback:**
    *   Create a callback function for the `/camera/led` subscriber.
    *   Inside the callback, read the boolean value from the message and use the appropriate ESP-IDF GPIO functions to turn the camera's flash LED on or off.

---

### Phase 4: Testing and Validation

**Goal:** Verify that the new firmware correctly communicates with the ROS 2 system.

1.  **Start the Micro-ROS Agent:**
    *   On the host computer, run the Micro-ROS agent, configured to listen for connections from the ESP32 over UDP.
    *   `ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888`

2.  **Flash and Monitor the ESP32:**
    *   Build and flash the new firmware to the ESP32-CAM.
    *   Monitor the serial output to ensure it successfully connects to Wi-Fi and the Micro-ROS agent.

3.  **Verify ROS 2 Topics:**
    *   On the host computer, use `ros2 topic list` to confirm that the `/camera/image_raw/compressed` and `/camera/camera_info` topics are being published.
    *   Use `ros2 topic echo /camera/image_raw/compressed` to see the raw image data being received.
    *   Use a tool like `rqt_image_view` to visualize the camera stream in real-time.
    *   Test the LED control by publishing to the `/camera/led` topic: `ros2 topic pub /camera/led std_msgs/msg/Bool "data: true"`

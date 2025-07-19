# ESP32 Camera Web Server

A high-performance camera streaming solution using ESP32 with integrated web server capabilities.

## Hardware Support

### Supported Camera Modules
- OV7670
- OV7725
- NT99141
- OV2640
- OV3660
- OV5640
- GC2145
- GC032A
- GC0308
- BF3005
- BF20A6
- SC030IOT
- MEGA_CCM

### Camera Interface
- SCCB Protocol (I2C variant)
- Clock: 100KHz
- Data pins: SIOD (26), SIOC (27)
- XCLK: 20MHz

## Web Server Features

The server runs on the ESP32's WiFi interface in station mode.

### Endpoints

#### Root (GET /)
- Main navigation interface
- Links to all available functions
- Basic system status

#### Capture (GET /capture)
- Single frame capture
- Returns JPEG image
- Content-Type: image/jpeg
- Inline display capability

#### Stream (GET /stream)
- Real-time MJPEG video stream
- Multipart content type
- Continuous frame delivery
- Boundary-based frame separation

#### Status (GET /status)
- System telemetry in JSON format
- Heap memory usage
- Active task count
- CPU frequency

## Technical Architecture

### Task Distribution
- Network handling on Core 1
- Camera operations on Core 0
- Frame processing on Core 0
- Priority-based scheduling

### Memory Management
- Frame queue buffer size: 2 frames
- JPEG compression
- SVGA resolution support
- Dynamic memory allocation for frames

### Network Configuration
- WiFi Station mode
- HTTP server with multiple socket support
- LRU cache enabled for connections
- Watchdog protection

## Build and Flash

Built using ESP-IDF v5.5 with CMake build system. Required components:
- esp32-camera
- esp_http_server
- esp_wifi
- freertos

## Performance Considerations

- JPEG quality set to 12 for optimal size/quality ratio
- Task watchdog monitoring enabled
- Core-specific task pinning for optimal performance
- Automatic WiFi reconnection handling
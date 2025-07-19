# ESP32 Camera Web Server

A high-performance camera streaming solution using ESP32 with integrated web server capabilities.

## Hardware Support

### Supported Camera Modules
- OV2640 (Primary - configured in main.c)
- OV7670, OV7725, NT99141
- OV3660, OV5640
- GC2145, GC032A, GC0308
- BF3005, BF20A6
- SC030IOT
- MEGA_CCM

### Camera Configuration
- **Resolution**: SVGA (800x600)
- **Format**: JPEG
- **Quality**: 12 (configurable)
- **Frame Buffer**: 1 buffer
- **XCLK Frequency**: 20MHz

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

## Web Server Features

The server runs on ESP32's WiFi in station mode with the following endpoints:

### HTTP Endpoints

#### Root (`GET /`)
- Main navigation interface
- Links to all available functions
- Basic system status

#### Capture (`GET /capture`)
- Single frame capture
- Returns JPEG image
- Content-Type: image/jpeg
- Inline display capability

#### Stream (`GET /stream`)
- Real-time MJPEG video stream
- Multipart content type: `multipart/x-mixed-replace`
- Continuous frame delivery
- Boundary: `123456789000000000000987654321`

#### Status (`GET /status`)
- System telemetry in JSON format
- Heap memory usage
- Active task count  
- CPU frequency

#### Hardware Info (`GET /hardware`)
- Hardware information in JSON format
- PSRAM size and features
- Chip capabilities (WiFi, BT, BLE)
- Core count and revision

## Technical Architecture

### Task Distribution
- **Network Task**: Core 1, Priority `configMAX_PRIORITIES - 1`
- **Camera Task**: Core 0, Priority `configMAX_PRIORITIES - 2`
- **Frame Processing**: Core 0, Priority `configMAX_PRIORITIES - 3` (disabled)

### Memory Management
- **Frame Queue**: 2 frame buffers
- **JPEG Compression**: Quality 12
- **SVGA Resolution**: 800x600
- **Stack Sizes**: 8192 bytes for camera/processing tasks

### Network Configuration
- **WiFi Mode**: Station (STA)
- **HTTP Server**: Multiple socket support (max 2)
- **LRU Cache**: Enabled for connections
- **Task Watchdog**: Enabled with monitoring

## Configuration

### WiFi Settings
Update in `main/network_manager.h`:
```c
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"
```

### Camera Settings
Modify in `main/main.c`:
```c
.frame_size = FRAMESIZE_SVGA,    // Resolution
.jpeg_quality = 12,              // Quality (10-63)
.fb_count = 1                    // Frame buffers
```

## Build and Flash

### Prerequisites
- ESP-IDF v5.5.0
- ESP32 with PSRAM enabled
- Camera module (OV2640 recommended)

### Build Commands
```bash
# Configure project
idf.py menuconfig

# Build project
idf.py build

# Flash to device
idf.py flash monitor
```

### Required Components
- esp32-camera (auto-installed via component manager)
- esp_http_server
- esp_wifi
- freertos

## Usage

1. **Connect to WiFi**: Device connects to configured network
2. **Find IP Address**: Check serial monitor for assigned IP
3. **Access Web Interface**: Open `http://[ESP32-IP]/` in browser
4. **Take Photos**: Click "Take Photo" for single capture
5. **Start Stream**: Click "Start Stream" for live video

## Performance Considerations

- **JPEG Quality**: Set to 12 for optimal size/quality ratio
- **Task Priorities**: Network task has highest priority
- **Core Pinning**: Camera operations on Core 0, Network on Core 1
- **Watchdog Protection**: Enabled for all tasks
- **PSRAM**: Required for frame buffering
- **WiFi Power**: Set to maximum (12dBm) for stable connection

## Troubleshooting

### Common Issues
1. **Camera Init Failed**: Check pin connections and power supply
2. **WiFi Connection Failed**: Verify SSID/password in network_manager.h
3. **Out of Memory**: Ensure PSRAM is enabled in menuconfig
4. **Slow Streaming**: Reduce JPEG quality or frame size

### Debug Options
- Enable verbose logging in menuconfig
- Monitor serial output with `idf.py monitor`
- Check heap usage via `/status` endpoint

## Hardware Requirements

- **ESP32** with external PSRAM
- **Camera Module** (OV2640 recommended)
- **Power Supply**: 5V/2A minimum
- **WiFi Network**: 2.4GHz support required

## License

This project uses ESP-IDF framework and esp32-camera component.
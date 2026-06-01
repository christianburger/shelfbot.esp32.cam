# ESP32 Camera – micro-ROS Streaming Platform

A firmware for ESP32-CAM that streams JPEG frames over ROS 2 (Humble) via micro-ROS, while simultaneously serving a live MJPEG feed and REST diagnostics through an embedded HTTP server.

---

## Feature Overview

| Feature | Details |
|---|---|
| Camera streaming | OV2640 (primary), SVGA 800×600, JPEG quality 12 |
| ROS 2 transport | micro-ROS over UDP/IP, Humble middleware |
| Wi-Fi | Station mode, up to 4 SSIDs, RSSI-based roaming |
| Web interface | Live MJPEG stream, single capture, health/status JSON |
| State tracking | Internal state machine covering camera, Wi-Fi, micro-ROS |

---

## Repository Structure

```
.
├── README.md                        This file
├── ENVIRONMENT.md                   Full environment & toolchain setup guide
├── CMakeLists.txt                   Top-level CMake (ESP-IDF project)
├── partitions.csv                   Custom partition table
├── sdkconfig                        Generated build configuration (do not hand-edit)
├── main/
│   ├── main.cpp                     app_main – wires all subsystems together
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild            Menuconfig entries (Wi-Fi SSIDs, micro-ROS agent hostname)
│   └── idf_component.yml           Component manager manifest (esp32-camera, mdns)
└── components/
    ├── camera_control/              Camera driver, sensor abstraction, manager singleton
    ├── http_server/                 HTTP server + REST controller
    ├── idf_includes/                Single umbrella header for all IDF/ROS C includes
    ├── microros_sync/               micro-ROS task: mDNS discovery, lifecycle, image publisher
    ├── state_machine/               Lightweight string-keyed state machine with periodic dump
    └── wifi_manager/                Multi-AP Wi-Fi with RSSI monitoring and roaming
```

---

## Quick Start

### 1. Set up the environment

Follow **[ENVIRONMENT.md](ENVIRONMENT.md)** to install ESP-IDF v5.3, clone and patch the micro-ROS component, install Python dependencies, and configure the partition table and flash size.

### 2. Configure credentials

```bash
idf.py menuconfig
```

Navigate to **Wi-Fi Configuration** and fill in up to four SSID/password pairs. Navigate to **Micro-ROS Configuration** and set the agent's mDNS hostname (default: `shelfbot-agent`).

### 3. Build and flash

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 4. Start the micro-ROS agent (host PC)

```bash
docker run -it --rm --net=host microros/micro-ros-agent:humble udp4 --port 8888 -v6
```

The agent must be reachable via mDNS as `<hostname>.local`. The firmware resolves the hostname at runtime — no hard-coded IPs required.

### 5. Access the web interface

After Wi-Fi connects, the assigned IP is printed to the serial console:

```
got IP: 192.168.x.x
```

Open `http://192.168.x.x/` in a browser. The dashboard auto-refreshes every 2 seconds.

---

## Hardware

### Supported camera modules

OV2640 (recommended and default), OV7670, OV7725, NT99141, OV3660, OV5640, GC2145, GC032A, GC0308, BF3005, BF20A6, SC030IOT, MEGA_CCM, HM1055, HM0360.

### Pin configuration (ESP32-CAM)

| Signal | GPIO | Signal | GPIO |
|---|---|---|---|
| PWDN | 32 | D4 | 36 |
| RESET | – (NC) | D3 | 21 |
| XCLK | 0 | D1 | 18 |
| SIOD (SDA) | 26 | D0 | 5 |
| SIOC (SCL) | 27 | VSYNC | 25 |
| D7 | 35 | HREF | 23 |
| D6 | 34 | PCLK | 22 |
| D5 | 39 | D2 | 19 |

PSRAM is required for frame buffering (`CAMERA_FB_IN_PSRAM`).

---

## Wi-Fi Manager

The firmware supports up to **4 Wi-Fi networks** configured at compile time via menuconfig. At boot it scans all visible APs, ranks them by RSSI, and connects to the strongest known network. While connected it polls RSSI every 5 seconds (configurable) and roams transparently when signal degrades below the warning threshold for N consecutive readings.

The manager exports two FreeRTOS event-group bits for other components to block on:

- `WM_CONNECTED_BIT` — set when an IP address is obtained
- `WM_DISCONNECTED_BIT` — set when not connected

The state machine key is `"wifi_manager"` with states `off → connecting → connected → disconnected → error`.

### Configurable thresholds (menuconfig → Wi-Fi Configuration)

| Parameter | Default | Description |
|---|---|---|
| `WIFI_RSSI_WARN_THRESHOLD` | -75 dBm | Begin counting degraded samples |
| `WIFI_RSSI_CRITICAL_THRESHOLD` | -85 dBm | Immediate rescan |
| `WIFI_DEGRADED_CONFIRM_N` | 3 | Consecutive samples before roam |
| `WIFI_MONITOR_INTERVAL_MS` | 5000 ms | RSSI poll interval |
| `WIFI_RETRIES_PER_NETWORK` | 3 | Connection attempts per AP |
| `WIFI_INTER_CYCLE_DELAY_S` | 10 s | Delay when no known AP is visible |

---

## micro-ROS Interface

Transport: **UDP/IP** (not serial). The firmware uses `rmw_uros_options_set_udp_address()` to inject the agent IP resolved via mDNS — `setenv("RMW_UXRCE_URI")` does **not** work on ESP-IDF and must not be used.

Node name: `shelfbot_camera`

### Published topics

| Topic | Type | Description |
|---|---|---|
| `/camera/compressed` | `sensor_msgs/msg/CompressedImage` | JPEG frames, format `"jpeg"`, frame_id `"camera_frame"` |

### micro-ROS lifecycle states

```
OFF → DISCOVERING → (INITIALIZING) → CONNECTED → DISCONNECTED → DISCOVERING …
```

The task backs off exponentially (250 ms → 5 s) on connection failures and re-enters mDNS discovery on disconnect. After 3 consecutive executor spin failures the connection is torn down and recreated.

---

## Web Interface

| Endpoint | Description |
|---|---|
| `GET /` | Dashboard with capture, stream, health, and status cards |
| `GET /capture` | Single JPEG snapshot (direct `esp_camera_fb_get`) |
| `GET /stream` | MJPEG multipart stream (`--frame` boundary) |
| `GET /api/health` | JSON: free heap, min heap, uptime, PSRAM size |
| `GET /api/status` | JSON: camera sensor PID, quality, brightness, AWB, AEC, etc. |

The web interface is served by `HttpServer` (port 80) and communicates directly with the camera driver — it does **not** use ROS topics.

---

## Component Architecture

```
app_main
  ├── StateMachine::init()
  ├── wifi_manager_init()          ← registers "wifi_manager" state, spawns manager task
  ├── MicrorosSync::init/start()   ← registers "microros" state, spawns microros_task
  ├── CameraManager::initialize()  ← registers "camera" state, initialises OV2640
  ├── NetworkManager::init()       ← blocks until wifi_manager state == CONNECTED, starts HTTP server
  └── CameraManager::startCapture(on_camera_frame)
        ├── MicrorosSync::publishCompressedImage()   → /camera/compressed
        └── xQueueSend(frame_queue)                  → HTTP capture/stream handlers
```

Each component registers itself with `StateMachine::setInitial()` and transitions through typed states. The state machine dumps all module states to the log every 10 seconds.

---

## Partition Table

```
# partitions.csv
nvs,      data, nvs,     , 24K
phy_init, data, phy,     ,  4K
factory,  app,  factory, ,  2M
```

The factory partition is 2 MB. Flash size must be set to **8 MB** in menuconfig (`Serial flasher config → Flash size → 8 MB`) — the sdkconfig in this repository is already configured for 8 MB.

---

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `app partition is too small` | Flash size not set correctly in menuconfig (see ENVIRONMENT.md) |
| micro-ROS agent never connects | Verify UDP agent is on port 8888; confirm mDNS hostname resolves; check Wi-Fi reachability |
| Wi-Fi never connects | Verify SSID/password in menuconfig; check that the AP is on 2.4 GHz |
| Web interface not loading | Find the IP in the serial log; ensure HTTP server started (look for `HTTP server started on port 80`) |
| Camera init failed | Check PSRAM enabled in menuconfig; verify power supply ≥ 500 mA |
| `rclc_support_init_with_options failed` | Agent not reachable at the resolved IP; check agent log for `create_client` and `session established` |
| Repeated `delete_client` churn in agent log | Spin failures accumulating — usually a UDP packet loss / RSSI problem |

### micro-ROS diagnostic log commands

```bash
# Lifecycle transitions in order
grep -nEi "Wi-Fi ready|mDNS Query|create_entities|AGENT_CONNECTED|AGENT_DISCONNECTED" monitor.log

# rcl/rmw errors with context
grep -nEi -B4 -A6 "rcl|rmw|Failed to init rcl" monitor.log

# Entity creation confirmation
grep -nEi "Creating micro-ROS entities|Entities created|micro-ROS ready" monitor.log

# Publisher activity
grep -nEi "image publish" monitor.log

# Backoff and spin failures
grep -nEi "consecutive spin failures|Backing off" monitor.log
```

---

## Further Reading

- [ENVIRONMENT.md](ENVIRONMENT.md) — Toolchain installation, micro-ROS integration, common pitfalls
- [micro-ROS ESP32 documentation](https://micro.ros.org/docs/tutorials/core/overview/)
- [FastAccelStepper library](https://github.com/gin66/FastAccelStepper) *(referenced in sibling Shelfbot project)*
- [esp32-camera component](https://github.com/espressif/esp32-camera)

---

## License

MIT. See `LICENSE` for details.

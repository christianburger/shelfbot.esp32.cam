# Environment Setup Guide

Complete walkthrough for building the ESP32-CAM micro-ROS firmware from scratch: toolchain, Python dependencies, micro-ROS component integration, project configuration, and build/flash.

> **Reference project:** The Shelfbot firmware (`ENVIRONMENT_SETUP.md`) covers an identical ESP-IDF + micro-ROS stack on ESP32 WROOM. Steps and workarounds that apply to both projects are noted where relevant.

---

## Prerequisites

| Tool | Minimum version | Notes |
|---|---|---|
| Ubuntu / Debian | 20.04 LTS or later | WSL2 on Windows works; macOS is supported but not covered here |
| Git | any recent | Required for recursive clones |
| Python | 3.8 – 3.11 | Python 3.12 breaks several ROS 2 build tools |
| CMake | 3.16+ | Installed automatically by ESP-IDF installer |
| Docker | any recent | Required for the micro-ROS agent; rootless Docker is fine |
| USB serial driver | — | `cp210x` or `ch341` depending on your board |

---

## 1. Install ESP-IDF v5.3

The micro-ROS component for ESP-IDF is tested against **v5.3**. Using a newer release (e.g. v5.4, v5.5) may compile but can introduce subtle RMW breakage that is hard to diagnose.

```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32
```

The `--recursive` flag is mandatory; the IDF contains several nested submodules that the installer will fail without.

### 1.1 Load the environment (every new terminal)

```bash
. ~/esp/esp-idf/export.sh
```

Verify the version before doing anything else:

```bash
idf.py --version
# Must print: ESP-IDF v5.3.x
```

Add the export line to your shell profile if you work on this project regularly:

```bash
echo '. ~/esp/esp-idf/export.sh' >> ~/.bashrc
```

---

## 2. Install Python packages

```bash
pip3 install \
  catkin_pkg \
  lark-parser \
  colcon-common-extensions \
  empy==3.3.4 \
  pyserial \
  setuptools
```

> **Critical: `empy==3.3.4`**
> micro-ROS uses `em` (EmPy) to generate ROS 2 message code. Version 4.x changed the API and breaks the generator with an error like `AttributeError: module 'em' has no attribute 'BUFFERED_OPT'`. Pin to `3.3.4` exactly. If you have a newer version installed globally, the safest fix is a virtualenv or `pip3 install --force-reinstall empy==3.3.4`.

---

## 3. Create the project

```bash
mkdir -p ~/camera_project
cd ~/camera_project
```

If starting from scratch with `idf.py create-project`, the top-level `CMakeLists.txt` must include the IDF project boilerplate:

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(camera_project)
```

If you are cloning this repository, skip the creation step and go straight to section 4.

---

## 4. Add micro-ROS (Humble)

This is the most involved step. Follow it exactly — out-of-order operations or wrong branch selections are the most common source of build failures.

### 4.1 Clone the component

```bash
cd ~/camera_project
mkdir -p components
cd components
git clone https://github.com/micro-ROS/micro_ros_espidf_component.git
cd micro_ros_espidf_component
git checkout humble
cd ../..
```

> **Why Humble?** The firmware uses ROS 2 Humble message types and the Humble micro-ROS agent Docker image. Mixing branches (e.g. cloning `main` and running a Humble agent) causes DDS protocol mismatches that are difficult to debug.

### 4.2 Delete pre-generated source caches

The component ships with pre-built micro-ROS source trees (`micro_ros_src`, `micro_ros_dev`) that may be stale, mis-targeted, or generated for a different IDF version. Always delete them before your first build and after any full clean:

```bash
rm -rf components/micro_ros_espidf_component/micro_ros_src \
       components/micro_ros_espidf_component/micro_ros_dev
```

The component's own CMake will regenerate these during the first `idf.py build` run. This step takes several minutes.

### 4.3 Add the component to idf_component.yml

The top-level component manifest (`main/idf_component.yml`) must declare the external dependencies resolved by the IDF Component Manager:

```yaml
dependencies:
  idf:
    version: '>=4.1.0'
  espressif/esp32-camera: '*'
  espressif/mdns: '*'
```

The micro-ROS component lives in `components/` as a local component and is therefore not listed here — it is picked up automatically.

### 4.4 Important: do not use setenv for the agent address

A common mistake when reading older micro-ROS tutorials is calling `setenv("RMW_UXRCE_DEFAULT_UDP_IP", ...)` or equivalent environment-variable hacks. **These do not work on ESP-IDF** because `esp_rom_printf` and the RMW layer are initialised before FreeRTOS tasks run.

The correct approach, used in `microros_sync.cpp`, is to inject the agent address directly into the RMW init options:

```cpp
rmw_uros_options_set_udp_address(
    agent_ip,   // char* resolved by mDNS
    "8888",
    rcl_init_options_get_rmw_init_options(&init_options));
```

Then pass those options to the support initialiser:

```cpp
rclc_support_init_with_options(
    &g_support, 0, NULL, &init_options, &g_allocator);
```

Do **not** use the plain `rclc_support_init()` when you need to supply a custom transport address.

---

## 5. Set the build target

```bash
idf.py set-target esp32
```

This writes `CONFIG_IDF_TARGET="esp32"` to `sdkconfig` and configures the Xtensa toolchain. Run this once per clean workspace. If you switch targets (e.g. to `esp32s3`) you must do a full clean first.

---

## 6. Configure flash size and partition table

### 6.1 Open menuconfig

```bash
idf.py menuconfig
```

### 6.2 Set flash size

Navigate to:

```
Serial flasher config → Flash size → 8 MB
```

The ESP32-CAM module on this board has an 8 MB flash. Setting the wrong size results in an `app partition is too small` error at flash time, or silent data corruption. The sdkconfig in this repository is already set to 8 MB — verify it is correct for your specific module.

> **Shelfbot reference:** The sibling Shelfbot project targets a 4 MB WROOM module. If adapting that project's `ENVIRONMENT_SETUP.md`, note that flash size differs. Always verify your module's datasheet.

### 6.3 Set a custom partition table

Navigate to:

```
Partition Table → Custom partition table CSV
Custom partition table CSV file → partitions.csv
```

The `partitions.csv` in this repository defines:

```
nvs,      data, nvs,     , 24K
phy_init, data, phy,     ,  4K
factory,  app,  factory, ,  2M
```

The 2 MB factory partition is sufficient for this firmware. If you add significant new components, increase it and reduce the NVS or phy partitions accordingly, keeping the total ≤ flash size.

### 6.4 Configure Wi-Fi credentials

Still in menuconfig, navigate to **Wi-Fi Configuration** and fill in your SSIDs and passwords for up to four networks. Leave unused slots empty.

### 6.5 Configure the micro-ROS agent hostname

Navigate to **Micro-ROS Configuration** and set `MICROROS_AGENT_HOSTNAME` to the mDNS hostname of the machine running the agent (without `.local`). For example, if your laptop is `my-laptop.local`, enter `my-laptop`.

Save and exit menuconfig.

---

## 7. Full clean rebuild procedure

After changing the partition table, flash size, or switching micro-ROS branches, always do a full clean to avoid stale artefacts:

```bash
rm -rf components/micro_ros_espidf_component/micro_ros_src \
       components/micro_ros_espidf_component/micro_ros_dev \
       build \
       managed_components \
       dependencies.lock

idf.py fullclean
idf.py reconfigure
idf.py build
```

`idf.py fullclean` removes the `build/` directory and all CMake caches. `idf.py reconfigure` re-runs CMake without building, which is a useful sanity check before committing to a full build.

---

## 8. Build, flash, monitor

```bash
# Build
idf.py build

# Flash (replace /dev/ttyUSB0 with your port)
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py monitor

# Flash and monitor in one command
idf.py -p /dev/ttyUSB0 flash monitor
```

The monitor session uses `Ctrl+]` to exit. Baud rate is 115200 (set in sdkconfig under `ESP System Settings → Console UART → Baud rate`).

To find your serial port:

```bash
ls /dev/ttyUSB* /dev/ttyACM*
# or
dmesg | tail -20
```

---

## 9. Run the micro-ROS agent

The agent must be running on a host reachable from the ESP32 over Wi-Fi. The easiest method is the official Docker image:

```bash
docker run -it --rm --net=host \
  microros/micro-ros-agent:humble \
  udp4 --port 8888 -v6
```

`--net=host` is required so the container shares the host's network interfaces and responds to mDNS queries. `-v6` enables verbose logging — useful for confirming that the ESP32 connects.

Expected agent output when the firmware connects:

```
[1715000000.000000] info     | UDPv4AgentLinux.cpp | ...
[1715000000.000000] info     | Root.cpp            | create_client ...
[1715000000.000000] info     | Root.cpp            | session established
```

If you see repeated `create_client` / `delete_client` cycling, the ESP32 is reaching the agent but the executor spin is failing — check RSSI and UDP packet loss.

### Running the agent without Docker

```bash
# Install micro-ROS agent natively
pip3 install micro-ros-agent  # if available for your distro
# or build from source via colcon in a ROS 2 Humble workspace
```

The Docker approach is strongly recommended to avoid ROS 2 workspace conflicts.

---

## 10. Verify the ROS 2 topic

On the host, with a ROS 2 Humble environment sourced:

```bash
source /opt/ros/humble/setup.bash
ros2 topic list
# Should include: /camera/compressed

ros2 topic hz /camera/compressed
# Should show ~10 Hz depending on capture interval
```

To view the stream with `rqt_image_view`:

```bash
ros2 run rqt_image_view rqt_image_view /camera/compressed
```

---

## 11. Known issues and workarounds

### empy version conflict

**Symptom:** Build fails with `AttributeError: module 'em' has no attribute 'BUFFERED_OPT'` or similar during micro-ROS message generation.

**Fix:**
```bash
pip3 install --force-reinstall empy==3.3.4
```

If you use a system Python that is shared with other tools, isolate with a virtualenv:
```bash
python3 -m venv ~/microros_venv
source ~/microros_venv/bin/activate
pip install empy==3.3.4 catkin_pkg lark-parser colcon-common-extensions pyserial setuptools
```
Then always activate the venv before running `idf.py`.

---

### micro-ROS source generation hangs or produces empty output

**Symptom:** First build hangs at `Generating micro-ROS library` for more than 15 minutes, or the build completes but `micro_ros_src/` is empty.

**Fix:**
```bash
rm -rf components/micro_ros_espidf_component/micro_ros_src \
       components/micro_ros_espidf_component/micro_ros_dev \
       build
idf.py fullclean
idf.py build
```
If it still hangs, check that all Python packages are installed and that the `empy` version is exactly `3.3.4`.

---

### `rcl_init_options_init` or `rclc_support_init_with_options` fails

**Symptom:** Log shows `rclc_support_init_with_options failed: <error code>` shortly after mDNS resolves the agent.

**Checklist:**
1. Is the agent running and reachable? Ping the agent IP from another device on the same subnet.
2. Does the mDNS resolution return the correct IP? The log prints `Agent IP: x.x.x.x` — verify it matches the host.
3. Is port 8888 UDP open? Check the host firewall (`sudo ufw allow 8888/udp`).
4. Is the micro-ROS Humble agent running (not Foxy, not Iron)?

---

### GPIO3 conflict (LiDAR + console) — Shelfbot reference

**Applies to:** Shelfbot sibling project only (not this camera project).

GPIO3 is the default UART0 RX (console input). The Shelfbot firmware routes the LYDSTO LDS02RR LiDAR to UART2 RX on the same pin. To resolve this either:
- Move the LiDAR to UART1 (GPIO9/10 on ESP32 WROOM), or
- Disable the console on GPIO3: menuconfig → `ESP System Settings → Channel for console output → Custom UART` → choose a different UART.

---

### `app partition is too small` at flash time

**Symptom:** `esptool` reports the factory partition cannot fit the firmware binary.

**Fix:** Increase the factory partition size in `partitions.csv`, or reduce code size (disable unused camera sensor drivers in menuconfig under `Camera configuration`). Ensure the flash size in menuconfig matches your physical module (8 MB for ESP32-CAM, 4 MB for ESP32 WROOM-02).

---

### Wi-Fi connects but micro-ROS agent never sees the ESP32

**Symptom:** Wi-Fi state machine reaches `CONNECTED`, mDNS resolves successfully, but the agent log shows no `create_client`.

**Likely cause:** The agent is running in a Docker container without `--net=host`, so it is on a different subnet.

**Fix:** Always start the agent with `--net=host`:
```bash
docker run -it --rm --net=host microros/micro-ros-agent:humble udp4 --port 8888 -v6
```

---

### sdkconfig differences between machines

The `sdkconfig` file is committed to this repository. It contains the build configuration for an 8 MB ESP32-CAM with PSRAM. If you run `idf.py menuconfig` on a machine with a different IDF version the file may be regenerated with different defaults. Key values to verify:

| Key | Required value |
|---|---|
| `CONFIG_ESPTOOLPY_FLASHSIZE` | `"8MB"` |
| `CONFIG_PARTITION_TABLE_CUSTOM` | `y` |
| `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` | `"partitions.csv"` |
| `CONFIG_SPIRAM` | `y` |
| `CONFIG_MICRO_ROS_AGENT_PORT` | `"8888"` |
| `CONFIG_MICRO_ROS_ESP_NETIF_WLAN` | `y` |

---

## 12. Component dependency graph

```
main
  └── wifi_manager         ← http_server, state_machine, mdns, esp_wifi
  └── microros_sync        ← state_machine, wifi_manager, micro_ros_espidf_component
  └── camera_control       ← esp32-camera, state_machine, freertos
  └── http_server          ← esp32-camera, esp_http_server, json
  └── state_machine        ← idf_includes, esp_timer
  └── idf_includes         ← (umbrella: freertos, log, wifi, http_server, mdns,
                               nvs_flash, lwip, json, micro_ros_espidf_component,
                               esp32-camera)
```

All C headers from IDF and micro-ROS are wrapped in `extern "C" { }` inside `idf_includes/include/idf_c_includes.hpp`. Every C++ source file includes this single header rather than mixing `extern "C"` blocks throughout the codebase.

---

## 13. Quick reference card

```bash
# One-time setup
mkdir -p ~/esp && cd ~/esp
git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32

pip3 install catkin_pkg lark-parser colcon-common-extensions empy==3.3.4 pyserial setuptools

# Every new terminal
. ~/esp/esp-idf/export.sh

# Clone micro-ROS component (once per project)
cd ~/camera_project/components
git clone https://github.com/micro-ROS/micro_ros_espidf_component.git
cd micro_ros_espidf_component && git checkout humble && cd ../..
rm -rf components/micro_ros_espidf_component/micro_ros_src \
       components/micro_ros_espidf_component/micro_ros_dev

# Configure
idf.py set-target esp32
idf.py menuconfig   # set flash=8MB, custom partition, Wi-Fi SSIDs, agent hostname

# Build & flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

# micro-ROS agent (host)
docker run -it --rm --net=host microros/micro-ros-agent:humble udp4 --port 8888 -v6

# Full clean rebuild
rm -rf components/micro_ros_espidf_component/micro_ros_src \
       components/micro_ros_espidf_component/micro_ros_dev \
       build managed_components dependencies.lock
idf.py fullclean && idf.py reconfigure && idf.py build
```

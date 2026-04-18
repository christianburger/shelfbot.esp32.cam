# micro-ROS Setup

This guide covers installing and rebuilding the micro-ROS ESP-IDF component for the shelfbot ESP32-CAM project.

Assumes a working ESP-IDF v5.3.4 environment. See [ENVIRONMENT.md](ENVIRONMENT.md) if you haven't set that up yet.

---

## Verified Working Versions

| Component                    | Version / Branch |
|------------------------------|-----------------|
| ESP-IDF                      | v5.3.4          |
| micro_ros_espidf_component   | `humble` branch |
| micro-ROS Agent (Docker)     | `humble` tag    |
| Python EmPy                  | 3.3.4 (pinned)  |

---

## Step 1 — Install Python Dependencies

Must be done inside the active IDF shell so packages go into the IDF Python venv:

```bash
get_idf
pip3 install catkin_pkg lark-parser colcon-common-extensions empy==3.3.4
```

> `empy==3.3.4` is mandatory. Version 4.x broke the ament build system and will cause
> cryptic colcon failures with no obvious error message.

---

## Step 2 — Install the micro-ROS Component

From the project root:

```bash
mkdir -p components && cd components

git clone https://github.com/micro-ROS/micro_ros_espidf_component.git
cd micro_ros_espidf_component
git checkout humble
git submodule update --init --recursive
cd ../..
```

> Always use the `humble` branch. The `kilted` and `rolling` branches require
> `ament_cmake_ros` to be installed on the host, which is not available outside
> a full ROS 2 installation.

---

## Step 3 — Build

The first build clones and cross-compiles the entire micro-ROS library from source.
This takes 10–20 minutes.

```bash
get_idf
idf.py build
```

---

## Step 4 — Run the micro-ROS Agent

The agent version must match the component branch:

```bash
docker run -it --rm --net=host microros/micro-ros-agent:humble udp4 --port 8888 -v6
```

---

## Wiping and Rebuilding micro-ROS

When switching branches, after pulling component updates, or when the build is in an
inconsistent state, wipe all generated micro-ROS artifacts together:

```bash
rm -rf components/micro_ros_espidf_component/micro_ros_src/
rm -rf components/micro_ros_espidf_component/micro_ros_dev/
rm -rf components/micro_ros_espidf_component/include/
rm -rf components/micro_ros_espidf_component/libmicroros.a
rm -rf build/

get_idf
idf.py build
```

> **Never wipe only `micro_ros_src/install/` or `micro_ros_src/build/` in isolation.**
> The `micro_ros_dev/` directory is a host-native ament bootstrap workspace that must be
> rebuilt alongside `micro_ros_src/`. If it is stale, colcon will source a broken ament
> prefix and fail with `Findament_cmake_ros.cmake not found` — the same error as a
> missing ROS 2 installation.

---

## Known Issues and Fixes

### `Findament_cmake_ros.cmake not found`

Caused by stale `micro_ros_dev/` or wrong component branch. Full wipe and rebuild fixes it.
See the wipe procedure above.

### Duplicate `__atomic_*` symbols at link time

`rcutils/atomic_64bits.c` and ESP-IDF newlib both provide 64-bit atomic helpers for ESP32.
Fixed in the project's root `CMakeLists.txt`:

```cmake
target_link_options(camera_project.elf PRIVATE -Wl,--allow-multiple-definition)
```

This is already committed. No action needed.

### GLOB mismatch warnings

```
-- GLOB mismatch! The following files were removed/added ...
```

Harmless — CMake detected that the micro-ROS include directory changed and re-ran
configure. The build will proceed normally.

### `rclc_timer_init_default2` not found

This function does not exist in the `humble` branch of rclc. Use `rclc_timer_init_default`
instead. Already fixed and committed in this project.

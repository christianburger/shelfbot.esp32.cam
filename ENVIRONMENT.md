# ESP-IDF Environment Setup

This guide covers setting up a clean ESP-IDF development environment for the shelfbot ESP32-CAM project on Gentoo Linux.

For micro-ROS specific setup, see [MICROROS.md](MICROROS.md).

---

## Prerequisites

```bash
# Required system packages (Gentoo)
emerge --ask dev-vcs/git dev-lang/python sys-devel/gcc app-misc/cmake ninja
```

---

## Step 1 — Install ESP-IDF v5.3.4

```bash
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.3.4
git submodule update --init --recursive
./install.sh esp32
```

---

## Step 2 — Configure Shell Access

Add an alias to `~/.bashrc` — do **not** source IDF in your default shell startup:

```bash
echo "alias get_idf='. ~/esp/esp-idf/export.sh'" >> ~/.bashrc
source ~/.bashrc
```

To activate the IDF environment in any terminal:

```bash
get_idf
```

> **Critical:** Never source ROS 2 and IDF in the same shell. They pollute each other's
> `CMAKE_PREFIX_PATH` and will cause subtle, hard-to-diagnose build failures.

---

## Step 3 — Verify Installation

```bash
get_idf
idf.py --version
# Expected: ESP-IDF v5.3.4
```

---

## Step 4 — Clone the Project

```bash
git clone --recursive <repository_url>
cd shelfbot.esp32.cam
```

---

## Step 5 — Set Up micro-ROS

Follow [MICROROS.md](MICROROS.md) to install the micro-ROS component and its dependencies before building.

---

## Step 6 — Configure and Build

```bash
# Always start from a fresh IDF shell
get_idf

idf.py set-target esp32
idf.py menuconfig   # verify micro-ROS transport and WiFi credentials
idf.py build
```

---

## Step 7 — Flash and Monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## Troubleshooting

**Build fails with CMake errors on first run**
Make sure micro-ROS setup from [MICROROS.md](MICROROS.md) was completed before building.

**`idf.py` not found**
You forgot to run `get_idf` in the current shell session.

**Unexpected CMake prefix path errors**
Check that no ROS 2 environment is sourced: `echo $AMENT_PREFIX_PATH` should be empty.

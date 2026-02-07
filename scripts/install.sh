#!/bin/bash
set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run this script with root privileges (sudo)"
    exit 1
fi

echo "  _   ___     _______ ____  "
echo " | \ | \ \   / /  ___|  _ \ "
echo " |  \| |\ \ / /| |_  | | | |"
echo " | |\  | \ V / |  _| | |_| |"
echo " |_| \_|  \_/  |_|   |____/ "
echo ""
NVFD_VER=$(grep '#define NVFD_VERSION' "$(dirname "$0")/../include/nvfd.h" | cut -d'"' -f2)
echo " NVFD v${NVFD_VER}"
echo " ======================================"
echo

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$NAME
elif type lsb_release >/dev/null 2>&1; then
    OS=$(lsb_release -si)
else
    OS=$(uname -s)
fi

echo "Detected OS: $OS"
echo "Installing dependencies..."

case "$OS" in
    "Ubuntu"|"Ubuntu "*|"Debian GNU/Linux"|"Debian")
        apt-get update
        apt-get install -y build-essential libjansson-dev libncursesw5-dev nvidia-cuda-toolkit
        ;;
    "Rocky Linux"|"CentOS Linux"|"Red Hat Enterprise Linux"|"Fedora"|"Fedora Linux")
        dnf install -y gcc make jansson-devel ncurses-devel
        ;;
    *)
        echo "Warning: Unsupported OS ($OS). Ensure gcc, make, libjansson-dev, libncurses-dev, and NVML headers are installed."
        ;;
esac

# Stop old service if running
if systemctl is-active --quiet infinirc-gpu-fan-control.service 2>/dev/null; then
    echo "Stopping old v1 service..."
    systemctl stop infinirc-gpu-fan-control.service
    systemctl disable infinirc-gpu-fan-control.service
    rm -f /etc/systemd/system/infinirc-gpu-fan-control.service
fi

# Stop old igfc service if upgrading from igfc name
if systemctl is-active --quiet igfc.service 2>/dev/null; then
    echo "Stopping old igfc service..."
    systemctl stop igfc.service
    systemctl disable igfc.service
    rm -f /etc/systemd/system/igfc.service
fi

# Stop current service if upgrading
if systemctl is-active --quiet nvfd.service 2>/dev/null; then
    echo "Stopping existing NVFD service..."
    systemctl stop nvfd.service
fi

# Determine script directory (where the repo is)
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "Building NVFD..."
cd "$SCRIPT_DIR"
make clean && make

echo "Installing..."
make install

# Run config migration
echo "Checking for config migration..."
/usr/local/bin/nvfd list >/dev/null 2>&1 || true

# Remove old alias if present
if grep -q 'alias igfc=' /etc/bash.bashrc 2>/dev/null; then
    sed -i '/alias igfc=/d' /etc/bash.bashrc
    echo "Removed old 'igfc' alias."
fi

# Remove old binaries
rm -f /usr/local/bin/infinirc_gpu_fan_control
rm -f /usr/local/bin/igfc

echo "Enabling and starting service..."
systemctl daemon-reload
systemctl enable nvfd.service
systemctl start nvfd.service

cat << EOF

======================================================================
           NVFD v${NVFD_VER} - Installation Complete
======================================================================

Service Status: Started

Usage:
  nvfd                       Interactive TUI dashboard (on TTY)
  nvfd auto                  Return fan control to NVIDIA driver
  nvfd curve                 Enable custom fan curve for all GPUs
  nvfd curve <temp> <speed>  Edit fan curve point
  nvfd curve show            Show current fan curve
  nvfd curve edit            Interactive curve editor (ncurses)
  nvfd curve reset           Reset fan curve to default
  nvfd <speed>               Set fixed fan speed for all GPUs (30-100)
  nvfd <gpu_index> <speed>   Set fixed fan speed for specific GPU
  nvfd list                  List all GPUs and their indices
  nvfd status                Show current status
  nvfd -h                    Show help

Works on X11, Wayland, and headless systems.
No nvidia-settings required.
No need for sudo - nvfd will auto-elevate when needed.
Runs as daemon when started via systemd (non-TTY).

Try it now:  nvfd

======================================================================
EOF

#!/bin/bash
set -e

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --help)
            echo "Usage: sudo $(basename "$0")"
            echo
            echo "Uninstalls NVFD and any installed utilities."
            echo
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Run with --help for usage information."
            exit 1
            ;;
    esac
done

if [ "$EUID" -ne 0 ]; then
    echo "Please run this script with root privileges (sudo)"
    exit 1
fi

echo "Uninstalling NVFD..."

# Stop and disable service
if systemctl is-active --quiet nvfd.service 2>/dev/null; then
    echo "Stopping NVFD service..."
    systemctl stop nvfd.service
fi
if systemctl is-enabled --quiet nvfd.service 2>/dev/null; then
    systemctl disable nvfd.service
fi
rm -f /etc/systemd/system/nvfd.service

# Also clean up old services if present
for svc in igfc.service infinirc-gpu-fan-control.service; do
    if systemctl is-active --quiet "$svc" 2>/dev/null; then
        systemctl stop "$svc"
        systemctl disable "$svc"
    fi
    rm -f "/etc/systemd/system/$svc"
done

systemctl daemon-reload

# Remove binaries
rm -f /usr/local/bin/nvfd
rm -f /usr/local/bin/igfc
rm -f /usr/local/bin/infinirc_gpu_fan_control
rm -f /usr/local/src/infinirc_gpu_fan_control.c

# Remove old alias if present
if grep -q 'alias igfc=' /etc/bash.bashrc 2>/dev/null; then
    sed -i '/alias igfc=/d' /etc/bash.bashrc
fi

# Auto-detect and remove utilities if installed
if [ -f /usr/local/bin/nvfd-fan-control.sh ] || [ -f /etc/systemd/system/nvfd-fan-control.service ]; then
    echo "Detected installed utility scripts, removing..."

    # Stop and disable fan-control service if running
    if systemctl is-active --quiet nvfd-fan-control.service 2>/dev/null; then
        systemctl stop nvfd-fan-control.service
    fi
    if systemctl is-enabled --quiet nvfd-fan-control.service 2>/dev/null; then
        systemctl disable nvfd-fan-control.service
    fi

    # Remove utility files
    rm -f /usr/local/bin/nvfd-fan-control.sh
    rm -f /etc/systemd/system/nvfd-fan-control.service

    systemctl daemon-reload
fi

echo ""
echo "NVFD has been uninstalled."
echo ""
echo "Config files preserved in /etc/nvfd/ (remove manually if desired):"
echo "  rm -rf /etc/nvfd"
echo ""
echo "Please reboot to ensure fans return to driver control."

#!/bin/bash
set -e

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

echo ""
echo "NVFD has been uninstalled."
echo ""
echo "Config files preserved in /etc/nvfd/ (remove manually if desired):"
echo "  rm -rf /etc/nvfd"
echo ""
echo "Please reboot to ensure fans return to driver control."

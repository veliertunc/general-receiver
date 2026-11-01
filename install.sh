#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this installer as root." >&2
    exit 1
fi

apt-get update
apt-get install -y python3 dvb-tools v4l-utils linux-firmware device-tree-compiler
id receiver >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin --groups video,audio receiver
install -d -m 0755 /opt/tv-receiver/receiver_app/services /etc/receiver
install -m 0755 firmware/receiver.py /opt/tv-receiver/receiver.py
for module in firmware/receiver_app/*.py; do
    install -m 0644 "$module" "/opt/tv-receiver/receiver_app/$(basename "$module")"
done
for module in firmware/receiver_app/services/*.py; do
    install -m 0644 "$module" "/opt/tv-receiver/receiver_app/services/$(basename "$module")"
done
for component in api television radio ethernet; do
    if [ ! -f "/etc/receiver/$component.json" ]; then
        install -m 0644 "config/$component.json" "/etc/receiver/$component.json"
    fi
done
python3 /opt/tv-receiver/receiver.py --check-config
touch /etc/receiver/channels.conf
chown root:video /etc/receiver/channels.conf
chmod 0640 /etc/receiver/channels.conf
install -m 0644 systemd/tv-receiver.service /etc/systemd/system/tv-receiver.service

# Reference wiring for the CM4 I/O Board: Si4703 on I2C1, IRQ on GPIO24,
# active-low RESET on GPIO17. Change the overlay if the carrier uses other pins.
boot_config=/boot/firmware/config.txt
overlay_dir=/boot/firmware/overlays
if [ -f "$boot_config" ] && [ -d "$overlay_dir" ]; then
    dtc -@ -I dts -O dtb -o "$overlay_dir/receiver-si4703.dtbo" hardware/receiver-si4703-overlay.dts
    if ! grep -q '^dtparam=i2c_arm=on$' "$boot_config"; then
        printf '\ndtparam=i2c_arm=on\n' >> "$boot_config"
    fi
    if ! grep -q '^dtoverlay=receiver-si4703$' "$boot_config"; then
        printf 'dtoverlay=receiver-si4703\n' >> "$boot_config"
    fi
else
    echo "CM4 boot config not found; compile and enable hardware/receiver-si4703-overlay.dts manually."
fi

systemctl daemon-reload
systemctl enable tv-receiver.service
systemctl restart tv-receiver.service
echo "Installed. Reboot to activate the Si4703 overlay, then add a dvbv5-compatible channel list at /etc/receiver/channels.conf."

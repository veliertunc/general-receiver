#!/bin/sh
set -eu
external_dir="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
install -d "$BINARIES_DIR/rpi-firmware/overlays"
"$HOST_DIR/bin/dtc" -@ -I dts -O dtb -o "$BINARIES_DIR/rpi-firmware/overlays/receiver-si4703.dtbo" "$external_dir/../hardware/receiver-si4703-overlay.dts"
cat >> "$BINARIES_DIR/rpi-firmware/config.txt" <<'EOF'

[all]
device_tree=bcm2711-rpi-cm4.dtb
dtparam=i2c_arm=on
dtoverlay=receiver-si4703
otg_mode=1
EOF
# Vendor tuner firmware is supplied by the hardware vendor, not generated here.
exec board/raspberrypi4-64/post-image.sh "$@"

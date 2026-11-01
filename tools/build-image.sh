#!/bin/sh
set -eu
# Run from a checked-out Buildroot source tree on Linux. Pass this repository.
repo=$(CDPATH= cd -- "${1:?Usage: build-image.sh /absolute/path/to/television-receiver}" && pwd)
test -f configs/raspberrypi4_64_defconfig
make BR2_EXTERNAL="$repo/buildroot" raspberrypi4_64_defconfig
support/kconfig/merge_config.sh -m .config "$repo/buildroot/receiver.fragment"
make BR2_EXTERNAL="$repo/buildroot" olddefconfig
make BR2_EXTERNAL="$repo/buildroot"
echo "Image: output/images/sdcard.img (bench validation required before deployment)"

#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arch=${1:-arm64}
test -f "$repo/third_party/linux/Makefile" || {
    echo 'Initialize third_party/linux first; see README.md' >&2
    exit 1
}
mkdir -p "$repo/build/linux-headers" "$repo/build/linux-uapi"
make -C "$repo/third_party/linux" O="$repo/build/linux-headers" ARCH="$arch" \
    INSTALL_HDR_PATH="$repo/build/linux-uapi" headers_install

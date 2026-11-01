#!/bin/sh
set -eu
test "$(id -u)" -eq 0 || { echo 'Run as root on the CM4 Linux target'; exit 1; }
apt-get update
apt-get install -y build-essential cmake device-tree-compiler
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
install -m 0755 build/receiver /usr/bin/receiver
install -d /etc/receiver
for name in receiver.conf channels.conf; do
    test -f "/etc/receiver/$name" || install -m 0644 "config/$name" "/etc/receiver/$name"
done
id receiver >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin --groups video,audio receiver
install -m 0644 systemd/tv-receiver.service /etc/systemd/system/tv-receiver.service
/usr/bin/receiver --check-config
systemctl daemon-reload
systemctl enable tv-receiver.service
systemctl restart tv-receiver.service
echo 'C receiver installed. Configure the radio overlay and tuner firmware as described in README.md.'

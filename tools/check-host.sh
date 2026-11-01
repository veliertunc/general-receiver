#!/bin/sh
# Linux host checks; no RF hardware is required.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
scratch=$(mktemp -d)
receiver_pid=
cleanup() {
    if [ -n "$receiver_pid" ]; then kill "$receiver_pid" 2>/dev/null || true; wait "$receiver_pid" 2>/dev/null || true; fi
    rm -rf "$scratch"
}
trap cleanup EXIT INT TERM
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -Werror "$repo"/firmware/c/*.c -pthread -lm -o "$scratch/receiver"
sed 's/port=8080/port=18087/' "$repo/config/receiver.conf" > "$scratch/receiver.conf"
"$scratch/receiver" --config "$scratch/receiver.conf" --check-config
sed 's/port=18087/port=0/' "$scratch/receiver.conf" > "$scratch/invalid.conf"
if "$scratch/receiver" --config "$scratch/invalid.conf" --check-config; then echo 'Invalid config accepted'; exit 1; fi
"$scratch/receiver" --config "$scratch/receiver.conf" > "$scratch/server.log" 2>&1 &
receiver_pid=$!
tries=0
until curl -fsS http://127.0.0.1:18087/status > "$scratch/status.json"; do
    tries=$((tries + 1)); test "$tries" -lt 20; sleep 0.1
done
grep -q 'television' "$scratch/status.json"
test "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:18087/unknown)" = 404
test "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:18087/tv.ts)" = 400
test "$(curl -s -o /dev/null -w '%{http_code}' -H 'Content-Type: application/json' -d '{"frequency_mhz":999}' http://127.0.0.1:18087/radio/tune)" = 400
test "$(curl -s -o /dev/null -w '%{http_code}' -H 'Content-Type: application/json' -d '{"frequency_mhz":99.5 garbage}' http://127.0.0.1:18087/radio/tune)" = 400
test "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:18087/ethernet/status)" = 200
echo 'C compilation, configuration rejection, and HTTP runtime checks passed.'

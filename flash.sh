#!/bin/bash
# Build + flash SD launcher to CYD
set -e
cd "$(dirname "$0")"
PORT=${1:-/dev/ttyUSB0}
pio run -e cyd-2432s028
pio run -e cyd-2432s028 -t upload --upload-port "$PORT"
echo "Launcher flashed. Open monitor: pio device monitor -b 115200"

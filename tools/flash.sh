#!/usr/bin/env bash
# Upload the last build for BOARD (see tools/build.sh) to the first /dev/cu.usbmodem* port.
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/board.sh
PORT="${PORT:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)}"
if [ -z "$PORT" ]; then
  echo "No /dev/cu.usbmodem* port found." >&2
  if [ "$BOARD" = waveshare ]; then
    echo "The Waveshare knob has a USB switch: if only a /dev/cu.usbserial-* (CH340) port shows up," >&2
    echo "you are talking to its secondary ESP32. Flip the USB-C plug over to reach the ESP32-S3." >&2
  fi
  exit 1
fi
arduino-cli upload --fqbn "$FQBN" --input-dir "$BUILD_PATH" -p "$PORT" firmware/VoltraKnob
echo "Flashed $BOARD build to $PORT"

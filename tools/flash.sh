#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=custom,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600"
PORT="${PORT:-$(ls /dev/cu.usbmodem* | head -1)}"
arduino-cli upload --fqbn "$FQBN" --input-dir build/VoltraKnob -p "$PORT" firmware/VoltraKnob
echo "Flashed to $PORT"

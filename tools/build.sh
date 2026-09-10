#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=custom,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600"
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries firmware/libraries \
  --build-path build/VoltraKnob \
  --warnings default \
  firmware/VoltraKnob "$@"

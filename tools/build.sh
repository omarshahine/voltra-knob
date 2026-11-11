#!/usr/bin/env bash
# Compile the firmware. BOARD selects the hardware:
#   BOARD=crowpanel  (default) Elecrow CrowPanel 2.1" ESP32 Rotary Display
#   BOARD=waveshare            Waveshare ESP32-S3-Knob-Touch-LCD-1.8
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/board.sh
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries firmware/libraries \
  --build-path "$BUILD_PATH" \
  --build-property "compiler.cpp.extra_flags=$BOARD_FLAGS" \
  --build-property "compiler.c.extra_flags=$BOARD_FLAGS" \
  --warnings default \
  firmware/VoltraKnob "$@"

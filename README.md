# Voltra Knob

A physical dial for the Beyond Power VOLTRA I built on the Elecrow CrowPanel 2.1" ESP32-S3 rotary display. Turn to set weight, hold to load, tap to unload. Direct BLE, no phone or computer.

Design: `docs/superpowers/specs/2026-09-10-voltra-knob-design.md`

## Build

    tools/build.sh
    tools/flash.sh
    uv run --with pyserial tools/serial_tail.py --seconds 10

## Test (host)

    tools/test.sh

Protocol tables come from `@voltras/node-sdk` (MIT). See `third_party/`.

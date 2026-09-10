# Voltra Knob

A physical dial for the Beyond Power VOLTRA I built on the Elecrow CrowPanel 2.1" ESP32-S3 rotary display (sold on Amazon as GrowCube). Turn to set weight, tap to load, hold to arm auto-load, tap to release. Direct BLE from the knob to the trainer: no phone, computer, or cloud in the loop.

Design: `docs/superpowers/specs/2026-09-10-voltra-knob-design.md`
Plan: `docs/superpowers/plans/2026-09-10-voltra-knob.md`

## Usage

1. Power the Voltra, close the Beyond+ app, power the knob.
2. The knob scans, connects, and identifies itself as "Voltra Knob". Accept the prompt on the Voltra the first time. The screen shows the trainer's current weight, battery, and a blue Bluetooth glyph when ready.
3. **Turn** to set weight. Slow clicks step 1 lb. A fast spin steps 5 lb and snaps to fives, a sustained fast spin steps 10 lb and snaps to tens. The number updates instantly; the weight is sent to the Voltra a quarter second after you stop turning. Amber ring means waiting for the device to confirm.
4. **Tap** while unloaded: load now.
5. **Hold** 0.6 s while unloaded: arm auto-load. The pill turns amber "AUTO ARMED"; the Voltra engages on your first pull. Arming times out after 18 s.
6. **Tap** while loaded or armed: unload or cancel, always instant.
7. Loading or unloading from the Voltra itself is reflected on the knob. Reps count while loaded.

While loaded, a single dial change is limited to 25 lb from the confirmed weight; unload to make bigger jumps.

Hold the knob while powering on to forget the pinned Voltra and pair with whichever one is found first.

## Safety

Nothing is ever loaded automatically at boot or reconnect. Unload is a single tap and is never delayed. The Voltra disengages itself if the Bluetooth link drops. Keep the Beyond+ app disconnected while using the knob; the trainer accepts one controller at a time.

## Build

    tools/build.sh
    tools/flash.sh
    uv run --with pyserial tools/serial_tail.py --seconds 10

Requires `arduino-cli` with the `esp32:esp32` core 3.x and `NimBLE-Arduino` 2.x in the sketchbook. Display, touch, and expander libraries are vendored under `firmware/libraries/`.

## Test (host)

    tools/test.sh

Compiles the protocol and dial logic with clang and runs the assertion suites. No hardware needed.

## Protocol

Frame tables come from `@voltras/node-sdk` (MIT, see `third_party/`). `tools/gen_protocol.py` turns the decoded JSON into `frames.gen.h`, ports the SDK's CRC8/CRC16 to build the custom-name auth frame and the parameter-read frames, and verifies itself by regenerating the SDK's iPad auth frame byte for byte.

## Hardware notes

- Encoder A/B are on GPIO 42 and 4 with no reliable external pull-up. The firmware enables the internal pull-ups and decodes full quadrature over both channels; without both, clicks are dropped.
- One detent is two quadrature transitions on this encoder.
- The ST7701 panel needs the R/B swap in the flush callback and MADCTL 0x08, as in Elecrow's factory sketch.
- LVGL font compression is off in `lv_conf.h`; generate fonts with `--no-compress`.

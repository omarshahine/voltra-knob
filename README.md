# Voltra Knob

A physical dial for the [Beyond Power VOLTRA I](https://www.beyond-power.com/) cable trainer. Turn the knob to set the weight, tap to load, hold to arm auto-load, tap to release. It talks to the trainer directly over Bluetooth Low Energy from an ESP32-S3 round touch display. No phone, computer, or cloud service is involved once it is flashed.

This is an independent hobby project. It is not affiliated with or endorsed by Beyond Power.

## What it does

Everything below was verified on real hardware against a VOLTRA I.

| Feature | How |
| --- | --- |
| Connects on its own | Scans for the trainer's BLE service, authenticates, enters weight-training mode, subscribes to telemetry, and reads the current settings. Shows "Voltra Knob wants to connect" on the trainer the first time. |
| Shows the real state | Current weight, battery, Bluetooth status, load state, and rep count come from the trainer, not from the knob's memory. Loading or unloading from the trainer itself is reflected within a second. |
| Weight from the dial | One click is 1 lb. A fast spin steps 5 lb and snaps to fives; keep spinning and it steps 10 lb and snaps to tens. The number updates instantly; one BLE write with the final value goes out a quarter second after you stop. Amber ring while waiting for the trainer to confirm. |
| Tap to load | A single tap while unloaded engages the motor at the set weight. |
| Hold to arm auto-load | Hold 0.6 s: the trainer arms its auto-load. The knob follows the trainer's own state machine: AUTO ARMED, PULL DETECTED (3 s countdown), ENGAGING, then LOADED. Auto-load stays active between sets until you tap. |
| Tap to release | A single tap while loaded or armed sends stop. It is never delayed, throttled, or gated. |
| Safety limits | While loaded, one dial change can move at most 25 lb from the confirmed weight. Nothing is ever loaded at boot or reconnect, and the knob never releases the trainer on its own. |
| Reconnects | If the trainer powers off, the knob shows SEARCHING and reconnects when it returns. The trainer's address is remembered; hold the knob while powering on to forget it. |

## Hardware

Built and tested on the **Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display**, sold on Amazon under the GrowCube name as "ESP32 Rotary Display 2.1 Inch HMI IPS Round Knob Touch Display". This is the exact unit used: [Amazon link](https://amzn.to/3UJtFTg) (affiliate link).

| Part | Detail |
| --- | --- |
| MCU | ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM, Wi-Fi + BLE 5.0, native USB |
| Display | 2.1" round IPS, 480x480, ST7701 over parallel RGB |
| Touch | CST8xx capacitive (initialized, unused so far) |
| Input | Rotary encoder with push button, encoder on GPIO 42/4, button via a PCF8574 I2C expander |
| Trainer | Beyond Power VOLTRA I |

### Also supported: Waveshare ESP32-S3-Knob-Touch-LCD-1.8

The [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8) (360x360 ST77916 QSPI display, CST816 touch, DRV2605 haptics) works too. Build and flash it with `BOARD=waveshare tools/build.sh` and `BOARD=waveshare tools/flash.sh`. The differences:

- **No push button.** The touchscreen is the button: tap anywhere to load or release, hold anywhere to arm auto-load, and touch the screen while powering on to forget the trainer.
- **Haptics.** A tick while turning, a click on load or release, a double click on arm.
- **Two chips behind a USB switch.** The ESP32-S3 enumerates as an Espressif USB JTAG/serial device (`303A:1001`, `/dev/cu.usbmodem*`). If you get a CH340 port (`/dev/cu.usbserial-*`) instead, you are talking to the board's secondary ESP32: flip the USB-C plug over.
- If clockwise turns lower the weight, set `ENCODER_INVERT = 1` in `firmware/VoltraKnob/src/board/board_waveshare.cpp`.

Elecrow publishes the schematic and factory source at [Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen). Other boards with the same MCU and panel should work with pin changes in `firmware/VoltraKnob/src/board/board.cpp`. Board selection lives in `src/board/board_config.h`.

## Using it

1. Power the VOLTRA and fully close the Beyond+ app on your phone. The trainer accepts one controller at a time.
2. Power the knob over USB-C. It shows SEARCHING, then CONNECTING, then the trainer's current weight with a blue Bluetooth glyph and the battery level.
3. The first time, accept "Voltra Knob wants to connect" on the trainer's screen.
4. Turn to set weight. Tap to load. Pull. Tap to release.
5. For auto-load: hold the knob until the inner red ring completes. The pill turns amber AUTO ARMED. Grab the handle and pull; the trainer counts down, engages, and the knob turns green. Tap to end the session.

If the pill says APP CONNECTED?, the trainer is refusing the link three times in a row, which almost always means the phone app is still connected.

## Building

Tested with `arduino-cli` 1.5.1, the `esp32:esp32` core 3.3.11, and `NimBLE-Arduino` 2.5.1 on macOS. The display, touch, I2C expander, and LVGL 9.1 libraries are vendored under `firmware/libraries/` so the build is reproducible.

```bash
brew install arduino-cli uv            # or your platform's equivalents
arduino-cli config init
arduino-cli config set board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "NimBLE-Arduino"

tools/build.sh                                        # compile
tools/flash.sh                                        # upload over USB (first /dev/cu.usbmodem*)
uv run --with pyserial tools/serial_tail.py --seconds 10   # watch the serial log
```

The board enumerates as an Espressif USB JTAG/serial device; no driver is needed on macOS. Set `PORT=/dev/...` to override the port.

### Host tests

```bash
tools/test.sh
```

Compiles the protocol tables, notification decoder, and dial/button logic with clang and runs the assertion suites. No hardware needed. Run this before flashing.

### Regenerating the protocol tables

`third_party/voltra-protocol.json` is the decoded protocol data from the SDK. `tools/gen_protocol.py` turns it into `firmware/VoltraKnob/src/protocol/frames.gen.h`, ports the SDK's CRC8 and CRC16 to build the custom-name auth frame and the parameter-read frames, and checks itself by regenerating the SDK's iPad auth frame byte for byte. The generated header is committed so builds do not need Python.

## How it works

```
Encoder task ─┐                         ┌─ BLE task (NimBLE) ── VOLTRA
              ├─ main loop: app logic ──┤
Button poll ──┘        │                └─ shared DeviceState (mutex)
                       └─ LVGL UI (ViewModel)
```

| Module | Responsibility |
| --- | --- |
| `src/protocol` | Pure C++: frame tables, encoders, and a decoder that turns notifications into events (settings, mode, battery, telemetry, reps, auto-load registers). Host-tested. |
| `src/app` | Pure C++: dial ramp and debounce, confirmation tracking, tap/hold classification, load-gate decisions. Host-tested. |
| `src/link` | NimBLE client task: scan, connect, handshake, coalesced writes, status polling, reconnect with backoff. Owns `DeviceState`. |
| `src/board` | Display, touch, expander, encoder, backlight. Reuses Elecrow's proven ST7701 init. |
| `src/ui` | LVGL screen: 150 px numerals, tick ring, weight arc with glow, hold ring, state pill, glyphs. |

Protocol facts learned the hard way are in the spec and plan under `docs/superpowers/`, including the auto-load register map (fitness mode `0x27` with `53c7` = 11 waiting, 12 countdown, 13 engaging, 14 loaded).

## Hardware notes

- The encoder lines float between detents. The firmware enables the internal pull-ups and decodes full quadrature over both channels; without both, clicks are dropped. One detent is two transitions.
- The ST7701 panel needs the red/blue swap in the flush callback and MADCTL `0x08`, as in Elecrow's factory sketch.
- LVGL font compression is off in `lv_conf.h`; generate fonts with `lv_font_conv --no-compress`.
- A full ring redraw costs about 190 ms at 480x480, so the ring is throttled while the numeral updates every frame.

## Credits

- BLE protocol tables from [`@voltras/node-sdk`](https://www.npmjs.com/package/@voltras/node-sdk) by Henry Jewkes (MIT). See `third_party/`.
- Display bring-up from Elecrow's factory sketch.
- Vendored libraries: LVGL, Arduino_GFX, Adafruit BusIO, Adafruit CST8XX, PCF8574 (see `THIRD_PARTY.md`).

## Safety

This controls a motor that pulls on a cable with up to 200 lb of force. Read the code before you trust it, start with light weights, keep the cable clear, and keep the Beyond+ app's own controls handy. Use at your own risk.

## License

MIT. See `LICENSE`.

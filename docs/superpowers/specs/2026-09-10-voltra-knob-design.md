# Voltra Knob: design spec

Date: 2026-09-10
Status: approved design, pending implementation plan

## Goal

A standalone physical dial for the Beyond Power VOLTRA I. Turn the knob to set the weight, hold the knob to load the motor, tap the knob to unload. Nothing else is required for v1. The knob talks to the Voltra directly over Bluetooth Low Energy. No phone, Mac, Pi, or cloud is involved at any point.

## Hardware

Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display (sold on Amazon as GrowCube, ASIN B0GVYV3HZN).

| Part | Detail |
| --- | --- |
| MCU | ESP32-S3R8, dual core 240 MHz, 16 MB flash, 8 MB PSRAM, Wi-Fi + BLE 5.0 |
| Display | 2.1" round IPS, 480x480, ST7701 over parallel RGB, 16-bit color |
| Touch | CST8xx capacitive, I2C address 0x15 |
| I/O expander | PCF8574 at I2C 0x21: P0 touch reset, P2 touch INT, P3 LCD power, P4 LCD reset, P5 encoder push button (active low, pull-up) |
| Encoder | A on GPIO 42, B on GPIO 4, quadrature |
| Backlight | GPIO 6, PWM |
| I2C bus | SDA GPIO 38, SCL GPIO 39 |
| USB | Native USB serial/JTAG, `/dev/cu.usbmodem*` on the Mac |

Display pin map, ST7701 init table, RGB timing, MADCTL fix, and the R/B swap in the flush callback are taken verbatim from Elecrow's factory sketch `RotaryScreen_2_1.ino`. Those are known-good and are not re-derived.

## Voltra BLE protocol

Source: `@voltras/node-sdk` 0.14.0 (MIT, Henry Jewkes). The decoded protocol tables are vendored at `third_party/voltra-protocol.json`; the license is at `third_party/voltra-node-sdk-LICENSE`. The firmware uses only pre-computed frames, so no CRC code is needed in v1.

| Item | Value |
| --- | --- |
| Service UUID | `E4DADA34-0867-8783-9F70-2CA29216C7E4` |
| Write characteristic | `A010891D-F50F-44F0-901F-9A2421A9E050` |
| Notify characteristic | `55CA1E52-7354-25DE-6AFC-B7DF1E8816AC` |
| Advertised name prefix | `VTR-` |

Connection sequence, in order:

1. Scan for the service UUID. Connect to the first `VTR-` device seen, or to a pinned address saved in NVS after the first successful connection.
2. Subscribe to the notify characteristic.
3. Write the 41-byte auth frame (`commands.auth.iphone`). Wait 3000 ms.
4. Write the two init frames (`commands.init[0]`, `commands.init[1]`) with 20 ms between them.
5. Write the mode frame `commands.modes.weightTraining`.
6. Treat the link as ready when a settings update (`552e`) or mode confirmation (`5512`) arrives.

Commands used in v1:

| Action | Frames |
| --- | --- |
| Set weight N lb (5..200) | `commands.weights[N]`, one 19-byte frame |
| Load | `commands.workout.setup`, wait 300 ms, `commands.workout.go` |
| Unload | `commands.workout.stop` |
| Enter weight training | `commands.modes.weightTraining` |

Consecutive writes are spaced at least 500 ms apart (the SDK's `COMMAND_DELAY_MS`) except the init pair.

Notifications decoded in v1:

| Header | Meaning | Fields used |
| --- | --- | --- |
| `552e` (46 bytes) | Settings update | Param list at offset 13, count at 11. Read `863e` base weight (uint16 LE) and `b04f` training mode (uint8). |
| `5516` | Multi-param update | Same param walk as above. |
| `5512` (18 bytes) | Mode confirmation | Param id at 13, value at 15. |
| `5523` (35 bytes) | Device init | Battery percent at 11. |
| `5534` (52 bytes) | Status | Battery percent at 12, unless bytes 2..3 are `04 ac` and byte 10..11 mark an `aa 80 25` state dump, which is ignored. |
| `553a0470` | Telemetry stream | Phase at 13 (0 idle, 1 concentric, 2 hold, 3 eccentric). Used only to derive rep count and the "active" indicator. |

A rep is counted on a phase transition from eccentric back to idle or concentric while loaded. This is a v1 approximation; the SDK's per-rep summary frames can replace it later.

Constraints:

- The Voltra accepts one BLE central at a time. The Beyond+ app must not be connected while the knob is in use. The UI says so when the device is visible in scans but refuses connection.
- The device drops the motor itself when the BLE link is lost. The firmware never relies on this as the primary stop; it is the backstop.

## Firmware architecture

Arduino framework on the ESP32 Arduino core 3.x, built with `arduino-cli` (Homebrew), no IDE. Libraries are vendored under `firmware/libraries/` from the Elecrow repo so builds are reproducible: `GFX_Library_for_Arduino` 1.6.7, `lvgl` 9.1.0 with the project's `lv_conf.h`, `PCF8574_library` 2.3.7, `Adafruit_CST8XX_Library` 1.1.1, `Adafruit_BusIO` 1.16.2. `NimBLE-Arduino` 2.x is added for BLE. Board settings: `esp32:esp32:esp32s3`, OPI PSRAM, 16 MB flash, Elecrow partition table `elecrow_s3.csv` (10 MB app with OTA slot, 1.3 MB SPIFFS), USB CDC on boot.

Modules, each with one responsibility and a header that a test or another module can use without reading the internals:

| Module | Responsibility | Depends on |
| --- | --- | --- |
| `protocol/` | Pure C++. Frame tables generated from `voltra-protocol.json` by `tools/gen_protocol.py`. Encoders (`weightFrame(n)`, `loadSetup()`, `loadGo()`, `unload()`, `modeWeightTraining()`, `auth()`, `init()`) and a notification decoder that turns a byte buffer into a `VoltraEvent` (settings, mode, battery, telemetry phase, unknown). No Arduino headers. | nothing |
| `voltra_link` | NimBLE client. Scan, connect, subscribe, run the auth/init sequence, serialize writes with the 500 ms spacing, feed notifications to the decoder, auto-reconnect with backoff. Exposes `requestWeight(n)`, `requestLoad()`, `requestUnload()`, a `LinkState` enum and a `DeviceState` struct. Runs on its own FreeRTOS task. | protocol |
| `board` | Display bring-up (PCF8574 resets, ST7701 init, backlight), touch init, encoder ISR/task producing `Rotate(+/-n)`, `Press`, `LongPress` events into a queue. | vendored libs |
| `ui` | LVGL screens and widgets. Renders a `ViewModel` struct. No knowledge of BLE. | lvgl |
| `app` | The state machine. Consumes input events and `voltra_link` state, applies safety rules, produces the `ViewModel`. | protocol, voltra_link, board, ui |

Data flow: encoder ISR → queue → `app` → `voltra_link` → BLE. Notifications → `voltra_link` task → `DeviceState` (mutex-protected) → `app` on the LVGL loop → `ui`.

All LVGL calls happen on the main loop task. The BLE task never touches LVGL.

## Application behavior

### Boot

1. Backlight on, splash with "VOLTRA" and "Searching" state.
2. Start scanning. Never send a load command on boot or reconnect, under any condition.
3. On connect, run the sequence above and enter weight training. The displayed weight is whatever the device reports in its first settings update, never a locally stored value.

### Dial: weight

- Each detent changes the target weight by 1 lb. If more than 4 detents arrive within 150 ms, the step becomes 5 lb for the rest of that burst.
- Target is clamped to 5..200 lb.
- The display updates instantly to the target. The frame is sent 250 ms after the last detent, with the final target only, so spinning the dial does not flood the link.
- When the device confirms the weight in a settings update, the "pending" style clears. If no confirmation arrives within 2 s, the display reverts to the last device-confirmed weight and shows a brief "not applied" notice.
- While loaded, a single send may move the weight at most 25 lb from the last device-confirmed weight. Turning further stops at that limit and the ring flashes amber. Unload to make bigger jumps.

### Button: load and unload

- Tap (press under 400 ms) while loaded: send unload immediately. This is always the fastest action on the device.
- Hold (press 600 ms or longer) while unloaded and connected: the ring fills during the hold; when it completes, send setup then go. Releasing before 600 ms cancels with no command sent.
- Tap while unloaded, or hold while loaded: no command, short haptic-style flash on the ring so the press is acknowledged.
- Any press while disconnected: no command; the UI shows the connection state.

### Display layout (480x480 round)

- Center: weight in lb, very large numerals (Montserrat 48 or a larger custom font). Below it a small "lb" label.
- Outer ring: an arc spanning 5..200 lb showing the current target. Grey when unloaded, green when loaded, amber while the weight is pending or capped, red while the hold-to-load ring is filling. The hold progress fills a second, inner arc.
- Top: BLE status dot (grey scanning, blue connected) and battery percent when known.
- Bottom: state word ("UNLOADED", "LOADED", "SEARCHING", "CONNECTING", "APP CONNECTED?") and rep count for the current loaded period.
- Dark background. Everything readable from two meters.

Touch is initialized but unused in v1.

### Connection handling

- Disconnect: state goes to "SEARCHING", the weight number dims, rep count resets, and scanning restarts with backoff 1 s, 2 s, 4 s, capped at 8 s.
- Reconnect: repeat the full auth/init/mode sequence. Never auto-load.
- The first successful device address is saved to NVS and preferred on later scans. Holding the button for 5 s on the splash screen clears it.

## Safety rules (summary)

1. No load command is ever sent without a completed 600 ms hold on the physical button.
2. Unload is a single tap and is never gated, throttled, or debounced beyond 30 ms contact bounce.
3. Loaded weight changes are limited to 25 lb per send from the device-confirmed value.
4. Nothing is auto-loaded on boot, reconnect, or mode entry.
5. Weight shown is device-confirmed unless explicitly styled as pending.
6. On BLE loss the device stops itself; the knob shows the loss within one second.

## Error handling

- BLE write failure: retry once after 200 ms, then mark the link as broken and reconnect.
- Auth timeout (no notification within 3 s of the auth frame): disconnect and rescan. After three consecutive failures, show "APP CONNECTED?" as the likely cause.
- Unknown notification headers are counted and ignored; the count is printed to the serial console every 10 s in debug builds.
- Display or touch init failure: print to serial, keep running the BLE side, and show a plain-text fallback if the display is up but touch is not.

## Build and flash

```
tools/build.sh      # arduino-cli compile with vendored libraries
tools/flash.sh      # arduino-cli upload to /dev/cu.usbmodem*, then open serial monitor
tools/gen_protocol.py   # regenerates firmware/VoltraKnob/protocol/frames.gen.h from third_party/voltra-protocol.json
```

The generated header is committed so builds do not require Python.

## Testing

- `protocol/` has host-side unit tests compiled with clang on the Mac (`tools/test.sh`), covering: every weight 5..200 maps to the expected frame from the JSON; decoder parses captured `552e`, `5512`, `5523`, `5534`, and `553a0470` frames into the right events; the `aa 80 25` state-dump alias does not produce a battery reading; unknown headers return `Unknown`.
- `app` state machine logic (debounce, burst step, clamp, 25 lb cap, hold timing, tap vs hold classification) is written as plain C++ with injected time and tested on the host too.
- Hardware verification on the bench, in this order: display shows the weight screen; encoder changes the number; scan finds `VTR-`; auth and mode entry succeed with the Beyond+ app closed; weight change is confirmed by the device; hold loads at 10 lb with the cable at zero; tap unloads; pulling the cable increments reps; powering off the Voltra drops the knob to "SEARCHING" and it reconnects when the Voltra returns.

## Out of scope for v1

Mode picker, chains, eccentric, inverse chains, damper, isokinetic, isometric, rowing, custom curves, session templates, Wi-Fi, OTA updates, HomeKit or Homebridge integration, twin mode, workout history, and any cloud API use. The architecture leaves room for the mode picker (a second `ui` screen and additional `protocol` encoders) without changing `voltra_link` or `app` interfaces.

## Open questions

None blocking. The rep-count heuristic and the exact large font are implementation details to tune on the bench.

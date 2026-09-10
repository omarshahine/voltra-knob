# Voltra Knob Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware for the Elecrow CrowPanel 2.1" ESP32-S3 rotary display that sets Voltra weight with the dial, loads on a 600 ms hold, and unloads on a tap, over a direct BLE link.

**Architecture:** A pure C++ `protocol` module (generated frame tables plus a notification decoder) and a pure C++ `app_logic` module (dial debounce, button classification, safety gate) are host-tested with clang. A `voltra_link` FreeRTOS task owns NimBLE and exposes a mutex-guarded `DeviceState`. A `board` module reuses Elecrow's proven display/touch/encoder bring-up. A `ui` module renders a `ViewModel` with LVGL 9. The `.ino` wires them together on the main loop.

**Tech Stack:** Arduino framework on ESP32 core 3.3.11 via `arduino-cli` 1.5.1; vendored `GFX_Library_for_Arduino` 1.6.7, `lvgl` 9.1.0, `PCF8574_library` 2.3.7, `Adafruit_CST8XX_Library` 1.1.1, `Adafruit_BusIO` 1.16.2; `NimBLE-Arduino` 2.5.1 (installed in the sketchbook); Python 3 for the frame generator; clang++ for host tests; `uv run --with pyserial` for serial capture.

**Spec:** `docs/superpowers/specs/2026-09-10-voltra-knob-design.md`

## Deviations from the spec

- Clearing the pinned Voltra address is a button hold during the first two seconds after power-on rather than a 5 s hold on the splash; simpler and equally deliberate.
- Rep counting uses the device's vendor per-rep frames (repCount byte) instead of the telemetry phase heuristic; more accurate.
- The "unknown notification count every 10 s" debug print is omitted; add a hex dump in `onNotify` when diagnosing.

## Global Constraints

- Weight range 5..200 lb; step 1 lb, burst step 5 lb after 4 detents within 150 ms.
- Send weight 250 ms after the last detent, final value only; confirmation timeout 2000 ms.
- While loaded, a single send may move at most 25 lb from the device-confirmed weight.
- Load = `setup` frame, wait 300 ms, `go` frame. Unload = `stop` frame. Never auto-load.
- Consecutive BLE writes spaced at least 500 ms apart, except the two init frames (20 ms).
- Auth wait 3000 ms; handshake deadline 5000 ms; request MTU 527.
- Tap is a press under 400 ms; hold completes at 600 ms; contact bounce 30 ms.
- Reconnect backoff 1 s, 2 s, 4 s, capped at 8 s.
- All LVGL calls on the main loop task only.
- Every commit ends with the session attribution trailer:
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_01MT9Hj2yGzpd6Y8SwnjuZAy`.
- Never use `rm`; use `trash`.
- Board FQBN: `esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=custom,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600`.
- Serial port: `/dev/cu.usbmodem1201` (or whatever `ls /dev/cu.usbmodem*` returns).

## File Structure

```
voltra-knob/
  README.md
  .gitignore
  third_party/voltra-protocol.json            (exists) decoded SDK protocol tables
  third_party/voltra-node-sdk-LICENSE         (exists) MIT
  tools/gen_protocol.py                       JSON -> frames.gen.h
  tools/build.sh                              arduino-cli compile
  tools/flash.sh                              arduino-cli upload
  tools/serial_tail.py                        capture N seconds of serial output
  tools/test.sh                               build and run host tests
  test/minitest.h                             tiny assert framework
  test/test_protocol.cpp                      frame tables + decoder
  test/test_app_logic.cpp                     dial, button, load gate
  firmware/libraries/                         vendored Arduino libraries + lv_conf.h
  firmware/VoltraKnob/VoltraKnob.ino          setup/loop wiring
  firmware/VoltraKnob/partitions.csv          Elecrow 10 MB app + OTA layout
  firmware/VoltraKnob/src/protocol/frames.gen.h        generated, committed
  firmware/VoltraKnob/src/protocol/voltra_protocol.h   frame accessors + decoder API
  firmware/VoltraKnob/src/protocol/voltra_protocol.cpp
  firmware/VoltraKnob/src/app/app_logic.h              WeightDial, ButtonClassifier, decideLoad
  firmware/VoltraKnob/src/app/app_logic.cpp
  firmware/VoltraKnob/src/link/voltra_link.h           NimBLE task + DeviceState
  firmware/VoltraKnob/src/link/voltra_link.cpp
  firmware/VoltraKnob/src/board/board.h                display, touch, encoder, button, backlight
  firmware/VoltraKnob/src/board/board.cpp
  firmware/VoltraKnob/src/ui/ui.h                      ViewModel + render
  firmware/VoltraKnob/src/ui/ui.cpp
```

`protocol` and `app` contain no Arduino includes so the host tests compile them directly.

---

### Task 1: Scaffold, vendored libraries, build scripts, first flash

**Files:**
- Create: `tools/build.sh`, `tools/flash.sh`, `tools/serial_tail.py`
- Create: `firmware/libraries/` (copied from the Elecrow repo clone at `<path-to-elecrow-repo>/example/Arduino/libraries`; re-clone `https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen` if that path is gone)
- Create: `firmware/VoltraKnob/VoltraKnob.ino`, `firmware/VoltraKnob/partitions.csv`
- Create: `README.md`

**Interfaces:**
- Produces: `tools/build.sh` (compiles, exits non-zero on failure), `tools/flash.sh` (uploads the last build), `tools/serial_tail.py --seconds N` (prints serial lines then exits).

- [ ] **Step 1: Vendor the libraries, trimmed**

```bash
cd ~/GitHub/voltra-knob
SRC=<path-to-elecrow-repo>/example/Arduino/libraries
mkdir -p firmware/libraries
cp -R "$SRC/Adafruit_BusIO" "$SRC/Adafruit_CST8XX_Library" "$SRC/PCF8574_library-master" firmware/libraries/
mkdir -p firmware/libraries/GFX_Library_for_Arduino
cp -R "$SRC/GFX_Library_for_Arduino/src" "$SRC/GFX_Library_for_Arduino/library.properties" "$SRC/GFX_Library_for_Arduino/LICENSE" firmware/libraries/GFX_Library_for_Arduino/
mkdir -p firmware/libraries/lvgl
cp -R "$SRC/lvgl/src" "$SRC/lvgl/lvgl.h" "$SRC/lvgl/lv_version.h" "$SRC/lvgl/library.properties" "$SRC/lvgl/library.json" "$SRC/lvgl/LICENCE.txt" firmware/libraries/lvgl/
cp "$SRC/lv_conf.h" firmware/libraries/lv_conf.h
du -sh firmware/libraries/*
```

Expected: five library folders plus `lv_conf.h`; lvgl under 30 MB.

- [ ] **Step 2: Enable the large fonts in `firmware/libraries/lv_conf.h`**

Change these three lines from `0` to `1`:

```c
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
```

Verify: `grep -nE "LV_FONT_MONTSERRAT_(28|36|48) 1" firmware/libraries/lv_conf.h` prints three lines.

- [ ] **Step 3: Write the partition table**

`firmware/VoltraKnob/partitions.csv`:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0xA00000,
spiffs,   data, spiffs,  0xA10000,0xE0000,
coredump, data, coredump,0xAF0000,0x10000,
```

- [ ] **Step 4: Write the build, flash, and serial scripts**

`tools/build.sh`:

```bash
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
```

`tools/flash.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=custom,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600"
PORT="${PORT:-$(ls /dev/cu.usbmodem* | head -1)}"
arduino-cli upload --fqbn "$FQBN" --input-dir build/VoltraKnob -p "$PORT" firmware/VoltraKnob
echo "Flashed to $PORT"
```

`tools/serial_tail.py`:

```python
#!/usr/bin/env python3
"""Print serial output for N seconds, then exit. Run with: uv run --with pyserial tools/serial_tail.py --seconds 10"""
import argparse, glob, sys, time
import serial

p = argparse.ArgumentParser()
p.add_argument("--port", default=None)
p.add_argument("--seconds", type=float, default=10)
p.add_argument("--baud", type=int, default=115200)
a = p.parse_args()
port = a.port or sorted(glob.glob("/dev/cu.usbmodem*"))[0]
deadline = time.time() + a.seconds
with serial.Serial(port, a.baud, timeout=0.5) as s:
    while time.time() < deadline:
        line = s.readline()
        if line:
            sys.stdout.write(line.decode("utf-8", "replace"))
            sys.stdout.flush()
```

Then `chmod +x tools/build.sh tools/flash.sh`.

- [ ] **Step 5: Write the minimal sketch that brings up the display**

`firmware/VoltraKnob/VoltraKnob.ino`:

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include "PCF8574.h"

// Board wiring (Elecrow CrowPanel 2.1" rotary). See spec, Hardware section.
static const int I2C_SDA_PIN = 38;
static const int I2C_SCL_PIN = 39;
static const int SCREEN_BACKLIGHT_PIN = 6;
static PCF8574 pcf8574(0x21);

static Arduino_DataBus *panelInitBus = new Arduino_SWSPI(
  GFX_NOT_DEFINED, 16 /* CS */, 2 /* SCK */, 1 /* SDA */, GFX_NOT_DEFINED);

static Arduino_ESP32RGBPanel *rgbPanel = new Arduino_ESP32RGBPanel(
  40 /* DE */, 7 /* VSYNC */, 15 /* HSYNC */, 41 /* PCLK */,
  46, 3, 8, 18, 17,            /* R0..R4 */
  14, 13, 12, 11, 10, 9,       /* G0..G5 */
  5, 45, 48, 47, 21,           /* B0..B4 */
  1, 10, 4, 20,                /* hsync polarity, front porch, pulse, back porch */
  1, 10, 4, 20,                /* vsync polarity, front porch, pulse, back porch */
  0, 12000000, false, 0, 0,
  480 * 20);

static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480, rgbPanel, 0, true, panelInitBus, GFX_NOT_DEFINED,
  st7701_type5_init_operations, sizeof(st7701_type5_init_operations));

static uint8_t *buf1 = nullptr;
static uint8_t *buf2 = nullptr;

static void disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  uint16_t *pixels = (uint16_t *)px_map;
  for (uint32_t i = 0; i < w * h; ++i) {
    const uint16_t c = pixels[i];
    pixels[i] = (c & 0x07E0) | ((c & 0x001F) << 11) | ((c & 0xF800) >> 11);
  }
  gfx->draw16bitRGBBitmap(area->x1, area->y1, pixels, w, h);
  lv_display_flush_ready(display);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("[BOOT] VoltraKnob task1 ESP32 Arduino %s LVGL %d.%d.%d\n",
                ESP_ARDUINO_VERSION_STR, LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  pcf8574.pinMode(P0, OUTPUT);
  pcf8574.pinMode(P2, OUTPUT);
  pcf8574.pinMode(P3, OUTPUT);
  pcf8574.pinMode(P4, OUTPUT);
  pcf8574.pinMode(P5, INPUT_PULLUP);
  Serial.println(pcf8574.begin() ? "[BOOT] pcf8574 ok" : "[BOOT] pcf8574 FAIL");

  pcf8574.digitalWrite(P3, HIGH); delay(100);
  pcf8574.digitalWrite(P4, HIGH); delay(100);
  pcf8574.digitalWrite(P4, LOW);  delay(120);
  pcf8574.digitalWrite(P4, HIGH); delay(120);
  pcf8574.digitalWrite(P0, HIGH); delay(100);
  pcf8574.digitalWrite(P0, LOW);  delay(120);
  pcf8574.digitalWrite(P0, HIGH); delay(120);
  pcf8574.digitalWrite(P2, HIGH); delay(120);

  gfx->begin();
  panelInitBus->beginWrite();
  panelInitBus->writeCommand(0x36);
  panelInitBus->write(0x08);
  panelInitBus->endWrite();
  gfx->fillScreen(0x0000);

  lv_init();
  lv_tick_set_cb(millis);
  const size_t bufSize = sizeof(uint16_t) * 480 * 480;
  buf1 = (uint8_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
  buf2 = (uint8_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
  if (!buf1 || !buf2) { Serial.println("[FATAL] LVGL buffers"); while (true) delay(1000); }
  lv_display_t *display = lv_display_create(480, 480);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, disp_flush);
  lv_display_set_buffers(display, buf1, buf2, bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_obj_t *label = lv_label_create(lv_screen_active());
  lv_label_set_text(label, "VOLTRA");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
  lv_obj_center(label);

  delay(200);
  ledcAttach(SCREEN_BACKLIGHT_PIN, 5000, 8);
  ledcWrite(SCREEN_BACKLIGHT_PIN, 204);
  pcf8574.digitalWrite(P3, LOW);
  Serial.println("[BOOT] display ready");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
```

- [ ] **Step 6: Build**

Run: `tools/build.sh 2>&1 | tail -15`
Expected: ends with `Sketch uses N bytes (x%) of program storage space.` and no `error:` lines. The line `Using custom partition file` or a `partitions.csv` copy is visible with `--verbose` if needed. If `st7701_type5_init_operations` is undeclared, the GFX copy missed `src/display/Arduino_RGB_Display.h`; recopy.

- [ ] **Step 7: Flash and verify**

Run: `tools/flash.sh 2>&1 | tail -5 && uv run --with pyserial tools/serial_tail.py --seconds 8`
Expected serial: `[BOOT] VoltraKnob task1 ...`, `[BOOT] pcf8574 ok`, `[BOOT] display ready`. The screen shows white "VOLTRA" on black. Ask the user to confirm the screen if it cannot be verified otherwise.

- [ ] **Step 8: README and commit**

`README.md`:

```markdown
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
```

```bash
git add -A
git commit -m "Scaffold firmware project with vendored libraries and display bring-up

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MT9Hj2yGzpd6Y8SwnjuZAy"
```

---

### Task 2: Protocol frame generator and frame accessors

**Files:**
- Create: `tools/gen_protocol.py`, `tools/test.sh`, `test/minitest.h`, `test/test_protocol.cpp`
- Create: `firmware/VoltraKnob/src/protocol/frames.gen.h`, `firmware/VoltraKnob/src/protocol/voltra_protocol.h`, `firmware/VoltraKnob/src/protocol/voltra_protocol.cpp`

**Interfaces:**
- Produces (namespace `voltra`):
  - `struct Frame { const uint8_t* data; size_t len; };`
  - `Frame authFrame(); Frame initFrame(size_t i); constexpr size_t INIT_COUNT = 2;`
  - `Frame setupFrame(); Frame goFrame(); Frame stopFrame(); Frame modeWeightTrainingFrame(); Frame modeIdleFrame();`
  - `bool weightFrame(int lbs, Frame& out);` returns false outside 5..200
  - `constexpr int WEIGHT_MIN = 5; constexpr int WEIGHT_MAX = 200;`
  - `constexpr const char* SERVICE_UUID`, `WRITE_CHAR_UUID`, `NOTIFY_CHAR_UUID`, `DEVICE_NAME_PREFIX`
  - `Event decode(const uint8_t*, size_t)` is declared here but implemented in Task 3.

- [ ] **Step 1: Write the test harness**

`test/minitest.h`:

```cpp
#pragma once
#include <cstdio>
#include <cstdlib>
static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; auto _a = (a); auto _b = (b); if (!(_a == _b)) { ++g_failures; std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b); } } while (0)
#define MINITEST_MAIN(body) int main() { body; std::printf("%d checks, %d failures\n", g_checks, g_failures); return g_failures ? 1 : 0; }
```

`tools/test.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC=firmware/VoltraKnob/src
CXX="clang++ -std=c++17 -Wall -Wextra -I$SRC -Itest"
$CXX test/test_protocol.cpp $SRC/protocol/voltra_protocol.cpp -o build/host/test_protocol
build/host/test_protocol
if [ -f test/test_app_logic.cpp ]; then
  $CXX test/test_app_logic.cpp $SRC/app/app_logic.cpp -o build/host/test_app_logic
  build/host/test_app_logic
fi
```

`chmod +x tools/test.sh`.

- [ ] **Step 2: Write the failing frame tests**

`test/test_protocol.cpp`:

```cpp
#include "minitest.h"
#include "protocol/voltra_protocol.h"
#include <cstring>
using namespace voltra;

static bool hexEq(const Frame& f, const char* hex) {
  if (std::strlen(hex) != f.len * 2) return false;
  for (size_t i = 0; i < f.len; ++i) {
    unsigned v; std::sscanf(hex + 2 * i, "%2x", &v);
    if (f.data[i] != v) return false;
  }
  return true;
}

static void testFrames() {
  CHECK_EQ(authFrame().len, 41u);
  CHECK(hexEq(authFrame(), "552904c90110000020004f6950686f6e6500000000000000000000000000000084ab1a5f29200172d8"));
  CHECK_EQ(initFrame(0).len, 15u);
  CHECK(hexEq(initFrame(0), "550f0801aad200002000ff00aa0419"));
  CHECK_EQ(initFrame(1).len, 31u);
  CHECK(hexEq(setupFrame(), "55130403aa10150020000f02006a50823e8f2f"));
  CHECK(hexEq(goFrame(),    "55130403aa1016002000110100893e05008173"));
  CHECK(hexEq(stopFrame(),  "55130403aa101b002000110100893e040037dd"));
  CHECK(hexEq(modeWeightTrainingFrame(), "55130403aa1000202000110100b04f0100f353"));
  Frame f;
  CHECK(weightFrame(5, f));   CHECK(hexEq(f, "55130403aa1000202000110100863e0500ffe9"));
  CHECK(weightFrame(20, f));  CHECK(hexEq(f, "55130403aa100f202000110100863e1400fa79"));
  CHECK(weightFrame(200, f)); CHECK_EQ(f.len, 19u);
  CHECK(f.data[15] == 200 && f.data[16] == 0);  // uint16 LE weight payload
  CHECK(!weightFrame(4, f));
  CHECK(!weightFrame(201, f));
  // Every weight frame carries its own value at bytes 15..16 and the correct length byte.
  for (int w = WEIGHT_MIN; w <= WEIGHT_MAX; ++w) {
    CHECK(weightFrame(w, f));
    CHECK_EQ(f.len, 19u);
    CHECK_EQ((int)f.data[1], 0x13);
    CHECK_EQ(f.data[15] | (f.data[16] << 8), w);
  }
}

MINITEST_MAIN({ testFrames(); })
```

- [ ] **Step 3: Run to verify it fails**

Run: `tools/test.sh`
Expected: compile error, `protocol/voltra_protocol.h` not found.

- [ ] **Step 4: Write the generator**

`tools/gen_protocol.py`:

```python
#!/usr/bin/env python3
"""Generate firmware/VoltraKnob/src/protocol/frames.gen.h from third_party/voltra-protocol.json."""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "third_party" / "voltra-protocol.json"
OUT = ROOT / "firmware" / "VoltraKnob" / "src" / "protocol" / "frames.gen.h"

def arr(name, hexstr):
    b = bytes.fromhex(hexstr)
    if b[0] != 0x55 or b[1] != len(b):
        sys.exit(f"{name}: bad frame header/length {hexstr}")
    body = ", ".join(f"0x{x:02x}" for x in b)
    return f"constexpr uint8_t {name}[{len(b)}] = {{{body}}};\n"

d = json.loads(SRC.read_text())
c = d["commands"]; ble = d["ble"]
lines = ["// @generated by tools/gen_protocol.py from third_party/voltra-protocol.json. Do not edit.\n",
         "#pragma once\n#include <cstdint>\n#include <cstddef>\nnamespace voltra { namespace frames {\n",
         f'constexpr const char* SERVICE_UUID = "{ble["serviceUuid"]}";\n',
         f'constexpr const char* WRITE_CHAR_UUID = "{ble["writeCharUuid"]}";\n',
         f'constexpr const char* NOTIFY_CHAR_UUID = "{ble["notifyCharUuid"]}";\n',
         f'constexpr const char* DEVICE_NAME_PREFIX = "{ble["deviceNamePrefix"]}";\n',
         arr("AUTH", c["auth"]["iphone"])]
for i, h in enumerate(c["init"]):
    lines.append(arr(f"INIT{i}", h))
lines.append(f"constexpr size_t INIT_COUNT = {len(c['init'])};\n")
for k in ("prepare", "setup", "go", "stop"):
    lines.append(arr(k.upper(), c["workout"][k]))
lines.append(arr("MODE_IDLE", c["modes"]["idle"]))
lines.append(arr("MODE_WEIGHT_TRAINING", c["modes"]["weightTraining"]))
weights = {int(k): v for k, v in c["weights"].items()}
lo, hi = min(weights), max(weights)
if sorted(weights) != list(range(lo, hi + 1)):
    sys.exit("weights table is not contiguous")
wlen = len(bytes.fromhex(weights[lo]))
lines.append(f"constexpr int WEIGHT_MIN = {lo};\nconstexpr int WEIGHT_MAX = {hi};\nconstexpr size_t WEIGHT_FRAME_LEN = {wlen};\n")
lines.append(f"constexpr uint8_t WEIGHT[{hi - lo + 1}][{wlen}] = {{\n")
for w in range(lo, hi + 1):
    b = bytes.fromhex(weights[w])
    if len(b) != wlen: sys.exit(f"weight {w} length {len(b)}")
    lines.append("  {" + ", ".join(f"0x{x:02x}" for x in b) + f"}}, // {w} lb\n")
lines.append("};\n}} // namespace voltra::frames\n")
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text("".join(lines))
print(f"wrote {OUT} ({len(weights)} weights)")
```

Run: `python3 tools/gen_protocol.py`
Expected: `wrote .../frames.gen.h (196 weights)`.

- [ ] **Step 5: Write the accessor header and implementation**

`firmware/VoltraKnob/src/protocol/voltra_protocol.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "frames.gen.h"

namespace voltra {

struct Frame { const uint8_t* data; size_t len; };

constexpr int WEIGHT_MIN = frames::WEIGHT_MIN;
constexpr int WEIGHT_MAX = frames::WEIGHT_MAX;
constexpr size_t INIT_COUNT = frames::INIT_COUNT;
constexpr const char* SERVICE_UUID = frames::SERVICE_UUID;
constexpr const char* WRITE_CHAR_UUID = frames::WRITE_CHAR_UUID;
constexpr const char* NOTIFY_CHAR_UUID = frames::NOTIFY_CHAR_UUID;
constexpr const char* DEVICE_NAME_PREFIX = frames::DEVICE_NAME_PREFIX;

Frame authFrame();
Frame initFrame(size_t i);
Frame setupFrame();
Frame goFrame();
Frame stopFrame();
Frame modeWeightTrainingFrame();
Frame modeIdleFrame();
bool weightFrame(int lbs, Frame& out);

enum class EventType : uint8_t { Unknown, Settings, ModeConfirmed, Battery, Telemetry, PerRep, SetSummary };

struct Event {
  EventType type = EventType::Unknown;
  int weightLbs = -1;   // Settings
  int mode = -1;        // Settings, ModeConfirmed (0 idle, 1 weight training, ...)
  int battery = -1;     // Battery, 0..100
  int phase = -1;       // Telemetry: 0 idle, 1 concentric, 2 hold, 3 eccentric
  int repCount = -1;    // PerRep, SetSummary
  int setCounter = -1;  // PerRep
};

// Decode one BLE notification. Never throws; unknown or short input yields EventType::Unknown.
Event decode(const uint8_t* d, size_t n);

}  // namespace voltra
```

`firmware/VoltraKnob/src/protocol/voltra_protocol.cpp` (decoder body added in Task 3; for now a stub returning Unknown):

```cpp
#include "voltra_protocol.h"

namespace voltra {

Frame authFrame() { return {frames::AUTH, sizeof(frames::AUTH)}; }
Frame initFrame(size_t i) {
  if (i == 0) return {frames::INIT0, sizeof(frames::INIT0)};
  return {frames::INIT1, sizeof(frames::INIT1)};
}
Frame setupFrame() { return {frames::SETUP, sizeof(frames::SETUP)}; }
Frame goFrame() { return {frames::GO, sizeof(frames::GO)}; }
Frame stopFrame() { return {frames::STOP, sizeof(frames::STOP)}; }
Frame modeWeightTrainingFrame() { return {frames::MODE_WEIGHT_TRAINING, sizeof(frames::MODE_WEIGHT_TRAINING)}; }
Frame modeIdleFrame() { return {frames::MODE_IDLE, sizeof(frames::MODE_IDLE)}; }

bool weightFrame(int lbs, Frame& out) {
  if (lbs < WEIGHT_MIN || lbs > WEIGHT_MAX) return false;
  out = {frames::WEIGHT[lbs - WEIGHT_MIN], frames::WEIGHT_FRAME_LEN};
  return true;
}

Event decode(const uint8_t*, size_t) { return Event{}; }

}  // namespace voltra
```

- [ ] **Step 6: Run the tests**

Run: `tools/test.sh`
Expected: `... checks, 0 failures`.

- [ ] **Step 7: Confirm the firmware still builds with the new sources present**

Run: `tools/build.sh 2>&1 | tail -3`
Expected: success (the `.ino` does not include the protocol yet, but `src/` is compiled by Arduino).

- [ ] **Step 8: Commit**

```bash
git add tools test firmware/VoltraKnob/src/protocol
git commit -m "Add generated Voltra frame tables with host tests

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MT9Hj2yGzpd6Y8SwnjuZAy"
```

---

### Task 3: Notification decoder

**Files:**
- Modify: `firmware/VoltraKnob/src/protocol/voltra_protocol.cpp` (replace the `decode` stub)
- Modify: `test/test_protocol.cpp` (add decoder tests)

**Interfaces:**
- Produces: `voltra::decode(const uint8_t*, size_t) -> Event` with the rules below. Consumed by `voltra_link` in Task 7.

Decoding rules, in this order (mirrors the SDK's `identifyMessageType`):

1. `n >= 30` and bytes 0..3 are `55 3a 04 70` → `Telemetry`, `phase = d[13]`.
2. `d[10] == 0xaa` and `d[11..12] == 82 3b` and `n >= 74` → `PerRep`, `repCount = d[17]`, `setCounter = d[15]` (frame offset = 10 + 1 + payload offset; repCount payload offset 6, setCounter 4).
3. `d[10] == 0xaa` and `d[11..12] == 85 5f` and `n >= 110` → `SetSummary`, `repCount = u16le(d, 26)` (payload offset 15).
4. `d[10] == 0xaa` and `d[11..12] == 86 7d` and `n >= 140` → `SetSummary`, `repCount = u16le(d, 16)` (payload offset 5).
5. `n >= 52` and `d[10] == 0xaa` and `d[11..12] == 80 25` → `Unknown` (state dump; must not read a battery byte).
6. `n >= 16` and `d[10] == 0x10` → `Settings`: `count = d[11]`, walk from offset 13; each param is a 2-byte id; ids `86 3e`, `87 3e`, `88 3e`, `89 3e` carry a uint16 LE value, all others 1 byte; `86 3e` → `weightLbs`, `b0 4f` → `mode`. Stop after 16 params or at the buffer end.
7. Bytes 0..1 `55 12` and `n >= 18` → `ModeConfirmed`, `mode = d[15]`.
8. Bytes 0..1 `55 23` and `n >= 35` → `Battery`, `battery = d[11]`.
9. Bytes 0..1 `55 34` and `n >= 52` → `Battery`, `battery = d[12]`.
10. Otherwise `Unknown`.

- [ ] **Step 1: Write the failing decoder tests**

Append to `test/test_protocol.cpp` before `MINITEST_MAIN`, and add `testDecode();` to the main body:

```cpp
static size_t fromHex(const char* hex, uint8_t* out, size_t cap) {
  size_t n = std::strlen(hex) / 2;
  for (size_t i = 0; i < n && i < cap; ++i) { unsigned v; std::sscanf(hex + 2 * i, "%2x", &v); out[i] = (uint8_t)v; }
  return n;
}

// Build a synthetic frame: header bytes then zero-fill to `len`, then apply (offset,value) patches.
static size_t synth(uint8_t* out, size_t len, const char* headerHex, std::initializer_list<std::pair<int,int>> patches) {
  std::memset(out, 0, len);
  fromHex(headerHex, out, len);
  out[1] = (uint8_t)len;
  for (auto& p : patches) out[p.first] = (uint8_t)p.second;
  return len;
}

static void testDecode() {
  uint8_t b[160];
  // Settings update (cmd 0x10 at byte 10): two params, base weight 863e=0x0032 (50 lb), mode b04f=1.
  size_t n = synth(b, 46, "552e04", {{10,0x10},{11,2},{13,0x86},{14,0x3e},{15,0x32},{16,0x00},{17,0xb0},{18,0x4f},{19,1}});
  Event e = decode(b, n);
  CHECK(e.type == EventType::Settings); CHECK_EQ(e.weightLbs, 50); CHECK_EQ(e.mode, 1);

  // Settings with a 1-byte param first, then weight: verifies width handling.
  n = synth(b, 46, "551604", {{10,0x10},{11,2},{13,0xb0},{14,0x4f},{15,1},{16,0x86},{17,0x3e},{18,0xc8},{19,0x00}});
  e = decode(b, n);
  CHECK(e.type == EventType::Settings); CHECK_EQ(e.weightLbs, 200); CHECK_EQ(e.mode, 1);

  // Mode confirmation: value at 15.
  n = synth(b, 18, "551204", {{15, 4}});
  e = decode(b, n); CHECK(e.type == EventType::ModeConfirmed); CHECK_EQ(e.mode, 4);

  // Device init battery at 11.
  n = synth(b, 35, "552304", {{11, 87}});
  e = decode(b, n); CHECK(e.type == EventType::Battery); CHECK_EQ(e.battery, 87);

  // Status battery at 12.
  n = synth(b, 52, "553404", {{12, 63}});
  e = decode(b, n); CHECK(e.type == EventType::Battery); CHECK_EQ(e.battery, 63);

  // State dump aliases the 5534 header: must NOT yield a battery reading.
  n = synth(b, 52, "553404ac", {{10,0xaa},{11,0x80},{12,0x25},{12,0x25}});
  e = decode(b, n); CHECK(e.type == EventType::Unknown);

  // Telemetry stream, phase eccentric.
  n = synth(b, 60, "553a0470", {{13, 3}});
  e = decode(b, n); CHECK(e.type == EventType::Telemetry); CHECK_EQ(e.phase, 3);
  n = synth(b, 20, "553a0470", {{13, 3}});           // too short
  e = decode(b, n); CHECK(e.type == EventType::Unknown);

  // Vendor per-rep: 74 bytes, aa 82 3b at 10..12, setCounter at 15, repCount at 17.
  n = synth(b, 74, "554a04", {{10,0xaa},{11,0x82},{12,0x3b},{15,2},{17,7}});
  e = decode(b, n); CHECK(e.type == EventType::PerRep); CHECK_EQ(e.repCount, 7); CHECK_EQ(e.setCounter, 2);

  // Vendor set summary: 110 bytes, aa 85 5f, repCount u16 at 26.
  n = synth(b, 110, "556e04", {{10,0xaa},{11,0x85},{12,0x5f},{26,12},{27,0}});
  e = decode(b, n); CHECK(e.type == EventType::SetSummary); CHECK_EQ(e.repCount, 12);

  // Vendor summary: 140 bytes, aa 86 7d, repCount u16 at 16.
  n = synth(b, 140, "558c04", {{10,0xaa},{11,0x86},{12,0x7d},{16,9},{17,0}});
  e = decode(b, n); CHECK(e.type == EventType::SetSummary); CHECK_EQ(e.repCount, 9);

  // Garbage and short input.
  uint8_t junk[3] = {1, 2, 3};
  CHECK(decode(junk, 3).type == EventType::Unknown);
  CHECK(decode(nullptr, 0).type == EventType::Unknown);
}
```

Add `#include <initializer_list>` and `#include <utility>` at the top of the test file.

- [ ] **Step 2: Run to verify it fails**

Run: `tools/test.sh`
Expected: many `FAIL` lines from `testDecode`, nonzero exit.

- [ ] **Step 3: Implement the decoder**

Replace the `decode` stub in `voltra_protocol.cpp`:

```cpp
namespace {
inline int u16le(const uint8_t* d, size_t off) { return d[off] | (d[off + 1] << 8); }
inline bool vendor(const uint8_t* d, size_t n, uint8_t a, uint8_t b) {
  return n >= 13 && d[10] == 0xaa && d[11] == a && d[12] == b;
}
inline bool isU16Param(uint8_t hi, uint8_t lo) {
  return lo == 0x3e && (hi == 0x86 || hi == 0x87 || hi == 0x88 || hi == 0x89);
}
}  // namespace

Event decode(const uint8_t* d, size_t n) {
  Event e;
  if (d == nullptr || n < 4) return e;

  if (n >= 30 && d[0] == 0x55 && d[1] == 0x3a && d[2] == 0x04 && d[3] == 0x70) {
    e.type = EventType::Telemetry; e.phase = d[13]; return e;
  }
  if (vendor(d, n, 0x82, 0x3b) && n >= 74) {
    e.type = EventType::PerRep; e.setCounter = d[15]; e.repCount = d[17]; return e;
  }
  if (vendor(d, n, 0x85, 0x5f) && n >= 110) {
    e.type = EventType::SetSummary; e.repCount = u16le(d, 26); return e;
  }
  if (vendor(d, n, 0x86, 0x7d) && n >= 140) {
    e.type = EventType::SetSummary; e.repCount = u16le(d, 16); return e;
  }
  if (vendor(d, n, 0x80, 0x25) && n >= 52) {
    return e;  // state dump; aliases the 5534 header, never read battery from it
  }
  if (n >= 16 && d[10] == 0x10) {
    e.type = EventType::Settings;
    const int count = d[11];
    size_t off = 13;
    for (int i = 0; i < count && i < 16; ++i) {
      if (off + 2 > n) break;
      const uint8_t hi = d[off], lo = d[off + 1];
      off += 2;
      const size_t width = isU16Param(hi, lo) ? 2 : 1;
      if (off + width > n) break;
      const int value = width == 2 ? u16le(d, off) : d[off];
      off += width;
      if (hi == 0x86 && lo == 0x3e) e.weightLbs = value;
      else if (hi == 0xb0 && lo == 0x4f) e.mode = value;
    }
    return e;
  }
  if (d[0] == 0x55 && d[1] == 0x12 && n >= 18) { e.type = EventType::ModeConfirmed; e.mode = d[15]; return e; }
  if (d[0] == 0x55 && d[1] == 0x23 && n >= 35) { e.type = EventType::Battery; e.battery = d[11]; return e; }
  if (d[0] == 0x55 && d[1] == 0x34 && n >= 52) { e.type = EventType::Battery; e.battery = d[12]; return e; }
  return e;
}
```

Note: the synthetic test frames set byte 1 to the frame length, so the `55 12`, `55 23`, `55 34` header checks in the test use lengths 18, 35, 52 which match the real headers `5512`, `5523`, `5534`.

- [ ] **Step 4: Run the tests**

Run: `tools/test.sh`
Expected: `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add firmware/VoltraKnob/src/protocol/voltra_protocol.cpp test/test_protocol.cpp
git commit -m "Decode Voltra notifications: settings, mode, battery, telemetry, reps

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MT9Hj2yGzpd6Y8SwnjuZAy"
```

---

### Task 4: App logic: weight dial, button classifier, load gate

**Files:**
- Create: `firmware/VoltraKnob/src/app/app_logic.h`, `firmware/VoltraKnob/src/app/app_logic.cpp`, `test/test_app_logic.cpp`

**Interfaces:**
- Produces (namespace `app`):

```cpp
constexpr int WEIGHT_MIN = 5, WEIGHT_MAX = 200, LOADED_MAX_DELTA = 25;
constexpr uint32_t SEND_DEBOUNCE_MS = 250, CONFIRM_TIMEOUT_MS = 2000, BURST_WINDOW_MS = 150;
constexpr int BURST_DETENTS = 4, BURST_STEP = 5;
constexpr uint32_t TAP_MAX_MS = 400, HOLD_MS = 600, CONTACT_BOUNCE_MS = 30;

class WeightDial {
 public:
  void setConfirmed(int lbs, uint32_t now);     // device reported this weight
  void rotate(int direction, uint32_t now);     // +1 or -1, one call per detent
  void setLoaded(bool loaded);
  bool pollSend(uint32_t now, int& lbs);        // true once when a frame should be sent
  bool pollTimeout(uint32_t now);               // true once when a pending send expired
  int target() const; int confirmed() const; bool pending() const; bool capped() const;
};

enum class ButtonAction { None, Tap, HoldComplete, HoldCancelled };
class ButtonClassifier {
 public:
  ButtonAction onLevel(bool pressed, uint32_t now);  // call every poll (about 10 ms)
  float holdProgress(uint32_t now) const;            // 0..1 while pressed, else 0
  bool isPressed() const;
};

enum class LoadCommand { None, Load, Unload };
LoadCommand decideLoad(ButtonAction a, bool connected, bool loaded);
```

- [ ] **Step 1: Write the failing tests**

`test/test_app_logic.cpp`:

```cpp
#include "minitest.h"
#include "app/app_logic.h"
using namespace app;

static void testDialBasics() {
  WeightDial d;
  d.setConfirmed(50, 0);
  CHECK_EQ(d.target(), 50); CHECK_EQ(d.confirmed(), 50); CHECK(!d.pending());
  d.rotate(+1, 10); d.rotate(+1, 20);
  CHECK_EQ(d.target(), 52);
  int lbs = 0;
  CHECK(!d.pollSend(100, lbs));          // 80 ms after last detent: too early
  CHECK(d.pollSend(271, lbs));           // 251 ms after last detent
  CHECK_EQ(lbs, 52); CHECK(d.pending());
  CHECK(!d.pollSend(300, lbs));          // only once
  d.setConfirmed(52, 400);
  CHECK(!d.pending()); CHECK_EQ(d.confirmed(), 52);
}

static void testDialClamp() {
  WeightDial d; d.setConfirmed(6, 0);
  d.rotate(-1, 1); d.rotate(-1, 2); d.rotate(-1, 3);
  CHECK_EQ(d.target(), 5);
  d.setConfirmed(199, 10); d.rotate(+1, 11); d.rotate(+1, 12);
  CHECK_EQ(d.target(), 200);
}

static void testDialBurst() {
  WeightDial d; d.setConfirmed(50, 0);
  for (int i = 0; i < 4; ++i) d.rotate(+1, 10 + i * 20);   // 4 detents in 60 ms -> still 1 lb each
  CHECK_EQ(d.target(), 54);
  d.rotate(+1, 100);                                        // 5th detent inside the burst: 5 lb
  CHECK_EQ(d.target(), 59);
  d.rotate(+1, 400);                                        // 300 ms gap ends the burst
  CHECK_EQ(d.target(), 60);
}

static void testDialTimeoutReverts() {
  WeightDial d; d.setConfirmed(50, 0);
  d.rotate(+1, 10);
  int lbs; CHECK(d.pollSend(300, lbs));
  CHECK(!d.pollTimeout(2000));
  CHECK(d.pollTimeout(2301));
  CHECK_EQ(d.target(), 50); CHECK(!d.pending());
  CHECK(!d.pollTimeout(2400));
}

static void testDialLoadedCap() {
  WeightDial d; d.setConfirmed(50, 0); d.setLoaded(true);
  for (int i = 0; i < 40; ++i) d.rotate(+1, 1000 + i * 200);   // slow detents, no burst
  CHECK_EQ(d.target(), 75); CHECK(d.capped());
  d.rotate(-1, 20000); CHECK_EQ(d.target(), 74); CHECK(!d.capped());
  d.setLoaded(false);
  for (int i = 0; i < 5; ++i) d.rotate(+1, 30000 + i * 200);
  CHECK_EQ(d.target(), 79);
}

static void testDialConfirmedWhileIdleFollowsDevice() {
  WeightDial d; d.setConfirmed(50, 0);
  d.setConfirmed(65, 100);          // e.g. someone changed it in another session
  CHECK_EQ(d.target(), 65);
}

static void testButtonTap() {
  ButtonClassifier b;
  CHECK(b.onLevel(false, 0) == ButtonAction::None);
  CHECK(b.onLevel(true, 100) == ButtonAction::None);
  CHECK(b.onLevel(true, 200) == ButtonAction::None);
  CHECK(b.onLevel(false, 300) == ButtonAction::Tap);
  CHECK(b.onLevel(false, 310) == ButtonAction::None);
}

static void testButtonHold() {
  ButtonClassifier b;
  b.onLevel(true, 1000);
  CHECK(b.onLevel(true, 1500) == ButtonAction::None);
  CHECK(b.holdProgress(1300) > 0.49f && b.holdProgress(1300) < 0.51f);
  CHECK(b.onLevel(true, 1600) == ButtonAction::HoldComplete);
  CHECK(b.onLevel(true, 1700) == ButtonAction::None);      // fires once
  CHECK(b.onLevel(false, 1800) == ButtonAction::None);     // release after hold is not a tap
}

static void testButtonHoldCancelled() {
  ButtonClassifier b;
  b.onLevel(true, 0);
  CHECK(b.onLevel(false, 500) == ButtonAction::HoldCancelled);   // between tap max and hold
}

static void testButtonBounce() {
  ButtonClassifier b;
  b.onLevel(true, 0);
  CHECK(b.onLevel(false, 10) == ButtonAction::None);   // bounce inside 30 ms ignored
  CHECK(b.isPressed());
  CHECK(b.onLevel(true, 20) == ButtonAction::None);
  CHECK(b.onLevel(false, 200) == ButtonAction::Tap);
}

static void testDecideLoad() {
  CHECK(decideLoad(ButtonAction::Tap, true, true) == LoadCommand::Unload);
  CHECK(decideLoad(ButtonAction::Tap, true, false) == LoadCommand::None);
  CHECK(decideLoad(ButtonAction::HoldComplete, true, false) == LoadCommand::Load);
  CHECK(decideLoad(ButtonAction::HoldComplete, true, true) == LoadCommand::None);
  CHECK(decideLoad(ButtonAction::HoldComplete, false, false) == LoadCommand::None);
  CHECK(decideLoad(ButtonAction::Tap, false, true) == LoadCommand::None);
  CHECK(decideLoad(ButtonAction::HoldCancelled, true, false) == LoadCommand::None);
}

MINITEST_MAIN({
  testDialBasics(); testDialClamp(); testDialBurst(); testDialTimeoutReverts();
  testDialLoadedCap(); testDialConfirmedWhileIdleFollowsDevice();
  testButtonTap(); testButtonHold(); testButtonHoldCancelled(); testButtonBounce(); testDecideLoad();
})
```

- [ ] **Step 2: Run to verify it fails**

Run: `tools/test.sh`
Expected: compile error, `app/app_logic.h` not found.

- [ ] **Step 3: Implement**

`firmware/VoltraKnob/src/app/app_logic.h`:

```cpp
#pragma once
#include <cstdint>

namespace app {

constexpr int WEIGHT_MIN = 5;
constexpr int WEIGHT_MAX = 200;
constexpr int LOADED_MAX_DELTA = 25;
constexpr uint32_t SEND_DEBOUNCE_MS = 250;
constexpr uint32_t CONFIRM_TIMEOUT_MS = 2000;
constexpr uint32_t BURST_WINDOW_MS = 150;
constexpr int BURST_DETENTS = 4;
constexpr int BURST_STEP = 5;
constexpr uint32_t TAP_MAX_MS = 400;
constexpr uint32_t HOLD_MS = 600;
constexpr uint32_t CONTACT_BOUNCE_MS = 30;

class WeightDial {
 public:
  void setConfirmed(int lbs, uint32_t now);
  void rotate(int direction, uint32_t now);
  void setLoaded(bool loaded) { loaded_ = loaded; }
  bool pollSend(uint32_t now, int& lbs);
  bool pollTimeout(uint32_t now);
  int target() const { return target_; }
  int confirmed() const { return confirmed_; }
  bool pending() const { return pending_; }
  bool capped() const { return capped_; }

 private:
  int target_ = WEIGHT_MIN;
  int confirmed_ = WEIGHT_MIN;
  bool loaded_ = false;
  bool dirty_ = false;
  bool pending_ = false;
  bool capped_ = false;
  int sent_ = 0;
  uint32_t lastDetentMs_ = 0;
  uint32_t sentMs_ = 0;
  int burstCount_ = 0;
};

enum class ButtonAction { None, Tap, HoldComplete, HoldCancelled };

class ButtonClassifier {
 public:
  ButtonAction onLevel(bool pressed, uint32_t now);
  float holdProgress(uint32_t now) const;
  bool isPressed() const { return pressed_; }

 private:
  bool pressed_ = false;
  bool holdFired_ = false;
  bool haveEdge_ = false;
  uint32_t pressMs_ = 0;
  uint32_t lastEdgeMs_ = 0;
};

enum class LoadCommand { None, Load, Unload };
LoadCommand decideLoad(ButtonAction a, bool connected, bool loaded);

}  // namespace app
```

`firmware/VoltraKnob/src/app/app_logic.cpp`:

```cpp
#include "app_logic.h"

namespace app {

void WeightDial::setConfirmed(int lbs, uint32_t /*now*/) {
  confirmed_ = lbs;
  if (pending_ && lbs == sent_) pending_ = false;
  if (!dirty_ && !pending_) target_ = lbs;
  capped_ = false;
}

void WeightDial::rotate(int direction, uint32_t now) {
  if (now - lastDetentMs_ > BURST_WINDOW_MS) burstCount_ = 0;
  lastDetentMs_ = now;
  ++burstCount_;
  const int step = burstCount_ > BURST_DETENTS ? BURST_STEP : 1;
  int next = target_ + (direction > 0 ? step : -step);
  if (next < WEIGHT_MIN) next = WEIGHT_MIN;
  if (next > WEIGHT_MAX) next = WEIGHT_MAX;
  capped_ = false;
  if (loaded_) {
    const int lo = confirmed_ - LOADED_MAX_DELTA;
    const int hi = confirmed_ + LOADED_MAX_DELTA;
    if (next > hi) { next = hi; capped_ = true; }
    if (next < lo) { next = lo; capped_ = true; }
    if (next < WEIGHT_MIN) next = WEIGHT_MIN;
    if (next > WEIGHT_MAX) next = WEIGHT_MAX;
  }
  if (next != target_) { target_ = next; dirty_ = true; }
}

bool WeightDial::pollSend(uint32_t now, int& lbs) {
  if (!dirty_ || now - lastDetentMs_ < SEND_DEBOUNCE_MS) return false;
  dirty_ = false;
  if (target_ == confirmed_) { pending_ = false; return false; }
  pending_ = true;
  sent_ = target_;
  sentMs_ = now;
  lbs = target_;
  return true;
}

bool WeightDial::pollTimeout(uint32_t now) {
  if (!pending_ || now - sentMs_ <= CONFIRM_TIMEOUT_MS) return false;
  pending_ = false;
  dirty_ = false;
  target_ = confirmed_;
  return true;
}

ButtonAction ButtonClassifier::onLevel(bool pressed, uint32_t now) {
  if (pressed != pressed_) {
    if (haveEdge_ && now - lastEdgeMs_ < CONTACT_BOUNCE_MS) return ButtonAction::None;
    haveEdge_ = true;
    lastEdgeMs_ = now;
    pressed_ = pressed;
    if (pressed) { pressMs_ = now; holdFired_ = false; return ButtonAction::None; }
    if (holdFired_) return ButtonAction::None;
    const uint32_t held = now - pressMs_;
    return held < TAP_MAX_MS ? ButtonAction::Tap : ButtonAction::HoldCancelled;
  }
  if (pressed_ && !holdFired_ && now - pressMs_ >= HOLD_MS) {
    holdFired_ = true;
    return ButtonAction::HoldComplete;
  }
  return ButtonAction::None;
}

float ButtonClassifier::holdProgress(uint32_t now) const {
  if (!pressed_) return 0.f;
  const uint32_t held = now - pressMs_;
  if (held >= HOLD_MS) return 1.f;
  return (float)held / (float)HOLD_MS;
}

LoadCommand decideLoad(ButtonAction a, bool connected, bool loaded) {
  if (!connected) return LoadCommand::None;
  if (a == ButtonAction::Tap && loaded) return LoadCommand::Unload;
  if (a == ButtonAction::HoldComplete && !loaded) return LoadCommand::Load;
  return LoadCommand::None;
}

}  // namespace app
```

- [ ] **Step 4: Run the tests**

Run: `tools/test.sh`
Expected: both binaries report `0 failures`. If `testDialBurst` fails on the fifth detent, check that `burstCount_ > BURST_DETENTS` (strictly greater) is used: the first four detents step 1 lb, the fifth and later step 5 lb.

- [ ] **Step 5: Commit**

```bash
git add firmware/VoltraKnob/src/app test/test_app_logic.cpp
git commit -m "Add host-tested dial, button, and load-gate logic

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MT9Hj2yGzpd6Y8SwnjuZAy"
```

---

### Task 5: Board module: display, touch, encoder, button, backlight

**Files:**
- Create: `firmware/VoltraKnob/src/board/board.h`, `firmware/VoltraKnob/src/board/board.cpp`
- Modify: `firmware/VoltraKnob/VoltraKnob.ino` (move display code into `board`, add encoder/button serial echo)

**Interfaces:**
- Produces (namespace `board`):

```cpp
struct RotateEvent { int8_t direction; uint32_t ms; };   // +1 clockwise, -1 counterclockwise
bool begin();                       // brings up expander, display, LVGL display driver, touch, encoder task; returns true if display is up
bool pollRotate(RotateEvent& out);  // non-blocking; drains the encoder queue one event at a time
bool buttonPressed();               // PCF8574 P5, active low; true while pressed
void setBacklight(uint8_t level);   // 0..255
```

- [ ] **Step 1: Write `board.h`**

```cpp
#pragma once
#include <cstdint>

namespace board {

struct RotateEvent { int8_t direction; uint32_t ms; };

bool begin();
bool pollRotate(RotateEvent& out);
bool buttonPressed();
void setBacklight(uint8_t level);

}  // namespace board
```

- [ ] **Step 2: Write `board.cpp`**

```cpp
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_CST8XX.h>
#include "PCF8574.h"

namespace board {
namespace {

constexpr int I2C_SDA_PIN = 38;
constexpr int I2C_SCL_PIN = 39;
constexpr int ENCODER_A_PIN = 42;
constexpr int ENCODER_B_PIN = 4;
constexpr int SCREEN_BACKLIGHT_PIN = 6;
constexpr uint8_t I2C_TOUCH_ADDR = 0x15;
// Set to 1 if clockwise rotation lowers the weight on the bench.
constexpr int ENCODER_INVERT = 0;

PCF8574 pcf8574(0x21);
Adafruit_CST8XX touch;

Arduino_DataBus *panelInitBus = new Arduino_SWSPI(GFX_NOT_DEFINED, 16, 2, 1, GFX_NOT_DEFINED);
Arduino_ESP32RGBPanel *rgbPanel = new Arduino_ESP32RGBPanel(
  40, 7, 15, 41,
  46, 3, 8, 18, 17,
  14, 13, 12, 11, 10, 9,
  5, 45, 48, 47, 21,
  1, 10, 4, 20,
  1, 10, 4, 20,
  0, 12000000, false, 0, 0,
  480 * 20);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480, rgbPanel, 0, true, panelInitBus, GFX_NOT_DEFINED,
  st7701_type5_init_operations, sizeof(st7701_type5_init_operations));

uint8_t *buf1 = nullptr;
uint8_t *buf2 = nullptr;
QueueHandle_t rotateQueue = nullptr;

void dispFlush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  uint16_t *pixels = (uint16_t *)px_map;
  for (uint32_t i = 0; i < w * h; ++i) {
    const uint16_t c = pixels[i];
    pixels[i] = (c & 0x07E0) | ((c & 0x001F) << 11) | ((c & 0xF800) >> 11);
  }
  gfx->draw16bitRGBBitmap(area->x1, area->y1, pixels, w, h);
  lv_display_flush_ready(display);
}

void touchRead(lv_indev_t *, lv_indev_data_t *data) {
  if (touch.touched()) {
    CST_TS_Point p = touch.getPoint(0);
    data->point.x = p.x;
    data->point.y = p.y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void encoderTask(void *) {
  int previousA = digitalRead(ENCODER_A_PIN);
  while (true) {
    const int a = digitalRead(ENCODER_A_PIN);
    if (a != previousA && a == HIGH) {
      RotateEvent ev;
      const bool ccw = digitalRead(ENCODER_B_PIN) != a;
      ev.direction = (ccw ^ (ENCODER_INVERT != 0)) ? -1 : +1;
      ev.ms = millis();
      xQueueSend(rotateQueue, &ev, 0);
    }
    previousA = a;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

}  // namespace

bool begin() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  pcf8574.pinMode(P0, OUTPUT);
  pcf8574.pinMode(P2, OUTPUT);
  pcf8574.pinMode(P3, OUTPUT);
  pcf8574.pinMode(P4, OUTPUT);
  pcf8574.pinMode(P5, INPUT_PULLUP);
  const bool expanderOk = pcf8574.begin();
  Serial.println(expanderOk ? "[board] pcf8574 ok" : "[board] pcf8574 FAIL");

  pcf8574.digitalWrite(P3, HIGH); delay(100);
  pcf8574.digitalWrite(P4, HIGH); delay(100);
  pcf8574.digitalWrite(P4, LOW);  delay(120);
  pcf8574.digitalWrite(P4, HIGH); delay(120);
  pcf8574.digitalWrite(P0, HIGH); delay(100);
  pcf8574.digitalWrite(P0, LOW);  delay(120);
  pcf8574.digitalWrite(P0, HIGH); delay(120);
  pcf8574.digitalWrite(P2, HIGH); delay(120);

  gfx->begin();
  panelInitBus->beginWrite();
  panelInitBus->writeCommand(0x36);
  panelInitBus->write(0x08);
  panelInitBus->endWrite();
  gfx->fillScreen(0x0000);

  Serial.println(touch.begin(&Wire, I2C_TOUCH_ADDR) ? "[board] touch ok" : "[board] touch missing");

  pinMode(ENCODER_A_PIN, INPUT);
  pinMode(ENCODER_B_PIN, INPUT);

  lv_init();
  lv_tick_set_cb(millis);
  const size_t bufSize = sizeof(uint16_t) * 480 * 480;
  buf1 = (uint8_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
  buf2 = (uint8_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
  if (!buf1 || !buf2) { Serial.println("[board] FATAL LVGL buffers"); return false; }
  lv_display_t *display = lv_display_create(480, 480);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, dispFlush);
  lv_display_set_buffers(display, buf1, buf2, bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchRead);
  lv_indev_set_display(indev, display);

  rotateQueue = xQueueCreate(32, sizeof(RotateEvent));
  xTaskCreatePinnedToCore(encoderTask, "ENC", 2048, nullptr, 1, nullptr, 0);

  delay(200);
  ledcAttach(SCREEN_BACKLIGHT_PIN, 5000, 8);
  setBacklight(204);
  pcf8574.digitalWrite(P3, LOW);
  Serial.println("[board] display ready");
  return true;
}

bool pollRotate(RotateEvent& out) {
  return rotateQueue && xQueueReceive(rotateQueue, &out, 0) == pdTRUE;
}

bool buttonPressed() {
  return pcf8574.digitalRead(P5, true) == LOW;
}

void setBacklight(uint8_t level) {
  ledcWrite(SCREEN_BACKLIGHT_PIN, level);
}

}  // namespace board
```

- [ ] **Step 3: Rewrite the sketch to use the board module and echo inputs**

`firmware/VoltraKnob/VoltraKnob.ino`:

```cpp
#include <Arduino.h>
#include <lvgl.h>
#include "src/board/board.h"
#include "src/app/app_logic.h"

static app::WeightDial dial;
static app::ButtonClassifier button;
static lv_obj_t *weightLabel = nullptr;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[BOOT] VoltraKnob task5");
  board::begin();
  dial.setConfirmed(50, millis());

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  weightLabel = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_color(weightLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(weightLabel, &lv_font_montserrat_48, 0);
  lv_label_set_text_fmt(weightLabel, "%d", dial.target());
  lv_obj_center(weightLabel);
}

void loop() {
  const uint32_t now = millis();
  board::RotateEvent ev;
  while (board::pollRotate(ev)) {
    dial.rotate(ev.direction, ev.ms);
    Serial.printf("[in] rotate %+d -> target %d\n", ev.direction, dial.target());
  }
  static uint32_t lastButtonPoll = 0;
  if (now - lastButtonPoll >= 10) {
    lastButtonPoll = now;
    const app::ButtonAction a = button.onLevel(board::buttonPressed(), now);
    if (a == app::ButtonAction::Tap) Serial.println("[in] tap");
    if (a == app::ButtonAction::HoldComplete) Serial.println("[in] hold complete");
    if (a == app::ButtonAction::HoldCancelled) Serial.println("[in] hold cancelled");
  }
  int lbs;
  if (dial.pollSend(now, lbs)) { Serial.printf("[in] would send weight %d\n", lbs); dial.setConfirmed(lbs, now); }
  lv_label_set_text_fmt(weightLabel, "%d", dial.target());
  lv_obj_center(weightLabel);
  lv_timer_handler();
  delay(5);
}
```

- [ ] **Step 4: Build, flash, verify by turning and pressing**

Run: `tools/build.sh 2>&1 | tail -3 && tools/flash.sh 2>&1 | tail -2`
Then: `uv run --with pyserial tools/serial_tail.py --seconds 20` and ask the user to turn the knob clockwise a few clicks, then tap, then hold for one second.
Expected serial: `[board] display ready`, `[in] rotate +1 -> target 51` lines increasing on clockwise turns, `[in] would send weight N` about 250 ms after the last click, `[in] tap`, `[in] hold complete`. If clockwise decreases the number, set `ENCODER_INVERT` to 1 and reflash.

- [ ] **Step 5: Commit**

```bash
git add firmware/VoltraKnob
git commit -m "Add board module for display, touch, encoder, and button

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MT9Hj2yGzpd6Y8SwnjuZAy"
```

---

### Task 6: UI module

**Files:**
- Create: `firmware/VoltraKnob/src/ui/ui.h`, `firmware/VoltraKnob/src/ui/ui.cpp`
- Modify: `firmware/VoltraKnob/VoltraKnob.ino` (render a ViewModel from the dial and button, still offline)

**Interfaces:**
- Produces (namespace `ui`):

```cpp
struct ViewModel {
  int weight;            // target weight to display
  bool pending;          // waiting for device confirmation
  bool capped;           // hit the 25 lb loaded limit
  bool loaded;
  bool connected;
  float holdProgress;    // 0..1
  int battery;           // -1 unknown
  int reps;
  const char* stateText; // "SEARCHING", "CONNECTING", "UNLOADED", "LOADED", "APP CONNECTED?"
};
void begin();                    // builds the screen once; call after board::begin()
void render(const ViewModel&);   // idempotent; cheap when nothing changed
```

Colors: background black; weight arc grey `0x555555` unloaded, green `0x2ECC71` loaded, amber `0xF39C12` pending or capped; hold arc red `0xE74C3C`; text white; status dot grey `0x777777` or blue `0x3498DB`.

- [ ] **Step 1: Write `ui.h`**

```cpp
#pragma once

namespace ui {

struct ViewModel {
  int weight = 5;
  bool pending = false;
  bool capped = false;
  bool loaded = false;
  bool connected = false;
  float holdProgress = 0.f;
  int battery = -1;
  int reps = 0;
  const char* stateText = "SEARCHING";
};

void begin();
void render(const ViewModel& vm);

}  // namespace ui
```

- [ ] **Step 2: Write `ui.cpp`**

```cpp
#include "ui.h"
#include <lvgl.h>
#include <cstring>

namespace ui {
namespace {

lv_obj_t *weightArc = nullptr;
lv_obj_t *holdArc = nullptr;
lv_obj_t *weightLabel = nullptr;
lv_obj_t *unitLabel = nullptr;
lv_obj_t *stateLabel = nullptr;
lv_obj_t *repsLabel = nullptr;
lv_obj_t *batteryLabel = nullptr;
lv_obj_t *linkDot = nullptr;
ViewModel last;
bool first = true;

lv_obj_t *makeArc(int size, int width, lv_color_t color, int startAngle, int endAngle) {
  lv_obj_t *arc = lv_arc_create(lv_screen_active());
  lv_obj_set_size(arc, size, size);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 0);
  lv_arc_set_bg_angles(arc, startAngle, endAngle);
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x222222), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  return arc;
}

lv_obj_t *makeLabel(const lv_font_t *font, lv_align_t align, int dx, int dy) {
  lv_obj_t *l = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_align(l, align, dx, dy);
  lv_label_set_text(l, "");
  return l;
}

}  // namespace

void begin() {
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

  weightArc = makeArc(470, 18, lv_color_hex(0x555555), 135, 45);   // 270 degree sweep, gap at the bottom
  lv_arc_set_range(weightArc, 5, 200);
  holdArc = makeArc(420, 10, lv_color_hex(0xE74C3C), 135, 45);
  lv_arc_set_range(holdArc, 0, 100);
  lv_arc_set_value(holdArc, 0);

  weightLabel = makeLabel(&lv_font_montserrat_48, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_transform_scale(weightLabel, 512, 0);   // 2x: 48 px font renders about 96 px tall
  lv_obj_set_style_transform_pivot_x(weightLabel, LV_PCT(50), 0);
  lv_obj_set_style_transform_pivot_y(weightLabel, LV_PCT(50), 0);
  unitLabel = makeLabel(&lv_font_montserrat_28, LV_ALIGN_CENTER, 0, 60);
  lv_label_set_text(unitLabel, "lb");
  lv_obj_set_style_text_color(unitLabel, lv_color_hex(0xAAAAAA), 0);
  stateLabel = makeLabel(&lv_font_montserrat_24, LV_ALIGN_BOTTOM_MID, 0, -70);
  repsLabel = makeLabel(&lv_font_montserrat_24, LV_ALIGN_BOTTOM_MID, 0, -40);
  batteryLabel = makeLabel(&lv_font_montserrat_20, LV_ALIGN_TOP_MID, 20, 60);
  linkDot = lv_obj_create(lv_screen_active());
  lv_obj_set_size(linkDot, 16, 16);
  lv_obj_set_style_radius(linkDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(linkDot, 0, 0);
  lv_obj_set_style_bg_color(linkDot, lv_color_hex(0x777777), 0);
  lv_obj_align(linkDot, LV_ALIGN_TOP_MID, -30, 62);
}

void render(const ViewModel& vm) {
  if (!first && vm.weight == last.weight && vm.pending == last.pending && vm.capped == last.capped &&
      vm.loaded == last.loaded && vm.connected == last.connected && vm.battery == last.battery &&
      vm.reps == last.reps && vm.holdProgress == last.holdProgress &&
      std::strcmp(vm.stateText, last.stateText) == 0) {
    return;
  }
  first = false;
  last = vm;

  lv_arc_set_value(weightArc, vm.weight);
  lv_color_t arcColor = lv_color_hex(0x555555);
  if (vm.loaded) arcColor = lv_color_hex(0x2ECC71);
  if (vm.pending || vm.capped) arcColor = lv_color_hex(0xF39C12);
  lv_obj_set_style_arc_color(weightArc, arcColor, LV_PART_INDICATOR);

  lv_arc_set_value(holdArc, (int)(vm.holdProgress * 100.f));

  lv_label_set_text_fmt(weightLabel, "%d", vm.weight);
  lv_obj_set_style_text_color(weightLabel, vm.connected ? lv_color_white() : lv_color_hex(0x666666), 0);
  lv_obj_align(weightLabel, LV_ALIGN_CENTER, 0, -20);

  lv_label_set_text(stateLabel, vm.stateText);
  lv_obj_align(stateLabel, LV_ALIGN_BOTTOM_MID, 0, -70);
  if (vm.loaded) lv_label_set_text_fmt(repsLabel, "%d reps", vm.reps);
  else lv_label_set_text(repsLabel, "");
  lv_obj_align(repsLabel, LV_ALIGN_BOTTOM_MID, 0, -40);

  if (vm.battery >= 0) lv_label_set_text_fmt(batteryLabel, "%d%%", vm.battery);
  else lv_label_set_text(batteryLabel, "");
  lv_obj_align(batteryLabel, LV_ALIGN_TOP_MID, 20, 60);
  lv_obj_set_style_bg_color(linkDot, vm.connected ? lv_color_hex(0x3498DB) : lv_color_hex(0x777777), 0);
}

}  // namespace ui
```

- [ ] **Step 3: Drive the UI from the dial and button in the sketch (still no BLE)**

Replace `VoltraKnob.ino`:

```cpp
#include <Arduino.h>
#include <lvgl.h>
#include "src/board/board.h"
#include "src/app/app_logic.h"
#include "src/ui/ui.h"

static app::WeightDial dial;
static app::ButtonClassifier button;
static bool fakeLoaded = false;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[BOOT] VoltraKnob task6");
  board::begin();
  ui::begin();
  dial.setConfirmed(50, millis());
}

void loop() {
  const uint32_t now = millis();
  board::RotateEvent ev;
  while (board::pollRotate(ev)) dial.rotate(ev.direction, ev.ms);

  static uint32_t lastButtonPoll = 0;
  if (now - lastButtonPoll >= 10) {
    lastButtonPoll = now;
    const app::ButtonAction a = button.onLevel(board::buttonPressed(), now);
    const app::LoadCommand cmd = app::decideLoad(a, true, fakeLoaded);
    if (cmd == app::LoadCommand::Load) { fakeLoaded = true; dial.setLoaded(true); Serial.println("[ui] fake load"); }
    if (cmd == app::LoadCommand::Unload) { fakeLoaded = false; dial.setLoaded(false); Serial.println("[ui] fake unload"); }
  }
  int lbs;
  if (dial.pollSend(now, lbs)) dial.setConfirmed(lbs, now);   // offline: confirm immediately

  ui::ViewModel vm;
  vm.weight = dial.target();
  vm.pending = dial.pending();
  vm.capped = dial.capped();
  vm.loaded = fakeLoaded;
  vm.connected = true;
  vm.holdProgress = button.holdProgress(now);
  vm.battery = 88;
  vm.reps = 0;
  vm.stateText = fakeLoaded ? "LOADED" : "UNLOADED";
  ui::render(vm);
  lv_timer_handler();
  delay(5);
}
```

- [ ] **Step 4: Build, flash, verify visually**

Run: `tools/build.sh 2>&1 | tail -3 && tools/flash.sh 2>&1 | tail -2 && uv run --with pyserial tools/serial_tail.py --seconds 15`
Ask the user to confirm: large number centered with "lb" under it, grey outer arc that follows the dial, "UNLOADED" at the bottom, "88%" and a grey dot at the top. Holding the button fills the red inner arc, then the outer arc turns green and the text says "LOADED" with "0 reps". A tap returns to grey. If the weight numeral renders too small or blurry with `transform_scale`, remove the three `transform_*` lines and accept 48 px for now; note it in the WORKLOG.

- [ ] **Step 5: Commit**

```bash
git add firmware/VoltraKnob
git commit -m "Add LVGL weight screen with arc, hold progress, and status

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MT9Hj2yGzpd6Y8SwnjuZAy"
```

---

### Task 7: Voltra BLE link and full integration

**Files:**
- Create: `firmware/VoltraKnob/src/link/voltra_link.h`, `firmware/VoltraKnob/src/link/voltra_link.cpp`
- Modify: `firmware/VoltraKnob/VoltraKnob.ino` (replace fake state with the link)

**Interfaces:**
- Consumes: `voltra::authFrame/initFrame/setupFrame/goFrame/stopFrame/modeWeightTrainingFrame/weightFrame/decode` (Tasks 2, 3).
- Produces (namespace `link`):

```cpp
enum class LinkState { Scanning, Connecting, Handshaking, Ready, AppConnectedSuspect };
struct DeviceState {
  LinkState link = LinkState::Scanning;
  int weightLbs = -1;    // last device-confirmed weight, -1 unknown
  int mode = -1;
  int battery = -1;
  bool loaded = false;   // true between a successful GO and STOP/disconnect
  int reps = 0;          // from per-rep frames since last load
  uint32_t confirmSeq = 0; // increments on every device weight confirmation
};
class VoltraLink {
 public:
  void begin();
  DeviceState state();
  bool requestWeight(int lbs);
  bool requestLoad();
  bool requestUnload();
  void clearPinnedAddress();
};
extern VoltraLink voltraLink;
```

Timing: MTU 527 requested before connect; auth then 3000 ms wait; init frames 20 ms apart; then 500 ms; mode frame; handshake deadline 5000 ms for a Settings or ModeConfirmed event; writes otherwise spaced 500 ms; load is `setup`, 300 ms, `go`; reconnect backoff 1, 2, 4, 8 s.

- [ ] **Step 1: Write `voltra_link.h`**

```cpp
#pragma once
#include <cstdint>

namespace link {

enum class LinkState : uint8_t { Scanning, Connecting, Handshaking, Ready, AppConnectedSuspect };

struct DeviceState {
  LinkState link = LinkState::Scanning;
  int weightLbs = -1;
  int mode = -1;
  int battery = -1;
  bool loaded = false;
  int reps = 0;
  uint32_t confirmSeq = 0;
};

class VoltraLink {
 public:
  void begin();
  DeviceState state();
  bool requestWeight(int lbs);
  bool requestLoad();
  bool requestUnload();
  void clearPinnedAddress();
};

extern VoltraLink voltraLink;

}  // namespace link
```

- [ ] **Step 2: Write `voltra_link.cpp`**

```cpp
#include "voltra_link.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <cstring>
#include "../protocol/voltra_protocol.h"

namespace link {

VoltraLink voltraLink;

namespace {

enum class CmdType : uint8_t { Weight, Load, Unload };
struct Cmd { CmdType type; int lbs; };

constexpr uint32_t AUTH_WAIT_MS = 3000;
constexpr uint32_t INIT_GAP_MS = 20;
constexpr uint32_t WRITE_GAP_MS = 500;
constexpr uint32_t HANDSHAKE_DEADLINE_MS = 5000;
constexpr uint32_t LOAD_SETUP_GAP_MS = 300;
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint16_t MTU_REQUEST = 527;
constexpr int APP_SUSPECT_AFTER_FAILURES = 3;

DeviceState g_state;
SemaphoreHandle_t g_mutex = nullptr;
QueueHandle_t g_cmdQueue = nullptr;
Preferences g_prefs;

NimBLEClient *g_client = nullptr;
NimBLERemoteCharacteristic *g_write = nullptr;
volatile bool g_disconnected = false;
volatile bool g_foundTarget = false;
NimBLEAddress g_targetAddr;
volatile uint32_t g_lastNotifyMs = 0;
volatile bool g_handshakeEvent = false;
int g_consecutiveFailures = 0;
uint32_t g_lastWriteMs = 0;
String g_pinned;   // pinned device address read from NVS at the start of each scan

void setState(void (*fn)(DeviceState&)) {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  fn(g_state);
  xSemaphoreGive(g_mutex);
}

void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  g_lastNotifyMs = millis();
  const voltra::Event e = voltra::decode(data, len);
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  switch (e.type) {
    case voltra::EventType::Settings:
      if (e.weightLbs >= 0) { g_state.weightLbs = e.weightLbs; ++g_state.confirmSeq; }
      if (e.mode >= 0) g_state.mode = e.mode;
      g_handshakeEvent = true;
      break;
    case voltra::EventType::ModeConfirmed:
      g_state.mode = e.mode;
      g_handshakeEvent = true;
      break;
    case voltra::EventType::Battery:
      g_state.battery = e.battery;
      break;
    case voltra::EventType::PerRep:
      if (e.repCount >= 0) g_state.reps = e.repCount;
      break;
    default:
      break;
  }
  xSemaphoreGive(g_mutex);
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    const bool byService = dev->isAdvertisingService(NimBLEUUID(voltra::SERVICE_UUID));
    const bool byName = dev->getName().rfind(voltra::DEVICE_NAME_PREFIX, 0) == 0;
    if (!byService && !byName) return;
    if (g_pinned.length() && g_pinned != String(dev->getAddress().toString().c_str())) return;
    g_targetAddr = dev->getAddress();
    g_foundTarget = true;
    Serial.printf("[link] found %s %s\n", dev->getName().c_str(), dev->getAddress().toString().c_str());
    NimBLEDevice::getScan()->stop();
  }
};

class ClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int reason) override {
    Serial.printf("[link] disconnected reason=%d\n", reason);
    g_disconnected = true;
  }
};

ScanCallbacks g_scanCb;
ClientCallbacks g_clientCb;

bool writeFrame(voltra::Frame f) {
  if (!g_write || !g_client || !g_client->isConnected()) return false;
  const uint32_t since = millis() - g_lastWriteMs;
  if (since < WRITE_GAP_MS) vTaskDelay(pdMS_TO_TICKS(WRITE_GAP_MS - since));
  bool ok = g_write->writeValue(f.data, f.len, g_write->canWrite());
  if (!ok) {
    vTaskDelay(pdMS_TO_TICKS(200));
    ok = g_write->writeValue(f.data, f.len, g_write->canWrite());
  }
  g_lastWriteMs = millis();
  return ok;
}

bool writeRaw(voltra::Frame f) {   // no spacing; used inside the init pair
  if (!g_write) return false;
  const bool ok = g_write->writeValue(f.data, f.len, g_write->canWrite());
  g_lastWriteMs = millis();
  return ok;
}

bool connectAndHandshake() {
  setState([](DeviceState &s) { s.link = LinkState::Connecting; s.loaded = false; s.reps = 0; });
  g_disconnected = false;
  g_handshakeEvent = false;
  if (!g_client) {
    g_client = NimBLEDevice::createClient();
    g_client->setClientCallbacks(&g_clientCb, false);
    g_client->setConnectTimeout(CONNECT_TIMEOUT_MS);
  }
  if (!g_client->connect(g_targetAddr)) { Serial.println("[link] connect failed"); return false; }
  NimBLERemoteService *svc = g_client->getService(voltra::SERVICE_UUID);
  if (!svc) { Serial.println("[link] service missing"); g_client->disconnect(); return false; }
  g_write = svc->getCharacteristic(voltra::WRITE_CHAR_UUID);
  NimBLERemoteCharacteristic *notify = svc->getCharacteristic(voltra::NOTIFY_CHAR_UUID);
  if (!g_write || !notify) { Serial.println("[link] characteristics missing"); g_client->disconnect(); return false; }
  if (!notify->subscribe(true, onNotify)) { Serial.println("[link] subscribe failed"); g_client->disconnect(); return false; }

  setState([](DeviceState &s) { s.link = LinkState::Handshaking; });
  if (!writeRaw(voltra::authFrame())) { Serial.println("[link] auth write failed"); g_client->disconnect(); return false; }
  vTaskDelay(pdMS_TO_TICKS(AUTH_WAIT_MS));
  for (size_t i = 0; i < voltra::INIT_COUNT; ++i) {
    if (!writeRaw(voltra::initFrame(i))) { Serial.println("[link] init write failed"); g_client->disconnect(); return false; }
    vTaskDelay(pdMS_TO_TICKS(INIT_GAP_MS));
  }
  if (!writeFrame(voltra::modeWeightTrainingFrame())) { Serial.println("[link] mode write failed"); g_client->disconnect(); return false; }

  const uint32_t deadline = millis() + HANDSHAKE_DEADLINE_MS;
  while (millis() < deadline && !g_handshakeEvent && !g_disconnected) vTaskDelay(pdMS_TO_TICKS(50));
  if (!g_handshakeEvent) { Serial.println("[link] handshake timeout"); g_client->disconnect(); return false; }

  g_prefs.begin("voltra", false);
  g_prefs.putString("addr", g_targetAddr.toString().c_str());
  g_prefs.end();
  setState([](DeviceState &s) { s.link = LinkState::Ready; });
  Serial.println("[link] ready");
  return true;
}

void servicePump() {
  Cmd cmd;
  while (!g_disconnected && g_client && g_client->isConnected()) {
    if (xQueueReceive(g_cmdQueue, &cmd, pdMS_TO_TICKS(50)) != pdTRUE) continue;
    switch (cmd.type) {
      case CmdType::Weight: {
        voltra::Frame f;
        if (voltra::weightFrame(cmd.lbs, f)) {
          const bool ok = writeFrame(f);
          Serial.printf("[link] set weight %d -> %s\n", cmd.lbs, ok ? "ok" : "FAIL");
          if (!ok) g_disconnected = true;
        }
        break;
      }
      case CmdType::Load: {
        bool ok = writeFrame(voltra::setupFrame());
        if (ok) { vTaskDelay(pdMS_TO_TICKS(LOAD_SETUP_GAP_MS)); ok = writeRaw(voltra::goFrame()); }
        Serial.printf("[link] load -> %s\n", ok ? "ok" : "FAIL");
        if (ok) setState([](DeviceState &s) { s.loaded = true; s.reps = 0; });
        else g_disconnected = true;
        break;
      }
      case CmdType::Unload: {
        const bool ok = writeFrame(voltra::stopFrame());
        Serial.printf("[link] unload -> %s\n", ok ? "ok" : "FAIL");
        setState([](DeviceState &s) { s.loaded = false; });
        if (!ok) g_disconnected = true;
        break;
      }
    }
  }
}

void linkTask(void *) {
  uint32_t backoffMs = 1000;
  while (true) {
    setState([](DeviceState &s) {
      s.link = g_consecutiveFailures >= APP_SUSPECT_AFTER_FAILURES ? LinkState::AppConnectedSuspect : LinkState::Scanning;
      s.loaded = false; s.reps = 0;
    });
    g_foundTarget = false;
    g_prefs.begin("voltra", true);
    g_pinned = g_prefs.getString("addr", "");
    g_prefs.end();
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_scanCb, false);
    scan->setActiveScan(true);
    scan->start(0, false, true);
    while (!g_foundTarget) vTaskDelay(pdMS_TO_TICKS(100));
    scan->stop();

    if (connectAndHandshake()) {
      g_consecutiveFailures = 0;
      backoffMs = 1000;
      servicePump();
      if (g_client && g_client->isConnected()) g_client->disconnect();
    } else {
      ++g_consecutiveFailures;
    }
    setState([](DeviceState &s) { s.loaded = false; s.link = LinkState::Scanning; });
    xQueueReset(g_cmdQueue);
    Serial.printf("[link] retry in %u ms\n", (unsigned)backoffMs);
    vTaskDelay(pdMS_TO_TICKS(backoffMs));
    backoffMs = backoffMs >= 8000 ? 8000 : backoffMs * 2;
  }
}

}  // namespace

void VoltraLink::begin() {
  g_mutex = xSemaphoreCreateMutex();
  g_cmdQueue = xQueueCreate(8, sizeof(Cmd));
  NimBLEDevice::init("VoltraKnob");
  NimBLEDevice::setMTU(MTU_REQUEST);
  xTaskCreatePinnedToCore(linkTask, "LINK", 8192, nullptr, 2, nullptr, 0);
}

DeviceState VoltraLink::state() {
  DeviceState copy;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  copy = g_state;
  xSemaphoreGive(g_mutex);
  return copy;
}

bool VoltraLink::requestWeight(int lbs) {
  Cmd c{CmdType::Weight, lbs};
  return xQueueSend(g_cmdQueue, &c, 0) == pdTRUE;
}

bool VoltraLink::requestLoad() {
  Cmd c{CmdType::Load, 0};
  return xQueueSend(g_cmdQueue, &c, 0) == pdTRUE;
}

bool VoltraLink::requestUnload() {
  Cmd c{CmdType::Unload, 0};
  return xQueueSendToFront(g_cmdQueue, &c, 0) == pdTRUE;   // unload jumps the queue
}

void VoltraLink::clearPinnedAddress() {
  g_prefs.begin("voltra", false);
  g_prefs.remove("addr");
  g_prefs.end();
}

}  // namespace link
```

- [ ] **Step 3: Wire the sketch to the link**

Replace `VoltraKnob.ino`:

```cpp
#include <Arduino.h>
#include <lvgl.h>
#include "src/board/board.h"
#include "src/app/app_logic.h"
#include "src/ui/ui.h"
#include "src/link/voltra_link.h"

static app::WeightDial dial;
static app::ButtonClassifier button;
static uint32_t lastConfirmSeq = 0;
static bool wasReady = false;

static const char *stateText(const link::DeviceState &s) {
  switch (s.link) {
    case link::LinkState::Scanning: return "SEARCHING";
    case link::LinkState::Connecting: return "CONNECTING";
    case link::LinkState::Handshaking: return "CONNECTING";
    case link::LinkState::AppConnectedSuspect: return "APP CONNECTED?";
    case link::LinkState::Ready: return s.loaded ? "LOADED" : "UNLOADED";
  }
  return "";
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[BOOT] VoltraKnob");
  board::begin();
  ui::begin();
  // Holding the button during the first two seconds clears the pinned device address.
  const uint32_t t0 = millis();
  bool held = true;
  while (millis() - t0 < 2000) { if (!board::buttonPressed()) { held = false; break; } delay(20); }
  link::voltraLink.begin();
  if (held) { link::voltraLink.clearPinnedAddress(); Serial.println("[BOOT] pinned address cleared"); }
}

void loop() {
  const uint32_t now = millis();
  const link::DeviceState dev = link::voltraLink.state();
  const bool ready = dev.link == link::LinkState::Ready;

  if (ready && !wasReady) { lastConfirmSeq = 0; }   // fresh session: accept the first confirmation
  wasReady = ready;
  if (dev.confirmSeq != lastConfirmSeq && dev.weightLbs >= 0) {
    lastConfirmSeq = dev.confirmSeq;
    dial.setConfirmed(dev.weightLbs, now);
  }
  dial.setLoaded(dev.loaded);

  board::RotateEvent ev;
  while (board::pollRotate(ev)) { if (ready) dial.rotate(ev.direction, ev.ms); }

  static uint32_t lastButtonPoll = 0;
  if (now - lastButtonPoll >= 10) {
    lastButtonPoll = now;
    const app::ButtonAction a = button.onLevel(board::buttonPressed(), now);
    switch (app::decideLoad(a, ready, dev.loaded)) {
      case app::LoadCommand::Load: Serial.println("[app] load requested"); link::voltraLink.requestLoad(); break;
      case app::LoadCommand::Unload: Serial.println("[app] unload requested"); link::voltraLink.requestUnload(); break;
      default: break;
    }
  }

  int lbs;
  if (ready && dial.pollSend(now, lbs)) link::voltraLink.requestWeight(lbs);
  if (dial.pollTimeout(now)) Serial.println("[app] weight not confirmed, reverted");

  ui::ViewModel vm;
  vm.weight = dev.weightLbs >= 0 ? dial.target() : app::WEIGHT_MIN;
  vm.pending = dial.pending();
  vm.capped = dial.capped();
  vm.loaded = dev.loaded;
  vm.connected = ready;
  vm.holdProgress = ready && !dev.loaded ? button.holdProgress(now) : 0.f;
  vm.battery = dev.battery;
  vm.reps = dev.reps;
  vm.stateText = stateText(dev);
  ui::render(vm);
  lv_timer_handler();
  delay(5);
}
```

- [ ] **Step 4: Build and flash; verify scan and handshake with the Beyond+ app closed**

Run: `tools/build.sh 2>&1 | tail -3 && tools/flash.sh 2>&1 | tail -2 && uv run --with pyserial tools/serial_tail.py --seconds 30`
Expected serial within 30 s, with the Voltra powered on and the Beyond+ app fully closed on the phone: `[link] found VTR-... `, `[link] ready`. The screen shows the device's current weight, the blue dot, and "UNLOADED". If `handshake timeout` repeats three times, the state text becomes "APP CONNECTED?"; confirm the phone app is closed and the Voltra is not connected elsewhere.

If NimBLE reports `writeValue` failures with response, change `g_write->canWrite()` to `false` in both write helpers (write without response) and retest.

- [ ] **Step 5: Verify weight set from the dial**

With the link ready, turn the dial three clicks. Expected serial: `[link] set weight N -> ok`, then the arc changes from amber back to grey within a second (device confirmation arrived). Check `voltra status --json` on the Mac is NOT running at the same time, since the Mac's daemon would compete for the connection.

- [ ] **Step 6: Bench test load and unload at a light weight (user present)**

Set 10 lb with the dial, make sure the cable is fully retracted, then hold the button. Expected: red inner arc fills, serial prints `[app] load requested` then `[link] load -> ok`, the outer arc turns green and "LOADED" shows. Pull the cable for two reps; "2 reps" appears. Tap: `[link] unload -> ok`, arc grey, "UNLOADED". Then power-cycle the Voltra: state goes to "SEARCHING" and returns to "UNLOADED" after it boots.

If the device refuses to load (no motor engagement but `ok` writes), read the next Settings/ModeConfirmed events on serial; add a temporary `Serial.printf` hex dump in `onNotify` to capture the frames for diagnosis. Do not add retries or `--force`-style overrides.

- [ ] **Step 7: Commit**

```bash
git add firmware/VoltraKnob
git commit -m "Connect knob to Voltra over BLE with dial weight and hold-to-load

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MT9Hj2yGzpd6Y8SwnjuZAy"
```

---

### Task 8: Documentation and wrap-up

**Files:**
- Modify: `README.md` (usage, safety notes, bench results)
- Create: `WORKLOG.md` (gitignored; session log)

- [ ] **Step 1: Update README with a Usage section**

Append to `README.md`:

```markdown
## Usage

1. Power the Voltra, close the Beyond+ app, power the knob.
2. Wait for the blue dot and "UNLOADED".
3. Turn to set weight. Spin fast for 5 lb steps.
4. Hold the knob for 0.6 s to load (inner ring fills). Tap to unload.
5. While loaded, weight changes are limited to 25 lb from the confirmed value.

Hold the knob while powering on to forget the pinned Voltra and pair with whichever one is found first.

## Safety

Nothing is ever loaded automatically. Unload is a single tap. The Voltra disengages itself if the Bluetooth link drops. Keep the Beyond+ app disconnected while using the knob; the device accepts one controller at a time.
```

- [ ] **Step 2: Write the WORKLOG entry**

`WORKLOG.md`:

```markdown
## 2026-09-10
- Built Voltra Knob v1: Elecrow CrowPanel 2.1" ESP32-S3 dial controlling the Voltra over direct BLE using frames from @voltras/node-sdk 0.14.0.
- Decision: no Homebridge, no cloud, no Mac in the loop. Cortex hosted MCP is data-only; device control is BLE-only.
- Decision: hold 600 ms to load, tap to unload, 25 lb cap while loaded.
- Bench results: (fill in from Task 7 steps 4 to 6)
- Open: rep count relies on vendor per-rep frames; verify counts match the app. Large-numeral font choice. Encoder direction constant.
```

- [ ] **Step 3: Run everything once more and commit**

Run: `tools/test.sh && tools/build.sh 2>&1 | tail -2`
Expected: `0 failures` twice and a successful build.

```bash
git add README.md
git commit -m "Document usage and safety for Voltra Knob

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MT9Hj2yGzpd6Y8SwnjuZAy"
git status --short   # WORKLOG.md must not appear (global gitignore)
```

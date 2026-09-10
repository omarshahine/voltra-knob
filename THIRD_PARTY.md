# Third-party components

Vendored under `firmware/libraries/` so the firmware builds reproducibly. Each keeps its own license; see the upstream projects.

| Component | Version | Upstream | License |
| --- | --- | --- | --- |
| LVGL | 9.1.0 | https://github.com/lvgl/lvgl | MIT (`firmware/libraries/lvgl/LICENCE.txt`) |
| GFX Library for Arduino (Arduino_GFX) | 1.6.7 | https://github.com/moononournation/Arduino_GFX | See upstream |
| Adafruit BusIO | 1.16.2 | https://github.com/adafruit/Adafruit_BusIO | MIT (`firmware/libraries/Adafruit_BusIO/LICENSE`) |
| Adafruit CST8XX Library | 1.1.1 | https://github.com/adafruit/Adafruit_CST8XX | MIT |
| PCF8574 library | 2.3.7 | https://github.com/xreef/PCF8574_library | MIT (`firmware/libraries/PCF8574_library-master/LICENSE`) |
| NimBLE-Arduino | 2.5.1 | https://github.com/h2zero/NimBLE-Arduino | Apache-2.0 (installed via arduino-cli, not vendored) |

Protocol data under `third_party/` is decoded from `@voltras/node-sdk` 0.14.0 by Henry Jewkes, MIT, license included as `third_party/voltra-node-sdk-LICENSE`.

The 150 px numeral font (`firmware/VoltraKnob/src/ui/font_weight_150.c`) was generated from Montserrat Medium (SIL Open Font License 1.1) with `lv_font_conv`.

Board pin map and display initialization follow Elecrow's factory example for the CrowPanel 2.1" HMI ESP32 Rotary Display.

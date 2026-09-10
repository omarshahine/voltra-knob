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

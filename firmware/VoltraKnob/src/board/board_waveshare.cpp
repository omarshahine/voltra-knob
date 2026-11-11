// Waveshare ESP32-S3-Knob-Touch-LCD-1.8 board layer.
// Pin map, ST77916 bring-up, knob decoding and DRV2605 setup follow kirby6365/voltra_remote (MIT),
// which in turn follows Waveshare's demo code. See THIRD_PARTY.md.
//
// The board has no push button, so the touchscreen acts as one: buttonPressed() is true while a
// finger is on the glass. Tap and hold are classified by the same app::ButtonClassifier as the
// CrowPanel's button.
#include "board_config.h"
#if VOLTRA_BOARD_WAVESHARE_KNOB18
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "lcd_qspi_panel.h"

namespace board {
namespace {

// ST77916 360x360 over QSPI
constexpr int PIN_LCD_CS = 14;
constexpr int PIN_LCD_SCK = 13;
constexpr int PIN_LCD_D0 = 15;
constexpr int PIN_LCD_D1 = 16;
constexpr int PIN_LCD_D2 = 17;
constexpr int PIN_LCD_D3 = 18;
constexpr int PIN_LCD_RST = 21;
constexpr int PIN_LCD_BL = 47;
constexpr int LCD_RES = BOARD_SCREEN_PX;
// Shared I2C: CST816 touch (0x15) and DRV2605 haptics (0x5A)
constexpr int PIN_I2C_SDA = 11;
constexpr int PIN_I2C_SCL = 12;
constexpr int PIN_TOUCH_RST = 10;
constexpr int PIN_TOUCH_INT = 9;
constexpr uint8_t ADDR_CST816 = 0x15;
constexpr uint8_t ADDR_DRV2605 = 0x5A;
// Rotary knob on the ESP32-S3
constexpr int ENCODER_A_PIN = 8;
constexpr int ENCODER_B_PIN = 7;
// Set to 1 if clockwise rotation lowers the weight on the bench.
constexpr int ENCODER_INVERT = 0;

constexpr int BUF_LINES = 40;   // 2 x 360 x 40 x 2 B = 57.6 KB of internal DMA RAM
constexpr spi_host_device_t LCD_HOST = SPI2_HOST;

esp_lcd_panel_handle_t panel = nullptr;
esp_lcd_panel_io_handle_t panelIo = nullptr;
lv_display_t *display = nullptr;
QueueHandle_t rotateQueue = nullptr;
esp_timer_handle_t encoderTimer = nullptr;
bool hapticsOk = false;
bool touchOk = false;

volatile uint32_t g_encLoops = 0, g_encEdges = 0, g_edgesA = 0, g_edgesB = 0;
volatile uint32_t g_encLevelA = 0, g_encLevelB = 0;
volatile uint32_t g_flushes = 0, g_flushPixels = 0, g_flushMaxUs = 0;

// ---- I2C helpers (only the main loop touches the bus) ----
bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)len) != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

// ---- Display ----
bool onColorTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx) {
  lv_display_flush_ready(static_cast<lv_display_t *>(ctx));
  return false;
}

void dispFlush(lv_display_t *, const lv_area_t *area, uint8_t *px_map) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  const uint32_t tStart = micros();
  ++g_flushes; g_flushPixels += w * h;
  lv_draw_sw_rgb565_swap(px_map, w * h);   // QSPI panel wants big-endian RGB565
  esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
  const uint32_t dt = micros() - tStart;
  if (dt > g_flushMaxUs) g_flushMaxUs = dt;
  // lv_display_flush_ready() comes from onColorTransDone once the DMA finishes.
}

// The ST77916 wants even-aligned windows.
void roundArea(lv_event_t *e) {
  lv_area_t *a = static_cast<lv_area_t *>(lv_event_get_param(e));
  a->x1 &= ~1;
  a->y1 &= ~1;
  a->x2 |= 1;
  a->y2 |= 1;
}

bool displayBegin() {
  spi_bus_config_t bus = {};
  bus.data0_io_num = PIN_LCD_D0;
  bus.data1_io_num = PIN_LCD_D1;
  bus.sclk_io_num = PIN_LCD_SCK;
  bus.data2_io_num = PIN_LCD_D2;
  bus.data3_io_num = PIN_LCD_D3;
  bus.max_transfer_sz = LCD_RES * BUF_LINES * 2;
  if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  esp_lcd_panel_io_spi_config_t io = {};
  io.cs_gpio_num = PIN_LCD_CS;
  io.dc_gpio_num = -1;
  io.spi_mode = 0;
  io.pclk_hz = 40 * 1000 * 1000;
  io.trans_queue_depth = 10;
  io.lcd_cmd_bits = 32;
  io.lcd_param_bits = 8;
  io.flags.quad_mode = 1;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &panelIo) != ESP_OK) return false;
  if (lcd_qspi_panel_create(panelIo, PIN_LCD_RST, &panel) != ESP_OK) return false;
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  return true;
}

// ---- Touch (CST816) ----
bool touchBegin() {
  pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(80);
  uint8_t id = 0;
  const bool ok = i2cRead(ADDR_CST816, 0xA7, &id, 1);
  i2cWrite(ADDR_CST816, 0xFE, 0x01);   // disable auto-sleep so the first tap after idle registers
  Serial.printf("[board] touch %s (id 0x%02x)\n", ok ? "ok" : "missing", id);
  return ok;
}

// ---- Haptics (DRV2605, ERM open loop, library 1) ----
bool hapticsBegin() {
  uint8_t status = 0;
  if (!i2cRead(ADDR_DRV2605, 0x00, &status, 1)) { Serial.println("[board] haptics missing"); return false; }
  i2cWrite(ADDR_DRV2605, 0x01, 0x00);   // mode: internal trigger
  i2cWrite(ADDR_DRV2605, 0x02, 0x00);
  i2cWrite(ADDR_DRV2605, 0x04, 1);
  i2cWrite(ADDR_DRV2605, 0x05, 0);
  i2cWrite(ADDR_DRV2605, 0x0D, 0);
  i2cWrite(ADDR_DRV2605, 0x0E, 0);
  i2cWrite(ADDR_DRV2605, 0x0F, 0);
  i2cWrite(ADDR_DRV2605, 0x10, 0);
  i2cWrite(ADDR_DRV2605, 0x13, 0x64);
  uint8_t v = 0;
  if (i2cRead(ADDR_DRV2605, 0x1A, &v, 1)) i2cWrite(ADDR_DRV2605, 0x1A, v & 0x7F);   // ERM
  if (i2cRead(ADDR_DRV2605, 0x1D, &v, 1)) i2cWrite(ADDR_DRV2605, 0x1D, v | 0x20);   // open loop
  i2cWrite(ADDR_DRV2605, 0x03, 1);
  Serial.println("[board] haptics ok");
  return true;
}

// ---- Knob ----
// Port of Waveshare's bidi_switch_knob.c: each channel fires once per rising edge that follows a
// stable low; a pulse on A is one step clockwise, a pulse on B one step counterclockwise.
struct Channel { uint8_t prev = 1; uint8_t debounce = 0; };
Channel chA, chB;
constexpr uint8_t DEBOUNCE_TICKS = 2;

bool risingEdge(Channel &ch, uint8_t level) {
  bool fired = false;
  if (level == 0) {
    if (level != ch.prev) ch.debounce = 0; else ch.debounce++;
  } else {
    if (level != ch.prev && ++ch.debounce >= DEBOUNCE_TICKS) { ch.debounce = 0; fired = true; }
    else ch.debounce = 0;
  }
  ch.prev = level;
  return fired;
}

void emitStep(bool cw) {
  RotateEvent ev;
  ev.direction = (cw ^ (ENCODER_INVERT != 0)) ? +1 : -1;
  ev.ms = millis();
  xQueueSend(rotateQueue, &ev, 0);
}

void encoderPoll(void *) {
  const uint8_t a = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
  const uint8_t b = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
  ++g_encLoops; g_encLevelA = a; g_encLevelB = b;
  if (risingEdge(chA, a)) { ++g_edgesA; ++g_encEdges; emitStep(true); }
  if (risingEdge(chB, b)) { ++g_edgesB; ++g_encEdges; emitStep(false); }
}

}  // namespace

bool begin() {
  ledcAttach(PIN_LCD_BL, 50000, 8);
  ledcWrite(PIN_LCD_BL, 0);   // dark until the first frame is drawn

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(20);
  touchOk = touchBegin();
  hapticsOk = hapticsBegin();

  if (!displayBegin()) { Serial.println("[board] FATAL panel init"); return false; }

  lv_init();
  lv_tick_set_cb(millis);
  const size_t bufSize = LCD_RES * BUF_LINES * 2;
  void *buf1 = heap_caps_malloc(bufSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *buf2 = heap_caps_malloc(bufSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!buf1 || !buf2) { Serial.println("[board] FATAL LVGL buffers"); return false; }
  display = lv_display_create(LCD_RES, LCD_RES);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, dispFlush);
  lv_display_set_buffers(display, buf1, buf2, bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_add_event_cb(display, roundArea, LV_EVENT_INVALIDATE_AREA, nullptr);
  esp_lcd_panel_io_callbacks_t cbs = {};
  cbs.on_color_trans_done = onColorTransDone;
  esp_lcd_panel_io_register_event_callbacks(panelIo, &cbs, display);
  // No LVGL pointer device: the UI has no touch targets, and the main loop reads the touch
  // controller directly through buttonPressed().

  gpio_config_t cfg = {};
  cfg.pin_bit_mask = (1ULL << ENCODER_A_PIN) | (1ULL << ENCODER_B_PIN);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  chA.prev = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
  chB.prev = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
  rotateQueue = xQueueCreate(32, sizeof(RotateEvent));
  esp_timer_create_args_t targs = {};
  targs.callback = encoderPoll;
  targs.dispatch_method = ESP_TIMER_TASK;
  targs.name = "knob";
  esp_timer_create(&targs, &encoderTimer);
  esp_timer_start_periodic(encoderTimer, 3000);   // 3 ms, as in the Waveshare demo

  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_refr_now(display);
  setBacklight(204);
  Serial.println("[board] display ready");
  return true;
}

bool pollRotate(RotateEvent& out) {
  return rotateQueue && xQueueReceive(rotateQueue, &out, 0) == pdTRUE;
}

bool buttonPressed() {
  if (!touchOk) return false;
  uint8_t fingers = 0;
  return i2cRead(ADDR_CST816, 0x02, &fingers, 1) && fingers > 0;
}

void setBacklight(uint8_t level) {
  ledcWrite(PIN_LCD_BL, level);
}

void haptic(Haptic kind) {
  if (!hapticsOk) return;
  uint8_t effect = 26;                         // sharp tick, 60%
  if (kind == Haptic::Click) effect = 1;       // strong click
  if (kind == Haptic::Double) effect = 10;     // double click
  i2cWrite(ADDR_DRV2605, 0x04, effect);
  i2cWrite(ADDR_DRV2605, 0x05, 0);
  i2cWrite(ADDR_DRV2605, 0x0C, 1);             // go
}

void dumpEncoderPins() {
  Serial.printf("[gpio] uptime=%lu ms\n", (unsigned long)millis());
  Serial.flush();
  gpio_dump_io_configuration(stdout, (1ULL << ENCODER_A_PIN) | (1ULL << ENCODER_B_PIN));
  fflush(stdout);
}

DebugStats debugStats() {
  DebugStats d;
  d.arduinoA = digitalRead(ENCODER_A_PIN); d.arduinoB = digitalRead(ENCODER_B_PIN);
  d.rawA = gpio_get_level((gpio_num_t)ENCODER_A_PIN); d.rawB = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
  d.encLoops = g_encLoops; d.encEdges = g_encEdges; d.levelA = g_encLevelA; d.levelB = g_encLevelB;
  d.encStackFree = 0;
  d.queueWaiting = rotateQueue ? uxQueueMessagesWaiting(rotateQueue) : 0;
  d.edgesA = g_edgesA; d.edgesB = g_edgesB;
  d.flushes = g_flushes; d.flushPixels = g_flushPixels; d.flushMaxUs = g_flushMaxUs;
  g_flushes = 0; g_flushPixels = 0; g_flushMaxUs = 0;
  return d;
}

}  // namespace board
#endif  // VOLTRA_BOARD_WAVESHARE_KNOB18

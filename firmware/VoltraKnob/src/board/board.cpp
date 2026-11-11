#include "board_config.h"
#if !VOLTRA_BOARD_WAVESHARE_KNOB18
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_CST8XX.h>
#include "PCF8574.h"
#include "driver/gpio.h"

namespace board {
namespace {

constexpr int I2C_SDA_PIN = 38;
constexpr int I2C_SCL_PIN = 39;
constexpr int ENCODER_A_PIN = 42;
constexpr int ENCODER_B_PIN = 4;
constexpr int SCREEN_BACKLIGHT_PIN = 6;
constexpr uint8_t I2C_TOUCH_ADDR = 0x15;
// Set to 1 if clockwise rotation lowers the weight on the bench.
constexpr int ENCODER_INVERT = 1;

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
TaskHandle_t encoderTaskHandle = nullptr;
volatile uint32_t g_encLoops = 0, g_encEdges = 0, g_encLevelA = 0, g_encLevelB = 0;
volatile uint32_t g_flushes = 0, g_flushPixels = 0, g_flushMaxUs = 0;

void dispFlush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  const uint32_t tStart = micros();
  ++g_flushes; g_flushPixels += w * h;
  uint16_t *pixels = (uint16_t *)px_map;
  for (uint32_t i = 0; i < w * h; ++i) {
    const uint16_t c = pixels[i];
    pixels[i] = (c & 0x07E0) | ((c & 0x001F) << 11) | ((c & 0xF800) >> 11);
  }
  gfx->draw16bitRGBBitmap(area->x1, area->y1, pixels, w, h);
  const uint32_t dt = micros() - tStart;
  if (dt > g_flushMaxUs) g_flushMaxUs = dt;
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

// Full quadrature decoding: every A or B transition contributes +1/-1 from a Gray-code table,
// which is immune to contact bounce (a bounce adds +1 then -1). One detent on this encoder is
// two transitions; ENCODER_COUNTS_PER_DETENT converts accumulated counts to steps.
constexpr int ENCODER_COUNTS_PER_DETENT = 2;
volatile uint32_t g_edgesA = 0, g_edgesB = 0;

void encoderTask(void *) {
  static const int8_t table[16] = {0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0};
  uint8_t prev = (gpio_get_level((gpio_num_t)ENCODER_A_PIN) << 1) | gpio_get_level((gpio_num_t)ENCODER_B_PIN);
  int accum = 0;
  while (true) {
    const int a = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
    const int bLevel = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
    ++g_encLoops; g_encLevelA = a; g_encLevelB = bLevel;
    const uint8_t cur = (a << 1) | bLevel;
    if (cur != prev) {
      if ((cur >> 1) != (prev >> 1)) ++g_edgesA;
      if ((cur & 1) != (prev & 1)) ++g_edgesB;
      ++g_encEdges;
      accum += table[(prev << 2) | cur];
      prev = cur;
      if (accum >= ENCODER_COUNTS_PER_DETENT || accum <= -ENCODER_COUNTS_PER_DETENT) {
        RotateEvent ev;
        const bool cw = accum > 0;
        accum = 0;
        ev.direction = (cw ^ (ENCODER_INVERT != 0)) ? +1 : -1;
        ev.ms = millis();
        xQueueSend(rotateQueue, &ev, 0);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

}  // namespace

bool begin() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setTimeOut(50);
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

  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);

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
  xTaskCreatePinnedToCore(encoderTask, "ENC", 4096, nullptr, 3, &encoderTaskHandle, 1);   // loop core, above loop priority

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

ButtonRead readButton() {
  return pcf8574.digitalRead(P5, true) == LOW ? ButtonRead::Pressed : ButtonRead::Released;
}

bool buttonPressed() { return readButton() == ButtonRead::Pressed; }

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
  d.encStackFree = encoderTaskHandle ? uxTaskGetStackHighWaterMark(encoderTaskHandle) : 0;
  d.queueWaiting = rotateQueue ? uxQueueMessagesWaiting(rotateQueue) : 0;
  d.edgesA = g_edgesA; d.edgesB = g_edgesB;
  d.flushes = g_flushes; d.flushPixels = g_flushPixels; d.flushMaxUs = g_flushMaxUs;
  g_flushes = 0; g_flushPixels = 0; g_flushMaxUs = 0;
  return d;
}

void setBacklight(uint8_t level) {
  ledcWrite(SCREEN_BACKLIGHT_PIN, level);
}

void haptic(Haptic) {}   // no haptic motor on this board

}  // namespace board

#endif  // !VOLTRA_BOARD_WAVESHARE_KNOB18

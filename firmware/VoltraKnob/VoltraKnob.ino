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

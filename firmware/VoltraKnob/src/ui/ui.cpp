#include "ui.h"
#include <lvgl.h>
#include <cstring>
#include <cstdint>
#include "../board/board_config.h"

extern "C" const lv_font_t font_weight_150;
extern "C" const lv_font_t font_weight_112;

namespace ui {
namespace {

// The layout was designed at 480x480; S() scales a length to this board's screen.
constexpr int S(int px) { return (px * BOARD_SCREEN_PX + 240) / 480; }
#if BOARD_SCREEN_PX >= 480
const lv_font_t &FONT_WEIGHT = font_weight_150;
const lv_font_t &FONT_UNIT = lv_font_montserrat_28;
const lv_font_t &FONT_STATE = lv_font_montserrat_20;
const lv_font_t &FONT_REPS = lv_font_montserrat_22;
const lv_font_t &FONT_BLE = lv_font_montserrat_24;
const lv_font_t &FONT_BATTERY = lv_font_montserrat_20;
#else
const lv_font_t &FONT_WEIGHT = font_weight_112;
const lv_font_t &FONT_UNIT = lv_font_montserrat_22;
const lv_font_t &FONT_STATE = lv_font_montserrat_16;
const lv_font_t &FONT_REPS = lv_font_montserrat_18;
const lv_font_t &FONT_BLE = lv_font_montserrat_18;
const lv_font_t &FONT_BATTERY = lv_font_montserrat_16;
#endif

// Palette
constexpr uint32_t BG_TOP = 0x0B1020;
constexpr uint32_t BG_BOTTOM = 0x000000;
constexpr uint32_t TRACK = 0x1C2230;
constexpr uint32_t TICK = 0x2E3748;
constexpr uint32_t TICK_MAJOR = 0x55607A;
constexpr uint32_t GREY = 0x6B7280;
constexpr uint32_t GREEN = 0x2ECC71;
constexpr uint32_t AMBER = 0xF5A524;
constexpr uint32_t RED = 0xE74C3C;
constexpr uint32_t BLUE = 0x3B82F6;
constexpr uint32_t TEXT_DIM = 0x8A94A6;

lv_obj_t *scale = nullptr;
lv_obj_t *glowArc = nullptr;
lv_obj_t *weightArc = nullptr;
lv_obj_t *holdArc = nullptr;
lv_obj_t *weightLabel = nullptr;
lv_obj_t *unitLabel = nullptr;
lv_obj_t *statePill = nullptr;
lv_obj_t *stateLabel = nullptr;
lv_obj_t *repsLabel = nullptr;
lv_obj_t *batteryLabel = nullptr;
lv_obj_t *bleLabel = nullptr;
ViewModel last;
bool first = true;
int animatedWeight = 5;

lv_obj_t *makeArc(int size, int width, uint32_t color) {
  lv_obj_t *arc = lv_arc_create(lv_screen_active());
  lv_obj_set_size(arc, size, size);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 0);
  lv_arc_set_bg_angles(arc, 135, 45);   // 270 degree sweep, gap at the bottom
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(TRACK), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  return arc;
}

lv_obj_t *makeLabel(const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_label_set_text(l, "");
  return l;
}

void arcAnimCb(void *obj, int32_t v) {
  lv_arc_set_value((lv_obj_t *)obj, v);
  if (obj == weightArc) lv_arc_set_value(glowArc, v);
}

void animateArc(lv_obj_t *arc, int from, int to, uint32_t ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, arc);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_exec_cb(&a, arcAnimCb);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

const char *batterySymbol(int pct) {
  if (pct < 0) return "";
  if (pct > 85) return LV_SYMBOL_BATTERY_FULL;
  if (pct > 60) return LV_SYMBOL_BATTERY_3;
  if (pct > 35) return LV_SYMBOL_BATTERY_2;
  if (pct > 10) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

}  // namespace

void begin() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(BG_TOP), 0);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(BG_BOTTOM), 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

  // Tick ring: 40 minor ticks over the 270 degree sweep, a major tick every 5 (every 25 lb).
  scale = lv_scale_create(scr);
  lv_obj_set_size(scale, S(440), S(440));
  lv_obj_center(scale);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_range(scale, 0, 40);
  lv_scale_set_total_tick_count(scale, 41);
  lv_scale_set_major_tick_every(scale, 5);
  lv_scale_set_angle_range(scale, 270);
  lv_scale_set_rotation(scale, 135);
  lv_scale_set_label_show(scale, false);
  lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scale, 0, 0);
  lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);
  lv_obj_set_style_line_color(scale, lv_color_hex(TICK), LV_PART_ITEMS);
  lv_obj_set_style_line_width(scale, 2, LV_PART_ITEMS);
  lv_obj_set_style_length(scale, S(8), LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_hex(TICK_MAJOR), LV_PART_INDICATOR);
  lv_obj_set_style_line_width(scale, 3, LV_PART_INDICATOR);
  lv_obj_set_style_length(scale, S(14), LV_PART_INDICATOR);
  lv_obj_remove_flag(scale, LV_OBJ_FLAG_CLICKABLE);

  // Soft glow behind the weight arc, then the arc itself.
  glowArc = makeArc(S(476), S(30), GREY);
  lv_obj_set_style_arc_opa(glowArc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(glowArc, LV_OPA_30, LV_PART_INDICATOR);
  lv_arc_set_range(glowArc, 5, 200);
  weightArc = makeArc(S(476), S(14), GREY);
  lv_arc_set_range(weightArc, 5, 200);
  lv_arc_set_value(weightArc, 5);
  lv_arc_set_value(glowArc, 5);

  // Hold-to-load ring inside the tick ring.
  holdArc = makeArc(S(380), S(8), RED);
  lv_obj_set_style_arc_opa(holdArc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_arc_set_range(holdArc, 0, 100);
  lv_arc_set_value(holdArc, 0);

  weightLabel = makeLabel(&FONT_WEIGHT, 0xFFFFFF);
  lv_obj_align(weightLabel, LV_ALIGN_CENTER, 0, -S(26));
  unitLabel = makeLabel(&FONT_UNIT, TEXT_DIM);
  lv_label_set_text(unitLabel, "lb");
  lv_obj_align(unitLabel, LV_ALIGN_CENTER, 0, S(64));

  statePill = lv_obj_create(scr);
  lv_obj_set_size(statePill, S(190), S(40));
  lv_obj_align(statePill, LV_ALIGN_BOTTOM_MID, 0, -S(92));
  lv_obj_set_style_radius(statePill, S(20), 0);
  lv_obj_set_style_border_width(statePill, 0, 0);
  lv_obj_set_style_bg_color(statePill, lv_color_hex(TRACK), 0);
  lv_obj_set_style_pad_all(statePill, 0, 0);
  lv_obj_remove_flag(statePill, LV_OBJ_FLAG_SCROLLABLE);
  stateLabel = lv_label_create(statePill);
  lv_obj_set_style_text_font(stateLabel, &FONT_STATE, 0);
  lv_obj_set_style_text_color(stateLabel, lv_color_white(), 0);
  lv_obj_set_style_text_letter_space(stateLabel, 2, 0);
  lv_label_set_text(stateLabel, "");
  lv_obj_center(stateLabel);

  repsLabel = makeLabel(&FONT_REPS, TEXT_DIM);
  lv_obj_align(repsLabel, LV_ALIGN_BOTTOM_MID, 0, -S(58));

  bleLabel = makeLabel(&FONT_BLE, GREY);
  lv_label_set_text(bleLabel, LV_SYMBOL_BLUETOOTH);
  lv_obj_align(bleLabel, LV_ALIGN_TOP_MID, -S(34), S(72));
  batteryLabel = makeLabel(&FONT_BATTERY, TEXT_DIM);
  lv_obj_align(batteryLabel, LV_ALIGN_TOP_MID, S(22), S(76));
}

namespace {
uint32_t lastArcColor = 0xFFFFFFFF;
lv_opa_t lastGlowOpa = 0;
int lastHold = -1;
bool lastConnectedStyle = false;
uint32_t lastPillColor = 0xFFFFFFFF;
constexpr uint32_t RING_THROTTLE_MS = 150;   // the ring redraws most of the screen; the numeral is cheap
int ringTarget = -1;
int ringShown = -1;
uint32_t ringLastMs = 0;
}  // namespace

void tick(uint32_t nowMs) {
  if (ringTarget == ringShown) return;
  if (nowMs - ringLastMs < RING_THROTTLE_MS) return;
  ringLastMs = nowMs;
  ringShown = ringTarget;
  lv_arc_set_value(weightArc, ringShown);
  lv_arc_set_value(glowArc, ringShown);
}

void render(const ViewModel& vm) {
  if (!first && vm.weight == last.weight && vm.pending == last.pending && vm.capped == last.capped &&
      vm.loaded == last.loaded && vm.armed == last.armed && vm.countdownMs == last.countdownMs &&
      vm.connected == last.connected && vm.battery == last.battery &&
      vm.reps == last.reps && vm.holdProgress == last.holdProgress &&
      std::strcmp(vm.stateText, last.stateText) == 0) {
    return;
  }
  const bool isFirst = first;
  const ViewModel prev = last;
  first = false;
  last = vm;

  // Weight arc: only the changed sector is invalidated by LVGL, so keep the animation short.
  if (isFirst || vm.weight != prev.weight) {
    ringTarget = vm.weight;
    if (isFirst) { ringShown = vm.weight; lv_arc_set_value(weightArc, vm.weight); lv_arc_set_value(glowArc, vm.weight); }
    animatedWeight = vm.weight;
    lv_label_set_text_fmt(weightLabel, "%d", vm.weight);
    lv_obj_align(weightLabel, LV_ALIGN_CENTER, 0, -S(26));
  }

  // Style writes invalidate the whole object, so apply them only on actual change.
  uint32_t arcColor = GREY;
  if (vm.loaded) arcColor = GREEN;
  if (vm.armed) arcColor = AMBER;
  if (vm.pending || vm.capped) arcColor = AMBER;
  if (arcColor != lastArcColor) {
    lastArcColor = arcColor;
    lv_obj_set_style_arc_color(weightArc, lv_color_hex(arcColor), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(glowArc, lv_color_hex(arcColor), LV_PART_INDICATOR);
  }
  const lv_opa_t glowOpa = vm.loaded ? LV_OPA_50 : LV_OPA_20;
  if (glowOpa != lastGlowOpa) {
    lastGlowOpa = glowOpa;
    lv_obj_set_style_arc_opa(glowArc, glowOpa, LV_PART_INDICATOR);
  }

  const int hold = (int)(vm.holdProgress * 100.f);
  if (hold != lastHold) { lastHold = hold; lv_arc_set_value(holdArc, hold); }

  if (isFirst || vm.connected != lastConnectedStyle) {
    lastConnectedStyle = vm.connected;
    lv_obj_set_style_text_color(weightLabel, vm.connected ? lv_color_white() : lv_color_hex(GREY), 0);
    lv_obj_set_style_text_color(bleLabel, lv_color_hex(vm.connected ? BLUE : GREY), 0);
  }

  uint32_t pillColor = TRACK;
  if (vm.connected && vm.loaded) pillColor = GREEN;
  else if (vm.connected && vm.armed) pillColor = AMBER;
  else if (vm.connected) pillColor = 0x2A3446;
  if (pillColor != lastPillColor) {
    lastPillColor = pillColor;
    lv_obj_set_style_bg_color(statePill, lv_color_hex(pillColor), 0);
    lv_obj_set_style_text_color(stateLabel, (vm.loaded || vm.armed) ? lv_color_hex(0x06261A) : lv_color_white(), 0);
  }
  if (isFirst || std::strcmp(vm.stateText, prev.stateText) != 0) {
    lv_label_set_text(stateLabel, vm.stateText);
    lv_obj_center(stateLabel);
  }

  if (isFirst || vm.loaded != prev.loaded || vm.reps != prev.reps) {
    if (vm.loaded) lv_label_set_text_fmt(repsLabel, "%d reps", vm.reps);
    else lv_label_set_text(repsLabel, "");
    lv_obj_align(repsLabel, LV_ALIGN_BOTTOM_MID, 0, -S(58));
  }

  if (isFirst || vm.battery != prev.battery) {
    if (vm.battery >= 0) lv_label_set_text_fmt(batteryLabel, "%s %d%%", batterySymbol(vm.battery), vm.battery);
    else lv_label_set_text(batteryLabel, "");
    lv_obj_align(batteryLabel, LV_ALIGN_TOP_MID, S(22), S(76));
  }
}

}  // namespace ui

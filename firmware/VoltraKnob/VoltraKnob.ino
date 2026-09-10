#include <Arduino.h>
#include <lvgl.h>
#include "src/board/board.h"
#include "src/app/app_logic.h"
#include "src/ui/ui.h"

static app::WeightDial dial;
static app::ButtonClassifier button;
static bool fakeLoaded = false;
static int lastWeightSeen = -1;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[BOOT] VoltraKnob task6");
  board::begin();
  ui::begin();
  dial.setConfirmed(50, millis());
  board::dumpEncoderPins();
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
  static uint32_t lastBeat = 0;
  static uint32_t maxTimerMs = 0, iterations = 0;
  const bool changed = vm.weight != lastWeightSeen;
  if (changed) { Serial.printf("[dbg] render weight %d\n", vm.weight); lastWeightSeen = vm.weight; }
  ui::render(vm);
  if (changed) Serial.println("[dbg] render done");
  const uint32_t t0 = millis();
  lv_timer_handler();
  const uint32_t dt = millis() - t0;
  if (dt > maxTimerMs) maxTimerMs = dt;
  if (changed) Serial.printf("[dbg] timer done in %u ms\n", (unsigned)dt);
  ++iterations;
  static uint32_t lastDump = 0;
  if (now - lastDump >= 15000) { lastDump = now; board::dumpEncoderPins(); }
  if (now - lastBeat >= 1000) {
    lastBeat = now;
    const board::DebugStats ds = board::debugStats();
    Serial.printf("[beat] iters=%u maxTimer=%u heap=%u encLoops=%u edges=%u A=%u B=%u stackFree=%u q=%u\n", (unsigned)iterations, (unsigned)maxTimerMs, (unsigned)ESP.getFreeHeap(), (unsigned)ds.encLoops, (unsigned)ds.encEdges, (unsigned)ds.levelA, (unsigned)ds.levelB, (unsigned)ds.encStackFree, (unsigned)ds.queueWaiting);
    iterations = 0; maxTimerMs = 0;
  }
  delay(5);
}

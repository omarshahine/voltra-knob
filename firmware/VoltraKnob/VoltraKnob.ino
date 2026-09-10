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

static const char *stateText(const vlink::DeviceState &s) {
  switch (s.link) {
    case vlink::LinkState::Scanning: return "SEARCHING";
    case vlink::LinkState::Connecting: return "CONNECTING";
    case vlink::LinkState::Handshaking: return "CONNECTING";
    case vlink::LinkState::AppConnectedSuspect: return "APP CONNECTED?";
    case vlink::LinkState::Ready: return s.loaded ? "LOADED" : "UNLOADED";
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
  vlink::voltraLink.begin();
  delay(500);
  board::dumpEncoderPins();
  if (held) { vlink::voltraLink.clearPinnedAddress(); Serial.println("[BOOT] pinned address cleared"); }
}

static uint32_t g_iters = 0, g_maxIterMs = 0, g_rotates = 0, g_lastIterStart = 0;

void loop() {
  const uint32_t now = millis();
  if (g_lastIterStart) { const uint32_t d = now - g_lastIterStart; if (d > g_maxIterMs) g_maxIterMs = d; }
  g_lastIterStart = now;
  ++g_iters;
  const vlink::DeviceState dev = vlink::voltraLink.state();
  const bool ready = dev.link == vlink::LinkState::Ready;

  if (ready && !wasReady) { lastConfirmSeq = 0; }   // fresh session: accept the first confirmation
  wasReady = ready;
  if (dev.confirmSeq != lastConfirmSeq && dev.weightLbs >= 0) {
    lastConfirmSeq = dev.confirmSeq;
    dial.setConfirmed(dev.weightLbs, now);
  }
  dial.setLoaded(dev.loaded);

  board::RotateEvent ev;
  {
    int got = 0;
    while (board::pollRotate(ev)) { ++g_rotates; ++got; if (ready) dial.rotate(ev.direction, ev.ms); }
    if (got) Serial.printf("[rot] t=%lu got=%d evAge=%lu ms target=%d\n", (unsigned long)now, got, (unsigned long)(now - ev.ms), dial.target());
  }

  static uint32_t lastButtonPoll = 0;
  if (now - lastButtonPoll >= 10) {
    lastButtonPoll = now;
    const app::ButtonAction a = button.onLevel(board::buttonPressed(), now);
    switch (app::decideLoad(a, ready, dev.loaded)) {
      case app::LoadCommand::Load: Serial.println("[app] load requested"); vlink::voltraLink.requestLoad(); break;
      case app::LoadCommand::Unload: Serial.println("[app] unload requested"); vlink::voltraLink.requestUnload(); break;
      default: break;
    }
  }

  int lbs;
  if (ready && dial.pollSend(now, lbs)) vlink::voltraLink.requestWeight(lbs);
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
  static uint32_t maxTimerMs = 0;
  const uint32_t t0 = millis();
  lv_timer_handler();
  const uint32_t dt = millis() - t0;
  if (dt > maxTimerMs) maxTimerMs = dt;

  static uint32_t lastBeat = 0;
  if (now - lastBeat >= 2000) {
    lastBeat = now;
    const board::DebugStats ds = board::debugStats();
    static uint32_t lastEncLoops = 0;
    const uint32_t encDelta = ds.encLoops - lastEncLoops; lastEncLoops = ds.encLoops;
    Serial.printf("[beat] t=%lu encLoops=%u edgesA=%u edgesB=%u A=%d B=%d | link=%d w=%d ld=%d batt=%d reps=%d heap=%u | iters=%u maxIter=%u ms maxTimer=%u ms rot=%u flushes=%u px=%u flushMax=%u us\n",
                  (unsigned long)now, (unsigned)encDelta, (unsigned)ds.edgesA, (unsigned)ds.edgesB, ds.rawA, ds.rawB,
                  (int)dev.link, dev.weightLbs, (int)dev.loaded, dev.battery, dev.reps, (unsigned)ESP.getFreeHeap(),
                  (unsigned)g_iters, (unsigned)g_maxIterMs, (unsigned)maxTimerMs, (unsigned)g_rotates,
                  (unsigned)ds.flushes, (unsigned)ds.flushPixels, (unsigned)ds.flushMaxUs);
    maxTimerMs = 0; g_iters = 0; g_maxIterMs = 0; g_rotates = 0;
  }
  delay(5);
}

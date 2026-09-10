#include "app_logic.h"

namespace app {

void WeightDial::setConfirmed(int lbs, uint32_t /*now*/) {
  confirmed_ = lbs;
  if (pending_ && lbs == sent_) pending_ = false;
  if (!dirty_ && !pending_) target_ = lbs;
  capped_ = false;
}

namespace {
// Move from `from` to the next multiple of `step` in `direction`.
int snapStep(int from, int step, int direction) {
  if (step == 1) return from + direction;
  if (direction > 0) return (from / step + 1) * step;
  return from % step == 0 ? from - step : (from / step) * step;
}
}  // namespace

void WeightDial::rotate(int direction, uint32_t now) {
  if (now - lastDetentMs_ > BURST_WINDOW_MS) burstCount_ = 0;
  lastDetentMs_ = now;
  ++burstCount_;
  int step = 1;
  if (burstCount_ > BURST_DETENTS_10) step = 10;
  else if (burstCount_ > BURST_DETENTS_5) step = 5;
  int next = snapStep(target_, step, direction > 0 ? 1 : -1);
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
  if (held < HOLD_RING_DELAY_MS) return 0.f;
  if (held >= HOLD_MS) return 1.f;
  return (float)(held - HOLD_RING_DELAY_MS) / (float)(HOLD_MS - HOLD_RING_DELAY_MS);
}

LoadCommand decideLoad(ButtonAction a, bool connected, bool loaded, bool armed) {
  if (!connected) return LoadCommand::None;
  if (a == ButtonAction::Tap && (loaded || armed)) return LoadCommand::Unload;
  if (a == ButtonAction::Tap) return LoadCommand::Load;
  if (a == ButtonAction::HoldComplete && !loaded && !armed) return LoadCommand::ArmAutoLoad;
  return LoadCommand::None;
}

}  // namespace app

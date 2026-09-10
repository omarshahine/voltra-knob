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

#pragma once
#include <cstdint>

namespace app {

constexpr int WEIGHT_MIN = 5;
constexpr int WEIGHT_MAX = 200;
constexpr int LOADED_MAX_DELTA = 25;
constexpr uint32_t SEND_DEBOUNCE_MS = 250;
constexpr uint32_t CONFIRM_TIMEOUT_MS = 3000;
constexpr uint32_t BURST_WINDOW_MS = 150;
constexpr int BURST_DETENTS_5 = 4;    // after this many detents inside the window, step 5 lb (snapped to 5s)
constexpr int BURST_DETENTS_10 = 11;  // after this many, step 10 lb (snapped to 10s)
constexpr uint32_t TAP_MAX_MS = 400;
constexpr uint32_t HOLD_MS = 600;
constexpr uint32_t HOLD_RING_DELAY_MS = 250;   // ring stays hidden this long so a tap never shows it
constexpr uint32_t CONTACT_BOUNCE_MS = 30;

class WeightDial {
 public:
  void setConfirmed(int lbs, uint32_t now);
  void rotate(int direction, uint32_t now);
  void setLoaded(bool loaded) { loaded_ = loaded; }
  bool pollSend(uint32_t now, int& lbs);
  bool pollTimeout(uint32_t now);
  int target() const { return target_; }
  int confirmed() const { return confirmed_; }
  bool pending() const { return pending_; }
  bool capped() const { return capped_; }

 private:
  int target_ = WEIGHT_MIN;
  int confirmed_ = WEIGHT_MIN;
  bool loaded_ = false;
  bool dirty_ = false;
  bool pending_ = false;
  bool capped_ = false;
  int sent_ = 0;
  uint32_t lastDetentMs_ = 0;
  uint32_t sentMs_ = 0;
  int burstCount_ = 0;
};

enum class ButtonAction { None, Tap, HoldComplete, HoldCancelled };

class ButtonClassifier {
 public:
  ButtonAction onLevel(bool pressed, uint32_t now);
  float holdProgress(uint32_t now) const;
  bool isPressed() const { return pressed_; }

 private:
  bool pressed_ = false;
  bool holdFired_ = false;
  bool haveEdge_ = false;
  uint32_t pressMs_ = 0;
  uint32_t lastEdgeMs_ = 0;
};

enum class LoadCommand { None, Load, Unload, ArmAutoLoad };
// Tap while loaded or armed: unload/cancel (always instant). Tap while idle: load now.
// Hold while idle: arm auto-load (device engages on the first pull after its countdown).
LoadCommand decideLoad(ButtonAction a, bool connected, bool loaded, bool armed = false);

}  // namespace app

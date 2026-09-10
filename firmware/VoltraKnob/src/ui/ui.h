#pragma once
#include <cstdint>

namespace ui {

struct ViewModel {
  int weight = 5;
  bool pending = false;
  bool capped = false;
  bool loaded = false;
  bool armed = false;      // auto-load armed
  int countdownMs = -1;
  bool connected = false;
  float holdProgress = 0.f;
  int battery = -1;
  int reps = 0;
  const char* stateText = "SEARCHING";
};

void begin();                    // builds the screen once; call after board::begin()
void render(const ViewModel& vm); // idempotent; cheap when nothing changed
void tick(uint32_t nowMs);        // applies the throttled ring update; call every loop

}  // namespace ui

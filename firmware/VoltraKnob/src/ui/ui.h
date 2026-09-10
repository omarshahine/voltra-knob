#pragma once

namespace ui {

struct ViewModel {
  int weight = 5;
  bool pending = false;
  bool capped = false;
  bool loaded = false;
  bool connected = false;
  float holdProgress = 0.f;
  int battery = -1;
  int reps = 0;
  const char* stateText = "SEARCHING";
};

void begin();                    // builds the screen once; call after board::begin()
void render(const ViewModel& vm); // idempotent; cheap when nothing changed

}  // namespace ui

#pragma once
#include <cstdint>

namespace vlink {

enum class LinkState : uint8_t { Scanning, Connecting, Handshaking, Ready, AppConnectedSuspect };

struct DeviceState {
  LinkState link = LinkState::Scanning;
  int weightLbs = -1;      // last device-confirmed weight, -1 unknown
  int mode = -1;
  int battery = -1;
  bool loaded = false;     // device reports motor engaged
  bool armed = false;      // auto-load armed; device will engage after its countdown / first pull
  int countdownMs = -1;    // remaining auto-load countdown when known
  int guidedState = -1;    // 53c7: 11 waiting, 12 countdown, 13 engaging, 14 loaded (while fitness mode is 0x27)
  int reps = 0;            // from per-rep frames since last load
  uint32_t confirmSeq = 0; // increments on every device weight confirmation
};

class VoltraLink {
 public:
  void begin();
  DeviceState state();
  bool requestWeight(int lbs);
  bool requestLoad();
  bool requestUnload();
  bool requestArmAutoLoad();
  void clearPinnedAddress();
};

extern VoltraLink voltraLink;

}  // namespace vlink

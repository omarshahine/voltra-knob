#pragma once
#include <cstddef>
#include <cstdint>
#include "frames.gen.h"

namespace voltra {

struct Frame { const uint8_t* data; size_t len; };

constexpr int WEIGHT_MIN = frames::WEIGHT_MIN;
constexpr int WEIGHT_MAX = frames::WEIGHT_MAX;
constexpr size_t INIT_COUNT = frames::INIT_COUNT;
constexpr const char* SERVICE_UUID = frames::SERVICE_UUID;
constexpr const char* WRITE_CHAR_UUID = frames::WRITE_CHAR_UUID;
constexpr const char* NOTIFY_CHAR_UUID = frames::NOTIFY_CHAR_UUID;
constexpr const char* DEVICE_NAME_PREFIX = frames::DEVICE_NAME_PREFIX;

Frame authFrame();
Frame initFrame(size_t i);
Frame setupFrame();
Frame goFrame();
Frame stopFrame();
Frame modeWeightTrainingFrame();
Frame modeIdleFrame();
bool weightFrame(int lbs, Frame& out);

enum class EventType : uint8_t { Unknown, Settings, ModeConfirmed, Battery, Telemetry, PerRep, SetSummary };

struct Event {
  EventType type = EventType::Unknown;
  int weightLbs = -1;   // Settings
  int mode = -1;        // Settings, ModeConfirmed (0 idle, 1 weight training, ...)
  int battery = -1;     // Battery, 0..100
  int phase = -1;       // Telemetry: 0 idle, 1 concentric, 2 hold, 3 eccentric
  int repCount = -1;    // PerRep, SetSummary
  int setCounter = -1;  // PerRep
};

// Decode one BLE notification. Never throws; unknown or short input yields EventType::Unknown.
Event decode(const uint8_t* d, size_t n);

}  // namespace voltra

#include "voltra_protocol.h"

namespace voltra {

Frame authFrame() { return {frames::AUTH_KNOB, sizeof(frames::AUTH_KNOB)}; }
Frame authFrameFallback() { return {frames::AUTH_IPHONE, sizeof(frames::AUTH_IPHONE)}; }
Frame telemetrySubscribeAllFrame() { return {frames::TELEMETRY_SUBSCRIBE_ALL, sizeof(frames::TELEMETRY_SUBSCRIBE_ALL)}; }
Frame readSettingsFrame() { return {frames::READ_SETTINGS, sizeof(frames::READ_SETTINGS)}; }
Frame guidedLoadTriggerFrame() { return {frames::GUIDED_LOAD_TRIGGER, sizeof(frames::GUIDED_LOAD_TRIGGER)}; }
Frame readGuidedLoadStatusFrame() { return {frames::READ_GUIDED_LOAD_STATUS, sizeof(frames::READ_GUIDED_LOAD_STATUS)}; }
Frame initFrame(size_t i) {
  if (i == 0) return {frames::INIT0, sizeof(frames::INIT0)};
  return {frames::INIT1, sizeof(frames::INIT1)};
}
Frame setupFrame() { return {frames::SETUP, sizeof(frames::SETUP)}; }
Frame goFrame() { return {frames::GO, sizeof(frames::GO)}; }
Frame stopFrame() { return {frames::STOP, sizeof(frames::STOP)}; }
Frame modeWeightTrainingFrame() { return {frames::MODE_WEIGHT_TRAINING, sizeof(frames::MODE_WEIGHT_TRAINING)}; }
Frame modeIdleFrame() { return {frames::MODE_IDLE, sizeof(frames::MODE_IDLE)}; }

bool weightFrame(int lbs, Frame& out) {
  if (lbs < WEIGHT_MIN || lbs > WEIGHT_MAX) return false;
  out = {frames::WEIGHT[lbs - WEIGHT_MIN], frames::WEIGHT_FRAME_LEN};
  return true;
}

namespace {
inline int u16le(const uint8_t* d, size_t off) { return d[off] | (d[off + 1] << 8); }
inline bool vendor(const uint8_t* d, size_t n, uint8_t a, uint8_t b) {
  return n >= 13 && d[10] == 0xaa && d[11] == a && d[12] == b;
}
inline bool isU16Param(uint8_t hi, uint8_t lo) {
  if (lo == 0x3e && (hi == 0x86 || hi == 0x87 || hi == 0x88 || hi == 0x89)) return true;
  if (lo == 0x53 && hi == 0xc8) return true;   // guided-load countdown ms
  return false;
}
// Widths for the cmd 0x0f bulk response (SDK CMD_0F_KNOWN_PARAM_WIDTHS). 0 = unknown, stop parsing.
inline size_t cmd0fWidth(uint8_t hi, uint8_t lo) {
  if (lo == 0x3e && (hi == 0x86 || hi == 0x87 || hi == 0x88 || hi == 0x89 || hi == 0x82)) return 2;
  if (lo == 0x50 && hi == 0x6a) return 2;
  if (lo == 0x53 && (hi == 0x62 || hi == 0xb7 || hi == 0xd2)) return 2;
  if (lo == 0x54 && hi == 0x31) return 2;
  if (lo == 0x53 && hi == 0xc8) return 2;                                    // guided-load countdown
  if (lo == 0x53 && (hi == 0x8d || hi == 0xc7 || hi == 0xc9)) return 1;      // guided-load safety, state, ctrl
  if (lo == 0x53 && (hi == 0x61 || hi == 0xb6 || hi == 0xb0 || hi == 0xc6)) return 1;
  if (lo == 0x52 && hi == 0xe3) return 1;
  if (lo == 0x51 && (hi == 0x06 || hi == 0x03)) return 1;
  if (lo == 0x4f && hi == 0xb0) return 1;
  return 0;
}
inline void applyParam(Event& e, uint8_t hi, uint8_t lo, int value) {
  if (hi == 0x86 && lo == 0x3e) e.weightLbs = value;
  else if (hi == 0xb0 && lo == 0x4f) e.mode = value;
  else if (hi == 0x89 && lo == 0x3e) e.fitnessMode = value;
  else if (hi == 0xc8 && lo == 0x53) e.guidedCountdownMs = value;
  else if (hi == 0x8d && lo == 0x53) e.guidedSafety = value;
}
}  // namespace

Event decode(const uint8_t* d, size_t n) {
  Event e;
  if (d == nullptr || n < 4) return e;

  if (n >= 30 && d[0] == 0x55 && d[1] == 0x3a && d[2] == 0x04 && d[3] == 0x70) {
    e.type = EventType::Telemetry; e.phase = d[13]; return e;
  }
  if (vendor(d, n, 0x82, 0x3b) && n >= 74) {
    e.type = EventType::PerRep; e.setCounter = d[15]; e.repCount = d[17]; return e;
  }
  if (vendor(d, n, 0x85, 0x5f) && n >= 110) {
    e.type = EventType::SetSummary; e.repCount = u16le(d, 26); return e;
  }
  if (vendor(d, n, 0x86, 0x7d) && n >= 140) {
    e.type = EventType::SetSummary; e.repCount = u16le(d, 16); return e;
  }
  if (vendor(d, n, 0x80, 0x25) && n >= 52) {
    return e;  // state dump; aliases the 5534 header, never read battery from it
  }
  if (n >= 16 && d[10] == 0x10) {
    e.type = EventType::Settings;
    const int count = d[11];
    size_t off = 13;
    for (int i = 0; i < count && i < 16; ++i) {
      if (off + 2 > n) break;
      const uint8_t hi = d[off], lo = d[off + 1];
      off += 2;
      const size_t width = isU16Param(hi, lo) ? 2 : 1;
      if (off + width > n) break;
      const int value = width == 2 ? u16le(d, off) : d[off];
      off += width;
      applyParam(e, hi, lo, value);
    }
    return e;
  }
  if (n >= 14 && d[0] == 0x55 && (d[2] == 0x08 || d[2] == 0x09) && d[10] == 0x0f) {
    e.type = EventType::Settings;
    const int count = u16le(d, 12);
    size_t off = 14;
    for (int i = 0; i < count && i < 32; ++i) {
      if (off + 2 > n) break;
      const uint8_t hi = d[off], lo = d[off + 1];
      off += 2;
      const size_t width = cmd0fWidth(hi, lo);
      if (width == 0 || off + width > n) break;
      const int value = width == 2 ? u16le(d, off) : d[off];
      off += width;
      applyParam(e, hi, lo, value);
    }
    return e;
  }
  if (d[0] == 0x55 && d[1] == 0x12 && n >= 18) {
    e.type = EventType::ModeConfirmed;
    if (d[13] == 0x89 && d[14] == 0x3e) e.fitnessMode = d[15];
    else e.mode = d[15];
    return e;
  }
  if (d[0] == 0x55 && d[1] == 0x23 && n >= 35) { e.type = EventType::Battery; e.battery = d[11]; return e; }
  if (d[0] == 0x55 && d[1] == 0x34 && n >= 52) { e.type = EventType::Battery; e.battery = d[12]; return e; }
  return e;
}

}  // namespace voltra

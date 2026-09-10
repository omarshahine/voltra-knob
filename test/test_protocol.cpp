#include "minitest.h"
#include "protocol/voltra_protocol.h"
#include <cstring>
#include <initializer_list>
#include <utility>
using namespace voltra;

static bool hexEq(const Frame& f, const char* hex) {
  if (std::strlen(hex) != f.len * 2) return false;
  for (size_t i = 0; i < f.len; ++i) {
    unsigned v; std::sscanf(hex + 2 * i, "%2x", &v);
    if (f.data[i] != v) return false;
  }
  return true;
}

static void testFrames() {
  CHECK_EQ(authFrameFallback().len, 41u);
  CHECK(hexEq(authFrameFallback(), "552904c90110000020004f6950686f6e6500000000000000000000000000000084ab1a5f29200172d8"));
  CHECK_EQ(authFrame().len, 41u);
  CHECK(std::memcmp(authFrame().data, authFrameFallback().data, 11) == 0);      // header + cmd identical
  CHECK(std::memcmp(authFrame().data + 11, "Voltra Knob", 11) == 0);            // custom name in the name field
  CHECK(std::memcmp(authFrame().data + 32, authFrameFallback().data + 32, 7) == 0);   // trailer identical
  CHECK(hexEq(telemetrySubscribeAllFrame(), "551504a9aa10012020001101008351ffffffff35eb"));
  CHECK_EQ(initFrame(0).len, 15u);
  CHECK(hexEq(initFrame(0), "550f0801aad200002000ff00aa0419"));
  CHECK_EQ(initFrame(1).len, 31u);
  CHECK(hexEq(setupFrame(), "55130403aa10150020000f02006a50823e8f2f"));
  CHECK(hexEq(goFrame(),    "55130403aa1016002000110100893e05008173"));
  CHECK(hexEq(stopFrame(),  "55130403aa101b002000110100893e040037dd"));
  CHECK(hexEq(modeWeightTrainingFrame(), "55130403aa1000202000110100b04f0100f353"));
  Frame f;
  CHECK(weightFrame(5, f));   CHECK(hexEq(f, "55130403aa1000202000110100863e0500ffe9"));
  CHECK(weightFrame(20, f));  CHECK(hexEq(f, "55130403aa100f202000110100863e1400fa79"));
  CHECK(weightFrame(200, f)); CHECK_EQ(f.len, 19u);
  CHECK(f.data[15] == 200 && f.data[16] == 0);  // uint16 LE weight payload
  CHECK(!weightFrame(4, f));
  CHECK(!weightFrame(201, f));
  // Every weight frame carries its own value at bytes 15..16 and the correct length byte.
  for (int w = WEIGHT_MIN; w <= WEIGHT_MAX; ++w) {
    CHECK(weightFrame(w, f));
    CHECK_EQ(f.len, 19u);
    CHECK_EQ((int)f.data[1], 0x13);
    CHECK_EQ(f.data[15] | (f.data[16] << 8), w);
  }
}

static size_t fromHex(const char* hex, uint8_t* out, size_t cap) {
  size_t n = std::strlen(hex) / 2;
  for (size_t i = 0; i < n && i < cap; ++i) { unsigned v; std::sscanf(hex + 2 * i, "%2x", &v); out[i] = (uint8_t)v; }
  return n;
}

// Build a synthetic frame: header bytes then zero-fill to `len`, then apply (offset,value) patches.
static size_t synth(uint8_t* out, size_t len, const char* headerHex, std::initializer_list<std::pair<int,int>> patches) {
  std::memset(out, 0, len);
  fromHex(headerHex, out, len);
  out[1] = (uint8_t)len;
  for (auto& p : patches) out[p.first] = (uint8_t)p.second;
  return len;
}

static void testDecode() {
  uint8_t b[160];
  // Settings update (cmd 0x10 at byte 10): two params, base weight 863e=0x0032 (50 lb), mode b04f=1.
  size_t n = synth(b, 46, "552e04", {{10,0x10},{11,2},{13,0x86},{14,0x3e},{15,0x32},{16,0x00},{17,0xb0},{18,0x4f},{19,1}});
  Event e = decode(b, n);
  CHECK(e.type == EventType::Settings); CHECK_EQ(e.weightLbs, 50); CHECK_EQ(e.mode, 1);

  // Settings with a 1-byte param first, then weight: verifies width handling.
  n = synth(b, 46, "551604", {{10,0x10},{11,2},{13,0xb0},{14,0x4f},{15,1},{16,0x86},{17,0x3e},{18,0xc8},{19,0x00}});
  e = decode(b, n);
  CHECK(e.type == EventType::Settings); CHECK_EQ(e.weightLbs, 200); CHECK_EQ(e.mode, 1);

  // Mode confirmation: value at 15.
  n = synth(b, 18, "551204", {{15, 4}});
  e = decode(b, n); CHECK(e.type == EventType::ModeConfirmed); CHECK_EQ(e.mode, 4);

  // Device init battery at 11.
  n = synth(b, 35, "552304", {{11, 87}});
  e = decode(b, n); CHECK(e.type == EventType::Battery); CHECK_EQ(e.battery, 87);

  // Status battery at 12.
  n = synth(b, 52, "553404", {{12, 63}});
  e = decode(b, n); CHECK(e.type == EventType::Battery); CHECK_EQ(e.battery, 63);

  // State dump aliases the 5534 header: must NOT yield a battery reading.
  n = synth(b, 52, "553404ac", {{10,0xaa},{11,0x80},{12,0x25}});
  e = decode(b, n); CHECK(e.type == EventType::Unknown);

  // Telemetry stream, phase eccentric.
  n = synth(b, 58, "553a0470", {{13, 3}});   // real stream frames are 0x3a = 58 bytes
  e = decode(b, n); CHECK(e.type == EventType::Telemetry); CHECK_EQ(e.phase, 3);
  n = synth(b, 20, "553a0470", {{1, 0x3a}, {13, 3}});   // too short (keep the 0x3a header byte)
  e = decode(b, n); CHECK(e.type == EventType::Unknown);

  // Vendor per-rep: 74 bytes, aa 82 3b at 10..12, setCounter at 15, repCount at 17.
  n = synth(b, 74, "554a04", {{10,0xaa},{11,0x82},{12,0x3b},{15,2},{17,7}});
  e = decode(b, n); CHECK(e.type == EventType::PerRep); CHECK_EQ(e.repCount, 7); CHECK_EQ(e.setCounter, 2);

  // Vendor set summary: 110 bytes, aa 85 5f, repCount u16 at 26.
  n = synth(b, 110, "556e04", {{10,0xaa},{11,0x85},{12,0x5f},{26,12},{27,0}});
  e = decode(b, n); CHECK(e.type == EventType::SetSummary); CHECK_EQ(e.repCount, 12);

  // Vendor summary: 140 bytes, aa 86 7d, repCount u16 at 16.
  n = synth(b, 140, "558c04", {{10,0xaa},{11,0x86},{12,0x7d},{16,9},{17,0}});
  e = decode(b, n); CHECK(e.type == EventType::SetSummary); CHECK_EQ(e.repCount, 9);

  // Settings with fitness mode 893e = 5 (motor engaged) alongside the weight.
  n = synth(b, 46, "552e04", {{10,0x10},{11,2},{13,0x86},{14,0x3e},{15,0x0a},{16,0x00},{17,0x89},{18,0x3e},{19,0x05},{20,0x00}});
  e = decode(b, n); CHECK(e.type == EventType::Settings); CHECK_EQ(e.weightLbs, 10); CHECK_EQ(e.fitnessMode, 5);
  CHECK(fitnessModeLoaded(5)); CHECK(fitnessModeUnloaded(4)); CHECK(!fitnessModeLoaded(4));

  // Mode confirmation carrying 893e reports fitnessMode, not training mode.
  n = synth(b, 18, "551204", {{13,0x89},{14,0x3e},{15,4}});
  e = decode(b, n); CHECK(e.type == EventType::ModeConfirmed); CHECK_EQ(e.fitnessMode, 4); CHECK_EQ(e.mode, -1);
  n = synth(b, 18, "551204", {{13,0xb0},{14,0x4f},{15,1}});
  e = decode(b, n); CHECK(e.type == EventType::ModeConfirmed); CHECK_EQ(e.mode, 1); CHECK_EQ(e.fitnessMode, -1);

  // cmd 0x0f bulk response: frame type 0x08, count u16 at 12, params from 14.
  n = synth(b, 30, "551e08", {{10,0x0f},{12,3},{13,0},{14,0x86},{15,0x3e},{16,0x28},{17,0x00},{18,0x89},{19,0x3e},{20,0x04},{21,0x00},{22,0xb0},{23,0x4f},{24,1}});
  e = decode(b, n); CHECK(e.type == EventType::Settings); CHECK_EQ(e.weightLbs, 40); CHECK_EQ(e.fitnessMode, 4); CHECK_EQ(e.mode, 1);

  // Read-settings request frame: cmd 0x0f, 3 params, valid header length.
  CHECK_EQ((int)readSettingsFrame().data[10], 0x0f);
  CHECK_EQ((int)readSettingsFrame().data[1], (int)readSettingsFrame().len);
  CHECK_EQ((int)readSettingsFrame().data[11], 3);

  // Guided-load status in a cmd 0x0f response: safety 538d=0, state 53c7=1, countdown 53c8=2500 ms, ctrl 53c9=1.
  n = synth(b, 30, "551e08", {{10,0x0f},{12,4},{13,0},{14,0x8d},{15,0x53},{16,0},{17,0xc7},{18,0x53},{19,1},{20,0xc8},{21,0x53},{22,0xc4},{23,0x09},{24,0xc9},{25,0x53},{26,1}});
  e = decode(b, n); CHECK(e.type == EventType::Settings); CHECK_EQ(e.guidedCountdownMs, 2500); CHECK_EQ(e.guidedSafety, 0);
  // Fitness mode 0x26 = armed, not unloaded.
  CHECK(fitnessModeArmed(0x26)); CHECK(!fitnessModeUnloaded(0x26)); CHECK(!fitnessModeLoaded(0x26));
  CHECK(fitnessModeArmed(0x27)); CHECK(!fitnessModeLoaded(0x27)); CHECK(fitnessModeLoaded(5));
  CHECK_EQ(e.guidedState, 1); CHECK_EQ(e.guidedCtrl, 1);
  // Trigger frame: vendor cmd 0xaa, sub-command 0x12, 14 bytes with valid length byte.
  CHECK_EQ(guidedLoadTriggerFrame().len, 14u);
  CHECK_EQ((int)guidedLoadTriggerFrame().data[10], 0xaa); CHECK_EQ((int)guidedLoadTriggerFrame().data[11], 0x12);
  CHECK_EQ((int)guidedLoadTriggerFrame().data[1], 14);
  CHECK_EQ((int)readGuidedLoadStatusFrame().data[11], 4);

  // Garbage and short input.
  uint8_t junk[3] = {1, 2, 3};
  CHECK(decode(junk, 3).type == EventType::Unknown);
  CHECK(decode(nullptr, 0).type == EventType::Unknown);
}

MINITEST_MAIN({ testFrames(); testDecode(); })

#include "voltra_protocol.h"

namespace voltra {

Frame authFrame() { return {frames::AUTH, sizeof(frames::AUTH)}; }
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

Event decode(const uint8_t*, size_t) { return Event{}; }

}  // namespace voltra

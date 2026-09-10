#include "minitest.h"
#include "app/app_logic.h"
using namespace app;

static void testDialBasics() {
  WeightDial d;
  d.setConfirmed(50, 0);
  CHECK_EQ(d.target(), 50); CHECK_EQ(d.confirmed(), 50); CHECK(!d.pending());
  d.rotate(+1, 10); d.rotate(+1, 20);
  CHECK_EQ(d.target(), 52);
  int lbs = 0;
  CHECK(!d.pollSend(100, lbs));          // 80 ms after last detent: too early
  CHECK(d.pollSend(271, lbs));           // 251 ms after last detent
  CHECK_EQ(lbs, 52); CHECK(d.pending());
  CHECK(!d.pollSend(300, lbs));          // only once
  d.setConfirmed(52, 400);
  CHECK(!d.pending()); CHECK_EQ(d.confirmed(), 52);
}

static void testDialClamp() {
  WeightDial d; d.setConfirmed(6, 0);
  d.rotate(-1, 1); d.rotate(-1, 2); d.rotate(-1, 3);
  CHECK_EQ(d.target(), 5);
  WeightDial d2; d2.setConfirmed(199, 0);
  d2.rotate(+1, 1000); d2.rotate(+1, 1001);
  CHECK_EQ(d2.target(), 200);
}

static void testDialBurst() {
  WeightDial d; d.setConfirmed(50, 0);
  for (int i = 0; i < 4; ++i) d.rotate(+1, 10 + i * 20);   // 4 detents in 60 ms -> still 1 lb each
  CHECK_EQ(d.target(), 54);
  d.rotate(+1, 100);                                        // 5th detent inside the burst: 5 lb
  CHECK_EQ(d.target(), 59);
  d.rotate(+1, 400);                                        // 300 ms gap ends the burst
  CHECK_EQ(d.target(), 60);
}

static void testDialTimeoutReverts() {
  WeightDial d; d.setConfirmed(50, 0);
  d.rotate(+1, 10);
  int lbs; CHECK(d.pollSend(300, lbs));
  CHECK(!d.pollTimeout(2000));
  CHECK(d.pollTimeout(2301));
  CHECK_EQ(d.target(), 50); CHECK(!d.pending());
  CHECK(!d.pollTimeout(2400));
}

static void testDialLoadedCap() {
  WeightDial d; d.setConfirmed(50, 0); d.setLoaded(true);
  for (int i = 0; i < 40; ++i) d.rotate(+1, 1000 + i * 200);   // slow detents, no burst
  CHECK_EQ(d.target(), 75); CHECK(d.capped());
  d.rotate(-1, 20000); CHECK_EQ(d.target(), 74); CHECK(!d.capped());
  d.setLoaded(false);
  for (int i = 0; i < 5; ++i) d.rotate(+1, 30000 + i * 200);
  CHECK_EQ(d.target(), 79);
}

static void testDialConfirmedWhileIdleFollowsDevice() {
  WeightDial d; d.setConfirmed(50, 0);
  d.setConfirmed(65, 100);          // e.g. someone changed it in another session
  CHECK_EQ(d.target(), 65);
}

static void testButtonTap() {
  ButtonClassifier b;
  CHECK(b.onLevel(false, 0) == ButtonAction::None);
  CHECK(b.onLevel(true, 100) == ButtonAction::None);
  CHECK(b.onLevel(true, 200) == ButtonAction::None);
  CHECK(b.onLevel(false, 300) == ButtonAction::Tap);
  CHECK(b.onLevel(false, 310) == ButtonAction::None);
}

static void testButtonHold() {
  ButtonClassifier b;
  b.onLevel(true, 1000);
  CHECK(b.onLevel(true, 1500) == ButtonAction::None);
  CHECK(b.holdProgress(1300) > 0.49f && b.holdProgress(1300) < 0.51f);
  CHECK(b.onLevel(true, 1600) == ButtonAction::HoldComplete);
  CHECK(b.onLevel(true, 1700) == ButtonAction::None);      // fires once
  CHECK(b.onLevel(false, 1800) == ButtonAction::None);     // release after hold is not a tap
}

static void testButtonHoldCancelled() {
  ButtonClassifier b;
  b.onLevel(true, 0);
  CHECK(b.onLevel(false, 500) == ButtonAction::HoldCancelled);   // between tap max and hold
}

static void testButtonBounce() {
  ButtonClassifier b;
  b.onLevel(true, 0);
  CHECK(b.onLevel(false, 10) == ButtonAction::None);   // bounce inside 30 ms ignored
  CHECK(b.isPressed());
  CHECK(b.onLevel(true, 20) == ButtonAction::None);
  CHECK(b.onLevel(false, 200) == ButtonAction::Tap);
}

static void testDecideLoad() {
  CHECK(decideLoad(ButtonAction::Tap, true, true) == LoadCommand::Unload);
  CHECK(decideLoad(ButtonAction::Tap, true, false) == LoadCommand::None);
  CHECK(decideLoad(ButtonAction::HoldComplete, true, false) == LoadCommand::Load);
  CHECK(decideLoad(ButtonAction::HoldComplete, true, true) == LoadCommand::None);
  CHECK(decideLoad(ButtonAction::HoldComplete, false, false) == LoadCommand::None);
  CHECK(decideLoad(ButtonAction::Tap, false, true) == LoadCommand::None);
  CHECK(decideLoad(ButtonAction::HoldCancelled, true, false) == LoadCommand::None);
}

MINITEST_MAIN({
  testDialBasics(); testDialClamp(); testDialBurst(); testDialTimeoutReverts();
  testDialLoadedCap(); testDialConfirmedWhileIdleFollowsDevice();
  testButtonTap(); testButtonHold(); testButtonHoldCancelled(); testButtonBounce(); testDecideLoad();
})

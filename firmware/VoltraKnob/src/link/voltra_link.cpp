#include "voltra_link.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <cstring>
#include "../protocol/voltra_protocol.h"

namespace vlink {

VoltraLink voltraLink;

namespace {

enum class CmdType : uint8_t { Load, Unload };
struct Cmd { CmdType type; int lbs; };

constexpr uint32_t AUTH_WAIT_MS = 3000;
constexpr uint32_t INIT_GAP_MS = 20;
constexpr uint32_t WRITE_GAP_MS = 500;
constexpr uint32_t HANDSHAKE_DEADLINE_MS = 5000;
constexpr uint32_t LOAD_SETUP_GAP_MS = 300;
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint16_t MTU_REQUEST = 527;
constexpr int APP_SUSPECT_AFTER_FAILURES = 3;

DeviceState g_state;
SemaphoreHandle_t g_mutex = nullptr;
QueueHandle_t g_cmdQueue = nullptr;
Preferences g_prefs;

NimBLEClient *g_client = nullptr;
NimBLERemoteCharacteristic *g_write = nullptr;
volatile bool g_disconnected = false;
volatile bool g_foundTarget = false;
NimBLEAddress g_targetAddr;
volatile uint32_t g_lastNotifyMs = 0;
volatile bool g_handshakeEvent = false;
int g_consecutiveFailures = 0;
uint32_t g_lastWriteMs = 0;
String g_pinned;   // pinned device address read from NVS at the start of each scan
bool g_useFallbackAuth = false;   // switch to the SDK iPhone identity if the custom name is rejected
volatile int g_pendingWeight = -1;   // latest dial target not yet written; coalesces bursts into one frame

void setState(void (*fn)(DeviceState&)) {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  fn(g_state);
  xSemaphoreGive(g_mutex);
}

// Device-reported motor state (893e). Called with the state mutex held.
void applyFitnessMode(int fm) {
  if (voltra::fitnessModeLoaded(fm) && !g_state.loaded) { g_state.loaded = true; g_state.reps = 0; Serial.printf("[link] device reports loaded (fm=%d)\n", fm); }
  else if (voltra::fitnessModeUnloaded(fm) && g_state.loaded) { g_state.loaded = false; Serial.printf("[link] device reports unloaded (fm=%d)\n", fm); }
}

void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  g_lastNotifyMs = millis();
  const voltra::Event e = voltra::decode(data, len);
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  switch (e.type) {
    case voltra::EventType::Settings:
      if (e.weightLbs >= 0) { g_state.weightLbs = e.weightLbs; ++g_state.confirmSeq; }
      if (e.mode >= 0) g_state.mode = e.mode;
      if (e.fitnessMode >= 0) applyFitnessMode(e.fitnessMode);
      g_handshakeEvent = true;
      break;
    case voltra::EventType::ModeConfirmed:
      if (e.mode >= 0) g_state.mode = e.mode;
      if (e.fitnessMode >= 0) applyFitnessMode(e.fitnessMode);
      g_handshakeEvent = true;
      break;
    case voltra::EventType::Battery:
      if (e.battery >= 0 && e.battery <= 100) {
        g_state.battery = e.battery;
      } else {
        Serial.printf("[link] odd battery frame (%d) len=%u:", e.battery, (unsigned)len);
        for (size_t i = 0; i < len && i < 24; ++i) Serial.printf(" %02x", data[i]);
        Serial.println();
      }
      break;
    case voltra::EventType::PerRep:
      if (e.repCount >= 0) g_state.reps = e.repCount;
      break;
    default:
      break;
  }
  xSemaphoreGive(g_mutex);
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    const bool byService = dev->isAdvertisingService(NimBLEUUID(voltra::SERVICE_UUID));
    const bool byName = dev->getName().rfind(voltra::DEVICE_NAME_PREFIX, 0) == 0;
    if (!byService && !byName) return;
    if (g_pinned.length() && g_pinned != String(dev->getAddress().toString().c_str())) return;
    g_targetAddr = dev->getAddress();
    g_foundTarget = true;
    Serial.printf("[link] found %s %s\n", dev->getName().c_str(), dev->getAddress().toString().c_str());
    NimBLEDevice::getScan()->stop();
  }
};

class ClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int reason) override {
    Serial.printf("[link] disconnected reason=%d\n", reason);
    g_disconnected = true;
  }
};

ScanCallbacks g_scanCb;
ClientCallbacks g_clientCb;

bool writeFrame(voltra::Frame f) {
  if (!g_write || !g_client || !g_client->isConnected()) return false;
  const uint32_t since = millis() - g_lastWriteMs;
  if (since < WRITE_GAP_MS) vTaskDelay(pdMS_TO_TICKS(WRITE_GAP_MS - since));
  bool ok = g_write->writeValue(f.data, f.len, g_write->canWrite());
  if (!ok) {
    vTaskDelay(pdMS_TO_TICKS(200));
    ok = g_write->writeValue(f.data, f.len, g_write->canWrite());
  }
  g_lastWriteMs = millis();
  return ok;
}

bool writeRaw(voltra::Frame f) {   // no spacing; used inside the init pair
  if (!g_write) return false;
  const bool ok = g_write->writeValue(f.data, f.len, g_write->canWrite());
  g_lastWriteMs = millis();
  return ok;
}

bool connectAndHandshake() {
  setState([](DeviceState &s) { s.link = LinkState::Connecting; s.loaded = false; s.reps = 0; });
  g_disconnected = false;
  g_handshakeEvent = false;
  if (!g_client) {
    g_client = NimBLEDevice::createClient();
    g_client->setClientCallbacks(&g_clientCb, false);
    g_client->setConnectTimeout(CONNECT_TIMEOUT_MS);
  }
  if (!g_client->connect(g_targetAddr)) { Serial.println("[link] connect failed"); return false; }
  NimBLERemoteService *svc = g_client->getService(voltra::SERVICE_UUID);
  if (!svc) { Serial.println("[link] service missing"); g_client->disconnect(); return false; }
  g_write = svc->getCharacteristic(voltra::WRITE_CHAR_UUID);
  NimBLERemoteCharacteristic *notify = svc->getCharacteristic(voltra::NOTIFY_CHAR_UUID);
  if (!g_write || !notify) { Serial.println("[link] characteristics missing"); g_client->disconnect(); return false; }
  if (!notify->subscribe(true, onNotify)) { Serial.println("[link] subscribe failed"); g_client->disconnect(); return false; }

  setState([](DeviceState &s) { s.link = LinkState::Handshaking; });
  const voltra::Frame auth = g_useFallbackAuth ? voltra::authFrameFallback() : voltra::authFrame();
  Serial.printf("[link] auth as %s\n", g_useFallbackAuth ? "iPhone (fallback)" : voltra::frames::AUTH_KNOB_NAME);
  if (!writeRaw(auth)) { Serial.println("[link] auth write failed"); g_client->disconnect(); return false; }
  vTaskDelay(pdMS_TO_TICKS(AUTH_WAIT_MS));
  for (size_t i = 0; i < voltra::INIT_COUNT; ++i) {
    if (!writeRaw(voltra::initFrame(i))) { Serial.println("[link] init write failed"); g_client->disconnect(); return false; }
    vTaskDelay(pdMS_TO_TICKS(INIT_GAP_MS));
  }
  if (!writeFrame(voltra::modeWeightTrainingFrame())) { Serial.println("[link] mode write failed"); g_client->disconnect(); return false; }

  const uint32_t deadline = millis() + HANDSHAKE_DEADLINE_MS;
  while (millis() < deadline && !g_handshakeEvent && !g_disconnected) vTaskDelay(pdMS_TO_TICKS(50));
  if (!g_handshakeEvent) {
    Serial.println("[link] handshake timeout");
    if (!g_useFallbackAuth) { g_useFallbackAuth = true; Serial.println("[link] will retry with the iPhone identity"); }
    g_client->disconnect();
    return false;
  }
  if (!writeFrame(voltra::telemetrySubscribeAllFrame())) Serial.println("[link] telemetry subscribe failed (continuing)");
  if (!writeFrame(voltra::readSettingsFrame())) Serial.println("[link] settings read failed (continuing)");

  g_prefs.begin("voltra", false);
  g_prefs.putString("addr", g_targetAddr.toString().c_str());
  g_prefs.end();
  setState([](DeviceState &s) { s.link = LinkState::Ready; });
  Serial.println("[link] ready");
  return true;
}

void servicePump() {
  Cmd cmd;
  while (!g_disconnected && g_client && g_client->isConnected()) {
    const int lbs = g_pendingWeight;
    if (lbs >= 0) {
      g_pendingWeight = -1;   // a newer target arriving during the write is picked up next round
      voltra::Frame f;
      if (voltra::weightFrame(lbs, f)) {
        const bool ok = writeFrame(f);
        Serial.printf("[link] set weight %d -> %s\n", lbs, ok ? "ok" : "FAIL");
        if (!ok) { g_disconnected = true; continue; }
      }
    }
    if (xQueueReceive(g_cmdQueue, &cmd, pdMS_TO_TICKS(20)) != pdTRUE) continue;
    switch (cmd.type) {
      case CmdType::Load: {
        bool ok = writeFrame(voltra::setupFrame());
        if (ok) { vTaskDelay(pdMS_TO_TICKS(LOAD_SETUP_GAP_MS)); ok = writeRaw(voltra::goFrame()); }
        Serial.printf("[link] load -> %s\n", ok ? "ok" : "FAIL");
        if (ok) setState([](DeviceState &s) { s.loaded = true; s.reps = 0; });
        else g_disconnected = true;
        break;
      }
      case CmdType::Unload: {
        const bool ok = writeFrame(voltra::stopFrame());
        Serial.printf("[link] unload -> %s\n", ok ? "ok" : "FAIL");
        setState([](DeviceState &s) { s.loaded = false; });
        if (!ok) g_disconnected = true;
        break;
      }
    }
  }
}

void linkTask(void *) {
  uint32_t backoffMs = 1000;
  while (true) {
    setState([](DeviceState &s) {
      s.link = g_consecutiveFailures >= APP_SUSPECT_AFTER_FAILURES ? LinkState::AppConnectedSuspect : LinkState::Scanning;
      s.loaded = false; s.reps = 0;
    });
    g_foundTarget = false;
    g_prefs.begin("voltra", true);
    g_pinned = g_prefs.getString("addr", "");
    g_prefs.end();
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_scanCb, false);
    scan->setActiveScan(true);
    scan->start(0, false, true);
    while (!g_foundTarget) vTaskDelay(pdMS_TO_TICKS(100));
    scan->stop();

    if (connectAndHandshake()) {
      g_consecutiveFailures = 0;
      backoffMs = 1000;
      servicePump();
      if (g_client && g_client->isConnected()) g_client->disconnect();
    } else {
      ++g_consecutiveFailures;
    }
    setState([](DeviceState &s) { s.loaded = false; s.link = LinkState::Scanning; });
    xQueueReset(g_cmdQueue);
    g_pendingWeight = -1;
    Serial.printf("[link] retry in %u ms\n", (unsigned)backoffMs);
    vTaskDelay(pdMS_TO_TICKS(backoffMs));
    backoffMs = backoffMs >= 8000 ? 8000 : backoffMs * 2;
  }
}

}  // namespace

void VoltraLink::begin() {
  g_mutex = xSemaphoreCreateMutex();
  g_cmdQueue = xQueueCreate(8, sizeof(Cmd));
  NimBLEDevice::init("VoltraKnob");
  NimBLEDevice::setMTU(MTU_REQUEST);
  xTaskCreatePinnedToCore(linkTask, "LINK", 8192, nullptr, 2, nullptr, 0);
}

DeviceState VoltraLink::state() {
  DeviceState copy;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  copy = g_state;
  xSemaphoreGive(g_mutex);
  return copy;
}

bool VoltraLink::requestWeight(int lbs) {
  g_pendingWeight = lbs;
  return true;
}

bool VoltraLink::requestLoad() {
  Cmd c{CmdType::Load, 0};
  return xQueueSend(g_cmdQueue, &c, 0) == pdTRUE;
}

bool VoltraLink::requestUnload() {
  Cmd c{CmdType::Unload, 0};
  return xQueueSendToFront(g_cmdQueue, &c, 0) == pdTRUE;   // unload jumps the queue
}

void VoltraLink::clearPinnedAddress() {
  g_prefs.begin("voltra", false);
  g_prefs.remove("addr");
  g_prefs.end();
}

}  // namespace vlink

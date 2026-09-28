#include "src/network/secure/command_channel.h"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "src/core/config/batched_nvs_write.h"
#include "src/core/security/secure_random.h"
#include "src/devices/device.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/video/local_camera/local_camera.h"

namespace command_channel {

namespace {

constexpr const char* kNamespace = "tab5_config";
// NVS keys are limited to 15 characters.
constexpr const char* kRecordKey = "cmd_pairing";
constexpr const char* kPanelLeaf = "/secure/panel";
constexpr const char* kBridgeLeaf = "/secure/bridge";
constexpr const char* kStatusLeaf = "/stat/secure";
constexpr uint32_t kHelloMinIntervalMs = 3000;
// Without an answer the hello is repeated after 10 s, 30 s, 60 s and then
// every five minutes, so an old Bridge sees very little extra traffic.
constexpr uint32_t kHelloRetryMs[] = {10000, 30000, 60000, 300000};
constexpr uint32_t kHoldMs = 5000;
constexpr uint32_t kLogIntervalMs = 30000;

// Session and pairing state. It exists only while pairing is set up and lives
// in PSRAM, so a panel without pairing keeps its internal RAM.
struct State {
  PairingState pairing;
  char code[kCodeLength + 1];
  Keys keys;
  bool has_session;
  uint8_t session[kSessionSize];
  uint32_t next_seq;
  ReplayWindow bridge_window;
  bool challenge_pending;
  char challenge[kSessionHexSize];
  uint32_t last_hello_ms;
  uint8_t hello_attempts;
  bool hello_requested;
  bool status_dirty;
  // One command waiting for the session: topic, NUL, payload.
  char* held;
  size_t held_topic_length;
  size_t held_length;
  uint32_t held_ms;
  uint32_t last_log_ms;
  uint32_t last_rekey_log_ms;
};

State* g_state = nullptr;
TaskHandle_t g_owner = nullptr;
bool g_loaded = false;
bool g_clear_status = false;

void* allocPreferPsram(size_t size) {
  void* block = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!block) block = heap_caps_malloc(size, MALLOC_CAP_8BIT);
  return block;
}

bool logDue(uint32_t* last_ms) {
  const uint32_t now = millis();
  if (*last_ms != 0 && static_cast<uint32_t>(now - *last_ms) < kLogIntervalMs) {
    return false;
  }
  *last_ms = now ? now : 1;
  return true;
}

void dropHeld() {
  if (!g_state || !g_state->held) return;
  ht_crypto::secureZero(g_state->held,
                        g_state->held_topic_length + 1 + g_state->held_length);
  heap_caps_free(g_state->held);
  g_state->held = nullptr;
  g_state->held_length = 0;
  g_state->held_topic_length = 0;
}

void releaseState() {
  if (!g_state) return;
  dropHeld();
  ht_crypto::secureZero(g_state, sizeof(*g_state));
  heap_caps_free(g_state);
  g_state = nullptr;
}

bool ensureState() {
  if (g_state) return true;
  g_state = static_cast<State*>(allocPreferPsram(sizeof(State)));
  if (!g_state) {
    Serial.println("[SecureCmd] Could not allocate pairing state");
    return false;
  }
  memset(g_state, 0, sizeof(*g_state));
  return true;
}

void resetSession() {
  g_state->has_session = false;
  ht_crypto::secureZero(g_state->session, sizeof(g_state->session));
  g_state->next_seq = 1;
  g_state->bridge_window = ReplayWindow();
  g_state->challenge_pending = false;
  g_state->hello_attempts = 0;
  g_state->hello_requested = true;
}

bool writeRecord(const PairingRecord* record) {
  Device::ScopedStorageWrite storage_write(BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.println("[SecureCmd] Could not open preferences");
    return false;
  }
  bool written = true;
  if (record) {
    written = prefs.putBytes(kRecordKey, record, sizeof(*record)) == sizeof(*record);
  } else {
    prefs.remove(kRecordKey);
  }
  const bool committed = BatchedNvsWrite::finish(prefs);
  return written && committed;
}

bool persist(PairingState pairing) {
  PairingRecord record = makeRecord(pairing, g_state->code);
  const bool saved = writeRecord(&record);
  ht_crypto::secureZero(&record, sizeof(record));
  return saved;
}

String topicFor(const char* leaf) {
  return mqttTopics.deviceBase() + leaf;
}

void publishStatus() {
  if (!networkManager.isMqttConnected()) return;
  const String topic = topicFor(kStatusLeaf);
  if (!g_state) {
    // An empty retained message removes an earlier status and stores nothing.
    networkManager.mqttEnqueuePublish(topic.c_str(), "", true);
    g_clear_status = false;
    return;
  }
  char payload[80];
  snprintf(payload, sizeof(payload), "{\"v\":1,\"state\":\"%s\",\"kid\":\"%s\"}",
           g_state->pairing == PairingState::Active ? "active" : "pending",
           g_state->keys.key_id);
  if (networkManager.mqttEnqueuePublish(topic.c_str(), payload, true)) {
    g_state->status_dirty = false;
  }
}

// Seals header + body for the Bridge. Returns a PSRAM block "topic\0envelope"
// and its lengths, or nullptr.
char* sealForBridge(const Header& header, const uint8_t* body, size_t body_length,
                    size_t* topic_length, size_t* envelope_length) {
  const String topic = topicFor(kPanelLeaf);
  const size_t work_size = 2 * (kMaxPlaintextLength + ht_crypto::kAeadTagSize);
  uint8_t* work = static_cast<uint8_t*>(allocPreferPsram(work_size));
  char* block = static_cast<char*>(
      allocPreferPsram(topic.length() + 1 + kMaxEnvelopeLength + 1));
  if (!work || !block) {
    if (work) heap_caps_free(work);
    if (block) heap_caps_free(block);
    Serial.println("[SecureCmd] Could not allocate a sealing buffer");
    return nullptr;
  }
  uint8_t* plaintext = work;
  uint8_t* scratch = work + kMaxPlaintextLength + ht_crypto::kAeadTagSize;
  const size_t plaintext_length =
      buildPlaintext(header, body, body_length, plaintext, kMaxPlaintextLength);
  uint8_t nonce[ht_crypto::kAeadNonceSize];
  secure_random::fill(nonce, sizeof(nonce));
  memcpy(block, topic.c_str(), topic.length() + 1);
  const size_t sealed_length =
      plaintext_length
          ? sealEnvelope(g_state->keys.panel_to_bridge, g_state->keys.key_id,
                         nonce, topic.c_str(), plaintext, plaintext_length,
                         scratch, block + topic.length() + 1,
                         kMaxEnvelopeLength + 1)
          : 0;
  ht_crypto::secureZero(work, work_size);
  heap_caps_free(work);
  if (!sealed_length) {
    heap_caps_free(block);
    return nullptr;
  }
  *topic_length = topic.length();
  *envelope_length = sealed_length;
  return block;
}

void sendHello() {
  if (!g_state || !networkManager.isMqttConnected()) return;
  const uint32_t now = millis();
  if (g_state->last_hello_ms != 0 &&
      static_cast<uint32_t>(now - g_state->last_hello_ms) < kHelloMinIntervalMs) {
    return;
  }
  uint8_t challenge[kChallengeSize];
  secure_random::fill(challenge, sizeof(challenge));
  ht_crypto::hexEncode(challenge, sizeof(challenge), g_state->challenge,
                       sizeof(g_state->challenge));
  Header header;
  header.type = MessageType::Hello;
  strcpy(header.name, g_state->challenge);
  size_t topic_length = 0, envelope_length = 0;
  char* block = sealForBridge(header, nullptr, 0, &topic_length, &envelope_length);
  if (!block) return;
  const bool queued = networkManager.mqttEnqueuePublishPriority(
      block, block + topic_length + 1, false);
  heap_caps_free(block);
  g_state->last_hello_ms = now ? now : 1;
  g_state->hello_requested = false;
  g_state->challenge_pending = queued;
  if (g_state->hello_attempts < 0xff) ++g_state->hello_attempts;
  Serial.printf("[SecureCmd] Session request sent (%s, attempt %u)\n",
                queued ? "queued" : "queue-full",
                static_cast<unsigned>(g_state->hello_attempts));
}

void flushHeld() {
  if (!g_state || !g_state->held) return;
  char* held = g_state->held;
  const size_t topic_length = g_state->held_topic_length;
  const size_t length = g_state->held_length;
  g_state->held = nullptr;
  const bool queued = networkManager.mqttEnqueuePublish(
      held, reinterpret_cast<const uint8_t*>(held + topic_length + 1), length,
      false);
  Serial.printf("[SecureCmd] Held command sent after the session (%s)\n",
                queued ? "queued" : "queue-full");
  ht_crypto::secureZero(held, topic_length + 1 + length);
  heap_caps_free(held);
}

void handleSession(const Header& header) {
  if (!g_state->challenge_pending || !header.has_session ||
      strcmp(header.name, g_state->challenge) != 0) {
    // A session answer to another or an old request (possibly replayed).
    if (logDue(&g_state->last_log_ms)) {
      Serial.println("[SecureCmd] Session answer for an old request ignored");
    }
    return;
  }
  memcpy(g_state->session, header.session, kSessionSize);
  g_state->has_session = true;
  g_state->next_seq = 1;
  g_state->bridge_window = ReplayWindow();
  g_state->challenge_pending = false;
  g_state->hello_attempts = 0;
  if (g_state->pairing != PairingState::Active) {
    g_state->pairing = PairingState::Active;
    if (!persist(PairingState::Active)) {
      Serial.println("[SecureCmd] Could not store the active pairing");
    }
    Serial.println("[SecureCmd] Pairing confirmed by the Bridge; commands are encrypted");
  }
  g_state->status_dirty = true;
  publishStatus();
  Serial.println("[SecureCmd] Bridge session established");
  flushHeld();
}

// Session and replay check shared by every numbered Bridge message.
bool acceptInSession(const Header& header) {
  if (!g_state->has_session || !header.has_session ||
      !ht_crypto::equalConstantTime(header.session, g_state->session,
                                    kSessionSize)) {
    g_state->hello_requested = true;
    return false;
  }
  if (!acceptSequence(g_state->bridge_window, header.seq)) {
    if (logDue(&g_state->last_log_ms)) {
      Serial.println("[SecureCmd] Repeated Bridge message ignored");
    }
    return false;
  }
  return true;
}

// Tells the Bridge that this panel removes the pairing. Needs the current
// session, so the message is numbered and cannot be replayed.
bool sendUnpair() {
  if (!g_state || !g_state->has_session || !networkManager.isMqttConnected()) {
    return false;
  }
  Header header;
  header.type = MessageType::Unpair;
  header.has_session = true;
  memcpy(header.session, g_state->session, kSessionSize);
  header.seq = g_state->next_seq++;
  size_t topic_length = 0, envelope_length = 0;
  char* block = sealForBridge(header, nullptr, 0, &topic_length, &envelope_length);
  if (!block) return false;
  const bool queued = networkManager.mqttEnqueuePublishPriority(
      block, block + topic_length + 1, false);
  heap_caps_free(block);
  return queued;
}

bool turnOff(bool tell_bridge, bool* bridge_notified) {
  if (bridge_notified) *bridge_notified = false;
  if (!writeRecord(nullptr)) {
    Serial.println("[SecureCmd] Could not remove the pairing");
    return false;
  }
  const bool notified = tell_bridge && sendUnpair();
  if (bridge_notified) *bridge_notified = notified;
  releaseState();
  g_clear_status = true;
  publishStatus();
  // Replace the signed retained announcement with an unsigned one.
  if (networkManager.isMqttConnected()) networkManager.publishBridgeConfig();
  if (!tell_bridge) {
    Serial.println("[SecureCmd] Pairing removed by the Bridge; commands are unencrypted again");
  } else if (notified) {
    Serial.println("[SecureCmd] Command encryption turned off; the Bridge removes its code too");
  } else {
    Serial.println("[SecureCmd] Command encryption turned off; no Bridge session, "
                   "remove the code in the Bridge as well");
  }
  return true;
}

void handleUnpair(const Header& header) {
  if (!acceptInSession(header)) return;
  turnOff(false, nullptr);
}

void handleData(const Header& header, const uint8_t* body, size_t length) {
  if (!acceptInSession(header)) return;
  if (strcmp(header.name, "camera") == 0) {
    char* text = static_cast<char*>(allocPreferPsram(length + 1));
    if (!text) return;
    memcpy(text, body, length);
    text[length] = '\0';
    camera_popup_handle_mqtt_status(text);
    heap_caps_free(text);
  } else if (strcmp(header.name, "local_camera") == 0) {
    local_camera::handleCommandPayload(body, length);
  }
}

}  // namespace

void SealedPublish::reset(char* block, size_t topic_length, size_t payload_length) {
  if (block_) heap_caps_free(block_);
  block_ = block;
  topic_length_ = topic_length;
  payload_length_ = payload_length;
}

SealedPublish::~SealedPublish() {
  if (block_) heap_caps_free(block_);
}

void begin() {
  if (g_loaded) return;
  g_loaded = true;
  g_owner = xTaskGetCurrentTaskHandle();
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  PairingRecord record{};
  const bool read =
      prefs.getBytesLength(kRecordKey) == sizeof(record) &&
      prefs.getBytes(kRecordKey, &record, sizeof(record)) == sizeof(record);
  prefs.end();
  if (!read) return;
  PairingState pairing = PairingState::Off;
  char code[kCodeLength + 1];
  if (!applyRecord(record, pairing, code) || !ensureState()) {
    ht_crypto::secureZero(&record, sizeof(record));
    Serial.println("[SecureCmd] Stored pairing is invalid; encryption stays off");
    return;
  }
  ht_crypto::secureZero(&record, sizeof(record));
  memcpy(g_state->code, code, sizeof(g_state->code));
  deriveKeys(g_state->code, g_state->keys);
  g_state->pairing = pairing;
  resetSession();
  g_state->status_dirty = true;
  Serial.printf("[SecureCmd] Pairing %s (key %s)\n",
                pairing == PairingState::Active ? "active" : "pending",
                g_state->keys.key_id);
}

PairingState state() {
  return g_state ? g_state->pairing : PairingState::Off;
}

bool sessionReady() {
  return g_state && g_state->has_session;
}

bool createCode() {
  begin();
  uint8_t random[kCodeLength];
  secure_random::fill(random, sizeof(random));
  char code[kCodeLength + 1];
  generateCode(random, code);
  ht_crypto::secureZero(random, sizeof(random));
  const bool had_state = g_state != nullptr;
  if (!ensureState()) return false;
  State previous;
  if (had_state) memcpy(&previous, g_state, sizeof(previous));
  dropHeld();
  memcpy(g_state->code, code, sizeof(g_state->code));
  g_state->pairing = PairingState::Pending;
  if (!persist(PairingState::Pending)) {
    if (had_state) {
      memcpy(g_state, &previous, sizeof(previous));
      g_state->held = nullptr;
    } else {
      releaseState();
    }
    ht_crypto::secureZero(&previous, sizeof(previous));
    Serial.println("[SecureCmd] Could not store the new pairing code");
    return false;
  }
  ht_crypto::secureZero(&previous, sizeof(previous));
  deriveKeys(g_state->code, g_state->keys);
  resetSession();
  g_state->status_dirty = true;
  if (networkManager.isMqttConnected()) {
    networkManager.mqttEnqueueSubscribe(topicFor(kBridgeLeaf).c_str());
    publishStatus();
    // The retained announcement is signed with the new code from now on.
    networkManager.publishBridgeConfig();
  }
  Serial.printf("[SecureCmd] New pairing code created (key %s)\n",
                g_state->keys.key_id);
  return true;
}

bool disable(bool* bridge_notified) {
  begin();
  return turnOff(true, bridge_notified);
}

char* signAnnouncement(const char* topic, const char* payload, size_t length) {
  if (!g_state || !topic || !payload ||
      xTaskGetCurrentTaskHandle() != g_owner) {
    return nullptr;
  }
  const size_t size = length + kAnnouncementSignatureOverhead + 1;
  char* out = static_cast<char*>(allocPreferPsram(size));
  if (!out) return nullptr;
  if (command_channel::signAnnouncement(g_state->keys.announce, topic, payload,
                                        length, out, size) == 0) {
    heap_caps_free(out);
    return nullptr;
  }
  return out;
}

bool displayCode(char out[kCodeDisplaySize]) {
  if (!out) return false;
  out[0] = '\0';
  if (!g_state) return false;
  formatCode(g_state->code, out);
  return true;
}

void onMqttConnected() {
  begin();
  if (!g_state) {
    if (g_clear_status) publishStatus();
    return;
  }
  networkManager.mqttEnqueueSubscribe(topicFor(kBridgeLeaf).c_str());
  // The Bridge may have restarted; ask for a fresh session.
  resetSession();
  g_state->last_hello_ms = 0;
  g_state->status_dirty = true;
  publishStatus();
}

void service() {
  if (!g_state) return;
  const uint32_t now = millis();
  if (g_state->held &&
      static_cast<uint32_t>(now - g_state->held_ms) >= kHoldMs) {
    dropHeld();
    Serial.println("[SecureCmd] Command dropped: no Bridge session");
  }
  if (!networkManager.isMqttConnected()) return;
  if (g_state->status_dirty) publishStatus();
  if (g_state->has_session) return;
  const uint8_t attempts = g_state->hello_attempts;
  const size_t retry_index =
      attempts == 0 ? 0
                    : (attempts - 1 < sizeof(kHelloRetryMs) / sizeof(kHelloRetryMs[0])
                           ? attempts - 1
                           : sizeof(kHelloRetryMs) / sizeof(kHelloRetryMs[0]) - 1);
  const bool retry_due =
      attempts == 0 ||
      static_cast<uint32_t>(now - g_state->last_hello_ms) >= kHelloRetryMs[retry_index];
  if (g_state->hello_requested || retry_due) sendHello();
}

bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length) {
  if (!topic) return false;
  const String bridge_topic = topicFor(kBridgeLeaf);
  if (strcmp(topic, bridge_topic.c_str()) != 0) return false;
  if (!g_state || !payload || length == 0) return true;

  const size_t work_size = 2 * kMaxPlaintextLength + ht_crypto::kAeadTagSize;
  uint8_t* work = static_cast<uint8_t*>(allocPreferPsram(work_size));
  if (!work) return true;
  uint8_t* plaintext = work;
  uint8_t* scratch = work + kMaxPlaintextLength;
  size_t plaintext_length = 0;
  const OpenResult result = openEnvelope(
      g_state->keys.bridge_to_panel, g_state->keys.key_id, topic,
      reinterpret_cast<const char*>(payload), length, scratch, plaintext,
      &plaintext_length);
  if (result != OpenResult::Ok) {
    if (logDue(&g_state->last_log_ms)) {
      Serial.printf("[SecureCmd] Bridge message ignored (%s)\n",
                    result == OpenResult::OtherKey ? "other pairing code"
                    : result == OpenResult::Rejected ? "authentication failed"
                                                     : "malformed");
    }
    ht_crypto::secureZero(work, work_size);
    heap_caps_free(work);
    return true;
  }
  Header header;
  const uint8_t* body = nullptr;
  size_t body_length = 0;
  if (parsePlaintext(plaintext, plaintext_length, header, &body, &body_length)) {
    switch (header.type) {
      case MessageType::Session:
        handleSession(header);
        break;
      case MessageType::Rekey:
        // The Bridge is listening now; restart the backoff so a hello lost
        // during its restart is repeated after 10 s instead of minutes.
        g_state->hello_requested = true;
        g_state->hello_attempts = 0;
        if (logDue(&g_state->last_rekey_log_ms)) {
          Serial.println("[SecureCmd] Bridge asked for a new session");
        }
        break;
      case MessageType::Data:
        handleData(header, body, body_length);
        break;
      case MessageType::Unpair:
        handleUnpair(header);
        break;
      default:
        break;
    }
  } else if (logDue(&g_state->last_log_ms)) {
    Serial.println("[SecureCmd] Bridge message ignored (unreadable header)");
  }
  ht_crypto::secureZero(work, work_size);
  heap_caps_free(work);
  return true;
}

bool blocksPlaintext(const char* topic) {
  if (!topic || !g_state || g_state->pairing != PairingState::Active) return false;
  const char* camera_status = mqttTopics.topic(TopicKey::CAMERA_STAT);
  const bool blocked = (camera_status && strcmp(topic, camera_status) == 0) ||
                       local_camera::isCommandTopic(topic);
  if (blocked && logDue(&g_state->last_log_ms)) {
    Serial.printf("[SecureCmd] Unencrypted %s ignored while pairing is active\n", topic);
  }
  return blocked;
}

OutboundResult prepareOutbound(const char* topic, const uint8_t* payload,
                               size_t length, SealedPublish* sealed) {
  if (!g_state || g_state->pairing != PairingState::Active || !sealed) {
    return OutboundResult::Plain;
  }
  const String& base = mqttTopics.deviceBase();
  const char* leaf = sealedCommandLeaf(topic, base.c_str(), base.length());
  if (!leaf) return OutboundResult::Plain;
  if (xTaskGetCurrentTaskHandle() != g_owner || length > kMaxBodyLength ||
      (!payload && length)) {
    Serial.printf("[SecureCmd] Command %s dropped (%s)\n", leaf,
                  length > kMaxBodyLength ? "too large" : "foreign task");
    return OutboundResult::Dropped;
  }
  if (!g_state->has_session) {
    // Keep only the newest command until the Bridge session exists.
    dropHeld();
    const size_t topic_length = strlen(topic);
    char* held = static_cast<char*>(allocPreferPsram(topic_length + 1 + length + 1));
    if (!held) return OutboundResult::Dropped;
    memcpy(held, topic, topic_length + 1);
    if (length) memcpy(held + topic_length + 1, payload, length);
    held[topic_length + 1 + length] = '\0';
    g_state->held = held;
    g_state->held_topic_length = topic_length;
    g_state->held_length = length;
    g_state->held_ms = millis();
    g_state->hello_requested = true;
    sendHello();
    return OutboundResult::Held;
  }

  Header header;
  header.type = MessageType::Command;
  header.has_session = true;
  memcpy(header.session, g_state->session, kSessionSize);
  header.seq = g_state->next_seq++;
  strcpy(header.name, leaf);
  if (g_state->next_seq == 0xffffffffUL) resetSession();
  size_t topic_length = 0, envelope_length = 0;
  char* block = sealForBridge(header, payload, length, &topic_length, &envelope_length);
  if (!block) return OutboundResult::Dropped;
  sealed->reset(block, topic_length, envelope_length);
  return OutboundResult::Sealed;
}

}  // namespace command_channel

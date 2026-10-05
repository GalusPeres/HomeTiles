#include "src/network/secure/command_channel.h"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "src/core/config/batched_nvs_write.h"
#include "src/core/security/secure_random.h"
#include "src/core/security/x25519.h"
#include "src/devices/device.h"
#include "src/network/bridge/entity_search.h"
#include "src/network/link/link_config.h"
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
constexpr const char* kPairPanelLeaf = "/pair/panel";
constexpr const char* kPairBridgeLeaf = "/pair/bridge";
constexpr uint32_t kHelloMinIntervalMs = 3000;
// Without an answer the hello is repeated after 10 s, 30 s, 60 s and then
// every five minutes, so an old Bridge sees very little extra traffic.
constexpr uint32_t kHelloRetryMs[] = {10000, 30000, 60000, 300000};
constexpr uint32_t kHoldMs = 5000;
constexpr uint32_t kLogIntervalMs = 30000;
// Pairing: an old Bridge never answers; every attempt ends after 120 s; a
// confirmation is repeated until the other side's arrives (QoS 0).
constexpr uint32_t kPairNoAnswerMs = 15000;
constexpr uint32_t kPairTimeoutMs = 120000;
constexpr uint32_t kPairConfirmRepeatMs = 2000;
constexpr uint32_t kClearRetryMs = 5000;
constexpr size_t kMaxBaseLength = 128;

// Session and pairing state. It exists only while the panel is paired and
// lives in PSRAM, so a panel without pairing keeps its internal RAM.
struct State {
  PairingState pairing;
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

// One number-comparison pairing attempt (contract v2), also in PSRAM. Every
// attempt uses a fresh key pair, nonce and id.
struct Attempt {
  PairingPhase phase;
  char id[kPairIdHexSize];
  char base[kMaxBaseLength + 1];
  uint8_t secret[x25519::kKeySize];
  uint8_t pk_p[kPairKeySize];
  uint8_t n_p[kPairNonceSize];
  bool have_commit;
  uint8_t pk_b[kPairKeySize];
  uint8_t commit[ht_crypto::kSha256Size];
  bool have_number;
  uint32_t number;
  uint8_t transcript[ht_crypto::kSha256Size];
  uint8_t key[kPairKeySize];
  uint8_t confirm_panel[ht_crypto::kSha256Size];
  uint8_t confirm_bridge[ht_crypto::kSha256Size];
  bool local_confirmed;
  bool peer_confirmed;
  bool start_sent;
  uint32_t started_ms;
  uint32_t last_confirm_ms;
  uint32_t last_log_ms;
};

State* g_state = nullptr;
Attempt* g_attempt = nullptr;
// Shows the pairing view when the panel pairs by itself after link setup.
void (*g_pairing_prompt)() = nullptr;
// Pair on a panel without a direct link: discoverable until this millis()
// value (0 = closed); see linkWindowOpen().
constexpr uint32_t kLinkWindowMs = 120000;
uint32_t g_link_window_until = 0;
// Home Assistant sent its address: the link connects in pair mode, and the
// view shows "asking" until the attempt starts (millis(), 0 = none).
constexpr uint32_t kLinkSetupWaitMs = 60000;
uint32_t g_link_setup_until = 0;

void closeLinkWindow() {
  if (!g_link_window_until) return;
  g_link_window_until = 0;
  networkManager.setPairingAdvertised(false);
}

bool linkSetupWaiting() {
  if (!g_link_setup_until) return false;
  if (static_cast<int32_t>(millis() - g_link_setup_until) >= 0) {
    g_link_setup_until = 0;
    return false;
  }
  return true;
}

// Expires the window on the loop task.
bool linkWindowActive() {
  if (!g_link_window_until) return false;
  if (static_cast<int32_t>(millis() - g_link_window_until) >= 0) {
    Serial.println("[SecureCmd] Pairing window closed without Home Assistant");
    closeLinkWindow();
    return false;
  }
  return true;
}
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

String topicFor(const char* leaf) {
  return mqttTopics.deviceBase() + leaf;
}

void publishStatus() {
  if (!networkManager.isMqttConnected()) return;
  const String topic = topicFor(kStatusLeaf);
  if (!g_state) {
    // An empty retained message removes an earlier status and stores nothing.
    // Only a queued clear counts; otherwise service() tries again.
    if (networkManager.mqttEnqueuePublish(topic.c_str(), "", true)) g_clear_status = false;
    return;
  }
  char payload[80];
  snprintf(payload, sizeof(payload), "{\"v\":1,\"state\":\"active\",\"kid\":\"%s\"}",
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

// Without its key the direct link is useless: the panel forgets the Bridge
// and is a new panel again, without a restart; Pair makes it discoverable.
// The worker sends queued messages before it leaves the link.
void forgetLink(const char* why) {
  if (!networkManager.linkConfigured()) return;
  link_config::clear();
  networkManager.leaveLink();
  Serial.printf("[SecureCmd] Direct link removed (%s); the panel is new again\n", why);
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
  networkManager.setLinkPairing(nullptr, nullptr);
  forgetLink("pairing removed");
  g_clear_status = true;
  publishStatus();
  // Replace the signed retained announcement with an unsigned one.
  if (networkManager.isMqttConnected()) networkManager.publishBridgeConfig();
  if (!tell_bridge) {
    Serial.println("[SecureCmd] Pairing removed by the Bridge; commands are unencrypted again");
  } else if (notified) {
    Serial.println("[SecureCmd] Pairing removed; the Bridge removes it too");
  } else {
    Serial.println("[SecureCmd] Pairing removed; no Bridge session, "
                   "remove it in Home Assistant as well");
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
  } else if (strcmp(header.name, "entities") == 0) {
    entity_search::handleAnswer(body, length);
  } else if (strcmp(header.name, "tiles") == 0) {
    entity_search::handleDeclarationAck(body, length);
  }
}

// ---------------------------------------------------------------------------
// Number-comparison pairing (contract v2)

bool attemptRunning() {
  return g_attempt && (g_attempt->phase == PairingPhase::Asking ||
                       g_attempt->phase == PairingPhase::Compare ||
                       g_attempt->phase == PairingPhase::Confirmed);
}

void wipeAttemptSecrets() {
  if (!g_attempt) return;
  ht_crypto::secureZero(g_attempt->secret, sizeof(g_attempt->secret));
  ht_crypto::secureZero(g_attempt->key, sizeof(g_attempt->key));
}

void releaseAttempt() {
  if (!g_attempt) return;
  ht_crypto::secureZero(g_attempt, sizeof(*g_attempt));
  heap_caps_free(g_attempt);
  g_attempt = nullptr;
}

String pairTopic(const char* leaf) {
  return String(g_attempt->base) + leaf;
}

bool publishPair(const char* type, const char* field, const uint8_t* bytes,
                 size_t bytes_length, const char* text) {
  if (!g_attempt || !networkManager.isMqttConnected()) return false;
  char message[kMaxPairMessageLength + 1];
  const size_t length = buildPairMessage(type, g_attempt->id, field, bytes,
                                         bytes_length, text, message, sizeof(message));
  if (!length) return false;
  return networkManager.mqttEnqueuePublishPriority(pairTopic(kPairPanelLeaf).c_str(),
                                                   message, false);
}

// Ends a running attempt with a result for the Security view. The reason, if
// any, tells the Bridge why (it is unauthenticated and only for display).
// The attempt ended (paired or not): a direct link leaves pair mode, and the
// panel does not pair again by itself after its next start.
void linkPairingEnded() {
  if (!networkManager.linkConfigured()) return;
  networkManager.requestLinkPairing(false);
  link_config::setPairRequested(false);
}

void endAttempt(PairingPhase phase, const char* reason) {
  if (!g_attempt) return;
  if (reason) publishPair("abort", "r", nullptr, 0, reason);
  g_attempt->phase = phase;
  wipeAttemptSecrets();
  linkPairingEnded();
  // Rejected, cancelled or timed out after Home Assistant sent its address:
  // keeping the address without a key would make the next Pair try the link
  // instead of announcing the panel, and a new panel would hang there.
  if (!g_state) forgetLink("pairing ended without a key");
}

void completePairing() {
  PairingRecord record = makeRecord(g_attempt->key);
  const bool saved = writeRecord(&record);
  ht_crypto::secureZero(&record, sizeof(record));
  if (!saved || !ensureState()) {
    Serial.println("[SecureCmd] Pairing: could not store the key");
    endAttempt(PairingPhase::Failed, "error");
    return;
  }
  deriveKeys(g_attempt->key, g_state->keys);
  // A direct link reconnects encrypted with the new key.
  networkManager.setLinkPairing(g_attempt->key, g_state->keys.key_id);
  linkPairingEnded();
  g_state->pairing = PairingState::Active;
  resetSession();
  g_state->status_dirty = true;
  g_clear_status = false;
  // Stays until 120 s after start to answer a repeated confirmation.
  g_attempt->phase = PairingPhase::Done;
  wipeAttemptSecrets();
  if (networkManager.isMqttConnected()) {
    networkManager.mqttEnqueueSubscribe(topicFor(kBridgeLeaf).c_str());
    publishStatus();
    // The retained announcement is signed from now on.
    networkManager.publishBridgeConfig();
  }
  Serial.printf("[SecureCmd] Paired with the Bridge (key %s); commands are encrypted\n",
                g_state->keys.key_id);
}

void handlePairMessage(const uint8_t* payload, size_t length) {
  if (!g_attempt || !payload) return;
  PairMessage message;
  // Malformed messages and other attempts are ignored without a trace.
  if (!parsePairMessage(reinterpret_cast<const char*>(payload), length, message) ||
      strcmp(message.id, g_attempt->id) != 0) {
    return;
  }
  Attempt& attempt = *g_attempt;
  switch (message.type) {
    case PairType::Commit:
      // Only the first commit of an attempt counts.
      if (attempt.phase != PairingPhase::Asking || attempt.have_commit) return;
      memcpy(attempt.pk_b, message.pk, sizeof(attempt.pk_b));
      memcpy(attempt.commit, message.c, sizeof(attempt.commit));
      attempt.have_commit = true;
      publishPair("nonce", "n", attempt.n_p, sizeof(attempt.n_p), nullptr);
      Serial.println("[SecureCmd] Pairing: the Bridge answered");
      break;
    case PairType::Nonce: {
      if (attempt.phase != PairingPhase::Asking || !attempt.have_commit ||
          attempt.have_number) {
        return;
      }
      uint8_t expected[ht_crypto::kSha256Size];
      pairingCommit(attempt.pk_b, attempt.pk_p, message.n, expected);
      if (!ht_crypto::equalConstantTime(expected, attempt.commit, sizeof(expected))) {
        // Another sender, or a nonce the Bridge did not commit to.
        if (logDue(&attempt.last_log_ms)) {
          Serial.println("[SecureCmd] Pairing: nonce does not match the commitment; ignored");
        }
        return;
      }
      uint8_t shared[kPairKeySize];
      if (!x25519::sharedSecret(attempt.secret, attempt.pk_b, shared) ||
          !pairingTranscript(attempt.base, attempt.pk_p, attempt.pk_b, attempt.n_p,
                             message.n, attempt.transcript)) {
        ht_crypto::secureZero(shared, sizeof(shared));
        Serial.println("[SecureCmd] Pairing: key agreement failed");
        endAttempt(PairingPhase::Failed, "error");
        return;
      }
      pairingKey(shared, attempt.transcript, attempt.key);
      ht_crypto::secureZero(shared, sizeof(shared));
      ht_crypto::secureZero(attempt.secret, sizeof(attempt.secret));
      pairingConfirmation(attempt.key, attempt.transcript, true, attempt.confirm_panel);
      pairingConfirmation(attempt.key, attempt.transcript, false, attempt.confirm_bridge);
      attempt.number = pairingNumber(attempt.transcript);
      attempt.have_number = true;
      attempt.phase = PairingPhase::Compare;
      Serial.println("[SecureCmd] Pairing: compare the number on the panel and in Home Assistant");
      break;
    }
    case PairType::Confirm:
      if (!attempt.have_number) return;
      if (!ht_crypto::equalConstantTime(message.m, attempt.confirm_bridge,
                                        sizeof(attempt.confirm_bridge))) {
        if (logDue(&attempt.last_log_ms)) {
          Serial.println("[SecureCmd] Pairing: invalid confirmation ignored");
        }
        return;
      }
      if (attempt.phase == PairingPhase::Done) {
        // The Bridge missed ours; repeat it until the attempt window ends.
        publishPair("confirm", "m", attempt.confirm_panel,
                    sizeof(attempt.confirm_panel), nullptr);
        return;
      }
      if (attempt.phase != PairingPhase::Compare &&
          attempt.phase != PairingPhase::Confirmed) {
        return;
      }
      attempt.peer_confirmed = true;
      if (attempt.local_confirmed) completePairing();
      break;
    case PairType::Abort:
      // Unauthenticated: it ends a running attempt but never undoes a pairing.
      if (!attemptRunning()) return;
      if (strcmp(message.reason, "paired") == 0) {
        endAttempt(PairingPhase::AlreadyPaired, nullptr);
      } else if (strcmp(message.reason, "busy") == 0 ||
                 strcmp(message.reason, "rate") == 0) {
        endAttempt(PairingPhase::Busy, nullptr);
      } else if (strcmp(message.reason, "rejected") == 0) {
        endAttempt(PairingPhase::Rejected, nullptr);
      } else {
        endAttempt(PairingPhase::Failed, nullptr);
      }
      Serial.printf("[SecureCmd] Pairing ended by the Bridge (%s)\n",
                    message.reason[0] ? message.reason : "no reason");
      break;
    case PairType::Start:
      // Only the panel starts an attempt.
      break;
  }
}

void servicePairing() {
  if (!g_attempt) return;
  Attempt& attempt = *g_attempt;
  const uint32_t now = millis();
  const uint32_t age = static_cast<uint32_t>(now - attempt.started_ms);
  if (attempt.phase == PairingPhase::Done) {
    if (age >= kPairTimeoutMs) releaseAttempt();
    return;
  }
  if (!attemptRunning()) return;  // A result waits for the Security view.
  // The start goes out once; a repeated start would look like a second attempt.
  if (!attempt.start_sent && networkManager.isMqttConnected()) {
    attempt.start_sent = publishPair("start", "pk", attempt.pk_p,
                                     sizeof(attempt.pk_p), nullptr);
  }
  if (attempt.phase == PairingPhase::Asking && !attempt.have_commit &&
      age >= kPairNoAnswerMs) {
    Serial.println("[SecureCmd] Pairing: no answer from the Bridge; "
                   "update the HomeTiles Bridge to encrypt");
    endAttempt(PairingPhase::NoAnswer, "timeout");
    return;
  }
  if (age >= kPairTimeoutMs) {
    Serial.println("[SecureCmd] Pairing timed out");
    endAttempt(PairingPhase::Failed, "timeout");
    return;
  }
  if (attempt.phase == PairingPhase::Confirmed &&
      static_cast<uint32_t>(now - attempt.last_confirm_ms) >= kPairConfirmRepeatMs) {
    publishPair("confirm", "m", attempt.confirm_panel, sizeof(attempt.confirm_panel),
                nullptr);
    attempt.last_confirm_ms = now ? now : 1;
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
  const size_t stored = prefs.getBytesLength(kRecordKey);
  PairingRecord record{};
  const bool read = stored == sizeof(record) &&
                    prefs.getBytes(kRecordKey, &record, sizeof(record)) == sizeof(record);
  prefs.end();
  if (stored == kLegacyRecordSize) {
    // A typed pairing code from a v1 beta: drop it and run unencrypted.
    writeRecord(nullptr);
    g_clear_status = true;
    Serial.println("[SecureCmd] Old pairing code discarded; pair the panel again");
    return;
  }
  if (!read) return;
  uint8_t key[kPairKeySize];
  if (!applyRecord(record, key) || !ensureState()) {
    ht_crypto::secureZero(&record, sizeof(record));
    ht_crypto::secureZero(key, sizeof(key));
    Serial.println("[SecureCmd] Stored pairing is invalid; encryption stays off");
    return;
  }
  ht_crypto::secureZero(&record, sizeof(record));
  deriveKeys(key, g_state->keys);
  // The direct link needs K itself for its session keys.
  networkManager.setLinkPairing(key, g_state->keys.key_id);
  ht_crypto::secureZero(key, sizeof(key));
  g_state->pairing = PairingState::Active;
  resetSession();
  g_state->status_dirty = true;
  Serial.printf("[SecureCmd] Pairing active (key %s)\n", g_state->keys.key_id);
}

PairingState state() {
  return g_state ? g_state->pairing : PairingState::Off;
}

bool sessionReady() {
  return g_state && g_state->has_session;
}

bool startPairing() {
  begin();
  if (g_state) return false;  // Unpair first.
  if (attemptRunning()) return true;
  if (!networkManager.linkConfigured()) {
    // No Bridge address yet: be discoverable for Home Assistant, which sends
    // its address when the panel is added (POST /api/link). The panel then
    // connects and pairs over the link (docs-dev/bridge-link.md, Setup).
    // A finished attempt (kept for two minutes) would hide the window from
    // pairingPhase(), and the view would fall back to the overview.
    releaseAttempt();
    const uint32_t until = millis() + kLinkWindowMs;
    g_link_window_until = until ? until : 1;
    networkManager.setPairingAdvertised(true);
    Serial.println("[SecureCmd] Pair pressed: discoverable for Home Assistant for two minutes");
    return true;
  }
  releaseAttempt();
  const String& base = mqttTopics.deviceBase();
  if (base.length() == 0 || base.length() > kMaxBaseLength) {
    Serial.println("[SecureCmd] Pairing: unusable base topic");
    return false;
  }
  if (!x25519::selfTest()) {
    Serial.println("[SecureCmd] Pairing: X25519 self-test failed");
    return false;
  }
  g_attempt = static_cast<Attempt*>(allocPreferPsram(sizeof(Attempt)));
  if (!g_attempt) {
    Serial.println("[SecureCmd] Could not allocate the pairing attempt");
    return false;
  }
  memset(g_attempt, 0, sizeof(*g_attempt));
  secure_random::fill(g_attempt->secret, sizeof(g_attempt->secret));
  secure_random::fill(g_attempt->n_p, sizeof(g_attempt->n_p));
  uint8_t id[kPairIdSize];
  secure_random::fill(id, sizeof(id));
  ht_crypto::hexEncode(id, sizeof(id), g_attempt->id, sizeof(g_attempt->id));
  if (!x25519::publicKey(g_attempt->secret, g_attempt->pk_p)) {
    releaseAttempt();
    Serial.println("[SecureCmd] Pairing: could not create a key pair");
    return false;
  }
  memcpy(g_attempt->base, base.c_str(), base.length() + 1);
  g_attempt->phase = PairingPhase::Asking;
  g_link_setup_until = 0;
  const uint32_t now = millis();
  g_attempt->started_ms = now ? now : 1;
  // A direct link connects in pair mode; the start goes out once it is up.
  if (networkManager.linkConfigured()) networkManager.requestLinkPairing(true);
  if (networkManager.isMqttConnected()) {
    networkManager.mqttEnqueueSubscribe(pairTopic(kPairBridgeLeaf).c_str());
    g_attempt->start_sent = publishPair("start", "pk", g_attempt->pk_p,
                                        sizeof(g_attempt->pk_p), nullptr);
  }
  Serial.println("[SecureCmd] Pairing started");
  return true;
}

PairingPhase pairingPhase() {
  if (g_attempt) return g_attempt->phase;
  // Between the Bridge address and the first pairing message.
  if (linkSetupWaiting()) return PairingPhase::Asking;
  return linkWindowActive() ? PairingPhase::Discoverable : PairingPhase::Idle;
}

void acceptLinkSetup(const char* host, uint16_t port) {
  closeLinkWindow();
  const uint32_t until = millis() + kLinkSetupWaitMs;
  g_link_setup_until = until ? until : 1;
  networkManager.switchToLink(host, port);
  networkManager.requestLinkPairing(true);
  Serial.println("[SecureCmd] Bridge address accepted; pairing over the direct link");
}

bool linkWindowOpen() {
  return linkWindowActive();
}

bool pairingNumber(char out[kPairNumberDisplaySize]) {
  if (!out) return false;
  out[0] = '\0';
  if (!g_attempt || !g_attempt->have_number ||
      (g_attempt->phase != PairingPhase::Compare &&
       g_attempt->phase != PairingPhase::Confirmed)) {
    return false;
  }
  formatPairingNumber(g_attempt->number, out);
  return true;
}

void confirmPairing() {
  if (!g_attempt || g_attempt->phase != PairingPhase::Compare) return;
  g_attempt->local_confirmed = true;
  g_attempt->phase = PairingPhase::Confirmed;
  publishPair("confirm", "m", g_attempt->confirm_panel,
              sizeof(g_attempt->confirm_panel), nullptr);
  const uint32_t now = millis();
  g_attempt->last_confirm_ms = now ? now : 1;
  Serial.println("[SecureCmd] Pairing: number confirmed on the panel");
  if (g_attempt->peer_confirmed) completePairing();
}

void endPairing() {
  if (g_link_window_until) {
    Serial.println("[SecureCmd] Pairing window closed on the panel");
    closeLinkWindow();
  }
  if (!g_attempt) {
    // Cancelled between Home Assistant's address and the first pairing
    // message: the link would still connect and pair by itself.
    if (g_link_setup_until) {
      g_link_setup_until = 0;
      Serial.println("[SecureCmd] Pairing cancelled on the panel");
      if (!g_state) forgetLink("setup cancelled");
    }
    return;
  }
  if (attemptRunning()) {
    Serial.println("[SecureCmd] Pairing cancelled on the panel");
    endAttempt(PairingPhase::Failed, "cancel");
  }
  // A finished pairing keeps answering repeated confirmations until its
  // window ends; everything else is cleared now.
  if (g_attempt->phase != PairingPhase::Done) releaseAttempt();
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

void setPairingPromptCallback(void (*callback)()) {
  g_pairing_prompt = callback;
}


void onMqttConnected() {
  begin();
  // The Bridge sent its address and the panel restarted: pair right away.
  if (networkManager.linkPairMode() && !g_state && !attemptRunning() &&
      link_config::current().pair_requested && startPairing()) {
    Serial.println("[SecureCmd] Pairing with the Bridge started after link setup");
    if (g_pairing_prompt) g_pairing_prompt();
  }
  if (attemptRunning()) {
    networkManager.mqttEnqueueSubscribe(pairTopic(kPairBridgeLeaf).c_str());
  }
  if (!g_state) {
    // Clear the status on every connect: a clear lost with the connection
    // (QoS 0) would leave "active" retained, and a Bridge whose encryption
    // is being turned off would then wait for this panel forever and refuse
    // a new pairing.
    publishStatus();
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
  // The Bridge refused the key three times: its entry was deleted in Home
  // Assistant. Become a new panel, so Pair sets it up again.
  if (networkManager.consumeLinkForgotten()) {
    Serial.println("[SecureCmd] The Bridge no longer knows this panel; it is new again");
    if (g_state) {
      turnOff(false, nullptr);
    } else {
      forgetLink("unknown to the Bridge");
    }
  }
  linkWindowActive();
  servicePairing();
  entity_search::service();
  if (!g_state) {
    // A full MQTT queue drops the clear (and logs it): retry every 5 s.
    static uint32_t last_clear_ms = 0;
    const uint32_t now = millis();
    if (g_clear_status && networkManager.isMqttConnected() &&
        static_cast<uint32_t>(now - last_clear_ms) >= kClearRetryMs) {
      last_clear_ms = now;
      publishStatus();
    }
    return;
  }
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
  if (g_attempt && strcmp(topic, pairTopic(kPairBridgeLeaf).c_str()) == 0) {
    handlePairMessage(payload, length);
    return true;
  }
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
                    result == OpenResult::OtherKey ? "other pairing key"
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
        // The Bridge does not know this session (after its restart or a
        // reload of the integration) or is listening now. The old session is
        // dropped so service() sends a hello: kept, it stopped every hello,
        // and the Bridge refused each command with another rekey (V2 camera:
        // "No camera response"). resetSession() also restarts the backoff,
        // so a hello lost during the restart is repeated after 10 s.
        resetSession();
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

#include "src/types/device/device_control.h"
#include "src/types/device/device_updates.h"

#include <ArduinoJson.h>
#include <esp_system.h>

#include <cstring>
#include <ctime>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"
#include "src/network/secure/command_channel.h"
#include "src/types/device/device_tile.h"
#include "src/ui/popups/device/device_popup.h"
#include "src/web/server/auth/web_admin_auth.h"

namespace device_control {
namespace {

using device_detail::Detail;

const char* language() { return configManager.getConfig().language; }
const char* text(i18n::DeviceLabel label) { return i18n::device_label(language(), label); }

// Entities whose detail state changed since the last process() pass; "*"
// refreshes every device tile after an overflow.
constexpr uint8_t kPendingEntities = 8;
String g_changed[kPendingEntities];
uint8_t g_changed_count = 0;

void mark_changed(const String& entity) {
  for (uint8_t i = 0; i < g_changed_count; ++i) {
    if (g_changed[i] == entity || g_changed[i] == "*") return;
  }
  if (g_changed_count == kPendingEntities) {
    g_changed[0] = "*";
    g_changed_count = 1;
    return;
  }
  g_changed[g_changed_count++] = entity;
}

// The optimistic target of a just sent Lock or Alarm command.
struct Pending {
  String entity;
  String target;
  // The state when it was sent: any other reported state ends it.
  char state_at_send[24] = {};
  uint32_t until_ms = 0;
};
constexpr uint8_t kPendingTargets = 4;
Pending g_pending[kPendingTargets];
constexpr uint32_t kPendingMs = 10000;

Pending* find_pending(const String& entity) {
  for (Pending& pending : g_pending) {
    if (pending.entity.length() && pending.entity == entity) return &pending;
  }
  return nullptr;
}

void clear_pending(const String& entity) {
  if (Pending* pending = find_pending(entity)) {
    *pending = Pending{};
    mark_changed(entity);
  }
}

void set_pending(const String& entity, const char* target, const char* state_now) {
  Pending* slot = find_pending(entity);
  for (Pending& pending : g_pending) {
    if (slot) break;
    if (!pending.entity.length() || static_cast<int32_t>(millis() - pending.until_ms) >= 0) slot = &pending;
  }
  if (!slot) slot = &g_pending[0];
  slot->entity = entity;
  slot->target = target;
  strlcpy(slot->state_at_send, state_now ? state_now : "", sizeof(slot->state_at_send));
  slot->until_ms = millis() + kPendingMs;
  mark_changed(entity);
}

// A Lock or Alarm command waiting for the Bridge's answer (15 s deadline,
// bridge-contract.md).
struct InFlight {
  String id;
  String entity;
  TileType type = TILE_EMPTY;
  uint32_t sent_ms = 0;
};
constexpr uint8_t kInFlight = 4;
InFlight g_in_flight[kInFlight];
constexpr uint32_t kAnswerMs = 15000;

// Answers wait for the next process() pass.
struct Answer {
  TileType type = TILE_EMPTY;
  String entity;
  String id;
  char status[16] = {};
  int retry_after = 0;
};
constexpr uint8_t kAnswers = 4;
Answer g_answers[kAnswers];
uint8_t g_answer_count = 0;

String new_command_id() {
  static uint32_t sequence = 0;
  char id[32];
  snprintf(id, sizeof(id), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(++sequence));
  return id;
}

bool is_failure(const char* status) { return std::strcmp(status, "ok") != 0 && std::strcmp(status, "pending") != 0; }

void deliver(const Answer& answer) {
  if (is_failure(answer.status)) clear_pending(answer.entity);
  device_popup_on_result(answer.type, answer.entity, answer.id, answer.status, answer.retry_after);
}

void publish(const char* leaf, const String& body) {
  const String topic = mqttTopics.deviceBase() + "/cmnd/" + leaf;
  networkManager.mqttEnqueuePublish(topic.c_str(), body.c_str(), false);
}

void publish_fan(const String& entity, JsonDocument& doc) {
  if (!entity.length() || !networkManager.isMqttConnected()) return;
  doc["entity_id"] = entity;
  String body;
  serializeJson(doc, body);
  publish("fan", body);
}

const AlarmMode kModes[kAlarmModeCount] = {
    {"armed_home", "arm_home", device_detail::kArmHome, "home", static_cast<uint8_t>(i18n::DeviceLabel::ModeHome)},
    {"armed_away", "arm_away", device_detail::kArmAway, "lock", static_cast<uint8_t>(i18n::DeviceLabel::ModeAway)},
    {"armed_night", "arm_night", device_detail::kArmNight, "moon-waning-crescent",
     static_cast<uint8_t>(i18n::DeviceLabel::ModeNight)},
    {"armed_vacation", "arm_vacation", device_detail::kArmVacation, "airplane",
     static_cast<uint8_t>(i18n::DeviceLabel::ModeVacation)},
    {"armed_custom_bypass", "arm_custom_bypass", device_detail::kArmCustomBypass, "shield",
     static_cast<uint8_t>(i18n::DeviceLabel::ModeCustom)},
};

}  // namespace

const AlarmMode& alarm_mode(size_t index) { return kModes[index < kAlarmModeCount ? index : 0]; }

const char* alarm_target(const char* action) {
  for (const AlarmMode& mode : kModes) {
    if (action && std::strcmp(action, mode.action) == 0) return mode.state;
  }
  return "disarmed";
}

Detail detail(const String& entity) {
  const String payload = haBridgeConfig.findDetailValue(entity);
  return device_detail::parse(payload.c_str(), payload.length());
}

void queue_detail(const String& entity, const char* payload, size_t length) {
  if (!entity.length() || !payload || length > device_detail::kMaxPayload) return;
  const Detail parsed = device_detail::parse(payload, length);
  if (!parsed.valid) {
    static uint32_t last_log_ms = 0;
    if (!last_log_ms || millis() - last_log_ms > 5000) {
      last_log_ms = millis();
      Serial.printf("[Device] Malformed detail state ignored: %s\n", entity.c_str());
    }
    return;
  }
  haBridgeConfig.updateDetailValue(entity, String(payload).substring(0, length));
  // A reported state other than the one at sending ends the optimistic target.
  if (Pending* pending = find_pending(entity)) {
    if (std::strcmp(pending->state_at_send, parsed.state) != 0) *pending = Pending{};
  }
  mark_changed(entity);
}

void queue_result(TileType type, const char* payload, size_t length) {
  if (!payload || length > 512) return;
  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, payload, length)) return;
  const char* id = doc["id"] | "";
  const char* status = doc["status"] | "";
  if (!*id || !*status || std::strlen(status) >= sizeof(Answer::status)) return;
  // Only answers to this panel's own commands: the in-flight list knows them.
  for (InFlight& flight : g_in_flight) {
    if (!flight.id.length() || flight.id != id) continue;
    Serial.printf("[Device] Bridge answer for %s (id %s): %s\n", flight.entity.c_str(), id, status);
    if (std::strcmp(status, "invalid") == 0) {
      Serial.printf("[Device] Bridge rejected a %s command as invalid\n", type == TILE_LOCK ? "lock" : "alarm");
    }
    if (g_answer_count < kAnswers) {
      Answer& answer = g_answers[g_answer_count++];
      answer.type = flight.type;
      answer.entity = flight.entity;
      answer.id = flight.id;
      strlcpy(answer.status, status, sizeof(answer.status));
      const int retry = doc["retry_after"] | 0;
      answer.retry_after = retry > 0 ? retry : 0;
    }
    flight = InFlight{};
    return;
  }
  Serial.printf("[Device] Bridge answer %s for a command this panel no longer waits for (id %s)\n", status, id);
}

void process(uint8_t budget) {
  uint8_t done = 0;
  while (g_changed_count && (!budget || done++ < budget)) {
    const String entity = g_changed[--g_changed_count];
    g_changed[g_changed_count] = "";
    device_tiles_refresh(entity);
    device_popup_refresh(entity);
  }
  for (uint8_t i = 0; i < g_answer_count; ++i) deliver(g_answers[i]);
  for (uint8_t i = 0; i < g_answer_count; ++i) g_answers[i] = Answer{};
  g_answer_count = 0;
  const uint32_t now = millis();
  for (InFlight& flight : g_in_flight) {
    if (!flight.id.length() || now - flight.sent_ms < kAnswerMs) continue;
    Answer answer;
    answer.type = flight.type;
    answer.entity = flight.entity;
    answer.id = flight.id;
    strlcpy(answer.status, "no_answer", sizeof(answer.status));
    Serial.printf("[Device] No Bridge answer for %s (id %s) within %u s\n", flight.entity.c_str(),
                  flight.id.c_str(), static_cast<unsigned>(kAnswerMs / 1000));
    flight = InFlight{};
    deliver(answer);
  }
  for (Pending& pending : g_pending) {
    if (pending.entity.length() && static_cast<int32_t>(now - pending.until_ms) >= 0) {
      const String entity = pending.entity;
      pending = Pending{};
      mark_changed(entity);
    }
  }
}

const char* pending_target(const String& entity) {
  const Pending* pending = find_pending(entity);
  if (!pending || static_cast<int32_t>(millis() - pending->until_ms) >= 0) return "";
  return pending->target.c_str();
}

bool panel_secured() {
  return command_channel::state() == command_channel::PairingState::Active && web_admin_auth::enabled();
}

const char* blocked_reason(TileType type, const Detail& detail, const char* action) {
  if (type != TILE_LOCK && type != TILE_ALARM) return nullptr;
  const bool paired = command_channel::state() == command_channel::PairingState::Active;
  const bool password = web_admin_auth::enabled();
  if (!paired && !password) return text(i18n::DeviceLabel::NeedPairAndPassword);
  if (!paired) return text(i18n::DeviceLabel::NeedPair);
  if (!password) return text(i18n::DeviceLabel::NeedPassword);
  if (detail.code_text) return text(i18n::DeviceLabel::TextCodeUnsupported);
  const bool general = !action || !*action;
  if (type == TILE_LOCK && !detail.unlock_allowed &&
      (general || std::strcmp(action, "unlock") == 0 || std::strcmp(action, "open") == 0)) {
    return text(i18n::DeviceLabel::UnlockOff);
  }
  if (type == TILE_ALARM && !detail.disarm_allowed && (general || std::strcmp(action, "disarm") == 0)) {
    return text(i18n::DeviceLabel::DisarmOff);
  }
  return nullptr;
}

bool needs_code(TileType type, const Detail& detail, const char* action) {
  if (!action) return false;
  if (type == TILE_LOCK) {
    if (std::strcmp(action, "lock") == 0) return detail.lock_code;
    // Open follows the unlock code (bridge-contract.md).
    return detail.unlock_code;
  }
  if (type == TILE_ALARM) return std::strcmp(action, "disarm") == 0 ? detail.disarm_code : detail.arm_code;
  return false;
}

String send_access(TileType type, const String& entity, const char* action, const char* code, const char** error) {
  if (error) *error = nullptr;
  if ((type != TILE_LOCK && type != TILE_ALARM) || !entity.length() || !action) return "";
  const Detail current = detail(entity);
  if (const char* reason = blocked_reason(type, current, action)) {
    if (error) *error = reason;
    return "";
  }
  if (!networkManager.isMqttConnected()) {
    if (error) *error = text(i18n::DeviceLabel::ResultUnavailable);
    return "";
  }
  const time_t now = time(nullptr);
  if (now < 1700000000) {
    if (error) *error = text(i18n::DeviceLabel::ClockNotSet);
    return "";
  }
  InFlight* slot = nullptr;
  for (InFlight& flight : g_in_flight) {
    if (!flight.id.length()) { slot = &flight; break; }
  }
  if (!slot) {
    if (error) *error = text(i18n::DeviceLabel::ResultBusy);
    return "";
  }
  StaticJsonDocument<384> doc;
  const String id = new_command_id();
  doc["entity_id"] = entity;
  doc["id"] = id;
  doc["deadline"] = static_cast<uint64_t>(now + 15);
  doc["action"] = action;
  if (code && *code) doc["code"] = code;
  doc["web_auth"] = web_admin_auth::enabled();
  String body;
  serializeJson(doc, body);
  doc.clear();
  publish(type == TILE_LOCK ? "lock" : "alarm", body);
  // Never the code itself.
  Serial.printf("[Device] %s %s sent for %s (id %s, %s, %u bytes, paired %d, session %d)\n",
                type == TILE_LOCK ? "Lock" : "Alarm", action, entity.c_str(), id.c_str(),
                code && *code ? "with code" : "without code", static_cast<unsigned>(body.length()),
                command_channel::state() == command_channel::PairingState::Active ? 1 : 0,
                command_channel::sessionReady() ? 1 : 0);
  // The plaintext body held the code.
  for (size_t i = 0; i < body.length(); ++i) body.setCharAt(i, '\0');
  slot->id = id;
  slot->entity = entity;
  slot->type = type;
  slot->sent_ms = millis();
  const char* target = type == TILE_LOCK ? (std::strcmp(action, "lock") == 0     ? "locked"
                                            : std::strcmp(action, "unlock") == 0 ? "unlocked"
                                                                                  : "")
                                         : alarm_target(action);
  if (*target) set_pending(entity, target, current.state);
  return id;
}

void fan_action(const String& entity, const char* action) {
  StaticJsonDocument<192> doc;
  doc["action"] = action;
  publish_fan(entity, doc);
}

void fan_percentage(const String& entity, uint8_t percentage) {
  StaticJsonDocument<192> doc;
  doc["action"] = "set_percentage";
  doc["percentage"] = percentage > 100 ? 100 : percentage;
  publish_fan(entity, doc);
}

void fan_preset(const String& entity, const char* preset) {
  if (!preset || !*preset) return;
  StaticJsonDocument<256> doc;
  doc["action"] = "set_preset_mode";
  doc["preset_mode"] = preset;
  publish_fan(entity, doc);
}

void fan_oscillate(const String& entity, bool oscillating) {
  StaticJsonDocument<192> doc;
  doc["action"] = "oscillate";
  doc["oscillating"] = oscillating;
  publish_fan(entity, doc);
}

void fan_direction(const String& entity, const char* direction) {
  StaticJsonDocument<192> doc;
  doc["action"] = "set_direction";
  doc["direction"] = direction;
  publish_fan(entity, doc);
}

}  // namespace device_control

void process_device_updates(uint8_t budget) { device_control::process(budget); }

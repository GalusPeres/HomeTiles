#pragma once

// ArduinoJson parser for {base}/cmnd/assist (assist_contract.h). Separate
// from the contract so the contract's host test needs no ArduinoJson.

#include <ArduinoJson.h>

#include "src/audio/assist_contract.h"

namespace assist_contract {

enum class CommandKind : uint8_t { Invalid, Start, Stop, WakeWords, Mic };

// More entries than any real list; a longer "active" array is refused.
constexpr size_t kMaxWakeWordEntries = 16;

enum class CommandStatus : uint8_t {
  Ok,
  TooLarge,
  InvalidJson,
  UnsupportedVersion,
  UnsupportedOp,
  InvalidId,
  InvalidValue,
};

inline const char* commandStatusName(CommandStatus status) {
  switch (status) {
    case CommandStatus::Ok: return "ok";
    case CommandStatus::TooLarge: return "too_large";
    case CommandStatus::InvalidJson: return "invalid_json";
    case CommandStatus::UnsupportedVersion: return "unsupported_version";
    case CommandStatus::UnsupportedOp: return "unsupported_op";
    case CommandStatus::InvalidId: return "invalid_id";
    case CommandStatus::InvalidValue: return "invalid_value";
  }
  return "unknown";
}

struct Command {
  CommandKind kind = CommandKind::Invalid;
  CommandStatus status = CommandStatus::InvalidJson;
  // Set for Start and Stop once the id is valid, also when a later field
  // was refused, so the status can name the request.
  char id[kMaxRequestIdLength + 1] = {};
  uint32_t max_ms = kDefaultMaxMs;
  // WakeWords: catalog mask of the known ids in "active".
  uint32_t wake_words = 0;
  // Mic: the requested privacy switch state.
  bool mic_enabled = true;
};

inline void parseCommand(const char* payload, size_t length, Command* out) {
  if (!out) return;
  *out = Command{};
  if (!payload || length == 0) return;
  if (length > kMaxRequestPayloadBytes) {
    out->status = CommandStatus::TooLarge;
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok ||
      !doc.is<JsonObjectConst>()) {
    return;
  }
  JsonVariantConst version = doc["v"];
  if (!version.is<long long>() || version.as<long long>() != kProtocolVersion) {
    out->status = CommandStatus::UnsupportedVersion;
    return;
  }
  JsonVariantConst op = doc["op"];
  const char* op_name = op.is<const char*>() ? op.as<const char*>() : nullptr;
  if (op_name && strcmp(op_name, "start") == 0) {
    out->kind = CommandKind::Start;
  } else if (op_name && strcmp(op_name, "stop") == 0) {
    out->kind = CommandKind::Stop;
  } else if (op_name && strcmp(op_name, "mic") == 0) {
    out->kind = CommandKind::Mic;
    JsonVariantConst enabled = doc["enabled"];
    if (!enabled.is<bool>()) {
      out->status = CommandStatus::InvalidValue;
      return;
    }
    out->mic_enabled = enabled.as<bool>();
    out->status = CommandStatus::Ok;
    return;
  } else if (op_name && strcmp(op_name, "wake_words") == 0) {
    out->kind = CommandKind::WakeWords;
    JsonVariantConst active = doc["active"];
    if (!active.is<JsonArrayConst>() || active.size() > kMaxWakeWordEntries) {
      out->status = CommandStatus::InvalidValue;
      return;
    }
    // Unknown, malformed and non-string entries are ignored.
    for (JsonVariantConst entry : active.as<JsonArrayConst>()) {
      const char* word = entry.is<const char*>() ? entry.as<const char*>() : nullptr;
      const int index = word ? wakeWordIndex(word, strlen(word)) : -1;
      if (index >= 0) out->wake_words |= 1u << index;
    }
    out->status = CommandStatus::Ok;
    return;
  } else {
    out->status = CommandStatus::UnsupportedOp;
    return;
  }
  JsonVariantConst id = doc["id"];
  const char* id_text = id.is<const char*>() ? id.as<const char*>() : nullptr;
  const size_t id_length = id_text ? strlen(id_text) : 0;
  if (!isValidRequestId(id_text, id_length)) {
    out->status = CommandStatus::InvalidId;
    return;
  }
  memcpy(out->id, id_text, id_length + 1);
  if (out->kind == CommandKind::Start) {
    JsonVariantConst max_ms = doc["max_ms"];
    if (!max_ms.isNull()) {
      if (!max_ms.is<double>()) {
        out->status = CommandStatus::InvalidValue;
        return;
      }
      out->max_ms = clampMaxMs(max_ms.as<double>());
    }
  }
  out->status = CommandStatus::Ok;
}

}  // namespace assist_contract

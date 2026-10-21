#pragma once

// ArduinoJson parser for {base}/cmnd/audio (speaker_contract.h). Separate
// from the contract so the contract's host test needs no ArduinoJson.

#include <ArduinoJson.h>

#include "src/audio/speaker_contract.h"

namespace speaker_contract {

enum class CommandKind : uint8_t { Invalid, Play, Stop, Volume, Mute, Pause, Resume };

enum class CommandStatus : uint8_t {
  Ok,
  TooLarge,
  InvalidJson,
  UnsupportedVersion,
  UnsupportedOp,
  InvalidId,
  InvalidUrl,
  HttpsUnsupported,
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
    case CommandStatus::InvalidUrl: return "invalid_url";
    case CommandStatus::HttpsUnsupported: return "https_unsupported";
    case CommandStatus::InvalidValue: return "invalid_value";
  }
  return "unknown";
}

struct Command {
  CommandKind kind = CommandKind::Invalid;
  CommandStatus status = CommandStatus::InvalidJson;
  char id[kMaxRequestIdLength + 1] = {};
  // Only for Play; points into the caller's url buffer.
  size_t url_length = 0;
  float volume = kDefaultVolume;
  bool muted = false;
};

// url_out must hold kMaxUrlLength + 1 bytes; it receives a play URL.
inline void parseCommand(const char* payload, size_t length, bool https_allowed,
                         Command* out, char* url_out) {
  if (!out) return;
  *out = Command{};
  if (url_out) url_out[0] = '\0';
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
  if (!op_name) {
    out->status = CommandStatus::UnsupportedOp;
    return;
  }
  if (strcmp(op_name, "stop") == 0) {
    out->kind = CommandKind::Stop;
    out->status = CommandStatus::Ok;
    return;
  }
  if (strcmp(op_name, "pause") == 0 || strcmp(op_name, "resume") == 0) {
    out->kind = op_name[0] == 'p' ? CommandKind::Pause : CommandKind::Resume;
    out->status = CommandStatus::Ok;
    return;
  }
  if (strcmp(op_name, "volume") == 0) {
    out->kind = CommandKind::Volume;
    JsonVariantConst volume = doc["volume"];
    if (!volume.is<double>()) {
      out->status = CommandStatus::InvalidValue;
      return;
    }
    const double value = volume.as<double>();
    if (!(value >= 0.0 && value <= 1.0)) {
      out->status = CommandStatus::InvalidValue;
      return;
    }
    out->volume = clampVolume(value);
    out->status = CommandStatus::Ok;
    return;
  }
  if (strcmp(op_name, "mute") == 0) {
    out->kind = CommandKind::Mute;
    JsonVariantConst muted = doc["muted"];
    if (!muted.is<bool>()) {
      out->status = CommandStatus::InvalidValue;
      return;
    }
    out->muted = muted.as<bool>();
    out->status = CommandStatus::Ok;
    return;
  }
  if (strcmp(op_name, "play") != 0) {
    out->status = CommandStatus::UnsupportedOp;
    return;
  }
  out->kind = CommandKind::Play;
  JsonVariantConst id = doc["id"];
  const char* id_text = id.is<const char*>() ? id.as<const char*>() : nullptr;
  const size_t id_length = id_text ? strlen(id_text) : 0;
  if (!isValidRequestId(id_text, id_length)) {
    out->status = CommandStatus::InvalidId;
    return;
  }
  memcpy(out->id, id_text, id_length + 1);
  JsonVariantConst url = doc["url"];
  const char* url_text = url.is<const char*>() ? url.as<const char*>() : nullptr;
  const size_t url_length = url_text ? strlen(url_text) : 0;
  const ErrorCode url_error = validateUrl(url_text, url_length, https_allowed);
  if (url_error == ErrorCode::HttpsUnsupported) {
    out->status = CommandStatus::HttpsUnsupported;
    return;
  }
  if (url_error != ErrorCode::None || !url_out) {
    out->status = CommandStatus::InvalidUrl;
    return;
  }
  memcpy(url_out, url_text, url_length + 1);
  out->url_length = url_length;
  out->status = CommandStatus::Ok;
}

}  // namespace speaker_contract

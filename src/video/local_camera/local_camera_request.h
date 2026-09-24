#pragma once

// JSON parsing of {base}/cmnd/local_camera requests. Kept separate from the
// pure contract headers so host tests without ArduinoJson can still run the
// contract checks.

#include <ArduinoJson.h>

#include "src/video/local_camera/local_camera_contract.h"
#include "src/video/local_camera/local_camera_stream_contract.h"

namespace local_camera_contract {

inline RequestStatus snapshotFromDocument(const JsonDocument& doc, SnapshotRequest* out) {
  JsonVariantConst version = doc["v"];
  JsonVariantConst op = doc["op"];
  JsonVariantConst id = doc["id"];
  JsonVariantConst max_bytes = doc["max_bytes"];
  if (!version.is<long long>()) return RequestStatus::UnsupportedVersion;
  if (!op.is<const char*>()) return RequestStatus::UnsupportedOperation;
  if (!id.is<const char*>()) return RequestStatus::InvalidId;
  const bool has_max_bytes = !max_bytes.isNull();
  if (has_max_bytes && !max_bytes.is<long long>()) {
    return RequestStatus::InvalidMaxBytes;
  }
  return validateRequestFields(version.as<long long>(), op.as<const char*>(),
                               id.as<const char*>(), has_max_bytes,
                               has_max_bytes ? max_bytes.as<long long>() : 0,
                               out);
}

inline RequestStatus parseRequestDocument(const char* payload, size_t length,
                                          JsonDocument& doc) {
  if (!payload || length == 0) return RequestStatus::Empty;
  if (length > kMaxRequestPayloadBytes) return RequestStatus::TooLong;
  const DeserializationError error = deserializeJson(
      doc, payload, length, DeserializationOption::NestingLimit(2));
  if (error || !doc.is<JsonObjectConst>()) return RequestStatus::Malformed;
  return RequestStatus::Ok;
}

inline RequestStatus parseSnapshotRequest(const char* payload, size_t length,
                                          SnapshotRequest* out) {
  JsonDocument doc;
  const RequestStatus status = parseRequestDocument(payload, length, doc);
  if (status != RequestStatus::Ok) return status;
  return snapshotFromDocument(doc, out);
}

// ---------------------------------------------------------------------------
// Snapshot or live-stream command. Messages without "action" are snapshot
// requests (unchanged contract); "stream" starts or keeps a live upload alive
// and "stream_stop" ends it.
// ---------------------------------------------------------------------------
enum class CommandKind : uint8_t { Snapshot, Stream, StreamStop, Pause, Resume };

struct LocalCameraCommand {
  CommandKind kind = CommandKind::Snapshot;
  // Snapshot kind, also used for payloads that are not a JSON object.
  RequestStatus snapshot_status = RequestStatus::Malformed;
  SnapshotRequest snapshot;
  // Stream and StreamStop kinds.
  local_camera_stream::CommandStatus stream_status =
      local_camera_stream::CommandStatus::UnsupportedAction;
  local_camera_stream::StreamCommand stream;
  char stop_session[local_camera_stream::kMaxSessionLength + 1] = {};
};

inline local_camera_stream::OptionalInt optionalInt(JsonVariantConst value) {
  local_camera_stream::OptionalInt result;
  if (value.is<long long>()) {
    result.present = true;
    result.value = value.as<long long>();
  }
  return result;
}

inline void parseCommand(const char* payload, size_t length, LocalCameraCommand* out) {
  using namespace local_camera_stream;
  if (!out) return;
  *out = LocalCameraCommand{};
  JsonDocument doc;
  const RequestStatus status = parseRequestDocument(payload, length, doc);
  if (status != RequestStatus::Ok) {
    out->kind = CommandKind::Snapshot;
    out->snapshot_status = status;
    return;
  }
  JsonVariantConst action = doc["action"];
  if (action.isNull()) {
    out->kind = CommandKind::Snapshot;
    out->snapshot_status = snapshotFromDocument(doc, &out->snapshot);
    return;
  }
  JsonVariantConst version = doc["v"];
  const long long v = version.is<long long>() ? version.as<long long>() : -1;
  const char* action_name = action.is<const char*>() ? action.as<const char*>() : nullptr;
  JsonVariantConst session = doc["session"];
  const char* session_text = session.is<const char*>() ? session.as<const char*>() : nullptr;
  if (action_name && (strcmp(action_name, kActionPause) == 0 ||
                      strcmp(action_name, kActionResume) == 0)) {
    out->kind = strcmp(action_name, kActionPause) == 0 ? CommandKind::Pause : CommandKind::Resume;
    out->stream_status = v == local_camera_contract::kProtocolVersion
                             ? CommandStatus::Ok
                             : CommandStatus::UnsupportedVersion;
    return;
  }
  if (action_name && strcmp(action_name, kActionStreamStop) == 0) {
    out->kind = CommandKind::StreamStop;
    out->stream_status = validateStopFields(v, session_text, out->stop_session,
                                            sizeof(out->stop_session));
    return;
  }
  out->kind = CommandKind::Stream;
  if (!action_name || strcmp(action_name, kActionStream) != 0) {
    out->stream_status = CommandStatus::UnsupportedAction;
    return;
  }
  JsonVariantConst host = doc["host"];
  JsonVariantConst token = doc["token"];
  const StreamHints hints = hintsFrom(optionalInt(doc["width"]), optionalInt(doc["height"]),
                                      optionalInt(doc["fps"]), optionalInt(doc["quality"]));
  out->stream_status = validateStreamFields(
      v, session_text, host.is<const char*>() ? host.as<const char*>() : nullptr,
      optionalInt(doc["port"]), token.is<const char*>() ? token.as<const char*>() : nullptr,
      optionalInt(doc["ttl_ms"]), hints, &out->stream);
  // A present but non-integer ttl_ms is invalid rather than silently defaulted.
  if (out->stream_status == CommandStatus::Ok && !doc["ttl_ms"].isNull() &&
      !doc["ttl_ms"].is<long long>()) {
    out->stream_status = CommandStatus::InvalidTtl;
  }
}

}  // namespace local_camera_contract

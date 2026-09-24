#pragma once

// Pure, host-testable part of the local camera feature: the MQTT contract
// shared with the Home Assistant Bridge, the status/error payloads, request
// validation, rate limiting and the small image-processing helpers used by the
// capture worker. Nothing in this header touches hardware, FreeRTOS or Arduino.
//
// MQTT contract (base = the configured device base topic):
//   {base}/cmnd/local_camera            Bridge -> panel, QoS0, not retained,
//       {"v":1,"id":"<16-32 lowercase hex>","op":"snapshot","max_bytes":131072}
//   {base}/stat/local_camera            panel -> Bridge, QoS0, retained,
//       {"v":1,"state":"ready"|"disabled"|"error","width":1280,"height":720,
//        "format":"jpeg","max_bytes":131072,"min_interval_ms":1000}
//       plus optional "sensor":"ov02c10" and "error":"<code>", and
//       "rotate":90 when the receiver has to turn every JPEG (width/height
//       are the JPEG as sent) clockwise by that many degrees
//   {base}/stat/local_camera/image/<id> panel -> Bridge, QoS0, not retained,
//       raw JPEG bytes (FFD8 ... FFD9)
//   {base}/stat/local_camera/error/<id> panel -> Bridge, QoS0, not retained,
//       {"v":1,"error":"busy"|"disabled"|"sensor_unavailable"|
//                      "encoder_busy"|"too_large"|"rate_limited"}

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace local_camera_contract {

constexpr int kProtocolVersion = 1;
// Largest JPEG the panel sends. A request may ask for less, never for more.
constexpr uint32_t kPanelMaxJpegBytes = 131072;
// Requests below this size cannot hold a useful 720p JPEG.
constexpr uint32_t kMinRequestMaxBytes = 16384;
// Upper bound accepted in a request before clamping to kPanelMaxJpegBytes.
constexpr uint32_t kMaxRequestMaxBytes = 16u * 1024u * 1024u;
constexpr uint32_t kMinIntervalMs = 1000;
// The Bridge gives up on a request after LOCAL_CAMERA_REQUEST_TIMEOUT_S = 6.0
// (const.py) and drops a later image as unsolicited. An upload that has not
// started this long after receipt is skipped; the margin covers MQTT latency.
constexpr uint32_t kRequestDeadlineMs = 5500;
constexpr size_t kMinRequestIdLength = 16;
constexpr size_t kMaxRequestIdLength = 32;
constexpr size_t kMaxRequestPayloadBytes = 512;

constexpr const char* kCommandLeaf = "/cmnd/local_camera";
constexpr const char* kStatusLeaf = "/stat/local_camera";
constexpr const char* kImageLeaf = "/stat/local_camera/image/";
constexpr const char* kErrorLeaf = "/stat/local_camera/error/";

enum class PublicState : uint8_t { Ready, Disabled, Error };

enum class ErrorCode : uint8_t {
  None,
  Busy,
  Disabled,
  SensorUnavailable,
  EncoderBusy,
  TooLarge,
  RateLimited,
};

inline const char* publicStateName(PublicState state) {
  switch (state) {
    case PublicState::Ready: return "ready";
    case PublicState::Disabled: return "disabled";
    case PublicState::Error: return "error";
  }
  return "error";
}

inline const char* errorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::None: return "";
    case ErrorCode::Busy: return "busy";
    case ErrorCode::Disabled: return "disabled";
    case ErrorCode::SensorUnavailable: return "sensor_unavailable";
    case ErrorCode::EncoderBusy: return "encoder_busy";
    case ErrorCode::TooLarge: return "too_large";
    case ErrorCode::RateLimited: return "rate_limited";
  }
  return "";
}

// Protocol identifiers are fixed ASCII tokens; refuse anything else instead of
// escaping it into JSON.
inline bool isProtocolToken(const char* text) {
  if (!text || !*text) return false;
  size_t length = 0;
  for (const char* p = text; *p; ++p) {
    const char c = *p;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok || ++length > 32) return false;
  }
  return true;
}

inline bool isValidRequestId(const char* id, size_t length) {
  if (!id || length < kMinRequestIdLength || length > kMaxRequestIdLength) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const char c = id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

struct SnapshotRequest {
  char id[kMaxRequestIdLength + 1] = {};
  // Effective limit after clamping to kPanelMaxJpegBytes.
  uint32_t max_bytes = kPanelMaxJpegBytes;
  // millis() when the loop task accepted the request; set by the firmware,
  // never parsed from the payload.
  uint32_t received_ms = 0;
};

enum class RequestStatus : uint8_t {
  Ok,
  Empty,
  TooLong,
  Malformed,
  UnsupportedVersion,
  UnsupportedOperation,
  InvalidId,
  InvalidMaxBytes,
};

inline const char* requestStatusName(RequestStatus status) {
  switch (status) {
    case RequestStatus::Ok: return "ok";
    case RequestStatus::Empty: return "empty";
    case RequestStatus::TooLong: return "too_long";
    case RequestStatus::Malformed: return "malformed";
    case RequestStatus::UnsupportedVersion: return "unsupported_version";
    case RequestStatus::UnsupportedOperation: return "unsupported_operation";
    case RequestStatus::InvalidId: return "invalid_id";
    case RequestStatus::InvalidMaxBytes: return "invalid_max_bytes";
  }
  return "malformed";
}

// Field-level validation shared by the JSON parser (local_camera_request.h).
// has_max_bytes=false selects the panel maximum.
inline RequestStatus validateRequestFields(long long version,
                                           const char* op,
                                           const char* id,
                                           bool has_max_bytes,
                                           long long max_bytes,
                                           SnapshotRequest* out) {
  if (version != kProtocolVersion) return RequestStatus::UnsupportedVersion;
  if (!op || strcmp(op, "snapshot") != 0) {
    return RequestStatus::UnsupportedOperation;
  }
  const size_t id_length = id ? strnlen(id, kMaxRequestIdLength + 1) : 0;
  if (!isValidRequestId(id, id_length)) return RequestStatus::InvalidId;
  uint32_t effective = kPanelMaxJpegBytes;
  if (has_max_bytes) {
    if (max_bytes < static_cast<long long>(kMinRequestMaxBytes) ||
        max_bytes > static_cast<long long>(kMaxRequestMaxBytes)) {
      return RequestStatus::InvalidMaxBytes;
    }
    if (max_bytes < static_cast<long long>(effective)) {
      effective = static_cast<uint32_t>(max_bytes);
    }
  }
  if (out) {
    memcpy(out->id, id, id_length);
    out->id[id_length] = '\0';
    out->max_bytes = effective;
  }
  return RequestStatus::Ok;
}

// Exact Bridge capability: only a real, enabled and detected sensor counts.
inline bool bridgeCapability(bool profile_supported, bool user_enabled,
                             bool sensor_detected) {
  return profile_supported && user_enabled && sensor_detected;
}

struct StatusFields {
  PublicState state = PublicState::Disabled;
  uint16_t width = 0;            // JPEG size of the active sensor mode.
  uint16_t height = 0;
  // Clockwise degrees the receiver turns every JPEG (0 or 90); sent only when
  // not 0, so older Bridges and landscape boards see no change.
  uint16_t rotate = 0;
  const char* sensor = nullptr;  // Included only when it is a protocol token.
  const char* error = nullptr;   // Included only for PublicState::Error.
  // Privacy pause from the display or Home Assistant: the camera stays
  // announced (Bridge capability) but captures nothing. Sent only when true,
  // together with PublicState::Disabled, so older Bridges see "disabled".
  bool paused = false;
  // Stream session the display ended (tap on the indicator). The Bridge ends
  // that session's viewers and starts a new session for the next viewer.
  // Included only when it is a protocol token.
  const char* ended_session = nullptr;
};

// Returns the JSON length, or 0 if the buffer is too small.
inline size_t buildStatusJson(char* out, size_t capacity,
                              const StatusFields& fields) {
  if (!out || capacity == 0) return 0;
  int written = snprintf(
      out, capacity,
      "{\"v\":%d,\"state\":\"%s\",\"width\":%u,\"height\":%u,"
      "\"format\":\"jpeg\",\"max_bytes\":%u,\"min_interval_ms\":%u",
      kProtocolVersion, publicStateName(fields.state),
      static_cast<unsigned>(fields.width), static_cast<unsigned>(fields.height),
      static_cast<unsigned>(kPanelMaxJpegBytes),
      static_cast<unsigned>(kMinIntervalMs));
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  size_t length = static_cast<size_t>(written);
  auto append = [&](const char* key, const char* value) -> bool {
    const int n = snprintf(out + length, capacity - length,
                           ",\"%s\":\"%s\"", key, value);
    if (n < 0 || static_cast<size_t>(n) >= capacity - length) return false;
    length += static_cast<size_t>(n);
    return true;
  };
  if (isProtocolToken(fields.sensor) && !append("sensor", fields.sensor)) {
    out[0] = '\0';
    return 0;
  }
  if (fields.state == PublicState::Error && isProtocolToken(fields.error) &&
      !append("error", fields.error)) {
    out[0] = '\0';
    return 0;
  }
  if (fields.paused) {
    const int n = snprintf(out + length, capacity - length, ",\"paused\":true");
    if (n < 0 || static_cast<size_t>(n) >= capacity - length) {
      out[0] = '\0';
      return 0;
    }
    length += static_cast<size_t>(n);
  }
  if (fields.rotate == 90 || fields.rotate == 180 || fields.rotate == 270) {
    const int n = snprintf(out + length, capacity - length, ",\"rotate\":%u",
                           static_cast<unsigned>(fields.rotate));
    if (n < 0 || static_cast<size_t>(n) >= capacity - length) {
      out[0] = '\0';
      return 0;
    }
    length += static_cast<size_t>(n);
  }
  if (isProtocolToken(fields.ended_session) && !append("ended", fields.ended_session)) {
    out[0] = '\0';
    return 0;
  }
  if (length + 2 > capacity) {
    out[0] = '\0';
    return 0;
  }
  out[length++] = '}';
  out[length] = '\0';
  return length;
}

inline size_t buildErrorJson(char* out, size_t capacity, ErrorCode code) {
  if (!out || capacity == 0 || code == ErrorCode::None) {
    if (out && capacity) out[0] = '\0';
    return 0;
  }
  const int written = snprintf(out, capacity, "{\"v\":%d,\"error\":\"%s\"}",
                               kProtocolVersion, errorCodeName(code));
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

// Builds base + leaf (+ id). Returns false instead of truncating.
inline bool buildTopic(char* out, size_t capacity, const char* base,
                       const char* leaf, const char* id = nullptr) {
  if (!out || capacity == 0 || !base || !*base || !leaf) return false;
  const int written = snprintf(out, capacity, "%s%s%s", base, leaf,
                               id ? id : "");
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return false;
  }
  return true;
}

// Minimum spacing between accepted snapshot requests. Rejected requests do not
// move the window, so a flood cannot starve legitimate requests forever.
class RequestRateLimiter {
 public:
  explicit RequestRateLimiter(uint32_t interval_ms = kMinIntervalMs)
      : interval_ms_(interval_ms) {}
  bool wouldAccept(uint32_t now_ms) const {
    return !has_last_ || static_cast<uint32_t>(now_ms - last_ms_) >= interval_ms_;
  }
  void markAccepted(uint32_t now_ms) {
    has_last_ = true;
    last_ms_ = now_ms;
  }
  void reset() { has_last_ = false; }

 private:
  uint32_t interval_ms_;
  uint32_t last_ms_ = 0;
  bool has_last_ = false;
};

inline bool looksLikeCompleteJpeg(const uint8_t* data, size_t length) {
  return data && length >= 4 && data[0] == 0xFF && data[1] == 0xD8 &&
         data[length - 2] == 0xFF && data[length - 1] == 0xD9;
}

// Moves the centred dst_w x dst_h window of a packed src_w x src_h image to
// the start of the same buffer as a packed dst_w-wide image. Every destination
// row starts at or before its source row, so a forward memmove is safe.
inline bool compactCenterCrop(uint8_t* buffer, size_t buffer_bytes,
                              uint32_t src_w, uint32_t src_h,
                              uint32_t dst_w, uint32_t dst_h,
                              uint32_t bytes_per_pixel) {
  if (!buffer || bytes_per_pixel == 0 || dst_w == 0 || dst_h == 0 ||
      dst_w > src_w || dst_h > src_h) {
    return false;
  }
  const uint64_t needed = static_cast<uint64_t>(src_w) * src_h * bytes_per_pixel;
  if (needed > buffer_bytes) return false;
  const uint32_t x0 = (src_w - dst_w) / 2;
  const uint32_t y0 = (src_h - dst_h) / 2;
  const size_t src_stride = static_cast<size_t>(src_w) * bytes_per_pixel;
  const size_t dst_stride = static_cast<size_t>(dst_w) * bytes_per_pixel;
  for (uint32_t row = 0; row < dst_h; ++row) {
    uint8_t* dst = buffer + static_cast<size_t>(row) * dst_stride;
    const uint8_t* src = buffer + static_cast<size_t>(row + y0) * src_stride +
                         static_cast<size_t>(x0) * bytes_per_pixel;
    if (dst != src) memmove(dst, src, dst_stride);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Auto exposure: sensor exposure in lines and analog gain in 1/16 steps.
// ---------------------------------------------------------------------------
struct ExposureSetting {
  uint16_t lines = 0;
  uint16_t gain_x16 = 16;  // 16 == 1.0x
};

struct ExposureLimits {
  uint16_t min_lines = 4;
  uint16_t max_lines = 1132;  // Table default of the 1288x728 mode (VTS 1164).
  uint16_t min_gain_x16 = 16;   // 1.0x
  uint16_t max_gain_x16 = 248;  // 15.5x
};

struct ExposureStep {
  ExposureSetting next;
  bool converged = false;
  bool limited = false;  // At a limit in the requested direction.
};

// Centre-weighted mean of the ISP's 5x5 AE luminance blocks. The weights are
// the "luma_adjust.weight" matrix of Espressif's OV02C10 IPA tuning file.
inline uint32_t weightedMeanLuma(const int luminance[5][5]) {
  static const uint8_t kWeights[5][5] = {
      {1, 1, 2, 1, 1},
      {1, 2, 3, 2, 1},
      {1, 3, 5, 3, 1},
      {1, 2, 3, 2, 1},
      {1, 1, 2, 1, 1},
  };
  uint32_t sum = 0;
  uint32_t weight = 0;
  for (int x = 0; x < 5; ++x) {
    for (int y = 0; y < 5; ++y) {
      int value = luminance[x][y];
      if (value < 0) value = 0;
      if (value > 255) value = 255;
      sum += static_cast<uint32_t>(value) * kWeights[x][y];
      weight += kWeights[x][y];
    }
  }
  return weight ? (sum + weight / 2) / weight : 0;
}

inline ExposureStep stepAutoExposure(ExposureSetting current,
                                     uint32_t mean_luma,
                                     uint32_t target_luma,
                                     uint32_t tolerance,
                                     const ExposureLimits& limits) {
  ExposureStep step;
  step.next = current;
  const uint32_t low = target_luma > tolerance ? target_luma - tolerance : 0;
  const uint32_t high = target_luma + tolerance;
  if (mean_luma >= low && mean_luma <= high) {
    step.converged = true;
    return step;
  }

  const uint64_t min_total =
      static_cast<uint64_t>(limits.min_lines) * limits.min_gain_x16;
  const uint64_t max_total =
      static_cast<uint64_t>(limits.max_lines) * limits.max_gain_x16;
  uint64_t lines = current.lines;
  uint64_t gain = current.gain_x16;
  if (lines < limits.min_lines) lines = limits.min_lines;
  if (lines > limits.max_lines) lines = limits.max_lines;
  if (gain < limits.min_gain_x16) gain = limits.min_gain_x16;
  if (gain > limits.max_gain_x16) gain = limits.max_gain_x16;
  const uint64_t total = lines * gain;

  // Ratio in 1/256 steps, bounded to 0.25x..4x per iteration. Saturated
  // frames carry no magnitude information, so halve at least.
  const uint32_t mean = mean_luma ? mean_luma : 1;
  uint64_t ratio_q8 = (static_cast<uint64_t>(target_luma) * 256u + mean / 2) / mean;
  if (ratio_q8 < 64) ratio_q8 = 64;
  if (ratio_q8 > 1024) ratio_q8 = 1024;
  if (mean_luma >= 250 && ratio_q8 > 128) ratio_q8 = 128;

  uint64_t wanted = (total * ratio_q8 + 128) / 256;
  if (wanted < min_total) wanted = min_total;
  if (wanted > max_total) wanted = max_total;

  // Prefer exposure time over gain to keep noise low.
  uint64_t next_lines = wanted / limits.min_gain_x16;
  if (next_lines > limits.max_lines) next_lines = limits.max_lines;
  if (next_lines < limits.min_lines) next_lines = limits.min_lines;
  uint64_t next_gain = (wanted + next_lines / 2) / next_lines;
  if (next_gain < limits.min_gain_x16) next_gain = limits.min_gain_x16;
  if (next_gain > limits.max_gain_x16) next_gain = limits.max_gain_x16;

  step.next.lines = static_cast<uint16_t>(next_lines);
  step.next.gain_x16 = static_cast<uint16_t>(next_gain);
  const bool unchanged = step.next.lines == current.lines &&
                         step.next.gain_x16 == current.gain_x16;
  const bool wants_brighter = mean_luma < low;
  step.limited = unchanged &&
                 ((wants_brighter && wanted >= max_total) ||
                  (!wants_brighter && wanted <= min_total));
  // Nothing more can be done at a limit; treat it as the final setting.
  step.converged = step.limited;
  return step;
}

// ---------------------------------------------------------------------------
// Exposure in stages:
//   0 exposure time up to the frame interval, then analog gain
//   1 sensor digital gain at the frame interval (gain above the analog
//     maximum, up to max_total_gain_x16)
//   2 longer exposure at the full gain, only in real darkness (lowers the
//     frame rate; only when night_max_lines is above the frame interval,
//     e.g. the Auto stream)
// The stage follows from the current setting; at a stage limit the next or
// previous stage takes over. limited is only set at the very last limit.
// ---------------------------------------------------------------------------
struct ExposureStages {
  ExposureLimits normal;
  uint16_t night_max_lines = 0;     // <= normal.max_lines: no night stage.
  uint16_t max_total_gain_x16 = 0;  // <= normal.max_gain_x16: no digital stage.
};

inline ExposureStep stepStagedExposure(ExposureSetting current, uint32_t mean_luma,
                                       uint32_t target_luma, uint32_t tolerance,
                                       const ExposureStages& stages) {
  const ExposureLimits& normal = stages.normal;
  const uint16_t night_lines =
      stages.night_max_lines > normal.max_lines ? stages.night_max_lines : normal.max_lines;
  const uint16_t total_gain = stages.max_total_gain_x16 > normal.max_gain_x16
                                  ? stages.max_total_gain_x16
                                  : normal.max_gain_x16;
  ExposureLimits limits[3] = {
      normal,
      {normal.max_lines, normal.max_lines, normal.max_gain_x16, total_gain},
      {normal.max_lines, night_lines, total_gain, total_gain},
  };
  const bool available[3] = {true, total_gain > normal.max_gain_x16,
                             night_lines > normal.max_lines};
  int stage = 0;
  if (available[2] && current.lines > normal.max_lines) {
    stage = 2;
  } else if (available[1] && current.gain_x16 > normal.max_gain_x16) {
    stage = 1;
  }
  ExposureStep step = stepAutoExposure(current, mean_luma, target_luma, tolerance, limits[stage]);
  if (!step.limited) return step;
  const uint32_t low = target_luma > tolerance ? target_luma - tolerance : 0;
  const int direction = mean_luma < low ? 1 : -1;
  int next = stage + direction;
  while (next >= 0 && next <= 2 && !available[next]) next += direction;
  if (next < 0 || next > 2) return step;
  return stepAutoExposure(current, mean_luma, target_luma, tolerance, limits[next]);
}

// ---------------------------------------------------------------------------
// Gray-world white balance folded into the colour-correction matrix. The
// ESP32-P4 before v3.0 has no separate white-balance gain block.
// ---------------------------------------------------------------------------
struct WhiteBalanceGains {
  float red = 1.0f;
  float blue = 1.0f;
};

constexpr float kMinWhiteBalanceGain = 0.5f;
constexpr float kMaxWhiteBalanceGain = 3.0f;

inline float clampGain(float value) {
  if (!(value == value)) return 1.0f;  // NaN
  if (value < kMinWhiteBalanceGain) return kMinWhiteBalanceGain;
  if (value > kMaxWhiteBalanceGain) return kMaxWhiteBalanceGain;
  return value;
}

// sum_* are the white-patch channel sums reported by the ISP AWB statistics
// before the CCM. Too few samples keep the previous gains.
inline WhiteBalanceGains grayWorldGains(uint64_t sum_r, uint64_t sum_g,
                                        uint64_t sum_b, uint32_t samples,
                                        uint32_t min_samples,
                                        WhiteBalanceGains previous) {
  if (samples < min_samples || sum_r == 0 || sum_g == 0 || sum_b == 0) {
    return previous;
  }
  const float red = clampGain(static_cast<float>(sum_g) / static_cast<float>(sum_r));
  const float blue = clampGain(static_cast<float>(sum_g) / static_cast<float>(sum_b));
  // Blend halfway to avoid oscillation between iterations.
  WhiteBalanceGains next;
  next.red = clampGain((previous.red + red) * 0.5f);
  next.blue = clampGain((previous.blue + blue) * 0.5f);
  return next;
}

inline bool gainsSettled(WhiteBalanceGains a, WhiteBalanceGains b) {
  auto close = [](float x, float y) {
    const float d = x > y ? x - y : y - x;
    return d <= 0.02f * (x > y ? x : y);
  };
  return close(a.red, b.red) && close(a.blue, b.blue);
}

// Colour-correction matrix of Espressif's OV02C10 IPA tuning (2320 K entry).
constexpr float kBaseCcm[3][3] = {
    {1.408f, -0.094f, -0.314f},
    {-0.130f, 1.280f, -0.150f},
    {-0.072f, -0.173f, 1.245f},
};

// out = base * diag(red, 1, blue): the gains act on the sensor channels before
// the colour correction. Entries stay inside the ISP's (-4, 4) range.
inline void buildCcmWithGains(const float base[3][3], WhiteBalanceGains gains,
                              float out[3][3]) {
  const float column_gain[3] = {clampGain(gains.red), 1.0f, clampGain(gains.blue)};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      float value = base[row][col] * column_gain[col];
      if (value > 3.99f) value = 3.99f;
      if (value < -3.99f) value = -3.99f;
      out[row][col] = value;
    }
  }
}

// Gamma exponent of the OV02C10 IPA tuning ("gamma_param": 0.688).
constexpr float kGammaExponent = 0.688f;

// ---------------------------------------------------------------------------
// User image controls (Web Admin). Integers so they validate, persist and
// compare exactly. The defaults reproduce the automatic pipeline unchanged.
// ---------------------------------------------------------------------------
constexpr int kImageAdjustMin = -50;  // Brightness, contrast, red and blue.
constexpr int kImageAdjustMax = 50;
constexpr int kSaturationMin = 0;     // Percent.
constexpr int kSaturationMax = 200;
constexpr int kSaturationDefault = 100;
// Max. gain setting in percent: 100 = the full sensor plus digital gain range
// (the default, the behaviour before the setting), 0 = no amplification.
constexpr int kGainLimitMin = 0;
constexpr int kGainLimitMax = 100;
constexpr int kGainLimitDefault = 100;
// Auto-exposure target bounds after the brightness adjustment.
constexpr uint32_t kMinAeTarget = 40;
constexpr uint32_t kMaxAeTarget = 220;
// Strongest blend toward the S-curve at contrast +-50; stays monotonic.
constexpr float kMaxContrastBlend = 0.6f;
// Rec.601 luma weights for the saturation blend.
constexpr float kLumaWeights[3] = {0.299f, 0.587f, 0.114f};

struct ImageSettings {
  int8_t brightness = 0;  // -50..50, scales the auto-exposure target.
  int8_t contrast = 0;    // -50..50, S-curve folded into the gamma curve.
  uint8_t saturation = kSaturationDefault;  // 0..200 %, blends the CCM toward luma.
  int8_t red = 0;         // -50..50 %, on top of the automatic white balance.
  int8_t blue = 0;        // -50..50 %.
  uint8_t gain = kGainLimitDefault;  // 0..100 %, caps sensor and digital gain.
};

inline bool isValidImageAdjust(long value) {
  return value >= kImageAdjustMin && value <= kImageAdjustMax;
}

inline bool isValidSaturation(long value) {
  return value >= kSaturationMin && value <= kSaturationMax;
}

// Strict decimal integer for the Web Admin arguments: optional sign and one
// to three digits, nothing else (no spaces, no fractions, no hex).
inline bool parseImageInteger(const char* text, long* out) {
  if (!text) return false;
  const char* p = text;
  bool negative = false;
  if (*p == '-' || *p == '+') {
    negative = *p == '-';
    ++p;
  }
  long value = 0;
  int digits = 0;
  for (; *p; ++p) {
    if (*p < '0' || *p > '9' || ++digits > 3) return false;
    value = value * 10 + (*p - '0');
  }
  if (digits == 0) return false;
  if (out) *out = negative ? -value : value;
  return true;
}

inline int clampImageAdjust(long value) {
  if (value < kImageAdjustMin) return kImageAdjustMin;
  if (value > kImageAdjustMax) return kImageAdjustMax;
  return static_cast<int>(value);
}

inline int clampSaturation(long value) {
  if (value < kSaturationMin) return kSaturationMin;
  if (value > kSaturationMax) return kSaturationMax;
  return static_cast<int>(value);
}

inline int clampGainLimit(long value) {
  if (value < kGainLimitMin) return kGainLimitMin;
  if (value > kGainLimitMax) return kGainLimitMax;
  return static_cast<int>(value);
}

// Clamps stored or requested values into the supported ranges.
inline ImageSettings makeImageSettings(long brightness, long contrast,
                                       long saturation, long red, long blue,
                                       long gain) {
  ImageSettings settings;
  settings.brightness = static_cast<int8_t>(clampImageAdjust(brightness));
  settings.contrast = static_cast<int8_t>(clampImageAdjust(contrast));
  settings.saturation = static_cast<uint8_t>(clampSaturation(saturation));
  settings.red = static_cast<int8_t>(clampImageAdjust(red));
  settings.blue = static_cast<int8_t>(clampImageAdjust(blue));
  settings.gain = static_cast<uint8_t>(clampGainLimit(gain));
  return settings;
}

inline bool sameImageSettings(const ImageSettings& a, const ImageSettings& b) {
  return a.brightness == b.brightness && a.contrast == b.contrast &&
         a.saturation == b.saturation && a.red == b.red && a.blue == b.blue &&
         a.gain == b.gain;
}

// Brightness applied to the image at once: the linear gain whose gamma
// response equals the AE target change of adjustedAeTarget(), so the auto
// exposure finds its new target already met and never undoes it. 0 is 1.
inline float brightnessLinearGain(int brightness, float exponent) {
  const int value = clampImageAdjust(brightness);
  if (value == 0) return 1.0f;
  const float ratio = static_cast<float>(100 + value) / 100.0f;
  return powf(ratio, 1.0f / (exponent > 0.05f ? exponent : 1.0f));
}

// Brightness scales the AE target linearly: -50 halves it, +50 adds half.
inline uint32_t adjustedAeTarget(uint32_t base_target, int brightness) {
  const int64_t factor = 100 + clampImageAdjust(brightness);
  int64_t target = (static_cast<int64_t>(base_target) * factor + 50) / 100;
  if (target < static_cast<int64_t>(kMinAeTarget)) target = kMinAeTarget;
  if (target > static_cast<int64_t>(kMaxAeTarget)) target = kMaxAeTarget;
  return static_cast<uint32_t>(target);
}

// Gentle S-curve around mid-grey on a normalized value t in [0, 1]:
// t + a * (smoothstep(t) - t). 0 and 1 and 0.5 stay fixed; for |a| <= 1 the
// derivative 1 + a * (6t(1 - t) - 1) never becomes negative.
inline float applyContrastCurve(float t, int contrast) {
  if (!(t == t) || t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  const int value = clampImageAdjust(contrast);
  if (value == 0) return t;
  const float blend = kMaxContrastBlend * static_cast<float>(value) /
                      static_cast<float>(kImageAdjustMax);
  const float smooth = t * t * (3.0f - 2.0f * t);
  const float out = t + blend * (smooth - t);
  if (out <= 0.0f) return 0.0f;
  if (out >= 1.0f) return 1.0f;
  return out;
}

// One point of the ISP gamma curve: removes the sensor pedestal, applies the
// digital gain (linear, before gamma), the tuning gamma and then the contrast
// S-curve. x is in [0, 256]; the result stays below 256 as
// esp_isp_gamma_fill_curve_points() requires. A gain of exactly 1 leaves the
// curve bit for bit unchanged.
inline uint32_t gammaLutValue(uint32_t x, uint32_t black_level, float exponent,
                              int contrast, float linear_gain = 1.0f) {
  const uint32_t black = black_level > 254 ? 254 : black_level;
  const uint32_t value = x > 255 ? 255 : x;
  float normalized =
      value <= black ? 0.0f : static_cast<float>(value - black) / static_cast<float>(255 - black);
  if (linear_gain != 1.0f) {
    normalized *= linear_gain > 0.0f ? linear_gain : 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
  }
  const float curved = applyContrastCurve(powf(normalized, exponent), contrast);
  const float y = 255.0f * curved + 0.5f;
  return y >= 255.0f ? 255u : static_cast<uint32_t>(y);
}

// ---------------------------------------------------------------------------
// Digital gain after the sensor limits. When exposure time and analog gain are
// both at their maximum and the image is still darker than the AE target
// (which the brightness setting scales), the missing part is applied as a
// linear gain in the gamma curve, in quarter-EV steps up to 8x. It only works
// while the AE statistics are taken after gamma, so the measured luma already
// contains it.
// ---------------------------------------------------------------------------
constexpr uint8_t kDigitalGainStepsPerEv = 4;
constexpr uint8_t kMaxDigitalGainStep = 12;  // 2^(12/4) = 8x linear.
constexpr int kMaxDigitalGainJump = 4;       // Steps per AE evaluation.

inline float digitalGainForStep(uint8_t step) {
  if (step == 0) return 1.0f;
  if (step > kMaxDigitalGainStep) step = kMaxDigitalGainStep;
  return powf(2.0f, static_cast<float>(step) / static_cast<float>(kDigitalGainStepsPerEv));
}

// Next digital gain step from the post-gamma mean luma. Too bright: lower the
// digital gain before any sensor change. Too dark: raise it only while the
// sensor is already at its brighter limit. The jump follows the missing ratio
// back through the gamma exponent, at least one and at most
// kMaxDigitalGainJump steps per call.
// max_step: the Max. gain setting (gainLimitsFor()); a step above it drops to
// it at once.
inline uint8_t nextDigitalGainStep(uint8_t step, uint32_t mean_luma, uint32_t target_luma,
                                   uint32_t tolerance, float exponent,
                                   bool sensor_at_brighter_limit,
                                   uint8_t max_step = kMaxDigitalGainStep) {
  if (max_step > kMaxDigitalGainStep) max_step = kMaxDigitalGainStep;
  if (step > max_step) return max_step;
  const uint32_t low = target_luma > tolerance ? target_luma - tolerance : 0;
  const uint32_t high = target_luma + tolerance;
  const bool too_bright = mean_luma > high;
  const bool too_dark = mean_luma < low && sensor_at_brighter_limit;
  if (too_bright && step == 0) return 0;
  if (!too_bright && !(too_dark && step < max_step)) return step;
  const float ratio = static_cast<float>(target_luma ? target_luma : 1) /
                      static_cast<float>(mean_luma ? mean_luma : 1);
  const float ev = log2f(ratio) / (exponent > 0.05f ? exponent : 1.0f);
  int delta = static_cast<int>(lroundf(ev * static_cast<float>(kDigitalGainStepsPerEv)));
  if (too_dark && delta < 1) delta = 1;
  if (too_bright && delta > -1) delta = -1;
  if (delta > kMaxDigitalGainJump) delta = kMaxDigitalGainJump;
  if (delta < -kMaxDigitalGainJump) delta = -kMaxDigitalGainJump;
  int next = static_cast<int>(step) + delta;
  if (next < 0) next = 0;
  if (next > max_step) next = max_step;
  return static_cast<uint8_t>(next);
}

// Limits of the Max. gain setting. 1x to the full amplification (sensor
// maximum times the 8x digital gain) is mapped on a log scale, so every
// percent changes the brightness by the same ratio. The sensor gain comes
// first (less noise than the same digital gain), the rest is digital.
struct GainLimits {
  uint16_t sensor_gain_x16;        // Analog stage (ExposureLimits::max_gain_x16).
  uint16_t sensor_total_gain_x16;  // Analog plus sensor digital gain.
  uint8_t max_digital_step;        // Digital gain in the gamma curve.
};

inline GainLimits gainLimitsFor(long percent, uint16_t min_gain_x16, uint16_t max_gain_x16,
                                uint16_t max_total_gain_x16) {
  const int value = clampGainLimit(percent);
  const uint16_t sensor_max =
      max_total_gain_x16 > max_gain_x16 ? max_total_gain_x16 : max_gain_x16;
  if (value >= kGainLimitMax) {
    return GainLimits{max_gain_x16, sensor_max, kMaxDigitalGainStep};
  }
  const float sensor_x = static_cast<float>(sensor_max) / 16.0f;
  const float full_x = sensor_x * digitalGainForStep(kMaxDigitalGainStep);
  const float limit_x = powf(full_x, static_cast<float>(value) / 100.0f);
  const float sensor_limit_x = limit_x < sensor_x ? limit_x : sensor_x;
  long total = lroundf(sensor_limit_x * 16.0f);
  if (total < min_gain_x16) total = min_gain_x16;
  if (total > sensor_max) total = sensor_max;
  const uint16_t total_x16 = static_cast<uint16_t>(total);
  const uint16_t analog_x16 = total_x16 < max_gain_x16 ? total_x16 : max_gain_x16;
  const float rest = limit_x / (static_cast<float>(total_x16) / 16.0f);
  int step = 0;
  if (rest > 1.0f) {
    step = static_cast<int>(
        floorf(log2f(rest) * static_cast<float>(kDigitalGainStepsPerEv) + 0.001f));
  }
  if (step < 0) step = 0;
  if (step > kMaxDigitalGainStep) step = kMaxDigitalGainStep;
  return GainLimits{analog_x16, total_x16, static_cast<uint8_t>(step)};
}

// ---------------------------------------------------------------------------
// Sensor readout orientation. A 180 degree turn is a horizontal plus a
// vertical flip; the user mirror is one more horizontal flip. Flips commute,
// so the result is the same image the former pixel pass produced.
// ---------------------------------------------------------------------------
struct SensorOrientation {
  bool mirror = false;
  bool flip = false;
};

// quarter_turn: the frame is turned by 90 degrees after readout, so sensor
// columns become image rows. An image mirror is then a sensor flip and the
// other way round; 180 degrees stays both.
inline SensorOrientation desiredOrientation(bool rotated_180, bool user_mirror,
                                           bool quarter_turn = false) {
  SensorOrientation orientation;
  orientation.mirror = rotated_180 != user_mirror;
  orientation.flip = rotated_180;
  if (quarter_turn) {
    const bool mirror = orientation.mirror;
    orientation.mirror = orientation.flip;
    orientation.flip = mirror;
  }
  return orientation;
}

inline uint8_t orientationCode(const SensorOrientation& orientation) {
  return static_cast<uint8_t>((orientation.flip ? 2u : 0u) | (orientation.mirror ? 1u : 0u));
}

// User red/blue correction as a multiplier on the automatic gain.
inline float userChannelGain(int percent) {
  return 1.0f + static_cast<float>(clampImageAdjust(percent)) / 100.0f;
}

// Final CCM: S * base * diag(red, 1, blue). The column gains are the automatic
// white balance times the user correction; S blends every output channel
// toward Rec.601 luma, S = s*I + (1 - s) * [w; w; w]. S rows sum to 1, so a
// neutral colour stays neutral and equal row sums of the corrected matrix
// stay equal. At the defaults (s = 1, no correction) the result is bit-for-bit
// buildCcmWithGains().
inline void buildImageCcm(const float base[3][3], WhiteBalanceGains gains,
                          const ImageSettings& image, float out[3][3]) {
  const float column_gain[3] = {clampGain(gains.red) * userChannelGain(image.red), 1.0f,
                                clampGain(gains.blue) * userChannelGain(image.blue)};
  float corrected[3][3];
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      corrected[row][col] = base[row][col] * column_gain[col];
    }
  }
  const float s = static_cast<float>(clampSaturation(image.saturation)) /
                  static_cast<float>(kSaturationDefault);
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      float value = 0.0f;
      for (int k = 0; k < 3; ++k) {
        const float weight = (1.0f - s) * kLumaWeights[k] + (row == k ? s : 0.0f);
        value += weight * corrected[k][col];
      }
      if (value > 3.99f) value = 3.99f;
      if (value < -3.99f) value = -3.99f;
      out[row][col] = value;
    }
  }
}

}  // namespace local_camera_contract

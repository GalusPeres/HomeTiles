// Host regression for the built-in camera MQTT contract shared with the Home
// Assistant Bridge: request validation, retained status and error payloads,
// the Bridge capability truth table, rate limiting and the pure image helpers
// used by the capture worker. A second program exercises the ArduinoJson
// request parser when the pinned library is installed.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const out = path.join(root, 'build/tests/local-camera-contract');
fs.mkdirSync(out, {recursive: true});

function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}

function compileAndRun(name, source, includes) {
  const cpp = path.join(out, `${name}.cpp`);
  const exe = path.join(out, process.platform === 'win32' ? `${name}.exe` : name);
  fs.writeFileSync(cpp, source);
  const args = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', root];
  for (const include of includes) args.push('-I', include);
  const build = spawnSync(compiler, [...args, cpp, '-o', exe], {encoding: 'utf8'});
  assert.equal(build.status, 0, `${name} did not compile:\n${build.stdout}${build.stderr}`);
  const run = spawnSync(exe, [], {encoding: 'utf8'});
  assert.equal(run.status, 0, `${name} failed:\n${run.stdout}${run.stderr}`);
  return run.stdout;
}

const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: local camera contract test needs a host C++ compiler');
  process.exit(0);
}

const contractProgram = String.raw`
#include "src/video/local_camera/local_camera_contract.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace local_camera_contract;

static std::string status(PublicState state, const char* sensor, const char* error) {
  // The size comes from the board's sensor mode (1280x720 on the OV02C10 boards).
  StatusFields fields;
  fields.width = 1280;
  fields.height = 720;
  fields.state = state;
  fields.sensor = sensor;
  fields.error = error;
  char buffer[256];
  const size_t length = buildStatusJson(buffer, sizeof(buffer), fields);
  assert(length == strlen(buffer));
  return buffer;
}

int main() {
  // Request identifiers: 16-32 lowercase hexadecimal characters.
  assert(isValidRequestId("0123456789abcdef", 16));
  assert(isValidRequestId("0123456789abcdef0123456789abcdef", 32));
  assert(!isValidRequestId("0123456789abcde", 15));
  assert(!isValidRequestId("0123456789abcdef0123456789abcdef0", 33));
  assert(!isValidRequestId("0123456789ABCDEF", 16));
  assert(!isValidRequestId("0123456789abcdeg", 16));
  assert(!isValidRequestId("0123456789abc/ef", 16));

  // Pause: disabled state plus an additive paused flag; absent otherwise.
  {
    StatusFields paused;
    paused.width = 1280;
    paused.height = 720;
    paused.state = PublicState::Disabled;
    paused.paused = true;
    char buffer[256];
    assert(buildStatusJson(buffer, sizeof(buffer), paused) > 0);
    const std::string text(buffer);
    assert(text.find("\"state\":\"disabled\"") != std::string::npos);
    assert(text.find(",\"paused\":true}") != std::string::npos);
    paused.paused = false;
    assert(buildStatusJson(buffer, sizeof(buffer), paused) > 0);
    assert(std::string(buffer).find("paused") == std::string::npos);
  }
  // Quarter-turn boards: the JPEG size as sent plus the clockwise turn the
  // Bridge applies; landscape boards (0) send no field at all.
  {
    StatusFields turned;
    turned.width = 544;
    turned.height = 960;
    turned.state = PublicState::Ready;
    turned.rotate = 90;
    char buffer[256];
    assert(buildStatusJson(buffer, sizeof(buffer), turned) > 0);
    const std::string text(buffer);
    assert(text.find("\"width\":544,\"height\":960") != std::string::npos);
    assert(text.find(",\"rotate\":90}") != std::string::npos);
    turned.rotate = 0;
    assert(buildStatusJson(buffer, sizeof(buffer), turned) > 0);
    assert(std::string(buffer).find("rotate") == std::string::npos);
    turned.rotate = 45;  // Not a quarter turn: never announced.
    assert(buildStatusJson(buffer, sizeof(buffer), turned) > 0);
    assert(std::string(buffer).find("rotate") == std::string::npos);
  }

  SnapshotRequest request;
  assert(validateRequestFields(1, "snapshot", "00112233445566778899aabbccddeeff",
                               true, 131072, &request) == RequestStatus::Ok);
  assert(strcmp(request.id, "00112233445566778899aabbccddeeff") == 0);
  assert(request.max_bytes == 131072);
  assert(validateRequestFields(1, "snapshot", "00112233445566778899aabbccddeeff",
                               true, 65536, &request) == RequestStatus::Ok);
  assert(request.max_bytes == 65536);
  // A larger request is clamped to the advertised panel maximum.
  assert(validateRequestFields(1, "snapshot", "00112233445566778899aabbccddeeff",
                               true, 1048576, &request) == RequestStatus::Ok);
  assert(request.max_bytes == kPanelMaxJpegBytes);
  assert(validateRequestFields(1, "snapshot", "00112233445566778899aabbccddeeff",
                               false, 0, &request) == RequestStatus::Ok);
  assert(request.max_bytes == kPanelMaxJpegBytes);
  assert(validateRequestFields(2, "snapshot", "00112233445566778899aabbccddeeff",
                               false, 0, &request) == RequestStatus::UnsupportedVersion);
  assert(validateRequestFields(1, "stream", "00112233445566778899aabbccddeeff",
                               false, 0, &request) == RequestStatus::UnsupportedOperation);
  assert(validateRequestFields(1, nullptr, "00112233445566778899aabbccddeeff",
                               false, 0, &request) == RequestStatus::UnsupportedOperation);
  assert(validateRequestFields(1, "snapshot", "short", false, 0, &request) ==
         RequestStatus::InvalidId);
  assert(validateRequestFields(1, "snapshot", "00112233445566778899aabbccddeeff",
                               true, 1024, &request) == RequestStatus::InvalidMaxBytes);
  assert(validateRequestFields(1, "snapshot", "00112233445566778899aabbccddeeff",
                               true, -1, &request) == RequestStatus::InvalidMaxBytes);

  // Retained status payload, exactly as agreed with the Bridge.
  assert(status(PublicState::Ready, "ov02c10", nullptr) ==
         "{\"v\":1,\"state\":\"ready\",\"width\":1280,\"height\":720,"
         "\"format\":\"jpeg\",\"max_bytes\":131072,\"min_interval_ms\":1000,"
         "\"sensor\":\"ov02c10\"}");
  assert(status(PublicState::Disabled, nullptr, "sensor_unavailable") ==
         "{\"v\":1,\"state\":\"disabled\",\"width\":1280,\"height\":720,"
         "\"format\":\"jpeg\",\"max_bytes\":131072,\"min_interval_ms\":1000}");
  assert(status(PublicState::Error, nullptr, "sensor_unavailable") ==
         "{\"v\":1,\"state\":\"error\",\"width\":1280,\"height\":720,"
         "\"format\":\"jpeg\",\"max_bytes\":131072,\"min_interval_ms\":1000,"
         "\"error\":\"sensor_unavailable\"}");
  // Non-token values are dropped rather than escaped into JSON.
  assert(status(PublicState::Error, "bad\"name", "x y").find("bad") == std::string::npos);
  char tiny[16];
  StatusFields fields;
  assert(buildStatusJson(tiny, sizeof(tiny), fields) == 0 && tiny[0] == '\0');

  const struct { ErrorCode code; const char* json; } errors[] = {
      {ErrorCode::Busy, "{\"v\":1,\"error\":\"busy\"}"},
      {ErrorCode::Disabled, "{\"v\":1,\"error\":\"disabled\"}"},
      {ErrorCode::SensorUnavailable, "{\"v\":1,\"error\":\"sensor_unavailable\"}"},
      {ErrorCode::EncoderBusy, "{\"v\":1,\"error\":\"encoder_busy\"}"},
      {ErrorCode::TooLarge, "{\"v\":1,\"error\":\"too_large\"}"},
      {ErrorCode::RateLimited, "{\"v\":1,\"error\":\"rate_limited\"}"},
  };
  for (const auto& entry : errors) {
    char buffer[64];
    assert(buildErrorJson(buffer, sizeof(buffer), entry.code) == strlen(entry.json));
    assert(strcmp(buffer, entry.json) == 0);
  }
  char none[8];
  assert(buildErrorJson(none, sizeof(none), ErrorCode::None) == 0);

  // Topics.
  char topic[96];
  assert(buildTopic(topic, sizeof(topic), "hometiles", kCommandLeaf));
  assert(strcmp(topic, "hometiles/cmnd/local_camera") == 0);
  assert(buildTopic(topic, sizeof(topic), "hometiles", kStatusLeaf));
  assert(strcmp(topic, "hometiles/stat/local_camera") == 0);
  assert(buildTopic(topic, sizeof(topic), "hometiles", kImageLeaf, "0123456789abcdef"));
  assert(strcmp(topic, "hometiles/stat/local_camera/image/0123456789abcdef") == 0);
  assert(buildTopic(topic, sizeof(topic), "hometiles", kErrorLeaf, "0123456789abcdef"));
  assert(strcmp(topic, "hometiles/stat/local_camera/error/0123456789abcdef") == 0);
  char short_topic[12];
  assert(!buildTopic(short_topic, sizeof(short_topic), "hometiles", kErrorLeaf, "x"));

  // Capability: exact profile, user opt-in and a detected sensor.
  for (int mask = 0; mask < 8; ++mask) {
    const bool supported = mask & 1, enabled = mask & 2, detected = mask & 4;
    assert(bridgeCapability(supported, enabled, detected) == (mask == 7));
  }

  // Rate limiting: 1000 ms after the last accepted request, wrap-safe.
  RequestRateLimiter limiter;
  assert(limiter.wouldAccept(5000));
  limiter.markAccepted(5000);
  assert(!limiter.wouldAccept(5999));
  assert(limiter.wouldAccept(6000));
  limiter.markAccepted(0xFFFFFF00u);
  assert(!limiter.wouldAccept(0x000002E7u));
  assert(limiter.wouldAccept(0x000002E8u));

  // JPEG framing.
  const uint8_t jpeg[] = {0xFF, 0xD8, 0x00, 0xFF, 0xD9};
  const uint8_t broken[] = {0xFF, 0xD8, 0x00, 0xFF, 0x00};
  assert(looksLikeCompleteJpeg(jpeg, sizeof(jpeg)));
  assert(!looksLikeCompleteJpeg(broken, sizeof(broken)));
  assert(!looksLikeCompleteJpeg(jpeg, 3));

  // Centred crop 1288x728 -> 1280x720 in place, one byte per "pixel" per axis.
  const uint32_t sw = 1288, sh = 728, dw = 1280, dh = 720;
  std::vector<uint8_t> image(sw * sh * 2);
  for (uint32_t y = 0; y < sh; ++y) {
    for (uint32_t x = 0; x < sw; ++x) {
      image[(y * sw + x) * 2] = static_cast<uint8_t>(x);
      image[(y * sw + x) * 2 + 1] = static_cast<uint8_t>(y);
    }
  }
  assert(compactCenterCrop(image.data(), image.size(), sw, sh, dw, dh, 2));
  for (uint32_t y = 0; y < dh; y += 37) {
    for (uint32_t x = 0; x < dw; x += 13) {
      assert(image[(y * dw + x) * 2] == static_cast<uint8_t>(x + 4));
      assert(image[(y * dw + x) * 2 + 1] == static_cast<uint8_t>(y + 4));
    }
  }
  assert(!compactCenterCrop(image.data(), 100, sw, sh, dw, dh, 2));
  assert(!compactCenterCrop(image.data(), image.size(), 100, 100, 200, 50, 2));

  // Auto exposure: converge inside the window, prefer lines, then gain.
  ExposureLimits limits;
  ExposureSetting setting;
  setting.lines = 1132;
  setting.gain_x16 = 128;
  ExposureStep step = stepAutoExposure(setting, 115, 115, 12, limits);
  assert(step.converged && step.next.lines == 1132 && step.next.gain_x16 == 128);
  step = stepAutoExposure(setting, 230, 115, 12, limits);
  assert(!step.converged);
  assert(step.next.lines == 1132 && step.next.gain_x16 == 64);  // halve the product
  setting.gain_x16 = 16;
  step = stepAutoExposure(setting, 255, 115, 12, limits);
  assert(step.next.gain_x16 == 16 && step.next.lines < 1132);  // cut exposure time
  setting.lines = 4;
  step = stepAutoExposure(setting, 255, 115, 12, limits);
  assert(step.limited && step.converged);  // Nothing darker is possible.
  setting.lines = 1132;
  setting.gain_x16 = 248;
  step = stepAutoExposure(setting, 3, 115, 12, limits);
  assert(step.limited && step.converged);  // Nothing brighter is possible.
  setting.lines = 100;
  setting.gain_x16 = 16;
  step = stepAutoExposure(setting, 0, 115, 12, limits);
  assert(!step.converged && step.next.lines == 400 && step.next.gain_x16 == 16);  // x4 max

  // Staged exposure: frame-interval exposure with analog gain, then sensor
  // digital gain, then the longer (night) exposure; back in reverse order.
  {
    ExposureStages stages;
    stages.normal = ExposureLimits{4, 2197, 16, 248};
    stages.night_max_lines = 4624;
    stages.max_total_gain_x16 = 992;
    ExposureSetting s;
    s.lines = 1000;
    s.gain_x16 = 16;
    ExposureStep st = stepStagedExposure(s, 60, 115, 12, stages);
    assert(!st.limited && st.next.lines > 1000 && st.next.lines <= 2197 && st.next.gain_x16 <= 248);
    s.lines = 2197;
    s.gain_x16 = 248;
    st = stepStagedExposure(s, 20, 115, 12, stages);  // Stage 0 full: digital gain.
    assert(!st.limited && st.next.lines == 2197 && st.next.gain_x16 > 248 && st.next.gain_x16 <= 992);
    s.gain_x16 = 992;
    st = stepStagedExposure(s, 20, 115, 12, stages);  // Digital full: night exposure.
    assert(!st.limited && st.next.lines > 2197 && st.next.lines <= 4624 && st.next.gain_x16 == 992);
    s.lines = 4624;
    st = stepStagedExposure(s, 20, 115, 12, stages);  // Everything at the limit.
    assert(st.limited && st.next.lines == 4624 && st.next.gain_x16 == 992);
    st = stepStagedExposure(s, 200, 115, 12, stages);  // Too bright: night exposure first.
    assert(st.next.gain_x16 == 992 && st.next.lines < 4624 && st.next.lines >= 2197);
    s.lines = 2197;
    s.gain_x16 = 600;
    st = stepStagedExposure(s, 200, 115, 12, stages);  // Then the digital gain.
    assert(st.next.lines == 2197 && st.next.gain_x16 < 600 && st.next.gain_x16 >= 248);
    s.gain_x16 = 248;
    st = stepStagedExposure(s, 200, 115, 12, stages);  // Then the normal range.
    assert(st.next.lines <= 2197 && st.next.gain_x16 < 248);
    // A fixed mode has no night stage: digital gain follows the frame interval.
    ExposureStages fixed = stages;
    fixed.night_max_lines = 2197;
    s.lines = 2197;
    s.gain_x16 = 248;
    st = stepStagedExposure(s, 20, 115, 12, fixed);
    assert(!st.limited && st.next.lines == 2197 && st.next.gain_x16 > 248);
    // Without a digital stage the night exposure follows the analog gain.
    ExposureStages analog_only = stages;
    analog_only.max_total_gain_x16 = 248;
    s.lines = 2197;
    s.gain_x16 = 248;
    st = stepStagedExposure(s, 20, 115, 12, analog_only);
    assert(!st.limited && st.next.lines > 2197 && st.next.gain_x16 == 248);
    s.lines = 4624;
    st = stepStagedExposure(s, 20, 115, 12, analog_only);
    assert(st.limited);
    // Converges from darkness: sensor gain above analog only at the longest exposure.
    ExposureSetting e;
    e.lines = 500;
    e.gain_x16 = 16;
    for (int i = 0; i < 40; ++i) {
      const double signal = 0.00002 * e.lines * e.gain_x16;  // Dark scene model.
      const uint32_t luma = static_cast<uint32_t>(255.0 * (signal > 1.0 ? 1.0 : signal));
      const ExposureStep next = stepStagedExposure(e, luma, 115, 12, stages);
      // Real darkness only: the longer exposure needs the full gain.
      if (next.next.lines > 2197) assert(next.next.gain_x16 == 992);
      if (next.next.gain_x16 > 248) assert(next.next.lines >= 2197);
      e = next.next;
    }
    const double final_signal = 0.00002 * e.lines * e.gain_x16;
    const uint32_t final_luma = static_cast<uint32_t>(255.0 * (final_signal > 1.0 ? 1.0 : final_signal));
    assert(final_luma >= 103 && final_luma <= 127);
  }

  int flat[5][5];
  for (auto& row : flat) for (int& value : row) value = 100;
  assert(weightedMeanLuma(flat) == 100);
  flat[2][2] = 300;  // Clamped and centre-weighted.
  assert(weightedMeanLuma(flat) > 100 && weightedMeanLuma(flat) < 255);

  // Gray world: gains move halfway toward G/R and G/B, bounded.
  WhiteBalanceGains gains;
  WhiteBalanceGains next = grayWorldGains(1000, 2000, 4000, 5000, 1000, gains);
  assert(next.red > 1.49f && next.red < 1.51f);
  assert(next.blue > 0.74f && next.blue < 0.76f);
  assert(!gainsSettled(gains, next));
  WhiteBalanceGains kept = grayWorldGains(1000, 2000, 4000, 10, 1000, next);
  assert(kept.red == next.red && kept.blue == next.blue);
  next = grayWorldGains(1, 1000000, 1, 5000, 1000, gains);
  assert(next.red <= kMaxWhiteBalanceGain && next.blue <= kMaxWhiteBalanceGain);

  float ccm[3][3];
  WhiteBalanceGains strong;
  strong.red = 3.0f;
  strong.blue = 3.0f;
  buildCcmWithGains(kBaseCcm, strong, ccm);
  for (auto& row : ccm) for (float value : row) assert(value < 4.0f && value > -4.0f);
  assert(ccm[1][1] == kBaseCcm[1][1]);  // Green column is untouched.

  // --- User image controls -------------------------------------------------
  // Defaults reproduce the automatic pipeline bit for bit.
  const ImageSettings defaults;
  assert(defaults.brightness == 0 && defaults.contrast == 0 &&
         defaults.saturation == 100 && defaults.red == 0 && defaults.blue == 0);
  {
    const WhiteBalanceGains samples[] = {{1.0f, 1.0f}, {1.37f, 0.81f}, {3.0f, 0.5f}, {0.2f, 9.0f}};
    for (const WhiteBalanceGains& wb : samples) {
      float automatic[3][3];
      float image[3][3];
      buildCcmWithGains(kBaseCcm, wb, automatic);
      buildImageCcm(kBaseCcm, wb, defaults, image);
      assert(std::memcmp(automatic, image, sizeof(image)) == 0);
    }
  }
  // Gamma: contrast 0 is exactly the previous pedestal + tuning gamma curve.
  for (uint32_t black : {0u, 16u, 64u}) {
    for (uint32_t x = 0; x <= 256; ++x) {
      const uint32_t value = x > 255 ? 255 : x;
      const float normalized = value <= black ? 0.0f
          : static_cast<float>(value - black) / static_cast<float>(255 - black);
      const float y = 255.0f * powf(normalized, kGammaExponent) + 0.5f;
      const uint32_t legacy = y >= 255.0f ? 255u : static_cast<uint32_t>(y);
      assert(gammaLutValue(x, black, kGammaExponent, 0) == legacy);
    }
  }
  // Every contrast gives a monotonic curve with fixed black, white and mid-grey.
  for (int contrast = -50; contrast <= 50; ++contrast) {
    uint32_t previous = 0;
    for (uint32_t x = 0; x <= 256; ++x) {
      const uint32_t y = gammaLutValue(x, 16, kGammaExponent, contrast);
      assert(y <= 255 && y >= previous);
      previous = y;
    }
    assert(gammaLutValue(0, 16, kGammaExponent, contrast) == 0);
    assert(gammaLutValue(256, 16, kGammaExponent, contrast) == 255);
    float last = 0.0f;
    for (int i = 0; i <= 1000; ++i) {
      const float t = static_cast<float>(i) / 1000.0f;
      const float out = applyContrastCurve(t, contrast);
      assert(out >= 0.0f && out <= 1.0f && out + 1e-6f >= last);
      last = out;
    }
    const float mid = applyContrastCurve(0.5f, contrast);
    assert(mid > 0.4999f && mid < 0.5001f);
  }
  // Positive contrast darkens shadows and brightens highlights; negative
  // does the opposite; out-of-range values clamp to +-50.
  assert(applyContrastCurve(0.25f, 50) < 0.25f && applyContrastCurve(0.75f, 50) > 0.75f);
  assert(applyContrastCurve(0.25f, -50) > 0.25f && applyContrastCurve(0.75f, -50) < 0.75f);
  assert(applyContrastCurve(0.25f, 50) < applyContrastCurve(0.25f, 20));
  assert(applyContrastCurve(0.25f, 120) == applyContrastCurve(0.25f, 50));
  assert(applyContrastCurve(-1.0f, 10) == 0.0f && applyContrastCurve(2.0f, 10) == 1.0f);

  // Sensor orientation: the readout flips reproduce the former pixel pass
  // (centre image turned by 180 degrees when rotated, then mirrored per row).
  {
    constexpr int W = 5;
    constexpr int H = 3;
    for (int rotated = 0; rotated < 2; ++rotated) {
      for (int user_mirror = 0; user_mirror < 2; ++user_mirror) {
        const SensorOrientation o = desiredOrientation(rotated != 0, user_mirror != 0);
        assert(o.flip == (rotated != 0) && o.mirror == ((rotated != 0) != (user_mirror != 0)));
        int old_path[H][W];
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) old_path[y][x] = y * W + x;
        if (rotated) {
          int* p = &old_path[0][0];
          std::reverse(p, p + W * H);
        }
        if (user_mirror) for (auto& row : old_path) std::reverse(row, row + W);
        for (int y = 0; y < H; ++y) {
          for (int x = 0; x < W; ++x) {
            const int sy = o.flip ? H - 1 - y : y;
            const int sx = o.mirror ? W - 1 - x : x;
            assert(old_path[y][x] == sy * W + sx);
          }
        }
      }
    }
    assert(orientationCode(desiredOrientation(false, false)) == 0);
    assert(orientationCode(desiredOrientation(false, true)) == 1);
    assert(orientationCode(desiredOrientation(true, true)) == 2);
    assert(orientationCode(desiredOrientation(true, false)) == 3);
  }

  // Quarter turn: the sensor flips before the clockwise PPA turn give the
  // same 180 degree turn and horizontal mirror of the turned image.
  {
    constexpr int W = 5;  // Sensor frame; the turned image is H wide and W tall.
    constexpr int H = 3;
    for (int rotated = 0; rotated < 2; ++rotated) {
      for (int user_mirror = 0; user_mirror < 2; ++user_mirror) {
        const SensorOrientation o = desiredOrientation(rotated != 0, user_mirror != 0, true);
        for (int r = 0; r < W; ++r) {
          for (int c = 0; c < H; ++c) {
            // Wanted: the unflipped turned image, turned by 180 and mirrored.
            int br = r;
            int bc = c;
            if (rotated) {
              br = W - 1 - br;
              bc = H - 1 - bc;
            }
            if (user_mirror) bc = H - 1 - bc;
            const int wanted = (H - 1 - bc) * W + br;
            // Actual: the flipped readout, then the clockwise turn.
            const int fy = H - 1 - c;
            const int fx = r;
            const int sy = o.flip ? H - 1 - fy : fy;
            const int sx = o.mirror ? W - 1 - fx : fx;
            assert(sy * W + sx == wanted);
          }
        }
      }
    }
    assert(orientationCode(desiredOrientation(false, true, true)) == 2);
    assert(orientationCode(desiredOrientation(true, false, true)) == 3);
  }

  // Instant brightness: 0 leaves the curve alone; the post-gamma effect of
  // the gain equals the AE target change, so the auto exposure keeps it.
  assert(brightnessLinearGain(0, kGammaExponent) == 1.0f);
  for (int b = -50; b <= 50; b += 5) {
    const float gain = brightnessLinearGain(b, kGammaExponent);
    const float post = powf(gain, kGammaExponent);
    const float target_ratio = (100.0f + static_cast<float>(b)) / 100.0f;  // adjustedAeTarget() ratio.
    assert(fabsf(post - target_ratio) < 0.002f);
    assert((b > 0) == (gain > 1.0f) && (b < 0) == (gain < 1.0f));
  }
  assert(brightnessLinearGain(500, kGammaExponent) == brightnessLinearGain(50, kGammaExponent));

  // Digital gain: exactly 1 at step 0 (curve unchanged), 8x at the top.
  assert(digitalGainForStep(0) == 1.0f);
  assert(fabsf(digitalGainForStep(4) - 2.0f) < 1e-4f);
  assert(fabsf(digitalGainForStep(12) - 8.0f) < 1e-3f);
  assert(digitalGainForStep(200) == digitalGainForStep(kMaxDigitalGainStep));
  for (uint32_t x = 0; x <= 256; ++x) {
    assert(gammaLutValue(x, 16, kGammaExponent, 0, 1.0f) == gammaLutValue(x, 16, kGammaExponent, 0));
  }
  for (uint8_t step = 0; step <= kMaxDigitalGainStep; ++step) {
    const float gain = digitalGainForStep(step);
    uint32_t previous = 0;
    for (uint32_t x = 0; x <= 256; ++x) {
      const uint32_t y = gammaLutValue(x, 16, kGammaExponent, 0, gain);
      assert(y <= 255 && y >= previous);
      assert(y >= gammaLutValue(x, 16, kGammaExponent, 0));  // Never darker.
      previous = y;
    }
    assert(gammaLutValue(16, 16, kGammaExponent, 0, gain) == 0);  // Black stays black.
    assert(gammaLutValue(256, 16, kGammaExponent, 0, gain) == 255);
  }
  assert(gammaLutValue(48, 16, kGammaExponent, 0, 8.0f) == 255);
  // Controller: in band or sensor not yet at its limit -> unchanged.
  assert(nextDigitalGainStep(0, 115, 115, 12, kGammaExponent, true) == 0);
  assert(nextDigitalGainStep(3, 110, 115, 12, kGammaExponent, true) == 3);
  assert(nextDigitalGainStep(0, 20, 115, 12, kGammaExponent, false) == 0);
  // Too dark at the sensor limit: bounded jump up, never past the maximum.
  assert(nextDigitalGainStep(0, 20, 115, 12, kGammaExponent, true) == kMaxDigitalGainJump);
  assert(nextDigitalGainStep(0, 0, 115, 12, kGammaExponent, true) == kMaxDigitalGainJump);
  assert(nextDigitalGainStep(0, 100, 115, 12, kGammaExponent, true) == 1);
  assert(nextDigitalGainStep(10, 20, 115, 12, kGammaExponent, true) == kMaxDigitalGainStep);
  assert(nextDigitalGainStep(kMaxDigitalGainStep, 20, 115, 12, kGammaExponent, true) ==
         kMaxDigitalGainStep);
  // Too bright: the digital gain goes down first, whatever the sensor does.
  assert(nextDigitalGainStep(8, 200, 115, 12, kGammaExponent, false) == 8 - kMaxDigitalGainJump);
  assert(nextDigitalGainStep(5, 130, 115, 12, kGammaExponent, true) == 4);
  assert(nextDigitalGainStep(0, 250, 115, 12, kGammaExponent, false) == 0);
  assert(nextDigitalGainStep(2, 255, 115, 12, kGammaExponent, false) == 0);
  // Converges: repeated steps from a dark scene end inside the band or at 8x.
  {
    uint8_t step = 0;
    const float scene = 0.02f;  // Normalized linear signal at the sensor limit.
    for (int i = 0; i < 20; ++i) {
      float lin = scene * digitalGainForStep(step);
      if (lin > 1.0f) lin = 1.0f;
      const uint32_t luma = static_cast<uint32_t>(255.0f * powf(lin, kGammaExponent) + 0.5f);
      step = nextDigitalGainStep(step, luma, 115, 12, kGammaExponent, true);
    }
    float lin = scene * digitalGainForStep(step);
    const uint32_t luma = static_cast<uint32_t>(255.0f * powf(lin > 1.0f ? 1.0f : lin, kGammaExponent) + 0.5f);
    assert(step == kMaxDigitalGainStep || (luma >= 103 && luma <= 127));
  }

  // Brightness scales the AE target, monotonic and bounded.
  assert(adjustedAeTarget(115, 0) == 115 && adjustedAeTarget(80, 0) == 80);
  assert(adjustedAeTarget(115, 50) == 173 && adjustedAeTarget(115, -50) == 58);
  assert(adjustedAeTarget(80, -50) == 40);
  assert(adjustedAeTarget(200, 50) == kMaxAeTarget && adjustedAeTarget(60, -50) == kMinAeTarget);
  assert(adjustedAeTarget(115, 500) == adjustedAeTarget(115, 50));
  {
    uint32_t previous = 0;
    for (int brightness = -50; brightness <= 50; ++brightness) {
      const uint32_t target = adjustedAeTarget(115, brightness);
      assert(target >= previous && target >= kMinAeTarget && target <= kMaxAeTarget);
      previous = target;
    }
  }

  // Saturation blends toward Rec.601 luma; row sums (the white point) stay.
  {
    const WhiteBalanceGains unity;
    for (int saturation = 0; saturation <= 200; saturation += 10) {
      ImageSettings image;
      image.saturation = static_cast<uint8_t>(saturation);
      float m[3][3];
      buildImageCcm(kBaseCcm, unity, image, m);
      for (int row = 0; row < 3; ++row) {
        const float sum = m[row][0] + m[row][1] + m[row][2];
        const float base = kBaseCcm[row][0] + kBaseCcm[row][1] + kBaseCcm[row][2];
        assert(std::fabs(sum - base) < 1e-4f);
        for (float value : m[row]) assert(value < 4.0f && value > -4.0f);
      }
    }
    ImageSettings gray;
    gray.saturation = 0;
    float m[3][3];
    buildImageCcm(kBaseCcm, unity, gray, m);
    for (int col = 0; col < 3; ++col) {
      assert(std::fabs(m[0][col] - m[1][col]) < 1e-5f && std::fabs(m[1][col] - m[2][col]) < 1e-5f);
      const float luma = kLumaWeights[0] * kBaseCcm[0][col] + kLumaWeights[1] * kBaseCcm[1][col] +
                         kLumaWeights[2] * kBaseCcm[2][col];
      assert(std::fabs(m[0][col] - luma) < 1e-5f);
    }
    // More saturation moves the diagonal away from the luma row.
    ImageSettings vivid;
    vivid.saturation = 150;
    float v[3][3];
    float d[3][3];
    buildImageCcm(kBaseCcm, unity, vivid, v);
    buildImageCcm(kBaseCcm, unity, defaults, d);
    assert(v[0][0] > d[0][0] && v[2][2] > d[2][2]);
  }

  // Red and blue multiply the automatic gains; green stays untouched.
  {
    WhiteBalanceGains wb;
    wb.red = 1.4f;
    wb.blue = 0.9f;
    ImageSettings warm;
    warm.red = 20;
    warm.blue = -30;
    float expected[3][3];
    WhiteBalanceGains combined;
    combined.red = 1.4f * 1.2f;
    combined.blue = 0.9f * 0.7f;
    buildCcmWithGains(kBaseCcm, combined, expected);
    float m[3][3];
    buildImageCcm(kBaseCcm, wb, warm, m);
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) assert(std::fabs(m[row][col] - expected[row][col]) < 1e-5f);
      assert(m[row][1] == kBaseCcm[row][1]);
    }
    assert(userChannelGain(0) == 1.0f && userChannelGain(50) == 1.5f && userChannelGain(-50) == 0.5f);
    assert(userChannelGain(90) == 1.5f && userChannelGain(-90) == 0.5f);
    // Extreme combinations stay inside the ISP range.
    WhiteBalanceGains strongest;
    strongest.red = 3.0f;
    strongest.blue = 3.0f;
    ImageSettings extreme = makeImageSettings(50, 50, 200, 50, 50, 100);
    buildImageCcm(kBaseCcm, strongest, extreme, m);
    for (auto& row : m) for (float value : row) assert(value <= 3.99f && value >= -3.99f);
  }

  // Validation, clamping and the strict integer parser of the Web handler.
  assert(isValidImageAdjust(-50) && isValidImageAdjust(50) && isValidImageAdjust(0));
  assert(!isValidImageAdjust(-51) && !isValidImageAdjust(51));
  assert(isValidSaturation(0) && isValidSaturation(200) && !isValidSaturation(201) && !isValidSaturation(-1));
  {
    const ImageSettings clamped = makeImageSettings(-120, 99, 250, -51, 127, 180);
    assert(clamped.brightness == -50 && clamped.contrast == 50 && clamped.saturation == 200 &&
           clamped.red == -50 && clamped.blue == 50 && clamped.gain == 100);
    assert(makeImageSettings(0, 0, 100, 0, 0, -5).gain == 0);
    assert(sameImageSettings(makeImageSettings(0, 0, 100, 0, 0, 100), defaults));
    assert(!sameImageSettings(makeImageSettings(0, 0, 100, 0, 1, 100), defaults));
    // Max. gain: the default is the full range; a change is a change.
    assert(defaults.gain == kGainLimitDefault && kGainLimitDefault == 100);
    assert(!sameImageSettings(makeImageSettings(0, 0, 100, 0, 0, 60), defaults));
  }
  {
    // Max. gain limits (Tab5 SC202CS: 16x analog, 31.5x with sensor digital
    // gain; hardware 2026-09-25 ran at 31.5x plus 3.4x digital at night).
    const GainLimits full = gainLimitsFor(100, 16, 256, 504);
    assert(full.sensor_gain_x16 == 256 && full.sensor_total_gain_x16 == 504 &&
           full.max_digital_step == kMaxDigitalGainStep);
    const GainLimits none = gainLimitsFor(0, 16, 256, 504);
    assert(none.sensor_gain_x16 == 16 && none.sensor_total_gain_x16 == 16 && none.max_digital_step == 0);
    // Log scale: 50 % is the square root of the full 252x, about 15.9x,
    // all on the sensor (analog first), no digital gain.
    const GainLimits half = gainLimitsFor(50, 16, 256, 504);
    assert(half.sensor_total_gain_x16 >= 250 && half.sensor_total_gain_x16 <= 256);
    assert(half.sensor_gain_x16 == half.sensor_total_gain_x16 && half.max_digital_step == 0);
    // Above the sensor range the rest becomes digital steps.
    const GainLimits high = gainLimitsFor(80, 16, 256, 504);
    assert(high.sensor_total_gain_x16 == 504 && high.sensor_gain_x16 == 256);
    assert(high.max_digital_step > 0 && high.max_digital_step < kMaxDigitalGainStep);
    // Monotonic: more percent never allows less gain.
    uint16_t previous_total = 0;
    uint8_t previous_step = 0;
    for (int percent = 0; percent <= 100; ++percent) {
      const GainLimits limits = gainLimitsFor(percent, 16, 256, 504);
      assert(limits.sensor_total_gain_x16 >= previous_total);
      assert(limits.sensor_total_gain_x16 > previous_total || limits.max_digital_step >= previous_step);
      assert(limits.sensor_gain_x16 <= 256 && limits.sensor_gain_x16 <= limits.sensor_total_gain_x16);
      previous_total = limits.sensor_total_gain_x16;
      previous_step = limits.max_digital_step;
    }
    // Out-of-range percent is clamped; a sensor without a digital stage.
    assert(gainLimitsFor(250, 16, 256, 504).max_digital_step == kMaxDigitalGainStep);
    const GainLimits analog_only = gainLimitsFor(60, 16, 248, 248);
    assert(analog_only.sensor_total_gain_x16 == analog_only.sensor_gain_x16);
    // The digital step obeys the limit: never above it, dropped to it at once.
    assert(nextDigitalGainStep(0, 20, 115, 12, kGammaExponent, true, 2) == 2);
    assert(nextDigitalGainStep(2, 20, 115, 12, kGammaExponent, true, 2) == 2);
    assert(nextDigitalGainStep(9, 115, 115, 12, kGammaExponent, true, 3) == 3);
    assert(nextDigitalGainStep(5, 20, 115, 12, kGammaExponent, true, 0) == 0);
  }
  long parsed = 0;
  assert(parseImageInteger("0", &parsed) && parsed == 0);
  assert(parseImageInteger("-50", &parsed) && parsed == -50);
  assert(parseImageInteger("+12", &parsed) && parsed == 12);
  assert(parseImageInteger("200", &parsed) && parsed == 200);
  for (const char* bad : {"", "-", "+", "1.5", "0x10", "12a", " 5", "5 ", "1000", "--5", "1e2"}) {
    assert(!parseImageInteger(bad, &parsed));
  }
  assert(!parseImageInteger(nullptr, &parsed));

  std::puts("contract ok");
  return 0;
}
`;
compileAndRun('contract', contractProgram, []);

// ArduinoJson request parser, compiled against the pinned library when present.
const jsonInclude = [
  process.env.ARDUINOJSON_INCLUDE,
  path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src'),
  path.join(root, 'third_party/ArduinoJson/src'),
].filter(Boolean).find(candidate => fs.existsSync(path.join(candidate, 'ArduinoJson.h')));

if (!jsonInclude) {
  console.log('Local camera contract passed; request parser skipped (ArduinoJson headers not found).');
  process.exit(0);
}

const parserProgram = String.raw`
#include "src/video/local_camera/local_camera_request.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace local_camera_contract;

static RequestStatus parse(const std::string& text, SnapshotRequest* request) {
  return parseSnapshotRequest(text.data(), text.size(), request);
}

int main() {
  SnapshotRequest request;
  assert(parse("{\"v\":1,\"id\":\"00112233445566778899aabbccddeeff\",\"op\":\"snapshot\",\"max_bytes\":131072}",
               &request) == RequestStatus::Ok);
  assert(strcmp(request.id, "00112233445566778899aabbccddeeff") == 0);
  assert(request.max_bytes == 131072);
  // Unknown fields stay forward compatible; max_bytes is optional.
  assert(parse("{\"v\":1,\"id\":\"0123456789abcdef\",\"op\":\"snapshot\",\"extra\":true}",
               &request) == RequestStatus::Ok);
  assert(request.max_bytes == kPanelMaxJpegBytes);
  assert(parse("", &request) == RequestStatus::Empty);
  assert(parseSnapshotRequest(nullptr, 0, &request) == RequestStatus::Empty);
  assert(parse("not json", &request) == RequestStatus::Malformed);
  assert(parse("[1,2,3]", &request) == RequestStatus::Malformed);
  assert(parse("{\"v\":1,\"id\":\"0123456789abcdef\",\"op\":{\"a\":{\"b\":1}}}", &request) ==
         RequestStatus::Malformed);  // Nesting limit.
  assert(parse(std::string(600, ' ') + "{}", &request) == RequestStatus::TooLong);
  assert(parse("{\"id\":\"0123456789abcdef\",\"op\":\"snapshot\"}", &request) ==
         RequestStatus::UnsupportedVersion);
  assert(parse("{\"v\":\"1\",\"id\":\"0123456789abcdef\",\"op\":\"snapshot\"}", &request) ==
         RequestStatus::UnsupportedVersion);
  assert(parse("{\"v\":1.5,\"id\":\"0123456789abcdef\",\"op\":\"snapshot\"}", &request) ==
         RequestStatus::UnsupportedVersion);
  assert(parse("{\"v\":2,\"id\":\"0123456789abcdef\",\"op\":\"snapshot\"}", &request) ==
         RequestStatus::UnsupportedVersion);
  assert(parse("{\"v\":1,\"id\":\"0123456789abcdef\",\"op\":\"stream\"}", &request) ==
         RequestStatus::UnsupportedOperation);
  assert(parse("{\"v\":1,\"id\":\"0123456789abcdef\"}", &request) ==
         RequestStatus::UnsupportedOperation);
  assert(parse("{\"v\":1,\"id\":\"0123456789ABCDEF\",\"op\":\"snapshot\"}", &request) ==
         RequestStatus::InvalidId);
  assert(parse("{\"v\":1,\"id\":12345678901234567,\"op\":\"snapshot\"}", &request) ==
         RequestStatus::InvalidId);
  assert(parse("{\"v\":1,\"id\":\"0123456789abcdef\",\"op\":\"snapshot\",\"max_bytes\":\"big\"}",
               &request) == RequestStatus::InvalidMaxBytes);
  assert(parse("{\"v\":1,\"id\":\"0123456789abcdef\",\"op\":\"snapshot\",\"max_bytes\":100}",
               &request) == RequestStatus::InvalidMaxBytes);
  std::puts("parser ok");
  return 0;
}
`;
compileAndRun('parser', parserProgram, [jsonInclude]);
console.log('Local camera contract and request parser passed.');

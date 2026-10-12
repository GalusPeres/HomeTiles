// Host regression for the on-panel wake word (Assist contract v1 addendum):
// the wake word catalog and stored list, the retained status fields, the
// assist_wake payload, the feature quantization, the sliding-window detector
// and the PCM ring (src/audio/wakeword/wake_word_detector.h), the vendored
// TFLM microfrontend on a synthetic tone, and, when the pinned ArduinoJson is
// installed, the wake_words command parser.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const out = path.join(root, 'build/tests/wake-word');
fs.mkdirSync(out, {recursive: true});

function findCompiler(candidates) {
  for (const candidate of candidates.filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}

const cxx = findCompiler([process.env.CXX, 'clang++', 'g++', 'c++']);
const cc = findCompiler([process.env.CC, 'clang', 'gcc', 'cc']);
if (!cxx || !cc) {
  console.log('SKIP: wake word test needs a host C and C++ compiler');
  process.exit(0);
}

function run(exe) {
  const result = spawnSync(exe, [], {encoding: 'utf8'});
  assert.equal(result.status, 0, `${path.basename(exe)} failed:\n${result.stdout}${result.stderr}`);
  return result.stdout.trim();
}

function compileAndRun(name, source, includes = []) {
  const cpp = path.join(out, `${name}.cpp`);
  const exe = path.join(out, process.platform === 'win32' ? `${name}.exe` : name);
  fs.writeFileSync(cpp, source);
  const args = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', root];
  for (const include of includes) args.push('-I', include);
  const build = spawnSync(cxx, [...args, cpp, '-o', exe], {encoding: 'utf8'});
  assert.equal(build.status, 0, `${name} did not compile:\n${build.stdout}${build.stderr}`);
  return run(exe);
}

// --- Contract: catalog, stored list, status fields, assist_wake payload ----
const contractProgram = String.raw`
#include "src/audio/assist_contract.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace assist_contract;

static std::string status(const StatusFields& fields) {
  char buffer[256];
  const size_t length = buildStatusJson(buffer, sizeof(buffer), fields);
  assert(length == strlen(buffer));
  return buffer;
}

int main() {
  assert(strcmp(kWakeLeaf, "/stat/assist_wake") == 0);
  assert(kWakeWordCount == 1 && strcmp(kWakeWordIds[0], "okay_nabu") == 0);
  assert(kAllWakeWordsMask == 1u);
  assert(kWakeHandoffMs == 3000);
  assert(strcmp(publicStateName(PublicState::Wake), "wake") == 0);
  assert(strcmp(publicStateName(PublicState::Listening), "listening") == 0);
  assert(strcmp(publicStateName(PublicState::Idle), "idle") == 0);

  // Ids: [a-z0-9_]{1,32}.
  assert(isValidWakeWordId("okay_nabu", 9));
  assert(isValidWakeWordId("a", 1));
  assert(isValidWakeWordId("hey_jarvis_2", 12));
  assert(isValidWakeWordId("abcdefghijklmnopqrstuvwxyz012345", 32));
  assert(!isValidWakeWordId("abcdefghijklmnopqrstuvwxyz0123456", 33));
  assert(!isValidWakeWordId("", 0));
  assert(!isValidWakeWordId(nullptr, 3));
  assert(!isValidWakeWordId("Okay_Nabu", 9));
  assert(!isValidWakeWordId("okay nabu", 9));
  assert(!isValidWakeWordId("okay-nabu", 9));
  assert(wakeWordIndex("okay_nabu", 9) == 0);
  assert(wakeWordIndex("hey_jarvis", 10) == -1);
  assert(wakeWordIndex("okay_nab", 8) == -1);
  assert(wakeWordIndex("okay_nabu_", 10) == -1);

  // Stored list: names, unknown and malformed entries dropped.
  assert(parseWakeWordList("okay_nabu") == 1u);
  assert(parseWakeWordList("") == 0u);
  assert(parseWakeWordList(nullptr) == 0u);
  assert(parseWakeWordList("hey_jarvis,okay_nabu") == 1u);
  assert(parseWakeWordList("okay_nabu,,BAD,okay_nabu") == 1u);
  assert(parseWakeWordList("hey_jarvis") == 0u);
  char list[kMaxWakeWordListChars];
  assert(formatWakeWordList(1u, list, sizeof(list)) == 9 && strcmp(list, "okay_nabu") == 0);
  assert(formatWakeWordList(0u, list, sizeof(list)) == 0 && list[0] == '\0');
  // Bits outside the catalog never appear.
  assert(formatWakeWordList(0xFFFFFFFEu, list, sizeof(list)) == 0 && list[0] == '\0');
  char quoted[16];
  assert(formatWakeWordList(1u, quoted, sizeof(quoted), ',', '"') == 11);
  assert(strcmp(quoted, "\"okay_nabu\"") == 0);
  char tiny[9];
  assert(formatWakeWordList(1u, tiny, sizeof(tiny)) == 0 && tiny[0] == '\0');
  assert(parseWakeWordList(list) == 0u);

  // Status: unchanged without wake fields; with them, both arrays always.
  StatusFields fields;
  assert(status(fields) == "{\"v\":1,\"state\":\"idle\"}");
  fields.wake_words = true;
  assert(status(fields) == "{\"v\":1,\"state\":\"idle\",\"wake_words\":[],\"active_wake_words\":[]}");
  fields.available_wake_words = kAllWakeWordsMask;
  assert(status(fields) ==
         "{\"v\":1,\"state\":\"idle\",\"wake_words\":[\"okay_nabu\"],\"active_wake_words\":[]}");
  fields.active_wake_words = 1u;
  assert(status(fields) == "{\"v\":1,\"state\":\"idle\",\"wake_words\":[\"okay_nabu\"],"
                           "\"active_wake_words\":[\"okay_nabu\"]}");
  // Active words the firmware cannot detect are not reported as active.
  fields.available_wake_words = 0;
  assert(status(fields) == "{\"v\":1,\"state\":\"idle\",\"wake_words\":[],\"active_wake_words\":[]}");
  fields.available_wake_words = kAllWakeWordsMask;
  fields.state = PublicState::Wake;
  assert(status(fields) == "{\"v\":1,\"state\":\"wake\",\"wake_words\":[\"okay_nabu\"],"
                           "\"active_wake_words\":[\"okay_nabu\"]}");
  fields.state = PublicState::Idle;
  fields.id = "0123456789abcdef0123456789abcdef";
  fields.error = ErrorCode::Timeout;
  assert(status(fields) ==
         "{\"v\":1,\"state\":\"idle\",\"id\":\"0123456789abcdef0123456789abcdef\","
         "\"error\":\"timeout\",\"wake_words\":[\"okay_nabu\"],\"active_wake_words\":[\"okay_nabu\"]}");
  // The firmware's 256-byte buffer holds the longest status; too small fails.
  char small[100];
  assert(buildStatusJson(small, sizeof(small), fields) == 0);

  // assist_wake payload.
  char wake[96];
  assert(buildWakeJson(wake, sizeof(wake), "okay_nabu", 0) > 0);
  assert(strcmp(wake, "{\"v\":1,\"word\":\"okay_nabu\",\"seq\":0}") == 0);
  assert(buildWakeJson(wake, sizeof(wake), "okay_nabu", 4294967295u) > 0);
  assert(strcmp(wake, "{\"v\":1,\"word\":\"okay_nabu\",\"seq\":4294967295}") == 0);
  assert(buildWakeJson(wake, sizeof(wake), "Okay Nabu", 1) == 0);
  assert(buildWakeJson(wake, sizeof(wake), nullptr, 1) == 0);
  assert(buildWakeJson(wake, 20, "okay_nabu", 1) == 0);

  // Plain stereo-to-mono for the ring.
  const int16_t stereo[] = {1000, 3000, -32768, -32768, 32767, -32768, -3, 0};
  int16_t mono[4];
  stereoToMono(stereo, 4, mono);
  assert(mono[0] == 2000 && mono[1] == -32768 && mono[2] == 0 && mono[3] == -1);

  puts("wake word contract ok");
  return 0;
}
`;
console.log(compileAndRun('wake_contract', contractProgram));

// --- Quantization, sliding window and ring ------------------------------------
const detectorProgram = String.raw`
#include "src/audio/wakeword/wake_word_detector.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace wake_word;

int main() {
  // okay_nabu (v2) input tensor: int8, scale 0.10196079, zero point -128.
  const float scale = 0.10196079f;
  const int zp = -128;
  assert(quantizeFeature(0, scale, zp) == -128);
  // 100 * 0.0390625 / 0.10196079 = 38.31 -> 38 - 128.
  assert(quantizeFeature(100, scale, zp) == -90);
  // 668 * 0.0390625 / 0.10196079 = 255.9 -> 256 - 128 = 128 clamps to 127.
  assert(quantizeFeature(668, scale, zp) == 127);
  assert(quantizeFeature(665, scale, zp) == 127);
  assert(quantizeFeature(660, scale, zp) == 125);
  assert(quantizeFeature(65535, scale, zp) == 127);
  // Rounds to nearest: 1.31 -> 1, 3.83 -> 4.
  assert(quantizeFeature(10, 0.390625f, 0) == 1);
  assert(quantizeFeature(98, scale, zp) == -90);
  // Monotonic over the whole uint16 range.
  int previous = -129;
  for (uint32_t v = 0; v <= 65535; ++v) {
    const int q = quantizeFeature(static_cast<uint16_t>(v), scale, zp);
    assert(q >= previous);
    previous = q;
  }
  // A broken scale cannot produce garbage.
  assert(quantizeFeature(500, 0.0f, zp) == -128);
  assert(quantizeFeature(500, -1.0f, 5) == 5);

  assert(cutoffToUint8(0.97f) == 247);
  assert(cutoffToUint8(0.0f) == 0);
  assert(cutoffToUint8(1.5f) == 255);
  assert(cutoffToUint8(0.5f) == 128);

  // Detector: cutoff 0.97 (247), window 5, after the warm-up.
  SlidingWindowDetector d;
  d.configure(0.97f, 5);
  assert(d.cutoff() == 247 && d.window() == 5);
  d.reset(kWarmupInvocations);
  assert(kWarmupInvocations == 100);
  // Warm-up ignores even certain detections.
  for (int i = 0; i < 100; ++i) assert(!d.push(255));
  assert(d.ignoring() == 0);
  // The window must fill first.
  for (int i = 0; i < 4; ++i) assert(!d.push(255));
  assert(d.push(255));
  // One weak value pulls the mean under the cutoff: 4 * 255 + 200 = 1220 < 1235.
  d.reset(0);
  for (int i = 0; i < 4; ++i) assert(!d.push(255));
  assert(!d.push(200));
  // It leaves the window after five more values.
  for (int i = 0; i < 4; ++i) assert(!d.push(255));
  assert(d.push(255));
  // Exactly the cutoff is not above it.
  d.reset(0);
  for (int i = 0; i < 10; ++i) assert(!d.push(247));
  assert(d.push(248));
  assert(d.mean() == 247);
  // A noisy stream below the cutoff never triggers.
  d.reset(0);
  for (int i = 0; i < 1000; ++i) assert(!d.push(static_cast<uint8_t>((i * 37) % 240)));
  // Window bounds.
  d.configure(0.5f, 0);
  assert(d.window() == 1);
  assert(d.push(200));
  d.configure(0.5f, 200);
  assert(d.window() == SlidingWindowDetector::kMaxWindow);

  // Ring: FIFO order across the wrap, all-or-nothing writes, byte peeks.
  int16_t memory[8];
  PcmRing ring;
  ring.reset(memory, 8);
  assert(ring.capacity() == 8 && ring.size() == 0 && ring.space() == 8);
  const int16_t a[] = {1, 2, 3, 4, 5, 6};
  assert(ring.write(a, 6));
  assert(!ring.write(a, 3));
  assert(ring.size() == 6);
  uint8_t bytes[1 + 8 * 2];
  assert(ring.peekBytes(bytes + 1, 4));  // Unaligned destination.
  int16_t first[4];
  memcpy(first, bytes + 1, sizeof(first));
  assert(first[0] == 1 && first[3] == 4);
  ring.discard(4);
  const int16_t b[] = {7, 8, 9, 10, 11, 12};
  assert(ring.write(b, 6));
  assert(ring.size() == 8 && ring.space() == 0);
  assert(!ring.peekBytes(bytes, 9));
  assert(ring.peekBytes(bytes, 8));
  int16_t all[8];
  memcpy(all, bytes, sizeof(all));
  for (int i = 0; i < 8; ++i) assert(all[i] == 5 + i);
  ring.discard(100);
  assert(ring.size() == 0);
  ring.clear();
  ring.reset(nullptr, 8);
  assert(ring.capacity() == 0 && !ring.write(a, 1) && ring.write(a, 0));

  puts("wake word detector ok");
  return 0;
}
`;
console.log(compileAndRun('wake_detector', detectorProgram));

// --- Vendored microfrontend on a 1 kHz tone ---------------------------------
const frontendDir = path.join(root, 'src/audio/wakeword/microfrontend');
const objects = [];
for (const file of fs.readdirSync(frontendDir).sort()) {
  const ext = path.extname(file);
  if (ext !== '.c' && ext !== '.cpp') continue;
  const object = path.join(out, `mf_${path.basename(file, ext)}.o`);
  const compiler = ext === '.c' ? cc : cxx;
  const std = ext === '.c' ? '-std=c11' : '-std=c++17';
  const build = spawnSync(compiler, [std, '-O1', '-Wall', '-c', path.join(frontendDir, file),
                                     '-o', object], {encoding: 'utf8'});
  assert.equal(build.status, 0, `${file} did not compile:\n${build.stdout}${build.stderr}`);
  objects.push(object);
}
// Arduino compiles every .c/.cpp under src/: the kissfft sources must not be
// standalone translation units (they would collide with the core's copy).
assert.ok(!fs.readdirSync(path.join(frontendDir, 'kissfft')).some((f) => /\.(c|cpp|cc)$/.test(f)));

const frontendProgram = String.raw`
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "src/audio/wakeword/microfrontend/frontend.h"
#include "src/audio/wakeword/microfrontend/frontend_util.h"
#include "src/audio/wakeword/wake_word_detector.h"

using namespace wake_word;

static bool configure(FrontendState* state) {
  FrontendConfig config;
  FrontendFillConfigWithDefaults(&config);
  config.window.size_ms = kWindowMs;
  config.window.step_size_ms = 10;
  config.filterbank.num_channels = kFeatureChannels;
  config.filterbank.lower_band_limit = kLowerBandHz;
  config.filterbank.upper_band_limit = kUpperBandHz;
  config.noise_reduction.smoothing_bits = kNoiseSmoothingBits;
  config.noise_reduction.even_smoothing = kNoiseEvenSmoothing;
  config.noise_reduction.odd_smoothing = kNoiseOddSmoothing;
  config.noise_reduction.min_signal_remaining = kNoiseMinSignalRemaining;
  config.pcan_gain_control.enable_pcan = 1;
  config.pcan_gain_control.strength = kPcanStrength;
  config.pcan_gain_control.offset = kPcanOffset;
  config.pcan_gain_control.gain_bits = kPcanGainBits;
  config.log_scale.enable_log = 1;
  config.log_scale.scale_shift = kLogScaleShift;
  return FrontendPopulateState(&config, state, kSampleRate) != 0;
}

// Feeds samples in chunks like the firmware (20 ms reads) and collects frames.
static std::vector<std::vector<uint16_t>> run(const std::vector<int16_t>& audio, size_t chunk) {
  FrontendState state;
  assert(configure(&state));
  std::vector<std::vector<uint16_t>> frames;
  for (size_t pos = 0; pos < audio.size(); pos += chunk) {
    const int16_t* samples = audio.data() + pos;
    size_t count = audio.size() - pos < chunk ? audio.size() - pos : chunk;
    while (count > 0) {
      size_t read = 0;
      FrontendOutput output = FrontendProcessSamples(&state, samples, count, &read);
      samples += read;
      count -= read;
      if (output.values) {
        assert(output.size == static_cast<size_t>(kFeatureChannels));
        frames.emplace_back(output.values, output.values + output.size);
      } else if (read == 0) {
        break;
      }
    }
  }
  FrontendFreeStateContents(&state);
  return frames;
}

static std::vector<int16_t> tone(double hz) {
  std::vector<int16_t> audio(kSampleRate);
  for (size_t i = 0; i < audio.size(); ++i) {
    audio[i] = static_cast<int16_t>(16384.0 * std::sin(2.0 * M_PI * hz * i / kSampleRate));
  }
  return audio;
}

static size_t loudest(const std::vector<uint16_t>& frame) {
  size_t peak = 0;
  for (size_t c = 1; c < frame.size(); ++c) {
    if (frame[c] > frame[peak]) peak = c;
  }
  return peak;
}

// Channel whose mel center is nearest hz: 40 bands spread evenly in mel
// between 125 and 7500 Hz (filterbank_util.c).
static size_t melChannel(double hz) {
  auto mel = [](double f) { return 1127.0 * std::log1p(f / 700.0); };
  const double step = (mel(kUpperBandHz) - mel(kLowerBandHz)) / (kFeatureChannels + 1);
  return static_cast<size_t>(std::lround((mel(hz) - mel(kLowerBandHz)) / step) - 1);
}

int main() {
  // One second of a 1 kHz tone at half scale, read in 20 ms chunks.
  const auto frames = run(tone(1000.0), 320);
  // 30 ms window, 10 ms step: (16000 - 480) / 160 + 1 = 98 frames.
  assert(frames.size() == 98);
  // Chunking does not change the result.
  assert(run(tone(1000.0), kSampleRate) == frames);
  assert(run(tone(1000.0), 7) == frames);

  // The first frame (before noise reduction adapts) peaks at the tone's mel
  // channel, for low, middle and high tones.
  size_t previous = 0;
  for (double hz : {500.0, 1000.0, 3000.0, 6000.0}) {
    const auto first = run(tone(hz), 320).front();
    const size_t peak = loudest(first);
    const size_t expected = melChannel(hz);
    printf("tone %.0f Hz: peak channel %zu (expected %zu), value %u\n", hz, peak, expected,
           static_cast<unsigned>(first[peak]));
    assert(peak + 1 >= expected && peak <= expected + 1);
    assert(peak > previous);
    previous = peak;
  }

  // Noise reduction treats a steady tone as noise: after a second it is gone.
  for (uint16_t v : frames.back()) assert(v < 50);

  // Silence stays at the bottom of the range, so the quantized model input
  // of a silent frame is the zero point.
  std::vector<int16_t> silence(kSampleRate, 0);
  const auto quiet = run(silence, 320);
  assert(quiet.size() == 98);
  for (uint16_t v : quiet.back()) {
    assert(quantizeFeature(v, 0.10196079f, -128) <= -120);
  }
  printf("microfrontend ok: %zu frames of %d channels per second\n", frames.size(),
         kFeatureChannels);
  return 0;
}
`;
{
  const cpp = path.join(out, 'wake_frontend.cpp');
  const exe = path.join(out, process.platform === 'win32' ? 'wake_frontend.exe' : 'wake_frontend');
  fs.writeFileSync(cpp, frontendProgram);
  const build = spawnSync(cxx, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', root, cpp,
                                ...objects, '-o', exe, '-lm'], {encoding: 'utf8'});
  assert.equal(build.status, 0, `wake_frontend did not compile:\n${build.stdout}${build.stderr}`);
  console.log(run(exe));
}

// --- Firmware wiring --------------------------------------------------------
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');
assert.match(read('src/network/mqtt/mqtt_topics.cpp'),
             /\{TopicKey::ASSIST_WAKE_STAT, TopicDomain::State, "assist_wake"\}/);
const select = read('src/audio/speaker_select.h');
assert.match(select, /DEVICE_WAVESHARE_TOUCH_LCD_7B\)[\s\S]*#define HOMETILES_WAKE_WORD 1[\s\S]*#endif/);
const speaker = read('src/audio/speaker.cpp');
assert.match(speaker, /TopicKey::ASSIST_WAKE_STAT/);
assert.match(speaker, /fields\.wake_words = true;/);
assert.match(speaker, /kPrefsWakeWordsKey = "wake_words"/);
// The command applies at once, persists and republishes the status.
assert.match(speaker, /CommandKind::WakeWords\) \{\s*g_wake_active\.store\(command\.wake_words\);\s*g_assist_state_generation\.fetch_add\(1\);\s*g_persist_pending = true;/);
const engine = read('src/audio/wakeword/wake_word_engine.cpp');
assert.match(engine, /#if defined\(HOMETILES_WAKE_WORD\)/);
// Every operator of okay_nabu (v2) is registered, and only those.
const ops = [...engine.matchAll(/resolver->Add(\w+)\(\)/g)].map((m) => m[1]).sort();
assert.deepEqual(ops, ['AssignVariable', 'CallOnce', 'Concatenation', 'Conv2D', 'DepthwiseConv2D',
                       'FullyConnected', 'Logistic', 'Quantize', 'ReadVariable', 'Reshape',
                       'SplitV', 'StridedSlice', 'VarHandle']);
assert.match(engine, /constexpr int kOpCount = 13;/);
const modelHeader = read('src/audio/wakeword/models/okay_nabu_model.h');
const manifest = JSON.parse(read('src/audio/wakeword/models/okay_nabu.json'));
assert.match(modelHeader, new RegExp(`kOkayNabuProbabilityCutoff = ${manifest.micro.probability_cutoff}f;`));
assert.match(modelHeader, new RegExp(`kOkayNabuSlidingWindowSize = ${manifest.micro.sliding_window_size};`));
assert.match(modelHeader, new RegExp(`kOkayNabuTensorArenaSize = ${manifest.micro.tensor_arena_size};`));
assert.match(modelHeader, new RegExp(`kOkayNabuFeatureStepMs = ${manifest.micro.feature_step_size};`));
const modelSource = read('src/audio/wakeword/models/okay_nabu_model.cpp');
const size = Number(/kOkayNabuModelSize = (\d+);/.exec(modelHeader)[1]);
assert.equal(modelSource.match(/0x[0-9a-f]{2}/g).length, size);
assert.match(modelSource, /alignas\(16\) const uint8_t kOkayNabuModel\[\]/);
assert.match(modelSource, /#if defined\(HOMETILES_WAKE_WORD\)/);
console.log('wake word wiring ok');

// --- ArduinoJson parser (assist_request.h) ----------------------------------
const arduinoJsonCandidates = [
  process.env.ARDUINOJSON_SRC,
  path.join(os.homedir(), 'Arduino/libraries/ArduinoJson/src'),
  path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src'),
].filter(Boolean);
const arduinoJson = arduinoJsonCandidates.find((dir) => fs.existsSync(path.join(dir, 'ArduinoJson.h')));
if (!arduinoJson) {
  console.log('SKIP: wake_words command parser test needs the ArduinoJson library');
  process.exit(0);
}

const requestProgram = String.raw`
#include "src/audio/assist_request.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace assist_contract;

static Command parse(const std::string& json) {
  Command command;
  parseCommand(json.c_str(), json.size(), &command);
  return command;
}

int main() {
  Command c = parse("{\"v\":1,\"op\":\"wake_words\",\"active\":[\"okay_nabu\"]}");
  assert(c.kind == CommandKind::WakeWords && c.status == CommandStatus::Ok && c.wake_words == 1u);
  // Empty list = off.
  c = parse("{\"v\":1,\"op\":\"wake_words\",\"active\":[]}");
  assert(c.kind == CommandKind::WakeWords && c.status == CommandStatus::Ok && c.wake_words == 0u);
  // Unknown, malformed and non-string entries are ignored.
  c = parse("{\"v\":1,\"op\":\"wake_words\",\"active\":[\"hey_jarvis\",\"okay_nabu\",\"OKAY\",3,null]}");
  assert(c.status == CommandStatus::Ok && c.wake_words == 1u);
  c = parse("{\"v\":1,\"op\":\"wake_words\",\"active\":[\"hey_jarvis\"]}");
  assert(c.status == CommandStatus::Ok && c.wake_words == 0u);
  // No id needed; a stray id is ignored.
  c = parse("{\"v\":1,\"op\":\"wake_words\",\"id\":\"x\",\"active\":[\"okay_nabu\"]}");
  assert(c.status == CommandStatus::Ok && c.wake_words == 1u);
  // "active" must be an array.
  assert(parse("{\"v\":1,\"op\":\"wake_words\"}").status == CommandStatus::InvalidValue);
  assert(parse("{\"v\":1,\"op\":\"wake_words\",\"active\":\"okay_nabu\"}").status ==
         CommandStatus::InvalidValue);
  assert(parse("{\"v\":1,\"op\":\"wake_words\",\"active\":null}").status ==
         CommandStatus::InvalidValue);
  std::string many = "{\"v\":1,\"op\":\"wake_words\",\"active\":[";
  for (size_t i = 0; i <= kMaxWakeWordEntries; ++i) many += i ? ",1" : "1";
  many += "]}";
  assert(parse(many).status == CommandStatus::InvalidValue);
  assert(parse("{\"v\":2,\"op\":\"wake_words\",\"active\":[]}").status ==
         CommandStatus::UnsupportedVersion);
  // Start and stop are unchanged.
  c = parse("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789abcdef\"}");
  assert(c.kind == CommandKind::Start && c.status == CommandStatus::Ok && c.wake_words == 0u);
  puts("wake_words command parser ok");
  return 0;
}
`;
console.log(compileAndRun('wake_request', requestProgram, [arduinoJson]));

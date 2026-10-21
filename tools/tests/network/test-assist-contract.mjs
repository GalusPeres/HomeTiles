// Host regression for the Assist microphone MQTT contract shared with the
// Home Assistant Bridge: the binary audio frame header (magic, id, flags,
// little-endian sequence), the stereo-to-mono PCM conversion, request id
// validation, the max_ms clamp and the retained status payload. A second
// program exercises the ArduinoJson command parser when the pinned library is
// installed.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const out = path.join(root, 'build/tests/assist-contract');
fs.mkdirSync(out, {recursive: true});

function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}

const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: assist contract test needs a host C++ compiler');
  process.exit(0);
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

const contractProgram = String.raw`
#include "src/audio/assist_contract.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace assist_contract;

static std::string status(const StatusFields& fields) {
  char buffer[160];
  const size_t length = buildStatusJson(buffer, sizeof(buffer), fields);
  assert(length == strlen(buffer));
  return buffer;
}

int main() {
  // Frame geometry: 100 ms of 16 kHz mono 16-bit PCM.
  static_assert(kFrameSamples == 1600, "100 ms at 16 kHz");
  static_assert(kFrameBytes == 3200, "3200 PCM bytes per frame");
  static_assert(kMaxFrameHeaderBytes == 42, "magic + len + 32-byte id + flags + seq");

  // Header: "HTA1", id length, id, flags, sequence little-endian.
  const char* id = "0123456789abcdef";
  uint8_t header[kMaxFrameHeaderBytes + 8];
  memset(header, 0xEE, sizeof(header));
  size_t length = buildFrameHeader(header, sizeof(header), id, 16, false, 0x04030201u);
  assert(length == 4 + 1 + 16 + 1 + 4);
  assert(length == frameHeaderLength(16));
  assert(memcmp(header, "HTA1", 4) == 0);
  assert(header[4] == 16);
  assert(memcmp(header + 5, id, 16) == 0);
  assert(header[21] == 0x00);
  assert(header[22] == 0x01 && header[23] == 0x02 && header[24] == 0x03 && header[25] == 0x04);
  // Nothing written past the header.
  assert(header[26] == 0xEE);

  // Last flag, sequence 0 and a 32-character id.
  const char* long_id = "0123456789abcdef0123456789abcdef";
  length = buildFrameHeader(header, sizeof(header), long_id, 32, true, 0);
  assert(length == kMaxFrameHeaderBytes);
  assert(header[4] == 32);
  assert(header[37] == kFlagLast);
  assert(header[38] == 0 && header[39] == 0 && header[40] == 0 && header[41] == 0);
  // Large sequence numbers stay little-endian.
  length = buildFrameHeader(header, sizeof(header), id, 16, false, 0xFFFFFFFEu);
  assert(header[22] == 0xFE && header[23] == 0xFF && header[24] == 0xFF && header[25] == 0xFF);

  // Invalid ids and short buffers write nothing usable.
  assert(buildFrameHeader(header, sizeof(header), "0123456789ABCDEF", 16, false, 0) == 0);
  assert(buildFrameHeader(header, sizeof(header), "0123456789abcde", 15, false, 0) == 0);
  assert(buildFrameHeader(header, sizeof(header), nullptr, 0, false, 0) == 0);
  assert(buildFrameHeader(header, frameHeaderLength(16) - 1, id, 16, false, 0) == 0);
  assert(buildFrameHeader(nullptr, 64, id, 16, false, 0) == 0);

  // Request ids: 16-32 lowercase hex (shared with the speaker contract).
  assert(isValidRequestId(id, 16));
  assert(isValidRequestId(long_id, 32));
  assert(!isValidRequestId("0123456789abcdef0123456789abcdef0", 33));
  assert(!isValidRequestId("0123456789abcdeg", 16));
  assert(!isValidRequestId("", 0));

  // max_ms clamps to 1000..30000; NaN falls back to the default.
  assert(clampMaxMs(15000) == 15000);
  assert(clampMaxMs(0) == kMinMaxMs);
  assert(clampMaxMs(-5) == kMinMaxMs);
  assert(clampMaxMs(999.9) == kMinMaxMs);
  assert(clampMaxMs(1000) == 1000);
  assert(clampMaxMs(30000) == 30000);
  assert(clampMaxMs(30001) == kMaxMaxMs);
  assert(clampMaxMs(1e12) == kMaxMaxMs);
  assert(clampMaxMs(2500.7) == 2500);
  assert(clampMaxMs(0.0 / 0.0) == kDefaultMaxMs);

  // Stereo (one mic per slot) to mono, written little-endian.
  const int16_t stereo[] = {1000, 3000, -32768, -32768, 32767, -32768, -3, 0, 0x1234, 0x1234};
  uint8_t mono[10];
  stereoToMonoLe(stereo, 5, mono);
  auto sample = [&](int i) {
    return static_cast<int16_t>(static_cast<uint16_t>(mono[2 * i] | (mono[2 * i + 1] << 8)));
  };
  assert(sample(0) == 2000);
  assert(sample(1) == -32768);
  assert(sample(2) == 0);
  assert(sample(3) == -1);
  assert(mono[8] == 0x34 && mono[9] == 0x12);

  // Retained status: idle without a session, then with id and error.
  StatusFields fields;
  assert(status(fields) == "{\"v\":1,\"state\":\"idle\"}");
  fields.state = PublicState::Listening;
  fields.id = id;
  assert(status(fields) == "{\"v\":1,\"state\":\"listening\",\"id\":\"0123456789abcdef\"}");
  fields.state = PublicState::Idle;
  fields.error = ErrorCode::Timeout;
  assert(status(fields) ==
         "{\"v\":1,\"state\":\"idle\",\"id\":\"0123456789abcdef\",\"error\":\"timeout\"}");
  // Invalid ids never reach the payload.
  fields.id = "not-an-id";
  assert(status(fields).find("\"id\"") == std::string::npos);
  // Too small a buffer fails instead of truncating.
  char tiny[24];
  assert(buildStatusJson(tiny, sizeof(tiny), fields) == 0);

  // Error codes of the contract.
  assert(strcmp(errorCodeName(ErrorCode::None), "") == 0);
  assert(strcmp(errorCodeName(ErrorCode::MicUnavailable), "mic_unavailable") == 0);
  assert(strcmp(errorCodeName(ErrorCode::Busy), "busy") == 0);
  assert(strcmp(errorCodeName(ErrorCode::OutOfMemory), "out_of_memory") == 0);
  assert(strcmp(errorCodeName(ErrorCode::Timeout), "timeout") == 0);
  assert(strcmp(errorCodeName(ErrorCode::Invalid), "invalid") == 0);
  assert(strcmp(errorCodeName(ErrorCode::MicOff), "mic_off") == 0);

  // Privacy switch in the status, only when announced.
  StatusFields mic;
  assert(status(mic) == "{\"v\":1,\"state\":\"idle\"}");
  mic.mic_state = true;
  mic.mic_enabled = false;
  mic.error = ErrorCode::MicOff;
  assert(status(mic) == "{\"v\":1,\"state\":\"idle\",\"error\":\"mic_off\",\"mic_enabled\":false}");
  mic.mic_enabled = true;
  mic.error = ErrorCode::None;
  assert(status(mic) == "{\"v\":1,\"state\":\"idle\",\"mic_enabled\":true}");

  // Topic leaves and the sealed data name.
  assert(strcmp(kCommandLeaf, "/cmnd/assist") == 0);
  assert(strcmp(kStatusLeaf, "/stat/assist") == 0);
  assert(strcmp(kAudioLeaf, "/stat/assist_audio") == 0);
  assert(strcmp(kSealedDataName, "assist") == 0);

  puts("assist contract ok");
  return 0;
}
`;

console.log(compileAndRun('assist_contract', contractProgram, []).trim());

// The firmware wiring must use the same names as the contract.
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8');
const topics = read('src/network/mqtt/mqtt_topics.cpp');
assert.match(topics, /\{TopicKey::ASSIST_CMND, TopicDomain::Command, "assist"\}/);
assert.match(topics, /\{TopicKey::ASSIST_STAT, TopicDomain::State, "assist"\}/);
assert.match(topics, /\{TopicKey::ASSIST_AUDIO_STAT, TopicDomain::State, "assist_audio"\}/);
const channel = read('src/network/secure/command_channel.cpp');
assert.match(channel, /strcmp\(header\.name, "assist"\) == 0\)\s*\{\s*speaker::handleAssistCommandPayload\(body, length, true\)/);
// The privacy switch: on only from the encrypted channel, starts refused while off.
const speakerCpp = read('src/audio/speaker.cpp');
assert.match(speakerCpp, /if \(command\.mic_enabled && !sealed\) \{/);
assert.match(speakerCpp, /if \(command\.kind == assist::CommandKind::Start && !g_mic_enabled\.load\(\)\) \{/);
assert.match(speakerCpp, /activeWakeWords\(\) != 0 && g_mic_ready\.load\(\) && g_mic_enabled\.load\(\)/);
assert.match(speakerCpp, /prefs\.putBool\(kPrefsMicKey, g_mic_enabled\.load\(\)\);/);
// A new panel keeps the microphone off until someone switches it on.
assert.match(speakerCpp, /std::atomic<bool> g_mic_enabled\{false\};/);
assert.match(speakerCpp, /g_mic_enabled\.store\(prefs\.getBool\(kPrefsMicKey, false\)\);/);
assert.match(channel, /speaker::isAssistCommandTopic\(topic\)/);
assert.match(read('src/network/mqtt/mqtt_handlers.cpp'), /speaker::assistCommandTopic\(\)/);
assert.match(read('src/network/network_manager.cpp'), /speaker::isAssistCommandTopic\(topic\)/);
assert.match(read('src/network/bridge/ha_bridge_config.cpp'),
             /"audio_input\\":";\s*json \+= speaker::micCapability\(\)/);
console.log('assist wiring ok');

// ArduinoJson parser (assist_request.h) when the pinned library is installed.
const arduinoJsonCandidates = [
  process.env.ARDUINOJSON_SRC,
  path.join(os.homedir(), 'Arduino/libraries/ArduinoJson/src'),
  path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src'),
].filter(Boolean);
const arduinoJson = arduinoJsonCandidates.find((dir) => fs.existsSync(path.join(dir, 'ArduinoJson.h')));
if (!arduinoJson) {
  console.log('SKIP: assist command parser test needs the ArduinoJson library');
  process.exit(0);
}

const requestProgram = String.raw`
#include "src/audio/assist_request.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace assist_contract;

static Command parse(const std::string& json) {
  Command command;
  parseCommand(json.c_str(), json.size(), &command);
  return command;
}

int main() {
  Command c = parse("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789abcdef\",\"max_ms\":15000}");
  assert(c.kind == CommandKind::Start && c.status == CommandStatus::Ok);
  assert(strcmp(c.id, "0123456789abcdef") == 0 && c.max_ms == 15000);
  // max_ms is optional and clamped.
  c = parse("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789abcdef\"}");
  assert(c.status == CommandStatus::Ok && c.max_ms == kDefaultMaxMs);
  c = parse("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789abcdef\",\"max_ms\":100}");
  assert(c.status == CommandStatus::Ok && c.max_ms == kMinMaxMs);
  c = parse("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789abcdef\",\"max_ms\":600000}");
  assert(c.status == CommandStatus::Ok && c.max_ms == kMaxMaxMs);
  c = parse("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789abcdef\",\"max_ms\":-1}");
  assert(c.status == CommandStatus::Ok && c.max_ms == kMinMaxMs);
  c = parse("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789abcdef\",\"max_ms\":2500.5}");
  assert(c.status == CommandStatus::Ok && c.max_ms == 2500);
  // A refused max_ms keeps the id, so the status can name the request.
  c = parse("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789abcdef\",\"max_ms\":\"x\"}");
  assert(c.kind == CommandKind::Start && c.status == CommandStatus::InvalidValue);
  assert(strcmp(c.id, "0123456789abcdef") == 0);

  c = parse("{\"v\":1,\"op\":\"stop\",\"id\":\"0123456789abcdef0123456789abcdef\"}");
  assert(c.kind == CommandKind::Stop && c.status == CommandStatus::Ok);
  assert(strcmp(c.id, "0123456789abcdef0123456789abcdef") == 0);
  // Stop needs the id of the session it ends.
  c = parse("{\"v\":1,\"op\":\"stop\"}");
  assert(c.kind == CommandKind::Stop && c.status == CommandStatus::InvalidId && c.id[0] == '\0');

  assert(parse("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789ABCDEF\"}").status ==
         CommandStatus::InvalidId);
  assert(parse("{\"v\":1,\"op\":\"start\",\"id\":\"0123456789abcde\"}").status ==
         CommandStatus::InvalidId);
  assert(parse("{\"v\":1,\"op\":\"start\",\"id\":12345}").status == CommandStatus::InvalidId);
  assert(parse("{\"v\":2,\"op\":\"start\",\"id\":\"0123456789abcdef\"}").status ==
         CommandStatus::UnsupportedVersion);
  assert(parse("{\"op\":\"stop\",\"id\":\"0123456789abcdef\"}").status ==
         CommandStatus::UnsupportedVersion);
  assert(parse("{\"v\":1,\"op\":\"play\",\"id\":\"0123456789abcdef\"}").status ==
         CommandStatus::UnsupportedOp);
  assert(parse("{\"v\":1}").status == CommandStatus::UnsupportedOp);
  assert(parse("not json").status == CommandStatus::InvalidJson);
  assert(parse("[1]").status == CommandStatus::InvalidJson);
  assert(parse("").status == CommandStatus::InvalidJson);
  std::string big = "{\"v\":1,\"op\":\"stop\",\"id\":\"0123456789abcdef\",\"pad\":\"" +
                    std::string(kMaxRequestPayloadBytes, 'a') + "\"}";
  assert(parse(big).status == CommandStatus::TooLarge);

  puts("assist command parser ok");
  return 0;
}
`;

console.log(compileAndRun('assist_request', requestProgram, [arduinoJson]).trim());

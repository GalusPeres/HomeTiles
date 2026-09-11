import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const salt = Buffer.from(Array.from({length: 16}, (_, index) => index + 1));
const credentialInput = pin => Buffer.concat([
  Buffer.from('HomeTiles-PIN-v1'), salt, Buffer.from(pin),
]);
const digest = pin => createHash('sha256').update(credentialInput(pin)).digest();
const bytes = value => Array.from(value).join(',');

// Encode independent fixtures by documented byte offsets, not production
// structs or the production checksum function. These bytes are the contract.
function recordFixture(version, flags) {
  const size = version < 3 ? 60 : version === 3 ? 132 : 144;
  const record = Buffer.alloc(size);
  record.writeUInt32LE(0x43415448, 0);
  record[4] = version;
  record[5] = flags;
  record[6] = 2;
  salt.copy(record, 8);
  digest('2468').copy(record, 24);
  if (version >= 3) {
    record[7] = 1;
    record.writeUInt32LE(0x123456, 56);
    record.write('Private settings', 60, 'utf8');
    record.write('cog', 92, 'utf8');
    record.set([1, 1, 2, 1], 124);
  }
  if (version === 4) {
    record[128] = 4;
    record.write('2468', 129, 'utf8');
  }
  let checksum = 2166136261;
  for (const byte of record.subarray(0, size - 4)) {
    checksum = Math.imul(checksum ^ byte, 16777619) >>> 0;
  }
  record.writeUInt32LE(checksum, size - 4);
  return record;
}

const fixtures = [recordFixture(1, 5), recordFixture(2, 5),
  recordFixture(3, 3), recordFixture(4, 7)];
const hashVectors = ['2468', '12345678', '1357'].map(pin => ({
  input: credentialInput(pin), hash: digest(pin),
}));
const hashBackend = hashVectors.map(({input, hash}, index) => `
  static const unsigned char input_${index}[] = {${bytes(input)}};
  static const unsigned char hash_${index}[] = {${bytes(hash)}};
  if (length == sizeof(input_${index}) &&
      std::memcmp(input, input_${index}, length) == 0) {
    std::memcpy(output, hash_${index}, sizeof(hash_${index}));
    return 0;
  }
`).join('');

const source = `
#include <cassert>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include "src/core/config/settings_access_record.h"

using namespace settings_access_record;

// The real pin_access.cpp runs below. Only platform RNG/SHA entry points are
// replaced with fixed known-answer vectors; this is not a cryptography test.
void esp_fill_random(void* output, size_t length) {
  const uint8_t fixture_salt[] = {${bytes(salt)}};
  assert(length == sizeof(fixture_salt));
  std::memcpy(output, fixture_salt, length);
}

int mbedtls_sha256(const unsigned char* input, size_t length,
                   unsigned char output[32], int is224) {
  assert(is224 == 0);
  ${hashBackend}
  return -1;
}

${fixtures.map((fixture, index) =>
  `static const uint8_t fixture_v${index + 1}[] = {${bytes(fixture)}};`).join('\n')}

template <typename Record, size_t size>
Record read_fixture(const uint8_t (&bytes)[size]) {
  static_assert(sizeof(Record) == size, "Fixture record size changed");
  Record record{};
  std::memcpy(&record, bytes, size);
  return record;
}

static bool all_zero(const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) if (bytes[i] != 0) return false;
  return true;
}

static void expect_snapshot(const DeviceConfig& config) {
  const auto& snapshot = config.settings_tile_snapshot;
  assert(snapshot.valid && snapshot.bg_color == 0x123456);
  assert(std::strcmp(snapshot.title, "Private settings") == 0);
  assert(std::strcmp(snapshot.icon_name, "cog") == 0);
  assert(snapshot.col == 1 && snapshot.row == 1);
  assert(snapshot.span_w == 2 && snapshot.span_h == 1);
}

static void expect_empty_snapshot(const DeviceConfig& config) {
  const auto& snapshot = config.settings_tile_snapshot;
  assert(!snapshot.valid && snapshot.bg_color == 0);
  assert(snapshot.title[0] == 0 && snapshot.icon_name[0] == 0);
  assert(snapshot.col == 0 && snapshot.row == 0);
  assert(snapshot.span_w == 1 && snapshot.span_h == 1);
}

static void expect_rejected_without_mutation(SettingsAccessRecord record) {
  DeviceConfig config{};
  std::memset(&config, 0x5A, sizeof(config));
  const DeviceConfig before = config;
  assert(!apply_settings_access_record(record, config));
  assert(std::memcmp(&config, &before, sizeof(config)) == 0);
}

int main() {
  static_assert(sizeof(LegacySettingsAccessRecord) == 60);
  static_assert(sizeof(SettingsAccessRecordV3) == 132);
  static_assert(sizeof(SettingsAccessRecord) == 144);
  static_assert(offsetof(LegacySettingsAccessRecord, checksum) == 56);
  static_assert(offsetof(SettingsAccessRecordV3, checksum) == 128);
  static_assert(offsetof(SettingsAccessRecord, checksum) == 140);
  static_assert(offsetof(SettingsAccessRecord, pin_length) == 128);
  static_assert(offsetof(SettingsAccessRecord, pin_digits) == 129);

  const auto current = read_fixture<SettingsAccessRecord>(fixture_v4);
  assert(settings_access_checksum(current) == current.checksum);
  DeviceConfig config{};
  std::strcpy(config.wifi_ssid, "unchanged-network");
  assert(apply_settings_access_record(current, config));
  assert(config.settings_pin_enabled && config.settings_tile_hidden);
  assert(config.settings_swipe_enabled && config.settings_reveal_edge == 2);
  assert(pin_access::verifyCredential(config.settings_pin_value,
      config.settings_pin_salt, config.settings_pin_hash));
  expect_snapshot(config);
  assert(std::strcmp(config.wifi_ssid, "unchanged-network") == 0);
  const auto encoded = make_settings_access_record(config);
  assert(std::memcmp(&encoded, fixture_v4, sizeof(encoded)) == 0);

  // Every legacy record migrates its own flags and clears an old recovery PIN.
  const auto v1 = read_fixture<LegacySettingsAccessRecord>(fixture_v1);
  assert(apply_legacy_settings_access_record(v1, config));
  assert(config.settings_pin_enabled && !config.settings_tile_hidden);
  assert(!config.settings_swipe_enabled);
  assert(all_zero(config.settings_pin_value, sizeof(config.settings_pin_value)));
  expect_empty_snapshot(config);
  const auto v2 = read_fixture<LegacySettingsAccessRecord>(fixture_v2);
  assert(apply_legacy_settings_access_record(v2, config));
  assert(config.settings_swipe_enabled);
  const auto v3 = read_fixture<SettingsAccessRecordV3>(fixture_v3);
  assert(apply_settings_access_record_v3(v3, config));
  assert(config.settings_tile_hidden && config.settings_swipe_enabled);
  assert(all_zero(config.settings_pin_value, sizeof(config.settings_pin_value)));
  expect_snapshot(config);

  // Corrupt metadata is rejected before it can modify the live configuration.
  auto bad = current;
  bad.magic ^= 1;
  bad.checksum = settings_access_checksum(bad);
  expect_rejected_without_mutation(bad);
  bad = current;
  bad.version = 5;
  bad.checksum = settings_access_checksum(bad);
  expect_rejected_without_mutation(bad);
  bad = current;
  bad.reveal_edge = 4;
  bad.checksum = settings_access_checksum(bad);
  expect_rejected_without_mutation(bad);
  bad = current;
  bad.hash[0] ^= 1;
  expect_rejected_without_mutation(bad);
  auto bad_v1 = v1;
  bad_v1.version = 3;
  bad_v1.checksum = settings_access_checksum(bad_v1);
  assert(!apply_legacy_settings_access_record(bad_v1, config));
  auto bad_v3 = v3;
  bad_v3.version = 4;
  bad_v3.checksum = settings_access_checksum(bad_v3);
  assert(!apply_settings_access_record_v3(bad_v3, config));

  // An invalid recovery copy never overrides the authoritative credential.
  bad = current;
  std::memcpy(bad.pin_digits, "1357", 4);
  bad.checksum = settings_access_checksum(bad);
  assert(apply_settings_access_record(bad, config));
  assert(config.settings_pin_enabled);
  assert(all_zero(config.settings_pin_value, sizeof(config.settings_pin_value)));
  assert(pin_access::verifyCredential("2468", config.settings_pin_salt,
                                     config.settings_pin_hash));
  for (uint8_t invalid_length : {uint8_t{3}, uint8_t{9}}) {
    bad = current;
    bad.pin_length = invalid_length;
    bad.checksum = settings_access_checksum(bad);
    assert(apply_settings_access_record(bad, config));
    assert(all_zero(config.settings_pin_value, sizeof(config.settings_pin_value)));
  }

  // Invalid snapshot geometry is isolated from the valid PIN record.
  bad = current;
  bad.snapshot_col = Device::kGridCols;
  bad.checksum = settings_access_checksum(bad);
  assert(apply_settings_access_record(bad, config));
  assert(config.settings_pin_enabled);
  expect_empty_snapshot(config);
  bad = current;
  std::memset(bad.snapshot_title, 'X', sizeof(bad.snapshot_title));
  std::memset(bad.snapshot_icon_name, 'Y', sizeof(bad.snapshot_icon_name));
  bad.checksum = settings_access_checksum(bad);
  assert(apply_settings_access_record(bad, config));
  assert(config.settings_tile_snapshot.title[31] == 0);
  assert(config.settings_tile_snapshot.icon_name[31] == 0);

  assert(apply_settings_access_record(current, config));
  std::strcpy(config.settings_pin_value, "1357");
  const auto mismatched = make_settings_access_record(config);
  assert(mismatched.pin_length == 0);
  assert(all_zero(mismatched.pin_digits, sizeof(mismatched.pin_digits)));
  config.settings_pin_enabled = false;
  std::strcpy(config.settings_pin_value, "2468");
  const auto disabled = make_settings_access_record(config);
  assert(disabled.pin_length == 0);
  assert(all_zero(disabled.pin_digits, sizeof(disabled.pin_digits)));

  // Exercise the maximum user PIN length with the real credential boundary.
  config.settings_pin_enabled = true;
  assert(pin_access::makeCredential("12345678", config.settings_pin_salt,
                                    config.settings_pin_hash));
  std::strcpy(config.settings_pin_value, "12345678");
  const auto maximum = make_settings_access_record(config);
  assert(maximum.pin_length == 8);
  assert(apply_settings_access_record(maximum, config));
  assert(std::strcmp(config.settings_pin_value, "12345678") == 0);

  // Preserve deliberate fail-open corruption recovery for parental controls.
  fail_open_settings_access(config);
  assert(!config.settings_pin_enabled && !config.settings_tile_hidden);
  assert(!config.settings_swipe_enabled);
  assert(config.settings_reveal_edge == uint8_t(SettingsRevealEdge::Left));
  assert(all_zero(config.settings_pin_salt, sizeof(config.settings_pin_salt)));
  assert(all_zero(config.settings_pin_hash, sizeof(config.settings_pin_hash)));
  assert(all_zero(config.settings_pin_value, sizeof(config.settings_pin_value)));
  expect_empty_snapshot(config);
  assert(std::strcmp(config.wifi_ssid, "unchanged-network") == 0);
  return 0;
}
`;

const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
  .find(candidate => {
    const result = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    return !result.error && result.status === 0;
  });
if (!compiler) {
  console.log('SKIP: Settings access codec runtime test requires a C++ compiler');
  process.exit(0);
}

const buildRoot = path.join(repoRoot, 'build', 'tests');
fs.mkdirSync(buildRoot, {recursive: true});
const tempRoot = fs.mkdtempSync(path.join(buildRoot, 'settings-access-record-'));
try {
  const write = (relative, text) => {
    const filename = path.join(tempRoot, relative);
    fs.mkdirSync(path.dirname(filename), {recursive: true});
    fs.writeFileSync(filename, text);
  };
  write('Arduino.h', '#pragma once\n#include <string>\n#include <stdint.h>\nusing String = std::string;\n');
  write('src/devices/device.h', '#pragma once\nnamespace Device { constexpr int kGridCellH = 110, kGridGap = 10; constexpr uint8_t kGridCols = 4; constexpr uint8_t kGridRows = 4; }\n');
  write('esp_random.h', '#pragma once\n#include <stddef.h>\nvoid esp_fill_random(void*, size_t);\n');
  write('mbedtls/sha256.h', '#pragma once\n#include <stddef.h>\nint mbedtls_sha256(const unsigned char*, size_t, unsigned char[32], int);\n');
  write('test.cpp', source);
  const outputPath = path.join(tempRoot,
    process.platform === 'win32' ? 'test.exe' : 'test');
  const compile = spawnSync(compiler,
    ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-D_CRT_SECURE_NO_WARNINGS',
      '-I', tempRoot, '-I', repoRoot,
      path.join(tempRoot, 'test.cpp'), path.join(repoRoot, 'src/core/config/pin_access.cpp'),
      '-o', outputPath], {encoding: 'utf8'});
  assert.equal(compile.status, 0,
    `Settings access codec harness did not compile:\n${compile.stdout}${compile.stderr}`);
  const run = spawnSync(outputPath, [], {encoding: 'utf8'});
  assert.equal(run.status, 0,
    `Settings access codec runtime contract failed:\n${run.stdout}${run.stderr}`);
  console.log('Settings access record layouts, migrations, validation, and recovery passed');
} finally {
  assert.ok(path.resolve(tempRoot).startsWith(path.resolve(buildRoot) + path.sep));
  fs.rmSync(tempRoot, {recursive: true, force: true});
}

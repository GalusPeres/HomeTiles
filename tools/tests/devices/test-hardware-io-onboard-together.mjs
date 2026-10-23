// Every board's onboard relays can be saved together (issue #93): the GS-T3E
// profile tied each relay to its own board variant, and a Hardware I/O save
// holds one variant, so the panel took only one relay at a time ("GPIO 46 is
// not available for this channel type"). The real variant and pin checks of
// src/io/hardware_io.cpp run on the host against every device profile: for
// each board one variant must accept all of its onboard relays at once.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile, repoRoot} from '../../lib/admin-source.mjs';

const io = readRepoFile('src/io/hardware_io.cpp').replace(/\r\n/g, '\n');
const functions = cppFunctionDefinitions(io);
const extract = name => functions.find(f => f.name === name)?.source ?? assert.fail(`${name} not in hardware_io.cpp`);
const checks = ['default_hardware_variant', 'pin_supports_type', 'pin_matches_variant', 'valid_hardware_variant', 'find_pin_option']
  .map(extract).join('\n\n');
// The save path uses exactly these checks.
const parseStart = io.indexOf('bool HardwareIoManager::parseDocument(');
const parse = io.slice(parseStart, io.indexOf('\n}\n', parseStart));
assert.match(parse, /if \(!valid_hardware_variant\(out_board_variant\)\)[\s\S]*find_pin_option\(config\.gpio, config\.type, out_board_variant\)/);
// A relay saved before the fix names its own GS-T3E variant. A stored file
// (the lenient load, allow_missing_names) falls back to the default variant
// and keeps the pins that are still offered; a save from the Web Admin still
// refuses an unknown variant.
assert.match(parse, /if \(!valid_hardware_variant\(out_board_variant\)\) \{\s*if \(!allow_missing_names\) \{\s*error = "Unknown hardware variant";\s*return false;\s*\}[\s\S]*?out_board_variant = default_hardware_variant\(\);\s*\}/);
assert.match(io, /parseDocument\(json, loaded, count, board_variant, error, true\)/, 'the stored file loads leniently');
assert.match(io, /parseDocument\(json, candidate, candidate_count,\s*candidate_board_variant, error, false\)/, 'a Web Admin save does not');

const profiles = fs.readdirSync(path.join(repoRoot, 'src/devices'))
  .filter(dir => fs.existsSync(path.join(repoRoot, 'src/devices', dir, 'hardware_io_profile.h')))
  .map(dir => {
    const header = readRepoFile(`src/devices/${dir}/hardware_io_profile.h`);
    const namespace = /^namespace (\w+) \{/m.exec(header)?.[1] ?? assert.fail(`${dir}: namespace`);
    return {dir, namespace};
  });
assert.ok(profiles.some(p => p.dir === 'golden_security_gs_t3e_v2'), 'the GS-T3E profile is checked');
assert.ok(profiles.length >= 10, `every board with a profile (${profiles.length})`);

const output = compileAndRun({
  label: 'Hardware I/O onboard relays together',
  files: {
    // The profiles include <Arduino.h>; the checks need String and the profile.
    'Arduino.h': String.raw`
#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
class String {
 public:
  String(const char* text = "") : text_(text ? text : "") {}
  bool equalsIgnoreCase(const char* other) const {
    if (!other) return false;
    size_t i = 0;
    for (; i < text_.size() && other[i]; ++i) {
      if (std::tolower(static_cast<unsigned char>(text_[i])) != std::tolower(static_cast<unsigned char>(other[i]))) return false;
    }
    return i == text_.size() && !other[i];
  }
  bool equalsIgnoreCase(const String& other) const { return equalsIgnoreCase(other.text_.c_str()); }
 private:
  std::string text_;
};
`,
  },
  harness: String.raw`
#include <Arduino.h>
#include <cstdio>
#include "src/devices/hardware_io_profile.h"
${profiles.map(p => `#include "src/devices/${p.dir}/hardware_io_profile.h"`).join('\n')}

enum class HardwareIoType : uint8_t { Relay = 0, Temperature = 1 };
namespace Device {
struct ProfileStub { HardwareIoProfile hardware_io; };
static ProfileStub g_profile;
const ProfileStub& profile() { return g_profile; }
}  // namespace Device

namespace {
${checks}
}  // namespace

// One variant that accepts every onboard relay of the board at once.
static const char* save_together(const Device::HardwareIoProfile& io_profile, int* relays) {
  Device::g_profile.hardware_io = io_profile;
  const char* candidates[1 + 64] = {default_hardware_variant()};
  int count = 1;
  for (uint8_t i = 0; i < io_profile.pin_count && count < 65; ++i) {
    const char* required = io_profile.pin_options[i].required_variant;
    if (required && *required) candidates[count++] = required;
  }
  *relays = 0;
  for (uint8_t i = 0; i < io_profile.pin_count; ++i) {
    const Device::HardwareIoPinOption& pin = io_profile.pin_options[i];
    if ((pin.capabilities & Device::kHardwareIoPinOnboard) && (pin.capabilities & Device::kHardwareIoPinSwitch)) ++*relays;
  }
  for (int c = 0; c < count; ++c) {
    const String variant(candidates[c]);
    if (!valid_hardware_variant(variant)) continue;
    bool all = true;
    for (uint8_t i = 0; i < io_profile.pin_count && all; ++i) {
      const Device::HardwareIoPinOption& pin = io_profile.pin_options[i];
      if (!(pin.capabilities & Device::kHardwareIoPinOnboard) || !(pin.capabilities & Device::kHardwareIoPinSwitch)) continue;
      all = find_pin_option(pin.gpio, HardwareIoType::Relay, variant) == &pin;
    }
    if (all) return candidates[c];
  }
  return nullptr;
}

int main() {
  int failures = 0;
${profiles.map(p => `  {
    int relays = 0;
    const char* variant = save_together(${p.namespace}::kHardwareIoProfile, &relays);
    std::printf("${p.dir} relays=%d variant=%s\\n", relays, variant ? variant : "NONE");
    if (!variant) ++failures;
  }`).join('\n')}
  // The GS-T3E variants of a relay saved before the fix are gone, so a stored
  // file takes the default variant, which keeps that relay.
  Device::g_profile.hardware_io = DeviceGoldenSecurityGST3EV2::kHardwareIoProfile;
  for (int gpio : {45, 46}) {
    char legacy[32];
    std::snprintf(legacy, sizeof(legacy), "gs_t3e_relay_gpio%d", gpio);
    const bool gone = !valid_hardware_variant(String(legacy));
    const bool kept = find_pin_option(gpio, HardwareIoType::Relay, String(default_hardware_variant())) != nullptr;
    std::printf("legacy %s gone=%d kept=%d\n", legacy, gone, kept);
    if (!gone || !kept) ++failures;
  }
  return failures ? 1 : 0;
}
`,
});

if (output === null) {
  console.log('Hardware I/O onboard relays: checks extracted, harness skipped');
} else {
  const rows = Object.fromEntries([...output.matchAll(/^(\S+) relays=(\d+) variant=(\S+)$/gm)]
    .map(([, dir, relays, variant]) => [dir, {relays: Number(relays), variant}]));
  assert.equal(Object.keys(rows).length, profiles.length, output);
  assert.deepEqual(rows.golden_security_gs_t3e_v2, {relays: 3, variant: 'standard'},
    'the GS-T3E saves its three relays together');
  assert.equal((output.match(/^legacy gs_t3e_relay_gpio4[56] gone=1 kept=1$/gm) || []).length, 2,
    'a relay saved before the fix comes back');
  console.log(`Hardware I/O: every board saves its onboard relays together (${profiles.length} profiles, ` +
    `GS-T3E ${rows.golden_security_gs_t3e_v2.relays} relays)`);
}

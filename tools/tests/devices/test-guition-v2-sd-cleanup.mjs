import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/devices/guition_jc8012p4a1_v2/vendor/guition_sdmmc.cpp'), 'utf8');
const configuration = source.match(/sdmmc_host_t host = SDMMC_HOST_DEFAULT\(\);[\s\S]*?host\.pwr_ctrl_handle = g_sd_power_handle;/)?.[0];
assert(configuration, 'Exercise the actual V2 host configuration');
const cc = ['clang++', 'g++'].find(c => spawnSync(c, ['--version']).status === 0);
if (!cc) {
  console.log('SKIP: V2 SD cleanup contract needs a C++ compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/guition-v2-sd-cleanup');
fs.mkdirSync(out, {recursive: true});
const cpp = path.join(out, 'test.cpp');
const exe = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(cpp, `
#include <cassert>
#include <initializer_list>
constexpr unsigned SDMMC_HOST_FLAG_4BIT = 1u << 1;
constexpr unsigned SDMMC_HOST_FLAG_DEINIT_ARG = 1u << 5;
constexpr unsigned defaults = 1u | SDMMC_HOST_FLAG_4BIT | (1u << 2) | (1u << 4) | SDMMC_HOST_FLAG_DEINIT_ARG;
constexpr int SDMMC_HOST_SLOT_0 = 0;
static int cleaned_slot = -1;
static bool slots[2] = {};
int deinit_slot(int slot) { assert(slot == 0); cleaned_slot = slot; slots[slot] = false; return 0; }
struct sdmmc_host_t {
  unsigned flags; int slot; int max_freq_khz;
  int (*deinit_p)(int); void* pwr_ctrl_handle;
};
#define SDMMC_HOST_DEFAULT() sdmmc_host_t{defaults, 1, 20000, deinit_slot, nullptr}
void* g_sd_power_handle = reinterpret_cast<void*>(1);
sdmmc_host_t configure(int sdmmc_frequency) {
  ${configuration}
  return host;
}
// Model the SDK cleanup dispatch: DEINIT_ARG is required to supply the slot.
// Fail explicitly instead of invoking a callback through the wrong signature.
void cleanup(const sdmmc_host_t& host) {
  assert(host.flags & SDMMC_HOST_FLAG_DEINIT_ARG);
  assert(host.deinit_p(host.slot) == 0);
}
int main() {
  for (int frequency : {40000, 20000}) {
    auto host = configure(frequency);
    assert(host.slot == 0 && host.max_freq_khz == frequency);
    assert(host.pwr_ctrl_handle == g_sd_power_handle);
    assert((host.flags & defaults) == defaults);
    slots[0] = slots[1] = true;
    cleanup(host); // Failed mount or successful-card unmount uses this callback.
    assert(cleaned_slot == 0 && !slots[0] && slots[1]);
    slots[0] = true; // Retrying can acquire the released slot again.
    cleanup(host);
    assert(!slots[0] && slots[1]);
  }
}
`);
const build = spawnSync(cc, ['-std=c++17', cpp, '-o', exe], {encoding: 'utf8'});
assert.equal(build.status, 0, build.stderr);
const run = spawnSync(exe, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stderr || 'SD cleanup must preserve DEINIT_ARG and release slot 0 only');
console.log('PASS: V2 SD default flags, slot-aware cleanup and retry contract');

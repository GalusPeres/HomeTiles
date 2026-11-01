// Guition V2: a small UI area turned by the CPU into the panel framebuffer
// is written back from the cache in one call over its rows' span (b536 log:
// one esp_cache_msync per row cost ~0.3 ms per moving Power flow dot);
// every byte of every row stays covered, and wide regions keep one call per
// row. Compiles the driver's real flush_framebuffer_rect on the host.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const driver = fs.readFileSync(path.join(root, 'src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp'), 'utf8');
const fn = cppFunctionDefinitions(driver).find(f => f.name === 'flush_framebuffer_rect');
assert.ok(fn, 'flush_framebuffer_rect');
const max = /constexpr size_t kSingleWritebackMaxBytes = ([^;]+);/.exec(driver);
assert.ok(max, 'kSingleWritebackMaxBytes');
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: V2 framebuffer write-back needs a native C++ compiler'); process.exit(0); }

const cpp = String.raw`
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>
struct {int width = 800; int height = 1280;} display_cfg;
constexpr size_t kSingleWritebackMaxBytes = ${max[1]};
struct Call {uintptr_t start; size_t size;};
std::vector<Call> calls;
void flush_cache_for_dma(const void* ptr, size_t size) {calls.push_back({reinterpret_cast<uintptr_t>(ptr), size});}
${fn.source}
static std::vector<uint16_t> fb(800 * 1280);
// Every byte of every row of the rect lies in one of the calls.
static bool covered(int x, int y, int w, int h) {
  for (int row = 0; row < h; ++row) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(fb.data() + (y + row) * 800 + x);
    const uintptr_t b = a + w * 2;
    bool hit = false;
    for (const Call& c : calls) hit = hit || (c.start <= a && b <= c.start + c.size);
    if (!hit) return false;
  }
  return true;
}
int main() {
  // A dot's area turned by the CPU: 14 rows of 13 pixels -> one call.
  flush_framebuffer_rect(fb.data(), 300, 500, 13, 14);
  assert(calls.size() == 1 && covered(300, 500, 13, 14));
  calls.clear();
  // One row: one call.
  flush_framebuffer_rect(fb.data(), 10, 10, 50, 1);
  assert(calls.size() == 1 && covered(10, 10, 50, 1));
  calls.clear();
  // A tall region beyond the span limit: one call per row, all covered.
  flush_framebuffer_rect(fb.data(), 0, 0, 40, 200);
  assert(calls.size() == 200 && covered(0, 0, 40, 200));
  calls.clear();
  // Nothing for empty rects or no framebuffer.
  flush_framebuffer_rect(fb.data(), 0, 0, 0, 10);
  flush_framebuffer_rect(nullptr, 0, 0, 10, 10);
  assert(calls.empty());
  std::puts("ok");
}
`;
const out = path.join(root, 'build/tests/guition-v2-framebuffer-writeback');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, cpp);
let r = spawnSync(compiler, ['-std=c++17', source, '-o', binary], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
r = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
console.log('Guition V2 framebuffer write-back: one call for a small CPU area, every row covered, wide regions per row');

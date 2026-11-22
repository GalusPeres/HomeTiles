// Blue flashes on P4 panels are DPI underruns, and ESP-IDF reports them only
// on the ROM console, which never reaches the USB CDC log (user 2026-10-31:
// three blue flashes after a start, nothing in the log). Test builds take the
// bridge's underrun bit over and log each burst once with the loop step that
// was running.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const watch = readRepoFile('src/core/diagnostics/dpi_underrun.h').replace(/\r\n/g, '\n');
const sketch = readRepoFile('HomeTiles.ino').replace(/\r\n/g, '\n');

// ---- Wiring ---------------------------------------------------------------------
assert.match(watch, /#if \(defined\(HOMETILES_ISSUE38_BETA\) \|\| defined\(HOMETILES_TEST_BETA\)\) && defined\(ESP_PLATFORM\)\s*#include <sdkconfig.h>\s*#if CONFIG_IDF_TARGET_ESP32P4\s*#define HOMETILES_DPI_UNDERRUN 1/,
  'only P4 test builds watch the bridge');
assert.match(watch, /MIPI_DSI_BRIDGE\.int_ena\.underrun_int_ena = 0;\s*MIPI_DSI_BRIDGE\.int_clr\.underrun_int_clr = 1;/,
  'the watch takes the bit over from the ESP-IDF interrupt, which would clear it first');
assert.match(watch, /const bool hit = MIPI_DSI_BRIDGE\.int_raw\.underrun_int_raw;\s*if \(hit\) MIPI_DSI_BRIDGE\.int_clr\.underrun_int_clr = 1;/,
  'each poll reads and clears the sticky raw bit');
assert.match(watch, /args\.dispatch_method = ESP_TIMER_TASK;/, 'the poll runs while the loop is blocked');
assert.match(sketch, /Serial\.println\("\[Setup\] BoardHAL OK"\);\n[^\n]*\n  dpi_underrun::begin\(\);/,
  'the watch starts as soon as the panel runs, before the start-up work');

// ---- Bursts and the loop step ---------------------------------------------------
const harness = String.raw`
#include <cstdio>
#include <Arduino.h>
#include "src/core/diagnostics/dpi_underrun.h"
#include "src/core/diagnostics/loop_stall.h"
uint32_t now_ms = 0;
uint32_t millis() { return now_ms; }
SerialShim Serial;
int main() {
  std::printf("before %s\n", loop_stall::currentStepName());
  loop_stall::enter(loop_stall::Step::WebAdmin);
  std::printf("entered %s\n", loop_stall::currentStepName());
  dpi_underrun::Burst b;
  int ended = 0;
  // A flash: three polls in a row see the bit, then it stays clear.
  for (uint32_t t = 0; t <= 300; t += dpi_underrun::kPollMs) {
    const bool hit = t >= 20 && t <= 30;
    if (b.poll(hit, t, loop_stall::currentStepName())) {
      ++ended;
      std::printf("burst %u %u %u %s ended %u\n", b.first_ms, b.last_ms, b.hits, b.step, t);
    }
  }
  // Two hits 50 ms apart are one burst; the step of its first hit is kept.
  loop_stall::enter(loop_stall::Step::Lvgl);
  for (uint32_t t = 400; t <= 700; t += dpi_underrun::kPollMs) {
    if (t == 460) loop_stall::enter(loop_stall::Step::Services);
    const bool hit = t == 410 || t == 460;
    if (b.poll(hit, t, loop_stall::currentStepName())) {
      ++ended;
      std::printf("burst %u %u %u %s ended %u\n", b.first_ms, b.last_ms, b.hits, b.step, t);
    }
  }
  std::printf("ended %d\n", ended);
  return 0;
}
`;
const out = compileAndRun({
  label: 'DPI underrun watch',
  harness,
  files: {
    'Arduino.h': '#pragma once\n#include <cstdint>\n#include <cstdio>\n#include <cstdarg>\nuint32_t millis();\n' +
      'struct SerialShim { void printf(const char*, ...) {} };\nextern SerialShim Serial;\n',
  },
  sources: ['src/core/diagnostics/loop_stall.cpp'],
});
if (out !== null) {
  const lines = out.trim().split('\n');
  assert.deepEqual(lines, [
    'before setup',
    'entered web-admin',
    'burst 20 30 3 web-admin ended 130',
    'burst 410 460 2 lvgl ended 560',
    'ended 2',
  ], out);
  console.log('DPI underrun watch: P4 test builds read the bridge bit themselves, one line per burst with the loop step it began in');
}

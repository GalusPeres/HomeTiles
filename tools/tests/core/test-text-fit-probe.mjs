// The beta text fit probe (src/core/diagnostics/text_fit_probe.cpp) logs
// visible labels whose text does not fit, to check French and Polish on the
// device (user 2026-10-03). It runs here on real LVGL: a cut and an
// ellipsized single-line label, a wrapped label taller than its fixed box
// and a label wider than its clipping parent are logged once with their
// whole text; fitting, wrapping, hidden and recolored labels whose codes
// take no space are not, and a value that changes is one finding.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the text fit probe test needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/text-fit-probe');
const stubs = path.join(out, 'stubs');
fs.mkdirSync(path.join(stubs, 'src/core/config'), {recursive: true});
fs.writeFileSync(path.join(stubs, 'Arduino.h'), `#pragma once
#include <cstdio>
#include <cstdint>
struct SerialStub {
  template <typename... A> void printf(const char* f, A... a) { std::printf(f, a...); }
  void println(const char* s) { std::printf("%s\\n", s); }
};
inline SerialStub Serial;
`);
fs.writeFileSync(path.join(stubs, 'src/core/config/config_manager.h'), `#pragma once
struct ProbeConfig { char language[8] = "pl"; };
struct ProbeConfigManager { ProbeConfig config; const ProbeConfig& getConfig() const { return config; } };
inline ProbeConfigManager configManager;
`);
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, `
#include <lvgl.h>
#include "src/core/diagnostics/text_fit_probe.cpp"
extern "C" { LV_FONT_DECLARE(ui_font_20); }

static lv_obj_t* label(lv_obj_t* parent, const char* text, int width, lv_label_long_mode_t mode, int height = LV_SIZE_CONTENT) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, &ui_font_20, 0);
  lv_label_set_long_mode(l, mode);
  lv_label_set_text(l, text);
  lv_obj_set_size(l, width, height);
  return l;
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(800, 480);
  static uint8_t buffer[800 * 40 * 4];
  lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

  label(screen, "Odblokowywanie z tego panelu", 120, LV_LABEL_LONG_MODE_CLIP);       // cut
  label(screen, "Uzbrojony (poza domem)", 120, LV_LABEL_LONG_MODE_DOTS);              // cut, dots
  label(screen, "Zamek", 120, LV_LABEL_LONG_MODE_DOTS);                               // fits
  label(screen, "Encja musi być wybrana w HomeTiles Bridge.", 160, LV_LABEL_LONG_MODE_WRAP, 30);  // too tall
  label(screen, "Encja musi być wybrana w HomeTiles Bridge.", 160, LV_LABEL_LONG_MODE_WRAP);      // wraps, grows: fine
  lv_obj_t* button = lv_obj_create(screen);
  lv_obj_set_size(button, 100, 50);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  label(button, "Zaktualizuj HomeTiles Bridge", LV_SIZE_CONTENT, LV_LABEL_LONG_MODE_WRAP);   // outside parent
  lv_obj_t* hidden = label(screen, "Bardzo długi ukryty tekst etykiety", 60, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_add_flag(hidden, LV_OBJ_FLAG_HIDDEN);                                        // hidden: not checked
  lv_obj_t* colored = label(screen, "#FFC53D 1234# Wh", 110, LV_LABEL_LONG_MODE_CLIP);
  lv_label_set_recolor(colored, true);                                                // codes take no space: fits
  label(screen, "Moc 1234567890 W", 80, LV_LABEL_LONG_MODE_CLIP);                     // cut
  label(screen, "Moc 9876543210 W", 80, LV_LABEL_LONG_MODE_CLIP);                     // same finding, other value

  lv_refr_now(display);
  text_fit_probe::start();
  for (int i = 0; i < 3; ++i) {  // three scans: every finding is logged once
    lv_tick_inc(2100);
    lv_timer_handler();
  }
  return 0;
}
`);
const binary = path.join(out, 'probe' + (process.platform === 'win32' ? '.exe' : ''));
// The stubs come first: the probe's Arduino and config headers resolve to them.
let run = spawnSync(host.cxx, ['-I', stubs, ...host.flags, '-std=c++17', '-DHOMETILES_TEST_BETA',
  source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
const findings = run.stdout.split(/\r?\n/).filter(line => line.startsWith('[TextFit] ') && !line.includes('Checking'));
const reasons = findings.map(line => /^\[TextFit\] (\S+) lang=pl .*text="(.*)"$/.exec(line)).map(m => m && `${m[1]} ${m[2]}`);
assert.deepEqual(reasons, [
  'cut Odblokowywanie z tego panelu',
  'cut Uzbrojony (poza domem)',
  'too-tall Encja musi być wybrana w HomeTiles Bridge.',
  'outside-parent Zaktualizuj HomeTiles Bridge',
  'cut Moc 1234567890 W',
], run.stdout);
for (const line of findings) assert.match(line, /need=\d+ have=\d+ line=\d+ at=-?\d+,-?\d+/, line);
assert.match(fs.readFileSync(path.join(root, 'HomeTiles.ino'), 'utf8'),
             /#if defined\(HOMETILES_TEXT_FIT_PROBE\)\s*text_fit_probe::start\(\);\s*#endif/,
             'the probe starts after the UI is built, in beta builds only');
console.log(`Text fit probe: ${findings.length} labels that do not fit are logged once with their whole text`);

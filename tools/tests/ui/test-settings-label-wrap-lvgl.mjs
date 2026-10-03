// French Settings labels must keep their colon (V2 and S3, 2026-10-03).
// "Format de l'heure :" lost its colon in the clipped row label, and
// "Écran de veille :" wrapped its colon alone onto the next line.
//   - French puts a no-break space before : ; ! ? so a wrap moves the last
//     word together with the mark;
//   - Settings row labels wrap at a word instead of clipping.
// The wrap runs on real LVGL with the real font.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

// French: no plain space before : ; ! ? in any string or locale text.
const i18n = read('src/core/i18n/i18n.cpp');
const table = head => {
  const start = i18n.indexOf(head);
  assert.ok(start >= 0, head);
  return i18n.slice(start, i18n.indexOf('\nstatic const ', start + head.length));
};
const french = table('static const Strings kStringsFr = {') + table('static const LocaleProfile kLocaleFr = {');
const literals = [...french.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => m[1]);
assert.ok(literals.length > 300, 'French strings found');
assert.deepEqual(literals.filter(text => / [:;!?]/.test(text)), [], 'French marks keep a no-break space');
assert.ok(literals.includes('Format de l\'heure\\u00A0:') && literals.includes('Écran de veille\\u00A0:'));

// Settings rows wrap their labels.
const functions = cppFunctionDefinitions(read('src/ui/tabs/settings/tab_settings.cpp'));
for (const name of ['create_locale_dropdown_row', 'wifi_create_entry_row']) {
  const body = functions.find(f => f.name === name).source;
  assert.match(body, /lv_label_set_long_mode\(label, LV_LABEL_LONG_WRAP\);/, `${name} wraps its label`);
}

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the Settings label wrap test needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}
const out = path.join(root, 'build/tests/settings-label-fit');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, String.raw`
#include <lvgl.h>
#include <cassert>
#include <initializer_list>
extern "C" { LV_FONT_DECLARE(ui_font_24) }

static int line_of(lv_obj_t* label, uint32_t char_id) {
  lv_point_t pos;
  lv_label_get_letter_pos(label, char_id, &pos);
  return pos.y;
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(1280, 800);
  static uint8_t buffer[1280 * 40 * 4];
  lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });

  // Room for "Écran de veille" but not for its colon.
  lv_point_t words;
  lv_text_get_size(&words, "Écran de veille", &ui_font_24, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  for (const bool no_break : {false, true}) {
    lv_obj_t* label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(label, &ui_font_24, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, words.x + 2);
    lv_label_set_text(label, no_break ? "Écran de veille :" : "Écran de veille :");
    lv_obj_update_layout(label);
    const int veille = line_of(label, 9), colon = line_of(label, 16);
    if (no_break) {
      assert(veille > line_of(label, 0) && "the last word moves to the second line");
      assert(colon == veille && "the colon stays with its word");
    } else {
      assert(colon > veille && "a plain space strands the colon (the reported wrap)");
    }
  }
  return 0;
}
`);
const binary = path.join(out, 'wrap' + (process.platform === 'win32' ? '.exe' : ''));
let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-w', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
console.log('Settings labels wrap at a word and French keeps the colon with it');

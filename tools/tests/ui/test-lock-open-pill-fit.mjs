// The lock popup's Open pill was 290 px wide whatever it said. Polish
// "Na pewno otworzyć?" (Really open?) ran past both ends, cutting the door
// icon and the question mark (V2 with the simulator, 2026-10-03). The pill
// now takes the width of its longest label beside the icon, so it never cuts
// and keeps its width when the label changes; short labels keep 290 px.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const popup = read('src/ui/popups/device/device_popup.cpp');
const lock = cppFunctionDefinitions(popup).find(f => f.name === 'build_lock');
assert.ok(lock, 'build_lock');
assert.match(lock.source, /for \(const DeviceLabel id : \{DeviceLabel::OpenDoor, DeviceLabel::ReallyOpen, DeviceLabel::DoorOpen\}\) \{/,
  'the width covers every label the pill can show');
assert.match(lock.source, /pw = std::max\(pw, static_cast<int>\(icon_size\.x \+ popup_layout::scale\(10\) \+ size\.x \+\s*2 \* popup_layout::scale\(16\)\)\);/);
assert.match(lock.source, /pw = std::min\(pw, content_w\);/, 'never wider than the popup');

// The reported Polish texts are the ones the panel shows.
const i18n = read('src/core/i18n/i18n.cpp');
const polish = i18n.slice(i18n.indexOf('static const LocaleProfile kLocalePl = {'));
for (const text of ['Na pewno otworzyć?', 'Otwórz drzwi']) assert.ok(polish.includes(`"${text}"`), text);

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the Open pill width check needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}
const out = path.join(root, 'build/tests/lock-open-pill-fit');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
// V2 layout: font24 is ui_font_24, the icons mdi_icons_48, the pill 92 px high.
fs.writeFileSync(source, String.raw`
#include <lvgl.h>
#include <cassert>
#include <cstdio>
extern "C" { LV_FONT_DECLARE(ui_font_24) LV_FONT_DECLARE(mdi_icons_48) }
static int width(const char* text, const lv_font_t* font) {
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}
int main() {
  lv_init();
  const int icon = lv_font_get_glyph_width(&mdi_icons_48, 0xF081C, 0);  // door-open
  const int pad = 2 * 16, gap = 10, old_width = 290;
  const int need = icon + gap + width("Na pewno otworzyć?", &ui_font_24) + pad;
  assert(need > old_width && "the Polish confirmation did not fit the 290 px pill");
  assert(icon + gap + width("Vraiment ouvrir ?", &ui_font_24) + pad <= old_width &&
         "the French confirmation keeps the 290 px pill");
  printf("%d\n", need);
  return 0;
}
`);
const binary = path.join(out, 'pill' + (process.platform === 'win32' ? '.exe' : ''));
let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-w', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
console.log(`Lock Open pill: Polish needs ${run.stdout.trim()} px, the pill now takes its longest label`);

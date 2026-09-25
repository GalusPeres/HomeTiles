// An open popup follows its tile's color, not only when it opens: when a
// rules tint changes (Climate mode, Battery level, ...), refresh_card hands
// the card's new background to the open popup of that card. Every opener
// goes through popup_background(), which remembers the opener chain without
// dereferencing it later; each popup restyles its derived surfaces.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const source = read('src/tiles/runtime/tile_icon_source.cpp');
for (const marker of [
  'lv_obj_t* g_popup_source[kPopupSourceDepth] = {};',
  'void remember_popup_source(lv_obj_t* obj) {',
  'for (lv_obj_t* source : g_popup_source) opened_here = opened_here || source == card;',
  'if (!card || !popup_shell_active()) return;',
  'remember_popup_source(obj);',
  'follow_open_popup(card);',
]) assert.ok(source.includes(marker), `tile_icon_source: ${marker}`);
for (const call of ['climate_popup_follow_tile_color(color);', 'light_popup_follow_tile_color(color);',
  'cover_popup_follow_tile_color(color);', 'sensor_popup_follow_tile_color(color);',
  'energy_popup_follow_tile_color(color);', 'weather_popup_follow_tile_color(color);',
  'popup_shell_follow_tile_color(color);']) assert.ok(source.includes(call), call);

// Each popup exports its follow function and ignores hidden popups.
for (const [file, header, fn] of [
  ['src/ui/popups/climate/climate_popup.cpp', 'src/ui/popups/climate/climate_popup.h', 'climate_popup_follow_tile_color'],
  ['src/ui/popups/light/light_popup.cpp', 'src/ui/popups/light/light_popup.h', 'light_popup_follow_tile_color'],
  ['src/ui/popups/cover/cover_popup.cpp', 'src/ui/popups/cover/cover_popup.h', 'cover_popup_follow_tile_color'],
  ['src/ui/popups/sensor/sensor_popup.cpp', 'src/ui/popups/sensor/sensor_popup.h', 'sensor_popup_follow_tile_color'],
  ['src/ui/popups/energy/energy_popup.cpp', 'src/ui/popups/energy/energy_popup.h', 'energy_popup_follow_tile_color'],
  ['src/ui/popups/weather/weather_popup.cpp', 'src/ui/popups/weather/weather_popup.h', 'weather_popup_follow_tile_color'],
]) {
  assert.ok(read(header).includes(`void ${fn}(uint32_t color);`), `${header} declares ${fn}`);
  const body = read(file);
  const start = body.indexOf(`void ${fn}(uint32_t`);
  assert.ok(start >= 0, `${file} defines ${fn}`);
  assert.ok(body.slice(start, start + 400).includes('lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN)'), `${fn} skips hidden popups`);
}
// Climate keeps an open control menu while following; Light re-derives its
// card-based visuals from the current state.
assert.ok(read('src/ui/popups/climate/climate_popup.cpp').includes('apply_card_color(ctx, color, false);'));
assert.match(read('src/ui/popups/light/light_popup.cpp'),
  /apply_card_cutouts\(ctx\);\s*apply_mode_visibility\(ctx\);\s*update_preview\(ctx\);/);
// Editable controls restyle when their card color changed.
assert.match(read('src/types/value/value_control.cpp'),
  /void editable_control_refresh\(EditableControl\* c\) \{\s*if \(!c \|\| !c->active\) return;\s*\/\/[^\n]*\s*apply_control_colors\(c\);/);
// The shell body setter for the other popups.
assert.ok(read('src/ui/popups/popup_shell.cpp').includes('void popup_shell_follow_tile_color(uint32_t color) {'));
console.log('Open popups follow their tile color');

// Media popup layout "Cover links" / "Cover Mitte" (user 2026-10-19): the
// segmented choice sets a hidden select that the editor binds like every
// other field (draft and autosave), travels as media_layout to
// Tile::sensor_display_mode (mediaPopupCoverCentered), survives copy/paste
// and import, and reaches the popup from both openers (the tile press and the
// tile's state updates). Its texts and the popup's sub-view texts come from
// the central Strings in every language.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

// Web Admin field and its segmented buttons.
assert.ok(read('src/types/media/web_html.cpp').includes(
  'append_switch_choice(html, tab_id, "media_layout", tr.media_popup_layout, layouts, 2);'));
assert.ok(read('src/types/media/web_html.cpp').includes(
  'const SwitchChoice layouts[] = {{"0", tr.media_layout_cover_left}, {"1", tr.media_layout_cover_center}};'));
assert.ok(read('src/types/switch/admin.js').includes("'media_layout'"), 'segmented buttons follow the select');

// Editor: load, save, reset, live binding, draft snapshot, import/export.
const admin = read('src/types/media/admin.js');
assert.ok(admin.includes("if (layout) layout.value = Number(data.sensor_display_mode) === 1 ? '1' : '0';"), 'load');
assert.ok(admin.includes("formData.append('media_layout', document.getElementById(prefix + '_media_layout')?.value === '1' ? '1' : '0');"), 'save');
assert.match(admin, /if \(layout\) layout\.value = '0';\s*if \(typeof syncSwitchChoices === 'function'\) syncSwitchChoices\(tab\);\s*\}/, 'reset');
assert.match(read('src/web/admin/tiles/editor.js'),
  /bindLive\(document\.getElementById\(prefix \+ '_media_layout'\), 'change', 'mediaLayout', \(\) => \{ updateDraft\(tab\); scheduleAutoSave\(tab\); \}\);/,
  'editor binds media_layout to the draft and autosave');
assert.match(read('src/web/admin/tiles/snapshots.js'),
  /if \(Number\(tile\.type\) === 15 && snapshot && Object\.prototype\.hasOwnProperty\.call\(snapshot, 'media_layout'\)\) \{\s*tile\.sensor_display_mode = String\(snapshot\.media_layout\) === '1' \? 1 : 0;/,
  'the draft snapshot keeps the layout');
assert.ok(read('src/web/admin/tiles/import-export.js').includes(
  "fd.append('media_layout', Number(tile.sensor_display_mode) === 1 ? '1' : '0');"), 'import and paste keep it');

// Firmware: stored in the existing V7 field, read by both popup openers.
assert.match(read('src/types/media/web_handler.cpp'),
  /tile\.sensor_display_mode = server\.hasArg\("media_layout"\) && server\.arg\("media_layout"\)\.toInt\(\) == 1 \? 1 : 0;/);
assert.match(read('src/tiles/config/tile_config.h'),
  /static inline bool mediaPopupCoverCentered\(const Tile& tile\) \{\s*return tile\.type == TILE_MEDIA && tile\.sensor_display_mode == 1;/);
const renderer = read('src/types/media/renderer.cpp');
assert.ok(renderer.includes('mediaPopupCoverCentered(tile)') && renderer.includes('init.cover_centered = data->cover_centered;'),
  'the tile press opens the chosen layout');
assert.ok(read('src/tiles/runtime/tile_renderer.cpp').includes('init.cover_centered = mediaPopupCoverCentered(tile);'),
  'state updates keep the chosen layout');
assert.match(read('src/ui/popups/media/media_popup.cpp'),
  /if \(!ctx->player \|\| ctx->centered != init\.cover_centered \|\| ctx->built_colors != colors_key\(ctx->colors\)\) \{\s*build_player\(ctx, init\.cover_centered\);/,
  'the popup builds the chosen layout');

// Every language has the new texts, appended in the Strings order.
const fields = ['media_popup_layout', 'media_layout_cover_left', 'media_layout_cover_center', 'media_source',
  'media_sound_mode', 'media_speakers', 'media_library', 'media_library_empty', 'media_library_failed'];
const header = read('src/core/i18n/i18n.h');
const structBody = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const declared = [...structBody.matchAll(/const char\* (\w+);/g)].map(match => match[1]);
assert.deepEqual(declared.slice(-fields.length), fields, 'the media texts end the Strings schema');
const i18n = read('src/core/i18n/i18n.cpp');
for (const lang of ['De', 'En', 'Fr', 'Pl', 'Cs', 'Es', 'Sk']) {
  const start = i18n.indexOf(`static const Strings kStrings${lang} = {`);
  assert.ok(start >= 0, lang);
  const table = i18n.slice(start, i18n.indexOf('\n};', start));
  const texts = [...table.matchAll(/^\s*"((?:[^"\\]|\\.)*)",\s*$/gm)].map(match => match[1]);
  const tail = texts.slice(-fields.length);
  assert.equal(tail.length, fields.length, `${lang}: media texts`);
  for (const text of tail) assert.ok(text.length > 0, `${lang}: empty media text`);
}
// The popup shows them through the central table only.
const popup = read('src/ui/popups/media/media_popup.cpp');
for (const field of ['media_source', 'media_sound_mode', 'media_speakers', 'media_library',
  'media_library_empty', 'media_library_failed']) {
  assert.ok(popup.includes(`tr.${field}`), `the popup shows ${field}`);
}
console.log('Media popup layout: Web Admin choice, editor draft, storage, both openers and the texts in every language');

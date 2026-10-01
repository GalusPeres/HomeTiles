// Climate Layout "Title only" / "With value" (user 2026-10-01) and the Cover
// value size: the segmented choice sets a hidden select, which the editor must
// bind like every other field, so a click updates the preview, the draft and
// autosave (b162 regression: the choice changed nothing). The value travels
// as climate_view to Tile::sensor_display_mode (climateTileShowsValue).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

const editor = read('src/web/admin/tiles/editor.js');
for (const [field, name] of [['climate_view', 'climateView'], ['cover_value_font', 'coverValueFont']]) {
  const binding = new RegExp(`bindLive\\(document\\.getElementById\\(prefix \\+ '_${field}'\\), 'change', '${name}', \\(\\) => \\{[\\s\\S]*?updateTilePreview\\(tab\\); updateDraft\\(tab\\); scheduleAutoSave\\(tab\\);`);
  assert.match(editor, binding, `editor binds ${field}`);
}
assert.match(editor, /'climateView', \(\) => \{\s*syncClimateSlotFields\(tab\);/, 'the layout updates the automatic mini fields');

assert.ok(read('src/types/climate/web_html.cpp').includes(
  'append_switch_choice(html, tab_id, "climate_view", tr.switch_display, layouts, 2);'));
assert.ok(read('src/types/switch/admin.js').includes("'climate_view'"), 'segmented buttons follow the select');
const persistence = read('src/types/climate/admin-persistence.js');
assert.ok(persistence.includes("formData.append('climate_view', view);"));
assert.ok(read('src/types/climate/admin-editor.js').includes(
  "if (view) view.value = Number(data.sensor_display_mode) === 1 ? '1' : '0';"));
assert.match(read('src/types/climate/web_handler.cpp'),
  /if \(server\.hasArg\("climate_view"\)\) \{\s*tile\.sensor_display_mode = server\.arg\("climate_view"\)\.toInt\(\) == 1 \? 1 : 0;/);
assert.match(read('src/tiles/config/tile_config.h'),
  /return tile\.type == TILE_CLIMATE && tile\.sensor_display_mode == 1;/);
assert.ok(read('src/types/climate/renderer.cpp').includes(
  'const bool with_value = compact || climateTileShowsValue(tile);'));
console.log('Climate layout and Cover value size: bound in the editor, saved and rendered');

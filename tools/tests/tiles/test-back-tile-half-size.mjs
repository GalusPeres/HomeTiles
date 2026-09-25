// The Back tile resizes in half steps down to 1x0.5 like Sensor/Binary/
// Energy/Clock; at half height it uses the half-height Sensor header (arrow in
// the corner disc, title beside it). It can hide its border per tile through
// the Clock/Text border flag. Settings stays on whole cells.
import assert from 'node:assert/strict';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// Firmware geometry.
const geometry = read('src/tiles/config/tile_geometry.h');
assert.match(geometry, /inline bool half_size\(int type\) \{ return sensor\(type\) \|\| type == TILE_CLOCK \|\| type == TILE_BACK; \}/);
assert.match(geometry, /if \(type == TILE_SETTINGS &&\s*\(fractional\(col\)/, 'Settings stays whole');
assert.doesNotMatch(geometry, /type == TILE_BACK\) &&\s*\(fractional/, 'Back may use half steps');
assert.match(geometry, /inline bool compact_back\(int type, float w, float h\) \{\s*return type == TILE_BACK && w >= 1 && h == 0\.5f;/);

// Editor geometry mirrors the firmware.
const helpers = ['isCompactSensorType', 'supportsHalfSize', 'supportedTileLayout'].map(extractDeliveredFunction).join('\n');
const {supportsHalfSize, supportedTileLayout} = new Function(`${helpers}; return {supportsHalfSize, supportedTileLayout};`)();
assert.ok(supportsHalfSize(8) && supportsHalfSize('8'));
assert.ok(!supportsHalfSize(7) && !supportsHalfSize(4));
assert.ok(supportedTileLayout(8, {col: 0, row: 0.5, span_w: 1, span_h: 0.5}), 'Back 1x0.5');
assert.ok(supportedTileLayout(8, {col: 1.5, row: 0, span_w: 1.5, span_h: 1}), 'Back half steps');
assert.ok(!supportedTileLayout(8, {col: 0, row: 0, span_w: 0.5, span_h: 1}), 'Back stays at least one cell wide');
assert.ok(!supportedTileLayout(7, {col: 0.5, row: 0, span_w: 1, span_h: 1}), 'Settings stays whole');
assert.match(read('src/web/admin/tiles/type-selection.js'), /const fixedGrid = type => Number\(type\) === 7;/);
assert.match(read('src/web/admin/tiles/layout.js'), /if \(type === 7\) \{/);

// Device layout: half-height Back uses the compact header; taller Back keeps
// the centered icon with the round disc and the title below.
const navigate = read('src/types/navigate/renderer.cpp');
assert.ok(navigate.includes('const bool compact = tile_geometry::compact_back(tile.type, tile.span_w, tile.span_h);'));
assert.ok(navigate.includes('if (compact) compact_sensor_layout::apply(btn, icon_lbl, title_lbl, nullptr, tile);'));
assert.match(navigate, /if \(!compact\) \{[\s\S]*?tile_icon_disc::add_round\(btn, icon_lbl\);/);
assert.ok(read('src/tiles/runtime/compact_sensor_layout.h').includes(
  'const int block = title_font()->line_height + (value ? value_face->line_height + gap : 0);'),
  'Without a value the title is centered on the disc row');

// Per-tile border: the Clock/Text flag path.
assert.match(read('src/tiles/config/tile_config.h'),
  /tile\.type != TILE_CLOCK && tile\.type != TILE_TEXT && tile\.type != TILE_BACK\) \|\|\s*tile\.sensor_display_mode != 1;/);
const registry = read('src/types/types_registry.cpp');
assert.match(registry, /bool apply_back_wrapper\(WebServer& server,[\s\S]*?if \(server\.hasArg\("tile_border"\)\) \{\s*tile\.sensor_display_mode = server\.arg\("tile_border"\)\.toInt\(\) == 0 \? 1 : 0;/);
assert.match(registry, /TILE_BACK,\s*"Zurück",\s*"navigate",\s*"back",\s*"none",\s*nullptr,\s*"loadBackFields",\s*"saveBackFields",\s*"resetBackFields",/);
assert.ok(read('src/web/server/handlers/web_admin_tiles.cpp').includes('(type == TILE_CLOCK || type == TILE_TEXT || type == TILE_BACK)) tile.sensor_display_mode = 0;'));
const backHtml = read('src/types/navigate/web_html.cpp');
assert.ok(backHtml.includes('_back_fields" class="type-fields">') && backHtml.includes('_back_tile_border" checked>)html";') &&
  backHtml.includes('html += tr.screensaver_tile_border;'), 'Localized Back border checkbox');
const admin = read('src/types/navigate/admin.js');
for (const name of ['loadBackFields', 'saveBackFields', 'resetBackFields']) assert.ok(admin.includes(`function ${name}(`), name);
assert.ok(read('src/web/admin/tiles/editor.js').includes("for (const kind of ['clock','text','back']) {"));
assert.ok(read('src/web/admin/tiles/live-preview.js').includes("const borderToggle = type === '8' ? '_back_tile_border'"));
assert.ok(read('src/web/admin/tiles/grid-preview.js').includes("['8','9','10'].includes(typeValue) && Number(tile.sensor_display_mode) === 1"));
assert.ok(read('src/web/admin/tiles/snapshots.js').includes('[8,9,10].includes(Number(tile.type)) && snapshot?.tile_border !== undefined'));
assert.match(read('src/web/admin/tiles/import-export.js'), /\} else if \(safeType === 8\) \{\s*fd\.append\('tile_border'/);

// Preview: half-height Back uses the compact header classes with a centered title.
assert.ok(read('src/web/server/render/web_admin_html.cpp').includes('cssClass += " sensor-compact sensor-half compact-title-only";'));
assert.ok(read('src/web/admin/tiles/layout.js').includes("const compactBack = Number(type) === 8 && halfHeight;"));
assert.match(read('src/web/assets/admin.css'), /\.tile\.sensor-compact\.compact-title-only > \.tile-title \{\s*top:max\(0px, calc\(\(var\(--compact-h\) - var\(--compact-title-line\)\) \/ 2\)\);/);
console.log('Back tile: half steps down to 1x0.5, compact header, per-tile border and preview pass');

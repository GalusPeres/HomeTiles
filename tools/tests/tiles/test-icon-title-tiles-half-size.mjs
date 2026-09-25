// The icon-and-title tiles (Scene, Folder, Back, Camera) resize in half steps
// down to 1x0.5 like Sensor/Binary/Energy/Clock; at half height they use the
// half-height Sensor header (icon in the corner disc, title beside it). They
// take a fixed icon color with optional glow. Back can hide its border per
// tile through the Clock/Text border flag. Settings stays on whole cells.
import assert from 'node:assert/strict';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// Firmware geometry.
const geometry = read('src/tiles/config/tile_geometry.h');
assert.match(geometry, /inline bool icon_title\(int type\) \{\s*return type == TILE_SCENE \|\| type == TILE_FOLDER \|\| type == TILE_BACK \|\| type == TILE_CAMERA;/);
assert.match(geometry, /inline bool half_size\(int type\) \{ return sensor\(type\) \|\| type == TILE_CLOCK \|\| icon_title\(type\); \}/);
assert.match(geometry, /if \(type == TILE_SETTINGS &&\s*\(fractional\(col\)/, 'Settings stays whole');
assert.doesNotMatch(geometry, /type == TILE_BACK\) &&\s*\(fractional/, 'Back may use half steps');
assert.match(geometry, /inline bool compact_icon_title\(int type, float w, float h\) \{\s*return icon_title\(type\) && w >= 1 && h == 0\.5f;/);
assert.doesNotMatch(geometry, /compact_back/);

// Editor geometry mirrors the firmware.
const helpers = ['isCompactSensorType', 'supportsHalfSize', 'supportedTileLayout'].map(extractDeliveredFunction).join('\n');
const {supportsHalfSize, supportedTileLayout} = new Function(`${helpers}; return {supportsHalfSize, supportedTileLayout};`)();
for (const type of [2, 4, 8, 18]) {
  assert.ok(supportsHalfSize(type) && supportsHalfSize(String(type)), `type ${type} may be half a row high`);
  assert.ok(supportedTileLayout(type, {col: 0, row: 0.5, span_w: 1, span_h: 0.5}), `type ${type} 1x0.5`);
}
assert.ok(!supportsHalfSize(7) && !supportsHalfSize(5) && !supportsHalfSize(10));
assert.ok(supportedTileLayout(8, {col: 0, row: 0.5, span_w: 1, span_h: 0.5}), 'Back 1x0.5');
assert.ok(supportedTileLayout(8, {col: 1.5, row: 0, span_w: 1.5, span_h: 1}), 'Back half steps');
assert.ok(!supportedTileLayout(8, {col: 0, row: 0, span_w: 0.5, span_h: 1}), 'Back stays at least one cell wide');
assert.ok(!supportedTileLayout(7, {col: 0.5, row: 0, span_w: 1, span_h: 1}), 'Settings stays whole');
assert.match(read('src/web/admin/tiles/type-selection.js'), /const fixedGrid = type => Number\(type\) === 7;/);
assert.match(read('src/web/admin/tiles/layout.js'), /if \(type === 7\) \{/);

// Device layout: half-height Back uses the compact header; taller Back keeps
// the centered icon with the round disc and the title below.
const navigate = read('src/types/navigate/renderer.cpp');
assert.ok(navigate.includes('const bool compact = tile_geometry::compact_icon_title(tile.type, tile.span_w, tile.span_h);'));
assert.ok(navigate.includes('if (compact) compact_sensor_layout::apply(btn, icon_lbl, title_lbl, nullptr, tile);'));
assert.match(navigate, /if \(!compact\) \{[\s\S]*?tile_icon_disc::add_round\(btn, icon_lbl\);/);
const scene = read('src/types/scene/renderer.cpp');
assert.ok(scene.includes('const bool compact = tile_geometry::compact_icon_title(tile.type, tile.span_w, tile.span_h);'));
assert.ok(scene.includes('if (compact) compact_sensor_layout::apply(btn, icon_lbl, title_lbl, nullptr, tile);'));
assert.match(scene, /if \(compact\) \{\s*\/\/ An image icon takes the disc's size and corner position\.\s*const int size = tile_icon_disc::diameter\(\);/);
assert.match(scene, /if \(!compact\) \{[\s\S]*?tile_icon_disc::add_round\(btn, icon_lbl\);/);
const camera = read('src/types/camera/renderer.cpp');
assert.ok(camera.includes('const bool compact = tile_geometry::compact_icon_title(tile.type, tile.span_w, tile.span_h);'));
assert.match(camera, /if \(compact\) \{\s*compact_sensor_layout::apply\(card, icon, title_label, nullptr, tile\);/);
assert.match(camera, /if \(!compact\) \{[\s\S]*?tile_icon_disc::add_round\(card, icon\);/);

// Fixed icon color with glow: persisted like the Sensor family's record,
// applied once when the tile is built, previewed with the same color.
const policy = read('src/types/tile_type_policy.h');
assert.match(policy, /static constexpr bool tileTypeHasFixedIconColorOnly\(int type\) \{\s*return type == TILE_SCENE \|\| type == TILE_FOLDER \|\| type == TILE_BACK \|\| type == TILE_CAMERA;/);
assert.match(policy, /tileTypeIsEditableValue\(type\) \|\| tileTypeHasFixedIconColorOnly\(type\);/);
assert.ok(!/tileTypeIconColorsByValue[^}]*TILE_(SCENE|FOLDER|BACK|CAMERA)/.test(policy) &&
  !/tileTypeIconColorsByState[^}]*TILE_(SCENE|FOLDER|BACK|CAMERA)/.test(policy), 'Fixed color only: no bar and no state list');
assert.match(read('src/tiles/runtime/tile_icon_color_rules.h'),
  /inline void apply_fixed\(lv_obj_t\* icon, const char\* record\) \{\s*apply\(icon, record, true, "", nullptr, lv_color_white\(\)\);/);
for (const [name, source, icon] of [['navigate', navigate, 'icon_lbl'], ['scene', scene, 'icon_lbl'], ['camera', camera, 'icon']]) {
  assert.ok(source.includes(`tile_icon_color_rules::apply_fixed(${icon}, tile.icon_colors.c_str());`), `${name} applies the fixed icon color`);
}
const colorHelpers = ['tileTypeHasIconColors', 'tileTypeHasFixedIconColorOnly'].map(extractDeliveredFunction).join('\n');
const iconColorsSource = read('src/web/admin/tiles/icon-colors.js');
const constants = iconColorsSource.match(/const ICON_COLOR_FIXED_TYPES = [^;]+;\s*const ICON_COLOR_TYPES = [^;]+;/)[0];
const {tileTypeHasIconColors, tileTypeHasFixedIconColorOnly} =
  new Function(`${constants}\n${colorHelpers}; return {tileTypeHasIconColors, tileTypeHasFixedIconColorOnly};`)();
for (const type of ['2', '4', '8', '18']) {
  assert.ok(tileTypeHasIconColors(type) && tileTypeHasFixedIconColorOnly(type), `type ${type} has a fixed icon color`);
}
assert.ok(!tileTypeHasFixedIconColorOnly('1') && !tileTypeHasIconColors('7'));
const tileTypeHasColoredIcon = new Function(`${extractDeliveredFunction('tileTypeHasColoredIcon')}; return tileTypeHasColoredIcon;`)();
for (const type of ['2', '4', '8', '18']) assert.ok(tileTypeHasColoredIcon(type), `Glow checkbox for type ${type}`);
assert.ok(!tileTypeHasColoredIcon('7') && !tileTypeHasColoredIcon('10'));
const iconColorRuleState = new Function('tileTypeHasFixedIconColorOnly',
  `${extractDeliveredFunction('iconColorRuleState')}; return iconColorRuleState;`)(tileTypeHasFixedIconColorOnly);
assert.deepEqual({...iconColorRuleState('18', 'camera.door', {values: {}}, null)}, {state: '', display: null},
  'Icon-and-title previews always resolve the fixed color');
for (const [file, names] of [['src/types/scene/admin.js', 'Scene'], ['src/types/camera/admin.js', 'Camera'],
                             ['src/types/navigate/admin.js', 'Navigate'], ['src/types/navigate/admin.js', 'Back']]) {
  const source = read(file);
  for (const [kind, call] of [['load', 'loadIconColorFields(tab, data);'], ['save', 'saveIconColorFields(tab, formData);'],
                              ['reset', 'resetIconColorFields(tab);']]) {
    const start = source.indexOf(`function ${kind}${names}Fields(`);
    const end = source.indexOf('\n  }', start);
    assert.ok(start >= 0 && source.slice(start, end).includes(call), `${kind}${names}Fields carries the icon color`);
  }
}
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
const serverPreview = read('src/web/server/render/web_admin_html.cpp');
assert.ok(serverPreview.includes('if (tile_geometry::compact_icon_title(tile.type, span_w, span_h)) {') &&
  serverPreview.includes('cssClass += " sensor-compact sensor-half compact-title-only";'));
assert.ok(read('src/web/admin/tiles/layout.js').includes("const compactIconTitle = [2, 4, 8, 18].includes(Number(type)) && halfHeight;"));
assert.match(read('src/web/assets/admin.css'), /\.tile\.sensor-compact\.compact-title-only > \.tile-title \{\s*top:max\(0px, calc\(\(var\(--compact-h\) - var\(--compact-title-line\)\) \/ 2\)\);/);
console.log('Icon-and-title tiles: 1x0.5 compact header, fixed icon color with glow, Back border and preview pass');

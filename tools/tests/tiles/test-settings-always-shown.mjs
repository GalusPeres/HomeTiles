// The Settings tile is always to be tapped (user 2026-10-12: "weil sie immer
// anklickbar sein muss"): parked, or on the screen. A Settings tile without a
// place in the active layout (left in the layout window's storage) is not
// hidden on the panel but shows at the first free spot from the bottom right,
// in its own size or 1 x 1, without a write. The layout window treats it like
// a folder: on the screen (a new layout puts it bottom right), red in the
// storage, which blocks saving; parking and the gear stay independent.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/tiles/config/tile_config.cpp'), 'utf8').replace(/\r\n/g, '\n');
const fn = name => cppFunctionDefinitions(source).find(f => f.name === name).source;

// After every tile has its place in the active layout (every layout,
// classic too).
assert.match(fn('applyLayoutPlaces'), /tile\.span_h = place\.span_h;\s*\}\s*showUnplacedSettingsTile\(grid\);\s*\}$/);

const show = fn('showUnplacedSettingsTile');
assert.match(show, /if \(tile\.type == TILE_SETTINGS && tile\.layout_hidden\) settings = &tile;/, 'only a hidden Settings tile');
// A spot inside the shown grid, of a size the type supports, clear of every
// tile that shows.
assert.match(show, /!grid_layout::inside\(col, row, span_w, span_h\) \|\|\s*!tile_geometry::supported_size\(TILE_SETTINGS, col, row, span_w, span_h\)/);
assert.match(show, /if \(&tile == settings \|\| tile\.type == TILE_EMPTY \|\| !shownTileLayout\(tile, c, r, w, h\)\) continue;/);
// From the bottom right, its own size first, then 1 x 1.
assert.match(show, /for \(float row = grid_layout::shown_rows\(\) - size\[1\]; row >= 0; row -= 0\.5f\)/);
assert.match(show, /for \(float col = grid_layout::shown\(\)\.cols - size\[0\]; col >= 0; col -= 0\.5f\)/);
assert.match(show, /\{1\.0f, 1\.0f\}\};/);
assert.match(show, /settings->layout_hidden = false;/);
// Shown only: nothing is written here.
assert.doesNotMatch(show, /save|commit|tile_layouts::set/);

// The layout window: red in the storage with its own note, the folders'
// badge (a tick on the screen, "!" in the storage).
const windowJs = fs.readFileSync(path.join(root, 'src/web/admin/tiles/layout-window.js'), 'utf8').replace(/\r\n/g, '\n');
assert.match(windowJs, /function layoutMustReach\(type\) \{ return \[4, 7\]\.includes\(Number\(type\)\); \}/);
assert.match(windowJs, /return !layoutInside\(tile, key, tab\) && \(layoutMustReach\(tile\.type\) \|\| !layoutParked\(tile, key\)\);/);
assert.match(windowJs, /:scope > \.tile:is\(\[data-type="4"\], \[data-type="7"\]\):not\(\.empty\)/);
assert.match(windowJs, /if \(Number\(tile\.type\) === 7\) settingsMissing\+\+;/);
assert.match(windowJs, /if \(settingsMissing\) notes\.push\(\['missing', 'cog-outline', t\('layoutSettingsMissing'\)\]\);/);
assert.match(windowJs, /const tiles = total - missing - settingsMissing;/);
// The note in every language, through the central table.
const i18n = fs.readFileSync(path.join(root, 'src/core/i18n/i18n.cpp'), 'utf8');
for (const [one, settings] of [['**1 Kachel** ohne Platz', '**Settings** ohne Platz'],
  ['**1 tile** without a place', '**Settings** without a place'], ['**1 tuile** sans place', '**Réglages** sans place'],
  ['**Kafelki bez miejsca: 1**', '**Ustawienia bez miejsca**']]) {
  assert.ok(i18n.replace(/\r\n/g, '\n').includes(`    "${one}",\n    "${settings}",\n`), settings);
}
assert.match(fs.readFileSync(path.join(root, 'src/web/server/render/web_admin_scripts.cpp'), 'utf8'),
  /appendJsEntry\("layoutSettingsMissing", tr\.layout_settings_missing\);/);
console.log('Settings tile: always shown on the panel unless parked, never stored in the layout window');

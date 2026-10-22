// The Settings field like a page's folder (user 2026-10-22, Tab5: classic
// 7 x 4, head bar upright 3 x 6). Parking from upright row 3.5 failed ("the
// parking sent the tile's upright place and the panel checked it against the
// classic 4 rows"), a fix that kept a classic place brought it back half
// high. Now: parked it keeps its look and ID but no place and no size; out
// of its field it is one cell where it is dropped (the drop spot checked in
// the layout the panel shows), else on the first free cell from the bottom
// right; the checkbox brings it back the same way. The compiled placement
// is tools/tests/core/test-settings-half-grid-restore.mjs.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const handlers = readRepoFile('src/web/server/handlers/web_admin_handlers.cpp').replace(/\r\n/g, '\n');
const access = handlers.slice(handlers.indexOf('if (server.hasArg("settings_access_present")) {'),
  handlers.indexOf('cfg.settings_tile_hidden = hide_tile;'));

// The drop spot: bounded by the shown layout's grid, the classic one only
// while the classic layout shows.
assert.match(access, /const bool classic_shown = grid_layout::active\(\) == grid_layout::Layout::kClassic;\s*const float place_cols = classic_shown \? GRID_COLS : grid_layout::shown\(\)\.cols;\s*const float place_rows = classic_shown \? GRID_ROWS : grid_layout::shown_rows\(\);/);
assert.match(access, /parse_bounded_arg\("settings_tile_target_col", 0, place_cols - 1,\s*settings_tile_target_col\) \|\|\s*!parse_bounded_arg\("settings_tile_target_row", 0, place_rows - 0\.5f,\s*settings_tile_target_row\)/);
// The parking's snapshot: no place and no size is read or checked.
assert.doesNotMatch(access, /settings_tile_col|settings_tile_row"|settings_tile_span/);
assert.match(access, /SettingsTileSnapshot& snapshot = cfg\.settings_tile_snapshot;[\s\S]*?snapshot\.span_w = 1;\s*snapshot\.span_h = 1;/);
const tab5 = {classic: {cols: 7, rows: 4}, upright: {cols: 3, rows: 6}};
const dropOk = (grid, row) => row >= 0 && row <= grid.rows - 0.5;
assert.equal(dropOk(tab5.classic, 5), false, 'Classic refused a drop in row 5');
assert.equal(dropOk(tab5.upright, 5), true, 'Upright takes a drop in row 5');

// No classic place, no repair of a dropped snapshot, no kept place per
// layout: the panel never looks one up.
const config = readRepoFile('src/tiles/config/tile_config.cpp').replace(/\r\n/g, '\n');
assert.doesNotMatch(config, /classicSettingsTilePlace/);
assert.match(config, /void TileConfig::giveParkedSettingsTileId\(\) \{\s*const SettingsTileSnapshot& kept = configManager\.getConfig\(\)\.settings_tile_snapshot;\s*if \(!kept\.valid \|\| kept\.view_id\) return;/,
  'only the old ID hand-out of b445, no snapshot made up');
const ensure = config.slice(config.indexOf('bool TileConfig::ensureSettingsTile('), config.indexOf('bool TileConfig::removeSettingsTiles('));
assert.match(ensure, /const float span_w = 1;\s*const float span_h = 1;/);
assert.doesNotMatch(ensure, /tile_layouts::find|snapshot\.col|snapshot\.row|snapshot\.span/);
assert.match(ensure, /tile\.view_id = snapshot\.valid \? snapshot\.view_id : 0;/, 'Back with its ID');
const snapshotFn = config.slice(config.indexOf('void TileConfig::snapshotSettingsTile('), config.indexOf('int TileConfig::settingsTileIndex() {'));
assert.match(snapshotFn, /out\.span_w = 1;\s*out\.span_h = 1;/, 'Parked it is one cell');
// A place outside the classic grid loses only the place, never the snapshot.
const manager = readRepoFile('src/core/config/config_manager.cpp').replace(/\r\n/g, '\n');
assert.match(manager, /if \(!snapshot\.valid\) \{\s*clear_settings_tile_snapshot\(normalized\);\s*\} else if \(!tile_geometry::supported\(TILE_SETTINGS, snapshot\.col, snapshot\.row,\s*snapshot\.span_w, snapshot\.span_h\)\) \{[\s\S]*?snapshot\.col = 0;\s*snapshot\.row = 0;/);
// The layout window keeps it one cell too.
const tiles = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp').replace(/\r\n/g, '\n');
assert.match(tiles, /if \(parked_settings_classic && cfg->settings_tile_snapshot\.valid\) \{[\s\S]*?cfg->settings_tile_snapshot\.span_w = 1;\s*cfg->settings_tile_snapshot\.span_h = 1;/);
assert.match(tiles, /if \(kept\.valid && kept\.view_id && tileConfig\.settingsTileIndex\(\) < 0\) \{\s*json \+= ",\\"tile\\":\{\\"type\\":7,\\"view_id\\":"/,
  'the layout window gets the parked tile while it has an ID');

// The Web Admin: one cell parked and out of its field, no place or size
// sent.
const accessJs = readRepoFile('src/web/admin/settings/access.js').replace(/\r\n/g, '\n');
const save = accessJs.slice(accessJs.indexOf('async function saveSettingsAccess('), accessJs.indexOf("const lockedBefore = pinToggle.dataset.pinConfigured === '1';"));
assert.doesNotMatch(save, /settings_tile_col'|settings_tile_row'|settings_tile_span/);
const normalize = accessJs.slice(accessJs.indexOf('function normalizeHiddenSettingsSnapshot('), accessJs.indexOf('function normalizeHiddenSettingsSnapshot(') + 2500);
assert.match(normalize, /span_w: '1',\s*span_h: '1'/);
assert.match(readRepoFile('src/web/admin/tiles/page-fields.js'), /kind: 'page-slot', folder: Number\(tile\.dataset\.navigateTarget \|\| 0\), settings,\s*(?:\/\/[^\n]*\n\s*)*layout: \{col: 0, row: 0, span_w: 1, span_h: 1\}/);
console.log('Settings field: parked without place or size, out as one cell where dropped or on a free cell');

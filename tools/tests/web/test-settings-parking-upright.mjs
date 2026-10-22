// The Settings field under an upright layout bigger than the classic grid
// (user 2026-10-22, Tab5: classic 7 x 4, head bar upright 3 x 6). Dragged out
// to row 3.5 it went, dragged back in "Speichern fehlgeschlagen": the parking
// sent the tile's upright place (row 3.5, high 1) and the panel checked it
// against the classic 4 rows; a drop below row 4 was refused the same way.
// The parking now sends no place (the panel keeps the tile's own per
// layout), and the drop spot is checked in the layout the panel shows.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const handlers = readRepoFile('src/web/server/handlers/web_admin_handlers.cpp').replace(/\r\n/g, '\n');
const access = handlers.slice(handlers.indexOf('if (server.hasArg("settings_access_present")) {'),
  handlers.indexOf('cfg.settings_tile_hidden = hide_tile;'));

// The drop spot: bounded by the shown layout's grid, the classic one only
// while the classic layout shows.
assert.match(access, /const bool classic_shown = grid_layout::active\(\) == grid_layout::Layout::kClassic;\s*const float place_cols = classic_shown \? GRID_COLS : grid_layout::shown\(\)\.cols;\s*const float place_rows = classic_shown \? GRID_ROWS : grid_layout::shown_rows\(\);/);
assert.match(access, /parse_bounded_arg\("settings_tile_target_col", 0, place_cols - 1,\s*settings_tile_target_col\) \|\|\s*!parse_bounded_arg\("settings_tile_target_row", 0, place_rows - 0\.5f,\s*settings_tile_target_row\)/);
// The parking's snapshot: no place is read or checked, its size within the
// larger of both grids.
assert.doesNotMatch(access, /settings_tile_col|settings_tile_row"/);
assert.doesNotMatch(access, /snapshot\.col = snapshot_col|snapshot\.row = snapshot_row/);
assert.match(access, /parse_bounded_arg\("settings_tile_span_w", 1, std::max<float>\(GRID_COLS, place_cols\),/);
assert.match(access, /parse_bounded_arg\("settings_tile_span_h", 0\.5f, std::max<float>\(GRID_ROWS, place_rows\),/);

// The numbers of the report: the upright spot passes now, the classic check
// refused it.
const tab5 = {classic: {cols: 7, rows: 4}, upright: {cols: 3, rows: 6}};
const dropOk = (grid, row) => row >= 0 && row <= grid.rows - 0.5;
assert.equal(dropOk(tab5.classic, 5), false, 'Classic refused a drop in row 5');
assert.equal(dropOk(tab5.upright, 5), true, 'Upright takes a drop in row 5');
assert.equal(3.5 + 1 > tab5.classic.rows, true, 'The old parking check refused row 3.5 high 1');

// The Web Admin sends the size only.
const accessJs = readRepoFile('src/web/admin/settings/access.js').replace(/\r\n/g, '\n');
const save = accessJs.slice(accessJs.indexOf('async function saveSettingsAccess('), accessJs.indexOf("const lockedBefore = pinToggle.dataset.pinConfigured === '1';"));
assert.doesNotMatch(save, /settings_tile_col'|settings_tile_row'/);
assert.match(save, /body\.set\('settings_tile_span_w', String\(layout\.span_w\)\);\s*body\.set\('settings_tile_span_h', String\(layout\.span_h\)\);/);

// The panel places it in the shown layout's cells there (a bar layout's
// target is checked in its own grid), and keeps its own place in each bar
// layout while it is parked.
const config = readRepoFile('src/tiles/config/tile_config.cpp').replace(/\r\n/g, '\n');
const ensure = config.slice(config.indexOf('bool TileConfig::ensureSettingsTile('), config.indexOf('bool TileConfig::ensureSettingsTile(') + 4000);
assert.match(ensure, /if \(explicit_target && bar_places\) \{\s*if \(target_col < 0 \|\| target_row < 0 \|\|\s*!settings_tile_rect_is_free_shown\(grid, target_col, target_row, span_w, span_h\)\)/);
assert.match(ensure, /tile_layouts::find\(grid_layout::active\(\), kRootFolderId, snapshot\.view_id, kept\)/);
console.log('Settings parking upright: no place in the parking, the drop spot checked in the shown layout');

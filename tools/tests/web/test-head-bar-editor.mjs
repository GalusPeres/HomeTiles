// Web Admin with the head bar layout (grid_layout.h): new places, moves and
// sizes stay inside the shown grid; a stored tile outside it is marked and
// never moved; Settings and Back give way to the head's gear and X; the
// screensaver tab keeps its stored grid. The page renders the head like the
// panel and offers the switch in the global settings.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const names = ['headBarLayout', 'placeCols', 'placeRows', 'insideShownGrid', 'hiddenByHeadBar', 'clampHalf',
  'setGridItemPosition', 'setTileGridPosition', 'markOccupied', 'slotFits', 'firstFreeSlot'];
const source = names.map(extractDeliveredFunction).join('\n');

function element(tab, type, empty = false) {
  const classes = new Set(empty ? ['tile', 'empty'] : ['tile']);
  const attributes = {};
  return {
    classes, attributes, dataset: {type: String(type)}, style: {setProperty() {}},
    classList: {
      contains: name => classes.has(name),
      toggle: (name, on) => { if (on) classes.add(name); else classes.delete(name); return on; },
    },
    closest: () => ({id: 'tab-tiles-' + tab}),
    set title(value) { attributes.title = value; },
    get title() { return attributes.title; },
    removeAttribute: name => { delete attributes[name]; },
  };
}

function context(headBar) {
  const ctx = vm.createContext({
    HEAD_BAR: headBar, GRID_COLS: 7, GRID_ROWS: 5, GRID_SHOWN_COLS: headBar ? 6 : 7, GRID_SHOWN_ROWS: headBar ? 4 : 5,
    currentTileTab: 'folder0', isScreensaverTileTab: tab => tab === 'screensaver',
    firstAllowedGridRow: tab => (tab === 'screensaver' ? 3 : 0), t: key => key,
  });
  vm.runInContext(source, ctx);
  return ctx;
}

// With the head bar: places inside 6 x 4; the screensaver keeps 7 x 5.
{
  const ctx = context(true);
  const run = code => vm.runInContext(code, ctx);
  assert.equal(run("placeCols('folder0')"), 6);
  assert.equal(run("placeRows('folder3')"), 4);
  assert.equal(run("placeCols('screensaver')"), 7);
  assert.equal(run("insideShownGrid('folder0', {col: 5, row: 3, span_w: 1, span_h: 1})"), true);
  assert.equal(run("insideShownGrid('folder0', {col: 5, row: 0, span_w: 2, span_h: 1})"), false);
  assert.equal(run("hiddenByHeadBar('folder0', 7) && hiddenByHeadBar('folder2', 8)"), true);
  assert.equal(run("hiddenByHeadBar('screensaver', 7) || hiddenByHeadBar('folder0', 5)"), false);
  // A free slot never lies outside the shown grid, even where the stored grid
  // has room (column 6, row 4).
  run("var occupied = Array.from({length: 10}, () => Array(14).fill(false));");
  assert.equal(run("slotFits('folder0', occupied, 6, 0, 1, 1)"), false);
  assert.equal(run("slotFits('folder0', occupied, 0, 4, 1, 1)"), false);
  assert.equal(run("slotFits('folder0', occupied, 5, 3, 1, 1)"), true);
  assert.equal(run("slotFits('screensaver', occupied, 6, 4, 1, 1)"), true);
  run("for (let r = 0; r < 8; r++) for (let c = 0; c < 12; c++) occupied[r][c] = true;");
  assert.equal(run("firstFreeSlot('folder0', occupied)"), null, 'the stored grid\'s last column is not offered');

  // A stored tile outside is drawn beside the screen and marked; its place stays.
  const outside = element('folder0', 5);
  ctx.el = outside;
  run('setTileGridPosition(el, 6, 1, 1, 1)');
  assert.ok(outside.classes.has('tile-outside') && outside.classes.has('fractional-tile'));
  assert.equal(outside.title, 'headBarTileOutside');
  assert.equal(outside.dataset.col, '6');
  run('setTileGridPosition(el, 2, 1, 1, 1)');
  assert.ok(!outside.classes.has('tile-outside') && !outside.classes.has('fractional-tile'));
  assert.equal(outside.title, undefined);
  const settings = element('folder0', 7);
  ctx.el = settings;
  run('setTileGridPosition(el, 0, 0, 1, 0.5)');
  assert.ok(settings.classes.has('tile-bar-hidden'));
}

// Without the head bar nothing changes: the stored grid is the shown grid.
{
  const ctx = context(false);
  const run = code => vm.runInContext(code, ctx);
  assert.equal(run("placeCols('folder0')"), 7);
  assert.equal(run("placeRows('folder0')"), 5);
  assert.equal(run("hiddenByHeadBar('folder0', 7)"), false);
  const tile = element('folder0', 5);
  ctx.el = tile;
  run('setTileGridPosition(el, 6, 4, 1, 1)');
  assert.ok(!tile.classes.has('tile-outside') && !tile.classes.has('tile-bar-hidden'));
}

// The server keeps reorders inside the shown grid (not the screensaver's).
const tiles = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tiles, /g_place_cols = screensaver_grid \? GRID_COLS : GRID_SHOWN_COLS;/);
assert.match(tiles, /target_col \+ span_w > g_place_cols \|\| target_row \+ span_h > g_place_rows\) return false;/);
assert.match(tiles, /for \(float row = first_row; row < g_place_rows; row \+= step\)/);

// The page: the head over the grid, outside and hidden tiles marked, the
// switch in the global settings, the shown grid's tracks.
const html = readRepoFile('src/web/server/render/web_admin_html.cpp');
assert.match(html, /if \(!screensaver_mode && grid_layout::head_bar\(\)\) \{/);
assert.match(html, /class=\\"head-bar-preview\\"/);
assert.match(html, /cssClass \+= " tile-outside";/);
assert.match(html, /cssClass \+= " tile-bar-hidden";/);
assert.match(html, /onchange=\\"saveHeadBar\(this\)\\"/);
const styles = readRepoFile('src/web/server/render/web_admin_styles.cpp');
assert.match(styles, /html \+= String\(GRID_SHOWN_COLS\);/);
assert.match(styles, /\.screensaver-tile-grid\{--grid-cols:/);
assert.match(styles, /const home_bar::Geometry head = home_bar::geometry\(\);/);
const scripts = readRepoFile('src/web/server/render/web_admin_scripts.cpp');
assert.match(scripts, /const GRID_SHOWN_COLS = /);
assert.match(scripts, /appendJsEntry\("headBarRestartConfirm", tr\.head_bar_restart_confirm\);/);

// The switch saves the setting only; the boot applies it (HomeTiles.ino).
const routes = readRepoFile('src/web/server/web_admin.cpp');
assert.match(routes, /"\/api\/display\/head-bar", HTTP_POST,\s*guarded\(withStorageHold\(\[this\]\(\) \{ this->handleSaveHeadBar\(\); \}\)\)\);/);
const handlers = readRepoFile('src/web/server/handlers/web_admin_handlers.cpp');
const headBarHandler = handlers.slice(handlers.indexOf('void WebAdminServer::handleSaveHeadBar()'));
assert.match(headBarHandler, /configManager\.saveHeadBar\(enabled\)/);
assert.match(headBarHandler, /\\"active\\":" \+ \(grid_layout::head_bar\(\) \? "true" : "false"\)/);
const boot = readRepoFile('HomeTiles.ino');
assert.match(boot, /grid_layout::apply\(configManager\.getConfig\(\)\.head_bar\);/);
const config = readRepoFile('src/core/config/config_manager.cpp');
assert.match(config, /config\.head_bar = prefs\.getBool\("head_bar", false\);/);

// Every language has the switch, the restart question and the hint.
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
for (const text of ['"Kopfleiste"', '"Head bar"', '"Barre d\'en-tête"', '"Pasek nagłówka"']) {
  assert.ok(i18n.includes(text), text);
}
console.log('Head bar editor keeps places in the shown grid, marks outside tiles and renders the head');

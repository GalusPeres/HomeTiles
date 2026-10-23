// "Layout bleibt Layout" (user 2026-10-21): the panel starts with the layout
// the user chose, whatever places it has. Until b509 TileConfig::load put a
// bar layout without places back to the classic one for that start: an
// emptied 8-inch (every folder and page deleted) came up black in the
// classic layout after an update, and switching an empty panel to the head
// bar never held (the choice was stored, the restart fell back). Every
// place in the firmware that applies or stores a layout is listed here, so
// a new automatic switch fails this test: only the boot (the stored
// choice), the user's switch (Web Admin, panel setup) and the new panel's
// own setup may change it.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = rel => fs.readFileSync(path.join(root, rel), 'utf8').replace(/\r\n/g, '\n');

// Every firmware source with its comments and strings masked.
const sources = [];
const walk = dir => {
  for (const entry of fs.readdirSync(path.join(root, dir), {withFileTypes: true})) {
    const rel = path.posix.join(dir, entry.name);
    if (entry.isDirectory()) {
      if (!['src/web/assets', 'src/web/generated'].includes(rel)) walk(rel);
    } else if (/\.(cpp|h|ino)$/.test(entry.name)) {
      sources.push({rel, text: maskCpp(read(rel))});
    }
  }
};
walk('src');
sources.push({rel: 'HomeTiles.ino', text: maskCpp(read('HomeTiles.ino'))});

// Where a pattern is used, by file and enclosing function.
const uses = pattern => sources.flatMap(({rel, text}) => {
  const functions = cppFunctionDefinitions(text);
  return [...text.matchAll(pattern)].map(match => {
    const inside = functions.filter(f => f.start <= match.index && match.index < f.end).at(-1);
    return `${rel}:${inside ? inside.name : '(top level)'}`;
  });
}).sort();

// The shown layout of a start: only the stored choice at boot and the new
// panel right after it stored its layout.
assert.deepEqual(uses(/grid_layout::apply\(/g), [
  'HomeTiles.ino:setup',
  'src/tiles/config/new_panel_tiles.cpp:seed'
].sort(), 'only the boot and the new panel apply a layout');
assert.match(read('HomeTiles.ino'), /grid_layout::apply\(configManager\.getConfig\(\)\.layout\);/,
  'the boot applies the stored choice');

// The stored choice: the user's switch and the new panel's setup.
assert.deepEqual(uses(/\.saveLayout\(/g), [
  'src/tiles/config/new_panel_tiles.cpp:seed',
  'src/ui/tabs/settings/tab_settings.cpp:layout_selected',
  'src/web/server/handlers/web_admin_tiles.cpp:WebAdminServer::handleSwitchLayout'
].sort(), 'only the user and the new panel store a layout');

// The shown grid itself: its definition, grid_layout::apply, and the Web
// Admin's style variables per layout, which put the shown grid back.
assert.deepEqual(uses(/\bg_shown\s*=[^=]/g), [
  'src/tiles/config/grid_layout.h:(top level)',
  'src/tiles/config/grid_layout.h:apply',
  'src/web/server/render/web_admin_styles.cpp:appendAdminStyles',
  'src/web/server/render/web_admin_styles.cpp:appendAdminStyles'
].sort(), 'nothing else sets the shown grid');
assert.match(read('src/web/server/render/web_admin_styles.cpp'),
  /const grid_layout::Shown shown = grid_layout::g_shown;[\s\S]*?grid_layout::g_shown = shown;/,
  'the style variables restore the shown grid');

// Loading the tiles reads the places and keeps the layout.
const config = read('src/tiles/config/tile_config.cpp');
const loads = cppFunctionDefinitions(config).filter(f => f.name === 'TileConfig::load');
assert.ok(loads.some(f => f.body.includes('tile_layouts::begin();')), 'TileConfig::load reads the places');
for (const load of loads) {
  assert.doesNotMatch(maskCpp(load.body), /grid_layout::|apply\(/, 'loading never switches the layout');
}

console.log('ok - the panel keeps the chosen layout; every layout switch is listed');

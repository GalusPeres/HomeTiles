// The screensaver in the layouts (user 2026-10-08): it takes the active
// layout's grid without the head, two rows of slots at the bottom, with own
// places per layout in the layout file (screensaver_places.h, keyed by slot
// + 1). The classic places stay in the screensaver grid; the export keeps
// them; the Web Admin's screensaver tab edits the active layout's places.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const places = read('src/ui/screensaver/screensaver_places.cpp');
const header = read('src/ui/screensaver/screensaver_places.h');
assert.match(header, /constexpr uint16_t kFolder = TileConfig::kScreensaverGridStorageId;/);
assert.match(header, /inline uint16_t key\(size_t index\) \{ return static_cast<uint16_t>\(index \+ 1\); \}/);
// Two rows at the bottom of the layout's grid.
assert.match(places, /return rows > 2 \? rows - 2 : 0;/);
// A layout without places yet: the classic rows moved onto its bottom rows.
assert.match(places, /out\.row -= rows_of\(grid_layout::profile_grid\(\)\) - rows_of\(grid\);/);
assert.match(places, /return out\.row >= first_row\(layout\) - 0\.001f &&\s*grid_layout::inside\(grid, out\.col, out\.row, out\.span_w, out\.span_h\);/);
// A new tile made in a bar layout: the first free classic cell, else none.
assert.match(places, /tile_layouts::set_classic_hidden\(kFolder, key\(index\), !free\);/);
// An emptied slot forgets every layout's place.
assert.match(places, /tile_layouts::remove\(grid_layout::from_index\(i\), kFolder, key\(index\)\);/);

// The panel draws the active layout's places, a tile without one is left out.
const screen = read('src/ui/screensaver/image_screensaver.cpp');
assert.match(screen, /screensaver_places::overlay\(\*st->shown_grid\);/);
assert.match(screen, /if \(tile\.type == TILE_EMPTY \|\| tile\.layout_hidden\) continue;\s*build_slot_tile\(st, i, tile\);/);
assert.match(screen, /screensaver_places::overlay\(\*next_storage\);/);
assert.ok(!screen.includes('row_shift'), 'no more shifted classic rows');

// The Web Admin: GET shows the active layout's places, POST and reorder
// store them in the layout file; the classic request (export/import) keeps
// the classic ones.
const handler = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(handler, /if \(server\.arg\("layout"\) != "classic"\) screensaver_places::overlay\(grid\);/);
assert.match(handler, /screensaver_places::take\(\*grid, \*classic_grid, static_cast<size_t>\(index\)\);/);
assert.match(handler, /if \(screensaver_layout\) screensaver_places::take_places\(grid\);/);
assert.match(handler, /bool success = screensaver_layout \? tile_layouts::commit\(\)/);
assert.match(read('src/web/admin/tiles/import-export.js'),
  /'\/api\/tiles\?folder=' \+ encodeURIComponent\(SCREENSAVER_FOLDER_ID\) \+ '&layout=classic'/);

// The screensaver tab: the shown grid, its two rows at the bottom.
const layout = read('src/web/admin/tiles/layout.js');
assert.match(layout, /return headBarLayout\(\) && typeof GRID_SHOWN_COLS === 'number' \? GRID_SHOWN_COLS : GRID_COLS;/);
const navigation = read('src/web/admin/folders/navigation.js');
assert.ok(navigation.includes("const rows = typeof layoutWindow !== 'undefined' && layoutWindow ? LAYOUTS[layoutWindow.key].rows : placeRows(tab);") &&
  navigation.includes('return Math.max(0, rows - 2);'), 'two rows at the bottom, in the window of that layout');
assert.ok(!read('src/web/server/render/web_admin_styles.cpp').includes('.screensaver-tile-grid{--grid-cols:'),
  'no more profile grid for the screensaver tab');

// The layout window: a screensaver tab after the pages, without picture and
// clock; its tiles keep to the bottom rows when taken over; the server loads
// and saves it like a folder with its slot keys as IDs.
const windowSource = read('src/web/admin/tiles/layout-window.js');
assert.ok(windowSource.includes('return [...tabs.filter(tab => !isScreensaverTileTab(tab)), ...tabs.filter(isScreensaverTileTab)];'));
assert.ok(windowSource.includes('const shift = isScreensaverTileTab(tab) ? L.rows - LAYOUTS[from].rows : 0;'));
assert.ok(read('src/web/assets/admin.css').includes('.tile-grid.setup-grid.screensaver-tile-grid > :not(.tile)'));
assert.ok(handler.includes('grid.tiles[i].view_id = grid.tiles[i].type == TILE_EMPTY ? 0 : screensaver_places::key(i);'));
assert.ok(handler.includes('if (folder_id != screensaver_places::kFolder) return tileConfig.loadFolderGridClassic(folder_id, *grid);'));
assert.ok(handler.includes('return place.inside && place.place.row < screensaver_places::first_row(layout) - 0.001f;'));
assert.ok(handler.includes('ok = screensaverConfig.replaceTileGrid(*grid) && ok;'));

console.log('Screensaver: the active layout\'s grid, two rows at the bottom, own places per layout');

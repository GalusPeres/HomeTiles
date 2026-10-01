// Moving, resizing or restyling a tile in the Web Admin felt slow on the
// panel: the change only appeared after the flash write, then waited for a
// quiet Web Admin, and every other prepared folder was rebuilt as well. The
// panel now shows the active folder's change before saving (or right after it
// for a new tile type), and only the changed folder's hidden cache is dropped.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, `${name} is missing`);
  return found.source;
};

const handler = fn(read('src/web/server/handlers/web_admin_tiles.cpp'), 'WebAdminServer::handleReorderTiles');
const show = handler.indexOf('tileConfig.previewActiveFolderGrid(folder_id, grid) &&\n                         tiles_show_active_layout_now()');
const save = handler.indexOf('tileConfig.saveFolderGrid(folder_id, grid)');
assert.ok(show > 0 && save > show, 'the new order is shown before the flash write');
assert.match(handler, /!screensaver_grid && !powerManager\.isInSleep\(\) &&/, 'no render while the display sleeps');
assert.match(handler, /tiles_invalidate_folder_only\(folder_id\);\n      if \(!shown_now && tileConfig\.getActiveFolderId\(\) == folder_id\) \{\n        tiles_request_reload_if_loaded\(GridType::TAB0\);/);
assert.doesNotMatch(handler, /tiles_invalidate_folder\(folder_id\)/, 'a reorder no longer drops every folder cache');
assert.match(handler, /if \(shown_now && tileConfig\.setActiveFolder\(folder_id\)\) \{\n      \/\/ Back to the stored order the failed save left\.\n      tiles_request_reload\(GridType::TAB0\);/);

const saveTiles = fn(read('src/web/server/handlers/web_admin_tiles.cpp'), 'WebAdminServer::handleSaveTiles');
const preview = saveTiles.indexOf('tileConfig.previewActiveFolderGrid(folder_id, *grid) &&\n      tiles_show_active_layout_now();');
assert.ok(preview > 0 && saveTiles.indexOf('tileConfig.saveFolderGrid(folder_id, *grid)') > preview,
  'a tile edit is shown before the flash write');
assert.match(saveTiles, /!screensaver_grid && !deleting_folder && display_awake &&\n      previous_tile\.type == tile\.type &&/,
  'only edits that keep the tile type are shown before saving (new tiles get their view ID first)');
assert.match(saveTiles, /if \(deleting_folder\) \{\n        tiles_invalidate_folder\(folder_id\);\n      \} else \{\n[^\n]*\n        tiles_invalidate_folder_only\(folder_id\);/);
assert.match(saveTiles, /if \(!shown_before_save && tileConfig\.getActiveFolderId\(\) == folder_id &&\n          !\(display_awake && tiles_show_active_layout_now\(\)\)\) \{\n        tiles_request_reload_if_loaded\(GridType::TAB0\);/,
  'other edits are shown right after the save, not after a quiet Web Admin');
assert.match(saveTiles, /if \(shown_before_save && tileConfig\.setActiveFolder\(folder_id\)\) \{\n      \/\/ Back to the stored tile the failed save left\.\n      tiles_request_reload\(GridType::TAB0\);/);

const config = read('src/tiles/config/tile_config.cpp');
assert.match(fn(config, 'TileConfig::previewActiveFolderGrid'),
  /if \(folder_id != active_folder_id \|\| !folderExists\(folder_id\)\) return false;\n  adoptActiveGrid\(folder_id, grid\);/);
assert.match(fn(config, 'TileConfig::saveFolderGrid'), /if \(ok && folder_id == active_folder_id\) \{[\s\S]*adoptActiveGrid\(folder_id, grid\);/,
  'preview and save keep the same normalized active grid');

const tiles = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
assert.match(fn(tiles, 'tiles_show_active_layout_now'),
  /g_active_cache->folder_id != tileConfig\.getActiveFolderId\(\)\) \{\n    return false;\n  \}\n  tiles_reload_layout\(GridType::TAB0\);\n  g_tiles_reload_requested\[idx\] = false;/);
const invalidation = fn(tiles, 'process_folder_cache_invalidation');
assert.match(invalidation, /if \(&entry == g_active_cache \|\|\n            entry\.folder_id != g_folder_only_invalidations\[n\]\) \{\n          continue;\n        \}\n        reset_cache_entry\(entry\);/,
  'only the changed folder\'s hidden cache is dropped');
assert.match(fn(tiles, 'tiles_invalidate_folder_only'),
  /if \(g_folder_only_invalidation_count >= kMaxFolderOnlyInvalidations\) \{\n    g_folder_cache_invalidate_requested = true;/, 'overflow falls back to every cache');

console.log('Web Admin moves and tile edits: shown before/after the save, only the changed folder cache dropped, failed save restored.');

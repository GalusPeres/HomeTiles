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
const show = handler.indexOf('tileConfig.previewActiveFolderGrid(folder_id, grid) &&\n                              tiles_show_active_layout_now())');
const save = handler.indexOf('tileConfig.saveFolderGrid(folder_id, grid)');
assert.ok(show > 0 && save > show, 'the new order is shown before the flash write');
assert.match(handler, /const bool shown_now =\n      !powerManager\.isInSleep\(\) &&/, 'no render while the display sleeps');
assert.match(handler, /tiles_invalidate_folder_only\(folder_id\);\n      if \(!shown_now && tileConfig\.getActiveFolderId\(\) == folder_id\) \{\n        tiles_request_reload_if_loaded\(GridType::TAB0\);/);
assert.doesNotMatch(handler, /tiles_invalidate_folder\(folder_id\)/, 'a reorder no longer drops every folder cache');
assert.match(handler, /if \(shown_now && tileConfig\.setActiveFolder\(folder_id\)\) \{\n      \/\/ Back to the stored order the failed save left\.\n      tiles_request_reload\(GridType::TAB0\);/);

const saveTiles = fn(read('src/web/server/handlers/web_admin_tiles.cpp'), 'WebAdminServer::handleSaveTiles');
const preview = saveTiles.indexOf('tileConfig.previewActiveFolderGrid(folder_id, *grid) &&\n                tiles_show_active_layout_now();');
assert.ok(preview > 0 && saveTiles.indexOf('tileConfig.saveFolderGrid(folder_id, *grid)') > preview,
  'a tile edit is shown before the flash write');
assert.match(saveTiles, /: !deleting_folder && display_awake &&\n                previous_tile\.type == tile\.type &&/,
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
  // Only changed tiles are rebuilt, moved ones take their cells
  // (test-tile-move-fast-path.mjs); an unexpected grid rebuilds as before.
  /g_active_cache->folder_id != tileConfig\.getActiveFolderId\(\)\) \{\n    return false;\n  \}\n  if \(!update_active_layout\(\)\) tiles_reload_layout\(GridType::TAB0\);\n  g_tiles_reload_requested\[idx\] = false;/);
const invalidation = fn(tiles, 'process_folder_cache_invalidation');
assert.match(invalidation, /if \(&entry == g_active_cache \|\|\n            entry\.folder_id != g_folder_only_invalidations\[n\]\) \{\n          continue;\n        \}\n        reset_cache_entry\(entry\);/,
  'only the changed folder\'s hidden cache is dropped');
assert.match(fn(tiles, 'tiles_invalidate_folder_only'),
  /if \(g_folder_only_invalidation_count >= kMaxFolderOnlyInvalidations\) \{\n    g_folder_cache_invalidate_requested = true;/, 'overflow falls back to every cache');

// The screensaver does the same (user 2026-10-02: changing its tiles was
// slow): an edit or move is shown before the flash write, only changed slots
// are rebuilt, and a failed save goes back to the stored grid.
const handlers = read('src/web/server/handlers/web_admin_tiles.cpp');
for (const [name, gridArg, flag] of [['WebAdminServer::handleReorderTiles', 'grid', 'shown_now'],
                                     ['WebAdminServer::handleSaveTiles', '*grid', 'shown_before_save']]) {
  const body = fn(handlers, name);
  const shown = body.indexOf('showScreensaverGridBeforeSave(' + gridArg + ')');
  assert.ok(shown > 0 && body.indexOf('screensaverConfig.replaceTileGrid(' + gridArg + ')') > shown, name + ': screensaver shown before the save');
  assert.ok(body.includes('} else if (!' + flag + ') {\n      image_screensaver_tiles_changed();'), name + ': no second rebuild after an early show');
  assert.ok(body.includes('if (screensaver_grid) {\n      if (' + flag + ') restoreScreensaverGridAfterFailedSave();'), name + ': failed save restores the screensaver');
}
assert.match(fn(handlers, 'showScreensaverGridBeforeSave'),
  /if \(!is_image_screensaver_visible\(\)\) return false;\n  screensaverConfig\.previewTileGrid\(grid\);\n  return image_screensaver_show_tiles_now\(\);/);
assert.match(fn(handlers, 'restoreScreensaverGridAfterFailedSave'),
  /tileConfig\.loadScreensaverGrid\(\*stored\)\) \{\n    screensaverConfig\.previewTileGrid\(\*stored\);\n  \}\n  delete stored;\n  image_screensaver_tiles_changed\(\);/);
assert.match(fn(read('src/ui/screensaver/screensaver_config.cpp'), 'ScreensaverConfigStore::previewTileGrid'),
  /storage = grid;\n  normalizeTileGrid\(storage\);/, 'preview and save keep the same normalized grid');

const screensaver = read('src/ui/screensaver/image_screensaver.cpp');
const update = fn(screensaver, 'update_slot_grid');
assert.match(update, /if \(after\.type != TILE_EMPTY && st->slot_objs\[i\] &&\n        tileContentEquals\(before, after\) && before\.col == after\.col &&\n        before\.row == after\.row\) \{\n      \+\+position;\n      continue;/,
  'unchanged slots stay');
for (const reset of ['sensor', 'switch', 'cover', 'binary_sensor', 'media']) {
  assert.ok(update.includes('reset_' + reset + '_widget(GridType::SCREENSAVER, slot);'), 'a rebuilt slot drops its ' + reset + ' widgets');
}
assert.match(update, /lv_obj_move_to_index\(card, static_cast<int32_t>\(position\+\+\)\);/, 'rebuilt cards keep the slot order');
assert.match(update, /if \(cards != lv_obj_get_child_count\(st->slot_grid\)\) return false;/, 'an unexpected grid rebuilds');
assert.match(fn(screensaver, 'rebuild_slot_grid'), /apply_slot_tile_borders\(st\);\n  remember_shown_grid\(st\);/);
assert.match(fn(screensaver, 'image_screensaver_show_tiles_now'),
  /if \(!update_slot_grid\(st\)\) \{\n    rebuild_slot_grid\(st\);\n    refresh_slot_values\(st\);\n  \}[\s\S]*lv_refr_now\(display\);/);
// Default color and icon glow restyle every card: they still rebuild all.
assert.match(fn(screensaver, 'global_screensaver_timer_cb'),
  /if \(g_live_grid_refresh_requested\) \{\n    g_live_grid_refresh_requested = false;\n    rebuild_slot_grid\(st\);/);

console.log('Web Admin moves and tile edits: shown before/after the save, only the changed folder cache dropped, failed save restored; the screensaver shows edits before the save and rebuilds only changed slots.');

// A page turn redraws the whole grid in full-width bands. b587 split it into
// one area per tile column so each tile took fewer, taller bands (user
// 2026-10-28, V2 log): the tiles drew faster (Home 91 -> 56-79 ms) but the
// V2's turned flush of the narrow columns took 82-89 ms instead of 22 ms, the
// frame got slower (137 -> 167-202 ms) and the page visibly came up in
// blocks from the middle ("einfach nur furchtbar"). Rejected; this keeps both
// switch paths on the whole-grid redraw.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const tiles = readRepoFile('src/ui/tabs/tiles/tab_tiles_unified.cpp').replace(/\r\n?/g, '\n');

// The cached turn times its steps in between (switch_step, diagnostics only);
// a flow on the page hears it is shown (power_flow_shown, b610).
assert.equal((tiles.match(/lv_obj_clear_flag\(target->grid, LV_OBJ_FLAG_HIDDEN\);\s*(?:power_flow_shown\(target->grid\);\s*)?(?:switch_step\("\w+"\);\s*)?home_bar::shown\(target->grid, folder_id, previous \? previous->folder_id : kInvalidFolderId\);\s*(?:switch_step\("\w+"\);\s*)?(?:\/\/[^\n]*\n\s*)*lv_obj_invalidate\(target->grid\);/g) || []).length, 2,
  'cached and built page turns mark the whole grid');
assert.doesNotMatch(tiles, /show_page_grid|by_columns|lv_inv_area\(/, 'no column areas on a page turn');
console.log('Page turn: whole grid in full-width bands (columns rejected, b587)');

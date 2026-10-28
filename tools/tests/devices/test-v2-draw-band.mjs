// Every board keeps the 72 KB SRAM draw band. The V2 with 100 KB (b588/b589,
// 40 rows) drew pages faster, but its PPA rotation hung twice on a 614x71
// piece of a widened weather tile drawn alone at boot (crash log
// "LVGL PPA rotation ... 648,545 614x71", blue screen and restart, user
// 2026-10-28). With 72 KB the pieces of a 614 px wide area stay at most 58
// rows, the size the V2 ran with for months.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const display = readRepoFile('src/core/display/display_manager.cpp').replace(/\r\n?/g, '\n');
assert.match(display, /\nstatic constexpr size_t kInternalDrawMaxBytes\s+= 72 \* 1024;/, 'one 72 KB cap');
assert.equal((display.match(/kInternalDrawMaxBytes\s+=/g) || []).length, 1, 'no board gets another cap');
assert.doesNotMatch(display, /kInternalDrawMaxBytes\s+= 100 \* 1024/, 'the 100 KB V2 band stays out');
assert.match(display, /nb1 = \(lv_color_t\*\)heap_caps_aligned_alloc\(64, draw_buffer_bytes\(sram_lines\),\s*MALLOC_CAP_INTERNAL \| MALLOC_CAP_DMA\);/,
  'the band is internal SRAM');

// The cap gives 28 whole rows of 1280 px; LVGL fills that buffer.
const buffer = Math.floor(72 * 1024 / (1280 * 2)) * 1280 * 2;
const rows = width => Math.floor(buffer / (width * 2));
assert.equal(rows(1280), 28, 'full width: 28 rows');
assert.equal(rows(614), 58, 'the wide weather tile: at most 58 rows per piece');
console.log('Draw band: 72 KB on every board (V2 100 KB hung the PPA)');

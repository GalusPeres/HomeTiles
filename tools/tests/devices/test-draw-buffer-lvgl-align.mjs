// LVGL 9.6 rounds the size a draw band needs up to LV_DRAW_BUF_ALIGN
// (lv_draw_buf_reshape -> _calculate_draw_buf_size), while the refresh takes
// as many rows as data_size / stride (lv_refr.c get_max_row). A band buffer
// of exactly `lines` rows is then too small whenever a row is not a multiple
// of the alignment: the Waveshare 4B (720 px RGB565 = 1440 bytes, 51 rows =
// 73,440 bytes, LVGL wanted 73,472) failed LV_ASSERT_NULL in
// layer_reshape_draw_buf on its first refresh and the boot hung silently
// (b132-b164; v0.7.0 with LVGL 9.5 booted). Draw buffers are allocated and
// reported to LVGL rounded up (display_manager.cpp draw_buffer_bytes).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const manager = read('src/core/display/display_manager.cpp');
const conf = read('lv_conf.h');

const align = Number(conf.match(/^#define LV_DRAW_BUF_ALIGN (\d+)/m)[1]);
assert.equal(align, 64);
assert.match(manager,
  /static size_t draw_buffer_bytes\(size_t lines\) \{\s*const size_t bytes = static_cast<size_t>\(SCREEN_WIDTH\) \* lines \* g_bytes_per_pixel;\s*return \(bytes \+ LV_DRAW_BUF_ALIGN - 1\) \/ LV_DRAW_BUF_ALIGN \* LV_DRAW_BUF_ALIGN;\s*\}/);

// Every buffer LVGL draws into is sized by draw_buffer_bytes: no raw
// width * lines * bytes per pixel product reaches lv_display_set_buffers or
// the band allocations any more.
const setBuffers = [...manager.matchAll(/lv_display_set_buffers\(disp, [^,]+, [^,]+,\s*(\w+)/g)].map(m => m[1]);
assert.deepEqual(setBuffers, ['buf_bytes', 'bytes', 'restored_bytes', 'bytes']);
for (const [name, source] of [
  ['allocDrawBuffers SRAM band', 'heap_caps_aligned_alloc(64, draw_buffer_bytes(sram_lines),'],
  ['allocDrawBuffers PSRAM bands', 'const size_t bytes = draw_buffer_bytes(requested_lines);'],
  ['allocDrawBuffers reported size', 'const size_t buf_bytes = draw_buffer_bytes(use_lines);'],
  ['camera band', 'const size_t bytes = draw_buffer_bytes(lines);'],
  ['camera restore', 'const size_t restored_bytes = draw_buffer_bytes(g_preserved_buffer_lines);'],
  ['render mode change', 'const size_t bytes = draw_buffer_bytes(g_buffer_lines);'],
]) assert.ok(manager.includes(source), name);
assert.doesNotMatch(manager, /SCREEN_WIDTH\)?\s*\*\s*\w*lines\w*\s*\*\s*g_bytes_per_pixel;\s*\n\s*lv_display_set_buffers/);

// LVGL 9.6's arithmetic for every panel width and band height: with the
// rounded size every band (and any narrower dirty area) fits; the old exact
// size failed on the 4B.
const roundUp = n => Math.ceil(n / align) * align;
const reshapeFits = (dataSize, areaWidth) => {
  const stride = areaWidth * 2;  // RGB565, LV_DRAW_BUF_STRIDE_ALIGN 1
  const rows = Math.floor(dataSize / stride);
  return rows > 0 && roundUp(stride * rows) <= dataSize;
};
assert.ok(!reshapeFits(720 * 2 * 51, 720), 'the 4B band of b132-b164 was 32 bytes short');
for (const width of [480, 720, 800, 1024, 1280]) {
  for (let lines = 1; lines <= 120; ++lines) {
    const dataSize = roundUp(width * 2 * lines);
    for (const areaWidth of [width, 1, 7, 100, 333, Math.floor(width / 2)]) {
      assert.ok(reshapeFits(dataSize, areaWidth), `${width} px, ${lines} lines, area ${areaWidth} px`);
    }
  }
}
console.log('Draw buffers: sized to LVGL 9.6 band rounding (Waveshare 4B boot hang)');

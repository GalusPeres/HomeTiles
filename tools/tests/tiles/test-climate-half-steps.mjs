// Climate mini tiles follow half steps in height: the mini-grid has one row
// per half cell below the header row (1 -> 1, 1.5 -> 2, 2 -> 3, 2.5 -> 4),
// on the device and in the Web preview, while whole sizes keep their rows.
// The mini-grid starts below the corner header disc, which is larger on
// taller cells (Tab5, 4B, S3) and used to reach into the first mini tile.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const code = source => source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(definition => definition.name === name);
  assert(found, name);
  return found.source;
};

// ---- Device: rows follow half steps ---------------------------------------
const config = read('src/tiles/config/tile_config.h');
const rowsFn = code(fn(config, 'climateTileGridRows'));
assert.match(rowsFn, /const float span_h =/, 'The row count reads the fractional height');
assert.match(rowsFn, /static_cast<uint8_t>\(span_h \* 2\.0f \+ 0\.5f\) - 1u/);
// The same arithmetic as the device, for the supported half steps.
const deviceRows = spanH => Math.floor(Math.min(Math.max(spanH, 1), 5) * 2 + 0.5) - 1;
assert.deepEqual([1, 1.5, 2, 2.5, 3, 5].map(deviceRows), [1, 2, 3, 4, 5, 9]);

const renderer = read('src/types/climate/renderer.cpp');
const automatic = code(fn(renderer, 'build_automatic_slot_kinds'));
assert.match(automatic, /const uint8_t span_w = climateTileGridColumns\(tile\);\s*const uint8_t rows = climateTileGridRows\(tile\);/);
assert.doesNotMatch(automatic, /span_h/, 'Automatic content follows mini-grid rows, not whole cells');
assert.match(automatic, /if \(span_w == 1 && rows == 1\)/);
assert.match(automatic, /if \(span_w >= 2 && rows == 1\)/);
assert.match(automatic, /if \(rows <= 3\) return count;/);

// ---- Device and preview: the mini-grid starts below the header disc -------
const layout = code(read('src/types/climate/layout.h'));
assert.match(layout, /inline constexpr int content_top\(int header_disc, int disc_inset\) \{\s*return disc_inset \* 2 \+ header_disc > kContentTop\s*\? disc_inset \* 2 \+ header_disc\s*: kContentTop;/);
const slots = code(fn(renderer, 'layout_climate_slots'));
assert.match(slots, /climate_layout::content_top\(tile_icon_disc::header_diameter\(icon_width\),\s*tile_icon_disc::inset\(\)\) -\s*climate_layout::kCardPaddingVertical/);
assert.match(slots, /lv_font_get_glyph_width\(FONT_MDI_ICONS,\s*tile_icon_disc::kMdiReferenceGlyph, 0\)/,
  'The device uses the same icon width as the preview header');
const styles = read('src/web/server/render/web_admin_styles.cpp');
assert.ok(styles.includes('emit_exact("climate-slots-top",\n             climate_layout::content_top(header.disc, tile_icon_disc::inset()));'),
  'The preview mini-grid starts where the device mini-grid starts');
// Header disc sizes per cell (test-weather-opening-lvgl.mjs renders them):
// 8/10-inch 64 px keeps the tuned top plus the disc gap, Tab5/4B 67 px and
// S3 44 px (inset 3) move below their disc, 1024x600 keeps its top.
const contentTop = (disc, inset, tuned) => Math.max(inset * 2 + disc, tuned);
assert.equal(contentTop(64, 4, 69), 72);
assert.equal(contentTop(67, 4, 69), 75);
assert.equal(contentTop(44, 3, 46), 50);
assert.equal(contentTop(56, 4, 69), 69);

// ---- Web Admin: the same rows and automatic content -----------------------
const geometry = read('src/types/climate/admin-geometry.js');
const jsFunction = name => {
  const start = geometry.indexOf(`function ${name}(`);
  assert.ok(start >= 0, name);
  let depth = 0;
  for (let i = geometry.indexOf('{', start); i < geometry.length; ++i) {
    if (geometry[i] === '{') ++depth;
    else if (geometry[i] === '}' && --depth === 0) return geometry.slice(start, i + 1);
  }
  throw new Error(name);
};
const sandbox = {GRID_COLS: 4, GRID_ROWS: 3};
vm.runInNewContext(['climateMaxGridColumns', 'climateMaxOuterRows', 'climateGridDimensions', 'climateSlotCapacity']
  .map(jsFunction).join('\n') + '\nthis.dims = climateGridDimensions; this.capacity = climateSlotCapacity;', sandbox);
for (const [spanW, spanH, columns, rows] of [[1, 1, 1, 1], [1, 1.5, 1, 2], [1, 2, 1, 3], [1.5, 2.5, 1, 4],
  [2, 1, 2, 1], [2.5, 1.5, 2, 2], [3, 3, 3, 5], [4, 9, 4, 5], ['1.5', '1.5', 1, 2]]) {
  assert.deepEqual({...sandbox.dims(spanW, spanH)}, {columns, rows}, `${spanW}x${spanH}`);
}
assert.equal(sandbox.capacity(1, 1.5), 2, 'A 1x1.5 tile holds two mini tiles');
assert.equal(sandbox.capacity(2, 1.5), 4);

const preview = code(read('src/types/climate/admin-preview.js'));
assert.match(preview, /const h = Math\.max\(1, Math\.round\(Number\(spanH\) \* 2 \|\| 2\) \/ 2\);/,
  'The preview keeps half heights');
assert.match(preview, /\} else if \(w === 1 && rows === 1\) \{/);
assert.match(preview, /\} else if \(w >= 2 && rows === 1\) \{/);
assert.match(preview, /if \(rows > 3 &&\s*state\.targetHumidity !== null/);
const content = code(read('src/types/climate/admin-content.js'));
assert.match(content, /const \{ rows \} = climateGridDimensions\(spanW, spanH\);/);
assert.match(content, /if \(spanW === 1 && rows === 1\) \{/);
assert.match(content, /if \(rows <= 3\) return kinds;/);
const editor = code(read('src/types/climate/admin-editor.js'));
assert.match(editor, /const spanH = Math\.max\(1, Math\.round\(Number\(document\.getElementById\(\s*tab \+ '_tile_span_h'\)\?\.value\) \* 2 \|\| 2\) \/ 2\);/);
for (const [name, source] of [['admin-preview', preview], ['admin-content', content], ['admin-editor', editor]])
  assert.doesNotMatch(source, /Math\.floor\(Number\([^)]*span_?h/i, `${name} must not floor the height`);

console.log('Climate half steps: mini-grid rows, automatic content, header disc gap and preview parity passed.');

// Camera popup frame size on every P4 layout (issue #63). The ESP32-P4 JPEG
// decoder rejects frames whose width * height is not divisible by 8 ("Picture
// sizes not divisible by 8 are not supported", IDF jpeg_parse_marker.c): the
// 1024x600 layout asked for 558x314 and every camera failed with "JPEG
// decoder error". The Bridge accepts only even 16:9 sizes within 16 of
// w*9 == h*16 (camera_stream.py _validate_stream_request).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (relativePath) => fs.readFileSync(path.join(repoRoot, relativePath), 'utf8');

const geometry = read('src/video/camera_geometry.h');
const layout = read('src/ui/popups/popup_layout.h');

// The stream keeps the sizes the Bridge was tested with: the content width
// of the card with its former 3 or 4 px margin (the card reaches the screen
// edge since 2026-10-08), so frame() below still uses that margin.
assert.match(geometry, /kWidth =\s*static_cast<uint16_t>\(\(popup_layout::kContentWidth - 2 \* kFormerCardMargin\) & ~7\);/);
assert.match(geometry, /#if defined\(DEVICE_LAYOUT_480X480\)\s*inline constexpr int kFormerCardMargin = 3;\s*#else\s*inline constexpr int kFormerCardMargin = 4;/);
assert.match(geometry, /kHeight = evenRound\(\s*static_cast<uint32_t>\(kWidth\) \* 9U, 16U\);/);
assert.match(geometry, /\(numerator \+ denominator\) \/ \(2U \* denominator\) \* 2U/);
assert.match(geometry, /% 8U == 0U,\s*"The P4 JPEG decoder needs width \* height divisible by 8"/);
// The popup formulas this test mirrors.
assert.match(layout, /kCardWidth =\s*\(SCREEN_WIDTH > SCREEN_HEIGHT\)\s*\? \(SCREEN_HEIGHT - \(kCardMargin \* 2\)\)\s*: \(SCREEN_WIDTH - \(kCardMargin \* 2\)\);/);
assert.match(layout, /kContentWidth = kCardWidth - \(kCardPad \* 2\);/);
assert.match(layout, /constexpr int kHeadCardPad = scale\(20\);[\s\S]*constexpr int kCardPad = kHeadCardPad;/);

const scale = {
  default: (value) => value,
  '1024x600': (value) => Math.trunc((value * 5 + 3) / 6),
  '480x480': (value) => Math.trunc((value * 2 + 1) / 3),
};

function frame(width, height, layoutName) {
  const margin = layoutName === '480x480' ? 3 : 4;
  const card = Math.min(width, height) - margin * 2;
  const content = card - scale[layoutName](20) * 2;
  const frameWidth = content & ~7;
  const frameHeight = Math.trunc((frameWidth * 9 + 16) / 32) * 2;
  return { content, frameWidth, frameHeight };
}

const profiles = [
  // [devices, logical screen, popup layout, expected frame]
  ['Waveshare 8-inch / 10.1-inch, Guition JC8012P4A1 V1/V2', 1280, 800, 'default', [752, 424]],
  ['M5Stack Tab5, Waveshare 7-inch', 1280, 720, 'default', [672, 378]],
  ['Guition JC1060P470C V1/V2, Waveshare 7B', 1024, 600, '1024x600', [552, 310]],
  ['Waveshare 4.3-inch', 800, 480, '480x480', [448, 252]],
  ['Guition JC4880P443 portrait', 480, 800, '480x480', [448, 252]],
];

for (const [devices, width, height, layoutName, expected] of profiles) {
  const { content, frameWidth, frameHeight } = frame(width, height, layoutName);
  assert.deepEqual([frameWidth, frameHeight], expected, `${devices}: frame size`);
  assert.equal((frameWidth * frameHeight) % 8, 0, `${devices}: P4 JPEG decoder size rule`);
  assert.equal(frameHeight % 2, 0, `${devices}: even yuv420 height`);
  assert.ok(Math.abs(frameWidth * 9 - frameHeight * 16) <= 16, `${devices}: Bridge 16:9 check`);
  assert.ok(frameWidth >= 320 && frameWidth <= 752, `${devices}: Bridge width range`);
  assert.ok(frameHeight >= 180 && frameHeight <= 424, `${devices}: Bridge height range`);
  assert.ok(content - frameWidth < 8, `${devices}: the frame keeps the popup width`);
}

// The old rounding asked the 1024x600 layout for 558x314.
assert.equal((558 * 314) % 8, 4);

console.log('Camera geometry on every P4 layout: PASS');

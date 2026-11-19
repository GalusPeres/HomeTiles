// Camera frames in every display rotation (#155). On a turned Waveshare 4B the
// popup stayed black while the sound played: its direct output refused every
// rotation but 0 ("Direct device output unavailable; frame skipped"), and the
// camera stream drops a frame the device does not present. Every device that
// presents frames itself must do so in each rotation; this test names how
// every device draws them and runs the 4B's path on the host.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const devicesDir = path.join(root, 'src/devices');
const walk = (dir) => fs.readdirSync(dir, {withFileTypes: true}).flatMap((entry) =>
  entry.isDirectory() ? walk(path.join(dir, entry.name)) : [path.join(dir, entry.name)]);

// How each device presents a camera frame. A new device must be added here.
const kinds = {
  // ESP32-S3: software JPEG, LVGL draws the frame (camera_stream.cpp never
  // asks these for direct output).
  golden_security_gs_t3e_v2: 'lvgl',
  guition_esp32_4848s040: 'lvgl',
  waveshare_s3_touch_lcd_4: 'lvgl',
  waveshare_s3_touch_lcd_4b: 'lvgl',
  // ESP32-P4 panels: the shared PPA presenter, given the panel's rotation.
  guition_jc1060p470c: 'presenter',
  guition_jc1060p470c_v2: 'presenter',
  guition_jc4880p443_portrait: 'presenter',
  guition_jc8012p4a1: 'presenter',
  guition_jc8012p4a1_v2: 'presenter',
  waveshare_touch_lcd_10_1: 'presenter',
  waveshare_touch_lcd_4_3: 'presenter',
  waveshare_touch_lcd_5: 'presenter',
  waveshare_touch_lcd_7: 'presenter',
  waveshare_touch_lcd_7b: 'presenter',
  waveshare_touch_lcd_8: 'presenter',
  // Own PPA turns into the panel framebuffer.
  m5stacks_tab5: 'tab5',
  layout_test_1024x600: 'layout-test',
  layout_test_480x480: 'layout-test',
  // Arduino_GFX, which turns bitmaps into its framebuffer (run below).
  waveshare_4b: 'arduino-gfx',
};

// The bracket closing the one at `open`.
const closing = (code, open) => {
  const pair = {'(': ')', '{': '}'}[code[open]];
  let depth = 0;
  for (let i = open; i < code.length; i++) {
    if (code[i] === code[open]) depth++;
    else if (code[i] === pair && --depth === 0) return i;
  }
  throw new Error('unbalanced brackets');
};
// Conditions of the if statements that give up: `return false` as their own
// statement, not inside a nested block.
const refusals = (body) => {
  const code = maskCpp(body);
  const conditions = [];
  for (const match of code.matchAll(/\bif\s*\(/g)) {
    const open = match.index + match[0].length - 1;
    const close = closing(code, open);
    const after = code.slice(close + 1);
    let consequent;
    if (after.trimStart().startsWith('{')) {
      const start = code.indexOf('{', close);
      let depth = 0;
      consequent = '';
      for (const ch of code.slice(start + 1, closing(code, start))) {
        if (ch === '{') depth++;
        if (depth === 0) consequent += ch;
        if (ch === '}') depth--;
      }
    } else {
      consequent = after.slice(0, after.indexOf(';') + 1);
    }
    if (/\breturn\s+false\s*;/.test(consequent)) conditions.push(code.slice(open + 1, close));
  }
  return conditions;
};

const found = new Map();
for (const file of walk(devicesDir).filter((name) => /\.(cpp|h)$/.test(name))) {
  const relative = path.relative(devicesDir, file).split(path.sep);
  if (relative.length < 2) continue;  // device.cpp only forwards
  const source = fs.readFileSync(file, 'utf8');
  for (const definition of cppFunctionDefinitions(source)) {
    if (!definition.name.endsWith('::displayTryFullFramePreview')) continue;
    assert.ok(!found.has(relative[0]), `${relative[0]}: two direct preview definitions`);
    found.set(relative[0], {file: relative.join('/'), body: definition.body});
  }
}
assert.deepEqual([...found.keys()].sort(), Object.keys(kinds).sort(),
  'every device with a direct preview is named in this test, and every named device has one');

for (const [device, {file, body}] of found) {
  const kind = kinds[device];
  // No device may refuse a frame because the panel is turned.
  for (const condition of refusals(body)) {
    assert.doesNotMatch(condition, /rotation/i,
      `${file}: a turned panel must not skip camera frames (#155): if (${condition.trim()}) return false`);
  }
  if (kind === 'lvgl') {
    assert.match(body, /^\{(?:\s*\(void\)\w+;)*\s*return false;\s*\}$/, `${file}: S3 boards leave drawing to LVGL`);
  } else if (kind === 'presenter') {
    assert.match(body, /present\(/, `${file}: frames go through the shared presenter`);
    assert.match(body, /present\(\s*x, y, w, h, source_stride, data, data_size, byte_swap,\s*\w*rotation\w*(?:\(\))?,/,
      `${file}: the presenter gets the panel's rotation`);
  } else if (kind === 'tab5') {
    assert.match(body, /ppa_rotate_to_panel\(/, `${file}: the Tab5 turns frames by its own PPA path`);
  } else if (kind === 'layout-test') {
    assert.match(body, /PPA_SRM_ROTATION_ANGLE_90[\s\S]*PPA_SRM_ROTATION_ANGLE_270/, `${file}: both quarter turns`);
  } else {
    assert.match(body, /draw16bitRGBBitmap\(/, `${file}: Arduino_GFX turns the bitmap`);
  }
}

// The 4B on the host: its preview, full-screen info and end, with a stand-in
// for Arduino_GFX's DSI display that places pixels like Arduino_G.cpp's
// gfx_draw_bitmap_to_framebuffer_rotate_N.
const cxx = ['clang++', 'g++'].find((name) => spawnSync(name, ['--version']).status === 0);
if (!cxx) {
  console.log('SKIP: 4B camera rotation runtime test requires a host C++ compiler');
  process.exit(0);
}
const b4 = fs.readFileSync(path.join(root, 'src/devices/waveshare_4b/device_waveshare_4b.cpp'), 'utf8');
const b4Definitions = cppFunctionDefinitions(b4);
const take = (name) => {
  const definition = b4Definitions.find((entry) => entry.name === `DeviceWaveshare4B::${name}`);
  assert.ok(definition, `4B: ${name} is missing`);
  return definition.source;
};
const cpp = String.raw`
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define MALLOC_CAP_SPIRAM 1
void* heap_caps_aligned_alloc(size_t, size_t bytes, int) { return std::malloc(bytes); }
void heap_caps_free(void* memory) { std::free(memory); }
struct FakeSerial {
  template <typename... Args> void printf(const char*, Args...) {}
  void println(const char*) {}
} Serial;

class Arduino_DSI_Display {
 public:
  Arduino_DSI_Display(int w, int h) : W(w), H(h), fb(static_cast<size_t>(w) * h, 0) {}
  uint8_t getRotation() const { return rotation; }
  int16_t width() const { return (rotation & 1) ? H : W; }
  int16_t height() const { return (rotation & 1) ? W : H; }
  uint16_t* getFramebuffer() { return fb.data(); }
  void flush(bool = false) { ++flushes; }
  void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t* bitmap, int16_t w, int16_t h) {
    for (int j = 0; j < h; ++j)
      for (int i = 0; i < w; ++i) put(rotation, x + i, y + j, bitmap[j * w + i]);
  }
  void draw16bitBeRGBBitmap(int16_t x, int16_t y, uint16_t* bitmap, int16_t w, int16_t h) {
    // Arduino_GFX copies byte-swapped bitmaps fast only at rotation 0;
    // turned, they go one writePixel at a time.
    if (rotation != 0) ++slow_draws;
    for (int j = 0; j < h; ++j)
      for (int i = 0; i < w; ++i) {
        const uint16_t c = bitmap[j * w + i];
        put(rotation, x + i, y + j, static_cast<uint16_t>((c << 8) | (c >> 8)));
      }
  }
  // Rotation 1: logical x along panel rows, y right to left; 2: both turned;
  // 3: the reverse of 1.
  void put(int turn, int lx, int ly, uint16_t c) {
    int row = ly, col = lx;
    if (turn == 1) { row = lx; col = W - 1 - ly; }
    if (turn == 2) { row = H - 1 - ly; col = W - 1 - lx; }
    if (turn == 3) { row = H - 1 - lx; col = ly; }
    fb[static_cast<size_t>(row) * W + col] = c;
  }
  uint8_t rotation = 0;
  int W, H;
  std::vector<uint16_t> fb;
  int flushes = 0, slow_draws = 0;
};

struct { uint16_t width; uint16_t height; } display_cfg{720, 720};
Arduino_DSI_Display* g_gfx = nullptr;
uint16_t* g_turn_copy = nullptr;
size_t g_turn_copy_bytes = 0;
bool g_turn_copy_failure_logged = false;

namespace DeviceWaveshare4B {
bool displayTryFullFramePreview(int32_t x, int32_t y, int32_t w, int32_t h, int32_t source_stride,
                                const uint16_t* data, size_t data_size, bool byte_swap);
void displayEndFullFramePreview();
bool displayFullFrameInfo(uint16_t& width, uint16_t& height, uint16_t& turn_cw);
}

${take('displayTryFullFramePreview')}
${take('displayEndFullFramePreview')}
${take('displayFullFrameInfo')}

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

static unsigned seed = 155;
static uint16_t next_pixel() { seed = seed * 1103515245u + 12345u; return static_cast<uint16_t>(seed >> 9); }

// A picture turned clockwise by quarter turns (what the Bridge sends).
static std::vector<uint16_t> turn_cw(const std::vector<uint16_t>& src, int w, int h, int quarters) {
  std::vector<uint16_t> out(src);
  int cw = w, ch = h;
  for (int q = 0; q < quarters; ++q) {
    std::vector<uint16_t> next(out.size());
    for (int y = 0; y < ch; ++y)
      for (int x = 0; x < cw; ++x) next[static_cast<size_t>(x) * ch + (ch - 1 - y)] = out[static_cast<size_t>(y) * cw + x];
    out.swap(next);
    const int t = cw; cw = ch; ch = t;
  }
  return out;
}

int main() {
  Arduino_DSI_Display gfx(720, 720);
  g_gfx = &gfx;
  // The 4B's popup frame (672 x 378), as the hardware decoder writes it:
  // RGB565 byte-swapped for LVGL.
  const int w = 672, h = 378, x = 24, y = 171;
  std::vector<uint16_t> swapped(static_cast<size_t>(w) * h);
  for (auto& px : swapped) px = next_pixel();
  for (uint8_t rotation = 0; rotation < 4; ++rotation) {
    gfx.rotation = rotation;
    gfx.slow_draws = 0;
    std::fill(gfx.fb.begin(), gfx.fb.end(), 0xA5A5);
    const bool shown = DeviceWaveshare4B::displayTryFullFramePreview(
        x, y, w, h, w, swapped.data(), swapped.size() * 2, true);
    CHECK(shown, "rotation %u: the camera frame was skipped", rotation);
    std::vector<uint16_t> expected(gfx.fb.size(), 0xA5A5);
    Arduino_DSI_Display reference(720, 720);
    reference.fb.assign(expected.begin(), expected.end());
    for (int j = 0; j < h; ++j)
      for (int i = 0; i < w; ++i) {
        const uint16_t c = swapped[static_cast<size_t>(j) * w + i];
        reference.put(rotation, x + i, y + j, static_cast<uint16_t>((c << 8) | (c >> 8)));
      }
    CHECK(gfx.fb == reference.fb, "rotation %u: the frame is not on the panel turned like the UI", rotation);
    CHECK(gfx.slow_draws == 0, "rotation %u: byte-swapped pixels went one at a time", rotation);
    CHECK((rotation == 0) == (g_turn_copy == nullptr), "rotation %u: native copy only for a turned panel", rotation);
    DeviceWaveshare4B::displayEndFullFramePreview();
    CHECK(g_turn_copy == nullptr && g_turn_copy_bytes == 0, "rotation %u: the end frees the native copy", rotation);
  }

  // The screensaver's composite: native RGB565 over the whole screen.
  std::vector<uint16_t> screen(static_cast<size_t>(720) * 720);
  for (auto& px : screen) px = next_pixel();
  for (uint8_t rotation = 0; rotation < 4; ++rotation) {
    gfx.rotation = rotation;
    std::fill(gfx.fb.begin(), gfx.fb.end(), 0);
    CHECK(DeviceWaveshare4B::displayTryFullFramePreview(0, 0, 720, 720, 720, screen.data(), screen.size() * 2, false),
          "rotation %u: the composite was refused", rotation);
    Arduino_DSI_Display reference(720, 720);
    for (int j = 0; j < 720; ++j)
      for (int i = 0; i < 720; ++i) reference.put(rotation, i, j, screen[static_cast<size_t>(j) * 720 + i]);
    CHECK(gfx.fb == reference.fb, "rotation %u: the composite is not turned like the UI", rotation);
  }

  // Out of the screen stays refused.
  gfx.rotation = 1;
  CHECK(!DeviceWaveshare4B::displayTryFullFramePreview(100, 400, w, h, w, swapped.data(), swapped.size() * 2, true),
        "a frame past the screen edge must be refused");
  CHECK(!DeviceWaveshare4B::displayTryFullFramePreview(x, y, w, h, w + 16, swapped.data(), swapped.size() * 2, true),
        "a padded stride is not a contiguous bitmap");
  DeviceWaveshare4B::displayEndFullFramePreview();

  // Full screen: the Bridge's turn makes its frame, written straight into the
  // framebuffer, look like the UI drawn through Arduino_GFX's rotation.
  std::vector<uint16_t> upright(static_cast<size_t>(720) * 720);
  for (auto& px : upright) px = next_pixel();
  for (uint8_t rotation = 0; rotation < 4; ++rotation) {
    gfx.rotation = rotation;
    uint16_t fw = 0, fh = 0, turn = 1;
    CHECK(DeviceWaveshare4B::displayFullFrameInfo(fw, fh, turn), "rotation %u: no full screen", rotation);
    CHECK(fw == 720 && fh == 720, "rotation %u: full frames are the framebuffer's size", rotation);
    CHECK(turn == rotation * 90, "rotation %u: turn %u", rotation, turn);
    Arduino_DSI_Display ui(720, 720);
    for (int j = 0; j < 720; ++j)
      for (int i = 0; i < 720; ++i) ui.put(rotation, i, j, upright[static_cast<size_t>(j) * 720 + i]);
    CHECK(turn_cw(upright, 720, 720, turn / 90) == ui.fb, "rotation %u: the Bridge's turn does not match the UI's", rotation);
  }

  if (failures) return 1;
  std::printf("4B camera frames in every rotation: PASS\n");
  return 0;
}
`;
const out = path.join(root, 'build/tests/camera-preview-rotations');
fs.mkdirSync(out, {recursive: true});
const sourcePath = path.join(out, 'b4_rotations.cpp');
const binaryPath = path.join(out, process.platform === 'win32' ? 'b4_rotations.exe' : 'b4_rotations');
fs.writeFileSync(sourcePath, cpp);
const compiled = spawnSync(cxx, ['-std=c++17', '-O1', '-Wall', '-o', binaryPath, sourcePath], {encoding: 'utf8'});
assert.equal(compiled.status, 0, `compile failed:\n${compiled.stderr}`);
const run = spawnSync(binaryPath, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `4B rotation run failed:\n${run.stdout}${run.stderr}`);
process.stdout.write(run.stdout);
console.log('Camera preview rotations on every device: PASS');

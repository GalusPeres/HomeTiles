// Host regression for the shared speaker base (src/audio/audio_board.h):
// every ESP32-P4 profile with voice names its board file, every board file
// states the facts researched from the board's schematic or BSP (pins,
// chips, addresses, amplifier), no audio pin is offered as a free I/O pin,
// the S3 panels and the original-panel JC1060 stay out, and the real codec
// drivers write the vendor register sequences through a recording codec_bus
// (ES8311 microphone start and gain, ES8388 playback and volume). Also the
// mic level meter and the left-slot capture of the ES8311 boards.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = rel => fs.readFileSync(path.join(root, rel), 'utf8').replace(/\r\n/g, '\n');

// --- Board table: researched facts (see each speaker_board.h) -------------
const WAVESHARE = {mclk: 13, bclk: 12, lrck: 10, dout: 9, din: 11, out: 'Es8311', codec: 0x18,
  inp: 'Es7210', mic: 0x40, amp: 53, limit: 1.0};
const GUITION_11 = {mclk: 13, bclk: 12, lrck: 10, dout: 9, din: 11, out: 'Es8311', codec: 0x18,
  inp: 'Es8311', mic: 0x00, amp: 20, limit: 0.6};
const GUITION_48 = {mclk: 13, bclk: 12, lrck: 10, dout: 9, din: 48, out: 'Es8311', codec: 0x18,
  inp: 'Es8311', mic: 0x00, amp: 11, limit: 0.6};
const BOARDS = {
  DEVICE_WAVESHARE_TOUCH_LCD_7B: {dir: 'waveshare_touch_lcd_7b', ...WAVESHARE},
  DEVICE_WAVESHARE_TOUCH_LCD_8: {dir: 'waveshare_touch_lcd_8', ...WAVESHARE},
  DEVICE_WAVESHARE_TOUCH_LCD_7: {dir: 'waveshare_touch_lcd_7', ...WAVESHARE},
  DEVICE_WAVESHARE_TOUCH_LCD_10_1: {dir: 'waveshare_touch_lcd_10_1', ...WAVESHARE},
  DEVICE_WAVESHARE_TOUCH_LCD_4_3: {dir: 'waveshare_touch_lcd_4_3', ...WAVESHARE},
  // The LCD-5 (#76): the LCD-X pins, chips and full volume.
  DEVICE_WAVESHARE_TOUCH_LCD_5: {dir: 'waveshare_touch_lcd_5', ...WAVESHARE},
  DEVICE_WAVESHARE_4B: {dir: 'waveshare_4b', ...WAVESHARE},
  DEVICE_GUITION_JC8012P4A1: {dir: 'guition_jc8012p4a1', ...GUITION_11},
  DEVICE_GUITION_JC8012P4A1_V2: {dir: 'guition_jc8012p4a1_v2', ...GUITION_11},
  DEVICE_GUITION_JC1060P470C_V2: {dir: 'guition_jc1060p470c_v2', ...GUITION_48},
  DEVICE_GUITION_JC4880P443_PORTRAIT: {dir: 'guition_jc4880p443_portrait', ...GUITION_48},
  DEVICE_M5STACKS_TAB5: {dir: 'm5stacks_tab5', mclk: 30, bclk: 27, lrck: 29, dout: 26, din: 28,
    out: 'Es8388', codec: 0x10, inp: 'Es7210', mic: 0x40, amp: 'NC', byBoard: true, limit: 0.9},
};
// No voice: the ESP32-S3 panels (user decision), the host layout tests and
// the original-panel JC1060 (board identity unproven, like its camera).
const WITHOUT_VOICE = [
  'DEVICE_WAVESHARE_S3_TOUCH_LCD_4', 'DEVICE_WAVESHARE_S3_TOUCH_LCD_4B',
  'DEVICE_GUITION_ESP32_4848S040', 'DEVICE_GOLDEN_SECURITY_GS_T3E_V2',
  'DEVICE_GOLDEN_SECURITY_GS_T3E_V24',
  'DEVICE_LAYOUT_TEST_1024X600', 'DEVICE_LAYOUT_TEST_480X480',
  'DEVICE_GUITION_JC1060P470C',
];

const deviceSelect = read('src/devices/device_select.h');
const allDefines = [...deviceSelect.matchAll(/^\/\/ #define (DEVICE_[A-Z0-9_]+)/gm)].map(m => m[1]);
allDefines.push('DEVICE_WAVESHARE_4B');
assert.equal(new Set(allDefines).size, Object.keys(BOARDS).length + WITHOUT_VOICE.length,
  'every device define is either a voice board or listed without voice');
for (const define of allDefines) {
  assert.ok(define in BOARDS || WITHOUT_VOICE.includes(define), `${define} is classified`);
}

const select = read('src/audio/speaker_select.h');
const selected = Object.fromEntries([...select.matchAll(
  /defined\((DEVICE_[A-Z0-9_]+)\)\n#define HOMETILES_SPEAKER_BOARD "src\/devices\/([a-z0-9_]+)\/speaker_board\.h"/g)]
  .map(m => [m[1], m[2]]));
assert.deepEqual(Object.keys(selected).sort(), Object.keys(BOARDS).sort(),
  'speaker_select.h maps exactly the voice boards');
for (const define of WITHOUT_VOICE) {
  assert.ok(!new RegExp(`defined\\(${define}\\)\\n`).test(select), `${define} has no speaker board`);
}
assert.match(select, /#if defined\(HOMETILES_SPEAKER_BOARD\)\n#define HOMETILES_SPEAKER 1\n#define HOMETILES_WAKE_WORD 1/);

function constant(header, name) {
  const match = header.match(new RegExp(`constexpr [A-Za-z_:0-9]+ ${name} = ([^;]+);`));
  assert.ok(match, `missing ${name}`);
  return match[1].trim();
}
const gpio = value => value === 'GPIO_NUM_NC' ? 'NC' : Number(value.replace('GPIO_NUM_', ''));

for (const [define, facts] of Object.entries(BOARDS)) {
  assert.equal(selected[define], facts.dir, `${define} board folder`);
  const header = read(`src/devices/${facts.dir}/speaker_board.h`);
  const source = read(`src/devices/${facts.dir}/speaker_board.cpp`);
  const where = `${facts.dir}/speaker_board.h`;
  assert.equal(gpio(constant(header, 'kI2sMclk')), facts.mclk, `${where} MCLK`);
  assert.equal(gpio(constant(header, 'kI2sBclk')), facts.bclk, `${where} BCLK`);
  assert.equal(gpio(constant(header, 'kI2sLrck')), facts.lrck, `${where} LRCK`);
  assert.equal(gpio(constant(header, 'kI2sDout')), facts.dout, `${where} DOUT`);
  assert.equal(gpio(constant(header, 'kI2sDin')), facts.din, `${where} DIN`);
  assert.equal(constant(header, 'kOutput'), `audio_chips::Output::${facts.out}`, `${where} output`);
  assert.equal(Number(constant(header, 'kCodecAddress')), facts.codec, `${where} codec address`);
  assert.equal(constant(header, 'kInput'), `audio_chips::Input::${facts.inp}`, `${where} input`);
  assert.equal(Number(constant(header, 'kMicAddress')), facts.mic, `${where} ES7210 address`);
  assert.equal(gpio(constant(header, 'kAmpEnable')), facts.amp, `${where} amplifier`);
  assert.equal(constant(header, 'kAmpActiveHigh'), 'true', `${where} amplifier level`);
  // Loudest clean level (user 2026-10-16: the Guition boards 60 %, Tab5 90 %).
  assert.equal(Number(constant(header, 'kVolumeLimit').replace(/f$/, '')), facts.limit,
    `${where} volume limit`);
  assert.match(source, new RegExp(`#if defined\\(${define}\\) && defined\\(HOMETILES_SPEAKER\\)`),
    `${facts.dir}/speaker_board.cpp is guarded by its own define`);
  if (facts.byBoard) {
    assert.match(source, /void setAmp\(bool on\)/);
    assert.match(source, /esp_err_t attach\(uint8_t address, Device\* device\)/);
    assert.match(select, /DEVICE_M5STACKS_TAB5\)\n#define HOMETILES_SPEAKER_BOARD [^\n]+\n#define HOMETILES_CODEC_BUS_BY_BOARD 1\n#define HOMETILES_SPEAKER_AMP_BY_BOARD 1/);
  } else {
    assert.match(header, /i2c_master_bus_handle_t bus\(\);/, `${where} declares its bus`);
    assert.match(source, /i2c_master_bus_handle_t bus\(\) \{ return [^;]+; \}/);
  }
  // Every fact cites its source in the header comment.
  assert.match(header, /^\/\/ Speaker (and microphone )?wiring of the /m, `${where} comment`);
  assert.match(header, /schematic|BSP/, `${where} names its source`);

  // No audio pin is a free I/O pin of the same profile.
  const ioPath = ['hardware_io_profile.h', 'profile.h']
    .map(name => `src/devices/${facts.dir}/${name}`)
    .find(rel => fs.existsSync(path.join(root, rel)) && /\{\s*\d+,/.test(read(rel)));
  if (ioPath) {
    const ioPins = [...read(ioPath).matchAll(/^\s*\{\s*(\d+),/gm)].map(m => Number(m[1]));
    for (const pin of [facts.mclk, facts.bclk, facts.lrck, facts.dout, facts.din, facts.amp]) {
      assert.ok(!ioPins.includes(pin), `${facts.dir}: GPIO${pin} is an audio pin and a free I/O pin`);
    }
  }
}
assert.ok(!fs.existsSync(path.join(root, 'src/devices/guition_jc1060p470c/speaker_board.h')));

// --- Real codec drivers through a recording codec_bus ---------------------
function findCompiler(candidates) {
  for (const candidate of candidates.filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}
const cxx = findCompiler([process.env.CXX, 'clang++', 'g++', 'c++']);
if (!cxx) {
  console.log('SKIP: codec driver part needs a host C++ compiler (board table checked)');
  process.exit(0);
}

const out = path.join(root, 'build/tests/speaker-boards');
const stubs = path.join(out, 'stubs');
fs.mkdirSync(path.join(stubs, 'freertos'), {recursive: true});
fs.writeFileSync(path.join(stubs, 'esp_err.h'), `#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
`);
fs.writeFileSync(path.join(stubs, 'freertos/FreeRTOS.h'), `#pragma once
#define pdMS_TO_TICKS(ms) (ms)
`);
fs.writeFileSync(path.join(stubs, 'freertos/task.h'), `#pragma once
inline void vTaskDelay(int) {}
`);

const program = String.raw`
#include <cassert>
#include <cstdio>
#include <vector>

#include "src/audio/codecs/codec_bus.h"
#include "src/audio/codecs/es8311/es8311_codec.h"
#include "src/audio/codecs/es8388/es8388_codec.h"
#include "src/audio/assist_contract.h"
#include "src/audio/mic_level.h"

// A recording bus with a register file per address.
struct Write { uint8_t address, reg, value; };
static std::vector<Write> g_writes;
static uint8_t g_regs[128][256];

namespace codec_bus {
esp_err_t attach(uint8_t address, Device* device) {
  *device = Device{};
  device->address = address;
  device->attached = true;
  return ESP_OK;
}
esp_err_t write(const Device& device, uint8_t reg, uint8_t value) {
  g_writes.push_back({device.address, reg, value});
  g_regs[device.address][reg] = value;
  return ESP_OK;
}
esp_err_t read(const Device& device, uint8_t reg, uint8_t* value) {
  *value = g_regs[device.address][reg];
  return ESP_OK;
}
void detach(Device* device) { *device = Device{}; }
}  // namespace codec_bus

static int lastWrite(uint8_t address, uint8_t reg) {
  int value = -1;
  for (const Write& w : g_writes) if (w.address == address && w.reg == reg) value = w.value;
  return value;
}

int main() {
  // ES8311: chip ID, playback init, then the microphone start of the
  // Guition boards (esp_codec_dev es8311_open/es8311_start ADC lines).
  g_regs[0x18][0xFD] = 0x83;
  g_regs[0x18][0xFE] = 0x11;
  es8311::Codec es8311;
  assert(es8311::probe(0x18, &es8311) == ESP_OK);
  assert(es8311::init(&es8311, 44100) == ESP_OK);
  assert(lastWrite(0x18, 0x0E) == 0x02 && lastWrite(0x18, 0x12) == 0x00);
  g_writes.clear();
  assert(es8311::setSampleRate(&es8311, 16000) == ESP_OK);
  assert(es8311::startMicrophone(&es8311, 4) == ESP_OK);
  assert(lastWrite(0x18, 0x44) == 0x08);  // ADC data on both slots
  assert(lastWrite(0x18, 0x1B) == 0x0A);  // ADC HPF
  assert(lastWrite(0x18, 0x17) == 0xBF);  // ADC volume 0 dB
  assert(lastWrite(0x18, 0x14) == 0x1A);  // analog MIC1, PGA 30 dB
  assert(lastWrite(0x18, 0x0D) == 0x01);
  assert(lastWrite(0x18, 0x15) == 0x40);
  assert(lastWrite(0x18, 0x16) == 4);     // 24 dB digital
  // The DAC stays powered: the microphone start never touches 0x12.
  assert(lastWrite(0x18, 0x12) == -1);
  assert(es8311::setMicScale(&es8311, 9) == ESP_OK && lastWrite(0x18, 0x16) == 7);
  // A wrong chip ID is refused and detached.
  g_regs[0x19][0xFD] = 0x12;
  es8311::Codec other;
  assert(es8311::probe(0x19, &other) == ESP_ERR_NOT_FOUND && !other.dev.attached);

  // Stored gain steps (tenths of a dB) to the ES8311 scale: the default
  // 30 dB step is Guition's demo value 24 dB (scale 4).
  assert(es8311::micScaleForTenthsDb(300) == 4);
  assert(es8311::micScaleForTenthsDb(0) == 0);
  assert(es8311::micScaleForTenthsDb(59) == 0);
  assert(es8311::micScaleForTenthsDb(120) == 1);
  assert(es8311::micScaleForTenthsDb(375) == 5);

  // ES8388 (Tab5): esp_codec_dev DAC setup, 256 x fs, all outputs at
  // M5Unified's +4.5 dB, DAC started, ADC off, muted until unmuted.
  g_writes.clear();
  es8388::Codec es8388;
  assert(es8388::probe(0x10, &es8388) == ESP_OK);
  assert(es8388::init(&es8388) == ESP_OK);
  assert(lastWrite(0x10, 0x08) == 0x00);  // I2S slave
  assert(lastWrite(0x10, 0x17) == 0x18);  // 16-bit I2S
  assert(lastWrite(0x10, 0x18) == 0x02);  // single speed, MCLK/LRCK 256
  assert(lastWrite(0x10, 0x27) == 0x90 && lastWrite(0x10, 0x2A) == 0x90);
  for (uint8_t reg = 0x2E; reg <= 0x31; ++reg) assert(lastWrite(0x10, reg) == 0x21);
  assert(lastWrite(0x10, 0x04) == 0x3C);  // DAC and all four outputs
  assert(lastWrite(0x10, 0x03) == 0xFF);  // ADC powered down
  assert(lastWrite(0x10, 0x02) == 0x00);
  assert((g_regs[0x10][0x19] & 0x04) != 0);  // still muted
  assert(es8388::setMute(&es8388, false) == ESP_OK && (g_regs[0x10][0x19] & 0x04) == 0);
  assert(es8388::setMute(&es8388, true) == ESP_OK && (g_regs[0x10][0x19] & 0x04) != 0);
  // The shared ES8311 volume scale as ES8388 attenuation.
  assert(es8388::setVolumeRegister(&es8388, 0xBF) == ESP_OK);
  assert(lastWrite(0x10, 0x1A) == 0x00 && lastWrite(0x10, 0x1B) == 0x00);
  assert(es8388::setVolumeRegister(&es8388, 0xBF - 40) == ESP_OK && lastWrite(0x10, 0x1A) == 40);
  assert(es8388::setVolumeRegister(&es8388, 0) == ESP_OK && lastWrite(0x10, 0x1A) == 0xBF);
  assert(es8388::setVolumeRegister(&es8388, 0xFF) == ESP_OK && lastWrite(0x10, 0x1A) == 0x00);
  // start(): repeated once MCLK runs, restarts the state machine and powers
  // the DAC without touching the mute bit.
  g_writes.clear();
  assert(es8388::start(&es8388) == ESP_OK);
  assert(g_writes.size() == 3 && g_writes[0].reg == 0x02 && g_writes[0].value == 0xF0 &&
         g_writes[1].reg == 0x02 && g_writes[1].value == 0x00 && g_writes[2].reg == 0x04 &&
         g_writes[2].value == 0x3C);
  assert(es8388::supportsSampleRate(16000) && es8388::supportsSampleRate(44100) &&
         !es8388::supportsSampleRate(96000));

  // Capture: the ES8311 boards take the left slot, ES7210 boards average.
  const int16_t stereo[6] = {1000, -2000, 300, 300, -32768, 0};
  int16_t mono[3] = {};
  assist_contract::stereoLeftToMono(stereo, 3, mono);
  assert(mono[0] == 1000 && mono[1] == 300 && mono[2] == -32768);
  assist_contract::stereoToMono(stereo, 3, mono);
  assert(mono[0] == -500 && mono[1] == 300 && mono[2] == -16384);

  // Mic level meter.
  mic_level::Meter meter;
  assert(meter.samples() == 0 && meter.rmsDbfs() == -96.0f && meter.peakDbfs() == -96.0f);
  const int16_t full[4] = {32767, -32768, 32767, -32768};
  meter.add(full, 4);
  assert(meter.samples() == 4);
  assert(meter.peakDbfs() > -0.01f && meter.peakDbfs() <= 0.0f);
  assert(meter.rmsDbfs() > -0.01f && meter.rmsDbfs() <= 0.0f);
  meter.reset();
  const int16_t tenth[2] = {3277, -3277};
  meter.add(tenth, 2);
  assert(meter.peakDbfs() > -20.01f && meter.peakDbfs() < -19.99f);
  std::puts("ok");
  return 0;
}
`;
const cpp = path.join(out, 'speaker-boards.cpp');
const exe = path.join(out, process.platform === 'win32' ? 'speaker-boards.exe' : 'speaker-boards');
fs.writeFileSync(cpp, program);
const flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', stubs, '-I', root,
  '-DHOMETILES_CI_TARGET', '-DDEVICE_WAVESHARE_TOUCH_LCD_8'];
if (process.platform === 'win32') flags.push('-D_USE_MATH_DEFINES');
const sources = [cpp, 'src/audio/codecs/es8311/es8311_codec.cpp', 'src/audio/codecs/es8388/es8388_codec.cpp']
  .map(file => path.isAbsolute(file) ? file : path.join(root, file));
const build = spawnSync(cxx, [...flags, ...sources, '-o', exe], {encoding: 'utf8'});
assert.equal(build.status, 0, `speaker board test did not compile:\n${build.stdout}${build.stderr}`);
const run = spawnSync(exe, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `speaker board test failed:\n${run.stdout}${run.stderr}`);
assert.equal(run.stdout.trim(), 'ok');
console.log('speaker boards: table, pins and codec drivers ok');

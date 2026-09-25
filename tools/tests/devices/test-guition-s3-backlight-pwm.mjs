// Issue #45: the Guition ESP32-4848S040 whined audibly while the backlight was
// dimmed with a 1 kHz LEDC PWM. This source contract keeps the exact-board
// low-rate PWM, the unchanged brightness mapping and the storage blackout
// wait that must cover one full PWM period.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8');
}

function constant(source, name) {
  const match = source.match(
    new RegExp(`constexpr\\s+\\w+\\s+${name}\\s*=\\s*(\\d+)u?;`));
  assert.ok(match, `${name} must be a plain numeric constant`);
  return Number(match[1]);
}

function functionBody(source, signature) {
  const start = source.indexOf(signature);
  assert.ok(start >= 0, `Missing function: ${signature}`);
  const open = source.indexOf('{', start);
  let depth = 0;
  for (let i = open; i < source.length; ++i) {
    if (source[i] === '{') ++depth;
    if (source[i] === '}' && --depth === 0) return source.slice(open, i + 1);
  }
  assert.fail(`Unterminated function: ${signature}`);
}

const devicePath =
  'src/devices/guition_esp32_4848s040/device_guition_esp32_4848s040.cpp';
const device = read(devicePath);
const profile = read(
  'src/devices/guition_esp32_4848s040/device_guition_esp32_4848s040.h');

// Exact-board backlight pin and PWM settings.
assert.equal(constant(device, 'kBacklightPin'), 38,
             'The 4848S040 backlight enable is GPIO38');
const frequency = constant(device, 'kBacklightFrequency');
const resolution = constant(device, 'kBacklightResolution');
assert.equal(frequency, 150,
             'Backlight PWM must use the silent exact-board 150 Hz rate');
assert.ok(frequency < 500,
          'Backlight PWM must stay below the audible whine band (Issue #45)');
assert.ok(frequency > 100,
          'Backlight PWM must stay above the visibly shimmering 100 Hz rate');
assert.equal(resolution, 10,
             'The raw 0..255 to 10-bit duty mapping must stay unchanged');
assert.match(
  device,
  /constexpr uint16_t kBacklightMaxDuty = \(1u << kBacklightResolution\) - 1u;/,
  'The maximum duty must remain derived from the resolution');

// Arduino-ESP32 3.3.7 selects the 40 MHz XTAL clock for the S3 LEDC. The
// timer divider has a 10-bit integer part, so it must stay within 1..1023.
const divider = 40_000_000 / (frequency * (1 << resolution));
assert.ok(divider >= 1 && divider < 1024,
          `S3 LEDC divider ${divider.toFixed(1)} is outside the hardware range`);

assert.ok(
  device.includes(
    'ledcAttach(kBacklightPin, kBacklightFrequency,\n                  kBacklightResolution)'),
  'The backlight must attach with the named frequency and resolution');

// The brightness mapping, off state and calibrated floor stay identical.
const applyBody = functionBody(device, 'void applyBrightness(');
assert.ok(
  applyBody.includes(
    '(static_cast<uint32_t>(value) * kBacklightMaxDuty + 127u) / 255u'),
  'applyBrightness must keep the raw 0..255 rounding to the 10-bit duty');
const maxDuty = (1 << resolution) - 1;
const dutyFor = raw => Math.floor((raw * maxDuty + 127) / 255);
assert.equal(dutyFor(0), 0, 'Raw 0 must switch the backlight fully off');
assert.equal(dutyFor(255), maxDuty,
             'Raw 255 must reach the duty that Arduino maps to full on');
assert.match(profile, /Raw 0 remains reserved[\s\S]*?\n\s*39,\n/,
             'The hardware-calibrated visible floor must stay at raw 39');
const floorDuty = dutyFor(39);
const pulseUs = hz => (floorDuty / (maxDuty + 1)) * (1_000_000 / hz);
assert.ok(pulseUs(frequency) >= pulseUs(1000),
          'The minimum visible pulse must not get shorter than at 1 kHz');

for (const signature of [
  'void DeviceGuitionESP324848S040::displaySleep()',
  'void DeviceGuitionESP324848S040::displayWakeDark()',
  'void DeviceGuitionESP324848S040::prepareForRestart()',
]) {
  assert.ok(functionBody(device, signature).includes('applyBrightness(0, false);'),
            `${signature} must still blank with raw 0`);
}

// LEDC latches a new duty only at the next PWM cycle. The flash-write
// blackout must wait at least one full period before the write starts.
assert.match(
  device,
  /constexpr uint32_t kBacklightDutyLatchMs =\s*\(1000u \+ kBacklightFrequency - 1u\) \/ kBacklightFrequency \+ 1u;/,
  'The blackout wait must be derived from the PWM period');
const latchMs = Math.floor((1000 + frequency - 1) / frequency) + 1;
assert.ok(latchMs > 1000 / frequency,
          'The blackout wait must exceed one PWM period');
assert.equal(Math.floor((1000 + 1000 - 1) / 1000) + 1, 2,
             'The derived wait must equal the former 2 ms at 1 kHz');
const storageBegin = functionBody(
  device, 'void DeviceGuitionESP324848S040::storageWriteBegin()');
assert.match(
  storageBegin,
  /applyBrightness\(0, false\);\s*delay\(kBacklightDutyLatchMs\);/,
  'The storage blackout must wait for the zero duty to latch');
assert.ok(!/delay\(2\);/.test(storageBegin),
          'A fixed 2 ms wait is shorter than one 150 Hz PWM period');

// The change is exact-profile only: no other device uses this rate.
const devicesDir = path.join(repoRoot, 'src/devices');
for (const entry of fs.readdirSync(devicesDir, { withFileTypes: true })) {
  if (!entry.isDirectory() || entry.name === 'guition_esp32_4848s040') continue;
  for (const file of fs.readdirSync(path.join(devicesDir, entry.name))) {
    if (!file.endsWith('.cpp') && !file.endsWith('.h')) continue;
    const source = fs.readFileSync(path.join(devicesDir, entry.name, file), 'utf8');
    assert.ok(!/kBacklightFreq\w*\s*=\s*150\b/.test(source),
              `${entry.name}/${file} must keep its own backlight PWM rate`);
  }
}

console.log('Guition S3 backlight PWM contract OK');

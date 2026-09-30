// Every panel keeps Wi-Fi awake at full power. The idle saving (modem sleep,
// 11 dBm) was left only on the ESP32-S3 profiles; on the Guition S3 it came
// with Web Admin pages holding the loop for 2.5 s and link drops of 20 s and
// more. The P4 had it off already. Display dimming and sleep stay unchanged.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const setter = cppFunctionDefinitions(read('src/network/network_manager.cpp'))
  .find(f => f.name === 'HomeTilesNetworkManager::setWifiPowerSaving').source;
const force = setter.indexOf('\n  enable = false;\n');
assert.ok(force >= 0, 'the request is forced to full power');
assert.doesNotMatch(setter.slice(0, force), /#if/, 'on every target, not only the P4');
assert.ok(force < setter.indexOf('if (wifi_ps_state_known && wifi_ps_enabled == enable)'),
  'before the mode is applied');

// The power manager still requests saving on idle; the setter answers with full power.
const power = read('src/core/power/power_manager.cpp');
assert.match(power, /applyCpuFrequency\(CPU_FREQ_LOW\);\n    networkManager\.setWifiPowerSaving\(true\);/);

console.log('Wi-Fi stays awake at full power on every panel.');

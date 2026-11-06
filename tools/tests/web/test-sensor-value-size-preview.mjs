// A chosen Sensor value size must reach the live and cached Web Admin
// previews (user 2026-11-03: "120" looked exactly like "96"). The panel draws
// real 120 px digits, so the preview needs its own class and the font size the
// device really uses, not the 2/3 UI font table behind --fs96.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');

const context = vm.createContext({});
vm.runInContext([
  extractDeliveredFunction('normalizeSensorValueFont'),
  extractDeliveredFunction('getSensorValueFontClass'),
].join('\n'), context);

assert.equal(context.getSensorValueFontClass('8'), 'sensor-value-size-96');
assert.equal(context.getSensorValueFontClass('9'), 'sensor-value-size-120');
assert.equal(context.getSensorValueFontClass(9), 'sensor-value-size-120');
assert.equal(context.getSensorValueFontClass('0'), 'sensor-value-size-default');

const css = read('src/web/assets/admin.css');
assert.match(css, /\.tile-value\.sensor-value-size-120\s*\{[^}]*var\(--sensor-value-fs-120/,
  'the preview needs a 120 class with its own device-scaled size');
const admin = read('src/types/sensor/admin-editor.js');
assert.match(admin, /classList\.remove\([^)]*'sensor-value-size-120'/,
  'switching back must remove the 120 class');
const styles = read('src/web/server/render/web_admin_styles.cpp');
assert.match(styles, /emit_scaled\("sensor-value-fs-120"/,
  'the 120 variable must follow the panel font, not the generic --fs table');
assert.match(readRepoFile('src/tiles/runtime/tile_renderer_fonts.h'), /case 9: return content_font_120\(\);/);

console.log('Sensor value 120 preview uses its own class and the device font size');

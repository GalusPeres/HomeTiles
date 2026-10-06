// A popup opened during AP mode must still get its deferred content
// (regression: Settings > Wi-Fi reopened after "Enable AP" stayed empty).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

// The AP-mode loop returns early and skips the normal loop's
// process_popup_open(); it must build deferred popup content itself.
const loop = cppFunctionDefinitions(read('HomeTiles.ino')).find(f => f.name === 'loop').source;
const start = loop.indexOf('  if (webConfigServer.isRunning()) {');
assert.ok(start >= 0, 'AP-mode loop branch not found');
const apBranch = loop.slice(start, loop.indexOf('\n    return;\n  }', start));
const open = apBranch.indexOf('process_popup_open();');
assert.ok(open >= 0, 'AP-mode loop must call process_popup_open()');
assert.ok(open < apBranch.indexOf('lv_timer_handler();'), 'build before the LVGL refresh');

// lv_qrcode_set_size clears the canvas: the Settings QR codes (the hotspot
// on the WiFi page, GitHub) are sized first and drawn after.
const qr = cppFunctionDefinitions(read('src/ui/tabs/settings/settings_parts.cpp'))
  .find(f => f.name === 'qr_code').source;
assert.ok(qr.indexOf('lv_qrcode_set_size(') < qr.indexOf('lv_qrcode_update('), 'size first, then the code');

console.log('AP-mode loop builds deferred popups; the AP QR code is redrawn after sizing.');

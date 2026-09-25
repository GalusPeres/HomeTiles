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

// lv_qrcode_set_size clears the canvas. A reopened popup's QR object can reuse
// the freed address, so the code must be redrawn after every sizing.
const status = cppFunctionDefinitions(read('src/ui/tabs/settings/tab_settings.cpp'))
  .find(f => f.name === 'wifi_update_conn_status_label').source;
assert.doesNotMatch(status, /last_qr_obj/);
assert.match(status, /if \(qr_resized \|\| strcmp\(last_qr_buf, qr_buf\) != 0\)/);

console.log('AP-mode loop builds deferred popups; the AP QR code is redrawn after sizing.');

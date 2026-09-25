// Wi-Fi popup list must take only the free height above the footer (4B
// regression: three networks pushed the Ethernet row off the popup).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/ui/tabs/settings/tab_settings.cpp'), 'utf8')
  .replace(/\r\n/g, '\n');
const fn = name => cppFunctionDefinitions(source).find(f => f.name === name).source;
const build = fn('build_wifi_popup');
const status = fn('wifi_update_conn_status_label');

assert.doesNotMatch(build, /max_height\(wifi_list_container, LV_PCT\(/, 'no percentage cap on the list');
for (const marker of [
  'wifi_list_block = lv_obj_create(wifi_list_view);',
  'lv_obj_set_flex_grow(wifi_list_block, 1);',
  'wifi_scan_status_label = lv_label_create(wifi_list_block);',
  'wifi_list_container = lv_obj_create(wifi_list_block);',
  'lv_obj_set_flex_grow(wifi_list_container, 1);',
  'lv_obj_set_style_max_height(wifi_list_container, LV_SIZE_CONTENT, 0);',
  'wifi_manual_gap = lv_obj_create(wifi_list_block);',
  'wifi_manual_row = wifi_create_row(wifi_list_block,',
]) assert.ok(build.includes(marker), `build_wifi_popup is missing: ${marker}`);
assert.ok(fn('reset_popup_refs').includes('wifi_list_block = nullptr;'));

// Two visible flex-grow items would split the free height: the spacer is
// for the AP view only and replaces the list block there.
assert.match(status, /if \(ap\) lv_obj_add_flag\(wifi_list_block, LV_OBJ_FLAG_HIDDEN\);\s*else lv_obj_clear_flag\(wifi_list_block,/);
assert.match(status, /if \(ap\) lv_obj_clear_flag\(wifi_list_spacer, LV_OBJ_FLAG_HIDDEN\);\s*else lv_obj_add_flag\(wifi_list_spacer,/);

// The AP QR code follows the measured free height; a large floor pushed the
// footer off screen on the 4B.
const floor = [...status.matchAll(/target < popup_layout::scale\((\d+)\)/g)].map(m => Number(m[1]));
assert.equal(floor.length, 1);
assert.ok(floor[0] <= 120, `QR floor ${floor[0]} is too large`);

console.log('Wi-Fi popup list is bounded to the free height; AP spacer and QR floor fit.');

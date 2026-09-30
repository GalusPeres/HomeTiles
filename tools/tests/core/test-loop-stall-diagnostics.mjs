// The Guition S3 main loop stopped for 16 to 44 seconds while MQTT dropped,
// and [LoopGap] (which covers only the steps before LVGL) stayed silent. The
// loop now names every step, including LVGL, Web Admin, MQTT post-connect and
// the network update, so a stall is reported with its step, the Web Admin
// request and task backtraces. It is logging only and active only on the
// exact Guition S3 profile.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const header = read('src/core/diagnostics/loop_stall.h');
assert.match(header, /#if defined\(DEVICE_GUITION_ESP32_4848S040\)\n#define HOMETILES_LOOP_STALL_DIAGNOSTICS 1\n#else\n#define HOMETILES_LOOP_STALL_DIAGNOSTICS 0/,
  'active only on the exact Guition S3 profile');
for (const fn of ['begin()', 'enter(Step)', 'webRequestBegin(const char*, const char*)', 'webRequestEnd()',
  'webUploadBegin(const char*)', 'webIdle()', 'sampleNetwork()']) {
  assert.ok(header.includes(`inline void ${fn} {}`), `other profiles compile ${fn} to nothing`);
}

const source = read('src/core/diagnostics/loop_stall.cpp');
assert.match(source, /^#include "src\/core\/diagnostics\/loop_stall.h"\n\n#if HOMETILES_LOOP_STALL_DIAGNOSTICS\n/);
assert.match(source, /args\.dispatch_method = ESP_TIMER_TASK;/, 'the check runs outside the loop task');
assert.match(source, /esp_backtrace_print_all_tasks\(kBacktraceDepth\)/, 'task backtraces name the blocking call');
assert.match(source, /constexpr uint8_t kMaxBacktraces = 3;/, 'backtraces are bounded per boot');
assert.match(source, /if \(!upload && stuck_ms >= kBacktraceStuckMs/, 'a long upload does not use up the backtraces');
assert.match(source, /WIFI_EVENT_STA_BEACON_TIMEOUT/);
assert.match(source, /esp_wifi_get_ps\(&ps\)/, 'the power save mode is read back from the driver');
assert.match(source, /esp_wifi_get_max_tx_power\(&tx\)/, 'the TX power is read back from the driver');
assert.doesNotMatch(source, /esp_wifi_set_|WiFi\.set|setWifiPowerSaving/, 'logging only: no Wi-Fi setting changes');

// Every loop step is marked, in loop order.
const loop = cppFunctionDefinitions(read('HomeTiles.ino')).find(f => f.name === 'loop').source;
let at = 0;
for (const [step, next] of [
  ['Top', 'if (hotspot_mode_change_pending)'],
  ['OtaWeb', 'if (webAdminServer.isRunning()) webAdminServer.handle();'],
  ['Board', 'BoardHAL::update();'],
  ['AccessPoint', 'if (webAdminServer.isRunning()) webAdminServer.stop();'],
  ['Power', 'powerManager.update(displayManager.getLastActivityTime());'],
  ['SleepNetwork', 'networkManager.update();'],
  ['SleepWeb', 'if (webAdminServer.isRunning()) webAdminServer.handle();'],
  ['SleepMqtt', 'mqttServicePostConnect();'],
  ['SleepTiles', 'process_tile_update_queues<TileUpdateBudget::DrainAll>();'],
  ['SleepTouch', 'BoardHAL::getTouch(&tp)'],
  ['PreLvgl', 'process_popup_open();'],
  ['Lvgl', 'lv_timer_handler();'],
  ['FolderSwitch', 'tiles_process_pending_folder_switch();'],
  ['WebAdmin', 'if (webAdminServer.isRunning()) webAdminServer.handle();'],
  ['PostConnect', 'mqttServicePostConnect();'],
  ['Services', 'command_channel::service();'],
  ['MqttInbound', 'mqtt_process_inbound_queue('],
  ['DynamicSlots', 'mqttServiceDynamicSlotsReload();'],
  ['NetworkUpdate', 'networkManager.update();'],
  ['Status', 'uiManager.updateStatusbar();'],
]) {
  const mark = loop.indexOf(`loop_stall::enter(loop_stall::Step::${step});`, at);
  assert.ok(mark >= at, `${step} is marked after the previous step`);
  assert.ok(loop.indexOf(next, mark) > mark, `${step} precedes ${next}`);
  at = mark;
}
assert.match(read('HomeTiles.ino'), /displayManager\.resetActivityTimer\(\);\n  loop_stall::begin\(\);/);

// Web Admin names the request (middleware) and uploads, and clears it when
// the server returns to the loop; all only on the S3 diagnostics build.
const web = read('src/web/server/web_admin.cpp');
assert.match(web, /#if HOMETILES_LOOP_STALL_DIAGNOSTICS\n    \/\/ Names the request[^\n]*\n    server\.addMiddleware\(/);
assert.equal(web.match(/#if HOMETILES_LOOP_STALL_DIAGNOSTICS\n        if \(first_chunk\) loop_stall::webUploadBegin\(this->server\.uri\(\)\.c_str\(\)\);\n#endif/g)?.length, 2);
assert.match(web, /server\.handleClient\(\);\n  loop_stall::webIdle\(\);/);

console.log('The S3 loop names every step; stalls are reported with the Web request and backtraces.');

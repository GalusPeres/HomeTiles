// Web Admin storage work holds the built-in camera off. Interrupt watchdog
// resets (b18, b22, b23, Guition V2) happened while the camera streamed and
// a Web Admin tile save or the layout reload after it read the flash: every
// flash operation needs the other core, which was busy with interrupts
// masked. The storage routes now stop a running stream (reason "storage",
// the session survives), abort a snapshot, wait a bounded time, and keep the
// camera off for a few more seconds (b23 crashed during the reload about one
// second after a save, with the stream already running again); the next
// keepalive then resumes the same session.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const routes = read('src/web/server/web_admin.cpp');
const api = read('src/video/local_camera/local_camera.h');
const service = read('src/video/local_camera/local_camera.cpp');
const contract = read('src/video/local_camera/local_camera_stream_contract.h');

// --- Routes -------------------------------------------------------------------
assert.match(routes, /#include "src\/video\/local_camera\/local_camera\.h"/);
assert.match(routes, /auto withStorageHold\(Handler handler\) \{\s*return \[handler\]\(\) \{\s*local_camera::ScopedStorageHold hold;\s*handler\(\);\s*\};/);
const held = ['handleSaveMQTT', 'handleSaveBridge', 'handleSaveTiles', 'handleReorderTiles',
  'handleSaveFolderAccess', 'handleDeleteFolder', 'handleSaveScreensaver', 'handleSaveHardwareIo',
  'handleSaveTileBorders', 'handleFileManagerDelete', 'handleFileManagerRename',
  'handleFileManagerMkdir', 'handleCoreDumpErase'];
for (const handler of held) {
  assert.match(routes, new RegExp(`withStorageHold\\(\\[this\\]\\(\\) \\{ this->${handler}\\(\\); \\}\\)\\)`),
    `${handler} runs with the camera held off`);
}
// Tile radius: only the save (not the live preview) touches the flash.
assert.match(routes, /server\.on\("\/api\/display\/tile-radius", HTTP_POST, \[this\]\(\) \{[\s\S]*?local_camera::ScopedStorageHold hold\(server\.arg\("preview"\) != "1"\);\s*handleTileRadius\(\);/);
// Reads, status polls and the camera's own settings never stop the stream.
for (const handler of ['handleGetTiles', 'handleStatus', 'handleLocalCamera', 'handleGetFolders',
                       'handleCreateScreenshot']) {
  assert.doesNotMatch(routes, new RegExp(`withStorageHold\\(\\[this\\]\\(\\) \\{ this->${handler}\\(`),
    `${handler} must not hold the camera`);
}

// --- API ------------------------------------------------------------------------
assert.match(api, /void beginStorageHold\(\);\s*void endStorageHold\(\);\s*class ScopedStorageHold \{/);
assert.match(api, /explicit ScopedStorageHold\(bool active = true\) : active_\(active\) \{\s*if \(active_\) beginStorageHold\(\);/);
assert.match(api, /~ScopedStorageHold\(\) \{\s*if \(active_\) endStorageHold\(\);/);

// --- Service ---------------------------------------------------------------------
const fns = cppFunctionDefinitions(service);
const body = name => {
  const found = fns.find(item => item.name === name);
  assert.ok(found, `${name} must exist`);
  return found.body;
};
const begin = body('beginStorageHold');
assert.match(begin, /if \(g_storage_holds\.fetch_add\(1\) != 0\) return;/, 'Nested holds wait only once');
assert.match(begin, /requestStreamStop\(StopReason::Storage\);/);
assert.match(begin, /g_stream_running\.load\(\) \|\| g_stream_wanted\.load\(\) \|\| g_capture_busy\.load\(\) \|\|\s*local_camera_upload::busy\(\)/,
  'Waits for the stream run, a snapshot and the sender');
assert.match(begin, /static_cast<uint32_t>\(millis\(\) - started_ms\) < kStorageHoldWaitMs/, 'Bounded wait');
assert.match(begin, /vTaskDelay\(pdMS_TO_TICKS\(10\)\);/);
assert.match(service, /constexpr uint32_t kStorageHoldWaitMs = 2000;/);
const end = body('endStorageHold');
assert.match(end, /if \(g_storage_holds\.load\(\) == 0\) return;\s*if \(g_storage_holds\.fetch_sub\(1\) != 1\) return;/);
assert.match(end, /const uint32_t until = millis\(\) \+ kStorageLingerMs;\s*g_storage_linger_until_ms\.store\(until \? until : 1\);/,
  'The hold lingers after the last storage work');
assert.doesNotMatch(end, /tryStartStream/, 'No immediate restart: the reload after a save reads the flash too');
assert.match(service, /constexpr uint32_t kStorageLingerMs = 5000;/);
assert.match(body('storageHoldActive'), /if \(g_storage_holds\.load\(\) != 0\) return true;[\s\S]*static_cast<int32_t>\(until - millis\(\)\) > 0/);
// A snapshot in progress aborts, new snapshot requests are busy, keepalives
// defer through the gate.
assert.match(body('abortRequested'), /storageHoldActive\(\)/);
assert.match(body('handleMqttMessage'), /g_stream_running\.load\(\) \|\| g_stream_wanted\.load\(\) \|\|\s*storageHoldActive\(\)\) \{/);
assert.match(body('currentGate'), /in\.storage_hold = storageHoldActive\(\);/);

// --- Contract ----------------------------------------------------------------------
assert.match(contract, /Storage,\s*\/\/ Web Admin storage work/);
assert.match(contract, /case StopReason::Storage: return "storage";/);
assert.match(contract, /if \(in\.storage_hold\) return StopReason::Storage;/);
{
  const ends = contract.slice(contract.indexOf('inline bool reasonEndsSession'));
  assert.doesNotMatch(ends.slice(0, ends.indexOf('}')), /Storage/, 'Storage work keeps the session');
}
// Appended last: existing numeric reasons are unchanged.
{
  const enumBody = contract.slice(contract.indexOf('enum class StopReason'), contract.indexOf('};', contract.indexOf('enum class StopReason')));
  const names = [...enumBody.matchAll(/^\s+(\w+),/gm)].map(match => match[1]);
  assert.equal(names.at(-1), 'Storage');
  assert.equal(names.indexOf('Error'), names.length - 2);
}

console.log('Local camera storage hold for Web Admin flash work OK');

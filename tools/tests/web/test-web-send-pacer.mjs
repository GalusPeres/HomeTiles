// lwIP keeps unsent and unacknowledged TCP data in internal RAM, up to 64 KB
// per socket on the P4 (CONFIG_LWIP_TCP_SND_BUF_DEFAULT, lwIP buffers not in
// PSRAM). The Web Admin wrote large responses in 512-byte chunks every 2 ms,
// faster than Wi-Fi sent them: the internal heap fell from 92 to 14 KB during
// /api/tiles (V2 b598) and 181 -> 43 KB for the page (b599), and the panel
// flashed blue for a moment. Both chunked senders now pause while their
// response holds more than 24 KB of internal RAM, bounded per chunk and per
// response, and a response that had to wait says so once in English.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

const header = read('src/web/server/web_admin_utils.h');
assert.ok(header.includes('class WebSendPacer {') &&
  header.includes('static constexpr uint32_t kInFlightBytes = 24U * 1024U;'), 'the bound is 24 KB');

const utils = read('src/web/server/web_admin_utils.cpp');
assert.ok(utils.includes('#include <esp_heap_caps.h>'));
const ctor = utils.slice(utils.indexOf('WebSendPacer::WebSendPacer()'), utils.indexOf('void WebSendPacer::wait()'));
assert.ok(ctor.includes('heap_caps_get_free_size(MALLOC_CAP_INTERNAL)'), 'measured against the free internal RAM at the start');
const wait = utils.slice(utils.indexOf('void WebSendPacer::wait()'), utils.indexOf('WebSendPacer::~WebSendPacer()'));
assert.ok(wait.includes('if (free_now + kInFlightBytes >= start_free_) break;'), 'goes on once the network drained');
assert.ok(wait.includes('constexpr uint32_t kChunkWaitMs = 200;') && wait.includes('constexpr uint32_t kResponseWaitMs = 2000;') &&
  wait.includes('if (waited >= kChunkWaitMs || waited_ms_ + waited >= kResponseWaitMs) break;') && wait.includes('delay(2);'),
  'bounded per chunk and per response, yielding while it waits');
const dtor = utils.slice(utils.indexOf('WebSendPacer::~WebSendPacer()'));
assert.ok(dtor.includes('if (waited_ms_ < 10) return;') &&
  dtor.includes('"[WebAdmin] Response paced for the network: waited %lu ms, internal free %u -> %u KB\\n"'),
  'one English line when a response had to wait');

// Both chunked senders pace every chunk.
const chunked = fn(utils, 'sendChunkedResponse');
assert.ok(chunked.indexOf('WebSendPacer pacer;') < chunked.indexOf('for (size_t offset = 0;') &&
  /server\.sendContent\(body\.substring\(offset, end\)\);\s*delay\(2\);\s*yield\(\);\s*pacer\.wait\(\);/.test(chunked),
  'pages and API answers');
const assets = read('src/web/server/assets/web_admin_assets.cpp');
assert.ok(assets.includes('#include "src/web/server/web_admin_utils.h"'));
const asset = fn(assets, 'sendGzipProgmemAsset');
assert.ok(asset.indexOf('WebSendPacer pacer;') < asset.indexOf('for (size_t offset = 0;') &&
  /delay\(2\);\s*yield\(\);\s*pacer\.wait\(\);/.test(asset), 'the Web Admin script and style files');
console.log('Web Admin: large responses wait for the network instead of filling internal RAM');

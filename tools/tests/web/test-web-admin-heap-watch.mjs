// After a restart the Web Admin loaded its folders and the lowest internal
// heap since boot fell to 19 KB (V2 b596; 134 KB for the same requests in
// b591). Every request whose handler lowers that minimum by 16 KB or more is
// logged with its path, so the request is named instead of guessed. b597
// read the path after handleClient(), when the server had cleared it already
// (it printed none): the watch runs around the handler now.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const admin = readRepoFile('src/web/server/web_admin.cpp').replace(/\r\n?/g, '\n');
assert.ok(admin.includes('static constexpr uint32_t kHeapDropBytes = 16U * 1024U;'));

const start = admin.slice(admin.indexOf('bool WebAdminServer::start() {'), admin.indexOf('#if HOMETILES_LOOP_STALL_DIAGNOSTICS'));
const watch = start.slice(start.indexOf('server.addMiddleware('));
assert.ok(watch.indexOf('const uint32_t lowest_before = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);') <
  watch.indexOf('const bool handled = next();'), 'the minimum is read before the handler');
assert.ok(watch.indexOf('const bool handled = next();') <
  watch.indexOf('const uint32_t lowest_after = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);'), 'and after it');
assert.ok(watch.indexOf('const String path = web.uri();') >= 0 &&
  watch.indexOf('const String path = web.uri();') < watch.indexOf('const bool handled = next();'),
  'the path is read before the handler (b598 read it after next() and printed none)');
assert.ok(watch.includes('if (lowest_after + kHeapDropBytes < lowest_before) {') &&
  watch.includes('"[Mem] Web Admin request %s lowered the internal minimum %u -> %u KB in %lu ms"') &&
  watch.includes('path.c_str()') && !watch.includes('web.uri().c_str()'),
  'a handler that lowers it by 16 KB is named in English, with its path');
assert.ok(!start.slice(0, start.indexOf('server.addMiddleware(')).includes('#if'), 'on every profile');

const handle = admin.slice(admin.indexOf('void WebAdminServer::handle() {'), admin.indexOf('void WebAdminServer::handleRoot() {'));
assert.ok(!handle.includes('server.uri()'), 'never the path after handleClient(): it is cleared there');
assert.ok(handle.includes('if (!g_heap_drop_named && lowest_after + kHeapDropBytes < lowest_before) {') &&
  handle.includes('outside a request handler'), 'a drop outside any handler is logged once');
console.log('Web Admin: a request handler that lowers the internal heap minimum is logged with its path');

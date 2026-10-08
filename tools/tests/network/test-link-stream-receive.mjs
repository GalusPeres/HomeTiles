// Streams from the Bridge over the link (docs-dev/bridge-link.md): pictures
// above the normal 64 KB message (covers, the screensaver image) arrive as
// begin, data and end frames, other frames in between. The panel announces
// its room in the session hello, collects a stream in PSRAM, drops one it has
// no room for without ending the connection, and hands only image topics to
// the inbound queue, without a copy.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const client = readRepoFile('src/network/link/bridge_link_client.cpp');
const body = name => {
  const start = client.indexOf(name);
  assert.ok(start >= 0, `missing ${name}`);
  const end = client.indexOf('\n}\n', start);
  return client.slice(start, end);
};

assert.match(body('bool BridgeLinkClient::connect('),
  /hello, sizeof\(hello\), session && stream_callback_ \? stream_limit_ : 0\);/,
  'only a session with a receiver announces its room');
assert.match(body('bool BridgeLinkClient::handleFrame('),
  /case kStreamBegin:\s*case kStreamData:\s*case kStreamEnd:\s*return handleStream\(type, payload, payload_length\);/);

const stream = body('bool BridgeLinkClient::handleStream(');
assert.match(stream, /if \(incoming_ \|\| !parseStreamBegin\(payload, length, begin\)\) \{\s*fail\(kProtocolError\);/,
  'a nested or malformed begin ends the connection');
assert.match(stream, /incoming_skip_ = pair_mode_ \|\| !stream_callback_ \|\| begin\.total > stream_limit_;/,
  'a stream without room is skipped, not fatal');
assert.match(stream, /heap_caps_malloc\(begin\.total, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT\)/, 'collected in PSRAM');
assert.match(stream, /if \(!incoming_ \|\| length > kMaxStreamChunk \|\| incoming_have_ \+ length > incoming_total_\) \{\s*fail\(kProtocolError\);/,
  'data beyond the announced total ends the connection');
assert.match(stream, /if \(!incoming_ \|\| length != 0 \|\| incoming_have_ != incoming_total_\) \{\s*fail\(kProtocolError\);/,
  'a short stream ends the connection');
assert.match(stream, /incoming_data_ = nullptr;  \/\/ The receiver owns it now\.\s*dropIncoming\(\);\s*stream_callback_\(incoming_topic_, data, total, incoming_retain_\);/,
  'the receiver takes the block');
assert.match(body('void BridgeLinkClient::fail('), /dropIncoming\(\);/, 'a broken connection frees a half stream');

const handlers = readRepoFile('src/network/mqtt/mqtt_handlers.cpp');
const streamCallback = handlers.slice(handlers.indexOf('void mqttStreamCallback('),
  handlers.indexOf('// ========== MQTT message processing'));
assert.match(streamCallback, /!strstr\(topic, "\/image\/"\)/, 'only image topics may be this large');
assert.match(streamCallback, /msg->payload = data;[\s\S]*msg->external = data;/, 'the payload is not copied');
assert.match(streamCallback, /mqttFreeInbound\(msg\);/);
const freeInbound = handlers.slice(handlers.indexOf('static void mqttFreeInbound('),
  handlers.indexOf('\n}\n', handlers.indexOf('static void mqttFreeInbound(')));
assert.match(freeInbound, /if \(msg->external\) heap_caps_free\(msg->external\);\s*heap_caps_free\(msg\);/);
assert.ok(!/heap_caps_free\((msg|deferred|g_deferred_bridge_apply)\)/.test(handlers.replace(freeInbound, '')),
  'every queued message is freed with its own payload block');

const manager = readRepoFile('src/network/network_manager.cpp');
assert.match(manager, /constexpr size_t kStreamReceiveLimit = 512 \* 1024;\s*#else\s*constexpr size_t kStreamReceiveLimit = 256 \* 1024;/,
  'P4 panels take 512 KB, the S3 256 KB');
assert.match(manager, /mqtt_client\.setStreamReceive\(kStreamReceiveLimit,[\s\S]*?mqttStreamCallback\(topic, data, length\);/);

console.log('Link streams: announced room, PSRAM collection, skipped when too large, image topics only');

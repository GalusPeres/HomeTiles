// Direct Bridge link: runs the production core (src/network/link/bridge_link_core.h)
// on the host and checks every byte against an independent Node/OpenSSL
// implementation of docs-dev/bridge-link.md. The fixed vectors are shared with
// HomeTiles Bridge tests/test_link_protocol.py.
import assert from 'node:assert/strict';
import {createCipheriv, createHash, hkdfSync} from 'node:crypto';

import {compileAndRun} from '../../lib/cpp-host.mjs';

const VECTOR = {
  K: 'b925def556256ead767b0f1d14879e50d6bddbd0bb44dc0d2435e4af011b3e19',
  id: 'A1B2C3D4E5F6',
  base: 'hometiles/test',
  n_p: '11'.repeat(16),
  n_b: '22'.repeat(16),
  salt: 'e2f7cbd13441d013eac632f477cc4089426d29a8b9d80d78bb1c9122521a344b',
  panel: '858d17321dd91836bffc192068a51f76cf9307dbc5686201abd6fefdcc63fbc0',
  bridge: '2857758680e582927ad615da7c129064288311e90d6d9555594917fca8137514',
  panel_ready: '00000011b230093592018101bedfa930245277e0b4',
  bridge_ready: '0000001156346d09190796699a0f2ce96c6763226c',
  bridge_publish: '0000002f0bbee66972e68a3cfe3a03196924acb3502794088cbb1936a4b7b7f09ee7649f85bf839a2f0d7001a5f70e8b9883f3',
  hello: '{"v":1,"id":"A1B2C3D4E5F6","base":"hometiles/test","mode":"session","kid":"20a8108ed11215c5","n":"11111111111111111111111111111111"}',
};
const hex = value => Buffer.from(value, 'hex');
const u16 = value => Buffer.from([value >> 8, value & 0xff]);

// Independent derivation of the whole chain.
{
  const id = Buffer.from(VECTOR.id);
  const base = Buffer.from(VECTOR.base);
  const salt = createHash('sha256').update(Buffer.concat([
    Buffer.from('HomeTiles link v1'), u16(id.length), id, u16(base.length), base, hex(VECTOR.n_p), hex(VECTOR.n_b),
  ])).digest();
  assert.equal(salt.toString('hex'), VECTOR.salt);
  const derive = info => Buffer.from(hkdfSync('sha256', hex(VECTOR.K), salt, Buffer.from(info), 32)).toString('hex');
  assert.equal(derive('HomeTiles link panel-to-bridge v1'), VECTOR.panel);
  assert.equal(derive('HomeTiles link bridge-to-panel v1'), VECTOR.bridge);
  const seal = (key, counter, type, payload) => {
    const nonce = Buffer.alloc(12);
    nonce.writeBigUInt64BE(BigInt(counter), 4);
    const header = Buffer.alloc(4);
    header.writeUInt32BE(1 + payload.length + 16);
    const cipher = createCipheriv('chacha20-poly1305', hex(key), nonce, {authTagLength: 16});
    cipher.setAAD(header, {plaintextLength: 1 + payload.length});
    const body = Buffer.concat([cipher.update(Buffer.concat([Buffer.from([type]), payload])), cipher.final(), cipher.getAuthTag()]);
    return Buffer.concat([header, body]).toString('hex');
  };
  assert.equal(seal(VECTOR.panel, 0, 0x04, Buffer.alloc(0)), VECTOR.panel_ready);
  assert.equal(seal(VECTOR.bridge, 0, 0x04, Buffer.alloc(0)), VECTOR.bridge_ready);
  const topic = Buffer.from('hometiles/test/stat/value');
  const publish = Buffer.concat([Buffer.from([1]), u16(topic.length), topic, Buffer.from('42')]);
  assert.equal(seal(VECTOR.bridge, 1, 0x10, publish), VECTOR.bridge_publish);
}

const harness = String.raw`
#include <cstdio>
#include <cstring>
#include <vector>
#include "src/network/link/bridge_link_core.h"

using namespace bridge_link;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static void show(const char* tag, const uint8_t* data, size_t size) {
  std::vector<char> text(2 * size + 1);
  ht_crypto::hexEncode(data, size, text.data(), text.size());
  std::printf("%s %s\n", tag, text.data());
}

int main() {
  auto bytes = [](const char* text, uint8_t* out, size_t size) {
    return ht_crypto::hexDecode(text, std::strlen(text), out, size);
  };
  uint8_t key[32], n_p[16], n_b[16];
  CHECK(bytes("${VECTOR.K}", key, 32) && bytes("${VECTOR.n_p}", n_p, 16) && bytes("${VECTOR.n_b}", n_b, 16));

  uint8_t salt[32], panel[32], bridge[32];
  sessionSalt("${VECTOR.id}", "${VECTOR.base}", n_p, n_b, salt);
  show("salt", salt, 32);
  sessionKeys(key, "${VECTOR.id}", "${VECTOR.base}", n_p, n_b, panel, bridge);
  show("panel", panel, 32);
  show("bridge", bridge, 32);

  // Sealed frames: the panel's ready, then the Bridge's ready and publish.
  uint8_t frame[256];
  size_t size = finishFrame(panel, 0, kReady, 0, frame, sizeof(frame));
  CHECK(size == sealedFrameSize(0));
  show("panel_ready", frame, size);
  size = finishFrame(bridge, 0, kReady, 0, frame, sizeof(frame));
  show("bridge_ready", frame, size);
  const char* topic = "hometiles/test/stat/value";
  size_t head = publishHeader(topic, std::strlen(topic), true, frame + kHeaderSize + 1, sizeof(frame) - 5);
  CHECK(head == 3 + std::strlen(topic));
  std::memcpy(frame + kHeaderSize + 1 + head, "42", 2);
  size = finishFrame(bridge, 1, kPublish, head + 2, frame, sizeof(frame));
  CHECK(size == sealedFrameSize(head + 2));
  show("bridge_publish", frame, size);

  // Opening in place: the right counter only, never with the other key.
  uint8_t copy[256];
  size_t payload_length = 0;
  std::memcpy(copy, frame, size);
  CHECK(frameLength(copy) == size - kHeaderSize);
  CHECK(!openFrame(bridge, 0, copy, copy + kHeaderSize, size - kHeaderSize, &payload_length));
  std::memcpy(copy, frame, size);
  CHECK(!openFrame(panel, 1, copy, copy + kHeaderSize, size - kHeaderSize, &payload_length));
  std::memcpy(copy, frame, size);
  copy[10] ^= 1;
  CHECK(!openFrame(bridge, 1, copy, copy + kHeaderSize, size - kHeaderSize, &payload_length));
  std::memcpy(copy, frame, size);
  CHECK(openFrame(bridge, 1, copy, copy + kHeaderSize, size - kHeaderSize, &payload_length));
  CHECK(copy[kHeaderSize] == kPublish && payload_length == head + 2);
  Publish publish;
  CHECK(parsePublish(copy + kHeaderSize + 1, payload_length, publish));
  CHECK(publish.retain && publish.topic_length == std::strlen(topic));
  CHECK(std::memcmp(publish.topic, topic, publish.topic_length) == 0);
  CHECK(publish.data_length == 2 && std::memcmp(publish.data, "42", 2) == 0);

  // Plain frames for the handshake and pair mode.
  size = finishFrame(nullptr, 0, kPing, 0, frame, sizeof(frame));
  CHECK(size == 5 && std::memcmp(frame, "\x00\x00\x00\x01\x20", 5) == 0);
  CHECK(finishFrame(nullptr, 0, kPublish, kMaxFrameLength, frame, sizeof(frame)) == 0);

  // Hello: the shared vector, pair mode and rejected input.
  char hello[kMaxHelloLength + 1];
  CHECK(buildHello("${VECTOR.id}", "${VECTOR.base}", "20a8108ed11215c5", n_p, hello, sizeof(hello)) > 0);
  std::printf("hello %s\n", hello);
  CHECK(buildHello("${VECTOR.id}", "${VECTOR.base}", nullptr, nullptr, hello, sizeof(hello)) > 0);
  CHECK(std::strcmp(hello, "{\"v\":1,\"id\":\"A1B2C3D4E5F6\",\"base\":\"hometiles/test\",\"mode\":\"pair\"}") == 0);
  CHECK(buildHello("bad id", "base", nullptr, nullptr, hello, sizeof(hello)) == 0);
  CHECK(buildHello("ID", "a/+/b", nullptr, nullptr, hello, sizeof(hello)) == 0);
  CHECK(buildHello("ID", "a\"b", nullptr, nullptr, hello, sizeof(hello)) == 0);
  CHECK(buildHello("ID", "base", "short", n_p, hello, sizeof(hello)) == 0);
  CHECK(buildHello("ID", "base", nullptr, nullptr, hello, 10) == 0);
  // The room for streams from the Bridge rides on the session hello only.
  CHECK(buildHello("${VECTOR.id}", "${VECTOR.base}", "20a8108ed11215c5", n_p, hello, sizeof(hello), 524288) > 0);
  CHECK(std::strcmp(hello, "{\"v\":1,\"id\":\"A1B2C3D4E5F6\",\"base\":\"hometiles/test\",\"mode\":\"session\","
                           "\"kid\":\"20a8108ed11215c5\",\"n\":\"11111111111111111111111111111111\",\"rx\":524288}") == 0);
  CHECK(buildHello("${VECTOR.id}", "${VECTOR.base}", nullptr, nullptr, hello, sizeof(hello), 524288) > 0);
  CHECK(std::strstr(hello, "rx") == nullptr);
  CHECK(buildHello("ID", "base", "20a8108ed11215c5", n_p, hello, sizeof(hello), kMaxPayloadLength) == 0);
  CHECK(buildHello("ID", "base", "20a8108ed11215c5", n_p, hello, sizeof(hello), kMaxStreamLength + 1) == 0);

  // Stream begin from the Bridge (link_protocol.stream_begin_payload).
  StreamBegin begin;
  const uint8_t stream_begin[] = {0x01, 0x00, 0x03, 'a', '/', 'b', 0x00, 0x08, 0x00, 0x00};
  CHECK(parseStreamBegin(stream_begin, sizeof(stream_begin), begin));
  CHECK(begin.retain && begin.topic_length == 3 && std::memcmp(begin.topic, "a/b", 3) == 0 &&
        begin.total == 524288);
  const uint8_t stream_empty[] = {0x00, 0x00, 0x01, 'a', 0x00, 0x00, 0x00, 0x00};
  CHECK(!parseStreamBegin(stream_empty, sizeof(stream_empty), begin));
  const uint8_t stream_huge[] = {0x00, 0x00, 0x01, 'a', 0x00, 0x20, 0x00, 0x01};
  CHECK(!parseStreamBegin(stream_huge, sizeof(stream_huge), begin));
  const uint8_t stream_short[] = {0x00, 0x00, 0x01, 'a', 0x00, 0x00, 0x10};
  CHECK(!parseStreamBegin(stream_short, sizeof(stream_short), begin));
  const uint8_t stream_wild[] = {0x00, 0x00, 0x01, '#', 0x00, 0x00, 0x10, 0x00};
  CHECK(!parseStreamBegin(stream_wild, sizeof(stream_wild), begin));

  // Welcome and refuse from the Bridge (link_protocol.build_welcome/refuse).
  uint8_t nonce[16];
  const char* welcome = "{\"v\":1,\"n\":\"22222222222222222222222222222222\"}";
  CHECK(parseWelcome(welcome, std::strlen(welcome), true, nonce));
  CHECK(std::memcmp(nonce, n_b, 16) == 0);
  CHECK(parseWelcome("{\"v\":1}", 7, false, nonce));
  CHECK(!parseWelcome("{\"v\":1}", 7, true, nonce));
  CHECK(!parseWelcome("{\"v\":12}", 8, false, nonce));
  CHECK(!parseWelcome("{\"v\":2,\"n\":\"22222222222222222222222222222222\"}", 45, true, nonce));
  CHECK(!parseWelcome("{\"v\":1,\"n\":\"2222\"}", 18, true, nonce));
  char reason[kReasonSize];
  parseRefuse("{\"v\":1,\"r\":\"unknown\"}", 21, reason);
  CHECK(std::strcmp(reason, "unknown") == 0);
  parseRefuse("{\"v\":1,\"r\":\"Bad!\"}", 18, reason);
  CHECK(std::strcmp(reason, "invalid") == 0);
  parseRefuse("garbage", 7, reason);
  CHECK(std::strcmp(reason, "invalid") == 0);

  // Publish parsing limits.
  const uint8_t bad_flags[] = {0x02, 0x00, 0x01, 'a'};
  CHECK(!parsePublish(bad_flags, sizeof(bad_flags), publish));
  const uint8_t short_topic[] = {0x00, 0x00, 0x05, 'a', 'b'};
  CHECK(!parsePublish(short_topic, sizeof(short_topic), publish));
  const uint8_t wildcard[] = {0x00, 0x00, 0x03, 'a', '/', '#'};
  CHECK(!parsePublish(wildcard, sizeof(wildcard), publish));
  const uint8_t empty_payload[] = {0x00, 0x00, 0x01, 'a'};
  CHECK(parsePublish(empty_payload, sizeof(empty_payload), publish) && publish.data_length == 0 && !publish.retain);
  CHECK(!validTopic("", 0) && validTopic("a/b", 3));
  uint8_t header[4] = {0, 0, 0, 0};
  CHECK(frameLength(header) == 0);
  putU32(header, kMaxFrameLength + 1);
  CHECK(frameLength(header) == 0);
  putU32(header, kMaxFrameLength);
  CHECK(frameLength(header) == kMaxFrameLength);
  std::printf("ok\n");
  return 0;
}
`;

const output = compileAndRun({
  label: 'bridge link core',
  harness,
  sources: ['src/core/security/ht_crypto.cpp'],
});
if (output !== null) {
  const values = Object.fromEntries(output.trim().split('\n').map(line => {
    const space = line.indexOf(' ');
    return space < 0 ? [line, ''] : [line.slice(0, space), line.slice(space + 1)];
  }));
  assert.equal(values.ok, '');
  for (const key of ['salt', 'panel', 'bridge', 'panel_ready', 'bridge_ready', 'bridge_publish', 'hello']) {
    assert.equal(values[key], VECTOR[key], key);
  }
  console.log('PASS: bridge link core matches the shared vectors and the Node implementation');
}

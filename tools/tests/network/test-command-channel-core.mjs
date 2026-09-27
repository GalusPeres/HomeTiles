// Encrypted Bridge commands: runs the production core
// (src/network/secure/command_channel_core.h) on the host and checks every
// byte against an independent Node/OpenSSL implementation of the protocol in
// docs-dev/command-encryption.md. The fixed vector at the end is shared with
// HomeTiles Bridge tests/test_command_channel.py.
import assert from 'node:assert/strict';
import {createCipheriv, createDecipheriv, hkdfSync} from 'node:crypto';

import {compileAndRun} from '../../lib/cpp-host.mjs';

const CODE = 'ABCDE-FGHJK-MNPQR-STVWX-YZ012';
const CANONICAL = CODE.replaceAll('-', '');
const SALT = 'HomeTiles command pairing v1';
const derive = info => Buffer.from(hkdfSync('sha256', Buffer.from(CANONICAL), Buffer.from(SALT), Buffer.from(info), info === 'key-id' ? 8 : 32));
const panelKey = derive('panel-to-bridge');
const bridgeKey = derive('bridge-to-panel');
const keyId = derive('key-id').toString('hex');
const TOPIC_PANEL = 'hometiles/secure/panel';
const TOPIC_BRIDGE = 'hometiles/secure/bridge';
const SESSION = '00112233445566778899aabbccddeeff';
const NONCE = Buffer.from('0102030405060708090a0b0c', 'hex');

function seal(key, topic, plaintext, nonce = NONCE) {
  const cipher = createCipheriv('chacha20-poly1305', key, nonce, {authTagLength: 16});
  cipher.setAAD(Buffer.from(topic), {plaintextLength: plaintext.length});
  const data = Buffer.concat([cipher.update(plaintext), cipher.final(), cipher.getAuthTag()]);
  return JSON.stringify({v: 1, k: keyId, n: nonce.toString('hex'), d: data.toString('hex')});
}

function open(key, topic, envelope) {
  const parsed = JSON.parse(envelope);
  assert.equal(parsed.v, 1);
  assert.equal(parsed.k, keyId);
  const data = Buffer.from(parsed.d, 'hex');
  const decipher = createDecipheriv('chacha20-poly1305', key, Buffer.from(parsed.n, 'hex'), {authTagLength: 16});
  decipher.setAAD(Buffer.from(topic), {plaintextLength: data.length - 16});
  decipher.setAuthTag(data.subarray(data.length - 16));
  return Buffer.concat([decipher.update(data.subarray(0, data.length - 16)), decipher.final()]);
}

const bridgeSession = seal(bridgeKey, TOPIC_BRIDGE, Buffer.from(`session ${SESSION} 0 ffeeddccbbaa99887766554433221100\n`));
const bridgeData = seal(bridgeKey, TOPIC_BRIDGE, Buffer.from(`data ${SESSION} 7 camera\n{"status":"ready","url":"tcp://h:1/t0k3n"}`));
const wrongTopic = seal(bridgeKey, 'other/secure/bridge', Buffer.from(`data ${SESSION} 8 camera\n{}`));
const wrongKey = seal(panelKey, TOPIC_BRIDGE, Buffer.from(`data ${SESSION} 9 camera\n{}`));

const harness = String.raw`
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include "src/network/secure/command_channel_core.h"

using namespace command_channel;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static std::string line() { std::string s; std::getline(std::cin, s); return s; }

int main() {
  // Pairing code generation, formatting and tolerant input.
  uint8_t random[kCodeLength];
  for (size_t i = 0; i < kCodeLength; ++i) random[i] = static_cast<uint8_t>(i * 37 + 200);
  char code[kCodeLength + 1];
  generateCode(random, code);
  for (size_t i = 0; i < kCodeLength; ++i) CHECK(std::strchr(kAlphabet, code[i]) != nullptr);
  char shown[kCodeDisplaySize];
  formatCode(code, shown);
  CHECK(std::strlen(shown) == 29 && shown[5] == '-' && shown[23] == '-');
  char canonical[kCodeLength + 1];
  CHECK(normalizeCode("abcde fghjk-mnpqr stvwx yzo12", canonical));
  CHECK(std::strcmp(canonical, "ABCDEFGHJKMNPQRSTVWXYZ012") == 0);
  CHECK(normalizeCode("ABCDE-FGHJK-MNPQR-STVWX-YZ0I2", canonical) && canonical[23] == '1');
  CHECK(!normalizeCode("ABCDE-FGHJK-MNPQR-STVWX-YZ01", canonical));
  CHECK(!normalizeCode("ABCDE-FGHJK-MNPQR-STVWX-YZ0123", canonical));
  CHECK(!normalizeCode("ABCDE-FGHJK-MNPQR-STVWX-YZ01U", canonical));

  Keys keys;
  CHECK(deriveKeys("ABCDE-FGHJK-MNPQR-STVWX-YZ012", keys));
  char hex[65];
  ht_crypto::hexEncode(keys.panel_to_bridge, 32, hex, sizeof(hex));
  std::printf("pb %s\n", hex);
  ht_crypto::hexEncode(keys.bridge_to_panel, 32, hex, sizeof(hex));
  std::printf("bp %s\n", hex);
  std::printf("kid %s\n", keys.key_id);

  // Panel command: header, sealing with the panel-to-bridge key.
  Header header;
  header.type = MessageType::Command;
  header.has_session = true;
  ht_crypto::hexDecode("00112233445566778899aabbccddeeff", 32, header.session, kSessionSize);
  header.seq = 42;
  std::strcpy(header.name, "light");
  const char* body = "{\"entity_id\":\"light.kitchen\",\"state\":\"toggle\"}";
  static uint8_t plaintext[kMaxPlaintextLength];
  size_t length = buildPlaintext(header, reinterpret_cast<const uint8_t*>(body), std::strlen(body), plaintext, sizeof(plaintext));
  CHECK(length > 0);
  const uint8_t nonce[12] = {1,2,3,4,5,6,7,8,9,10,11,12};
  static uint8_t scratch[kMaxPlaintextLength + 16];
  static char envelope[kMaxEnvelopeLength + 1];
  size_t envelope_length = sealEnvelope(keys.panel_to_bridge, keys.key_id, nonce, "hometiles/secure/panel", plaintext, length, scratch, envelope, sizeof(envelope));
  CHECK(envelope_length == std::strlen(envelope));
  std::printf("cmd %s\n", envelope);

  // Hello without a session.
  Header hello;
  hello.type = MessageType::Hello;
  std::strcpy(hello.name, "ffeeddccbbaa99887766554433221100");
  length = buildPlaintext(hello, nullptr, 0, plaintext, sizeof(plaintext));
  CHECK(length == std::strlen("hello - 0 ffeeddccbbaa99887766554433221100\n"));
  CHECK(std::memcmp(plaintext, "hello - 0 ffeeddccbbaa99887766554433221100\n", length) == 0);

  // Bridge envelopes produced by Node: session, data, wrong topic, wrong key.
  static uint8_t opened[kMaxPlaintextLength];
  size_t opened_length = 0;
  for (int i = 0; i < 4; ++i) {
    const std::string input = line();
    const OpenResult result = openEnvelope(keys.bridge_to_panel, keys.key_id, "hometiles/secure/bridge", input.c_str(), input.size(), scratch, opened, &opened_length);
    if (i < 2) {
      CHECK(result == OpenResult::Ok);
      Header parsed;
      const uint8_t* parsed_body = nullptr;
      size_t body_length = 0;
      CHECK(parsePlaintext(opened, opened_length, parsed, &parsed_body, &body_length));
      std::printf("open %s %u %s %.*s\n", typeName(parsed.type), static_cast<unsigned>(parsed.seq), parsed.name,
                  static_cast<int>(body_length), reinterpret_cast<const char*>(parsed_body));
    } else if (i == 2) {
      CHECK(result == OpenResult::Rejected);
    } else {
      CHECK(result == OpenResult::Rejected);
    }
  }
  // Tampering, a foreign key id and garbage are rejected.
  std::string tampered = envelope;
  tampered[tampered.size() - 5] = tampered[tampered.size() - 5] == '0' ? '1' : '0';
  CHECK(openEnvelope(keys.panel_to_bridge, keys.key_id, "hometiles/secure/panel", tampered.c_str(), tampered.size(), scratch, opened, &opened_length) == OpenResult::Rejected);
  CHECK(openEnvelope(keys.panel_to_bridge, keys.key_id, "hometiles/secure/panel", envelope, envelope_length, scratch, opened, &opened_length) == OpenResult::Ok);
  Keys other;
  CHECK(deriveKeys("ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ-ZZZZZ", other));
  CHECK(openEnvelope(other.panel_to_bridge, other.key_id, "hometiles/secure/panel", envelope, envelope_length, scratch, opened, &opened_length) == OpenResult::OtherKey);
  CHECK(openEnvelope(keys.panel_to_bridge, keys.key_id, "hometiles/secure/panel", "{\"v\":1}", 7, scratch, opened, &opened_length) == OpenResult::Malformed);
  CHECK(openEnvelope(keys.panel_to_bridge, keys.key_id, "hometiles/secure/panel", "not json", 8, scratch, opened, &opened_length) == OpenResult::Malformed);

  // Header parsing is strict.
  Header parsed;
  const uint8_t* parsed_body;
  size_t body_length;
  auto parse = [&](const char* text) {
    return parsePlaintext(reinterpret_cast<const uint8_t*>(text), std::strlen(text), parsed, &parsed_body, &body_length);
  };
  CHECK(parse("rekey - 0 -\n") && parsed.type == MessageType::Rekey && !parsed.has_session && body_length == 0);
  CHECK(!parse("rekey - 0 -"));
  CHECK(!parse("launch - 0 -\n"));
  CHECK(!parse("cmd 0011 1 light\n"));
  CHECK(!parse("cmd - 01 light\n"));
  CHECK(!parse("cmd - 4294967296 light\n"));
  CHECK(!parse("cmd - 1 Light\n"));
  CHECK(!parse("cmd - 1 light extra\n"));
  CHECK(parse("cmd - 4294967295 light\n{}") && parsed.seq == 4294967295u && body_length == 2);

  // Replay window: new numbers pass once, reordering within 64 passes once.
  ReplayWindow window;
  CHECK(!acceptSequence(window, 0));
  CHECK(acceptSequence(window, 1) && !acceptSequence(window, 1));
  CHECK(acceptSequence(window, 5) && acceptSequence(window, 3) && !acceptSequence(window, 3));
  CHECK(acceptSequence(window, 100) && !acceptSequence(window, 36) && acceptSequence(window, 37));
  CHECK(!acceptSequence(window, 5));
  CHECK(acceptSequence(window, 1000) && acceptSequence(window, 999) && !acceptSequence(window, 1000));

  // Stored pairing record.
  PairingRecord record = makeRecord(PairingState::Active, "ABCDEFGHJKMNPQRSTVWXYZ012");
  PairingState state;
  char restored[kCodeLength + 1];
  CHECK(applyRecord(record, state, restored) && state == PairingState::Active && std::strcmp(restored, "ABCDEFGHJKMNPQRSTVWXYZ012") == 0);
  record.code[3] = 'U';
  record.checksum = recordChecksum(record);
  CHECK(!applyRecord(record, state, restored));
  record = makeRecord(PairingState::Off, "ABCDEFGHJKMNPQRSTVWXYZ012");
  CHECK(!applyRecord(record, state, restored));
  record = makeRecord(PairingState::Pending, "ABCDEFGHJKMNPQRSTVWXYZ012");
  record.checksum ^= 1;
  CHECK(!applyRecord(record, state, restored));

  // Only the Bridge command leaves under this panel's base topic are sealed.
  CHECK(std::strcmp(sealedCommandLeaf("hometiles/cmnd/light", "hometiles", 9), "light") == 0);
  CHECK(std::strcmp(sealedCommandLeaf("hometiles/cmnd/value", "hometiles", 9), "value") == 0);
  CHECK(sealedCommandLeaf("hometiles/cmnd/view", "hometiles", 9) == nullptr);
  CHECK(sealedCommandLeaf("hometiles/cmnd/light/x", "hometiles", 9) == nullptr);
  CHECK(sealedCommandLeaf("hometiles2/cmnd/light", "hometiles", 9) == nullptr);
  CHECK(sealedCommandLeaf("hometiles/stat/light", "hometiles", 9) == nullptr);
  std::printf("ok\n");
  return 0;
}
`;

const stdout = compileAndRun({
  label: 'Command channel core harness',
  harness,
  sources: ['src/core/security/ht_crypto.cpp'],
  input: [bridgeSession, bridgeData, wrongTopic, wrongKey].join('\n') + '\n'
});
if (stdout !== null) {
  const lines = stdout.trim().split('\n');
  assert.equal(lines.at(-1), 'ok', stdout);
  const value = tag => lines.find(entry => entry.startsWith(tag + ' '))?.slice(tag.length + 1);
  assert.equal(value('pb'), panelKey.toString('hex'), 'panel-to-bridge key = HKDF(code, salt, "panel-to-bridge")');
  assert.equal(value('bp'), bridgeKey.toString('hex'));
  assert.equal(value('kid'), keyId);
  const command = open(panelKey, TOPIC_PANEL, value('cmd'));
  assert.equal(command.toString(),
    `cmd ${SESSION} 42 light\n{"entity_id":"light.kitchen","state":"toggle"}`);
  assert.equal(value('cmd'), seal(panelKey, TOPIC_PANEL, command), 'byte-identical envelope');
  const opened = lines.filter(entry => entry.startsWith('open '));
  assert.deepEqual(opened, [
    'open session 0 ffeeddccbbaa99887766554433221100 ',
    'open data 7 camera {"status":"ready","url":"tcp://h:1/t0k3n"}'
  ]);
  // Shared vector with the Python Bridge.
  assert.equal(keyId, '8982fb24a78d94e1');
  assert.equal(value('cmd'), '{"v":1,"k":"8982fb24a78d94e1","n":"0102030405060708090a0b0c","d":"7832dd1bd249db728b0a50bc7f872a71128e0ceb9914732336b5052b125d2e15b17d8e61b37b8088f840f1bf08fff2509452d86153ead2e27de853dc2e18ceb8216139e4dd9f2ca29ecc3c75b9e87b874ff98daef024584a856613fa5f1d52fbb43bb25a9350ab84ebf6299d"}');
  console.log('Command channel core: keys, envelopes, headers, replay window and records passed');
}

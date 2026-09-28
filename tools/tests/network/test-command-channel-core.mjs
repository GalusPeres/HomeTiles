// Encrypted Bridge commands: runs the production core
// (src/network/secure/command_channel_core.h) on the host and checks every
// byte against an independent Node/OpenSSL implementation of the protocol in
// docs-dev/command-encryption.md. The fixed vector at the end is shared with
// HomeTiles Bridge tests/test_command_channel.py.
import assert from 'node:assert/strict';
import {createCipheriv, createDecipheriv, createHash, createHmac, hkdfSync} from 'node:crypto';

import {compileAndRun} from '../../lib/cpp-host.mjs';

// Shared number-comparison pairing vector (contract v2). X25519 runs in
// mbedTLS on the panel; the host checks everything around it.
const PAIR = {
  base: 'hometiles/test',
  pk_p: 'c306fb0ef2bf8b7f93bad98155fa37daec74db0c4cbeda6c6f1dba9d36558252',
  pk_b: 'db48257e1237976a74ad8cfedca00213408fe89ac6251f1b930245f242b5c31a',
  n_p: 'c3'.repeat(16),
  n_b: 'd4'.repeat(16),
  s: '9502af7a4b678841b839429623a09a23f6cc551836e48a52c0e4faf4b9d3b06e',
  c: '3262f80a0dcdf8b757e44596ed47c05afdc9a16405a915c710ab33443f8af112',
  T: '2d4cd2f1d56b383880cc9e27ec65419c1ee1bf1df99bbe5dd115e65d3c613e9f',
  number: '061 806',
  K: 'b925def556256ead767b0f1d14879e50d6bddbd0bb44dc0d2435e4af011b3e19',
  m_panel: 'a2bbe3db083e9884b39df9d41eac55ed94b652e364c636157423f773bc35516b',
  m_bridge: 'ce6193e03597bf02204ee8dd3a1f05d38675088c497a6f5ae35718acf78ef456',
};
const hexBuf = value => Buffer.from(value, 'hex');
const sha = (...parts) => createHash('sha256').update(Buffer.concat(parts)).digest();
{
  // Independent Node derivation of the whole chain from the raw vector.
  const base = Buffer.from(PAIR.base);
  const length = Buffer.from([base.length >> 8, base.length & 0xff]);
  assert.equal(sha(Buffer.from('HomeTiles pairing commit v2'), hexBuf(PAIR.pk_b), hexBuf(PAIR.pk_p), hexBuf(PAIR.n_b)).toString('hex'), PAIR.c);
  const T = sha(Buffer.from('HomeTiles pairing v2'), length, base, hexBuf(PAIR.pk_p), hexBuf(PAIR.pk_b), hexBuf(PAIR.n_p), hexBuf(PAIR.n_b));
  assert.equal(T.toString('hex'), PAIR.T);
  const number = sha(Buffer.from('HomeTiles pairing number v2'), T).readUInt32BE(0) % 1000000;
  const digits = String(number).padStart(6, '0');
  assert.equal(`${digits.slice(0, 3)} ${digits.slice(3)}`, PAIR.number);
  assert.equal(Buffer.from(hkdfSync('sha256', hexBuf(PAIR.s), T, 'pairing key', 32)).toString('hex'), PAIR.K);
  assert.equal(createHmac('sha256', hexBuf(PAIR.K)).update(Buffer.concat([Buffer.from('confirm panel'), T])).digest('hex'), PAIR.m_panel);
  assert.equal(createHmac('sha256', hexBuf(PAIR.K)).update(Buffer.concat([Buffer.from('confirm bridge'), T])).digest('hex'), PAIR.m_bridge);
}
const SALT = 'HomeTiles command pairing v2';
const derive = info => Buffer.from(hkdfSync('sha256', hexBuf(PAIR.K), Buffer.from(SALT), Buffer.from(info), info === 'key-id' ? 8 : 32));
const panelKey = derive('panel-to-bridge');
const bridgeKey = derive('bridge-to-panel');
const keyId = derive('key-id').toString('hex');
const announceKey = derive('announce');
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
// Shared unpair vectors (docs-dev/command-encryption.md, Bridge tests).
const UNPAIR_NONCE = Buffer.from('000102030405060708090a0b', 'hex');
const UNPAIR_PLAINTEXT = Buffer.from('unpair 0123456789abcdef0123456789abcdef 1 -\n');
const PANEL_UNPAIR = '{"v":1,"k":"20a8108ed11215c5","n":"000102030405060708090a0b","d":"11a6abad551643a47b5deb36c6616860a1b94678af75e8ac6bfee025bc2f3e4c190eac833e0cb381557d3e5bda1967f7f79b5700e0ab459406d83fef"}';
const BRIDGE_UNPAIR = '{"v":1,"k":"20a8108ed11215c5","n":"000102030405060708090a0b","d":"2df48e85bda4d37632843fde4a78d178089f741c47e3291ce1d451a60cc7473dd4676251129cc80d9219d7433ce992181d00bc712a5ce6e994abb98f"}';
assert.equal(seal(panelKey, TOPIC_PANEL, UNPAIR_PLAINTEXT, UNPAIR_NONCE), PANEL_UNPAIR, 'panel unpair vector');
assert.equal(seal(bridgeKey, TOPIC_BRIDGE, UNPAIR_PLAINTEXT, UNPAIR_NONCE), BRIDGE_UNPAIR, 'Bridge unpair vector');

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
  // Number-comparison pairing (shared v2 vector). X25519 runs in mbedTLS on
  // the panel; its shared secret s is part of the vector.
  auto bytes = [](const char* text, uint8_t* out, size_t size) {
    return ht_crypto::hexDecode(text, std::strlen(text), out, size);
  };
  uint8_t pk_p[kPairKeySize], pk_b[kPairKeySize], n_p[kPairNonceSize], n_b[kPairNonceSize], shared[kPairKeySize];
  CHECK(bytes("${PAIR.pk_p}", pk_p, sizeof(pk_p)) && bytes("${PAIR.pk_b}", pk_b, sizeof(pk_b)));
  CHECK(bytes("${PAIR.n_p}", n_p, sizeof(n_p)) && bytes("${PAIR.n_b}", n_b, sizeof(n_b)));
  CHECK(bytes("${PAIR.s}", shared, sizeof(shared)));
  char hex[65];
  auto show = [&](const char* tag, const uint8_t* data, size_t size) {
    ht_crypto::hexEncode(data, size, hex, sizeof(hex));
    std::printf("%s %s\n", tag, hex);
  };
  uint8_t commit[32], transcript[32], pairing_key[kPairKeySize], confirm_panel[32], confirm_bridge[32];
  pairingCommit(pk_b, pk_p, n_b, commit);
  show("commit", commit, 32);
  CHECK(!pairingTranscript("", pk_p, pk_b, n_p, n_b, transcript));
  CHECK(!pairingTranscript(nullptr, pk_p, pk_b, n_p, n_b, transcript));
  CHECK(pairingTranscript("${PAIR.base}", pk_p, pk_b, n_p, n_b, transcript));
  show("transcript", transcript, 32);
  char number[kPairNumberDisplaySize];
  formatPairingNumber(pairingNumber(transcript), number);
  std::printf("number %s\n", number);
  formatPairingNumber(7, number);
  CHECK(std::strcmp(number, "000 007") == 0);
  pairingKey(shared, transcript, pairing_key);
  show("K", pairing_key, 32);
  pairingConfirmation(pairing_key, transcript, true, confirm_panel);
  pairingConfirmation(pairing_key, transcript, false, confirm_bridge);
  show("m_panel", confirm_panel, 32);
  show("m_bridge", confirm_bridge, 32);
  // Another base topic gives another transcript, so another number and key.
  uint8_t other_transcript[32];
  CHECK(pairingTranscript("hometiles/other", pk_p, pk_b, n_p, n_b, other_transcript));
  CHECK(std::memcmp(other_transcript, transcript, 32) != 0);

  // Pairing messages: built exactly, parsed strictly.
  char message[kMaxPairMessageLength + 1];
  CHECK(buildPairMessage("start", "0123456789abcdef", "pk", pk_p, sizeof(pk_p), nullptr, message, sizeof(message)) > 0);
  std::printf("start %s\n", message);
  CHECK(buildPairMessage("abort", "0123456789abcdef", "r", nullptr, 0, "cancel", message, sizeof(message)) > 0);
  CHECK(std::strcmp(message, "{\"v\":2,\"t\":\"abort\",\"id\":\"0123456789abcdef\",\"r\":\"cancel\"}") == 0);
  CHECK(buildPairMessage("confirm", "0123456789abcdef", "m", confirm_panel, 32, nullptr, message, 40) == 0);
  PairMessage pair;
  auto parse_pair = [&](const char* text) { return parsePairMessage(text, std::strlen(text), pair); };
  CHECK(parse_pair("{\"v\":2,\"t\":\"commit\",\"id\":\"0123456789abcdef\",\"pk\":\"${PAIR.pk_b}\",\"c\":\"${PAIR.c}\",\"x\":\"ignored\"}"));
  CHECK(pair.type == PairType::Commit && pair.has_pk && pair.has_c && !pair.has_n);
  CHECK(std::strcmp(pair.id, "0123456789abcdef") == 0);
  CHECK(std::memcmp(pair.pk, pk_b, 32) == 0 && std::memcmp(pair.c, commit, 32) == 0);
  CHECK(parse_pair("{\"v\":2,\"t\":\"nonce\",\"id\":\"0123456789abcdef\",\"n\":\"${PAIR.n_b}\"}") && pair.type == PairType::Nonce && pair.has_n);
  CHECK(parse_pair("{\"v\":2,\"t\":\"abort\",\"id\":\"0123456789abcdef\",\"r\":\"paired\"}") && pair.type == PairType::Abort);
  CHECK(std::strcmp(pair.reason, "paired") == 0);
  CHECK(parse_pair("{\"v\":2,\"t\":\"abort\",\"id\":\"0123456789abcdef\"}") && pair.reason[0] == '\0');
  CHECK(!parse_pair("{\"v\":1,\"t\":\"abort\",\"id\":\"0123456789abcdef\"}"));
  CHECK(!parse_pair("{\"v\":22,\"t\":\"abort\",\"id\":\"0123456789abcdef\"}"));
  CHECK(!parse_pair("{\"v\":2,\"t\":\"abort\",\"id\":\"0123456789ABCDEF\"}"));
  CHECK(!parse_pair("{\"v\":2,\"t\":\"abort\",\"id\":\"0123456789abcde\"}"));
  CHECK(!parse_pair("{\"v\":2,\"t\":\"launch\",\"id\":\"0123456789abcdef\"}"));
  CHECK(!parse_pair("{\"v\":2,\"t\":\"nonce\",\"id\":\"0123456789abcdef\",\"n\":\"C3C3C3C3C3C3C3C3C3C3C3C3C3C3C3C3\"}"));
  CHECK(!parse_pair("{\"v\":2,\"t\":\"commit\",\"id\":\"0123456789abcdef\",\"c\":\"${PAIR.c}\"}"));

  Keys keys;
  deriveKeys(pairing_key, keys);
  ht_crypto::hexEncode(keys.panel_to_bridge, 32, hex, sizeof(hex));
  std::printf("pb %s\n", hex);
  ht_crypto::hexEncode(keys.bridge_to_panel, 32, hex, sizeof(hex));
  std::printf("bp %s\n", hex);
  std::printf("kid %s\n", keys.key_id);
  ht_crypto::hexEncode(keys.announce, 32, hex, sizeof(hex));
  std::printf("ak %s\n", hex);

  // Signed announcement: the signature member replaces the final brace.
  {
    const char* topic = "tab5_lvgl/config/A1B2C3D4E5F6/bridge";
    const char* unsigned_payload = "{\"device_id\":\"A1B2C3D4E5F6\",\"base_topic\":\"hometiles\",\"ha_prefix\":\"ha\"}";
    const size_t unsigned_length = std::strlen(unsigned_payload);
    char signed_payload[256];
    const size_t signed_length = signAnnouncement(keys.announce, topic, unsigned_payload, unsigned_length,
                                                  signed_payload, sizeof(signed_payload));
    CHECK(signed_length == unsigned_length + kAnnouncementSignatureOverhead);
    CHECK(signed_length == std::strlen(signed_payload));
    std::printf("announce %s\n", signed_payload);
    // Too small, not an object, or empty: nothing is written.
    CHECK(signAnnouncement(keys.announce, topic, unsigned_payload, unsigned_length, signed_payload,
                           unsigned_length + kAnnouncementSignatureOverhead) == 0);
    CHECK(signAnnouncement(keys.announce, topic, "[1]", 3, signed_payload, sizeof(signed_payload)) == 0);
    CHECK(signAnnouncement(keys.announce, topic, "{", 1, signed_payload, sizeof(signed_payload)) == 0);
  }

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

  // Unpair from the panel: numbered in the current session, empty body.
  {
    Header unpair;
    unpair.type = MessageType::Unpair;
    unpair.has_session = true;
    ht_crypto::hexDecode("0123456789abcdef0123456789abcdef", 32, unpair.session, kSessionSize);
    unpair.seq = 1;
    length = buildPlaintext(unpair, nullptr, 0, plaintext, sizeof(plaintext));
    CHECK(length == std::strlen("unpair 0123456789abcdef0123456789abcdef 1 -\n"));
    CHECK(std::memcmp(plaintext, "unpair 0123456789abcdef0123456789abcdef 1 -\n", length) == 0);
    const uint8_t shared_nonce[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
    static char unpair_envelope[kMaxEnvelopeLength + 1];
    CHECK(sealEnvelope(keys.panel_to_bridge, keys.key_id, shared_nonce, "hometiles/secure/panel", plaintext, length,
                       scratch, unpair_envelope, sizeof(unpair_envelope)) > 0);
    std::printf("unpair %s\n", unpair_envelope);
  }

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
  // The Bridge's unpair (shared vector): opens and parses as a numbered
  // message of the current session.
  {
    const std::string input = line();
    CHECK(openEnvelope(keys.bridge_to_panel, keys.key_id, "hometiles/secure/bridge", input.c_str(), input.size(),
                       scratch, opened, &opened_length) == OpenResult::Ok);
    Header parsed;
    const uint8_t* parsed_body = nullptr;
    size_t body_length = 0;
    CHECK(parsePlaintext(opened, opened_length, parsed, &parsed_body, &body_length));
    CHECK(parsed.type == MessageType::Unpair && parsed.has_session && parsed.seq == 1 &&
          parsed.name[0] == '\0' && body_length == 0);
    char session_hex[kSessionHexSize];
    ht_crypto::hexEncode(parsed.session, kSessionSize, session_hex, sizeof(session_hex));
    CHECK(std::strcmp(session_hex, "0123456789abcdef0123456789abcdef") == 0);
  }

  // Tampering, a foreign key id and garbage are rejected.
  std::string tampered = envelope;
  tampered[tampered.size() - 5] = tampered[tampered.size() - 5] == '0' ? '1' : '0';
  CHECK(openEnvelope(keys.panel_to_bridge, keys.key_id, "hometiles/secure/panel", tampered.c_str(), tampered.size(), scratch, opened, &opened_length) == OpenResult::Rejected);
  CHECK(openEnvelope(keys.panel_to_bridge, keys.key_id, "hometiles/secure/panel", envelope, envelope_length, scratch, opened, &opened_length) == OpenResult::Ok);
  Keys other;
  uint8_t other_key[kPairKeySize];
  std::memset(other_key, 0x5a, sizeof(other_key));
  deriveKeys(other_key, other);
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
  CHECK(parse("unpair 00112233445566778899aabbccddeeff 3 -\n") && parsed.type == MessageType::Unpair &&
        parsed.has_session && parsed.seq == 3);
  CHECK(std::strcmp(typeName(MessageType::Unpair), "unpair") == 0);
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

  // Stored pairing record v2 holds K; a v1 record (typed code) has another size.
  PairingRecord record = makeRecord(pairing_key);
  uint8_t restored[kPairKeySize];
  CHECK(applyRecord(record, restored) && std::memcmp(restored, pairing_key, kPairKeySize) == 0);
  record.checksum ^= 1;
  CHECK(!applyRecord(record, restored));
  record = makeRecord(pairing_key);
  record.version = 1;
  record.checksum = recordChecksum(record);
  CHECK(!applyRecord(record, restored));
  record = makeRecord(pairing_key);
  record.state = static_cast<uint8_t>(PairingState::Off);
  record.checksum = recordChecksum(record);
  CHECK(!applyRecord(record, restored));
  CHECK(kLegacyRecordSize != sizeof(PairingRecord));

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
  input: [bridgeSession, bridgeData, wrongTopic, wrongKey, BRIDGE_UNPAIR].join('\n') + '\n'
});
if (stdout !== null) {
  const lines = stdout.trim().split('\n');
  assert.equal(lines.at(-1), 'ok', stdout);
  const value = tag => lines.find(entry => entry.startsWith(tag + ' '))?.slice(tag.length + 1);
  assert.equal(value('commit'), PAIR.c, 'commitment');
  assert.equal(value('transcript'), PAIR.T, 'the transcript binds the base topic');
  assert.equal(value('number'), PAIR.number, 'six digits with a leading zero');
  assert.equal(value('K'), PAIR.K);
  assert.equal(value('m_panel'), PAIR.m_panel);
  assert.equal(value('m_bridge'), PAIR.m_bridge);
  assert.deepEqual(JSON.parse(value('start')), {v: 2, t: 'start', id: '0123456789abcdef', pk: PAIR.pk_p});
  assert.equal(value('pb'), panelKey.toString('hex'), 'panel-to-bridge key = HKDF(K, salt v2, "panel-to-bridge")');
  assert.equal(value('bp'), bridgeKey.toString('hex'));
  assert.equal(value('kid'), keyId);
  const command = open(panelKey, TOPIC_PANEL, value('cmd'));
  assert.equal(command.toString(),
    `cmd ${SESSION} 42 light\n{"entity_id":"light.kitchen","state":"toggle"}`);
  assert.equal(value('cmd'), seal(panelKey, TOPIC_PANEL, command), 'byte-identical envelope');
  assert.equal(value('unpair'), PANEL_UNPAIR, 'the panel seals the shared unpair vector byte for byte');
  const opened = lines.filter(entry => entry.startsWith('open '));
  assert.deepEqual(opened, [
    'open session 0 ffeeddccbbaa99887766554433221100 ',
    'open data 7 camera {"status":"ready","url":"tcp://h:1/t0k3n"}'
  ]);
  // Announcement signature: HMAC(announce key, topic "\n" unsigned payload).
  assert.equal(value('ak'), announceKey.toString('hex'));
  const announceTopic = 'tab5_lvgl/config/A1B2C3D4E5F6/bridge';
  const unsignedAnnouncement = '{"device_id":"A1B2C3D4E5F6","base_topic":"hometiles","ha_prefix":"ha"}';
  const signature = createHmac('sha256', announceKey).update(`${announceTopic}\n${unsignedAnnouncement}`).digest('hex');
  assert.equal(value('announce'), `${unsignedAnnouncement.slice(0, -1)},"sig":"${signature}"}`);
  assert.deepEqual(JSON.parse(value('announce')), {...JSON.parse(unsignedAnnouncement), sig: signature});
  // Shared vectors with the Python Bridge (tests/test_command_channel.py and
  // tests/test_announcement_guard.py).
  assert.equal(announceKey.toString('hex'), '318f1b1153aed588afc39a727b1f7a56659c9104b8f4d2eac4b8ee08eb71563e');
  assert.equal(signature, 'be8539d3139c63fe108fafd4a9ffc736c7ec51fbdfc51a067f9c4d45578b68b9');
  assert.equal(keyId, '20a8108ed11215c5');
  assert.equal(value('cmd'), '{"v":1,"k":"20a8108ed11215c5","n":"0102030405060708090a0b0c","d":"d6897ddce5bea6e0aa22053a5ef1bd4bfde4f4955a583f8114a4389b1d8763a81913cd4023b4686a4f505a521561c61f493950b10c113309dfcdf4e97cdb40c2355f680ae55b1a36c7740244160c7facdae9637913ba13e9598e1f05642507bc34fa264f166e670e20a04aab"}');
  console.log('Command channel core: keys, envelopes, headers, replay window and records passed');
}

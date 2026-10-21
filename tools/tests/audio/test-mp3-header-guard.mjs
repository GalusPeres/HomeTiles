// #119: a short MP3 signal tone restarted a Guition V1. The Helix decoder of
// the ESP32 core (chmorgan/esp-libhelix-mp3 1.0.3) searches 12-bit sync words
// only, so an MPEG-2.5 clip (8-12 kHz) has no frame it can see. It took the
// bytes "FF FF 00 00" of the LAME/Info tag for an MPEG-1 Layer I free-format
// frame, kept free-bitrate mode with -1 slots when the next sync was not
// found, and copied the next frames with a length of -1 over its own memory
// (reproduced on the host with AddressSanitizer: 171 of 240 MPEG-2.5 clips).
// playMp3 now hands Helix only headers it can decode, and the firmware builds
// its own Helix copy with the 11-bit sync word, so MPEG-2.5 decodes too
// (test-helix-mpeg25.mjs). The headers below come from those reproduction
// clips.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const speaker = fs.readFileSync(path.join(root, 'src/audio/speaker.cpp'), 'utf8')
  .replaceAll('\r\n', '\n');

const play = cppFunctionDefinitions(speaker).find(item => item.name === 'playMp3');
assert.ok(play, 'playMp3 must exist');
assert.match(play.body,
  /if \(left >= 4 && !isDecodableMp3Header\(read\)\) \{\s*src->buffer\.consume\(1\);\s*\+\+unsynced_bytes;\s*if \(!synced && unsynced_bytes > kMaxSyncHuntBytes\) break;\s*continue;\s*\}\s*const int err = MP3Decode\(/,
  'a header Helix cannot decode is skipped before MP3Decode');
assert.equal((play.body.match(/MP3Decode\(/g) || []).length, 1, 'one decode call, behind the check');

const output = compileAndRun({
  label: 'MP3 header guard',
  harness: String.raw`
#include <cstdio>
#include "src/audio/speaker_contract.h"
using speaker_contract::isDecodableMp3Header;
static int failures = 0;
static void expect(const char* name, const unsigned char* h, bool want) {
  if (isDecodableMp3Header(h) != want) { std::printf("FAIL %s\n", name); ++failures; }
}
int main() {
  const unsigned char mpeg1_44k_128[] = {0xFF, 0xFB, 0x90, 0x64};   // MPEG-1 L3 44.1 kHz
  const unsigned char mpeg1_48k_320[] = {0xFF, 0xFB, 0xE4, 0x44};   // MPEG-1 L3 48 kHz
  const unsigned char mpeg2_22k_64[] = {0xFF, 0xF3, 0x80, 0xC4};    // MPEG-2 L3 22.05 kHz
  const unsigned char mpeg2_crc[] = {0xFF, 0xF2, 0x50, 0xC0};       // MPEG-2 L3 with CRC
  const unsigned char mpeg25_12k[] = {0xFF, 0xE3, 0x64, 0xC4};      // MPEG-2.5: 11-bit sync
  const unsigned char mpeg25_8k[] = {0xFF, 0xE3, 0x18, 0xC4};       // MPEG-2.5 8 kHz 8 kbit/s
  const unsigned char reserved_v[] = {0xFF, 0xEB, 0x90, 0x64};      // version bits 01
  const unsigned char tag_bytes[] = {0xFF, 0xFF, 0x00, 0x00};       // Info tag: MPEG-1 L1 free
  const unsigned char layer2[] = {0xFF, 0xFD, 0x94, 0x00};          // MP2 frame
  const unsigned char free_l3[] = {0xFF, 0xFB, 0x00, 0x64};         // Layer III free format
  const unsigned char bad_rate[] = {0xFF, 0xFB, 0xF0, 0x64};        // bitrate index 15
  const unsigned char bad_sr[] = {0xFF, 0xFB, 0x9C, 0x64};          // sample rate index 3
  const unsigned char no_sync[] = {0xFE, 0xFB, 0x90, 0x64};
  expect("mpeg1_44k_128", mpeg1_44k_128, true);
  expect("mpeg1_48k_320", mpeg1_48k_320, true);
  expect("mpeg2_22k_64", mpeg2_22k_64, true);
  expect("mpeg2_crc", mpeg2_crc, true);
  expect("mpeg25_12k", mpeg25_12k, true);
  expect("mpeg25_8k", mpeg25_8k, true);
  expect("reserved_v", reserved_v, false);
  expect("tag_bytes", tag_bytes, false);
  expect("layer2", layer2, false);
  expect("free_l3", free_l3, false);
  expect("bad_rate", bad_rate, false);
  expect("bad_sr", bad_sr, false);
  expect("no_sync", no_sync, false);
  if (isDecodableMp3Header(nullptr)) { std::printf("FAIL null\n"); ++failures; }
  std::printf("%d failures\n", failures);
  return failures ? 1 : 0;
}
`,
});
if (output !== null) {
  assert.match(output, /^0 failures$/m);
  console.log('PASS: only MPEG-1/2/2.5 Layer III headers with a table bitrate reach the Helix decoder');
}

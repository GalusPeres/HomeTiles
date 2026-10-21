// #119: a 6 KB "audio/mpeg" clip from Home Assistant decoded no frame on a
// Guition V1 and the panel restarted in MP3FreeDecoder (Helix overwrote its
// own memory). The file may not be shared, so the format is recorded: on the
// speaker task's stack, where the core dump keeps it, and in one log line
// when a clip decodes nothing. Only header bytes, never audio data.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/audio/speaker.cpp'), 'utf8')
  .replaceAll('\r\n', '\n');

function bodyOf(name) {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, `${name} must exist`);
  return found.body;
}

assert.match(source, /char marker\[8\] = \{'M', 'P', '3', 'D', 'I', 'A', 'G', '\\0'\};/,
  'a marker finds the record in a core dump even where gdb cannot');
assert.match(source, /uint8_t headers\[8\]\[4\] = \{\};/);
assert.match(source, /__attribute__\(\(noinline\)\) void logUndecodedMp3\(const Mp3Diagnostics& diag\)/);

const play = bodyOf('playMp3');
assert.match(play, /Mp3Diagnostics diag;\s*diag\.id3_bytes = static_cast<uint32_t>\(tag\);/,
  'the record lives on the speaker task stack');
assert.match(play,
  /if \(diag\.candidates < 8\) \{\s*memcpy\(diag\.headers\[diag\.candidates\], read, left < 4 \? static_cast<size_t>\(left\) : 4\);\s*\}\s*\+\+diag\.candidates;\s*\/\/[^\n]*\n\s*\/\/[^\n]*\n\s*if \(left >= 4 && !isDecodableMp3Header\(read\)\)/,
  'the header bytes are kept before the header check and Helix see them');
assert.match(play, /if \(err == ERR_MP3_NONE\) \{\s*if \(\+\+diag\.decoded == 1\) noteMp3Duration\(decoder, read, left, src, diag\.id3_bytes\);\s*\} else if \(err < 0 && err > -16\) \{\s*\+\+diag\.errors\[-err\];/);
assert.match(play, /if \(diag\.decoded == 0\) logUndecodedMp3\(diag\);\s*heap_caps_free\(pcm\);\s*MP3FreeDecoder\(decoder\);/,
  'the line and the record come before the free that crashed');
// The attribute hides it from cppFunctionDefinitions: cut it out by hand.
const logStart = source.indexOf('void logUndecodedMp3(');
const logBody = source.slice(logStart, source.indexOf('ErrorCode playMp3(', logStart));
assert.ok(logStart > 0 && logBody.includes('MP3 decoded no frame'));
assert.doesNotMatch(logBody, /pcm|buffer/, 'no audio data goes into the log');

console.log('PASS: an MP3 clip that decodes nothing leaves its frame headers in the log and the core dump');

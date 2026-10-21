// The speaker as a small player (2026-10-21, V2 b500 log): a clip downloaded
// over HTTPS took the free internal RAM from 155 to 44 KB and the DMA heap to
// 2 KB, the link waited and the UI slowed, and the next clip's TLS setup
// failed (esp-aes). HTTPS downloads now keep their TLS buffers in PSRAM like
// the P4 update download, the Helix state lives in PSRAM, and each clip logs
// how low internal RAM went. Home Assistant's player bar gets the position,
// the length and pause/resume.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/audio/speaker.cpp'), 'utf8')
  .replaceAll('\r\n', '\n');
const fn = name => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, `${name} exists`);
  return found.body;
};

// TLS in PSRAM for the whole download, internal again afterwards.
assert.match(source, /void\* tlsPsramCalloc\(size_t count, size_t size\) \{\s*void\* ptr = heap_caps_calloc\(count, size, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT\);/);
assert.match(source, /class ScopedPsramTls \{[\s\S]*?mbedtls_platform_set_calloc_free\(tlsPsramCalloc, tlsFree\)[\s\S]*?~ScopedPsramTls\(\) \{\s*if \(active_\) mbedtls_platform_set_calloc_free\(tlsInternalCalloc, tlsFree\);/);
const playUrl = fn('playUrl');
assert.match(playUrl, /const bool https = strncmp\(url, "https:\/\/", 8\) == 0;\s*\/\/[^\n]*\n\s*ScopedPsramTls tls_memory\(https\);\s*HTTPClient http;/,
  'the scope opens before the HTTP client, so it outlives every TLS allocation');
assert.match(playUrl, /src\.total_bytes = src\.remaining;/);

// Pause: both playback loops hold before reading on (playPcm plays WAV clips
// and the Bridge's PCM stream).
assert.match(fn('playWav'), /return playPcm\(src, generation, info\.sample_rate, info\.channels,/);
for (const loop of ['playMp3', 'playPcm']) {
  const body = fn(loop);
  assert.match(body, /while \([^)]*!stopRequested\(generation\)\) \{\s*if \(!holdWhilePaused\(src, generation\)\) break;\s*pump\(src\);/, `${loop} pauses`);
  assert.match(body, /if \(!writeFrames\([a-z]+, frames\)\) \{[\s\S]*?\}\s*notePlayed\(frames\);/, `${loop} counts the position`);
}
const hold = fn('holdWhilePaused');
assert.match(hold, /setPublic\(PublicState::Paused, ErrorCode::None\);\s*while \(g_pause_requested\.load\(\) && !stopRequested\(generation\)\) \{\s*pump\(src\);/);
assert.match(hold, /if \(stopRequested\(generation\)\) return false;\s*setPublic\(PublicState::Playing, ErrorCode::None\);/);

const handler = fn('handleCommandPayload');
assert.match(handler, /case CommandKind::Pause:\s*case CommandKind::Resume:[\s\S]*?if \(static_cast<PublicState>\(g_public_state\.load\(\)\) != PublicState::Idle\) \{\s*g_pause_requested\.store\(command\.kind == CommandKind::Pause\);/);
assert.match(handler, /case CommandKind::Stop:[\s\S]*?g_pause_requested\.store\(false\);/);
assert.match(handler, /g_play_pending\.store\(true\);\s*g_pause_requested\.store\(false\);/);

// One line per clip with the internal RAM, and one restart after a pause
// that outlasted the server.
const play = fn('playRequest');
assert.match(play, /g_clip_stats\.internal_before = heap_caps_get_free_size\(MALLOC_CAP_INTERNAL\);/);
assert.match(play, /"%s Clip %s %s after %\.1f s: internal RAM %u KB before, lowest %u KB\\n"/);
assert.match(play, /if \(result == ErrorCode::DownloadFailed && g_clip_paused && !stopRequested\(generation\) &&\s*!bridge_stream\) \{[\s\S]*?result = playUrl\(request\.id, request\.url, request\.live, generation, stream_memory\);/);
assert.match(fn('pump'), /^[\s{]*sampleClipMemory\(\);\s*if \(src->ended\) return;/);

// The status carries position and length.
const publish = fn('publishStatus');
assert.match(publish, /fields\.position_ms = g_position_ms\.load\(\);\s*fields\.duration_ms = g_duration_ms\.load\(\);/);
assert.match(publish, /char payload\[320\];/);
// A pause of a clip its Bridge plays again closes the stream (user
// 2026-10-21): no buffers or FFmpeg held while paused, the position is what
// was heard, the status says "released", and a stop or a bare resume ends
// such a pause (no worker is left to do it).
assert.match(hold, /if \(g_clip_restart\) \{[\s\S]*?g_played_us = g_played_us > queued_us \? g_played_us - queued_us : 0;[\s\S]*?g_clip_released = true;\s*return false;\s*\}/);
assert.ok(hold.indexOf('setAmp(false)') < hold.indexOf('if (g_clip_restart)') &&
          hold.indexOf('if (g_clip_restart)') < hold.indexOf('setPublic(PublicState::Paused'),
          'quiet first, then released instead of held');
assert.match(play, /g_clip_restart = bridge_stream && request\.restart;\s*g_clip_released = false;\s*g_released\.store\(false\);/);
assert.match(play, /if \(g_clip_released && result == ErrorCode::None && !stopRequested\(generation\)\) \{[\s\S]*?g_released\.store\(true\);\s*setStatusId\(request\.id\);\s*setPublic\(PublicState::Paused, ErrorCode::None\);\s*return;/);
assert.match(handler, /g_request->restart = command\.restart;/);
assert.match(handler, /case CommandKind::Stop:[\s\S]*?if \(g_released\.exchange\(false\)\) setPublic\(PublicState::Idle, ErrorCode::None\);/);
assert.match(handler, /if \(g_released\.load\(\)\) \{[\s\S]*?command\.kind == CommandKind::Resume && g_released\.exchange\(false\)[\s\S]*?setPublic\(PublicState::Idle, ErrorCode::None\);[\s\S]*?break;\s*\}/);
assert.match(publish, /fields\.released = g_released\.load\(\);/);
console.log('PASS: HTTPS in PSRAM, pause/resume, position and length, one memory line per clip, released pause');

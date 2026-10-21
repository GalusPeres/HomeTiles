// #119: the Helix MP3 decoder of the ESP32 core searches 12-bit sync words
// only, so MP3s at 8, 11.025 and 12 kHz (MPEG-2.5, common for short tones)
// never played. The firmware builds its own Helix copy (src/audio/helix_mp3)
// with the 11-bit sync word and prefixed names. This compiles that copy on
// the host, decodes three self-made "dong" clips (ffmpeg tone, no third-party
// audio) and checks every frame of each: the 8 kHz clip decodes no frame with
// the 12-bit sync word.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const helix = path.join(root, 'src/audio/helix_mp3');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replaceAll('\r\n', '\n');

// The firmware takes this copy, not the core's prebuilt one.
assert.match(read('src/audio/speaker.cpp'), /#include "src\/audio\/helix_mp3\/mp3dec\.h"/);
assert.doesNotMatch(read('src/audio/speaker.cpp'), /#include "mp3dec\.h"/);
const common = read('src/audio/helix_mp3/mp3common.h');
assert.match(common, /^#define\tSYNCWORDL\t\t0xe0$/m, '11-bit sync word: MPEG-2.5 frames are found');
assert.match(common, /\/\*\n#define\tSYNCWORDH\t\t0xff\n#define\tSYNCWORDL\t\t0xf0\n\*\//,
  'the 12-bit sync word stays commented out');
assert.match(read('src/audio/helix_mp3/statname.h'), /^#define STAT_PREFIX\t\thtmp3$/m);
const api = read('src/audio/helix_mp3/mp3dec.h');
for (const name of ['MP3InitDecoder', 'MP3FreeDecoder', 'MP3Decode', 'MP3GetLastFrameInfo',
                    'MP3GetNextFrameInfo', 'MP3FindSyncWord']) {
  assert.match(api, new RegExp(`#define ${name}\\s+htmp3_${name}`), `${name} is prefixed`);
}
// The decoder state goes to PSRAM on the panel.
assert.match(read('src/audio/helix_mp3/buffers.c'),
  /#if defined\(ESP_PLATFORM\)[\s\S]*#define malloc\(size\) heap_caps_malloc_prefer\(\(size\), 2, MALLOC_CAP_SPIRAM/);
for (const file of ['LICENSE.txt', 'RPSL.txt', 'RCSL.txt']) {
  assert.ok(fs.existsSync(path.join(helix, file)), `${file} ships with the decoder`);
}

const compiler = [process.env.CC, 'clang', 'gcc', 'cc'].filter(Boolean)
  .find(candidate => {
    const result = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    return !result.error && result.status === 0;
  });
if (!compiler) {
  console.log('SKIP: Helix MPEG-2.5 decode test needs a host C compiler');
  process.exit(0);
}

const out = path.join(root, 'build/tests/helix-mpeg25');
fs.mkdirSync(out, {recursive: true});
const harness = path.join(out, 'decode.c');
fs.writeFileSync(harness, String.raw`
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mp3dec.h"

static int decodable(const unsigned char *h) {
  if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0 || ((h[1] >> 3) & 3) == 1) return 0;
  if (((h[1] >> 1) & 3) != 1) return 0;
  if ((h[2] >> 4) == 0 || (h[2] >> 4) == 15 || ((h[2] >> 2) & 3) == 3) return 0;
  return 1;
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    FILE *f = fopen(argv[i], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = calloc(1, (size_t)size + 4096);
    if (fread(data, 1, (size_t)size, f) != (size_t)size) return 2;
    fclose(f);
    HMP3Decoder dec = MP3InitDecoder();
    short pcm[MAX_NCHAN * MAX_NGRAN * MAX_NSAMP];
    long pos = 0, samples = 0;
    int frames = 0, rate = 0;
    while (pos + 4 <= size) {
      if (!decodable(data + pos)) { ++pos; continue; }
      unsigned char *p = data + pos;
      int left = (int)(size - pos);
      if (MP3Decode(dec, &p, &left, pcm, 0) == ERR_MP3_NONE) {
        MP3FrameInfo info;
        MP3GetLastFrameInfo(dec, &info);
        rate = info.samprate;
        samples += info.outputSamps / info.nChans;
        ++frames;
        pos = (long)(p - data);
      } else {
        ++pos;
      }
    }
    MP3FreeDecoder(dec);
    free(data);
    printf("%s %d %d %ld\n", strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i],
           frames, rate, samples);
  }
  return 0;
}
`);
const sources = fs.readdirSync(helix).filter(name => name.endsWith('.c'))
  .map(name => path.join(helix, name));
const exe = path.join(out, process.platform === 'win32' ? 'decode.exe' : 'decode');
const build = spawnSync(compiler, ['-std=c99', '-O1', '-w', '-DHOMETILES_HELIX_PORTABLE_C',
                                   '-I', helix, harness, ...sources, '-o', exe],
                        {encoding: 'utf8'});
assert.equal(build.status, 0, `Helix did not compile:\n${build.stdout}${build.stderr}`);
const clips = ['dong-8khz-mpeg25.mp3', 'dong-22khz-mpeg2.mp3', 'dong-44khz-mpeg1.mp3']
  .map(name => path.join(root, 'tools/tests/audio/fixtures', name).replaceAll('\\', '/'));
const run = spawnSync(exe, clips, {encoding: 'utf8'});
assert.equal(run.status, 0, `decode failed:\n${run.stdout}${run.stderr}`);
const lines = run.stdout.replace(/\r\n/g, '\n').trim().split('\n');
// name frames rate samples: every frame of each 0.6 s clip.
assert.deepEqual(lines, [
  'dong-8khz-mpeg25.mp3 11 8000 6336',
  'dong-22khz-mpeg2.mp3 25 22050 14400',
  'dong-44khz-mpeg1.mp3 24 44100 27648',
]);
console.log('PASS: the firmware Helix copy decodes MPEG-2.5 (8 kHz), MPEG-2 and MPEG-1 clips');

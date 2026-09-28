import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8')
    .replace(/\r\n?/g, '\n');
}

function requireMarker(source, marker, label) {
  if (!source.includes(marker)) {
    throw new Error(`${label} is missing: ${marker}`);
  }
}

const renderer = read('src/tiles/runtime/tile_renderer.cpp');
const rendererShared = read('src/tiles/runtime/tile_renderer_shared.h');
const rendererHeader = read('src/tiles/runtime/tile_renderer.h');
const folders = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
const sketch = read('sketch.yaml');

for (const [label, source] of [
  ['renderer', renderer],
  ['renderer shared declarations', rendererShared],
  ['folder cache', folders],
]) {
  if (source.includes('DEVICE_WAVESHARE_TOUCH_LCD_8')) {
    throw new Error(`${label} still limits the shared P4 cache to Waveshare 8`);
  }
}
for (const [label, source] of [
  ['renderer', renderer],
  ['folder cache', folders],
]) {
  requireMarker(source, 'CONFIG_IDF_TARGET_ESP32P4', label);
}

// Weather/Media widget state and the entity cache live in PSRAM on every chip.
if (rendererShared.includes('CONFIG_IDF_TARGET_ESP32P4')) {
  throw new Error('renderer shared declarations must not split Weather/Media storage by chip');
}
requireMarker(rendererShared, 'extern WeatherTileWidgets* g_tab0_weather;', 'renderer shared declarations');
requireMarker(rendererShared, 'extern MediaTileWidgets* g_screensaver_media;', 'renderer shared declarations');
requireMarker(
  rendererHeader,
  'PSRAM. Every chip uses this storage',
  'renderer storage contract');
if (/static EntityCacheEntry g_entity_cache\[/.test(folders) ||
    !folders.includes('static EntityCacheEntry* g_entity_cache = nullptr;')) {
  throw new Error('the entity cache must use PSRAM on every chip');
}

if (!/#if defined\(CONFIG_IDF_TARGET_ESP32P4\)[\s\S]*?kMaxResidentFolderUiCaches = 6;[\s\S]*?#else[\s\S]*?kMaxResidentFolderUiCaches = 4;/.test(folders)) {
  throw new Error('P4 must use six folder-cache slots and non-P4 must retain four');
}

// S3 (b85 logs): a folder grid costs 1-2 KB internal RAM and 44-48 KB stay
// free at runtime, so the fourth grid must be admissible there.
const s3Floor = /kFolderCacheGrowMinInternalFreeBytes = (\d+)UL \* 1024UL;[\s\S]*?kFolderCacheGrowMinLargestInternalBytes = (\d+)UL \* 1024UL;/.exec(folders);
if (!s3Floor || Number(s3Floor[1]) > 44 || Number(s3Floor[2]) > 30) {
  throw new Error('the S3 folder cache must be able to grow at 44 KB free internal RAM');
}

const p4Profiles = [
  'tab5',
  'waveshare_b4',
  'waveshare_4_3',
  'waveshare_7',
  'waveshare_7b',
  'waveshare_8',
  'waveshare_10_1',
  'guition_jc8012p4a1',
  'guition_jc8012p4a1_v2',
  'guition_jc1060p470c',
  'guition_jc1060p470c_v2',
  'guition_jc4880p443_portrait',
];

for (const profile of p4Profiles) {
  const pattern = new RegExp(
    `\\n  ${profile}:\\r?\\n([\\s\\S]*?)(?=\\n  [A-Za-z0-9_]+:|$)`);
  const match = sketch.match(pattern);
  if (!match) throw new Error(`Missing P4 build profile: ${profile}`);
  if (!match[1].includes('PSRAM=enabled')) {
    throw new Error(`${profile} must enable PSRAM for the shared P4 cache`);
  }
}

for (const profile of [
  'guition_esp32_4848s040',
  'waveshare_s3_touch_lcd_4b',
]) {
  const pattern = new RegExp(
    `\\n  ${profile}:\\r?\\n([\\s\\S]*?)(?=\\n  [A-Za-z0-9_]+:|$)`);
  const match = sketch.match(pattern);
  if (!match || !match[1].includes('esp32s3')) {
    throw new Error(`${profile} must remain outside the P4 cache scope`);
  }
}

console.log('ESP32-P4 folder-cache scope: PASS');

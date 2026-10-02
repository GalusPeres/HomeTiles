// Sidecar texts (long titles, icon color records, long entity IDs, image
// paths) are cached in PSRAM after each read and write (user 2026-10-02: one
// file open per sidecar made every Home grid load and unchanged save take
// about 300 ms on the V2). A read answers from the cache before opening the
// file; a write forgets the old text before it touches the file and keeps the
// new one only after it succeeded; removing a sidecar key forgets its text.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const source = readRepoFile('src/tiles/config/tile_config.cpp').replace(/\r\n?/g, '\n');
const fn = signature => {
  const at = source.indexOf(signature);
  assert.ok(at >= 0, signature);
  return source.slice(at, source.indexOf('\n}\n', at));
};
const order = (body, first, second, message) => {
  const a = body.indexOf(first);
  const b = body.indexOf(second);
  assert.ok(a >= 0 && b >= 0 && a < b, `${message}: ${first} before ${second}`);
};

// PSRAM storage behind a lock; key removal forgets the text.
assert.ok(source.includes('using SidecarTexts = std::vector<SidecarText, PsramAllocator<SidecarText>>;'));
assert.ok(/struct SidecarText \{\s+uint32_t key;\s+PsString text;\s+\};/.test(source));
for (const name of ['sidecarTextCached', 'sidecarTextStore', 'sidecarTextForget']) {
  assert.ok(fn(`static ${name === 'sidecarTextCached' ? 'bool' : 'void'} ${name}(`).includes('SidecarTextsGuard guard;'), name);
}
assert.ok(fn('static void sidecarKeyRemove(').includes('sidecarTextForget(sidecarTextsFor(keys), key);'));
const map = fn('static SidecarTexts& sidecarTextsFor(');
for (const kind of ['image', 'entity', 'title']) {
  assert.ok(map.includes(`if (&keys == &g_${kind}_sidecar_keys) return g_${kind}_sidecar_texts;`), kind);
}

// Reads: cache first, then the file, whose text is kept.
for (const [signature, texts, open] of [
  ['static bool readLongTitleSd(', 'g_title_sidecar_texts', 'storageFS().open(candidate, FILE_READ)'],
  ['static bool readIconColorsSd(', 'g_icon_color_sidecar_texts', 'storageFS().open(candidate, FILE_READ)'],
  ['static bool readImagePathSd(', 'g_image_sidecar_texts', 'storageFS().open(imagePathFile(folder_id, index), FILE_READ)'],
  ['static bool readLongEntityIdSd(', 'g_entity_sidecar_texts', 'storageFS().open(entityPathFile(folder_id, index), FILE_READ)'],
]) {
  const body = fn(signature);
  order(body, `sidecarTextCached(${texts}, key, out)`, open, signature);
  order(body, open, `sidecarTextStore(${texts}, key,`, signature);
}

// Writes: unchanged texts end early; a real write forgets first and stores
// only after it succeeded.
for (const [signature, texts, write, done] of [
  ['static bool writeLongTitleSd(', 'g_title_sidecar_texts', 'File file = storageFS().open(temporary, FILE_WRITE);', 'sidecarKeyAdd(g_title_sidecar_keys, key);'],
  ['static bool writeIconColorsSd(', 'g_icon_color_sidecar_texts', 'File file = storageFS().open(temporary, FILE_WRITE);', 'sidecarKeyAdd(g_icon_color_sidecar_keys, key);'],
  ['static bool writeImagePathSd(', 'g_image_sidecar_texts', 'File f = storageFS().open(filePath, FILE_WRITE);', 'sidecarKeyAdd(g_image_sidecar_keys, key);'],
  ['static bool writeLongEntityIdSd(', 'g_entity_sidecar_texts', 'File f = storageFS().open(filePath, FILE_WRITE);', 'sidecarKeyAdd(g_entity_sidecar_keys, key);'],
]) {
  const body = fn(signature);
  order(body, `sidecarTextForget(${texts}, key);`, write, signature);
  assert.ok(body.indexOf(done) > body.indexOf(write) &&
    body.lastIndexOf(`sidecarTextStore(${texts}, key,`) > body.indexOf(done), `${signature}: stores after the write`);
}
for (const signature of ['static bool writeImagePathSd(', 'static bool writeLongEntityIdSd(']) {
  const body = fn(signature);
  order(body, 'if (sidecarTextCached(', 'File current_file = storageFS().open(filePath, FILE_READ);', signature);
}

console.log('Sidecar texts: reads answer from PSRAM, writes keep the cache equal to the files');

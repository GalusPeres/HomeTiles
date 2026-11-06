// A Camera tile's entries (src/types/camera/camera_entries.h): the record keeps
// only what names something, every kind becomes one entity id, and the panel and
// the Web Admin build the same id, or the tile would lose its shown entry on save.
// Runs the pure core on the host and the delivered browser functions in a VM.
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const CASES = [
  ['h', 'camera.terrasse'],
  ['h', 'image.wetterkarte'],
  ['h', 'sensor.nope'],
  ['h', 'camera.Bad Name'],
  ['r', 'zone.home'],
  ['r', 'zone.arbeit'],
  ['r', 'zone.' + 'a'.repeat(50)],
  ['r', 'geo:46.0664,11.1242'],
  ['r', 'geo:-33.8688,151.2093'],
  ['r', 'geo:95,10'],
  ['r', 'zone.Bad'],
  ['c', 'geo:47.58,12.17'],
  ['c', 'geo:-0.004,-0.005'],
  ['c', 'nothing'],
  ['w', 'https://www.bergfex.at/abtenau/webcams/c1679/'],
  ['w', 'https://images.bergfex.at/webcams/?id=62&format=4'],
  ['w', 'https://example.org/ü.jpg'],
  ['w', 'http://example.org/a.jpg'],
  ['w', 'https://exa mple.org/a.jpg'],
  ['w', 'https://' + 'x'.repeat(400)],
  ['q', 'camera.x'],
];

const harness = String.raw`
#include <cassert>
#include <cstdio>
#include <string>
#include "src/types/camera/camera_entries.h"

using namespace camera_entries;

int main() {
  static const struct { char kind; const char* value; } cases[] = {
` + CASES.map(([kind, value]) => `    {'${kind}', ${JSON.stringify(value)}},`).join('\n') + String.raw`
  };
  for (const auto& item : cases) std::printf("id %s\n", entity_of(item.kind, item.value).c_str());

  // What names nothing goes, each entity once, at most kMaxEntries.
  const std::string record =
      "h\tcamera.a\tOwn\tcctv\tName A\tcamera.a\n"
      "q\tcamera.b\n"
      "h\tsensor.c\n"
      "h\tcamera.a\tTwice\n"
      "r\tzone.home\t\t\tZuhause\tHome Assistant\n"
      "w\thttps://example.org/a.jpg\n";
  const std::vector<Entry> entries = parse(record.c_str());
  assert(entries.size() == 3);
  assert(entries[0].title == "Own" && entries[0].icon == "cctv" && entries[0].name == "Name A");
  assert(entries[1].entity == "camera.ht_radar_home");
  assert(entries[2].kind == kWebcam && entries[2].title.empty());
  assert(index_of(entries, "camera.ht_radar_home") == 1);
  assert(index_of(entries, "camera.gone") == 0);
  const std::string normalized = normalize(record.c_str());
  assert(normalized.find("sensor") == std::string::npos && normalized.find("Twice") == std::string::npos);
  assert(normalize(normalized.c_str()) == normalized);
  assert(normalize("").empty() && parse(nullptr).empty());
  std::string many;
  for (int i = 0; i < 12; ++i) many += "h\tcamera.c" + std::to_string(i) + "\n";
  assert(parse(many.c_str()).size() == kMaxEntries);
  // A record never outgrows its file, also with the longest addresses.
  std::string long_lines;
  for (int i = 0; i < 8; ++i) {
    long_lines += "w\thttps://example.org/" + std::string(370, 'a' + i) + "\t" + std::string(64, 't') + "\t" +
                  std::string(48, 'i') + "\t" + std::string(64, 'n') + "\t" + std::string(64, 'l') + "\n";
  }
  assert(normalize(long_lines.c_str()).size() <= kMaxRecord);
  // A title is cut at a whole character.
  const std::vector<Entry> cut = parse(("h\tcamera.a\t" + std::string(63, 'x') + "\xC3\xBC").c_str());
  assert(cut[0].title.size() == 63);

  // The Bridge's declaration items (camera_sources.py clean_source).
  for (const Entry& entry : parse(
           "h\tcamera.a\n"
           "r\tzone.home\n"
           "r\tzone.arbeit\t\t\tArbeit\n"
           "r\tgeo:46.0664,11.1242\tGardasee\t\tTrento\n"
           "c\tgeo:47.58,-12.17\t\t\tOwn \"place\"\n"
           "w\thttps://example.org/a.jpg?x=1&y=2\t\t\texample.org\n")) {
    std::printf("src %s\n", source_json(entry).c_str());
  }
  return 0;
}
`;

const output = compileAndRun({label: 'Camera entries', harness});
const lines = output === null ? null : output.trim().split('\n');
if (lines) {
  const ids = lines.filter(line => line.startsWith('id')).map(line => line.slice(3));
  assert.equal(ids.length, CASES.length);
  assert.deepEqual(ids.slice(0, 14), ['camera.terrasse', 'image.wetterkarte', '', '', 'camera.ht_radar_home',
    'camera.ht_radar_zone_arbeit', ids[6], 'camera.ht_radar_4607_1112', 'camera.ht_radar_m3387_15121', '', '',
    'camera.ht_radar_4758_1217', ids[12], '']);
  assert.match(ids[6], /^camera\.ht_radar_zone_[0-9a-f]{8}$/, 'A long zone fits the Bridge\'s 48 characters');
  assert.match(ids[14], /^camera\.ht_cam_[0-9a-f]{8}$/);
  assert.deepEqual(ids.slice(17), ['', '', '', ''], 'Only https addresses');
  assert.deepEqual(lines.filter(line => line.startsWith('src')).map(line => line.slice(4)), [
    '',
    '{"id":"ht_radar_home"}',
    '{"id":"ht_radar_zone_arbeit","zone":"zone.arbeit","n":"Arbeit"}',
    '{"id":"ht_radar_4607_1112","lat":46.0664,"lon":11.1242,"n":"Gardasee"}',
    '{"id":"ht_radar_4758_m1217","lat":47.5800,"lon":-12.1700,"n":"Own \\"place\\""}',
    '{"id":"ht_cam_' + ids[14].slice(14).replace(/.*/, '') + lines.find(line => line.includes('ht_cam_') && line.startsWith('src')).match(/ht_cam_([0-9a-f]{8})/)[1] +
      '","url":"https://example.org/a.jpg?x=1&y=2","n":"example.org"}',
  ]);
  for (const line of lines.filter(item => item.startsWith('src') && item.length > 4)) JSON.parse(line.slice(4));

  // ---- The Web Admin's copy builds the same ids ----
  const context = {TextEncoder, Number, String, Math, CAMERA_PLACE_KINDS: ['r', 'c'], CAMERA_MAX_ENTRIES: 8};
  vm.runInNewContext(['cameraFnv1a', 'cameraHex', 'cameraCoordinates', 'cameraHundredths', 'cameraHttpsAddress',
    'cameraEntryEntity', 'cameraEntryField', 'cameraParseRecord', 'cameraValidRows', 'cameraRecordOf']
    .map(extractDeliveredFunction).join('\n'), context);
  CASES.forEach(([kind, value], index) => {
    assert.equal(context.cameraEntryEntity(kind, value), ids[index], `${kind} ${value}`);
  });
  const rows = [{kind: 'h', value: 'camera.a', title: 'Own', icon: 'cctv', name: 'Name A', line: 'camera.a'},
    {kind: 'h', value: '', title: '', icon: '', name: '', line: ''},
    {kind: 'w', value: 'https://example.org/a.jpg', title: '', icon: '', name: 'example.org', line: 'example.org'}];
  const record = context.cameraRecordOf(rows);
  assert.equal(record, 'h\tcamera.a\tOwn\tcctv\tName A\tcamera.a\nw\thttps://example.org/a.jpg\t\t\texample.org\texample.org',
    'An empty row is not saved');
  assert.equal(JSON.stringify(context.cameraParseRecord(record).map(row => row.value)),
    JSON.stringify(['camera.a', 'https://example.org/a.jpg']));
  assert.equal(context.cameraEntryField('x'.repeat(63) + 'ü', 64).length, 63, 'Cut at a whole character');
}

// ---- Where it goes: persistence, declaration, popup, tile ----
const config = readRepoFile('src/tiles/config/tile_config.h');
assert.match(config, /type == TILE_POWER_FLOW \|\| type == TILE_VACUUM \|\| type == TILE_CAMERA;/, 'Through the existing sidecar');
assert.match(config, /tile\.flow_entities = camera_entries::normalize\(tile\.flow_entities\.c_str\(\)\)\.c_str\(\);/);
assert.match(readRepoFile('src/tiles/config/tile_config.cpp'), /kFlowEntityRecordMax = camera_entries::kMaxRecord;/);
const handler = readRepoFile('src/types/camera/web_handler.cpp');
assert.match(handler, /tile\.flow_entities = server\.arg\("camera_entries"\);\s*normalizeFlowEntities\(tile\);/);
assert.match(handler, /entries\[camera_entries::index_of\(entries, tile\.sensor_entity\.c_str\(\)\)\]\.entity/,
  'The tile always shows one of its entries');
assert.match(handler, /server\.arg\("camera_shows"\) == "last" \? camera_picture::kLastChosen : 0/);
assert.match(readRepoFile('src/web/server/handlers/web_admin_tiles.cpp'), /if \(tile\.type == TILE_CAMERA\) \{\s*out \+= ",\\"camera_entries\\":\\"";/,
  'A fresh GET returns the record');
const search = readRepoFile('src/network/bridge/entity_search.cpp');
assert.match(search, /if \(slots\[i\]\.type == TILE_CAMERA\) addCameraEntries\(declaration, slots\[i\]\.extra_entities\);/);
assert.match(search, /declaration\.addSource\(entry\.entity, camera_entries::source_json\(entry\)\);/);
assert.match(search, /declaration\.setRadarSource\(quoted\);/);
assert.match(readRepoFile('src/network/mqtt/mqtt_handlers.cpp'), /slot\.type == TILE_VACUUM \|\| slot\.type == TILE_CAMERA \? "" : slot\.extra_entities/,
  'A record is no list of states to follow');
assert.match(readRepoFile('src/network/secure/command_channel.cpp'), /strcmp\(header\.name, "sources"\) == 0/);

// "Zuletzt gewaehlt" lives only in RAM, also the image has a popup now.
const renderer = readRepoFile('src/types/camera/renderer.cpp');
assert.match(renderer, /std::vector<LastChoice> g_last_choices;/);
assert.doesNotMatch(renderer, /configManager\.save|tileConfig\.save|prefs\./, 'Nothing is written when the popup switches');
assert.doesNotMatch(renderer, /has no popup/);
assert.match(renderer, /camera_picture::attach\(card, icon, title_label, tile, shown_entity\);/);
assert.match(renderer, /g_remembering\.erase\(/, 'A deleted tile leaves the list');
assert.match(readRepoFile('src/types/camera/picture.h'), /constexpr uint8_t kLastChosen = 0x08;/);
assert.match(readRepoFile('src/types/camera/picture.cpp'), /tile->key\[0\] = '\\0';\s*tile->due = true;/,
  'The old picture stays until the new one arrives');

console.log('Camera entries: PASS');

// Pictures from the Bridge over the direct link (docs-dev/images.md): a Media
// tile subscribes to "<ha_prefix>/media_player/<object>/image/<e>x<e>" in the
// size the Media popup shows its cover, the screensaver to its image or camera
// entity in the screen's size; the message is "HTIMG1 <key> <w>x<h>\n" + JPEG.
// The panel reads the header and topic strictly, stores the picture and hands
// a cover to the tiles of its song, any other picture to the screensaver.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const harness = String.raw`
#include <cstdio>
#include <cstring>
#include "src/network/bridge/bridge_images_core.h"
using namespace bridge_images;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d %s\n", __LINE__, #x); return 1; } } while (0)

static bool header(const char* text, size_t extra, Header& out) {
  unsigned char data[128];
  const size_t length = strlen(text);
  memcpy(data, text, length);
  data[length] = 0xFF;
  data[length + 1] = 0xD8;
  return parseHeader(data, length + extra, out);
}

int main() {
  Header h;
  CHECK(header("HTIMG1 0123456789abcdef 240x240\n", 2, h));
  CHECK(strcmp(h.key, "0123456789abcdef") == 0 && h.width == 240 && h.height == 240 && h.jpeg_offset == 32);
  CHECK(header("HTIMG1 0123456789abcdef 1280x800\n", 2, h) && h.width == 1280 && h.height == 800);
  CHECK(!header("HTIMG1 0123456789abcdef 240x240\n", 1, h));   // no JPEG start
  CHECK(!header("HTIMG2 0123456789abcdef 240x240\n", 2, h));
  CHECK(!header("HTIMG1 0123456789ABCDEF 240x240\n", 2, h));   // lower-case hex only
  CHECK(!header("HTIMG1 0123456789abcde 240x240\n", 2, h));
  CHECK(!header("HTIMG1 0123456789abcdef 240x240 \n", 2, h));
  CHECK(!header("HTIMG1 0123456789abcdef 8x8\n", 2, h));
  CHECK(!header("HTIMG1 0123456789abcdef 2000x240\n", 2, h));
  CHECK(!header("HTIMG1 0123456789abcdef 0240x240\n", 2, h));
  CHECK(!header("HTIMG1 0123456789abcdef 240x240", 2, h));      // no line end
  CHECK(!parseHeader(nullptr, 0, h));

  char entity[72];
  uint16_t w = 0, hh = 0;
  CHECK(entityFromTopic("ha/statestream/media_player/wohnzimmer_tv/image/240x240", "ha/statestream",
                        entity, sizeof(entity), &w, &hh));
  CHECK(strcmp(entity, "media_player.wohnzimmer_tv") == 0 && w == 240 && hh == 240);
  CHECK(entityFromTopic("ha/statestream/image/garten/image/1280x800", "ha/statestream",
                        entity, sizeof(entity), &w, &hh));
  CHECK(strcmp(entity, "image.garten") == 0 && w == 1280 && hh == 800);
  CHECK(entityFromTopic("ha/statestream/camera/haustuer_2/image/800x1280", "ha/statestream",
                        entity, sizeof(entity), &w, &hh));
  CHECK(strcmp(entity, "camera.haustuer_2") == 0 && w == 800 && hh == 1280);
  CHECK(!entityFromTopic("ha/statestream/imagex/garten/image/240x240", "ha/statestream", entity, sizeof(entity), &w, &hh));
  CHECK(!entityFromTopic("ha/statestream/image/garten/image/2000x800", "ha/statestream", entity, sizeof(entity), &w, &hh));
  CHECK(!entityFromTopic("ha/statestreamimage/garten/image/240x240", "ha/statestream", entity, sizeof(entity), &w, &hh));
  CHECK(!entityFromTopic("ha/statestream/media_player/tv/state", "ha/statestream", entity, sizeof(entity), &w, &hh));
  CHECK(!entityFromTopic("ha/statestream/light/tv/image/240x240", "ha/statestream", entity, sizeof(entity), &w, &hh));
  CHECK(!entityFromTopic("other/media_player/tv/image/240x240", "ha/statestream", entity, sizeof(entity), &w, &hh));
  CHECK(!entityFromTopic("ha/statestream/media_player/TV/image/240x240", "ha/statestream", entity, sizeof(entity), &w, &hh));
  CHECK(!entityFromTopic("ha/statestream/media_player/tv/image/240x240/x", "ha/statestream", entity, sizeof(entity), &w, &hh));
  CHECK(!entityFromTopic("ha/statestream/media_player//image/240x240", "ha/statestream", entity, sizeof(entity), &w, &hh));
  char small[8];
  CHECK(!entityFromTopic("ha/statestream/media_player/tv/image/240x240", "ha/statestream", small, sizeof(small), &w, &hh));
  std::printf("ok\n");
  return 0;
}
`;
const output = compileAndRun({label: 'bridge images core', harness});
if (output !== null) assert.equal(output.trim(), 'ok');

// The panel side: picture topics end before any state handler, a Media tile
// subscribes its player's cover only over the link, in the Media popup's size.
const handlers = readRepoFile('src/network/mqtt/mqtt_handlers.cpp');
const process = handlers.slice(handlers.indexOf('static void processMqttMessage('));
assert.ok(process.indexOf('bridge_images::handleMqttMessage(topic, payload, length)') <
  process.indexOf('hardwareIo.handleMqttMessage('), 'pictures never reach the state handlers');
assert.equal((handlers.match(/if \(networkManager\.linkConfigured\(\)\) \{\s*add_route\([^;]*bridge_images::coverSuffix\(\)\);/g) || []).length, 2,
  'folder and screensaver Media tiles subscribe the cover only over the link');

const images = readRepoFile('src/network/bridge/bridge_images.cpp');
assert.match(images, /return static_cast<uint16_t>\(popup_layout::scale\(240\)\);/);
assert.match(readRepoFile('src/ui/popups/media/media_popup.cpp'), /constexpr int kCoverSize = popup_layout::scale\(240\);/,
  'the cover picture has the size the Media popup shows');
assert.match(images, /if \(!entityFromTopic\(topic, prefix, entity, sizeof\(entity\), nullptr, nullptr\)\) return false;/);
assert.match(images, /if \(!parseHeader\(payload, length, header\)\) \{[\s\S]*?return true;\s*\}/, 'a malformed picture is dropped');
assert.match(images, /if \(strncmp\(entity, "media_player\.", 13\) == 0\) \{\s*tile_renderer_media_picture_arrived\(entity\);\s*\} else \{\s*image_screensaver_picture_arrived\(entity\);\s*\}/,
  'a cover goes to the tiles, an image or camera picture to the screensaver');
assert.equal((images.match(/announce\(entity\);/g) || []).length, 2, 'a new and a cleared picture are told');
assert.match(images, /snprintf\(suffix, sizeof\(suffix\), "image\/%ux%u", static_cast<unsigned>\(grid_layout::screen_w\(\)\),\s*static_cast<unsigned>\(grid_layout::screen_h\(\)\)\);/,
  'the screensaver picture has the screen\'s size as shown, upright or not');

const renderer = readRepoFile('src/tiles/runtime/tile_renderer.cpp');
assert.match(renderer, /if \(should_update_cover && bridge_pictures_enabled\(\) &&\s*media_artwork::read_string\(payload_start, "image_key", image_key\)\) \{\s*update_media_cover_from_picture\(grid_type, grid_index, widgets, image_key\);\s*\} else if \(should_update_cover\) \{/,
  'over the link the state names its picture; otherwise the embedded copy as before');

console.log('Bridge pictures: strict header and topic, covers in the popup size, screensaver pictures in the screen size');

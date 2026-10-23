// A media player's icon in its state form (user 2026-10-23, V2 b527): the
// tile kept its plain icon while the popup's head showed speaker-play, and
// after reopening a paused player the head showed the tile's plain icon too.
// The tile opened the popup with the icon name it had when it was built; a
// name from before the Bridge's icons arrived ("television") no longer
// matched the tile's icon, so the head fell back to the tile's glyph until
// the next report, and a paused player sends none. Now the tile and the head
// show the same state form (media/state_icon.h), the name is read when the
// popup opens, and the Bridge's icon refresh keeps the state form.
import assert from 'node:assert/strict';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).filter(f => f.name === name);
  assert.equal(found.length, 1, name);
  return found[0].source;
};

// The state form itself, run natively against an MDI table stub.
const output = compileAndRun({
  label: 'Media state icon',
  files: {
    'Arduino.h': String.raw`
#pragma once
#include <cctype>
#include <string>
class String {
 public:
  String(const char* text = "") : s_(text) {}
  String(const std::string& text) : s_(text) {}
  unsigned length() const { return s_.size(); }
  const char* c_str() const { return s_.c_str(); }
  void trim() {
    const auto first = s_.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { s_.clear(); return; }
    s_ = s_.substr(first, s_.find_last_not_of(" \t\r\n") - first + 1);
  }
  bool equalsIgnoreCase(const char* other) const {
    const std::string b(other);
    if (b.size() != s_.size()) return false;
    for (size_t i = 0; i < b.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(s_[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
  }
  String operator+(const char* other) const { return String(s_ + other); }
 private:
  std::string s_;
};
`,
    'src/tiles/icons/mdi_icons.h': String.raw`
#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstring>
inline uint32_t mdiUnknownIconCodepoint() { return 0xF02D7; }
inline uint32_t getMdiCodepoint(const String& name) {
  for (const char* known : {"speaker", "speaker-play", "speaker-pause", "speaker-off",
                            "television", "television-play", "television-pause", "television-off", "radio"}) {
    if (std::strcmp(known, name.c_str()) == 0) return 0xF0001;
  }
  return mdiUnknownIconCodepoint();
}
`,
  },
  harness: String.raw`
#include <cstdio>
#include "src/types/media/state_icon.h"
using namespace media_state_icon;
static void show(const char* icon, const char* state) {
  std::printf("%s/%s=%s\n", icon, state, name(String(icon), form_of(String(state))).c_str());
}
int main() {
  show("speaker", "playing");
  show("speaker", "paused");
  show("speaker", "off");
  show("speaker", "standby");
  show("speaker", "idle");
  show("speaker", " Paused ");
  show("television", "playing");
  show("radio", "playing");
  show("", "playing");
  std::printf("zero=%d\n", static_cast<int>(Form{}) == static_cast<int>(Form::Plain));
  return 0;
}
`,
});
if (output !== null) {
  assert.deepEqual(output.trim().split('\n'), [
    'speaker/playing=speaker-play',
    'speaker/paused=speaker-pause',
    'speaker/off=speaker-off',
    'speaker/standby=speaker-off',
    'speaker/idle=speaker',
    'speaker/ Paused =speaker-pause',
    'television/playing=television-play',
    'radio/playing=radio',
    '/playing=',
    'zero=1',
  ]);
}

// The popup opens with the tile's icon name of now, never one kept from the
// tile's build.
const renderer = readRepoFile('src/types/media/renderer.cpp');
const click = fn(renderer, 'show_media_popup_event_cb');
assert.match(click, /const Tile\* tile = tile_renderer_get_tile_config\(data->grid_type, data->grid_index\);\s*if \(tile && tile->type == TILE_MEDIA\) init\.icon_name = media_tile_icon_name\(\*tile\);/);
assert.ok(!/data->icon_name/.test(renderer), 'no icon name kept from the build');
assert.ok(!/struct MediaPopupEventData \{[^}]*icon_name/.test(renderer), 'the click data holds no icon name');
assert.match(fn(renderer, 'render_media_tile'), /const String icon_name = media_tile_icon_name\(tile\);/);
assert.match(fn(renderer, 'render_media_tile'), /target\[index\]\.icon_form = 0;  \/\/ media_state_icon::Form::Plain/);

// The tile's icon follows the state; the popup's update reads the same name.
const tileRenderer = readRepoFile('src/tiles/runtime/tile_renderer.cpp');
const update = fn(tileRenderer, 'update_media_tile_state');
assert.match(update, /const media_state_icon::Form icon_form = media_state_icon::form_of\(state\);\s*widgets\.icon_form = static_cast<uint8_t>\(icon_form\);/);
assert.match(update, /if \(widgets\.icon_label && !lv_obj_has_flag\(widgets\.icon_label, LV_OBJ_FLAG_HIDDEN\)\) \{/);
assert.match(update, /getMdiChar\(media_state_icon::name\(media_tile_icon_name\(\*tile\), icon_form\)\)/);
assert.match(fn(tileRenderer, 'update_media_popup_from_widgets'), /init\.icon_name = media_tile_icon_name\(tile\);/);
assert.ok(!/media_popup_icon_for_tile/.test(tileRenderer), 'one icon name helper');

// The Bridge's icon refresh keeps the state form (a paused player sends no
// report that would bring it back).
const refresh = fn(readRepoFile('src/ui/tabs/tiles/tab_tiles_unified.cpp'), 'tiles_refresh_icons_for_grid');
assert.match(refresh, /if \(tile\.type == TILE_MEDIA && !icon_disabled && icon_name\.length\(\)\) \{\s*MediaTileWidgets\* widgets = tile_renderer_get_media_widgets\(grid_type\);\s*if \(widgets\) icon_name = media_state_icon::name\(icon_name, static_cast<media_state_icon::Form>\(widgets\[i\]\.icon_form\)\);/);

// The head: the state form of the popup's icon name, not a comparison with
// the tile's glyph.
const head = fn(readRepoFile('src/ui/popups/media/media_popup.cpp'), 'head_icon');
assert.match(head, /getMdiChar\(media_state_icon::name\(ctx->icon_name, media_state_icon::form_of\(state\)\)\)/);
assert.ok(!/!= ctx->icon_char/.test(head), 'the head does not fall back on a glyph mismatch');

// The Web Admin previews show the same form.
const mediaAdmin = readRepoFile('src/types/media/admin.js');
assert.match(mediaAdmin, /function mediaPreviewIcon\(state, iconName\) \{/);
assert.match(mediaAdmin, /\{playing: '-play', paused: '-pause', off: '-off', standby: '-off'\}/);
assert.match(mediaAdmin, /mediaPreviewIcon\(state, 'television'\)/);
for (const file of ['src/web/admin/tiles/grid-preview.js', 'src/web/admin/tiles/live-preview.js']) {
  assert.match(readRepoFile(file), /if \(previewKind === 'media'\) \{\s*iconName = mediaPreviewIcon\(parseMediaPreviewPayload\(/, file);
}
console.log('Media state icon: tile and popup head in the same state form, name read at opening, refresh keeps it');

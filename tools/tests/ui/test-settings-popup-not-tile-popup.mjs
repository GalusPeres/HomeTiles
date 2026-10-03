// The Settings popup is no tile popup (V2 hardware 2026-10-03). After a
// Weather or Media tile with the tile color "From icon"/"From cover" had
// opened a popup, saving the language in Settings reloaded the tiles; that
// tile's new color reached follow_open_popup(), which still took it for the
// opener, and the open Settings card turned olive. The visible header also
// kept the old language: the title went to the hidden body label with
// lv_label_set_text, which neither hometiles_title nor the shell copy sees.
// b203 still turned beige: every Media player update ran popup_background(),
// which made the Media tile the opener again while Settings was open, so its
// "From cover" retint on the reload recolored Settings anyway.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const read = file => maskCpp(readRepoFile(file)).replace(/\r\n?/g, '\n');
const iconSource = read('src/tiles/runtime/tile_icon_source.cpp');
const shell = read('src/ui/popups/popup_shell.cpp');
const settings = read('src/ui/tabs/settings/tab_settings.cpp');
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
const media = read('src/ui/popups/media/media_popup.cpp');

// A popup without a tile forgets the opener and the header disc options a
// tile click left behind (a folder without a PIN opens no popup).
assert.match(iconSource, /void open_popup_without_tile\(\) \{\s*remember_popup_source\(nullptr\);\s*popup_shell_use_no_tile_disc\(\);\s*\}/);
assert.match(shell, /void popup_shell_use_no_tile_disc\(\) \{ g_next_disc = \{\}; \}/);
// follow_open_popup only recolors popups opened by the changed card.
assert.ok(iconSource.includes('for (lv_obj_t* source : g_popup_source) opened_here = opened_here || source == card;'));

const open = settings.slice(settings.indexOf('static void open_settings_popup(SettingsPopupKind kind) {'));
const forget = open.indexOf('tile_icon_source::open_popup_without_tile();');
assert.ok(forget > 0, 'Settings forgets the last tile opener');
assert.ok(forget < open.indexOf('create_popup_body(') && forget < open.indexOf('show_popup_shell('),
  'before the shell takes the disc options');

// A player update reaches only an open Media popup of its entity and never
// makes its tile the opener otherwise.
const update = renderer.slice(renderer.indexOf('static void update_media_popup_from_widgets('));
const showing = update.indexOf('if (!media_popup_showing(tile.sensor_entity)) return;');
assert.ok(showing > 0, 'the update checks the open Media popup');
assert.ok(showing < update.indexOf('tile_icon_source::popup_background('), 'before popup_background');
assert.match(media, /bool media_popup_showing\(const String& entity_id\) \{[\s\S]*?return !lv_obj_has_flag\(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN\);\s*\}/);
assert.match(media, /void update_media_popup\(const MediaPopupInit& init\) \{\s*if \(!media_popup_showing\(init\.entity_id\)\) return;/);
assert.equal(renderer.split('tile_icon_source::popup_background(').length - 1, 1, 'no other renderer path sets the opener');

// Every title change reaches the visible header.
assert.match(settings, /static void set_settings_popup_title\(const char\* text\) \{\s*if \(!settings_popup_title\) return;\s*hometiles_title::set\(settings_popup_title, text\);\s*sync_popup_shell\(\);\s*\}/);
assert.doesNotMatch(settings, /lv_label_set_text\(settings_popup_title/, 'no title bypasses the shell copy');
const refresh = settings.slice(settings.indexOf('void settings_refresh_language() {'));
assert.match(refresh, /set_settings_popup_title\(popup_title_for_kind\(settings_popup_kind\)\);/);

console.log('Settings popup: no tile recolors it, a language change reaches its header');

// The Settings popup is no tile popup (V2 hardware 2026-10-03). After a
// Weather or Media tile with the tile color "From icon"/"From cover" had
// opened a popup, saving the language in Settings reloaded the tiles; that
// tile's new color reached follow_open_popup(), which still took it for the
// opener, and the open Settings card turned olive. The visible header also
// kept the old language: the title went to the hidden body label with
// lv_label_set_text, which neither hometiles_title nor the shell copy sees.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const read = file => maskCpp(readRepoFile(file)).replace(/\r\n?/g, '\n');
const iconSource = read('src/tiles/runtime/tile_icon_source.cpp');
const shell = read('src/ui/popups/popup_shell.cpp');
const settings = read('src/ui/tabs/settings/tab_settings.cpp');

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

// Every title change reaches the visible header.
assert.match(settings, /static void set_settings_popup_title\(const char\* text\) \{\s*if \(!settings_popup_title\) return;\s*hometiles_title::set\(settings_popup_title, text\);\s*sync_popup_shell\(\);\s*\}/);
assert.doesNotMatch(settings, /lv_label_set_text\(settings_popup_title/, 'no title bypasses the shell copy');
const refresh = settings.slice(settings.indexOf('void settings_refresh_language() {'));
assert.match(refresh, /set_settings_popup_title\(popup_title_for_kind\(settings_popup_kind\)\);/);

console.log('Settings popup: no tile recolors it, a language change reaches its header');

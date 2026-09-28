// Settings takes the global tile color like the home tiles: the menu tiles,
// the Back button and the popup cards follow it, also when it changes while
// Settings exists. Buttons, rows and fields inside the popups keep their own
// colors.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const settings = maskCpp(readRepoFile('src/ui/tabs/settings/tab_settings.cpp'));

assert.match(settings, /static uint32_t settings_tile_color\(\) \{\s*return tileDefaultBgColor\(\);\s*\}/,
  'Settings reads the global tile color');

const back = settings.slice(settings.indexOf('static void create_settings_back_button('),
  settings.indexOf('static void on_sleep_slider('));
assert.match(back, /style_settings_button\(btn, settings_tile_color\(\)\);\s*settings_track_tinted\(btn\);/,
  'the Back button uses and follows the global tile color');

const menuTile = settings.slice(settings.indexOf('static lv_obj_t* create_settings_menu_tile('));
assert.match(menuTile, /style_settings_button\(tile, settings_tile_color\(\)\);\s*settings_track_tinted\(tile\);/,
  'the menu tiles use and follow the global tile color');

assert.match(settings, /create_popup_body\(on_settings_popup_close_clicked, nullptr,\s*settings_tile_color\(\)\);/,
  'the Settings popup card uses the global tile color');

const timer = settings.slice(settings.indexOf('static void on_settings_tint_timer('),
  settings.indexOf('static uint16_t sleep_seconds_from_index('));
assert.match(timer, /if \(color == settings_tinted_color\) return;/, 'an unchanged color costs one compare');
assert.match(timer, /style_settings_button\(settings_tinted\[i\], color\);/);
const build = settings.slice(settings.indexOf('void build_settings_tab('));
assert.match(build, /lv_obj_clean\(tab\);\s*settings_tinted_count = 0;/, 'a rebuilt tab starts a new list');
assert.match(build, /if \(!settings_tint_timer\) \{\s*settings_tint_timer = lv_timer_create\(on_settings_tint_timer, 1000, nullptr\);/);

// Buttons inside the popups keep their neutral grey.
assert.match(settings, /static constexpr uint32_t kSystemToggleIdle = 0x424242;/);
assert.doesNotMatch(settings, /popup_surface::lighter/, 'popup buttons do not derive from the card color');

console.log('Settings: menu tiles, Back and popup cards follow the global tile color');

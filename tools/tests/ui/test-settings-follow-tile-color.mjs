// Settings takes the global tile color like the home tiles: the card, the
// category tiles and the popup cards follow it, also when it changes while
// Settings exists (the screen is rebuilt when it opens next). Buttons, rows
// and fields inside the old popups keep their own colors.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const settings = maskCpp(readRepoFile('src/ui/tabs/settings/tab_settings.cpp'));
const screen = maskCpp(readRepoFile('src/ui/tabs/settings/settings_screen.cpp'));

assert.match(settings, /static uint32_t settings_tile_color\(\) \{\s*return tileDefaultBgColor\(\);\s*\}/,
  'Settings reads the global tile color');

assert.match(settings, /uint32_t card_color\(\) \{ return settings_tile_color\(\); \}/,
  'the Settings screen gets the global tile color');

const build = screen.slice(screen.indexOf('void build_frame() {'), screen.indexOf('void select_category(Category category) {'));
assert.match(build, /g_built_card = settings_model::card_color\(\) & 0xFFFFFF;/,
  'the screen is built in the global tile color');
assert.match(build, /lv_obj_set_style_bg_color\(card, lv_color_hex\(palette\.card\), 0\);/,
  'the page card uses the global tile color');

const style = screen.slice(screen.indexOf('void style_category(uint8_t index) {'), screen.indexOf('lv_obj_t* circle('));
assert.match(style, /const uint32_t card = g_built_card;/, 'category tiles start from the card color');
assert.match(style, /selected \? settings_style::from_icon_card\(card, color\) : card/,
  'the open category is a "From icon" tile, the others keep the card color');

const show = screen.slice(screen.indexOf('void prepare_show() {'), screen.indexOf('void did_hide() {'));
assert.match(show, /\(settings_model::card_color\(\) & 0xFFFFFF\) != g_built_card/,
  'a changed global color rebuilds the screen when Settings opens');
assert.match(show, /build_frame\(\);/);

assert.match(settings, /create_popup_body\(on_settings_popup_close_clicked, nullptr,\s*settings_tile_color\(\)\);/,
  'the Settings popup card uses the global tile color');

// Buttons inside the popups keep their neutral grey.
assert.match(settings, /static constexpr uint32_t kSystemToggleIdle = 0x424242;/);
assert.doesNotMatch(settings, /popup_surface::lighter/, 'popup buttons do not derive from the card color');

console.log('Settings: card, category tiles and popup cards follow the global tile color');

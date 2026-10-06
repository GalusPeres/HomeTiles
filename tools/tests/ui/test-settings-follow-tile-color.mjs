// Settings takes the global tile color like the home tiles: the card, the
// category tiles and the popup cards follow it, also when it changes while
// Settings exists (the screen is rebuilt when it opens next). Buttons, rows
// and fields inside the old popups keep their own colors.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const settings = maskCpp(readRepoFile('src/ui/tabs/settings/tab_settings.cpp'));

assert.match(settings, /static uint32_t settings_tile_color\(\) \{\s*return tileDefaultBgColor\(\);\s*\}/,
  'Settings reads the global tile color');

const build = settings.slice(settings.indexOf('static void build_settings_screen() {'),
  settings.indexOf('static void select_settings_category(SettingsCategory category) {'));
assert.match(build, /settings_built_card = settings_tile_color\(\) & 0xFFFFFF;/,
  'the screen is built in the global tile color');
assert.match(build, /lv_obj_set_style_bg_color\(card, lv_color_hex\(colors\.card\), 0\);/,
  'the page card uses the global tile color');

const style = settings.slice(settings.indexOf('static void style_settings_category(uint8_t index) {'),
  settings.indexOf('static lv_obj_t* settings_circle('));
assert.match(style, /const uint32_t card = settings_built_card;/, 'category tiles start from the card color');
assert.match(style, /selected \? tile_tint::background\(card, color, tone_color::kReferenceTint\) : card/,
  'the open category is tinted like a "From icon" tile, the others keep the card color');

const show = settings.slice(settings.indexOf('void settings_prepare_show() {'),
  settings.indexOf('void settings_did_hide() {'));
assert.match(show, /\(settings_tile_color\(\) & 0xFFFFFF\) != settings_built_card/,
  'a changed global color rebuilds the screen when Settings opens');
assert.match(show, /build_settings_screen\(\);/);

assert.match(settings, /create_popup_body\(on_settings_popup_close_clicked, nullptr,\s*settings_tile_color\(\)\);/,
  'the Settings popup card uses the global tile color');

// Buttons inside the popups keep their neutral grey.
assert.match(settings, /static constexpr uint32_t kSystemToggleIdle = 0x424242;/);
assert.doesNotMatch(settings, /popup_surface::lighter/, 'popup buttons do not derive from the card color');

console.log('Settings: card, category tiles and popup cards follow the global tile color');

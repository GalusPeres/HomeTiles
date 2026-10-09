// The Settings System page of the approved mockup
// (build/design-mockups/settings/settings-menu.html, pageSystem and dialog()):
// the HomeTiles head with the update button, Connection, Security (Pairing,
// Web Admin password), Setup / Restart / GitHub at the card's bottom, and the
// dialogs. Every action runs the same firmware path as the former System
// popup; every text comes from the central translations.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const header = readRepoFile('src/core/i18n/i18n.h');
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
const rawScreen = readRepoFile('src/ui/tabs/settings/settings_screen.cpp');
const screen = maskCpp(rawScreen);
const parts = readRepoFile('src/ui/tabs/settings/settings_parts.cpp');
const style = readRepoFile('src/ui/tabs/settings/settings_style.h');
const model = maskCpp(readRepoFile('src/ui/tabs/settings/tab_settings.cpp'));

const between = (source, from, to) => {
  const start = source.indexOf(from);
  const end = source.indexOf(to, start);
  assert.ok(start >= 0 && end > start, `${from} .. ${to}`);
  return source.slice(start, end);
};

// --- Translations ------------------------------------------------------------------------------
const keys = ['settings_connection', 'settings_link_direct_fmt', 'settings_link_mqtt_fmt', 'settings_pairing',
  'settings_commands_encrypted', 'settings_states_encrypted', 'settings_mqtt_unused', 'settings_mqtt_unused_note',
  'settings_paired', 'settings_not_paired', 'settings_pair', 'settings_password_asks', 'settings_password_none',
  'settings_allow', 'settings_set_password_now', 'settings_setup', 'settings_unpair_question', 'settings_unpair_text',
  'settings_unpair', 'settings_restart_question', 'settings_restart_text', 'settings_github_text'];
const english = ['Connection', 'Direct · Bridge %s', 'MQTT · Broker %s', 'Pairing', 'Commands are encrypted',
  'Commands and states are encrypted', 'Not used, the direct connection is active',
  'While the direct connection is active, the panel does not connect to the broker. The fields stay saved.', 'Paired', 'Not paired', 'Pair',
  'Web Admin asks for it', 'Web Admin opens without one', 'Allow', 'Set it in Web Admin now', 'Setup',
  'Unpair from Home Assistant?', 'Home Assistant is disconnected until you pair again.', 'Unpair',
  'Restart the panel?', 'The panel is back in a few seconds.', 'Scan to open the project. A star helps it grow.'];
const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const fields = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(match => match[1]);
const first = fields.indexOf(keys[0]);
assert.deepEqual(fields.slice(first, first + keys.length), keys, 'the System strings sit together');
const tables = [...new Set([...i18n.matchAll(/\{&(kStrings\w+), &kLocale\w+\}/g)].map(match => match[1]))];
assert.deepEqual([...tables].sort(), ['kStringsDe', 'kStringsEn', 'kStringsFr', 'kStringsPl']);
for (const table of tables) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  const values = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)]
    .map(match => match[1]);
  assert.equal(values.length, fields.length, `${table} has one value per field`);
  const texts = values.slice(first, first + keys.length);
  for (const [i, text] of texts.entries()) {
    assert.ok(text.length > 0, `${table}.${keys[i]} is set`);
    assert.doesNotMatch(text, /:\s*$/, `${table}.${keys[i]} has no colon`);
  }
  if (table === 'kStringsEn') assert.deepEqual(texts, english, 'English System texts');
  // The way to Home Assistant: one %s each, the Bridge or the broker address.
  assert.match(texts[1], /^[^%]* · Bridge %s$/, `${table}: the direct link pattern`);
  assert.equal(texts[2], 'MQTT · Broker %s', `${table}: the MQTT pattern`);
}

// --- Page ---------------------------------------------------------------------------------------------
const head = between(screen, 'void build_system_head(', 'void build_system_page(');
const page = between(screen, 'void build_system_page(lv_obj_t* page) {', 'void system_tick() {');
const rawPage = between(rawScreen, 'void build_system_head(', 'void system_tick() {');
const rawBody = between(rawScreen, 'void build_system_page(lv_obj_t* page) {', 'void system_tick() {');
assert.doesNotMatch(rawPage, /"[A-Z][a-z]+/, 'no display text in the page code (product names come from settings_style)');
for (const name of ['kProductName = "HomeTiles"', 'kHomeAssistant = "Home Assistant"', 'kGitHub = "GitHub"']) {
  assert.ok(style.includes(`constexpr const char* ${name};`), name);
}
// Head: the logo image, name and version on one baseline, the update states.
assert.match(head, /lv_image_set_src\(logo, hometiles_logo_for_theme\(\)\);/);
assert.match(head, /const bool wide = g_card_width >= settings_style::kWideHeadCard;/);
assert.match(style, /constexpr int kWideHeadCard = 720;/);
assert.match(head, /settings_style::kProductName/);
assert.match(head, /settings_model::firmware_version\(\)/);
for (const key of ['system_checking', 'system_update_available_fmt', 'system_up_to_date',
  'system_check_failed', 'system_downloading', 'system_installed_restarting', 'system_install_failed',
  'system_restarting', 'settings_update_short']) {
  assert.match(head, new RegExp(`s\\.${key}\\b`), `the head shows ${key}`);
}
// The update button stays short (magnifier or download icon and "Update",
// user 2026-10-07), so the device name keeps its room on narrow cards.
assert.doesNotMatch(head, /s\.system_check_updates_btn|s\.system_install_btn_fmt/);
{
  const i18nSource = readRepoFile('src/core/i18n/i18n.cpp');
  const header = readRepoFile('src/core/i18n/i18n.h');
  const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
  const fields = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(match => match[1]);
  const index = fields.indexOf('settings_update_short');
  assert.ok(index >= 0, 'settings_update_short is a Strings field');
  const expected = {kStringsEn: 'Update', kStringsDe: 'Update', kStringsFr: 'Mise à jour', kStringsPl: 'Aktualizacja'};
  for (const [table, text] of Object.entries(expected)) {
    const start = i18nSource.indexOf(`static const Strings ${table} = {`);
    const values = [...i18nSource.slice(start, i18nSource.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)]
      .map(match => match[1]);
    assert.equal(values.length, fields.length, `${table} has one value per field`);
    assert.equal(values[index], text, `${table} settings_update_short`);
  }
}
// Connection and Security.
assert.match(page, /settings_parts::section\(page, s\.settings_connection, false\)/);
assert.match(page, /settings_parts::section\(page, s\.security_btn, false\)/);
assert.match(rawBody, /settings_parts::row\(connection, "lan-connect", settings_style::kHomeAssistant/);
// Under Home Assistant: the way to it (direct link or MQTT), kept current by
// the tick; the panel's own address is on the WLAN page.
assert.match(rawBody, /settings_model::bridge_route\(route, sizeof\(route\)\)/);
assert.match(between(rawScreen, 'void system_tick() {', 'sync_dialog(v);'),
  /settings_model::bridge_route\(route, sizeof\(route\)\)[\s\S]*lv_label_set_text\(g_route_label, route\)/);
assert.match(model, /bool bridge_route\(char\* buf, size_t len\) \{[\s\S]*linkConfigured\(\)[\s\S]*settings_link_direct_fmt[\s\S]*mqtt_host[\s\S]*settings_link_mqtt_fmt/);
// Paired over the direct link, the states are sealed too.
assert.match(rawBody, /settings_model::link_active\(\) \? s\.settings_states_encrypted : s\.settings_commands_encrypted/);
// The longer line scrolls back and forth like the media title (shared helper).
assert.match(rawBody, /ui_text_scroll::apply\(r\.sub\);\s*good_state\(r\.row, s\.settings_paired/);
assert.match(readRepoFile('src/ui/popups/media/media_popup.cpp'), /ui_text_scroll::apply\(ctx->media_title_label\);/);
for (const key of ['settings_commands_encrypted', 'settings_paired', 'pairing_discoverable', 'settings_not_paired',
  'settings_pair', 'pairing_no_answer', 'pairing_rejected', 'settings_password_asks', 'security_state_on',
  'settings_set_password_now', 'settings_password_none', 'settings_allow']) {
  assert.match(page, new RegExp(`s\\.${key}\\b`), `Security shows ${key}`);
}
// Paired and the password on: a tap on the row asks before turning them off.
assert.match(page, /make_tap\(r\.row, palette\.button, on_system_action,\s*action_data\(SystemAction::Unpair\)\)/);
assert.match(page, /make_tap\(r\.row, palette\.button, on_system_action,\s*action_data\(SystemAction::RemovePassword\)\)/);
// Paired and the password on show a green shield (security at a glance);
// Connected keeps its check.
assert.match(rawBody, /good_state\(r\.row, s\.settings_paired, "shield-check", true\);/);
assert.match(rawBody, /good_state\(r\.row, s\.security_state_on, "shield-check", true\);/);
assert.match(rawBody, /good_state\(ha\.row, s\.security_value_connected, "check", false\);/);
// Pair and Allow: one width in every language (the longest of both texts in
// all of them) and a little taller than the other row buttons.
// The width is shared with the setup's Pair (switch_on_width).
const switchWidth = between(screen, 'int switch_on_width() {', 'lv_obj_t* switch_on_button(');
assert.match(switchWidth, /for \(uint8_t i = 0; i < settings_model::language_count\(\); \+\+i\)/);
assert.match(switchWidth, /\{s\.settings_pair, s\.settings_allow\}/);
const switchOn = between(screen, 'lv_obj_t* switch_on_button(', 'void refresh_async(');
assert.match(switchOn, /settings_style::kSwitchOnHeight/);
assert.match(switchOn, /lv_obj_set_width\(b, switch_on_width\(\)\);/);
assert.match(style, /constexpr int kSwitchOnHeight = pick\(64, 42\);/);
assert.match(page, /switch_on_button\(r\.row, s\.settings_pair, SystemAction::Pair\);/);
assert.match(page, /switch_on_button\(r\.row, s\.settings_allow, SystemAction::Allow\);/);
// Setup, Restart and GitHub one grid gap under the groups (user 2026-10-07:
// not pinned to the bottom), with the tile radius: twice the radius high, at
// least a touch-friendly minimum; the free room stays at the bottom.
assert.match(rawBody, /\{s\.settings_setup, "rocket-launch-outline", SystemAction::Setup\}/);
assert.match(rawBody, /\{s\.restart_button, "restart", SystemAction::Restart\}/);
assert.match(rawBody, /\{settings_style::kGitHub, "github", SystemAction::GitHub\}/);
assert.match(style, /constexpr int kSystemButtonMin = pick\(60, 44\);/);
assert.match(page, /2 \* corner > settings_style::kSystemButtonMin \? 2 \* corner : settings_style::kSystemButtonMin/);
assert.match(page, /settings_style::apply_tile_radius\(b\);/);
assert.doesNotMatch(page, /head_height \+ free/, 'the head no longer stretches to push the buttons down');
// The page follows the state while it shows.
assert.match(between(screen, 'void build_page() {', 'void clear_refs() {'),
  /g_page_timer = lv_timer_create\(on_page_timer, 500, nullptr\);/);
assert.match(between(screen, 'void did_hide() {', 'void refresh_lines() {'), /stop_page_timer\(\);/);

// --- Dialogs -------------------------------------------------------------------------------------------
const dialogs = between(screen, 'void open_dialog(Dialog dialog) {', 'void sync_dialog(');
assert.match(dialogs, /s\.settings_unpair_question[\s\S]*?s\.settings_unpair_text[\s\S]*?ButtonKind::Danger, DialogAction::Unpair/);
assert.match(dialogs, /s\.security_password_question[\s\S]*?ButtonKind::Danger, DialogAction::RemovePassword/);
assert.match(dialogs, /s\.settings_restart_question[\s\S]*?ButtonKind::Danger, DialogAction::Restart/);
assert.match(dialogs, /settings_parts::qr_code\(box, settings_style::kDialogQr, url\)/);
assert.match(dialogs, /popup_layout::font72\(\)/, 'the pairing number is large');
assert.match(dialogs, /button_set_enabled\(confirm, g_dialog_pair == PairState::Compare\)/);
assert.match(between(screen, 'void on_outside(lv_event_t*) {', 'lv_obj_t* dialog_button('),
  /if \(g_dialog == Dialog::Pairing\) return;/, 'a tap beside the number does not cancel pairing');
for (const [name, big, small] of [['kDialogWidth', 600, 440], ['kDialogButtonHeight', 72, 48], ['kDialogQr', 210, 140]]) {
  assert.match(style, new RegExp(`constexpr int ${name} = pick\\(${big}, ${small}\\);`), name);
}
// No veil (it would draw the whole screen): the card has the popups' shadow,
// and the transparent layer for taps beside it shows and hides without
// marking the screen (popup_shell invalidate_shell).
assert.match(parts, /settings_style::apply_tile_radius\(box\);\s*\/\/[^\n]*\n\s*popup_layout::apply_card_shadow\(box\);/,
  'the popups\' shadow, after the radius it follows');
assert.doesNotMatch(parts, /kVeilOpa|lv_obj_set_style_bg_opa\(root/);
const quiet = between(parts, 'void toggle_quietly(lv_obj_t* root, bool hidden) {', '// ---------- Option list ----------');
assert.match(quiet, /lv_display_enable_invalidation\(display, false\);[\s\S]*?LV_OBJ_FLAG_HIDDEN[\s\S]*?lv_display_enable_invalidation\(display, true\);/);
assert.match(between(parts, 'void reveal(lv_obj_t* root, lv_obj_t* shown) {', 'void conceal('), /toggle_quietly\(root, false\);\s*if \(shown\) lv_obj_invalidate\(shown\);/);
assert.match(between(screen, 'void close_dialog(', 'bool address_known()'), /settings_parts::conceal\(root, lv_obj_get_child\(root, 0\)\);/);
assert.match(between(parts, 'void close_options() {', 'void reveal('), /conceal\(overlay, lv_obj_get_child\(overlay, 0\)\);/);
assert.match(parts, /settings_style::apply_tile_radius\(box\);/);

// --- Model: the former System popup's paths ---------------------------------------------------------
const values = between(model, 'SystemValues system_values() {', 'const char* latest_version()');
assert.match(values, /networkManager\.isMqttConnected\(\)/);
assert.match(values, /command_channel::pairingPhase\(\)/);
assert.match(values, /if \(g_ha_pair_callback\) g_ha_pair_callback\(\);/, 'an older Bridge finds the panel again');
assert.match(values, /web_admin_auth::firstPasswordAllowed\(\)/);
const update = between(model, 'void update_pressed() {', 'void restart() {');
assert.match(update, /g_fw_install_callback\(system_latest_tag\);/);
assert.match(update, /g_fw_check_callback\(\);/);
assert.match(between(model, 'void restart() {', 'void pair() {'), /g_system_reboot_callback\(\);/);
assert.match(between(model, 'void pair() {', 'void confirm_pairing()'), /command_channel::startPairing\(\)/);
assert.match(between(model, 'void unpair() {', 'void allow_password()'), /command_channel::disable\(&bridge_notified\)/);
assert.match(between(model, 'void allow_password() {', 'void remove_password()'), /web_admin_auth::allowFirstPassword\(\);/);
const remove = between(model, 'void remove_password() {', 'static uint8_t wifi_bars(');
assert.match(remove, /web_admin_auth::clearCredential\(\)/);
assert.match(remove, /entity_search::scheduleTilesReport\(\);/);
// Update results and the Bridge's pairing prompt reach the page.
assert.match(between(model, 'void settings_fw_check_result(', 'void settings_fw_install_progress('), /settings_screen::system_changed\(\);/);
assert.match(between(model, 'void settings_fw_install_failed(', 'void build_settings_tab('), /UpdateState::InstallFailed/);
const prompt = between(model, 'void settings_show_pairing() {', 'namespace settings_model {');
assert.match(prompt, /uiManager\.switchToTab\(3\);/);
assert.match(prompt, /settings_screen::show_category\(3\);/);

console.log('Settings System page: head, Connection, Security, buttons and dialogs on the former paths');

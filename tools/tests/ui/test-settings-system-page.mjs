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
const keys = ['settings_connection', 'settings_ip_fmt', 'settings_pairing', 'settings_commands_encrypted',
  'settings_paired', 'settings_not_paired', 'settings_pair', 'settings_password_asks', 'settings_password_none',
  'settings_allow', 'settings_set_password_now', 'settings_setup', 'settings_unpair_question', 'settings_unpair_text',
  'settings_unpair', 'settings_restart_question', 'settings_restart_text', 'settings_github_text'];
const english = ['Connection', 'IP %s', 'Pairing', 'Commands are encrypted', 'Paired', 'Not paired', 'Pair',
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
  assert.equal(texts[1], 'IP %s', `${table}: the address pattern`);
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
assert.match(head, /lv_image_set_src\(logo, &hometiles_logo_dsc\);/);
assert.match(head, /const bool wide = g_card_width >= settings_style::kWideHeadCard;/);
assert.match(style, /constexpr int kWideHeadCard = 720;/);
assert.match(head, /settings_style::kProductName/);
assert.match(head, /settings_model::firmware_version\(\)/);
for (const key of ['system_checking', 'system_update_available_fmt', 'system_install_btn_fmt', 'system_up_to_date',
  'system_check_failed', 'system_downloading', 'system_installed_restarting', 'system_install_failed',
  'system_restarting', 'system_check_updates_btn']) {
  assert.match(head, new RegExp(`s\\.${key}\\b`), `the head shows ${key}`);
}
// Connection and Security.
assert.match(page, /settings_parts::section\(page, s\.settings_connection, false\)/);
assert.match(page, /settings_parts::section\(page, s\.security_btn, false\)/);
assert.match(rawBody, /settings_parts::row\(connection, "lan-connect", settings_style::kHomeAssistant/);
for (const key of ['settings_commands_encrypted', 'settings_paired', 'pairing_discoverable', 'settings_not_paired',
  'settings_pair', 'pairing_no_answer', 'pairing_rejected', 'settings_password_asks', 'security_state_on',
  'settings_set_password_now', 'settings_password_none', 'settings_allow']) {
  assert.match(page, new RegExp(`s\\.${key}\\b`), `Security shows ${key}`);
}
// Paired and the password on: a tap on the row asks before turning them off.
assert.match(page, /make_tap\(r\.row, palette\.button, on_system_action,\s*action_data\(SystemAction::Unpair\)\)/);
assert.match(page, /make_tap\(r\.row, palette\.button, on_system_action,\s*action_data\(SystemAction::RemovePassword\)\)/);
// Setup, Restart and GitHub on the card; the head takes the space that is left.
assert.match(rawBody, /\{s\.settings_setup, "rocket-launch-outline", SystemAction::Setup\}/);
assert.match(rawBody, /\{s\.restart_button, "restart", SystemAction::Restart\}/);
assert.match(rawBody, /\{settings_style::kGitHub, "github", SystemAction::GitHub\}/);
assert.match(style, /constexpr int kSystemButtonHeight = pick\(72, 47\);/);
assert.match(page, /if \(free > 0\) lv_obj_set_height\(head_row, head_height \+ free\);/);
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
assert.match(between(screen, 'void on_veil(lv_event_t*) {', 'lv_obj_t* dialog_button('),
  /if \(g_dialog == Dialog::Pairing\) return;/, 'a tap beside the number does not cancel pairing');
for (const [name, big, small] of [['kDialogWidth', 600, 440], ['kDialogButtonHeight', 72, 48], ['kDialogQr', 210, 140]]) {
  assert.match(style, new RegExp(`constexpr int ${name} = pick\\(${big}, ${small}\\);`), name);
}
assert.match(parts, /lv_obj_set_style_bg_opa\(root, kVeilOpa, 0\);/);
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

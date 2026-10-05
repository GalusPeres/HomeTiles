// Wiring of the encrypted Bridge command channel into the firmware: sealing
// happens in the one outbound queue path, sealed Bridge messages and blocked
// plaintext are handled before any other MQTT route, and nothing changes while
// pairing is off.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const network = readRepoFile('src/network/network_manager.cpp');
const enqueue = network.slice(network.indexOf('static bool enqueueOutboundCmd('),
  network.indexOf('static void purgeOutboundQueue()'));
assert.match(enqueue, /command_channel::SealedPublish sealed;\s*if \(kind == MqttCmdKind::PUBLISH\) \{\s*switch \(command_channel::prepareOutbound\(topic, payload, payload_len, &sealed\)\)/);
assert.match(enqueue, /case command_channel::OutboundResult::Sealed:\s*topic = sealed\.topic\(\);\s*payload = sealed\.payload\(\);\s*payload_len = sealed\.length\(\);\s*retain = false;/);
assert.ok(enqueue.indexOf('prepareOutbound') < enqueue.indexOf('mqttAllocOutbound'),
  'commands are sealed before they are copied into the queue');

const handlers = readRepoFile('src/network/mqtt/mqtt_handlers.cpp');
const process = handlers.slice(handlers.indexOf('static void processMqttMessage(char* topic, uint8_t* payload, unsigned int length) {\n'));
const first = process.indexOf('command_channel::handleMqttMessage(topic, payload, length)');
assert.ok(first > 0 && first < process.indexOf('hardwareIo.handleMqttMessage'),
  'sealed Bridge messages are handled before every plain route');
assert.ok(process.indexOf('command_channel::blocksPlaintext(topic)') < process.indexOf('local_camera::handleMqttMessage'),
  'plain stream-token topics are blocked before the camera handlers while pairing is active');
assert.match(handlers, /local_camera::onMqttConnected\(\);\s*\/\/[^\n]*\n\s*command_channel::onMqttConnected\(\);/);

const sketch = readRepoFile('HomeTiles.ino');
assert.match(sketch, /local_camera::begin\(\);\s*\/\/[^\n]*\n\s*command_channel::begin\(\);/);
assert.equal((sketch.match(/command_channel::service\(\);/g) || []).length, 2, 'active and sleep loops service the channel');

const channel = readRepoFile('src/network/secure/command_channel.cpp');
assert.match(channel, /MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT/, 'pairing state and buffers prefer PSRAM');
assert.match(channel, /if \(!g_state \|\| g_state->pairing != PairingState::Active \|\| !sealed\) \{\s*return OutboundResult::Plain;/,
  'without an active pairing every publish stays unchanged');
assert.match(channel, /xTaskGetCurrentTaskHandle\(\) != g_owner/, 'sealing stays on the loop task');
for (const match of channel.matchAll(/Serial\.printf?\(([^;]*)\);/g)) {
  const argumentsOnly = match[1].replace(/"(?:\\.|[^"\\])*"/g, '""');
  assert.doesNotMatch(argumentsOnly, /->code\b|panel_to_bridge|bridge_to_panel|->challenge|->session\b|header\.session/,
    `the pairing code, keys, challenges and session ids are never logged: ${match[1]}`);
}
assert.match(channel, /strcmp\(header\.name, g_state->challenge\) != 0/, 'a session must answer the current challenge');
assert.match(channel, /acceptSequence\(g_state->bridge_window, header\.seq\)/, 'Bridge data is replay-checked');
// Removing the pairing on either side turns it off on the other side too.
assert.match(channel, /case MessageType::Unpair:\s*handleUnpair\(header\);/);
const handleUnpair = channel.slice(channel.indexOf('void handleUnpair('), channel.indexOf('void handleData('));
assert.match(handleUnpair, /if \(!acceptInSession\(header\)\) return;\s*turnOff\(false, nullptr\);/,
  'a Bridge unpair counts only inside the current session and replay window');
const turnOff = channel.slice(channel.indexOf('bool turnOff('), channel.indexOf('void handleUnpair('));
assert.ok(turnOff.indexOf('writeRecord(nullptr)') < turnOff.indexOf('sendUnpair()') &&
  turnOff.indexOf('sendUnpair()') < turnOff.indexOf('releaseState()'),
  'the stored pairing is removed first, the Bridge is told while the keys still exist, then the keys go');
const sendUnpair = channel.slice(channel.indexOf('bool sendUnpair()'), channel.indexOf('bool turnOff('));
assert.match(sendUnpair, /!g_state->has_session/, 'unpair needs a session');
assert.match(sendUnpair, /header\.seq = g_state->next_seq\+\+;/, 'unpair is numbered like a command');
assert.match(channel, /bool disable\(bool\* bridge_notified\) \{\s*begin\(\);\s*return turnOff\(true, bridge_notified\);/);
assert.match(readRepoFile('src/ui/tabs/settings/tab_settings.cpp'), /command_channel::disable\(&bridge_notified\)[\s\S]{0,400}bridge_notified \? nullptr : tr\(\)\.security_unpaired_offline/,
  'the hint to remove the pairing in Home Assistant appears only when the Bridge could not be told');
// A rekey means the Bridge does not know the session (restart, reload). The
// panel kept its old session, so service() never sent a hello and the Bridge
// refused every command (V2 camera: "No camera response").
assert.match(channel, /case MessageType::Rekey:(?:\s*\/\/[^\n]*)*\s*resetSession\(\);/,
  'a rekey drops the old session');
const resetSession = channel.slice(channel.indexOf('void resetSession() {'), channel.indexOf('bool writeRecord('));
for (const line of ['g_state->has_session = false;', 'g_state->hello_attempts = 0;', 'g_state->hello_requested = true;'])
  assert.ok(resetSession.includes(line), `resetSession: ${line}`);
assert.match(channel, /if \(g_state->has_session\) return;[\s\S]{0,700}if \(g_state->hello_requested \|\| retry_due\) sendHello\(\);/,
  'without a session service() sends the hello');

// Every Bridge message that authenticates but is not used leaves a
// rate-limited trace, so a stuck pairing can be diagnosed from the panel log.
for (const line of ['Session answer for an old request ignored', 'Bridge asked for a new session',
  'Bridge message ignored (unreadable header)']) {
  const index = channel.indexOf(`Serial.println("[SecureCmd] ${line}")`);
  assert.ok(index > 0, `the panel logs "${line}"`);
  assert.match(channel.slice(Math.max(0, index - 80), index), /if \(logDue\(&g_state->last_(?:rekey_)?log_ms\)\) \{\s*$|\} else if \(logDue\(&g_state->last_log_ms\)\) \{\s*$/,
    `"${line}" is rate-limited`);
}

const camera = readRepoFile('src/video/local_camera/local_camera.cpp');
assert.match(camera, /bool handleMqttMessage\(const char\* topic, const uint8_t\* payload, size_t length\) \{\s*if \(!isCommandTopic\(topic\)\) return false;\s*handleCommandPayload\(payload, length\);\s*return true;\s*\}/);

const settings = readRepoFile('src/ui/tabs/settings/tab_settings.cpp');
assert.match(settings, /const bool has_number = command_channel::pairingNumber\(number\);/,
  'the number is shown only while the attempt has one');
assert.match(settings, /if \(security_refresh_timer\) \{\s*lv_timer_del\(security_refresh_timer\);\s*security_refresh_timer = nullptr;\s*\}\s*if \(networkTransport\.isWifiDriverActive\(\)\) WiFi\.scanDelete\(\);/,
  'closing the popup deletes the refresh timer');
for (const key of ['system_updates_btn', 'system_install_btn', 'security_value_connected', 'security_encryption_label',
  'security_value_offline', 'security_hint_pair', 'security_hint_unpair',
  'security_pair_short', 'security_unpair_short', 'web_auth_window_open',
  'security_password_btn', 'security_unpair_question', 'security_unpair_question_hint',
  'security_password_question', 'security_password_question_hint', 'security_remove', 'security_cancel',
  'security_confirm', 'security_close', 'security_unpaired_offline', 'pairing_title', 'pairing_asking',
  'pairing_compare', 'pairing_compare_hint', 'pairing_waiting', 'pairing_no_answer', 'pairing_no_answer_hint',
  'pairing_already_paired', 'pairing_busy', 'pairing_rejected', 'pairing_failed', 'web_auth_section',
  'security_btn', 'restart_button']) {
  assert.match(settings, new RegExp(`tr\\(\\)\\.${key}`), `${key} comes from i18n`);
}
// System popup: every view keeps exactly two button rows and the branding.
const systemPopup = settings.slice(settings.indexOf('static void build_system_popup('),
  settings.indexOf('static const char* popup_title_for_kind('));
assert.equal((systemPopup.match(/= create_system_button_row\(box\);|create_system_button_row\(box\);/g) || []).length, 3,
  'Updates/Restart, the Security actions and GitHub/Security');
assert.match(systemPopup, /create_system_icon_button\(system_action_row, "magnify"[\s\S]*create_system_icon_button\(system_action_row, "restart"/);
assert.match(systemPopup, /create_system_icon_button\(link_row, "github"[\s\S]*create_system_icon_button\(link_row, "shield-lock"/);
assert.match(systemPopup, /constexpr bool kCompactSystem = SCREEN_HEIGHT < popup_layout::scale\(780\);\s*if \(kCompactSystem\) lv_obj_set_style_pad_top\(parent, 0, 0\);\s*lv_obj_set_style_pad_top\(box, kCompactSystem \? 0 : popup_layout::scale\(20\), 0\);/,
  'the branding sits just below the header, right at it on displays lower than 800 layout pixels');
// 1280x800 keeps the spacing; 480x480 (2/3 scale), 1024x600 (5/6) and the
// 720-high layouts are compact.
const compact = (height, scale) => height < Math.round(780 * scale);
assert.ok(!compact(800, 1) && compact(720, 1) && compact(480, 2 / 3) && compact(600, 5 / 6));
assert.doesNotMatch(systemPopup, /system_pair_btn|"Pairing"/, 'pairing lives in the Security view');
const applyView = settings.slice(settings.indexOf('static void system_apply_view() {'), settings.indexOf('static void system_set_view('));
assert.match(applyView, /system_set_hidden\(system_action_row, !main\);\s*system_set_hidden\(security_action_row, !security\);/,
  'the first button row belongs to the current view');
assert.doesNotMatch(applyView, /system_brand/, 'the branding stays in every view');
assert.match(settings, /security_set_buttons\("close", tr\(\)\.security_cancel, 0x424242, "lock-open-variant",\s*tr\(\)\.security_remove, 0xC62828\);/,
  'removing the password asks first and is red');
assert.match(settings, /security_set_buttons\("close", tr\(\)\.security_cancel, 0x424242, "shield-off",\s*tr\(\)\.security_unpair_short, 0xC62828\);/,
  'turning encryption off asks first and is red');
// Password is always offered: it removes a set password, and without one it
// allows the first one in Web Admin for two minutes (like Pair).
assert.match(settings, /const char\* password_icon = password_on \? "lock-open-variant" : "lock-plus";/);
assert.match(settings, /if \(web_admin_auth::enabled\(\)\) \{\s*security_set_message\(nullptr, 0xA8A8A8\);\s*security_step = SecurityStep::ConfirmPassword;\s*\} else \{[\s\S]*?web_admin_auth::allowFirstPassword\(\);\s*security_set_message\(tr\(\)\.web_auth_window_open, 0x4DB6AC\);/,
  'without a password the button opens the window for the first one');
// The device name stays under the branding. One row per fact (Home Assistant
// connected with a check, Encryption and Web Admin password with a shield)
// sits centered in the middle area, which only takes the free space, so the
// buttons never move; a question or the pairing number takes the rows' place
// with the same line height and gap.
const rows = settings.slice(settings.indexOf('static void system_refresh_rows() {'),
  settings.indexOf('static constexpr uint32_t kSystemToggleActive'));
assert.match(rows, /connected \? tr\(\)\.security_value_connected : tr\(\)\.security_value_offline/);
assert.match(rows, /lv_obj_set_style_text_opa\(security_ha_check, connected \? LV_OPA_COVER : LV_OPA_TRANSP, 0\);/,
  'the check keeps its place while offline, so the texts stay aligned');
assert.match(rows, /system_set_row\(security_encryption_value, security_encryption_icon,\s*encrypted \? tr\(\)\.security_state_on : tr\(\)\.security_state_off, encrypted\);/,
  'a green shield means encrypted');
assert.match(rows, /if \(state == system_rows_state\) return;/, 'the timer redraws the rows only on a change');
assert.match(systemPopup, /system_device_name = lv_label_create\(head\);\s*lv_label_set_text\(system_device_name, Device::displayName\(\)\);/);
assert.match(systemPopup, /lv_obj_set_flex_grow\(system_middle, 1\);/);
assert.match(systemPopup, /if \(kCompactSystem\) lv_obj_set_style_pad_bottom\(system_middle, popup_layout::scale\(24\), 0\);/,
  'lower displays center the rows a little higher');
for (const parent of ['system_info_rows', 'security_prompt_box', 'security_pair_box']) {
  assert.match(systemPopup, new RegExp(`${parent} = create_centered_column\\(system_middle, system_line_gap\\(\\)\\);`),
    `${parent} shares the middle area and the line gap`);
}
assert.match(systemPopup, /create_security_row\(system_info_rows, tr\(\)\.security_encryption_label,/);
assert.match(settings, /lv_obj_t\* row = create_system_line\(parent, true\);/, 'every row is one line high');
assert.match(applyView, /system_set_hidden\(system_middle, qr\);\s*system_set_hidden\(system_spacer, !qr\);/,
  'the QR view keeps its place under the branding');
assert.match(applyView, /system_set_hidden\(system_info_rows, !\(main \|\| list\)\);/);
// The status area always keeps two lines (messages, the Security hint or the
// download progress), so neither a message nor another view moves the rows.
assert.match(systemPopup, /lv_obj_set_height\(system_status_row,\s*2 \* lv_font_get_line_height\(popup_layout::font24\(\)\)\);/);
assert.match(systemPopup, /security_hint_label = create_centered_label\(system_status_row,/);
assert.match(systemPopup, /system_progress_bar = lv_bar_create\(system_status_row\);/);
// While pairing, the title and the number replace the rows; the instruction
// and its hint use the status area, so the number fits on 480x480.
assert.match(systemPopup, /security_pair_text = create_centered_label\(system_status_row,/);
assert.match(systemPopup, /security_pair_hint = create_centered_label\(system_status_row,/);
assert.match(applyView, /system_set_hidden\(security_pair_text, !pairing\);\s*system_set_hidden\(security_pair_hint, !pairing \|\| !pair_hint \|\| !pair_hint\[0\]\);/);
assert.match(applyView, /system_set_hidden\(system_status_row, qr\);\s*system_set_hidden\(system_status_label, !main\);\s*system_set_hidden\(security_hint_label, !list\);/);
assert.match(applyView, /system_set_toggle\(system_github_btn, &system_github_color, qr\);\s*system_set_toggle\(system_security_btn, &system_security_color, security\);/,
  'GitHub and Security are colored while their view is open');
assert.doesNotMatch(settings, /system_status_icon|system_show_pairing_status|link-variant-off/,
  'the status line holds only messages; the buttons use the Security shields');
// The retained announcement is signed while a code exists and republished
// whenever the code changes; without pairing it stays byte-identical.
const announce = network.slice(network.indexOf('void HomeTilesNetworkManager::publishBridgeConfig() {'),
  network.indexOf('const char* HomeTilesNetworkManager::getBridgeApplyTopic()'));
assert.match(announce, /command_channel::signAnnouncement\(topic\.c_str\(\), payload\.c_str\(\), payload\.length\(\)\)/);
assert.match(announce, /is_signed \? signed_payload : payload\.c_str\(\)/);
assert.match(announce, /if \(signed_payload\) heap_caps_free\(signed_payload\);/);
assert.ok(announce.indexOf('mqttEnqueuePublishWithLargeBuffer') < announce.indexOf('heap_caps_free(signed_payload)'),
  'the signed copy is freed only after the queue copied it');
const signer = channel.slice(channel.indexOf('char* signAnnouncement(const char* topic, const char* payload, size_t length) {'));
assert.match(signer, /if \(!g_state \|\| !topic \|\| !payload \|\|\s*xTaskGetCurrentTaskHandle\(\) != g_owner\) \{\s*return nullptr;/,
  'no pairing (or a foreign task) leaves the announcement unsigned');
assert.match(signer, /allocPreferPsram\(size\)/);
const complete = channel.slice(channel.indexOf('void completePairing() {'), channel.indexOf('void handlePairMessage('));
// Turning off (on the display or by the Bridge) republishes the unsigned announcement.
const off = channel.slice(channel.indexOf('bool turnOff('), channel.indexOf('void handleUnpair('));
assert.match(complete, /networkManager\.publishBridgeConfig\(\);/);
assert.match(off, /releaseState\(\);[\s\S]*networkManager\.publishBridgeConfig\(\);/);

// Number-comparison pairing (contract v2).
const start = channel.slice(channel.indexOf('bool startPairing() {'), channel.indexOf('PairingPhase pairingPhase() {'));
assert.match(start, /if \(g_state\) return false;/, 'a paired panel unpairs first');
assert.ok(start.indexOf('x25519::selfTest()') < start.indexOf('allocPreferPsram(sizeof(Attempt))'),
  'the RFC 7748 self-test runs before every attempt');
for (const field of ['secret', 'n_p']) {
  assert.match(start, new RegExp(`secure_random::fill\\(g_attempt->${field}, sizeof\\(g_attempt->${field}\\)\\);`),
    `every attempt has a fresh ${field}`);
}
assert.match(start, /secure_random::fill\(id, sizeof\(id\)\);/, 'every attempt has a fresh id');
const pairHandler = channel.slice(channel.indexOf('void handlePairMessage('), channel.indexOf('void servicePairing() {'));
assert.match(pairHandler, /strcmp\(message\.id, g_attempt->id\) != 0\) \{\s*return;/, 'other attempts are ignored');
assert.match(pairHandler, /if \(attempt\.phase != PairingPhase::Asking \|\| attempt\.have_commit\) return;/,
  'only the first commit counts');
assert.match(pairHandler, /pairingCommit\(attempt\.pk_b, attempt\.pk_p, message\.n, expected\);\s*if \(!ht_crypto::equalConstantTime\(expected, attempt\.commit/,
  'the Bridge nonce must open its commitment, compared in constant time');
assert.match(pairHandler, /pairingTranscript\(attempt\.base,/, 'the transcript binds the base topic');
assert.match(pairHandler, /equalConstantTime\(message\.m, attempt\.confirm_bridge/, 'the Bridge confirmation is checked in constant time');
assert.match(pairHandler, /if \(attempt\.local_confirmed\) completePairing\(\);/, 'pairing needs both confirmations');
assert.match(pairHandler, /case PairType::Abort:[\s\S]{0,200}if \(!attemptRunning\(\)\) return;/,
  'an abort never undoes a finished pairing');
const confirm = channel.slice(channel.indexOf('void confirmPairing() {'), channel.indexOf('void endPairing() {'));
assert.match(confirm, /if \(g_attempt->peer_confirmed\) completePairing\(\);/);
const servicePair = channel.slice(channel.indexOf('void servicePairing() {'), channel.indexOf('}  // namespace\n'));
assert.match(servicePair, /if \(!attempt\.start_sent && networkManager\.isMqttConnected\(\)\)/, 'start goes out once');
assert.match(servicePair, /age >= kPairNoAnswerMs\) \{[\s\S]{0,200}endAttempt\(PairingPhase::NoAnswer, "timeout"\);/);
assert.match(servicePair, /if \(age >= kPairTimeoutMs\) \{[\s\S]{0,120}endAttempt\(PairingPhase::Failed, "timeout"\);/);
assert.match(servicePair, /now - attempt\.last_confirm_ms\) >= kPairConfirmRepeatMs\) \{\s*publishPair\("confirm"/,
  'the confirmation repeats until the Bridge confirms (QoS 0)');
assert.match(channel, /constexpr uint32_t kPairNoAnswerMs = 15000;\s*constexpr uint32_t kPairTimeoutMs = 120000;\s*constexpr uint32_t kPairConfirmRepeatMs = 2000;/);
for (const match of channel.matchAll(/Serial\.printf?\(([^;]*)\);/g)) {
  const argumentsOnly = match[1].replace(/"(?:\\.|[^"\\])*"/g, '""');
  assert.doesNotMatch(argumentsOnly, /secret|->key\b|\.key\b|transcript|confirm_(?:panel|bridge)|number|n_p|\.n\b/,
    `pairing secrets and the number are never logged: ${match[1]}`);
}
const begin = channel.slice(channel.indexOf('void begin() {'), channel.indexOf('PairingState state() {'));
assert.match(begin, /if \(stored == kLegacyRecordSize\) \{[\s\S]{0,200}writeRecord\(nullptr\);\s*g_clear_status = true;/,
  'a v1 record with a typed code is discarded');
// Turning encryption off must reach the Bridge even when the connection
// drops: the clear repeats until it is queued and goes out on every connect.
const publishStatus = channel.slice(channel.indexOf('void publishStatus() {'), channel.indexOf('// Seals header + body'));
assert.match(publishStatus, /if \(networkManager\.mqttEnqueuePublish\(topic\.c_str\(\), "", true\)\) g_clear_status = false;/,
  'only a queued clear ends the retry');
const connected = channel.slice(channel.indexOf('void onMqttConnected() {'), channel.indexOf('void service() {'));
assert.match(connected, /if \(!g_state\) \{[\s\S]{0,400}publishStatus\(\);\s*return;\s*\}/,
  'an unpaired panel clears its status on every connect');
assert.doesNotMatch(connected, /if \(g_clear_status\) publishStatus\(\);/,
  'the clear on connect no longer depends on a flag that a lost publish reset');
const serviceFn = channel.slice(channel.indexOf('void service() {'));
assert.match(serviceFn, /if \(!g_state\) \{[\s\S]{0,300}if \(g_clear_status && networkManager\.isMqttConnected\(\) &&\s*static_cast<uint32_t>\(now - last_clear_ms\) >= kClearRetryMs\) \{\s*last_clear_ms = now;\s*publishStatus\(\);/,
  'a clear that could not be queued is retried every 5 s');
assert.match(channel, /constexpr uint32_t kClearRetryMs = 5000;/);
const x25519 = readRepoFile('src/core/security/x25519.cpp');
assert.match(x25519, /#include <mbedtls\/ecp\.h>/);
assert.match(x25519, /#error/, 'a build without Curve25519 fails loudly');
assert.match(x25519, /MBEDTLS_ECP_DP_CURVE25519/);
assert.match(x25519, /mbedtls_ecp_mul\(/, 'the curve arithmetic stays in mbedTLS');
assert.match(x25519, /0x4a, 0x5d, 0x9d, 0x5b/, 'the self-test uses the RFC 7748 section 6.1 shared secret');

const doc = readRepoFile('docs-dev/command-encryption.md');
for (const marker of ['secure/panel', 'secure/bridge', 'stat/secure', 'HomeTiles command pairing v2',
  'pair/panel', 'pair/bridge', 'HomeTiles pairing commit v2', 'HomeTiles pairing number v2',
  'panel-to-bridge', 'bridge-to-panel', 'key-id', 'announce', '"sig"', 'ChaCha20-Poly1305', 'replay']) {
  assert.ok(doc.includes(marker), `protocol document covers ${marker}`);
}
console.log('Command channel wiring: outbound sealing, inbound routing, lifecycle and UI passed');

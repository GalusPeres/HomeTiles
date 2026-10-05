// The picker searches through the Bridge, the panel declares every entity its
// tiles use (like an ESPHome device names the Home Assistant states it
// needs), and the first Web Admin password needs a tap on the display. Search
// and declaration travel only sealed (paired panel, Bridge announcing
// "entity_search"); the Bridge decides with the Web Admin password claim like
// for Lock and Alarm (HomeTiles Bridge entity_search.py, whose FlowTests run
// both sides through restarts and changes). Source contracts of the panel
// side; the picker's use of it runs in test-entity-picker.mjs.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// ---- Sealed only ----
const core = read('src/network/secure/command_channel_core.h');
assert.match(core, /"alarm", "entities", "tiles"\};/, 'search and report are sealed commands');
const maxBody = Number(core.match(/kMaxBodyLength = (\d+)/)?.[1]);
assert.ok(maxBody >= 2048, 'sealed body limit');
const channel = read('src/network/secure/command_channel.cpp');
assert.match(channel, /\} else if \(strcmp\(header\.name, "entities"\) == 0\) \{\s*entity_search::handleAnswer\(body, length\);/,
  'sealed "entities" data reaches the search');
assert.match(channel, /\} else if \(strcmp\(header\.name, "tiles"\) == 0\) \{\s*entity_search::handleDeclarationAck\(body, length\);/,
  'sealed "tiles" data acknowledges a declaration');
assert.match(channel, /servicePairing\(\);\s*entity_search::service\(\);\s*if \(!g_state\) \{/, 'the declaration runs every loop');

// ---- Module ----
const search = read('src/network/bridge/entity_search.cpp');
assert.match(search, /bool available\(\) \{\s*return command_channel::state\(\) == command_channel::PairingState::Active &&\s*haBridgeConfig\.supportsEntitySearch\(\);/,
  'only paired with a Bridge that searches, never plain');
assert.match(search, /if \(!available\(\) \|\| !knownList\(list\)\) return 0;/);
assert.match(search, /if \(offset\) doc\["o"\] = offset;/, 'the picker\'s next page starts at its offset');
assert.match(search, /doc\["web_auth"\] = web_admin_auth::enabled\(\);[\s\S]*publish\("entities", body\);/,
  'the search claims the password like Lock and Alarm');
assert.match(search, /g_answer\.received != \(1u << g_answer\.parts\) - 1u/, 'an answer is complete only with every part');
assert.match(search, /if \(!id \|\| id != g_answer\.id \|\| parts < 1 \|\| parts > kMaxParts \|\| part < 0 \|\| part >= parts\) return;/,
  'parts of older searches and invalid parts are ignored');
assert.match(search, /constexpr uint8_t kMaxParts = 6;/);
assert.match(search, /g_answer\.received = 0;\s*g_answer\.full = false;\s*g_answer\.more = false;/,
  'a new search forgets the previous "more matches"');
// The declaration depends on the tiles and the password alone. The Bridge's
// configuration is read only to know whether it searches and takes
// declarations; nothing it serves can change the next declaration (the loop
// of b232-b235: a reported entity came back released, was dropped from the
// next report, removed by the Bridge and reported again).
assert.equal((search.match(/haBridgeConfig\./g) || []).length, 2, 'only the two capability checks read the Bridge');
assert.match(search, /bool declares\(\) \{\s*return command_channel::state\(\) == command_channel::PairingState::Active &&\s*haBridgeConfig\.supportsEntityDeclarations\(\);/);
assert.doesNotMatch(search, /released|g_reported|g_last_report|parseSensorList/, 'no released lists, no sticky report state');
const collect = search.slice(search.indexOf('std::vector<Declared> collectDeclared()'), search.indexOf('uint32_t fnv1a('));
assert.match(collect, /declare\(lists, listOfType\(slots\[i\]\.type\), String\(slots\[i\]\.entity\)\);/, 'every folder tile');
assert.match(collect, /const String source\(slots\[i\]\.rule_entity\);\s*declare\(lists, listOfSource\(source\), source\);/,
  'icon color sources too');
assert.match(collect, /screensaverConfig\.tileGrid\(\)[\s\S]*declare\(lists, listOfType\(tile\.type\), tile\.sensor_entity\);[\s\S]*tileIconSourceEntity\(tile\.type, tile\.icon_colors\)/,
  'screensaver tiles and their icon color sources');
assert.match(collect, /std::sort\(item\.ids\.begin\(\), item\.ids\.end\(\)\);/, 'sorted: the same tiles, the same version');
const partBytes = Number(search.match(/kMaxPartBytes = (\d+)/)?.[1]);
assert.ok(partBytes > 0 && partBytes < maxBody, 'every part fits one sealed message');
assert.match(search, /constexpr size_t kMaxDeclarationParts = 8;/, 'entity_search.py MAX_DECLARATION_PARTS');
assert.match(search, /constexpr size_t kMaxDeclaredEntities = 300;/, 'entity_search.py MAX_PANEL_ENTITIES');
assert.match(search, /if \(added > kMaxPartBytes\) \{\s*\+\+dropped;[^}]*continue;\s*\}/, 'one overlong value never overflows a part');
assert.match(search, /if \(parts\.size\(\) == kMaxDeclarationParts\) \{\s*\+\+dropped;\s*continue;\s*\}/);
assert.match(search, /array\.add\(id\);\s*version = fnv1a\(version, String\('\|'\) \+ item\.list \+ ',' \+ id\);/,
  'the version covers exactly what is declared');
assert.match(search, /void handleDeclarationAck[\s\S]*?if \(g_sent && version == g_sent_version\) \{\s*g_acked = true;\s*g_acked_version = version;/,
  'only the acknowledgement of the sent version counts');
const service = search.slice(search.indexOf('void service() {'));
assert.match(service, /if \(ready && !g_session_seen\) \{[\s\S]*?g_sent = false;\s*g_acked = false;\s*scheduleTilesReport\(\);/,
  'every new session declares again');
assert.match(service, /if \(!ready \|\| !declares\(\)\) return;/);
assert.match(service, /const bool retry = unconfirmed && now - g_sent_ms >= kDeclarationRetryMs;/, 'repeated until acknowledged');
assert.match(service, /if \(g_acked && version == g_acked_version\) return;/, 'an acknowledged declaration is not sent again');
assert.match(service, /for \(const String& part : parts\) publish\("tiles", part\);/);
for (const [list, type] of [['sensors', 'TILE_SENSOR'], ['switches', 'TILE_SWITCH'], ['locks', 'TILE_LOCK'],
  ['alarm_panels', 'TILE_ALARM'], ['fans', 'TILE_FAN'], ['media', 'TILE_MEDIA']]) {
  assert.ok(search.includes(`{"${list}", ${type}}`), `${list} <- ${type}`);
}
const routes = read('src/network/mqtt/mqtt_handlers.cpp');
assert.match(routes, /networkManager\.setMqttMediaBufferNeeded\(has_media_tiles\);[\s\S]{0,300}entity_search::scheduleTilesReport\(\);/,
  'changed tiles look at the declaration again (sent only when it changed)');
const config = read('src/network/bridge/ha_bridge_config.cpp');
assert.match(config, /entity_search_ = digit \? static_cast<uint8_t>\(json\[at\] - '0'\) : 0;/,
  'the Bridge announces its level in its configuration');
const configHeader = read('src/network/bridge/ha_bridge_config.h');
assert.match(configHeader, /bool supportsEntitySearch\(\) const \{ return entity_search_ >= 1; \}\s*bool supportsEntityDeclarations\(\) const \{ return entity_search_ >= 2; \}/,
  'Bridge v0.9.0b11/b12 (level 1) search but get no declarations');

// ---- Web Admin endpoints ----
const admin = read('src/web/server/web_admin.cpp');
assert.match(admin, /"\/api\/entity_search", HTTP_POST,\s*guarded\(\[this\]\(\) \{ this->handleStartEntitySearch\(\); \}\)\);/);
assert.match(admin, /"\/api\/entity_search", HTTP_GET,\s*guarded\(\[this\]\(\) \{ this->handleGetEntitySearch\(\); \}\)\);/);
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tiles, /json \+= id \? "true,\\"id\\":" \+ String\(id\) : String\("false"\);/, 'no Bridge search: the picker keeps its list');
assert.match(tiles, /if \(!entity_search::result\(id, answer\)\) \{\s*server\.send\(200, "application\/json", "\{\\"success\\":true,\\"ready\\":false\}"\);/);

// ---- First Web Admin password only after a tap on the display ----
const auth = read('src/web/server/auth/web_admin_auth.cpp');
assert.match(auth, /constexpr uint32_t kFirstPasswordWindowMs = 120000;/, 'two minutes, like Pair');
assert.match(auth, /if \(saved\) \{\s*\/\/[^\n]*\n\s*g_first_password_open = false;/, 'a set password closes the window');
const handlers = read('src/web/server/handlers/web_admin_auth_handlers.cpp');
const passwordHandler = handlers.slice(handlers.indexOf('void WebAdminServer::handleAuthPassword()'));
assert.ok(passwordHandler.indexOf('firstPasswordAllowed()') < passwordHandler.indexOf('setCredential('),
  'the first password is refused before anything is stored');
assert.match(passwordHandler, /\} else if \(!web_admin_auth::firstPasswordAllowed\(\)\) \{[\s\S]*?sendAuthError\(server, 403, "panel_tap_required", "panel"\);\s*return;/);
assert.match(passwordHandler, /if \(web_admin_auth::enabled\(\)\) \{\s*if \(!authorizeRequest\(\)\) return;\s*\}/,
  'changing a set password still needs the session');
assert.equal((passwordHandler.match(/entity_search::scheduleTilesReport\(\);/g) || []).length, 2,
  'setting and removing the password declares again');
const settings = read('src/ui/tabs/settings/tab_settings.cpp');
assert.match(settings, /web_admin_auth::allowFirstPassword\(\);\s*security_set_message\(tr\(\)\.web_auth_window_open, 0x4DB6AC\);/);
const html = read('src/web/server/render/web_admin_security_html.cpp');
assert.match(html, /if \(!enabled\) \{[\s\S]*?id="web_auth_tap_note"[\s\S]*?tr\.web_auth_panel_tap_required/,
  'Web Admin says where to tap before the first password');
const authJs = read('src/web/assets/auth.js');
assert.match(authJs, /else passwordErrorCode = String\(\(await response\.json\(\)\.catch\(\(\) => \(\{\}\)\)\)\.error \|\| ''\);/);
assert.match(read('src/web/admin/settings/web-password.js'),
  /t\(auth\.passwordError\?\.\(\) === 'panel_tap_required' \? 'webAuthPanelTap' : 'webAuthChangeFailed'\)/);

console.log('Entity search: sealed transport, search, declaration, endpoints and first password window pass');

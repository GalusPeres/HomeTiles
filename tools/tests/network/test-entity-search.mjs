// The picker searches through the Bridge, the panel reports its tiles' own
// entities, and the first Web Admin password needs a tap on the display.
// Search and report travel only sealed (paired panel, Bridge announcing
// "entity_search"); the Bridge decides with the Web Admin password claim like
// for Lock and Alarm (HomeTiles Bridge entity_search.py). Source contracts of
// the panel side; the picker's use of it runs in test-entity-picker.mjs.
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
assert.match(channel, /servicePairing\(\);\s*entity_search::service\(\);\s*if \(!g_state\) \{/, 'the report runs every loop');

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
const reportLimit = Number(search.match(/kMaxReportBytes = (\d+)/)?.[1]);
assert.ok(reportLimit > 0 && reportLimit < maxBody, 'the report fits one sealed message');
assert.match(search, /if \(!contains\(slot->released, id\) && !contains\(slot->ids, id\)\) slot->ids\.push_back\(id\);/,
  'only entities beyond the released lists are reported');
assert.match(search, /screensaverConfig\.tileGrid\(\)/, 'screensaver tiles count too');
assert.match(search, /if \(ready && !g_session_seen\) \{[\s\S]*?g_last_report = "";\s*scheduleTilesReport\(\);/,
  'every new session reports again (the Bridge keeps the extras in memory)');
assert.match(search, /if \(body == g_last_report\) return;\s*publish\("tiles", body\);/, 'unchanged reports are not repeated');
for (const [list, type] of [['sensors', 'TILE_SENSOR'], ['switches', 'TILE_SWITCH'], ['locks', 'TILE_LOCK'],
  ['alarm_panels', 'TILE_ALARM'], ['fans', 'TILE_FAN'], ['media', 'TILE_MEDIA']]) {
  assert.ok(search.includes(`{"${list}", ${type}}`), `${list} <- ${type}`);
}
const routes = read('src/network/mqtt/mqtt_handlers.cpp');
assert.match(routes, /networkManager\.setMqttMediaBufferNeeded\(has_media_tiles\);[\s\S]{0,300}entity_search::scheduleTilesReport\(\);/,
  'changed tiles or Bridge lists schedule a report');
const config = read('src/network/bridge/ha_bridge_config.cpp');
assert.match(config, /entity_search_ = at > 0 && at < static_cast<int>\(json\.length\(\)\) && json\[at\] == '1';/,
  'the Bridge announces the search in its configuration');

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
  'setting and removing the password reports the tiles again');
const settings = read('src/ui/tabs/settings/tab_settings.cpp');
assert.match(settings, /web_admin_auth::allowFirstPassword\(\);\s*security_set_message\(tr\(\)\.web_auth_window_open, 0x4DB6AC\);/);
const html = read('src/web/server/render/web_admin_security_html.cpp');
assert.match(html, /if \(!enabled\) \{[\s\S]*?id="web_auth_tap_note"[\s\S]*?tr\.web_auth_panel_tap_required/,
  'Web Admin says where to tap before the first password');
const authJs = read('src/web/assets/auth.js');
assert.match(authJs, /else passwordErrorCode = String\(\(await response\.json\(\)\.catch\(\(\) => \(\{\}\)\)\)\.error \|\| ''\);/);
assert.match(read('src/web/admin/settings/web-password.js'),
  /t\(auth\.passwordError\?\.\(\) === 'panel_tap_required' \? 'webAuthPanelTap' : 'webAuthChangeFailed'\)/);

console.log('Entity search: sealed transport, module, endpoints and first password window pass');

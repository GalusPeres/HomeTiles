// With the optional Web Admin password, stored Wi-Fi/MQTT passwords and PINs
// must not reach any browser: not the admin page, not a JSON reply and not the
// setup portal. They stay replaceable with a new value.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import {readRepoFile, repoRoot} from '../../lib/admin-source.mjs';

function sourceFiles(directory) {
  return fs.readdirSync(path.join(repoRoot, directory), {withFileTypes: true}).flatMap(entry => {
    const relative = path.join(directory, entry.name);
    if (entry.isDirectory()) return sourceFiles(relative);
    return /\.(cpp|h)$/.test(entry.name) ? [relative] : [];
  });
}

// Every place that serialises a stored secret for HTTP output.
const webSources = [...sourceFiles('src/web'), ...sourceFiles('src/types')];
let guardedReads = 0;
for (const file of webSources) {
  const source = readRepoFile(file);
  for (const call of ['getFolderPin(', 'getSettingsPin(']) {
    let index = source.indexOf(call);
    while (index >= 0) {
      const context = source.slice(Math.max(0, index - 220), index);
      assert.match(context, /web_admin_auth::storedSecretsHidden\(\)/,
        `${file}: ${call} must be skipped while stored secrets are hidden`);
      guardedReads++;
      index = source.indexOf(call, index + call.length);
    }
  }
  // A stored Wi-Fi/MQTT password is written only behind the hidden check.
  for (const match of source.matchAll(/appendHtmlEscaped\(html,\s*(?:String\()?cfg\.(?:wifi_pass|mqtt_pass)\)/g)) {
    const guard = source.slice(Math.max(0, match.index - 40), match.index);
    assert.ok(/if \(!hide_password\) $/.test(guard),
      `${file}: a stored Wi-Fi/MQTT password must go through the hidden-secret check`);
  }
}
assert.equal(guardedReads, 5, 'three folder PIN replies, the Settings tile PIN and the /mqtt reply');

const adminPage = readRepoFile('src/web/server/render/web_admin_html.cpp');
for (const field of ['wifi_pass', 'mqtt_pass']) {
  assert.match(adminPage, new RegExp(`id="${field}" name="${field}"[\\s\\S]{0,80}value="\\)html";\\n  appendStoredSecretValue\\(html, cfg\\.${field}, tr\\);`),
    `${field} value uses appendStoredSecretValue`);
}
const helper = readRepoFile('src/web/server/render/web_admin_security_html.cpp');
const body = helper.slice(helper.indexOf('void appendStoredSecretValue('),
  helper.indexOf('void appendWebAdminCsrfMeta('));
assert.match(body, /if \(!web_admin_auth::storedSecretsHidden\(\)\) \{\s*appendHtmlEscaped\(html, String\(secret \? secret : ""\)\);\s*return;\s*\}/);
assert.equal((body.match(/appendHtmlEscaped\(html, String\(secret/g) || []).length, 1,
  'the secret itself is written only on the unprotected path');
assert.match(body, /tr\.secret_hidden_placeholder/);

const auth = readRepoFile('src/web/server/auth/web_admin_auth.cpp');
assert.match(auth, /bool storedSecretsHidden\(\) \{\s*return enabled\(\);\s*\}/);

// Saving keeps a hidden secret when the field stays empty.
const handlers = readRepoFile('src/web/server/handlers/web_admin_handlers.cpp');
assert.match(handlers, /copyIfNonEmpty\(cfg\.wifi_pass, sizeof\(cfg\.wifi_pass\), "wifi_pass"\);/);
assert.match(handlers, /copyIfNonEmpty\(cfg\.mqtt_pass, sizeof\(cfg\.mqtt_pass\), "mqtt_pass"\);/);
const tiles = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tiles, /if \(!pin\.length\(\) && tileConfig\.isFolderPinEnabled\(folder_id\)\) \{\s*success = true;/,
  'an empty folder PIN keeps the configured one');
const setup = readRepoFile('src/web/setup/web_config.cpp');
assert.match(setup, /const bool keep_hidden_password =\s*pass\.isEmpty\(\) && web_admin_auth::storedSecretsHidden\(\) &&\s*strcmp\(cfg\.wifi_ssid, previous_ssid\) == 0;/,
  'the setup portal keeps the hidden password for the same network');
assert.match(setup, /if \(!hide_password\) appendHtmlEscaped\(html, String\(cfg\.wifi_pass\)\);/);
assert.match(setup, /ap_wifi_keep_password_hint/);
console.log(`Stored secrets hidden behind the Web Admin password (${guardedReads} PIN reads guarded)`);

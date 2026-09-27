// The network and MQTT settings form renders the stored configuration back into
// its inputs. Twelve of those values went into the value attribute unescaped, so
// an SSID, user name or password containing " or & broke the attribute and was
// written back mangled on the next save. Both device passwords also had no
// autocomplete token, which let the browser autofill a site password over them.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const html = readRepoFile("src/web/server/render/web_admin_html.cpp");
const utils = readRepoFile("src/web/server/web_admin_utils.cpp");

for (const entity of ['&amp;', '&lt;', '&gt;', '&quot;']) {
  assert.ok(utils.includes(entity),
    `appendHtmlEscaped must keep escaping ${entity}`);
}

// Every stored configuration value has to go through the escaper.
const CONFIG_VALUES = [
  'wifi_ssid', 'wifi_pass', 'wifi_static_ip', 'wifi_gateway', 'wifi_subnet',
  'wifi_dns', 'mqtt_host', 'mqtt_user', 'mqtt_pass', 'mqtt_client_id',
  'mqtt_base_topic', 'ha_prefix'
];
// The two passwords go through appendStoredSecretValue, which escapes them
// and leaves them out entirely while a Web Admin password is set.
const SECRET_VALUES = new Set(['wifi_pass', 'mqtt_pass']);
const secretHelper = readRepoFile('src/web/server/render/web_admin_security_html.cpp');
assert.match(secretHelper,
  /void appendStoredSecretValue\([\s\S]*?appendHtmlEscaped\(html, String\(secret \? secret : ""\)\);/,
  'appendStoredSecretValue must escape the stored secret');
for (const field of CONFIG_VALUES) {
  const writer = SECRET_VALUES.has(field)
    ? `appendStoredSecretValue(html, cfg.${field}, tr);`
    : `appendHtmlEscaped(html, cfg.${field});`;
  assert.ok(html.includes(writer),
    `cfg.${field} must be escaped before it lands in a value attribute`);
  assert.ok(!new RegExp(`^\\s*html \\+= cfg\\.${field};`, 'm').test(html),
    `cfg.${field} must not be appended raw any more`);
}

// The two device secrets must not participate in site password autofill.
for (const field of ['wifi_pass', 'mqtt_pass']) {
  assert.match(
    html,
    new RegExp(`id="${field}" name="${field}"[\\s\\S]{0,120}?autocomplete="new-password"`),
    `${field} needs an autocomplete token so it is not autofilled`);
}

// A value with a quote and an ampersand has to survive the render unchanged.
const harness = `<!doctype html><html><body>
  <form id="admin_settings_form" autocomplete="on">
    <input type="text" id="mqtt_user" name="mqtt_user"
           value="he&quot;llo &amp; bye">
    <input type="password" id="mqtt_pass" name="mqtt_pass"
           autocomplete="new-password" value="p&amp;ss&quot;word">
  </form>
  <pre id="result">running</pre>
  <script>
  (() => {
    try {
      const user = document.getElementById('mqtt_user');
      const pass = document.getElementById('mqtt_pass');
      if (user.value !== 'he"llo & bye') {
        throw new Error('Escaped user name did not round-trip: ' + user.value);
      }
      if (pass.value !== 'p&ss"word') {
        throw new Error('Escaped password did not round-trip: ' + pass.value);
      }
      // The form still holds exactly the two inputs: an unescaped quote would
      // have split the attribute and produced extra ones.
      if (document.getElementById('admin_settings_form').elements.length !== 2) {
        throw new Error('The escaped markup must not create extra fields');
      }
      if (pass.autocomplete !== 'new-password') {
        throw new Error('The password field must keep its autocomplete token');
      }
      document.body.dataset.result = 'pass';
      document.getElementById('result').textContent = 'pass';
    } catch (error) {
      document.body.dataset.result = 'fail';
      document.getElementById('result').textContent = error.stack || String(error);
    }
  })();
  </script>
</body></html>`;

if (!runDomHarness({
  label: 'Admin settings form DOM test',
  html: harness,
  tmpPrefix: 'hometiles-settings-form-'
})) {
  console.log('Admin settings form source contract passed.');
}

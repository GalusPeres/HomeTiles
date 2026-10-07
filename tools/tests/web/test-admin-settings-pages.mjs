// The Web Admin Settings tab follows the panel's Settings: a list of pages
// (WLAN, Lokalisierung, System, then I/O, camera, files and diagnostics), one
// page at a time, and one footer whose buttons follow the page. I/O moved in
// from its own tab. The System page shows the way to Home Assistant, MQTT as a
// row that folds out, and Pairing and the password under Security.
//
// Issue #73: a language change made on the display left an open Web Admin in
// the old language until a manual reload. The page now polls the panel's
// language and reloads, but never while something is being edited.
import assert from 'node:assert/strict';

import {extractDeliveredFunction, inlineScriptSafe, readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const html = readRepoFile('src/web/server/render/web_admin_html.cpp').replace(/\r\n?/g, '\n');
const handlers = readRepoFile('src/web/server/handlers/web_admin_handlers.cpp').replace(/\r\n?/g, '\n');
const routes = readRepoFile('src/web/server/web_admin.cpp').replace(/\r\n?/g, '\n');
const tab = html.slice(html.indexOf('static void appendSettingsTabHtml('),
  html.indexOf('String WebAdminServer::getAdminPage() {'));

// --- Server markup ------------------------------------------------------------------------------
const order = [...tab.matchAll(/appendSettingsNavItem\(html, "(\w+)"/g)].map(match => match[1]);
assert.deepEqual(order, ['network', 'locale', 'system', 'io', 'camera', 'files', 'diagnostics'],
  'the list follows the panel (WLAN, Lokalisierung, System), then the Web Admin pages');
assert.ok(tab.indexOf('appendSettingsNavItem(html, "system"') < tab.indexOf('settings-nav-sep') &&
  tab.indexOf('settings-nav-sep') < tab.indexOf('appendSettingsNavItem(html, "io"'),
  'a line separates the panel\'s pages from the Web Admin\'s');
for (const page of ['network', 'locale', 'system', 'io', 'files', 'diagnostics']) {
  assert.match(tab, new RegExp(`<section class="settings-page[^"]*" data-settings-page="${page}">`), `${page} page`);
}
assert.match(tab, /appendSettingsNavItem\(html, "system", "#26A69A", "chip", "System", FW_VERSION\);/,
  'System: the panel\'s color, icon and name, with the version');
// The form holds WLAN, Lokalisierung and System (MQTT); I/O and the rest act at once.
const form = tab.slice(tab.indexOf('<form id="admin_settings_form"'), tab.indexOf('</form>'));
for (const page of ['network', 'locale', 'system']) assert.ok(form.includes(`data-settings-page="${page}"`));
assert.ok(!form.includes('data-settings-page="io"'), 'I/O saves through its own API');
// One footer: Save for WLAN and Lokalisierung, Restart for System, the I/O buttons.
const foot = tab.slice(tab.indexOf('id="settingsFoot"'));
assert.deepEqual([...foot.matchAll(/data-settings-foot="(\w+)"/g)].map(match => match[1]),
  ['network', 'locale', 'system', 'io']);
for (const id of ['hardwareIoSaveState', 'hardwareIoSave', 'hardwareIoRestart']) {
  assert.ok(foot.includes(`id="${id}"`), `${id} keeps its id for the I/O editor`);
}
// System: the way to Home Assistant, MQTT folded (kept, unused on the link),
// Pairing with the right line, then the password row.
assert.match(tab, /settings_model::bridge_route\(route, sizeof\(route\)\)/);
assert.match(tab, /<details class="settings-fold" id="mqtt_settings">/);
assert.match(tab, /if \(link\) \{\n    mqtt_line = tr\.settings_mqtt_unused;/);
assert.match(tab, /appendHtmlEscaped\(html, tr\.settings_mqtt_unused_note\);/);
assert.match(tab, /: link  \? tr\.settings_states_encrypted\n\s*: tr\.settings_commands_encrypted/);
assert.doesNotMatch(html, /data-tab-target="tab-hardware"/, 'I/O has no tab of its own any more');

// --- Issue #73 server side ---------------------------------------------------------------------
assert.match(routes, /server\.on\("\/api\/language", HTTP_GET,\s*guarded\(\[this\]\(\) \{ this->handleLanguage\(\); \}\)\);/);
assert.match(handlers, /void WebAdminServer::handleLanguage\(\) \{[\s\S]*i18n::strings\(configManager\.getConfig\(\)\.language\)\.html_lang/,
  'the language arrives as the page\'s own <html lang> code');

// --- Browser behavior --------------------------------------------------------------------------
const functions = ['settingsTabActive', 'updateSettingsScrollbarWidth', 'runSettingsPageWork',
  'showSettingsPage', 'openSettingsPage', 'adminEditsPending', 'checkDeviceLanguage']
  .map(name => extractDeliveredFunction(name))
  // location.reload() cannot be replaced in the browser; count it instead.
  .map(source => source.replace('location.reload()', 'window.reloads++'))
  .join('\n');

const harness = `<!doctype html><html lang="de"><body>
<div id="tab-network" class="tab-content active">
  <button class="settings-nav-item" data-settings-page="network"></button>
  <button class="settings-nav-item" data-settings-page="system"></button>
  <button class="settings-nav-item" data-settings-page="io"></button>
  <button class="settings-nav-item" data-settings-page="files"></button>
  <section class="settings-page" data-settings-page="network" style="height:50px;overflow-y:auto"></section>
  <section class="settings-page" data-settings-page="system" style="height:50px;overflow-y:auto">
    <details id="web_auth_section"><summary>pw</summary><input id="pw"></details>
  </section>
  <section class="settings-page" data-settings-page="io"></section>
  <section class="settings-page" data-settings-page="files"></section>
  <div id="settingsFoot">
    <div class="settings-foot-set" data-settings-foot="network"></div>
    <div class="settings-foot-set" data-settings-foot="system"></div>
    <div class="settings-foot-set" data-settings-foot="io"></div>
  </div>
</div>
<pre id="result">running</pre>
<script>
window.reloads = 0;
let activeSettingsPage = '';
let dragSource = null, resizeState = null, fileManagerUploadBusy = false, hardwareIoDirty = false;
let autoSaveTimers = {}, saveInFlightByTile = {}, queuedSaveByTile = {};
let fileManagerLoaded = false, fileLoads = 0, ioInits = 0, tabs = [];
const APP_LOCALE = 'de';
function loadFileManager() { fileLoads++; fileManagerLoaded = true; }
function initHardwareIo() { ioInits++; }
function switchTab(name) { tabs.push(name); }
let deviceLanguage = 'de';
window.fetch = async url => ({ok: true, json: async () => ({language: deviceLanguage})});
${inlineScriptSafe(functions)}
(async () => {
  const check = (value, message) => { if (!value) throw new Error(message); };
  const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
  const visible = () => [...document.querySelectorAll('.settings-page')].filter(p => !p.hidden).map(p => p.dataset.settingsPage);
  const feet = () => [...document.querySelectorAll('.settings-foot-set')].filter(f => !f.hidden).map(f => f.dataset.settingsFoot);
  try {
    showSettingsPage('system');
    check(visible().join() === 'system', 'one page shows: ' + visible());
    check(feet().join() === 'system', 'the footer follows the page: ' + feet());
    check(document.querySelector('[data-settings-page="system"].settings-nav-item').getAttribute('aria-current') === 'page',
      'the list marks the page');
    check(localStorage.getItem('activeSettingsPage') === 'system', 'the page is remembered');
    showSettingsPage('files');
    check(document.getElementById('settingsFoot').hidden, 'pages that act at once have no footer');
    await wait(10);
    check(fileLoads === 1, 'the file list loads when its page opens');
    showSettingsPage('io');
    await wait(10);
    check(ioInits === 1, 'the I/O editor starts when its page opens');
    showSettingsPage('nothing');
    check(visible().join() === 'network', 'an unknown page falls back to the first');
    check(document.getElementById('tab-network').style.getPropertyValue('--settings-scrollbar') !== '',
      'the scrollbar width is measured for the margin');
    openSettingsPage('system', 'web_auth_section');
    check(tabs.at(-1) === 'tab-network' && visible().join() === 'system' && document.getElementById('web_auth_section').open,
      'the badge opens Settings > System with the password row folded out');

    // Issue #73.
    await checkDeviceLanguage();
    check(window.reloads === 0, 'the same language keeps the page');
    deviceLanguage = 'fr';
    document.getElementById('pw').focus();
    await checkDeviceLanguage();
    check(window.reloads === 0, 'no reload while a field is being edited');
    document.getElementById('pw').blur();
    autoSaveTimers = {tile: 1};
    await checkDeviceLanguage();
    check(window.reloads === 0, 'no reload while a tile still saves');
    autoSaveTimers = {};
    await checkDeviceLanguage();
    check(window.reloads === 1, 'a language changed on the display reloads the page');
    document.body.dataset.result = 'pass';
    document.getElementById('result').textContent = 'pass';
  } catch (error) {
    document.body.dataset.result = 'fail';
    document.getElementById('result').textContent = error.stack || String(error);
  }
})();
</script></body></html>`;

if (!runDomHarness({label: 'Web Admin Settings pages', html: harness, tmpPrefix: 'hometiles-settings-pages-',
  extraArgs: ['--virtual-time-budget=3000']})) {
  console.log('Web Admin Settings pages: server contract passed.');
}

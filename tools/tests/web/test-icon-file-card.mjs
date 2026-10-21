// The System page's Icons card: the icon file is there (its name, all icons)
// or missing (its download link, built-in / all, "Update" for the panel to
// fetch it now). Below it the file by hand; an upload goes into PSRAM, its
// header is checked at once and its CRC at the end, the loop installs it.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readRepoFile, extractDeliveredFunction} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

// Texts: the card's title and row label in every language, the rest reused.
const i18nHeader = readRepoFile('src', 'core', 'i18n', 'i18n.h');
assert.match(i18nHeader, /const char\* icons_section;\n  const char\* icon_file_label;/);
const i18n = readRepoFile('src', 'core', 'i18n', 'i18n.cpp');
for (const [title, label] of [['Icons', 'Icon-Datei'], ['Icons', 'Icon file'], ['Icônes', 'Fichier d’icônes'], ['Ikony', 'Plik ikon']]) {
  assert.ok(i18n.includes(`    "${title}",\n    "${label}",\n`), `${label} is in its language table`);
}

// Markup: below the firmware card, before Connection.
const html = readRepoFile('src', 'web', 'server', 'render', 'web_admin_html.cpp');
const firmwareNote = html.indexOf('html += tr.ota_update_note;');
const card = html.indexOf('id="icon_file_section"');
const connection = html.indexOf('html += tr.settings_connection;', firmwareNote);
assert.ok(firmwareNote > 0 && card > firmwareNote && connection > card, 'the card sits between firmware and Connection');
const block = html.slice(card, connection);
assert.match(block, /if \(icons_ready\) \{\n\s*html \+= mdi_icon_store::release_file_name\(\);/, 'with the file: its name');
assert.match(block, /settings-state-link[\s\S]*release_file_url\(\)/, 'without it: the download link to its own release');
assert.match(block, /builtin_count\(\)\) \+ " \/ " \+ icon_total/, 'without it: built-in / all icons');
assert.match(block, /id="icon_download_btn" onclick="downloadIconFile\(\)"/, 'without it: Update');
assert.ok(block.indexOf('icon_download_btn') > block.indexOf('} else {'), 'Update shows only while the file is missing');
// Without the file the card names why the last download or install failed.
const reasonAt = block.indexOf('mdi_icon_store::last_failure()');
assert.ok(reasonAt > block.indexOf('} else {') && reasonAt < block.indexOf('if (icons_ready) {', reasonAt),
  'the reason shows only while the file is missing');
assert.match(block, /settings-state-error[\s\S]*appendHtmlEscaped\(html, reason\)/, 'the reason is escaped');
for (const reused of ['tr.ota_choose_file', 'tr.ota_upload_install', 'tr.ota_no_file_selected']) {
  assert.ok(block.includes(reused), `${reused} is reused`);
}

// Firmware: routes, PSRAM upload with its checks, install and download in the loop.
const server = readRepoFile('src', 'web', 'server', 'web_admin.cpp');
assert.match(server, /"\/api\/icons\/download", HTTP_POST,\s*guarded\(\[this\]\(\) \{ this->handleIconFileDownload\(\); \}\)\)/);
assert.match(server, /"\/api\/icons\/upload", HTTP_POST,\s*guardedUploadDone\([^\n]*handleIconFileUploadDone[^\n]*\n\s*guardedRaw\([^\n]*handleIconFileUpload\(\)/);
const store = cppFunctionDefinitions(readRepoFile('src', 'tiles', 'icons', 'mdi_icon_store.cpp'));
const body = name => store.find(item => item.name === name).body;
assert.match(body('upload_begin'), /heap_caps_malloc\(file_bytes\(\), MALLOC_CAP_SPIRAM/);
assert.match(body('upload_write'), /!had_header && g_upload_len >= mdi_icon_cache::kHeaderBytes[\s\S]*check_header/,
  'the header is checked as soon as it arrived');
assert.match(body('upload_finish'), /g_upload_len != file_bytes\(\)[\s\S]*header_bitmap_crc/, 'length and CRC at the end');
assert.match(body('install_upload'), /begin_write[\s\S]*write\(g_upload, g_upload_len\)[\s\S]*finish_write[\s\S]*upload_discard/);
const sketch = readRepoFile('HomeTiles.ino');
assert.match(sketch, /if \(mdi_icon_store::upload_ready\(\)\) apply_icon_upload\(\);/);
assert.match(sketch, /mdi_icon_store::take_download_request\(\) && networkManager\.isNetworkConnected\(\)\) apply_icon_download\(true\);/);
// "Update" does not use up one of the three automatic attempts.
assert.match(sketch, /static void apply_icon_download\(bool requested = false\) \{[^}]*if \(!requested\) mdi_icon_store::note_download_attempt\(\);/);
// A failure is noted for the card, in the firmware's own words.
assert.match(sketch, /Icon download failed: [^\n]*\n\s*mdi_icon_store::note_failure\(String\("Download failed: "\) \+ err\);/);
assert.match(sketch, /Uploaded icon file not installed: [^\n]*\n\s*mdi_icon_store::note_failure\(String\("Install failed: "\) \+ err\);/);
// A written file clears the reason with the attempts; a new firmware drops an old one.
assert.match(body('clear_download_attempts'), /prefs\.clear\(\)/);
assert.match(body('note_download_attempt'), /!same_version && prefs\.isKey\("err"\)\) prefs\.remove\("err"\)/);
assert.match(body('last_failure'), /prefs\.isKey\("err"\)/, 'no NVS error for a missing reason');
assert.match(sketch, /static void apply_icon_upload\(\) \{[\s\S]*?Device::storageWriteBegin\(\);[\s\S]*?install_upload[\s\S]*?BoardHAL::restart\(\);/);

// The delivered script: upload as raw body with progress, Update, errors.
const ids = {};
const element = id => (ids[id] ||= {id, textContent: '', disabled: false, style: {}, files: null,
  classes: new Set(), classList: {add(n) { this.owner.classes.add(n); }, remove(...n) { n.forEach(x => this.owner.classes.delete(x)); },
    toggle(n, on) { on ? this.owner.classes.add(n) : this.owner.classes.delete(n); }}});
['icon_status', 'icon_progress', 'icon_progress_bar', 'icon_download_btn', 'icon_choose_btn', 'icon_upload_btn', 'icon_file', 'icon_file_name']
  .forEach(id => { const el = element(id); el.classList.owner = el; });
const sent = [];
let reloadScheduled = 0;
class FakeXhr {
  constructor() { this.headers = {}; this.upload = {}; sent.push(this); }
  open(method, url) { this.method = method; this.url = url; }
  setRequestHeader(name, value) { this.headers[name] = value; }
  send(body) { this.body = body; }
}
const fetches = [];
let fetchAnswer = {ok: true, json: async () => ({success: true})};
const context = vm.createContext({
  APP_I18N: {otaNoFileSelected: 'No file selected', otaUploading: 'Uploading', otaInstalling: 'Installing',
    otaSuccess: 'Done.', otaReconnecting: 'Reconnecting', otaFailed: 'Failed', otaGithubDownloading: 'Downloading'},
  document: {getElementById: id => ids[id] || null},
  XMLHttpRequest: FakeXhr,
  fetch: async (url, options) => { fetches.push([url, options]); return fetchAnswer; },
  window: {location: {pathname: '/'}, setTimeout: () => { reloadScheduled++; }},
  Math, JSON, Date,
});
vm.runInContext(['t', 'updateIconFileName', 'setIconFileUi', 'setIconFileControlsDisabled', 'waitForIconFileRestart',
  'downloadIconFile', 'uploadIconFile'].map(extractDeliveredFunction).join('\n') +
  '\nthis.upload = uploadIconFile; this.download = downloadIconFile; this.pick = updateIconFileName;', context);

context.upload();
assert.equal(ids.icon_status.textContent, 'No file selected', 'no file: says so');
assert.equal(sent.length, 0);

const file = {name: 'hometiles-icons-48.bin', size: 2100493};
ids.icon_file.files = [file];
context.pick(ids.icon_file);
assert.equal(ids.icon_file_name.textContent, 'hometiles-icons-48.bin');
context.upload();
const xhr = sent.at(-1);
assert.equal(xhr.method, 'POST');
assert.equal(xhr.url, '/api/icons/upload');
assert.equal(xhr.headers['Content-Type'], 'application/octet-stream');
assert.equal(xhr.body, file, 'the file goes as the raw body');
assert.equal(ids.icon_upload_btn.disabled, true, 'controls are off while uploading');
xhr.upload.onprogress({lengthComputable: true, loaded: 1050246, total: 2100493});
assert.equal(ids.icon_status.textContent, 'Uploading 50%');
assert.equal(ids.icon_progress_bar.style.width, '50%');
xhr.status = 400;
xhr.responseText = JSON.stringify({success: false, error: 'icon file not usable: icons of another size'});
xhr.onload();
assert.equal(ids.icon_status.textContent, 'icon file not usable: icons of another size', 'the panel says why');
assert.ok(ids.icon_status.classes.has('error'));
assert.equal(ids.icon_upload_btn.disabled, false, 'controls are back after an error');

context.upload();
const ok = sent.at(-1);
ok.status = 200;
ok.responseText = '{"success":true}';
const before = reloadScheduled;
ok.onload();
assert.equal(ids.icon_status.textContent, 'Done. Reconnecting');
assert.ok(reloadScheduled > before, 'the page reloads after the restart');

await context.download();
assert.equal(fetches.at(-1)[0], '/api/icons/download');
assert.equal(fetches.at(-1)[1].method, 'POST');
assert.equal(ids.icon_status.textContent, 'Downloading Reconnecting');
fetchAnswer = {ok: false, json: async () => ({success: false, error: 'no network'})};
await context.download();
assert.equal(ids.icon_status.textContent, 'no network');
assert.equal(ids.icon_download_btn.disabled, false);

console.log('PASS: the Icons card shows the icon file or its download, Update and the file by hand');

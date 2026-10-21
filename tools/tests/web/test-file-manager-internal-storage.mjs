// Test builds show the panel's own storage (LittleFS) in the Web Admin file
// manager beside the microSD: its files, how full it is, and deleting, so the
// icon file can be removed by hand to test the icon download. Release builds
// keep microSD only; the internal storage never takes an upload, new folder
// or rename.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readRepoFile, extractDeliveredFunction} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const version = readRepoFile('src', 'core', 'firmware', 'firmware_version.h');
const betaBlock = version.match(/#if ([^\n]*(?:\\\n[^\n]*)*)\n#define HOMETILES_BETA_BUILD 1\n#endif/);
assert.ok(betaBlock, 'firmware_version.h defines HOMETILES_BETA_BUILD');
for (const define of ['HOMETILES_ISSUE30_SAFE_BETA', 'HOMETILES_ISSUE38_BETA',
  'HOMETILES_CAMERA_BETA', 'HOMETILES_TEST_BETA']) {
  assert.ok(betaBlock[1].includes(`defined(${define})`), `${define} is a test build`);
}

const files = readRepoFile('src', 'web', 'server', 'handlers', 'web_admin_files.cpp');
const definitions = cppFunctionDefinitions(files);
const body = name => {
  const found = definitions.find(item => item.name === name || item.name.endsWith('::' + name));
  assert.ok(found, `${name} exists`);
  return found.body;
};

// Only a test build resolves the internal storage.
const resolve = body('resolveFileManagerFsByKey');
const guard = resolve.indexOf('#if defined(HOMETILES_BETA_BUILD)');
const internal = resolve.indexOf('key == "internal"');
assert.ok(guard >= 0 && internal > guard && resolve.indexOf('#endif', internal) > internal,
  'the internal storage resolves only inside HOMETILES_BETA_BUILD');

// Rename, new folder and upload refuse it; deleting is the one change.
for (const name of ['handleFileManagerRename', 'handleFileManagerMkdir']) {
  assert.match(body(name), /rejectInternalStorageChange\(server, fs_key\)/, `${name} refuses the internal storage`);
}
assert.match(body('handleFileManagerUpload'), /fs_key == "internal"[\s\S]*?g_file_manager_upload_error =/,
  'an upload to the internal storage is refused');

// Deleting the icon file closes it first, under the storage write guard.
const remove = body('handleFileManagerDelete');
const scoped = remove.indexOf('Device::ScopedStorageWrite storage_write(internal)');
const release = remove.indexOf('mdi_icon_store::release_file()');
const removal = remove.indexOf('removeFileManagerPathRecursive(');
assert.ok(scoped >= 0 && release > scoped && removal > release,
  'the icon file is released under the write guard before it is deleted');
assert.match(remove, /path == mdi_icon_store::file_path\(\)/, 'only the icon file path releases the icon file');

// The list reports the internal storage's size and use.
const list = body('handleFileManagerList');
assert.match(list, /fs_key == "internal"[\s\S]*LittleFS\.totalBytes\(\)[\s\S]*LittleFS\.usedBytes\(\)/,
  'the internal list carries total and used bytes');

const store = readRepoFile('src', 'tiles', 'icons', 'mdi_icon_store.cpp');
const storeDefinitions = cppFunctionDefinitions(store);
const releaseBody = storeDefinitions.find(item => item.name === 'release_file').body;
assert.match(releaseBody, /set_source\(MDI_ICON_LOADER, nullptr\)/, 'the font falls back to the built-in icons');
assert.match(releaseBody, /g_file\.close\(\)/, 'the icon file is closed');
assert.match(storeDefinitions.find(item => item.name === 'begin_write').body, /release_file\(\);/,
  'writing a new icon file uses the same release');

// The storage switch is markup of test builds only, with central texts.
const html = readRepoFile('src', 'web', 'server', 'render', 'web_admin_html.cpp');
const switchAt = html.indexOf('file-manager-storage-switch');
const switchGuard = html.lastIndexOf('#if defined(HOMETILES_BETA_BUILD)', switchAt);
assert.ok(switchAt > 0 && switchGuard > 0 && html.indexOf('#endif', switchGuard) > switchAt,
  'the storage switch is rendered only in test builds');
// A test tool carries no translated texts into the firmware: technical names.
assert.ok(html.slice(switchGuard, switchAt + 600).includes('>LittleFS</button>'), 'the switch names LittleFS');
assert.doesNotMatch(readRepoFile('src', 'core', 'i18n', 'i18n.h'), /file_manager_(sd|internal|storage_used)/,
  'no translated texts for the test tool');

// The delivered Admin script: switch, request, use badge and the buttons.
class Element {
  constructor(dataset = {}) {
    this.dataset = dataset;
    this.disabled = false;
    this.textContent = '';
    this.title = '';
    this.classes = new Set();
    this.classList = {
      add: (...names) => names.forEach(name => this.classes.add(name)),
      remove: (...names) => names.forEach(name => this.classes.delete(name)),
      contains: name => this.classes.has(name),
      toggle: (name, on) => (on ? this.classes.add(name) : this.classes.delete(name)),
    };
  }
}
const ids = Object.fromEntries(['file_manager_sd_state', 'file_manager_status', 'file_manager_selection',
  'file_manager_primary_btn', 'file_manager_rename_btn', 'file_manager_delete_btn']
  .map(id => [id, new Element()]));
const requiresSd = [new Element(), new Element(), new Element()];
const switchButtons = [new Element({fs: 'sd'}), new Element({fs: 'internal'})];
switchButtons[0].classList.add('active');
switchButtons[1].textContent = 'LittleFS';

const requests = [];
let answer = null;
const context = vm.createContext({
  APP_I18N: {},
  URLSearchParams,
  document: {
    documentElement: {lang: 'en'},
    getElementById: id => ids[id] || null,
    querySelectorAll: selector => selector === '.file-manager-requires-sd' ? requiresSd
      : selector === '.file-manager-storage-switch button' ? switchButtons : [],
    querySelector: selector => selector === '.file-manager-storage-switch [data-fs="internal"]' ? switchButtons[1] : null,
  },
  fetch: async url => {
    requests.push(url);
    return {ok: answer.ok, status: answer.status, json: async () => answer.body};
  },
  formatLocalizedNumber: (value, digits) => value.toFixed(digits),
  renderFileManagerEntries: () => {},
  updateFileManagerRowSelection: () => {},
  showNotification: () => {},
});
vm.runInContext(`
  let fileManagerLoaded = false;
  const fileManagerState = { fs: 'sd', path: '/', selected: null, sdAvailable: null };
  ${['t', 'tf', 'fileManagerText', 'normalizeFileManagerClientPath', 'setFileManagerStatus',
    'setFileManagerStorageState', 'formatFileManagerSize', 'fileManagerUrl',
    'updateFileManagerSelectionBar', 'clearFileManagerSelection', 'selectFileManagerStorage',
    'loadFileManager'].map(extractDeliveredFunction).join('\n')}
  this.state = fileManagerState;
  this.select = selectFileManagerStorage;
  this.selectionBar = updateFileManagerSelectionBar;
`, context);
const settle = () => new Promise(resolve => setTimeout(resolve, 0));

answer = {ok: true, status: 200, body: {success: true, fs: 'internal', path: '/', total: 3014656,
  used: 2203648, entries: [{name: '_mdi_icons_48.bin', path: '/_mdi_icons_48.bin', size: 2100493}]}};
context.select('internal');
await settle();
assert.match(requests.at(-1), /fs=internal/, 'the internal storage is requested');
assert.equal(ids.file_manager_sd_state.textContent, '2.1 MB / 2.9 MB', 'the badge shows the internal use');
assert.ok(switchButtons[1].classList.contains('active') && !switchButtons[0].classList.contains('active'));
assert.ok(requiresSd.every(button => button.disabled), 'upload and new folder are off on the internal storage');
context.state.selected = {path: '/_mdi_icons_48.bin', name: '_mdi_icons_48.bin', dir: false, size: 2100493};
context.selectionBar();
assert.equal(ids.file_manager_rename_btn.disabled, true, 'rename is off on the internal storage');
assert.equal(ids.file_manager_delete_btn.disabled, false, 'the icon file can be deleted');

answer = {ok: false, status: 500, body: {success: false, error: 'Folder not found'}};
context.select('internal');
await settle();
assert.ok(ids.file_manager_sd_state.classList.contains('error'));
assert.equal(ids.file_manager_sd_state.textContent, 'LittleFS', 'a failed list names the internal storage');

answer = {ok: true, status: 200, body: {success: true, fs: 'sd', path: '/', entries: []}};
context.select('sd');
await settle();
assert.match(requests.at(-1), /fs=sd/);
assert.ok(requiresSd.every(button => !button.disabled), 'the microSD keeps upload and new folder');
context.state.selected = {path: '/a.png', name: 'a.png', dir: false, size: 1};
context.selectionBar();
assert.equal(ids.file_manager_rename_btn.disabled, false, 'the microSD keeps rename');

console.log('PASS: test builds show the internal storage (use, delete) in the file manager');

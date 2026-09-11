import assert from 'node:assert/strict';
import fs from 'node:fs';
import {cppFunctionDefinitions, cppTokens, maskCpp} from '../../lib/cpp-source.mjs';

const directory = new URL('../../../src/web/server/', import.meta.url);
function collectFiles(directory, prefix = '') {
  return fs.readdirSync(directory, {withFileTypes: true}).flatMap(entry => {
    const file = prefix + entry.name;
    return entry.isDirectory() ? collectFiles(new URL(entry.name + '/', directory), file + '/') : [file];
  });
}
const serverFiles = collectFiles(directory);
const read = (name) => {
  const matches = serverFiles.filter(file => file.split('/').at(-1) === name);
  assert.equal(matches.length, 1, `${name} needs one unambiguous source owner`);
  return fs.readFileSync(new URL(matches[0], directory), 'utf8');
};
const owners = {
  'web_admin_handlers.cpp': ['SaveMQTT', 'SaveBridge', 'BridgeRefresh', 'Status',
    'Restart', 'SaveTileBorders', 'TileRadius'],
  'web_admin_tiles.cpp': ['GetTiles', 'SaveTiles', 'ReorderTiles', 'GetSensorValues',
    'GetEntityOptions', 'GetFolders', 'GetFolderTab', 'SaveFolderAccess', 'DeleteFolder'],
  'web_admin_screensaver.cpp': ['GetScreensaver', 'SaveScreensaver', 'GetScreensaverWallpaper'],
  'web_admin_files.cpp': ['GetSdImages', 'GetSdIcons', 'UploadIcon', 'UploadIconDone',
    'FileManagerList', 'FileManagerDownload', 'FileManagerDelete', 'FileManagerRename',
    'FileManagerMkdir', 'FileManagerUpload', 'FileManagerUploadDone'],
  'web_admin_ota.cpp': ['PrepareOtaUpload', 'OtaUpdate', 'OtaRawUpdate', 'OtaUploadDone',
    'StartOtaInstall', 'GetOtaStatus', 'GithubUpdateCheck', 'GithubUpdateInstall',
    'GetGithubUpdateStatus'],
  'web_admin_diagnostics.cpp': ['CreateScreenshot', 'DownloadScreenshot',
    'CoreDumpDownload', 'CoreDumpErase', 'CrashLogDownload', 'SdDiagnosticsDownload'],
  'web_admin.cpp': ['Root'],
  'web_admin_hardware_io.cpp': ['GetHardwareIo', 'SaveHardwareIo'],
};
const sources = new Map(serverFiles.filter(name => name.endsWith('.cpp'))
  .map(file => [file.split('/').at(-1), read(file.split('/').at(-1))]));
const declarations = [...maskCpp(read('web_admin.h')).matchAll(/\bvoid (handle[A-Z]\w*)\(\);/g)]
  .map((match) => match[1]).sort();
const expected = Object.entries(owners).flatMap(([file, names]) =>
  names.map((name) => ({name: `handle${name}`, file})));
assert.deepEqual(expected.map(({name}) => name).sort(), declarations,
  'Every declared HTTP endpoint needs an explicit responsibility owner');

function verifyEndpoints(units) {
  const actual = [...units].flatMap(([file, source]) =>
    [...maskCpp(source).matchAll(/\bvoid WebAdminServer::(handle[A-Z]\w*)\(\)\s*\{/g)]
      .map((match) => ({name: match[1], file})));
  for (const endpoint of expected) {
    assert.deepEqual(actual.filter(({name}) => name === endpoint.name), [endpoint],
      `${endpoint.name} must have exactly one definition in ${endpoint.file}`);
  }
  assert.equal(actual.length, expected.length, 'Unexpected HTTP endpoint definition');
}
verifyEndpoints(sources);
const duplicate = new Map(sources);
duplicate.set('duplicate.cpp', 'void WebAdminServer::handleGetTiles() {}');
assert.throws(() => verifyEndpoints(duplicate), /handleGetTiles must have exactly one/);
const missing = new Map(sources);
missing.set('web_admin_tiles.cpp', missing.get('web_admin_tiles.cpp')
  .replace('void WebAdminServer::handleGetTiles()', 'void missingGetTiles()'));
assert.throws(() => verifyEndpoints(missing), /handleGetTiles must have exactly one/);
const misplaced = new Map(sources);
misplaced.set('misplaced.cpp', misplaced.get('web_admin_tiles.cpp'));
misplaced.delete('web_admin_tiles.cpp');
assert.throws(() => verifyEndpoints(misplaced), /handleGetTiles must have exactly one/);

for (const [file, source] of sources) {
  assert.doesNotMatch(source, /#include\s+["<][^">]*web_admin_(?:handlers|tiles|screensaver|files|ota|diagnostics)\.cpp[">]/,
    `${file} must compile handlers as independent translation units`);
  if (file !== 'web_admin_ota.cpp') {
    assert.doesNotMatch(maskCpp(source), /\bg_ota_\w+\b|\bOtaUploadState\b/,
      `${file} must not own or access OTA upload state`);
  }
  if (file !== 'web_admin_files.cpp') {
    assert.doesNotMatch(maskCpp(source), /\bg_file_manager_upload_\w+\b/,
      `${file} must not own or access file upload state`);
  }
}
const otaFunctions = cppFunctionDefinitions(sources.get('web_admin_ota.cpp'));
for (const name of ['webAdminOtaInProgress', 'webAdminServiceOta']) {
  assert.equal(otaFunctions.filter((definition) => definition.name === name).length, 1,
    `${name} must stay beside the OTA state it services`);
}
const common = read('web_admin_handler_utils.h');
assert.doesNotMatch(maskCpp(common), /\bg_\w+\b|\bextern\b/,
  'Shared handler helpers must not own or expose mutable state');
for (const definition of cppFunctionDefinitions(common)) {
  assert.match(definition.source, /^inline /, 'Small hardware accessors remain inline');
}
for (const name of ['endsWithIgnoreCase', 'appendJsonEscaped', 'sendJsonError']) {
  assert.equal(cppFunctionDefinitions(common).filter(definition => definition.name === name).length, 0,
    `${name} must not produce an internal copy in each caller`);
  const definitions = [...sources].flatMap(([file, source]) =>
    cppFunctionDefinitions(source).filter(definition => definition.name === name).map(() => file));
  assert.deepEqual(definitions, ['web_admin_handler_utils.cpp'],
    `${name} needs one compiled helper owner`);
}

// Exercise extraction against comments, literal braces and alternative
// preprocessor branches before using it to inspect real handler bodies.
const fixture = `void example() {
  const char* text = "} // literal";
  // ignored }
#if DEVICE_A
  if (a) {
#else
  if (b) {
#endif
    action();
  }
}
void next() { /* ignored { */ }
`;
const extracted = cppFunctionDefinitions(fixture);
assert.deepEqual(extracted.map(({name}) => name), ['example', 'next']);
assert.ok(extracted[0].body.endsWith('  }\n}'));
assert.deepEqual(cppTokens('call("a b"); // old'), cppTokens(' call ( "a b" ); // translated'));
assert.notDeepEqual(cppTokens('call("a b");'), cppTokens('call("ab");'));
console.log('HTTP endpoint ownership, upload state boundaries and extraction checks: PASS');

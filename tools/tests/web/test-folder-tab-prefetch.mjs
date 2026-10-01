// Opening a Web Admin folder tab for the first time waited about a second for
// the device. Folder tabs are now prefetched in the background while the
// editor is idle: one request at a time, tab HTML first, then the tile grid,
// the Home tab (folder 0) never, and nothing after the first failure.
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const navigation = read('src/web/admin/folders/navigation.js');
const start = navigation.indexOf('  // Folder tabs not opened yet are prefetched');
assert.ok(start > 0, 'prefetch block is missing');
const prefetch = navigation.slice(start);

const bootstrap = read('src/web/admin/core/bootstrap.js');
assert.match(bootstrap, /\['pointerdown', 'keydown', 'input'\]\.forEach\(type =>\n      document\.addEventListener\(type, noteAdminInteraction, true\)\);\n    scheduleFolderTabPrefetch\(\);/);
const admin = read('src/web/assets/admin.js');
assert.ok(admin.includes('function runFolderTabPrefetch()'), 'generated admin.js carries the prefetch');

function run({failTabFor = null, busy = false} = {}) {
  const log = [];
  const timers = [];
  let now = 100000;
  const installed = new Set(['tab-tiles-home']);
  const context = {
    console,
    Date: {now: () => now},
    window: {setTimeout: (fn) => { timers.push(fn); return timers.length; }},
    document: {hidden: false, getElementById: id => (installed.has(id) ? {} : null)},
    tabByFolder: {0: 'home', 3: 'folder3', 5: 'folder5'},
    tileDataLoadedTabs: new Set(['home']),
    dragSource: busy ? {} : null,
    resizeState: null,
    fileManagerUploadBusy: false,
    ensureFolderTabUi: async folderId => {
      log.push('tab' + folderId);
      if (folderId === failTabFor) return false;
      installed.add('tab-tiles-folder' + folderId);
      return true;
    },
    fetchTileGridData: async tab => {
      log.push('tiles:' + tab);
      context.tileDataLoadedTabs.add(tab);
      return [];
    },
  };
  vm.createContext(context);
  vm.runInContext(prefetch + '\nthis.api = {scheduleFolderTabPrefetch, noteAdminInteraction};', context);
  const flush = async () => {
    for (let i = 0; i < 20 && timers.length; ++i) {
      now += 3000;
      await timers.shift()();
    }
  };
  return {context, log, timers, flush, setNow: value => { now = value; }};
}

{
  const {context, log, flush} = run();
  context.api.scheduleFolderTabPrefetch();
  await flush();
  assert.deepEqual(log, ['tab3', 'tiles:folder3', 'tab5', 'tiles:folder5'], 'one step at a time, Home skipped');
}
{
  const {context, log, flush} = run({failTabFor: 3});
  context.api.scheduleFolderTabPrefetch();
  await flush();
  assert.deepEqual(log, ['tab3'], 'stops after the first failure');
}
{
  const {context, log, timers} = run({busy: true});
  context.api.scheduleFolderTabPrefetch();
  for (let i = 0; i < 3; ++i) await timers.shift()();
  assert.deepEqual(log, [], 'waits while a tile is dragged');
  assert.equal(timers.length, 1, 'keeps one timer while waiting');
}
{
  const {context, log, timers} = run();
  context.api.scheduleFolderTabPrefetch();
  context.api.noteAdminInteraction();
  await timers.shift()();
  assert.deepEqual(log, [], 'fresh editing postpones the prefetch');
}

console.log('Folder tab prefetch: idle-only, one request at a time, Home skipped, stops on failure.');

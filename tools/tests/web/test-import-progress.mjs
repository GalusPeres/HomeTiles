// The import showed "Import läuft..." for a few seconds and then nothing
// for a minute or two (user 2026-10-21: "nicht klar was passiert"). It now
// keeps a progress line and bar under its buttons like the firmware update:
// every tile written or cleared and every layout saved is a step, counted
// before anything is written; the end and an error stay readable, and the
// buttons rest while it runs. The real import runs against a fake panel.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

// The text in every language, with both numbers, reaches the page.
const strings = read('src/core/i18n/i18n.cpp');
const languages = [...strings.matchAll(/^static const Strings kStrings(\w+) = \{/gm)].map(m => m[1]);
assert.equal(languages.length, 7, 'seven languages');
const progressTexts = strings.split('\n').filter(line => line.includes('{done}') && line.includes('{total}'));
assert.equal(progressTexts.length, languages.length, 'the progress text in every language');
assert.match(read('src/core/i18n/i18n.h'), /const char\* js_import_progress;/);
assert.match(read('src/web/server/render/web_admin_scripts.cpp'),
  /appendJsEntry\("importProgress", tr\.js_import_progress\);/);
const html = read('src/web/server/render/web_admin_html.cpp');
for (const id of ['tile_import_status', 'tile_import_progress', 'tile_import_progress_bar', 'tile_import_btn', 'tile_export_btn']) {
  assert.ok(html.includes(`id="${id}"`), `${id} under the import buttons`);
}

const COLS = 4, ROWS = 4, COUNT = 32;
const empty = () => ({type: 0, title: '', icon_name: '', bg_color: 0, col: 0, row: 0, span_w: 1, span_h: 1});

function makePanel(failTitle = null) {
  const panel = {grids: {0: Array.from({length: COUNT}, empty), 65534: Array.from({length: COUNT}, empty)}, writes: 0};
  panel.grids[0][15] = {...empty(), type: 7, icon_name: 'cog', col: 3, row: 3};
  panel.grids[0][0] = {...empty(), type: 1, title: 'Old', col: 0, row: 0};
  panel.grids[0][1] = {...empty(), type: 1, title: 'Older', col: 1, row: 0};
  panel.grids[65534][8] = {...empty(), type: 1, title: 'Old saver', col: 0, row: 2};
  const answer = (status, body) => ({ok: status === 200, status, json: async () => body});
  panel.fetch = async (url, options = {}) => {
    const u = new URL(url, 'http://panel');
    if (u.pathname === '/api/folders') return answer(200, [{id: 0, parent_id: 0, name: 'Home', icon_name: 'home'}]);
    if (u.pathname === '/api/screensaver') return answer(200, {success: true});
    if (u.pathname === '/api/tiles' && options.method !== 'POST') {
      return answer(200, panel.grids[Number(u.searchParams.get('folder'))].map(tile => ({...tile})));
    }
    if (u.pathname === '/api/tiles') {
      const fd = options.body;
      const grid = panel.grids[Number(fd.get('folder'))];
      if (failTitle && fd.get('title') === failTitle) return answer(500, {success: false, error: 'Tile apply failed'});
      panel.writes++;
      const type = Number(fd.get('type'));
      grid[Number(fd.get('index'))] = type ? {...empty(), type, title: fd.get('title'), col: Number(fd.get('col')),
        row: Number(fd.get('row')), span_w: Number(fd.get('span_w')), span_h: Number(fd.get('span_h'))} : empty();
      return answer(200, {success: true});
    }
    throw new Error('unexpected request ' + url);
  };
  return panel;
}

// The progress elements as the page has them.
function makeDocument() {
  const elements = {};
  for (const id of ['tile_import_status', 'tile_import_progress', 'tile_import_progress_bar', 'tile_import_btn', 'tile_export_btn']) {
    const classes = new Set(id === 'tile_import_progress' ? ['ota-progress', 'is-hidden'] : []);
    elements[id] = {
      id, textContent: '', style: {width: '0%'}, disabled: false, attributes: {},
      classList: {
        add: name => classes.add(name), remove: (...names) => names.forEach(name => classes.delete(name)),
        toggle: (name, on) => (on ? classes.add(name) : classes.delete(name)), contains: name => classes.has(name)
      },
      setAttribute(name, value) { this.attributes[name] = value; }
    };
  }
  return {elements, document: {getElementById: id => elements[id] || null, querySelector: () => null}};
}

function loadImport(panel) {
  const notifications = [];
  const {elements, document} = makeDocument();
  const context = vm.createContext({
    console: {error() {}, warn() {}, log() {}},
    URL, FormData, Blob: class {}, Promise, setTimeout: () => 0,
    GRID_COLS: COLS, GRID_ROWS: ROWS, TILES_PER_GRID: COUNT, MEDIA_TILE_TYPE: 15, MEDIA_TILE_MIN_SPAN: 2, MEDIA_TILE_MAX_SPAN: 4,
    LAYOUTS: {classic: {screenW: 800, screenH: 480, available: true}},
    SCREENSAVER_FOLDER_ID: 65534, tabByFolder: {0: 'folder0'}, currentTileTab: 'folder0',
    firstAllowedGridRow: () => 0,
    normalizeIconName: value => String(value || '').replace(/^mdi[:-]/, '').toLowerCase(),
    ssClamp: (value, min, max) => Math.max(min, Math.min(max, Number(value))),
    devicePreviewKind: () => 'lock', getClimateLayoutPayload: value => value || 0,
    t: key => (key === 'importProgress' ? 'Import {done}/{total}' : key),
    showNotification: (text, ok = true) => notifications.push({text, ok}),
    localStorage: {removeItem() {}}, location: {reload() {}}, document, fetch: panel.fetch
  });
  for (const file of ['src/web/admin/tiles/layout.js', 'src/web/admin/tiles/import-export.js']) {
    vm.runInContext(read(file), context, {filename: file});
  }
  // Every step the line showed, read straight from the elements.
  const steps = [];
  const setter = context.setImportProgress;
  context.setImportProgress = (...args) => {
    setter(...args);
    steps.push({text: elements.tile_import_status.textContent, width: elements.tile_import_progress_bar.style.width,
                hidden: elements.tile_import_progress.classList.contains('is-hidden'),
                error: elements.tile_import_status.classList.contains('error'),
                success: elements.tile_import_status.classList.contains('success'),
                disabled: elements.tile_import_btn.disabled && elements.tile_export_btn.disabled});
  };
  vm.runInContext('setImportProgress = this.setImportProgress;', context);
  return {context, notifications, steps};
}

const payload = () => ({
  version: 3,
  folders: [{id: 0, parent_id: 0, name: 'Home', icon_name: 'home'}],
  grids: {'0': [
    {type: 1, title: 'One', col: 0, row: 0, span_w: 1, span_h: 1},
    {type: 1, title: 'Two', col: 1, row: 0, span_w: 1, span_h: 1},
    {type: 2, title: 'Three', col: 2, row: 0, span_w: 1, span_h: 1},
    {type: 7, title: '', icon_name: 'cog', col: 3, row: 3, span_w: 1, span_h: 1}
  ]},
  screensaver: {version: 2, config: null, grid: [{type: 1, title: 'Saver', col: 0, row: 2, span_w: 1, span_h: 1}],
                source_layout: {grid_cols: COLS, grid_rows: ROWS}}
});

// 1. A whole import: from 0 to every step, the end stays, the buttons rest
// meanwhile and come back.
{
  const panel = makePanel();
  const {context, notifications, steps} = loadImport(panel);
  await context.importTilesPayload(payload());
  assert.deepEqual(notifications.at(-1), {text: 'importComplete', ok: true}, JSON.stringify(notifications));
  const running = steps.filter(step => step.text.startsWith('Import '));
  // Home: 2 old tiles cleared, Settings and 3 tiles written; the screensaver:
  // 1 cleared, 1 written; no layouts in this export.
  const total = 2 + 1 + 3 + 1 + 1;
  assert.equal(panel.writes, total, 'every write is a step');
  assert.equal(running[0].text, `Import 0/${total + 3}`, 'counted before anything is written');
  assert.ok(running.every(step => step.disabled && !step.hidden && !step.error), 'running: bar shown, buttons resting');
  assert.deepEqual(running.map(step => Number(step.text.split(' ')[1].split('/')[0])),
                   Array.from({length: total + 1}, (_, i) => i), 'one step per write, none skipped');
  const end = steps.at(-1);
  assert.deepEqual(end, {text: 'importComplete', width: '100%', hidden: false, error: false, success: true, disabled: false},
                   'the end stays with a full bar');
}

// 2. A tile the panel refuses: the reason stays in the line, the bar goes,
// the buttons come back.
{
  const panel = makePanel('Two');
  const {context, notifications, steps} = loadImport(panel);
  await context.importTilesPayload(payload());
  assert.equal(notifications.at(-1).text, 'importStopped');
  assert.deepEqual(steps.at(-1), {text: 'importStopped', width: '0%', hidden: true, error: true, success: false, disabled: false});
}

// 3. A file that is no export: said in the line too.
{
  const {context, steps} = loadImport(makePanel());
  await context.importTilesPayload({});
  assert.deepEqual(steps.at(-1), {text: 'importInvalidJson', width: '0%', hidden: true, error: true, success: false, disabled: false});
}

console.log('ok - the import shows its progress until the end, and an error stays');

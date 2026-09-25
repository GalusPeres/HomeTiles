import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {extractFunction} from '../../lib/admin-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8')
  .replace(/\r\n?/g, '\n');
const requireMarker = (source, marker, label) => {
  if (!source.includes(marker)) throw new Error(`${label} is missing: ${marker}`);
};

const html = read('src/web/server/render/web_admin_html.cpp');
const settingsHtml = read('src/types/settings/web_html.cpp');
const registry = read('src/types/types_registry.cpp');
const handlers = read('src/web/server/handlers/web_admin_handlers.cpp') +
  read('src/web/server/handlers/web_admin_tiles.cpp');
const routes = read('src/web/server/web_admin.cpp');
const navigateHtml = read('src/types/navigate/web_html.cpp');
const navigateScripts = read('src/types/navigate/web_scripts.cpp');
const admin = read('src/web/assets/admin.js');
const adminCss = read('src/web/assets/admin.css');
const webScripts = read('src/web/server/render/web_admin_scripts.cpp');
const i18nHeader = read('src/core/i18n/i18n.h');
const i18nSource = read('src/core/i18n/i18n.cpp');

for (const marker of [
  'settings_access_fields',
  'settings-access-tile-fields',
  'settings-access-toggles',
  'settings-access-columns',
  'settings-pin-fields',
  'settings-edge-fields',
  'settings_pin_enabled',
  'data-pin-configured=\\"',
  'settings_pin',
  'autocomplete=\\"new-password\\"',
  'settings_pin_apply',
  'settings_pin_status',
  'data-configured-text=\\"',
  'configManager.getSettingsPin(stored_pin);',
  'appendHtmlEscaped(html, stored_pin);',
  'settings_tile_hidden',
  'settings_swipe_enabled',
  'settings_reveal_edge',
  'pin_access::kRecoveryPin',
]) requireMarker(settingsHtml, marker, 'Settings tile access form');

const togglesPosition = settingsHtml.indexOf('settings-access-toggles');
const pinFieldsPosition = settingsHtml.indexOf('settings-pin-fields');
const edgeFieldsPosition = settingsHtml.indexOf('settings-edge-fields');
if (togglesPosition < 0 || pinFieldsPosition < togglesPosition ||
    edgeFieldsPosition < pinFieldsPosition) {
  throw new Error('Settings access controls must render as top toggles, then PIN and edge');
}

for (const forbidden of [
  'name="settings_access_present"',
  'name="settings_pin_enabled"',
  'name="settings_pin"',
  'name="settings_tile_hidden"',
  'name="settings_swipe_enabled"',
  'name="settings_reveal_edge"',
]) {
  if (html.includes(forbidden) || settingsHtml.includes(forbidden)) {
    throw new Error(`Access control must not be submitted by the general form: ${forbidden}`);
  }
}

for (const marker of [
  'TILE_SETTINGS',
  '"settings_access"',
  'append_settings_fields_wrapper',
  'append_settings_styles',
  'append_settings_scripts',
]) requireMarker(registry, marker, 'Settings type registration');

if (html.includes('settings_pin_enabled" data-pin-configured')) {
  throw new Error('Settings access controls still exist in the global settings form');
}

for (const marker of [
  'id=\\"settingsHiddenSlot\\"',
  'id=\\"settingsHiddenTile\\"',
  'id=\\"settingsHiddenHint\\"',
  'class=\\"settings-hidden-parking\\"',
  'class=\\"tile-grid settings-hidden-slot',
  'class=\\"tile settings-hidden-tile ',
  'bool settings_in_grid = false;',
  'const bool hidden = cfg.settings_tile_hidden && !settings_in_grid;',
  'snapshot.valid && snapshot.bg_color != 0',
  ': tileDefaultBgColor();',
  'data-hidden=\\"',
  'draggable=\\"true\\"',
  'tile-icon',
  'tile-title',
  'appendHtmlEscaped(html, hidden_icon);',
  'mdi-tray-arrow-down tile-icon',
  'tr.settings_tile_parking',
]) requireMarker(html, marker, 'Hidden Settings slot');
requireMarker(webScripts,
              'appendJsEntry("settingsTileParking", tr.settings_tile_parking);',
              'Localized Settings parking slot');

for (const marker of [
  'id=")html";\n  html += tab_id;\n  html += R"html(_folder_pin_enabled"',
  'class="type-fields folder-pin-fields is-hidden"',
  'class="folder-pin-label is-hidden"',
  'togglePasswordVisibility(\'\)html";',
  '_folder_pin_apply',
  'tr.folder_pin_placeholder',
]) requireMarker(navigateHtml, marker, 'Folder PIN editor');

for (const marker of [
  'const NAVIGATE_I18N=',
  'tr.folder_pin_saved',
  'tr.folder_pin_save_failed',
  'tr.folder_pin_create_first',
]) requireMarker(navigateScripts, marker, 'Navigate-localized Web script');

for (const marker of [
  'server.on("/api/folders/access", HTTP_POST',
]) requireMarker(routes, marker, 'Folder access route');
for (const marker of [
  'void WebAdminServer::handleSaveFolderAccess()',
  'tileConfig.setFolderPin(folder_id, pin)',
  'tileConfig.clearFolderPin(folder_id)',
  'tileConfig.getFolderPin(folder_id, stored_pin)',
  '\\"pin_enabled\\"',
  '\\"folder_pin\\"',
]) requireMarker(handlers, marker, 'Folder access handler');

const validationPosition = handlers.indexOf(
  'tileConfig.validateSettingsTileVisible(');
const savePosition = handlers.indexOf(
  'settings_config_saved = configManager.save(cfg);', validationPosition);
const visibilityPosition = handlers.indexOf(
  'tileConfig.setSettingsTileVisible(', savePosition);
if (validationPosition < 0 || savePosition < validationPosition ||
    visibilityPosition < savePosition) {
  throw new Error('Settings tile visibility must preflight before config save and commit after it');
}
for (const marker of [
  'const bool settings_tile_present = tileConfig.getSettingsTile(settings_tile);',
  'if (hide_tile && settings_tile_present)',
  'settings_visibility_commit_needed =',
  'settings_tile_present != requested_visible',
  'if (settings_visibility_commit_needed)',
]) requireMarker(handlers, marker, 'Idempotent Settings visibility repair');

for (const marker of [
  'function toggleSettingsAccessFields()',
  "const SETTINGS_ACCESS_PREFIX = 'folder0_'",
  'async function saveSettingsAccess(',
  "body.set('_access_only', '1')",
  "body.set('settings_access_present', '1')",
  "fetch('/mqtt'",
  'function initSettingsAccessControls()',
  "pinApply?.addEventListener('click'",
  "hidden.addEventListener('change'",
  "swipe.addEventListener('change'",
  "edge.addEventListener('change'",
  'settingsAccessSaveQueue',
  'const requestedState = {...readSettingsAccessState()};',
  'pinValue, requestedTarget, requestedSnapshot, requestedState',
  'requestedState = null, reconcileAfterSave = true)',
  'reconcileAfterSave &&\n          (visibilityChanged || visibilityMismatch)',
  'settingsAccessStatesEqual(readSettingsAccessState(), requested)',
  'const visibilityMismatch =',
  'visibilityChanged || visibilityMismatch',
  'setSettingsPinStatus(true)',
  "String(result.settings_pin || '')",
  "pinInput.type = 'password';",
  "settingsAccessElement('settings_swipe_enabled')",
  'if (tileHidden && swipe) swipe.checked = true;',
  'if (swipe) swipe.disabled = tileHidden;',
  'tileHidden ||',
  "edgeFields?.classList.toggle('is-hidden', !swipeEnabled)",
  "fetch('/api/folders/access'",
  "body.set('pin', input?.value || '')",
  "input.value = String(data?.folder_pin || '')",
  "const storedPin = enabled ? String(result.folder_pin || '') : '';",
  "const showPinEditor = isFolderTile && toggle?.checked === true;",
  "fields.classList.toggle('is-hidden', !isFolderTile)",
  "targetSelect?.value ?? tile?.navigate_target",
  'delete exported.folder_pin_enabled;',
  'delete exported.folder_pin;',
  'result.error || t(\'networkErrorSave\')',
  'function selectHiddenSettingsTile()',
  'function renderSettingsHiddenSlot(hidden, source = null)',
  'selectRestoredSettings = false)',
  '(selectRestoredSettings ||',
  'reconcileSettingsTileUi(false, snapshot, true)',
  'function saveHiddenSettingsTile(tab, silent = false)',
  "body.set('settings_tile_snapshot_present', '1')",
  "body.set('settings_tile_col', String(layout.col))",
  "body.set('settings_tile_row', String(layout.row))",
  "body.set('settings_tile_span_w', String(layout.span_w))",
  "body.set('settings_tile_span_h', String(layout.span_h))",
  'if (settingsBody) settingsBody.scrollTop = 0;',
  'function enableSettingsHiddenSlot()',
  "dragSource.kind !== 'grid-tile'",
  "Number(tile?.type || 0) === 7",
  "kind: 'hidden-settings'",
  'function canPlaceHiddenSettingsLayout',
  'tileDataLoadedTabs.has(tab)',
  "grid.querySelectorAll(':scope > .tile')",
  'type: Number(tile.dataset.type || 0)',
  'Number(dragSource.type || tile?.type || 0) === 7',
  "body.set('settings_tile_target_col'",
  "body.set('settings_tile_target_row'",
  'async function flushSettingsTileSaveBeforeHide',
  'settingsTileTransferInFlight',
  "const swipe = settingsAccessElement('settings_swipe_enabled');",
  'if (swipe) swipe.checked = true;',
  'dragSource.hiddenTarget = null;',
  'await reconcileSettingsTileUi(true, snapshot)',
  'await reconcileSettingsTileUi(false, snapshot, true)',
  "hint?.classList.toggle('is-hidden', !!hidden)",
]) requireMarker(admin, marker, 'Admin access interaction');

const hideRoundtrip = extractFunction('hideSettingsTileFromGrid');
const restoreRoundtrip = extractFunction('restoreHiddenSettingsTile');
const immutableSnapshotPosition = hideRoundtrip.indexOf(
  'const snapshot = normalizeHiddenSettingsSnapshot(');
const flushBeforeHidePosition = hideRoundtrip.indexOf(
  "await flushSettingsTileSaveBeforeHide('folder0', settingsTile)");
if (immutableSnapshotPosition < 0 ||
    flushBeforeHidePosition < immutableSnapshotPosition) {
  throw new Error('Grid to parking must freeze its tile snapshot before awaiting autosave');
}
for (const [label, block, queueMarker, reconcileMarker] of [
  [
    'Grid to parking', hideRoundtrip,
    'null, null, snapshot, false)',
    'return await reconcileSettingsTileUi(true, snapshot)'
  ],
  [
    'Parking to grid', restoreRoundtrip,
    'null, {col, row}, null, false)',
    'return await reconcileSettingsTileUi(false, snapshot, true)'
  ],
]) {
  requireMarker(block, queueMarker, `${label} immutable transfer`);
  requireMarker(block, reconcileMarker, `${label} final reconciliation`);
  if ((block.match(/reconcileSettingsTileUi\(/g) || []).length !== 1) {
    throw new Error(`${label} must expose the destination only after one reconciliation`);
  }
  if (block.includes('location.reload')) {
    throw new Error(`${label} must not reload the page`);
  }
}

const saveAccess = extractFunction('saveSettingsAccess');
if (!saveAccess.includes('reconcileAfterSave &&') ||
    !saveAccess.includes('else if (reconcileAfterSave && tileSnapshot')) {
  throw new Error('Explicit Settings transfers must suppress the automatic early UI update');
}

const accessControls = extractFunction('initSettingsAccessControls');
for (const marker of [
  'hidden.checked',
  '? await hideSettingsTileFromGrid()',
  ': await restoreHiddenSettingsTile()',
  'restoreSettingsAccessState(settingsAccessCommittedState)',
]) requireMarker(accessControls, marker, 'Canonical Settings checkbox transfer');
if (accessControls.includes('flushSettingsTileSaveBeforeHide')) {
  throw new Error('Settings visibility checkbox must not keep a second transfer path');
}

const tileDrag = extractFunction('enableTileDrag');
if (tileDrag.includes("document.querySelectorAll('#tab-tiles-' + tab + ' .tile')")) {
  throw new Error('Parking tile must not receive normal grid-tile drag listeners');
}

// The server must reconcile the physical grid even when the persisted flag
// already has the requested value. These are the two inconsistent states
// seen after interrupted or overlapping older saves.
const visibilityCommitNeeded = (hidden, settingsPresent, flagChanged = false) =>
  flagChanged || settingsPresent !== !hidden;
for (const scenario of [
  {hidden: true, present: true, label: 'hidden config with Settings in grid'},
  {hidden: false, present: false, label: 'visible config without Settings in grid'},
]) {
  if (!visibilityCommitNeeded(scenario.hidden, scenario.present, false)) {
    throw new Error(`Server visibility repair misses ${scenario.label}`);
  }
}

let roundtripHidden = false;
let roundtripPresent = true;
for (let pass = 0; pass < 4; pass++) {
  roundtripHidden = !roundtripHidden;
  if (visibilityCommitNeeded(roundtripHidden, roundtripPresent, true)) {
    roundtripPresent = !roundtripHidden;
  }
  if (roundtripPresent !== !roundtripHidden) {
    throw new Error('Settings grid/config diverged during repeated roundtrip');
  }
}

if (admin.includes('const savedState = readSettingsAccessState();')) {
  throw new Error('An async Settings save must commit its immutable requested state');
}

if (admin.includes('hidden-settings-selected') ||
    adminCss.includes('hidden-settings-selected')) {
  throw new Error('The hidden Settings tile must use the complete normal tile editor');
}

for (const functionName of [
  'saveSettingsAccess',
  'reconcileSettingsTileUi',
  'selectHiddenSettingsTile',
  'initSettingsAccessControls',
  'hideSettingsTileFromGrid',
  'restoreHiddenSettingsTile',
]) {
  const start = admin.indexOf(`function ${functionName}`);
  const next = admin.indexOf('\n  function ', start + 1);
  const block = admin.slice(start, next < 0 ? admin.length : next);
  if (start < 0 || block.includes('location.reload')) {
    throw new Error(`${functionName} must update in place without reloading the page`);
  }
}

const hiddenSaveStart = admin.indexOf('function saveHiddenSettingsTile');
const hiddenSaveEnd = admin.indexOf('\n  function saveTile', hiddenSaveStart);
const hiddenSaveBlock = admin.slice(hiddenSaveStart, hiddenSaveEnd);
if (hiddenSaveStart < 0 || hiddenSaveEnd < 0 ||
    !hiddenSaveBlock.includes('queueSettingsAccessSave(null, null, snapshot)') ||
    hiddenSaveBlock.includes("fetch('/api/tiles'")) {
  throw new Error('Hidden Settings autosave must update only the NVS snapshot');
}

for (const marker of [
  'const bool access_only =',
  '? cfg.wifi_static_enabled',
  'access_changed && !access_only',
  'if (!access_only) networkManager.requestMqttReconfigure();',
  'settings_gesture_changed',
  'uiManager.refreshSettingsGestureZone();',
]) requireMarker(handlers, marker, 'Settings access-only save isolation');

for (const marker of [
  '.settings-pin-control {',
  '.settings-pin-control .settings-pin-apply {',
  '.settings-access-tile-fields .settings-access-toggles {',
  '.settings-access-columns {',
  'flex-direction:column;',
  '.settings-access-tile-fields .settings-pin-control {',
  'grid-template-columns:minmax(0,1fr) auto;',
  'padding:9px 12px;',
  'font-size:13px;',
  'padding:8px 10px;',
  'border-radius:12px;',
  'line-height:normal;',
  'font-size:12px;',
  '.tile-grid.settings-hidden-slot {',
  '.settings-hidden-parking {',
  '.settings-hidden-hint {',
  'grid-template-columns:var(--preview-cell-w);',
  '.settings-hidden-slot.has-tile .settings-hidden-tile {',
  '.settings-hidden-slot:not(.has-tile) .settings-hidden-tile {',
  'justify-content:center;',
  '.tile-settings .folder-pin-toggle {',
  'align-items:center;',
  '.tile-settings .folder-pin-toggle input {',
  '.type-fields.show > .folder-pin-label {',
  'align-self:start;',
  '.folder-pin-control .btn {',
  'width:auto;',
]) requireMarker(adminCss, marker, 'Compact folder PIN styling');

if (adminCss.includes('height:44px;\n      min-width:0;\n      margin:0;')) {
  throw new Error('Settings PIN controls must use the same height as standard tile text fields');
}

for (const marker of [
  'const char* admin_access_control;',
  'const char* settings_home_full;',
  'const char* folder_pin_enable;',
  'const char* pin_popup_incorrect;',
]) requireMarker(i18nHeader, marker, 'Central PIN translations');
for (const marker of [
  '"Zugriffsschutz"',
  '"Access control"',
  '"Contrôle d’accès"',
  '"Falsche PIN"',
  '"Incorrect PIN"',
  '"Code PIN incorrect"',
  '"PIN gespeichert."',
  '"PIN saved."',
  '"Code PIN enregistré."',
]) requireMarker(i18nSource, marker, 'German, English and French PIN strings');

if (i18nSource.includes('stored PIN is not displayed') ||
    i18nSource.includes('gespeicherte PIN wird nicht angezeigt')) {
  throw new Error('Settings PIN status still claims the PIN cannot be shown');
}

for (const forbidden of ['settings_pin_hash', 'settings_pin_salt']) {
  if (html.includes(forbidden) || navigateHtml.includes(forbidden) ||
      admin.includes(forbidden)) {
    throw new Error(`Stored credential material leaked into Web UI: ${forbidden}`);
  }
}

console.log('Web Admin PIN access contract tests passed.');

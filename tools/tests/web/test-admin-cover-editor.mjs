import {readAdminDeliverySource} from '../../lib/admin-source.mjs';
import fs from 'node:fs';
import vm from 'node:vm';

const readText = url =>
  fs.readFileSync(url, 'utf8').replace(/\r\n?/g, '\n');

const noop = () => {};
const classList = {
  add: noop,
  remove: noop,
  toggle: noop,
  contains: () => false
};

class TestElement {
  constructor(value = '') {
    this.value = value;
    this.dataset = {};
    this.listeners = {};
    this.attributes = {};
    this.inline = {};
    this.options = [];
    this.selectedOptions = [];
    this.classList = classList;
    this.style = {
      removeProperty: noop,
      setProperty: noop
    };
    this.className = '';
    this.innerHTML = '';
  }

  setAttribute(name, value) {
    this.attributes[name] = String(value);
  }

  removeAttribute(name) {
    delete this.attributes[name];
  }

  getAttribute(name) {
    return Object.prototype.hasOwnProperty.call(this.attributes, name)
      ? this.attributes[name]
      : null;
  }

  addEventListener(name, handler) {
    (this.listeners[name] ??= []).push(handler);
  }

  removeEventListener(name, handler) {
    this.listeners[name] = (this.listeners[name] || [])
      .filter(candidate => candidate !== handler);
  }

  appendChild(option) {
    this.options.push(option);
  }

  querySelector() {
    return null;
  }

  dispatch(name) {
    // Inline HTML handlers run before listeners registered later through
    // addEventListener. A listener removed by that inline handler must not be
    // invoked for the same event; this models the real tile-type select.
    const handlers = [...(this.listeners[name] || [])];
    this.inline[name]?.({ target: this });
    for (const handler of handlers) {
      if (!(this.listeners[name] || []).includes(handler)) continue;
      handler({ target: this });
    }
  }
}

const elements = {};
const storage = {
  getItem: () => null,
  setItem: noop,
  removeItem: noop,
  key: () => null,
  length: 0
};
const documentElement = { lang: 'en', dataset: {}, addEventListener: noop };
const document = {
  documentElement,
  addEventListener: noop,
  getElementById: id => elements[id] || null,
  querySelector: () => null,
  querySelectorAll: () => [],
  body: { classList, dataset: {} },
  createElement: () => new TestElement()
};

const sandbox = {
  console,
  document,
  localStorage: storage,
  sessionStorage: storage,
  setTimeout: () => 0,
  clearTimeout: noop,
  setInterval: () => 0,
  clearInterval: noop,
  requestAnimationFrame: noop,
  addEventListener: noop,
  fetch: () => Promise.reject(new Error('unexpected fetch')),
  FormData,
  URLSearchParams,
  Intl,
  Date,
  Math,
  JSON,
  Number,
  String,
  Object,
  Array,
  Map,
  Set,
  Promise,
  parseInt,
  parseFloat,
  isNaN,
  isFinite,
  APP_I18N: {},
  COVER_I18N: {
    open: 'Offen',
    opening: '\u00d6ffnet',
    closed: 'Geschlossen',
    closing: 'Schlie\u00dft',
    unavailable: 'Nicht verf\u00fcgbar',
    unknown: 'Unbekannt'
  },
  GRID_COLS: 6,
  GRID_ROWS: 4,
  TILES_PER_GRID: 24,
  ADMIN_WEB_SESSION_TOKEN: 'test',
  TILE_TYPE_REGISTRY: {
    19: {
      fields: 'cover',
      preview: 'cover',
      load: 'loadCoverFields',
      save: 'saveCoverFields',
      reset: 'resetCoverFields'
    }
  },
  TILE_TABS: [],
  TAB_BY_FOLDER: {},
  FOLDER_BY_TAB: { folder1: 1 },
  SCREENSAVER_FOLDER_ID: 65535,
  SCREENSAVER_TILE_DEFAULT_OPACITY: 0,
  SCREENSAVER_TILE_DEFAULT_COLOR: '#000000',
  MEDIA_TILE_TYPE: 15,
  MEDIA_TILE_MIN_SPAN: 1,
  MEDIA_TILE_MAX_SPAN: 6,
  APP_LOCALE: 'en',
  navigator: {},
  location: {},
  confirm: () => true
};
sandbox.window = sandbox;

vm.createContext(sandbox);
vm.runInContext(
  readAdminDeliverySource(),
  sandbox,
  { filename: 'admin.js' }
);

for (const [id, value] of Object.entries({
  folder1_tile_title: '',
  folder1_tile_icon: '',
  folder1_tile_color: '#2A2A2A',
  folder1_tile_col: '5',
  folder1_tile_row: '1',
  folder1_tile_span_w: '1',
  folder1_tile_span_h: '1',
  folder1_tile_type: '19',
  folder1_cover_entity: '',
  folder1_cover_popup_open_mode: '0'
})) {
  elements[id] = new TestElement(value);
}
elements['folder1-tile-3'] = new TestElement();
elements['folder1-tile-3'].dataset.type = '0';

const placeholder = { value: '', textContent: 'No selection' };
const coverOption = {
  value: 'cover.test',
  textContent: 'Test Cover - cover.test'
};
const coverSelect = elements.folder1_cover_entity;
coverSelect.options = [placeholder, coverOption];
coverSelect.selectedOptions = [placeholder];

vm.runInContext(`
  currentTileIndex = 3;
  currentTileTab = 'folder1';
  tilesData.folder1 = [];
  let __preview = 0;
  let __draft = 0;
  let __save = 0;
  const __realUpdateTilePreview = updateTilePreview;
  const __realUpdateDraft = updateDraft;
  updateTilePreview = () => { __preview += 1; };
  updateDraft = tab => {
    __draft += 1;
    __realUpdateDraft(tab);
  };
  scheduleAutoSave = () => { __save += 1; };
  maybeFillTitleFromEntity('folder1', '_cover_entity');
  setupLivePreview('folder1');
  setupLivePreview('folder1');
`, sandbox);

if (elements.folder1_tile_title.value !== '') {
  throw new Error('placeholder became the Cover title');
}

elements.folder1_tile_type.inline.change = () => {
  vm.runInContext("updateTileType('folder1')", sandbox);
};
elements.folder1_tile_type.dispatch('change');
coverSelect.value = coverOption.value;
coverSelect.selectedOptions = [coverOption];
elements.folder1_cover_popup_open_mode.value = '0';
coverSelect.dispatch('change');
elements.folder1_tile_title.value = 'Manual title';
elements.folder1_tile_title.dispatch('input');

const result = vm.runInContext(`(() => {
  const form = new FormData();
  saveCoverFields('folder1', form);
  return {
    preview: __preview,
    draft: __draft,
    save: __save,
    title: document.getElementById('folder1_tile_title').value,
    entity: form.get('cover_entity'),
    popup: form.get('popup_open_mode'),
    draftEntity: getTileSnapshotForSave('folder1', 3)?.cover_entity,
    draftTitle: getTileSnapshotForSave('folder1', 3)?.title
  };
})()`, sandbox);

const expected = {
  preview: 3,
  draft: 3,
  save: 3,
  title: 'Manual title',
  entity: 'cover.test',
  popup: '0',
  draftEntity: 'cover.test',
  draftTitle: 'Manual title'
};
if (JSON.stringify(result) !== JSON.stringify(expected)) {
  throw new Error(`Cover editor contract failed: ${JSON.stringify(result)}`);
}

vm.runInContext(`
  updateTilePreview = __realUpdateTilePreview;
  tilesData.folder1 = [];
  sensorMetaCache.values['cover.test'] = JSON.stringify({
    state: 'open',
    available: true,
    current_position: 40,
    device_class: 'shutter'
  });
  updateTilePreview('folder1');
`, sandbox);
const previewHtml = elements['folder1-tile-3'].innerHTML;
if (!previewHtml.includes('tile-value tile-cover-value') ||
    !previewHtml.includes('Offen<br>40%') ||
    !previewHtml.includes('style="color:#926bc7"') ||
    previewHtml.includes('tile-cover-state')) {
  throw new Error('Cover preview did not render through the real WebUI path');
}

const localizedCoverStates = vm.runInContext(`[
  coverPreviewStateText({ state: 'open', available: true }),
  coverPreviewStateText({ state: 'opening', available: true }),
  coverPreviewStateText({ state: 'closed', available: true }),
  coverPreviewStateText({ state: 'closing', available: true }),
  coverPreviewStateText({ state: 'unavailable', available: false }),
  coverPreviewStateText({ state: 'unknown', available: true })
]`, sandbox);
const expectedCoverStates = [
  'Offen', '\u00d6ffnet', 'Geschlossen', 'Schlie\u00dft',
  'Nicht verf\u00fcgbar', 'Unbekannt'
];
if (JSON.stringify(localizedCoverStates) !== JSON.stringify(expectedCoverStates)) {
  throw new Error(
    `Cover state localization failed: ${JSON.stringify(localizedCoverStates)}`);
}

const officialCoverIcons = vm.runInContext(`[
  coverPreviewIcon({ deviceClass: 'blind', state: 'open' }),
  coverPreviewIcon({ deviceClass: 'blind', state: 'closed' }),
  coverPreviewIcon({ deviceClass: 'curtain', state: 'closing' }),
  coverPreviewIcon({ deviceClass: 'curtain', state: 'opening' }),
  coverPreviewIcon({ deviceClass: 'damper', state: 'closed' }),
  coverPreviewIcon({ deviceClass: 'shade', state: 'closed' }),
  coverPreviewIcon({ deviceClass: '', state: 'opening' })
]`, sandbox);
const expectedCoverIcons = [
  'blinds-horizontal', 'blinds-horizontal-closed',
  'arrow-collapse-horizontal', 'arrow-split-vertical',
  'circle-slice-8', 'roller-shade-closed', 'arrow-up-box'
];
if (JSON.stringify(officialCoverIcons) !== JSON.stringify(expectedCoverIcons)) {
  throw new Error(
    `Cover icons no longer match Home Assistant: ${JSON.stringify(officialCoverIcons)}`);
}

const adminCss = readText(
  new URL('../../../src/web/assets/admin.css', import.meta.url));
if (!/\.tile\.sensor,\s*\.tile\.cover\s*\{/.test(adminCss) ||
    !/\.tile:is\(\.sensor,[^)]*\.cover[^)]*\) > \.tile-title\s*\{/.test(adminCss) ||
    !/\.tile:is\(\.sensor,[^)]*\.cover[^)]*\) > \.tile-icon\s*\{/.test(adminCss)) {
  throw new Error('Cover preview no longer shares the Sensor tile layout');
}

const coverRenderer = readText(
  new URL('../../../src/types/cover/renderer.cpp', import.meta.url));
for (const marker of [
  'tile_layout::scale_480(-8)',
  'tile_layout::header_title_font()',
  'lv_obj_set_width(widget.title_label, LV_PCT(70))',
  'lv_obj_set_style_text_line_space(widget.value_label, 8, 0)',
  'tile_layout::scale(28)',
  'resolve("blinds-horizontal", "blinds-horizontal-closed"',
  '"arrow-collapse-horizontal", "arrow-split-vertical"',
  'resolve("window-open", "window-closed", "arrow-down-box"',
  'bool is_standard_cover_icon(const String& icon_name)',
  'String cover_resolve_icon(const Tile& tile, const CoverState& state',
  'if (icon.length() && !is_standard_cover_icon(icon)) return icon;',
  'cover_resolve_icon(*tile, init.state)'
]) {
  if (!coverRenderer.includes(marker)) {
    throw new Error(`Cover renderer lost Sensor layout marker: ${marker}`);
  }
}
const tileUiSource = readText(
  new URL('../../../src/ui/tabs/tiles/tab_tiles_unified.cpp', import.meta.url));
for (const marker of [
  '#include "src/types/cover/renderer.h"',
  'icon_name = cover_resolve_icon(',
  'widgets[i].dynamic_icon = dynamic_icon',
  'refresh_cover_popup_for_tile(grid_type, i)'
]) {
  if (!tileUiSource.includes(marker)) {
    throw new Error(`Cover live-icon refresh contract is missing: ${marker}`);
  }
}
if (!/set_label_style\(widget\.value_label,\s*lv_color_white\(\),\s*tile_layout::header_title_font\(\)\)/s.test(coverRenderer)) {
  throw new Error('Cover value no longer uses the smaller title font');
}

const coverWebScriptSource = readText(
  new URL('../../../src/types/cover/web_scripts.cpp', import.meta.url));
for (const key of ['open', 'opening', 'closed', 'closing', 'unavailable', 'unknown']) {
  const injection = new RegExp(`append_i18n\\(\\s*"${key}"`);
  if (!injection.test(coverWebScriptSource)) {
    throw new Error(`Cover WebUI translation is missing: ${key}`);
  }
}
const i18nSource = readText(
  new URL('../../../src/core/i18n/i18n.cpp', import.meta.url));
if (!i18nSource.includes('return locale(language_code).cover_labels[index]') ||
    !i18nSource.includes('return profile.cover_states[index]')) {
  throw new Error('Cover texts no longer use the central LocaleProfile');
}

const coverPopupSource = readText(
  new URL('../../../src/ui/popups/cover/cover_popup.cpp', import.meta.url));
if (!/if \(remote_update_blocked\(g_ctx\)\) \{\s*defer_remote_apply\(g_ctx, init\);\s*return;/s
    .test(coverPopupSource)) {
  throw new Error(
    'Cover popup no longer defers remote state while a slider is active');
}
if (coverPopupSource.includes('apply_remote_state_preserving_active_value')) {
  throw new Error(
    'Cover popup must not rebuild the full popup for remote updates during a drag');
}
const coverSliderApply = coverPopupSource.match(
  /void apply_slider_point\([\s\S]*?\r?\n}\r?\n\r?\nvoid commit_slider/,
)?.[0] ?? '';
if (!coverSliderApply ||
    !coverSliderApply.includes('is_preset_value(value)') ||
    !coverSliderApply.includes('is_preset_value(old_value)') ||
    !coverSliderApply.includes('update_preset_group')) {
  throw new Error(
    'Cover slider must update preset indicators only at preset boundaries');
}
const coverSliderCommit = coverPopupSource.match(
  /void commit_slider\([\s\S]*?\r?\n}\r?\n\r?\nvoid on_slider_track/,
)?.[0] ?? '';
if (!coverSliderCommit.includes('update_preset_group')) {
  throw new Error('Cover slider must update preset selection on final release');
}
for (const marker of [
  'lv_timer_create(remote_apply_timer_cb, delay_ms, ctx)',
  'apply_init(ctx, pending)',
  'cancel_deferred_remote_apply(g_ctx)',
  'CoverPopupMode::Position',
  'CoverPopupMode::Controls',
  'create_slider_view(',
  'create_tilt_gap_mask',
  'kTiltGapTopHeight',
  'kTiltGapBottomHeight',
  'kTiltGapPitch',
  'CoverControlsLayout::Cross',
  'CoverControlsLayout::MainLine',
  'CoverControlsLayout::TiltLine',
  'channel == CoverChannel::Position ? LV_ALIGN_RIGHT_MID',
  'const bool show_mode_switch = position_available && controls_available',
  'set_hidden(ctx->mode_row, !show_mode_switch)',
  'constexpr uint8_t kPresetValues[kPresetCount] = {100, 75, 50, 25, 0}',
  'create_preset_buttons(ctx, ctx->position_slider, CoverChannel::Position',
  'create_preset_buttons(ctx, ctx->tilt_slider, CoverChannel::Tilt',
  'slider_center_y_for_value(kPresetValues[i])',
  'void on_preset(lv_event_t* event)',
  'constexpr int kPresetButtonHeight = kVerticalSliderRadius * 2',
  'constexpr int kPresetButtonGap = kSliderColumnGap',
  'active ? LV_OPA_20 : LV_OPA_TRANSP',
  'lv_obj_set_style_text_font(label, popup_layout::font24(), 0)',
  '"arrow-bottom-left"',
  '"arrow-top-right"',
  'constexpr int kActionButtonSize = kModeButtonSize',
  '(kVerticalSliderHeight - (3 * kActionButtonSize)) / 2',
  'lv_obj_set_style_bg_opa(button, LV_OPA_10, 0)',
  'lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_STATE_PRESSED)',
  'lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0)',
  '"format-list-bulleted"',
  '"swap-vertical"',
  'constexpr uint32_t kLivePublishIntervalMs = 500',
  'schedule_live_publish(ctx, channel)',
  'cancel_live_publish(ctx)',
  'lv_obj_invalidate_area(view.track, &dirty)',
  'lv_color_hex(kHaCoverActive)'
]) {
  if (!coverPopupSource.includes(marker)) {
    throw new Error(`Cover popup deferred-update contract is missing: ${marker}`);
  }
}
if (coverPopupSource.includes('kAccent') ||
    !coverPopupSource.includes('constexpr uint32_t kHaCoverActive = 0x926BC7')) {
  throw new Error(
    'Cover popup controls and state icon must use one fixed HA accent color');
}
if (!/kPresetLabels\[kPresetCount\][\s\S]*"100%", "75%", "50%", "25%", "0%"/.test(
      coverPopupSource)) {
  throw new Error('Cover popup preset labels no longer match slider heights');
}
if (coverPopupSource.includes('ctx->state.position > 0') ||
    !coverPopupSource.includes(
      'lv_area_t middle = {area.x1, top_center_y, area.x2, center_y}')) {
  throw new Error(
    'Cover position fill must run from the fixed top cap to the moving edge');
}
if (coverPopupSource.includes('LV_GRAD_DIR_VER') ||
    !coverPopupSource.includes(
      'lv_obj_set_style_bg_grad_dir(view.track, LV_GRAD_DIR_NONE, 0)') ||
    !coverPopupSource.includes('lv_obj_set_size(gap, LV_PCT(100), gap_height)') ||
    !coverPopupSource.includes('lv_obj_set_style_clip_corner(view.track, true, 0)')) {
  throw new Error(
    'Cover tilt gaps must be full-width children clipped by a uniform rounded track');
}
if (coverPopupSource.includes('view.value_label') ||
    coverPopupSource.includes('update_slider_label')) {
  throw new Error('Cover sliders restored the removed labels below the tracks');
}
if (coverPopupSource.includes('"arrow-down-left"') ||
    coverPopupSource.includes('"arrow-up-right"') ||
    coverPopupSource.includes('kButtonBg')) {
  throw new Error(
    'Cover controls restored bent tilt arrows or opaque action tiles');
}

const adminSource = readText(
  new URL('../../../src/web/assets/admin.js', import.meta.url));
if (/\bhumanizeIdentifier\s*\(/.test(adminSource)) {
  throw new Error(
    'Admin WebUI calls the firmware-only humanizeIdentifier helper');
}

console.log('Admin Cover editor contract: PASS');

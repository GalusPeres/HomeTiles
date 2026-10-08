// PIN lock in the icon (user 2026-10-02, mockup
// build/design-mockups/pin-lock-badge/pin-lock-icon.html): a PIN-protected
// Folder or Settings tile shows a lock at the bottom right of its icon, any
// icon (a Folder takes every icon), with a rim in the color behind the icon;
// the PIN popup header shows the same lock. Device, live preview, grid preview
// and server-rendered grid agree.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const between = (source, start, end) => {
  const at = source.indexOf(start);
  assert.ok(at >= 0, `missing ${start}`);
  return source.slice(at, end ? source.indexOf(end, at) : source.indexOf('\n}\n', at));
};

// The shared mark: mdi:lock in UTF-8, about 46 % of the icon like
// mdi:folder-lock (variant A), a 6 % rim.
const mark = read('src/ui/shared/icon_lock_mark.h');
const lockCode = Number.parseInt(read('src/tiles/icons/mdi_icons.cpp').match(/\{"lock", 0x([0-9A-Fa-f]+)\}/)[1], 16);
assert.ok(mark.includes(`inline constexpr uint32_t kCodepoint = 0x${lockCode.toString(16).toUpperCase()};`));
const utf8 = [...Buffer.from(String.fromCodePoint(lockCode), 'utf8')].map(b => '\\x' + b.toString(16).toUpperCase()).join('');
assert.ok(mark.includes(`inline constexpr char kGlyph[] = "${utf8}";`), 'kGlyph is mdi:lock in UTF-8');
assert.ok(mark.includes('const int32_t want = icon_line * 46 / 100;'));
assert.ok(mark.includes('&mdi_bar_icons_15, &mdi_bar_icons_18, &mdi_bar_icons_22, &mdi_bar_icons_26,'));
assert.ok(mark.includes('inline int32_t rim_for(int32_t icon_line) { return LV_MAX(2, (icon_line * 6 + 50) / 100); }'));
const draw = between(mark, 'inline void draw(lv_layer_t* layer');
// Right and bottom edge at 0.9 of the icon: the rim leaves the top corner whole
// and stays inside a round disc.
// The glyph's own box (a wider header label centers the icon).
assert.ok(draw.includes('lv_label_get_letter_pos(icon, 0, &at);') &&
  draw.includes('const int32_t x = box.x1 + at.x + glyph_width * 90 / 100 - lock_width * 833 / 1000;') &&
  draw.includes('const int32_t y = box.y1 + at.y + line * 90 / 100 - lock_line * 958 / 1000;') &&
  draw.includes('dsc.color = rim_color;') && draw.includes('lv_area_move(&shifted,') &&
  draw.includes('dsc.color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);'));
assert.ok(between(mark, 'inline lv_color_t behind(').includes('lv_color_mix(lv_obj_get_style_bg_color(disc, LV_PART_MAIN), under, opa)'));

// Tiles: Folders with a target PIN and Settings with the Settings PIN; no own
// icon shows the lock as the icon; the rim takes the disc over the card.
const nav = read('src/types/navigate/renderer.cpp');
assert.ok(between(nav, 'static bool navigate_tile_locked(').includes('configManager.getConfig().settings_pin_enabled') &&
  between(nav, 'static bool navigate_tile_locked(').includes('tileConfig.isFolderPinEnabled(navFolderIdFromTile(tile))'));
assert.ok(nav.includes('const bool lock_is_icon = locked && !has_icon && FONT_MDI_ICONS != nullptr;') &&
  nav.includes('iconChar = getMdiChar("lock");'));
assert.ok(nav.includes('if (locked && !lock_is_icon && icon_lbl) {') &&
  nav.includes('lv_obj_add_event_cb(icon_lbl, lock_mark_event_cb, LV_EVENT_DRAW_POST, nullptr);') &&
  nav.includes('lv_obj_add_event_cb(icon_lbl, lock_mark_event_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);'));
const tileCb = between(nav, 'static void lock_mark_event_cb(');
assert.ok(tileCb.includes('lv_obj_t* disc = tile_icon_disc::disc_of(icon);') &&
  tileCb.includes('lv_obj_t* card = lv_obj_get_parent(disc ? disc : icon);'));

// A PIN change rebuilds the tiles so the lock appears or goes at once.
assert.match(read('src/web/server/handlers/web_admin_tiles.cpp'),
  /const bool was_enabled = tileConfig\.isFolderPinEnabled\(folder_id\);[\s\S]*if \(tileConfig\.isFolderPinEnabled\(folder_id\) != was_enabled\) tiles_request_reload_all\(\);/);
assert.match(read('src/web/server/handlers/web_admin_handlers.cpp'),
  /cfg\.settings_pin_enabled != previous_cfg\.settings_pin_enabled\) \{[\s\S]{0,120}tiles_request_reload_all\(\);/);

// PIN popup: the shared header carries the lock for Folders and Settings only;
// another popup removes it.
const shell = read('src/ui/popups/popup_shell.cpp');
assert.ok(between(shell, 'void detach() {').includes('set_header_lock(false);'));
assert.ok(between(shell, 'void header_lock_event_cb(').includes('icon_lock_mark::behind(shell.icon_disc, under)'));
const pin = read('src/ui/popups/pin/pin_popup.cpp');
assert.equal((pin.match(/popup_shell_icon_lock\(g_ctx->card, init\.lock_mark\);/g) || []).length, 2);
const ui = read('src/ui/ui_manager.cpp');
assert.ok(between(ui, 'void UIManager::requestSettingsAccess(').includes('init.lock_mark = true;'));
assert.ok(between(ui, 'void UIManager::requestFolderAccess(').includes('init.lock_mark = true;'));
assert.ok(!read('src/ui/popups/device/device_popup.cpp').includes('lock_mark'), 'device codes show no PIN lock');

// Previews: the same rule, markup and rim.
const previewTileLocked = extractDeliveredFunction('previewTileLocked');
const run = (typeValue, folderPin, settingsPin) => {
  const document = {getElementById: id => id === 'folder0_settings_pin_enabled'
    ? {dataset: {pinConfigured: settingsPin ? '1' : '0'}} : null};
  return vm.runInNewContext(`${previewTileLocked}; previewTileLocked(type, tile)`,
    {document, type: typeValue, tile: {dataset: {folderPinEnabled: folderPin ? '1' : '0'}}});
};
assert.equal(run('4', true, false), true, 'Folder with PIN');
assert.equal(run('4', false, true), false, 'Folder without PIN');
assert.equal(run('7', false, true), true, 'Settings with PIN');
assert.equal(run('7', true, false), false, 'Settings without PIN');
assert.equal(run('8', true, true), false, 'Back never');
for (const file of ['src/web/admin/tiles/grid-preview.js', 'src/web/admin/tiles/live-preview.js']) {
  const source = read(file);
  assert.ok(source.includes("typeof previewTileLocked === 'function' && previewTileLocked(") &&
    source.includes("if (lockIsIcon) iconName = 'lock';") &&
    source.includes("(locked && !lockIsIcon ? PREVIEW_LOCK_MARK : '') + '</i>'"), file);
}
const grid = read('src/web/admin/tiles/grid-preview.js');
assert.ok(grid.includes("el.dataset.folderPinEnabled = tile.folder_pin_enabled === true ? '1' : '0';"));
assert.ok(between(grid, 'function applyIconDiscTint(').includes("tileElem.style.setProperty('--icon-lock-rim',"));
assert.ok(read('src/types/navigate/admin.js').includes("if (typeof updateTilePreview === 'function') updateTilePreview(tab);"));
const server = read('src/web/server/render/web_admin_html.cpp');
assert.ok(server.includes('html += "\\" data-folder-pin-enabled=\\"";') &&
  server.includes('if (lock_is_icon) iconName = "lock";') &&
  server.includes('<span class=\\"tile-icon-lock mdi mdi-lock\\" aria-hidden=\\"true\\"></span>'));
// The parked Settings tile (Home parking slot) shows the lock too, from the
// server and after a browser redraw; a Settings PIN change redraws it.
assert.ok(server.includes('const bool hidden_locked = cfg.settings_pin_enabled;') &&
  server.includes('if (hidden_locked && hidden_icon != "lock") {'));
const access = read('src/web/admin/settings/access.js');
const parked = between(access, '  function renderSettingsHiddenSlot(', '\n  }\n');
assert.ok(parked.includes("previewTileLocked('7', tile)") &&
  parked.includes("if (locked && iconName !== 'lock') icon.innerHTML = PREVIEW_LOCK_MARK;"));
assert.ok(access.includes("if ((pinToggle.dataset.pinConfigured === '1') !== lockedBefore) refreshSettingsTileLock();"));
assert.ok(between(access, '  function refreshSettingsTileLock(', '\n  }\n').includes('renderSettingsHiddenSlot(true);'));
const css = read('src/web/assets/admin.css');
const rule = between(css, '.tile-icon > .tile-icon-lock,\n.head-bar-preview .head-gear > i > .tile-icon-lock {', '}');
assert.ok(rule.includes('font-size:0.46em;') && rule.includes('margin-left:0.037em; margin-top:-0.088em;') &&
  rule.includes('var(--icon-lock-rim, #2e2e2e)'));
assert.equal((rule.match(/var\(--icon-lock-rim/g) || []).length, 24, 'a full ring and a half ring');

// The head's gear (home_bar.cpp) shows the lock too, on the panel and in the
// preview, and follows a PIN change in the Web Admin.
const bar = read('src/ui/tabs/tiles/home_bar.cpp');
assert.ok(bar.includes('if (configManager.getConfig().settings_pin_enabled) {') &&
  bar.includes('lv_obj_add_event_cb(glyph, gear_lock_event_cb, LV_EVENT_DRAW_POST, nullptr);') &&
  bar.includes('icon_lock_mark::draw(lv_event_get_layer(event), icon, under);'));
assert.ok(server.includes('html += home && configManager.getConfig().settings_pin_enabled'));
assert.ok(between(access, '  function refreshSettingsTileLock(', '\n  }\n').includes(
  "if (locked) icon.insertAdjacentHTML('beforeend', PREVIEW_LOCK_MARK);"));

console.log('PIN lock: tiles, the head gear, PIN popup header and Web Admin previews show the lock in the icon');

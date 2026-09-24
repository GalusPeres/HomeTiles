// Built-in camera privacy indicator: a gently pulsing red stripe in the top
// grid margin whose ends fade out, and a pill hanging from it that is laid out
// like a 2x0.5 tile with the popup shadow (the first user of a pill that later
// notifications can share), above every screen, popup and the screensaver.
// A tap on the pill only ends the running live stream: nothing is paused or
// stored, and Home Assistant can open the camera again right away. The pause
// is a Home Assistant switch only and is not persisted.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const indicator = read('src/ui/shared/camera_indicator.h');
const service = read('src/video/local_camera/local_camera.cpp');
const api = read('src/video/local_camera/local_camera.h');
const contract = read('src/video/local_camera/local_camera_contract.h');
const request = read('src/video/local_camera/local_camera_request.h');
const ui = read('src/ui/ui_manager.cpp');
const i18nHeader = read('src/core/i18n/i18n.h');
const i18nTables = read('src/core/i18n/i18n.cpp');
const tileConfig = read('src/tiles/config/tile_config.h');
const compact = read('src/tiles/runtime/compact_sensor_layout.h');
const popupShell = read('src/ui/popups/popup_shell.cpp');

// Stripe: top margin minus the one-pixel gap, fixed width, never clickable,
// static. The wings fade out towards their outer ends, the section along the
// pill stays solid. No animation: it would redraw and rotate part of the
// screen every frame while the camera shares the 2D-DMA with the display.
assert.match(indicator, /constexpr int kStripeHeight = GRID_PAD_TOP > 1 \? GRID_PAD_TOP - 1 : 1;/);
assert.match(indicator, /lv_obj_remove_flag\(part, static_cast<lv_obj_flag_t>\(LV_OBJ_FLAG_CLICKABLE/,
  'The stripe never takes touch input');
assert.doesNotMatch(indicator, /lv_obj_set_width\(/, 'The stripe no longer grows and shrinks');
assert.match(indicator, /setFadeGradient\(ui\.left, lv_color_hex\(kRed\), LV_OPA_TRANSP, LV_OPA_COVER, 0, kFadeStop\);/);
assert.match(indicator, /setFadeGradient\(ui\.right, lv_color_hex\(kRed\), LV_OPA_COVER, LV_OPA_TRANSP, 255 - kFadeStop, 255\);/);
assert.match(indicator, /lv_obj_set_style_bg_grad_dir\(obj, LV_GRAD_DIR_HOR, 0\);/);
assert.doesNotMatch(indicator, /lv_anim_/, 'No animation');
assert.match(indicator, /ui\.bar = createStripePart\(pill_x, pill_width\);/, 'Solid stripe along the pill');
assert.match(indicator, /ui\.left = createStripePart\(pill_x - wing_width, wing_width\);/);
assert.match(indicator, /ui\.right = createStripePart\(pill_x \+ pill_width, wing_width\);/);
{
  const stop = Number(indicator.match(/kFadeStop = (\d+);/)[1]);
  assert.ok(stop > 0 && stop < 255, 'The outer ends fade out');
}

// Pill: hangs from the stripe (top edge 0), a 2x0.5 tile with the shared
// half-height tile content, the tile radius, the webcam icon and the popup
// card shadow.
assert.match(indicator, /lv_obj_set_pos\(ui\.pill, pill_x, 0\);/);
assert.match(indicator, /constexpr float kPillSpan = 2\.0f;/);
assert.match(indicator, /tile_geometry::extent\(0\.0f, kPillSpan, GRID_CELL_W, GRID_GAP\)/);
// The pill reaches the bottom edge of the half tiles in the top row; its
// tile part sits exactly where such a half tile sits.
assert.match(indicator, /const int tile_height = tile_geometry::extent\(0\.0f, 0\.5f, GRID_CELL_H, GRID_GAP\);\s*const int pill_height = GRID_PAD_TOP \+ tile_height;/,
  'Same rounding as the half tiles (apply_fractional_tile_geometry)');
assert.match(indicator, /lv_obj_set_size\(ui\.pill, pill_width, pill_height\);/);
assert.match(indicator, /lv_obj_set_pos\(ui\.card, 0, GRID_PAD_TOP\);/);
assert.match(indicator, /lv_obj_set_size\(ui\.card, pill_width, tile_height\);/);
// Contour border (global tile border setting only): outline frame for the
// sides and bottom, clipped below the fillets, as bright as the icon disc;
// border ring in the fillets that fades out towards the stripe. No lines
// along the stripe.
assert.doesNotMatch(indicator, /apply_global_tile_border\(/, 'Own brighter outline, not the shared tile style');
assert.match(indicator, /constexpr lv_opa_t kBorderOpa = kDiscOpa;/);
assert.match(indicator, /lv_obj_set_style_outline_pad\(ui\.frame, -1, 0\);\s*lv_obj_set_style_outline_color\(ui\.frame, lv_color_white\(\), 0\);\s*lv_obj_set_style_outline_opa\(ui\.frame, kBorderOpa, 0\);/);
assert.match(indicator, /lv_obj_set_style_outline_width\(ui\.frame, border \? 1 : 0, 0\);/,
  'The outline follows the tile border setting');
assert.match(indicator, /ui\.frame = lv_obj_create\(ui\.frame_clip\);/);
assert.match(indicator, /const int frame_top = kStripeHeight \+ fillet;/);
assert.match(indicator, /lv_obj_set_pos\(ui\.frame_clip, ui\.pill_x, frame_top\);/);
assert.match(indicator, /lv_obj_set_pos\(ui\.frame, 0, -radius\);/);
assert.doesNotMatch(indicator, /line_left|line_right/, 'No border lines along the stripe');
assert.match(indicator, /if \(d2 >= radius2 && d2 < ring2\) ring \+= 1\.0f - std::atan2\(dy, dx\) \* kQuarterTurnInv;/);
assert.match(indicator, /inline bool bordersEnabled\(\) \{ return configManager\.getConfig\(\)\.tile_borders; \}/);
assert.doesNotMatch(indicator, /apply_global_tile_border\(ui\.pill\)/);
assert.match(indicator, /lv_obj_remove_flag\(ui\.card, static_cast<lv_obj_flag_t>\(LV_OBJ_FLAG_CLICKABLE/,
  'Taps on the tile part reach the pill');
assert.ok(indicator.indexOf('lv_obj_t* top = lv_obj_create(ui.pill)') < indicator.indexOf('ui.card = lv_obj_create(ui.pill)'),
  'The tile part (content, border) lies above the square-corner block');
assert.match(indicator, /ui_surface_style::apply_radius\(ui\.pill, tile_layout::scale_480\(22\), 0\);/);
assert.match(indicator, /compact_sensor_layout::apply_content\(ui\.card, ui\.icon, ui\.title, ui\.hint, pill_width\);/);
assert.match(indicator, /getMdiChar\("webcam"\)/);
// Darker red, and a stronger icon disc than on tiles (red background).
assert.match(indicator, /constexpr uint32_t kRed = 0xC62828;/);
assert.match(indicator, /constexpr lv_opa_t kDiscOpa = 64;/);
assert.match(indicator, /lv_obj_set_style_bg_opa\(disc, kDiscOpa, 0\);/);
for (const line of ['lv_obj_set_style_shadow_width(parts.card, popup_layout::scale480(28), 0);',
                    'lv_obj_set_style_shadow_opa(parts.card, LV_OPA_40, 0);',
                    'lv_obj_set_style_shadow_spread(parts.card, popup_layout::scale480(2), 0);']) {
  assert.ok(popupShell.includes(line), `popup reference: ${line}`);
  assert.ok(indicator.includes(line.replace('parts.card', 'ui.pill')), `pill shadow: ${line}`);
}
assert.match(indicator, /lv_label_set_text\(ui\.title, strings\.local_camera_indicator_active\);/);
assert.match(indicator, /lv_label_set_text\(ui\.hint, strings\.local_camera_indicator_end\);/);
assert.doesNotMatch(indicator, /lv_label_set_text\([^,]+, "[^"]/, 'No hard-coded UI text');
// Square top corners flowing into the stripe through concave fillets; no
// pressed colour, so the joined shape stays uniform.
assert.match(indicator, /lv_obj_set_size\(top, pill_width, pill_height \/ 2\);/);
// The fillets use the tile radius (same as the pill corners) and are redrawn
// while visible when the setting or the Web Admin preview changes it.
// Fillets: half the tile radius; buffers in PSRAM, not in internal RAM.
assert.match(indicator, /inline int filletRadius\(\) \{ return std::clamp\(tileRadius\(\) \/ 2, 1, kMaxFillet\); \}/);
assert.match(indicator, /constexpr int kMaxFillet = \(tile_radius::maximum\(GRID_CELL_H, GRID_GAP\) \+ 1\) \/ 2;/);
assert.match(indicator, /LV_CANVAS_BUF_SIZE\(kMaxFillet, kMaxFillet, 32, LV_DRAW_BUF_STRIDE_ALIGN\)/);
assert.match(indicator, /heap_caps_malloc\(kBufferBytes, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT\)/);
assert.doesNotMatch(indicator, /static uint8_t buffers/, 'No static fillet buffers in internal RAM');
assert.match(indicator, /drawFillet\(ui\.fillet_left, ui\.fillet_buffers\[0\], fillet, ui\.pill_x - fillet, true, border\);/);
assert.match(indicator, /drawFillet\(ui\.fillet_right, ui\.fillet_buffers\[1\], fillet, ui\.pill_x \+ ui\.pill_width, false,\s*border\);/);
assert.match(indicator, /if \(!visible\) return;\s*updateShape\(ui\);\s*keepOnTop\(ui\);/);
// Style (Web Admin, experimental): none hides everything, line keeps only the
// stripe, pill adds the pill, its fillets and border.
assert.match(indicator, /const local_camera::IndicatorStyle style = local_camera::indicatorStyle\(\);\s*const bool visible =\s*style != local_camera::IndicatorStyle::None && local_camera::indicatorActive\(\);/);
assert.match(indicator, /setVisible\(ui, visible, style == local_camera::IndicatorStyle::Pill && pillAllowed\(\)\);/);
assert.match(indicator, /for \(lv_obj_t\* part : \{ui\.bar, ui\.left, ui\.right\}\) setShown\(part, visible\);/);
assert.match(indicator, /for \(lv_obj_t\* part : \{ui\.pill, ui\.frame_clip, ui\.fillet_left, ui\.fillet_right\}\) \{\s*setShown\(part, with_pill\);/);
assert.match(indicator, /if \(radius == ui\.radius && fillet == ui\.fillet && border == ui\.border\) return;/);
assert.match(indicator, /lv_obj_set_pos\(canvas, x, kStripeHeight\);/);
assert.doesNotMatch(indicator, /constexpr int kFillet =/, 'No fixed fillet size');
// Square top block covers the largest radius; pill bottom = half-tile bottom:
// extent(0, 0.5) = lround(0.5 * 161) - 16 = 65, so rows 0..69 like the half
// tiles in the screenshot (one row more than header_height() gave).
{
  const extent = Math.round(0.5 * (145 + 16)) - 0 - 16;
  assert.equal(extent, 65);
  assert.equal(5 + extent, 70);
  assert.ok(Math.floor((5 + extent) / 2) >= Math.floor((145 - 16 + 2) / 4));
}
assert.doesNotMatch(indicator, /LV_STATE_PRESSED/);
{
  const stop = Number(indicator.match(/kFadeStop = (\d+);/)[1]);
  const maxFillet = (Math.floor((145 - 16 + 2) / 4) + 1) >> 1;
  assert.ok(208 * (1 - stop / 255) >= maxFillet, 'The wing is solid where the largest fillet meets it');
}
// Fillet coverage: the corner between stripe and pill is filled, the far
// corner is empty (same maths as createFillet, left side).
{
  const f = 12, n = 4;
  const coverage = (column, y) => {
    let covered = 0;
    for (let sy = 0; sy < n; ++sy) for (let sx = 0; sx < n; ++sx) {
      const dx = column + (sx + 0.5) / n, dy = f - (y + (sy + 0.5) / n);
      if (dx * dx + dy * dy >= f * f) ++covered;
    }
    return covered / (n * n);
  };
  assert.equal(coverage(f - 1, 0), 1, 'Joint of stripe and pill side');
  assert.equal(coverage(0, f - 1), 0, 'Outside the curve');
  // Border ring weight: full where the curve meets the pill side, zero at the
  // stripe (same formula as drawFillet).
  const weight = (dx, dy) => 1 - Math.atan2(dy, dx) * (2 / Math.PI);
  assert.equal(weight(f, 0), 1);
  assert.ok(Math.abs(weight(0, f)) < 1e-9);
  assert.ok(weight(f * 0.7, f * 0.7) > 0.4 && weight(f * 0.7, f * 0.7) < 0.6);
}
// Half-height tiles and the pill share one content layout.
assert.match(compact, /inline void apply_content\(lv_obj_t\* card, lv_obj_t\* icon, lv_obj_t\* title, lv_obj_t\* value, int width\)/);
assert.match(compact, /apply_fractional_tile_geometry\(card, tile\);\s*apply_content\(card, icon, title, value,/);

// A tap ends the stream, it never pauses or disables the camera.
assert.match(indicator, /local_camera::endStream\(\);/);
assert.doesNotMatch(indicator, /setPaused\(|setEnabled\(/);

// Stacking: pill below the stripe (its shadow never darkens the stripe), all
// of them above later popups and the screensaver.
assert.match(indicator, /lv_obj_t\* const order\[\] = \{ui\.pill, ui\.frame_clip, ui\.bar, ui\.left, ui\.right,\s*ui\.fillet_left, ui\.fillet_right\};/);
assert.ok(indicator.indexOf('ui.pill = lv_button_create') < indicator.indexOf('ui.bar = createStripePart'),
  'The stripe is created above the pill');
assert.match(indicator, /constexpr uint32_t kPollMs = 100;/);
assert.match(indicator, /if \(started \|\| !local_camera::supported\(\)\) return;/,
  'Devices without a camera never create the timer');
assert.match(ui, /#include "src\/ui\/shared\/camera_indicator\.h"/);
assert.match(ui, /camera_indicator::init\(\);\s*\n\s*Serial\.println\("\[UI\] UI built"\);/);

// Translations: every language has the title and the hint.
assert.match(i18nHeader, /const char\* local_camera_indicator_active;\s*const char\* local_camera_indicator_end;/);
assert.doesNotMatch(i18nHeader, /local_camera_indicator_paused/);
for (const [title, hint] of [['Kamera aktiv', 'Tippen zum Beenden'], ['Camera active', 'Tap to end'],
                             ['Caméra active', 'Toucher pour arrêter']]) {
  assert.ok(i18nTables.includes(`"${title}",\n    "${hint}",`), `${title} / ${hint}`);
}

// Activity: set right before the sensor starts streaming, cleared by the
// standby every stop path runs, held briefly after.
assert.match(api, /bool indicatorActive\(\);/);
const fns = cppFunctionDefinitions(service);
const body = name => {
  const found = fns.find(item => item.name === name);
  assert.ok(found, `${name} must exist`);
  return found.body;
};
assert.match(body('sensorStandby'), /if \(g_sensor_capturing\.exchange\(false\)\) \{\s*const bool skip_hold = g_indicator_skip_hold\.exchange\(false\);\s*g_indicator_hold_until_ms\.store\(millis\(\) \+ \(skip_hold \? 0 : kIndicatorHoldMs\)\);/);
assert.equal((service.match(/g_sensor_capturing\.store\(true\);\s*(?:err = )?g_sensor\.setStream\(true\);/g) || []).length, 1,
  'Only the live stream marks the capture (no pill for thumbnail snapshots)');
assert.match(service, /No indicator for single still images[\s\S]*?\n  err = g_sensor\.setStream\(true\);/);
assert.match(body('indicatorActive'), /if \(g_sensor_capturing\.load\(\)\) return true;/);

// endStream(): ends the current session and ignores its keepalives; other
// sessions start normally. Nothing is stored.
assert.match(api, /bool endStream\(\);/);
const endStream = body('endStream');
assert.match(endStream, /if \(!g_session\.valid\) return false;/);
assert.match(endStream, /snprintf\(g_ended_session, sizeof\(g_ended_session\), "%s", g_session\.session\);/);
assert.match(endStream, /endStreamSession\(StopReason::StreamStop\);/);
assert.doesNotMatch(endStream, /prefs|g_paused|g_enabled/);
assert.match(endStream, /if \(g_stream_running\.load\(\)\) g_indicator_skip_hold\.store\(true\);/,
  'The pill disappears right after the tap');
assert.match(body('handleStreamCommand'),
  /if \(g_ended_session\[0\] != '\\0' && strcmp\(g_ended_session, command\.session\) == 0\) \{[\s\S]*?return;\s*\}\s*const bool new_session =/);
// The retained status names the ended session so the Bridge closes its
// viewers and gives the next viewer a new session; still images stay allowed.
assert.match(endStream, /endStreamSession\(StopReason::StreamStop\);[\s\S]*?publishStatus\(\);/);
assert.match(body('currentStatusFields'), /if \(g_ended_session\[0\] != '\\0'\) fields\.ended_session = g_ended_session;/);
assert.match(contract, /if \(isProtocolToken\(fields\.ended_session\) && !append\("ended", fields\.ended_session\)\)/);
assert.doesNotMatch(service, /endedViewerActive|g_ended_seen_ms/);

// The stripe stays above popups and the screensaver; the pill belongs to the
// tile grids and hides on Settings and under every open top-layer overlay.
assert.match(indicator, /lv_obj_add_event_cb\(lv_layer_top\(\), detail::onTopLayerChanged, LV_EVENT_CHILD_CREATED, nullptr\);/);
assert.match(indicator, /lv_obj_add_event_cb\(lv_layer_top\(\), detail::onTopLayerChanged, LV_EVENT_CHILD_CHANGED, nullptr\);/);
assert.match(indicator, /if \(busy \|\| !ui\.pill \|\| !ui\.visible\) return;\s*busy = true;\s*refresh\(ui\);\s*busy = false;/);
assert.match(indicator, /inline void poll\(lv_timer_t\*\) \{ refresh\(objects\(\)\); \}/);
// Tab switches set the pill before the switch is drawn (b27 hid it one poll
// later, visibly over the Settings page), and the partial Settings refresh
// after the framebuffer clear redraws the stripe (b27 kept only the pieces
// above the Settings controls on the 8-inch).
{
  const ui = read('src/ui/ui_manager.cpp');
  const partial = ui.slice(ui.indexOf('if (partial_settings_switch) {\n    lv_display_enable_invalidation(disp, true);'));
  assert.match(partial, /^if \(partial_settings_switch\) \{\s*lv_display_enable_invalidation\(disp, true\);\s*\/\/[^\n]*\n\s*camera_indicator::refreshNow\(\);/);
  // Stripe first: LVGL draws the dirty areas in order, and b28 drew the faded
  // ends only after the ~150 ms of Settings controls.
  assert.ok(partial.indexOf('BoardHAL::displayFillScreen(0x0000);') < partial.indexOf('camera_indicator::invalidateVisible();') &&
    partial.indexOf('camera_indicator::invalidateVisible();') < partial.indexOf('lv_obj_invalidate(child);') &&
    partial.indexOf('lv_obj_invalidate(child);') < partial.indexOf('lv_refr_now(disp);'));
  assert.match(ui, /camera_indicator::refreshNow\(\);\s*lv_obj_invalidate\(lv_scr_act\(\)\);/);
  assert.match(indicator, /inline void invalidateVisible\(\) \{[\s\S]*?for \(lv_obj_t\* part : \{ui\.bar, ui\.left, ui\.right\}\) lv_obj_invalidate\(part\);/);
}
assert.match(indicator, /constexpr uint8_t kSettingsTab = 3;/);
// Closed popups stay parked but visible on the top layer (b26 hid the pill for
// good on the 8-inch and the V2): the shell and the screensaver decide.
assert.match(indicator, /return tab != UINT8_MAX && tab != kSettingsTab && !popup_shell_active\(\) &&\s*!image_screensaver_covers_ui\(\);/);
// The screensaver covers the UI from its overlay's creation on, not only after
// its ~0.5 s setup (b28 showed the pill over the opening screensaver).
{
  const saver = read('src/ui/screensaver/image_screensaver.cpp');
  assert.match(saver, /bool image_screensaver_covers_ui\(\) \{\s*return g_opening \|\| g_state != nullptr;/);
  const show = saver.slice(saver.indexOf('void show_image_screensaver() {'), saver.indexOf('void hide_image_screensaver() {'));
  assert.ok(show.indexOf('g_opening = true;') < show.indexOf('st->overlay = lv_obj_create(lv_layer_top());'));
  assert.match(show, /g_state = st;\s*g_opening = false;/);
  assert.doesNotMatch(show.slice(show.indexOf('g_opening = true;'), show.indexOf('g_opening = false;')), /return;/,
    'No early return leaves the flag set');
}
assert.doesNotMatch(indicator, /overlayOpen|lv_obj_get_child_count\(layer\);[\s\S]*LV_OBJ_FLAG_HIDDEN\)\) \{\s*return true;/,
  'No guess from the top-layer children');
const shellSource = read('src/ui/popups/popup_shell.cpp');
assert.match(shellSource, /bool popup_shell_active\(\) \{ return shell\.active != nullptr; \}/);
assert.match(shellSource, /shell\.active = binding;\s*lv_obj_set_parent\(owner, lv_layer_top\(\)\);[\s\S]*?lv_obj_set_parent\(shell\.overlay, lv_screen_active\(\)\);/,
  'The shell is active before it leaves the top layer, whose change event refreshes the pill');
assert.match(shellSource, /void hide_popup_shell\(lv_obj_t\* body\) \{[\s\S]*?detach\(\);\s*lv_obj_add_flag\(shell\.overlay, LV_OBJ_FLAG_HIDDEN\);\s*lv_obj_set_parent\(shell\.overlay, lv_layer_top\(\)\);/);
for (const popup of ['camera', 'climate', 'cover', 'energy', 'light', 'media', 'pin', 'sensor', 'weather']) {
  assert.match(read(`src/ui/popups/${popup}/${popup}_popup.cpp`), /show_popup_shell\(/, ` ${popup} opens through the shell`);
}

// Encode only frames the sender can take: a pending frame is waited for (at
// most one frame interval) instead of being replaced by a fresh encode.
const upload = read('src/video/local_camera/local_camera_upload.cpp');
assert.match(upload, /bool framePending\(\) \{[\s\S]*?slot\.state == SlotState::Ready/);
const capture = body('streamCaptureFrame');
assert.ok(capture.indexOf('local_camera_upload::framePending()') < capture.indexOf('Dma2dArbiterGuard'),
  'The pending check comes before the encoder takes the 2D-DMA');
assert.match(capture, /if \(local_camera_upload::framePending\(\)\) \{\s*noteUploadBusy\(run\);\s*return;\s*\}/);
assert.match(body('noteUploadBusy'), /\+\+run\.window\.busy;[\s\S]*?reducedQuality/);

// Pause (Home Assistant switch only): RAM-only, refuses snapshots and streams,
// stops a running stream and a snapshot in progress, keeps the Bridge
// capability. The persisted pause of b17 is removed on boot.
assert.match(api, /bool paused\(\);\s*\/\/[^\n]*\n\s*bool setPaused\(bool paused\);/);
assert.doesNotMatch(body('setPaused'), /prefs|putBool/, 'The pause is not persisted');
assert.match(body('setPaused'), /g_paused\.store\(paused\);[\s\S]*?publishStatus\(\);/);
assert.match(service, /constexpr char kPrefsLegacyPausedKey\[\] = "lcam_pause";/);
assert.match(body('begin'), /legacy_pause = prefs\.isKey\(kPrefsLegacyPausedKey\);/);
assert.match(body('begin'), /if \(legacy_pause\) \{[\s\S]*?writable\.remove\(kPrefsLegacyPausedKey\);/);
assert.doesNotMatch(service, /g_paused\.store\(prefs\./, 'A stored pause is never loaded');
assert.match(body('currentGate'), /in\.enabled = g_enabled\.load\(\) && !g_paused\.load\(\);/);
assert.match(body('streamStopCondition'), /if \(!g_enabled\.load\(\) \|\| g_paused\.load\(\)\) return StopReason::Disabled;/);
assert.match(body('abortRequested'), /g_paused\.load\(\)/);
assert.match(body('handleMqttMessage'), /if \(!g_enabled\.load\(\) \|\| g_paused\.load\(\)\) \{\s*code = ErrorCode::Disabled;/);
assert.doesNotMatch(body('bridgeCapability'), /g_paused/, 'A pause keeps the camera in Home Assistant');
assert.match(body('currentStatusFields'), /if \(g_paused\.load\(\)\) \{\s*fields\.state = PublicState::Disabled;\s*fields\.paused = true;/);
assert.match(contract, /if \(fields\.paused\) \{[\s\S]*?",\\"paused\\":true"/);
// Home Assistant pause/resume commands on the camera command topic.
assert.match(request, /enum class CommandKind : uint8_t \{ Snapshot, Stream, StreamStop, Pause, Resume \};/);
assert.match(body('handleMqttMessage'), /command\.kind == CommandKind::Pause \|\| command\.kind == CommandKind::Resume[\s\S]*?setPaused\(pause\);/);

// Grid margins: leftover pixels are split between both sides (1280x800: top
// 5, bottom 6) for the tiles, the screensaver tiles and the settings grid.
assert.match(tileConfig, /static constexpr int GRID_PAD_TOP = GRID_PAD \+ GRID_EXTRA_Y \/ 2;/);
assert.match(tileConfig, /static constexpr int GRID_PAD_BOTTOM = GRID_PAD \+ GRID_EXTRA_Y - GRID_EXTRA_Y \/ 2;/);
assert.match(tileConfig, /static_assert\(GRID_EXTRA_X >= 0 && GRID_EXTRA_Y >= 0/);
for (const [file, obj] of [['src/ui/tabs/tiles/tab_tiles_unified.cpp', 'grid'],
                           ['src/ui/screensaver/image_screensaver.cpp', 'st->slot_grid'],
                           ['src/ui/tabs/settings/tab_settings.cpp', 'tab']]) {
  const source = read(file);
  for (const side of ['left', 'right', 'top', 'bottom']) {
    assert.ok(source.includes(`lv_obj_set_style_pad_${side}(${obj}, GRID_PAD_${side.toUpperCase()}, 0);`),
      `${file} ${side}`);
  }
}
// Exact numbers for the 1280x800 profiles: 7x5 cells of 168x145, gap 16, pad 4.
{
  const extraY = 800 - (5 * 145 + 4 * 16 + 2 * 4);
  assert.equal(extraY, 3);
  assert.equal(4 + Math.floor(extraY / 2), 5);
  assert.equal(4 + extraY - Math.floor(extraY / 2), 6);
  // Pill 2x0.5 tile: 352 x 65 (top-row half tile); stripe 60 % of 1280 =
  // 768, wings 208 each.
  assert.equal(2 * 168 + 16, 352);
  assert.equal(Math.round(0.5 * (145 + 16)) - 16, 65);
  assert.equal((1280 * 600 / 1000 - 352) / 2, 208);
}

console.log('Local camera indicator, stream end, pause and grid margin contract OK');

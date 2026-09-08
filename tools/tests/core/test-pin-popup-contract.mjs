import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8')
  .replace(/\r\n?/g, '\n');
const requireMarker = (source, marker, label) => {
  if (!source.includes(marker)) throw new Error(`${label} is missing: ${marker}`);
};

const popup = read('src/ui/popups/pin/pin_popup.cpp');
const manager = read('src/ui/ui_manager.cpp');
const renderer = read('src/types/navigate/renderer.cpp');
const tiles = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
const mainLoop = read('HomeTiles.ino');
const popupHeader = read('src/ui/popups/pin/pin_popup.h');
const i18nHeader = read('src/core/i18n/i18n.h');
const i18nSource = read('src/core/i18n/i18n.cpp');
const power = read('src/core/power/power_manager.cpp');
const screensaver = read('src/ui/screensaver/image_screensaver.cpp');

for (const marker of [
  'constexpr int kKeySize = popup_layout::scale(92);',
  'constexpr uint32_t kAutoCloseMs = 60000;',
  'constexpr int kDigitGridExtent = popup_layout::contentScale(340);',
  'constexpr int kKeypadHeight =',
  'constexpr int kKeypadTop = kBottomRowTop - kKeypadHeight + kKeySize;',
  'kKeypadTop - kDigitGap - popup_layout::kValueHeight;',
  'constexpr uint32_t kLastDigitRevealMs = 600;',
  'constexpr int keypad_row_y(uint8_t row)',
  'return row * (kKeySize + kDigitGap);',
  'for (uint8_t digit = 1; digit <= 9; ++digit)',
  'getMdiChar("backspace-outline")',
  'grid, "0", popup_layout::font32()',
  'getMdiChar("check-bold")',
  "masked[i] = '*'",
  'masked[ctx->length - 1] = ctx->input[ctx->length - 1];',
  'tr.pin_popup_incorrect',
  'pin_access::secureClear(ctx->input, sizeof(ctx->input))',
  'LV_EVENT_CLICKED',
  'lv_obj_set_style_bg_color(g_ctx->card, lv_color_hex(init.bg_color), 0);',
  'create_popup_body(on_close, ctx, init.bg_color);',
  'popup_icon_glyph(init.icon_name)',
  'lv_timer_create(auto_close_timer_cb, kAutoCloseMs, ctx)',
  'arm_auto_close_timer(ctx);',
  'arm_auto_close_timer(g_ctx);',
  'lv_timer_pause(g_ctx->auto_close_timer);',
  'lv_timer_delete(ctx->auto_close_timer);',
  'lv_timer_create(',
  'last_digit_reveal_timer_cb, kLastDigitRevealMs, ctx);',
  'lv_timer_delete(ctx->last_digit_reveal_timer);',
  'cancel_last_digit_reveal(ctx);',
  'lv_obj_set_pos(backspace_button, 0, keypad_row_y(3));',
  'lv_obj_set_pos(zero_button, kKeySize + kDigitGap, keypad_row_y(3));',
  '2 * (kKeySize + kDigitGap)',
]) requireMarker(popup, marker, 'PIN popup');

requireMarker(popupHeader, 'String icon_name;', 'PIN popup tile icon');
requireMarker(popupHeader, 'bool hide_on_success = true;',
              'PIN popup success-close policy');
requireMarker(popupHeader, 'resume_pin_popup_after_failed_success();',
              'PIN popup failed-switch recovery');
if ((popup.match(/popup_icon_glyph\(init\.icon_name\)/g) || []).length < 2) {
  throw new Error('PIN popup must update the source tile icon on create and reuse');
}
if (popup.includes('lv_obj_t* bottom = lv_obj_create')) {
  throw new Error('PIN popup must use one continuous 3x4 keypad grid');
}

if (/DEVICE_[A-Z0-9_]+/.test(popup)) {
  throw new Error('PIN popup layout must not contain device-specific branches');
}

const acceptedStart = popup.indexOf('const bool accepted = ctx->verify');
const acceptedEnd = popup.indexOf('\n      },\n      LV_EVENT_CLICKED', acceptedStart);
const acceptedBlock = popup.slice(acceptedStart, acceptedEnd);
for (const marker of [
  'if (hide_on_success)',
  'ctx->waiting_for_success_completion = true;',
  'pin_access::secureClear(ctx->input, sizeof(ctx->input));',
  'if (ctx->auto_close_timer) lv_timer_pause(ctx->auto_close_timer);',
  'if (success) success(callback_context);',
]) requireMarker(acceptedBlock, marker, 'Deferred PIN success');
for (const marker of [
  'if (ctx && ctx->waiting_for_success_completion) return;',
  'if (ctx->waiting_for_success_completion) return;',
]) requireMarker(popup, marker, 'Deferred PIN interaction lock');
if (acceptedStart < 0 || acceptedEnd < 0 ||
    acceptedBlock.indexOf('ctx->waiting_for_success_completion = true;') >
      acceptedBlock.indexOf('if (success) success(callback_context);')) {
  throw new Error('Protected PIN success must cover the old page before scheduling navigation');
}

for (const marker of [
  'preload_pin_popup();',
  'configManager.verifySettingsPin(pin)',
  'tileConfig.verifyFolderPin(self->pending_access_folder_id, pin)',
  'void UIManager::lockProtectedAccess()',
  'tileConfig.isFolderPinEnabled(folder_id)',
  'tiles_switch_to_folder(TileConfig::rootFolderId())',
  'config.settings_swipe_enabled',
  'createSettingsGestureZone();',
  'static constexpr int kSettingsGestureCaptureWidth = popup_layout::scale(56);',
  'const int32_t edge_zone = kSettingsGestureCaptureWidth;',
  'settings_gesture_indev_event_cb',
  'lv_indev_get_point(input, &point);',
  'LV_EVENT_PRESSED, this);',
  'LV_EVENT_RELEASED, this);',
  'static_cast<lv_obj_t*>(lv_event_get_param(event));',
  'if (object == self->tab_panels[0])',
  'if (!started_on_home) return;',
  'settings_gesture_pressed_object_event_cb',
  'LV_EVENT_PRESSING, self);',
  'LV_EVENT_DELETE, self);',
  'void UIManager::processSettingsGestureMotion',
  'access_gesture_triggered = true;',
  'lv_indev_wait_release(input);',
  'lv_indev_reset(input, nullptr);',
  'void UIManager::refreshSettingsGestureZone()',
  'void UIManager::setSettingsGestureStyle(const String& title,',
  'const uint32_t snapshot_color = snapshot.bg_color;',
  '? (snapshot_color != 0',
  ': 0x2A2A2A)',
  ': settings_gesture_bg_color;',
  'config.settings_tile_hidden && snapshot.valid',
  'requestSettingsAccess(title, icon_name, bg_color);',
  'SettingsRevealEdge::Left:',
  'SettingsRevealEdge::Right:',
  'SettingsRevealEdge::Top:',
  'SettingsRevealEdge::Bottom:',
  'static constexpr int kSettingsGestureThreshold = popup_layout::scale(48);',
  'const int32_t threshold = kSettingsGestureThreshold;',
  'matches = dx >= threshold && abs_dx > abs_dy;',
  'matches = -dx >= threshold && abs_dx > abs_dy;',
  'matches = dy >= threshold && abs_dy > abs_dx;',
  'matches = -dy >= threshold && abs_dy > abs_dx;',
]) requireMarker(manager, marker, 'PIN navigation and edge gesture');

const settingsAccessStart = manager.indexOf('void UIManager::requestSettingsAccess');
const folderAccessStart = manager.indexOf('void UIManager::requestFolderAccess');
const lockAccessStart = manager.indexOf('void UIManager::lockProtectedAccess');
const settingsAccessBlock = manager.slice(settingsAccessStart, folderAccessStart);
const folderAccessBlock = manager.slice(folderAccessStart, lockAccessStart);
if (settingsAccessBlock.includes('hide_on_success = false')) {
  throw new Error('Settings PIN must retain its established immediate-close behavior');
}
requireMarker(folderAccessBlock, 'init.hide_on_success = false;',
              'Folder PIN deferred close');
requireMarker(manager.slice(lockAccessStart,
                            manager.indexOf('\nbool UIManager::verify_pending_access', lockAccessStart)),
              'tiles_cancel_folder_switch(pending_access_folder_id);',
              'Sleep cancellation of pending protected folder');
requireMarker(manager.slice(lockAccessStart,
                            manager.indexOf('\nbool UIManager::verify_pending_access', lockAccessStart)),
              'detachSettingsGesturePressedObject();',
              'Sleep cancellation of active edge gesture listener');

const finishStart = manager.indexOf('void UIManager::finishFolderSwitch');
const finishEnd = manager.indexOf('\nvoid UIManager::createSettingsGestureZone', finishStart);
const finishBlock = manager.slice(finishStart, finishEnd);
for (const marker of [
  'pending_access_folder_id != folder_id',
  'if (!success)',
  'resume_pin_popup_after_failed_success();',
  'hide_pin_popup();',
]) requireMarker(finishBlock, marker, 'Folder PIN switch completion');

for (const marker of [
  'void tiles_process_pending_folder_switch()',
  'tiles_process_reload_requests();',
  'if (g_folder_switch_pending) return;',
  'lv_refr_now(display);',
  'void tiles_cancel_folder_switch(uint16_t folder_id)',
]) requireMarker(tiles, marker, 'Prioritized folder switch');
if ((tiles.match(/uiManager\.finishFolderSwitch\(folder_id, true\);/g) || []).length < 3 ||
    (tiles.match(/uiManager\.finishFolderSwitch\(folder_id, false\);/g) || []).length < 4) {
  throw new Error('Folder cache success and failure exits must complete the PIN lifecycle');
}

const activeLvgl = mainLoop.lastIndexOf('lv_timer_handler();');
const prioritizedSwitch = mainLoop.indexOf(
  'tiles_process_pending_folder_switch();', activeLvgl);
const mqttDrain = mainLoop.indexOf('mqtt_process_inbound_queue(', activeLvgl);
if (activeLvgl < 0 || prioritizedSwitch < activeLvgl ||
    mqttDrain < prioritizedSwitch) {
  throw new Error('A touch-requested folder switch must run after LVGL and before MQTT draining');
}
const apLvgl = mainLoop.indexOf('lv_timer_handler();');
const apPrioritizedSwitch = mainLoop.indexOf(
  'tiles_process_pending_folder_switch();', apLvgl);
const apWebHandling = mainLoop.indexOf('webConfigServer.handle();', apLvgl);
if (apLvgl < 0 || apPrioritizedSwitch < apLvgl ||
    apWebHandling < apPrioritizedSwitch) {
  throw new Error('AP-mode folder switching must also precede Web handling');
}

const completeAccessStart = manager.indexOf('void UIManager::complete_pending_access');
const completeAccessEnd = manager.indexOf('\nvoid UIManager::finishFolderSwitch', completeAccessStart);
if (manager.slice(completeAccessStart, completeAccessEnd)
    .includes('tiles_process_reload_requests')) {
  throw new Error('A PIN touch callback must never build a folder grid synchronously');
}

for (const forbidden of [
  'settings_gesture_zone = lv_obj_create',
  'lv_obj_move_foreground(settings_gesture_zone)',
  'lv_indev_stop_processing(input)',
  'access_gesture_timer',
  'processAccessGesture()',
]) {
  if (manager.includes(forbidden)) {
    throw new Error(`Settings gesture must not reserve or block an edge strip: ${forbidden}`);
  }
}

if (!manager.includes('(snapshot_color & TILE_BG_COLOR_RGB_MASK)')) {
  throw new Error('An explicitly black Settings tile must remain black in the PIN popup');
}

if (manager.includes('popup_layout::scale(72)') ||
    manager.includes('config.settings_tile_hidden)')) {
  throw new Error('The Settings gesture must be independent from tile visibility');
}
for (const forbidden of ['tiles_is_active_grid_object(pressed_object)']) {
  if (manager.includes(forbidden)) {
    throw new Error(`Edge gesture must not depend on a tile object: ${forbidden}`);
  }
}

const motionStart = manager.indexOf('void UIManager::processSettingsGestureMotion');
const motionEnd = manager.indexOf(
  '\nvoid UIManager::settings_gesture_pressed_object_event_cb', motionStart);
const motionBlock = manager.slice(motionStart, motionEnd);
const duplicateGuard = motionBlock.indexOf('access_gesture_triggered)');
const matchGate = motionBlock.indexOf('if (!matches) {');
const markTriggered = motionBlock.indexOf('access_gesture_triggered = true;');
const activeContactGate = motionBlock.indexOf('if (!released)');
const waitRelease = motionBlock.indexOf('lv_indev_wait_release(input);');
const reset = motionBlock.indexOf('lv_indev_reset(input, nullptr);');
if (motionStart < 0 || motionEnd < 0 || duplicateGuard < 0 ||
    matchGate < duplicateGuard || markTriggered < matchGate ||
    activeContactGate < markTriggered || waitRelease < activeContactGate ||
    reset < waitRelease) {
  throw new Error('Only one confirmed edge swipe may consume the contact and open Settings');
}

const objectPressingStart = manager.indexOf(
  'void UIManager::settings_gesture_pressed_object_event_cb');
const objectPressingEnd = manager.indexOf(
  '\nvoid UIManager::settings_gesture_indev_event_cb', objectPressingStart);
const objectPressingBlock = manager.slice(objectPressingStart, objectPressingEnd);
for (const marker of [
  'if (code == LV_EVENT_DELETE)',
  'if (code != LV_EVENT_PRESSING) return;',
  'processSettingsGestureMotion(input, false);',
]) requireMarker(objectPressingBlock, marker, 'Active-object PRESSING bridge');

const indevStart = manager.indexOf('void UIManager::settings_gesture_indev_event_cb');
const indevEnd = manager.indexOf('\nvoid UIManager::nav_button_event_cb', indevStart);
const indevBlock = manager.slice(indevStart, indevEnd);
for (const marker of [
  'if (code == LV_EVENT_RELEASED)',
  'processSettingsGestureMotion(input, true);',
]) requireMarker(indevBlock, marker, 'RELEASED fallback');
if (indevBlock.includes('if (code == LV_EVENT_PRESSING)')) {
  throw new Error('LVGL 9.5 does not forward PRESSING to input-device callbacks');
}

requireMarker(renderer, 'String icon_name;', 'Navigation popup icon');
requireMarker(renderer, 'uint32_t bg_color;', 'Navigation popup color');
requireMarker(renderer,
              'uiManager.setSettingsGestureStyle(tile.title, tile.icon_name, btn_color);',
              'Visible Settings edge-swipe identity');
requireMarker(renderer,
              'uiManager.requestSettingsAccess(data->title, data->icon_name,',
              'Settings navigation gate');
requireMarker(renderer,
              'uiManager.requestFolderAccess(data->target_folder_id, data->title,',
              'Folder navigation gate');
requireMarker(renderer, 'data->icon_name,', 'Folder navigation icon');
for (const marker of [
  'const char* pin_popup_unlock_format;',
  'const char* settings_tile_parking;',
]) requireMarker(i18nHeader, marker, 'Central PIN popup translations');
for (const marker of [
  '"%s entsperren"',
  '"Unlock %s"',
  '"Déverrouiller %s"',
]) requireMarker(i18nSource, marker, 'Localized source-tile unlock title');
requireMarker(power, 'uiManager.lockProtectedAccess();', 'Sleep PIN reset');
requireMarker(screensaver, 'uiManager.lockProtectedAccess();',
              'Screensaver PIN reset');

const popupFiles = [
  'camera_popup.cpp', 'climate_popup.cpp', 'cover_popup.cpp',
  'energy_popup.cpp', 'light_popup.cpp', 'media_popup.cpp',
  'sensor_popup.cpp', 'weather_popup.cpp'
];
for (const file of popupFiles) {
  requireMarker(read(`src/ui/popups/${file.replace('_popup.cpp', '')}/${file}`), 'hide_pin_popup();',
                `${file} mutual exclusion`);
}

const layouts = [
  {name: '480x480', height: 480, compact: '480', scale: value => Math.trunc((value * 2 + 1) / 3), content: value => Math.trunc((value * 2 + 1) / 3)},
  {name: '1024x600', height: 600, compact: '1024', scale: value => Math.trunc((value * 5 + 3) / 6), content: value => Math.trunc((value * 3 + 2) / 4)},
  {name: '720x720', height: 720, compact: '', scale: value => value, content: value => value},
  {name: '1280x720', height: 720, compact: '', scale: value => value, content: value => value},
  {name: '1280x800', height: 800, compact: '', scale: value => value, content: value => value},
];
for (const layout of layouts) {
  const key = layout.scale(92);
  const extent = layout.content(340);
  const gap = Math.max(0, Math.trunc((extent - 3 * key) / 2));
  if (3 * key + 2 * gap > extent) {
    throw new Error(`${layout.name} PIN digit grid overflows its body`);
  }
  const margin = layout.compact === '480' ? 3 : 4;
  const cardHeight = layout.height - 2 * margin;
  const cardPad = layout.scale(20);
  const navInset = layout.scale(6);
  let bodyY;
  let extraHeight = 0;
  if (layout.compact === '480') bodyY = layout.scale(178) - 12;
  else if (layout.compact === '1024') bodyY = layout.scale(178);
  else {
    extraHeight = Math.max(0, cardHeight - (720 - 2 * margin));
    const compactLift = extraHeight === 0 ? 28 : 0;
    bodyY = 186 - compactLift + Math.trunc(Math.trunc(extraHeight / 2) / 2);
  }
  const bottomRow = cardHeight - 2 * cardPad - navInset - key;
  const keypadHeight = 4 * key + 3 * gap;
  const keypadTop = bottomRow - keypadHeight + key;
  const valueHeight = layout.scale(74);
  const pinValueY = keypadTop - gap - valueHeight;
  const rows = [0, 1, 2, 3].map(row =>
    keypadTop + row * (key + gap));
  if (rows[3] !== bottomRow) {
    throw new Error(`${layout.name} PIN keypad misses the navigation anchor`);
  }
  if (keypadTop < bodyY) {
    throw new Error(`${layout.name} PIN keypad overlaps the value area`);
  }
  if (pinValueY >= keypadTop) {
    throw new Error(`${layout.name} masked PIN value overlaps the keypad`);
  }
  if (keypadTop - (pinValueY + valueHeight) !== gap) {
    throw new Error(
      `${layout.name} masked PIN value must use the keypad row gap`);
  }
  const clearances = rows.slice(1).map((row, index) =>
    row - rows[index] - key);
  if (clearances.some(clearance => clearance !== gap)) {
    throw new Error(
      `${layout.name} PIN keypad vertical gaps must match horizontal gaps`);
  }
}

const simulateMasking = digits => {
  const input = [];
  const frames = [];
  for (const digit of digits) {
    input.push(digit);
    frames.push(`${'*'.repeat(input.length - 1)}${digit}`);
    frames.push('*'.repeat(input.length));
  }
  return frames;
};

const maskingFrames = simulateMasking(['1', '2', '3']);
if (maskingFrames.join(',') !== '1,*,*2,**,**3,***') {
  throw new Error('Only the latest PIN digit may be revealed briefly');
}

const clearInputStart = popup.indexOf('void clear_input(PinPopupContext* ctx)');
const clearInputEnd = popup.indexOf('\nvoid update_value', clearInputStart);
const clearInputBlock = popup.slice(clearInputStart, clearInputEnd);
for (const marker of [
  'cancel_last_digit_reveal(ctx);',
  'pin_access::secureClear(ctx->input, sizeof(ctx->input));',
]) requireMarker(clearInputBlock, marker, 'PIN plaintext cleanup');

const confirmStart = popup.indexOf('cancel_last_digit_reveal(ctx);\n        update_value(ctx);');
if (confirmStart < 0 || confirmStart > popup.indexOf('const bool accepted = ctx->verify')) {
  throw new Error('PIN confirmation must mask the latest digit before verification');
}

const edgeSwipeMatches = (edge, start, end, width = 1280, height = 800) => {
  const zone = 56;
  const threshold = 48;
  const startsAtEdge = edge === 'left' ? start.x < zone
    : edge === 'right' ? start.x >= width - zone
      : edge === 'top' ? start.y < zone
        : start.y >= height - zone;
  if (!startsAtEdge) return false;
  const dx = end.x - start.x;
  const dy = end.y - start.y;
  const absDx = Math.abs(dx);
  const absDy = Math.abs(dy);
  if (edge === 'left') return dx >= threshold && absDx > absDy;
  if (edge === 'right') return -dx >= threshold && absDx > absDy;
  if (edge === 'top') return dy >= threshold && absDy > absDx;
  return -dy >= threshold && absDy > absDx;
};

if (edgeSwipeMatches('left', {x: 10, y: 200}, {x: 10, y: 200}) ||
    edgeSwipeMatches('right', {x: 1270, y: 200}, {x: 1230, y: 260}) ||
    edgeSwipeMatches('left', {x: 10, y: 200}, {x: 57, y: 200}) ||
    edgeSwipeMatches('left', {x: 56, y: 200}, {x: 120, y: 200})) {
  throw new Error('Edge taps, diagonal motion, sub-threshold motion, and non-edge starts must pass through');
}
for (const [edge, start, end] of [
  ['left', {x: 10, y: 200}, {x: 58, y: 205}],
  ['right', {x: 1270, y: 200}, {x: 1222, y: 195}],
  ['top', {x: 200, y: 10}, {x: 205, y: 58}],
  ['bottom', {x: 200, y: 790}, {x: 195, y: 742}],
]) {
  if (!edgeSwipeMatches(edge, start, end)) {
    throw new Error(`${edge} confirmed edge swipe must be recognized at release`);
  }
}

const simulateGestureContact = ({startedOnHome = true, edge = 'left',
                                 start, samples}) => {
  let eligible = startedOnHome;
  let triggered = false;
  let opens = 0;
  let consumed = 0;
  for (const sample of samples) {
    if (!eligible || triggered) continue;
    if (edgeSwipeMatches(edge, start, sample.point)) {
      triggered = true;
      eligible = false;
      opens += 1;
      consumed += 1;
    }
    else if (sample.event === 'released') {
      eligible = false;
    }
  }
  return {opens, consumed, triggered};
};

const earlyOpen = simulateGestureContact({
  start: {x: 10, y: 200},
  samples: [
    {event: 'pressing', point: {x: 30, y: 201}},
    {event: 'pressing', point: {x: 58, y: 202}},
    {event: 'released', point: {x: 70, y: 202}},
  ],
});
if (earlyOpen.opens !== 1 || earlyOpen.consumed !== 1 ||
    !earlyOpen.triggered) {
  throw new Error('A confirmed PRESSING swipe must open once before release');
}

const releaseFallback = simulateGestureContact({
  start: {x: 10, y: 200},
  samples: [{event: 'released', point: {x: 58, y: 202}}],
});
if (releaseFallback.opens !== 1 || releaseFallback.consumed !== 1) {
  throw new Error('RELEASED must retain the confirmed-swipe fallback');
}

for (const contact of [
  simulateGestureContact({
    start: {x: 10, y: 200},
    samples: [
      {event: 'pressing', point: {x: 25, y: 201}},
      {event: 'released', point: {x: 25, y: 201}},
    ],
  }),
  simulateGestureContact({
    startedOnHome: false,
    start: {x: 10, y: 200},
    samples: [
      {event: 'pressing', point: {x: 80, y: 200}},
      {event: 'released', point: {x: 80, y: 200}},
    ],
  }),
]) {
  if (contact.opens !== 0 || contact.consumed !== 0) {
    throw new Error('Edge taps and Close/Back contacts must remain untouched');
  }
}

console.log('PIN popup and gesture contract tests passed.');

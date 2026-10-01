import fs from 'node:fs';

const read = path => fs.readFileSync(new URL(path, import.meta.url), 'utf8');
const switchStateHeader = read('../../../src/types/switch/state.h');
const tileRenderer = read('../../../src/tiles/runtime/tile_renderer.cpp');
const lightHeader = read('../../../src/ui/popups/light/light_popup.h');
const lightPopup = read('../../../src/ui/popups/light/light_popup.cpp');
const switchRenderer = read('../../../src/types/switch/renderer.cpp');
const pacer = read('../../../src/ui/shared/command_pacer.h');

if (!/struct SwitchState \{\s*bool available = true;/s.test(switchStateHeader) ||
    !lightHeader.includes('bool available = true;')) {
  throw new Error('Light availability is missing from a runtime state hop');
}
for (const marker of [
  'if (normalized_state == "unavailable")',
  'out.available = false;',
  'init.available = state.available;',
  'lv_color_hex(state.available ? icon_rgb : kIconOff)',
  'view->available = state.available;',
  'if (!view->available) {'
]) {
  if (!tileRenderer.includes(marker) && !switchRenderer.includes(marker)) {
    throw new Error(`Light tile availability contract is missing: ${marker}`);
  }
}

const directToggle = switchRenderer.slice(
  switchRenderer.indexOf('void toggle_switch_tile('),
  switchRenderer.indexOf('LightPopupInit build_light_popup_init(')
);
if (!directToggle.includes('if (!current.available) return;')) {
  throw new Error('Unavailable Light still reaches the direct tile toggle path');
}
if (!switchRenderer.includes(
      'init.available = state.available;')) {
  throw new Error('Light popup builder dropped availability');
}

for (const marker of [
  'ctx->available = init.available;',
  'if (!ctx->available) {',
  'cancel_pending_live_publish(ctx);',
  'next_available != g_light_popup_ctx->available',
  'set_control_disabled(ctx->power_button, !ctx->available);',
  'if (!ctx || ctx->suppress_events || !ctx->available)',
  'get_unavailable_text()',
  '!ctx->available || !ctx->is_on'
]) {
  if (!lightPopup.includes(marker)) {
    throw new Error(`Light popup availability guard is missing: ${marker}`);
  }
}

const temperatureRelease = lightPopup.slice(
  lightPopup.indexOf('static void on_temp_track_event('),
  lightPopup.indexOf('static void apply_color_field_point(')
);
if (!temperatureRelease.includes('LV_EVENT_RELEASED') ||
    !temperatureRelease.includes('commit_color_temperature(ctx);')) {
  throw new Error('Light CCT slider no longer guarantees its final release value');
}
const temperatureCommit = lightPopup.slice(
  lightPopup.lastIndexOf('static void commit_color_temperature('),
  lightPopup.lastIndexOf('static void maybe_live_publish_color_temperature(')
);
const pacedCommit = lightPopup.slice(
  lightPopup.indexOf('static void commit_paced('),
  lightPopup.lastIndexOf('static void commit_popup_state(')
);
if (!temperatureCommit.includes('commit_paced(ctx, LightPublishKind::Temperature);') ||
    !pacedCommit.includes('cancel_pending_live_publish(ctx);') ||
    !pacedCommit.includes('send_paced(ctx, kind);')) {
  throw new Error('Light CCT release does not replace the throttled pending value');
}
// GitHub issue #11: Home Assistant's slider timing (command_pacer.h). A
// press alone sends nothing, live values need a real drag, and a waiting
// final value is sent rather than dropped when the popup closes.
for (const marker of ['kIntervalMs = 500', 'bool final_redundant(', 'uint32_t wait(']) {
  if (!pacer.includes(marker)) throw new Error(`Command pacer is missing: ${marker}`);
}
for (const marker of [
  '!ctx->user_dragging || !ctx->drag_moved',
  'constexpr int kDragThreshold = popup_layout::scale(10);',
  'flush_pending_final_publish(ctx);',
  'flush_pending_final_publish(g_light_popup_ctx);'
]) {
  if (!lightPopup.includes(marker)) throw new Error(`Light popup pacing is missing: ${marker}`);
}
for (const marker of [
  'ctx->min_color_temp_kelvin = init.min_color_temp_kelvin;',
  'ctx->max_color_temp_kelvin = init.max_color_temp_kelvin;',
  'clamp_color_temp_kelvin_to_range('
]) {
  if (!lightPopup.includes(marker)) {
    throw new Error(`Light CCT exact HA range handling is missing: ${marker}`);
  }
}

console.log('Light Home Assistant contract tests passed.');

import fs from 'node:fs';

const read = path => fs.readFileSync(new URL(path, import.meta.url), 'utf8');
const climateStateHeader = read('../../../src/types/climate/state.h');
const tileRenderer = read('../../../src/tiles/runtime/tile_renderer.cpp');
const climateHeader = read('../../../src/ui/popups/climate/climate_popup.h');
const climatePopup = read('../../../src/ui/popups/climate/climate_popup.cpp');
const climateRenderer = read('../../../src/types/climate/renderer.cpp');
const i18n = read('../../../src/core/i18n/i18n.cpp');

for (const marker of [
  'CLIMATE_FEATURE_TARGET_TEMPERATURE = 1U << 0',
  'CLIMATE_FEATURE_TARGET_TEMPERATURE_RANGE = 1U << 1',
  'CLIMATE_FEATURE_TARGET_HUMIDITY = 1U << 2',
  'CLIMATE_FEATURE_FAN_MODE = 1U << 3',
  'CLIMATE_FEATURE_PRESET_MODE = 1U << 4',
  'CLIMATE_FEATURE_SWING_MODE = 1U << 5',
  'CLIMATE_FEATURE_SWING_HORIZONTAL_MODE = 1U << 9'
]) {
  if (!climateHeader.includes(marker)) {
    throw new Error(`Official Climate feature bit is missing: ${marker}`);
  }
}

for (const source of [climateStateHeader, climateHeader]) {
  if (!source.includes('float target_humidity_step = 1.0f;')) {
    throw new Error('target_humidity_step is missing from a Climate state hop');
  }
}
for (const marker of [
  'source, "target_humidity_step", number',
  'init.target_humidity_step = state.target_humidity_step;',
  'ctx->humidity_step = init.target_humidity_step;',
  'ctx->humidity_control_active ? ctx->humidity_step : ctx->step',
  'state.target_humidity + direction * humidity_step'
]) {
  if (!(tileRenderer + climatePopup + climateRenderer).includes(marker)) {
    throw new Error(`target_humidity_step path is incomplete: ${marker}`);
  }
}

if (!climatePopup.includes('constexpr uint32_t kAdjustmentDebounceMs = 1000;') ||
    !climateRenderer.includes('constexpr uint32_t kMiniTargetDebounceMs = 1000;')) {
  throw new Error('Climate +/- controls no longer use a trailing 1,000 ms debounce');
}
for (const source of [climatePopup, climateRenderer]) {
  if (!source.includes('lv_timer_reset(')) {
    throw new Error('Climate trailing debounce no longer coalesces repeated taps');
  }
}

const arcRelease = climatePopup.match(
  /if \(code == LV_EVENT_RELEASED \|\| code == LV_EVENT_PRESS_LOST\) \{[\s\S]*?\n  \}/
)?.[0] ?? '';
for (const marker of [
  'capture_pending_humidity(ctx);',
  'commit_pending_humidity(ctx);',
  'capture_pending_temperature(ctx);',
  'commit_pending_temperature(ctx);'
]) {
  if (!arcRelease.includes(marker)) {
    throw new Error(`Climate slider final-release publish is missing: ${marker}`);
  }
}

for (const marker of [
  'if (!ctx || !ctx->available) return;',
  'discard_pending_climate_commands(ctx);',
  'capability_removed',
  '!climate_control_available(',
  'if (!state.available) return false;',
  'slot_is_interactive(kind, state)'
]) {
  if (!(climatePopup + climateRenderer).includes(marker)) {
    throw new Error(`Climate unavailable/feature guard is missing: ${marker}`);
  }
}

for (const text of [
  '"Nicht verfügbar",',
  '"Unavailable",',
  '"Indisponible",',
  '"Unbekannt"',
  '"Unknown"',
  '"Inconnu"'
]) {
  if (!i18n.includes(text)) {
    throw new Error(`Central Climate state translation is missing: ${text}`);
  }
}
if (!tileRenderer.includes('if (!state.available) return ui_theme::icon_inactive();')) {
  throw new Error('Unavailable Climate tile color is no longer neutral');
}

console.log('Climate firmware Home Assistant contract tests passed.');

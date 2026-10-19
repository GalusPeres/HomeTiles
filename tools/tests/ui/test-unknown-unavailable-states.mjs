// Unknown and unavailable like Home Assistant (user 2026-10-02 review of all
// tile types): an unknown Climate showed a white icon and popup ring, an
// unavailable media player "No playback" with working controls. Unknown is
// inactive grey; unavailable says so and disables the controls.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

// Climate: the shared mode color serves tile icon, popup icon and ring.
const visuals = read('src/types/climate/visuals.h');
const modeColor = visuals.slice(visuals.indexOf('inline uint32_t mode_foreground_color(const char* mode)'),
  visuals.indexOf('inline uint32_t mode_ring_color(const char* mode)'));
assert.match(modeColor, /if \(equals\(mode, "unknown"\)\) return ui_theme::icon_rest\(\);\s*return 0xFFFFFF;/);
assert.match(visuals, /!equals\(mode, "unknown"\)/, 'unknown stays inactive (no tile tint)');
// Unavailable: the light theme's fainter grey (user 2026-10-10); unknown and
// the dark theme keep the inactive grey.
const climatePreview = read('src/types/climate/admin-preview.js');
assert.match(climatePreview,
  /state\?\.available === false \|\| mode === 'unavailable'\) \{\s*return panelLight\(\) \? panelRoleHex\('icon-unavailable', '#bdbdbd'\) : panelRoleHex\('icon-rest', '#9e9e9e'\);/);
assert.ok(climatePreview.includes("if (mode === 'unknown') return panelRoleHex('icon-rest', '#9e9e9e');"));

// Media tile: the state label and disabled controls.
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.match(renderer, /if \(state == "unavailable" \|\| state == "unknown"\) \{\s*return i18n::entity_state_label\(configManager\.getConfig\(\)\.language, state\);/);
assert.ok(renderer.includes('widgets.available = !state.equalsIgnoreCase("unavailable");') &&
  renderer.includes('{widgets.previous_label, widgets.play_pause_label, widgets.next_label}') &&
  renderer.includes('else lv_obj_add_state(button, LV_STATE_DISABLED);'));
assert.ok(renderer.includes('init.available = widgets.available;'), 'the popup learns the availability');
const mediaTile = read('src/types/media/renderer.cpp');
assert.ok(mediaTile.includes('lv_obj_set_style_opa(btn, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);'));
assert.ok(mediaTile.includes('if (lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_current_target(e)), LV_STATE_DISABLED)) return;'));

// Media popup: buttons disabled and every command handler checks it.
const popup = read('src/ui/popups/media/media_popup.cpp');
assert.ok(popup.includes('ctx->available = init.available;') && popup.includes('apply_availability(ctx);'));
{
  const start = popup.indexOf('static void apply_availability(MediaPopupContext* ctx) {');
  const body = popup.slice(start, popup.indexOf('\n}\n', start));
  for (const label of ['shuffle_label', 'previous_label', 'play_pause_label', 'next_label', 'repeat_label',
    'volume_icon_label', 'group_label']) assert.ok(body.includes(`ctx->${label}`), `${label} dims while unavailable`);
  assert.ok(body.includes('set_control_enabled(ctx->source_button, ctx->available);') &&
    body.includes('set_control_enabled(ctx->library_button, ctx->available);'));
}
for (const handler of ['static void on_media_command(', 'static void on_volume_slider_event(',
  'static void on_seek_slider_event(', 'static void on_volume_mute_click(']) {
  // The definition, not the forward declaration.
  const start = popup.indexOf(`${handler}lv_event_t* e) {`);
  assert.ok(start >= 0, handler);
  assert.ok(popup.slice(start, popup.indexOf('\n}\n', start)).includes('available'), `${handler} checks availability`);
}
assert.equal((popup.match(/LV_OPA_30, LV_PART_MAIN \| LV_STATE_DISABLED/g) || []).length, 3,
  'the round controls, the head buttons and the sliders dim while disabled');

console.log('Unknown is inactive grey; an unavailable player disables its controls');

// Cover moving pulse (user 2026-10-02): while Home Assistant reports opening
// or closing (the arrow icon), the tile icon and the popup header icon pulse
// on the shared clock like a Lock or Alarm panel waiting for its device. A
// tilt that turns changes no state and shows nothing.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const state = read('src/types/cover/state.h');
const renderer = read('src/types/cover/renderer.cpp');
const popup = read('src/ui/popups/cover/cover_popup.cpp');

assert.match(state, /inline bool cover_state_moving\(const CoverState& state\) \{\s*return state\.valid && state\.available &&\s*\(strcmp\(state\.state, "opening"\) == 0 \|\| strcmp\(state\.state, "closing"\) == 0\);/);

// Tile: the shared clock, a refresh keeps a running pulse, every state stops
// or starts it.
assert.ok(renderer.includes('#include "src/ui/shared/ui_pulse.h"'));
assert.ok(renderer.includes('lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), ui_pulse::opa_now(), 0);'));
assert.ok(renderer.includes('if (on == (lv_anim_get(icon, icon_pulse_exec) != nullptr)) return;') &&
  renderer.includes('lv_obj_set_style_opa(icon, LV_OPA_COVER, 0);') &&
  renderer.includes('if (on) ui_pulse::start(icon, icon_pulse_exec);'));
const apply = renderer.slice(renderer.indexOf('void apply_state('), renderer.indexOf('void show_view(GridType grid_type, uint8_t index) {'));
assert.ok(apply.includes('set_icon_pulse(widget.icon_label, cover_state_moving(state));'));

// Popup: every refresh and both opening paths (the shell takes the pulse only
// once it shows).
const refresh = popup.slice(popup.indexOf('void refresh_popup('), popup.indexOf('void apply_init('));
assert.ok(refresh.includes('popup_shell_pulse_icon(ctx->card, cover_state_moving(ctx->state));'));
assert.equal((popup.match(/show_popup_shell\([^;]*\);\n\s*popup_shell_pulse_icon\(g_ctx->card, cover_state_moving\(init\.state\)\);/g) || []).length, 2);

console.log('Cover: the arrow pulses on tile and popup header while opening or closing');

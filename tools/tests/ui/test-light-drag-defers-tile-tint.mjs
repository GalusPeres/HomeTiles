// Dragging a Light color or Kelvin value must not retint the tile under the
// popup on every step: each card tint redraws the tile and the popup above
// it, which made the drag stutter. Rules refreshes wait while the Light popup
// is dragged, and the icon color "Tint tile" retint is coalesced (250 ms after
// the last change, never during a drag) with validity-checked pending cards.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const light = read('src/ui/popups/light/light_popup.cpp');
assert.ok(read('src/ui/popups/light/light_popup.h').includes('bool light_popup_is_dragging();'));
assert.match(light, /bool light_popup_is_dragging\(\) \{\s*const LightPopupContext\* ctx = g_light_popup_ctx;\s*return ctx && ctx->user_dragging && ctx->card && !lv_obj_has_flag\(ctx->card, LV_OBJ_FLAG_HIDDEN\);/);
assert.match(read('src/ui/tabs/tiles/tab_tiles_unified.cpp'),
  /void process_icon_source_updates\(\) \{[\s\S]*?if \(light_popup_is_dragging\(\)\) return;\s*uint64_t pending = g_icon_source_pending\.exchange\(0\);/,
  'Rules refreshes stay pending during a drag');
const source = read('src/tiles/runtime/tile_icon_source.cpp');
for (const marker of [
  'constexpr uint32_t kIconFillSettleMs = 250;',
  'if (light_popup_is_dragging()) return;\n  lv_timer_pause(timer);',
  'if (pending && lv_obj_is_valid(pending)) apply_icon_fill_now(pending);',
  'lv_timer_reset(g_icon_fill_timer);',
]) assert.ok(source.includes(marker), 'tile_icon_source: ' + marker);
// The hook itself never retints directly unless the pending list is full.
const hook = source.slice(source.indexOf('void on_icon_color(lv_obj_t* disc) {'));
assert.ok(hook.indexOf('apply_icon_fill_now(card);') > hook.indexOf('if (!queued) {'), 'Direct retint only when the queue is full');
console.log('Light drags defer tile retints');

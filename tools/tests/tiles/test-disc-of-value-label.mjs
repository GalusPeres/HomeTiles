// A half-height tile without title (screensaver temperature tile, user
// 2026-10-10) has its value label right after the corner disc, which wraps the
// icon. disc_of() took any object after a disc for its icon, so the
// screensaver treated the value as an icon: no own text color (red stayed
// white) and hidden together with the icon. Only an empty round disc sits
// behind its icon as a sibling.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const disc = readRepoFile('src/tiles/runtime/tile_icon_disc.h').replace(/\r\n/g, '\n');
assert.match(disc, /return is_disc\(below\) && lv_obj_get_child_count\(below\) == 0 \? below : nullptr;/);
// The wrapping disc holds the icon and is moved to the background, so the next
// sibling of a title-less compact tile is its value.
const compact = readRepoFile('src/tiles/runtime/compact_sensor_layout.h');
assert.match(compact, /if \(lv_obj_t\* disc = tile_icon_disc::wrap\(card, icon\)\) \{[\s\S]*?lv_obj_move_background\(disc\);/);
assert.match(disc, /lv_obj_set_parent\(icon, disc\);/);
// The screensaver decides icon or text through disc_of().
const panel = readRepoFile('src/ui/screensaver/image_screensaver.cpp');
assert.match(panel, /bool is_icon_label\(lv_obj_t\* label\) \{\n\s*if \(tile_icon_disc::disc_of\(label\)\) return true;/);
console.log('disc_of: a value label after a wrapping disc is text, not an icon');

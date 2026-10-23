// The speakers tab shows how many players play together (user 2026-10-23:
// the menu of 2026-10-22 lost the group button's number of b494): the same
// badge on the tab, from two joined players on, also when a report changes
// the group while the menu is open.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const popup = readRepoFile('src/ui/popups/media/media_popup.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(popup).filter(f => f.name === name);
  assert.equal(found.length, 1, name);
  return found[0].source;
};

const menu = fn('build_menu');
assert.match(menu, /if \(view != MediaView::Group\) continue;/);
assert.match(menu, /ctx->group_badge = add_text\(tab, popup_layout::font20\(\), c\.card, ""\);/);
assert.match(menu, /lv_obj_set_style_bg_color\(ctx->group_badge, lv_color_hex\(c\.control_icon\), 0\);/);
assert.match(menu, /lv_obj_set_size\(ctx->group_badge, badge, badge\);/);
assert.match(menu, /lv_obj_align\(ctx->group_badge, LV_ALIGN_TOP_RIGHT, 0, 0\);/);

const badge = fn('show_group_badge');
assert.match(badge, /const size_t members = extras_of\(ctx->entity_id\)\.members\.size\(\);\s*set_shown\(ctx->group_badge, members >= 2\);/);
assert.match(badge, /if \(strcmp\(lv_label_get_text\(ctx->group_badge\), count\) != 0\) lv_label_set_text\(ctx->group_badge, count\);/,
  'an unchanged number draws nothing');
assert.match(fn('show_menu_head'), /show_group_badge\(ctx\);/);
assert.match(fn('apply_init_to_context'), /if \(ctx->view != MediaView::Player\) show_group_badge\(ctx\);/);
assert.match(fn('delete_menu'), /ctx->menu_back = ctx->group_badge = nullptr;/);
console.log('Media popup group badge: the speakers tab shows the number of joined players');

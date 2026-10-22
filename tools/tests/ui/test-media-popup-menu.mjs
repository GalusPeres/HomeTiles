// The Media popup's menu (user 2026-10-22, the live mockup
// build/design-mockups/media-popup-simple, approved "jetzt eigentlich ganz
// gut"): no buttons in the head; the volume row's mute icon, bar and number
// symmetric around play, moved in so the menu button (three dots) has room in
// the bottom right corner; the menu opens from it as a box inside the popup
// in the card's color with the popup's shadow, its edge halfway in the edge
// L and its radius concentric with the card's; a head row as high as a
// button (the library's back button in its corner, the list's name over the
// rows' texts), a hairline, five rows, and the switch (speakers, source,
// library) in the volume row; a tap outside closes it; the scroll frames
// redraw only the list (one square opaque area).
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const popup = readRepoFile('src/ui/popups/media/media_popup.cpp');
const layout = readRepoFile('src/ui/popups/media/media_popup_layout.h');
const fn = name => {
  const found = cppFunctionDefinitions(popup).filter(f => f.name === name);
  assert.equal(found.length, 1, name);
  return found[0].source;
};

// The head: the player's icon, name and state, nothing else.
assert.ok(!/head_button|source_button|library_button|back_button/.test(popup), 'no buttons in the head');
const head = fn('show_head');
assert.match(head, /hometiles_title::set\(ctx->title_label, ctx->title\.c_str\(\)\);/);
assert.ok(!/view_icon|MediaView::Player/.test(head), 'the open menu does not change the head');

// The volume row.
assert.match(layout, /constexpr int kVolumeInward = popup_layout::scale\(28\);/);
assert.match(layout, /constexpr int kMenuRows = 5;/);
const player = fn('build_player');
assert.match(player, /const int mute_x = cx\(0\) \+ ml::kVolumeInward;\s*const int value_x = cx\(4\) - ml::kVolumeInward;/);
assert.match(player, /const int volume_x = mute_x \+ ml::kButton \+ g;\s*make_slider\(ctx->volume_slider, ctx->player, c, volume_x, [^;]*value_x - g - volume_x,/);
assert.match(player, /lv_obj_set_width\(ctx->volume_value_label, ml::kButton\);\s*lv_obj_set_style_text_align\(ctx->volume_value_label, LV_TEXT_ALIGN_CENTER, 0\);\s*lv_obj_set_pos\(ctx->volume_value_label, value_x,/);
assert.match(player, /ctx->menu_button = round_button\(ctx->root, c, ml::kButton, "dots-horizontal", c\.control_icon, nullptr\);\s*lv_obj_set_pos\(ctx->menu_button, W - L - ml::kButton, ml::kVolumeTop\);/);
assert.ok(!/speaker-multiple", MediaAction::Group/.test(player), 'grouping moved into the menu');

// The menu.
const menu = fn('rebuild_list');
assert.match(menu, /const int box_offset = L \/ 2;\s*const int box_pad = L - box_offset;\s*const int head_height = ml::kButton;\s*const int list_height = ml::kMenuRows \* kListRowHeight;/);
assert.match(menu, /const int top = ml::kVolumeTop - g - list_height - g - head_height - box_pad;/);
assert.match(menu, /lv_obj_set_pos\(ctx->list_box, box_offset, top\);\s*lv_obj_set_size\(ctx->list_box, W - 2 \* box_offset, H - box_offset - top\);/);
assert.match(menu, /lv_obj_set_style_radius\(ctx->list_box, std::max<int32_t>\(0, card_radius - box_offset\), 0\);/, 'concentric with the card');
assert.match(menu, /lv_obj_set_style_bg_color\(ctx->list_box, lv_color_hex\(c\.card\), 0\);/);
assert.match(menu, /popup_layout::apply_card_shadow\(ctx->list_box\);/, 'the popups\' shadow rule (shadow cache)');
assert.match(menu, /if \(ctx->view == MediaView::Library && media_browse_can_go_back\(\)\) \{\s*lv_obj_t\* back = round_button\(ctx->list_box, c, ml::kButton, "arrow-left", c\.control_icon, nullptr\);\s*lv_obj_set_pos\(back, box_pad, box_pad\);/);
assert.match(menu, /lv_obj_t\* title = add_text\(ctx->list_box, settings_style::row_font\(\), c\.text, ""\);/);
assert.match(menu, /lv_obj_set_pos\(rule, box_pad, box_pad \+ head_height \+ g \/ 2\);\s*lv_obj_set_size\(rule, content_width, 1\);/);
assert.match(menu, /lv_obj_set_style_bg_opa\(rule, settings_style::kSeparatorOpa, 0\);/);
assert.match(menu, /lv_obj_set_style_pad_right\(ctx->list, bar_width \+ settings_style::kOptionInset, LV_PART_MAIN\);/, 'rows end before the scrollbar');
assert.match(menu, /lv_obj_set_pos\(tab, cx\(static_cast<int>\(k\) \+ 1\) - box_offset, ml::kVolumeTop - top\);/);
assert.match(menu, /if \(view == ctx->view\) \{\s*popup_nav_style::set_bg\(tab, c\.control, c\.control_opa, LV_PART_MAIN\);/, 'the shown list like shuffle on');
assert.match(menu, /if \(ctx->menu_button\) lv_obj_move_foreground\(ctx->menu_button\);/);
assert.match(popup, /constexpr MediaView kMenuViews\[\] = \{MediaView::Group, MediaView::Source, MediaView::Library\};/);
// A tap outside the box closes it; the box keeps its own taps.
assert.match(menu, /ctx->list_shield = plain\(ctx->root\);\s*lv_obj_set_size\(ctx->list_shield, W, H\);\s*lv_obj_add_flag\(ctx->list_shield, LV_OBJ_FLAG_CLICKABLE\);/);
assert.match(fn('on_menu_shield'), /open_view\(ctx, MediaView::Player\);/);
// The menu button toggles, opening at the last list the player offers.
const toggle = fn('toggle_menu');
assert.match(toggle, /if \(ctx->view != MediaView::Player\) \{\s*open_view\(ctx, MediaView::Player\);/);
assert.match(toggle, /menu_view_offered\(ctx, ctx->last_menu_view\) \? ctx->last_menu_view : MediaView::Player/);
assert.match(fn('on_media_command'), /if \(action == MediaAction::Menu\) \{\s*toggle_menu\(ctx\);/);
// The player stays under the open menu (not hidden).
assert.ok(!/set_shown\(ctx->player/.test(popup), 'the player is never hidden');
console.log('Media popup menu: free head, symmetric volume row, menu button, menu box, switch, outside tap');

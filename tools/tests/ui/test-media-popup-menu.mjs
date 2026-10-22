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
// redraw only the list (one square opaque area). Built once with the player
// and only shown, with the big popup's shadow; the library keeps its level
// while the popup is open.
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

// The menu: built once with the player (user 2026-10-22: it opened slowly,
// the player popup is built once too), then only shown and hidden.
const geometry = fn('menu_geometry');
assert.match(geometry, /m\.box_offset = L \/ 2;\s*m\.box_pad = L - m\.box_offset;\s*m\.head_height = ml::kButton;\s*m\.list_height = ml::kMenuRows \* kListRowHeight;/);
assert.match(geometry, /m\.top = ml::kVolumeTop - g - m\.list_height - g - m\.head_height - m\.box_pad;/);
assert.match(player, /delete_menu\(ctx\);[\s\S]*build_menu\(ctx\);\s*\/\/ The menu button[\s\S]*ctx->menu_button = round_button\(ctx->root,/, 'the menu under its button, built with the player');
const menu = fn('build_menu');
assert.match(menu, /lv_obj_set_pos\(ctx->list_box, m\.box_offset, m\.top\);\s*lv_obj_set_size\(ctx->list_box, box_w, box_h\);/);
assert.match(menu, /const int32_t box_radius = std::max<int32_t>\(0, card_radius - m\.box_offset\);/, 'concentric with the card');
assert.match(menu, /lv_obj_set_style_radius\(ctx->list_box, box_radius, 0\);/);
assert.match(menu, /lv_obj_set_style_bg_color\(ctx->list_box, lv_color_hex\(c\.card\), 0\);/);
// The big popup's shadow rule at the box's concentric radius (user
// 2026-10-22), as pictures: LVGL caches one shadow corner (radius and
// width), and the box's own shadow made every 28-line band compute the
// card's and the box's corner again (V2 b524: opening frames 157-180 ms);
// a layer with the card's radius was fast but its corners did not follow
// the box (b525). LVGL draws the box's shadow once into an alpha snapshot,
// kept as four corners and four edges drawn in black under the box.
const parts = fn('menu_shadow_parts');
assert.match(parts, /if \(s\.part\[0\] && s\.width == w && s\.height == h && s\.radius == radius && s\.blur == blur\) return &s;/, 'made once');
assert.match(parts, /lv_obj_set_style_radius\(probe, radius, 0\);\s*popup_layout::apply_card_shadow\(probe\);\s*lv_obj_set_style_shadow_width\(probe, blur, 0\);/);
assert.match(parts, /lv_draw_buf_t\* snap = lv_snapshot_take\(probe, LV_COLOR_FORMAT_A8\);\s*lv_obj_delete\(probe\);/);
assert.match(parts, /lv_draw_buf_destroy\(snap\);/);
assert.match(menu, /menu_shadow_parts\(ctx->root, box_w, box_h, box_radius,\s*popup_layout::shadow_width_for\(card_radius\)\)/);
assert.match(menu, /lv_obj_t\* piece = lv_image_create\(ctx->menu_shadow\);\s*lv_image_set_src\(piece, parts->part\[i\]\);\s*lv_obj_set_pos\(piece, parts->area\[i\]\.x1, parts->area\[i\]\.y1\);\s*lv_obj_set_style_image_recolor\(piece, lv_color_black\(\), 0\);/);
assert.ok(!/apply_card_shadow\(ctx->list_box\)|shadow_width\(ctx->list_box/.test(popup), 'no shadow of its own on the box');
assert.ok(menu.indexOf('ctx->menu_shadow = plain') < menu.indexOf('ctx->list_box = plain'), 'the shadow layer under the box');
assert.match(fn('rebuild_list'), /set_shown\(ctx->menu_shadow, true\);\s*set_shown\(ctx->list_box, true\);/);
assert.match(fn('rebuild_list'), /set_shown\(ctx->list_box, false\);\s*set_shown\(ctx->menu_shadow, false\);/);
assert.match(menu, /const int32_t card_radius = lv_obj_get_style_radius\(ctx->card, LV_PART_MAIN\);/);
assert.match(menu, /lv_obj_add_flag\(ctx->list_box, LV_OBJ_FLAG_HIDDEN\);/, 'built hidden');
assert.match(menu, /ctx->menu_back = round_button\(ctx->list_box, c, ml::kButton, "arrow-left", c\.control_icon, nullptr\);\s*lv_obj_set_pos\(ctx->menu_back, m\.box_pad, m\.box_pad\);/);
assert.match(menu, /ctx->menu_title = add_text\(ctx->list_box, settings_style::row_font\(\), c\.text, ""\);/);
assert.match(menu, /lv_obj_set_pos\(rule, m\.box_pad, m\.box_pad \+ m\.head_height \+ g \/ 2\);\s*lv_obj_set_size\(rule, m\.content_width, 1\);/);
assert.match(menu, /lv_obj_set_style_bg_opa\(rule, settings_style::kSeparatorOpa, 0\);/);
assert.match(menu, /lv_obj_set_pos\(tab, cx\(static_cast<int>\(k\) \+ 1\) - m\.box_offset, ml::kVolumeTop - m\.top\);/);
assert.match(popup, /constexpr MediaView kMenuViews\[\] = \{MediaView::Group, MediaView::Source, MediaView::Library\};/);
// The open list's head and switch: the back button only in a library
// level, only the offered lists, the shown one like shuffle on.
const head2 = fn('show_menu_head');
assert.match(head2, /const bool back = ctx->view == MediaView::Library && media_browse_can_go_back\(\);\s*set_shown\(ctx->menu_back, back\);/);
assert.match(head2, /set_shown\(tab, menu_view_offered\(ctx, kMenuViews\[k\]\)\);/);
assert.match(head2, /popup_nav_style::set_bg\(tab, c\.control, on \? c\.control_opa : LV_OPA_TRANSP, LV_PART_MAIN\);/);
assert.match(head2, /if \(ctx->menu_button\) lv_obj_move_foreground\(ctx->menu_button\);/);
// Opening shows the menu; the rows are built again only when what they
// show changed.
const show = fn('rebuild_list');
assert.match(show, /if \(ctx->view == MediaView::Player\) \{\s*set_shown\(ctx->list_box, false\);\s*set_shown\(ctx->menu_shadow, false\);\s*lv_obj_remove_flag\(ctx->list_shield, LV_OBJ_FLAG_CLICKABLE\);\s*return;/);
assert.match(show, /const uint32_t rows = menu_rows_of\(ctx\);\s*if \(ctx->list && ctx->menu_rows_key == rows\) \{\s*show_library_status\(ctx\);\s*return;/);
assert.match(show, /lv_obj_set_style_pad_right\(ctx->list, bar_width \+ settings_style::kOptionInset, LV_PART_MAIN\);/, 'rows end before the scrollbar');
assert.match(show, /ctx->menu_rows_key = rows;/);
assert.match(fn('menu_rows_of'), /key \^= media_browse_items_version\(\) \* 2654435761u;[\s\S]*key \^= list_signature\(ctx\);/);
// A row's own click can build the rows again: they go after the event.
assert.match(fn('delete_list'), /drop_later\(ctx, ctx->list\);/);
// A tap outside the box closes it; the box keeps its own taps. The layer
// stays shown and only takes taps while the menu is open (showing it
// redrew the whole card, 640000 px on V2).
assert.match(menu, /ctx->list_shield = plain\(ctx->root\);\s*lv_obj_set_size\(ctx->list_shield, W, H\);\s*lv_obj_add_event_cb\(ctx->list_shield, on_menu_shield, LV_EVENT_CLICKED, nullptr\);/);
assert.ok(!/set_shown\(ctx->list_shield/.test(popup), 'the outside layer is never shown or hidden');
assert.match(show, /lv_obj_add_flag\(ctx->list_shield, LV_OBJ_FLAG_CLICKABLE\);\s*set_shown\(ctx->menu_shadow, true\);\s*set_shown\(ctx->list_box, true\);/);
assert.match(fn('on_menu_shield'), /open_view\(ctx, MediaView::Player\);/);
// The library keeps its level while the popup is open (user 2026-10-22:
// "Laden" on every opening); closing the popup starts it at its root.
const open = fn('open_view');
assert.ok(!/media_browse_reset/.test(open), 'switching lists keeps the library');
assert.match(open, /if \(view == MediaView::Library && before != MediaView::Library &&\s*\(!media_browse_current_level\(\) \|\| !media_browse_entity\(\)\.equalsIgnoreCase\(ctx->entity_id\)\)\) \{\s*media_browse_start\(ctx->entity_id\.c_str\(\)\);/);
const close = fn('close_media_popup');
assert.match(close, /media_browse_reset\(\);\s*if \(ctx->library_status\) delete_list\(ctx\);/);
assert.match(close, /ctx->view = MediaView::Player;\s*rebuild_list\(ctx, false\);/);
// The menu button toggles, opening at the last list the player offers.
const toggle = fn('toggle_menu');
assert.match(toggle, /if \(ctx->view != MediaView::Player\) \{\s*open_view\(ctx, MediaView::Player\);/);
assert.match(toggle, /menu_view_offered\(ctx, ctx->last_menu_view\) \? ctx->last_menu_view : MediaView::Player/);
assert.match(fn('on_media_command'), /if \(action == MediaAction::Menu\) \{\s*toggle_menu\(ctx\);/);
// The player stays under the open menu (not hidden).
assert.ok(!/set_shown\(ctx->player/.test(popup), 'the player is never hidden');
console.log("Media popup menu: free head, symmetric volume row, menu button, menu built once, the big popup's shadow, library level kept");

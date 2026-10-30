#include "src/ui/shared/ui_surface_style.h"
#include "src/types/vacuum/vacuum_tile.h"

#include <algorithm>
#include <cstring>

#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/level_bar.h"
#include "src/tiles/runtime/tile_header.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_rule_icon.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/climate/layout.h"
#include "src/types/vacuum/vacuum_control.h"
#include "src/types/vacuum/vacuum_map.h"
#include "src/types/vacuum/vacuum_visual.h"
#include "src/ui/popups/vacuum/vacuum_popup.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_theme.h"

namespace {

using vacuum_detail::Detail;

enum class Layout : uint8_t { Compact, Pill, Row, Map };

// The card's content box starts at its paddings (the Sensor paddings).
int pad_x() { return tile_layout::scale_480(20); }
int pad_y() { return tile_layout::scale_480(24); }

struct View {
  View* next = nullptr;
  GridType grid = GridType::TAB0;
  String entity;
  String title;
  // The configured icon; "" shows Home Assistant's icon of the entity (the
  // Web Admin's "Automatic"), else the state icon.
  String icon_name;
  bool icon_visible = true;
  bool map_centered = false;
  // The map chosen in the Web Admin ("" takes the Bridge's, detail.map).
  String map_entity;
  Layout layout = Layout::Pill;
  lv_obj_t* card = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* state_label = nullptr;
  const lv_font_t* state_font = nullptr;
  int16_t state_width = 0;
  int16_t state_center = -1;
  String shown_icon;
  // Controls (none at half height). The main button is a resting surface in
  // the card's control fill (like the Climate target pill and the level
  // bar) holding a control that presses one step above it.
  lv_obj_t* main_box = nullptr;
  lv_obj_t* main = nullptr;
  lv_obj_t* main_glyph = nullptr;
  lv_obj_t* main_text = nullptr;
  lv_obj_t* dock = nullptr;
  lv_obj_t* stop = nullptr;
  lv_obj_t* map = nullptr;
  int16_t main_center = 0;  // Row: the main button's centre, the side buttons beside it
  int16_t row_top = 0;
  int16_t button = 0;
  int16_t gap = 0;
  bool pauses = false;
  bool interactive = false;
};

View* g_views = nullptr;

void unregister(View* view) {
  for (View** link = &g_views; *link; link = &(*link)->next) {
    if (*link == view) {
      *link = view->next;
      return;
    }
  }
}

VacuumPopupTarget popup_target(const View* view) {
  VacuumPopupTarget target;
  target.entity = view->entity;
  target.title = view->title;
  target.icon_name = view->icon_name;
  target.icon_visible = view->icon_visible;
  target.map_centered = view->map_centered;
  target.map_entity = view->map_entity;
  target.source = view->card;
  return target;
}

void plain_button(lv_obj_t* btn) {
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
  disable_pressed_button_animation(btn);
}

// A round side button: the icon in the text color, the control circle only
// while pressed (tile_icon_source marks controls).
lv_obj_t* side_button(lv_obj_t* card, const char* icon, int size) {
  lv_obj_t* btn = lv_button_create(card);
  tile_icon_disc::mark_control(btn);
  lv_obj_set_size(btn, size, size);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  plain_button(btn);
  if (FONT_MDI_ICONS) {
    lv_obj_t* glyph = lv_label_create(btn);
    set_label_style(glyph, lv_color_hex(ui_theme::text()), FONT_MDI_ICONS);
    lv_label_set_text(glyph, getMdiChar(icon).c_str());
    lv_obj_center(glyph);
  }
  return btn;
}

// The main button: a surface in the control fill with the level bar's radius
// (concentric with the card's corners), glyph and text in the theme's text
// color; `width` 0 sizes it to its content.
void main_button(View* view, int width, int height, bool with_text) {
  lv_obj_t* box = lv_obj_create(view->card);
  lv_obj_remove_style_all(box);
  tile_icon_disc::mark_surface(box);
  ui_surface_style::apply_radius(box, climate_layout::kControlRadius, 0);
  lv_obj_remove_flag(box, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_t* btn = lv_button_create(box);
  tile_icon_disc::mark_control(btn);
  ui_surface_style::apply_radius(btn, climate_layout::kControlRadius, 0);
  plain_button(btn);
  if (width > 0) {
    lv_obj_set_size(box, width, height);
    lv_obj_set_size(btn, LV_PCT(100), LV_PCT(100));
  } else {
    lv_obj_set_size(box, LV_SIZE_CONTENT, height);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, height);
    lv_obj_set_style_pad_left(btn, tile_layout::scale_480(12), 0);
    lv_obj_set_style_pad_right(btn, tile_layout::scale_480(16), 0);
  }
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btn, tile_layout::scale_480(4), 0);
  if (FONT_MDI_ICONS) {
    view->main_glyph = lv_label_create(btn);
    set_label_style(view->main_glyph, lv_color_hex(ui_theme::text()), FONT_MDI_ICONS);
    lv_label_set_text(view->main_glyph, getMdiChar("play").c_str());
  }
  if (with_text) {
    view->main_text = lv_label_create(btn);
    set_label_style(view->main_text, lv_color_hex(ui_theme::text()), FONT_TITLE);
    lv_label_set_long_mode(view->main_text, LV_LABEL_LONG_DOT);
    lv_label_set_text(view->main_text, "");
  }
  view->main_box = box;
  view->main = btn;
}

// Row: Dock and Stop sit one gap beside the main button, whose width follows
// its text (the mockup's 2x1 tile).
void place_row(View* view) {
  if (view->layout != Layout::Row || !view->main_box) return;
  lv_obj_update_layout(view->main_box);
  const int width = lv_obj_get_width(view->main_box);
  const int left = view->main_center - width / 2;
  lv_obj_set_x(view->main_box, left);
  if (view->dock) lv_obj_set_pos(view->dock, left - view->gap - view->button, view->row_top);
  if (view->stop) lv_obj_set_pos(view->stop, left + width + view->gap, view->row_top);
}

void set_enabled(lv_obj_t* obj, lv_obj_t* faded, bool enabled) {
  if (!obj) return;
  if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
  else lv_obj_add_state(obj, LV_STATE_DISABLED);
  lv_obj_set_style_opa(faded ? faded : obj, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
}

void show(View* view) {
  if (!view) return;
  const ui_theme::SurfaceScope surface = tile_surface::scope(view->card);
  const Detail& d = vacuum_control::detail(view->entity);
  const vacuum_visual::Visual v = vacuum_visual::visual(d);
  if (view->icon) {
    if (!view->icon_name.length()) {
      // Home Assistant's icon of the entity, as the Web Admin shows it.
      const String ha_icon = haBridgeConfig.findEntityIcon(view->entity);
      const String icon = vacuum_visual::icon_name(ha_icon, v);
      if (icon != view->shown_icon) {
        view->shown_icon = icon;
        tile_rule_icon::set_glyph(view->icon, getMdiChar(icon).c_str());
      }
    }
    tile_icon_disc::set_icon_color(view->icon, lv_color_hex(v.color));
  }
  tile_header::set_state(view->state_label, v.label.c_str(),
                         view->layout == Layout::Compact ? nullptr : view->state_font, view->state_width,
                         view->state_center);
  if (view->layout == Layout::Compact) return;

  const bool ready = d.valid && d.available;
  view->pauses = vacuum_detail::main_pauses(d);
  if (view->main_glyph) lv_label_set_text(view->main_glyph, getMdiChar(view->pauses ? "pause" : "play").c_str());
  if (view->main_text) {
    lv_label_set_text(view->main_text,
                      vacuum_visual::text(view->pauses ? i18n::VacuumLabel::Pause : i18n::VacuumLabel::Clean));
  }
  set_enabled(view->main, view->main_box, ready && (view->pauses || vacuum_detail::can_start(d)));
  set_enabled(view->dock, nullptr, ready && (d.features & vacuum_detail::kReturnHome));
  set_enabled(view->stop, nullptr, ready && (d.features & vacuum_detail::kStop));
  // The map: on the tile's square, and the popup's ahead of its opening.
  const char* map = view->map_entity.length() ? view->map_entity.c_str() : d.map;
  if (view->map) {
    vacuum_map::set_entity(view->map, map, vacuum_detail::moving(d));
    vacuum_map::show_now(view->map);
  }
  if (view->interactive) {
    vacuum_map::want(view, view->card, map, static_cast<uint16_t>(vacuum_popup_map_edge(view->map_centered)),
                     vacuum_detail::moving(d));
  }
  place_row(view);
  // The icon color steers the controls' fill (Circle in icon color).
  tile_icon_source::refresh_controls(view->card);
}

void on_button(lv_event_t* e) {
  View* view = static_cast<View*>(lv_event_get_user_data(e));
  if (!view || lv_event_get_code(e) != LV_EVENT_CLICKED || !view->entity.length()) return;
  lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  const Detail& d = vacuum_control::detail(view->entity);
  if (!d.valid || !d.available) return;
  if (btn == view->main) {
    vacuum_control::send(view->entity, vacuum_detail::main_pauses(d) ? "pause" : "start");
  } else if (btn == view->dock) {
    vacuum_control::send(view->entity, "return_to_base");
  } else if (btn == view->stop) {
    vacuum_control::send(view->entity, "stop");
  }
}

void on_card_event(lv_event_t* e) {
  View* view = static_cast<View*>(lv_event_get_user_data(e));
  if (!view) return;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_DELETE) {
    vacuum_map::unwant(view);
    unregister(view);
    delete view;
    return;
  }
  if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
  if (!view->entity.length()) return;
  const VacuumPopupTarget target = popup_target(view);
  finish_press_before_popup(e);
  show_vacuum_popup(target);
}

void wire(View* view, lv_obj_t* btn, bool interactive) {
  if (!btn) return;
  if (interactive) {
    lv_obj_add_event_cb(btn, on_button, LV_EVENT_CLICKED, view);
  } else {
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  }
}

}  // namespace

void vacuum_tiles_refresh(const String& entity) {
  const bool all = entity == "*";
  for (View* view = g_views; view; view = view->next) {
    if (all || view->entity == entity) show(view);
  }
}

lv_obj_t* render_vacuum_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index,
                             GridType grid_type) {
  lv_obj_t* card = lv_button_create(parent);
  const uint32_t color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  const uint32_t pressed_color = tone_color::pressed_card(color);
  lv_obj_set_style_bg_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_hor(card, pad_x(), 0);
  lv_obj_set_style_pad_ver(card, pad_y(), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);
  place_tile_card(card, col, row, tile);

  View* view = new View();
  view->grid = grid_type;
  view->entity = tile.sensor_entity;
  view->card = card;
  view->title = tile.title.length() ? tile.title : haBridgeConfig.findSensorName(tile.sensor_entity);
  if (!view->title.length()) view->title = tile.sensor_entity;
  view->icon_visible = !isMdiIconDisabled(tile.icon_name);
  view->icon_name = view->icon_visible ? normalizeMdiIconName(tile.icon_name) : String();
  view->map_centered = tile.sensor_display_mode == 1;
  view->map_entity = tile.flow_entities;
  const bool compact = tile_geometry::compact_device_control(tile.type, tile.span_w, tile.span_h);
  view->layout = compact                                  ? Layout::Compact
                 : tile.span_w >= 2 && tile.span_h >= 2   ? Layout::Map
                 : tile.span_w >= 2                       ? Layout::Row
                                                          : Layout::Pill;

  if (view->icon_visible && FONT_MDI_ICONS) {
    view->icon = lv_label_create(card);
    set_label_style(view->icon, lv_color_hex(vacuum_visual::kColor), FONT_MDI_ICONS);
    lv_label_set_text(view->icon, getMdiChar(view->icon_name.length() ? view->icon_name : String("robot-vacuum")).c_str());
    lv_obj_align(view->icon, LV_ALIGN_TOP_LEFT, tile_layout::scale_480(-8), tile_layout::scale_480(-8));
  }

  const level_bar::BarBox box = level_bar::box(tile);
  // The Map layout keeps the header beside the disc; the state sits in the
  // header like a one-row tile.
  const bool tall = view->layout == Layout::Pill && tile.span_h > 1.0f;
  const tile_header::Header header = tile_header::create(card, tile, tall, box.top);
  view->state_label = header.state;
  view->state_font = header.state_font;
  view->state_width = header.state_width;
  view->state_center = header.state_center;
  const bool interactive = view->entity.length() && grid_type != GridType::SCREENSAVER;
  view->interactive = interactive;

  if (view->layout == Layout::Compact) {
    compact_sensor_layout::apply(card, view->icon, header.title, header.state, tile);
  } else {
    if (view->icon) tile_icon_disc::add_round(card, view->icon);
    const int tile_w = tile_geometry::extent(tile.col, std::max(1.0f, tile.span_w), GRID_CELL_W, GRID_GAP);
    const int tile_h = tile_geometry::extent(tile.row, std::max(1.0f, tile.span_h), GRID_CELL_H, GRID_GAP);
    const int inset = climate_layout::kOuterInset;
    view->button = static_cast<int16_t>(box.base);
    view->gap = static_cast<int16_t>(climate_layout::kGap);
    if (view->layout == Layout::Map) {
      // The map below the header, centred, as high as the rest of the tile;
      // the side strips keep room for one button each.
      const int icon_width =
          FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0) : 0;
      const int content_top =
          climate_layout::content_top(tile_icon_disc::header_diameter(icon_width), tile_icon_disc::inset());
      int edge = tile_h - inset - content_top;
      edge = std::min(edge, tile_w - 2 * (view->button + 2 * inset));
      if (edge >= 16) {
        const int x = (tile_w - edge) / 2;
        view->map = vacuum_map::create(card, x - pad_x(), tile_h - inset - edge - pad_y(), edge,
                                       climate_layout::kControlRadius);
      }
      const int side = (tile_w - std::max(edge, 0)) / 2;
      const int y = tile_h - inset - view->button - pad_y();
      view->dock = side_button(card, "home-import-outline", view->button);
      lv_obj_set_pos(view->dock, (side - view->button) / 2 - pad_x(), y);
      main_button(view, view->button, view->button, false);
      lv_obj_set_pos(view->main_box, tile_w - side + (side - view->button) / 2 - pad_x(), y);
    } else if (view->layout == Layout::Row) {
      view->row_top = static_cast<int16_t>(box.top + box.height - view->button - pad_y());
      view->main_center = static_cast<int16_t>(tile_w / 2 - pad_x());
      main_button(view, 0, view->button, true);
      lv_obj_set_y(view->main_box, view->row_top);
      if (view->main_text) {
        lv_obj_set_style_max_width(view->main_text, std::max(1, box.width - 3 * view->button - 2 * view->gap), 0);
      }
      view->dock = side_button(card, "home-import-outline", view->button);
      view->stop = side_button(card, "stop", view->button);
    } else {
      // One column: the main button across the box, like the level bar.
      main_button(view, box.width, view->button, true);
      lv_obj_set_pos(view->main_box, inset - pad_x(), box.top + box.height - view->button - pad_y());
      if (view->main_text) lv_obj_set_style_max_width(view->main_text, box.width - view->button, 0);
    }
    wire(view, view->main, interactive);
    wire(view, view->dock, interactive);
    wire(view, view->stop, interactive);
  }

  view->next = g_views;
  g_views = view;
  show(view);

  if (interactive) {
    const lv_event_code_t popup_event =
        getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS ? LV_EVENT_SHORT_CLICKED : LV_EVENT_LONG_PRESSED;
    lv_obj_add_event_cb(card, on_card_event, popup_event, view);
  }
  lv_obj_add_event_cb(card, on_card_event, LV_EVENT_DELETE, view);
  (void)index;
  return card;
}

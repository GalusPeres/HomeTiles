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
#include "src/types/media/tile_layout.h"
#include "src/types/vacuum/vacuum_control.h"
#include "src/types/vacuum/vacuum_map.h"
#include "src/types/vacuum/vacuum_visual.h"
#include "src/ui/popups/vacuum/vacuum_popup.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_theme.h"
#include "src/ui/shared/value_unit.h"

namespace {

using vacuum_detail::Detail;

// Compact: half height. Row: the header and the row of buttons. Map: two
// columns and two rows, the Media tile's arrangement.
enum class Layout : uint8_t { Compact, Row, Map };

// The card's content box starts at its paddings (the Sensor paddings).
int pad_x() { return tile_layout::scale_480(20); }
int pad_y() { return tile_layout::scale_480(24); }

// One button of the row: a resting surface in the card's control fill with
// the level bar's radius (concentric with the card's corners) holding a
// control that presses one step above it, glyph and text in the theme's
// text color.
struct Button {
  lv_obj_t* box = nullptr;
  lv_obj_t* btn = nullptr;
  lv_obj_t* glyph = nullptr;
  lv_obj_t* text = nullptr;
};

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
  // The map chosen in the Web Admin ("" takes the Bridge's, detail.map) and
  // a camera map's interval (Tile::image_slideshow_sec, 3..60 s).
  String map_entity;
  uint8_t map_every = 5;
  Layout layout = Layout::Row;
  lv_obj_t* card = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* state_label = nullptr;
  const lv_font_t* state_font = nullptr;
  int16_t state_width = 0;
  int16_t state_center = -1;
  // Map: the values line below the state, the two lines centred on the map.
  lv_obj_t* values_label = nullptr;
  int16_t text_center = 0;
  String shown_icon;
  // The row (none at half height): Clean (Pause while cleaning), from two
  // columns Dock, from three Stop between them.
  Button main;
  Button stop;
  Button dock;
  lv_obj_t* map = nullptr;
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
  target.map_every = view->map_every;
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

// A row button at `x`/`y` in the card's content box, `width` x `height`;
// the text gives way (dots) before it would leave the button.
Button row_button(lv_obj_t* card, int x, int y, int width, int height, const char* icon, const char* label) {
  Button b;
  b.box = lv_obj_create(card);
  lv_obj_remove_style_all(b.box);
  tile_icon_disc::mark_surface(b.box);
  ui_surface_style::apply_radius(b.box, climate_layout::kControlRadius, 0);
  lv_obj_remove_flag(b.box, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_pos(b.box, x, y);
  lv_obj_set_size(b.box, width, height);
  b.btn = lv_button_create(b.box);
  tile_icon_disc::mark_control(b.btn);
  ui_surface_style::apply_radius(b.btn, climate_layout::kControlRadius, 0);
  plain_button(b.btn);
  lv_obj_set_size(b.btn, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(b.btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b.btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  const int gap = tile_layout::scale_480(4);
  lv_obj_set_style_pad_column(b.btn, gap, 0);
  int glyph_w = 0;
  if (FONT_MDI_ICONS) {
    b.glyph = lv_label_create(b.btn);
    set_label_style(b.glyph, lv_color_hex(ui_theme::text()), FONT_MDI_ICONS);
    lv_label_set_text(b.glyph, getMdiChar(icon).c_str());
    glyph_w = lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0);
  }
  b.text = lv_label_create(b.btn);
  set_label_style(b.text, lv_color_hex(ui_theme::text()), FONT_TITLE);
  lv_label_set_long_mode(b.text, LV_LABEL_LONG_DOT);
  lv_label_set_text(b.text, label ? label : "");
  lv_obj_set_style_max_width(b.text, std::max<int>(1, width - glyph_w - gap - 2 * tile_layout::scale_480(8)), 0);
  return b;
}

void set_enabled(const Button& b, bool enabled) {
  if (!b.btn) return;
  if (enabled) lv_obj_remove_state(b.btn, LV_STATE_DISABLED);
  else lv_obj_add_state(b.btn, LV_STATE_DISABLED);
  lv_obj_set_style_opa(b.box, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
}

// Map: the state and the values line as one block centred on the map; the
// state alone when there are no values.
void place_texts(View* view) {
  if (view->layout != Layout::Map || !view->state_label) return;
  const bool values = view->values_label && !lv_obj_has_flag(view->values_label, LV_OBJ_FLAG_HIDDEN);
  const int state_h = lv_font_get_line_height(lv_obj_get_style_text_font(view->state_label, LV_PART_MAIN));
  const int values_h =
      values ? lv_font_get_line_height(lv_obj_get_style_text_font(view->values_label, LV_PART_MAIN)) : 0;
  const int top = view->text_center - (state_h + values_h) / 2;
  lv_obj_set_y(view->state_label, top);
  if (values) lv_obj_set_y(view->values_label, top + state_h);
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
  if (view->layout == Layout::Map) {
    tile_header::set_state(view->state_label, v.state.c_str(), view->state_font, view->state_width, -1);
    const String values = vacuum_visual::values_line(d);
    if (view->values_label) {
      if (values.length()) {
        value_unit::show_text(view->values_label, values.c_str());
        lv_obj_remove_flag(view->values_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(view->values_label, LV_OBJ_FLAG_HIDDEN);
      }
    }
    place_texts(view);
  } else {
    tile_header::set_state(view->state_label, v.label.c_str(),
                           view->layout == Layout::Compact ? nullptr : view->state_font, view->state_width,
                           view->state_center);
  }
  if (view->layout == Layout::Compact) return;

  const bool ready = d.valid && d.available;
  view->pauses = vacuum_detail::main_pauses(d);
  if (view->main.glyph) lv_label_set_text(view->main.glyph, getMdiChar(view->pauses ? "pause" : "play").c_str());
  if (view->main.text) {
    lv_label_set_text(view->main.text,
                      vacuum_visual::text(view->pauses ? i18n::VacuumLabel::Pause : i18n::VacuumLabel::Clean));
  }
  set_enabled(view->main, ready && (view->pauses || vacuum_detail::can_start(d)));
  set_enabled(view->stop, ready && (d.features & vacuum_detail::kStop));
  set_enabled(view->dock, ready && (d.features & vacuum_detail::kReturnHome));
  // The map: the popup's picture wished first (decoded ahead), so the
  // tile's square shows its copy in its first frame.
  const char* map = view->map_entity.length() ? view->map_entity.c_str() : d.map;
  if (view->interactive) {
    vacuum_map::want(view, view->card, map, static_cast<uint16_t>(vacuum_popup_source_edge()), view->map_every);
  }
  if (view->map) {
    vacuum_map::set_entity(view->map, map, view->map_every);
    vacuum_map::show_now(view->map);
  }
  // The icon color steers the controls' fill (Circle in icon color).
  tile_icon_source::refresh_controls(view->card);
}

void on_button(lv_event_t* e) {
  View* view = static_cast<View*>(lv_event_get_user_data(e));
  if (!view || lv_event_get_code(e) != LV_EVENT_CLICKED || !view->entity.length()) return;
  lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  const Detail& d = vacuum_control::detail(view->entity);
  if (!d.valid || !d.available) return;
  if (btn == view->main.btn) {
    vacuum_control::send(view->entity, vacuum_detail::main_pauses(d) ? "pause" : "start");
  } else if (btn == view->dock.btn) {
    vacuum_control::send(view->entity, "return_to_base");
  } else if (btn == view->stop.btn) {
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
  view->map_every = vacuum_map::every_of(tile.image_slideshow_sec);
  const bool compact = tile_geometry::compact_device_control(tile.type, tile.span_w, tile.span_h);
  view->layout = compact                                ? Layout::Compact
                 : tile.span_w >= 2 && tile.span_h >= 2 ? Layout::Map
                                                        : Layout::Row;

  if (view->icon_visible && FONT_MDI_ICONS) {
    view->icon = lv_label_create(card);
    set_label_style(view->icon, lv_color_hex(vacuum_visual::kColor), FONT_MDI_ICONS);
    lv_label_set_text(view->icon, getMdiChar(view->icon_name.length() ? view->icon_name : String("robot-vacuum")).c_str());
    lv_obj_align(view->icon, LV_ALIGN_TOP_LEFT, tile_layout::scale_480(-8), tile_layout::scale_480(-8));
  }

  const level_bar::BarBox box = level_bar::box(tile);
  // One row: title and state beside the disc. From 1.5 rows the state
  // centred like the Lock/Alarm/Fan tiles' (device_tile.cpp). Map: the
  // Media tile's title top right, the state beside the map (below).
  const bool tall = view->layout == Layout::Map || tile.span_h > 1.0f;
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
    const int inset = climate_layout::kOuterInset;
    const int gap = climate_layout::kGap;
    const int button = box.base;
    // The row's top in the card: one row high at the card's bottom inset.
    const int row_top = box.top + box.height - button;
    if (view->layout == Layout::Map) {
      // The map in the Media cover's place: one inset in from the card like
      // the row, from the content top to one gap above the row, at most the
      // Media cover's share of the width; state and values right of it.
      const int icon_width =
          FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0) : 0;
      const int content_top =
          climate_layout::content_top(tile_icon_disc::header_diameter(icon_width), tile_icon_disc::inset());
      const int edge = std::max<int>(0, std::min<int>(row_top - gap - content_top, tile_w * 42 / 100));
      if (edge >= 16) {
        view->map = vacuum_map::create(card, inset - pad_x(), content_top - pad_y(), edge,
                                       climate_layout::kControlRadius);
      }
      const int text_x = inset + edge + media_tile::kTextAfterCover;
      const int text_w = std::max<int>(1, tile_w - text_x - 2 * tile_icon_disc::inset());
      view->text_center = static_cast<int16_t>(content_top + edge / 2 - pad_y());
      view->state_center = -1;
      view->state_width = static_cast<int16_t>(text_w);
      if (view->state_label) {
        lv_obj_set_width(view->state_label, text_w);
        lv_obj_set_style_text_align(view->state_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_x(view->state_label, text_x - pad_x());
      }
      view->values_label = lv_label_create(card);
      set_label_style(view->values_label, lv_color_hex(ui_theme::text()),
                      compact_sensor_layout::value_font(tile.sensor_value_font));
      lv_label_set_long_mode(view->values_label, LV_LABEL_LONG_DOT);
      lv_obj_set_width(view->values_label, text_w);
      lv_obj_set_style_text_align(view->values_label, LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_set_x(view->values_label, text_x - pad_x());
      lv_obj_clear_flag(view->values_label, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_flag(view->values_label, LV_OBJ_FLAG_HIDDEN);
    }
    // Equal buttons across the row (user 2026-10-30): one column Clean, two
    // Clean and Dock, from three Clean, Stop and Dock.
    const int count = tile.span_w >= 3 ? 3 : tile.span_w >= 2 ? 2 : 1;
    const int usable = box.width - (count - 1) * gap;
    const int y = row_top - pad_y();
    int x = inset;
    for (int i = 0; i < count; ++i) {
      const int width = usable * (i + 1) / count - usable * i / count;
      Button* slot = i == 0 ? &view->main : (i == count - 1 ? &view->dock : &view->stop);
      const char* icon = slot == &view->main ? "play" : (slot == &view->dock ? "home-import-outline" : "stop");
      const char* label = slot == &view->main   ? vacuum_visual::text(i18n::VacuumLabel::Clean)
                          : slot == &view->dock ? vacuum_visual::text(i18n::VacuumLabel::Dock)
                                                : vacuum_visual::text(i18n::VacuumLabel::Stop);
      *slot = row_button(card, x - pad_x(), y, width, button, icon, label);
      x += width + gap;
    }
    wire(view, view->main.btn, interactive);
    wire(view, view->stop.btn, interactive);
    wire(view, view->dock.btn, interactive);
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

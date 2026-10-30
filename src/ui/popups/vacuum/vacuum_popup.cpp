#include "src/ui/popups/vacuum/vacuum_popup.h"

// lvgl.h no longer exports lv_image_cache_drop() (tile_renderer.cpp).
#include <misc/cache/instance/lv_image_cache.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/devices/device.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/fonts/ui_fonts.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/vacuum/vacuum_control.h"
#include "src/types/vacuum/vacuum_map.h"
#include "src/types/vacuum/vacuum_visual.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/calendar/calendar_popup.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/device/device_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/media/media_popup_layout.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_nav_style.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_surface.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_theme.h"
#include "src/ui/tabs/settings/settings_style.h"

namespace {

namespace ml = media_popup_layout;
using vacuum_detail::Detail;

// The row (user 2026-10-30, mockup): Dock, the main button in the middle,
// Stop, each this far from the middle; the menu button in the corner like
// the Media popup's.
constexpr int kRowStep = popup_layout::scale(190);
constexpr int kListRowHeight = popup_layout::scale(72);
constexpr int kOptionGap = 2;
// The values beside the map: one card edge apart; under the centred map in
// one line, this far apart.
constexpr int kValueLineGap = popup_layout::scale(48);
constexpr int kMenuRows = 5;
// The map's corners: the Media popup's cover radius.
constexpr int kMapRadius = popup_layout::scale480(18);

enum class Tab : uint8_t { None, Speed, Rooms, Locate };
constexpr Tab kTabs[] = {Tab::Speed, Tab::Rooms, Tab::Locate};

struct Colors {
  uint32_t card = 0;
  uint32_t icon = 0;
  uint32_t text = 0;
  uint32_t secondary = 0;
  uint32_t control_icon = 0;
  lv_color_t control = {};
  lv_opa_t control_opa = 0;
  lv_color_t raised = {};
  lv_opa_t raised_opa = 0;
};

struct Value {
  lv_obj_t* box = nullptr;
  lv_obj_t* text = nullptr;
};

struct Popup {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* close = nullptr;
  // The header's state line (show_popup_shell value).
  lv_obj_t* value = nullptr;
  // The whole card in card coordinates (the card's content box is padded).
  lv_obj_t* root = nullptr;
  VacuumPopupTarget target;
  uint32_t card_rgb = popup_surface::default_card();
  uint32_t icon_rgb = device_visual::grey();
  Colors colors;
  bool open = false;
  // The body is built for one layout and colors; a change builds it again.
  bool built = false;
  bool built_centered = false;
  uint32_t built_colors = 0;
  lv_obj_t* map = nullptr;
  Value values[3];  // cleaned area, cleaning time, battery
  lv_obj_t* dock = nullptr;
  lv_obj_t* main = nullptr;
  lv_obj_t* main_glyph = nullptr;
  lv_obj_t* main_text = nullptr;
  lv_obj_t* stop = nullptr;
  lv_obj_t* menu_button = nullptr;
  // The menu (Media's menu box).
  lv_obj_t* shield = nullptr;
  lv_obj_t* box = nullptr;
  // Under the box: its shadow as pictures while the popup is open
  // (build_menu_shadow).
  lv_obj_t* menu_shadow = nullptr;
  lv_draw_buf_t* menu_shadow_parts[8] = {};
  lv_obj_t* menu_title = nullptr;
  lv_obj_t* go = nullptr;
  lv_obj_t* go_text = nullptr;
  lv_obj_t* list = nullptr;
  lv_obj_t* tabs[3] = {};
  Tab tab = Tab::None;
  Tab last_tab = Tab::None;
  uint32_t list_key = 0;
  // The rooms chosen in the Rooms tab (bit per area index), kept per vacuum
  // while the popup is open.
  uint32_t rooms = 0;
} pop;

const char* lang() { return configManager.getConfig().language; }
const char* text(i18n::VacuumLabel label) { return i18n::vacuum_label(lang(), label); }

Colors compute_colors(uint32_t card, uint32_t icon) {
  Colors c;
  c.card = card;
  c.icon = icon;
  c.text = ui_theme::text();
  c.secondary = ui_theme::text_secondary();
  c.control_icon = ui_theme::icon();
  popup_shell_control_fill(card, icon, c.control, c.control_opa);
  popup_shell_control_raised_fill(card, icon, c.raised, c.raised_opa);
  return c;
}

uint32_t colors_key(const Colors& c) {
  return c.card * 2654435761u ^ c.icon ^ (c.text << 1) ^ (static_cast<uint32_t>(c.control_opa) << 24);
}

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return obj;
}

lv_obj_t* add_text(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* value) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_label_set_text(label, value ? value : "");
  return label;
}

lv_obj_t* add_icon(lv_obj_t* parent, const char* name, uint32_t color) {
  lv_obj_t* label = add_text(parent, FONT_MDI_ICONS, color, getMdiChar(name).c_str());
  popup_layout::applyIconScale(label);
  return label;
}

void set_shown(lv_obj_t* obj, bool shown) {
  if (!obj) return;
  if (shown) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void set_enabled(lv_obj_t* obj, bool enabled) {
  if (!obj) return;
  if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
  else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

// A press shows the control fill, like the Media popup's buttons.
void press_fill(lv_obj_t* btn, const Colors& c) {
  popup_nav_style::style_press_fill(btn, c.control, c.control_opa);
  disable_pressed_button_animation(btn);
}

// A filled button (the main button, "Clean N"): the controls' fill at rest
// (popup_shell_control_fill, like the Climate and Light popups' pills), one
// control step up while pressed, glyph and text in the theme's colors.
void filled(lv_obj_t* btn, const Colors& c) {
  popup_nav_style::set_bg(btn, c.control, c.control_opa, LV_PART_MAIN);
  popup_nav_style::set_bg(btn, c.raised, c.raised_opa, LV_PART_MAIN | LV_STATE_PRESSED);
  popup_nav_style::no_press_filter(btn, LV_PART_MAIN | LV_STATE_PRESSED);
  disable_pressed_button_animation(btn);
}

lv_obj_t* round_button(lv_obj_t* parent, int x, int y, const char* icon, const Colors& c) {
  lv_obj_t* btn = plain(parent);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, ml::kButton, ml::kButton);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_opa(btn, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);
  press_fill(btn, c);
  lv_obj_center(add_icon(btn, icon, c.control_icon));
  return btn;
}

// A button with icon and text centred on `center_x` (Dock, Stop): no circle
// at rest, the control fill while pressed.
lv_obj_t* text_button(lv_obj_t* parent, int center_x, int y, const char* icon, const char* label, const Colors& c) {
  lv_obj_t* btn = plain(parent);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_height(btn, ml::kButton);
  lv_obj_set_width(btn, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_opa(btn, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);
  lv_obj_set_style_pad_hor(btn, popup_layout::scale(14), 0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btn, popup_layout::scale(8), 0);
  press_fill(btn, c);
  add_icon(btn, icon, c.control_icon);
  add_text(btn, popup_layout::font20(), c.text, label);
  lv_obj_update_layout(btn);
  lv_obj_set_pos(btn, center_x - lv_obj_get_width(btn) / 2, y);
  return btn;
}

void place_centered(lv_obj_t* btn, int center_x) {
  if (!btn) return;
  lv_obj_update_layout(btn);
  lv_obj_set_x(btn, center_x - lv_obj_get_width(btn) / 2);
}

// ---------------------------------------------------------------------------
// Commands

const Detail& current() { return vacuum_control::detail(pop.target.entity); }

void on_main(lv_event_t*) {
  const Detail& d = current();
  if (!d.valid || !d.available) return;
  vacuum_control::send(pop.target.entity, vacuum_detail::main_pauses(d) ? "pause" : "start");
}

void on_dock(lv_event_t*) { vacuum_control::send(pop.target.entity, "return_to_base"); }
void on_stop(lv_event_t*) { vacuum_control::send(pop.target.entity, "stop"); }

// ---------------------------------------------------------------------------
// Menu

bool tab_offered(const Detail& d, Tab tab) {
  if (!d.valid || !d.available) return false;
  switch (tab) {
    case Tab::Speed: return (d.features & vacuum_detail::kFanSpeed) && d.speed_count > 0;
    case Tab::Rooms: return (d.features & vacuum_detail::kCleanSpot) || ((d.features & vacuum_detail::kCleanArea) && d.area_count);
    case Tab::Locate: return (d.features & vacuum_detail::kLocate) != 0;
    default: return false;
  }
}

const char* tab_icon(Tab tab) {
  return tab == Tab::Speed ? "fan" : tab == Tab::Rooms ? "texture-box" : "map-marker";
}

const char* tab_title(Tab tab) {
  return text(tab == Tab::Speed ? i18n::VacuumLabel::FanSpeed
              : tab == Tab::Rooms ? i18n::VacuumLabel::Rooms
                                  : i18n::VacuumLabel::Locate);
}

struct MenuGeometry {
  int box_offset, box_pad, head_height, list_height, top, content_width;
};

// The Media popup's menu box (media_popup.cpp menu_geometry).
MenuGeometry menu_geometry() {
  const int L = ml::kEdge;
  const int g = Device::kGridGap;
  MenuGeometry m{};
  m.box_offset = L / 2;
  m.box_pad = L - m.box_offset;
  m.head_height = ml::kButton;
  m.list_height = kMenuRows * kListRowHeight;
  m.top = ml::kVolumeTop - g - m.list_height - g - m.head_height - m.box_pad;
  m.content_width = popup_layout::kCardWidth - 2 * L;
  return m;
}

void open_tab(Tab tab);

// The menu box's shadow as pictures, exactly like the Media popup's
// (media_popup.cpp build_menu_shadow): LVGL draws the shadow once around a
// small probe with the box's radius; its four corners and a short piece of
// each side stay as alpha pictures, drawn in black under the box, made when
// the menu first opens and freed with the popup. A real LVGL shadow on the
// box pushed the card's shadow corner out of LVGL's one-entry shadow cache
// (V2 b524: 157-180 ms per opening frame).
constexpr int32_t kShadowSidePiece = 16;  // px along a side, tiled

void free_menu_shadow(bool delete_objects) {
  if (pop.menu_shadow && delete_objects) lv_obj_delete(pop.menu_shadow);
  pop.menu_shadow = nullptr;
  for (lv_draw_buf_t*& part : pop.menu_shadow_parts) {
    if (!part) continue;
    lv_image_cache_drop(part);
    lv_draw_buf_destroy(part);
    part = nullptr;
  }
}

void build_menu_shadow() {
  free_menu_shadow(true);
  if (!pop.box || !pop.root) return;
  const uint32_t started_ms = millis();
  const MenuGeometry m = menu_geometry();
  const int32_t card_radius = lv_obj_get_style_radius(pop.card, LV_PART_MAIN);
  const int32_t radius = lv_obj_get_style_radius(pop.box, LV_PART_MAIN);
  const int32_t blur = popup_layout::shadow_width_for(card_radius);
  // LVGL's shadow corner piece (lv_draw_sw_box_shadow.c corner_size).
  const int32_t corner = blur + radius;
  lv_obj_t* probe = plain(pop.root);
  lv_obj_set_size(probe, 2 * corner + kShadowSidePiece, 2 * corner + kShadowSidePiece);
  lv_obj_set_style_radius(probe, radius, 0);
  popup_layout::apply_card_shadow(probe);
  lv_obj_set_style_shadow_width(probe, blur, 0);
  lv_obj_update_layout(probe);
  const int32_t ext = lv_obj_get_ext_draw_size(probe);
  lv_draw_buf_t* snap = lv_snapshot_take(probe, LV_COLOR_FORMAT_A8);
  lv_obj_delete(probe);
  if (!snap) return;
  const int32_t sw = snap->header.w;
  const int32_t sh = snap->header.h;
  const int32_t piece = kShadowSidePiece;
  const lv_area_t from[8] = {{0, 0, corner - 1, corner - 1},
                             {sw - corner, 0, sw - 1, corner - 1},
                             {0, sh - corner, corner - 1, sh - 1},
                             {sw - corner, sh - corner, sw - 1, sh - 1},
                             {corner, 0, corner + piece - 1, ext - 1},
                             {corner, sh - ext, corner + piece - 1, sh - 1},
                             {0, corner, ext - 1, corner + piece - 1},
                             {sw - ext, corner, sw - 1, corner + piece - 1}};
  const int32_t w = popup_layout::kCardWidth - 2 * m.box_offset + 2 * ext;
  const int32_t h = popup_layout::kCardHeight - m.box_offset - m.top + 2 * ext;
  const lv_area_t to[8] = {{0, 0, corner - 1, corner - 1},
                           {w - corner, 0, w - 1, corner - 1},
                           {0, h - corner, corner - 1, h - 1},
                           {w - corner, h - corner, w - 1, h - 1},
                           {corner, 0, w - corner - 1, ext - 1},
                           {corner, h - ext, w - corner - 1, h - 1},
                           {0, corner, ext - 1, h - corner - 1},
                           {w - ext, corner, w - 1, h - corner - 1}};
  pop.menu_shadow = plain(pop.root);
  lv_obj_move_to_index(pop.menu_shadow, lv_obj_get_index(pop.box));
  lv_obj_set_pos(pop.menu_shadow, m.box_offset - ext, m.top - ext);
  lv_obj_set_size(pop.menu_shadow, w, h);
  uint32_t bytes = 0;
  for (int i = 0; i < 8; ++i) {
    const lv_area_t& a = from[i];
    const int32_t pw = lv_area_get_width(&a);
    const int32_t ph = lv_area_get_height(&a);
    lv_draw_buf_t* part = lv_draw_buf_create(pw, ph, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
    if (!part) continue;
    for (int32_t y = 0; y < ph; ++y) {
      memcpy(part->data + y * part->header.stride, snap->data + (a.y1 + y) * snap->header.stride + a.x1, pw);
    }
    bytes += part->data_size;
    pop.menu_shadow_parts[i] = part;
    lv_obj_t* image = lv_image_create(pop.menu_shadow);
    lv_image_set_src(image, part);
    lv_obj_set_pos(image, to[i].x1, to[i].y1);
    lv_obj_set_size(image, lv_area_get_width(&to[i]), lv_area_get_height(&to[i]));
    if (i >= 4) lv_image_set_inner_align(image, LV_IMAGE_ALIGN_TILE);
    lv_obj_set_style_image_recolor(image, lv_color_black(), 0);
  }
  lv_draw_buf_destroy(snap);
  Serial.printf("[VacuumPopup] Menu shadow pictures: %u bytes until the popup closes, %lu ms\n",
                static_cast<unsigned>(bytes), static_cast<unsigned long>(millis() - started_ms));
}

void on_shield(lv_event_t*) { open_tab(Tab::None); }

void on_tab(lv_event_t* e) {
  const Tab tab = static_cast<Tab>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  open_tab(tab);
}

void on_menu_button(lv_event_t*) {
  if (pop.tab != Tab::None) {
    open_tab(Tab::None);
    return;
  }
  const Detail& d = current();
  Tab tab = tab_offered(d, pop.last_tab) ? pop.last_tab : Tab::None;
  for (Tab candidate : kTabs) {
    if (tab == Tab::None && tab_offered(d, candidate)) tab = candidate;
  }
  if (tab != Tab::None) open_tab(tab);
}

void on_speed(lv_event_t* e) {
  const intptr_t index = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  const Detail& d = current();
  if (index < 0 || index >= d.speed_count) return;
  vacuum_control::set_fan_speed(pop.target.entity, d.speeds[index]);
}

void on_room(lv_event_t* e) {
  const intptr_t index = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  if (index < 0 || index >= static_cast<intptr_t>(vacuum_detail::kMaxAreas)) return;
  pop.rooms ^= 1u << index;
  pop.list_key = 0;
  open_tab(Tab::Rooms);
}

void on_spot(lv_event_t*) { vacuum_control::send(pop.target.entity, "clean_spot"); }
void on_locate(lv_event_t*) { vacuum_control::send(pop.target.entity, "locate"); }

void on_go(lv_event_t*) {
  const Detail& d = current();
  const char* ids[vacuum_detail::kMaxAreas];
  size_t count = 0;
  for (uint8_t i = 0; i < d.area_count; ++i) {
    if (pop.rooms & (1u << i)) ids[count++] = d.areas[i].id;
  }
  if (!count) return;
  vacuum_control::clean_areas(pop.target.entity, ids, count);
  pop.rooms = 0;
  open_tab(Tab::None);
}

// A list row like the Media menu's (media_popup.cpp list_row): the kind
// icon, the text, a check circle (chosen: filled) or a play circle (an
// action) at the right.
lv_obj_t* list_row(const char* label, const char* icon, int mark, lv_event_cb_t cb, intptr_t index) {
  const Colors& c = pop.colors;
  lv_obj_t* row = plain(pop.list);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, LV_PCT(100), kListRowHeight - kOptionGap);
  settings_style::apply_radius(row, settings_style::kGroupRadius - settings_style::kOptionInset);
  lv_obj_set_style_bg_color(row, c.control, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(row, c.control_opa, LV_PART_MAIN | LV_STATE_PRESSED);
  popup_nav_style::no_press_filter(row, LV_PART_MAIN | LV_STATE_PRESSED);
  disable_pressed_button_animation(row);
  const int pad = settings_style::kRowPadLeft - settings_style::kOptionInset;
  lv_obj_set_style_pad_left(row, pad, 0);
  lv_obj_set_style_pad_right(row, pad, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, Device::kGridGap, 0);
  if (icon) add_icon(row, icon, c.secondary);
  lv_obj_t* name = add_text(row, settings_style::row_font(), c.text, label);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(name, 1);
  // mark: 0 open circle, 1 chosen (filled with a check), 2 an action (play).
  const int edge = popup_layout::scale(40);
  lv_obj_t* circle = plain(row);
  lv_obj_set_size(circle, edge, edge);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(circle, lv_color_hex(c.text), 0);
  lv_obj_set_style_border_width(circle, mark == 1 ? 0 : popup_layout::scale(3), 0);
  lv_obj_set_style_bg_color(circle, lv_color_hex(c.text), 0);
  lv_obj_set_style_bg_opa(circle, mark == 1 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  if (mark != 0) {
    lv_obj_t* glyph = add_icon(circle, mark == 1 ? "check" : "play", mark == 1 ? c.card : c.text);
    lv_obj_center(glyph);
  }
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(index));
  return row;
}

uint32_t list_signature(const Detail& d) {
  uint32_t key = static_cast<uint32_t>(pop.tab) << 28 ^ pop.rooms * 40503u;
  for (const char* p = d.fan_speed; *p; ++p) key = key * 31 + static_cast<uint8_t>(*p);
  key ^= static_cast<uint32_t>(d.speed_count) << 8 ^ static_cast<uint32_t>(d.area_count) << 16 ^ d.features;
  for (uint8_t i = 0; i < d.area_count; ++i) {
    for (const char* p = d.areas[i].id; *p; ++p) key = key * 33 + static_cast<uint8_t>(*p);
  }
  return key ? key : 1;
}

void show_menu() {
  const Detail& d = current();
  if (!pop.box) return;
  if (pop.tab != Tab::None && !tab_offered(d, pop.tab)) pop.tab = Tab::None;
  if (pop.tab == Tab::None) {
    set_shown(pop.box, false);
    set_shown(pop.menu_shadow, false);
    lv_obj_remove_flag(pop.shield, LV_OBJ_FLAG_CLICKABLE);
    return;
  }
  lv_obj_add_flag(pop.shield, LV_OBJ_FLAG_CLICKABLE);
  if (!pop.menu_shadow) build_menu_shadow();
  set_shown(pop.menu_shadow, true);
  set_shown(pop.box, true);
  lv_label_set_text(pop.menu_title, tab_title(pop.tab));
  const Colors& c = pop.colors;
  for (size_t k = 0; k < 3; ++k) {
    lv_obj_t* tab = pop.tabs[k];
    set_shown(tab, tab_offered(d, kTabs[k]));
    const bool on = kTabs[k] == pop.tab;
    popup_nav_style::set_bg(tab, c.control, on ? c.control_opa : LV_OPA_TRANSP, LV_PART_MAIN);
    popup_nav_style::set_bg(tab, on ? c.raised : c.control, on ? c.raised_opa : c.control_opa,
                            LV_PART_MAIN | LV_STATE_PRESSED);
  }
  // "Clean N" while rooms are chosen.
  uint8_t chosen = 0;
  for (uint8_t i = 0; i < d.area_count; ++i) chosen += (pop.rooms >> i) & 1u;
  set_shown(pop.go, pop.tab == Tab::Rooms && chosen > 0);
  if (chosen) {
    char go[48];
    snprintf(go, sizeof(go), text(i18n::VacuumLabel::CleanRooms), static_cast<unsigned>(chosen));
    lv_label_set_text(pop.go_text, go);
  }
  const uint32_t key = list_signature(d);
  if (pop.list_key == key) return;
  pop.list_key = key;
  lv_obj_clean(pop.list);
  if (pop.tab == Tab::Speed) {
    static const char* const kSpeedIcons[] = {"fan-speed-1", "fan-speed-2", "fan-speed-3", "fan-plus"};
    for (uint8_t i = 0; i < d.speed_count; ++i) {
      const char* icon = kSpeedIcons[std::min<int>(i * 4 / std::max<int>(1, d.speed_count), 3)];
      list_row(d.speeds[i], icon, std::strcmp(d.speeds[i], d.fan_speed) == 0 ? 1 : 0, on_speed, i);
    }
  } else if (pop.tab == Tab::Rooms) {
    if (d.features & vacuum_detail::kCleanSpot) {
      String spot = text(i18n::VacuumLabel::Spot);
      spot += " · ";
      spot += text(i18n::VacuumLabel::SpotHint);
      list_row(spot.c_str(), "target-variant", 2, on_spot, 0);
    }
    if (d.features & vacuum_detail::kCleanArea) {
      for (uint8_t i = 0; i < d.area_count; ++i) {
        list_row(d.areas[i].name, "texture-box", (pop.rooms >> i) & 1u ? 1 : 0, on_room, i);
      }
    }
  } else {
    String locate = text(i18n::VacuumLabel::Locate);
    locate += " · ";
    locate += text(i18n::VacuumLabel::LocateHint);
    list_row(locate.c_str(), "map-marker", 2, on_locate, 0);
  }
}

void open_tab(Tab tab) {
  pop.tab = tab;
  if (tab != Tab::None) pop.last_tab = tab;
  show_menu();
}

void build_menu() {
  const Colors& c = pop.colors;
  const int W = popup_layout::kCardWidth;
  const int H = popup_layout::kCardHeight;
  const int g = Device::kGridGap;
  const MenuGeometry m = menu_geometry();
  // A tap outside the box closes it; the layer draws nothing.
  pop.shield = plain(pop.root);
  lv_obj_set_size(pop.shield, W, H);
  lv_obj_add_event_cb(pop.shield, on_shield, LV_EVENT_CLICKED, nullptr);
  const int32_t card_radius = lv_obj_get_style_radius(pop.card, LV_PART_MAIN);
  pop.box = plain(pop.root);
  lv_obj_add_flag(pop.box, LV_OBJ_FLAG_CLICKABLE);  // a tap inside stays inside
  lv_obj_set_pos(pop.box, m.box_offset, m.top);
  lv_obj_set_size(pop.box, W - 2 * m.box_offset, H - m.box_offset - m.top);
  lv_obj_set_style_radius(pop.box, std::max<int32_t>(0, card_radius - m.box_offset), 0);
  lv_obj_set_style_bg_color(pop.box, lv_color_hex(c.card), 0);
  lv_obj_set_style_bg_opa(pop.box, LV_OPA_COVER, 0);
  // Its shadow comes as pictures when the menu opens (build_menu_shadow).
  lv_obj_add_flag(pop.box, LV_OBJ_FLAG_HIDDEN);

  pop.menu_title = add_text(pop.box, settings_style::row_font(), c.text, "");
  lv_obj_set_pos(pop.menu_title, m.box_pad + settings_style::kRowPadLeft - settings_style::kOptionInset,
                 m.box_pad + m.head_height / 2 - lv_font_get_line_height(settings_style::row_font()) / 2);
  // "Clean N" in the head row's corner: the filled main button's look.
  pop.go = plain(pop.box);
  lv_obj_add_flag(pop.go, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_height(pop.go, popup_layout::scale(56));
  lv_obj_set_width(pop.go, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(pop.go, LV_RADIUS_CIRCLE, 0);
  filled(pop.go, c);
  lv_obj_set_style_pad_left(pop.go, popup_layout::scale(16), 0);
  lv_obj_set_style_pad_right(pop.go, popup_layout::scale(22), 0);
  lv_obj_set_flex_flow(pop.go, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pop.go, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pop.go, popup_layout::scale(8), 0);
  add_icon(pop.go, "play", c.control_icon);
  pop.go_text = add_text(pop.go, popup_layout::font20(), c.text, "");
  lv_obj_align(pop.go, LV_ALIGN_TOP_RIGHT, -m.box_pad, m.box_pad + (m.head_height - popup_layout::scale(56)) / 2);
  lv_obj_add_event_cb(pop.go, on_go, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(pop.go, LV_OBJ_FLAG_HIDDEN);
  // The hairline like the Settings rows' separator.
  lv_obj_t* rule = plain(pop.box);
  lv_obj_set_pos(rule, m.box_pad, m.box_pad + m.head_height + g / 2);
  lv_obj_set_size(rule, m.content_width, 1);
  lv_obj_set_style_bg_color(rule, lv_color_hex(c.text), 0);
  lv_obj_set_style_bg_opa(rule, settings_style::kSeparatorOpa, 0);

  pop.list = plain(pop.box);
  lv_obj_set_pos(pop.list, m.box_pad, m.box_pad + m.head_height + g);
  lv_obj_set_size(pop.list, m.content_width, m.list_height);
  lv_obj_set_flex_flow(pop.list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_width(pop.list, popup_layout::scale(5), LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(pop.list, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(pop.list, lv_color_hex(c.text), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(pop.list, LV_OPA_50, LV_PART_SCROLLBAR);
  lv_obj_set_scrollbar_mode(pop.list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(pop.list, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_add_flag(pop.list, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                                       LV_OBJ_FLAG_SCROLL_MOMENTUM));
  lv_obj_set_scroll_dir(pop.list, LV_DIR_VER);

  // The tabs in the row's place: the Media menu's columns.
  auto cx = [&](int k) { return W / 2 + (k - 2) * ml::kStep - ml::kButton / 2; };
  for (size_t k = 0; k < 3; ++k) {
    pop.tabs[k] = round_button(pop.box, cx(static_cast<int>(k) + 1) - m.box_offset, ml::kVolumeTop - m.top,
                               tab_icon(kTabs[k]), c);
    lv_obj_add_event_cb(pop.tabs[k], on_tab, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(kTabs[k])));
  }
}

// ---------------------------------------------------------------------------
// Body

// One value line under the centred map: the text or the icon, the taller.
int value_line() {
  return std::max<int>(lv_font_get_line_height(popup_layout::font20()), lv_font_get_line_height(FONT_MDI_ICONS));
}

Value make_value(lv_obj_t* parent, const char* icon, const Colors& c) {
  Value value;
  value.box = plain(parent);
  lv_obj_set_size(value.box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(value.box, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(value.box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(value.box, popup_layout::scale(12), 0);
  add_icon(value.box, icon, c.control_icon);
  // The size of the Media popup's times and volume.
  value.text = add_text(value.box, popup_layout::font20(), c.text, "");
  return value;
}

int map_edge(bool centered) {
  const int L = ml::kEdge;
  const int top = ml::kLeftCoverTop;
  const int row = ml::kVolumeTop;
  if (!centered) return row - L - top;
  return row - L - top - value_line() - L;
}

void build_body(bool centered) {
  // Everything on the root goes (the map view frees its picture itself, the
  // shadow pictures are freed here).
  free_menu_shadow(false);
  if (pop.root) lv_obj_clean(pop.root);
  pop.map = pop.dock = pop.main = pop.main_glyph = pop.main_text = pop.stop = pop.menu_button = nullptr;
  pop.shield = pop.box = pop.menu_title = pop.go = pop.go_text = pop.list = nullptr;
  for (lv_obj_t*& tab : pop.tabs) tab = nullptr;
  for (Value& value : pop.values) value = Value{};
  pop.list_key = 0;
  pop.tab = Tab::None;
  const Colors& c = pop.colors;
  const int W = popup_layout::kCardWidth;
  const int L = ml::kEdge;
  const int top = ml::kLeftCoverTop;
  const int row = ml::kVolumeTop;
  static const char* const kValueIcons[3] = {"texture-box", "timer-outline", "battery-80"};
  if (!centered) {
    // The map left (the Media cover's place over the seek and volume rows),
    // the values in a column beside it.
    const int edge = map_edge(false);
    pop.map = vacuum_map::create(pop.root, L, top, edge, kMapRadius);
    lv_obj_t* column = plain(pop.root);
    lv_obj_set_pos(column, L + edge + L, top);
    lv_obj_set_size(column, W - (L + edge + L) - L, edge);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(column, L, 0);
    for (int i = 0; i < 3; ++i) pop.values[i] = make_value(column, kValueIcons[i], c);
  } else {
    // The map centred, one value line and one edge less high, the values in
    // that line below it.
    const int line = value_line();
    const int edge = map_edge(true);
    pop.map = vacuum_map::create(pop.root, (W - edge) / 2, top, edge, kMapRadius);
    lv_obj_t* line_box = plain(pop.root);
    lv_obj_set_pos(line_box, 0, top + edge + L);
    lv_obj_set_size(line_box, W, line);
    lv_obj_set_flex_flow(line_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(line_box, kValueLineGap, 0);
    for (int i = 0; i < 3; ++i) pop.values[i] = make_value(line_box, kValueIcons[i], c);
  }
  // The row: Dock, the main button in the middle, Stop, the menu corner.
  pop.dock = text_button(pop.root, W / 2 - kRowStep, row, "home-import-outline", text(i18n::VacuumLabel::Dock), c);
  lv_obj_add_event_cb(pop.dock, on_dock, LV_EVENT_CLICKED, nullptr);
  pop.stop = text_button(pop.root, W / 2 + kRowStep, row, "stop", text(i18n::VacuumLabel::Stop), c);
  lv_obj_add_event_cb(pop.stop, on_stop, LV_EVENT_CLICKED, nullptr);
  pop.main = plain(pop.root);
  lv_obj_add_flag(pop.main, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_height(pop.main, ml::kButton);
  lv_obj_set_width(pop.main, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(pop.main, LV_RADIUS_CIRCLE, 0);
  filled(pop.main, c);
  lv_obj_set_style_opa(pop.main, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);
  lv_obj_set_style_pad_left(pop.main, popup_layout::scale(22), 0);
  lv_obj_set_style_pad_right(pop.main, popup_layout::scale(28), 0);
  lv_obj_set_flex_flow(pop.main, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pop.main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pop.main, popup_layout::scale(10), 0);
  pop.main_glyph = add_icon(pop.main, "play", c.control_icon);
  pop.main_text = add_text(pop.main, popup_layout::font20(), c.text, text(i18n::VacuumLabel::Clean));
  lv_obj_set_y(pop.main, row);
  lv_obj_add_event_cb(pop.main, on_main, LV_EVENT_CLICKED, nullptr);
  pop.menu_button = round_button(pop.root, W - L - ml::kButton, row, "dots-horizontal", c);
  lv_obj_add_event_cb(pop.menu_button, on_menu_button, LV_EVENT_CLICKED, nullptr);
  build_menu();
  // The menu button stays over the open menu (it closes it).
  lv_obj_move_foreground(pop.menu_button);
  pop.built = true;
  pop.built_centered = centered;
  pop.built_colors = colors_key(c);
}

void show_values(const Detail& d) {
  char buffer[32];
  // Cleaned area, cleaning time, battery: only what the vacuum reports.
  const bool has[3] = {d.has_cleaned_area, d.has_cleaning_time, d.has_battery};
  for (int i = 0; i < 3; ++i) {
    set_shown(pop.values[i].box, has[i]);
    if (!has[i] || !pop.values[i].text) continue;
    if (i == 0) {
      const String number = i18n::localize_numeric_text(lang(), String(d.cleaned_area, d.cleaned_area < 10 ? 1 : 0));
      snprintf(buffer, sizeof(buffer), "%s %s", number.c_str(), d.area_unit);
    } else if (i == 1) {
      snprintf(buffer, sizeof(buffer), "%u min", static_cast<unsigned>(d.cleaning_minutes));
    } else {
      snprintf(buffer, sizeof(buffer), "%u %%", static_cast<unsigned>(d.battery));
    }
    lv_label_set_text(pop.values[i].text, buffer);
  }
}

void ensure_popup() {
  if (pop.card) return;
  const auto parts = create_popup_body([](lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) hide_vacuum_popup();
  }, nullptr, pop.card_rgb);
  pop.overlay = parts.overlay;
  // A theme change deletes the popup (popup_shell_delete_popups); the next
  // opening builds it again.
  lv_obj_add_event_cb(pop.overlay, [](lv_event_t*) {
    free_menu_shadow(false);
    pop = Popup{};
  }, LV_EVENT_DELETE, nullptr);
  pop.card = parts.card;
  pop.title = parts.title;
  pop.icon = parts.icon;
  pop.close = parts.close;
  disable_pressed_button_animation(parts.close);
  pop.value = lv_label_create(pop.card);
  lv_obj_add_flag(pop.value, LV_OBJ_FLAG_HIDDEN);
  pop.root = plain(pop.card);
  lv_obj_add_flag(pop.root, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(pop.root, popup_layout::kCardWidth, popup_layout::kCardHeight);
  lv_obj_set_pos(pop.root, -popup_layout::kCardPad, -popup_layout::kCardPad);
  lv_obj_move_foreground(pop.icon);
  lv_obj_move_foreground(pop.title);
  lv_obj_move_foreground(pop.close);
  lv_obj_add_flag(pop.card, LV_OBJ_FLAG_HIDDEN);
}

void refresh(bool show) {
  if (!pop.target.entity.length()) return;
  ensure_popup();
  const Detail& d = current();
  const vacuum_visual::Visual v = vacuum_visual::visual(d);
  pop.icon_rgb = v.color;
  lv_obj_set_style_bg_color(pop.card, lv_color_hex(pop.card_rgb), 0);
  const String icon = pop.target.icon_name.length()
                          ? pop.target.icon_name
                          : vacuum_visual::icon_name(haBridgeConfig.findEntityIcon(pop.target.entity), v);
  lv_label_set_text(pop.icon, pop.target.icon_visible ? getMdiChar(icon).c_str() : "");
  lv_obj_set_style_text_color(pop.icon, lv_color_hex(v.color), 0);
  hometiles_title::set(pop.title, pop.target.title.c_str());
  // The header's state without the battery (it stands beside the map).
  lv_label_set_text(pop.value, d.valid ? i18n::vacuum_state_label(lang(), d.available ? d.state : "unavailable") : "--");
  const Colors colors = compute_colors(pop.card_rgb, v.color);
  pop.colors = colors;
  if (!pop.built || pop.built_centered != pop.target.map_centered || pop.built_colors != colors_key(colors)) {
    build_body(pop.target.map_centered);
  }
  // The corners round towards the card (the popup body is transparent).
  vacuum_map::set_under(pop.map, pop.card_rgb);
  vacuum_map::set_entity(pop.map, pop.target.map_entity.length() ? pop.target.map_entity.c_str() : d.map,
                         vacuum_detail::moving(d));
  // A picture fetched ahead shows in the popup's first frame.
  if (show) vacuum_map::show_now(pop.map);
  show_values(d);
  const bool ready = d.valid && d.available;
  const bool pauses = vacuum_detail::main_pauses(d);
  lv_label_set_text(pop.main_glyph, getMdiChar(pauses ? "pause" : "play").c_str());
  lv_label_set_text(pop.main_text, text(pauses ? i18n::VacuumLabel::Pause : i18n::VacuumLabel::Clean));
  place_centered(pop.main, popup_layout::kCardWidth / 2);
  set_enabled(pop.main, ready && (pauses || vacuum_detail::can_start(d)));
  set_enabled(pop.dock, ready && (d.features & vacuum_detail::kReturnHome));
  set_enabled(pop.stop, ready && (d.features & vacuum_detail::kStop));
  bool menu = false;
  for (Tab tab : kTabs) menu = menu || tab_offered(d, tab);
  set_shown(pop.menu_button, menu);
  show_menu();
  if (show) {
    pop.open = true;
    lv_obj_remove_flag(pop.card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pop.overlay, LV_OBJ_FLAG_CLICKABLE);
    viewNavigationPopupShown(pop.card, pop.target.entity.c_str());
    show_popup_shell(pop.overlay, pop.card, pop.title, pop.icon, pop.close, nullptr, pop.value);
  } else if (pop.open) {
    sync_popup_shell();
  }
}

uint32_t theme_default_card() {
  const ui_theme::SurfaceScope theme = popup_surface::theme_scope();
  return popup_surface::card_or_default(tileDefaultBgColor());
}

}  // namespace

void show_vacuum_popup(const VacuumPopupTarget& target) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::theme_scope();
  if (!target.entity.length()) return;
  hide_pin_popup();
  hide_light_popup();
  hide_climate_popup();
  hide_cover_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_energy_popup();
  hide_media_popup();
  hide_camera_popup();
  hide_calendar_popup();
  hide_device_popup();
  if (pop.target.entity != target.entity) {
    pop.rooms = 0;
    pop.last_tab = Tab::None;
  }
  pop.target = target;
  const uint32_t fallback = theme_default_card();
  if (!pop.target.source) tile_icon_source::open_popup_without_tile();
  pop.card_rgb = pop.target.source ? tile_icon_source::popup_background(pop.target.source, fallback) : fallback;
  const ui_theme::SurfaceScope surface = popup_surface::scope();
  pop.tab = Tab::None;
  refresh(true);
}

void hide_vacuum_popup() {
  if (!pop.card || !pop.overlay) return;
  pop.open = false;
  pop.tab = Tab::None;
  show_menu();
  // Nothing kept for good: the shadow pictures come again with the next menu.
  free_menu_shadow(true);
  hide_popup_shell(pop.card);
  cancel_popup_open(pop.card);
  lv_obj_add_flag(pop.card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(pop.overlay, LV_OBJ_FLAG_CLICKABLE);
}

void preload_vacuum_popup() {
  const ui_theme::SurfaceScope popup_scope = popup_surface::theme_scope();
  ensure_popup();
}

void vacuum_popup_follow_tile_color(uint32_t color) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (!pop.open || !pop.card) return;
  pop.card_rgb = color;
  lv_obj_set_style_bg_color(pop.card, lv_color_hex(color), 0);
  popup_shell_follow_tile_color(color);
  refresh(false);
}

int vacuum_popup_map_edge(bool centered) { return map_edge(centered); }

void vacuum_popup_refresh(const String& entity) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (!pop.open || (entity != "*" && pop.target.entity != entity)) return;
  refresh(false);
}

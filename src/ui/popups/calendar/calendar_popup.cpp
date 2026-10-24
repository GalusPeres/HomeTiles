#include "src/ui/popups/calendar/calendar_popup.h"

#include <algorithm>
#include <cstring>

#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/calendar/calendar_model.h"
#include "src/types/calendar/calendar_store.h"
#include "src/types/calendar/calendar_text.h"
#include "src/types/calendar/calendar_tile.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/device/device_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_nav_style.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_surface.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/ui_theme.h"

namespace {

using calendar_events::Event;
using calendar_model::Item;
using calendar_model::Sources;
using calendar_text::Now;
using i18n::CalendarLabel;

// The Weather popup's footer (weather_popup.cpp kFooter*): the range pill on
// the left, two action slots and the arrows on the right.
constexpr int kFooterButtonHeight = popup_layout::kNavHeight;
constexpr int kFooterActionButtonWidth = popup_layout::scale(92);
constexpr int kFooterButtonRadius = kFooterButtonHeight / 2;
constexpr int kFooterButtonGap = popup_layout::scale(10);
constexpr int kFooterInsetX = popup_layout::scale(6);
constexpr int kFooterOffsetY = -popup_layout::scale(6);
constexpr int kNavButtonSize = kFooterButtonHeight;
constexpr int kFooterNextButtonX = -kFooterInsetX;
constexpr int kFooterPrevButtonX = -(kFooterInsetX + kNavButtonSize + kFooterButtonGap);
constexpr int kFooterInnerActionX = -(kFooterInsetX + (kNavButtonSize * 2) + (kFooterButtonGap * 2));
constexpr int kContentWidth = popup_layout::kContentWidth;
constexpr int kContentHeight = popup_layout::kCardHeight - 2 * popup_layout::kCardPad;

// The list shows this many days from its first one; the arrows move it a week.
constexpr int32_t kListDays = 28;
constexpr int32_t kListStep = 7;
constexpr size_t kMaxDayEvents = 24;

enum class Mode : uint8_t { Month, List };

struct Popup {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* close = nullptr;
  lv_obj_t* body = nullptr;
  lv_obj_t* grid = nullptr;
  lv_obj_t* divider = nullptr;
  lv_obj_t* day_title = nullptr;
  lv_obj_t* day_list = nullptr;
  lv_obj_t* list = nullptr;
  lv_obj_t* pill = nullptr;
  lv_obj_t* pill_label = nullptr;
  lv_obj_t* mode_btn = nullptr;
  lv_obj_t* today_btn = nullptr;
  lv_obj_t* prev_btn = nullptr;
  lv_obj_t* next_btn = nullptr;
  CalendarPopupTarget target;
  String entities[calendar_model::kMaxCalendars];
  uint8_t entity_count = 0;
  uint32_t card_rgb = popup_surface::default_card();
  bool open = false;
  Mode mode = Mode::Month;
  int year = 1970;
  unsigned month = 1;
  int32_t selected = 0;
  int32_t list_start = 0;
  // What the grid draws.
  int32_t first_day = 0;
  uint8_t masks[42] = {};
  int32_t today = 0;
  bool today_valid = false;
  int32_t window_from = 0;
  int32_t window_to = -1;
} pop;

const char* const kNumbers[31] = {"1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "10", "11",
                                  "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22",
                                  "23", "24", "25", "26", "27", "28", "29", "30", "31"};

// Body geometry in the card's content box: below the header, above the footer.
int body_top() {
  return popup_layout::kHeaderCenterY + popup_layout::kCloseButtonSize / 2 + popup_layout::scale(8) -
         popup_layout::kCardPad;
}
int body_bottom() { return kContentHeight + kFooterOffsetY - kFooterButtonHeight - popup_layout::scale(14); }
int body_height() { return std::max(1, body_bottom() - body_top()); }
// The month grid takes about three fifths of the body, the chosen day the rest.
int grid_height() { return body_height() * 3 / 5; }

Sources sources() {
  Sources out;
  for (uint8_t i = 0; i < pop.entity_count; ++i) out.calendars[i] = calendar_store::get(pop.entities[i]);
  out.count = pop.entity_count;
  return out;
}

uint32_t color_of(uint8_t calendar) { return calendar_model::calendar_color(calendar, ui_theme::light()); }

lv_color_t card_color() {
  return pop.card ? lv_obj_get_style_bg_color(pop.card, LV_PART_MAIN) : lv_color_hex(ui_theme::popup_card());
}

lv_color_t icon_color() { return lv_color_hex(pop.entity_count ? color_of(0) : ui_theme::icon()); }

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color) {
  lv_obj_t* label = lv_label_create(parent);
  set_label_style(label, lv_color_hex(color), font);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
  lv_label_set_text(label, "");
  return label;
}

lv_obj_t* make_box(lv_obj_t* parent) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  return box;
}

// The calendar's name for the event rows ("Family", or its id).
String calendar_name(uint8_t calendar) {
  if (calendar >= pop.entity_count) return String();
  String name = haBridgeConfig.findSensorName(pop.entities[calendar]);
  if (!name.length()) name = pop.entities[calendar].substring(9);
  return name;
}

void refresh(bool show);

// ---------------------------------------------------------------------------
// Month grid: one object drawn in one pass, a tap picks a day.

void draw_text(lv_layer_t* layer, const lv_area_t& box, const char* text, const lv_font_t* font, lv_color_t color) {
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.base.layer = layer;
  dsc.font = font;
  dsc.color = color;
  dsc.text = text;
  dsc.align = LV_TEXT_ALIGN_CENTER;
  const int32_t line = lv_font_get_line_height(font);
  const int32_t top = box.y1 + (lv_area_get_height(&box) - line) / 2;
  const lv_area_t area = {box.x1, top, box.x2, top + line - 1};
  lv_draw_label(layer, &dsc, &area);
}

void draw_circle(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t diameter, lv_color_t color, lv_opa_t opa,
                 int32_t border = 0, lv_color_t border_color = lv_color_black()) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.base.layer = layer;
  dsc.bg_color = color;
  dsc.bg_opa = opa;
  dsc.radius = LV_RADIUS_CIRCLE;
  if (border) {
    dsc.border_width = border;
    dsc.border_color = border_color;
    dsc.border_opa = LV_OPA_COVER;
  }
  const int32_t r = diameter / 2;
  const lv_area_t area = {cx - r, cy - r, cx - r + diameter - 1, cy - r + diameter - 1};
  lv_draw_rect(layer, &dsc, &area);
}

int32_t header_height() { return lv_font_get_line_height(popup_layout::font20()) + popup_layout::scale(6); }

void grid_draw_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!layer || !pop.grid) return;
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  lv_area_t area;
  lv_obj_get_coords(pop.grid, &area);
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  const lv_color_t text = lv_color_hex(ui_theme::text());
  const lv_color_t card = card_color();
  const lv_color_t faint = lv_color_mix(text, card, LV_OPA_40);
  const lv_color_t secondary = lv_color_hex(ui_theme::text_secondary());
  const uint8_t week_start = calendar_text::week_start(pop.target.week_start);
  const lv_font_t* small = popup_layout::font20();
  const lv_font_t* font = popup_layout::font24();
  const int32_t header = header_height();
  const int32_t cell_w = width / 7;
  const int32_t cell_h = (height - header) / 6;
  for (int i = 0; i < 7; ++i) {
    const lv_area_t box = {area.x1 + i * cell_w, area.y1, area.x1 + (i + 1) * cell_w - 1, area.y1 + header - 1};
    draw_text(layer, box, i18n::locale(calendar_text::language()).weather_weekdays_short[(week_start + i) % 7],
              small, secondary);
  }
  const int32_t line = lv_font_get_line_height(font);
  const int32_t dot = popup_layout::scale(7);
  const int32_t circle = std::min<int32_t>(std::min<int32_t>(cell_w, cell_h) - dot - 4, line + popup_layout::scale(14));
  lv_color_t select_fill;
  lv_opa_t select_opa;
  popup_nav_style::fill(card, icon_color(), select_fill, select_opa);
  for (int index = 0; index < 42; ++index) {
    const int32_t day = pop.first_day + index;
    int y;
    unsigned m, d;
    calendar_events::civil_from_days(day, y, m, d);
    const int32_t x1 = area.x1 + (index % 7) * cell_w;
    const int32_t y1 = area.y1 + header + (index / 7) * cell_h;
    const int32_t cx = x1 + cell_w / 2;
    const int32_t block = circle + 3 + dot;
    const int32_t cy = y1 + (cell_h - block) / 2 + circle / 2;
    const bool outside = m != pop.month;
    const bool today = pop.today_valid && day == pop.today;
    const bool selected = day == pop.selected;
    lv_color_t number = outside ? faint : (pop.today_valid && day < pop.today ? secondary : text);
    if (today) {
      draw_circle(layer, cx, cy, circle, text, LV_OPA_COVER);
      number = card;
    } else if (selected) {
      draw_circle(layer, cx, cy, circle, select_fill, select_opa, popup_layout::scale(2), icon_color());
    }
    const lv_area_t box = {x1, cy - line / 2, x1 + cell_w - 1, cy - line / 2 + line - 1};
    draw_text(layer, box, kNumbers[d - 1], font, number);
    const uint8_t mask = outside ? 0 : pop.masks[index];
    int count = 0;
    for (uint8_t c = 0; c < calendar_model::kMaxCalendars; ++c) count += (mask >> c) & 1;
    if (!count) continue;
    const int32_t gap = std::max<int32_t>(2, dot / 2);
    int32_t x = cx - (count * dot + (count - 1) * gap) / 2 + dot / 2;
    for (uint8_t c = 0; c < calendar_model::kMaxCalendars; ++c) {
      if (!((mask >> c) & 1)) continue;
      draw_circle(layer, x, cy + circle / 2 + 3 + dot / 2, dot, lv_color_hex(color_of(c)), LV_OPA_COVER);
      x += dot + gap;
    }
  }
}

void on_grid_click(lv_event_t*) {
  lv_indev_t* indev = lv_indev_get_act();
  if (!indev || !pop.grid) return;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_area_t area;
  lv_obj_get_coords(pop.grid, &area);
  const int32_t header = header_height();
  const int32_t cell_w = lv_area_get_width(&area) / 7;
  const int32_t cell_h = (lv_area_get_height(&area) - header) / 6;
  if (cell_w <= 0 || cell_h <= 0 || point.y < area.y1 + header) return;
  const int col = std::min(6, static_cast<int>((point.x - area.x1) / cell_w));
  const int row = std::min(5, static_cast<int>((point.y - area.y1 - header) / cell_h));
  if (col < 0 || row < 0) return;
  const int32_t day = pop.first_day + row * 7 + col;
  pop.selected = day;
  int y;
  unsigned m, d;
  calendar_events::civil_from_days(day, y, m, d);
  // A day of the neighbouring month shows that month.
  pop.year = y;
  pop.month = m;
  refresh(false);
}

// ---------------------------------------------------------------------------
// Event rows

// One event: the time (start above end), a bar in its calendar's color, the
// title and the calendar's name (and place) below it.
void add_event_row(lv_obj_t* parent, int32_t y, int32_t row_h, const Item& item, int32_t day, int32_t time_w) {
  const Event& e = *item.event;
  const lv_font_t* time_font = popup_layout::font20();
  const lv_font_t* title_font = popup_layout::font24();
  const lv_font_t* sub_font = popup_layout::font20();
  lv_obj_t* time = make_label(parent, time_font, ui_theme::text_secondary());
  lv_obj_set_width(time, time_w);
  lv_label_set_long_mode(time, LV_LABEL_LONG_CLIP);
  String text;
  if (e.all_day()) {
    text = calendar_text::label(CalendarLabel::AllDay);
  } else {
    const int start = e.start_day == day ? e.start_min : 0;
    const int end = e.end_day == day ? e.end_min : 24 * 60;
    text = calendar_text::clock(start) + "\n" + calendar_text::clock(end);
  }
  lv_label_set_text(time, text.c_str());
  const int32_t time_lines = e.all_day() ? 1 : 2;
  lv_obj_set_pos(time, 0, y + (row_h - time_lines * lv_font_get_line_height(time_font)) / 2);
  const int32_t bar_w = popup_layout::scale(6);
  lv_obj_t* bar = make_box(parent);
  lv_obj_set_size(bar, bar_w, row_h - popup_layout::scale(14));
  lv_obj_set_pos(bar, time_w + popup_layout::scale(12), y + popup_layout::scale(7));
  lv_obj_set_style_radius(bar, bar_w / 2, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(color_of(item.calendar)), 0);
  const int32_t text_x = time_w + bar_w + popup_layout::scale(28);
  const int32_t text_w = std::max<int32_t>(1, kContentWidth - text_x - popup_layout::scale(8));
  String sub = calendar_name(item.calendar);
  if (e.start_day != e.end_day) {
    sub += " \xC2\xB7 " + calendar_text::day_label(e.start_day, calendar_text::now()) + " \xE2\x80\x93 " +
           calendar_text::day_label(e.end_day, calendar_text::now());
  }
  if (e.location[0]) sub += String(" \xC2\xB7 ") + e.location;
  const int32_t block = lv_font_get_line_height(title_font) + lv_font_get_line_height(sub_font);
  lv_obj_t* title = make_label(parent, title_font, ui_theme::text());
  lv_obj_set_width(title, text_w);
  lv_obj_set_pos(title, text_x, y + (row_h - block) / 2);
  lv_label_set_text(title, e.title[0] ? e.title : "--");
  lv_obj_t* below = make_label(parent, sub_font, ui_theme::text_secondary());
  lv_obj_set_width(below, text_w);
  lv_obj_set_pos(below, text_x, y + (row_h - block) / 2 + lv_font_get_line_height(title_font));
  lv_label_set_text(below, sub.c_str());
}

int32_t time_column_width() {
  const lv_font_t* font = popup_layout::font20();
  lv_point_t size;
  lv_text_get_size(&size, calendar_text::label(CalendarLabel::AllDay), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  int32_t width = size.x;
  const String sample = calendar_text::clock(23 * 60 + 58);
  lv_text_get_size(&size, sample.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return std::min<int32_t>(std::max<int32_t>(width, size.x) + 4, kContentWidth / 4);
}

int32_t event_row_height() {
  return lv_font_get_line_height(popup_layout::font24()) + lv_font_get_line_height(popup_layout::font20()) +
         popup_layout::scale(16);
}

void message(lv_obj_t* parent, const char* text) {
  lv_obj_t* label = make_label(parent, popup_layout::font24(), ui_theme::text_secondary());
  lv_obj_set_width(label, kContentWidth);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(label, text);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, popup_layout::scale(16));
}

// The chosen day below the month.
void fill_day(const Sources& s, const Now& now) {
  lv_obj_clean(pop.day_list);
  // "Today · Saturday", "Mon 26 Oct".
  String label = calendar_text::day_label(pop.selected, now);
  if (now.valid && (pop.selected == now.today || pop.selected == now.today + 1)) {
    label += " \xC2\xB7 " +
             String(i18n::locale(calendar_text::language()).weekday_names[calendar_events::weekday(pop.selected)]);
  }
  lv_label_set_text(pop.day_title, label.c_str());
  if (!s.any_valid()) {
    message(pop.day_list, calendar_text::label(CalendarLabel::NoData));
    return;
  }
  Item items[kMaxDayEvents];
  const size_t total = calendar_model::items_on(s, pop.selected, items, kMaxDayEvents);
  if (!total) {
    message(pop.day_list, calendar_text::label(CalendarLabel::NoEvents));
    return;
  }
  const int32_t row_h = event_row_height();
  const int32_t time_w = time_column_width();
  for (size_t i = 0; i < std::min(total, kMaxDayEvents); ++i) {
    add_event_row(pop.day_list, static_cast<int32_t>(i) * row_h, row_h, items[i], pop.selected, time_w);
  }
}

// The coming days, each with its events.
void fill_list(const Sources& s, const Now& now) {
  lv_obj_clean(pop.list);
  if (!s.any_valid()) {
    message(pop.list, calendar_text::label(CalendarLabel::NoData));
    return;
  }
  const lv_font_t* day_font = popup_layout::font24();
  const int32_t day_h = lv_font_get_line_height(day_font) + popup_layout::scale(18);
  const int32_t row_h = event_row_height();
  const int32_t time_w = time_column_width();
  const lv_color_t line_color = lv_color_mix(lv_color_hex(ui_theme::text()), card_color(), LV_OPA_20);
  int32_t y = 0;
  bool any = false;
  for (int32_t day = pop.list_start; day < pop.list_start + kListDays; ++day) {
    Item items[kMaxDayEvents];
    const size_t total = std::min(calendar_model::items_on(s, day, items, kMaxDayEvents), kMaxDayEvents);
    if (!total) continue;
    any = true;
    lv_obj_t* head = make_label(pop.list, day_font, ui_theme::text());
    lv_obj_set_width(head, kContentWidth);
    String label = calendar_text::day_label(day, now);
    if (now.valid && (day == now.today || day == now.today + 1)) {
      label += " \xC2\xB7 " + String(i18n::locale(calendar_text::language()).weekday_names[calendar_events::weekday(day)]);
    }
    lv_label_set_text(head, label.c_str());
    lv_obj_set_pos(head, 0, y + popup_layout::scale(8));
    lv_obj_t* rule = make_box(pop.list);
    lv_obj_set_size(rule, kContentWidth, 1);
    lv_obj_set_pos(rule, 0, y + day_h - 2);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rule, line_color, 0);
    y += day_h;
    for (size_t i = 0; i < total; ++i) {
      add_event_row(pop.list, y, row_h, items[i], day, time_w);
      y += row_h;
    }
    y += popup_layout::scale(8);
  }
  if (!any) message(pop.list, calendar_text::label(CalendarLabel::NothingPlanned));
}

// ---------------------------------------------------------------------------
// Footer

lv_obj_t* make_text_button(const char* text, int x_ofs, lv_event_cb_t cb) {
  lv_obj_t* btn = lv_button_create(pop.card);
  lv_obj_set_size(btn, kFooterActionButtonWidth, kFooterButtonHeight);
  lv_obj_set_style_radius(btn, kFooterButtonRadius, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_anim_time(btn, 0, 0);
  lv_obj_set_style_anim_time(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(btn, 0, LV_STATE_PRESSED);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, x_ofs, kFooterOffsetY);
  lv_obj_set_ext_click_area(btn, popup_layout::scale480(12));
  lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* label = lv_label_create(btn);
  set_label_style(label, lv_color_hex(ui_theme::text()), FONT_UNIT);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

lv_obj_t* make_nav_button(const char* icon, int x_ofs, lv_event_cb_t cb) {
  lv_obj_t* btn = lv_button_create(pop.card);
  lv_obj_set_size(btn, kNavButtonSize, kNavButtonSize);
  lv_obj_set_style_radius(btn, kNavButtonSize / 2, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_anim_time(btn, 0, 0);
  lv_obj_set_style_anim_time(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, x_ofs, kFooterOffsetY);
  lv_obj_set_ext_click_area(btn, popup_layout::scale480(12));
  lv_obj_t* label = lv_label_create(btn);
  set_label_style(label, lv_color_hex(ui_theme::icon()), FONT_MDI_ICONS);
  lv_label_set_text(label, getMdiChar(icon).c_str());
  lv_obj_center(label);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  return btn;
}

void set_nav_state(lv_obj_t* btn, bool enabled) {
  if (!btn) return;
  const lv_color_t inactive = lv_color_mix(lv_color_hex(ui_theme::text()), card_color(), 96);
  if (lv_obj_t* icon = lv_obj_get_child(btn, 0)) {
    lv_obj_set_style_text_color(icon, enabled ? lv_color_hex(ui_theme::text()) : inactive, 0);
    lv_obj_set_style_text_color(icon, enabled ? lv_color_hex(ui_theme::text()) : inactive, LV_STATE_PRESSED);
  }
  popup_nav_style::style_press(btn, card_color(), icon_color());
  if (enabled) {
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  }
}

// The Today button keeps the footer font; a long word widens it to the left.
int today_width() {
  lv_point_t size;
  lv_text_get_size(&size, i18n::weather_today_button_label(calendar_text::language()), FONT_UNIT, 0, 0,
                   LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return std::max<int>(kFooterActionButtonWidth, size.x + kFooterButtonHeight / 2);
}

int mode_width() {
  lv_point_t size;
  int width = kFooterActionButtonWidth;
  for (CalendarLabel which : {CalendarLabel::ViewList, CalendarLabel::ViewMonth}) {
    lv_text_get_size(&size, calendar_text::label(which), FONT_UNIT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    width = std::max<int>(width, size.x + kFooterButtonHeight / 2);
  }
  return width;
}

void layout_footer() {
  const int today_w = today_width();
  const int mode_w = mode_width();
  lv_obj_set_width(pop.today_btn, today_w);
  lv_obj_align(pop.today_btn, LV_ALIGN_BOTTOM_RIGHT, kFooterInnerActionX, kFooterOffsetY);
  const int mode_x = kFooterInnerActionX - today_w - kFooterButtonGap;
  lv_obj_set_width(pop.mode_btn, mode_w);
  lv_obj_align(pop.mode_btn, LV_ALIGN_BOTTOM_RIGHT, mode_x, kFooterOffsetY);
  const int pill_w = std::max(kFooterButtonHeight,
                              kContentWidth + mode_x - mode_w - kFooterButtonGap - kFooterInsetX * 2);
  lv_obj_set_width(pop.pill, pill_w);
  lv_obj_set_width(pop.pill_label, std::max(1, pill_w - popup_layout::scale(24)));
}

void on_mode(lv_event_t*) {
  pop.mode = pop.mode == Mode::Month ? Mode::List : Mode::Month;
  if (pop.mode == Mode::List) pop.list_start = pop.selected;
  refresh(false);
}

void on_today(lv_event_t*) {
  const Now now = calendar_text::now();
  if (!now.valid) return;
  pop.selected = now.today;
  pop.list_start = now.today;
  int y;
  unsigned m, d;
  calendar_events::civil_from_days(now.today, y, m, d);
  pop.year = y;
  pop.month = m;
  refresh(false);
}

void step(int direction) {
  if (pop.mode == Mode::List) {
    pop.list_start += direction * kListStep;
  } else {
    calendar_model::add_months(pop.year, pop.month, direction);
    // Keep the chosen day in the shown month (the 1st, or today there).
    const Now now = calendar_text::now();
    int y;
    unsigned m, d;
    if (now.valid) {
      calendar_events::civil_from_days(now.today, y, m, d);
      if (y == pop.year && m == pop.month) {
        pop.selected = now.today;
        refresh(false);
        return;
      }
    }
    pop.selected = calendar_events::days_from_civil(pop.year, pop.month, 1);
  }
  refresh(false);
}

void on_prev(lv_event_t*) { step(-1); }
void on_next(lv_event_t*) { step(1); }

// ---------------------------------------------------------------------------
// Popup object

void on_close(lv_event_t* e) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) hide_calendar_popup();
}

void on_overlay_delete(lv_event_t*) { pop = Popup{}; }

lv_obj_t* make_scroller(lv_obj_t* parent, int y, int height) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, kContentWidth, height);
  lv_obj_set_pos(box, 0, y);
  lv_obj_add_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLL_ELASTIC);
  return box;
}

void ensure_popup() {
  if (pop.card) return;
  const auto parts = create_popup_body(on_close, nullptr, pop.card_rgb);
  pop.overlay = parts.overlay;
  lv_obj_add_event_cb(pop.overlay, on_overlay_delete, LV_EVENT_DELETE, nullptr);
  pop.card = parts.card;
  pop.title = parts.title;
  pop.icon = parts.icon;
  pop.close = parts.close;
  disable_pressed_button_animation(parts.close);

  pop.body = make_box(pop.card);
  lv_obj_set_size(pop.body, kContentWidth, body_height());
  lv_obj_set_pos(pop.body, 0, body_top());

  // Month: the grid, a hairline, the chosen day and its events.
  pop.grid = make_box(pop.body);
  lv_obj_set_size(pop.grid, kContentWidth, grid_height());
  lv_obj_set_pos(pop.grid, 0, 0);
  lv_obj_add_flag(pop.grid, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(pop.grid, grid_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(pop.grid, on_grid_click, LV_EVENT_CLICKED, nullptr);
  const int divider_y = grid_height() + popup_layout::scale(6);
  pop.divider = make_box(pop.body);
  lv_obj_set_size(pop.divider, kContentWidth, 1);
  lv_obj_set_pos(pop.divider, 0, divider_y);
  lv_obj_set_style_bg_opa(pop.divider, LV_OPA_COVER, 0);
  const int day_title_y = divider_y + popup_layout::scale(10);
  pop.day_title = make_label(pop.body, popup_layout::font20(), ui_theme::text_secondary());
  lv_obj_set_width(pop.day_title, kContentWidth);
  lv_obj_set_pos(pop.day_title, 0, day_title_y);
  const int list_y = day_title_y + lv_font_get_line_height(popup_layout::font20()) + popup_layout::scale(4);
  pop.day_list = make_scroller(pop.body, list_y, std::max(1, body_height() - list_y));

  // List: the coming days.
  pop.list = make_scroller(pop.body, 0, body_height());
  lv_obj_add_flag(pop.list, LV_OBJ_FLAG_HIDDEN);

  // Footer: range pill, view, Today, arrows.
  pop.pill = lv_obj_create(pop.card);
  lv_obj_remove_style_all(pop.pill);
  lv_obj_set_size(pop.pill, kFooterButtonHeight * 3, kFooterButtonHeight);
  lv_obj_set_style_radius(pop.pill, kFooterButtonRadius, 0);
  lv_obj_set_style_bg_opa(pop.pill, LV_OPA_COVER, 0);
  lv_obj_remove_flag(pop.pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(pop.pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(pop.pill, LV_ALIGN_BOTTOM_LEFT, kFooterInsetX, kFooterOffsetY);
  pop.pill_label = make_label(pop.pill, FONT_UNIT, ui_theme::text());
  lv_obj_set_style_text_align(pop.pill_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(pop.pill_label);
  pop.mode_btn = make_text_button("", kFooterInnerActionX, on_mode);
  pop.today_btn =
      make_text_button(i18n::weather_today_button_label(calendar_text::language()), kFooterInnerActionX, on_today);
  pop.prev_btn = make_nav_button("chevron-left", kFooterPrevButtonX, on_prev);
  pop.next_btn = make_nav_button("chevron-right", kFooterNextButtonX, on_next);
  layout_footer();

  lv_obj_move_foreground(pop.icon);
  lv_obj_move_foreground(pop.title);
  lv_obj_move_foreground(pop.close);
  lv_obj_add_flag(pop.card, LV_OBJ_FLAG_HIDDEN);
}

void refresh(bool show) {
  if (!pop.target.entities.length() && !show) return;
  ensure_popup();
  const Now now = calendar_text::now();
  const Sources s = sources();
  pop.today_valid = now.valid;
  pop.today = now.today;
  if (!s.window(pop.window_from, pop.window_to)) {
    pop.window_from = 0;
    pop.window_to = -1;
  }
  lv_obj_set_style_bg_color(pop.card, lv_color_hex(pop.card_rgb), 0);
  const String icon = pop.target.icon_name.length() ? pop.target.icon_name : String("calendar");
  lv_label_set_text(pop.icon, pop.target.icon_visible ? getMdiChar(icon).c_str() : "");
  lv_obj_set_style_text_color(pop.icon, icon_color(), 0);
  const String title =
      pop.target.title.length() ? pop.target.title : String(calendar_text::label(CalendarLabel::TypeCalendar));
  hometiles_title::set(pop.title, title.c_str());
  popup_layout::alignHeader(pop.card, pop.title, pop.icon);

  // Grid data.
  const uint8_t week_start = calendar_text::week_start(pop.target.week_start);
  pop.first_day = calendar_model::month_grid_start(pop.year, pop.month, week_start);
  for (int i = 0; i < 42; ++i) pop.masks[i] = calendar_model::day_mask(s, pop.first_day + i);

  const bool month = pop.mode == Mode::Month;
  for (lv_obj_t* obj : {pop.grid, pop.divider, pop.day_title, pop.day_list}) {
    if (month) {
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (month) {
    lv_obj_add_flag(pop.list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(pop.divider, lv_color_mix(lv_color_hex(ui_theme::text()), card_color(), LV_OPA_20), 0);
    lv_obj_set_style_text_color(pop.day_title, lv_color_hex(ui_theme::text_secondary()), 0);
    fill_day(s, now);
    lv_obj_invalidate(pop.grid);
  } else {
    lv_obj_remove_flag(pop.list, LV_OBJ_FLAG_HIDDEN);
    fill_list(s, now);
  }

  // Footer.
  String range;
  bool can_prev = true;
  bool can_next = true;
  if (month) {
    range = calendar_text::month_title(pop.year, pop.month);
    const int32_t first = calendar_events::days_from_civil(pop.year, pop.month, 1);
    const int32_t last = first + static_cast<int32_t>(calendar_events::days_in_month(pop.year, pop.month)) - 1;
    can_prev = pop.window_to >= pop.window_from && first > pop.window_from;
    can_next = pop.window_to >= pop.window_from && last < pop.window_to;
  } else {
    int y;
    unsigned m, d;
    calendar_events::civil_from_days(pop.list_start, y, m, d);
    range = i18n::format_short_date(calendar_text::language(), static_cast<int>(d),
                                    i18n::weather_month_short(calendar_text::language(), static_cast<int>(m)));
    calendar_events::civil_from_days(pop.list_start + kListDays - 1, y, m, d);
    range += " \xE2\x80\x93 " +
             i18n::format_short_date(calendar_text::language(), static_cast<int>(d),
                                     i18n::weather_month_short(calendar_text::language(), static_cast<int>(m)));
    can_prev = pop.window_to >= pop.window_from && pop.list_start > pop.window_from;
    can_next = pop.window_to >= pop.window_from && pop.list_start + kListDays - 1 < pop.window_to;
  }
  lv_label_set_text(pop.pill_label, range.c_str());
  popup_nav_style::style_pill(pop.pill, pop.pill_label, card_color(), icon_color());
  lv_label_set_text(lv_obj_get_child(pop.mode_btn, 0),
                    calendar_text::label(month ? CalendarLabel::ViewList : CalendarLabel::ViewMonth));
  popup_nav_style::style_toggle(pop.mode_btn, lv_obj_get_child(pop.mode_btn, 0), card_color(), icon_color(), false);
  const bool at_today = now.valid && (month ? pop.selected == now.today : pop.list_start == now.today);
  popup_nav_style::style_toggle(pop.today_btn, lv_obj_get_child(pop.today_btn, 0), card_color(), icon_color(),
                                at_today);
  lv_label_set_text(lv_obj_get_child(pop.today_btn, 0), i18n::weather_today_button_label(calendar_text::language()));
  layout_footer();
  set_nav_state(pop.prev_btn, can_prev);
  set_nav_state(pop.next_btn, can_next);

  if (show) {
    pop.open = true;
    lv_obj_remove_flag(pop.card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pop.overlay, LV_OBJ_FLAG_CLICKABLE);
    viewNavigationPopupShown(pop.card, pop.target.entities.c_str());
    show_popup_shell(pop.overlay, pop.card, pop.title, pop.icon, pop.close, nullptr, nullptr);
  } else if (pop.open) {
    sync_popup_shell();
  }
}

uint32_t theme_default_card() {
  const ui_theme::SurfaceScope theme = popup_surface::theme_scope();
  return popup_surface::card_or_default(tileDefaultBgColor());
}

}  // namespace

void show_calendar_popup(const CalendarPopupTarget& target) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::theme_scope();
  hide_pin_popup();
  hide_light_popup();
  hide_climate_popup();
  hide_cover_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_energy_popup();
  hide_media_popup();
  hide_camera_popup();
  hide_device_popup();
  const bool same = pop.target.entities == target.entities;
  pop.target = target;
  pop.entity_count = static_cast<uint8_t>(
      calendar_store::split_entities(target.entities, pop.entities, calendar_model::kMaxCalendars));
  if (!pop.entity_count) return;
  if (!calendar_tile_alive(target.source)) pop.target.source = nullptr;
  const uint32_t fallback = theme_default_card();
  if (!pop.target.source) tile_icon_source::open_popup_without_tile();
  pop.card_rgb = pop.target.source ? tile_icon_source::popup_background(pop.target.source, fallback) : fallback;
  const ui_theme::SurfaceScope open_scope = popup_surface::scope();
  // A new opening starts at today's month (another tile, or after a while).
  const Now now = calendar_text::now();
  if (!same || !pop.open) {
    pop.mode = Mode::Month;
    pop.selected = now.valid ? now.today : calendar_events::days_from_civil(2026, 1, 1);
    pop.list_start = pop.selected;
    int y;
    unsigned m, d;
    calendar_events::civil_from_days(pop.selected, y, m, d);
    pop.year = y;
    pop.month = m;
  }
  refresh(true);
}

void hide_calendar_popup() {
  if (!pop.card || !pop.overlay) return;
  pop.open = false;
  hide_popup_shell(pop.card);
  cancel_popup_open(pop.card);
  lv_obj_add_flag(pop.card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(pop.overlay, LV_OBJ_FLAG_CLICKABLE);
}

void preload_calendar_popup() {
  const ui_theme::SurfaceScope popup_scope = popup_surface::theme_scope();
  ensure_popup();
}

void calendar_popup_follow_tile_color(uint32_t color) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (!pop.open || !pop.card) return;
  pop.card_rgb = color;
  lv_obj_set_style_bg_color(pop.card, lv_color_hex(color), 0);
  popup_shell_follow_tile_color(color);
}

void calendar_popup_refresh(const String& entity) {
  if (!pop.open) return;
  bool shows = entity == "*";
  for (uint8_t i = 0; !shows && i < pop.entity_count; ++i) shows = pop.entities[i] == entity;
  if (!shows) return;
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  refresh(false);
}

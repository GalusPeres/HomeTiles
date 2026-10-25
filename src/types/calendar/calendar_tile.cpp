#include "src/ui/shared/ui_surface_style.h"
#include "src/types/calendar/calendar_tile.h"

#include <algorithm>
#include <cstring>

#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/calendar/calendar_model.h"
#include "src/types/calendar/calendar_store.h"
#include "src/types/calendar/calendar_text.h"
#include "src/types/calendar/calendar_updates.h"
#include "src/ui/popups/calendar/calendar_popup.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_theme.h"

namespace {

using calendar_events::Calendar;
using calendar_events::Event;
using calendar_model::Item;
using calendar_model::Sources;
using calendar_text::Now;
using i18n::CalendarLabel;

// Compact: half height, next event; Count: half height, today's events;
// Countdown: 1x1, the next event's day; Next: 2x1 "Up next".
enum class Layout : uint8_t { Compact, Count, Countdown, Next, Agenda, Week, Month };

// The month view needs a panel with room for a readable grid.
#if defined(DEVICE_LAYOUT_480X480)
constexpr bool kMonthAvailable = false;
#else
constexpr bool kMonthAvailable = true;
#endif

// How far the agenda and "Up next" look ahead (the Bridge sends about three
// months; a quiet calendar says "Nothing planned" instead of a date months
// away).
constexpr int32_t kAgendaDays = 31;
constexpr size_t kMaxAgendaRows = 12;

struct View {
  View* next = nullptr;
  GridType grid = GridType::TAB0;
  uint8_t index = 0;
  String entities[calendar_model::kMaxCalendars];
  uint8_t entity_count = 0;
  String entity_list;
  String title;
  String icon_name;
  bool icon_visible = true;
  uint8_t week_start = calendar_model::kWeekStartAuto;
  Layout layout = Layout::Next;
  // Half height at least two cells wide: the countdown on the top line.
  bool wide = false;
  // A one-row week strip starts today instead of on the first weekday.
  bool rolling = false;
  lv_obj_t* card = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* title_label = nullptr;
  // Compact, Count, Countdown and Up next: the when line and the event's
  // title (Countdown: the countdown and the large day).
  lv_obj_t* when = nullptr;
  lv_obj_t* what = nullptr;
  lv_obj_t* bar = nullptr;
  // Agenda rows (rebuilt when their text changes), Week and Month grid.
  lv_obj_t* list = nullptr;
  lv_obj_t* grid_obj = nullptr;
  int16_t content_x = 0;
  int16_t content_y = 0;
  int16_t content_w = 0;
  int16_t content_h = 0;
  uint32_t signature = 0;
  // What the grid draws.
  int32_t first_day = 0;
  int32_t today = 0;
  bool today_valid = false;
  uint8_t rows = 0;
  uint8_t month = 0;
  uint8_t masks[42] = {};
};

View* g_views = nullptr;
lv_timer_t* g_clock_timer = nullptr;
int32_t g_shown_day = -1;
int g_shown_minute = -1;

const char* const kNumbers[31] = {"1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "10", "11",
                                  "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22",
                                  "23", "24", "25", "26", "27", "28", "29", "30", "31"};

uint32_t hash_text(uint32_t hash, const char* text) {
  for (const char* c = text ? text : ""; *c; ++c) hash = (hash ^ static_cast<uint8_t>(*c)) * 16777619u;
  return (hash ^ 0xFF) * 16777619u;
}

Sources sources_of(const View* view) {
  Sources sources;
  for (uint8_t i = 0; i < view->entity_count; ++i) {
    sources.calendars[i] = calendar_store::get(view->entities[i]);
  }
  sources.count = view->entity_count;
  return sources;
}

uint32_t color_of(uint8_t calendar) { return calendar_model::calendar_color(calendar, ui_theme::light()); }

lv_obj_t* plain_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color) {
  lv_obj_t* label = lv_label_create(parent);
  set_label_style(label, lv_color_hex(color), font);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
  lv_label_set_text(label, "");
  return label;
}

lv_obj_t* plain_box(lv_obj_t* parent) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  return box;
}

void set_text(lv_obj_t* label, const char* text) {
  if (label && std::strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

// The largest of the content fonts whose line fits `height`.
const lv_font_t* font_for(int height) {
  const lv_font_t* fonts[] = {tile_layout::content_font_28(), tile_layout::content_font_24(),
                              tile_layout::content_font_20(), FONT_SMALL};
  for (const lv_font_t* font : fonts) {
    if (lv_font_get_line_height(font) <= height) return font;
  }
  return FONT_SMALL;
}

// ---------------------------------------------------------------------------
// Week strip and month grid: one object each, drawn in one DRAW_MAIN pass
// (no child per day).

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

void draw_circle(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t diameter, lv_color_t color, lv_opa_t opa) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.base.layer = layer;
  dsc.bg_color = color;
  dsc.bg_opa = opa;
  dsc.radius = LV_RADIUS_CIRCLE;
  const int32_t r = diameter / 2;
  const lv_area_t area = {cx - r, cy - r, cx - r + diameter - 1, cy - r + diameter - 1};
  lv_draw_rect(layer, &dsc, &area);
}

void draw_dots(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t dot, uint8_t mask) {
  int count = 0;
  for (uint8_t c = 0; c < calendar_model::kMaxCalendars; ++c) count += (mask >> c) & 1;
  if (!count) return;
  const int32_t gap = std::max<int32_t>(2, dot / 2);
  int32_t x = cx - (count * dot + (count - 1) * gap) / 2 + dot / 2;
  for (uint8_t c = 0; c < calendar_model::kMaxCalendars; ++c) {
    if (!((mask >> c) & 1)) continue;
    draw_circle(layer, x, cy, dot, lv_color_hex(color_of(c)), LV_OPA_COVER);
    x += dot + gap;
  }
}

void grid_draw_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
  View* view = static_cast<View*>(lv_event_get_user_data(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!view || !view->grid_obj || !layer || !view->rows) return;
  const ui_theme::SurfaceScope surface = tile_surface::scope(view->card);
  lv_area_t area;
  lv_obj_get_coords(view->grid_obj, &area);
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  if (width < 14 || height < 10) return;
  const lv_color_t text = lv_color_hex(ui_theme::text());
  const lv_color_t card = lv_obj_get_style_bg_color(view->card, LV_PART_MAIN);
  const lv_color_t faint = lv_color_mix(text, card, LV_OPA_40);
  const lv_color_t past = lv_color_hex(ui_theme::text_secondary());
  const char* lang = calendar_text::language();
  const uint8_t week_start = calendar_text::week_start(view->week_start);
  const bool month = view->layout == Layout::Month;

  // Weekday names on top of the month; the week strip shows them in its cells.
  const lv_font_t* small = FONT_SMALL;
  const int32_t header = month ? lv_font_get_line_height(small) : 0;
  const int32_t cell_w = width / 7;
  const int32_t cell_h = (height - header) / view->rows;
  if (month) {
    for (int i = 0; i < 7; ++i) {
      const int wd = (week_start + i) % 7;
      const lv_area_t box = {area.x1 + i * cell_w, area.y1, area.x1 + (i + 1) * cell_w - 1, area.y1 + header - 1};
      draw_text(layer, box, i18n::locale(lang).weather_weekdays_short[wd], small, past);
    }
  }
  const int32_t dot = std::max<int32_t>(4, tile_layout::scale_480(6));
  for (int row = 0; row < view->rows; ++row) {
    for (int col = 0; col < 7; ++col) {
      const int index = row * 7 + col;
      const int32_t day = view->first_day + index;
      int y;
      unsigned m, d;
      calendar_events::civil_from_days(day, y, m, d);
      const int32_t x1 = area.x1 + col * cell_w;
      const int32_t y1 = area.y1 + header + row * cell_h;
      const int32_t cx = x1 + cell_w / 2;
      const bool today = view->today_valid && day == view->today;
      const bool outside = month && m != view->month;
      lv_color_t number = outside ? faint : (view->today_valid && day < view->today ? past : text);
      if (month) {
        // Number and dots stacked in the cell.
        const lv_font_t* font = font_for(cell_h - dot - 4);
        const int32_t line = lv_font_get_line_height(font);
        const int32_t block = line + dot + 3;
        const int32_t top = y1 + (cell_h - block) / 2;
        const int32_t circle = std::min<int32_t>(cell_w - 2, line + 4);
        if (today) {
          draw_circle(layer, cx, top + line / 2, circle, text, LV_OPA_COVER);
          number = card;
        }
        const lv_area_t box = {x1, top, x1 + cell_w - 1, top + line - 1};
        draw_text(layer, box, kNumbers[d - 1], font, number);
        if (!outside) draw_dots(layer, cx, top + line + 3 + dot / 2, dot, view->masks[index]);
      } else {
        // Week strip: weekday, number (today in a circle), dots.
        const lv_font_t* font = font_for((cell_h - lv_font_get_line_height(small) - dot) / 2 + 6);
        const int32_t line = lv_font_get_line_height(font);
        const int32_t small_h = lv_font_get_line_height(small);
        const int32_t circle = std::min<int32_t>(cell_w - 4, line + tile_layout::scale_480(14));
        const int32_t block = small_h + 4 + circle + 4 + dot;
        const int32_t top = y1 + (cell_h - block) / 2;
        const lv_area_t name = {x1, top, x1 + cell_w - 1, top + small_h - 1};
        draw_text(layer, name, calendar_text::weekday_short(day), small, past);
        const int32_t cy = top + small_h + 4 + circle / 2;
        if (today) {
          // Today's column in a soft box of the icon's color (the mockup's
          // strip), behind the number's circle.
          lv_draw_rect_dsc_t box;
          lv_draw_rect_dsc_init(&box);
          box.base.layer = layer;
          box.bg_color = view->entity_count ? lv_color_hex(color_of(0)) : text;
          box.bg_opa = static_cast<lv_opa_t>(38);
          box.radius = tile_layout::scale_480(14);
          const int32_t inset = std::max<int32_t>(1, tile_layout::scale_480(2));
          const lv_area_t column = {x1 + inset, y1 + inset, x1 + cell_w - 1 - inset, y1 + cell_h - 1 - inset};
          lv_draw_rect(layer, &box, &column);
          draw_circle(layer, cx, cy, circle, text, LV_OPA_COVER);
          number = card;
        }
        const lv_area_t box = {x1, cy - line / 2, x1 + cell_w - 1, cy - line / 2 + line - 1};
        draw_text(layer, box, kNumbers[d - 1], font, number);
        draw_dots(layer, cx, top + small_h + 4 + circle + 4 + dot / 2, dot, view->masks[index]);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Showing the events

String header_text(const View* view, const Now& now) {
  if (view->layout == Layout::Month && now.valid) {
    int y;
    unsigned m, d;
    calendar_events::civil_from_days(now.today, y, m, d);
    return calendar_text::month_title(y, m);
  }
  return view->title;
}

void show_next(View* view, const Sources& sources, const Now& now) {
  String when;
  String what;
  String header;
  uint32_t bar = 0;
  bool has_bar = false;
  if (!view->entity_count) {
    what = "--";
  } else if (!sources.any_valid()) {
    what = calendar_text::label(CalendarLabel::NoData);
  } else if (!now.valid) {
    what = "--";
  } else {
    int32_t from = 0, to = 0;
    sources.window(from, to);
    const calendar_model::Next next =
        calendar_model::next_event(sources, now.today, now.minute, std::min(to, now.today + kAgendaDays));
    if (next.found) {
      if (view->layout == Layout::Compact) {
        const calendar_text::Lines lines = calendar_text::chip_lines(next, now, view->wide);
        when = lines.top;
        what = lines.value;
      } else if (view->layout == Layout::Countdown) {
        const calendar_text::Lines lines = calendar_text::countdown_lines(next, now);
        what = lines.top;
        when = lines.value;
        header = next.item.event->title[0] ? next.item.event->title : "--";
      } else {
        what = next.item.event->title;
        when = calendar_text::when_line(next, now, false);
      }
      bar = color_of(next.item.calendar);
      has_bar = true;
    } else {
      what = calendar_text::label(CalendarLabel::NothingPlanned);
    }
  }
  set_text(view->when, when.c_str());
  set_text(view->what, what.c_str());
  if (view->layout == Layout::Countdown && view->title_label) {
    // The event's title in the header; the tile's own title without one.
    if (!header.length()) header = view->title;
    const char* shown = hometiles_title::text(view->title_label);
    if (!shown || std::strcmp(shown, header.c_str()) != 0) hometiles_title::set(view->title_label, header.c_str(), true);
  }
  if (view->bar) {
    if (has_bar) {
      lv_obj_set_style_bg_color(view->bar, lv_color_hex(bar), 0);
      lv_obj_remove_flag(view->bar, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(view->bar, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (view->icon && has_bar) tile_icon_disc::set_icon_color(view->icon, lv_color_hex(bar));
}

// Half height, Today view: "Today" over the count of today's events left.
void show_count(View* view, const Sources& sources, const Now& now) {
  String what;
  if (!view->entity_count || !now.valid) {
    what = "--";
  } else if (!sources.any_valid()) {
    what = calendar_text::label(CalendarLabel::NoData);
  } else {
    what = calendar_text::event_count(calendar_model::remaining_today(sources, now.today, now.minute));
  }
  set_text(view->when, i18n::weather_today_label(calendar_text::language()));
  set_text(view->what, what.c_str());
  if (view->icon && view->entity_count) tile_icon_disc::set_icon_color(view->icon, lv_color_hex(color_of(0)));
}

// One agenda row: time, color bar, title.
void add_row(View* view, int32_t y, int32_t row_h, const String& time, uint32_t color, const char* title,
             int32_t time_w) {
  const lv_font_t* font = tile_layout::content_font_20();
  const int32_t line = lv_font_get_line_height(font);
  lv_obj_t* label = plain_label(view->list, font, ui_theme::text_secondary());
  lv_obj_set_width(label, time_w);
  lv_obj_set_pos(label, 0, y + (row_h - line) / 2);
  lv_label_set_text(label, time.c_str());
  lv_obj_t* bar = plain_box(view->list);
  const int32_t bar_w = std::max<int32_t>(3, tile_layout::scale_480(5));
  lv_obj_set_size(bar, bar_w, std::max<int32_t>(4, row_h - tile_layout::scale_480(10)));
  lv_obj_set_pos(bar, time_w + tile_layout::scale_480(8), y + tile_layout::scale_480(5));
  lv_obj_set_style_radius(bar, bar_w / 2, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);
  lv_obj_t* name = plain_label(view->list, font, ui_theme::text());
  const int32_t name_x = time_w + bar_w + tile_layout::scale_480(20);
  lv_obj_set_width(name, std::max<int32_t>(1, view->content_w - name_x));
  lv_obj_set_pos(name, name_x, y + (row_h - line) / 2);
  lv_label_set_text(name, title);
}

void show_agenda(View* view, const Sources& sources, const Now& now) {
  const lv_font_t* font = tile_layout::content_font_20();
  const lv_font_t* small = FONT_SMALL;
  const int32_t row_h = lv_font_get_line_height(font) + tile_layout::scale_480(10);
  const int32_t day_h = lv_font_get_line_height(small) + tile_layout::scale_480(6);
  const String all_day = calendar_text::label(CalendarLabel::AllDay);

  // What fits, collected first: a redraw only when the text changes.
  struct Line {
    bool day = false;
    int32_t date = 0;
    Item item;
  };
  Line lines[kMaxAgendaRows * 2];
  size_t line_count = 0;
  int32_t used = 0;
  const char* message = nullptr;
  uint32_t signature = 2166136261u;
  if (!view->entity_count) {
    message = "--";
  } else if (!sources.any_valid()) {
    message = calendar_text::label(CalendarLabel::NoData);
  } else if (!now.valid) {
    message = "--";
  } else {
    int32_t from = 0, to = 0;
    sources.window(from, to);
    const int32_t last = std::min(to, now.today + kAgendaDays);
    for (int32_t day = std::max(from, now.today); day <= last && line_count < kMaxAgendaRows * 2; ++day) {
      Item items[kMaxAgendaRows];
      size_t count = calendar_model::items_on(sources, day, items, kMaxAgendaRows);
      count = std::min(count, kMaxAgendaRows);
      size_t shown = 0;
      for (size_t i = 0; i < count; ++i) {
        const Event& e = *items[i].event;
        // Today's timed events that are over are left out.
        if (day == now.today && !e.all_day() && e.end_day == day && e.end_min <= now.minute) continue;
        if (!shown) {
          if (used + day_h + row_h > view->content_h) break;
          lines[line_count++] = Line{true, day, Item{}};
          used += day_h;
        }
        if (used + row_h > view->content_h || line_count >= kMaxAgendaRows * 2) break;
        lines[line_count++] = Line{false, day, items[i]};
        used += row_h;
        ++shown;
      }
      if (used + day_h + row_h > view->content_h) break;
    }
    if (!line_count) message = calendar_text::label(CalendarLabel::NothingPlanned);
  }
  if (message) signature = hash_text(signature, message);
  for (size_t i = 0; i < line_count; ++i) {
    const Line& line = lines[i];
    signature = hash_text(signature, line.day ? "D" : "E");
    signature = hash_text(signature, kNumbers[(line.date % 31 + 31) % 31]);
    if (!line.day) {
      signature = hash_text(signature, line.item.event->title);
      signature = hash_text(signature, calendar_text::start_text(*line.item.event, line.date).c_str());
      signature ^= line.item.calendar;
    } else {
      signature = hash_text(signature, calendar_text::day_label(line.date, now).c_str());
    }
  }
  signature ^= ui_theme::light() ? 0x55u : 0;
  if (signature == view->signature) return;
  view->signature = signature;
  lv_obj_clean(view->list);
  if (message) {
    lv_obj_t* label = plain_label(view->list, font, ui_theme::text_secondary());
    lv_obj_set_width(label, view->content_w);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, message);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    return;
  }
  // The time column as wide as "All day" or a time, whichever is wider.
  lv_point_t size;
  lv_text_get_size(&size, all_day.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  int32_t time_w = size.x;
  const String sample = calendar_text::clock(23 * 60 + 58);
  lv_text_get_size(&size, sample.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  time_w = std::min<int32_t>(std::max<int32_t>(time_w, size.x) + 2, view->content_w / 3);
  int32_t y = 0;
  for (size_t i = 0; i < line_count; ++i) {
    const Line& line = lines[i];
    if (line.day) {
      lv_obj_t* label = plain_label(view->list, small, ui_theme::text_secondary());
      lv_obj_set_width(label, view->content_w);
      lv_obj_set_pos(label, 0, y + tile_layout::scale_480(4));
      lv_label_set_text(label, calendar_text::day_label(line.date, now).c_str());
      y += day_h;
    } else {
      add_row(view, y, row_h, calendar_text::start_text(*line.item.event, line.date),
              color_of(line.item.calendar), line.item.event->title, time_w);
      y += row_h;
    }
  }
}

void show_grid(View* view, const Sources& sources, const Now& now) {
  view->today_valid = now.valid;
  view->today = now.today;
  const uint8_t week_start = calendar_text::week_start(view->week_start);
  int y = 1970;
  unsigned m = 1, d = 1;
  if (now.valid) calendar_events::civil_from_days(now.today, y, m, d);
  if (view->layout == Layout::Month) {
    view->first_day = calendar_model::month_grid_start(y, m, week_start);
    view->rows = static_cast<uint8_t>(calendar_model::month_rows(y, m, week_start));
    view->month = static_cast<uint8_t>(m);
  } else {
    view->first_day = calendar_model::strip_first_day(now.today, week_start, view->rolling);
    view->rows = 1;
  }
  uint32_t signature = 2166136261u ^ static_cast<uint32_t>(view->first_day) ^ (now.valid ? now.today << 3 : 7);
  for (int i = 0; i < view->rows * 7 && i < 42; ++i) {
    view->masks[i] = now.valid ? calendar_model::day_mask(sources, view->first_day + i) : 0;
    signature = (signature ^ view->masks[i]) * 16777619u;
  }
  signature ^= ui_theme::light() ? 0x55u : 0;
  if (signature != view->signature) {
    view->signature = signature;
    lv_obj_invalidate(view->grid_obj);
  }
  // The icon in the first calendar's color.
  if (view->icon && view->entity_count) tile_icon_disc::set_icon_color(view->icon, lv_color_hex(color_of(0)));
}

void show(View* view) {
  if (!view || !view->card) return;
  const ui_theme::SurfaceScope surface = tile_surface::scope(view->card);
  const Now now = calendar_text::now();
  const Sources sources = sources_of(view);
  if (view->title_label && view->layout != Layout::Countdown) {
    const String header = header_text(view, now);
    const char* shown = hometiles_title::text(view->title_label);
    if (!shown || std::strcmp(shown, header.c_str()) != 0) hometiles_title::set(view->title_label, header.c_str(), true);
  }
  switch (view->layout) {
    case Layout::Compact:
    case Layout::Countdown:
    case Layout::Next: show_next(view, sources, now); break;
    case Layout::Count: show_count(view, sources, now); break;
    case Layout::Agenda: show_agenda(view, sources, now); break;
    case Layout::Week:
    case Layout::Month: show_grid(view, sources, now); break;
  }
}

// ---------------------------------------------------------------------------
// Lifecycle

void clock_timer_cb(lv_timer_t*) {
  const Now now = calendar_text::now();
  const int32_t day = now.valid ? now.today : -2;
  const int minute = now.valid ? now.minute : -2;
  if (day == g_shown_day && minute == g_shown_minute) return;
  g_shown_day = day;
  g_shown_minute = minute;
  for (View* view = g_views; view; view = view->next) show(view);
  calendar_popup_refresh("*");
}

void unregister(View* view) {
  for (View** link = &g_views; *link; link = &(*link)->next) {
    if (*link == view) {
      *link = view->next;
      break;
    }
  }
  if (!g_views && g_clock_timer) {
    lv_timer_delete(g_clock_timer);
    g_clock_timer = nullptr;
  }
}

CalendarPopupTarget popup_target(const View* view) {
  CalendarPopupTarget target;
  target.entities = view->entity_list;
  target.title = view->title;
  target.icon_name = view->icon_name;
  target.icon_visible = view->icon_visible;
  target.week_start = view->week_start;
  target.source = view->card;
  return target;
}

void on_card_event(lv_event_t* e) {
  View* view = static_cast<View*>(lv_event_get_user_data(e));
  if (!view) return;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_DELETE) {
    unregister(view);
    delete view;
    return;
  }
  if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
  if (!view->entity_count) return;
  const CalendarPopupTarget target = popup_target(view);
  finish_press_before_popup(e);
  show_calendar_popup(target);
}

Layout layout_of(const Tile& tile) {
  const uint8_t view = calendar_model::normalize_view(tile.sensor_display_mode);
  if (tile_geometry::compact_calendar(tile.type, tile.span_w, tile.span_h)) {
    return view == calendar_model::kViewToday ? Layout::Count : Layout::Compact;
  }
  if (view == calendar_model::kViewMonth && kMonthAvailable && tile.span_w >= 2 && tile.span_h >= 2) {
    return Layout::Month;
  }
  if (view == calendar_model::kViewWeek && tile.span_w >= 1.5f) return Layout::Week;
  if (tile.span_h >= 2) return Layout::Agenda;
  return tile.span_w >= 2 ? Layout::Next : Layout::Countdown;
}

}  // namespace

void calendar_tiles_refresh(const String& entity) {
  const bool all = entity == "*";
  for (View* view = g_views; view; view = view->next) {
    bool shows = all;
    for (uint8_t i = 0; !shows && i < view->entity_count; ++i) shows = view->entities[i] == entity;
    if (shows) show(view);
  }
}

bool calendar_tile_alive(lv_obj_t* card) {
  if (!card) return false;
  for (View* view = g_views; view; view = view->next) {
    if (view->card == card) return true;
  }
  return false;
}

void process_calendar_updates(uint8_t budget) { calendar_store::process(budget); }

lv_obj_t* render_calendar_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index,
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
  // The Sensor card paddings, like the device tiles.
  const int pad_x = tile_layout::scale_480(20);
  const int pad_y = tile_layout::scale_480(24);
  lv_obj_set_style_pad_hor(card, pad_x, 0);
  lv_obj_set_style_pad_ver(card, pad_y, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);
  place_tile_card(card, col, row, tile);

  View* view = new View();
  view->grid = grid_type;
  view->index = index;
  view->entity_list = tile.sensor_entity;
  view->entity_count = static_cast<uint8_t>(
      calendar_store::split_entities(tile.sensor_entity, view->entities, calendar_model::kMaxCalendars));
  view->card = card;
  view->title = tile.title;
  view->icon_visible = !isMdiIconDisabled(tile.icon_name);
  view->icon_name = view->icon_visible ? normalizeMdiIconName(tile.icon_name) : String();
  view->week_start = calendar_model::normalize_week_start(tile.sensor_decimals);
  view->layout = layout_of(tile);
  view->wide = tile.span_w >= 2;
  view->rolling = tile.span_h < 2;
  const ui_theme::SurfaceScope surface = tile_surface::scope(card);

  if (view->icon_visible && FONT_MDI_ICONS) {
    view->icon = lv_label_create(card);
    set_label_style(view->icon, lv_color_hex(view->entity_count ? color_of(0) : ui_theme::icon_inactive()),
                    FONT_MDI_ICONS);
    lv_label_set_text(view->icon, getMdiChar(view->icon_name.length() ? view->icon_name : String("calendar")).c_str());
    lv_obj_align(view->icon, LV_ALIGN_TOP_LEFT, tile_layout::scale_480(-8), tile_layout::scale_480(-8));
  }

  const int card_w = tile_geometry::extent(tile.col, std::max(1.0f, tile.span_w), GRID_CELL_W, GRID_GAP);
  const int card_h = tile_geometry::extent(tile.row, std::max(0.5f, tile.span_h), GRID_CELL_H, GRID_GAP);

  if (view->layout == Layout::Compact || view->layout == Layout::Count) {
    view->when = plain_label(card, compact_sensor_layout::title_font(), ui_theme::text_secondary());
    view->what = plain_label(card, compact_sensor_layout::value_font(tile.sensor_value_font), ui_theme::text());
    compact_sensor_layout::apply(card, view->icon, view->when, view->what, tile);
  } else {
    if (view->icon) tile_icon_disc::add_round(card, view->icon);
    // The Sensor tile's tall header: the title top right.
    if (tile.title.length() || view->layout == Layout::Month || view->layout == Layout::Countdown) {
      view->title_label = lv_label_create(card);
      set_label_style(view->title_label, lv_color_hex(ui_theme::text()), tile_layout::header_title_font());
      lv_label_set_long_mode(view->title_label, LV_LABEL_LONG_DOT);
      lv_obj_set_width(view->title_label, LV_PCT(70));
      lv_obj_set_style_text_align(view->title_label, LV_TEXT_ALIGN_RIGHT, 0);
      hometiles_title::tile(view->title_label, tile.title.c_str(), true);
      lv_obj_align(view->title_label, LV_ALIGN_TOP_RIGHT, tile_layout::scale_480(4), tile_layout::scale_480(4));
    }
    // The content below the header row, in the card's content box.
    const int header = tile_icon_disc::row_height();
    const int gap = tile_layout::scale_480(4);
    view->content_x = 0;
    view->content_y = static_cast<int16_t>(header + gap - pad_y);
    view->content_w = static_cast<int16_t>(std::max(1, card_w - 2 * pad_x));
    view->content_h = static_cast<int16_t>(std::max(1, card_h - header - gap - pad_y));
    if (view->layout == Layout::Next) {
      const lv_font_t* what_font = tile.span_w >= 2 ? tile_layout::content_font_28() : tile_layout::content_font_24();
      const lv_font_t* when_font = tile_layout::content_font_20();
      const int bar_w = std::max(3, static_cast<int>(tile_layout::scale_480(6)));
      const int text_x = bar_w + tile_layout::scale_480(14);
      const int block = lv_font_get_line_height(what_font) + lv_font_get_line_height(when_font);
      const int top = view->content_y + std::max(0, (view->content_h - block) / 2);
      view->bar = plain_box(card);
      lv_obj_set_size(view->bar, bar_w, std::max(8, block - static_cast<int>(tile_layout::scale_480(4))));
      lv_obj_set_pos(view->bar, 0, top + tile_layout::scale_480(2));
      lv_obj_set_style_radius(view->bar, bar_w / 2, 0);
      lv_obj_set_style_bg_opa(view->bar, LV_OPA_COVER, 0);
      lv_obj_add_flag(view->bar, LV_OBJ_FLAG_HIDDEN);
      view->what = plain_label(card, what_font, ui_theme::text());
      lv_obj_set_width(view->what, std::max(1, view->content_w - text_x));
      lv_obj_set_pos(view->what, text_x, top);
      view->when = plain_label(card, when_font, ui_theme::text_secondary());
      lv_obj_set_width(view->when, std::max(1, view->content_w - text_x));
      lv_obj_set_pos(view->when, text_x, top + lv_font_get_line_height(what_font));
    } else if (view->layout == Layout::Countdown) {
      // The large day and the countdown, centred in the content box.
      const lv_font_t* when_font = tile_layout::content_font_20();
      const lv_font_t* what_font =
          font_for(std::max(1, view->content_h - static_cast<int>(lv_font_get_line_height(when_font))));
      const int block = lv_font_get_line_height(what_font) + lv_font_get_line_height(when_font);
      const int top = view->content_y + std::max(0, (view->content_h - block) / 2);
      view->what = plain_label(card, what_font, ui_theme::text());
      lv_obj_set_width(view->what, view->content_w);
      lv_obj_set_style_text_align(view->what, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(view->what, 0, top);
      view->when = plain_label(card, when_font, ui_theme::text_secondary());
      lv_obj_set_width(view->when, view->content_w);
      lv_obj_set_style_text_align(view->when, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(view->when, 0, top + lv_font_get_line_height(what_font));
    } else if (view->layout == Layout::Agenda) {
      view->list = plain_box(card);
      lv_obj_set_size(view->list, view->content_w, view->content_h);
      lv_obj_set_pos(view->list, view->content_x, view->content_y);
    } else {
      view->grid_obj = plain_box(card);
      lv_obj_set_size(view->grid_obj, view->content_w, view->content_h);
      lv_obj_set_pos(view->grid_obj, view->content_x, view->content_y);
      lv_obj_add_event_cb(view->grid_obj, grid_draw_cb, LV_EVENT_DRAW_MAIN, view);
    }
  }

  view->next = g_views;
  g_views = view;
  if (!g_clock_timer) g_clock_timer = lv_timer_create(clock_timer_cb, 15000, nullptr);
  show(view);

  if (view->entity_count && grid_type != GridType::SCREENSAVER) {
    const lv_event_code_t popup_event =
        getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS ? LV_EVENT_SHORT_CLICKED : LV_EVENT_LONG_PRESSED;
    lv_obj_add_event_cb(card, on_card_event, popup_event, view);
  }
  lv_obj_add_event_cb(card, on_card_event, LV_EVENT_DELETE, view);
  return card;
}

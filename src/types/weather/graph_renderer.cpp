#include "src/types/weather/graph_renderer.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/weather/graph_history.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/ui_theme.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/types/weather/weather_icons.h"

namespace {

constexpr uint8_t kMaxGraphHours = 24;
constexpr uint8_t kMaxParsedHours = 48;
constexpr uint8_t kBandCells = 24;
// The axis always covers the whole day, 00 to 23, with a mark every three
// hours: eight hour labels and eight temperature readouts.
constexpr uint8_t kHourStep = 3;
constexpr uint8_t kHourLabels = kMaxGraphHours / kHourStep;
constexpr uint8_t kTempLabels = kMaxGraphHours / kHourStep;
// The smooth line: the chart draws straight segments, so the curve is sampled
// four times per hour (Catmull-Rom through the hourly points). Hour i keeps
// index i * kCurveSub, exactly x = i / 23 of the width.
constexpr uint8_t kCurveSub = 4;
constexpr uint16_t kCurvePoints =
    kMaxGraphHours + (kMaxGraphHours - 1) * (kCurveSub - 1);
// Contiguous precipitation episodes named inside the condition band.
constexpr uint8_t kBandEpisodeLabels = 6;

struct GraphHour {
  String date;
  int hour = -1;
  float temperature = 0.0f;
  bool has_temperature = false;
  String condition;
  String icon;
  float precipitation = 0.0f;
  bool has_precipitation = false;
  // History entries carry "src":"hist" (measured past from HA history);
  // the rest is forecast. Duplicates merge by time: hist wins in the past,
  // forecast wins for the present and future.
  bool is_hist = false;
};

struct GraphData {
  GraphHour hours[kMaxParsedHours];
  uint8_t count = 0;
  String today;
  String tomorrow;
  int now_hour = 0;
  // The current time as hours with minutes (0..24), -1 without a clock: the
  // position of the "now" marker on the today half.
  float now_pos = -1.0f;
  bool has_time = false;
};

// ---- Small JSON helpers (the weather payload is already validated upstream,
// these only need to be tolerant; no heap-heavy parser is created per tile). ----

int find_key(const String& src, const char* key) {
  if (!key || !key[0]) return -1;
  String needle = String("\"") + key + "\"";
  int at = src.indexOf(needle);
  if (at < 0) return -1;
  at += needle.length();
  while (at < static_cast<int>(src.length()) &&
         (src[at] == ' ' || src[at] == ':' || src[at] == '\t' ||
          src[at] == '\r' || src[at] == '\n')) {
    ++at;
  }
  return at;
}

bool json_string(const String& src, const char* key, String& out) {
  out = "";
  int at = find_key(src, key);
  if (at < 0 || at >= static_cast<int>(src.length())) return false;
  if (src[at] != '"') return false;
  ++at;
  while (at < static_cast<int>(src.length())) {
    const char c = src[at++];
    if (c == '\\') {
      if (at < static_cast<int>(src.length())) {
        const char e = src[at++];
        if (e == 'n') out += '\n';
        else if (e == 't') out += '\t';
        else if (e == 'r') out += '\r';
        else out += e;
      }
      continue;
    }
    if (c == '"') return true;
    out += c;
  }
  return false;
}

bool json_number(const String& src, const char* key, float& out) {
  int at = find_key(src, key);
  if (at < 0 || at >= static_cast<int>(src.length())) return false;
  const char* start = src.c_str() + at;
  char* end = nullptr;
  const float value = strtof(start, &end);
  if (!end || end == start || std::isnan(value) || std::isinf(value)) return false;
  out = value;
  return true;
}

int find_array(const String& src, const char* key) {
  int at = find_key(src, key);
  if (at < 0) return -1;
  while (at < static_cast<int>(src.length()) && src[at] != '[') {
    if (src[at] == '}') return -1;
    ++at;
  }
  return at < static_cast<int>(src.length()) ? at + 1 : -1;
}

bool next_object(const String& src, int& cursor, String& out) {
  const int len = static_cast<int>(src.length());
  while (cursor < len && src[cursor] != '{') {
    if (src[cursor] == ']') return false;
    ++cursor;
  }
  if (cursor >= len) return false;
  const int start = cursor;
  int depth = 0;
  bool quoted = false;
  bool escaped = false;
  for (; cursor < len; ++cursor) {
    const char c = src[cursor];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
      continue;
    }
    if (c == '"') {
      quoted = true;
      continue;
    }
    if (c == '{') ++depth;
    else if (c == '}') {
      --depth;
      if (depth == 0) {
        out = src.substring(start, cursor + 1);
        ++cursor;
        return true;
      }
    }
  }
  return false;
}

String iso_date_part(const String& iso) {
  if (iso.length() >= 10) return iso.substring(0, 10);
  return String();
}

int iso_hour_part(const String& iso) {
  if (iso.length() >= 13) return iso.substring(11, 13).toInt();
  return -1;
}

String iso_add_days(const String& iso, int days) {
  if (iso.length() < 10) return String();
  int y = 0, m = 0, d = 0;
  if (sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return String();
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d + days;
  t.tm_hour = 12;
  const time_t value = mktime(&t);
  if (value < 0) return String();
  struct tm out = {};
  if (!localtime_r(&value, &out)) return String();
  char out_date[11] = {};
  if (strftime(out_date, sizeof(out_date), "%Y-%m-%d", &out) == 0) return String();
  return String(out_date);
}

bool local_now(String& today, int& hour, float& pos) {
  const time_t now = time(nullptr);
  if (now < 1704067200) return false;
  struct tm local = {};
  if (!localtime_r(&now, &local)) return false;
  char date[11] = {};
  if (strftime(date, sizeof(date), "%Y-%m-%d", &local) == 0) return false;
  today = date;
  hour = local.tm_hour;
  pos = static_cast<float>(hour) + local.tm_min / 60.0f;
  return true;
}

String lower(String value) {
  value.toLowerCase();
  return value;
}

void parse_graph_data_impl(const String& payload, GraphData& out,
                           bool keep_pair_only) {
  out = GraphData{};
  out.today = "";
  out.now_hour = 0;
  out.has_time = local_now(out.today, out.now_hour, out.now_pos);
  // The pair only ever draws today and tomorrow. A payload can carry more
  // (history of yesterday, a third forecast day); those entries filled
  // kMaxParsedHours before tomorrow was reached, so the tomorrow half drew
  // nothing while the Web Admin preview, which has no such limit, showed it.
  String pair_tomorrow;
  if (out.has_time && out.today.length()) pair_tomorrow = iso_add_days(out.today, 1);

  int array_start = find_array(payload, "forecast_hourly");
  if (array_start < 0) return;
  int cursor = array_start;
  String object;
  while (out.count < kMaxParsedHours && next_object(payload, cursor, object)) {
    GraphHour hour;
    String date;
    float hour_value = -1.0f;
    String datetime;
    json_string(object, "date_local", date);
    if (!date.length()) json_string(object, "d", date);
    json_string(object, "datetime", datetime);
    if (!date.length() && datetime.length()) date = iso_date_part(datetime);
    if (!json_number(object, "hour_local", hour_value)) {
      json_number(object, "h", hour_value);
    }
    if (hour_value < 0.0f && datetime.length()) {
      hour_value = static_cast<float>(iso_hour_part(datetime));
    }
    if (!date.length() || hour_value < 0.0f) continue;
    // Keep the two drawn days only; without a clock the dates cannot be
    // judged and every entry is parsed as before.
    if (keep_pair_only && out.has_time && date != out.today &&
        date != pair_tomorrow) {
      continue;
    }
    hour.date = date;
    hour.hour = static_cast<int>(lroundf(hour_value));
    if (hour.hour < 0 || hour.hour > 23) continue;
    float temperature = 0.0f;
    if (json_number(object, "temperature", temperature) ||
        json_number(object, "t", temperature) ||
        json_number(object, "native_temperature", temperature)) {
      hour.temperature = temperature;
      hour.has_temperature = true;
    }
    // The same key order as the popup's resolve_weather_visual_fields: the
    // bridge may flatten the hourly entries to "c"/"i".
    if (!json_string(object, "condition", hour.condition)) {
      if (!json_string(object, "c", hour.condition)) {
        json_string(object, "state", hour.condition);
      }
    }
    if (!json_string(object, "icon", hour.icon)) {
      json_string(object, "i", hour.icon);
    }
    float precipitation = 0.0f;
    if (json_number(object, "precipitation", precipitation) ||
        json_number(object, "p", precipitation)) {
      hour.precipitation = precipitation;
      hour.has_precipitation = precipitation > 0.0f;
    }
    String src;
    if (json_string(object, "src", src)) {
      src.toLowerCase();
      hour.is_hist = src == "hist";
    }
    if (!out.has_time && !out.today.length()) {
      out.today = date;
      out.now_hour = hour.hour;
    }
    // Merge duplicates of the same date+hour: hist fills the past, forecast
    // covers the present and future. Unique slots stay within the 24+24 day
    // pair, so the merged set never exceeds kMaxParsedHours.
    int existing = -1;
    for (uint8_t k = 0; k < out.count; ++k) {
      if (out.hours[k].date == date && out.hours[k].hour == hour.hour) {
        existing = k;
        break;
      }
    }
    if (existing < 0) {
      if (out.count < kMaxParsedHours) out.hours[out.count++] = hour;
      continue;
    }
    GraphHour& old = out.hours[existing];
    if (hour.is_hist != old.is_hist) {
      bool past = false;
      if (out.has_time && out.today.length()) {
        const int cmp = strcmp(date.c_str(), out.today.c_str());
        past = cmp < 0 || (cmp == 0 && hour.hour < out.now_hour);
      }
      if (hour.is_hist && !old.is_hist) {
        if (past) old = hour;
      } else if (!hour.is_hist && old.is_hist) {
        if (!past) old = hour;
      }
      continue;
    }
    // Same source twice: fill what the first entry missed, keep the first
    // otherwise so the result stays deterministic.
    if (!old.has_temperature && hour.has_temperature) {
      old = hour;
    } else if (!old.condition.length() && hour.condition.length()) {
      old.condition = hour.condition;
      old.icon = hour.icon;
      old.precipitation = hour.precipitation;
      old.has_precipitation = hour.has_precipitation;
    }
  }
  if (!out.today.length() && out.count) {
    out.today = out.hours[0].date;
  }
  out.tomorrow = iso_add_days(out.today, 1);
}

// The pair's parse: only the two drawn days, so yesterday's history or a third
// forecast day cannot fill kMaxParsedHours before tomorrow is reached. A
// payload whose dates do not match the clock keeps the old behaviour and
// parses everything rather than drawing nothing.
void parse_graph_data(const String& payload, GraphData& out) {
  parse_graph_data_impl(payload, out, true);
  if (!out.count) parse_graph_data_impl(payload, out, false);
}

bool is_today_or_tomorrow(const GraphData& data, const GraphHour& hour) {
  // Both halves show the whole day, so the shared scale covers every hour the
  // pair can draw.
  if (!data.today.length()) return true;
  if (hour.date == data.today) return true;
  return data.tomorrow.length() && hour.date == data.tomorrow;
}

void compute_scale(const GraphData& data, const char* entity_id, int32_t& min_tenths,
                   int32_t& max_tenths, bool& has_temp) {
  float low = 0.0f;
  float high = 0.0f;
  has_temp = false;
  for (uint8_t i = 0; i < data.count; ++i) {
    const GraphHour& hour = data.hours[i];
    if (!hour.has_temperature || !is_today_or_tomorrow(data, hour)) continue;
    if (!has_temp) {
      low = high = hour.temperature;
      has_temp = true;
    } else {
      low = std::min(low, hour.temperature);
      high = std::max(high, hour.temperature);
    }
  }
  // The cached past hours join the shared range too, so both halves keep one
  // Y scale once the day fills in behind the forecast.
  if (entity_id && entity_id[0] && data.today.length()) {
    for (int h = 0; h < kMaxGraphHours; ++h) {
      float cached = 0.0f;
      if (!weather_history::cached_temperature(entity_id, data.today.c_str(), h, cached)) continue;
      if (!has_temp) {
        low = high = cached;
        has_temp = true;
      } else {
        low = std::min(low, cached);
        high = std::max(high, cached);
      }
    }
  }
  if (!has_temp) {
    min_tenths = 0;
    max_tenths = 300;
    return;
  }
  float span = high - low;
  if (span < 2.0f) span = 2.0f;
  low -= std::max(1.0f, span * 0.15f);
  high += std::max(1.0f, span * 0.15f);
  min_tenths = static_cast<int32_t>(lroundf(low * 10.0f));
  max_tenths = static_cast<int32_t>(lroundf(high * 10.0f));
  if (max_tenths <= min_tenths) max_tenths = min_tenths + 10;
}

uint32_t fnv1a(const char* text) {
  uint32_t hash = 2166136261u;
  if (!text) return hash;
  for (const char* p = text; *p; ++p) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 16777619u;
  }
  return hash;
}

const char* day_label_for(const Tile& tile) {
  const char* language = configManager.getConfig().language;
  return tile.sensor_display_mode == 1 ? i18n::weather_tomorrow_label(language)
                                       : i18n::weather_today_label(language);
}

String graph_title(const Tile& tile) {
  // The tile's own name when it has one, so the two halves of a pair may be
  // named differently; an unnamed half shows the day alone, "Today" and
  // "Tomorrow", with no entity prefix (user 2026-11-04).
  if (tile.title.length()) return tile.title;
  return String(day_label_for(tile));
}

lv_color_t condition_color(const GraphHour& hour) {
  const String condition = lower(hour.condition);
  const String icon = lower(hour.icon);
  const String both = condition + " " + icon;
  if (both.indexOf("thunder") >= 0 || both.indexOf("lightning") >= 0) {
    return lv_color_hex(0x9B5CFF);
  }
  if (both.indexOf("hail") >= 0) return lv_color_hex(0xD85CFF);
  if (both.indexOf("snow") >= 0 || both.indexOf("sleet") >= 0) {
    return lv_color_hex(0x8FD3FF);
  }
  if (both.indexOf("rain") >= 0 || both.indexOf("showers") >= 0 ||
      both.indexOf("pouring") >= 0) {
    return lv_color_hex(0x3D8BFD);
  }
  if (both.indexOf("fog") >= 0) return lv_color_hex(0x7A7A7A);
  // No rain/snow wording, but the payload counts millimetres: rain by number,
  // so the cell colour and the episode label always agree.
  if (hour.has_precipitation) return lv_color_hex(0x3D8BFD);
  if (both.indexOf("cloudy") >= 0 || both.indexOf("overcast") >= 0) {
    return lv_color_hex(0x4A4A4A);
  }
  return lv_color_hex(0x2E2E2E);
}

void set_text_if_changed(lv_obj_t* label, const String& text) {
  if (!label) return;
  const char* shown = lv_label_get_text(label);
  if (!shown || text != shown) lv_label_set_text(label, text.c_str());
}

// The precipitation family an hour belongs to, as an i18n condition key
// (weather_condition_label): rain, snow, thunderstorm or hail. Null for dry
// hours; the same order as condition_color, so cell and label agree.
const char* precipitation_group(const GraphHour& hour) {
  const String both = lower(hour.condition) + " " + lower(hour.icon);
  if (both.indexOf("thunder") >= 0 || both.indexOf("lightning") >= 0) return "lightning";
  if (both.indexOf("hail") >= 0) return "hail";
  if (both.indexOf("snow") >= 0 || both.indexOf("sleet") >= 0) return "snowy";
  if (both.indexOf("rain") >= 0 || both.indexOf("showers") >= 0 ||
      both.indexOf("drizzle") >= 0 || both.indexOf("pouring") >= 0) {
    return "rainy";
  }
  // No condition at all, but the payload counts millimetres: rain by number.
  if (hour.has_precipitation) return "rainy";
  return nullptr;
}

// The graph's place inside the tile's content area. Derived from the stored
// tile geometry alone, never from live object coordinates: an update that
// runs right after a render would otherwise read a not-yet-laid-out card and
// clamp the chart to its minimum size.
struct GraphBox {
  lv_coord_t graph_x = 0;
  lv_coord_t graph_y = 0;
  lv_coord_t graph_w = 0;
  lv_coord_t graph_h = 0;
  lv_coord_t label_h = 0;
  lv_coord_t value_label_h = 0;
  lv_coord_t band_h = 0;
  lv_coord_t band_y = 0;
};

GraphBox graph_box(int tile_w, int tile_h) {
  const lv_coord_t pad_hor = tile_layout::scale_480(20);
  const lv_coord_t pad_ver = tile_layout::scale_480(24);
  const lv_coord_t content_w = tile_w - 2 * pad_hor;
  const lv_coord_t content_h = tile_h - 2 * pad_ver;
  const lv_coord_t title_h = lv_font_get_line_height(tile_layout::header_title_font());
  const lv_coord_t label_h = lv_font_get_line_height(FONT_SMALL);
  // The band carries the precipitation names, so it fits one small text line.
  const lv_coord_t band_h = label_h + tile_layout::scale_480(4);
  GraphBox box;
  // No left axis: the min/max readouts are gone, the graph takes the width.
  box.graph_x = 0;
  box.graph_w = std::max<lv_coord_t>(20, content_w);
  box.graph_y = title_h + tile_layout::scale_480(4);
  box.graph_h = std::max<lv_coord_t>(
      30, content_h - box.graph_y - label_h - tile_layout::scale_480(4) - band_h);
  box.label_h = label_h;
  box.value_label_h = lv_font_get_line_height(FONT_VALUE);
  box.band_h = band_h;
  box.band_y = box.graph_y + box.graph_h + label_h + tile_layout::scale_480(4);
  return box;
}

void layout_graph(WeatherGraphWidgets& widgets, lv_obj_t* card, int tile_w, int tile_h,
                  uint8_t count) {
  if (!widgets.chart || !widgets.band) return;
  const GraphBox box = graph_box(tile_w, tile_h);
  const lv_coord_t graph_x = box.graph_x;
  const lv_coord_t graph_y = box.graph_y;
  const lv_coord_t graph_w = box.graph_w;
  const lv_coord_t graph_h = box.graph_h;
  const lv_coord_t band_h = box.band_h;
  (void)card;
  lv_obj_set_pos(widgets.chart, graph_x, graph_y);
  lv_obj_set_size(widgets.chart, graph_w, graph_h);
  const lv_coord_t labels_y = graph_y + graph_h + tile_layout::scale_480(2);
  const lv_coord_t label_w = tile_layout::scale_480(36);
  // The hour marks sit under their slot of the 00-23 axis, every three hours.
  for (uint8_t i = 0; i < kHourLabels; ++i) {
    lv_obj_t* label = widgets.hour_labels[i];
    if (!label) continue;
    const lv_coord_t point_x = count > 1
        ? static_cast<lv_coord_t>((graph_w * (i * kHourStep)) / (count - 1)) : 0;
    lv_coord_t x = graph_x + point_x - label_w / 2;
    if (x < 0) x = 0;
    if (x + label_w > graph_x + graph_w) x = graph_x + graph_w - label_w;
    lv_obj_set_pos(label, x, labels_y);
    lv_obj_set_width(label, label_w);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    if (count == 0) lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_pos(widgets.band, graph_x, box.band_y);
  lv_obj_set_size(widgets.band, graph_w, band_h);
  const lv_coord_t cell_w = count ? graph_w / count : graph_w;
  for (uint8_t i = 0; i < kBandCells; ++i) {
    lv_obj_t* cell = widgets.band_cells[i];
    if (!cell) continue;
    if (i >= count) {
      lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(cell, i * cell_w, 0);
    lv_obj_set_size(cell, cell_w + 1, band_h);
  }
  for (uint8_t i = 0; i < kTempLabels; ++i) {
    if (widgets.temp_labels[i] && count == 0) {
      lv_obj_add_flag(widgets.temp_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  for (uint8_t i = 0; i < kBandEpisodeLabels; ++i) {
    if (widgets.band_labels[i] && count == 0) {
      lv_obj_add_flag(widgets.band_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (widgets.placeholder) {
    lv_obj_set_pos(widgets.placeholder, graph_x, graph_y);
    lv_obj_set_size(widgets.placeholder, graph_w, graph_h);
  }
}

}  // namespace

// The card only gets its real size in the layout pass, after the first paint
// has used the stored geometry (the screensaver's shown place differs from the
// stored one, so that geometry is wrong there). Repaint from the cache when
// the size arrives, or the graph keeps the size it was built with (user
// 2026-11-05).
struct GraphCardRef {
  GridType grid_type;
  uint8_t index;
};

namespace {

void on_graph_card_resized(lv_event_t* e) {
  auto* ref = static_cast<GraphCardRef*>(lv_event_get_user_data(e));
  if (!ref) return;
  lv_obj_t* card = static_cast<lv_obj_t*>(lv_event_get_target(e));
  if (!card || lv_obj_get_width(card) <= 40 || lv_obj_get_height(card) <= 40) {
    return;
  }
  const Tile* tile = tile_renderer_get_tile_config(ref->grid_type, ref->index);
  if (!tile || !tile->sensor_entity.length()) return;
  String payload;
  if (!tiles_get_cached_entity_payload(tile->sensor_entity.c_str(), payload) ||
      !payload.length()) {
    return;
  }
  WeatherGraphWidgets* target =
      tile_renderer_get_weather_graph_widgets(ref->grid_type);
  if (!target || ref->index >= TILES_PER_GRID) return;
  // Force the redraw: the payload may be unchanged, but its layout was made
  // for the geometry the card had when it was built.
  target[ref->index].last_hash = 0;
  update_weather_graph_tile_state(ref->grid_type, ref->index, payload.c_str());
}

void free_graph_card_ref(lv_event_t* e) {
  delete static_cast<GraphCardRef*>(lv_event_get_user_data(e));
}

}  // namespace

lv_obj_t* render_weather_graph_tile(lv_obj_t* parent, int col, int row,
                                    const Tile& tile, uint8_t index,
                                    GridType grid_type) {
  if (!parent) return nullptr;
  const lv_coord_t tile_w = tile_geometry::extent(
      tile.col, tile.span_w < 1 ? 1.0f : tile.span_w, GRID_CELL_W, GRID_GAP);
  const lv_coord_t tile_h = tile_geometry::extent(
      tile.row, tile.span_h < 1 ? 1.0f : tile.span_h, GRID_CELL_H, GRID_GAP);

  lv_obj_t* card = lv_button_create(parent);
  if (!card) return nullptr;
  const uint32_t card_color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_hor(card, tile_layout::scale_480(20), 0);
  lv_obj_set_style_pad_ver(card, tile_layout::scale_480(24), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);
  place_tile_card(card, col, row, tile);

  WeatherGraphWidgets* target = tile_renderer_get_weather_graph_widgets(grid_type);
  if (!target || index >= TILES_PER_GRID) return card;
  WeatherGraphWidgets& widgets = target[index];
  widgets = WeatherGraphWidgets{};

  widgets.title_label = lv_label_create(card);
  set_label_style(widgets.title_label, lv_color_hex(ui_theme::text()),
                  tile_layout::header_title_font());
  lv_label_set_long_mode(widgets.title_label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(widgets.title_label, LV_PCT(100));
  lv_obj_set_pos(widgets.title_label, 0, 0);
  hometiles_title::tile(widgets.title_label, graph_title(tile).c_str(), true);

  widgets.chart = lv_chart_create(card);
  if (widgets.chart) {
    lv_chart_set_type(widgets.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(widgets.chart, 2);
    lv_chart_set_range(widgets.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(widgets.chart, 0, 0);
    lv_chart_set_update_mode(widgets.chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_opa(widgets.chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(widgets.chart, 0, 0);
    lv_obj_set_style_shadow_width(widgets.chart, 0, 0);
    lv_obj_set_style_radius(widgets.chart, 0, 0);
    lv_obj_set_style_pad_all(widgets.chart, 0, 0);
    lv_obj_set_style_line_width(widgets.chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(widgets.chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(widgets.chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(widgets.chart, LV_OBJ_FLAG_SCROLLABLE);
    widgets.series = lv_chart_add_series(widgets.chart,
                                        lv_color_hex(ui_theme::icon()),
                                        LV_CHART_AXIS_PRIMARY_Y);
    if (widgets.series) {
      lv_chart_set_all_value(widgets.chart, widgets.series, LV_CHART_POINT_NONE);
    }
    // The current hour on the today half: a dashed vertical line with its dot
    // on the curve (lv_chart.c draw_cursors reads the styles of
    // LV_PART_CURSOR; the line colour comes from the cursor itself).
    widgets.now_cursor = lv_chart_add_cursor(
        widgets.chart, lv_color_hex(ui_theme::text()),
        static_cast<lv_dir_t>(LV_DIR_VER | LV_DIR_TOP | LV_DIR_BOTTOM));
    lv_obj_set_style_line_width(widgets.chart, 2, LV_PART_CURSOR);
    lv_obj_set_style_line_dash_width(widgets.chart, 6, LV_PART_CURSOR);
    lv_obj_set_style_line_dash_gap(widgets.chart, 4, LV_PART_CURSOR);
    lv_obj_set_style_line_opa(widgets.chart, LV_OPA_70, LV_PART_CURSOR);
    lv_obj_set_style_width(widgets.chart, 10, LV_PART_CURSOR);
    lv_obj_set_style_radius(widgets.chart, LV_RADIUS_CIRCLE, LV_PART_CURSOR);
    lv_obj_set_style_bg_color(widgets.chart, lv_color_hex(ui_theme::text()),
                              LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(widgets.chart, LV_OPA_COVER, LV_PART_CURSOR);
  }

  widgets.placeholder = lv_label_create(card);
  set_label_style(widgets.placeholder, lv_color_hex(ui_theme::text()), FONT_TITLE);
  lv_label_set_text(widgets.placeholder, "--");
  lv_obj_set_style_text_align(widgets.placeholder, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(widgets.placeholder, LV_OBJ_FLAG_HIDDEN);

  for (uint8_t i = 0; i < kHourLabels; ++i) {
    widgets.hour_labels[i] = lv_label_create(card);
    set_label_style(widgets.hour_labels[i], lv_color_hex(ui_theme::text()), FONT_SMALL);
    lv_label_set_text(widgets.hour_labels[i], "");
  }

  for (uint8_t i = 0; i < kTempLabels; ++i) {
    widgets.temp_labels[i] = lv_label_create(card);
    set_label_style(widgets.temp_labels[i], lv_color_hex(ui_theme::text()), FONT_VALUE);
    lv_label_set_long_mode(widgets.temp_labels[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(widgets.temp_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(widgets.temp_labels[i], "");
    lv_obj_add_flag(widgets.temp_labels[i], LV_OBJ_FLAG_HIDDEN);
  }

  widgets.band = lv_obj_create(card);
  if (widgets.band) {
    lv_obj_remove_style_all(widgets.band);
    lv_obj_set_style_bg_opa(widgets.band, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(widgets.band, 0, 0);
    lv_obj_set_style_border_width(widgets.band, 0, 0);
    lv_obj_set_style_radius(widgets.band, 0, 0);
    lv_obj_remove_flag(widgets.band, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(widgets.band, LV_OBJ_FLAG_SCROLLABLE);
    for (uint8_t i = 0; i < kBandCells; ++i) {
      widgets.band_cells[i] = lv_obj_create(widgets.band);
      lv_obj_remove_style_all(widgets.band_cells[i]);
      lv_obj_set_style_bg_opa(widgets.band_cells[i], LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(widgets.band_cells[i], 0, 0);
      lv_obj_set_style_radius(widgets.band_cells[i], 0, 0);
      lv_obj_remove_flag(widgets.band_cells[i], LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_flag(widgets.band_cells[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (uint8_t i = 0; i < kBandEpisodeLabels; ++i) {
      widgets.band_labels[i] = lv_label_create(widgets.band);
      set_label_style(widgets.band_labels[i], lv_color_hex(0x141414), FONT_SMALL);
      lv_label_set_long_mode(widgets.band_labels[i], LV_LABEL_LONG_DOT);
      lv_obj_set_style_text_align(widgets.band_labels[i], LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_text(widgets.band_labels[i], "");
      lv_obj_add_flag(widgets.band_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  layout_graph(widgets, card, tile_w, tile_h, 0);

  // The card's real size arrives with the layout pass; repaint the graph for
  // it, and free the reference with the card.
  GraphCardRef* ref = new GraphCardRef{grid_type, static_cast<uint8_t>(index)};
  lv_obj_add_event_cb(card, on_graph_card_resized, LV_EVENT_SIZE_CHANGED, ref);
  lv_obj_add_event_cb(card, free_graph_card_ref, LV_EVENT_DELETE, ref);

  // Paint from the payload already in the cache: the retained state can
  // arrive before this card exists (update_weather_graph_tile_state drops it
  // then) and the next forecast message is hours away, which left the tile at
  // "--" until the grid was rebuilt (user 2026-11-05).
  if (tile.sensor_entity.length()) {
    String payload;
    if (tiles_get_cached_entity_payload(tile.sensor_entity.c_str(), payload) &&
        payload.length()) {
      update_weather_graph_tile_state(grid_type, index, payload.c_str());
    }
  }
  return card;
}

void update_weather_graph_tile_state(GridType grid_type, uint8_t grid_index,
                                     const char* payload) {
  if (grid_index >= TILES_PER_GRID || !payload) return;
  WeatherGraphWidgets* target = tile_renderer_get_weather_graph_widgets(grid_type);
  if (!target) return;
  WeatherGraphWidgets& widgets = target[grid_index];
  if (!widgets.chart || !widgets.series) return;
  const uint32_t hash = fnv1a(payload);
  if (widgets.last_hash == hash) return;
  widgets.last_hash = hash;

  const Tile* tile = tile_renderer_get_tile_config(grid_type, grid_index);
  if (!tile) return;
  GraphData data;
  parse_graph_data(String(payload), data);
  const char* entity_id = tile->sensor_entity.c_str();
  // File every received hour: later today the missing past hours are filled
  // from here, keyed by entity+date+hour.
  if (tile->sensor_entity.length()) {
    for (uint8_t i = 0; i < data.count; ++i) {
      const GraphHour& hour = data.hours[i];
      if (hour.hour < 0 || hour.hour >= kMaxGraphHours || !hour.date.length()) continue;
      if (!hour.has_temperature && !hour.condition.length() && !hour.has_precipitation) continue;
      weather_history::Hour cached;
      cached.has_temperature = hour.has_temperature;
      cached.temperature = hour.temperature;
      strncpy(cached.condition, hour.condition.c_str(), sizeof(cached.condition) - 1);
      strncpy(cached.icon, hour.icon.c_str(), sizeof(cached.icon) - 1);
      cached.has_precipitation = hour.has_precipitation;
      cached.precipitation = hour.precipitation;
      weather_history::store_hour(entity_id, hour.date.c_str(), hour.hour, cached);
    }
  }
  int32_t min_tenths = 0, max_tenths = 300;
  bool has_temp = false;
  compute_scale(data, entity_id, min_tenths, max_tenths, has_temp);

  if (widgets.title_label) {
    const String title = graph_title(*tile);
    const char* shown = hometiles_title::text(widgets.title_label);
    if (!shown || title != shown) {
      hometiles_title::tile(widgets.title_label, title.c_str(), true);
    }
  }

  // The day's 24 hourly slots, 00 to 23, exactly like tomorrow: an hour the
  // payload does not carry is filled from the panel's own hourly cache
  // (forecasts received earlier today), so the past draws on the same axis
  // as the other half. Payload entries always win over the cache.
  GraphHour display[kMaxGraphHours];
  for (uint8_t i = 0; i < kMaxGraphHours; ++i) display[i] = GraphHour{};
  bool any = false;
  for (uint8_t i = 0; i < data.count; ++i) {
    const GraphHour& hour = data.hours[i];
    if (hour.hour < 0 || hour.hour >= kMaxGraphHours) continue;
    const bool is_today = hour.date == data.today;
    const bool is_tomorrow = data.tomorrow.length() && hour.date == data.tomorrow;
    if ((tile->sensor_display_mode == 1 && is_tomorrow) ||
        (tile->sensor_display_mode != 1 && is_today)) {
      display[hour.hour] = hour;
      any = true;
    }
  }
  const char* shown_date = (tile->sensor_display_mode == 1 && data.tomorrow.length())
                               ? data.tomorrow.c_str()
                               : data.today.c_str();
  if (shown_date[0]) {
    for (uint8_t h = 0; h < kMaxGraphHours; ++h) {
      if (display[h].hour >= 0) {
        // A payload entry without a temperature still takes the cached one.
        if (!display[h].has_temperature &&
            weather_history::cached_temperature(entity_id, shown_date, h,
                                                display[h].temperature)) {
          display[h].has_temperature = true;
        }
        continue;
      }
      weather_history::Hour cached;
      if (!weather_history::lookup_hour(entity_id, shown_date, h, cached)) continue;
      if (!cached.has_temperature && !cached.condition[0] && !cached.has_precipitation) continue;
      display[h].date = shown_date;
      display[h].hour = h;
      display[h].temperature = cached.temperature;
      display[h].has_temperature = cached.has_temperature;
      display[h].condition = cached.condition;
      display[h].icon = cached.icon;
      display[h].precipitation = cached.precipitation;
      display[h].has_precipitation = cached.has_precipitation;
      any = true;
    }
  }
  const uint8_t count = any ? kMaxGraphHours : 0;

  const lv_coord_t tile_w_cfg = tile_geometry::extent(
      tile->col, tile->span_w < 1 ? 1.0f : tile->span_w, GRID_CELL_W, GRID_GAP);
  const lv_coord_t tile_h_cfg = tile_geometry::extent(
      tile->row, tile->span_h < 1 ? 1.0f : tile->span_h, GRID_CELL_H, GRID_GAP);
  // The card's live size wins once LVGL laid it out: on the screensaver the
  // stored (classic) place can differ from the shown one, so the stored
  // geometry would draw the graph at the wrong size. Right after a render
  // the card may still be unlaid-out (near zero) — then stored geometry
  // applies, like on the first paint.
  lv_coord_t tile_w = tile_w_cfg;
  lv_coord_t tile_h = tile_h_cfg;
  if (widgets.chart) {
    lv_obj_t* card = lv_obj_get_parent(widgets.chart);
    if (card) {
      const lv_coord_t live_w = lv_obj_get_width(card);
      const lv_coord_t live_h = lv_obj_get_height(card);
      if (live_w > 40 && live_h > 40) {
        tile_w = live_w;
        tile_h = live_h;
      }
    }
  }

  const bool has_data = count > 0;
  if (widgets.chart) {
    if (has_data) lv_obj_remove_flag(widgets.chart, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(widgets.chart, LV_OBJ_FLAG_HIDDEN);
  }
  if (!has_data && widgets.now_cursor) {
    lv_chart_set_cursor_point(widgets.chart, widgets.now_cursor,
                              widgets.series, LV_CHART_POINT_NONE);
  }
  if (widgets.placeholder) {
    if (has_data) lv_obj_add_flag(widgets.placeholder, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(widgets.placeholder, LV_OBJ_FLAG_HIDDEN);
  }
  if (widgets.band) {
    if (has_data) lv_obj_remove_flag(widgets.band, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(widgets.band, LV_OBJ_FLAG_HIDDEN);
  }
  if (!has_data) {
    layout_graph(widgets, lv_obj_get_parent(widgets.chart), tile_w, tile_h, 0);
    return;
  }

  // The line is smooth: the chart only draws straight segments, so the day's
  // curve is sampled four times per hour with cosine easing between the hourly
  // points. Cosine stays inside [p1, p2], so unlike Catmull-Rom it never
  // overshoots on a step (6->7) and never dips below a flat run. Hour i keeps
  // index i * kCurveSub, exactly x = i / 23 of the width, so the readouts and
  // the hour marks still sit on their slots.
  auto slot_present = [&](uint8_t hour) {
    return hour < kMaxGraphHours && display[hour].hour >= 0 &&
           display[hour].has_temperature;
  };
  constexpr float kSmoothPi = 3.14159265f;
  int32_t sample_temp[kCurvePoints];
  bool sample_has[kCurvePoints];
  for (uint16_t i = 0; i < kCurvePoints; ++i) {
    sample_temp[i] = 0;
    sample_has[i] = false;
  }
  for (uint8_t hour = 0; hour < kMaxGraphHours; ++hour) {
    const uint16_t at = static_cast<uint16_t>(hour * kCurveSub);
    sample_has[at] = slot_present(hour);
    sample_temp[at] = static_cast<int32_t>(lroundf(display[hour].temperature * 10.0f));
    if (!slot_present(hour) || hour + 1 >= kMaxGraphHours ||
        !slot_present(static_cast<uint8_t>(hour + 1))) {
      continue;
    }
    const float p1 = display[hour].temperature;
    const float p2 = display[hour + 1].temperature;
    for (uint8_t s = 1; s < kCurveSub; ++s) {
      const float t = static_cast<float>(s) / kCurveSub;
      const float blend = 0.5f - 0.5f * cosf(kSmoothPi * t);
      const uint16_t idx = static_cast<uint16_t>(at + s);
      sample_has[idx] = true;
      sample_temp[idx] =
          static_cast<int32_t>(lroundf((p1 + (p2 - p1) * blend) * 10.0f));
    }
  }

  lv_chart_set_point_count(widgets.chart, kCurvePoints);
  lv_chart_set_range(widgets.chart, LV_CHART_AXIS_PRIMARY_Y, min_tenths, max_tenths);
  for (uint16_t i = 0; i < kCurvePoints; ++i) {
    const int32_t value = sample_has[i] ? sample_temp[i] : LV_CHART_POINT_NONE;
    lv_chart_set_value_by_id(widgets.chart, widgets.series, i, value);
  }
  lv_chart_refresh(widgets.chart);

  // The current hour: the today half only (the tomorrow half has no "now"),
  // and only when that hour has a temperature for the dot to sit on. The
  // cursor's dot takes its place from the series, so it follows the curve.
  if (widgets.now_cursor && widgets.series) {
    const bool show_now =
        data.has_time && tile->sensor_display_mode != 1 &&
        data.now_pos >= 0.0f &&
        static_cast<int>(data.now_pos) < kMaxGraphHours &&
        slot_present(static_cast<uint8_t>(data.now_pos));
    // The last hour keeps the last sample: index 4 * 23 = kCurvePoints - 1.
    const float now_h = data.now_pos > 23.0f ? 23.0f : data.now_pos;
    const uint32_t now_index = show_now
        ? static_cast<uint32_t>(lroundf(now_h * kCurveSub))
        : static_cast<uint32_t>(LV_CHART_POINT_NONE);
    lv_chart_set_cursor_point(widgets.chart, widgets.now_cursor,
                              widgets.series, now_index);
  }

  // Hour marks under the scale: 00, 03, ... 21.
  for (uint8_t i = 0; i < kHourLabels; ++i) {
    lv_obj_t* label = widgets.hour_labels[i];
    if (!label) continue;
    char text[8];
    snprintf(text, sizeof(text), "%02d", i * kHourStep);
    set_text_if_changed(label, String(text));
  }

  // A slot the payload did not carry stays a dry cell, so the band is whole.
  for (uint8_t i = 0; i < count; ++i) {
    lv_obj_t* cell = widgets.band_cells[i];
    if (!cell) continue;
    lv_obj_set_style_bg_color(cell, condition_color(display[i]), 0);
  }

  // Layout first: the readouts and episode labels use its geometry.
  layout_graph(widgets, lv_obj_get_parent(widgets.chart), tile_w, tile_h, count);
  const GraphBox box = graph_box(tile_w, tile_h);

  // The temperature above the line, every three hours. A readout sits above
  // its point and moves below it when the curve crosses its box.
  const lv_coord_t temp_w = tile_layout::scale_480(76);
  const lv_coord_t label_gap = tile_layout::scale_480(6);
  const int32_t span = max_tenths - min_tenths;
  auto sample_x_at = [&](uint16_t i) {
    return static_cast<lv_coord_t>(
        (static_cast<int32_t>(box.graph_w) * i) / (kCurvePoints - 1));
  };
  auto sample_y_at = [&](uint16_t i) -> lv_coord_t {
    if (!sample_has[i] || span <= 0) return box.graph_y;
    return box.graph_y + static_cast<lv_coord_t>(
        (static_cast<int32_t>(max_tenths - sample_temp[i]) * box.graph_h) / span);
  };
  // True when a drawn segment of the curve runs through the label's box. A
  // missing hour breaks it, like LV_CHART_POINT_NONE.
  auto line_crosses = [&](lv_coord_t x0, lv_coord_t x1, lv_coord_t y0, lv_coord_t y1) {
    for (uint16_t i = 0; i + 1 < kCurvePoints; ++i) {
      if (!sample_has[i] || !sample_has[i + 1]) continue;
      const lv_coord_t ax = sample_x_at(i);
      const lv_coord_t ay = sample_y_at(i);
      const lv_coord_t bx = sample_x_at(i + 1);
      const lv_coord_t by = sample_y_at(i + 1);
      const lv_coord_t ov_lo = std::max(std::min(ax, bx), x0);
      const lv_coord_t ov_hi = std::min(std::max(ax, bx), x1);
      if (ov_lo > ov_hi) continue;
      lv_coord_t ya = ay;
      lv_coord_t yb = by;
      if (bx != ax) {
        ya = ay + static_cast<lv_coord_t>(
            (static_cast<int32_t>(by - ay) * (ov_lo - ax)) / (bx - ax));
        yb = ay + static_cast<lv_coord_t>(
            (static_cast<int32_t>(by - ay) * (ov_hi - ax)) / (bx - ax));
      }
      const lv_coord_t lo = std::min(ya, yb);
      const lv_coord_t hi = std::max(ya, yb);
      if (hi < y0 || lo > y1) continue;
      return true;
    }
    return false;
  };
  uint8_t temp_index = 0;
  if (has_temp && span > 0) {
    for (uint8_t hour = 0; hour < kMaxGraphHours && temp_index < kTempLabels;
         hour += kHourStep) {
      if (!slot_present(hour)) continue;
      lv_obj_t* label = widgets.temp_labels[temp_index++];
      if (!label) break;
      const uint16_t at = static_cast<uint16_t>(hour) * kCurveSub;
      lv_obj_set_width(label, temp_w);
      lv_coord_t x = box.graph_x + sample_x_at(at) - temp_w / 2;
      if (x < 0) x = 0;
      if (x + temp_w > box.graph_x + box.graph_w) x = box.graph_x + box.graph_w - temp_w;
      lv_coord_t y = sample_y_at(at) - label_gap - box.value_label_h;
      if (y < box.graph_y) y = box.graph_y;
      if (line_crosses(x, x + temp_w, y, y + box.value_label_h)) {
        const lv_coord_t below = sample_y_at(at) + label_gap;
        if (below + box.value_label_h <= box.graph_y + box.graph_h) y = below;
      }
      lv_obj_set_pos(label, x, y);
      set_text_if_changed(label, String(static_cast<int>(lroundf(display[hour].temperature))) +
                                      "\xC2\xB0");
      lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
  }
  for (uint8_t i = temp_index; i < kTempLabels; ++i) {
    if (widgets.temp_labels[i]) lv_obj_add_flag(widgets.temp_labels[i], LV_OBJ_FLAG_HIDDEN);
  }

  // Contiguous precipitation episodes, named inside the condition band.
  const lv_coord_t cell_w = box.graph_w / count;
  const char* language = configManager.getConfig().language;
  uint8_t episode_index = 0;
  uint8_t episode_start = 0;
  const char* episode_group = precipitation_group(display[0]);
  auto close_episode = [&](uint8_t end) {
    if (episode_index >= kBandEpisodeLabels) return;
    if (!episode_group || end <= episode_start) return;
    lv_obj_t* label = widgets.band_labels[episode_index];
    if (!label) return;
    const String name = i18n::weather_condition_label(language, episode_group);
    lv_point_t text_size;
    lv_text_get_size(&text_size, name.c_str(), FONT_SMALL, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    const lv_coord_t episode_w = (end - episode_start) * cell_w;
    // A one-hour episode in a wide word stays a colored cell alone.
    if (text_size.x > episode_w + cell_w / 2) return;
    ++episode_index;
    const lv_coord_t label_w = std::max(episode_w, text_size.x + 2);
    lv_coord_t x = episode_start * cell_w + (episode_w - label_w) / 2;
    if (x < 0) x = 0;
    if (x + label_w > box.graph_w) x = box.graph_w - label_w;
    lv_obj_set_pos(label, x, 0);
    lv_obj_set_size(label, label_w, box.band_h);
    set_text_if_changed(label, name);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
  };
  for (uint8_t i = 1; i < count; ++i) {
    const char* group = precipitation_group(display[i]);
    if (group != episode_group) {
      close_episode(i);
      episode_start = i;
      episode_group = group;
    }
  }
  close_episode(count);
  for (uint8_t i = episode_index; i < kBandEpisodeLabels; ++i) {
    if (widgets.band_labels[i]) lv_obj_add_flag(widgets.band_labels[i], LV_OBJ_FLAG_HIDDEN);
  }
}

void weather_graph_scale(const char* payload, int32_t& min_tenths,
                         int32_t& max_tenths, bool& has_temp) {
  GraphData data;
  parse_graph_data(String(payload ? payload : ""), data);
  compute_scale(data, nullptr, min_tenths, max_tenths, has_temp);
}

namespace {

String tenths_str(float value) {
  int32_t tenths = static_cast<int32_t>(lroundf(value * 10.0f));
  const bool negative = tenths < 0;
  int32_t magnitude = negative ? -tenths : tenths;
  char buf[16];
  snprintf(buf, sizeof(buf), "%s%d.%d", negative ? "-" : "",
           static_cast<int>(magnitude / 10), static_cast<int>(magnitude % 10));
  return String(buf);
}

void append_json_escaped(String& out, const char* text) {
  if (!text) return;
  for (const char* p = text; *p; ++p) {
    if (*p == '"' || *p == '\\') out += '\\';
    out += *p;
  }
}

bool is_json_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Position of the ']' closing the array that starts right before `array_at`
// (the index after '['), or -1 when the brackets never balance.
int array_close(const String& src, int array_at) {
  const int len = static_cast<int>(src.length());
  int depth = 1;
  bool quoted = false;
  bool escaped = false;
  for (int i = array_at; i < len; ++i) {
    const char c = src[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
      continue;
    }
    if (c == '"') quoted = true;
    else if (c == '[') ++depth;
    else if (c == ']') {
      if (--depth == 0) return i;
    }
  }
  return -1;
}

}  // namespace

void weather_graph_append_cache(String& payload, const char* entity_id) {
  if (!entity_id || !entity_id[0] || !payload.length()) return;
  GraphData data;
  parse_graph_data(payload, data);
  if (!data.has_time || !data.today.length() || data.now_hour < 0 ||
      data.now_hour > kMaxGraphHours) {
    return;
  }
  uint32_t present = 0;
  for (uint8_t i = 0; i < data.count; ++i) {
    const GraphHour& hour = data.hours[i];
    if (hour.date == data.today && hour.hour >= 0 && hour.hour < kMaxGraphHours) {
      present |= (1UL << hour.hour);
    }
  }
  String extra;
  for (int h = 0; h < data.now_hour && h < kMaxGraphHours; ++h) {
    if (present & (1UL << h)) continue;
    weather_history::Hour cached;
    if (!weather_history::lookup_hour(entity_id, data.today.c_str(), h, cached)) continue;
    if (!cached.has_temperature && !cached.condition[0] && !cached.has_precipitation) continue;
    if (extra.length()) extra += ',';
    extra += "{\"date_local\":\"";
    extra += data.today;
    extra += "\",\"hour_local\":";
    extra += String(h);
    if (cached.has_temperature) {
      extra += ",\"temperature\":";
      extra += tenths_str(cached.temperature);
    }
    extra += ",\"condition\":\"";
    append_json_escaped(extra, cached.condition);
    extra += "\",\"icon\":\"";
    append_json_escaped(extra, cached.icon);
    extra += "\"";
    if (cached.has_precipitation) {
      extra += ",\"precipitation\":";
      extra += tenths_str(cached.precipitation);
    }
    extra += ",\"src\":\"hist\"}";
  }
  if (!extra.length()) return;
  const int array_at = find_array(payload, "forecast_hourly");
  if (array_at < 0) return;
  const int close_at = array_close(payload, array_at);
  if (close_at < 0) return;
  bool empty = true;
  for (int i = array_at; i < close_at; ++i) {
    if (!is_json_space(payload[i])) {
      empty = false;
      break;
    }
  }
  payload = payload.substring(0, close_at) + (empty ? "" : ",") + extra +
            payload.substring(close_at);
}

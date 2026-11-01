#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_rule_icon.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/types/sensor/renderer.h"
#include "src/types/sensor/gauge_range.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/shared/tone_color.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include <Arduino.h>
#include <lvgl_private.h>
#include "src/tiles/runtime/tile_graph_box.h"
#include "src/tiles/runtime/tile_header.h"
#include "src/types/climate/layout.h"
#include "src/types/value/value_control.h"
#include "src/ui/shared/area_graph.h"
#include "src/ui/shared/ui_theme.h"
#include "src/types/sensor/look_view.h"
#include "src/types/sensor/sensor_look.h"

static const lv_font_t* get_sensor_value_font(const Tile& tile) {
  return tile_layout::value_font_for_choice(tile.sensor_value_font, FONT_VALUE);
}

// The graph tile (mockup build/design-mockups/sensor-graph-2, user
// 2026-10-26): the Switch header beside the corner disc, the graph below it
// in the Switch bar's box over the rest of the tile (insets, the bar's
// radius at the bottom, an open top). The line in the icon's color, the area
// below it in the circle's color. The box is shared with the Energy tile's
// bars (tile_graph_box.h).
static lv_area_t sensor_graph_box(const Tile& tile) { return tile_graph_box::box(tile); }

// The graph's pictures (area_graph.h): the area and the line as A8 masks in
// the LVGL heap, drawn recolored in the circle's and the icon's current
// color; the line's points and its top per column beside them; the icon
// whose color the line takes. Owned by the chart and freed with it. The
// masks are computed again only when the line or the size change: `key`
// covers the chart's raw values, range, size and corner radius
// (area_graph::data_key), so an unchanged graph never asks lv_chart for its
// points (one padding lookup per point) while it redraws.
struct SensorGraphArea {
  lv_obj_t* icon = nullptr;
  uint8_t* masks = nullptr;  // the area's, then the line's
  size_t capacity = 0;
  int16_t* tops = nullptr;
  int32_t tops_capacity = 0;
  area_graph::Point* points = nullptr;
  int32_t points_capacity = 0;
  lv_image_dsc_t area = {};
  lv_image_dsc_t line = {};
  // The rows each mask uses (area_graph::rows_used): only they are drawn.
  area_graph::Rows rows;
  uint32_t key = 0;
  bool built = false;
};

static void set_mask(lv_image_dsc_t& image, uint8_t* pixels, int32_t width, int32_t height) {
  image.header.magic = LV_IMAGE_HEADER_MAGIC;
  image.header.cf = LV_COLOR_FORMAT_A8;
  image.header.w = width;
  image.header.h = height;
  image.header.stride = width;
  image.data = pixels;
  image.data_size = static_cast<size_t>(width) * static_cast<size_t>(height);
}

template <typename T>
static bool ensure_capacity(T*& buffer, int32_t& capacity, int32_t count) {
  if (count <= capacity) return true;
  lv_free(buffer);
  buffer = static_cast<T*>(lv_malloc(static_cast<size_t>(count) * sizeof(T)));
  capacity = buffer ? count : 0;
  return buffer != nullptr;
}

// Before lv_chart draws (its line is off, line width 0): the area in the
// circle's color and the line in the icon's, two recolored images. The
// graph's frame is a resting surface, so tile_icon_source::refresh_controls
// gives it the control fill like the Switch bar's track; its local
// transparent background keeps it from filling the box. The box's round
// bottom corners are part of the masks (area_graph::round_bottom_corners):
// the frame's clip_corner drew the graph three times per redraw, twice into
// ARGB8888 layers that were masked and blended again (user 2026-10-28: graph
// tiles slowed the UI).
static void sensor_graph_area_cb(lv_event_t* e) {
  lv_obj_t* chart = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_chart_series_t* series = chart ? lv_chart_get_series_next(chart, nullptr) : nullptr;
  lv_obj_t* frame = chart ? lv_obj_get_parent(chart) : nullptr;
  auto* area = static_cast<SensorGraphArea*>(lv_event_get_user_data(e));
  if (!layer || !series || !frame || !area) return;
  const ui_theme::SurfaceScope surface = tile_surface::scope(chart);
  const lv_color_t line_color =
      area->icon ? lv_obj_get_style_text_color(area->icon, LV_PART_MAIN) : lv_color_hex(ui_theme::text());
  // Set while drawing (lv_chart_set_series_color would invalidate): the
  // color update path compares against it.
  series->color = line_color;
  lv_area_t coords;
  lv_obj_get_coords(chart, &coords);
  const int32_t width = lv_area_get_width(&coords), height = lv_area_get_height(&coords);
  const int32_t count = static_cast<int32_t>(lv_chart_get_point_count(chart));
  if (width < 2 || height < 2) return;
  const int32_t radius = std::min<int32_t>(lv_obj_get_style_radius(frame, LV_PART_MAIN), std::min(width / 2, height));
  const uint32_t key = area_graph::data_key(
      chart, series, {static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(radius)});
  if (!area->built || key != area->key) {
    area->rows = {};
    if (!ensure_capacity(area->tops, area->tops_capacity, width) ||
        !ensure_capacity(area->points, area->points_capacity, count)) {
      return;
    }
    const int32_t points = area_graph::chart_points(chart, series, area->points, count);
    area->key = key;
    area->built = points > 0;
    if (!points) return;
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (2 * bytes > area->capacity) {
      if (area->masks) {
        lv_image_cache_drop(&area->area);
        lv_image_cache_drop(&area->line);
      }
      lv_free(area->masks);
      area->masks = static_cast<uint8_t*>(lv_malloc(2 * bytes));
      area->capacity = area->masks ? 2 * bytes : 0;
      if (!area->masks) {
        area->built = false;
        return;
      }
    }
    lv_image_cache_drop(&area->area);
    lv_image_cache_drop(&area->line);
    const float line_width = static_cast<float>(tile_layout::scale(3));
    area_graph::column_tops(area->points, points, area->tops, width);
    area_graph::render_mask(area->masks, static_cast<uint32_t>(width), width, height, area->tops);
    area_graph::line_mask(area->masks + bytes, static_cast<uint32_t>(width), width, height, area->points, points,
                          line_width);
    area_graph::round_bottom_corners(area->masks, static_cast<uint32_t>(width), width, height, radius);
    area_graph::round_bottom_corners(area->masks + bytes, static_cast<uint32_t>(width), width, height, radius);
    area->rows = area_graph::rows_used(area->points, points, height, line_width);
    set_mask(area->area, area->masks + area->rows.area_top * width, width, height - area->rows.area_top);
    set_mask(area->line, area->masks + bytes + area->rows.line_top * width, width, area->rows.line_rows);
  }
  if (area->rows.line_rows <= 0) return;
  lv_draw_image_dsc_t image;
  lv_draw_image_dsc_init(&image);
  image.base.layer = layer;
  image.src = &area->area;
  image.recolor = lv_obj_get_style_bg_color(frame, LV_PART_MAIN);
  image.recolor_opa = LV_OPA_COVER;
  image.opa =
      tone_color::control_opa(ui_surface_style::icon_glow_percent(), tile_icon_disc::see_through(lv_obj_get_parent(frame)));
  const lv_area_t area_at = {coords.x1, coords.y1 + area->rows.area_top, coords.x2, coords.y2};
  lv_draw_image(layer, &image, &area_at);
  image.src = &area->line;
  image.recolor = line_color;
  image.opa = LV_OPA_COVER;
  const lv_area_t line_at = {coords.x1, coords.y1 + area->rows.line_top, coords.x2,
                             coords.y1 + area->rows.line_top + area->rows.line_rows - 1};
  lv_draw_image(layer, &image, &line_at);
  // The pictures are the whole graph: lv_chart's own pass only walked the
  // points, and with more points than columns it writes past its buffer
  // (sensor_popup.cpp on_chart_area_draw).
  lv_event_stop_processing(e);
}

static void sensor_graph_delete_cb(lv_event_t* e) {
  auto* area = static_cast<SensorGraphArea*>(lv_event_get_user_data(e));
  if (!area) return;
  if (area->masks) {
    lv_image_cache_drop(&area->area);
    lv_image_cache_drop(&area->line);
  }
  lv_free(area->masks);
  lv_free(area->tops);
  lv_free(area->points);
  delete area;
}

// The head of the looks with the value inside their arc or ring
// (sensor_look::value_inside, lab B/G): the title alone beside the corner
// disc, right-aligned like the Switch tile's tall title, up to two lines
// centred on the disc.
static lv_obj_t* create_title_head(lv_obj_t* card, const Tile& tile) {
  if (!tile.title.length()) return nullptr;
  const int card_w = tile_geometry::extent(tile.col, std::max(1.0f, tile.span_w), GRID_CELL_W, GRID_GAP);
  const int inset = tile_icon_disc::inset();
  const int icon_width =
      FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0) : 0;
  const int disc = tile_icon_disc::header_diameter(icon_width);
  const int text_x = inset + disc + 2 * inset;
  const int pad_x = lv_obj_get_style_pad_left(card, LV_PART_MAIN);
  const int pad_y = lv_obj_get_style_pad_top(card, LV_PART_MAIN);
  // The right edge of the Switch tall title (tile_header.h).
  const int right = pad_x - tile_layout::scale_480(4);
  const lv_font_t* font = tile_layout::header_title_font();
  lv_obj_t* title = lv_label_create(card);
  if (!title) return nullptr;
  set_label_style(title, lv_color_hex(ui_theme::text()), font);
  lv_obj_set_width(title, std::max(1, card_w - text_x - right));
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_RIGHT, 0);
  // Top-aligned titles grow upwards by half a line for their second line,
  // so one or two lines stay centred on the disc.
  hometiles_title::tile(title, tile.title.c_str(), true);
  lv_obj_set_pos(title, text_x - pad_x, inset + disc / 2 - lv_font_get_line_height(font) / 2 - pad_y);
  lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);
  return title;
}

struct SensorEventData {
  String entity_id;
  String title;
  String icon_name;
  bool icon_override = false;
  String unit;
  uint8_t decimals = 0xFF;
  uint32_t bg_color = 0;
  bool editable = false;
  // Per-tile icon colors for the popup header icon.
  String icon_colors;
};

static bool is_disabled_token(const String& value);

// The unit the popup shows: the tile's own (kept as set), none when the tile
// switched it off, else Home Assistant's.
static String popup_unit(const SensorEventData& data) {
  String unit = data.unit;
  if (is_disabled_token(unit)) return String();
  if (!unit.length()) return haBridgeConfig.findSensorUnit(data.entity_id);
  unit.trim();
  return unit;
}

// Whether the popup shows the state history instead of the numeric graph.
static bool popup_state_history(const String& entity_id, const String& value, const String& unit) {
  String state_kind = haBridgeConfig.findSensorStateKind(entity_id);
  state_kind.trim();
  state_kind.toLowerCase();
  if (state_kind == "state") return true;
  if (state_kind == "number") return false;
  // Backward compatibility with older Bridge metadata.
  return sensor_popup_should_use_state_history(value, unit);
}

static void sensor_popup_click_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
  SensorEventData* data = static_cast<SensorEventData*>(lv_event_get_user_data(e));
  if (!data || !data->entity_id.length()) return;
  SensorPopupInit init;
  init.entity_id = data->entity_id;
  init.title = data->title;
  if (!init.title.length()) {
    init.title = haBridgeConfig.findSensorName(data->entity_id);
  }
  if (!init.title.length()) {
    init.title = data->entity_id;
  }
  String icon_name = data->icon_name;
  if (!data->icon_override) {
    String resolved = normalizeMdiIconName(haBridgeConfig.findEntityIcon(data->entity_id));
    if (resolved.length()) {
      icon_name = resolved;
    }
  }
  init.icon_name = icon_name;
  init.binary_icon_override = data->icon_override;
  init.unit = popup_unit(*data);
  init.lock_unit = data->unit.length() > 0;
  init.decimals = data->decimals;
  // The popup inherits the tile background, including a rules tint.
  init.bg_color = tile_icon_source::popup_background(static_cast<lv_obj_t*>(lv_event_get_current_target(e)), data->bg_color);
  init.value = haBridgeConfig.findSensorInitialValue(data->entity_id);
  init.state_history_mode = popup_state_history(data->entity_id, init.value, init.unit);
  init.editable = data->editable;
  init.icon_colors = data->icon_colors;
  if (init.editable) {
    const EditableValue value = parse_editable_value(haBridgeConfig.findEditableValue(data->entity_id));
    init.value = value.state; init.unit = value.unit;
    init.state_history_mode = value.kind != "number";
  }
  // Rules that force the tile icon color also color the header icon,
  // and show their own icon there.
  lv_color_t forced;
  lv_obj_t* tile_icon = tile_icon_source::card_icon(static_cast<lv_obj_t*>(lv_event_get_current_target(e)));
  if (tile_icon_disc::forced_color(tile_icon, forced)) {
    init.forced_icon = true;
    init.forced_icon_color = lv_color_to_u32(forced) & 0xFFFFFF;
    tile_rule_icon::shown(tile_icon, init.forced_icon_glyph);
  }
  finish_press_before_popup(e);
  show_sensor_popup(init);
}

void sensor_tile_touch_down(lv_obj_t* pressed) {
  for (lv_obj_t* obj = pressed; obj; obj = lv_obj_get_parent(obj)) {
    const uint32_t count = lv_obj_get_event_count(obj);
    for (uint32_t i = 0; i < count; ++i) {
      lv_event_dsc_t* dsc = lv_obj_get_event_dsc(obj, i);
      if (!dsc || lv_event_dsc_get_cb(dsc) != sensor_popup_click_cb) continue;
      const SensorEventData* data = static_cast<const SensorEventData*>(lv_event_dsc_get_user_data(dsc));
      // Only a numeric graph: editable values and state histories keep
      // asking when their popup opens (sensor_popup.cpp request_history_early).
      if (!data || !data->entity_id.length() || data->editable ||
          popup_state_history(data->entity_id, haBridgeConfig.findSensorInitialValue(data->entity_id),
                              popup_unit(*data))) {
        return;
      }
      sensor_popup_prefetch_history(data->entity_id);
      return;
    }
  }
}

static bool is_disabled_token(const String& value) {
  if (!value.length()) return false;
  String t = value;
  t.trim();
  if (!t.length()) return true;
  t.toLowerCase();
  return t == "-" || t == "none" || t == "null" || t == "no" || t == "off";
}

lv_obj_t* render_sensor_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type) {
  if (!parent) {
    Serial.println("[TileRenderer] ERROR: parent NULL for sensor tile");
    return nullptr;
  }

  // 0 the value, 1 the gauge, 2 the graph, then the looks (sensor_look.h).
  const uint8_t display_mode = tile.span_h == 0.5f ? 0 : tile.sensor_display_mode;
  const bool gauge_enabled = (display_mode == 1);
  const bool graph_enabled = (display_mode == 2);
  const bool look_enabled = sensor_look::is_look(display_mode);
  // The bounds with their decimals (gauge_range.h, issue #83).
  float gauge_min = sensor_gauge::value(tile.sensor_gauge_min, tile.sensor_gauge_decimals);
  float gauge_max = sensor_gauge::value(tile.sensor_gauge_max, tile.sensor_gauge_decimals);
  if (gauge_max <= gauge_min) {
    gauge_min = 0;
    gauge_max = 100;
  }

  lv_obj_t* card = lv_button_create(parent);
  if (!card) {
    Serial.println("[TileRenderer] ERROR: Could not create sensor card");
    return nullptr;
  }

  // Use the configured color, else the global default tile color.
  uint32_t card_color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

  // Pressed state: 10% brighter.
  uint32_t pressed_color = tone_color::pressed_card(card_color);
  lv_obj_set_style_bg_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);

  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_hor(card, tile_layout::scale_480(20), 0);
  lv_obj_set_style_pad_ver(card, tile_layout::scale_480(24), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);

  place_tile_card(card, col, row, tile);

  // Optional right-aligned icon label when icon_name is set.
  lv_obj_t* icon_lbl = nullptr;
  String icon_name = tile.icon_name;
  bool icon_disabled = isMdiIconDisabled(icon_name);
  icon_name = normalizeMdiIconName(icon_name);
  if (!icon_disabled && !icon_name.length() && tile.sensor_entity.length()) {
    icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  if (icon_name.length() > 0 && FONT_MDI_ICONS != nullptr) {
    String iconChar = getMdiChar(icon_name);
    if (iconChar.length() > 0) {
      icon_lbl = lv_label_create(card);
      if (icon_lbl) {
        set_label_style(icon_lbl, lv_color_hex(ui_theme::icon()), FONT_MDI_ICONS);
        lv_label_set_text(icon_lbl, iconChar.c_str());
        lv_obj_align(icon_lbl, LV_ALIGN_TOP_LEFT,
                     tile_layout::scale_480(-8),
                     tile_layout::scale_480(-8));
      }
    }
  }

  lv_obj_t* title_label = nullptr;
  // Right-aligned title label, shown only when a title is set. The graph
  // tile's header (tile_header) and the looks make their own.
  if (tile.title.length() > 0 && !graph_enabled && !look_enabled) {
    title_label = lv_label_create(card);
    if (title_label) {
      set_label_style(title_label, lv_color_hex(ui_theme::text()),
                      tile_layout::header_title_font());
      lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
      lv_obj_set_width(title_label, LV_PCT(70));
      lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_RIGHT, 0);
      hometiles_title::tile(title_label, tile.title.c_str(), true);
      lv_obj_align(title_label, LV_ALIGN_TOP_RIGHT,
                   tile_layout::scale_480(4),
                   tile_layout::scale_480(4));
    }
  }

  // At half-step heights the arc and its value stay together: they share the
  // extra half row equally. Whole spans give 0 and stay pixel-identical.
  const lv_coord_t gauge_extra_h =
      tile.span_h >= 1
          ? tile_geometry::extent(tile.row, tile.span_h, GRID_CELL_H, GRID_GAP) -
                tile_geometry::extent(tile.row, std::floor(tile.span_h), GRID_CELL_H, GRID_GAP)
          : 0;
  const lv_coord_t gauge_shift = gauge_extra_h / 2;

  lv_obj_t* gauge = nullptr;
  if (gauge_enabled) {
    // Get gauge appearance from tile settings (with defaults)
    uint16_t arc_degrees = tile.sensor_gauge_arc;
    if (arc_degrees < 90) arc_degrees = 90;
    if (arc_degrees > 359) arc_degrees = 359;
    uint16_t gauge_size = tile.sensor_gauge_size;
    if (gauge_size < 100) gauge_size = 100;
    if (gauge_size > 800) gauge_size = 800;
    gauge_size = tile_layout::scale_u16(gauge_size);
    int16_t y_offset = tile.sensor_gauge_y_offset;
    if (y_offset < -100) y_offset = -100;
    if (y_offset > 200) y_offset = 200;
    y_offset = tile_layout::scale_i16(y_offset);
    // Calculate rotation so gap is always at bottom: rotation = 270 - arc/2
    uint16_t rotation = 270 - (arc_degrees / 2);

    gauge = lv_arc_create(card);
    if (gauge) {
      lv_obj_set_size(gauge, gauge_size, gauge_size);
      lv_obj_align(gauge, LV_ALIGN_TOP_MID, 0, y_offset + gauge_shift);
      lv_obj_remove_flag(gauge, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(gauge, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_bg_opa(gauge, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(gauge, 0, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(gauge, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(gauge, 0, LV_PART_MAIN);

      lv_arc_set_range(gauge, 0, GAUGE_ARC_STEPS);
      lv_arc_set_value(gauge, 0);
      lv_arc_set_rotation(gauge, rotation);
      lv_arc_set_bg_angles(gauge, 0, arc_degrees);
      lv_arc_set_angles(gauge, 0, arc_degrees);

      lv_obj_set_style_arc_width(gauge, tile_layout::scale(14),
                                 LV_PART_MAIN);
      lv_obj_set_style_arc_color(gauge, lv_color_hex(0x2E2E2E), LV_PART_MAIN);
      lv_obj_set_style_arc_rounded(gauge, true, LV_PART_MAIN);

      lv_obj_set_style_arc_width(gauge, tile_layout::scale(14),
                                 LV_PART_INDICATOR);
      lv_obj_set_style_arc_color(gauge, lv_color_hex(0x20A4FF), LV_PART_INDICATOR);
      lv_obj_set_style_arc_rounded(gauge, true, LV_PART_INDICATOR);

      lv_obj_set_style_bg_opa(gauge, LV_OPA_TRANSP, LV_PART_KNOB);
      lv_obj_set_style_border_opa(gauge, LV_OPA_TRANSP, LV_PART_KNOB);
      lv_obj_set_style_shadow_width(gauge, 0, LV_PART_KNOB);
    }
  }
  if (gauge_enabled) {
    if (icon_lbl) lv_obj_move_foreground(icon_lbl);
    if (title_label) lv_obj_move_foreground(title_label);
  }

  // The graph (display_mode 2) in its box below the header
  // (sensor_graph_box): the day's history from the Bridge, the line rounded
  // but cut flush at both ends by the chart's own bounds.
  lv_obj_t* chart = nullptr;
  lv_chart_series_t* series = nullptr;
  lv_obj_t* v = nullptr;
  if (graph_enabled) {
    const lv_area_t box = sensor_graph_box(tile);
    // The frame: a resting surface (the area's color, see
    // sensor_graph_area_cb) with the Switch bar's radius that never fills
    // itself; the graph's masks take its bottom corners, the top stays open.
    lv_obj_t* frame = lv_obj_create(card);
    if (frame) {
      tile_icon_disc::mark_surface(frame);
      lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(frame, 0, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(frame, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(frame, 0, LV_PART_MAIN);
      ui_surface_style::apply_radius(frame, climate_layout::kControlRadius, 0);
      lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
      // Positions inside the card's content box (its paddings).
      lv_obj_set_size(frame, lv_area_get_width(&box), lv_area_get_height(&box));
      lv_obj_set_pos(frame, box.x1 - tile_layout::scale_480(20), box.y1 - tile_layout::scale_480(24));
      chart = lv_chart_create(frame);
      if (chart) {
        lv_obj_set_size(chart, lv_area_get_width(&box), lv_area_get_height(&box));
        lv_obj_set_pos(chart, 0, 0);
      }
    }
    if (chart) {
      lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);

      lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
      lv_chart_set_point_count(chart, 288);  // Same as popup
      lv_chart_set_div_line_count(chart, 0, 0);
      lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

      lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(chart, 0, LV_PART_MAIN);
      lv_obj_set_style_radius(chart, 0, LV_PART_MAIN);
      // Room above the day's maximum and below its minimum for the line.
      lv_obj_set_style_pad_hor(chart, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_ver(chart, tile_layout::scale(8), LV_PART_MAIN);

      series = lv_chart_add_series(chart, lv_color_hex(ui_theme::icon()), LV_CHART_AXIS_PRIMARY_Y);
      // The line is one of the graph's pictures (sensor_graph_area_cb).
      lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);
      lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

      // Initialize with LV_CHART_POINT_NONE; history will replace it.
      lv_chart_set_all_value(chart, series, LV_CHART_POINT_NONE);
      auto* area = new SensorGraphArea();
      area->icon = icon_lbl;
      lv_obj_add_event_cb(chart, sensor_graph_area_cb,
                          static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), area);
      lv_obj_add_event_cb(chart, sensor_graph_delete_cb, LV_EVENT_DELETE, area);
    }
    // The Switch header: title and value beside the corner disc.
    if (icon_lbl) {
      lv_obj_align(icon_lbl, LV_ALIGN_TOP_LEFT, tile_layout::scale_480(-8), tile_layout::scale_480(-8));
    }
    const tile_header::Header header = tile_header::create(card, tile, false, box.y1);
    title_label = header.title;
    v = header.state;
  }

  // The looks (sensor_look.h, gauge lab 2026-10-28) in the graph's box: the
  // Switch header with the value beside the disc, or the title alone with
  // the value inside the arc or ring (Default: 20 on one-row tiles, 24 on
  // taller ones, or the chosen value size; its top at the middle of the
  // arc's inside).
  lv_obj_t* look = nullptr;
  if (look_enabled) {
    const lv_area_t box = sensor_graph_box(tile);
    look = sensor_look_view::create(card, tile, icon_lbl, box, gauge_min, gauge_max);
    if (sensor_look::value_inside(display_mode)) {
      title_label = create_title_head(card, tile);
      const lv_font_t* standard = tile.span_h > 1 ? tile_layout::content_font_24() : tile_layout::content_font_20();
      const lv_font_t* font = tile_layout::value_font_for_choice(tile.sensor_value_font, standard);
      v = lv_label_create(card);
      if (v) {
        const int card_w = tile_geometry::extent(tile.col, std::max(1.0f, tile.span_w), GRID_CELL_W, GRID_GAP);
        set_label_style(v, lv_color_hex(ui_theme::text()), font);
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_set_width(v, card_w);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(v, "--");
        lv_obj_clear_flag(v, LV_OBJ_FLAG_CLICKABLE);
        // The lab places the value's em box (line height 1); the label's
        // line is Inter's ascent plus descent, 1.211 em.
        const int32_t line_h = lv_font_get_line_height(font);
        const int32_t size = (line_h * 1000 + 605) / 1211;
        // A chosen size keeps the standard size's centre (sensor_look::value_top).
        const int32_t standard_size = (lv_font_get_line_height(standard) * 1000 + 605) / 1211;
        const int32_t top = look ? sensor_look_view::value_top(look, size, standard_size) : 0;
        lv_obj_set_pos(v, -lv_obj_get_style_pad_left(card, LV_PART_MAIN), top - (line_h - size) / 2);
      }
    } else {
      const tile_header::Header header = tile_header::create(card, tile, false, box.y1);
      title_label = header.title;
      v = header.state;
    }
  }

  // Value label combines the value and unit.
  if (!v) v = lv_label_create(card);
  if (!v) {
    Serial.println("[TileRenderer] ERROR: Could not create value label");
    return card;
  }
  if (!graph_enabled && !look_enabled) {
    set_label_style(v, lv_color_hex(ui_theme::text()), get_sensor_value_font(tile));
    lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(v, LV_PCT(100));
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(v, 8, 0);
    lv_label_set_text(v, "--");
  }

  // Get value y offset from tile settings (with defaults and clamping)
  int16_t value_y_offset = tile.sensor_value_y_offset;
  if (value_y_offset < -100) value_y_offset = -100;
  if (value_y_offset > 200) value_y_offset = 200;
  value_y_offset = tile_layout::scale_i16(value_y_offset);

  if (gauge_enabled) {
    lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0,
                 tile_layout::scale(12) + value_y_offset - (gauge_extra_h - gauge_shift));
  } else if (graph_enabled || look_enabled) {
    // The header placed it beside the disc, or the look in its arc.
  } else {
    lv_obj_align(v, LV_ALIGN_CENTER, 0,
                 tile_layout::scale(28) + value_y_offset);
  }

  if ((tile_geometry::compact(tile.type, tile.span_w, tile.span_h) ||
       tile_geometry::compact_editable(tile.type, tile.span_w, tile.span_h)) &&
      display_mode == 0) {
    compact_sensor_layout::apply(card, icon_lbl, title_label, v, tile);
  } else {
    tile_icon_disc::add_round(card, icon_lbl);
  }

  // Store for later updates.
  SensorTileWidgets* target = tile_renderer_get_sensor_widgets(grid_type);
  if (target && index < TILES_PER_GRID) {
    target[index].value_label = v;
    target[index].icon_label = icon_lbl;
    target[index].unit_label = nullptr;
    target[index].gauge = gauge;
    target[index].gauge_min = gauge_min;
    target[index].gauge_max = gauge_max;
    target[index].chart = chart;
    target[index].series = series;
    target[index].look = look;
    target[index].editable_hash = 0;
  }
  // The graph's area and the look's track take the circle's color (after
  // add_round made the disc).
  if (chart || look) tile_icon_source::refresh_controls(card);

  // PPA presents the screensaver as a complete fullscreen frame. Popups would
  // add a second overlay, so this mode disables them. Normal grids retain
  // their existing behavior.
  if (tile.sensor_entity.length() && grid_type != GridType::SCREENSAVER) {
    bool icon_override = false;
    if (tile.icon_name.length() &&
        (tileTypeIsEditableValue(tile.type) || !isMdiIconDisabled(tile.icon_name))) {
      icon_override = true;
    }
    SensorEventData* data = new SensorEventData{
      tile.sensor_entity,
      tile.title,
      icon_name,
      icon_override,
      tile.sensor_unit,
      tile.sensor_decimals,
      tileBgColorOrDefault(tile, tileDefaultBgColor()),
      tileTypeIsEditableValue(tile.type),
      tile.icon_colors
    };

    const lv_event_code_t popup_event =
        (getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS)
            ? LV_EVENT_SHORT_CLICKED
            : LV_EVENT_LONG_PRESSED;

    lv_obj_add_event_cb(card, sensor_popup_click_cb, popup_event, data);

    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
          if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
          SensorEventData* data = static_cast<SensorEventData*>(lv_event_get_user_data(e));
          delete data;
        },
        LV_EVENT_DELETE,
        data);
  }

  // Request history for graph tiles
  if (graph_enabled && tile.sensor_entity.length()) {
    request_tile_graph_history(tile.sensor_entity.c_str());
  }

  return card;
}



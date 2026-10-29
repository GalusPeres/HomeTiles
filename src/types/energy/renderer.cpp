#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/types/energy/renderer.h"

#include <Arduino.h>
#include <math.h>

#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_graph_box.h"
#include "src/tiles/runtime/tile_header.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/shared/tone_color.h"
#include "src/types/climate/layout.h"
#include "src/types/energy/energy_data.h"
#include "src/tiles/config/tile_icon_colors.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/shared/ui_theme.h"

namespace {

// The Energy tile's bars (Tile::sensor_display_mode 2, user 2026-10-29,
// mockup build/design-mockups/energy-tile-bars): today's hours under the
// Switch header like the Sensor graph tile, in the circle's tone with the
// value's edge in the icon color, the running hour all icon color. A zero
// line where the range puts it: feed-in hangs below it with its edge at the
// bottom. The bars keep the box's outer inset from its sides, top and
// bottom, so the first and last hour never reach the rounded corners and
// keep the same distance to the tile's edge at the sides as at the bottom.
constexpr uint8_t kEnergyBarsMode = 2;
constexpr uint8_t kEnergyBarHours = 24;

struct EnergyTileBars {
  String entity_id;
  lv_obj_t* icon = nullptr;
  float values[kEnergyBarHours] = {};
  bool valid[kEnergyBarHours] = {};
};

// Live bars by tile, so a new day answer reaches every grid that shows the
// source (also the cached pages); a full table only keeps a tile from
// following new answers.
constexpr size_t kMaxEnergyTileBars = 32;
struct LiveBars {
  lv_obj_t* frame = nullptr;
  EnergyTileBars* bars = nullptr;
};
LiveBars g_live_bars[kMaxEnergyTileBars] = {};
bool g_bar_table_full_logged = false;

// Copies a day answer's hours; true when the bars changed.
bool copy_day_values(EnergyTileBars& bars, const EnergyEntryData& entry) {
  bool changed = false;
  for (uint8_t i = 0; i < kEnergyBarHours; ++i) {
    const bool valid = i < entry.value_count && entry.value_valid[i] && isfinite(entry.values[i]);
    const float value = valid ? entry.values[i] : 0.0f;
    if (bars.valid[i] != valid || bars.values[i] != value) changed = true;
    bars.valid[i] = valid;
    bars.values[i] = value;
  }
  return changed;
}

void energy_bars_draw_cb(lv_event_t* e) {
  lv_obj_t* frame = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  auto* bars = static_cast<EnergyTileBars*>(lv_event_get_user_data(e));
  if (!frame || !layer || !bars) return;
  const ui_theme::SurfaceScope surface = tile_surface::scope(frame);
  lv_area_t coords;
  lv_obj_get_coords(frame, &coords);
  const int32_t room = climate_layout::kOuterInset;
  const int32_t left = coords.x1 + room, width = lv_area_get_width(&coords) - 2 * room;
  const int32_t top = coords.y1 + room, height = lv_area_get_height(&coords) - 2 * room;
  if (width < kEnergyBarHours || height < 4) return;

  float max_v = 0.0f, min_v = 0.0f;
  int newest = -1;
  for (int i = 0; i < kEnergyBarHours; ++i) {
    if (!bars->valid[i]) continue;
    newest = i;
    if (bars->values[i] > max_v) max_v = bars->values[i];
    if (bars->values[i] < min_v) min_v = bars->values[i];
  }
  if (newest < 0) return;
  const float span = max_v - min_v;
  // A day of zeros: the zero line at the bottom.
  const int32_t zero_y =
      span > 0.0001f ? top + static_cast<int32_t>(lroundf(max_v / span * static_cast<float>(height))) : top + height;
  const int32_t bar_w = std::max<int32_t>(2, static_cast<int32_t>(floorf(width / float(kEnergyBarHours) * 0.62f)));
  const int32_t edge = std::max<int32_t>(1, tile_layout::scale(2));
  const int32_t min_h = std::max<int32_t>(2, edge);

  // The body like the Sensor graph's area: the circle's color the frame
  // takes as a resting surface (tile_icon_source::refresh_controls).
  lv_draw_rect_dsc_t body;
  lv_draw_rect_dsc_init(&body);
  body.base.layer = layer;
  body.radius = 0;
  body.bg_color = lv_obj_get_style_bg_color(frame, LV_PART_MAIN);
  body.bg_opa = tone_color::control_opa(ui_surface_style::icon_glow_percent(),
                                        tile_icon_disc::see_through(lv_obj_get_parent(frame)));
  lv_draw_rect_dsc_t mark = body;
  mark.bg_color = bars->icon ? lv_obj_get_style_text_color(bars->icon, LV_PART_MAIN) : lv_color_hex(ui_theme::text());
  mark.bg_opa = LV_OPA_COVER;

  for (int i = 0; i < kEnergyBarHours; ++i) {
    const float value = bars->values[i];
    if (!bars->valid[i]) continue;
    // The first hour flush at the left room, the last at the right one.
    const int32_t x = left + static_cast<int32_t>(lroundf(i * float(width - bar_w) / float(kEnergyBarHours - 1)));
    if (fabsf(value) < 0.0001f) {
      // A measured 0: its edge on the zero line (below it at the top), so
      // the night does not read as missing data; an hour without data shows
      // nothing.
      const int32_t y1 = zero_y - edge >= top ? zero_y - edge : zero_y;
      const lv_area_t cap = {x, y1, x + bar_w - 1, y1 + edge - 1};
      lv_draw_rect(layer, &mark, &cap);
      continue;
    }
    const int32_t h = std::max(min_h, static_cast<int32_t>(lroundf(fabsf(value) / span * static_cast<float>(height))));
    lv_area_t bar = {x, value > 0 ? zero_y - h : zero_y, x + bar_w - 1, value > 0 ? zero_y - 1 : zero_y + h - 1};
    if (i == newest) {
      lv_draw_rect(layer, &mark, &bar);
      continue;
    }
    lv_draw_rect(layer, &body, &bar);
    lv_area_t cap = bar;
    if (value > 0) cap.y2 = bar.y1 + edge - 1;
    else cap.y1 = bar.y2 - edge + 1;
    lv_draw_rect(layer, &mark, &cap);
  }
}

void energy_bars_delete_cb(lv_event_t* e) {
  lv_obj_t* frame = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  for (LiveBars& live : g_live_bars) {
    if (live.frame == frame) live = LiveBars{};
  }
  delete static_cast<EnergyTileBars*>(lv_event_get_user_data(e));
}

// The bars' frame in the graph's box (tile_graph_box.h): a resting surface
// with the Switch bar's radius that never fills itself, like the Sensor
// graph's; its draw callback draws the bars.
lv_obj_t* create_energy_bars(lv_obj_t* card, const Tile& tile, lv_obj_t* icon) {
  const lv_area_t box = tile_graph_box::box(tile);
  lv_obj_t* frame = lv_obj_create(card);
  if (!frame) return nullptr;
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

  auto* bars = new EnergyTileBars();
  bars->entity_id = tile.sensor_entity;
  bars->icon = icon;
  EnergyEntryData entry;
  if (energy_find_entry(tile.sensor_entity, "day", entry)) copy_day_values(*bars, entry);
  lv_obj_add_event_cb(frame, energy_bars_draw_cb, LV_EVENT_DRAW_MAIN, bars);
  lv_obj_add_event_cb(frame, energy_bars_delete_cb, LV_EVENT_DELETE, bars);
  bool registered = false;
  for (LiveBars& live : g_live_bars) {
    if (live.frame) continue;
    live = LiveBars{frame, bars};
    registered = true;
    break;
  }
  if (!registered && !g_bar_table_full_logged) {
    g_bar_table_full_logged = true;
    Serial.printf("[EnergyTile] More than %u tiles with bars; the rest keep their bars until rebuilt\n",
                  static_cast<unsigned>(kMaxEnergyTileBars));
  }
  return frame;
}

struct EnergyEventData {
  String entity_id;
  String title;
  String icon_name;
  bool icon_override = false;
  String unit;
  uint8_t decimals = 1;
  uint32_t bg_color = 0;
  // Per-tile icon colors for the popup header icon.
  String icon_colors;
};

const lv_font_t* get_energy_value_font(const Tile& tile) {
  switch (tile.sensor_value_font) {
    case 1:
      return tile_layout::content_font_20();
    case 2:
      return tile_layout::content_font_24();
    case 3:
      return tile_layout::content_font_32();
    case 4:
      return tile_layout::content_font_40();
    case 5:
      return tile_layout::content_font_28();
    default:
      return FONT_VALUE;
  }
}

bool is_disabled_token(const String& value) {
  if (!value.length()) return false;
  String t = value;
  t.trim();
  if (!t.length()) return true;
  t.toLowerCase();
  return t == "-" || t == "none" || t == "null" || t == "no" || t == "off";
}

}  // namespace

void energy_tile_bars_update(const EnergyEntryData& entry) {
  if (entry.period != "day" || !entry.id.length()) return;
  for (const LiveBars& live : g_live_bars) {
    if (!live.frame || !live.bars || !live.bars->entity_id.equalsIgnoreCase(entry.id)) continue;
    if (copy_day_values(*live.bars, entry)) lv_obj_invalidate(live.frame);
  }
}

lv_obj_t* render_energy_tile(lv_obj_t* parent,
                             int col,
                             int row,
                             const Tile& tile,
                             uint8_t index,
                             GridType grid_type) {
  if (!parent) {
    Serial.println("[TileRenderer] ERROR: parent NULL for energy tile");
    return nullptr;
  }

  lv_obj_t* card = lv_button_create(parent);
  if (!card) {
    Serial.println("[TileRenderer] ERROR: Could not create energy card");
    return nullptr;
  }

  uint32_t card_color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

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

  lv_obj_t* icon_lbl = nullptr;
  String icon_name = tile.icon_name;
  bool icon_disabled = isMdiIconDisabled(icon_name);
  icon_name = normalizeMdiIconName(icon_name);
  if (!icon_disabled && !icon_name.length() && tile.sensor_entity.length()) {
    icon_name = normalizeMdiIconName(energy_source_icon(tile.sensor_entity));
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

  String title_text = tile.title;
  if (!title_text.length() && tile.sensor_entity.length()) {
    title_text = haBridgeConfig.findSensorName(tile.sensor_entity);
  }

  // The bars on whole-height tiles outside the screensaver: the Switch
  // header beside the disc and the bars in the box below, like the Sensor
  // graph tile; else the title right and the value in the middle.
  const bool compact = tile_geometry::compact(tile.type, tile.span_w, tile.span_h);
  const bool bars_shown = tile.sensor_display_mode == kEnergyBarsMode && tile.span_h >= 1 && !compact &&
                          grid_type != GridType::SCREENSAVER;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* value_label = nullptr;
  lv_obj_t* bars_frame = nullptr;
  if (bars_shown) {
    bars_frame = create_energy_bars(card, tile, icon_lbl);
    Tile head = tile;
    head.title = title_text;  // the source's name when the tile has none
    const tile_header::Header header = tile_header::create(card, head, false, tile_graph_box::box(tile).y1);
    title_label = header.title;
    value_label = header.state;
  } else {
    if (title_text.length() > 0) {
      title_label = lv_label_create(card);
      if (title_label) {
        set_label_style(title_label, lv_color_hex(ui_theme::text()),
                        tile_layout::header_title_font());
        lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title_label, LV_PCT(70));
        lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_RIGHT, 0);
        hometiles_title::tile(title_label, title_text.c_str(), true);
        lv_obj_align(title_label, LV_ALIGN_TOP_RIGHT,
                     tile_layout::scale_480(4),
                     tile_layout::scale_480(4));
      }
    }

    value_label = lv_label_create(card);
    if (!value_label) {
      return card;
    }
    set_label_style(value_label, lv_color_hex(ui_theme::text()), get_energy_value_font(tile));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(value_label, LV_PCT(100));
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(value_label, tile_layout::scale_480(8), 0);
    lv_label_set_text(value_label, "--");

    int16_t value_y_offset = tile.sensor_value_y_offset;
    if (value_y_offset < -100) value_y_offset = -100;
    if (value_y_offset > 200) value_y_offset = 200;
    value_y_offset = tile_layout::scale_i16(value_y_offset);
    lv_obj_align(value_label, LV_ALIGN_CENTER, 0,
                 tile_layout::scale(28) + value_y_offset);
  }
  if (!value_label) return card;

  if (compact) {
    compact_sensor_layout::apply(card, icon_lbl, title_label, value_label, tile);
  } else {
    tile_icon_disc::add_round(card, icon_lbl);
  }
  // The bars' body takes the circle's color (after add_round made the disc).
  if (bars_frame) tile_icon_source::refresh_controls(card);

  SensorTileWidgets* target = tile_renderer_get_sensor_widgets(grid_type);
  if (target && index < TILES_PER_GRID) {
    target[index].value_label = value_label;
    target[index].icon_label = icon_lbl;
    target[index].unit_label = nullptr;
    target[index].gauge = nullptr;
    target[index].gauge_min = 0;
    target[index].gauge_max = 100;
    target[index].chart = nullptr;
    target[index].series = nullptr;
    target[index].look = nullptr;
  }

  if (tile.sensor_entity.length() && grid_type != GridType::SCREENSAVER) {
    bool icon_override = false;
    if (tile.icon_name.length() && !isMdiIconDisabled(tile.icon_name)) {
      icon_override = true;
    }
    EnergyEventData* data = new EnergyEventData();
    data->entity_id = tile.sensor_entity;
    data->title = title_text;
    data->icon_name = icon_name;
    data->icon_override = icon_override;
    data->unit = tile.sensor_unit;
    data->decimals = tile.sensor_decimals == 0xFF ? static_cast<uint8_t>(1) : tile.sensor_decimals;
    data->bg_color = card_color;
    data->icon_colors = tile.icon_colors;

    const lv_event_code_t popup_event =
        (getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS)
            ? LV_EVENT_SHORT_CLICKED
            : LV_EVENT_LONG_PRESSED;

    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
          lv_event_code_t code = lv_event_get_code(e);
          if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
          EnergyEventData* data = static_cast<EnergyEventData*>(lv_event_get_user_data(e));
          if (!data || !data->entity_id.length()) return;

          EnergyPopupInit init;
          init.entity_id = data->entity_id;
          init.title = data->title.length() ? data->title : haBridgeConfig.findSensorName(data->entity_id);
          if (!init.title.length()) init.title = data->entity_id;
          init.icon_name = data->icon_name;

          String unit = data->unit;
          if (is_disabled_token(unit)) {
            unit = "";
          } else if (!unit.length()) {
            unit = haBridgeConfig.findSensorUnit(data->entity_id);
          } else {
            unit.trim();
          }
          init.unit = unit;
          init.decimals = data->decimals;
          init.bg_color = tile_icon_source::popup_background(static_cast<lv_obj_t*>(lv_event_get_current_target(e)), data->bg_color);
          // The header icon shows the color the tile icon shows right now
          // (fixed, own-state colors or rules).
          if (lv_obj_t* icon = tile_icon_source::card_icon(static_cast<lv_obj_t*>(lv_event_get_current_target(e)))) {
            init.icon_color = lv_color_to_u32(lv_obj_get_style_text_color(icon, LV_PART_MAIN)) & 0xFFFFFF;
          }

          finish_press_before_popup(e);
          show_energy_popup(init);
        },
        popup_event,
        data);

    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
          if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
          EnergyEventData* data = static_cast<EnergyEventData*>(lv_event_get_user_data(e));
          delete data;
        },
        LV_EVENT_DELETE,
        data);

    energy_request_period("day", false);
  }

  return card;
}

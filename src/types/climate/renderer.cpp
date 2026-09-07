#include "src/types/climate/renderer.h"

#include <algorithm>
#include <cmath>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/climate/layout.h"
#include "src/ui/popups/climate/climate_popup.h"

namespace {

struct ClimateEventData {
  GridType grid_type = GridType::TAB0;
  uint8_t index = 0;
};

enum class ClimateTileSlotKind : uint8_t {
  NONE = 0,
  CURRENT_TEMPERATURE,
  CURRENT_HUMIDITY,
  TARGET_TEMPERATURE,
  TARGET_TEMPERATURE_LOW,
  TARGET_TEMPERATURE_HIGH,
  TARGET_HUMIDITY,
  HVAC_MODE
};

struct ClimateAdjustEventData {
  GridType grid_type = GridType::TAB0;
  uint8_t index = 0;
  uint8_t slot_index = 0;
  int8_t direction = 0;
};

constexpr uint32_t kMiniTargetRemoteBlockMs = 2200;
constexpr uint32_t kMiniTargetDebounceMs = 1000;

enum class ClimateMiniTargetCommand : uint8_t {
  NONE = 0,
  TEMPERATURE,
  RANGE,
  HUMIDITY
};

struct ClimateMiniTargetContext {
  GridType grid_type = GridType::TAB0;
  uint8_t index = 0;
  String entity_id;
  ClimateMiniTargetCommand command = ClimateMiniTargetCommand::NONE;
  float target_temperature = 20.0f;
  float target_temp_low = 18.0f;
  float target_temp_high = 24.0f;
  float target_humidity = 50.0f;
  uint32_t block_remote_until_ms = 0;
  lv_timer_t* publish_timer = nullptr;
};

ClimateMiniTargetContext g_mini_target;

bool slot_is_adjustable(ClimateTileSlotKind kind) {
  return kind == ClimateTileSlotKind::TARGET_TEMPERATURE ||
         kind == ClimateTileSlotKind::TARGET_TEMPERATURE_LOW ||
         kind == ClimateTileSlotKind::TARGET_TEMPERATURE_HIGH ||
         kind == ClimateTileSlotKind::TARGET_HUMIDITY;
}

bool climate_state_feature_supported(
    const ClimateState& state, uint16_t feature, bool legacy_supported) {
  if (!state.has_supported_features) return legacy_supported;
  return (state.supported_features & feature) != 0;
}

bool slot_is_interactive(
    ClimateTileSlotKind kind, const ClimateState& state) {
  if (!state.available) return false;
  switch (kind) {
    case ClimateTileSlotKind::TARGET_TEMPERATURE:
      return state.has_target_temperature && climate_state_feature_supported(
          state, CLIMATE_FEATURE_TARGET_TEMPERATURE, true);
    case ClimateTileSlotKind::TARGET_TEMPERATURE_LOW:
    case ClimateTileSlotKind::TARGET_TEMPERATURE_HIGH:
      return state.has_target_range && climate_state_feature_supported(
          state, CLIMATE_FEATURE_TARGET_TEMPERATURE_RANGE, true);
    case ClimateTileSlotKind::TARGET_HUMIDITY:
      return state.has_target_humidity && climate_state_feature_supported(
          state, CLIMATE_FEATURE_TARGET_HUMIDITY, true);
    default:
      return false;
  }
}

float humidity_step_for(const ClimateState& state) {
  return state.target_humidity_step > 0.0f &&
                 state.target_humidity_step <= 100.0f
             ? state.target_humidity_step
             : 1.0f;
}

float clamp_value(float value, float minimum, float maximum) {
  return std::max(minimum, std::min(value, maximum));
}

float snap_value(float value, float step) {
  if (step <= 0.0f) return value;
  return roundf(value / step) * step;
}

bool mini_target_block_active(
    const ClimateMiniTargetContext& context, uint32_t now) {
  return static_cast<int32_t>(
             context.block_remote_until_ms - now) > 0;
}

ClimateMiniTargetCommand mini_target_command_for(
    ClimateTileSlotKind kind) {
  switch (kind) {
    case ClimateTileSlotKind::TARGET_TEMPERATURE:
      return ClimateMiniTargetCommand::TEMPERATURE;
    case ClimateTileSlotKind::TARGET_TEMPERATURE_LOW:
    case ClimateTileSlotKind::TARGET_TEMPERATURE_HIGH:
      return ClimateMiniTargetCommand::RANGE;
    case ClimateTileSlotKind::TARGET_HUMIDITY:
      return ClimateMiniTargetCommand::HUMIDITY;
    default:
      return ClimateMiniTargetCommand::NONE;
  }
}

void clear_pending_mini_target() {
  if (g_mini_target.publish_timer) {
    lv_timer_delete(g_mini_target.publish_timer);
    g_mini_target.publish_timer = nullptr;
  }
  g_mini_target.entity_id = "";
  g_mini_target.command = ClimateMiniTargetCommand::NONE;
  g_mini_target.block_remote_until_ms = 0;
}

bool mini_target_command_supported(
    const ClimateState& state, ClimateMiniTargetCommand command) {
  if (!state.available) return false;
  switch (command) {
    case ClimateMiniTargetCommand::TEMPERATURE:
      return state.has_target_temperature && climate_state_feature_supported(
          state, CLIMATE_FEATURE_TARGET_TEMPERATURE, true);
    case ClimateMiniTargetCommand::RANGE:
      return state.has_target_range && climate_state_feature_supported(
          state, CLIMATE_FEATURE_TARGET_TEMPERATURE_RANGE, true);
    case ClimateMiniTargetCommand::HUMIDITY:
      return state.has_target_humidity && climate_state_feature_supported(
          state, CLIMATE_FEATURE_TARGET_HUMIDITY, true);
    default:
      return false;
  }
}

void publish_pending_mini_target() {
  if (!g_mini_target.entity_id.length()) return;
  switch (g_mini_target.command) {
    case ClimateMiniTargetCommand::TEMPERATURE:
      mqttPublishClimateTemperature(
          g_mini_target.entity_id.c_str(),
          g_mini_target.target_temperature);
      break;
    case ClimateMiniTargetCommand::RANGE:
      mqttPublishClimateTemperature(
          g_mini_target.entity_id.c_str(), 0.0f, true,
          g_mini_target.target_temp_low,
          g_mini_target.target_temp_high);
      break;
    case ClimateMiniTargetCommand::HUMIDITY:
      mqttPublishClimateHumidity(
          g_mini_target.entity_id.c_str(),
          g_mini_target.target_humidity);
      break;
    default:
      return;
  }
  g_mini_target.block_remote_until_ms =
      millis() + kMiniTargetRemoteBlockMs;
}

void mini_target_publish_timer_cb(lv_timer_t* timer) {
  if (g_mini_target.publish_timer != timer) return;
  g_mini_target.publish_timer = nullptr;
  publish_pending_mini_target();
}

void schedule_mini_target_publish(
    GridType grid_type, uint8_t index, const String& entity_id,
    ClimateTileSlotKind kind, const ClimateState& state) {
  const ClimateMiniTargetCommand command =
      mini_target_command_for(kind);
  if (command == ClimateMiniTargetCommand::NONE ||
      !entity_id.length()) {
    return;
  }

  const bool same_target =
      g_mini_target.command == command &&
      g_mini_target.grid_type == grid_type &&
      g_mini_target.index == index &&
      g_mini_target.entity_id.equalsIgnoreCase(entity_id);
  if (g_mini_target.publish_timer && !same_target) {
    lv_timer_t* previous_timer = g_mini_target.publish_timer;
    g_mini_target.publish_timer = nullptr;
    lv_timer_delete(previous_timer);
    publish_pending_mini_target();
  }

  g_mini_target.grid_type = grid_type;
  g_mini_target.index = index;
  g_mini_target.entity_id = entity_id;
  g_mini_target.command = command;
  g_mini_target.target_temperature = state.target_temperature;
  g_mini_target.target_temp_low = state.target_temp_low;
  g_mini_target.target_temp_high = state.target_temp_high;
  g_mini_target.target_humidity = state.target_humidity;
  g_mini_target.block_remote_until_ms =
      millis() + kMiniTargetRemoteBlockMs;

  if (g_mini_target.publish_timer) {
    lv_timer_reset(g_mini_target.publish_timer);
    return;
  }
  g_mini_target.publish_timer =
      lv_timer_create(
          mini_target_publish_timer_cb,
          kMiniTargetDebounceMs, nullptr);
  if (g_mini_target.publish_timer) {
    lv_timer_set_repeat_count(g_mini_target.publish_timer, 1);
  } else {
    publish_pending_mini_target();
  }
}

String climate_temperature_text(const ClimateState& state, float value) {
  String text = i18n::format_number(
      configManager.getConfig().language, value, 1);
  text += " ";
  text += state.temperature_unit[0]
              ? state.temperature_unit
              : "\xC2\xB0"
                "C";
  return text;
}

String climate_slot_text(
    ClimateTileSlotKind kind, const ClimateState& state) {
  switch (kind) {
    case ClimateTileSlotKind::CURRENT_TEMPERATURE:
      return state.has_current_temperature
                 ? climate_temperature_text(state, state.current_temperature)
                 : String("-- \xC2\xB0"
                          "C");
    case ClimateTileSlotKind::CURRENT_HUMIDITY:
      if (!state.has_current_humidity) return "--%";
      return i18n::format_number(
                 configManager.getConfig().language,
                 state.current_humidity,
                 0) +
             "%";
    case ClimateTileSlotKind::TARGET_TEMPERATURE:
      return state.has_target_temperature
                 ? climate_temperature_text(state, state.target_temperature)
                 : String("-- \xC2\xB0"
                          "C");
    case ClimateTileSlotKind::TARGET_TEMPERATURE_LOW:
      return state.has_target_range
                 ? climate_temperature_text(state, state.target_temp_low)
                 : String("-- \xC2\xB0"
                          "C");
    case ClimateTileSlotKind::TARGET_TEMPERATURE_HIGH:
      return state.has_target_range
                 ? climate_temperature_text(state, state.target_temp_high)
                 : String("-- \xC2\xB0"
                          "C");
    case ClimateTileSlotKind::TARGET_HUMIDITY:
      if (!state.has_target_humidity) return "--%";
      return i18n::format_number(
                 configManager.getConfig().language,
                 state.target_humidity,
                 0) +
             "%";
    case ClimateTileSlotKind::HVAC_MODE: {
      if (!state.hvac_mode[0]) return "--";
      const char* entity_state = i18n::entity_state_label(
          configManager.getConfig().language, String(state.hvac_mode));
      if (entity_state[0]) {
        return entity_state;
      }
      return i18n::climate_option_label(
          configManager.getConfig().language, String(state.hvac_mode));
    }
    default:
      return "";
  }
}

String climate_slot_caption(
    ClimateTileSlotKind kind, const ClimateState& state) {
  const char* language = configManager.getConfig().language;
  if (kind == ClimateTileSlotKind::TARGET_HUMIDITY) {
    return i18n::climate_target_humidity_label(language);
  }
  if (kind == ClimateTileSlotKind::TARGET_TEMPERATURE_LOW) {
    return i18n::climate_target_heat_label(language);
  }
  if (kind == ClimateTileSlotKind::TARGET_TEMPERATURE_HIGH) {
    return i18n::climate_target_cool_label(language);
  }
  if (kind == ClimateTileSlotKind::TARGET_TEMPERATURE) {
    String action = state.hvac_action;
    action.toLowerCase();
    if (action == "heating" || action == "preheating" ||
        action == "cooling") {
      return action == "cooling"
                 ? i18n::climate_target_cool_label(language)
                 : i18n::climate_target_heat_label(language);
    }
    String mode = state.hvac_mode;
    mode.toLowerCase();
    if (mode == "heat") {
      return i18n::climate_target_heat_label(language);
    }
    if (mode == "cool") {
      return i18n::climate_target_cool_label(language);
    }
    if (mode.length()) {
      return i18n::climate_state_label(language, mode, "");
    }
    return i18n::climate_target_temperature_label(language);
  }
  return "";
}

ClimateTileSlotKind configured_slot_kind(ClimateTileContent content) {
  switch (content) {
    case CLIMATE_TILE_CONTENT_CURRENT_TEMPERATURE:
      return ClimateTileSlotKind::CURRENT_TEMPERATURE;
    case CLIMATE_TILE_CONTENT_CURRENT_HUMIDITY:
      return ClimateTileSlotKind::CURRENT_HUMIDITY;
    case CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE:
      return ClimateTileSlotKind::TARGET_TEMPERATURE;
    case CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE_LOW:
      return ClimateTileSlotKind::TARGET_TEMPERATURE_LOW;
    case CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE_HIGH:
      return ClimateTileSlotKind::TARGET_TEMPERATURE_HIGH;
    case CLIMATE_TILE_CONTENT_TARGET_HUMIDITY:
      return ClimateTileSlotKind::TARGET_HUMIDITY;
    case CLIMATE_TILE_CONTENT_HVAC_MODE:
      return ClimateTileSlotKind::HVAC_MODE;
    default:
      return ClimateTileSlotKind::NONE;
  }
}

uint8_t build_automatic_slot_kinds(
    const Tile& tile, const ClimateState& state,
    ClimateTileSlotKind* out, uint8_t capacity) {
  if (!out || capacity == 0) return 0;
  uint8_t count = 0;
  auto append = [&](ClimateTileSlotKind kind) {
    if (count < capacity) {
      out[count++] = kind;
    }
  };

  const uint8_t span_w = std::max<uint8_t>(1, tile.span_w);
  const uint8_t span_h = std::max<uint8_t>(1, tile.span_h);

  const String mode = state.hvac_mode;
  if (!state.available || mode.equalsIgnoreCase("unavailable") ||
      mode.equalsIgnoreCase("unknown")) {
    append(ClimateTileSlotKind::HVAC_MODE);
    return count;
  }

  auto append_primary_target = [&]() {
    if (state.has_target_range) {
      append(ClimateTileSlotKind::TARGET_TEMPERATURE_LOW);
    } else if (state.has_target_temperature) {
      append(ClimateTileSlotKind::TARGET_TEMPERATURE);
    } else if (state.has_target_humidity) {
      append(ClimateTileSlotKind::TARGET_HUMIDITY);
    } else if (!state.valid) {
      // Keep the standard layout stable even before the first HA state has
      // arrived. A valid state without any target capability deliberately
      // leaves this slot empty.
      append(ClimateTileSlotKind::TARGET_TEMPERATURE);
    }
  };

  if (span_w == 1 && span_h == 1) {
    if (!state.valid || state.has_current_temperature) {
      append(ClimateTileSlotKind::CURRENT_TEMPERATURE);
    } else {
      // A climate entity without a current-temperature sensor can still
      // expose a real adjustable target.
      append_primary_target();
    }
    return count;
  }

  if (span_w >= 2 && span_h == 1) {
    if (!state.valid || state.has_current_temperature) {
      append(ClimateTileSlotKind::CURRENT_TEMPERATURE);
    }
    append_primary_target();
    return count;
  }

  if (span_w == 1) {
    if (!state.valid || state.has_current_temperature) {
      append(ClimateTileSlotKind::CURRENT_TEMPERATURE);
    }
    append_primary_target();
    if (span_h == 2) return count;
    if (state.has_target_humidity &&
        (state.has_target_range ||
         state.has_target_temperature)) {
      append(ClimateTileSlotKind::TARGET_HUMIDITY);
    }
    return count;
  }

  if (!state.valid || state.has_current_temperature) {
    append(ClimateTileSlotKind::CURRENT_TEMPERATURE);
  }
  if (state.has_current_humidity) {
    append(ClimateTileSlotKind::CURRENT_HUMIDITY);
  }
  if (state.has_target_range) {
    append(ClimateTileSlotKind::TARGET_TEMPERATURE_LOW);
    append(ClimateTileSlotKind::TARGET_TEMPERATURE_HIGH);
  } else if (state.has_target_temperature) {
    append(ClimateTileSlotKind::TARGET_TEMPERATURE);
  }
  if (state.has_target_humidity) {
    append(ClimateTileSlotKind::TARGET_HUMIDITY);
  }
  if (state.hvac_mode[0]) {
    append(ClimateTileSlotKind::HVAC_MODE);
  }
  return count;
}

uint8_t build_slot_kinds(
    const Tile& tile, const ClimateState& state,
    ClimateTileSlotKind* out,
    ClimateTileTargetLayout* out_layouts,
    ClimateTileItemGeometry* out_geometry,
    uint8_t capacity) {
  if (!out || capacity == 0) return 0;
  const uint8_t slot_capacity =
      std::min<uint8_t>(
          capacity, climateTileSlotCapacity(tile));
  const uint8_t columns = climateTileGridColumns(tile);
  const uint8_t rows = climateTileGridRows(tile);
  ClimateTileSlotKind automatic[CLIMATE_TILE_MAX_CONTENT_SLOTS] = {};
  const uint8_t automatic_count =
      build_automatic_slot_kinds(
          tile, state, automatic, slot_capacity);

  bool explicitly_configured[
      static_cast<uint8_t>(ClimateTileSlotKind::HVAC_MODE) + 1] = {};
  for (uint8_t slot = 0; slot < slot_capacity; ++slot) {
    const ClimateTileContent configured =
        getClimateTileSlotContent(tile, slot);
    if (configured == CLIMATE_TILE_CONTENT_AUTO ||
        configured == CLIMATE_TILE_CONTENT_EMPTY) {
      continue;
    }
    const ClimateTileSlotKind kind = configured_slot_kind(configured);
    if (kind != ClimateTileSlotKind::NONE) {
      explicitly_configured[static_cast<uint8_t>(kind)] = true;
    }
  }

  bool occupied[
      CLIMATE_TILE_MAX_GRID_ROWS][
      CLIMATE_TILE_MAX_GRID_COLUMNS] = {};
  uint64_t stored_geometry = 0;
  const bool has_stored_geometry =
      parseClimateTileGeometry(tile, stored_geometry);
  uint8_t count = 0;
  uint8_t automatic_cursor = 0;
  for (uint8_t slot = 0; slot < slot_capacity; ++slot) {
    const ClimateTileContent configured =
        getClimateTileSlotContent(tile, slot);
    if (configured == CLIMATE_TILE_CONTENT_EMPTY) continue;
    ClimateTileSlotKind kind = ClimateTileSlotKind::NONE;
    if (configured == CLIMATE_TILE_CONTENT_AUTO) {
      while (automatic_cursor < automatic_count) {
        const ClimateTileSlotKind candidate =
            automatic[automatic_cursor++];
        if (explicitly_configured[
                static_cast<uint8_t>(candidate)]) {
          continue;
        }
        kind = candidate;
        break;
      }
    } else {
      kind = configured_slot_kind(configured);
    }
    if (kind == ClimateTileSlotKind::NONE) continue;

    ClimateTileItemGeometry geometry =
        getClimateTileItemGeometry(tile, slot);
    if (!has_stored_geometry && slot_is_adjustable(kind) &&
        geometry.span_w == 1 && geometry.span_h == 1) {
      const ClimateTileTargetLayout requested =
          configured == CLIMATE_TILE_CONTENT_AUTO
              ? CLIMATE_TILE_TARGET_LAYOUT_AUTO
              : getClimateTileTargetLayout(tile, slot);
      const bool can_horizontal =
          geometry.col + 1 < columns;
      const bool can_vertical =
          geometry.row + 1 < rows;
      if (requested == CLIMATE_TILE_TARGET_LAYOUT_HORIZONTAL &&
          can_horizontal) {
        geometry.span_w = 2;
      } else if (requested == CLIMATE_TILE_TARGET_LAYOUT_VERTICAL &&
                 can_vertical) {
        geometry.span_h = 2;
      } else if (columns == 1 && can_vertical) {
        geometry.span_h = 2;
      } else if (rows == 1 && can_horizontal) {
        geometry.span_w = 2;
      } else if (can_vertical) {
        geometry.span_h = 2;
      } else if (can_horizontal) {
        geometry.span_w = 2;
      }
    }

    auto can_place = [&](const ClimateTileItemGeometry& candidate) {
      if (candidate.col + candidate.span_w > columns ||
          candidate.row + candidate.span_h > rows) {
        return false;
      }
      for (uint8_t row = candidate.row;
           row < candidate.row + candidate.span_h; ++row) {
        for (uint8_t column = candidate.col;
             column < candidate.col + candidate.span_w; ++column) {
          if (occupied[row][column]) return false;
        }
      }
      return true;
    };
    if (!can_place(geometry)) {
      bool found = false;
      for (uint8_t row = 0;
           row + geometry.span_h <= rows && !found; ++row) {
        for (uint8_t column = 0;
             column + geometry.span_w <= columns; ++column) {
          ClimateTileItemGeometry candidate = geometry;
          candidate.col = column;
          candidate.row = row;
          if (!can_place(candidate)) continue;
          geometry = candidate;
          found = true;
          break;
        }
      }
      if (!found) continue;
    }
    for (uint8_t row = geometry.row;
         row < geometry.row + geometry.span_h; ++row) {
      for (uint8_t column = geometry.col;
           column < geometry.col + geometry.span_w; ++column) {
        occupied[row][column] = true;
      }
    }

    ClimateTileTargetLayout layout =
        CLIMATE_TILE_TARGET_LAYOUT_AUTO;
    if (geometry.span_w > 1 && geometry.span_h == 1) {
      layout = CLIMATE_TILE_TARGET_LAYOUT_HORIZONTAL;
    } else if (geometry.span_h > 1) {
      layout = CLIMATE_TILE_TARGET_LAYOUT_VERTICAL;
    }
    out[count] = kind;
    if (out_layouts) out_layouts[count] = layout;
    if (out_geometry) out_geometry[count] = geometry;
    ++count;
  }
  return count;
}

void layout_climate_slots(
    ClimateTileWidgets& widgets, const Tile& tile) {
  const uint8_t count = widgets.active_slot_count;
  if (count == 0) return;

  const uint8_t span_w = std::max<uint8_t>(1, tile.span_w);
  const uint8_t span_h = std::max<uint8_t>(1, tile.span_h);
  const uint8_t columns = climateTileGridColumns(tile);
  const uint8_t logical_rows = climateTileGridRows(tile);
  const lv_coord_t tile_w =
      static_cast<lv_coord_t>(
          span_w * GRID_CELL_W + (span_w - 1) * GRID_GAP);
  const lv_coord_t tile_h =
      static_cast<lv_coord_t>(
          span_h * GRID_CELL_H + (span_h - 1) * GRID_GAP);
  // Climate cards retain the original 20/24 px tile padding. Child
  // coordinates therefore address the padded content box, not the full card.
  const lv_coord_t content_x =
      climate_layout::kOuterInset -
      climate_layout::kCardPaddingHorizontal;
  const lv_coord_t content_w =
      tile_w - climate_layout::kOuterInset * 2;
  const lv_coord_t grid_top =
      climate_layout::kContentTopInPaddedCard;
  const lv_coord_t grid_bottom =
      tile_h - climate_layout::kCardPaddingVertical -
      climate_layout::kOuterInset;
  const lv_coord_t grid_h =
      std::max<lv_coord_t>(1, grid_bottom - grid_top);

  auto track_start = [](
      lv_coord_t origin, lv_coord_t extent, uint8_t tracks,
      uint8_t track) -> lv_coord_t {
    const lv_coord_t total_gap =
        static_cast<lv_coord_t>(tracks - 1) *
        climate_layout::kGap;
    const lv_coord_t track_space =
        std::max<lv_coord_t>(tracks, extent - total_gap);
    return origin +
           static_cast<lv_coord_t>(
               static_cast<int32_t>(track_space) * track / tracks) +
           static_cast<lv_coord_t>(track) * climate_layout::kGap;
  };
  auto track_end = [](
      lv_coord_t origin, lv_coord_t extent, uint8_t tracks,
      uint8_t track_exclusive) -> lv_coord_t {
    const lv_coord_t total_gap =
        static_cast<lv_coord_t>(tracks - 1) *
        climate_layout::kGap;
    const lv_coord_t track_space =
        std::max<lv_coord_t>(tracks, extent - total_gap);
    return origin +
           static_cast<lv_coord_t>(
               static_cast<int32_t>(track_space) *
               track_exclusive / tracks) +
           static_cast<lv_coord_t>(track_exclusive - 1) *
               climate_layout::kGap;
  };

  for (uint8_t i = 0; i < ClimateTileWidgets::kMaxSlots; ++i) {
    lv_obj_t* root = widgets.slot_roots[i];
    if (!root) continue;
    if (i >= count) {
      lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    const ClimateTileSlotKind kind =
        static_cast<ClimateTileSlotKind>(widgets.slot_kinds[i]);
    const bool adjustable = slot_is_adjustable(kind);
    const ClimateTileItemGeometry geometry =
        widgets.slot_geometry[i];
    if (geometry.col >= columns ||
        geometry.row >= logical_rows ||
        geometry.span_w < 1 ||
        geometry.span_h < 1 ||
        geometry.col + geometry.span_w > columns ||
        geometry.row + geometry.span_h > logical_rows) {
      lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const bool horizontal_target =
        adjustable && geometry.span_w > 1 &&
        geometry.span_h == 1;
    const bool vertical_target =
        adjustable && geometry.span_w == 1 &&
        geometry.span_h > 1;
    const bool large_target =
        adjustable && geometry.span_w > 1 &&
        geometry.span_h > 1;
    const bool compact_target =
        adjustable && geometry.span_w == 1 &&
        geometry.span_h == 1;

    const lv_coord_t root_x =
        track_start(
            content_x, content_w, columns, geometry.col);
    const lv_coord_t root_right =
        track_end(
            content_x, content_w, columns,
            geometry.col + geometry.span_w);
    const lv_coord_t root_y =
        track_start(
            grid_top, grid_h, logical_rows, geometry.row);
    const lv_coord_t root_bottom =
        track_end(
            grid_top, grid_h, logical_rows,
            geometry.row + geometry.span_h);
    const lv_coord_t root_w = root_right - root_x;
    const lv_coord_t root_h = root_bottom - root_y;

    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(root, root_x, root_y);
    lv_obj_set_size(root, root_w, root_h);

    lv_obj_t* minus = lv_obj_get_child(root, 0);
    lv_obj_t* value = lv_obj_get_child(root, 1);
    lv_obj_t* plus = lv_obj_get_child(root, 2);
    lv_obj_t* caption = lv_obj_get_child(root, 3);
    auto set_pressed_feedback = [
        compact_target, horizontal_target,
        vertical_target](lv_obj_t* button) {
      if (!button) return;
      lv_obj_set_style_bg_opa(
          button,
          (compact_target || horizontal_target ||
           vertical_target)
              ? LV_OPA_TRANSP
              : LV_OPA_50,
          LV_PART_MAIN | LV_STATE_PRESSED);
    };
    auto layout_adjust_symbol = [
        compact_target, horizontal_target,
        vertical_target, root_w](
        lv_obj_t* button, bool left) {
      if (!button) return;
      lv_obj_t* label = lv_obj_get_child(button, 0);
      if (!label) return;
      lv_obj_set_style_text_font(
          label,
          (compact_target || horizontal_target)
              ? (root_w < 130 ? &ui_font_16
                              : tile_layout::content_font_20())
              : tile_layout::content_font_24(),
          0);
      if (compact_target) {
        lv_obj_align(
            label,
            left ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID,
            left ? tile_layout::scale(8) : tile_layout::scale(-8), 0);
      } else if (horizontal_target) {
        lv_obj_align(
            label,
            left ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID,
            left ? tile_layout::scale(8) : tile_layout::scale(-8), 0);
      } else if (vertical_target) {
        lv_obj_align(label, LV_ALIGN_CENTER, 0, tile_layout::scale(-4));
      } else {
        lv_obj_center(label);
      }
    };
    set_pressed_feedback(minus);
    set_pressed_feedback(plus);

    if (compact_target) {
      if (caption) {
        lv_obj_add_flag(caption, LV_OBJ_FLAG_HIDDEN);
      }
      if (minus) {
        lv_obj_set_size(minus, root_w / 2, root_h);
        lv_obj_align(minus, LV_ALIGN_LEFT_MID, 0, 0);
      }
      if (plus) {
        lv_obj_set_size(
            plus, root_w - root_w / 2, root_h);
        lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
      }
      if (value) {
        lv_obj_set_width(
            value,
            std::max<lv_coord_t>(1, root_w - tile_layout::scale(32)));
        lv_obj_set_style_text_font(
            value,
#if defined(DEVICE_LAYOUT_480X480)
            // The compact target still has enough room for the same value font
            // as the current-temperature slot. A smaller fallback made the two
            // values look unrelated in a 2x1 climate tile.
            FONT_VALUE,
#else
            root_w < 110
                ? tile_layout::content_font_20()
                : (root_w < 130
                       ? tile_layout::content_font_24()
                       : FONT_VALUE),
#endif
            0);
        lv_obj_align(value, LV_ALIGN_CENTER, 0, 0);
      }
    } else if (horizontal_target) {
      // A horizontal target spans two real mini-grid columns. Keep the
      // internal caption/control split on those same tracks instead of a
      // plain 50/50 split, otherwise both centers move by half a grid gap.
      const lv_coord_t internal_gap =
          std::min<lv_coord_t>(
              climate_layout::kGap,
              std::max<lv_coord_t>(0, root_w - 2));
      const lv_coord_t caption_w =
          std::max<lv_coord_t>(1, (root_w - internal_gap) / 2);
      const lv_coord_t control_x = caption_w + internal_gap;
      const lv_coord_t control_w =
          std::max<lv_coord_t>(1, root_w - control_x);
      const lv_coord_t minus_w = control_w / 2;
      const lv_coord_t plus_w = control_w - minus_w;
      const lv_coord_t value_w =
          std::max<lv_coord_t>(1,
                               control_w - tile_layout::scale(32));
      if (caption) {
        lv_obj_set_width(caption, caption_w);
        lv_obj_set_style_text_align(
            caption, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(caption, LV_ALIGN_LEFT_MID, 0, 0);
      }
      if (minus) {
        lv_obj_set_size(minus, minus_w, root_h);
        lv_obj_align(
            minus, LV_ALIGN_LEFT_MID, control_x, 0);
      }
      if (plus) {
        lv_obj_set_size(plus, plus_w, root_h);
        lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
      }
      if (value) {
        lv_obj_set_width(value, value_w);
        lv_obj_set_style_text_font(value, FONT_VALUE, 0);
        lv_obj_align(
            value, LV_ALIGN_LEFT_MID,
            control_x + (control_w - value_w) / 2, 0);
      }
    } else if (vertical_target) {
      const lv_coord_t button_h = tile_layout::scale(40);
      if (caption) {
        lv_obj_set_width(caption, root_w - tile_layout::scale(16));
        lv_obj_set_style_text_align(
            caption, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(caption, LV_ALIGN_TOP_MID, 0,
                     tile_layout::scale(10));
      }
      if (value) {
        lv_obj_set_width(value, root_w - tile_layout::scale(12));
        lv_obj_set_style_text_font(value, FONT_VALUE, 0);
        lv_obj_align(value, LV_ALIGN_CENTER, 0, 0);
      }
      if (minus) {
        lv_obj_set_size(minus, root_w / 2, button_h);
        lv_obj_align(minus, LV_ALIGN_BOTTOM_LEFT, 0,
                     tile_layout::scale(-8));
      }
      if (plus) {
        lv_obj_set_size(
            plus, root_w - root_w / 2, button_h);
        lv_obj_align(plus, LV_ALIGN_BOTTOM_RIGHT, 0,
                     tile_layout::scale(-8));
      }
    } else if (large_target) {
      const lv_coord_t button_w = tile_layout::scale(54);
      const lv_coord_t button_h = tile_layout::scale(42);
      if (caption) {
        lv_obj_set_width(caption, root_w - tile_layout::scale(24));
        lv_obj_set_style_text_align(
            caption, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(caption, LV_ALIGN_CENTER, 0,
                     tile_layout::scale(-44));
      }
      if (value) {
        lv_obj_set_width(value, root_w - tile_layout::scale(24));
        lv_obj_set_style_text_font(value, FONT_VALUE, 0);
        lv_obj_align(value, LV_ALIGN_CENTER, 0, 0);
      }
      if (minus) {
        lv_obj_set_size(minus, button_w, button_h);
        lv_obj_align(minus, LV_ALIGN_CENTER, tile_layout::scale(-36),
                     tile_layout::scale(44));
      }
      if (plus) {
        lv_obj_set_size(plus, button_w, button_h);
        lv_obj_align(plus, LV_ALIGN_CENTER, tile_layout::scale(36),
                     tile_layout::scale(44));
      }
    } else {
      if (caption) {
        lv_obj_add_flag(caption, LV_OBJ_FLAG_HIDDEN);
      }
      if (minus) {
        lv_obj_add_flag(minus, LV_OBJ_FLAG_HIDDEN);
      }
      if (plus) {
        lv_obj_add_flag(plus, LV_OBJ_FLAG_HIDDEN);
      }
      if (value) {
        lv_obj_set_width(value, root_w);
        lv_obj_align(value, LV_ALIGN_CENTER, 0, 0);
      }
    }
    layout_adjust_symbol(minus, true);
    layout_adjust_symbol(plus, false);
  }
}

ClimatePopupInit popup_init_for(const ClimateEventData* data) {
  ClimatePopupInit init;
  if (!data || data->index >= TILES_PER_GRID) return init;
  const Tile* tile = tile_renderer_get_tile_config(data->grid_type, data->index);
  ClimateState* states = tile_renderer_get_climate_states(data->grid_type);
  if (!tile || !states) return init;
  const ClimateState& state = states[data->index];

  init.entity_id = tile->sensor_entity;
  init.title = tile->title.length()
                   ? tile->title
                   : haBridgeConfig.findSensorName(tile->sensor_entity);
  if (!init.title.length()) init.title = tile->sensor_entity;
  String configured_icon = normalizeMdiIconName(tile->icon_name);
  init.icon_visible = !isMdiIconDisabled(tile->icon_name);
  init.dynamic_icon = init.icon_visible && !configured_icon.length();
  init.available = state.available;
  init.has_supported_features = state.has_supported_features;
  init.supported_features = state.supported_features;
  init.icon_name = climate_tile_base_icon(*tile);
  init.hvac_mode = state.hvac_mode;
  init.hvac_action = state.hvac_action;
  init.hvac_modes = climateHvacModesCsv(state.hvac_modes_mask);
  init.preset_mode = climatePresetName(state.preset_mode_id);
  init.preset_modes = climatePresetModesCsv(state.preset_modes_mask);
  init.fan_mode = state.fan_mode;
  init.fan_modes = climateFanModesCsv(state.fan_modes_mask);
  init.swing_mode = state.swing_mode;
  init.swing_modes = climateSwingModesCsv(state.swing_modes_mask);
  init.swing_horizontal_mode = state.swing_horizontal_mode;
  init.swing_horizontal_modes =
      climateHorizontalSwingModesCsv(state.swing_horizontal_modes_mask);
  init.temperature_unit = state.temperature_unit;
  init.current_temperature = state.current_temperature;
  init.current_humidity = state.current_humidity;
  init.target_temperature = state.target_temperature;
  init.target_humidity = state.target_humidity;
  init.target_temp_low = state.target_temp_low;
  init.target_temp_high = state.target_temp_high;
  init.min_temp = state.min_temp;
  init.max_temp = state.max_temp;
  init.min_humidity = state.min_humidity;
  init.max_humidity = state.max_humidity;
  init.target_temp_step = state.target_temp_step;
  init.target_humidity_step = state.target_humidity_step;
  init.has_current_temperature = state.has_current_temperature;
  init.has_current_humidity = state.has_current_humidity;
  init.has_target_temperature = state.has_target_temperature;
  init.has_target_humidity = state.has_target_humidity;
  init.has_target_range = state.has_target_range;
  return init;
}

void climate_adjust_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  ClimateAdjustEventData* data =
      static_cast<ClimateAdjustEventData*>(
          lv_event_get_user_data(event));
  if (!data || data->index >= TILES_PER_GRID ||
      data->slot_index >= ClimateTileWidgets::kMaxSlots) {
    return;
  }
  ClimateTileWidgets* widgets =
      tile_renderer_get_climate_widgets(data->grid_type);
  ClimateState* states =
      tile_renderer_get_climate_states(data->grid_type);
  const Tile* tile =
      tile_renderer_get_tile_config(data->grid_type, data->index);
  if (!widgets || !states || !tile || !tile->sensor_entity.length()) {
    return;
  }

  ClimateState& state = states[data->index];
  const ClimateTileSlotKind kind =
      static_cast<ClimateTileSlotKind>(
          widgets[data->index].slot_kinds[data->slot_index]);
  if (!slot_is_interactive(kind, state)) return;
  const float direction = data->direction < 0 ? -1.0f : 1.0f;
  const float temperature_step =
      (state.target_temp_step > 0.0f &&
       state.target_temp_step <= 10.0f)
          ? state.target_temp_step
          : 0.5f;

  switch (kind) {
    case ClimateTileSlotKind::TARGET_TEMPERATURE:
      if (!state.has_target_temperature) return;
      state.target_temperature = clamp_value(
          snap_value(
              state.target_temperature +
                  direction * temperature_step,
              temperature_step),
          state.min_temp, state.max_temp);
      break;
    case ClimateTileSlotKind::TARGET_TEMPERATURE_LOW:
      if (!state.has_target_range) return;
      state.target_temp_low = clamp_value(
          snap_value(
              state.target_temp_low +
                  direction * temperature_step,
              temperature_step),
          state.min_temp, state.target_temp_high);
      break;
    case ClimateTileSlotKind::TARGET_TEMPERATURE_HIGH:
      if (!state.has_target_range) return;
      state.target_temp_high = clamp_value(
          snap_value(
              state.target_temp_high +
                  direction * temperature_step,
              temperature_step),
          state.target_temp_low, state.max_temp);
      break;
    case ClimateTileSlotKind::TARGET_HUMIDITY:
      if (!state.has_target_humidity) return;
      {
        const float humidity_step = humidity_step_for(state);
        state.target_humidity = clamp_value(
            state.target_humidity + direction * humidity_step,
            state.min_humidity, state.max_humidity);
      }
      break;
    default:
      return;
  }

  // Match the popup controls: show every tap immediately, publish only the
  // final value of a short burst, and invalidate the old MQTT payload hash so
  // delayed states can be reconciled instead of restoring an older target.
  widgets[data->index].last_payload_hash = 0;
  schedule_mini_target_publish(
      data->grid_type, data->index, tile->sensor_entity,
      kind, state);
  refresh_climate_tile_content(
      data->grid_type, data->index, state);
  const ClimateEventData popup_data{data->grid_type, data->index};
  update_climate_popup(popup_init_for(&popup_data));
}

void climate_adjust_event_data_delete_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) return;
  delete static_cast<ClimateAdjustEventData*>(
      lv_event_get_user_data(event));
}

lv_obj_t* create_climate_slot(
    lv_obj_t* card, GridType grid_type, uint8_t index,
    uint8_t slot_index) {
  lv_obj_t* root = lv_obj_create(card);
  lv_obj_set_style_bg_color(
      root, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_shadow_width(root, 0, 0);
  // Keep the inner control radius concentric with the active layout's card.
  lv_obj_set_style_radius(
      root, climate_layout::kControlRadius, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);

  auto create_adjust_button = [&](int8_t direction) {
    lv_obj_t* button = lv_button_create(root);
    lv_obj_set_style_bg_opa(
        button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        button, lv_color_hex(0x5A5A5A),
        LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(
        button, LV_OPA_50,
        LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    disable_pressed_button_animation(button);

    lv_obj_t* label = lv_label_create(button);
    set_label_style(label, lv_color_white(),
                    tile_layout::content_font_24());
    lv_label_set_text(label, direction < 0 ? "-" : "+");
    lv_obj_center(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);

    ClimateAdjustEventData* data =
        new ClimateAdjustEventData{
            grid_type, index, slot_index, direction};
    lv_obj_add_event_cb(
        button, climate_adjust_event_cb,
        LV_EVENT_CLICKED, data);
    lv_obj_add_event_cb(
        button, climate_adjust_event_data_delete_cb,
        LV_EVENT_DELETE, data);
    return button;
  };

  create_adjust_button(-1);

  lv_obj_t* value = lv_label_create(root);
  set_label_style(value, lv_color_white(),
                  tile_layout::content_font_24());
  lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
  lv_label_set_text(value, "--");
  lv_obj_clear_flag(value, LV_OBJ_FLAG_CLICKABLE);

  create_adjust_button(1);

  lv_obj_t* caption = lv_label_create(root);
  set_label_style(caption, lv_color_hex(0xE0E0E0), FONT_TITLE);
  lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
  lv_label_set_text(caption, "");
  lv_obj_clear_flag(caption, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(caption, LV_OBJ_FLAG_HIDDEN);
  return root;
}

}  // namespace

bool reconcile_climate_mini_target_state(
    GridType grid_type, uint8_t index, ClimateState& state) {
  if (g_mini_target.command == ClimateMiniTargetCommand::NONE ||
      g_mini_target.grid_type != grid_type ||
      g_mini_target.index != index) {
    return false;
  }
  const Tile* tile =
      tile_renderer_get_tile_config(grid_type, index);
  if (!tile ||
      !tile->sensor_entity.equalsIgnoreCase(
          g_mini_target.entity_id)) {
    clear_pending_mini_target();
    return false;
  }
  if (!mini_target_command_supported(state, g_mini_target.command)) {
    clear_pending_mini_target();
    return false;
  }

  bool confirmed = false;
  switch (g_mini_target.command) {
    case ClimateMiniTargetCommand::TEMPERATURE:
      confirmed =
          state.has_target_temperature &&
          fabsf(
              state.target_temperature -
              g_mini_target.target_temperature) < 0.01f;
      break;
    case ClimateMiniTargetCommand::RANGE:
      confirmed =
          state.has_target_range &&
          fabsf(
              state.target_temp_low -
              g_mini_target.target_temp_low) < 0.01f &&
          fabsf(
              state.target_temp_high -
              g_mini_target.target_temp_high) < 0.01f;
      break;
    case ClimateMiniTargetCommand::HUMIDITY:
      confirmed =
          state.has_target_humidity &&
          fabsf(
              state.target_humidity -
              g_mini_target.target_humidity) < 0.01f;
      break;
    default:
      return false;
  }

  // A matching state after the debounced publish is the acknowledgement.
  if (!g_mini_target.publish_timer && confirmed) {
    clear_pending_mini_target();
    return false;
  }

  const bool protect_local_value =
      g_mini_target.publish_timer ||
      mini_target_block_active(g_mini_target, millis());
  if (!protect_local_value) {
    clear_pending_mini_target();
    return false;
  }

  switch (g_mini_target.command) {
    case ClimateMiniTargetCommand::TEMPERATURE:
      state.has_target_temperature = true;
      state.target_temperature = g_mini_target.target_temperature;
      break;
    case ClimateMiniTargetCommand::RANGE:
      state.has_target_range = true;
      state.target_temp_low = g_mini_target.target_temp_low;
      state.target_temp_high = g_mini_target.target_temp_high;
      break;
    case ClimateMiniTargetCommand::HUMIDITY:
      state.has_target_humidity = true;
      state.target_humidity = g_mini_target.target_humidity;
      break;
    default:
      return false;
  }
  return true;
}

void refresh_climate_tile_content(
    GridType grid_type, uint8_t index,
    const ClimateState& state) {
  if (index >= TILES_PER_GRID) return;
  ClimateTileWidgets* widgets =
      tile_renderer_get_climate_widgets(grid_type);
  const Tile* tile =
      tile_renderer_get_tile_config(grid_type, index);
  if (!widgets || !tile) return;

  ClimateTileWidgets& widget = widgets[index];
  const bool has_slot_layout = widget.slot_roots[0] != nullptr;
  ClimateTileSlotKind kinds[ClimateTileWidgets::kMaxSlots] = {};
  ClimateTileTargetLayout layouts[
      ClimateTileWidgets::kMaxSlots] = {};
  ClimateTileItemGeometry geometry[
      ClimateTileWidgets::kMaxSlots] = {};
  const uint8_t count =
      build_slot_kinds(
          *tile, state, kinds, layouts, geometry,
          ClimateTileWidgets::kMaxSlots);

  if (!has_slot_layout) {
    widget.active_slot_count = 0;
    if (widget.value_label) {
      const ClimateTileSlotKind kind =
          count > 0 ? kinds[0]
                    : ClimateTileSlotKind::CURRENT_TEMPERATURE;
      const String text = climate_slot_text(kind, state);
      lv_label_set_text(widget.value_label, text.c_str());
      lv_obj_clear_flag(widget.value_label, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  // Slot geometry is also the empty/unavailable placeholder geometry. Falling
  // back to the former single centered value here made an empty 2x1 tile look
  // completely different on LVGL than in the Web UI.
  if (widget.value_label) {
    lv_obj_add_flag(widget.value_label, LV_OBJ_FLAG_HIDDEN);
  }

  widget.active_slot_count = count;

  for (uint8_t i = 0; i < ClimateTileWidgets::kMaxSlots; ++i) {
    lv_obj_t* root = widget.slot_roots[i];
    if (!root) continue;
    if (i >= count) {
      widget.slot_kinds[i] =
          static_cast<uint8_t>(ClimateTileSlotKind::NONE);
      widget.slot_layouts[i] =
          static_cast<uint8_t>(CLIMATE_TILE_TARGET_LAYOUT_AUTO);
      widget.slot_geometry[i] = ClimateTileItemGeometry{};
      lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    const ClimateTileSlotKind kind = kinds[i];
    widget.slot_kinds[i] = static_cast<uint8_t>(kind);
    widget.slot_layouts[i] = static_cast<uint8_t>(layouts[i]);
    widget.slot_geometry[i] = geometry[i];
    const bool adjustable = slot_is_adjustable(kind);
    const bool interactive_control = slot_is_interactive(kind, state);
    lv_obj_set_style_bg_opa(
        root, interactive_control ? LV_OPA_COVER : LV_OPA_TRANSP,
        LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);

    lv_obj_t* minus = lv_obj_get_child(root, 0);
    lv_obj_t* value = lv_obj_get_child(root, 1);
    lv_obj_t* plus = lv_obj_get_child(root, 2);
    lv_obj_t* caption = lv_obj_get_child(root, 3);
    if (minus) {
      if (interactive_control) {
        lv_obj_clear_state(minus, LV_STATE_DISABLED);
      } else {
        lv_obj_add_state(minus, LV_STATE_DISABLED);
      }
      if (interactive_control) {
        lv_obj_clear_flag(minus, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(minus, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (plus) {
      if (interactive_control) {
        lv_obj_clear_state(plus, LV_STATE_DISABLED);
      } else {
        lv_obj_add_state(plus, LV_STATE_DISABLED);
      }
      if (interactive_control) {
        lv_obj_clear_flag(plus, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(plus, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (value) {
      const String text = climate_slot_text(kind, state);
      lv_label_set_text(value, text.c_str());
      lv_obj_set_style_text_color(value, lv_color_white(), 0);
      lv_obj_set_style_text_font(
          value,
          adjustable
              ? FONT_VALUE
              : (kind == ClimateTileSlotKind::HVAC_MODE
                     ? FONT_TITLE
                     : FONT_VALUE),
          0);
    }
    if (caption) {
      if (interactive_control) {
        const String text = climate_slot_caption(kind, state);
        lv_label_set_text(caption, text.c_str());
        lv_obj_clear_flag(caption, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(caption, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  layout_climate_slots(widget, *tile);
}

lv_obj_t* render_climate_tile(lv_obj_t* parent,
                              int col,
                              int row,
                              const Tile& tile,
                              uint8_t index,
                              GridType grid_type) {
  if (!parent) return nullptr;

  lv_obj_t* card = lv_button_create(parent);
  const uint32_t color = tileBgColorOrDefault(tile, 0x2A2A2A);
  lv_obj_set_style_bg_color(card, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      card, lv_color_hex(brighten_rgb_color(color, 0x10)),
      LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_hor(
      card, climate_layout::kCardPaddingHorizontal, 0);
  lv_obj_set_style_pad_ver(
      card, climate_layout::kCardPaddingVertical, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);
  set_tile_grid_cell(card, col, row, tile.span_w, tile.span_h);

  const bool icon_disabled = isMdiIconDisabled(tile.icon_name);
  const String configured_icon = normalizeMdiIconName(tile.icon_name);
  const bool dynamic_icon = !icon_disabled && !configured_icon.length();
  String icon_name = climate_tile_base_icon(tile);

  lv_obj_t* icon_label = nullptr;
  String icon_char = getMdiChar(icon_name);
  if (!icon_char.length()) icon_char = getMdiChar("thermostat");
  if (!icon_disabled && FONT_MDI_ICONS) {
    icon_label = lv_label_create(card);
    set_label_style(icon_label, lv_color_white(), FONT_MDI_ICONS);
    lv_label_set_text(icon_label, icon_char.c_str());
    lv_obj_align(
        icon_label, LV_ALIGN_TOP_LEFT,
        -8, -8);
  }

  if (tile.title.length()) {
    lv_obj_t* title = lv_label_create(card);
    set_label_style(title, lv_color_white(),
                    tile_layout::header_title_font());
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, LV_PCT(70));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_RIGHT, 0);
    hometiles_title::tile(title, tile.title.c_str(), true);
    lv_obj_align(
        title, LV_ALIGN_TOP_RIGHT,
        4, 4);
  }

  ClimateTileWidgets* widgets = tile_renderer_get_climate_widgets(grid_type);
  if (widgets && index < TILES_PER_GRID) {
    ClimateTileWidgets& widget = widgets[index];
    // A rebuilt tile must not inherit LVGL pointers from the previous card at
    // the same grid index. This is especially important after resizing to
    // 1x1, where an old multi-slot card otherwise hides the value as if its
    // stale mini-slot layout still existed.
    widget = ClimateTileWidgets{};
    widget.icon_label = icon_label;
    widget.dynamic_icon = dynamic_icon;
    // A newly created card must accept the next state payload even when the
    // same entity/value was rendered by a previous cached card instance.
    widget.last_payload_hash = 0;
    lv_obj_t* value = lv_label_create(card);
    set_label_style(value, lv_color_white(), FONT_VALUE);
    lv_obj_set_width(value, LV_PCT(100));
    lv_obj_set_style_text_align(
        value, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(
        value, "-- \xC2\xB0"
               "C");
    lv_obj_align(value, LV_ALIGN_CENTER, 0, tile_layout::scale(28));
    widget.value_label = value;

    // Every climate size, including 1x1, renders its configured mini content
    // through the same slot path. A 1x1 slot remains visually identical to
    // the former centered value label, but now matches the Web UI semantics.
    const uint8_t slot_count =
        std::min<uint8_t>(
            ClimateTileWidgets::kMaxSlots,
            climateTileSlotCapacity(tile));
    for (uint8_t slot = 0; slot < slot_count; ++slot) {
      widget.slot_roots[slot] =
          create_climate_slot(
              card, grid_type, index, slot);
    }
    ClimateState* states =
        tile_renderer_get_climate_states(grid_type);
    const ClimateState initial_state =
        states ? states[index] : ClimateState{};
    refresh_climate_tile_content(
        grid_type, index, initial_state);
  }

  if (grid_type != GridType::SCREENSAVER && tile.sensor_entity.length()) {
    ClimateEventData* data = new ClimateEventData{grid_type, index};
    const lv_event_code_t popup_event =
        getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS
            ? LV_EVENT_SHORT_CLICKED
            : LV_EVENT_LONG_PRESSED;
    lv_obj_add_event_cb(
        card,
        [](lv_event_t* event) {
          const lv_event_code_t code = lv_event_get_code(event);
          if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
          ClimateEventData* data =
              static_cast<ClimateEventData*>(lv_event_get_user_data(event));
          ClimatePopupInit init = popup_init_for(data);
          if (!init.entity_id.length()) return;
          finish_press_before_popup(event);
          show_climate_popup(init);
        },
        popup_event,
        data);
    lv_obj_add_event_cb(
        card,
        [](lv_event_t* event) {
          if (lv_event_get_code(event) != LV_EVENT_DELETE) return;
          delete static_cast<ClimateEventData*>(lv_event_get_user_data(event));
        },
        LV_EVENT_DELETE,
        data);
  }

  return card;
}

#ifndef TILE_CONFIG_H
#define TILE_CONFIG_H

#include <Arduino.h>
#include <vector>

#include "src/devices/device.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/config/grid_layout.h"
#include "src/core/config/pin_access.h"
#include "src/core/config/tile_color.h"
#include "src/ui/shared/ui_theme.h"
#include "src/tiles/config/tile_icon_colors.h"
#include "src/types/tile_type_policy.h"

// The stored grid: the profile's columns and rows. Tile positions, the
// slots per grid and their validation keep it whatever the panel shows.
static constexpr uint8_t GRID_COLS = Device::kGridCols;
static constexpr uint8_t GRID_ROWS = Device::kGridRows;
static constexpr size_t TILES_PER_GRID = GRID_COLS * GRID_ROWS;

static constexpr int GRID_GAP = Device::kGridGap;
static constexpr int GRID_PAD = Device::kGridPad;

// The shown grid (grid_layout.h), read at boot: the profile's own or, with
// the head bar, fewer and larger cells below the head. Cell sizes and the
// outer margins follow it; with the profile's grid they are the former
// constants (the fixed tracks' leftover split between both outer margins,
// the top and left ones taking the smaller half).
static_assert(grid_layout::profile_grid().pad_left >= Device::kGridPad &&
                  grid_layout::profile_grid().pad_top >= Device::kGridPad,
              "The tile grid must fit the screen");
static const int& GRID_CELL_W = grid_layout::g_shown.cell_w;
static const int& GRID_CELL_H = grid_layout::g_shown.cell_h;
static const int& GRID_PAD_LEFT = grid_layout::g_shown.pad_left;
static const int& GRID_PAD_RIGHT = grid_layout::g_shown.pad_right;
static const int& GRID_PAD_TOP = grid_layout::g_shown.pad_top;
static const int& GRID_PAD_BOTTOM = grid_layout::g_shown.pad_bottom;
// The shown grid's columns and whole rows (an upright layout's half row is
// grid_layout::shown().half_row).
static const uint8_t& GRID_SHOWN_COLS = grid_layout::g_shown.cols;
static const uint8_t& GRID_SHOWN_ROWS = grid_layout::g_shown.rows;
// The room of every layout's grid (grid_layout.h): the size of the tile
// grids' cell arrays. An upright layout can have more rows than GRID_ROWS.
static constexpr uint8_t GRID_SPACE_COLS = grid_layout::kSpaceCols;
static constexpr uint8_t GRID_SPACE_ROWS = grid_layout::kSpaceRows;

// A media tile renders its (often long) title as a horizontally scrolling band the
// full width of the tile. On the 8-inch device every flush is PPA-rotated, and a
// band wider than the safe rotate width jams the single-slot SRM engine (see
// kPpaMinRotateWidth in the Waveshare 8" driver) -> the whole UI drops onto the slow
// CPU rotate until a power cycle. Capping a media tile to a small square keeps its
// title band well under that limit (3 cells ~= 540 px on the 8" 7-col grid).
static constexpr uint8_t MEDIA_TILE_MIN_SPAN = 2;
static constexpr uint8_t MEDIA_TILE_MAX_SPAN = 3;

template <typename T>
static inline void clamp_media_tile_span(TileType type, T& span_w, T& span_h) {
  if (type != TILE_MEDIA) return;
  const uint8_t min_w = GRID_COLS >= MEDIA_TILE_MIN_SPAN
                            ? MEDIA_TILE_MIN_SPAN
                            : GRID_COLS;
  const uint8_t min_h = GRID_ROWS >= MEDIA_TILE_MIN_SPAN
                            ? MEDIA_TILE_MIN_SPAN
                            : GRID_ROWS;
  if (span_w < min_w) span_w = min_w;
  if (span_h < min_h) span_h = min_h;
  if (span_w > MEDIA_TILE_MAX_SPAN) span_w = MEDIA_TILE_MAX_SPAN;
  if (span_h > MEDIA_TILE_MAX_SPAN) span_h = MEDIA_TILE_MAX_SPAN;
}

// A media tile must not be clipped back below its 2x2 minimum at the right or
// bottom edge. Move its position inwards in that case; the layout of every other
// tile type stays as it is.
// The bounds are the profile's grid unless given (the shown grid of a head
// bar layout, tilePlaceCols()/tilePlaceRows()).
template <typename P, typename S>
static inline void clamp_media_tile_layout(TileType type,
                                           P& col, P& row,
                                           S& span_w, S& span_h,
                                           float cols = GRID_COLS, float rows = GRID_ROWS) {
  clamp_media_tile_span(type, span_w, span_h);
  if (type != TILE_MEDIA) return;
  if (span_w > cols) span_w = static_cast<S>(cols);
  if (span_h > rows) span_h = static_cast<S>(rows);
  if (col > cols - span_w) col = static_cast<P>(cols - span_w);
  if (row > rows - span_h) row = static_cast<P>(rows - span_h);
}

// The grid the panel places its tiles in: the profile's (the classic layout)
// or the shown one of a head bar layout, which may have more rows upright and
// a half row (grid_layout.h).
static inline float tilePlaceCols() { return grid_layout::head_bar() ? GRID_SHOWN_COLS : GRID_COLS; }
static inline float tilePlaceRows() { return grid_layout::head_bar() ? grid_layout::shown_rows() : GRID_ROWS; }
// A size and place the type allows inside that grid.
static inline bool tilePlaceSupported(int type, float col, float row, float span_w, float span_h) {
  return grid_layout::head_bar() ? tile_geometry::supported_size(type, col, row, span_w, span_h) &&
                                       grid_layout::inside(col, row, span_w, span_h)
                                 : tile_geometry::supported(type, col, row, span_w, span_h);
}

enum TilePopupOpenMode : uint8_t {
  TILE_POPUP_OPEN_LONG_PRESS = 0,
  TILE_POPUP_OPEN_SHORT_PRESS = 1
};

enum SwitchPopupOpenModeStorage : uint8_t {
  TILE_SWITCH_POPUP_MODE_LEGACY = 0,
  TILE_SWITCH_POPUP_MODE_SHORT = 1,
  TILE_SWITCH_POPUP_MODE_LONG = 2
};

// Value size choices (Tile::sensor_value_font): 0 = default (28 px, the title
// size in half-height tiles), 1 = 20, 2 = 24, 3 = 32, 4 = 40, 5 = 28 (sizes on
// the 1280x800 layouts). Half-height tiles show at most 28.
static constexpr uint8_t SENSOR_VALUE_FONT_MAX = 5;

// A new field also belongs in tileContentEquals() below.
struct Tile {
  TileType type;
  // Stable navigation identity, stored in the two unused V7 reserved bytes.
  uint16_t view_id = 0;
  // Not stored: the tile has no place in the active layout (tile_layouts.h).
  // It is not drawn and takes no room; its places in the other layouts stay.
  bool layout_hidden = false;
  String title;
  String icon_name;
  uint32_t bg_color;
  // Currently always fully opaque for normal tiles. The screensaver uses the
  // same tile object and can draw its background transparently through this.
  // Persisted in the reserved byte V7 already carries.
  uint8_t background_opacity;

  float col;
  float row;
  float span_w;
  float span_h;

  String sensor_entity;
  String sensor_unit;
  uint8_t sensor_decimals;
  uint8_t sensor_value_font;
  uint8_t sensor_display_mode;
  int32_t sensor_gauge_min;
  int32_t sensor_gauge_max;
  uint16_t sensor_gauge_arc;
  uint16_t sensor_gauge_size;
  int16_t sensor_gauge_y_offset;
  int16_t sensor_value_y_offset;
  uint16_t sensor_graph_height;
  uint8_t popup_open_mode;

  String scene_alias;

  String key_macro;
  uint8_t key_code;
  uint8_t key_modifier;

  String image_path;
  uint16_t image_slideshow_sec;

  // Icon disc override: TILE_ICON_DISC_GLOBAL follows the global option.
  uint8_t icon_disc_mode = 0;
  // Glow: a colored icon tints its disc with the same hue.
  bool icon_glow = true;
  // Fixed icon color, color bar and state colors (tile_icon_colors.h), kept
  // in the /_tile_icon_colors sidecar. Empty = the type's default icon colors.
  String icon_colors;

  Tile()
      : type(TILE_EMPTY),
        bg_color(0),
        background_opacity(255),
        col(0),
        row(0),
        span_w(1),
        span_h(1),
        sensor_decimals(0xFF),
        sensor_value_font(0),
        sensor_display_mode(0),
        sensor_gauge_min(0),
        sensor_gauge_max(100),
        sensor_gauge_arc(100),
        sensor_gauge_size(350),
        sensor_gauge_y_offset(12),
        sensor_value_y_offset(0),
        sensor_graph_height(60),
        popup_open_mode(TILE_POPUP_OPEN_LONG_PRESS),
        key_code(0),
        key_modifier(0),
        image_slideshow_sec(10) {}
};

// Everything a tile shows, all fields but its cell (col, row): equal tiles
// can move to another cell without being rebuilt (tiles_show_active_layout_now).
static inline bool tileContentEquals(const Tile& a, const Tile& b) {
  return a.type == b.type && a.view_id == b.view_id && a.layout_hidden == b.layout_hidden && a.title == b.title &&
         a.icon_name == b.icon_name && a.bg_color == b.bg_color &&
         a.background_opacity == b.background_opacity &&
         a.span_w == b.span_w && a.span_h == b.span_h &&
         a.sensor_entity == b.sensor_entity && a.sensor_unit == b.sensor_unit &&
         a.sensor_decimals == b.sensor_decimals &&
         a.sensor_value_font == b.sensor_value_font &&
         a.sensor_display_mode == b.sensor_display_mode &&
         a.sensor_gauge_min == b.sensor_gauge_min &&
         a.sensor_gauge_max == b.sensor_gauge_max &&
         a.sensor_gauge_arc == b.sensor_gauge_arc &&
         a.sensor_gauge_size == b.sensor_gauge_size &&
         a.sensor_gauge_y_offset == b.sensor_gauge_y_offset &&
         a.sensor_value_y_offset == b.sensor_value_y_offset &&
         a.sensor_graph_height == b.sensor_graph_height &&
         a.popup_open_mode == b.popup_open_mode && a.scene_alias == b.scene_alias &&
         a.key_macro == b.key_macro && a.key_code == b.key_code &&
         a.key_modifier == b.key_modifier && a.image_path == b.image_path &&
         a.image_slideshow_sec == b.image_slideshow_sec &&
         a.icon_disc_mode == b.icon_disc_mode && a.icon_glow == b.icon_glow &&
         a.icon_colors == b.icon_colors;
}

// A deleted (empty) tile keeps only its slot geometry. Its entity, texts and
// options must not return when a new tile is placed in the same slot.
static inline void clearEmptyTileFields(Tile& tile) {
  Tile empty;
  empty.col = tile.col;
  empty.row = tile.row;
  empty.span_w = tile.span_w;
  empty.span_h = tile.span_h;
  tile = empty;
}

enum TileIconDiscMode : uint8_t {
  TILE_ICON_DISC_GLOBAL = 0,
  TILE_ICON_DISC_ON = 1,
  TILE_ICON_DISC_OFF = 2
};

static inline uint8_t normalizeTileIconDiscMode(int mode) {
  return (mode >= TILE_ICON_DISC_GLOBAL && mode <= TILE_ICON_DISC_OFF)
             ? static_cast<uint8_t>(mode)
             : TILE_ICON_DISC_GLOBAL;
}

// Canonical icon color record for a type: numeric types keep only the color
// bar, text types only the state lines, Sensor keeps both; icon-and-title
// tiles keep the fixed color and a source entity (with the bar and state
// lines for a "rules" source); Media keeps "From cover"; types without icon
// colors keep none.
static inline String normalizeTileIconColors(int type, const char* record) {
  if (!tileTypeHasIconColors(type) || !record || !*record) return String();
  char out[tile_icon_colors::kMaxRecordBytes + 1];
  const size_t length = tile_icon_colors::normalize(
      record, out, sizeof(out), tileTypeIconColorsByValue(type), tileTypeIconColorsByState(type),
      true, tileTypeRulesUseOwnEntity(type), type == TILE_MEDIA);
  return length ? String(out) : String();
}

// The other entity of a tile's enabled rules (subscriptions), or "".
static inline String tileIconSourceEntity(int type, const String& record) {
  if (!tileTypeHasIconColors(type) || !record.length()) return String();
  const char* entity = nullptr;
  size_t length = 0;
  if (tile_icon_colors::source(record.c_str(), entity, length) == tile_icon_colors::SourceMode::None) {
    return String();
  }
  String out;
  out.reserve(length);
  for (size_t i = 0; i < length; ++i) out += entity[i];
  return out;
}

// Clock/Text/Back use the otherwise unused display mode byte: 0 inherits
// borders, 1 hides them.
static inline bool tileBorderEnabled(const Tile& tile) {
  return (tile.type != TILE_CLOCK && tile.type != TILE_TEXT && tile.type != TILE_BACK) ||
         tile.sensor_display_mode != 1;
}

// Where a tile shows in the shown grid (grid_layout.h). A tile without a
// place in the active layout is not drawn (layout_hidden). Without the head
// bar a span is cut at the grid's edge as always. With the head bar only a
// tile wholly inside the grid shows, and the Back tile gives way to the
// head's X.
static inline bool shownTileLayout(const Tile& tile, float& col, float& row, float& span_w, float& span_h) {
  if (tile.layout_hidden || tile.col >= tilePlaceCols() || tile.row >= tilePlaceRows()) return false;
  col = tile.col;
  row = tile.row;
  span_w = tile.span_w < 0.5f ? 1 : tile.span_w;
  span_h = tile.span_h < 0.5f ? 1 : tile.span_h;
  clamp_media_tile_layout(tile.type, col, row, span_w, span_h, tilePlaceCols(), tilePlaceRows());
  if (grid_layout::head_bar()) {
    if (tile.type == TILE_BACK) return false;
    return grid_layout::inside(col, row, span_w, span_h);
  }
  if (span_w > GRID_COLS - col) span_w = GRID_COLS - col;
  if (span_h > GRID_ROWS - row) span_h = GRID_ROWS - row;
  return true;
}

// Weather uses the same byte for its icons: 0 draws filled, colored weather
// icons, 1 the white MDI outlines.
static inline bool weatherColoredIcons(const Tile& tile) {
  return tile.type != TILE_WEATHER || tile.sensor_display_mode != 1;
}

// Climate tile content is packed into sensor_gauge_min. Climate tiles do not
// use the sensor gauge range, so this preserves the existing V7 storage layout
// while allowing six independently configurable 4-bit slots.
enum ClimateTileContent : uint8_t {
  CLIMATE_TILE_CONTENT_AUTO = 0,
  CLIMATE_TILE_CONTENT_EMPTY = 1,
  CLIMATE_TILE_CONTENT_CURRENT_TEMPERATURE = 2,
  CLIMATE_TILE_CONTENT_CURRENT_HUMIDITY = 3,
  CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE = 4,
  CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE_LOW = 5,
  CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE_HIGH = 6,
  CLIMATE_TILE_CONTENT_TARGET_HUMIDITY = 7,
  CLIMATE_TILE_CONTENT_HVAC_MODE = 8
};

static constexpr uint8_t CLIMATE_TILE_MAX_CONTENT_SLOTS = 6;
static constexpr uint32_t CLIMATE_TILE_CONTENT_PACKED_MASK = 0x00FFFFFFu;
static constexpr uint8_t CLIMATE_TILE_MAX_GRID_COLUMNS = GRID_COLS;
static constexpr uint8_t CLIMATE_TILE_MAX_GRID_ROWS =
    static_cast<uint8_t>(GRID_ROWS * 2u - 1u);
static constexpr uint8_t CLIMATE_TILE_MAX_GRID_CELLS =
    static_cast<uint8_t>(
        CLIMATE_TILE_MAX_GRID_COLUMNS * CLIMATE_TILE_MAX_GRID_ROWS);

// Climate tile "Layout" (Web Admin): 0 = title only (the title top right,
// the current temperature as a mini field), 1 = with value (title and
// "Cooling · 20.5 °C" left beside the disc, like the Switch header). Stored
// in sensor_display_mode, which the Climate tile does not use otherwise.
static inline bool climateTileShowsValue(const Tile& tile) {
  return tile.type == TILE_CLIMATE && tile.sensor_display_mode == 1;
}

static inline uint8_t climateTileGridColumns(const Tile& tile) {
  const uint8_t span_w = tile.span_w < 1 ? 1 : tile.span_w;
  return span_w > CLIMATE_TILE_MAX_GRID_COLUMNS
             ? CLIMATE_TILE_MAX_GRID_COLUMNS
             : span_w;
}

// One mini-grid row per half cell below the header row, so half steps add a
// row: 1 -> 1, 1.5 -> 2, 2 -> 3, 2.5 -> 4. Whole sizes keep their rows.
static inline uint8_t climateTileGridRows(const Tile& tile) {
  const float span_h =
      tile.span_h < 1
          ? 1.0f
          : (tile.span_h > GRID_ROWS ? static_cast<float>(GRID_ROWS) : tile.span_h);
  return static_cast<uint8_t>(static_cast<uint8_t>(span_h * 2.0f + 0.5f) - 1u);
}

// Adjustable climate values consume two cells. Their preferred orientation is
// packed into sensor_gauge_max (two bits per configured item). A magic prefix
// distinguishes the climate layout data from the legacy sensor default (100).
enum ClimateTileTargetLayout : uint8_t {
  CLIMATE_TILE_TARGET_LAYOUT_AUTO = 0,
  CLIMATE_TILE_TARGET_LAYOUT_HORIZONTAL = 1,
  CLIMATE_TILE_TARGET_LAYOUT_VERTICAL = 2
};

static constexpr uint32_t CLIMATE_TILE_LAYOUT_PACKED_MAGIC = 0x434C0000u;
static constexpr uint32_t CLIMATE_TILE_LAYOUT_PACKED_MAGIC_MASK = 0xFFFF0000u;
static constexpr uint32_t CLIMATE_TILE_LAYOUT_PACKED_VALUE_MASK = 0x00000FFFu;

static inline uint8_t climateTileSlotCapacity(const Tile& tile) {
  const uint8_t grid_cells = static_cast<uint8_t>(
      climateTileGridColumns(tile) * climateTileGridRows(tile));
  return grid_cells < CLIMATE_TILE_MAX_CONTENT_SLOTS
             ? grid_cells
             : CLIMATE_TILE_MAX_CONTENT_SLOTS;
}

static inline ClimateTileContent getClimateTileSlotContent(
    const Tile& tile, uint8_t slot_index) {
  if (slot_index >= CLIMATE_TILE_MAX_CONTENT_SLOTS) {
    return CLIMATE_TILE_CONTENT_AUTO;
  }
  const uint32_t packed =
      static_cast<uint32_t>(tile.sensor_gauge_min) &
      CLIMATE_TILE_CONTENT_PACKED_MASK;
  const uint8_t raw =
      static_cast<uint8_t>((packed >> (slot_index * 4)) & 0x0Fu);
  if (raw > CLIMATE_TILE_CONTENT_HVAC_MODE) {
    return CLIMATE_TILE_CONTENT_AUTO;
  }
  return static_cast<ClimateTileContent>(raw);
}

static inline void setClimateTileSlotContent(
    Tile& tile, uint8_t slot_index, ClimateTileContent content) {
  if (slot_index >= CLIMATE_TILE_MAX_CONTENT_SLOTS) return;
  uint32_t packed =
      static_cast<uint32_t>(tile.sensor_gauge_min) &
      CLIMATE_TILE_CONTENT_PACKED_MASK;
  const uint32_t shift = static_cast<uint32_t>(slot_index) * 4u;
  packed &= ~(0x0Fu << shift);
  packed |=
      (static_cast<uint32_t>(content) & 0x0Fu) << shift;
  tile.sensor_gauge_min = static_cast<int32_t>(packed);
}

static inline uint32_t getClimateTileLayoutsPacked(const Tile& tile) {
  const uint32_t stored = static_cast<uint32_t>(tile.sensor_gauge_max);
  if ((stored & CLIMATE_TILE_LAYOUT_PACKED_MAGIC_MASK) !=
      CLIMATE_TILE_LAYOUT_PACKED_MAGIC) {
    return 0;
  }
  return stored & CLIMATE_TILE_LAYOUT_PACKED_VALUE_MASK;
}

static inline ClimateTileTargetLayout getClimateTileTargetLayout(
    const Tile& tile, uint8_t slot_index) {
  if (slot_index >= CLIMATE_TILE_MAX_CONTENT_SLOTS) {
    return CLIMATE_TILE_TARGET_LAYOUT_AUTO;
  }
  const uint32_t packed = getClimateTileLayoutsPacked(tile);
  const uint8_t raw =
      static_cast<uint8_t>((packed >> (slot_index * 2)) & 0x03u);
  if (raw > CLIMATE_TILE_TARGET_LAYOUT_VERTICAL) {
    return CLIMATE_TILE_TARGET_LAYOUT_AUTO;
  }
  return static_cast<ClimateTileTargetLayout>(raw);
}

static inline void setClimateTileTargetLayout(
    Tile& tile, uint8_t slot_index, ClimateTileTargetLayout layout) {
  if (slot_index >= CLIMATE_TILE_MAX_CONTENT_SLOTS) return;
  uint32_t packed = getClimateTileLayoutsPacked(tile);
  const uint32_t shift = static_cast<uint32_t>(slot_index) * 2u;
  packed &= ~(0x03u << shift);
  packed |= (static_cast<uint32_t>(layout) & 0x03u) << shift;
  tile.sensor_gauge_max = static_cast<int32_t>(
      CLIMATE_TILE_LAYOUT_PACKED_MAGIC |
      (packed & CLIMATE_TILE_LAYOUT_PACKED_VALUE_MASK));
}

// Climate mini-tile geometry is stored in the otherwise unused scene_alias
// field. CLG2 stores four hexadecimal digits per configured item:
//   bits 0-2   column (0..7)
//   bits 3-6   row (0..15)
//   bits 7-9   width minus one (0..7)
//   bits 10-13 height minus one (0..15)
// Six items therefore need 24 digits and still fit in the existing 32-byte
// storage field. The former CLG1 2 x 3 format remains readable.
struct ClimateTileItemGeometry {
  uint8_t col = 0;
  uint8_t row = 0;
  uint8_t span_w = 1;
  uint8_t span_h = 1;
};

static constexpr const char* CLIMATE_TILE_GEOMETRY_PREFIX = "CLG2:";
static constexpr size_t CLIMATE_TILE_GEOMETRY_PREFIX_LENGTH = 5;
static constexpr uint8_t CLIMATE_TILE_GEOMETRY_HEX_DIGITS =
    CLIMATE_TILE_MAX_CONTENT_SLOTS * 4;
static constexpr const char* CLIMATE_TILE_GEOMETRY_LEGACY_PREFIX = "CLG1:";
static constexpr uint8_t CLIMATE_TILE_GEOMETRY_LEGACY_HEX_DIGITS = 9;

static inline bool climateTileHexNibble(char c, uint8_t& nibble) {
  if (c >= '0' && c <= '9') {
    nibble = static_cast<uint8_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    nibble = static_cast<uint8_t>(c - 'a' + 10);
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    nibble = static_cast<uint8_t>(c - 'A' + 10);
    return true;
  }
  nibble = 0;
  return false;
}

static inline bool climateTileGeometryHasPrefix(const String& text) {
  return text.startsWith(CLIMATE_TILE_GEOMETRY_PREFIX) ||
         text.startsWith(CLIMATE_TILE_GEOMETRY_LEGACY_PREFIX);
}

static inline bool parseClimateTileGeometry(
    const Tile& tile, uint64_t& packed) {
  const String& text = tile.scene_alias;
  const bool current =
      text.startsWith(CLIMATE_TILE_GEOMETRY_PREFIX);
  const bool legacy =
      text.startsWith(CLIMATE_TILE_GEOMETRY_LEGACY_PREFIX);
  const uint8_t digits =
      current
          ? CLIMATE_TILE_GEOMETRY_HEX_DIGITS
          : (legacy
                 ? CLIMATE_TILE_GEOMETRY_LEGACY_HEX_DIGITS
                 : 0);
  if (digits == 0 ||
      text.length() <
          CLIMATE_TILE_GEOMETRY_PREFIX_LENGTH + digits) {
    return false;
  }
  packed = 0;
  const size_t offset = CLIMATE_TILE_GEOMETRY_PREFIX_LENGTH;
  for (uint8_t i = 0; i < digits; ++i) {
    const char c = text[offset + i];
    uint8_t nibble = 0;
    if (!climateTileHexNibble(c, nibble)) {
      packed = 0;
      return false;
    }
    // Only the legacy format fits in uint64_t. Current callers use the
    // returned value merely as a validity flag for CLG2.
    if (legacy) packed = (packed << 4) | nibble;
  }
  return true;
}

// Stored position of one mini tile; build_slot_kinds clamps it to the grid.
static inline ClimateTileItemGeometry getClimateTileItemGeometry(
    const Tile& tile, uint8_t item_index) {
  const uint8_t columns = climateTileGridColumns(tile);
  const uint8_t rows = climateTileGridRows(tile);
  ClimateTileItemGeometry geometry;
  uint64_t packed = 0;
  if (item_index < CLIMATE_TILE_MAX_CONTENT_SLOTS &&
      parseClimateTileGeometry(tile, packed)) {
    if (tile.scene_alias.startsWith(CLIMATE_TILE_GEOMETRY_PREFIX)) {
      uint16_t raw = 0;
      const size_t offset =
          CLIMATE_TILE_GEOMETRY_PREFIX_LENGTH +
          static_cast<size_t>(item_index) * 4u;
      for (uint8_t digit = 0; digit < 4; ++digit) {
        uint8_t nibble = 0;
        climateTileHexNibble(tile.scene_alias[offset + digit], nibble);
        raw = static_cast<uint16_t>((raw << 4) | nibble);
      }
      geometry.col = raw & 0x07u;
      geometry.row = (raw >> 3) & 0x0Fu;
      geometry.span_w =
          static_cast<uint8_t>(((raw >> 7) & 0x07u) + 1u);
      geometry.span_h =
          static_cast<uint8_t>(((raw >> 10) & 0x0Fu) + 1u);
    } else {
      const uint8_t raw = static_cast<uint8_t>(
          (packed >> (item_index * 6u)) & 0x3Fu);
      geometry.col = raw & 0x01u;
      geometry.row = (raw >> 1) & 0x03u;
      geometry.span_w =
          static_cast<uint8_t>(((raw >> 3) & 0x01u) + 1u);
      geometry.span_h =
          static_cast<uint8_t>(((raw >> 4) & 0x03u) + 1u);
    }
  } else {
    geometry.col = item_index % columns;
    geometry.row = item_index / columns;
    const ClimateTileContent content =
        getClimateTileSlotContent(tile, item_index);
    const bool adjustable =
        content >= CLIMATE_TILE_CONTENT_TARGET_TEMPERATURE &&
        content <= CLIMATE_TILE_CONTENT_TARGET_HUMIDITY;
    if (adjustable) {
      const ClimateTileTargetLayout layout =
          getClimateTileTargetLayout(tile, item_index);
      const bool can_horizontal =
          columns >= 2 && geometry.col + 1 < columns;
      const bool can_vertical =
          rows >= 2 && geometry.row + 1 < rows;
      if (layout == CLIMATE_TILE_TARGET_LAYOUT_HORIZONTAL &&
          can_horizontal) {
        geometry.span_w = 2;
      } else if (layout == CLIMATE_TILE_TARGET_LAYOUT_VERTICAL &&
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
  }
  // Deliberately not clamped into the current grid: placement orders the
  // items by where they were stored and clamps afterwards, so an item from a
  // row that no longer exists cannot jump ahead of the items above it.
  return geometry;
}

static inline uint8_t getTilePopupOpenMode(const Tile& tile) {
  if (tile.type == TILE_SWITCH) {
    return (tile.key_code == TILE_SWITCH_POPUP_MODE_LONG)
               ? TILE_POPUP_OPEN_LONG_PRESS
               : TILE_POPUP_OPEN_SHORT_PRESS;
  }
  if (!tileTypeStoresPopupModeDirectly(tile.type)) {
    return TILE_POPUP_OPEN_LONG_PRESS;
  }
  return (tile.popup_open_mode == TILE_POPUP_OPEN_SHORT_PRESS)
             ? TILE_POPUP_OPEN_SHORT_PRESS
             : TILE_POPUP_OPEN_LONG_PRESS;
}

static inline void setTilePopupOpenMode(Tile& tile, uint8_t mode) {
  if (tile.type == TILE_SWITCH) {
    const uint8_t normalized =
        (mode == TILE_POPUP_OPEN_SHORT_PRESS)
            ? TILE_POPUP_OPEN_SHORT_PRESS
            : TILE_POPUP_OPEN_LONG_PRESS;
    tile.popup_open_mode = normalized;
    // Switch tiles previously had no configurable popup mode. Use the otherwise
    // unused key_code slot to distinguish legacy tiles (default to short press)
    // from an explicitly saved long-press configuration.
    tile.key_code = (normalized == TILE_POPUP_OPEN_SHORT_PRESS)
                        ? TILE_SWITCH_POPUP_MODE_SHORT
                        : TILE_SWITCH_POPUP_MODE_LONG;
    tile.key_modifier = 0;
    return;
  }
  if (!tileTypeStoresPopupModeDirectly(tile.type)) return;
  tile.popup_open_mode = (mode == TILE_POPUP_OPEN_SHORT_PRESS)
                             ? TILE_POPUP_OPEN_SHORT_PRESS
                             : TILE_POPUP_OPEN_LONG_PRESS;
}

struct TileGridConfig {
  Tile tiles[TILES_PER_GRID];
};

// Allocates a default TileGridConfig for a grid that lives as long as the
// firmware: PSRAM first, internal RAM as fallback. Aborts when neither has
// room, because the caller cannot run without its grid.
TileGridConfig* allocateTileGridStorage(const char* name);

static constexpr uint32_t TILE_BG_COLOR_RGB_MASK = 0x00FFFFFFu;
static constexpr uint32_t TILE_BG_COLOR_EXPLICIT = 0x01000000u;

static inline uint32_t makeTileBgColor(uint32_t rgb) {
  return (rgb & TILE_BG_COLOR_RGB_MASK) | TILE_BG_COLOR_EXPLICIT;
}

static inline bool tileBgColorIsSet(const Tile& tile) {
  return tile.bg_color != 0;
}

static inline uint32_t tileBgColorRgb(const Tile& tile) {
  return tile.bg_color & TILE_BG_COLOR_RGB_MASK;
}

// Background of tiles without their own color: the global default tile color
// from the display settings (tile_color::kDefault until the user picks one).
uint32_t tileDefaultBgColor();

// A stored built-in default grey (saved explicitly by older editors) counts
// as "no own color" and follows the global default tile color like an unset
// color. Every other stored color is kept.
static inline bool tileBgColorFollowsDefault(uint32_t stored) {
  return stored == 0 || tile_color::isDefaultGrey(stored);
}

static inline uint32_t tileBgColorOrDefault(const Tile& tile, uint32_t default_color) {
  if (!tileBgColorIsSet(tile)) return default_color & TILE_BG_COLOR_RGB_MASK;
  if (tileBgColorFollowsDefault(tile.bg_color)) return tileDefaultBgColor();
  // Light theme: a black tile (the dark screen's color, a clock meant to sit
  // on the screen) takes the light screen's color instead of a black block.
  if (ui_theme::light() && tileBgColorRgb(tile) == 0) return ui_theme::screen();
  return tileBgColorRgb(tile);
}

struct FolderEntry {
  uint16_t id;
  uint16_t parent_id;
  char name[32];
  char icon_name[32];
  bool pin_enabled;
  uint8_t pin_salt[pin_access::kSaltSize];
  uint8_t pin_hash[pin_access::kHashSize];
  char pin_value[pin_access::kUserPinMaxDigits + 1];
};

enum class SettingsTileVisibilityResult : uint8_t {
  Success = 0,
  NoFreeCell,
  StorageError,
};

// Lightweight per-tile projection used by background scans (cache refresh,
// MQTT dynamic-route rebuild) that only need type + entity id, not the full
// Tile (title/icon/scene/macro/image_path). Skips ~5 of 6 per-tile String
// allocations that a full TileGridConfig load pays for every tile.
struct TileEntitySlot {
  TileType type = TILE_EMPTY;
  String sensor_entity;
  // The other entity of the tile's rules (tile_icon_colors.h), or "".
  String rule_entity;
};

// Read-only view of one slot of the PSRAM folder entity cache, see
// TileConfig::getFolderEntitiesCached(). The entity pointers point into the
// cache and stay valid only until the next getFolderEntitiesCached() call for
// the same folder: use them right away, do not keep them.
struct FolderEntitySlotView {
  TileType type = TILE_EMPTY;
  const char* entity = "";       // Never nullptr.
  const char* rule_entity = "";  // Never nullptr; the rules' other entity.
};

struct FolderEntityCacheEntry;

class TileConfig {
public:
  // Stand-alone grid for the screensaver. Its reserved storage ID is not a
  // folder, so it appears neither in the folder list nor in the navigation. It
  // is still stored in exactly the same packed LittleFS format as every normal
  // tile grid.
  static constexpr uint16_t kScreensaverGridStorageId = 0xFFFE;
  static constexpr uint16_t rootFolderId() { return 0; }

  TileConfig();

  bool load();
  bool loadFolderGrid(uint16_t folder_id, TileGridConfig& out);
  // The classic layout's places whatever layout is shown (tile_layouts.h):
  // export, import and the Layout window work on them.
  bool loadFolderGridClassic(uint16_t folder_id, TileGridConfig& out);
  bool saveFolderGridClassic(uint16_t folder_id, TileGridConfig& grid);
  bool loadScreensaverGrid(TileGridConfig& out);
  bool loadFolderGridEntitiesOnly(uint16_t folder_id, TileEntitySlot* out, size_t count);
  // Like loadFolderGridEntitiesOnly(), but through a PSRAM cache: the flash
  // read (~20ms per folder, more than half of a 33ms frame at 30fps) only
  // happens on first access or after a grid change. Call from the loop task
  // ONLY, because it rebuilds the cache; invalidateFolderEntityCache() in turn
  // is allowed from any task.
  bool getFolderEntitiesCached(uint16_t folder_id, FolderEntitySlotView* out, size_t count);
  void invalidateFolderEntityCache();
  bool saveFolderGrid(uint16_t folder_id, TileGridConfig& grid);
  // A Web Admin reorder shows the active folder's new grid before its flash
  // write; saveFolderGrid() then stores and keeps the same grid. False for
  // any other folder.
  bool previewActiveFolderGrid(uint16_t folder_id, const TileGridConfig& grid);
  uint32_t viewRevision() const { return view_revision_; }
  bool saveScreensaverGrid(const TileGridConfig& grid);

  bool setActiveFolder(uint16_t folder_id);
  bool setActiveFolderCached(uint16_t folder_id, const TileGridConfig& grid);
  uint16_t getActiveFolderId() const { return active_folder_id; }
  const TileGridConfig& getActiveGrid() const { return activeGrid(); }
  TileGridConfig& getActiveGrid() { return activeGrid(); }

  const FolderEntry* getFolder(uint16_t folder_id) const;
  uint16_t getFolderParent(uint16_t folder_id) const;
  const std::vector<FolderEntry>& getFolders() const { return folders; }
  bool folderExists(uint16_t folder_id) const;
  bool createFolder(uint16_t parent_id, const String& name, const String& icon, uint16_t& out_id);
  bool updateFolder(uint16_t folder_id, const String& name, const String& icon);
  bool deleteFolder(uint16_t folder_id);
  bool isFolderPinEnabled(uint16_t folder_id) const;
  bool setFolderPin(uint16_t folder_id, const String& pin);
  bool clearFolderPin(uint16_t folder_id);
  bool verifyFolderPin(uint16_t folder_id, const char* pin) const;
  bool getFolderPin(uint16_t folder_id, String& out) const;
  bool getSettingsTile(Tile& out);
  SettingsTileVisibilityResult validateSettingsTileVisible(
      bool visible, float target_col = -1, float target_row = -1);
  SettingsTileVisibilityResult setSettingsTileVisible(
      bool visible, float target_col = -1, float target_row = -1);
  // Parks or restores the Settings tile in the visible Home grid without
  // writing anything, like previewActiveFolderGrid() for a reorder; the save
  // that follows stores the same grid. False when nothing changed.
  bool previewSettingsTileVisible(bool visible, float target_col,
                                  float target_row);
  // Slot of the Settings tile in the Home grid, -1 while it is hidden.
  int settingsTileIndex();

private:
  volatile uint32_t view_revision_ = 1;
  static constexpr uint16_t kRootFolderId = 0;
  static constexpr uint16_t kInvalidFolderId = 0xFFFF;

  // PSRAM, allocated on first use (load() in setup()) because PSRAM is not
  // ready while the global constructors run. Never freed.
  mutable TileGridConfig* active_grid_ = nullptr;
  TileGridConfig& activeGrid() const;
  void adoptActiveGrid(uint16_t folder_id, const TileGridConfig& grid);
  uint16_t active_folder_id = kRootFolderId;
  std::vector<FolderEntry> folders;

  // Folder entity cache in PSRAM: one entry per folder with the type and
  // entity ID of every tile. Invalidation runs through a global generation
  // counter because grid saves also come from the web task. The writer only
  // raises that counter, with no free and no rebuild; the cache is rebuilt
  // exclusively on the loop task at the next access.
  FolderEntityCacheEntry* folder_entity_cache_ = nullptr;
  size_t folder_entity_cache_count_ = 0;
  volatile uint32_t folder_entity_cache_gen_ = 1;
  FolderEntityCacheEntry* findFolderEntityCacheEntry(uint16_t folder_id);
  FolderEntityCacheEntry* storeFolderEntityCache(uint16_t folder_id,
                                                 const TileEntitySlot* slots,
                                                 uint32_t built_gen);

  bool loadFolders();
  bool saveFolders() const;
  bool loadFolderAccess();
  bool saveFolderAccess() const;
  // layout_places: the active layout's places (tile_layouts.h); false keeps
  // the stored classic ones.
  bool loadGrid(uint16_t folder_id, TileGridConfig& grid,
                bool ensure_navigation_tile = true, bool layout_places = true);
  bool saveGrid(uint16_t folder_id, const TileGridConfig& grid,
                bool ensure_navigation_tile = true);
  // Normalizes and saves the caller's grid without another full copy.
  // layout_places: the grid holds the active layout's places; false: the
  // classic ones.
  bool saveGridInPlace(uint16_t folder_id, TileGridConfig& grid,
                       bool ensure_navigation_tile = true, bool layout_places = true);
  uint16_t nextFolderId() const;
  void ensureRootFolder();
  bool ensureSettingsTile(TileGridConfig& grid, float target_col = -1,
                          float target_row = -1);
  bool removeSettingsTiles(TileGridConfig& grid);
  bool applySettingsTilePolicy(TileGridConfig& grid);
  bool ensureBackTile(uint16_t folder_id, TileGridConfig& grid);
};

extern TileConfig tileConfig;

#endif // TILE_CONFIG_H

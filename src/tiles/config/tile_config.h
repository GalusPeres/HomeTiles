#ifndef TILE_CONFIG_H
#define TILE_CONFIG_H

#include <Arduino.h>
#include <vector>

#include "src/devices/device.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/core/config/pin_access.h"
#include "src/types/tile_type_policy.h"

static constexpr uint8_t GRID_COLS = Device::kGridCols;
static constexpr uint8_t GRID_ROWS = Device::kGridRows;
static constexpr size_t TILES_PER_GRID = GRID_COLS * GRID_ROWS;

static constexpr int GRID_GAP = Device::kGridGap;
static constexpr int GRID_PAD = Device::kGridPad;
static constexpr int GRID_CELL_W = Device::kGridCellW;
static constexpr int GRID_CELL_H = Device::kGridCellH;

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
template <typename P, typename S>
static inline void clamp_media_tile_layout(TileType type,
                                           P& col, P& row,
                                           S& span_w, S& span_h) {
  clamp_media_tile_span(type, span_w, span_h);
  if (type != TILE_MEDIA) return;
  if (span_w > GRID_COLS) span_w = GRID_COLS;
  if (span_h > GRID_ROWS) span_h = GRID_ROWS;
  if (col > GRID_COLS - span_w) col = GRID_COLS - span_w;
  if (row > GRID_ROWS - span_h) row = GRID_ROWS - span_h;
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

struct Tile {
  TileType type;
  // Stable navigation identity, stored in the two unused V7 reserved bytes.
  uint16_t view_id = 0;
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

// Clock/Text use the otherwise unused display mode byte: 0 inherits borders, 1 hides them.
static inline bool tileBorderEnabled(const Tile& tile) {
  return (tile.type != TILE_CLOCK && tile.type != TILE_TEXT) || tile.sensor_display_mode != 1;
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

static inline uint8_t climateTileGridColumns(const Tile& tile) {
  const uint8_t span_w = tile.span_w < 1 ? 1 : tile.span_w;
  return span_w > CLIMATE_TILE_MAX_GRID_COLUMNS
             ? CLIMATE_TILE_MAX_GRID_COLUMNS
             : span_w;
}

static inline uint8_t climateTileGridRows(const Tile& tile) {
  const uint8_t span_h =
      tile.span_h < 1
          ? 1
          : (tile.span_h > GRID_ROWS ? GRID_ROWS : tile.span_h);
  return static_cast<uint8_t>(span_h * 2u - 1u);
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
  if (geometry.col >= columns) geometry.col = columns - 1;
  if (geometry.row >= rows) geometry.row = rows - 1;
  if (geometry.span_w < 1) geometry.span_w = 1;
  if (geometry.span_h < 1) geometry.span_h = 1;
  if (geometry.span_w > columns - geometry.col) {
    geometry.span_w = columns - geometry.col;
  }
  if (geometry.span_h > rows - geometry.row) {
    geometry.span_h = rows - geometry.row;
  }
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

static inline uint32_t tileBgColorOrDefault(const Tile& tile, uint32_t default_color) {
  return tileBgColorIsSet(tile) ? tileBgColorRgb(tile) : (default_color & TILE_BG_COLOR_RGB_MASK);
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
};

// Read-only view of one slot of the PSRAM folder entity cache, see
// TileConfig::getFolderEntitiesCached(). The entity pointers point into the
// cache and stay valid only until the next getFolderEntitiesCached() call for
// the same folder: use them right away, do not keep them.
struct FolderEntitySlotView {
  TileType type = TILE_EMPTY;
  const char* entity = "";  // Never nullptr.
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
  uint32_t viewRevision() const { return view_revision_; }
  bool saveScreensaverGrid(const TileGridConfig& grid);

  bool setActiveFolder(uint16_t folder_id);
  bool setActiveFolderCached(uint16_t folder_id, const TileGridConfig& grid);
  uint16_t getActiveFolderId() const { return active_folder_id; }
  const TileGridConfig& getActiveGrid() const { return active_grid; }
  TileGridConfig& getActiveGrid() { return active_grid; }

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
      bool visible, int target_col = -1, int target_row = -1);
  SettingsTileVisibilityResult setSettingsTileVisible(
      bool visible, int target_col = -1, int target_row = -1);

private:
  volatile uint32_t view_revision_ = 1;
  static constexpr uint16_t kRootFolderId = 0;
  static constexpr uint16_t kInvalidFolderId = 0xFFFF;

  TileGridConfig active_grid;
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
  bool loadGrid(uint16_t folder_id, TileGridConfig& grid,
                bool ensure_navigation_tile = true);
  bool saveGrid(uint16_t folder_id, const TileGridConfig& grid,
                bool ensure_navigation_tile = true);
  uint16_t nextFolderId() const;
  void ensureRootFolder();
  bool ensureSettingsTile(TileGridConfig& grid, int target_col = -1,
                          int target_row = -1);
  bool removeSettingsTiles(TileGridConfig& grid);
  bool applySettingsTilePolicy(TileGridConfig& grid);
  bool ensureBackTile(uint16_t folder_id, TileGridConfig& grid);
};

extern TileConfig tileConfig;

#endif // TILE_CONFIG_H

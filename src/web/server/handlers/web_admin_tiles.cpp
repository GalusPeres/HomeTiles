#include "src/web/server/web_admin.h"
#include "src/core/text/title_text.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/web/server/render/web_admin_html.h"
#include "src/core/i18n/i18n.h"
#include "src/core/config/pin_access.h"
#include "src/network/bridge/device_entities.h"
#include "src/io/hardware_io.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/tiles/config/tile_config.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/web/server/web_admin_utils.h"
#include "src/web/server/handlers/web_admin_tile_helpers.h"
#include "src/types/types_registry.h"
#include "src/types/energy/energy_data.h"
#include <algorithm>
#include <vector>
#include <memory>
#include <new>
#include "src/web/server/handlers/web_admin_handler_utils.h"

using namespace web_admin_handlers;

namespace {

static String dynamicMqttEntityForTile(const Tile& tile) {
  if (!tileTypeHasDynamicMqttRoute(tile.type)) return "";
  String entity = tile.sensor_entity;
  entity.trim();
  return entity;
}

static bool tileChangeAffectsDynamicMqttRoutes(const Tile& before, const Tile& after) {
  const String before_entity = dynamicMqttEntityForTile(before);
  const String after_entity = dynamicMqttEntityForTile(after);
  const bool before_has_route = before_entity.length() > 0;
  const bool after_has_route = after_entity.length() > 0;
  if (before_has_route != after_has_route) return true;
  if (!before_has_route && !after_has_route) return false;
  if (before.type != after.type) return true;
  return !before_entity.equalsIgnoreCase(after_entity);
}

void appendKeyValueMapJson(String& out, const String& map) {
  out += "{";
  bool first = true;
  int start = 0;

  while (start < map.length()) {
    int eqPos = map.indexOf('=', start);
    if (eqPos < 0) break;

    int endPos = map.indexOf('\n', eqPos);
    if (endPos < 0) endPos = map.length();

    String key = map.substring(start, eqPos);
    String value = map.substring(eqPos + 1, endPos);

    key.trim();
    value.trim();

    if (key.length() > 0 && value.length() > 0) {
      if (!first) out += ",";
      out += "\"";
      appendJsonEscaped(out, key);
      out += "\":\"";
      appendJsonEscaped(out, value);
      out += "\"";
      first = false;
    }

    start = endPos + 1;
  }

  out += "}";
}

struct TileRect {
  uint8_t col;
  uint8_t row;
  uint8_t span_w;
  uint8_t span_h;
};

static bool buildTileRect(uint8_t col, uint8_t row, uint8_t span_w, uint8_t span_h, TileRect& out) {
  if (col >= GRID_COLS || row >= GRID_ROWS) return false;
  if (span_w < 1 || span_h < 1) return false;
  if (span_w > GRID_COLS - col) return false;
  if (span_h > GRID_ROWS - row) return false;
  out = TileRect{col, row, span_w, span_h};
  return true;
}

static bool getTileRect(const Tile& tile, TileRect& out) {
  uint8_t col = tile.col;
  uint8_t row = tile.row;
  uint8_t span_w = tile.span_w < 1 ? 1 : tile.span_w;
  uint8_t span_h = tile.span_h < 1 ? 1 : tile.span_h;
  clamp_media_tile_layout(tile.type, col, row, span_w, span_h);
  return buildTileRect(col, row, span_w, span_h, out);
}

static bool rectsOverlap(const TileRect& a, const TileRect& b) {
  return !(a.col + a.span_w <= b.col ||
           b.col + b.span_w <= a.col ||
           a.row + a.span_h <= b.row ||
           b.row + b.span_h <= a.row);
}

static bool placementOverlaps(const TileGridConfig& grid, size_t self_index, const TileRect& rect, size_t ignore_index = static_cast<size_t>(-1)) {
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (i == self_index || i == ignore_index) continue;
    const Tile& other = grid.tiles[i];
    if (other.type == TILE_EMPTY) continue;
    TileRect other_rect{};
    if (!getTileRect(other, other_rect)) continue;
    if (rectsOverlap(rect, other_rect)) return true;
  }
  return false;
}

static bool indexInList(size_t value, const std::vector<size_t>& values) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

static bool placementOverlapsAny(
    const TileGridConfig& grid,
    size_t self_index,
    const TileRect& rect,
    const std::vector<size_t>& ignore_indices) {
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (i == self_index || indexInList(i, ignore_indices)) continue;
    const Tile& other = grid.tiles[i];
    if (other.type == TILE_EMPTY) continue;
    TileRect other_rect{};
    if (!getTileRect(other, other_rect)) continue;
    if (rectsOverlap(rect, other_rect)) return true;
  }
  return false;
}

struct TilePosSnapshot {
  size_t index;
  uint8_t col;
  uint8_t row;
};

struct PlacementCandidate {
  uint8_t col;
  uint8_t row;
  uint16_t distance;
};

static uint16_t manhattanDistance(uint8_t col_a, uint8_t row_a, uint8_t col_b, uint8_t row_b) {
  const int dx = static_cast<int>(col_a) - static_cast<int>(col_b);
  const int dy = static_cast<int>(row_a) - static_cast<int>(row_b);
  return static_cast<uint16_t>(abs(dx) + abs(dy));
}

static std::vector<PlacementCandidate> buildPlacementCandidates(
    uint8_t span_w,
    uint8_t span_h,
    int preferred_col,
    int preferred_row,
    uint8_t first_row = 0) {
  std::vector<PlacementCandidate> out;
  for (uint8_t row = first_row; row < GRID_ROWS; ++row) {
    for (uint8_t col = 0; col < GRID_COLS; ++col) {
      TileRect rect{};
      if (!buildTileRect(col, row, span_w, span_h, rect)) continue;
      uint16_t distance = static_cast<uint16_t>(row * GRID_COLS + col);
      if (preferred_col >= 0 && preferred_row >= 0) {
        distance = manhattanDistance(col, row,
                                     static_cast<uint8_t>(preferred_col),
                                     static_cast<uint8_t>(preferred_row));
      }
      out.push_back(PlacementCandidate{col, row, distance});
    }
  }

  std::sort(out.begin(), out.end(), [](const PlacementCandidate& a, const PlacementCandidate& b) {
    if (a.distance != b.distance) return a.distance < b.distance;
    if (a.row != b.row) return a.row < b.row;
    return a.col < b.col;
  });
  return out;
}

static bool findPlacementForTile(
    TileGridConfig& grid,
    size_t tile_index,
    int preferred_col,
    int preferred_row,
    const std::vector<size_t>& floating_indices,
    uint8_t first_row = 0) {
  if (tile_index >= TILES_PER_GRID) return false;
  Tile& tile = grid.tiles[tile_index];
  const uint8_t span_w = tile.span_w < 1 ? 1 : tile.span_w;
  const uint8_t span_h = tile.span_h < 1 ? 1 : tile.span_h;

  auto can_place = [&](uint8_t col, uint8_t row) -> bool {
    TileRect rect{};
    if (!buildTileRect(col, row, span_w, span_h, rect)) return false;
    return !placementOverlapsAny(grid, tile_index, rect, floating_indices);
  };

  const std::vector<PlacementCandidate> candidates =
      buildPlacementCandidates(span_w, span_h, preferred_col, preferred_row,
                               first_row);
  for (const PlacementCandidate& candidate : candidates) {
    if (!can_place(candidate.col, candidate.row)) continue;
    tile.col = candidate.col;
    tile.row = candidate.row;
    return true;
  }

  return false;
}

static bool applySmartReorder(
    TileGridConfig& grid,
    size_t from_index,
    uint8_t target_col,
    uint8_t target_row,
    uint8_t first_row = 0) {
  if (from_index >= TILES_PER_GRID) return false;
  if (target_row < first_row) return false;
  Tile& moving_tile = grid.tiles[from_index];
  if (moving_tile.type == TILE_EMPTY) return false;

  const uint8_t from_col = moving_tile.col;
  const uint8_t from_row = moving_tile.row;
  const uint8_t span_w = moving_tile.span_w < 1 ? 1 : moving_tile.span_w;
  const uint8_t span_h = moving_tile.span_h < 1 ? 1 : moving_tile.span_h;

  TileRect target_rect{};
  if (!buildTileRect(target_col, target_row, span_w, span_h, target_rect)) return false;

  std::vector<size_t> displaced_indices;
  std::vector<TilePosSnapshot> snapshots;
  snapshots.push_back(TilePosSnapshot{from_index, from_col, from_row});

  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (i == from_index) continue;
    const Tile& other = grid.tiles[i];
    if (other.type == TILE_EMPTY) continue;
    TileRect other_rect{};
    if (!getTileRect(other, other_rect)) continue;
    if (!rectsOverlap(target_rect, other_rect)) continue;
    displaced_indices.push_back(i);
    snapshots.push_back(TilePosSnapshot{i, other.col, other.row});
  }

  moving_tile.col = target_col;
  moving_tile.row = target_row;

  std::sort(displaced_indices.begin(), displaced_indices.end(), [&](size_t a, size_t b) {
    if (grid.tiles[a].row != grid.tiles[b].row) return grid.tiles[a].row < grid.tiles[b].row;
    if (grid.tiles[a].col != grid.tiles[b].col) return grid.tiles[a].col < grid.tiles[b].col;
    return a < b;
  });

  std::vector<size_t> floating_indices = displaced_indices;
  for (size_t displaced_index : displaced_indices) {
    auto it = std::find(floating_indices.begin(), floating_indices.end(), displaced_index);
    if (it != floating_indices.end()) floating_indices.erase(it);

    const int preferred_col = (displaced_index == displaced_indices.front()) ? from_col : grid.tiles[displaced_index].col;
    const int preferred_row = (displaced_index == displaced_indices.front()) ? from_row : grid.tiles[displaced_index].row;
    if (findPlacementForTile(grid, displaced_index, preferred_col, preferred_row,
                             floating_indices, first_row)) {
      continue;
    }

    for (const TilePosSnapshot& snapshot : snapshots) {
      if (snapshot.index >= TILES_PER_GRID) continue;
      grid.tiles[snapshot.index].col = snapshot.col;
      grid.tiles[snapshot.index].row = snapshot.row;
    }
    return false;
  }

  return true;
}


static bool parseFolderIdArg(WebServer& server, uint16_t& out) {
  String raw;
  if (server.hasArg("folder")) raw = server.arg("folder");
  else if (server.hasArg("folder_id")) raw = server.arg("folder_id");
  else if (server.hasArg("tab")) {
    String tab = server.arg("tab");
    tab.toLowerCase();
    if (tab == "home" || tab == "tab0") raw = "0";
  }
  raw.trim();
  if (!raw.length()) return false;
  int v = raw.toInt();
  if (v < 0 || v > 0xFFFF) return false;
  out = static_cast<uint16_t>(v);
  return true;
}

}  // namespace

// Ignore Back tiles when checking folder contents. Treat read failures
// as nonempty so a type change cannot orphan existing content.
static bool folderHasContent(uint16_t folder_id) {
  if (folder_id == 0) return true;  // Root is never referenced by a folder tile
  std::unique_ptr<TileGridConfig> grid(new (std::nothrow) TileGridConfig{});
  if (!grid) return true;
  if (!tileConfig.loadFolderGrid(folder_id, *grid)) return true;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const TileType t = grid->tiles[i].type;
    if (t != TILE_EMPTY && t != TILE_BACK) return true;
  }
  return false;
}

void WebAdminServer::handleGetTiles() {
  // GET /api/tiles?folder=<id>[&index=0-23]
  webAdminMarkActivity();
  if (!server.hasArg("tab") && !server.hasArg("folder") && !server.hasArg("folder_id")) {
    server.send(400, "application/json", "{\"error\":\"Missing folder parameter\"}");
    return;
  }

  uint16_t folder_id = 0;
  if (!parseFolderIdArg(server, folder_id)) {
    server.send(404, "application/json", "{\"error\":\"Folder not found\"}");
    return;
  }
  const bool screensaver_grid =
      folder_id == TileConfig::kScreensaverGridStorageId;
  if (!screensaver_grid && !tileConfig.folderExists(folder_id)) {
    server.send(404, "application/json", "{\"error\":\"Folder not found\"}");
    return;
  }

  TileGridConfig grid{};
  bool loaded = true;
  if (screensaver_grid) {
    grid = screensaverConfig.tileGrid();
  } else {
    loaded = tileConfig.loadFolderGrid(folder_id, grid);
  }
  if (!loaded) {
    server.send(500, "application/json", "{\"error\":\"Grid load failed\"}");
    return;
  }

  auto appendTileJson = [&](String& out, const Tile& tile) {
    out += "{\"type\":";
    out += String(static_cast<int>(tile.type));
    out += ",\"title\":\"";
    appendJsonEscaped(out, tile.title);
    out += "\",\"icon_name\":\"";
    appendJsonEscaped(out, tile.icon_name);
    out += "\",\"bg_color\":";
    out += String(tile.bg_color);
    out += ",\"background_opacity\":";
    out += String(tile.background_opacity);
    out += ",\"col\":";
    out += String(tile.col);
    out += ",\"row\":";
    out += String(tile.row);
    out += ",\"span_w\":";
    out += String(tile.span_w);
    out += ",\"span_h\":";
    out += String(tile.span_h);
    out += ",\"sensor_entity\":\"";
    appendJsonEscaped(out, tile.sensor_entity);
    out += "\",\"sensor_unit\":\"";
    appendJsonEscaped(out, tile.sensor_unit);
    out += "\",\"sensor_decimals\":";
    out += String(tile.sensor_decimals == 0xFF ? -1 : static_cast<int>(tile.sensor_decimals));
    out += ",\"sensor_value_font\":";
    out += String(tile.sensor_value_font);
    out += ",\"sensor_display_mode\":";
    out += String(tile.sensor_display_mode);
    out += ",\"sensor_gauge_min\":";
    out += String(tile.sensor_gauge_min);
    out += ",\"sensor_gauge_max\":";
    out += String(tile.sensor_gauge_max);
    out += ",\"sensor_gauge_arc\":";
    out += String(tile.sensor_gauge_arc);
    out += ",\"sensor_gauge_size\":";
    out += String(tile.sensor_gauge_size);
    out += ",\"sensor_gauge_y_offset\":";
    out += String(tile.sensor_gauge_y_offset);
    out += ",\"sensor_value_y_offset\":";
    out += String(tile.sensor_value_y_offset);
    out += ",\"sensor_graph_height\":";
    out += String(tile.sensor_graph_height);
    out += ",\"image_slideshow_sec\":";
    out += String(tile.image_slideshow_sec);
    out += ",\"scene_alias\":\"";
    appendJsonEscaped(out, tile.scene_alias);
    out += "\",\"key_macro\":\"";
    appendJsonEscaped(out, tile.key_macro);
    out += "\",\"key_code\":";
    out += String(tile.key_code);
    out += ",\"key_modifier\":";
    out += String(tile.key_modifier);
    out += ",\"popup_open_mode\":";
    out += String(getTilePopupOpenMode(tile));
    out += ",\"switch_style\":";
    out += String((tile.type == TILE_SWITCH && tile.sensor_decimals == 1) ? 1 : 0);
    out += ",\"navigate_target\":";
    out += String((tile.type == TILE_FOLDER) ? getNavigateTargetId(tile) : 0);
    // The editor prevents type changes for nonempty folders to keep their
    // content reachable. Deletion remains allowed.
    out += ",\"folder_empty\":";
    out += (tile.type == TILE_FOLDER && !folderHasContent(getNavigateTargetId(tile)))
               ? "true" : "false";
    out += ",\"folder_pin_enabled\":";
    out += (tile.type == TILE_FOLDER &&
            tileConfig.isFolderPinEnabled(getNavigateTargetId(tile)))
               ? "true"
               : "false";
    out += ",\"folder_pin\":\"";
    if (tile.type == TILE_FOLDER) {
      String folder_pin;
      if (tileConfig.getFolderPin(getNavigateTargetId(tile), folder_pin)) {
        appendJsonEscaped(out, folder_pin);
      }
      folder_pin = "";
    }
    out += "\"";
    out += "}";
  };

  if (server.hasArg("index")) {
    int index = server.arg("index").toInt();
    if (index < 0 || index >= TILES_PER_GRID) {
      server.send(400, "application/json", "{\"error\":\"Invalid index\"}");
      return;
    }

    String json;
    appendTileJson(json, grid.tiles[index]);
    sendChunkedResponse(server, 200, "application/json", json);
    return;
  }

  String json = "[";
  for (uint8_t i = 0; i < TILES_PER_GRID; i++) {
    if (i > 0) json += ",";
    appendTileJson(json, grid.tiles[i]);
  }
  json += "]";

  sendChunkedResponse(server, 200, "application/json", json);
}


void WebAdminServer::handleSaveTiles() {
  // POST /api/tiles
  webAdminMarkActivity();
  if (!server.hasArg("index") || !server.hasArg("type")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
    return;
  }

  uint16_t folder_id = 0;
  if (!parseFolderIdArg(server, folder_id)) {
    server.send(404, "application/json", "{\"success\":false,\"error\":\"Folder not found\"}");
    return;
  }
  const bool screensaver_grid =
      folder_id == TileConfig::kScreensaverGridStorageId;
  if (!screensaver_grid && !tileConfig.folderExists(folder_id)) {
    server.send(404, "application/json", "{\"success\":false,\"error\":\"Folder not found\"}");
    return;
  }

  int index = server.arg("index").toInt();
  int type = server.arg("type").toInt();

  if (!get_tile_type_descriptor(static_cast<TileType>(type))) {
    server.send(
        400, "application/json",
        "{\"success\":false,\"error\":\"Tile type not supported\"}");
    return;
  }

  if (screensaver_grid && !tileTypeAllowedInScreensaver(type)) {
    server.send(400, "application/json",
                "{\"success\":false,\"error\":\"Tile type not supported in screensaver\"}");
    return;
  }

  if (index < 0 || index >= TILES_PER_GRID) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid parameters\"}");
    return;
  }

  std::unique_ptr<TileGridConfig> grid(new (std::nothrow) TileGridConfig{});
  if (!grid) {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Out of memory\"}");
    return;
  }
  // Never overwrite a folder from a failed load: a single tile edit rewrites the
  // whole grid, so if the existing grid can't be read we must abort instead of
  // persisting an (empty) grid over every tile in the folder.
  bool grid_loaded = true;
  if (screensaver_grid) {
    *grid = screensaverConfig.tileGrid();
  } else {
    grid_loaded = tileConfig.loadFolderGrid(folder_id, *grid);
  }
  if (!grid_loaded) {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Folder load failed\"}");
    return;
  }

  Tile& tile = grid->tiles[index];
  Tile previous_tile = tile;
  const bool is_root = (!screensaver_grid && folder_id == 0);
  const bool was_settings_tile = is_root && previous_tile.type == TILE_SETTINGS;
  const bool was_back_tile =
      (!screensaver_grid && !is_root) && previous_tile.type == TILE_BACK;
  const bool force_settings_tile = was_settings_tile;
  const bool force_back_tile = was_back_tile;

  if (force_settings_tile) type = TILE_SETTINGS;
  if (force_back_tile) type = TILE_BACK;

  if (type == TILE_SETTINGS && !is_root) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Settings tile only allowed in Home\"}");
    return;
  }
  if (type == TILE_BACK && is_root) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Back tile only allowed in folders\"}");
    return;
  }
  if (type == TILE_SETTINGS && !force_settings_tile) {
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      if (i == static_cast<size_t>(index)) continue;
        if (grid->tiles[i].type == TILE_SETTINGS) {
          server.send(409, "application/json", "{\"success\":false,\"error\":\"Settings tile already exists\"}");
          return;
        }
    }
  }
  if (type == TILE_BACK && !force_back_tile) {
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      if (i == static_cast<size_t>(index)) continue;
        if (grid->tiles[i].type == TILE_BACK) {
          server.send(409, "application/json", "{\"success\":false,\"error\":\"Back tile already exists\"}");
          return;
        }
    }
  }

  // A folder tile can change type only when its folder is empty.
  // Deletion (type -> EMPTY) remains allowed and removes the folder too.
  if (previous_tile.type == TILE_FOLDER && type != TILE_FOLDER && type != TILE_EMPTY) {
    const uint16_t target_id = getNavigateTargetId(previous_tile);
    if (target_id != 0 && folderHasContent(target_id)) {
      server.send(409, "application/json",
                  "{\"success\":false,\"error\":\"Folder not empty\"}");
      return;
    }
  }

  // Leaving the folder type removes its folder record and grid. Deletion
  // removes its contents; an allowed type conversion removes the empty grid
  // so no orphan folder tab remains in Web Admin.
  const bool deleting_folder =
      !screensaver_grid && previous_tile.type == TILE_FOLDER && type != TILE_FOLDER;

  // Update tile data
  if (tile.type != static_cast<TileType>(type)) tile.view_id = 0;
  tile.type = static_cast<TileType>(type);
  tile.title = hometiles_title::normalize(server.hasArg("title") ? server.arg("title").c_str() : "").c_str();
  tile.icon_name = server.hasArg("icon_name") ? server.arg("icon_name") : "";
  // Parse color. bg_color_default keeps legacy/default tiles as true defaults;
  // bg_color=0 is reserved for an explicitly selected black background.
  if (server.hasArg("bg_color_default") && server.arg("bg_color_default").toInt() != 0) {
    tile.bg_color = 0;
  } else if (server.hasArg("bg_color")) {
    tile.bg_color = makeTileBgColor(static_cast<uint32_t>(server.arg("bg_color").toInt()));
  }
  if (server.hasArg("background_opacity")) {
    tile.background_opacity = static_cast<uint8_t>(constrain(
        server.arg("background_opacity").toInt(), 0, 255));
  } else if (screensaver_grid && previous_tile.type == TILE_EMPTY &&
             type != TILE_EMPTY) {
    tile.background_opacity = kScreensaverDefaultTileOpacity;
  }

  // Parse layout (0-based col/row, span >= 1)
  uint8_t col = tile.col;
  uint8_t row = tile.row;
  uint8_t span_w = tile.span_w < 1 ? 1 : tile.span_w;
  uint8_t span_h = tile.span_h < 1 ? 1 : tile.span_h;

  if (server.hasArg("col")) {
    int raw = server.arg("col").toInt();
    if (raw < 0) raw = 0;
    if (raw >= GRID_COLS) raw = GRID_COLS - 1;
    col = static_cast<uint8_t>(raw);
  }
  if (server.hasArg("row")) {
    int raw = server.arg("row").toInt();
    const int first_row = screensaver_grid && GRID_ROWS > 1 ? GRID_ROWS - 2 : 0;
    if (raw < first_row) raw = first_row;
    if (raw >= GRID_ROWS) raw = GRID_ROWS - 1;
    row = static_cast<uint8_t>(raw);
  }
  if (server.hasArg("span_w")) {
    int raw = server.arg("span_w").toInt();
    if (raw < 1) raw = 1;
    if (raw > GRID_COLS) raw = GRID_COLS;
    span_w = static_cast<uint8_t>(raw);
  }
  if (server.hasArg("span_h")) {
    int raw = server.arg("span_h").toInt();
    if (raw < 1) raw = 1;
    if (raw > GRID_ROWS) raw = GRID_ROWS;
    span_h = static_cast<uint8_t>(raw);
  }

  if (screensaver_grid && GRID_ROWS > 1 && row < GRID_ROWS - 2) {
    row = GRID_ROWS - 2;
  }

  clamp_media_tile_layout(static_cast<TileType>(type), col, row,
                          span_w, span_h);
  if (screensaver_grid && GRID_ROWS > 1 && row < GRID_ROWS - 2) {
    row = GRID_ROWS - 2;
  }
  if (span_w > GRID_COLS - col) span_w = GRID_COLS - col;
  if (span_h > GRID_ROWS - row) span_h = GRID_ROWS - row;

  tile.col = col;
  tile.row = row;
  tile.span_w = span_w;
  tile.span_h = span_h;

  // Type-specific fields
  String error_message;
  TileTypeApplyContext apply_ctx;
  apply_ctx.folder_id = folder_id;
  apply_ctx.tile_config = &tileConfig;
  apply_ctx.error_message = &error_message;
  // Preserve the folder a folder tile already points to so a rename reuses it
  // instead of spawning a duplicate. Only trust the stored id when the tile was
  // actually a folder before (otherwise key_code/key_modifier hold key data).
  if (previous_tile.type == TILE_FOLDER) {
    apply_ctx.previous_navigate_target = getNavigateTargetId(previous_tile);
  }
  const TileTypeDescriptor* desc = get_tile_type_descriptor(tile.type);
  if (desc && desc->apply) {
    if (!desc->apply(server, tile, apply_ctx)) {
      tile = previous_tile;
      String err = error_message;
      if (!err.length()) {
        err = (type == TILE_FOLDER) ? "Folder create failed" : "Tile apply failed";
      }
      server.send(500, "application/json", String("{\"success\":false,\"error\":\"") + err + "\"}");
      return;
    }
  }
  if (screensaver_grid && tile.type == TILE_SENSOR) {
    // Sensor history is not routed into the screensaver widget context.
    // Enforce the plain value mode for old imports and direct API callers too.
    tile.sensor_display_mode = 0;
  }

  if (deleting_folder) {
    const uint16_t target_id = getNavigateTargetId(previous_tile);
    if (target_id != 0) {
      if (!tileConfig.deleteFolder(target_id)) {
        tile = previous_tile;
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Folder delete failed\"}");
        return;
      }
      tiles_invalidate_folder(target_id);
    }
  }

  if (tile.type != TILE_EMPTY) {
    TileRect rect{};
    if (!buildTileRect(col, row, span_w, span_h, rect)) {
      tile = previous_tile;
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid layout\"}");
      return;
    }
    if (placementOverlaps(*grid, index, rect)) {
      tile = previous_tile;
      server.send(409, "application/json", "{\"success\":false,\"error\":\"Tile overlaps\"}");
      return;
    }
  }

  bool success = screensaver_grid
                     ? screensaverConfig.replaceTileGrid(*grid)
                     : tileConfig.saveFolderGrid(folder_id, *grid);
  if (success) {
    Serial.printf("[WebAdmin] Tile in folder %u[%d] saved - type: %d\n", static_cast<unsigned>(folder_id), index, type);

    const bool routes_changed =
        deleting_folder || tileChangeAffectsDynamicMqttRoutes(previous_tile, tile);
    if (routes_changed) {
      // Coalesce rapid tile edits into one expensive route rebuild, including
      // the separate screensaver grid so its media entity subscribes to both
      // state and state_fast.
      mqttRequestDynamicSlotsReload(5000);
      Serial.println("[WebAdmin] MQTT routes marked for deferred rebuild");
    } else if (!screensaver_grid) {
      Serial.println("[WebAdmin] MQTT routes unchanged (no rebuild for style/layout)");
    }

    if (!screensaver_grid) {
      tiles_invalidate_folder(folder_id);
      if (tileConfig.getActiveFolderId() == folder_id) {
        tiles_request_reload_if_loaded(GridType::TAB0);
      }
    } else {
      image_screensaver_tiles_changed();
    }

    String response = "{\"success\":true";
    if (tile.type == TILE_FOLDER) {
      response += ",\"navigate_target\":";
      response += String(getNavigateTargetId(tile));
      response += ",\"folder_pin_enabled\":";
      response += tileConfig.isFolderPinEnabled(getNavigateTargetId(tile))
                      ? "true"
                      : "false";
      response += ",\"folder_pin\":\"";
      String folder_pin;
      if (tileConfig.getFolderPin(getNavigateTargetId(tile), folder_pin)) {
        appendJsonEscaped(response, folder_pin);
      }
      folder_pin = "";
      response += "\"";
    }
    response += "}";
    sendChunkedResponse(server, 200, "application/json", response);
  } else {
    Serial.printf("[WebAdmin] Failed to save tile in folder %u[%d]\n", static_cast<unsigned>(folder_id), index);
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
  }
}


void WebAdminServer::handleReorderTiles() {
  webAdminMarkActivity();
  if (!server.hasArg("from") || !server.hasArg("to")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
    return;
  }

  uint16_t folder_id = 0;
  if (!parseFolderIdArg(server, folder_id)) {
    server.send(404, "application/json", "{\"success\":false,\"error\":\"Folder not found\"}");
    return;
  }
  const bool screensaver_grid =
      folder_id == TileConfig::kScreensaverGridStorageId;
  if (!screensaver_grid && !tileConfig.folderExists(folder_id)) {
    server.send(404, "application/json", "{\"success\":false,\"error\":\"Folder not found\"}");
    return;
  }

  int from = server.arg("from").toInt();
  int to = server.arg("to").toInt();

  if (from < 0 || from >= TILES_PER_GRID || to < 0 || to >= TILES_PER_GRID) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid parameters\"}");
    return;
  }

  TileGridConfig grid{};
  // Abort rather than overwrite the whole folder if the current grid can't be loaded.
  bool grid_loaded = true;
  if (screensaver_grid) {
    grid = screensaverConfig.tileGrid();
  } else {
    grid_loaded = tileConfig.loadFolderGrid(folder_id, grid);
  }
  if (!grid_loaded) {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Folder load failed\"}");
    return;
  }

  Tile& tile_to = grid.tiles[to];

  int target_col_raw = server.hasArg("target_col") ? server.arg("target_col").toInt() : -1;
  int target_row_raw = server.hasArg("target_row") ? server.arg("target_row").toInt() : -1;
  uint8_t target_col = (target_col_raw >= 0 && target_col_raw < GRID_COLS) ? static_cast<uint8_t>(target_col_raw) : tile_to.col;
  uint8_t target_row = (target_row_raw >= 0 && target_row_raw < GRID_ROWS) ? static_cast<uint8_t>(target_row_raw) : tile_to.row;

  if (target_col >= GRID_COLS || target_row >= GRID_ROWS) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid target\"}");
    return;
  }

  const uint8_t first_row = screensaver_grid && GRID_ROWS > 1
                                ? GRID_ROWS - 2
                                : 0;
  if (!applySmartReorder(grid, static_cast<size_t>(from), target_col,
                         target_row, first_row)) {
    server.send(409, "application/json", "{\"success\":false,\"error\":\"Tile overlaps\"}");
    return;
  }

  bool success = screensaver_grid
                     ? screensaverConfig.replaceTileGrid(grid)
                     : tileConfig.saveFolderGrid(folder_id, grid);
  if (success) {
    if (!screensaver_grid) {
      tiles_invalidate_folder(folder_id);
      if (tileConfig.getActiveFolderId() == folder_id) {
        tiles_request_reload_if_loaded(GridType::TAB0);
      }
    } else {
      image_screensaver_tiles_changed();
    }
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
  }
}

void WebAdminServer::handleGetSensorValues() {
  webAdminMarkActivity();
  const HaBridgeConfigData& ha = haBridgeConfig.get();


  // Build JSON response with values + meta
  String json = "{";
  json += "\"values\":";
  appendKeyValueMapJson(json, ha.sensor_values_map);
  json += ",\"units\":";
  appendKeyValueMapJson(json, ha.sensor_units_map);
  json += ",\"icons\":";
  appendKeyValueMapJson(json, ha.entity_icons_map);
  json += ",\"names\":";
  appendKeyValueMapJson(json, ha.sensor_names_map);

  // Binary sensors use a structured state payload so the browser can apply
  // the same device-class label and automatic icon rules as the display.
  // Overlay the live entity cache on the retained configuration snapshot.
  json += ",\"binary_sensor_values\":{";
  bool first_binary_sensor_value = true;
  const auto binary_sensor_ids = parseSensorList(ha.binary_sensors_text);
  for (const auto& id : binary_sensor_ids) {
    String payload;
    if (!tiles_get_cached_entity_payload(id.c_str(), payload)) {
      payload = haBridgeConfig.findSensorInitialValue(id);
    }
    payload.trim();
    if (!payload.length()) continue;
    if (!first_binary_sensor_value) json += ',';
    first_binary_sensor_value = false;
    json += '"';
    appendJsonEscaped(json, id);
    json += "\":\"";
    appendJsonEscaped(json, payload);
    json += '"';
  }
  json += "}";

  json += ",\"editable_values\":{";
  bool first_editable_value = true;
  for (const String* list : {&ha.numbers_text, &ha.selects_text, &ha.datetimes_text}) {
    for (const auto& id : parseSensorList(*list)) {
      const String payload = haBridgeConfig.findEditableValue(id);
      if (!payload.length()) continue;
      if (!first_editable_value) json += ',';
      first_editable_value = false;
      json += '\"'; appendJsonEscaped(json, id); json += "\":\"";
      appendJsonEscaped(json, payload); json += '\"';
    }
  }
  json += "}";

  // Climate states include HVAC mode, action and unit alongside temperature.
  // Keep the complete JSON payload from the central entity cache because the
  // Web editor also uses it to derive the dynamic icon.
  json += ",\"climate_values\":{";
  bool first_climate_value = true;
  const auto climate_ids = parseSensorList(ha.climates_text);
  for (const auto& id : climate_ids) {
    String payload;
    if (!tiles_get_cached_entity_payload(id.c_str(), payload)) {
      payload = haBridgeConfig.findSensorInitialValue(id);
    }
    payload.trim();
    if (!payload.length()) continue;
    if (!first_climate_value) json += ',';
    first_climate_value = false;
    json += '"';
    appendJsonEscaped(json, id);
    json += "\":\"";
    appendJsonEscaped(json, payload);
    json += '"';
  }
  json += "}";

  // Aggregated energy sources such as solar_total are not Home Assistant
  // entities and are absent from the general sensor cache. Supply their
  // current daily totals separately; the browser merges both maps without
  // changing ordinary sensors.
  auto energy_ids = parseSensorList(ha.energy_text);
  energy_append_cached_entity_ids(energy_ids);
  json += ",\"energy_values\":{";
  bool first_energy_value = true;
  for (const auto& id : energy_ids) {
    EnergyEntryData entry;
    if (!energy_find_entry(id, "day", entry)) continue;
    if (!first_energy_value) json += ',';
    first_energy_value = false;
    json += '"';
    appendJsonEscaped(json, id);
    json += "\":\"";
    appendJsonEscaped(json, String(entry.total, entry.is_cost ? 2 : 3));
    json += '"';
  }
  json += "},\"energy_units\":{";
  bool first_energy_unit = true;
  for (const auto& id : energy_ids) {
    String unit = energy_find_cached_unit(id);
    if (!unit.length()) continue;
    if (!first_energy_unit) json += ',';
    first_energy_unit = false;
    json += '"';
    appendJsonEscaped(json, id);
    json += "\":\"";
    appendJsonEscaped(json, unit);
    json += '"';
  }
  json += "}";
  json += "}";

  sendChunkedResponse(server, 200, "application/json", json);
}

// Return the current HA Bridge entity lists as JSON entries {v: id, t: label}.
// Refresh editor dropdowns whenever a tile opens so new entities are
// selectable without reloading the page. Wallpapers use /api/files/list.
// Label formats match append_*_fields_html in the type modules.
void WebAdminServer::handleGetEntityOptions() {
  webAdminMarkActivity();
  const HaBridgeConfigData& ha = haBridgeConfig.get();

  auto appendPair = [](String& json, const String& value, const String& label, bool& first) {
    if (!first) json += ",";
    first = false;
    json += "{\"v\":\"";
    appendJsonEscaped(json, value);
    json += "\",\"t\":\"";
    appendJsonEscaped(json, label);
    json += "\"}";
  };

  auto appendHumanizedList = [&](String& json, const char* key, const std::vector<String>& ids) {
    json += "\"";
    json += key;
    json += "\":[";
    bool first = true;
    for (const auto& id : ids) {
      String label;
      if (hardwareIo.isLocalEntityId(id.c_str())) {
        label = haBridgeConfig.findSensorName(id);
      }
      if (!label.length()) label = humanizeIdentifier(id, true);
      appendPair(json, id, label + " - " + id, first);
    }
    json += "]";
  };

  auto appendLocalIds = [&](std::vector<String>& ids, HardwareIoType wanted) {
    for (uint8_t i = 0; i < hardwareIo.channelCount(); ++i) {
      String entity_id;
      String name;
      HardwareIoType type = HardwareIoType::Relay;
      if (!hardwareIo.localEntityInfo(i, entity_id, name, type) ||
          type != wanted) {
        continue;
      }
      bool duplicate = false;
      for (const auto& existing : ids) {
        if (existing.equalsIgnoreCase(entity_id)) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) ids.push_back(entity_id);
    }
  };

  // Match label_already_has_unit_suffix in the Energy form.
  auto hasUnitSuffix = [](const String& name, const String& unit) {
    String n = name;
    n.trim();
    String u = unit;
    u.trim();
    if (!n.length() || !u.length()) return false;
    String suffix = "(" + u + ")";
    n.toLowerCase();
    suffix.toLowerCase();
    return n.endsWith(suffix);
  };

  String json = "{\"success\":true,";
  auto sensor_ids = parseSensorList(ha.sensors_text);
  appendLocalIds(sensor_ids, HardwareIoType::Temperature);
  appendHumanizedList(json, "sensors", sensor_ids);
  json += ",";
  json += "\"binary_sensors\":[";
  {
    bool first = true;
    for (const auto& id : parseSensorList(ha.binary_sensors_text)) {
      String name = haBridgeConfig.findSensorName(id);
      if (!name.length()) name = humanizeIdentifier(id, true);
      appendPair(json, id, name + " - " + id, first);
    }
  }
  json += "],";
  appendHumanizedList(json, "numbers", parseSensorList(ha.numbers_text));
  json += ",";
  appendHumanizedList(json, "selects", parseSensorList(ha.selects_text));
  json += ",";
  appendHumanizedList(json, "datetimes", parseSensorList(ha.datetimes_text));
  json += ",";
  appendHumanizedList(json, "weathers", parseSensorList(ha.weathers_text));
  json += ",";
  appendHumanizedList(json, "climates", parseSensorList(ha.climates_text));
  json += ",";
  appendHumanizedList(json, "covers", parseSensorList(ha.covers_text));
  json += ",\"cameras\":[";
  {
    bool first = true;
    for (const auto& id : parseSensorList(ha.cameras_text)) {
      String name = haBridgeConfig.findSensorName(id);
      if (!name.length()) name = humanizeIdentifier(id, true);
      appendPair(json, id, name + " - " + id, first);
    }
  }
  json += "],";

  json += "\"energy\":[";
  {
    auto energy_ids = parseSensorList(ha.energy_text);
    energy_append_cached_entity_ids(energy_ids);
    bool first = true;
    for (const auto& id : energy_ids) {
      String name = haBridgeConfig.findSensorName(id);
      if (!name.length()) name = humanizeIdentifier(id, true);
      String unit = haBridgeConfig.findSensorUnit(id);
      if (!unit.length()) unit = energy_find_cached_unit(id);
      String label = name;
      if (unit.length() && !hasUnitSuffix(label, unit)) {
        label += " (";
        label += unit;
        label += ")";
      }
      label += " - ";
      label += id;
      appendPair(json, id, label, first);
    }
  }
  json += "],\"media\":[";
  {
    bool first = true;
    for (const auto& id : parseSensorList(ha.media_players_text)) {
      String name = haBridgeConfig.findSensorName(id);
      if (!name.length()) name = humanizeIdentifier(id, true);
      appendPair(json, id, name + " - " + id, first);
    }
  }
  json += "],";

  // Lichter + Schalter + Geraete-Entities, dedupliziert — gleiche Reihenfolge
  // wie buildAdminFolderTabFragments.
  {
    std::vector<String> switch_options;
    auto addSwitchOption = [&](const String& entry) {
      if (!entry.length()) return;
      for (const auto& existing : switch_options) {
        if (existing.equalsIgnoreCase(entry)) return;
      }
      switch_options.push_back(entry);
    };
    for (const auto& opt : parseSensorList(ha.lights_text)) addSwitchOption(opt);
    for (const auto& opt : parseSensorList(ha.switches_text)) addSwitchOption(opt);
    addSwitchOption(kEntityDisplayBrightness);
    addSwitchOption(kEntityScreensaverBrightness);
    addSwitchOption(kEntityDisplayRotate);
    addSwitchOption(kEntityDisplaySleep);
    std::vector<String> local_relays;
    appendLocalIds(local_relays, HardwareIoType::Relay);
    for (const auto& opt : local_relays) addSwitchOption(opt);
    appendHumanizedList(json, "switches", switch_options);
  }

  json += ",\"scenes\":[";
  {
    bool first = true;
    for (const auto& scene : parseSceneList(ha.scene_alias_text)) {
      appendPair(json, scene.alias,
                 humanizeIdentifier(scene.alias, false) + " - " + scene.entity,
                 first);
    }
  }
  json += "]}";
  sendChunkedResponse(server, 200, "application/json", json);
}

// ========== Folder API ==========

void WebAdminServer::handleGetFolders() {
  const auto& folders = tileConfig.getFolders();
  String json = "[";
  for (size_t i = 0; i < folders.size(); ++i) {
    const auto& entry = folders[i];
    if (i > 0) json += ",";
    json += "{\"id\":";
    json += String(entry.id);
    json += ",\"parent_id\":";
    json += String(entry.parent_id);
    json += ",\"name\":\"";
    appendJsonEscaped(json, entry.name);
    json += "\",\"icon_name\":\"";
    appendJsonEscaped(json, entry.icon_name);
    json += "\",\"pin_enabled\":";
    json += tileConfig.isFolderPinEnabled(entry.id) ? "true" : "false";
    json += "}";
  }
  json += "]";
  sendChunkedResponse(server, 200, "application/json", json);
}

void WebAdminServer::handleGetFolderTab() {
  webAdminMarkActivity();
  if (!server.hasArg("folder_id")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing folder_id\"}");
    return;
  }

  const uint16_t folder_id = static_cast<uint16_t>(server.arg("folder_id").toInt());
  if (!tileConfig.folderExists(folder_id)) {
    server.send(404, "application/json", "{\"success\":false,\"error\":\"Folder not found\"}");
    return;
  }

  String button_html;
  String tab_html;
  String tab_id;
  if (!buildAdminFolderTabFragments(folder_id, button_html, tab_html, tab_id)) {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Folder tab build failed\"}");
    return;
  }

  String json = "{\"success\":true,\"folder_id\":";
  json += String(folder_id);
  json += ",\"tab_id\":\"";
  appendJsonEscaped(json, tab_id);
  json += "\",\"button_html\":\"";
  appendJsonEscaped(json, button_html);
  json += "\",\"tab_html\":\"";
  appendJsonEscaped(json, tab_html);
  json += "\"}";
  sendChunkedResponse(server, 200, "application/json", json);
  webAdminMarkActivity();
}

void WebAdminServer::handleSaveFolderAccess() {
  webAdminMarkActivity();
  const auto& tr = i18n::strings(configManager.getConfig().language);
  auto sendError = [this](int status, const char* message) {
    String json = "{\"success\":false,\"error\":\"";
    appendJsonEscaped(json, String(message ? message : ""));
    json += "\"}";
    server.send(status, "application/json", json);
  };

  if (!server.hasArg("folder_id")) {
    sendError(400, tr.folder_pin_create_first);
    return;
  }
  const int requested_id = server.arg("folder_id").toInt();
  if (requested_id <= 0 || requested_id > 0xFFFF ||
      !tileConfig.folderExists(static_cast<uint16_t>(requested_id))) {
    sendError(404, tr.folder_pin_create_first);
    return;
  }

  const uint16_t folder_id = static_cast<uint16_t>(requested_id);
  const bool enable = server.hasArg("enabled") &&
                      server.arg("enabled") != "0";
  bool success = false;
  if (!enable) {
    success = tileConfig.clearFolderPin(folder_id);
  } else {
    String pin = server.hasArg("pin") ? server.arg("pin") : String();
    pin.trim();
    if (!pin.length() && tileConfig.isFolderPinEnabled(folder_id)) {
      success = true;
    } else if (!pin_access::isValidUserPin(pin)) {
      pin = "";
      sendError(400, tr.pin_invalid);
      return;
    } else {
      success = tileConfig.setFolderPin(folder_id, pin);
    }
    pin = "";
  }

  if (!success) {
    sendError(500, tr.folder_pin_save_failed);
    return;
  }
  String json = "{\"success\":true,\"pin_enabled\":";
  json += tileConfig.isFolderPinEnabled(folder_id) ? "true" : "false";
  json += ",\"folder_pin\":\"";
  String stored_pin;
  if (tileConfig.getFolderPin(folder_id, stored_pin)) {
    appendJsonEscaped(json, stored_pin);
  }
  stored_pin = "";
  json += "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void WebAdminServer::handleDeleteFolder() {
  webAdminMarkActivity();
  if (!server.hasArg("folder_id")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing folder_id\"}");
    return;
  }

  uint16_t folder_id = static_cast<uint16_t>(server.arg("folder_id").toInt());
  if (folder_id == 0) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Root folder cannot be deleted\"}");
    return;
  }
  if (!tileConfig.folderExists(folder_id)) {
    server.send(404, "application/json", "{\"success\":false,\"error\":\"Folder not found\"}");
    return;
  }

  // Find parent folder and clear the tile that references this folder
  uint16_t parent_id = tileConfig.getFolderParent(folder_id);
  TileGridConfig parent_grid{};
  if (tileConfig.loadFolderGrid(parent_id, parent_grid)) {
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      Tile& t = parent_grid.tiles[i];
      if (t.type == TILE_FOLDER) {
        uint16_t target = getNavigateTargetId(t);
        if (target == folder_id) {
          t = Tile{};
          break;
        }
      }
    }
    tileConfig.saveFolderGrid(parent_id, parent_grid);
    tiles_invalidate_folder(parent_id);
  }

  if (!tileConfig.deleteFolder(folder_id)) {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Delete failed\"}");
    return;
  }

  mqttRequestDynamicSlotsReload(5000);
  Serial.printf("[WebAdmin] Folder %u deleted\n", static_cast<unsigned>(folder_id));
  server.send(200, "application/json", "{\"success\":true}");
}

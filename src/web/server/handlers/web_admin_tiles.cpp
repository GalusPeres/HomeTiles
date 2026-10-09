#include "src/web/server/web_admin.h"
#include "src/web/server/auth/web_admin_auth.h"
#include "src/core/text/title_text.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/ui/screensaver/screensaver_places.h"
#include "src/web/server/render/web_admin_html.h"
#include "src/core/i18n/i18n.h"
#include "src/core/config/pin_access.h"
#include "src/network/bridge/device_entities.h"
#include "src/io/hardware_io.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/core/power/power_manager.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_layouts.h"
#include "src/core/config/config_manager.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/web/server/web_admin_utils.h"
#include "src/web/server/handlers/web_admin_tile_helpers.h"
#include "src/web/server/handlers/preview_payload.h"
#include "src/types/types_registry.h"
#include "src/types/energy/energy_data.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/network/bridge/entity_search.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <vector>
#include <memory>
#include <new>
#include "src/web/server/handlers/web_admin_handler_utils.h"

using namespace web_admin_handlers;

namespace {

static String dynamicMqttEntityForTile(const Tile& tile) {
  // The tile's own route plus the other entity of its rules.
  String entity = tileTypeHasDynamicMqttRoute(tile.type) ? tile.sensor_entity : String();
  entity.trim();
  const String rule_entity = tileIconSourceEntity(tile.type, tile.icon_colors);
  if (rule_entity.length()) entity += "|" + rule_entity;
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
  float col;
  float row;
  float span_w;
  float span_h;
};

// Where a request places tiles, set at its start: the shown grid
// (grid_layout.h), with the head bar its fewer cells (upright more rows and a
// half row); the classic places (import) and the screensaver without a bar
// layout keep the stored grid. Tiles that already lie outside it stay where
// they are.
static float g_place_cols = GRID_COLS;
static float g_place_rows = GRID_ROWS;

static void setPlaceGrid(bool stored_grid) {
  g_place_cols = stored_grid ? GRID_COLS : tilePlaceCols();
  g_place_rows = stored_grid ? GRID_ROWS : tilePlaceRows();
}

static bool buildTileRect(float col, float row, float span_w, float span_h, TileRect& out) {
  if (col >= g_place_cols || row >= g_place_rows) return false;
  if (!tile_geometry::half_step(col) || !tile_geometry::half_step(row) ||
      !tile_geometry::half_step(span_w) || !tile_geometry::half_step(span_h) ||
      span_w < 0.5f || span_h < 0.5f) return false;
  if (span_w > g_place_cols - col) return false;
  if (span_h > g_place_rows - row) return false;
  out = TileRect{col, row, span_w, span_h};
  return true;
}

static bool getTileRect(const Tile& tile, TileRect& out) {
  if (tile.layout_hidden) return false;
  float col = tile.col;
  float row = tile.row;
  float span_w = tile.span_w < 0.5f ? 1 : tile.span_w;
  float span_h = tile.span_h < 0.5f ? 1 : tile.span_h;
  clamp_media_tile_layout(tile.type, col, row, span_w, span_h, g_place_cols, g_place_rows);
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
  float col;
  float row;
};

struct PlacementCandidate {
  float col;
  float row;
  float distance;
};

static float manhattanDistance(float col_a, float row_a, float col_b, float row_b) {
  return std::abs(col_a - col_b) + std::abs(row_a - row_b);
}

static std::vector<PlacementCandidate> buildPlacementCandidates(
    float span_w,
    float span_h,
    float preferred_col,
    float preferred_row,
    float first_row = 0,
    float step = 1) {
  std::vector<PlacementCandidate> out;
  for (float row = first_row; row < g_place_rows; row += step) {
    for (float col = 0; col < g_place_cols; col += step) {
      TileRect rect{};
      if (!buildTileRect(col, row, span_w, span_h, rect)) continue;
      if (col + span_w > g_place_cols || row + span_h > g_place_rows) continue;
      float distance = row * GRID_COLS + col;
      if (preferred_col >= 0 && preferred_row >= 0) {
        distance = manhattanDistance(col, row,
                                     preferred_col,
                                     preferred_row);
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
    float preferred_col,
    float preferred_row,
    const std::vector<size_t>& floating_indices,
    float first_row = 0,
    float step = 1) {
  if (tile_index >= TILES_PER_GRID) return false;
  Tile& tile = grid.tiles[tile_index];
  const float span_w = tile.span_w < 0.5f ? 1 : tile.span_w;
  const float span_h = tile.span_h < 0.5f ? 1 : tile.span_h;

  auto can_place = [&](float col, float row) -> bool {
    TileRect rect{};
    if (!buildTileRect(col, row, span_w, span_h, rect)) return false;
    return !placementOverlapsAny(grid, tile_index, rect, floating_indices);
  };

  const std::vector<PlacementCandidate> candidates =
      buildPlacementCandidates(span_w, span_h, preferred_col, preferred_row,
                               first_row, step);
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
    float target_col,
    float target_row,
    float first_row = 0) {
  if (from_index >= TILES_PER_GRID) return false;
  if (target_row < first_row) return false;
  Tile& moving_tile = grid.tiles[from_index];
  if (moving_tile.type == TILE_EMPTY) return false;

  const float from_col = moving_tile.col;
  const float from_row = moving_tile.row;
  const float span_w = moving_tile.span_w < 0.5f ? 1 : moving_tile.span_w;
  const float span_h = moving_tile.span_h < 0.5f ? 1 : moving_tile.span_h;

  TileRect target_rect{};
  if (!tile_geometry::supported_size(moving_tile.type, target_col, target_row, span_w, span_h) ||
      !buildTileRect(target_col, target_row, span_w, span_h, target_rect) ||
      target_col + span_w > g_place_cols || target_row + span_h > g_place_rows) return false;

  bool fractional_grid = false;
  for (const Tile& item : grid.tiles) {
    if (item.type != TILE_EMPTY && tile_geometry::fraction_bits(item.col, item.row, item.span_w, item.span_h)) fractional_grid = true;
  }
  fractional_grid = fractional_grid || tile_geometry::fraction_bits(target_col, target_row, span_w, span_h);

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

    const float preferred_col = (displaced_index == displaced_indices.front()) ? from_col : grid.tiles[displaced_index].col;
    const float preferred_row = (displaced_index == displaced_indices.front()) ? from_row : grid.tiles[displaced_index].row;
    const TileType displaced_type = grid.tiles[displaced_index].type;
    const float step = fractional_grid && displaced_type != TILE_BACK ? 0.5f : 1.0f;
    if (findPlacementForTile(grid, displaced_index, preferred_col, preferred_row,
                             floating_indices, first_row, step)) {
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

// An open screensaver shows a tile edit or move before the flash write, like
// the visible folder (user 2026-10-02: screensaver edits were slow).
static bool showScreensaverGridBeforeSave(const TileGridConfig& grid) {
  if (!is_image_screensaver_visible()) return false;
  screensaverConfig.previewTileGrid(grid);
  return image_screensaver_show_tiles_now();
}

// A failed save takes the open screensaver back to the stored grid.
static void restoreScreensaverGridAfterFailedSave() {
  TileGridConfig* stored = new (std::nothrow) TileGridConfig();
  if (stored && tileConfig.loadScreensaverGrid(*stored)) {
    screensaverConfig.previewTileGrid(*stored);
  }
  delete stored;
  image_screensaver_tiles_changed();
}

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

// A tile in the classic storage (the layout window's, tile_layouts.h) has no
// classic place: its stored one is stale and may lie under another tile. The
// export marks it, and the import's overlap checks leave it out.
static void markClassicStorage(uint16_t folder_id, TileGridConfig& grid) {
  for (Tile& tile : grid.tiles) {
    tile.layout_hidden = tile.type != TILE_EMPTY && tile_layouts::classic_hidden(folder_id, tile.view_id);
  }
}

// An imported storage tile's spot beside the classic screen ("col,row,w,h").
static bool parseClassicStorageSpot(const String& value, tile_layouts::Place& out) {
  float v[4] = {0, 0, 0, 0};
  if (sscanf(value.c_str(), "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) != 4) return false;
  for (float f : v) {
    if (!std::isfinite(f) || f * 2 != std::floor(f * 2)) return false;
  }
  if (v[2] < 0.5f || v[3] < 0.5f || v[0] < -16 || v[1] < -16 || v[0] > 32 || v[1] > 32) return false;
  out = {v[0], v[1], v[2], v[3]};
  return !grid_layout::inside(grid_layout::layout_grid(grid_layout::Layout::kClassic), out.col, out.row,
                              out.span_w, out.span_h);
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

  // A full folder grid is too large for the WebServer/loop task stack; keep
  // it on the heap (saving no longer adds a second copy, see saveGridInPlace).
  std::unique_ptr<TileGridConfig> grid_storage(new (std::nothrow) TileGridConfig{});
  if (!grid_storage) {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"No memory\"}");
    return;
  }
  TileGridConfig& grid = *grid_storage;
  bool loaded = true;
  // The export takes the classic layout's places whatever layout is shown.
  const bool classic_places = !screensaver_grid && server.arg("layout") == "classic";
  if (screensaver_grid) {
    grid = screensaverConfig.tileGrid();
    // The active layout's places (screensaver_places.h), the export the
    // classic ones. Its tiles carry their key in the layout file as ID (the
    // layout window's places).
    if (server.arg("layout") != "classic") screensaver_places::overlay(grid);
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      grid.tiles[i].view_id = grid.tiles[i].type == TILE_EMPTY ? 0 : screensaver_places::key(i);
    }
  } else {
    loaded = classic_places ? tileConfig.loadFolderGridClassic(folder_id, grid)
                            : tileConfig.loadFolderGrid(folder_id, grid);
    if (loaded && classic_places) markClassicStorage(folder_id, grid);
  }
  if (!loaded) {
    server.send(500, "application/json", "{\"error\":\"Grid load failed\"}");
    return;
  }

  auto appendTileJson = [&](String& out, const Tile& tile) {
    out += "{\"type\":";
    out += String(static_cast<int>(tile.type));
    out += ",\"view_id\":";
    out += String(tile.view_id);
    // No place in the active layout: the editor leaves it out (tile_layouts.h).
    if (tile.layout_hidden) out += ",\"layout_hidden\":true";
    out += ",\"title\":\"";
    appendJsonEscaped(out, tile.title);
    out += "\",\"icon_name\":\"";
    appendJsonEscaped(out, tile.icon_name);
    out += "\",\"icon_disc\":";
    out += String(tile.icon_disc_mode);
    out += ",\"icon_glow\":";
    out += tile.icon_glow ? "1" : "0";
    out += ",\"icon_colors\":\"";
    appendJsonEscaped(out, tile.icon_colors);
    out += "\"";
    out += ",\"bg_color\":";
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
    out += String((tile.type == TILE_SWITCH && tile.sensor_decimals <= 3) ? tile.sensor_decimals : 0);
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
      if (!web_admin_auth::storedSecretsHidden() &&
          tileConfig.getFolderPin(getNavigateTargetId(tile), folder_pin)) {
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
  // The import writes the classic layout's places whatever layout is shown.
  const bool classic_places = !screensaver_grid && server.arg("layout") == "classic";
  // A bar layout shows the screensaver in its own grid: the editor's places
  // are that layout's (screensaver_places.h); the import writes the classic
  // ones.
  const bool screensaver_layout = screensaver_grid && grid_layout::active() != grid_layout::Layout::kClassic &&
                                  server.arg("layout") != "classic";
  if (screensaver_grid) {
    *grid = screensaverConfig.tileGrid();
    if (screensaver_layout) screensaver_places::overlay(*grid);
  } else {
    grid_loaded = classic_places ? tileConfig.loadFolderGridClassic(folder_id, *grid)
                                 : tileConfig.loadFolderGrid(folder_id, *grid);
    if (grid_loaded && classic_places) markClassicStorage(folder_id, *grid);
  }
  setPlaceGrid(classic_places || (screensaver_grid && !screensaver_layout));
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
  if (tile.type != static_cast<TileType>(type) && (type == TILE_CLOCK || type == TILE_TEXT || type == TILE_BACK)) tile.sensor_display_mode = 0;
  tile.type = static_cast<TileType>(type);
  tile.title = hometiles_title::normalize(server.hasArg("title") ? server.arg("title").c_str() : "").c_str();
  tile.icon_name = server.hasArg("icon_name") ? server.arg("icon_name") : "";
  // Partial requests keep the stored disc override.
  if (server.hasArg("icon_disc")) {
    tile.icon_disc_mode = normalizeTileIconDiscMode(server.arg("icon_disc").toInt());
  }
  if (server.hasArg("icon_glow")) tile.icon_glow = server.arg("icon_glow").toInt() != 0;
  // Icon colors: partial requests keep the stored record; the record is
  // normalized (clamped, unknown lines dropped) and cleared for types
  // without icon colors.
  if (server.hasArg("icon_colors")) tile.icon_colors = server.arg("icon_colors");
  tile.icon_colors = normalizeTileIconColors(tile.type, tile.icon_colors.c_str());
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

  // Parse exact half-cell values without truncating malformed input.
  float col = tile.col, row = tile.row;
  float span_w = tile.span_w > 0 ? tile.span_w : 1;
  float span_h = tile.span_h > 0 ? tile.span_h : 1;
  auto parse_geometry = [&](const char* name, float& value) {
    if (!server.hasArg(name)) return true;
    const String input = server.arg(name);
    char* end = nullptr;
    value = strtof(input.c_str(), &end);
    return end != input.c_str() && *end == '\0' && tile_geometry::half_step(value);
  };
  if (!parse_geometry("col", col) || !parse_geometry("row", row) ||
      !parse_geometry("span_w", span_w) || !parse_geometry("span_h", span_h)) {
    tile = previous_tile;
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid layout\"}");
    return;
  }
  clamp_media_tile_layout(static_cast<TileType>(type), col, row, span_w, span_h, g_place_cols, g_place_rows);
  if ((type != TILE_EMPTY && (!tile_geometry::supported_size(type, col, row, span_w, span_h) ||
                              col + span_w > g_place_cols || row + span_h > g_place_rows)) ||
      (screensaver_grid &&
       row < (screensaver_layout ? screensaver_places::first_row(grid_layout::active()) : GRID_ROWS - 2) - 0.001f)) {
    tile = previous_tile;
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Unsupported tile size\"}");
    return;
  }
  tile.col = col; tile.row = row; tile.span_w = span_w; tile.span_h = span_h;

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
  // Deletion: the old entity and options must not come back with a new tile.
  if (tile.type == TILE_EMPTY) clearEmptyTileFields(tile);

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

  // An imported tile from the classic storage: no classic place, it takes
  // no room (its stored place is only a valid placeholder).
  tile.layout_hidden = classic_places && tile.type != TILE_EMPTY && server.arg("layout_hidden") == "1";
  tile_layouts::Place storage_spot{};
  const bool has_storage_spot = tile.layout_hidden && parseClassicStorageSpot(server.arg("layout_spot"), storage_spot);
  if (tile.type != TILE_EMPTY) {
    TileRect rect{};
    if (!buildTileRect(col, row, span_w, span_h, rect)) {
      tile = previous_tile;
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid layout\"}");
      return;
    }
    if (!tile.layout_hidden && placementOverlaps(*grid, index, rect)) {
      tile = previous_tile;
      server.send(409, "application/json", "{\"success\":false,\"error\":\"Tile overlaps\"}");
      return;
    }
  }

  // The visible folder shows the edit right away instead of after a quiet Web
  // Admin: an edit that keeps the tile type (color, text, size, entity) before
  // the flash write; a new or changed type right after it, once the saved
  // grid carries its view ID.
  // A bar layout's edit: its places to the layout file, the tile with its
  // classic place into the screensaver grid.
  std::unique_ptr<TileGridConfig> classic_grid;
  if (screensaver_layout) {
    classic_grid.reset(new (std::nothrow) TileGridConfig(screensaverConfig.tileGrid()));
    if (!classic_grid) {
      tile = previous_tile;
      server.send(500, "application/json", "{\"success\":false,\"error\":\"No memory\"}");
      return;
    }
    screensaver_places::take(*grid, *classic_grid, static_cast<size_t>(index));
  }
  TileGridConfig& saved_grid = classic_grid ? *classic_grid : *grid;
  // An emptied slot's places in the other layouts go too.
  if (screensaver_grid && !screensaver_layout && tile.type == TILE_EMPTY) {
    screensaver_places::forget(static_cast<size_t>(index));
  }
  const bool display_awake = !powerManager.isInSleep();
  const bool shown_before_save =
      screensaver_grid
          ? display_awake && showScreensaverGridBeforeSave(saved_grid)
          : !deleting_folder && !classic_places && display_awake &&
                previous_tile.type == tile.type &&
                tileConfig.previewActiveFolderGrid(folder_id, *grid) &&
                tiles_show_active_layout_now();
  const uint32_t save_started_ms = millis();
  bool success = screensaver_grid ? tile_layouts::commit() && screensaverConfig.replaceTileGrid(saved_grid)
                 : classic_places ? tileConfig.saveFolderGridClassic(folder_id, *grid)
                                  : tileConfig.saveFolderGrid(folder_id, *grid);
  // The classic storage follows the import: in it (with its spot when the
  // export came from the same screen) or on the screen. The view ID is the
  // saved one.
  if (success && classic_places && tile.type != TILE_EMPTY) {
    if (has_storage_spot) {
      tile_layouts::set(grid_layout::Layout::kClassic, folder_id, tile.view_id, storage_spot);
    } else {
      tile_layouts::set_classic_hidden(folder_id, tile.view_id, tile.layout_hidden);
    }
    success = tile_layouts::commit();
  }
  // The shown folder takes the saved tiles with the active layout's places.
  if (success && classic_places && tileConfig.getActiveFolderId() == folder_id) {
    tileConfig.setActiveFolder(folder_id);
  }
  const uint32_t save_ms = millis() - save_started_ms;
  if (success) {
    Serial.printf("[WebAdmin] Tile in folder %u[%d] saved - type: %d shown=%u save=%lu ms\n",
                  static_cast<unsigned>(folder_id), index, type,
                  shown_before_save ? 1U : 0U,
                  static_cast<unsigned long>(save_ms));

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
      if (deleting_folder) {
        tiles_invalidate_folder(folder_id);
      } else {
        // Only this folder changed; the other prepared folders stay cached.
        tiles_invalidate_folder_only(folder_id);
      }
      if (!shown_before_save && tileConfig.getActiveFolderId() == folder_id &&
          !(display_awake && tiles_show_active_layout_now())) {
        tiles_request_reload_if_loaded(GridType::TAB0);
      }
    } else if (!shown_before_save) {
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
      if (!web_admin_auth::storedSecretsHidden() &&
          tileConfig.getFolderPin(getNavigateTargetId(tile), folder_pin)) {
        appendJsonEscaped(response, folder_pin);
      }
      folder_pin = "";
      response += "\"";
    }
    response += "}";
    sendChunkedResponse(server, 200, "application/json", response);
  } else {
    Serial.printf("[WebAdmin] Failed to save tile in folder %u[%d]\n", static_cast<unsigned>(folder_id), index);
    if (screensaver_grid) {
      if (shown_before_save) restoreScreensaverGridAfterFailedSave();
    } else if (shown_before_save && tileConfig.setActiveFolder(folder_id)) {
      // Back to the stored tile the failed save left.
      tiles_request_reload(GridType::TAB0);
    }
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

  // A full folder grid is too large for the WebServer/loop task stack; keep
  // it on the heap (saving no longer adds a second copy, see saveGridInPlace).
  std::unique_ptr<TileGridConfig> grid_storage(new (std::nothrow) TileGridConfig{});
  if (!grid_storage) {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"No memory\"}");
    return;
  }
  TileGridConfig& grid = *grid_storage;
  // Abort rather than overwrite the whole folder if the current grid can't be loaded.
  bool grid_loaded = true;
  // A bar layout: the moves are in its grid (screensaver_places.h).
  const bool screensaver_layout = screensaver_grid && grid_layout::active() != grid_layout::Layout::kClassic;
  if (screensaver_grid) {
    grid = screensaverConfig.tileGrid();
    if (screensaver_layout) screensaver_places::overlay(grid);
  } else {
    grid_loaded = tileConfig.loadFolderGrid(folder_id, grid);
  }
  if (!grid_loaded) {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Folder load failed\"}");
    return;
  }

  Tile& tile_to = grid.tiles[to];

  float target_col_raw = server.hasArg("target_col") ? server.arg("target_col").toFloat() : -1;
  float target_row_raw = server.hasArg("target_row") ? server.arg("target_row").toFloat() : -1;
  setPlaceGrid(screensaver_grid && !screensaver_layout);
  float target_col = (target_col_raw >= 0 && target_col_raw < g_place_cols) ? target_col_raw : tile_to.col;
  float target_row = (target_row_raw >= 0 && target_row_raw < g_place_rows) ? target_row_raw : tile_to.row;

  if (target_col >= g_place_cols || target_row >= g_place_rows) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid target\"}");
    return;
  }

  // The screensaver's rows at the bottom (three upright: from row 3.5).
  const float first_row = screensaver_layout ? screensaver_places::first_row(grid_layout::active())
                          : screensaver_grid && GRID_ROWS > 1 ? GRID_ROWS - 2
                                                              : 0;
  if (!applySmartReorder(grid, static_cast<size_t>(from), target_col,
                         target_row, first_row)) {
    server.send(409, "application/json", "{\"success\":false,\"error\":\"Tile overlaps\"}");
    return;
  }

  // The visible folder shows the new order before the flash write (about a
  // second) instead of after it and a quiet Web Admin.
  const uint32_t show_started_ms = millis();
  // A bar layout's moves go to the layout file only.
  if (screensaver_layout) screensaver_places::take_places(grid);
  const bool shown_now =
      !powerManager.isInSleep() &&
      (screensaver_layout ? false
       : screensaver_grid ? showScreensaverGridBeforeSave(grid)
                          : tileConfig.previewActiveFolderGrid(folder_id, grid) &&
                                tiles_show_active_layout_now());
  const uint32_t save_started_ms = millis();
  bool success = screensaver_layout ? tile_layouts::commit()
                 : screensaver_grid ? screensaverConfig.replaceTileGrid(grid)
                                    : tileConfig.saveFolderGrid(folder_id, grid);
  const uint32_t saved_ms = millis();
  if (success) {
    if (!screensaver_grid) {
      // Only this folder changed; the other prepared folders stay cached.
      tiles_invalidate_folder_only(folder_id);
      if (!shown_now && tileConfig.getActiveFolderId() == folder_id) {
        tiles_request_reload_if_loaded(GridType::TAB0);
      }
    } else if (!shown_now) {
      image_screensaver_tiles_changed();
    }
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    if (screensaver_grid) {
      if (shown_now) restoreScreensaverGridAfterFailedSave();
    } else if (shown_now && tileConfig.setActiveFolder(folder_id)) {
      // Back to the stored order the failed save left.
      tiles_request_reload(GridType::TAB0);
    }
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
  }
  Serial.printf("[WebAdmin] Reorder folder=%u shown=%u show=%lu ms save=%lu ms\n",
                static_cast<unsigned>(folder_id), shown_now ? 1U : 0U,
                static_cast<unsigned long>(save_started_ms - show_started_ms),
                static_cast<unsigned long>(saved_ms - save_started_ms));
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

  // Lock, Alarm panel and Fan previews read the retained detail state.
  json += ",\"device_values\":{";
  bool first_device_value = true;
  for (const String* list : {&ha.locks_text, &ha.alarm_panels_text, &ha.fans_text}) {
    for (const auto& id : parseSensorList(*list)) {
      const String payload = haBridgeConfig.findDetailValue(id);
      if (!payload.length()) continue;
      if (!first_device_value) json += ',';
      first_device_value = false;
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

  // Weather and media previews draw what their tiles draw: the cached payload
  // of each configured entity, else the retained bridge value, without the
  // hourly forecast (popup only) and the embedded artwork, which the preview
  // loads from its URL like the device without data.
  auto append_preview_payloads = [&](const char* key, const String& entities) {
    json += ",\"";
    json += key;
    json += "\":{";
    bool first = true;
    for (const auto& id : parseSensorList(entities)) {
      String payload;
      if (!tiles_get_cached_entity_payload(id.c_str(), payload)) {
        payload = haBridgeConfig.findSensorInitialValue(id);
      }
      payload.trim();
      if (!payload.length()) continue;
      for (const char* heavy : {"forecast_hourly", "entity_picture_data"}) {
        int from = 0, to = 0;
        if (preview_payload::member_span(payload.c_str(), payload.length(), heavy, &from, &to)) {
          payload.remove(from, to - from);
        }
      }
      if (!first) json += ',';
      first = false;
      json += '"';
      appendJsonEscaped(json, id);
      json += "\":\"";
      appendJsonEscaped(json, payload);
      json += '"';
    }
    json += "}";
  };
  append_preview_payloads("weather_values", ha.weathers_text);
  append_preview_payloads("media_values", ha.media_players_text);
  // "From cover": the color a shown media card sampled from its cover.
  json += ",\"media_cover_colors\":{";
  bool first_cover_color = true;
  for (const auto& id : parseSensorList(ha.media_players_text)) {
    uint32_t rgb = 0;
    if (!tile_renderer_media_cover_color(id, rgb)) continue;
    char color[10];
    snprintf(color, sizeof(color), "#%06X", static_cast<unsigned>(rgb & 0xFFFFFF));
    if (!first_cover_color) json += ',';
    first_cover_color = false;
    json += '"';
    appendJsonEscaped(json, id);
    json += "\":\"";
    json += color;
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
  // Scene tiles store an alias; the device resolves its entity (and the
  // entity's icon) through the bridge scene list. The preview needs the same
  // alias -> entity map to show the scene's Home Assistant icon.
  json += ",\"scene_entities\":{";
  bool first_scene = true;
  for (const auto& scene : parseSceneList(ha.scene_alias_text)) {
    if (!first_scene) json += ',';
    first_scene = false;
    json += '"';
    appendJsonEscaped(json, scene.alias);
    json += "\":\"";
    appendJsonEscaped(json, scene.entity);
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

  // The entities the Bridge released, one list per tile field, for the shared
  // entity picker (tiles/entity-picker.js): v = entity id (scene alias),
  // t = name, i = MDI icon when the Bridge sent one, e = scene entity.
  String json = "{\"success\":true";
  auto appendEntry = [&](const String& value, const String& name, const String& icon,
                         const String& entity, bool& first) {
    json += first ? "{\"v\":\"" : ",{\"v\":\"";
    first = false;
    appendJsonEscaped(json, value);
    json += "\",\"t\":\"";
    appendJsonEscaped(json, name);
    json += "\"";
    if (icon.length()) {
      json += ",\"i\":\"";
      appendJsonEscaped(json, icon);
      json += "\"";
    }
    if (entity.length()) {
      json += ",\"e\":\"";
      appendJsonEscaped(json, entity);
      json += "\"";
    }
    json += "}";
  };
  auto entityName = [](const String& id) {
    String name = haBridgeConfig.findSensorName(id);
    return name.length() ? name : humanizeIdentifier(id, true);
  };
  auto appendList = [&](const char* key, const std::vector<String>& ids) {
    json += ",\"";
    json += key;
    json += "\":[";
    bool first = true;
    for (const auto& id : ids) {
      appendEntry(id, entityName(id), haBridgeConfig.findEntityIcon(id), String(), first);
    }
    json += "]";
  };
  // Appends `id` unless the list already holds it (any case).
  auto addUnique = [](std::vector<String>& ids, const String& id) {
    if (!id.length()) return;
    for (const auto& existing : ids) {
      if (existing.equalsIgnoreCase(id)) return;
    }
    ids.push_back(id);
  };
  auto appendLocalIds = [&](std::vector<String>& ids, HardwareIoType wanted) {
    for (uint8_t i = 0; i < hardwareIo.channelCount(); ++i) {
      String entity_id;
      String name;
      HardwareIoType type = HardwareIoType::Relay;
      if (hardwareIo.localEntityInfo(i, entity_id, name, type) && type == wanted) {
        addUnique(ids, entity_id);
      }
    }
  };

  auto sensor_ids = parseSensorList(ha.sensors_text);
  appendLocalIds(sensor_ids, HardwareIoType::Temperature);
  appendList("sensors", sensor_ids);
  appendList("binary_sensors", parseSensorList(ha.binary_sensors_text));
  appendList("numbers", parseSensorList(ha.numbers_text));
  appendList("selects", parseSensorList(ha.selects_text));
  appendList("datetimes", parseSensorList(ha.datetimes_text));
  appendList("weathers", parseSensorList(ha.weathers_text));
  appendList("climates", parseSensorList(ha.climates_text));
  appendList("covers", parseSensorList(ha.covers_text));
  appendList("locks", parseSensorList(ha.locks_text));
  appendList("alarm_panels", parseSensorList(ha.alarm_panels_text));
  appendList("fans", parseSensorList(ha.fans_text));
  appendList("cameras", parseSensorList(ha.cameras_text));
  // The screensaver picture: released images, then cameras.
  std::vector<String> picture_ids = parseSensorList(ha.images_text);
  for (const auto& id : parseSensorList(ha.cameras_text)) addUnique(picture_ids, id);
  appendList("images", picture_ids);
  appendList("media", parseSensorList(ha.media_players_text));

  // Lights, switches, the panel's own entities and local relays.
  std::vector<String> switch_ids;
  for (const auto& id : parseSensorList(ha.lights_text)) addUnique(switch_ids, id);
  for (const auto& id : parseSensorList(ha.switches_text)) addUnique(switch_ids, id);
  addUnique(switch_ids, kEntityDisplayBrightness);
  addUnique(switch_ids, kEntityScreensaverBrightness);
  addUnique(switch_ids, kEntityDisplayRotate);
  addUnique(switch_ids, kEntityDisplaySleep);
  appendLocalIds(switch_ids, HardwareIoType::Relay);
  appendList("switches", switch_ids);

  // Energy names carry their unit, as in the Energy form; skip it when the
  // name already ends with "(unit)".
  json += ",\"energy\":[";
  {
    auto energy_ids = parseSensorList(ha.energy_text);
    energy_append_cached_entity_ids(energy_ids);
    bool first = true;
    for (const auto& id : energy_ids) {
      String name = entityName(id);
      String unit = haBridgeConfig.findSensorUnit(id);
      if (!unit.length()) unit = energy_find_cached_unit(id);
      unit.trim();
      String lower_name = name;
      lower_name.trim();
      lower_name.toLowerCase();
      String suffix = "(" + unit + ")";
      suffix.toLowerCase();
      if (unit.length() && !lower_name.endsWith(suffix)) name += " (" + unit + ")";
      appendEntry(id, name, haBridgeConfig.findEntityIcon(id), String(), first);
    }
  }
  json += "],\"scenes\":[";
  {
    bool first = true;
    for (const auto& scene : parseSceneList(ha.scene_alias_text)) {
      appendEntry(scene.alias, humanizeIdentifier(scene.alias, false),
                  haBridgeConfig.findEntityIcon(scene.entity), scene.entity, first);
    }
  }
  json += "]}";
  sendChunkedResponse(server, 200, "application/json", json);
}

// Every icon name the panel can draw, one per line, for the icon picker
// (tiles/tile-head.js). Streamed from the flash table in small chunks; the
// browser keeps the list for the page.
void WebAdminServer::handleGetMdiIcons() {
  webAdminMarkActivity();
  server.sendHeader("Cache-Control", "private, max-age=3600");
  server.sendHeader("Connection", "close");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain; charset=utf-8", "");
  String chunk;
  chunk.reserve(1600);
  const size_t count = mdiIconCount();
  for (size_t i = 0; i < count; ++i) {
    chunk += mdiIconName(i);
    chunk += '\n';
    if (chunk.length() >= 1400 || i + 1 == count) {
      server.sendContent(chunk);
      chunk = "";
      yield();
    }
  }
  server.sendContent("");
}

// The picker searches through the Bridge (network/bridge/entity_search.h):
// POST q and list starts a search ("bridge":false without a paired Bridge
// that searches: the picker keeps its own list), GET id polls the answer.
void WebAdminServer::handleStartEntitySearch() {
  webAdminMarkActivity();
  const uint32_t offset = static_cast<uint32_t>(strtoul(server.arg("o").c_str(), nullptr, 10));
  const uint32_t id = entity_search::start(server.arg("q"), server.arg("list"), offset);
  String json = "{\"success\":true,\"bridge\":";
  json += id ? "true,\"id\":" + String(id) : String("false");
  json += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void WebAdminServer::handleGetEntitySearch() {
  webAdminMarkActivity();
  const uint32_t id = static_cast<uint32_t>(strtoul(server.arg("id").c_str(), nullptr, 10));
  String answer;
  server.sendHeader("Cache-Control", "no-store");
  if (!entity_search::result(id, answer)) {
    server.send(200, "application/json", "{\"success\":true,\"ready\":false}");
    return;
  }
  // {"full":..,"more":..,"r":[...]} behind success and ready.
  String json = "{\"success\":true,\"ready\":true,";
  json += answer.substring(1);
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

// GET /api/layouts: the layout window's data. The active layout, every
// folder's tiles with their classic places ([slot, view, col, row, w, h]),
// and the other layouts' places (tile_layouts.h).
void WebAdminServer::handleGetLayouts() {
  webAdminMarkActivity();
  std::unique_ptr<TileGridConfig> grid(new (std::nothrow) TileGridConfig{});
  if (!grid) {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"No memory\"}");
    return;
  }
  String json = "{\"success\":true,\"active\":\"";
  json += grid_layout::key(grid_layout::active());
  json += "\",\"stored\":\"";
  json += grid_layout::key(grid_layout::from_index(configManager.getConfig().layout));
  json += "\",\"classic\":{";
  bool first_folder = true;
  for (const auto& folder : tileConfig.getFolders()) {
    if (!tileConfig.loadFolderGridClassic(folder.id, *grid)) continue;
    if (!first_folder) json += ",";
    first_folder = false;
    json += "\"";
    json += String(folder.id);
    json += "\":[";
    bool first_tile = true;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      const Tile& tile = grid->tiles[i];
      if (tile.type == TILE_EMPTY) continue;
      if (!first_tile) json += ",";
      first_tile = false;
      json += "[" + String(i) + "," + String(tile.view_id) + "," + String(tile.col) + "," + String(tile.row) + "," +
              String(tile.span_w) + "," + String(tile.span_h) + "]";
    }
    json += "]";
  }
  // The screensaver like a folder (screensaver_places.h): its slot keys as IDs.
  {
    const TileGridConfig& saver = screensaverConfig.tileGrid();
    if (!first_folder) json += ",";
    json += "\"" + String(screensaver_places::kFolder) + "\":[";
    bool first_tile = true;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      const Tile& tile = saver.tiles[i];
      if (tile.type == TILE_EMPTY) continue;
      if (!first_tile) json += ",";
      first_tile = false;
      json += "[" + String(i) + "," + String(screensaver_places::key(i)) + "," + String(tile.col) + "," +
              String(tile.row) + "," + String(tile.span_w) + "," + String(tile.span_h) + "]";
    }
    json += "]";
  }
  json += "},\"places\":";
  tile_layouts::append_json(json);
  // The screensaver clock per layout ([x, y, time size, date size]; a layout
  // without its own shows the classic one).
  json += ",\"clock\":{";
  for (uint8_t i = 0; i < grid_layout::kLayoutCount; ++i) {
    const grid_layout::Layout layout = grid_layout::from_index(i);
    const ScreensaverConfigStore::ClockPlace clock = screensaverConfig.clockPlace(layout);
    if (i) json += ",";
    json += "\"";
    json += grid_layout::key(layout);
    json += "\":[" + String(clock.x) + "," + String(clock.y) + "," + String(clock.time_size) + "," +
            String(clock.date_size) + "]";
  }
  json += "}}";
  sendChunkedResponse(server, 200, "application/json", json);
}

namespace {

// One tile's place from the layout window.
struct LayoutPlace {
  uint16_t view;
  tile_layouts::Place place;
  bool inside;
};

bool layoutFromKey(const String& key, grid_layout::Layout& out) {
  for (uint8_t i = 0; i < grid_layout::kLayoutCount; ++i) {
    if (key != grid_layout::key(grid_layout::from_index(i))) continue;
    out = grid_layout::from_index(i);
    return grid_layout::available(out);
  }
  return false;
}

// The window's places of one folder, checked against its stored tiles: half
// steps, sizes the type allows, no overlap on the screen, every folder tile
// on the screen (else it could not be reached).
const char* readFolderPlaces(JsonObjectConst places, const TileGridConfig& grid, const grid_layout::Shown& screen,
                             std::vector<LayoutPlace>& out) {
  out.clear();
  for (JsonPairConst entry : places) {
    JsonArrayConst value = entry.value().as<JsonArrayConst>();
    const uint16_t view = static_cast<uint16_t>(atoi(entry.key().c_str()));
    if (!view || value.size() != 4) return "Invalid place";
    const tile_layouts::Place place{value[0].as<float>(), value[1].as<float>(), value[2].as<float>(),
                                    value[3].as<float>()};
    // Half steps; a classic tile above its screen (the window's folder row)
    // lies at a negative row.
    auto half = [](float value) { return std::isfinite(value) && value * 2 == std::floor(value * 2); };
    if (!half(place.col) || !half(place.row) || !half(place.span_w) || !half(place.span_h) ||
        place.span_w < 0.5f || place.span_h < 0.5f || place.col < -16 || place.row < -16 || place.col > 32 ||
        place.row > 32) {
      return "Invalid place";
    }
    const Tile* tile = nullptr;
    for (const Tile& candidate : grid.tiles) {
      if (candidate.type != TILE_EMPTY && candidate.view_id == view) tile = &candidate;
    }
    if (!tile) continue;  // deleted meanwhile
    const bool inside = grid_layout::inside(screen, place.col, place.row, place.span_w, place.span_h);
    // The sizes the type allows (tile_geometry::supported without the
    // classic grid's bounds: the upright screen has more rows).
    if (inside && (place.span_w < 1 || (place.span_h < 1 && !tile_geometry::half_size(tile->type)))) {
      return "Unsupported tile size";
    }
    for (const LayoutPlace& other : out) {
      if (!inside || !other.inside) continue;
      if (place.col < other.place.col + other.place.span_w && other.place.col < place.col + place.span_w &&
          place.row < other.place.row + other.place.span_h && other.place.row < place.row + place.span_h) {
        return "Tile overlaps";
      }
    }
    out.push_back({view, place, inside});
  }
  for (const Tile& tile : grid.tiles) {
    if (tile.type != TILE_FOLDER) continue;
    const bool placed = std::any_of(out.begin(), out.end(),
                                    [&](const LayoutPlace& entry) { return entry.view == tile.view_id && entry.inside; });
    if (!placed) return "Folder without place";
  }
  return nullptr;
}

}  // namespace

// POST /api/layouts {"layout":"bar","folders":{"<id>":{"<view>":[col,row,w,h]}}}:
// the layout window's "Speichern". Every layout keeps every place, also those
// beside the screen (the window's storage): the classic layout takes the
// places on its screen into the tile data, a tile beside it has no classic
// place and keeps its storage spot in the layout file. Folders not sent stay
// as they are.
void WebAdminServer::handleSaveLayouts() {
  webAdminMarkActivity();
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    sendJsonError(server, 400, "Invalid JSON");
    return;
  }
  grid_layout::Layout layout = grid_layout::Layout::kClassic;
  if (!layoutFromKey(String(doc["layout"] | ""), layout)) {
    sendJsonError(server, 400, "Unknown layout");
    return;
  }
  const grid_layout::Shown screen = grid_layout::layout_grid(layout);
  std::unique_ptr<TileGridConfig> grid(new (std::nothrow) TileGridConfig{});
  if (!grid) {
    sendJsonError(server, 500, "No memory");
    return;
  }
  // The screensaver clock in this layout ([x, y, time size, date size]),
  // sent when it changed.
  JsonArrayConst clock = doc["clock"].as<JsonArrayConst>();
  if (!clock.isNull() && clock.size() != 4) {
    sendJsonError(server, 400, "Invalid clock");
    return;
  }
  JsonObjectConst folders = doc["folders"].as<JsonObjectConst>();
  std::vector<LayoutPlace> places;
  // A folder's classic grid, or the screensaver's with its slot keys as IDs
  // (screensaver_places.h).
  auto load = [&](uint16_t folder_id) {
    if (folder_id != screensaver_places::kFolder) return tileConfig.loadFolderGridClassic(folder_id, *grid);
    *grid = screensaverConfig.tileGrid();
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      grid->tiles[i].view_id = grid->tiles[i].type == TILE_EMPTY ? 0 : screensaver_places::key(i);
    }
    return true;
  };
  // Everything is checked before anything is written.
  for (JsonPairConst folder : folders) {
    const uint16_t folder_id = static_cast<uint16_t>(atoi(folder.key().c_str()));
    if (!load(folder_id)) continue;
    if (const char* error = readFolderPlaces(folder.value().as<JsonObjectConst>(), *grid, screen, places)) {
      sendJsonError(server, 409, error);
      return;
    }
    // The screensaver's tiles on the screen keep to its two bottom rows.
    if (folder_id == screensaver_places::kFolder &&
        std::any_of(places.begin(), places.end(), [&](const LayoutPlace& place) {
          return place.inside && place.place.row < screensaver_places::first_row(layout) - 0.001f;
        })) {
      sendJsonError(server, 409, "Invalid place");
      return;
    }
  }
  bool ok = true;
  bool screensaver_changed = false;
  for (JsonPairConst folder : folders) {
    const uint16_t folder_id = static_cast<uint16_t>(atoi(folder.key().c_str()));
    if (!load(folder_id)) continue;
    const bool screensaver = folder_id == screensaver_places::kFolder;
    screensaver_changed = screensaver_changed || screensaver;
    readFolderPlaces(folder.value().as<JsonObjectConst>(), *grid, screen, places);
    if (layout != grid_layout::Layout::kClassic) {
      for (const Tile& tile : grid->tiles) {
        if (tile.type == TILE_EMPTY || !tile.view_id) continue;
        auto entry = std::find_if(places.begin(), places.end(),
                                  [&](const LayoutPlace& place) { return place.view == tile.view_id; });
        if (entry != places.end() && tile.type != TILE_BACK) {
          tile_layouts::set(layout, folder_id, tile.view_id, entry->place);
        } else {
          tile_layouts::remove(layout, folder_id, tile.view_id);
        }
      }
      continue;
    }
    for (Tile& tile : grid->tiles) {
      if (tile.type == TILE_EMPTY || !tile.view_id) continue;
      auto entry = std::find_if(places.begin(), places.end(),
                                [&](const LayoutPlace& place) { return place.view == tile.view_id; });
      // Beside the classic screen (the window's storage): no classic place,
      // the spot is kept for the window.
      if (entry != places.end() && !entry->inside) {
        tile_layouts::set(grid_layout::Layout::kClassic, folder_id, tile.view_id, entry->place);
        continue;
      }
      tile_layouts::set_classic_hidden(folder_id, tile.view_id, entry == places.end());
      if (entry == places.end()) continue;
      tile.col = entry->place.col;
      tile.row = entry->place.row;
      tile.span_w = entry->place.span_w;
      tile.span_h = entry->place.span_h;
    }
    if (screensaver) {
      // Its tiles have no IDs of their own.
      for (Tile& tile : grid->tiles) tile.view_id = 0;
      ok = screensaverConfig.replaceTileGrid(*grid) && ok;
      continue;
    }
    ok = tileConfig.saveFolderGridClassic(folder_id, *grid) && ok;
  }
  ok = tile_layouts::commit() && ok;
  if (clock.size() == 4) {
    const ScreensaverConfigStore::ClockPlace place{
        static_cast<uint16_t>(constrain(clock[0].as<int>(), 0, 1000)),
        static_cast<uint16_t>(constrain(clock[1].as<int>(), 0, 1000)),
        static_cast<uint8_t>(constrain(clock[2].as<int>(), 0, 255)),
        static_cast<uint8_t>(constrain(clock[3].as<int>(), 0, 255)), true};
    ok = screensaverConfig.setClockPlace(layout, place) && ok;
  }
  if (!ok) {
    sendJsonError(server, 500, "Save failed");
    return;
  }
  // The panel shows this layout: it takes the new places right away.
  if (layout == grid_layout::active()) {
    tileConfig.setActiveFolder(tileConfig.getActiveFolderId());
    tiles_invalidate_folder(tileConfig.getActiveFolderId());
    tiles_request_reload(GridType::TAB0);
    if (screensaver_changed) image_screensaver_tiles_changed();
    if (clock.size() == 4) image_screensaver_config_changed();
  }
  Serial.printf("[WebAdmin] Layout %s saved\n", grid_layout::key(layout));
  server.send(200, "application/json", "{\"success\":true}");
}

// POST /api/layouts/active layout=<key>: the panel starts with this layout
// after the restart the browser asks for next.
void WebAdminServer::handleSwitchLayout() {
  webAdminMarkActivity();
  grid_layout::Layout layout = grid_layout::Layout::kClassic;
  if (!layoutFromKey(server.arg("layout"), layout) || !grid_layout::switchable(layout)) {
    sendJsonError(server, 409, "Layout not available");
    return;
  }
  if (!configManager.saveLayout(static_cast<uint8_t>(layout))) {
    sendJsonError(server, 500, "Could not save the layout");
    return;
  }
  Serial.printf("[WebAdmin] Layout %s chosen (this boot: %s)\n", grid_layout::key(layout),
                grid_layout::key(grid_layout::active()));
  server.send(200, "application/json", "{\"success\":true}");
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
  const bool was_enabled = tileConfig.isFolderPinEnabled(folder_id);
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
  // The Folder tiles show a lock while the PIN is on (navigate renderer);
  // rebuild them in the loop, never inside the WebServer callback.
  if (tileConfig.isFolderPinEnabled(folder_id) != was_enabled) tiles_request_reload_all();
  String json = "{\"success\":true,\"pin_enabled\":";
  json += tileConfig.isFolderPinEnabled(folder_id) ? "true" : "false";
  json += ",\"folder_pin\":\"";
  String stored_pin;
  if (!web_admin_auth::storedSecretsHidden() &&
      tileConfig.getFolderPin(folder_id, stored_pin)) {
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
  std::unique_ptr<TileGridConfig> parent_storage(new (std::nothrow) TileGridConfig{});
  if (parent_storage && tileConfig.loadFolderGrid(parent_id, *parent_storage)) {
    TileGridConfig& parent_grid = *parent_storage;
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

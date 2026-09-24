#include "src/ui/screensaver/screensaver_config.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <new>

#include "src/devices/device.h"
#include "src/types/clock/clock_format.h"

namespace {

constexpr char kConfigDir[] = "/_screensaver";
constexpr char kConfigPath[] = "/_screensaver/config_v2.json";
constexpr char kConfigTmpPath[] = "/_screensaver/config_v2.json.tmp";
constexpr char kConfigBackupPath[] = "/_screensaver/config_v2.json.bak";
constexpr char kLegacyConfigPath[] = "/_screensaver/config_v1.json";
constexpr char kLegacyConfigTmpPath[] = "/_screensaver/config_v1.json.tmp";
constexpr char kLegacyConfigBackupPath[] = "/_screensaver/config_v1.json.bak";
constexpr uint16_t kConfigVersion = 2;
constexpr uint16_t kLegacyConfigVersion = 1;
constexpr size_t kMaxConfigBytes = 64U * 1024U;

uint16_t clamp_u16(int value, int low, int high) {
  if (value < low) value = low;
  if (value > high) value = high;
  return static_cast<uint16_t>(value);
}

uint8_t normalize_font(int value, uint8_t fallback, uint8_t maximum = 96) {
  switch (value) {
    case 20:
    case 24:
    case 28:
    case 32:
    case 40:
    case 48:
    case 56:
    case 64:
    case 72:
    case 80:
    case 96:
      return static_cast<uint8_t>(value > maximum ? maximum : value);
    default:
      return fallback;
  }
}

TileType normalize_screensaver_type(int raw) {
  const TileType type = static_cast<TileType>(raw);
  return tileTypeAllowedInScreensaver(type) ? type : TILE_EMPTY;
}

String safe_wallpaper_name(const String& raw) {
  String name = raw;
  name.trim();
  if (!name.length() || name.length() > 120 || name.indexOf('/') >= 0 ||
      name.indexOf('\\') >= 0 || name.indexOf("..") >= 0) {
    return String();
  }
  String lower = name;
  lower.toLowerCase();
  if (!lower.endsWith(".jpg") && !lower.endsWith(".jpeg")) return String();
  return name;
}

uint16_t legacy_duration_from_doc(const JsonDocument& doc) {
  int first_duration = -1;
  for (JsonObjectConst item : doc["wallpapers"].as<JsonArrayConst>()) {
    const int duration = item["duration_seconds"] | 15;
    if (first_duration < 0) first_duration = duration;
    if (item["enabled"] | true) {
      return clamp_u16(duration, 3, 3600);
    }
  }
  return clamp_u16(first_duration < 0 ? 15 : first_duration, 3, 3600);
}

void legacy_tile_from_json(JsonObjectConst in, Tile& tile, size_t index) {
  tile = Tile{};
  tile.type = normalize_screensaver_type(in["type"] | 0);
  tile.title = String(in["title"] | "");
  tile.icon_name = String(in["icon_name"] | "");
  const uint32_t raw_bg_color = in["bg_color"] | 0U;
  tile.bg_color = raw_bg_color == 0
                      ? 0
                      : ((raw_bg_color & TILE_BG_COLOR_EXPLICIT) != 0
                             ? raw_bg_color
                             : makeTileBgColor(raw_bg_color));
  tile.background_opacity = static_cast<uint8_t>(
      constrain(in["opacity"] | 0, 0, 255));
  tile.col = static_cast<uint8_t>(index % GRID_COLS);
  tile.row = GRID_ROWS > 0 ? GRID_ROWS - 1 : 0;
  tile.span_w = 1;
  tile.span_h = 1;
  tile.sensor_entity = String(in["sensor_entity"] | "");
  tile.sensor_unit = String(in["sensor_unit"] | "");
  const int decimals = in["sensor_decimals"] | -1;
  tile.sensor_decimals = decimals < 0 ? 0xFF
                                      : static_cast<uint8_t>(min(decimals, 9));
  tile.sensor_value_font = normalize_font(in["sensor_value_font"] | 0, 0);
  tile.sensor_display_mode = static_cast<uint8_t>(
      constrain(in["sensor_display_mode"] | 0, 0, 2));
  tile.sensor_gauge_min = in["sensor_gauge_min"] | 0;
  tile.sensor_gauge_max = in["sensor_gauge_max"] | 100;
  tile.sensor_gauge_arc = clamp_u16(in["sensor_gauge_arc"] | 100, 90, 359);
  tile.sensor_gauge_size = clamp_u16(in["sensor_gauge_size"] | 350, 100, 800);
  tile.sensor_gauge_y_offset = static_cast<int16_t>(
      constrain(in["sensor_gauge_y_offset"] | 12, -100, 200));
  tile.sensor_value_y_offset = static_cast<int16_t>(
      constrain(in["sensor_value_y_offset"] | 0, -100, 200));
  tile.sensor_graph_height = clamp_u16(in["sensor_graph_height"] | 60, 20, 200);
  tile.scene_alias = String(in["scene_alias"] | "");
  if (tile.type == TILE_SWITCH) {
    tile.sensor_decimals = static_cast<uint8_t>(
        constrain(in["switch_style"] | 0, 0, 1));
  }
  setTilePopupOpenMode(tile, static_cast<uint8_t>(
      in["popup_open_mode"] | TILE_POPUP_OPEN_LONG_PRESS));
}

bool replace_file_atomically(fs::FS& fs) {
  if (!fs.exists(kConfigTmpPath)) return false;
  if (fs.exists(kConfigBackupPath)) fs.remove(kConfigBackupPath);
  if (fs.exists(kConfigPath) && !fs.rename(kConfigPath, kConfigBackupPath)) {
    return false;
  }
  if (fs.rename(kConfigTmpPath, kConfigPath)) {
    if (fs.exists(kConfigBackupPath)) fs.remove(kConfigBackupPath);
    return true;
  }
  if (fs.exists(kConfigBackupPath) && !fs.exists(kConfigPath)) {
    fs.rename(kConfigBackupPath, kConfigPath);
  }
  return false;
}

}  // namespace

ScreensaverConfigStore screensaverConfig;

ScreensaverConfigStore::ScreensaverConfigStore() {
  resetDefaults();
}

void ScreensaverConfigStore::resetGrid(TileGridConfig& grid,
                                        bool transparent_defaults) {
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    grid.tiles[i] = Tile{};
    grid.tiles[i].col = static_cast<uint8_t>(i % GRID_COLS);
    grid.tiles[i].row = static_cast<uint8_t>(i / GRID_COLS);
    grid.tiles[i].span_w = 1;
    grid.tiles[i].span_h = 1;
    if (transparent_defaults) grid.tiles[i].background_opacity = 0;
  }
}

void ScreensaverConfigStore::resetDefaults() {
  data_ = ScreensaverConfigData{};
  resetGrid(tile_grid_, true);
  for (size_t i = 0; i < GRID_COLS; ++i) legacy_tiles_[i] = Tile{};
  legacy_slot_count_ = 0;
  legacy_slots_loaded_ = false;
}

void ScreensaverConfigStore::normalize() {
  data_.time_format = clock_tile::normalize_time_format(data_.time_format);
  data_.date_format = clock_tile::normalize_date_format(data_.date_format);
  data_.time_alignment = static_cast<uint8_t>(
      constrain(data_.time_alignment, 0, 2));
  data_.date_alignment = static_cast<uint8_t>(
      constrain(data_.date_alignment, 0, 2));
  data_.time_font_size = normalize_font(data_.time_font_size, 48);
  data_.date_font_size = normalize_font(data_.date_font_size, 28, 72);
  data_.clock_x = clamp_u16(data_.clock_x, 0, 1000);
  data_.clock_y = clamp_u16(data_.clock_y, 0, 1000);
  data_.duration_seconds = clamp_u16(data_.duration_seconds, 3, 3600);

  if (data_.wallpapers.size() > kMaxScreensaverWallpapers) {
    data_.wallpapers.resize(kMaxScreensaverWallpapers);
  }
  std::vector<ScreensaverWallpaperConfig> clean;
  clean.reserve(data_.wallpapers.size());
  for (auto wallpaper : data_.wallpapers) {
    wallpaper.file_name = safe_wallpaper_name(wallpaper.file_name);
    if (!wallpaper.file_name.length()) continue;
    wallpaper.focus_x = clamp_u16(wallpaper.focus_x, 0, 1000);
    wallpaper.focus_y = clamp_u16(wallpaper.focus_y, 0, 1000);
    wallpaper.zoom = clamp_u16(wallpaper.zoom, 1000, 3000);
    clean.push_back(wallpaper);
  }
  data_.wallpapers.swap(clean);
}

void ScreensaverConfigStore::normalizeTileGrid(TileGridConfig& grid) {
  const uint8_t first_row = GRID_ROWS > 1 ? GRID_ROWS - 2 : 0;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    Tile& tile = grid.tiles[i];
    tile.type = normalize_screensaver_type(tile.type);
    if (tile.type == TILE_EMPTY) {
      tile.background_opacity = 0;
      continue;
    }
    if (tile.type == TILE_SENSOR) {
      // History is distributed only to normal folder grids. Screensaver
      // sensors therefore use the compact value presentation exclusively.
      tile.sensor_display_mode = 0;
    }
    if (tile.col >= GRID_COLS) tile.col = GRID_COLS - 1;
    if (tile.row < first_row) tile.row = first_row;
    if (tile.row >= GRID_ROWS) tile.row = GRID_ROWS - 1;
    const bool compact = tile_geometry::supported(tile.type, tile.col, tile.row, tile.span_w, tile.span_h);
    if (tile.span_w < 1 && !compact) tile.span_w = 1;
    if (tile.span_h < 1 && !compact) tile.span_h = 1;
    clamp_media_tile_layout(tile.type, tile.col, tile.row,
                            tile.span_w, tile.span_h);
    if (tile.row < first_row) tile.row = first_row;
    if (tile.span_w > GRID_COLS - tile.col) tile.span_w = GRID_COLS - tile.col;
    if (tile.span_h > GRID_ROWS - tile.row) tile.span_h = GRID_ROWS - tile.row;
  }
}

bool ScreensaverConfigStore::loadPath(const char* path) {
  if (!path || !Device::storageReady()) return false;
  fs::File f = Device::storageFS().open(path, FILE_READ);
  if (!f) return false;
  if (f.size() == 0 || static_cast<size_t>(f.size()) > kMaxConfigBytes) {
    f.close();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  const uint16_t version = doc["version"] | 0;
  if (err || (version != kConfigVersion &&
              version != kLegacyConfigVersion)) return false;

  ScreensaverConfigData loaded;
  loaded.use_wallpapers = doc["use_wallpapers"] | true;
  loaded.shuffle = doc["shuffle"] | false;
  loaded.tile_shadow = doc["tile_shadow"] | false;
  loaded.tile_border = doc["tile_border"] | true;
  loaded.show_time = doc["show_time"] | true;
  loaded.show_date = doc["show_date"] | true;
  loaded.show_weekday = doc["show_weekday"] | false;
  loaded.clock_shadow = doc["clock_shadow"] | true;
  loaded.time_format = doc["time_format"] | 0;
  loaded.date_format = doc["date_format"] | 0;
  loaded.time_alignment = doc["time_alignment"] | 1;
  loaded.date_alignment = doc["date_alignment"] | 1;
  loaded.time_font_size = doc["time_font_size"] | 48;
  loaded.date_font_size = doc["date_font_size"] | 28;
  loaded.clock_x = doc["clock_x"] | 500;
  loaded.clock_y = doc["clock_y"] | 350;
  loaded.duration_seconds = doc["duration_seconds"].is<int>()
                                ? clamp_u16(doc["duration_seconds"].as<int>(), 3, 3600)
                                : legacy_duration_from_doc(doc);

  JsonArrayConst wallpapers = doc["wallpapers"].as<JsonArrayConst>();
  for (JsonObjectConst item : wallpapers) {
    if (loaded.wallpapers.size() >= kMaxScreensaverWallpapers) break;
    ScreensaverWallpaperConfig wallpaper;
    wallpaper.file_name = String(item["file_name"] | "");
    wallpaper.enabled = item["enabled"] | true;
    wallpaper.focus_x = item["focus_x"] | 500;
    wallpaper.focus_y = item["focus_y"] | 500;
    wallpaper.zoom = item["zoom"] | 1000;
    loaded.wallpapers.push_back(wallpaper);
  }

  bool have_legacy = false;
  JsonArrayConst slots = doc["slots"].as<JsonArrayConst>();
  size_t index = 0;
  for (JsonObjectConst item : slots) {
    if (index >= GRID_COLS || index >= TILES_PER_GRID) break;
    legacy_tile_from_json(item, legacy_tiles_[index], index);
    have_legacy = true;
    ++index;
  }

  data_ = loaded;
  legacy_slot_count_ = index;
  legacy_slots_loaded_ = have_legacy;
  normalize();
  return true;
}

bool ScreensaverConfigStore::load() {
  resetDefaults();
  if (!Device::storageReady()) return false;

  bool config_ok = false;
  bool config_needs_migration = false;
  fs::FS& fs = Device::storageFS();
  if (loadPath(kConfigTmpPath)) {
#if defined(DEVICE_ESP32_S3_RGB_480)
    Device::ScopedStorageWrite storage_write;
#endif
    fs.remove(kConfigPath);
    fs.rename(kConfigTmpPath, kConfigPath);
    Serial.println("[ScreensaverConfig] Restored from .tmp");
    config_ok = true;
  } else if (loadPath(kConfigPath)) {
    config_ok = true;
  } else if (loadPath(kConfigBackupPath)) {
#if defined(DEVICE_ESP32_S3_RGB_480)
    Device::ScopedStorageWrite storage_write;
#endif
    fs.remove(kConfigPath);
    fs.rename(kConfigBackupPath, kConfigPath);
    Serial.println("[ScreensaverConfig] Restored from .bak");
    config_ok = true;
  } else if (loadPath(kLegacyConfigTmpPath)) {
    Serial.println("[ScreensaverConfig] Loaded legacy v1 .tmp");
    config_ok = true;
    config_needs_migration = true;
  } else if (loadPath(kLegacyConfigPath)) {
    Serial.println("[ScreensaverConfig] Loaded legacy v1 configuration");
    config_ok = true;
    config_needs_migration = true;
  } else if (loadPath(kLegacyConfigBackupPath)) {
    Serial.println("[ScreensaverConfig] Loaded legacy v1 .bak");
    config_ok = true;
    config_needs_migration = true;
  } else {
    Serial.println("[ScreensaverConfig] No configuration, defaults active");
  }

  const bool grid_ok = tileConfig.loadScreensaverGrid(tile_grid_);
  normalizeTileGrid(tile_grid_);

  bool grid_has_tiles = false;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (tile_grid_.tiles[i].type != TILE_EMPTY) {
      grid_has_tiles = true;
      break;
    }
  }
  if (!grid_has_tiles && legacy_slots_loaded_) {
    resetGrid(tile_grid_, true);
    for (size_t i = 0; i < legacy_slot_count_ && i < GRID_COLS; ++i) {
      tile_grid_.tiles[i] = legacy_tiles_[i];
    }
    normalizeTileGrid(tile_grid_);
    if (tileConfig.saveScreensaverGrid(tile_grid_)) {
      // One-time migration from the discarded JSON slot format.
      config_needs_migration = true;
      Serial.println("[ScreensaverConfig] Migrated legacy JSON slots into TileGrid");
    }
  }
  if (config_ok && config_needs_migration) {
    if (save()) {
      Serial.println("[ScreensaverConfig] Configuration migrated to v2");
    } else {
      Serial.println("[ScreensaverConfig] v2 migration failed, v1 remains usable");
    }
  }
  return config_ok || grid_ok;
}

String ScreensaverConfigStore::toJson(bool include_device_meta) const {
  JsonDocument doc;
  doc["version"] = kConfigVersion;
  doc["use_wallpapers"] = data_.use_wallpapers;
  doc["shuffle"] = data_.shuffle;
  doc["tile_shadow"] = data_.tile_shadow;
  doc["tile_border"] = data_.tile_border;
  doc["show_time"] = data_.show_time;
  doc["show_date"] = data_.show_date;
  doc["show_weekday"] = data_.show_weekday;
  doc["clock_shadow"] = data_.clock_shadow;
  doc["time_format"] = data_.time_format;
  doc["date_format"] = data_.date_format;
  doc["time_alignment"] = data_.time_alignment;
  doc["date_alignment"] = data_.date_alignment;
  doc["time_font_size"] = data_.time_font_size;
  doc["date_font_size"] = data_.date_font_size;
  doc["clock_x"] = data_.clock_x;
  doc["clock_y"] = data_.clock_y;
  doc["duration_seconds"] = data_.duration_seconds;
  if (include_device_meta) {
    doc["screen_width"] = Device::kScreenWidth;
    doc["screen_height"] = Device::kScreenHeight;
    doc["grid_cols"] = Device::kGridCols;
    doc["grid_rows"] = Device::kGridRows;
    doc["grid_cell_w"] = Device::kGridCellW;
    doc["grid_cell_h"] = Device::kGridCellH;
    doc["grid_gap"] = Device::kGridGap;
    doc["grid_pad"] = Device::kGridPad;
  }

  JsonArray wallpapers = doc["wallpapers"].to<JsonArray>();
  for (const auto& wallpaper : data_.wallpapers) {
    JsonObject item = wallpapers.add<JsonObject>();
    item["file_name"] = wallpaper.file_name;
    item["enabled"] = wallpaper.enabled;
    item["focus_x"] = wallpaper.focus_x;
    item["focus_y"] = wallpaper.focus_y;
    item["zoom"] = wallpaper.zoom;
  }

  String json;
  serializeJson(doc, json);
  return json;
}

bool ScreensaverConfigStore::save() {
  if (!Device::storageReady()) return false;
  normalize();
#if defined(DEVICE_ESP32_S3_RGB_480)
  Device::ScopedStorageWrite storage_write;
#endif
  fs::FS& fs = Device::storageFS();
  if (!fs.exists(kConfigDir) && !fs.mkdir(kConfigDir)) return false;
  if (fs.exists(kConfigTmpPath)) fs.remove(kConfigTmpPath);

  yield();
  fs::File f = fs.open(kConfigTmpPath, FILE_WRITE);
  if (!f) return false;
  const String json = toJson(false);
  const size_t written = f.print(json);
  f.flush();
  f.close();
  yield();
  if (written != json.length()) {
    fs.remove(kConfigTmpPath);
    return false;
  }
  if (!replace_file_atomically(fs)) {
    fs.remove(kConfigTmpPath);
    return false;
  }
  Serial.printf("[ScreensaverConfig] Saved (%u bytes)\n",
                static_cast<unsigned>(written));
  return true;
}

bool ScreensaverConfigStore::replaceFromJson(const String& json, String& error,
                                             String* preview_wallpaper) {
  if (!json.length() || json.length() > kMaxConfigBytes) {
    error = "Invalid configuration size";
    return false;
  }
  if (!Device::storageReady()) {
    error = "Storage unavailable";
    return false;
  }

  const ScreensaverConfigData previous = data_;
  JsonDocument doc;
  const DeserializationError parse_error = deserializeJson(doc, json);
  if (parse_error) {
    error = String("Invalid JSON: ") + parse_error.c_str();
    return false;
  }
  if (preview_wallpaper) {
    *preview_wallpaper = String(doc["preview_wallpaper"] | "");
  }
  // v1 stored a duration per wallpaper. When importing an old configuration,
  // promote the first enabled image's duration to the global v2 value once.
  if (!doc["duration_seconds"].is<int>()) {
    doc["duration_seconds"] = legacy_duration_from_doc(doc);
  }
  for (JsonObject item : doc["wallpapers"].as<JsonArray>()) {
    item.remove("duration_seconds");
  }
  doc["version"] = kConfigVersion;
  // Tile data does not belong in this JSON file, preventing older or
  // incorrect clients from accidentally overwriting the grid.
  doc.remove("slots");
  // Only for live preview on the currently open display. Selecting an
  // image in the editor changes neither order nor slideshow state,
  // so it does not belong in persisted configuration.
  doc.remove("preview_wallpaper");
  String normalized_json;
  serializeJson(doc, normalized_json);

#if defined(DEVICE_ESP32_S3_RGB_480)
  Device::ScopedStorageWrite storage_write;
#endif
  fs::FS& fs = Device::storageFS();
  if (!fs.exists(kConfigDir) && !fs.mkdir(kConfigDir)) {
    error = "Could not create config folder";
    return false;
  }
  if (fs.exists(kConfigTmpPath)) fs.remove(kConfigTmpPath);
  fs::File f = fs.open(kConfigTmpPath, FILE_WRITE);
  if (!f) {
    error = "Could not prepare configuration";
    return false;
  }
  f.print(normalized_json);
  f.flush();
  f.close();

  if (!loadPath(kConfigTmpPath)) {
    data_ = previous;
    fs.remove(kConfigTmpPath);
    error = "Configuration validation failed";
    return false;
  }
  if (!replace_file_atomically(fs)) {
    data_ = previous;
    error = "Configuration save failed";
    return false;
  }
  return true;
}

bool ScreensaverConfigStore::replaceTileGrid(const TileGridConfig& grid) {
  // Avoid copying the full grid onto the limited loopTask stack. Normalize
  // and save a heap-allocated candidate first; replace the active RAM state
  // only after the atomic LittleFS write succeeds.
  TileGridConfig* candidate = new (std::nothrow) TileGridConfig(grid);
  if (!candidate) return false;
  normalizeTileGrid(*candidate);
  const bool saved = tileConfig.saveScreensaverGrid(*candidate);
  if (saved) tile_grid_ = *candidate;
  delete candidate;
  return saved;
}

const Tile* ScreensaverConfigStore::tile(size_t index) const {
  if (index >= TILES_PER_GRID) return nullptr;
  return &tile_grid_.tiles[index];
}

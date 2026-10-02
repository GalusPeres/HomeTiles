#pragma once

#include <Arduino.h>
#include <vector>

#include "src/tiles/config/tile_config.h"

static constexpr size_t kMaxScreensaverWallpapers = 32;
static constexpr uint8_t kScreensaverDefaultTileOpacity = 217;  // approximately 85%

struct ScreensaverWallpaperConfig {
  String file_name;
  bool enabled = true;
  uint16_t focus_x = 500;       // 0..1000
  uint16_t focus_y = 500;       // 0..1000
  uint16_t zoom = 1000;         // 1000..3000
};

// config_v2.json contains only screensaver-specific data. Tiles are
// stored separately as a normal TileGridConfig through TileConfig's
// packed LittleFS format.
struct ScreensaverConfigData {
  bool use_wallpapers = true;
  bool shuffle = false;
  bool tile_shadow = false;
  bool tile_border = true;
  bool show_time = true;
  bool show_date = true;
  bool show_weekday = false;
  bool clock_shadow = true;
  uint8_t time_format = 0;
  uint8_t date_format = 0;
  uint8_t time_alignment = 1;  // 0=left, 1=center, 2=right
  uint8_t date_alignment = 1;
  uint8_t time_font_size = 48;
  uint8_t date_font_size = 28;
  uint16_t clock_x = 500;  // Center relative to the screen, 0..1000
  uint16_t clock_y = 350;
  uint16_t duration_seconds = 15;  // Global slide duration, 3..3600 s
  std::vector<ScreensaverWallpaperConfig> wallpapers;
};

class ScreensaverConfigStore {
 public:
  ScreensaverConfigStore();

  bool load();
  bool save();
  bool replaceFromJson(const String& json, String& error,
                       String* preview_wallpaper = nullptr);
  String toJson(bool include_device_meta = false) const;

  const ScreensaverConfigData& get() const { return data_; }
  ScreensaverConfigData& mutableData() { return data_; }

  const TileGridConfig& tileGrid() const { return gridStorage(); }
  TileGridConfig& mutableTileGrid() { return gridStorage(); }
  bool replaceTileGrid(const TileGridConfig& grid);
  // Sets only the RAM grid, normalized like a save, so an open screensaver
  // shows an edit before its flash write (replaceTileGrid follows).
  void previewTileGrid(const TileGridConfig& grid);
  const Tile* tile(size_t index) const;

 private:
  ScreensaverConfigData data_;
  // PSRAM, allocated with transparent defaults on first use (load() in
  // setup()) because PSRAM is not ready while the global constructors run.
  // Never freed.
  mutable TileGridConfig* tile_grid_ = nullptr;
  TileGridConfig& gridStorage() const;
  Tile legacy_tiles_[GRID_COLS];
  size_t legacy_slot_count_ = 0;
  bool legacy_slots_loaded_ = false;

  void resetDefaults();
  void resetSettings();
  static void resetGrid(TileGridConfig& grid, bool transparent_defaults);
  void normalize();
  void normalizeTileGrid(TileGridConfig& grid);
  bool loadPath(const char* path);
};

extern ScreensaverConfigStore screensaverConfig;

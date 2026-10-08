#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
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
  // One background opacity for every screensaver tile (user 2026-10-02:
  // set beside borders, radius and shadows instead of per tile).
  uint8_t tile_opacity = kScreensaverDefaultTileOpacity;
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
  // The last loaded file carried tile_opacity; older files kept it per tile.
  bool tile_opacity_stored_ = false;

  // The clock's place and size per layout (user 2026-10-08): the classic
  // one in the file's top-level keys (older firmware reads them), the others
  // in "clock_layouts"; a layout without its own takes the classic one.
  // data_ always carries the active layout's (grid_layout::active()), so the
  // panel and the Web Admin see and edit only that one.
  struct ClockPlace {
    uint16_t x = 500;
    uint16_t y = 350;
    uint8_t time_size = 48;
    uint8_t date_size = 28;
    bool set = false;
  };
  ClockPlace clock_places_[3];
  // The places with the active layout's taken from data_.
  void currentClockPlaces(ClockPlace (&out)[3]) const;
  static void writeClockPlaces(JsonDocument& doc, const ClockPlace (&places)[3]);

  void resetDefaults();
  void resetSettings();
  static void resetGrid(TileGridConfig& grid, bool transparent_defaults);
  void normalize();
  void normalizeTileGrid(TileGridConfig& grid);
  bool loadPath(const char* path);
};

extern ScreensaverConfigStore screensaverConfig;

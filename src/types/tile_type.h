#pragma once

#include <stdint.h>

// These IDs are persisted. Retired entries keep their original numeric values.
enum TileType : uint8_t {
  TILE_EMPTY = 0,
  TILE_SENSOR = 1,
  TILE_SCENE = 2,
  TILE_KEY = 3,      // retired; numeric value kept for stored configurations
  TILE_FOLDER = 4,
  TILE_SWITCH = 5,
  TILE_IMAGE = 6,    // retired; numeric value kept for stored configurations
  TILE_SETTINGS = 7,
  TILE_BACK = 8,
  TILE_CLOCK = 9,
  TILE_TEXT = 10,
  TILE_COUNTER = 11, // retired; numeric value kept for stored configurations
  TILE_WEATHER = 12,
  TILE_RADAR = 13,   // retired; numeric value kept for stored configurations
  TILE_ENERGY = 14,
  TILE_MEDIA = 15,
  TILE_PIXELANIM = 16,
  TILE_CLIMATE = 17,
  TILE_CAMERA = 18,
  TILE_COVER = 19,
  TILE_BINARY_SENSOR = 20,
  TILE_NUMBER = 21,
  TILE_SELECT = 22,
  TILE_DATETIME = 23
};

static constexpr bool isRetiredTileType(TileType type) {
  return type == TILE_KEY || type == TILE_IMAGE ||
         type == TILE_COUNTER || type == TILE_RADAR;
}

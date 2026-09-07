#pragma once

#include "src/types/tile_type.h"

static constexpr bool tileTypeIsEditableValue(int type) {
  return type == TILE_NUMBER || type == TILE_SELECT || type == TILE_DATETIME;
}

// Persistence, runtime state, and editor policies are deliberately separate.
// For example, Camera stores an entity but does not consume cached tile state.
static constexpr bool entityTileStoresSensorEntity(TileType type) {
  return type == TILE_SENSOR || type == TILE_SWITCH || type == TILE_WEATHER ||
         type == TILE_ENERGY || type == TILE_MEDIA || type == TILE_CLIMATE ||
         type == TILE_CAMERA || type == TILE_COVER ||
         tileTypeIsEditableValue(type) || type == TILE_BINARY_SENSOR;
}

static constexpr bool tileTypeUsesCachedEntityState(TileType type) {
  return type == TILE_SENSOR || type == TILE_SWITCH || type == TILE_WEATHER ||
         type == TILE_ENERGY || type == TILE_MEDIA || type == TILE_CLIMATE ||
         type == TILE_COVER || tileTypeIsEditableValue(type) || type == TILE_BINARY_SENSOR;
}

static constexpr bool tileTypeStoresPopupMode(TileType type) {
  return type == TILE_SENSOR || type == TILE_WEATHER || type == TILE_ENERGY ||
         type == TILE_SWITCH || type == TILE_CLIMATE || type == TILE_COVER ||
         tileTypeIsEditableValue(type) || type == TILE_BINARY_SENSOR;
}

// Switch preserves its legacy popup choice in key_code instead.
static constexpr bool tileTypeStoresPopupModeDirectly(TileType type) {
  return type == TILE_SENSOR || type == TILE_WEATHER || type == TILE_ENERGY ||
         type == TILE_CLIMATE || type == TILE_COVER ||
         tileTypeIsEditableValue(type) || type == TILE_BINARY_SENSOR;
}

// Preserve the existing edit-triggered reload policy, including its Energy
// exception. This is not the list of entities subscribed by the MQTT layer.
static constexpr bool tileTypeHasDynamicMqttRoute(TileType type) {
  return type == TILE_SENSOR || tileTypeIsEditableValue(type) || type == TILE_BINARY_SENSOR ||
         type == TILE_SWITCH || type == TILE_MEDIA || type == TILE_WEATHER ||
         type == TILE_CLIMATE || type == TILE_COVER;
}

// Weather uses a separate weather-topic subscription path.
static constexpr bool tileTypeSubscribesDynamicState(TileType type) {
  return type == TILE_SENSOR || type == TILE_ENERGY || type == TILE_SWITCH ||
         type == TILE_MEDIA || type == TILE_CLIMATE || type == TILE_COVER ||
         tileTypeIsEditableValue(type) || type == TILE_BINARY_SENSOR;
}

static constexpr bool tileTypeSubscribesScreensaverState(TileType type) {
  return type == TILE_SENSOR || type == TILE_ENERGY || type == TILE_SWITCH ||
         type == TILE_MEDIA || type == TILE_COVER ||
         tileTypeIsEditableValue(type) || type == TILE_BINARY_SENSOR;
}

// Keep an int argument so HTTP validation cannot wrap an invalid input to a
// persisted uint8_t ID before testing eligibility.
static constexpr bool tileTypeAllowedInScreensaver(int type) {
  return type == TILE_EMPTY || type == TILE_SENSOR || type == TILE_ENERGY ||
         type == TILE_SCENE || type == TILE_SWITCH || type == TILE_MEDIA ||
         type == TILE_COVER || tileTypeIsEditableValue(type) || type == TILE_BINARY_SENSOR;
}

static constexpr bool tileTypeRefreshesEntityIcon(TileType type) {
  return type == TILE_SENSOR || type == TILE_SWITCH || type == TILE_SCENE ||
         type == TILE_ENERGY || type == TILE_MEDIA || type == TILE_CLIMATE ||
         type == TILE_COVER || tileTypeIsEditableValue(type) || type == TILE_BINARY_SENSOR;
}

#include "src/types/scene/web_handler.h"
#include "src/network/bridge/ha_bridge_config.h"

// A Bridge alias names its scene, script or button through the Bridge's list;
// the entity id is stored instead, so it keeps every character (sensor_entity
// and its sidecar). An alias the Bridge does not know stays as it is.
static String scene_entity_for(const String& value) {
  if (sceneTargetIsEntityId(value)) return value;
  const String entity = haBridgeConfig.findSceneEntity(value);
  return sceneTargetIsEntityId(entity) ? entity : value;
}

void apply_scene_fields_from_request(WebServer& server, Tile& tile) {
  // The picker's value: a scene, script or button id, or the Bridge alias of
  // a tile from before (sceneTarget).
  setSceneTarget(tile, scene_entity_for(server.hasArg("scene_alias") ? server.arg("scene_alias") : String()));
  tile.sensor_decimals = 0xFF;
  tile.sensor_value_font = 0;
  tile.sensor_display_mode = 0;
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
}

String scene_picker_value(const Tile& tile) {
  return scene_entity_for(sceneTarget(tile));
}

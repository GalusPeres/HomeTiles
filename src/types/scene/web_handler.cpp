#include "src/types/scene/web_handler.h"
#include "src/types/scene/picture.h"

void apply_scene_fields_from_request(WebServer& server, Tile& tile) {
  // The picker's value: a scene, script or button id, or the Bridge alias of
  // a tile from before (sceneTarget).
  setSceneTarget(tile, server.hasArg("scene_alias") ? server.arg("scene_alias") : String());
  // Picture (scene_picture.h). A request without the field keeps the stored
  // picture; "" removes it; anything outside /pictures/*.jpg is ignored.
  if (server.hasArg("scene_image")) {
    String path = server.arg("scene_image");
    path.trim();
    if (!path.length() || scene_picture::valid_path(path)) tile.image_path = path;
  }
  tile.sensor_decimals = 0xFF;
  tile.sensor_value_font = 0;
  tile.sensor_display_mode = 0;
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
}

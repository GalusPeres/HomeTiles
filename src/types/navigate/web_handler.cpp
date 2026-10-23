#include "src/types/navigate/web_handler.h"
#include "src/core/text/title_text.h"
#include "src/web/server/handlers/web_admin_tile_helpers.h"

bool apply_navigate_fields_from_request(
    WebServer& server,
    Tile& tile,
    uint16_t folder_id,
    TileConfig& tileConfig,
    String& error_message,
    uint16_t previous_target) {
  const bool has_arg = server.hasArg("navigate_target");
  int raw = has_arg ? server.arg("navigate_target").toInt() : -1;
  uint16_t target_id = 0;
  if (raw > 0 && tileConfig.folderExists(static_cast<uint16_t>(raw))) {
    target_id = static_cast<uint16_t>(raw);
  } else if (!has_arg && previous_target != 0 && tileConfig.folderExists(previous_target)) {
    // The web editor has no target picker and never re-sends navigate_target,
    // so a rename arrives with the arg absent. Keep the folder this tile
    // already points to instead of orphaning it and creating a duplicate.
    target_id = previous_target;
  }

  if (target_id == 0 || !tileConfig.folderExists(target_id)) {
    uint16_t new_id = 0;
    if (!tileConfig.createFolder(folder_id, tile.title, tile.icon_name, new_id)) {
      error_message = "Folder create failed";
      return false;
    }
    target_id = new_id;
    // A new folder without title or icon gets them written into its tile
    // the way the Web Admin's tab shows it: "Folder 17" and the folder icon
    // (user 2026-10-22: the tab had both, the tile neither). "none" stays no
    // icon; existing folders keep what they have.
    if (!tile.title.length()) {
      tile.title = hometiles_title::normalize(TileConfig::unnamedFolderName(target_id).c_str()).c_str();
    }
    if (!tile.icon_name.length()) tile.icon_name = "folder";
  }
  tileConfig.updateFolder(target_id, tile.title, tile.icon_name);

  tile.sensor_decimals = 0xFF;
  setNavigateTargetId(tile, target_id);
  tile.sensor_value_font = 0;
  tile.sensor_display_mode = 0;
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
  return true;
}

#include "src/web/server/render/web_admin_scripts.h"
#include "src/web/server/assets/web_admin_assets.h"
#include "src/types/types_registry.h"
#include "src/tiles/config/tile_config.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/ui/screensaver/screensaver_config.h"

#include <esp_system.h>

namespace {

uint32_t adminWebSessionToken() {
  // Stable for this device boot, different after reboot/OTA. Browser-side
  // fragment caches can therefore survive a refresh without ever crossing a
  // firmware boot boundary.
  static const uint32_t token = esp_random();
  return token;
}

}  // namespace

static void appendJsStringLiteral(String& html, const char* value) {
  html += "'";
  if (value) {
    for (const char* p = value; *p; ++p) {
      switch (*p) {
        case '\\': html += "\\\\"; break;
        case '\'': html += "\\'"; break;
        case '\r': break;
        case '\n': html += "\\n"; break;
        default: html += *p; break;
      }
    }
  }
  html += "'";
}
void appendAdminScripts(String& html) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
  <script>
)html";
  append_tile_type_registry_js(html);
  html += "\n  const APP_I18N = {\n";
  auto appendJsEntry = [&](const char* key, const char* value) {
    html += "    ";
    html += key;
    html += ": ";
    appendJsStringLiteral(html, value);
    html += ",\n";
  };
  appendJsEntry("folderPrefix", tr.folder_prefix);
  appendJsEntry("selectTileFirst", tr.js_select_tile_first);
  appendJsEntry("tileCopied", tr.js_tile_copied);
  appendJsEntry("noCopiedTile", tr.js_no_copied_tile);
  appendJsEntry("tilePasted", tr.js_tile_pasted);
  appendJsEntry("settingsTileFixed", tr.js_settings_tile_fixed);
  appendJsEntry("settingsTileParking", tr.settings_tile_parking);
  appendJsEntry("backTileFixed", tr.js_back_tile_fixed);
  appendJsEntry("tileCannotDelete", tr.js_tile_cannot_delete);
  appendJsEntry("tileCannotReplace", tr.js_tile_cannot_replace);
  appendJsEntry("folderCannotDelete", tr.js_folder_cannot_delete);
  appendJsEntry("deleteFolderConfirm", tr.js_delete_folder_confirm);
  appendJsEntry("folderDeleted", tr.js_folder_deleted);
  appendJsEntry("deleteFailed", tr.js_delete_failed);
  appendJsEntry("folderNotFound", tr.js_folder_not_found);
  appendJsEntry("tileSaved", tr.js_tile_saved);
  appendJsEntry("unknownError", tr.js_unknown_error);
  appendJsEntry("networkError", tr.js_network_error);
  appendJsEntry("networkErrorSave", tr.js_network_error_save);
  appendJsEntry("exportCreated", tr.js_export_created);
  appendJsEntry("exportFailed", tr.js_export_failed);
  appendJsEntry("importInvalidJson", tr.js_import_invalid_json);
  appendJsEntry("importFailed", tr.js_import_failed);
  appendJsEntry("importRunning", tr.js_import_running);
  appendJsEntry("importComplete", tr.js_import_complete);
  appendJsEntry("tileDoesNotFit", tr.js_tile_does_not_fit);
  appendJsEntry("noLayoutFound", tr.js_no_layout_found);
  appendJsEntry("tilesMovedSaved", tr.js_tiles_moved_saved);
  appendJsEntry("screensaverSaved", tr.js_screensaver_saved);
  appendJsEntry("screensaverSaveFailed", tr.js_screensaver_save_failed);
  appendJsEntry("screensaverLoadFailed", tr.js_screensaver_load_failed);
  appendJsEntry("screensaverNoWallpapers", tr.screensaver_no_wallpapers);
  appendJsEntry("moveFailed", tr.js_move_failed);
  appendJsEntry("moveUp", tr.js_move_up);
  appendJsEntry("moveDown", tr.js_move_down);
  appendJsEntry("networkErrorMove", tr.js_network_error_move);
  appendJsEntry("screenshotCreating", tr.js_screenshot_creating);
  appendJsEntry("screenshotSaved", tr.js_screenshot_saved);
  appendJsEntry("screenshotFailed", tr.js_screenshot_failed);
  appendJsEntry("otaSelectFile", tr.js_ota_select_file);
  appendJsEntry("otaUploading", tr.js_ota_uploading);
  appendJsEntry("otaInstalling", tr.js_ota_installing);
  appendJsEntry("otaReconnecting", tr.js_ota_reconnecting);
  appendJsEntry("otaSuccess", tr.js_ota_success);
  appendJsEntry("otaFailed", tr.js_ota_failed);
  appendJsEntry("otaChooseFile", tr.ota_choose_file);
  appendJsEntry("otaNoFileSelected", tr.ota_no_file_selected);
  appendJsEntry("otaGithubCheck", tr.system_check_updates_btn);
  appendJsEntry("otaGithubChecking", tr.system_checking);
  appendJsEntry("otaGithubUpToDate", tr.system_up_to_date);
  appendJsEntry("otaGithubAvailable", tr.system_update_available_fmt);
  appendJsEntry("otaGithubInstall", tr.system_install_btn_fmt);
  appendJsEntry("otaGithubCheckFailed", tr.system_check_failed);
  appendJsEntry("otaGithubDownloading", tr.system_downloading);
  appendJsEntry("save", tr.save);
  appendJsEntry("restart", tr.restart_button);
  appendJsEntry("restartConfirm", tr.restart_confirm);
  appendJsEntry("saveFailed", tr.save_failed);
  appendJsEntry("loading", tr.loading);
  appendJsEntry("ioSwitch", tr.tile_type_switch);
  appendJsEntry("ioTemperature", tr.admin_io_temperature);
  appendJsEntry("ioName", tr.admin_io_name);
  appendJsEntry("ioGpio", tr.admin_io_gpio);
  appendJsEntry("ioNoFreeGpio", tr.admin_io_no_free_gpio);
  appendJsEntry("ioOutputLogic", tr.admin_io_output_logic);
  appendJsEntry("ioActiveHigh", tr.admin_io_active_high);
  appendJsEntry("ioActiveLow", tr.admin_io_active_low);
  appendJsEntry("ioHigh", tr.admin_io_high);
  appendJsEntry("ioLow", tr.admin_io_low);
  appendJsEntry("ioAfterRestart", tr.admin_io_after_restart);
  appendJsEntry("ioOff", tr.light_off);
  appendJsEntry("ioOn", tr.light_on);
  appendJsEntry("ioPrecision", tr.admin_io_precision);
  appendJsEntry("ioDecimalsZero", tr.admin_io_decimals_zero);
  appendJsEntry("ioDecimalOne", tr.admin_io_decimal_one);
  appendJsEntry("ioDecimalsTwo", tr.admin_io_decimals_two);
  appendJsEntry("ioDecimalsThree", tr.admin_io_decimals_three);
  appendJsEntry("ioRemoveAssignment", tr.admin_io_remove_assignment);
  appendJsEntry("ioRemoveConfirm", tr.admin_io_remove_confirm_fmt);
  appendJsEntry("ioEmpty", tr.admin_io_empty);
  appendJsEntry("ioNoProfile", tr.admin_io_no_profile);
  appendJsEntry("ioUnsavedChanges", tr.admin_io_unsaved_changes);
  appendJsEntry("ioNoCompatibleGpio", tr.admin_io_no_compatible_gpio);
  appendJsEntry("ioNameRequired", tr.admin_io_name_required);
  appendJsEntry("ioSaving", tr.admin_io_saving);
  appendJsEntry("ioSaved", tr.admin_io_saved);
  appendJsEntry("ioLoadFailed", tr.admin_io_load_failed);
  appendJsEntry("ioCouldNotLoad", tr.admin_io_could_not_load);
  appendJsEntry("ioRestartUnsavedConfirm", tr.admin_io_restart_unsaved_confirm);
  appendJsEntry("ioRestarting", tr.admin_io_restarting);
  html += "  };\n";
  html += "  const GRID_COLS = " + String(GRID_COLS) + ";\n";
  html += "  const GRID_ROWS = " + String(GRID_ROWS) + ";\n";
  html += "  const ADMIN_WEB_SESSION_TOKEN = " +
          String(adminWebSessionToken()) + ";\n";
  html += "  const MEDIA_TILE_TYPE = " +
          String(static_cast<unsigned>(TILE_MEDIA)) + ";\n";
  html += "  const MEDIA_TILE_MIN_SPAN = " +
          String(MEDIA_TILE_MIN_SPAN) + ";\n";
  html += "  const MEDIA_TILE_MAX_SPAN = " +
          String(MEDIA_TILE_MAX_SPAN) + ";\n";
  html += "  const SCREENSAVER_TILE_DEFAULT_OPACITY = " +
          String(kScreensaverDefaultTileOpacity) + ";\n";
  html += "  const SCREENSAVER_FOLDER_ID = " +
          String(TileConfig::kScreensaverGridStorageId) + ";\n";
  html += "  </script>\n";

  // Tile-type-specific runtime data (currently Climate translations) must be
  // available before the deferred static application script executes.
  append_tile_type_scripts(html);

  html += R"html(  <script defer src=")html";
  html += adminJsAssetPath();
  html += R"html("></script>
)html";
}

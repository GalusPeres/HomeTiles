#include "src/web/server/render/web_admin_scripts.h"
#include "src/web/server/assets/web_admin_assets.h"
#include "src/types/types_registry.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/grid_layout.h"
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
  appendJsEntry("pasteEmptyOnly", tr.js_paste_empty_only);
  appendJsEntry("pasteNoSpace", tr.js_paste_no_space);
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
  appendJsEntry("importConflict", tr.js_import_conflict);
  appendJsEntry("importStopped", tr.js_import_stopped);
  appendJsEntry("importScreensaver", tr.js_import_screensaver);
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
  appendJsEntry("layoutChange", tr.layout_change);
  appendJsEntry("layoutActive", tr.layout_active);
  appendJsEntry("layoutCopyFrom", tr.layout_copy_from);
  appendJsEntry("layoutUndo", tr.layout_undo);
  appendJsEntry("layoutSaved", tr.layout_saved);
  appendJsEntry("layoutSwitch", tr.layout_switch);
  appendJsEntry("layoutSwitchConfirm", tr.layout_switch_confirm);
  appendJsEntry("layoutFoldersMissing", tr.layout_folders_missing);
  appendJsEntry("layoutFolderMissing", tr.layout_folder_missing);
  appendJsEntry("layoutTilesMissing", tr.layout_tiles_missing);
  appendJsEntry("layoutTileMissing", tr.layout_tile_missing);
  appendJsEntry("layoutAllFit", tr.layout_all_fit);
  appendJsEntry("layoutUnsavedConfirm", tr.layout_unsaved_confirm);
  appendJsEntry("layoutStorage", tr.layout_storage);
  appendJsEntry("layoutStorageHint", tr.layout_storage_hint);
  appendJsEntry("layoutPark", tr.layout_park);
  appendJsEntry("layoutParkedTiles", tr.layout_parked_tiles);
  appendJsEntry("layoutParkedTile", tr.layout_parked_tile);
  appendJsEntry("layoutDeleteTile", tr.layout_delete_tile);
  appendJsEntry("layoutDeleteConfirm", tr.layout_delete_confirm);
  appendJsEntry("close", tr.security_close);
  appendJsEntry("home", tr.home);
  appendJsEntry("loadFailed", tr.admin_io_load_failed);
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
  appendJsEntry("webAuthTooShort", tr.web_auth_too_short);
  appendJsEntry("webAuthMismatch", tr.web_auth_mismatch);
  appendJsEntry("webAuthSaved", tr.web_auth_saved);
  appendJsEntry("webAuthRemoved", tr.web_auth_removed);
  appendJsEntry("webAuthChangeFailed", tr.web_auth_change_failed);
  appendJsEntry("webAuthRemoveConfirm", tr.web_auth_remove_confirm);
  appendJsEntry("webAuthPanelTap", tr.web_auth_panel_tap_required);
  appendJsEntry("webAuthSet", tr.web_auth_set);
  const auto& loc = i18n::locale(configManager.getConfig().language);
  static const char* const kEntityPickerKeys[] = {
      "entityPickerChoose", "entityPickerSearch", "entityPickerNoMatch", "entityPickerLoadFailed",
      "entityPickerRetry", "entityPickerClear", "entityPickerNone", "entityPickerReleased"};
  static_assert(sizeof(kEntityPickerKeys) / sizeof(kEntityPickerKeys[0]) ==
                    sizeof(i18n::LocaleProfile::entity_picker_labels) / sizeof(const char*),
                "one key per entity picker label");
  for (size_t i = 0; i < sizeof(kEntityPickerKeys) / sizeof(kEntityPickerKeys[0]); ++i) {
    appendJsEntry(kEntityPickerKeys[i], loc.entity_picker_labels[i]);
  }
  static const char* const kIconPickerKeys[] = {
      "iconPickerAuto", "iconPickerAutoHint", "iconPickerNone", "iconPickerNoneHint", "iconPickerNoMatch"};
  static_assert(sizeof(kIconPickerKeys) / sizeof(kIconPickerKeys[0]) ==
                    sizeof(i18n::LocaleProfile::icon_picker_labels) / sizeof(const char*),
                "one key per icon picker label");
  for (size_t i = 0; i < sizeof(kIconPickerKeys) / sizeof(kIconPickerKeys[0]); ++i) {
    appendJsEntry(kIconPickerKeys[i], loc.icon_picker_labels[i]);
  }
  appendJsEntry("iconPickerSearch", tr.admin_icon_placeholder);
  html += "  };\n";
  // Order of LocaleProfile::entity_kind_labels (ENTITY_KIND_DOMAINS in
  // tiles/entity-picker.js).
  html += "  const ENTITY_KIND_LABELS = [";
  for (size_t i = 0; i < sizeof(loc.entity_kind_labels) / sizeof(loc.entity_kind_labels[0]); ++i) {
    if (i) html += ", ";
    appendJsStringLiteral(html, loc.entity_kind_labels[i]);
  }
  html += "];\n";
  // The stored grid and the shown grid of this boot (grid_layout.h): with
  // the head bar fewer, larger cells; tiles are placed and moved inside it.
  // Variables: the layout window (tiles/layout-window.js) edits every
  // layout in its own area with the same editor.
  html += "  let GRID_COLS = " + String(GRID_COLS) + ";\n";
  html += "  let GRID_ROWS = " + String(GRID_ROWS) + ";\n";
  html += "  let GRID_SHOWN_COLS = " + String(GRID_SHOWN_COLS) + ";\n";
  html += "  let GRID_SHOWN_ROWS = " + String(GRID_SHOWN_ROWS) + ";\n";
  html += String("  let HEAD_BAR = ") + (grid_layout::head_bar() ? "true" : "false") + ";\n";
  // The three layouts in screen pixels (the window scales them like the
  // preview): grid, margins, screen, and whether the panel can show them.
  {
    const char* names[] = {tr.layout_classic, tr.layout_bar, tr.layout_portrait};
    html += "  const LAYOUTS = {";
    for (uint8_t i = 0; i < grid_layout::kLayoutCount; ++i) {
      const grid_layout::Layout layout = grid_layout::from_index(i);
      const grid_layout::Shown grid = grid_layout::layout_grid(layout);
      if (i) html += ", ";
      html += grid_layout::key(layout);
      html += ": {name: ";
      appendJsStringLiteral(html, names[i]);
      html += ", cols: " + String(grid.cols) + ", rows: " + String(grid.rows) + (grid.half_row ? ".5" : "");
      html += String(", bar: ") + (grid.head_bar ? "true" : "false");
      html += String(", portrait: ") + (grid.portrait ? "true" : "false");
      html += String(", available: ") + (grid_layout::available(layout) ? "true" : "false");
      html += String(", switchable: ") + (grid_layout::switchable(layout) ? "true" : "false");
      html += ", screenW: " + String(grid.screen_w) + ", screenH: " + String(grid.screen_h);
      html += ", cellW: " + String(grid.cell_w) + ", cellH: " + String(grid.cell_h);
      html += ", padLeft: " + String(grid.pad_left) + ", padRight: " + String(grid.pad_right);
      html += ", padTop: " + String(grid.pad_top) + ", padBottom: " + String(grid.pad_bottom) + "}";
    }
    html += "};\n";
    html += "  const ACTIVE_LAYOUT = '";
    html += grid_layout::key(grid_layout::active());
    html += "';\n";
  }
  html += "  const TILES_PER_GRID = " +
          String(static_cast<unsigned>(TILES_PER_GRID)) + ";\n";
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

  // The password helpers run first: on a protected panel they add the CSRF
  // header to every request admin.js sends.
  html += R"html(  <script defer src=")html";
  html += authJsAssetPath();
  html += R"html("></script>
)html";
  html += R"html(  <script defer src=")html";
  html += adminJsAssetPath();
  html += R"html("></script>
)html";
}

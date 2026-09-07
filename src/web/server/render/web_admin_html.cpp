#include "src/types/value/value_control.h"
#include "src/web/server/web_admin.h"
#include "src/web/server/web_admin_utils.h"
#include <WiFi.h>
#include <math.h>
#include <stdlib.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "src/core/config/config_manager.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/transport/network_transport.h"
#include "src/network/transport/usb_ethernet_backend.h"
#include "src/web/server/render/web_admin_scripts.h"
#include "src/web/server/render/web_admin_styles.h"
#include "src/web/server/assets/web_admin_fonts.h"
#include "src/web/server/handlers/web_admin_tile_helpers.h"
#include "src/tiles/config/tile_config.h"
#include "src/types/types_registry.h"
#include "src/core/diagnostics/crash_log.h"
#include "src/network/bridge/device_entities.h"
#include "src/core/firmware/firmware_version.h"
#include "src/core/i18n/i18n.h"
#include "src/devices/device.h"
#include "src/types/clock/clock_format.h"
#include "src/types/binary_sensor/renderer.h"
#include "src/types/energy/energy_data.h"
#include "src/ui/screensaver/screensaver_config.h"
#include <cstring>

namespace {

static String buildTimezoneOptionsHtml(const char* selected_code,
                                       const i18n::LocaleProfile& profile) {
  const char* selected = (selected_code && selected_code[0]) ? selected_code : "berlin";
  String html;
  html.reserve(2048);
  uint8_t current_group = 255;
  for (size_t i = 0; i < i18n::kTimezoneOptionCount; ++i) {
    const i18n::TimezoneOptionInfo& option = i18n::timezone_option(i);
    if (option.group != current_group) {
      if (current_group != 255) html += "</optgroup>";
      current_group = option.group;
      html += "<optgroup label=\"";
      html += profile.timezone_group_labels[current_group];
      html += "\">";
    }
    html += "<option value=\"";
    html += option.code;
    html += "\"";
    if (strcmp(selected, option.code) == 0) html += " selected";
    html += ">";
    html += profile.timezone_labels[i];
    html += "</option>";
  }
  if (current_group != 255) html += "</optgroup>";
  return html;
}

static String buildGlobalTimeFormatOptionsHtml(uint8_t selected_format, const i18n::Strings& tr) {
  selected_format = clock_tile::normalize_time_format(selected_format);
  String html;
  html.reserve(192);
  html += "<option value=\"0\"";
  if (selected_format == clock_tile::TIME_FORMAT_AUTO) html += " selected";
  html += ">";
  html += tr.format_auto_language;
  html += "</option>";
  html += "<option value=\"1\"";
  if (selected_format == clock_tile::TIME_FORMAT_24H) html += " selected";
  html += ">";
  html += tr.format_24_hour;
  html += "</option>";
  html += "<option value=\"2\"";
  if (selected_format == clock_tile::TIME_FORMAT_12H) html += " selected";
  html += ">";
  html += tr.format_12_hour;
  html += "</option>";
  return html;
}

static String buildGlobalDateFormatOptionsHtml(uint8_t selected_format, const i18n::Strings& tr) {
  selected_format = clock_tile::normalize_date_format(selected_format);
  String html;
  html.reserve(224);
  html += "<option value=\"0\"";
  if (selected_format == clock_tile::DATE_FORMAT_AUTO) html += " selected";
  html += ">";
  html += tr.format_auto_language;
  html += "</option>";
  html += "<option value=\"1\"";
  if (selected_format == clock_tile::DATE_FORMAT_DMY) html += " selected";
  html += ">DD.MM.YYYY</option>";
  html += "<option value=\"2\"";
  if (selected_format == clock_tile::DATE_FORMAT_MDY) html += " selected";
  html += ">MM/DD/YYYY</option>";
  html += "<option value=\"3\"";
  if (selected_format == clock_tile::DATE_FORMAT_YMD) html += " selected";
  html += ">YYYY/MM/DD</option>";
  return html;
}

}  // namespace

// Helper function to generate tile tab HTML (unified for all folders)
static void appendTileTabHTML(
    String& html,
    uint16_t folder_id,
    const FolderEntry& folder,
    const TileGridConfig& grid,
    const std::vector<String>& sensorOptions,
    const std::vector<String>& binarySensorOptions,
    const std::vector<String>& energyOptions,
    const std::vector<String>& weatherOptions,
    const std::vector<SceneOption>& sceneOptions,
    const std::vector<String>& switchOptions,
    const std::vector<String>& mediaOptions,
    const std::vector<String>& climateOptions,
    const std::vector<String>& coverOptions,
    const std::vector<String>& cameraOptions,
    const std::function<String(const String&, uint8_t)>& formatSensorValue,
    const String& navigateOptionsHtml,
    bool screensaver_mode = false
) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  String tab_id = screensaver_mode ? String("screensaver")
                                   : String("folder") + String(folder_id);

  html += R"html(
      <!-- Tile Folder -->
      <div id="tab-tiles-)html";
  html += tab_id;
  html += R"html(" class="tab-content tile-tab" data-tab-id=")html";
  html += tab_id;
  html += R"html(" data-folder-id=")html";
  html += String(folder_id);
  html += R"html(" data-folder-parent=")html";
  html += String(folder.parent_id);
  html += R"html(" data-folder-name=")html";
  appendHtmlEscaped(html, folder.name);
  html += R"html(" data-folder-icon=")html";
  appendHtmlEscaped(html, folder.icon_name);
  if (screensaver_mode) html += R"html(" data-screensaver-grid="1)html";
  html += R"html(">
        <div class="tile-editor">
          <div class="tile-editor-main">
          <div class="tile-grid-scroll">
          <!-- Grid Preview -->
          <div class="tile-grid)html";
  if (screensaver_mode) {
    html += " screensaver-tile-grid";
  } else if (configManager.getConfig().tile_borders) {
    html += " tiles-bordered";
  }
  html += R"html(" id=")html";
  html += tab_id;
  html += R"html(Grid">
)html";

  if (screensaver_mode) {
    html += R"html(            <div class="screensaver-grid-image-frame">
              <img id="screensaverPreviewImage" class="screensaver-grid-image" alt="" draggable="false" hidden>
            </div>
            <div id="screensaverClock" class="screensaver-grid-clock">
              <div id="screensaverClockTime">--:--</div>
              <div id="screensaverClockDate">--.--.----</div>
              <span class="screensaver-clock-resize-handle" title="Resize"></span>
            </div>
)html";
  }

  // Generate tiles
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = grid.tiles[i];
    String cssClass = "tile";
    String tileStyle = "";
    const TileTypeDescriptor* type_desc = get_tile_type_descriptor(tile.type);
    const char* type_css = type_desc ? type_desc->css_class : nullptr;
    uint8_t col = (tile.col < GRID_COLS) ? tile.col : 0;
    uint8_t row = (tile.row < GRID_ROWS) ? tile.row : 0;
    uint8_t span_w = (tile.span_w < 1) ? 1 : tile.span_w;
    uint8_t span_h = (tile.span_h < 1) ? 1 : tile.span_h;
    clamp_media_tile_layout(tile.type, col, row, span_w, span_h);
    if (screensaver_mode && GRID_ROWS > 1 && row < GRID_ROWS - 2) {
      row = GRID_ROWS - 2;
    }
    if (span_w > GRID_COLS - col) span_w = GRID_COLS - col;
    if (span_h > GRID_ROWS - row) span_h = GRID_ROWS - row;

    if (type_css && type_css[0]) {
      cssClass += " ";
      cssClass += type_css;
    } else if (tile.type == TILE_EMPTY) {
      cssClass += " empty";
    }

    if (tile.type != TILE_EMPTY) {
      uint32_t bg_color = tileBgColorIsSet(tile)
                              ? tileBgColorRgb(tile)
                              : (type_desc ? type_desc->default_bg_color : 0);
      if (bg_color == 0) bg_color = 0x353535;
      char colorHex[10];
      if (screensaver_mode) {
        snprintf(colorHex, sizeof(colorHex), "#%06X%02X",
                 (unsigned int)bg_color,
                 static_cast<unsigned int>(tile.background_opacity));
      } else {
        snprintf(colorHex, sizeof(colorHex), "#%06X", (unsigned int)bg_color);
      }
      tileStyle = "background:";
      tileStyle += colorHex;
    }

    tileStyle += ";grid-column:";
    tileStyle += String(static_cast<unsigned>(col + 1));
    tileStyle += " / span ";
    tileStyle += String(static_cast<unsigned>(span_w));
    tileStyle += ";grid-row:";
    tileStyle += String(static_cast<unsigned>(row + 1));
    tileStyle += " / span ";
    tileStyle += String(static_cast<unsigned>(span_h));
    tileStyle += ";";
    if (screensaver_mode && tile.type == TILE_EMPTY &&
        row < (GRID_ROWS > 1 ? GRID_ROWS - 2 : 0)) {
      tileStyle += "display:none;";
    }

    html += "<div class=\"";
    html += cssClass;
    html += "\" data-index=\"";
    html += String(i);
    html += "\" data-col=\"";
    html += String(static_cast<unsigned>(col));
    html += "\" data-row=\"";
    html += String(static_cast<unsigned>(row));
    html += "\" data-span-w=\"";
    html += String(static_cast<unsigned>(span_w));
    html += "\" data-span-h=\"";
    html += String(static_cast<unsigned>(span_h));
    html += "\" data-type=\"";
    html += String(static_cast<unsigned>(tile.type));
    if (tile.type == TILE_FOLDER) {
      html += "\" data-navigate-target=\"";
      html += String(getNavigateTargetId(tile));
    }
    html += "\" draggable=\"true\" id=\"";
    html += tab_id;
    html += "-tile-";
    html += String(i);
    html += "\" style=\"";
    html += tileStyle;
    // The grid is the primary control of the editor, so every tile is a real
    // button: reachable with Tab, activated by Enter/Space (see enableTileKeys
    // in admin.js) and announced with its title or its localized type name.
    html += "\" role=\"button\" tabindex=\"0\" aria-label=\"";
    appendHtmlEscaped(html, tile.title.length()
                                ? tile.title
                                : String(get_tile_type_localized_label(tile.type)));
    html += "\" onclick=\"selectTile(parseInt(this.dataset.index), '";
    html += tab_id;
    html += "')\" ondblclick=\"openPreviewNavigation(this, '";
    html += tab_id;
    html += "')\">";

    const char* preview_kind = get_tile_type_preview_kind(tile.type);
    const bool binary_sensor_preview =
        preview_kind && strcmp(preview_kind, "binary_sensor") == 0;
    BinarySensorState binary_sensor_state;
    if (binary_sensor_preview && tile.sensor_entity.length()) {
      const String payload =
          haBridgeConfig.findSensorInitialValue(tile.sensor_entity);
      binary_sensor_state = parse_binary_sensor_payload(payload.c_str());
    }

    if (tile.type != TILE_EMPTY) {
      // Icon (optional) - normalize icon name (lowercase, trim, remove mdi: prefix)
      String iconName = tile.icon_name;
      iconName.toLowerCase();
      iconName.trim();
      if (iconName.startsWith("mdi:")) iconName.remove(0, 4);
      else if (iconName.startsWith("mdi-")) iconName.remove(0, 4);

      if (binary_sensor_preview) {
        iconName = binary_sensor_resolve_icon(tile, binary_sensor_state);
      }

      bool hasIcon = iconName.length() > 0;

      if (hasIcon) {
        html += "<i class=\"mdi mdi-";
        appendHtmlEscaped(html, iconName);
        html += " tile-icon\"";
        if (binary_sensor_preview) {
          char color_hex[8];
          snprintf(color_hex, sizeof(color_hex), "#%06X",
                   static_cast<unsigned>(
                       binary_sensor_visual_color(binary_sensor_state)));
          html += " style=\"color:";
          html += color_hex;
          html += "\"";
        }
        html += "></i>";
      }

      // Show a title only when one is configured.
      if (tile.title.length()) {
        html += "<div class=\"tile-title\" id=\"";
        html += tab_id;
        html += "-tile-";
        html += String(i);
        html += "-title\">";
        appendTileTitleHtml(html, tile.title);
        html += "</div>";
      }
    }

    if (preview_kind && strcmp(preview_kind, "weather") == 0) {
      html += "<div class=\"tile-ghost-icon\"><i class=\"mdi mdi-weather-partly-cloudy\"></i></div>";
    }
    if (preview_kind && strcmp(preview_kind, "media") == 0) {
      html += "<div class=\"tile-ghost-icon\"><i class=\"mdi mdi-music\"></i></div>";
    }
    if (preview_kind && strcmp(preview_kind, "sensor") == 0) {
      html += "<div class=\"tile-value\" id=\"";
      html += tab_id;
      html += "-tile-";
      html += String(i);
      html += "-value\">";

      String sensorValue = "--";
      if (tile.sensor_entity.length()) {
        sensorValue = haBridgeConfig.findSensorInitialValue(tile.sensor_entity);
        sensorValue = formatSensorValue(sensorValue, tile.sensor_decimals);
        if (sensorValue.length() == 0) {
          sensorValue = "--";
        }
      }
      appendHtmlEscaped(html, sensorValue);

      if (tile.sensor_unit.length()) {
        html += "<span class=\"tile-unit\">";
        appendHtmlEscaped(html, tile.sensor_unit);
        html += "</span>";
      }
      html += "</div>";
    }
    if (tileTypeIsEditableValue(tile.type)) {
      html += "<div class=\"tile-value tile-editable-value sensor-value-size-";
      html += tile.sensor_value_font == 1 ? "20" : tile.sensor_value_font == 2 ? "24" :
              tile.sensor_value_font == 3 ? "32" : tile.sensor_value_font == 4 ? "40" : "default";
      html += "\">";
      appendHtmlEscaped(html, editable_display_value(parse_editable_value(haBridgeConfig.findEditableValue(tile.sensor_entity))));
      html += "</div>";
    }
    if (binary_sensor_preview) {
      html += "<div class=\"tile-value tile-binary-sensor-value\" id=\"";
      html += tab_id;
      html += "-tile-";
      html += String(i);
      html += "-value\">";
      String state_text = "--";
      if (binary_sensor_state.valid) {
        const String state_name = binary_sensor_state.available
                                      ? String(binary_sensor_state_name(
                                            binary_sensor_state.value))
                                      : String("unavailable");
        state_text = i18n::binary_sensor_state_label(
            configManager.getConfig().language, state_name,
            String(binary_sensor_state.device_class));
      }
      appendHtmlEscaped(html, state_text);
      html += "</div>";
    }

    if (preview_kind && strcmp(preview_kind, "clock") == 0) {
      uint8_t flags = tile.sensor_decimals;
      if (flags == 0xFF) flags = 1;
      flags &= 0x03;
      if (flags == 0) flags = 1;
      if (flags & 1) {
        html += "<div class=\"tile-clock-time\">--:--</div>";
      }
      if (flags & 2) {
        html += "<div class=\"tile-clock-date\">--.--.----</div>";
      }
    }

    html += "</div>";
  }

  html += R"html(
          </div>
          </div>
)html";
  if (!screensaver_mode && folder_id == 0) {
    const DeviceConfig& cfg = configManager.getConfig();
    const SettingsTileSnapshot& snapshot = cfg.settings_tile_snapshot;
    bool settings_in_grid = false;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      if (grid.tiles[i].type == TILE_SETTINGS) {
        settings_in_grid = true;
        break;
      }
    }
    // The root grid and the parking slot are two views of one tile. Never
    // render the Settings tile in both places, even if a stale runtime cache
    // briefly disagrees with the persisted visibility flag.
    const bool hidden = cfg.settings_tile_hidden && !settings_in_grid;
    const String hidden_title =
        snapshot.valid && snapshot.title[0]
            ? String(snapshot.title)
            : String(tr.tile_type_settings);
    String hidden_icon =
        snapshot.valid && snapshot.icon_name[0]
            ? String(snapshot.icon_name)
            : String("cog");
    hidden_icon.toLowerCase();
    hidden_icon.trim();
    if (hidden_icon.startsWith("mdi:")) hidden_icon.remove(0, 4);
    else if (hidden_icon.startsWith("mdi-")) hidden_icon.remove(0, 4);
    const uint32_t hidden_color =
        snapshot.valid && snapshot.bg_color != 0
            ? (snapshot.bg_color & TILE_BG_COLOR_RGB_MASK)
            : 0x2A2A2A;
    char hidden_color_hex[8];
    snprintf(hidden_color_hex, sizeof(hidden_color_hex), "#%06X",
             static_cast<unsigned>(hidden_color));
    html += "<div class=\"settings-hidden-parking\"><div id=\"settingsHiddenSlot\" class=\"tile-grid settings-hidden-slot";
    if (configManager.getConfig().tile_borders) html += " tiles-bordered";
    if (hidden) html += " has-tile";
    html += "\"><div id=\"settingsHiddenTile\" class=\"tile settings-hidden-tile ";
    html += hidden ? "navigate" : "empty";
    html += "\"";
    html += hidden ? " draggable=\"true\"" : " draggable=\"false\"";
    html += " data-hidden=\"" + String(hidden ? "1" : "0") + "\"";
    html += " data-title=\"";
    appendHtmlEscaped(html, hidden_title);
    html += "\" data-icon=\"";
    appendHtmlEscaped(html, hidden_icon);
    html += "\" data-bg-color=\"" + String(snapshot.valid ? snapshot.bg_color : 0) + "\"";
    html += " data-col=\"" + String(snapshot.valid ? snapshot.col : 0) + "\"";
    html += " data-row=\"" + String(snapshot.valid ? snapshot.row : 0) + "\"";
    html += " data-span-w=\"" +
            String(snapshot.valid && snapshot.span_w ? snapshot.span_w : 1) +
            "\" data-span-h=\"" +
            String(snapshot.valid && snapshot.span_h ? snapshot.span_h : 1) +
            "\" style=\"background:" +
            String(hidden ? hidden_color_hex : "transparent") + "\"";
    if (hidden) {
      html += "><i class=\"mdi mdi-";
      appendHtmlEscaped(html, hidden_icon);
      html += " tile-icon\"></i><div class=\"tile-title\">";
      appendTileTitleHtml(html, hidden_title);
      html += "</div>";
    } else {
      html += "><i class=\"mdi mdi-tray-arrow-down tile-icon\"></i>";
    }
    html += "</div></div><div id=\"settingsHiddenHint\" class=\"settings-hidden-hint";
    if (hidden) html += " is-hidden";
    html += "\">";
    appendHtmlEscaped(html, tr.settings_tile_parking);
    html += "</div></div>";
  }
  html += R"html(          <div class="folder-footer">
            <div class="folder-footer-options">
)html";
  if (screensaver_mode) {
    html += R"html(              <label class="inline-checkbox"><input id="screensaverTileBorder" type="checkbox"> )html";
  } else {
    html += R"html(              <label class="inline-checkbox"><input class="normal-tile-border-toggle" type="checkbox" onchange="saveNormalTileBorders(this.checked)" )html";
    if (configManager.getConfig().tile_borders) html += "checked";
    html += "> ";
  }
  html += tr.screensaver_tile_border;
  html += R"html(</label>
)html";
  if (screensaver_mode) {
    html += R"html(              <label class="inline-checkbox"><input id="screensaverTileShadow" type="checkbox"> )html";
    html += tr.screensaver_tile_shadow;
    html += R"html(</label>
)html";
  }
  html += R"html(            </div>
            <p class="hint">)html";
  if (screensaver_mode) {
    html += tr.screensaver_hint;
  } else {
    html += tr.admin_tile_hint;
  }
  html += R"html(</p>
)html";
  if (!screensaver_mode && folder_id != 0) {
    html += R"html(            <button type="button" class="btn btn-danger btn-delete-folder" onclick="deleteFolder(')html";
    html += tab_id;
    html += R"html(')">)html";
    html += tr.admin_delete_folder_tab;
    html += R"html(</button>
)html";
  }
  html += R"html(          </div>
          </div>

          <!-- Settings Panel -->
          <div class="tile-settings" id=")html";
  html += tab_id;
  html += R"html(Settings">
)html";
  if (screensaver_mode) {
    html += R"html(            <div id="screensaverBackgroundSettings" class="screensaver-background-settings">
              <div class="tile-settings-head"><h3 style="margin-top:0;">)html";
    html += tr.admin_slideshow;
    html += R"html(</h3></div>
              <div class="tile-settings-body">
                <div class="screensaver-fixed-type"><label>)html";
    html += tr.admin_type;
    html += R"html(</label><input value=")html";
    html += tr.admin_slideshow;
    html += R"html(" disabled></div>
                <label class="inline-checkbox"><input id="screensaverUseWallpapers" type="checkbox"> )html";
    html += tr.screensaver_use_wallpapers;
    html += R"html(</label>
                <label class="inline-checkbox"><input id="screensaverShuffle" type="checkbox"> )html";
    html += tr.screensaver_shuffle;
    html += R"html(</label>
                <div class="screensaver-wallpaper-heading">)html";
    html += tr.screensaver_wallpapers_heading;
    html += R"html(</div>
                <div class="screensaver-storage-hint">)html";
    html += tr.screensaver_storage_hint;
    html += R"html(</div>
                <div id="screensaverWallpaperList" class="screensaver-wallpaper-list"></div>
                <div id="screensaverWallpaperControls" class="screensaver-wallpaper-controls">
                  <label>)html";
    html += tr.screensaver_duration_seconds;
    html += R"html(</label><input id="screensaverWallpaperDuration" type="number" min="3" max="3600" value="15">
                  <label>)html";
    html += tr.screensaver_zoom;
    html += R"html(</label><input id="screensaverWallpaperZoom" type="range" min="1000" max="3000" step="25" value="1000">
                  <div class="screensaver-focus-grid">
                    <label>)html";
    html += tr.screensaver_focus_x;
    html += R"html(<input id="screensaverFocusX" type="range" min="0" max="1000" value="500"></label>
                    <label>)html";
    html += tr.screensaver_focus_y;
    html += R"html(<input id="screensaverFocusY" type="range" min="0" max="1000" value="500"></label>
                  </div>
                </div>
              </div>
            </div>
            <div id="screensaverClockSettings" class="screensaver-background-settings screensaver-clock-settings hidden">
              <div class="tile-settings-head"><h3 style="margin-top:0;">)html";
    html += tr.screensaver_clock_heading;
    html += R"html(</h3></div>
              <div class="tile-settings-body">
                <div class="screensaver-fixed-type"><label>)html";
    html += tr.admin_type;
    html += R"html(</label><input value=")html";
    html += tr.tile_type_clock;
    html += R"html(" disabled></div>
                  <div class="clock-toggle-row">
                    <label class="inline-checkbox"><input id="screensaverShowTime" type="checkbox"> )html";
    html += tr.show_time;
    html += R"html(</label>
                    <label class="inline-checkbox"><input id="screensaverShowDate" type="checkbox"> )html";
    html += tr.show_date;
    html += R"html(</label>
                    <label class="inline-checkbox"><input id="screensaverShowWeekday" type="checkbox"> )html";
    html += tr.screensaver_show_weekday;
    html += R"html(</label>
                    <label class="inline-checkbox"><input id="screensaverClockShadow" type="checkbox"> )html";
    html += tr.screensaver_clock_shadow;
    html += R"html(</label>
                  </div>
                  <div class="screensaver-two-fields">
                    <label>)html";
    html += tr.time_font_size;
    html += R"html(<select id="screensaverTimeFont"><option>20</option><option>24</option><option>28</option><option>32</option><option>40</option><option selected>48</option><option>56</option><option>64</option><option>72</option><option>80</option><option>96</option></select></label>
                    <label>)html";
    html += tr.date_font_size;
    html += R"html(<select id="screensaverDateFont"><option>20</option><option>24</option><option selected>28</option><option>32</option><option>40</option><option>48</option><option>56</option><option>64</option><option>72</option></select></label>
                    <label>)html";
    html += tr.screensaver_time_alignment;
    html += R"html(<select id="screensaverTimeAlignment"><option value="0">)html";
    html += tr.alignment_left;
    html += R"html(</option><option value="1" selected>)html";
    html += tr.alignment_center;
    html += R"html(</option><option value="2">)html";
    html += tr.alignment_right;
    html += R"html(</option></select></label>
                    <label>)html";
    html += tr.screensaver_date_alignment;
    html += R"html(<select id="screensaverDateAlignment"><option value="0">)html";
    html += tr.alignment_left;
    html += R"html(</option><option value="1" selected>)html";
    html += tr.alignment_center;
    html += R"html(</option><option value="2">)html";
    html += tr.alignment_right;
    html += R"html(</option></select></label>
                    <label>)html";
    html += tr.time_format_label;
    html += R"html(<select id="screensaverTimeFormat"><option value="0">)html";
    html += tr.format_auto_localization;
    html += R"html(</option><option value="1">24 h</option><option value="2">12 h</option></select></label>
                    <label>)html";
    html += tr.date_format_label;
    html += R"html(<select id="screensaverDateFormat"><option value="0">)html";
    html += tr.format_auto_localization;
    html += R"html(</option><option value="1">DD.MM.YYYY</option><option value="2">MM/DD/YYYY</option><option value="3">YYYY/MM/DD</option></select></label>
                  </div>
              </div>
            </div>
)html";
  }
  html += R"html(
            <!-- Tile Settings (Visible only when tile selected) -->
            <div class="tile-specific-settings hidden">
            <div class="tile-settings-head">
              <h3 style="margin-top:0;">)html";
  html += tr.admin_tile_settings;
  html += R"html(</h3>

            <label>)html";
  html += tr.admin_type;
  html += R"html(</label>
            <select id=")html";
  html += tab_id;
  html += R"html(_tile_type" onchange="updateTileType(')html";
  html += tab_id;
  html += R"html(')">
            )html";
  if (screensaver_mode) {
    html += "<option value=\"0\">";
    html += tr.tile_type_empty;
    html += "</option><option value=\"1\">";
    html += tr.tile_type_sensor;
    html += "</option><option value=\"20\">";
    html += i18n::binary_sensor_label(
        configManager.getConfig().language, 0);
    html += "</option><option value=\"21\">";
    html += i18n::locale(configManager.getConfig().language).editable_labels[0];
    html += "</option><option value=\"22\">";
    html += i18n::locale(configManager.getConfig().language).editable_labels[1];
    html += "</option><option value=\"23\">";
    html += i18n::locale(configManager.getConfig().language).editable_labels[2];
    html += "</option><option value=\"14\">";
    html += tr.tile_type_energy;
    html += "</option><option value=\"2\">";
    html += tr.tile_type_scene;
    html += "</option><option value=\"5\">";
    html += tr.tile_type_switch;
    html += "</option><option value=\"15\">";
    html += tr.tile_type_media;
    html += "</option>";
  } else {
    append_tile_type_select_options(html);
  }
  html += R"html(
            </select>
            <p class="hint hidden" id=")html";
  html += tab_id;
  html += R"html(_tile_type_hint">)html";
  html += tr.admin_folder_type_locked;
  html += R"html(</p>
            </div>
            <div class="tile-settings-body">

            <label>)html";
  html += tr.admin_title;
  html += R"html(</label>
            <textarea rows="2" class="tile-title-input" id=")html";
  html += tab_id;
  html += R"html(_tile_title" placeholder=")html";
  html += tr.admin_tile_title_placeholder;
  html += R"html("></textarea>

            <label>)html";
  html += tr.admin_icon_label;
  html += R"html(</label>
            <input type="text" id=")html";
  html += tab_id;
  html += R"html(_tile_icon" placeholder=")html";
  html += tr.admin_icon_placeholder;
  html += R"html(">
            <div style="font-size:11px;color:#8a8a8a;margin-top:4px;">
              Material Design Icons: <a href="https://pictogrammers.com/library/mdi/" target="_blank" style="color:#4db6ac;">)html";
  html += tr.admin_icon_list;
  html += R"html(</a>
            </div>

            <div class="tile-color-label-row)html";
  if (screensaver_mode) html += " has-opacity";
  html += R"html("><span>)html";
  html += tr.admin_color;
  html += R"html(</span>)html";
  if (screensaver_mode) {
    html += R"html(<span>)html";
    html += tr.screensaver_background_opacity;
    html += R"html(</span><span aria-hidden="true"></span>)html";
  }
  html += R"html(</div>
            <div class="tile-color-row)html";
  if (screensaver_mode) html += " has-opacity";
  html += R"html(">
            <input type="color" id=")html";
  html += tab_id;
  html += R"html(_tile_color" value="#2A2A2A">
)html";
  if (screensaver_mode) {
    html += R"html(              <input type="range" id="screensaver_tile_opacity" min="0" max="255" step="1" value="0">
)html";
  }
  html += R"html(              <button type="button" class="tile-color-reset-btn" title="Reset" onclick="resetTileColor(')html";
  html += tab_id;
  html += R"html(')"><i class="mdi mdi-restore"></i></button>
            </div>

            <div class="tile-layout">
              <div class="layout-field">
                <label>)html";
  html += tr.admin_column;
  html += R"html(</label>
                <input type="number" id=")html";
  html += tab_id;
  html += R"html(_tile_col" min="1" max=")html";
  html += String(GRID_COLS);
  html += R"html(" step="1" value="1">
              </div>
              <div class="layout-field">
                <label>)html";
  html += tr.admin_row;
  html += R"html(</label>
                <input type="number" id=")html";
  html += tab_id;
  html += R"html(_tile_row" min=")html";
  html += String(screensaver_mode && GRID_ROWS > 1 ? GRID_ROWS - 1 : 1);
  html += R"html(" max=")html";
  html += String(GRID_ROWS);
  html += R"html(" step="1" value="1">
              </div>
              <div class="layout-field">
                <label>)html";
  html += tr.admin_width_cells;
  html += R"html(</label>
                <input type="number" id=")html";
  html += tab_id;
  html += R"html(_tile_span_w" min="1" max=")html";
  html += String(GRID_COLS);
  html += R"html(" step="1" value="1">
              </div>
              <div class="layout-field">
                <label>)html";
  html += tr.admin_height_cells;
  html += R"html(</label>
                <input type="number" id=")html";
  html += tab_id;
  html += R"html(_tile_span_h" min="1" max=")html";
  html += String(GRID_ROWS);
  html += R"html(" step="1" value="1">
              </div>
            </div>

)html";

            TileTypeWebContext type_ctx;
            type_ctx.tab_id = &tab_id;
            type_ctx.sensor_options = &sensorOptions;
            type_ctx.binary_sensor_options = &binarySensorOptions;
            type_ctx.energy_options = &energyOptions;
            type_ctx.weather_options = &weatherOptions;
            type_ctx.scene_options = &sceneOptions;
            type_ctx.switch_options = &switchOptions;
            type_ctx.media_options = &mediaOptions;
            type_ctx.climate_options = &climateOptions;
            type_ctx.cover_options = &coverOptions;
            type_ctx.camera_options = &cameraOptions;
            type_ctx.navigate_options_html = &navigateOptionsHtml;
            append_tile_type_fields_html(html, type_ctx);

  html += R"html(
            </div><!-- /tile-settings-body -->
            <div class="tile-actions">
                <button type="button" class="btn" onclick="copyTile(')html";
  html += tab_id;
  html += R"html(')">)html";
  html += tr.admin_copy;
  html += R"html(</button>
                <button type="button" class="btn" onclick="pasteTile(')html";
  html += tab_id;
  html += R"html(')">)html";
  html += tr.admin_paste;
  html += R"html(</button>
                <button type="button" class="btn btn-danger" onclick="resetTile(')html";
  html += tab_id;
  html += R"html(')">)html";
  html += tr.admin_delete;
  html += R"html(</button>
            </div>
            </div><!-- /tile-specific-settings -->
          </div>
        </div>
      </div>
)html";
}

static String buildFolderTabButtonHtml(const FolderEntry& entry) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  String tab_id = "folder" + String(entry.id);
  String icon = String(entry.icon_name);
  String name = String(entry.name);
  icon.trim();
  icon.toLowerCase();
  if (icon.startsWith("mdi:")) icon = icon.substring(4);
  else if (icon.startsWith("mdi-")) icon = icon.substring(4);
  name.trim();
  if (!name.length()) {
    name = (entry.id == 0) ? String(tr.home) : String(tr.folder_prefix) + String(entry.id);
  }

  String html;
  html += R"html(
        <button class="tab-btn folder-tab-btn" data-folder-id=")html";
  html += String(entry.id);
  html += R"html(" data-folder-parent=")html";
  html += String(entry.parent_id);
  html += R"html(" data-folder-name=")html";
  appendHtmlEscaped(html, name);
  html += R"html(" data-folder-icon=")html";
  appendHtmlEscaped(html, icon);
  html += R"html(" data-tab-id=")html";
  html += tab_id;
  html += R"html(" data-tab-target="tab-tiles-)html";
  html += tab_id;
  html += R"html(" type="button" onclick="switchTab('tab-tiles-)html";
  html += tab_id;
  html += R"html(')">)html";
  if (icon.length()) {
    html += R"html(
          <i class="mdi mdi-)html";
    html += icon;
    html += R"html(" style="font-size:24px;"></i>)html";
  }
  html += R"html(
          <span style="font-size:14px;font-weight:600;">)html";
  appendHtmlEscaped(html, name);
  html += R"html(</span>
        </button>
)html";
  return html;
}

bool buildAdminFolderTabFragments(uint16_t folder_id, String& button_html, String& tab_html, String& tab_id) {
  const FolderEntry* folder = tileConfig.getFolder(folder_id);
  if (!folder) return false;

  const HaBridgeConfigData& ha = haBridgeConfig.get();
  const auto sensorOptions = parseSensorList(ha.sensors_text);
  const auto binarySensorOptions = parseSensorList(ha.binary_sensors_text);
  auto energyOptions = parseSensorList(ha.energy_text);
  energy_append_cached_entity_ids(energyOptions);
  const auto weatherOptions = parseSensorList(ha.weathers_text);
  const auto sceneOptions = parseSceneList(ha.scene_alias_text);
  const auto lightOptions = parseSensorList(ha.lights_text);
  const auto switchOptionsRaw = parseSensorList(ha.switches_text);
  const auto mediaOptions = parseSensorList(ha.media_players_text);
  const auto climateOptions = parseSensorList(ha.climates_text);
  const auto coverOptions = parseSensorList(ha.covers_text);
  const auto cameraOptions = parseSensorList(ha.cameras_text);
  std::vector<String> switchOptions;
  switchOptions.reserve(lightOptions.size() + switchOptionsRaw.size());
  auto addSwitchOption = [&](const String& entry) {
    if (!entry.length()) return;
    for (const auto& existing : switchOptions) {
      if (existing.equalsIgnoreCase(entry)) return;
    }
    switchOptions.push_back(entry);
  };
  for (const auto& opt : lightOptions) addSwitchOption(opt);
  for (const auto& opt : switchOptionsRaw) addSwitchOption(opt);
  addSwitchOption(kEntityDisplayBrightness);
  addSwitchOption(kEntityScreensaverBrightness);
  addSwitchOption(kEntityDisplayRotate);
  addSwitchOption(kEntityDisplaySleep);

  auto formatSensorValue = [](const String& raw, uint8_t decimals) -> String {
    String v = raw;
    v.trim();
    if (!v.length()) return String("--");
    String lower = v;
    lower.toLowerCase();
    if (lower == "unavailable") return String("--");
    if (decimals == 0xFF) {
      return i18n::localize_numeric_text(
          configManager.getConfig().language, v);
    }
    String normalized = v;
    normalized.replace(",", ".");
    char* end = nullptr;
    float f = strtof(normalized.c_str(), &end);
    if (!end || end == normalized.c_str()) return v;
    if (isnan(f) || isinf(f)) return v;
    return i18n::format_number(
        configManager.getConfig().language, f, decimals);
  };

  String navigateOptionsHtml;
  for (const auto& entry : tileConfig.getFolders()) {
    if (entry.id == 0) continue;
    String label = String(entry.name);
    label.trim();
    if (!label.length()) {
      label = i18n::strings(configManager.getConfig().language).folder_prefix;
      label += String(entry.id);
    }
    navigateOptionsHtml += "<option value=\"";
    navigateOptionsHtml += String(entry.id);
    navigateOptionsHtml += "\">";
    appendHtmlEscaped(navigateOptionsHtml, label);
    navigateOptionsHtml += "</option>\n";
  }

  TileGridConfig grid{};
  tileConfig.loadFolderGrid(folder_id, grid);
  tab_id = "folder" + String(folder_id);
  button_html = buildFolderTabButtonHtml(*folder);
  tab_html = "";
  appendTileTabHTML(tab_html, folder_id, *folder, grid, sensorOptions,
                    binarySensorOptions, energyOptions, weatherOptions,
                    sceneOptions, switchOptions, mediaOptions, climateOptions,
                    coverOptions, cameraOptions,
                    formatSensorValue, navigateOptionsHtml);
  return true;
}

String WebAdminServer::getAdminPage() {
  const DeviceConfig& cfg = configManager.getConfig();
  const auto& tr = i18n::strings(cfg.language);
  const bool supports_ethernet =
      NetworkTransportManager::deviceSupportsEthernet();
  const bool use_static = cfg.wifi_static_enabled;
  const String admin_panel_title =
      String(Device::displayName()) + " " + tr.admin_panel_word;
  const String admin_heading_title = String("HomeTiles ") + tr.admin_panel_word;
  const String admin_heading_subtitle =
      String(FW_VERSION) + "  \xC2\xB7  " + Device::displayName();
  const String current_firmware_name =
      String("hometiles_") + FW_VERSION + "_" + Device::profile().key;
  const HaBridgeConfigData& ha = haBridgeConfig.get();
  const auto sensorOptions = parseSensorList(ha.sensors_text);
  const auto binarySensorOptions = parseSensorList(ha.binary_sensors_text);
  auto energyOptions = parseSensorList(ha.energy_text);
  energy_append_cached_entity_ids(energyOptions);
  const auto weatherOptions = parseSensorList(ha.weathers_text);
  const auto sceneOptions = parseSceneList(ha.scene_alias_text);
  const auto lightOptions = parseSensorList(ha.lights_text);
  const auto switchOptionsRaw = parseSensorList(ha.switches_text);
  const auto mediaOptions = parseSensorList(ha.media_players_text);
  const auto climateOptions = parseSensorList(ha.climates_text);
  const auto coverOptions = parseSensorList(ha.covers_text);
  const auto cameraOptions = parseSensorList(ha.cameras_text);
  std::vector<String> switchOptions;
  switchOptions.reserve(lightOptions.size() + switchOptionsRaw.size());
  auto addSwitchOption = [&](const String& entry) {
    if (!entry.length()) return;
    for (const auto& existing : switchOptions) {
      if (existing.equalsIgnoreCase(entry)) {
        return;
      }
    }
    switchOptions.push_back(entry);
  };
  for (const auto& opt : lightOptions) {
    addSwitchOption(opt);
  }
  for (const auto& opt : switchOptionsRaw) {
    addSwitchOption(opt);
  }
  addSwitchOption(kEntityDisplayBrightness);
  addSwitchOption(kEntityScreensaverBrightness);
  addSwitchOption(kEntityDisplayRotate);
  addSwitchOption(kEntityDisplaySleep);
  auto formatSensorValue = [](const String& raw, uint8_t decimals) -> String {
    String v = raw;
    v.trim();
    if (!v.length()) return String("--");
    String lower = v;
    lower.toLowerCase();
    if (lower == "unavailable") return String("--");
    if (decimals == 0xFF) {
      return i18n::localize_numeric_text(
          configManager.getConfig().language, v);
    }
    String normalized = v;
    normalized.replace(",", ".");
    char* end = nullptr;
    float f = strtof(normalized.c_str(), &end);
    if (!end || end == normalized.c_str()) return v;  // Non-numeric value.
    if (isnan(f) || isinf(f)) return v;
    return i18n::format_number(
        configManager.getConfig().language, f, decimals);
  };

  const auto& folders = tileConfig.getFolders();
  String navigateOptionsHtml;
  for (const auto& entry : folders) {
    if (entry.id == 0) continue;
    String label = String(entry.name);
    label.trim();
    if (!label.length()) {
      label = tr.folder_prefix;
      label += String(entry.id);
    }
    navigateOptionsHtml += "<option value=\"";
    navigateOptionsHtml += String(entry.id);
    navigateOptionsHtml += "\">";
    appendHtmlEscaped(navigateOptionsHtml, label);
    navigateOptionsHtml += "</option>\n";
  }

  String html;
  // The shell still contains Home, settings and the screensaver editor. A
  // realistic reserve avoids repeated reallocations while the other folders
  // are loaded on demand by the browser.
  html.reserve(192 * 1024);
  html += "<!DOCTYPE html>\n<html lang=\"";
  html += tr.html_lang;
  html += R"html(">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>)html";
  html += admin_panel_title;
  html += R"html(</title>
)html";

  appendAdminStyles(html);
  appendAdminScripts(html);

  html += R"html(
</head>
<body>
  <div class="wrapper">
    <div class="card">
      <div class="brand">
        <svg width="44" height="44" viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
          <rect x="4" y="4" width="17" height="17" rx="4" fill="#ffffff"/>
          <rect x="27" y="4" width="17" height="17" rx="4" fill="#ffffff"/>
          <rect x="4" y="27" width="17" height="17" rx="4" fill="#ffffff"/>
          <path d="M33 26h5v6.5h6.5v5H38V44h-5v-6.5h-6.5v-5H33z" fill="#26a69a"/>
        </svg>
        <div>
          <h1>)html";
  html += admin_heading_title;
  html += R"html(</h1>
          <div class="device">)html";
  html += admin_heading_subtitle;
  html += R"html(</div>
        </div>
        <div class="brand-links">
          <a class="brand-link" href="https://galusperes.github.io/HomeTiles/" target="_blank" rel="noopener"><i class="mdi mdi-book-open-variant"></i>Docs</a>
          <a class="brand-link" href="https://github.com/GalusPeres/HomeTiles" target="_blank" rel="noopener"><i class="mdi mdi-github"></i>GitHub</a>
        </div>
      </div>
      
      <!-- Tab Navigation -->
      <div class="tab-nav">
)html";

  for (const auto& entry : folders) {
    html += buildFolderTabButtonHtml(entry);
  }

  html += R"html(
        <button class="tab-btn" type="button" data-tab-target="tab-tiles-screensaver"
                onclick="switchTab('tab-tiles-screensaver')">
          <i class="mdi mdi-monitor" style="font-size:24px;"></i>
          <span style="font-size:14px;font-weight:600;">Screensaver</span>
        </button>
        <button class="tab-btn" type="button" data-tab-target="tab-hardware"
                onclick="switchTab('tab-hardware')">
          <i class="mdi mdi-electric-switch" style="font-size:24px;"></i>
          <span style="font-size:14px;font-weight:600;">)html";
  html += tr.admin_io;
  html += R"html(</span>
        </button>
        <button class="tab-btn" type="button" data-tab-target="tab-network"
                onclick="switchTab('tab-network')">
          <i class="mdi mdi-cog" style="font-size:24px;"></i>
          <span style="font-size:14px;font-weight:600;">)html";
  html += tr.tile_type_settings;
  html += R"html(</span>
        </button>
      </div>
)html";

  // Only Home is part of the initial page. Other folder editors are generated
  // on first use via /api/folders/tab, avoiding synchronous all-folder reads.
  for (const auto& entry : folders) {
    if (entry.id != 0) continue;
    TileGridConfig grid{};
    tileConfig.loadFolderGrid(entry.id, grid);
    appendTileTabHTML(html, entry.id, entry, grid, sensorOptions,
                      binarySensorOptions, energyOptions,
                      weatherOptions, sceneOptions, switchOptions, mediaOptions,
                      climateOptions, coverOptions, cameraOptions, formatSensorValue,
                      navigateOptionsHtml);
  }

  FolderEntry screensaver_folder{};
  screensaver_folder.id = TileConfig::kScreensaverGridStorageId;
  screensaver_folder.parent_id = 0;
  snprintf(screensaver_folder.name, sizeof(screensaver_folder.name), "%s",
           "Screensaver");
  snprintf(screensaver_folder.icon_name, sizeof(screensaver_folder.icon_name),
           "%s", "monitor");
  appendTileTabHTML(html, TileConfig::kScreensaverGridStorageId,
                    screensaver_folder, screensaverConfig.tileGrid(),
                    sensorOptions, binarySensorOptions, energyOptions,
                    weatherOptions, sceneOptions,
                    switchOptions, mediaOptions, climateOptions, coverOptions, cameraOptions,
                    formatSensorValue, navigateOptionsHtml, true);

  html += R"html(
      <!-- Local GPIO / relay / temperature assignments -->
      <div id="tab-hardware" class="tab-content">
        <div class="hardware-io-content">
          <div class="settings-actions hardware-io-toolbar">
            <button id="hardwareIoAddSwitch" class="btn btn-secondary hardware-io-add-button" type="button">+ )html";
  html += tr.tile_type_switch;
  html += R"html(</button>
            <button id="hardwareIoAddTemperature" class="btn btn-secondary hardware-io-add-button" type="button">+ )html";
  html += tr.admin_io_temperature;
  html += R"html(</button>
          </div>
          <div id="hardwareIoList" class="hardware-io-list">
            <div class="hardware-io-loading">)html";
  html += tr.loading;
  html += R"html(</div>
          </div>
          <div class="admin-footer-actions hardware-io-footer">
            <div id="hardwareIoSaveState" class="hardware-io-save-state"></div>
            <button id="hardwareIoSave" class="btn btn-go admin-footer-btn" type="button">)html";
  html += tr.save;
  html += R"html(</button>
            <button id="hardwareIoRestart" class="btn btn-secondary admin-footer-btn" type="button">)html";
  html += tr.restart_button;
  html += R"html(</button>
          </div>
        </div>
      </div>

      <!-- Tab 3: Settings (Network/MQTT Configuration) -->
      <div id="tab-network" class="tab-content">
        <form id="admin_settings_form" action="/mqtt" method="POST" autocomplete="on">
          <div class="settings-section">
            <div class="section-title-row">
              <div class="section-title">)html";
  html += supports_ethernet
              ? tr.admin_network_section
              : tr.admin_settings_wifi;
  html += R"html(</div>
              <div class="wifi-inline-status"><span class="wifi-inline-dot)html";
  if (!networkTransport.isConnected()) {
    html += " off";
  }
  html += R"html("></span>)html";
  html += networkTransport.isConnected() ? tr.wifi_connected : tr.wifi_disconnected;
  if (networkTransport.isConnected()) {
    html += " \xC2\xB7 ";
    if (networkTransport.activeKind() == NetworkTransportKind::Wifi) {
      appendHtmlEscaped(html, WiFi.SSID());
    } else {
      appendHtmlEscaped(html, networkTransport.activeName());
    }
    html += " \xC2\xB7 ";
    html += networkTransport.localIP().toString();
  }
  html += R"html(</div>
            </div>
            <div class="settings-grid">)html";
  if (supports_ethernet) {
    html += R"html(
              <div class="settings-full">
                <label for="network_mode">)html";
    html += tr.admin_connection_type;
    html += R"html(:</label>
                <select id="network_mode" name="network_mode" onchange="toggleNetworkSettings()">
                  <option value="wifi")html";
    if (!cfg.ethernet_enabled) {
      html += " selected";
    }
    html += R"html(>WiFi</option>
                  <option value="ethernet")html";
    if (cfg.ethernet_enabled) {
      html += " selected";
    }
    html += R"html(>Ethernet</option>
                </select>
                <div class="settings-note">)html";
    html += tr.admin_connection_type_note;
    html += R"html(</div>
              </div>)html";
  }
  html += R"html(
              <div id="wifi_network_settings" class="network-settings-group settings-full )html";
  if (supports_ethernet && cfg.ethernet_enabled) {
    html += "is-hidden";
  }
  html += R"html(">)html";
  if (supports_ethernet) {
    html += R"html(
                <div class="network-settings-heading">WiFi</div>)html";
  }
  html += R"html(
                <div class="settings-subgrid">
                  <div>
                <label for="wifi_ssid">)html";
  html += tr.ssid_label;
  html += R"html(:</label>
                <input type="text" id="wifi_ssid" name="wifi_ssid" value=")html";
  appendHtmlEscaped(html, cfg.wifi_ssid);
  html += R"html(">
                  </div>
                  <div>
                <label for="wifi_pass">)html";
  html += tr.wifi_password_label;
  html += R"html(:</label>
                <div class="password-field">
                  <input type="password" id="wifi_pass" name="wifi_pass"
                         autocomplete="new-password" value=")html";
  appendHtmlEscaped(html, cfg.wifi_pass);
  html += R"html(">
                  <button type="button" class="password-toggle" data-label-show=")html";
  html += tr.password_show;
  html += R"html(" data-label-hide=")html";
  html += tr.password_hide;
  html += R"html(" onclick="togglePasswordVisibility('wifi_pass', this)">)html";
  html += tr.password_show;
  html += R"html(</button>
                </div>
                  </div>
                </div>
              </div>

              <div id="network_ip_settings" class="network-settings-group settings-full">
                <div class="network-settings-heading">)html";
  html += tr.admin_ip_configuration;
  html += R"html(</div>
                <label class="settings-checkbox">
                  <input type="checkbox" id="network_use_static" name="network_use_static" onchange="toggleStaticNetworkFields()")html";
  if (use_static) html += " checked";
  html += R"html(>
                  <span>)html";
  html += tr.admin_ip_use_static;
  html += R"html(</span>
                </label>
                <div id="network_ip_mode_note" class="settings-note"
                     data-dhcp-note=")html";
  html += tr.admin_ip_dhcp_note;
  html += R"html(" data-static-note=")html";
  html += tr.admin_ip_static_note;
  html += R"html(">)html";
  html += use_static ? tr.admin_ip_static_note : tr.admin_ip_dhcp_note;
  html += R"html(</div>
                <div id="network_static_fields" class="settings-subgrid )html";
  if (!use_static) {
    html += "is-hidden";
  }
  html += R"html(">
                <div>
                <label for="network_static_ip">)html";
  html += tr.wifi_static_ip_label;
  html += R"html(:</label>
                <input type="text" id="network_static_ip" name="network_static_ip" inputmode="decimal" autocomplete="on" value=")html";
  appendHtmlEscaped(html, cfg.wifi_static_ip);
  html += R"html(">
                </div>
                <div>
                <label for="network_gateway">)html";
  html += tr.wifi_gateway_label;
  html += R"html(:</label>
                <input type="text" id="network_gateway" name="network_gateway" inputmode="decimal" autocomplete="on" value=")html";
  appendHtmlEscaped(html, cfg.wifi_gateway);
  html += R"html(">
                </div>
                <div>
                <label for="network_subnet">)html";
  html += tr.wifi_subnet_label;
  html += R"html(:</label>
                <input type="text" id="network_subnet" name="network_subnet" inputmode="decimal" autocomplete="on" value=")html";
  appendHtmlEscaped(html, cfg.wifi_subnet);
  html += R"html(">
                </div>
                <div>
                <label for="network_dns">)html";
  html += tr.wifi_dns_label;
  html += R"html(:</label>
                <input type="text" id="network_dns" name="network_dns" inputmode="decimal" autocomplete="on" value=")html";
  appendHtmlEscaped(html, cfg.wifi_dns);
  html += R"html(">
                </div>
                </div>
              </div>)html";
  html += R"html(
            </div>
          </div>

          <div class="settings-section">
            <div class="section-title">)html";
  html += tr.admin_settings_mqtt;
  html += R"html(</div>
            <div class="settings-grid">
              <div>
                <label for="mqtt_host">)html";
  html += tr.mqtt_host;
  html += R"html(:</label>
                <input type="text" id="mqtt_host" name="mqtt_host" value=")html";
  appendHtmlEscaped(html, cfg.mqtt_host);
  html += R"html(">
              </div>
              <div>
                <label for="mqtt_port">)html";
  html += tr.mqtt_port;
  html += R"html(:</label>
                <input type="number" id="mqtt_port" name="mqtt_port" value=")html";
  html += String(cfg.mqtt_port ? cfg.mqtt_port : 1883);
  html += R"html(">
              </div>
              <div>
                <label for="mqtt_user">)html";
  html += tr.mqtt_username;
  html += R"html(:</label>
                <input type="text" id="mqtt_user" name="mqtt_user" value=")html";
  appendHtmlEscaped(html, cfg.mqtt_user);
  html += R"html(">
              </div>
              <div>
                <label for="mqtt_pass">)html";
  html += tr.mqtt_password;
  html += R"html(:</label>
                <div class="password-field">
                  <input type="password" id="mqtt_pass" name="mqtt_pass"
                         autocomplete="new-password" value=")html";
  appendHtmlEscaped(html, cfg.mqtt_pass);
  html += R"html(">
                  <button type="button" class="password-toggle" data-label-show=")html";
  html += tr.password_show;
  html += R"html(" data-label-hide=")html";
  html += tr.password_hide;
  html += R"html(" onclick="togglePasswordVisibility('mqtt_pass', this)">)html";
  html += tr.password_show;
  html += R"html(</button>
                </div>
              </div>
              <div class="settings-full">
                <label for="mqtt_client_id">)html";
  html += tr.mqtt_client_id;
  html += R"html(:</label>
                <input type="text" id="mqtt_client_id" name="mqtt_client_id" placeholder=")html";
  html += tr.mqtt_client_id_placeholder;
  html += R"html(" value=")html";
  appendHtmlEscaped(html, cfg.mqtt_client_id);
  html += R"html(">
                <div class="settings-note">)html";
  html += tr.mqtt_client_id_hint;
  html += R"html(</div>
              </div>
              <div>
                <label for="mqtt_base">)html";
  html += tr.mqtt_base_topic;
  html += R"html(:</label>
                <input type="text" id="mqtt_base" name="mqtt_base" value=")html";
  appendHtmlEscaped(html, cfg.mqtt_base_topic);
  html += R"html(">
              </div>
              <div>
                <label for="ha_prefix">)html";
  html += tr.ha_prefix;
  html += R"html(:</label>
                <input type="text" id="ha_prefix" name="ha_prefix" value=")html";
  appendHtmlEscaped(html, cfg.ha_prefix);
  html += R"html(">
              </div>
            </div>
          </div>

          <div class="settings-section">
            <div class="section-title">)html";
  html += tr.admin_settings_language;
  html += R"html(</div>
            <div class="settings-grid">
              <div>
                <label for="language">)html";
  html += tr.language_label;
  html += R"html(</label>
                <select id="language" name="language">)html";
  html += i18n::build_language_options_html(cfg.language);
  html += R"html(</select>
              </div>
              <div>
                <label for="timezone">)html";
  html += tr.timezone_label;
  html += R"html(</label>
                <select id="timezone" name="timezone">)html";
  html += buildTimezoneOptionsHtml(cfg.timezone, i18n::locale(cfg.language));
  html += R"html(</select>
              </div>
              <div>
                <label for="locale_time_format">)html";
  html += tr.time_format_label;
  html += R"html(</label>
                <select id="locale_time_format" name="locale_time_format">)html";
  html += buildGlobalTimeFormatOptionsHtml(cfg.global_time_format, tr);
  html += R"html(</select>
              </div>
              <div>
                <label for="locale_date_format">)html";
  html += tr.date_format_label;
  html += R"html(</label>
                <select id="locale_date_format" name="locale_date_format">)html";
  html += buildGlobalDateFormatOptionsHtml(cfg.global_date_format, tr);
  html += R"html(</select>
              </div>
            </div>
          </div>

          <div class="settings-section">
            <div class="section-title">)html";
  html += tr.admin_settings_screenshot;
  html += R"html(</div>
            <div class="settings-grid">
              <div class="settings-full">
                <div class="settings-actions">
                  <button class="btn" type="button" onclick="createScreenshotAndDownload()">)html";
  html += tr.screenshot_create_download;
  html += R"html(</button>
                  <button class="btn btn-secondary" type="button" onclick="downloadCrashLog()">)html";
  html += tr.crash_log_download;
  html += R"html(</button>
)html";
  html += R"html(                  <button class="btn btn-secondary" type="button" onclick="window.open('/api/sd-diagnostics?ts=' + Date.now(), '_blank')">)html";
  html += tr.sd_diagnostics_open;
  html += R"html(</button>
)html";
  html += R"html(
                </div>
                <div class="settings-note">)html";
  html += tr.screenshot_saved_note;
  html += R"html(</div>)html";

  // Offer the core dump only when its partition contains one. The panic
  // handler writes it automatically; see src/core/diagnostics/crash_log.h.
  // The separate crashlog.txt download above reads LittleFS, whereas the file
  // manager exposes only the microSD card.
  if (CrashLog::hasCoreDump()) {
    const String summary = CrashLog::coreDumpSummaryLine();
    html += R"html(
                <div class="settings-note"><strong>)html";
    html += tr.coredump_stored;
    html += R"html(:</strong></div>)html";
    if (summary.length()) {
      html += R"html(
                <div class="settings-note ota-version-value">)html";
      html += summary;
      html += R"html(</div>)html";
    }
    html += R"html(
                <div class="settings-actions" id="coredump_actions">
                  <button class="btn btn-secondary" type="button" onclick="window.location.href='/api/coredump'">)html";
    html += tr.coredump_download;
    html += R"html(</button>
                  <button class="btn btn-secondary" type="button" onclick="eraseCoreDump()">)html";
    html += tr.coredump_delete;
    html += R"html(</button>
                </div>
                <div class="settings-note">)html";
    html += tr.coredump_decode_note;
    html += R"html(</div>)html";
  }

  html += R"html(
              </div>
            </div>
          </div>

          <div class="settings-section">
            <div class="section-title">)html";
  html += tr.file_manager_title;
  html += R"html(</div>
            <div class="settings-grid file-manager">
              <div class="settings-full">
                <div class="file-manager-topbar">
                  <span id="file_manager_sd_state" class="file-manager-storage-state">)html";
  html += tr.file_manager_checking;
  html += R"html(</span>
                  <div class="file-manager-toolbar-group">
                    <button class="btn btn-secondary file-manager-toolbar-btn" type="button" onclick="loadFileManager()">)html";
  html += tr.file_manager_refresh;
  html += R"html(</button>
                    <button class="btn btn-secondary file-manager-toolbar-btn file-manager-requires-sd" type="button" onclick="createFileManagerFolder()" disabled>)html";
  html += tr.file_manager_new_folder;
  html += R"html(</button>
                  </div>
                </div>
                <div class="file-manager-upload-row">
                  <button class="btn btn-secondary file-manager-toolbar-btn file-manager-requires-sd" type="button" onclick="document.getElementById('file_manager_upload').click()" disabled>)html";
  html += tr.file_manager_choose_files;
  html += R"html(</button>
                  <button class="btn btn-secondary file-manager-toolbar-btn file-manager-requires-sd" type="button" onclick="uploadFileManagerFile()" disabled>)html";
  html += tr.file_manager_upload;
  html += R"html(</button>
                  <span id="file_manager_upload_name" class="file-picker-name">)html";
  html += tr.ota_no_file_selected;
  html += R"html(</span>
                </div>
                <input type="file" id="file_manager_upload" multiple style="display:none" onchange="updateFileManagerUploadName(this)">
                <div class="file-manager-selection-bar">
                  <div id="file_manager_selection" class="file-manager-selection-info">)html";
  html += tr.file_manager_no_selection;
  html += R"html(</div>
                  <div class="file-manager-selection-actions">
                    <button class="btn btn-secondary file-manager-selection-btn" id="file_manager_primary_btn" type="button" onclick="openSelectedFileManagerEntry()" disabled>)html";
  html += tr.file_manager_open;
  html += R"html(</button>
                    <button class="btn btn-secondary file-manager-selection-btn" id="file_manager_rename_btn" type="button" onclick="renameSelectedFileManagerEntry()" disabled>)html";
  html += tr.file_manager_rename;
  html += R"html(</button>
                    <button class="btn btn-danger file-manager-selection-btn" id="file_manager_delete_btn" type="button" onclick="deleteSelectedFileManagerEntry()" disabled>)html";
  html += tr.admin_delete;
  html += R"html(</button>
                  </div>
                </div>
              </div>
              <div class="settings-full">
                <div id="file_manager_breadcrumb" class="file-manager-breadcrumb"></div>
                <div class="file-manager-table-wrap">
                  <table class="file-manager-table">
                    <thead>
                      <tr>
                        <th>)html";
  html += tr.file_manager_name;
  html += R"html(</th>
                        <th>)html";
  html += tr.file_manager_modified;
  html += R"html(</th>
                        <th>)html";
  html += tr.file_manager_size;
  html += R"html(</th>
                      </tr>
                    </thead>
                    <tbody id="file_manager_entries">
                      <tr><td colspan="3">)html";
  html += tr.file_manager_not_loaded;
  html += R"html(</td></tr>
                    </tbody>
                  </table>
                </div>
                <div id="file_manager_status" class="settings-note file-manager-status"></div>
              </div>
            </div>
          </div>

          <div class="settings-section">
            <div class="section-title">)html";
  html += tr.admin_import_export;
  html += R"html(</div>
            <div class="settings-grid">
              <div class="settings-full">
                <div class="settings-actions">
                  <button type="button" class="btn" onclick="exportTilesConfig()">)html";
  html += tr.admin_export;
  html += R"html(</button>
                  <input type="file" id="settings_tile_import" accept="application/json" style="display:none" onchange="importTilesConfig('settings', this.files)">
                  <button type="button" class="btn" onclick="triggerTilesImport('settings')">)html";
  html += tr.admin_import;
  html += R"html(</button>
                </div>
                <div class="settings-note">)html";
  html += tr.admin_import_overwrite;
  html += R"html(</div>
              </div>
            </div>
          </div>

          <div class="settings-section">
            <div class="section-title">)html";
  html += tr.admin_settings_ota;
  html += R"html(</div>
            <div class="settings-grid">
              <div class="settings-full">
                <div class="settings-note"><strong>)html";
  html += tr.ota_current_version;
  html += R"html(:</strong></div>
                <div class="settings-note ota-version-value">)html";
  html += current_firmware_name;
  html += R"html(</div>
                <div class="settings-note"><strong>GitHub OTA:</strong></div>
                <div class="settings-actions ota-github-actions">
                  <button class="btn btn-go" type="button" id="ota_github_btn" onclick="checkOrInstallGithubFirmware()">)html";
  html += tr.system_check_updates_btn;
  html += R"html(</button>
                </div>
                <div id="ota_github_status" class="settings-note ota-status"></div>
                <div id="ota_github_progress" class="ota-progress is-hidden" aria-hidden="true">
                  <div id="ota_github_progress_bar" class="ota-progress-bar"></div>
                </div>
                <div class="settings-note"><strong>)html";
  html += tr.ota_firmware_file;
  html += R"html(:</strong></div>
                <input type="file" id="ota_file" accept=".bin,application/octet-stream" style="display:none" onchange="updateOtaFileName(this)">
                <div class="file-picker">
                  <button class="btn btn-secondary btn-inline" type="button" id="ota_choose_btn" onclick="document.getElementById('ota_file').click()">)html";
  html += tr.ota_choose_file;
  html += R"html(</button>
                  <button class="btn btn-go btn-inline" type="button" id="ota_upload_btn" onclick="uploadOtaFirmware()">)html";
  html += tr.ota_upload_install;
  html += R"html(</button>
                  <span id="ota_file_name" class="file-picker-name">)html";
  html += tr.ota_no_file_selected;
  html += R"html(</span>
                </div>
                <div id="ota_status" class="settings-note ota-status"></div>
                <div id="ota_progress" class="ota-progress is-hidden" aria-hidden="true">
                  <div id="ota_progress_bar" class="ota-progress-bar"></div>
                </div>
                <div class="settings-note">)html";
  html += tr.ota_update_note;
  html += R"html(</div>
              </div>
            </div>
          </div>
        </form>

        <form id="admin_restart_form" action="/restart" method="POST" onsubmit="return confirm('Gerät wirklich neu starten?');" class="admin-hidden-form"></form>
        <div class="admin-footer-actions">
          <button class="btn btn-go admin-footer-btn" type="submit" form="admin_settings_form">Speichern</button>
          <button class="btn btn-secondary admin-footer-btn" type="submit" form="admin_restart_form">Gerät neu starten</button>
        </div>
      </div>
    </div>
  </div>

  <!-- Notification Toast -->
  <div id="notification" class="notification" role="status" aria-live="polite"></div>
</body>
</html>
)html";

  html.replace(">Speichern</button>", String(">") + tr.save + "</button>");
  html.replace("return confirm('Gerät wirklich neu starten?');", String("return confirm('") + tr.restart_confirm + "');");
  html.replace(">Gerät neu starten</button>", String(">") + tr.restart_button + "</button>");

  return html;
}

String WebAdminServer::getSuccessPage() {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  String html = "<!DOCTYPE html>\n<html lang=\"";
  html += tr.html_lang;
  html += R"html(">
<head>
  <meta charset="utf-8">
  <title>)html";
  html += tr.save;
  html += R"html(</title>
)html";
  appendWebFontFaceStyles(html);
  html += R"html(
  <style>
    body { font-family:'HomeTiles Inter', sans-serif; background:#0a0a0a; height:100vh; margin:0; display:flex; align-items:center; justify-content:center; }
    .box { background:#1c1c1c; border:1px solid #2a2a2a; padding:32px; border-radius:22px; box-shadow:0 20px 60px rgba(0,0,0,.5); text-align:center; }
    h1 { margin:0 0 10px; color:#ffffff; font-size:22px; }
    p { margin:0; color:#8a8a8a; }
  </style>
  <script>setTimeout(function(){window.location.href='/'},1500);</script>
</head>
<body>
  <div class="box">
    <h1>)html";
  html += tr.mqtt_saved_title;
  html += R"html(</h1>
    <p>)html";
  html += tr.mqtt_saved_message;
  html += R"html(</p>
  </div>
</body>
</html>)html";
  return html;
}

String WebAdminServer::getBridgeSuccessPage() {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  String html = "<!DOCTYPE html>\n<html lang=\"";
  html += tr.html_lang;
  html += R"html(">
<head>
  <meta charset="utf-8">
  <title>)html";
  html += tr.bridge_saved_title;
  html += R"html(</title>
)html";
  appendWebFontFaceStyles(html);
  html += R"html(
  <style>
    body { font-family:'HomeTiles Inter', sans-serif; background:#0a0a0a; height:100vh; margin:0; display:flex; align-items:center; justify-content:center; }
    .box { background:#1c1c1c; border:1px solid #2a2a2a; padding:32px; border-radius:22px; box-shadow:0 20px 60px rgba(0,0,0,.5); text-align:center; }
    h1 { margin:0 0 10px; color:#ffffff; font-size:22px; }
    p { margin:0; color:#8a8a8a; }
  </style>
  <script>setTimeout(function(){window.location.href='/'},1500);</script>
</head>
<body>
  <div class="box">
    <h1>)html";
  html += tr.bridge_saved_title;
  html += R"html(</h1>
    <p>)html";
  html += tr.bridge_saved_message;
  html += R"html(</p>
  </div>
</body>
</html>)html";
  return html;
}

String WebAdminServer::getStatusJSON() {
  const DeviceConfig& cfg = configManager.getConfig();
  const UsbEthernetSnapshot usb_ethernet = usbEthernetBackend.snapshot();
  nvs_stats_t stats{};
  bool stats_ok = (nvs_get_stats(nullptr, &stats) == ESP_OK);

  auto get_ns_used = [](const char* ns) -> size_t {
    if (!ns) return static_cast<size_t>(-1);
    nvs_handle_t h = 0;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return static_cast<size_t>(-1);
    size_t used = 0;
    esp_err_t err = nvs_get_used_entry_count(h, &used);
    nvs_close(h);
    return (err == ESP_OK) ? used : static_cast<size_t>(-1);
  };

  size_t tiles_used = get_ns_used("tab5_tiles");
  size_t config_used = get_ns_used("tab5_config");

  String json = "{";
  json += "\"wifi_connected\":";
  json += networkTransport.isConnected() ? "true" : "false";
  json += ",\"wifi_ssid\":\"";
  json += networkTransport.activeKind() == NetworkTransportKind::Wifi
              ? String(cfg.wifi_ssid)
              : String(networkTransport.activeName());
  json += "\"";
  json += ",\"wifi_ip\":\"" + networkTransport.localIP().toString() + "\"";
  json += ",\"network_transport\":\"" +
          String(networkTransport.activeName()) + "\"";
  json += ",\"usb_host_ready\":" +
          String(usb_ethernet.host_ready ? "true" : "false");
  json += ",\"usb_device_count\":" +
          String(usb_ethernet.enumerated_devices);
  json += ",\"usb_ethernet_attached\":" +
          String(usb_ethernet.adapter_attached ? "true" : "false");
  json += ",\"usb_ethernet_link\":" +
          String(usb_ethernet.link_up ? "true" : "false");
  json += ",\"usb_ethernet_has_ip\":" +
          String(usb_ethernet.has_ip ? "true" : "false");
  json += ",\"mqtt_host\":\"" + String(cfg.mqtt_host) + "\"";
  json += ",\"mqtt_port\":" + String(cfg.mqtt_port);
  json += ",\"mqtt_client_id\":\"" + String(cfg.mqtt_client_id) + "\"";
  json += ",\"mqtt_base\":\"" + String(cfg.mqtt_base_topic) + "\"";
  json += ",\"ha_prefix\":\"" + String(cfg.ha_prefix) + "\"";
  json += ",\"bridge_configured\":" + String(haBridgeConfig.hasData() ? "true" : "false");
  json += ",\"free_heap\":" + String(ESP.getFreeHeap());
  json += ",\"heap_total\":" + String(ESP.getHeapSize());
  json += ",\"heap_min_free\":" + String(ESP.getMinFreeHeap());
  json += ",\"psram_free\":" + String(ESP.getFreePsram());
  json += ",\"psram_total\":" + String(ESP.getPsramSize());
  json += ",\"nvs_used_entries\":" + String(stats_ok ? stats.used_entries : -1);
  json += ",\"nvs_free_entries\":" + String(stats_ok ? stats.free_entries : -1);
  json += ",\"nvs_namespace_count\":" + String(stats_ok ? stats.namespace_count : -1);
  json += ",\"nvs_tab5_tiles_used\":" + String(tiles_used);
  json += ",\"nvs_tab5_config_used\":" + String(config_used);
  json += "}";
  return json;
}

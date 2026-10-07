#include "src/types/value/value_control.h"
#include "src/web/server/web_admin.h"
#include "src/web/server/web_admin_utils.h"
#include "src/web/server/render/tile_icon_colors_html.h"
#include <WiFi.h>
#include <math.h>
#include <stdlib.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "src/core/config/config_manager.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/network_manager.h"
#include "src/network/secure/command_channel.h"
#include "src/network/transport/network_transport.h"
#include "src/network/transport/usb_ethernet_backend.h"
#include "src/web/server/render/web_admin_scripts.h"
#include "src/web/server/render/web_admin_html.h"
#include "src/web/server/auth/web_admin_auth.h"
#include "src/web/server/render/web_admin_styles.h"
#include "src/web/server/assets/web_admin_fonts.h"
#include "src/web/server/handlers/web_admin_tile_helpers.h"
#include "src/tiles/config/tile_config.h"
#include "src/types/types_registry.h"
#include "src/core/diagnostics/crash_log.h"
#include "src/core/firmware/firmware_version.h"
#include "src/core/i18n/i18n.h"
#include "src/devices/device.h"
#include "src/types/clock/clock_format.h"
#include "src/types/binary_sensor/renderer.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/ui/tabs/settings/settings_model.h"
#include "src/video/local_camera/local_camera.h"
#include "src/video/local_camera/local_camera_stream_contract.h"
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

// One image-control slider: translated label, range, current value. The
// number is rendered untranslated; data-unit adds "%" for percent controls.
static void appendLocalCameraImageSlider(String& html, const char* key,
                                         const char* label, int min_value,
                                         int max_value, int value,
                                         const char* unit) {
  html += R"html(
<label class="local-camera-slider" for="local_camera_)html";
  html += key;
  html += R"html("><span>)html";
  appendHtmlEscaped(html, label);
  html += R"html(</span><input type="range" id="local_camera_)html";
  html += key;
  html += R"html(" data-image-key=")html";
  html += key;
  html += R"html(" data-unit=")html";
  html += unit;
  html += R"html(" min=")html";
  html += String(min_value);
  html += R"html(" max=")html";
  html += String(max_value);
  html += R"html(" step="1" value=")html";
  html += String(value);
  html += R"html(" data-saved=")html";
  html += String(value);
  html += R"html(" oninput="localCameraImageInput(this)" onchange="localCameraImageChange(this)"><output id="local_camera_)html";
  html += key;
  html += R"html(_value">)html";
  html += String(value);
  html += unit;
  html += R"html(</output></label>)html";
}

// One Custom stream mode slider (frames per second or JPEG quality), laid
// out like the image sliders; saved through /api/local-camera on release.
static void appendLocalCameraCustomSlider(String& html, const char* key,
                                          const char* label, int min_value,
                                          int max_value, int value) {
  html += R"html(
<label class="local-camera-slider" for="local_camera_custom_)html";
  html += key;
  html += R"html("><span>)html";
  appendHtmlEscaped(html, label);
  html += R"html(</span><input type="range" id="local_camera_custom_)html";
  html += key;
  html += R"html(" data-custom-key=")html";
  html += key;
  html += R"html(" min=")html";
  html += String(min_value);
  html += R"html(" max=")html";
  html += String(max_value);
  html += R"html(" step="1" value=")html";
  html += String(value);
  html += R"html(" data-saved=")html";
  html += String(value);
  html += R"html(" oninput="localCameraCustomInput(this)" onchange="localCameraCustomChange(this)"><output id="local_camera_custom_)html";
  html += key;
  html += R"html(_value">)html";
  html += String(value);
  html += R"html(</output></label>)html";
}

static const char* localCameraStateText(const char* state,
                                        const i18n::Strings& tr) {
  if (!state) return tr.local_camera_status_error;
  if (strcmp(state, "disabled") == 0) return tr.local_camera_status_disabled;
  if (strcmp(state, "probing") == 0) return tr.local_camera_status_probing;
  if (strcmp(state, "ready") == 0) return tr.local_camera_status_ready;
  if (strcmp(state, "not_found") == 0) return tr.local_camera_status_not_found;
  return tr.local_camera_status_error;
}

// Built-in camera opt-in. Rendered only on the exact camera profile; the
// toggle and the live-stream mode save immediately through /api/local-camera
// and are not part of the /mqtt settings form (no name attributes). All state
// texts come from the central translations and travel as data attributes for
// the status line; stream mode names are untranslated technical values.
static void appendLocalCameraSettingsHtml(String& html, const i18n::Strings& tr) {
  if (!local_camera::supported() || !Device::kCapabilities.has_builtin_camera) {
    return;
  }
  const char* state = local_camera::stateName();
  html += R"html(
<div class="settings-section" id="local_camera_section">
<div class="section-title">)html";
  appendHtmlEscaped(html, tr.local_camera_section);
  html += R"html(</div>
<div class="settings-grid">
<div class="settings-full">
<label class="settings-checkbox">
<input type="checkbox" id="local_camera_enabled" onchange="saveLocalCameraEnabled(this.checked)")html";
  if (local_camera::enabled()) html += " checked";
  html += R"html(>
<span>)html";
  appendHtmlEscaped(html, tr.local_camera_enable);
  html += R"html(</span>
</label>
<div class="settings-note">)html";
  appendHtmlEscaped(html, tr.local_camera_note);
  html += R"html(</div>
<div id="local_camera_status" class="settings-note" data-state=")html";
  html += state;
  html += R"html(" data-label=")html";
  appendHtmlEscaped(html, tr.local_camera_status_label);
  html += R"html(" data-state-disabled=")html";
  appendHtmlEscaped(html, tr.local_camera_status_disabled);
  html += R"html(" data-state-probing=")html";
  appendHtmlEscaped(html, tr.local_camera_status_probing);
  html += R"html(" data-state-ready=")html";
  appendHtmlEscaped(html, tr.local_camera_status_ready);
  html += R"html(" data-state-not-found=")html";
  appendHtmlEscaped(html, tr.local_camera_status_not_found);
  html += R"html(" data-state-error=")html";
  appendHtmlEscaped(html, tr.local_camera_status_error);
  html += R"html(">)html";
  appendHtmlEscaped(html, tr.local_camera_status_label);
  html += ": ";
  appendHtmlEscaped(html, localCameraStateText(state, tr));
  html += R"html(</div>
</div>
<div class="local-camera-group" id="local_camera_stream">
<div class="network-settings-heading">)html";
  // Two columns: the live stream (mode, Custom sliders) on the left, the
  // experimental indicator on the right, the collapsed Advanced block below
  // over the full width. Narrow screens stack them (.settings-grid).
  appendHtmlEscaped(html, tr.local_camera_stream_section);
  html += R"html(</div>
<div>
<label for="local_camera_stream_mode">)html";
  appendHtmlEscaped(html, tr.local_camera_stream_mode);
  html += R"html(:</label>
<select id="local_camera_stream_mode" onchange="saveLocalCameraStreamMode(this.value)">)html";
  const uint8_t selected_mode = local_camera::streamMode();
  html += R"html(
<option value="0")html";
  if (selected_mode == local_camera_stream::kModeAuto) html += " selected";
  html += ">";
  appendHtmlEscaped(html, tr.local_camera_stream_mode_auto);
  html += "</option>";
  for (const local_camera_stream::ModeEntry& mode : local_camera_stream::kModes) {
    char label[48];
    if (!local_camera_stream::formatModeLabel(label, sizeof(label), mode,
                                              local_camera::imageWidth(),
                                              local_camera::imageHeight())) {
      continue;
    }
    html += R"html(
<option value=")html";
    html += String(static_cast<unsigned>(mode.id));
    html += "\"";
    if (mode.id == selected_mode) html += " selected";
    html += ">";
    html += label;
    html += "</option>";
  }
  html += R"html(
<option value=")html";
  html += String(static_cast<unsigned>(local_camera_stream::kModeCustom));
  html += "\"";
  if (selected_mode == local_camera_stream::kModeCustom) html += " selected";
  html += ">";
  appendHtmlEscaped(html, tr.local_camera_stream_mode_custom);
  html += R"html(</option>
</select>
</div>
<div class="local-camera-custom" id="local_camera_custom" data-mode=")html";
  // Custom mode: frames per second and JPEG quality, shown only while the
  // Custom mode is selected; the resolution stays the full image.
  html += String(static_cast<unsigned>(local_camera_stream::kModeCustom));
  html += "\"";
  if (selected_mode != local_camera_stream::kModeCustom) html += " hidden";
  html += ">";
  const local_camera_stream::CustomMode custom = local_camera::customMode();
  appendLocalCameraCustomSlider(html, "fps", tr.local_camera_custom_fps,
                                local_camera_stream::kCustomMinFps,
                                local_camera_stream::kCustomMaxFps, custom.fps);
  appendLocalCameraCustomSlider(html, "quality", tr.local_camera_custom_quality,
                                local_camera_stream::kCustomMinQuality,
                                local_camera_stream::kMaxQuality, custom.quality);
  html += R"html(
</div>
</div>
<div class="local-camera-group local-camera-indicator" id="local_camera_indicator">
<div class="network-settings-heading">)html";
  // Indicator style (experimental): the line checkbox switches the whole
  // indicator, the pill checkbox only applies while the line is shown.
  appendHtmlEscaped(html, tr.local_camera_indicator_section);
  html += R"html(</div>
<label class="settings-checkbox">
<input type="checkbox" id="local_camera_indicator_line" onchange="saveLocalCameraIndicator()")html";
  const local_camera::IndicatorStyle indicator = local_camera::indicatorStyle();
  if (indicator != local_camera::IndicatorStyle::None) html += " checked";
  html += R"html(>
<span>)html";
  appendHtmlEscaped(html, tr.local_camera_indicator_line);
  html += R"html(</span>
</label>
<label class="settings-checkbox">
<input type="checkbox" id="local_camera_indicator_pill" onchange="saveLocalCameraIndicator()")html";
  if (indicator != local_camera::IndicatorStyle::Line) html += " checked";
  if (indicator == local_camera::IndicatorStyle::None) html += " disabled";
  html += R"html(>
<span>)html";
  appendHtmlEscaped(html, tr.local_camera_indicator_pill);
  html += R"html(</span>
</label>
<div class="settings-note">)html";
  appendHtmlEscaped(html, tr.local_camera_indicator_note);
  html += R"html(</div>
</div>
<details class="settings-full local-camera-advanced" id="local_camera_advanced">
<summary class="network-settings-heading">)html";
  // Fine-tuning controls, collapsed by default: rotation and mirror (left),
  // red/blue swap (right), the image controls below. Every control keeps
  // saving immediately through /api/local-camera.
  appendHtmlEscaped(html, tr.local_camera_advanced);
  html += R"html(</summary>
<div class="settings-grid">
<div class="local-camera-group" id="local_camera_orientation">
<div>
<label for="local_camera_rotation">)html";
  appendHtmlEscaped(html, tr.local_camera_rotation);
  html += R"html(:</label>
<select id="local_camera_rotation" onchange="saveLocalCameraRotation(this.value)">)html";
  const uint8_t rotation = local_camera::rotation();
  for (uint8_t turns = 0; turns <= local_camera_contract::kRotationMax; ++turns) {
    html += R"html(
<option value=")html";
    html += String(static_cast<unsigned>(turns));
    html += "\"";
    if (turns == rotation) html += " selected";
    html += ">";
    // Clockwise degrees: untranslated numbers.
    html += String(static_cast<unsigned>(turns) * 90u);
    html += "\xC2\xB0</option>";
  }
  html += R"html(
</select>
</div>
<label class="settings-checkbox">
<input type="checkbox" id="local_camera_mirror" onchange="saveLocalCameraMirror(this.checked)")html";
  if (local_camera::mirror()) html += " checked";
  html += R"html(>
<span>)html";
  appendHtmlEscaped(html, tr.local_camera_mirror);
  html += R"html(</span>
</label>
</div>
<div class="local-camera-group" id="local_camera_color">
<label class="settings-checkbox">
<input type="checkbox" id="local_camera_rb_swap" onchange="saveLocalCameraRbSwap(this.checked)")html";
  if (local_camera::redBlueSwap()) html += " checked";
  html += R"html(>
<span>)html";
  appendHtmlEscaped(html, tr.local_camera_rb_swap);
  html += R"html(</span>
</label>
<div class="settings-note">)html";
  appendHtmlEscaped(html, tr.local_camera_rb_swap_note);
  html += R"html(</div>
</div>
<div class="settings-full local-camera-image" id="local_camera_image">
<div class="network-settings-heading">)html";
  appendHtmlEscaped(html, tr.local_camera_image_section);
  html += R"html(</div>)html";
  // Live controls: saved through /api/local-camera while dragging (debounced)
  // and on release; the camera applies them with the next frame.
  const local_camera_contract::ImageSettings image = local_camera::imageSettings();
  using local_camera_contract::kImageAdjustMax;
  using local_camera_contract::kImageAdjustMin;
  appendLocalCameraImageSlider(html, "brightness", tr.local_camera_brightness,
                               kImageAdjustMin, kImageAdjustMax, image.brightness, "");
  appendLocalCameraImageSlider(html, "contrast", tr.local_camera_contrast,
                               kImageAdjustMin, kImageAdjustMax, image.contrast, "");
  appendLocalCameraImageSlider(html, "saturation", tr.local_camera_saturation,
                               local_camera_contract::kSaturationMin,
                               local_camera_contract::kSaturationMax, image.saturation, "%");
  appendLocalCameraImageSlider(html, "red", tr.local_camera_red,
                               kImageAdjustMin, kImageAdjustMax, image.red, "%");
  appendLocalCameraImageSlider(html, "blue", tr.local_camera_blue,
                               kImageAdjustMin, kImageAdjustMax, image.blue, "%");
  appendLocalCameraImageSlider(html, "gain", tr.local_camera_gain,
                               local_camera_contract::kGainLimitMin,
                               local_camera_contract::kGainLimitMax, image.gain, "%");
  html += R"html(
<div class="settings-actions local-camera-image-actions">
<button type="button" class="btn btn-secondary" id="local_camera_image_reset" onclick="resetLocalCameraImage()">)html";
  appendHtmlEscaped(html, tr.local_camera_image_reset);
  html += R"html(</button>
</div>
</div>
</div>
</details>
</div>
</div>
)html";
}

// Helper function to generate tile tab HTML (unified for all folders)
static void appendTileTabHTML(
    String& html,
    uint16_t folder_id,
    const FolderEntry& folder,
    const TileGridConfig& grid,
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
    float col = (tile.col < GRID_COLS) ? tile.col : 0;
    float row = (tile.row < GRID_ROWS) ? tile.row : 0;
    float span_w = (tile.span_w < 0.5f) ? 1 : tile.span_w;
    float span_h = (tile.span_h < 0.5f) ? 1 : tile.span_h;
    clamp_media_tile_layout(tile.type, col, row, span_w, span_h);
    if (screensaver_mode && GRID_ROWS > 1 && row < GRID_ROWS - 2) {
      row = GRID_ROWS - 2;
    }
    if (span_w > GRID_COLS - col) span_w = GRID_COLS - col;
    if (span_h > GRID_ROWS - row) span_h = GRID_ROWS - row;

    if (!tileBorderEnabled(tile)) cssClass += " tile-border-hidden";
    if (type_css && type_css[0]) {
      cssClass += " ";
      cssClass += type_css;
    } else if (tile.type == TILE_EMPTY) {
      cssClass += " empty";
    }

    if (tile.type != TILE_EMPTY &&
        tileBgColorFollowsDefault(tile.bg_color) && tile_type_follows_default_tile_color(tile.type)) {
      // Tiles without their own color follow the global default tile color
      // through one CSS variable, so the preview repaints them live.
      if (screensaver_mode) {
        tileStyle = "background:color-mix(in srgb,var(--tile-default-bg) ";
        tileStyle += String(screensaverConfig.get().tile_opacity * 100.0f / 255.0f, 2);
        tileStyle += "%,transparent)";
      } else {
        tileStyle = "background:var(--tile-default-bg)";
      }
    } else if (tile.type != TILE_EMPTY) {
      uint32_t bg_color = tileBgColorIsSet(tile)
                              ? tileBgColorRgb(tile)
                              : (type_desc ? type_desc->default_bg_color : 0);
      if (bg_color == 0) bg_color = 0x353535;
      char colorHex[10];
      if (screensaver_mode) {
        // One opacity for every screensaver tile (ScreensaverConfigData).
        snprintf(colorHex, sizeof(colorHex), "#%06X%02X",
                 (unsigned int)bg_color,
                 static_cast<unsigned int>(screensaverConfig.get().tile_opacity));
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
    // A fully transparent screensaver card casts no shadow
    // (apply_slot_tile_shadows).
    if (screensaver_mode && tile.type != TILE_EMPTY && screensaverConfig.get().tile_opacity == 0) {
      cssClass += " screensaver-bg-clear";
    }

    if (tile_geometry::fraction_bits(col, row, span_w, span_h)) {
      cssClass += " fractional-tile";
      tileStyle += ";--tile-col:" + String(col) + ";--tile-row:" + String(row) +
                   ";--tile-w:" + String(span_w) + ";--tile-h:" + String(span_h) +
                   ";grid-column:auto;grid-row:auto";
    }
    if (tile_geometry::compact(tile.type, span_w, span_h) &&
        (tile.type == TILE_BINARY_SENSOR || span_h == 0.5f || tile.sensor_display_mode == 0)) {
      cssClass += " sensor-compact";
      if (span_h == 0.5f) cssClass += " sensor-half";
    }
    if (tile_geometry::compact_clock(tile.type, span_w, span_h)) {
      cssClass += " clock-compact";
    } else if (tile.type == TILE_CLOCK &&
               (tile.title.length() || normalizeMdiIconName(tile.icon_name).length())) {
      // A title or icon moves the clock down (clock/renderer.cpp).
      cssClass += " clock-has-header";
    }
    if (tile_geometry::compact_icon_title(tile.type, span_w, span_h)) {
      cssClass += " sensor-compact sensor-half compact-title-only";
    }
    if (tile_geometry::compact_switch(tile.type, span_w, span_h) ||
        tile_geometry::compact_cover(tile.type, span_w, span_h) ||
        tile_geometry::compact_device_control(tile.type, span_w, span_h) ||
        tile_geometry::compact_climate(tile.type, span_w, span_h) ||
        tile_geometry::compact_editable(tile.type, span_w, span_h)) {
      cssClass += " sensor-compact sensor-half";
    }
    html += "<div class=\"";
    html += cssClass;
    html += "\" data-index=\"";
    html += String(i);
    html += "\" data-col=\"";
    html += String(col);
    html += "\" data-row=\"";
    html += String(row);
    html += "\" data-span-w=\"";
    html += String(span_w);
    html += "\" data-span-h=\"";
    html += String(span_h);
    html += "\" data-type=\"";
    html += String(static_cast<unsigned>(tile.type));
    html += "\" data-icon-disc=\"";
    html += String(tile.icon_disc_mode);
    html += "\" data-icon-glow=\"";
    html += tile.icon_glow ? "1" : "0";
    // A PIN-protected Folder or Settings tile shows a lock in its icon
    // (navigate renderer, previewTileLocked in the browser).
    const bool tile_locked =
        (tile.type == TILE_SETTINGS && configManager.getConfig().settings_pin_enabled) ||
        (tile.type == TILE_FOLDER && tileConfig.isFolderPinEnabled(getNavigateTargetId(tile)));
    if (tile.type == TILE_FOLDER) {
      html += "\" data-navigate-target=\"";
      html += String(getNavigateTargetId(tile));
      html += "\" data-folder-pin-enabled=\"";
      html += tile_locked ? "1" : "0";
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
      // Scene tiles without an icon use their scene entity's icon, as on the device.
      if (tile.type == TILE_SCENE && !iconName.length() &&
          !isMdiIconDisabled(tile.icon_name) && tile.scene_alias.length()) {
        const String scene_entity = haBridgeConfig.findSceneEntity(tile.scene_alias);
        if (scene_entity.length()) {
          iconName = normalizeMdiIconName(haBridgeConfig.findEntityIcon(scene_entity));
        }
      }

      // Without an own icon a protected tile shows the lock as its icon.
      const bool lock_is_icon = tile_locked && !iconName.length();
      if (lock_is_icon) iconName = "lock";
      bool hasIcon = iconName.length() > 0;

      if (hasIcon) {
        html += "<i class=\"mdi mdi-";
        appendHtmlEscaped(html, iconName);
        html += " tile-icon";
        if (binary_sensor_preview && tile.icon_glow &&
            tile_icon_disc::icon_color_tints(binary_sensor_visual_color(binary_sensor_state))) {
          html += " tile-icon-tinted";
        }
        html += "\"";
        if (binary_sensor_preview) {
          char color_hex[8];
          snprintf(color_hex, sizeof(color_hex), "#%06X",
                   static_cast<unsigned>(
                       binary_sensor_visual_color(binary_sensor_state)));
          html += " style=\"color:";
          html += color_hex;
          html += "\"";
        }
        html += ">";
        if (tile_locked && !lock_is_icon) {
          html += "<span class=\"tile-icon-lock mdi mdi-lock\" aria-hidden=\"true\"></span>";
        }
        html += "</i>";
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

    // Weather and media tiles get their state from the preview script
    // (applyWeatherPreview, applyMediaPreview) once the values arrive.
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
      html += "<div class=\"tile-value tile-binary-sensor-value";
      if (tile.sensor_value_font >= 1 && tile.sensor_value_font <= 4) {
        html += " sensor-value-size-";
        html += tile.sensor_value_font == 1 ? "20" : tile.sensor_value_font == 2 ? "24" : tile.sensor_value_font == 3 ? "32" : "40";
      }
      html += "\" id=\"";
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
      // Each line in its stored size with the LVGL line height
      // (getClockPreviewTextStyle in the browser).
      auto append_line = [&html](const char* css_class, uint8_t size, const char* placeholder) {
        char attributes[200];
        snprintf(attributes, sizeof(attributes),
                 "<div class=\"%s\" data-clock-font=\"%u\" style=\"font-size:var(--fs%u);"
                 "line-height:var(--lh%u);top:var(--ldy%u, 0px)\">",
                 css_class, static_cast<unsigned>(size), static_cast<unsigned>(size),
                 static_cast<unsigned>(size), static_cast<unsigned>(size));
        html += attributes;
        html += placeholder;
        html += "</div>";
      };
      if (flags & 1) {
        append_line("tile-clock-time", clock_tile::normalize_font_size(tile.key_code, 40), "--:--");
      }
      if (flags & 2) {
        append_line("tile-clock-date", clock_tile::normalize_date_font_size(tile.key_modifier, 20),
                    "--.--.----");
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
        snapshot.valid && !tileBgColorFollowsDefault(snapshot.bg_color)
            ? (snapshot.bg_color & TILE_BG_COLOR_RGB_MASK)
            : tileDefaultBgColor();
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
      // A parked Settings tile shows the lock of the Settings PIN too
      // (previewTileLocked in the browser); without an icon the lock is it.
      const bool hidden_locked = cfg.settings_pin_enabled;
      if (hidden_locked && !hidden_icon.length()) hidden_icon = "lock";
      html += "><i class=\"mdi mdi-";
      appendHtmlEscaped(html, hidden_icon);
      html += " tile-icon\">";
      if (hidden_locked && hidden_icon != "lock") {
        html += "<span class=\"tile-icon-lock mdi mdi-lock\" aria-hidden=\"true\"></span>";
      }
      html += "</i><div class=\"tile-title\">";
      appendTileTitleHtml(html, hidden_title);
      html += "</div>";
    } else {
      html += "><i class=\"mdi mdi-tray-arrow-down tile-icon\"></i>";
    }
    // Beside the slot: its drop hint while it is empty, and the tile editing
    // hint the other grids show in their footer.
    html += "</div></div><div class=\"settings-parking-texts\"><div id=\"settingsHiddenHint\" class=\"settings-hidden-hint";
    if (hidden) html += " is-hidden";
    html += "\">";
    appendHtmlEscaped(html, tr.settings_tile_parking);
    html += "</div><p class=\"hint\">";
    html += tr.admin_tile_hint;
    html += "</p></div></div>";
  } else if (!screensaver_mode) {
    // A folder shows the editing hint and Delete Folder where Home keeps its
    // Settings parking slot: beside the grid on four-column devices, below
    // it on the others.
    html += "<div class=\"settings-hidden-parking folder-side\"><div class=\"settings-parking-texts\"><p class=\"hint\">";
    html += tr.admin_tile_hint;
    html += "</p><button type=\"button\" class=\"btn btn-danger btn-delete-folder\" onclick=\"deleteFolder('";
    html += tab_id;
    html += "')\">";
    html += tr.admin_delete_folder_tab;
    html += "</button></div></div>";
  }
  html += R"html(          <div class="folder-footer">
)html";
  const String radius_value = String(configManager.getConfig().tile_radius);
  auto append_radius_input = [&](const String& id) {
    html += "<input class=\"global-tile-radius\"";
    if (id.length()) html += " id=\"" + id + "\"";
    html += " type=\"range\" min=\"";
    html += String(tile_radius::kMinimum);
    html += "\" max=\"";
    html += String(tile_radius::kMaximum);
    html += "\" step=\"1\" value=\"";
    html += radius_value;
    html += "\" oninput=\"previewTileRadiusLive(this.value)\" onchange=\"saveTileRadius(this.value)\"><output class=\"global-tile-radius-value\">";
    html += radius_value;
    html += "</output>";
  };
  if (!screensaver_mode) {
    // Global display settings in the Tile Settings style: two compact
    // columns, checkboxes like Glow, fields with a small label above them
    // like Column/Row, and the Color field with its reset button.
    const DeviceConfig& display = configManager.getConfig();
    const String borders_id = tab_id + "_global_tile_borders";
    const String discs_id = tab_id + "_global_icon_discs";
    const String radius_id = tab_id + "_global_tile_radius";
    const String color_id = tab_id + "_global_tile_color";
    const String glow_id = tab_id + "_global_icon_glow";
    const String glow_value = String(icon_glow::clamp(display.icon_glow));
    html += "<section class=\"global-settings-panel\"><h3>";
    appendHtmlEscaped(html, tr.global_settings_heading);
    html += "</h3><div class=\"global-settings-grid\"><label class=\"inline-checkbox\">"
            "<input class=\"normal-tile-border-toggle\" id=\"" +
            borders_id + "\" type=\"checkbox\" onchange=\"saveNormalTileBorders(this.checked)\"";
    if (display.tile_borders) html += " checked";
    html += "> ";
    appendHtmlEscaped(html, tr.screensaver_tile_border);
    html += "</label><label class=\"inline-checkbox\"><input class=\"global-icon-disc-toggle\" id=\"" +
            discs_id + "\" type=\"checkbox\" onchange=\"saveIconDiscs(this.checked)\"";
    if (display.icon_discs) html += " checked";
    html += "> ";
    appendHtmlEscaped(html, tr.icon_discs);
    html += "</label><div class=\"global-settings-field\"><label for=\"" + radius_id + "\">";
    appendHtmlEscaped(html, tr.tile_radius);
    html += "</label><div class=\"global-radius-field\">";
    append_radius_input(radius_id);
    html += "</div></div><div class=\"global-settings-field\"><label for=\"" + glow_id + "\">";
    appendHtmlEscaped(html, tr.icon_glow_strength);
    html += "</label><div class=\"global-radius-field\"><input class=\"global-icon-glow\" id=\"" + glow_id +
            "\" type=\"range\" min=\"" + String(icon_glow::kMinimum) + "\" max=\"" +
            String(icon_glow::kMaximum) + "\" step=\"" + String(icon_glow::kStep) + "\" value=\"" + glow_value +
            "\" oninput=\"previewIconGlowLive(this.value)\" onchange=\"saveIconGlow(this.value)\">"
            "<output class=\"global-icon-glow-value\">" + glow_value + " %</output>"
            "<button type=\"button\" class=\"tile-color-reset-btn global-icon-glow-reset\" title=\"Reset\" "
            "onclick=\"saveIconGlow(" + String(icon_glow::kDefault) + ")\"><i class=\"mdi mdi-restore\"></i></button>"
            "</div></div>";
    html += "<div class=\"global-settings-field\"><label for=\"" + color_id + "\">";
    appendHtmlEscaped(html, tr.default_tile_color);
    char default_color_hex[8];
    snprintf(default_color_hex, sizeof(default_color_hex), "#%06X",
             static_cast<unsigned>(tileDefaultBgColor()));
    char factory_color_hex[8];
    snprintf(factory_color_hex, sizeof(factory_color_hex), "#%06X",
             static_cast<unsigned>(tile_color::kDefault));
    html += "</label><div class=\"tile-color-row\"><input class=\"global-tile-color\" id=\"" + color_id +
            "\" type=\"color\" value=\"";
    html += default_color_hex;
    html += "\" oninput=\"previewDefaultTileColor(this.value)\" "
            "onchange=\"saveDefaultTileColor(this.value)\">"
            "<button type=\"button\" class=\"tile-color-reset-btn\" title=\"Reset\" "
            "onclick=\"saveDefaultTileColor('";
    html += factory_color_hex;
    html += "')\"><i class=\"mdi mdi-restore\"></i></button></div></div></div></section>\n";
  } else {
    html += R"html(            <div class="folder-footer-options screensaver-tile-options">
<label class="inline-checkbox"><input id="screensaverTileBorder" type="checkbox"> )html";
    html += tr.screensaver_tile_border;
    html += R"html(</label>
)html";
    html += "<label class=\"tile-radius-control\"><span>";
    appendHtmlEscaped(html, tr.tile_radius);
    html += "</span>";
    append_radius_input(String());
    html += "</label>";
    html += R"html(              <label class="inline-checkbox"><input id="screensaverTileShadow" type="checkbox"> )html";
    html += tr.screensaver_tile_shadow;
    html += R"html(</label>
)html";
    // One opacity for every screensaver tile, beside its other tile options.
    const uint8_t tile_opacity = screensaverConfig.get().tile_opacity;
    html += "<label class=\"tile-radius-control\"><span>";
    appendHtmlEscaped(html, tr.screensaver_background_opacity);
    html += "</span><input id=\"screensaverTileOpacity\" type=\"range\" min=\"0\" max=\"255\" step=\"1\" value=\"";
    html += String(tile_opacity);
    html += "\"><output id=\"screensaverTileOpacityValue\" class=\"global-tile-opacity-value\">";
    html += String((tile_opacity * 100 + 127) / 255);
    html += " %</output></label>";
    html += R"html(            </div>
<p class="hint">)html";
  }
  if (screensaver_mode) {
    html += tr.screensaver_hint;
    html += R"html(</p>
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
<div class="tile-head-top"><h3>)html";
  html += tr.admin_tile_settings;
  html += R"html(</h3>
<select id=")html";
  html += tab_id;
  html += R"html(_tile_type" aria-label=")html";
  appendHtmlEscaped(html, tr.admin_type);
  html += R"html(" onchange="updateTileType(')html";
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
</select></div>
<p class="hint hidden" id=")html";
  html += tab_id;
  html += R"html(_tile_type_hint">)html";
  html += tr.admin_folder_type_locked;
  // The entity of the shown type right under Type (placeTileEntityField in
  // tiles/entity-picker.js), then title and icon side by side. The head
  // stays while the body scrolls. The icon picker (tiles/tile-head.js) keeps
  // the icon name in the hidden input: empty = the entity's icon, "none" =
  // no icon.
  html += R"html(</p>
<div class="tile-entity-slot" id=")html";
  html += tab_id;
  html += R"html(_tile_entity_slot"></div>
<div class="tile-head-row"><div><div class="tile-title-label"><label for=")html";
  html += tab_id;
  html += R"html(_tile_title">)html";
  appendHtmlEscaped(html, tr.admin_title);
  // Auto: the title is the entity's name from Home Assistant (tile-head.js).
  html += R"html(</label><button type="button" class="tile-title-auto" id=")html";
  html += tab_id;
  html += R"html(_tile_title_auto" aria-pressed="false" title=")html";
  appendHtmlEscaped(html, tr.tile_title_auto_hint);
  html += R"html(">)html";
  appendHtmlEscaped(html, tr.tile_title_auto);
  html += R"html(</button></div><textarea rows="1" class="tile-title-input" spellcheck="false" id=")html";
  html += tab_id;
  html += R"html(_tile_title" placeholder=")html";
  appendHtmlEscaped(html, tr.admin_tile_title_placeholder);
  html += R"html("></textarea></div><div><label for=")html";
  html += tab_id;
  html += R"html(_tile_icon_picker">)html";
  appendHtmlEscaped(html, tr.tile_group_icon);
  html += R"html(</label><input type="hidden" id=")html";
  html += tab_id;
  html += R"html(_tile_icon" data-tile-icon><button type="button" class="tile-icon-picker" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_picker" aria-haspopup="listbox"></button></div></div>
</div>
<div class="tile-settings-body">

<div class="tile-settings-group">)html";
  appendHtmlEscaped(html, tr.tile_group_icon);
  html += R"html(</div>
)html";
  append_tile_icon_color_fixed_html(html, tab_id);
  html += R"html(            <div class="tile-icon-disc-fields" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_disc_fields">
<label class="inline-checkbox" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_disc_row"><input type="checkbox" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_disc" checked> )html";
  appendHtmlEscaped(html, tr.icon_disc_label);
  html += R"html(</label>
<label class="inline-checkbox" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_glow_row"><input type="checkbox" id=")html";
  html += tab_id;
  html += R"html(_tile_icon_glow" checked> )html";
  appendHtmlEscaped(html, tr.icon_glow);
  html += R"html(</label>
)html";
  // Weather only: filled, colored weather icons or the white outlines.
  if (!screensaver_mode) {
    html += R"html(              <label class="inline-checkbox hidden" id=")html";
    html += tab_id;
    html += R"html(_weather_colored_icons_row"><input type="checkbox" id=")html";
    html += tab_id;
    html += R"html(_weather_colored_icons" checked> )html";
    appendHtmlEscaped(html, tr.weather_colored_icons);
    html += R"html(</label>
)html";
  }
  html += R"html(            </div>

<div class="tile-settings-group">)html";
  appendHtmlEscaped(html, tr.tile_group_tile);
  html += R"html(</div>
<div class="tile-color-label-row no-reset"><span>)html";
  html += tr.admin_color;
  html += R"html(</span></div>
<div class="icon-color-segmented choice-row tile-color-modes" role="group" id=")html";
  html += tab_id;
  html += R"html(_tile_color_modes">)html";
  // Tile color is one choice: the global tile color, an own color, a tint
  // that follows the icon color (only for tiles with icon colors) or, on
  // Media tiles, a tint from the album cover.
  const struct {
    const char* mode;
    const char* label;
  } tile_color_modes[] = {{"global", tr.tile_color_mode_global},
                          {"custom", tr.tile_color_mode_custom},
                          {"icon", tr.tile_color_mode_from_icon},
                          {"cover", tr.tile_color_mode_from_cover}};
  for (const auto& entry : tile_color_modes) {
    html += R"html(<button type="button" id=")html";
    html += tab_id;
    html += "_tile_color_mode_";
    html += entry.mode;
    html += R"html(" data-tile-color-mode=")html";
    html += entry.mode;
    html += R"html(" onclick="setTileColorMode(')html";
    html += tab_id;
    html += "', '";
    html += entry.mode;
    html += R"html(')">)html";
    appendHtmlEscaped(html, entry.label);
    html += "</button>";
  }
  html += R"html(</div>
<div class="tile-color-row no-reset" id=")html";
  html += tab_id;
  html += R"html(_tile_color_row">
<input type="color" id=")html";
  html += tab_id;
  html += R"html(_tile_color" value="#2A2A2A">
)html";
  // Screensaver tiles share one opacity (screensaver footer, tile_opacity).
  html += R"html(            </div>
)html";
  append_tile_color_from_icon_html(html, tab_id);
  // Position and size: dragging and resizing in the grid edit them; the
  // values stay in hidden inputs for drafts, undo, copy and saving.
  html += R"html(
<input type="hidden" id=")html";
  html += tab_id;
  html += R"html(_tile_col" min="1" max=")html";
  html += String(GRID_COLS);
  html += R"html(" step="0.5" value="1">
<input type="hidden" id=")html";
  html += tab_id;
  html += R"html(_tile_row" min=")html";
  html += String(screensaver_mode && GRID_ROWS > 1 ? GRID_ROWS - 1 : 1);
  html += R"html(" max=")html";
  html += String(GRID_ROWS);
  html += R"html(" step="0.5" value="1">
<input type="hidden" id=")html";
  html += tab_id;
  html += R"html(_tile_span_w" min="1" max=")html";
  html += String(GRID_COLS);
  html += R"html(" step="0.5" value="1">
<input type="hidden" id=")html";
  html += tab_id;
  html += R"html(_tile_span_h" min="1" max=")html";
  html += String(GRID_ROWS);
  html += R"html(" step="0.5" value="1">

)html";

  html += "<p class=\"settings-note\" hidden id=\"" + tab_id + "_tile_size_note\">";
  appendHtmlEscaped(html, tr.tile_fractional_type_hint);
  html += "</p>";
            TileTypeWebContext type_ctx;
            type_ctx.tab_id = &tab_id;
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
  html += R"html(" type="button")html";
  html += R"html( onclick="switchTab('tab-tiles-)html";
  html += tab_id;
  html += R"html(')">
<span class="tab-disc"><i class="mdi mdi-)html";
  html += icon.length() ? icon : String(entry.id == 0 ? "home" : "folder");
  html += R"html("></i></span>
<span class="tab-label">)html";
  appendHtmlEscaped(html, name);
  html += R"html(</span>
</button>
)html";
  return html;
}

bool buildAdminFolderTabFragments(uint16_t folder_id, String& button_html, String& tab_html, String& tab_id) {
  const FolderEntry* folder = tileConfig.getFolder(folder_id);
  if (!folder) return false;

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
  appendTileTabHTML(tab_html, folder_id, *folder, grid, formatSensorValue,
                    navigateOptionsHtml);
  return true;
}

// One entry of the Settings list: a colored icon disc, the title and an
// optional line under it, like the category tiles of the panel's Settings.
static void appendSettingsNavItem(String& html, const char* page, const char* color,
                                  const char* icon, const String& title, const String& sub) {
  html += R"html(
<button class="settings-nav-item" type="button" data-settings-page=")html";
  html += page;
  html += R"html(" style="--settings-color:)html";
  html += color;
  html += R"html("><span class="settings-nav-disc"><i class="mdi mdi-)html";
  html += icon;
  html += R"html("></i></span><span class="settings-nav-text"><span class="settings-nav-title">)html";
  appendHtmlEscaped(html, title);
  html += "</span>";
  if (sub.length()) {
    html += R"html(<span class="settings-nav-sub">)html";
    appendHtmlEscaped(html, sub);
    html += "</span>";
  }
  html += "</span></button>";
}

// A status row: icon, title, an optional line under it, and a value on the
// right ("Connected", "Paired"), as on the panel's System page.
static void appendSettingsStateRow(String& html, const char* icon, const String& title,
                                   const String& sub, const String& value, const char* value_icon) {
  html += R"html(
<div class="settings-state-row"><i class="mdi mdi-)html";
  html += icon;
  html += R"html( settings-state-icon"></i><div class="settings-state-text"><div class="settings-state-title">)html";
  appendHtmlEscaped(html, title);
  html += "</div>";
  if (sub.length()) {
    html += R"html(<div class="settings-state-sub">)html";
    appendHtmlEscaped(html, sub);
    html += "</div>";
  }
  html += "</div>";
  if (value.length()) {
    html += value_icon ? R"html(<div class="settings-state-value is-good">)html"
                       : R"html(<div class="settings-state-value">)html";
    appendHtmlEscaped(html, value);
    if (value_icon) {
      html += R"html(<i class="mdi mdi-)html";
      html += value_icon;
      html += R"html("></i>)html";
    }
    html += "</div>";
  }
  html += "</div>";
}

// The Settings tab, ordered like the panel's Settings (WLAN, Lokalisierung,
// System), then the pages only the Web Admin has: I/O, the built-in camera,
// files and diagnostics. The list on the left picks one page; the footer
// under both carries that page's buttons (settings/pages.js). WLAN,
// Lokalisierung and the MQTT fields share the /mqtt form, so every Save
// stores them together as before.
static void appendSettingsTabHtml(String& html, const DeviceConfig& cfg,
                                  const i18n::Strings& tr, bool supports_ethernet,
                                  bool use_static) {
  const bool connected = networkTransport.isConnected();
  const bool on_wifi = networkTransport.activeKind() == NetworkTransportKind::Wifi;
  const bool camera = local_camera::supported() && Device::kCapabilities.has_builtin_camera;
  const bool link = networkManager.linkConfigured();
  const bool paired = command_channel::state() == command_channel::PairingState::Active;
  const String network_title = supports_ethernet ? tr.settings_network : tr.wifi_label;
  String network_name;
  if (connected) network_name = on_wifi ? WiFi.SSID() : String(networkTransport.activeName());

  html += R"html(
<div id="tab-network" class="tab-content">
<div class="settings-layout">
<nav class="settings-nav" aria-label=")html";
  appendHtmlEscaped(html, tr.tile_type_settings);
  html += R"html(">)html";
  appendSettingsNavItem(html, "network", "#42A5F5",
                        !connected ? "wifi-off" : on_wifi ? "wifi" : "ethernet", network_title,
                        connected ? network_name : String(tr.settings_not_connected));
  appendSettingsNavItem(html, "locale", "#AB47BC", "translate", tr.admin_settings_language,
                        String(i18n::locale(cfg.language).native_name) + " \xC2\xB7 " + cfg.timezone);
  appendSettingsNavItem(html, "system", "#26A69A", "chip", "System", FW_VERSION);
  html += R"html(
<div class="settings-nav-sep" aria-hidden="true"></div>)html";
  appendSettingsNavItem(html, "io", "#FF8A65", "electric-switch", tr.admin_io,
                        String(tr.tile_type_switch) + " \xC2\xB7 " + tr.admin_io_temperature);
  if (camera) {
    appendSettingsNavItem(html, "camera", "#FFB300", "camera", tr.local_camera_section,
                          localCameraStateText(local_camera::stateName(), tr));
  }
  appendSettingsNavItem(html, "files", "#78909C", "folder-open", tr.file_manager_title,
                        String(tr.admin_export) + " \xC2\xB7 " + tr.admin_import + " \xC2\xB7 microSD");
  appendSettingsNavItem(html, "diagnostics", "#9E9E9E", "stethoscope", tr.admin_settings_screenshot, "");
  html += R"html(
</nav>
<div class="settings-pages">
<form id="admin_settings_form" class="settings-form" action="/mqtt" method="POST" autocomplete="on">
<section class="settings-page" data-settings-page="network">
<div class="settings-section">
<div class="section-title-row">
<div class="section-title">)html";
  appendHtmlEscaped(html, network_title);
  html += R"html(</div>
<div class="wifi-inline-status"><span class="wifi-inline-dot)html";
  if (!connected) html += " off";
  html += R"html("></span>)html";
  html += connected ? tr.wifi_connected : tr.wifi_disconnected;
  if (connected) {
    html += " \xC2\xB7 ";
    appendHtmlEscaped(html, network_name);
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
    if (!cfg.ethernet_enabled) html += " selected";
    html += R"html(>WiFi</option>
<option value="ethernet")html";
    if (cfg.ethernet_enabled) html += " selected";
    html += R"html(>Ethernet</option>
</select>
<div class="settings-note">)html";
    html += tr.admin_connection_type_note;
    html += R"html(</div>
</div>)html";
  }
  html += R"html(
<div id="wifi_network_settings" class="network-settings-group settings-full )html";
  if (supports_ethernet && cfg.ethernet_enabled) html += "is-hidden";
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
  appendStoredSecretValue(html, cfg.wifi_pass, tr);
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
  if (!use_static) html += "is-hidden";
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
</div>
</div>
</div>
</section>

<section class="settings-page" data-settings-page="locale">
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
</section>

<section class="settings-page" data-settings-page="system">
<div class="settings-section">
<div class="settings-state-row settings-version-row">
<svg width="40" height="40" viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
<rect x="4" y="4" width="17" height="17" rx="4" fill="#ffffff"/>
<rect x="27" y="4" width="17" height="17" rx="4" fill="#ffffff"/>
<rect x="4" y="27" width="17" height="17" rx="4" fill="#ffffff"/>
<path d="M33 26h5v6.5h6.5v5H38V44h-5v-6.5h-6.5v-5H33z" fill="#26a69a"/>
</svg>
<div class="settings-state-text">
<div class="settings-state-title settings-version">HomeTiles <span>)html";
  html += FW_VERSION;
  html += R"html(</span></div>
<div class="settings-state-sub">)html";
  appendHtmlEscaped(html, Device::displayName());
  html += R"html(</div>
</div>
<button class="btn btn-go settings-row-btn" type="button" id="ota_github_btn" onclick="checkOrInstallGithubFirmware()"><i class="mdi mdi-magnify"></i> )html";
  html += tr.system_check_updates_btn;
  html += R"html(</button>
</div>
<div id="ota_github_status" class="settings-note ota-status"></div>
<div id="ota_github_progress" class="ota-progress is-hidden" aria-hidden="true">
<div id="ota_github_progress_bar" class="ota-progress-bar"></div>
</div>
<div class="settings-fw-file">
<span class="settings-note">)html";
  html += tr.ota_firmware_file;
  html += R"html(:</span>
<input type="file" id="ota_file" accept=".bin,application/octet-stream" style="display:none" onchange="updateOtaFileName(this)">
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

<div class="settings-section">
<div class="section-title">)html";
  html += tr.settings_connection;
  html += R"html(</div>)html";
  char route[96];
  const bool has_route = settings_model::bridge_route(route, sizeof(route));
  const bool ha_connected = networkManager.isMqttConnected();
  appendSettingsStateRow(html, "lan-connect", "Home Assistant", has_route ? String(route) : String(),
                         ha_connected ? tr.security_value_connected : tr.settings_not_connected,
                         ha_connected ? "check" : nullptr);
  // MQTT stays reachable, folded into one row; on the direct link its fields
  // are kept but not used.
  String mqtt_line;
  if (link) {
    mqtt_line = tr.settings_mqtt_unused;
  } else if (cfg.mqtt_host[0]) {
    mqtt_line = String(cfg.mqtt_host) + ":" + String(cfg.mqtt_port ? cfg.mqtt_port : 1883);
  } else {
    mqtt_line = tr.mqtt_not_configured;
  }
  html += R"html(
<details class="settings-fold" id="mqtt_settings">
<summary><i class="mdi mdi-access-point-network settings-state-icon"></i><span class="settings-state-text"><span class="settings-state-title">MQTT</span><span class="settings-state-sub">)html";
  appendHtmlEscaped(html, mqtt_line);
  html += R"html(</span></span><i class="mdi mdi-chevron-right settings-fold-chev"></i></summary>
<div class="settings-fold-body">)html";
  if (link) {
    html += R"html(
<div class="settings-note settings-fold-note">)html";
    appendHtmlEscaped(html, tr.settings_mqtt_unused_note);
    html += "</div>";
  }
  html += R"html(
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
  appendStoredSecretValue(html, cfg.mqtt_pass, tr);
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
<div class="settings-fold-actions">
<button class="btn btn-go" type="submit" form="admin_settings_form">)html";
  html += tr.save;
  html += R"html(</button>
</div>
</div>
</details>
</div>

<div class="settings-section">
<div class="section-title">)html";
  html += tr.security_btn;
  html += R"html(</div>)html";
  appendSettingsStateRow(html, paired ? "link-variant" : "link-variant-off", tr.settings_pairing,
                         !paired ? tr.settings_not_paired
                         : link  ? tr.settings_states_encrypted
                                 : tr.settings_commands_encrypted,
                         paired ? tr.settings_paired : "", paired ? "shield-check" : nullptr);
  // The Web Admin password row folds out its fields (web_admin_security_html.cpp).
  appendWebAdminPasswordSettingsHtml(html, tr);
  html += R"html(
</div>
</section>
</form>

<!-- Local GPIO / relay / temperature assignments -->
<section class="settings-page settings-page-io" data-settings-page="io">
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
</div>
</section>
)html";

  if (camera) {
    html += R"html(
<section class="settings-page" data-settings-page="camera">)html";
    appendLocalCameraSettingsHtml(html, tr);
    html += R"html(
</section>
)html";
  }

  html += R"html(
<section class="settings-page" data-settings-page="files">
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
</section>

<section class="settings-page" data-settings-page="diagnostics">
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
<button class="btn btn-secondary" type="button" onclick="window.open('/api/sd-diagnostics?ts=' + Date.now(), '_blank')">)html";
  html += tr.sd_diagnostics_open;
  html += R"html(</button>
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
</section>
</div>
</div>

<form id="admin_restart_form" action="/restart" method="POST" class="admin-hidden-form" data-confirm=")html";
  appendHtmlEscaped(html, tr.restart_confirm);
  html += R"html(" onsubmit="return confirm(this.dataset.confirm);"></form>
<!-- One footer under the list and the page, across the full width; its
buttons follow the chosen page. Pages that act at once have none. -->
<div class="admin-footer-actions settings-foot" id="settingsFoot">
<div class="settings-foot-set" data-settings-foot="network"><button class="btn btn-go admin-footer-btn" type="submit" form="admin_settings_form">)html";
  html += tr.save;
  html += R"html(</button></div>
<div class="settings-foot-set" data-settings-foot="locale"><button class="btn btn-go admin-footer-btn" type="submit" form="admin_settings_form">)html";
  html += tr.save;
  html += R"html(</button></div>
<div class="settings-foot-set" data-settings-foot="system"><button class="btn btn-secondary admin-footer-btn" type="submit" form="admin_restart_form">)html";
  html += tr.restart_button;
  html += R"html(</button></div>
<div class="settings-foot-set" data-settings-foot="io">
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
)html";
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
  // The global icon disc option is a root class, so every preview grid,
  // including lazily inserted folders, follows it without re-rendering.
  if (!configManager.getConfig().icon_discs) html += "\" class=\"icon-discs-off";
  html += R"html(">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>)html";
  html += admin_panel_title;
  html += R"html(</title>
)html";

  appendWebAdminCsrfMeta(html, server);
  appendAdminStyles(html);
  appendAdminScripts(html);

  html += R"html(
</head>
<body>
<div class="wrapper)html";
  // Four-column devices keep their preview size on the wide page; the
  // Settings slot moves beside it (admin.css .compact-grid).
  if (GRID_COLS <= 4) html += " compact-grid";
  html += R"html(">
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
<div class="brand-side">
<div class="brand-links">)html";
  appendWebAdminPasswordBadgeHtml(html, tr);
  html += R"html(
<a class="brand-link" href="https://galusperes.github.io/HomeTiles/" target="_blank" rel="noopener"><i class="mdi mdi-book-open-variant"></i>Docs</a>
<a class="brand-link" href="https://github.com/GalusPeres/HomeTiles" target="_blank" rel="noopener"><i class="mdi mdi-github"></i>GitHub</a>
</div>)html";
  // Small support line: a GitHub star (GitHub has no direct star URL, the
  // repository page has the button) or a coffee.
  html += R"html(
<div class="brand-support">)html";
  appendHtmlEscaped(html, String(tr.web_support_prefix));
  html += R"html( <a class="brand-star" href="https://github.com/GalusPeres/HomeTiles" target="_blank" rel="noopener"><i class="mdi mdi-star"></i>)html";
  appendHtmlEscaped(html, String(tr.web_support_stars));
  html += R"html(</a> )html";
  appendHtmlEscaped(html, String(tr.web_support_or));
  html += R"html( <a class="brand-coffee" href="https://buymeacoffee.com/galusperes" target="_blank" rel="noopener"><i class="mdi mdi-coffee"></i>Buy Me a Coffee</a></div>
</div>
</div>

<!-- Tab Navigation -->
<div class="tab-nav">
)html";

  // Home stands alone on the left. A divider separates it from the folders,
  // which follow in tree order, all as wide as Home, and wrap into more rows.
  // A second divider separates Screensaver, I/O and Settings on the right. The
  // folder buttons keep their classes and data, so switching, renaming and
  // lazily added folders work as before (folders/navigation.js).
  for (const auto& entry : folders) {
    if (entry.id == 0) html += buildFolderTabButtonHtml(entry);
  }
  html += R"html(
<span class="tab-sep" aria-hidden="true"></span>
<div class="tab-folders" id="folderTabs" role="group" aria-label=")html";
  appendHtmlEscaped(html, String(tr.admin_folders));
  html += R"html(">)html";
  auto append_children = [&](auto&& self, uint16_t parent, int depth) -> void {
    if (depth > 8) return;
    for (const auto& entry : folders) {
      if (entry.id == 0 || entry.parent_id != parent) continue;
      html += buildFolderTabButtonHtml(entry);
      self(self, entry.id, depth + 1);
    }
  };
  append_children(append_children, 0, 0);
  // A folder whose parent no longer exists still gets its button.
  for (const auto& entry : folders) {
    if (entry.id != 0 && entry.parent_id != 0 && !tileConfig.getFolder(entry.parent_id)) {
      html += buildFolderTabButtonHtml(entry);
    }
  }
  html += R"html(</div>
<span class="tab-sep" aria-hidden="true"></span>
<div class="tab-system">
<button class="tab-btn" type="button" data-tab-target="tab-tiles-screensaver"
onclick="switchTab('tab-tiles-screensaver')">
<span class="tab-disc"><i class="mdi mdi-monitor"></i></span>
<span class="tab-label">Screensaver</span>
</button>
<button class="tab-btn" type="button" data-tab-target="tab-network"
onclick="switchTab('tab-network')">
<span class="tab-disc"><i class="mdi mdi-cog"></i></span>
<span class="tab-label">)html";
  html += tr.tile_type_settings;
  html += R"html(</span>
</button>
</div>
</div>
)html";

  // Only Home is part of the initial page. Other folder editors are generated
  // on first use via /api/folders/tab, avoiding synchronous all-folder reads.
  for (const auto& entry : folders) {
    if (entry.id != 0) continue;
    TileGridConfig grid{};
    tileConfig.loadFolderGrid(entry.id, grid);
    appendTileTabHTML(html, entry.id, entry, grid, formatSensorValue,
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
                    formatSensorValue, navigateOptionsHtml, true);

  appendSettingsTabHtml(html, cfg, tr, supports_ethernet, use_static);
  html += R"html(
</div>
</div>

<!-- Notification Toast -->
<div id="notification" class="notification" role="status" aria-live="polite"></div>
</body>
</html>
)html";

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
  // Camera profile only; every other profile keeps its previous API.
  if (local_camera::supported() && Device::kCapabilities.has_builtin_camera) {
    json += ",\"local_camera\":";
    local_camera::appendStatusJson(json);
  }
  json += "}";
  return json;
}

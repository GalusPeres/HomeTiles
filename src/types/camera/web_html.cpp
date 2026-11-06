#include "src/types/camera/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/switch/web_html.h"
#include "src/web/server/web_admin_utils.h"

// One source per tile, under it its entries (types/camera/camera_entries.h, drawn
// by types/camera/admin.js): cameras and images of Home Assistant, rain radar
// places or webcams. Below them what applies to the tile: with "Picture" it shows
// the Bridge's picture of its entry (picture.h), its placement, a camera's
// interval, the colors and transparency of the circle and the title pill; a tap
// opens the popup or the full screen. The editor shows those rows only where they
// apply (syncCameraFields). Laid out like every tile, only with more entities (user
// 2026-11-06): source and entries in the head under Type, where a tile's entity
// stands (data-tile-place, type-selection.js placeTileTypeParts), the options under
// Advanced. No hint text (user 2026-10-11: too much text).
void append_camera_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <div id=")html";
  html += tab_id;
  html += R"html(_camera_fields" class="type-fields">
              )html";
  html += "<input type=\"hidden\" id=\"" + tab_id + "_camera_entity\"><input type=\"hidden\" id=\"" + tab_id +
          "_camera_entries\">\n";
  html += "              <div class=\"tile-place camera-tile-fields\" data-tile-place=\"head\">\n";
  const SwitchChoice sources[] = {
      {"h", tr.camera_source_ha}, {"r", tr.camera_source_radar}, {"w", tr.camera_source_webcam}};
  append_switch_choice(html, tab_id, "camera_source", tr.media_source, sources, 3);
  html += "              <label class=\"switch-choice-label\" id=\"" + tab_id + "_camera_list_label\"></label>\n";
  html += "              <div class=\"camera-entries-wrap\" data-tab=\"" + tab_id + "\"><div class=\"camera-entries\" id=\"" +
          tab_id + "_camera_list\"></div><button type=\"button\" class=\"camera-entry-add\" id=\"" + tab_id +
          "_camera_add\" data-camera-add></button></div>\n              </div>\n";
  const SwitchChoice shows[] = {{"0", tr.camera_show_icon}, {"1", tr.camera_show_picture}};
  append_switch_choice(html, tab_id, "camera_display", tr.camera_show, shows, 2);
  // Which entry the tile shows, only with more than one (cameraRenderShows).
  html += "              <div class=\"camera-shows-row hidden\" id=\"" + tab_id + "_camera_shows_row\"><label for=\"" + tab_id +
          "_camera_shows\">";
  appendHtmlEscaped(html, tr.camera_tile_shows);
  html += "</label><select id=\"" + tab_id + "_camera_shows\"></select></div>\n";
  html += R"html(              <div id=")html";
  html += tab_id;
  html += R"html(_camera_tap_row">
)html";
  const SwitchChoice taps[] = {{"0", tr.camera_tap_popup}, {"1", tr.camera_tap_full}};
  append_switch_choice(html, tab_id, "camera_tap", tr.camera_tap, taps, 2);
  html += "              </div>\n";
  html += R"html(              <div id=")html";
  html += tab_id;
  html += R"html(_camera_picture_fields" class="camera-picture-fields hidden">
)html";
  const SwitchChoice fits[] = {{"fill", tr.screensaver_fit_fill},
                               {"fit", tr.screensaver_fit_contain},
                               {"original", tr.screensaver_fit_original}};
  append_switch_choice(html, tab_id, "camera_fit", tr.screensaver_picture_fit, fits, 3);
  const SwitchChoice heads[] = {
      {"auto", tr.camera_head_auto}, {"dark", tr.camera_head_dark}, {"light", tr.camera_head_light}};
  append_switch_choice(html, tab_id, "camera_head", tr.camera_head, heads, 3);
  // A camera's interval; an image entity sends each new picture at once.
  html += R"html(              <div id=")html";
  html += tab_id;
  html += R"html(_camera_every_row" class="hidden"><label for=")html";
  html += tab_id;
  html += R"html(_camera_every">)html";
  appendHtmlEscaped(html, tr.screensaver_picture_every);
  html += R"html(</label><input type="number" id=")html";
  html += tab_id;
  html += R"html(_camera_every" min="3" max="60" step="1" value="10"></div>
              <label class="tile-radius-control camera-transparency-control"><span>)html";
  appendHtmlEscaped(html, tr.camera_head_transparency);
  html += R"html(</span><input type="range" id=")html";
  html += tab_id;
  html += R"html(_camera_transparency" min="0" max="100" step="5" value="0"><output id=")html";
  html += tab_id;
  html += R"html(_camera_transparency_value" class="global-tile-opacity-value">0 %</output></label>
              </div>
            </div>
)html";
}

#include "src/types/camera/web_scripts.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/types/camera/picture.h"

// The editable JavaScript lives in src/web/assets/admin.js and is served as
// one precompressed, cacheable firmware asset. The preview of a Camera tile
// showing its picture chooses its head like the panel (picture.cpp
// show_head): the geometry in display pixels and the title font's
// whole-pixel advances from U+0020 to U+017F (0: not in the font; the preview
// measures such a title itself).
namespace {

void append_camera_js_string(String& html, const char* value) {
  html += "'";
  for (const char* c = value ? value : ""; *c; ++c) {
    switch (*c) {
      case '\\': html += "\\\\"; break;
      case '\'': html += "\\'"; break;
      case '<': html += "\\x3c"; break;
      default: html += *c; break;
    }
  }
  html += "'";
}

}  // namespace

void append_camera_scripts(String& html) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += "  <script>\n  const CAMERA_TEXT = Object.freeze({";
  const auto add_text = [&](const char* key, const char* value) {
    html += key;
    html += ":";
    append_camera_js_string(html, value);
    html += ",";
  };
  add_text("entity", tr.screensaver_picture_entity);
  add_text("listPlaces", tr.camera_list_places);
  add_text("listWebcams", tr.camera_list_webcams);
  add_text("place", tr.camera_place);
  add_text("address", tr.camera_address);
  add_text("addEntity", tr.camera_add_entity);
  add_text("addPlace", tr.camera_add_place);
  add_text("addWebcam", tr.camera_add_webcam);
  add_text("newWebcam", tr.camera_new_webcam);
  add_text("placeHome", tr.camera_place_home);
  add_text("kindZone", tr.camera_kind_zone);
  add_text("kindImage", tr.camera_show_picture);
  add_text("ownCoordinates", tr.camera_own_coordinates);
  add_text("ownCoordinatesHint", tr.camera_own_coordinates_hint);
  add_text("noCoordinates", tr.camera_no_coordinates);
  add_text("latitudeLabel", tr.camera_latitude);
  add_text("longitudeLabel", tr.camera_longitude);
  add_text("latitudeRange", tr.camera_latitude_range);
  add_text("longitudeRange", tr.camera_longitude_range);
  add_text("noPictureFound", tr.camera_no_picture_found);
  add_text("showsLast", tr.camera_tile_shows_last);
  add_text("siteBlocked", tr.camera_site_blocked);
  add_text("remove", tr.camera_remove);
  add_text("title", tr.admin_title);
  add_text("auto", tr.tile_title_auto);
  add_text("icon", tr.tile_group_icon);
  html += "});\n  </script>\n";
  const int icon_w =
      FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0) : 0;
  char text[200];
  snprintf(text, sizeof(text),
           "  <script>\n  const CAMERA_TILE_LAYOUT = Object.freeze({cellW: %d, cellH: %d, gap: %d, inset: %d, "
           "disc: %d, shrink: %d, pad: %d, first: 32, adv: [",
           GRID_CELL_W, GRID_CELL_H, GRID_GAP, tile_icon_disc::inset(), tile_icon_disc::header_diameter(icon_w),
           tile_icon_disc::corner_shrink(), static_cast<int>(tile_layout::scale_480(camera_picture::kTitlePad)));
  html += text;
  const lv_font_t* font = tile_layout::header_title_font();
  for (uint32_t code = 0x20; code <= 0x17F; ++code) {
    if (code != 0x20) html += ',';
    html += String(font ? lv_font_get_glyph_width(font, code, 0) : 0);
  }
  html += "]});\n  </script>\n";
}

#include "src/types/media/web_scripts.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/types/media/cover_geometry.h"
#include "src/types/media/tile_layout.h"

namespace {

// {px, line, base}: a label is as tall as the line height, its baseline
// base_line above the bottom.
int media_font_px(const lv_font_t* font) {
  struct Size {
    const lv_font_t* font;
    int px;
  };
  static const Size kSizes[] = {
#if defined(DEVICE_LAYOUT_480X480)
      {&ui_font_12, 12}, {&ui_font_14, 14},
#endif
      {&ui_font_16, 16}, {&ui_font_20, 20}, {&ui_font_24, 24}, {&ui_font_28, 28}, {&ui_font_32, 32},
  };
  for (const Size& size : kSizes) {
    if (size.font == font) return size.px;
  }
  return 20;
}

void append_media_font(String& html, const char* key, const lv_font_t* font) {
  char text[96];
  snprintf(text, sizeof(text), "    %s: {px: %d, line: %d, base: %d},\n", key, media_font_px(font),
           static_cast<int>(font->line_height), static_cast<int>(font->base_line));
  html += text;
}

void append_media_js_string(String& html, const String& value) {
  html += "'";
  for (size_t index = 0; index < value.length(); ++index) {
    const char c = value[index];
    switch (c) {
      case '\\': html += "\\\\"; break;
      case '\'': html += "\\'"; break;
      case '\r': break;
      case '\n': html += "\\n"; break;
      case '<': html += "\\x3c"; break;
      default: html += c; break;
    }
  }
  html += "'";
}

}  // namespace

// The editable JavaScript lives in src/web/assets/admin.js and is served as
// one precompressed, cacheable firmware asset. The media tile preview shows
// the device's state texts when nothing plays (media_empty_title_label).
void append_media_scripts(String& html) {
  const char* language = configManager.getConfig().language;
  const i18n::Strings& tr = i18n::strings(language);
  html += "  <script>\n  const MEDIA_I18N = Object.freeze({\n";
  auto append_i18n = [&](const char* key, const String& value) {
    html += "    ";
    html += key;
    html += ": ";
    append_media_js_string(html, value);
    html += ",\n";
  };
  append_i18n("playing", tr.media_state_playing);
  append_i18n("paused", tr.media_state_paused);
  append_i18n("idle", tr.media_state_idle);
  append_i18n("standby", tr.media_state_standby);
  append_i18n("off", tr.media_state_off);
  append_i18n("noPlayback", tr.media_no_playback);
  append_i18n("unavailable", i18n::entity_state_label(language, "unavailable"));
  append_i18n("unknown", i18n::entity_state_label(language, "unknown"));
  html += "  });\n";

  // The tile geometry in display pixels (renderer.cpp, content_layout.cpp,
  // tile_layout.h); the preview scales it like every other tile.
  html += "  const MEDIA_TILE_LAYOUT = Object.freeze({\n";
  char text[200];
  snprintf(text, sizeof(text),
           "    cellW: %d, cellH: %d, gap: %d, padH: %d, padV: %d, button: %d, buttonSide: %d,\n"
           "    buttonBottom: %d, coverLeft: %d, coverRadius: %d, maxCover: %d,\n",
           GRID_CELL_W, GRID_CELL_H, GRID_GAP, static_cast<int>(tile_layout::scale_480(20)),
           static_cast<int>(tile_layout::scale_480(24)), static_cast<int>(media_tile::kControlButtonSize),
           static_cast<int>(media_tile::kControlSideOffset), static_cast<int>(media_tile::kControlBottomOffset),
           static_cast<int>(media_tile::kCoverLeft), static_cast<int>(media_tile::kCoverRadius),
           media_layout::cover_side(1 << 20, 1 << 20, 0, 0));
  html += text;
  snprintf(text, sizeof(text),
           "    coverTop: %d, coverTopSmall: %d, textLeft: %d, textAfterCover: %d, textRight: %d,\n"
           "    subtitleGap: %d, footerGap: %d,\n",
           static_cast<int>(media_tile::cover_top(true)), static_cast<int>(media_tile::cover_top(false)),
           static_cast<int>(media_tile::kTextLeft), static_cast<int>(media_tile::kTextAfterCover),
           static_cast<int>(media_tile::kTextRight), static_cast<int>(media_tile::kSubtitleGap),
           static_cast<int>(media_tile::kFooterGap));
  html += text;
  append_media_font(html, "title", media_tile::title_font(true));
  append_media_font(html, "titleSmall", media_tile::title_font(false));
  append_media_font(html, "subtitle", media_tile::subtitle_font());
  {
    // A control icon is an MDI label centered in its button; its em box (the
    // 24-unit grid) starts this far below the label top.
    const lv_font_t* icon_font = FONT_MDI_ICONS;
    const int icon_px = static_cast<int>(lv_font_get_glyph_width(icon_font, tile_icon_disc::kMdiReferenceGlyph, 0));
    snprintf(text, sizeof(text), "    iconPx: %d, iconLine: %d, iconEmDy: %d,\n", icon_px,
             static_cast<int>(icon_font->line_height),
             static_cast<int>(icon_font->line_height - icon_font->base_line) - icon_px * 21 / 24);
    html += text;
  }
  html += "  });\n  </script>\n";
}

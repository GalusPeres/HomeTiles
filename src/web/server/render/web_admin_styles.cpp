#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/core/config/config_manager.h"
#include "src/web/server/render/web_admin_styles.h"
#include "src/web/server/assets/web_admin_assets.h"
#include "src/types/climate/layout.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/core/config/tile_radius.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/tabular_digits.h"
#include "src/types/clock/clock_face.h"
#include "src/types/text/text_options.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/screensaver/screensaver_tile_shadow.h"
#include "src/ui/tabs/settings/settings_style.h"
#include "src/ui/tabs/tiles/home_bar.h"

namespace {

constexpr int kPreviewTargetHeight = 430;
constexpr int kWideFourRowPreviewTargetHeight = 390;

int preview_target_height_px() {
  // At equal height, the Tab5 (7x4) preview would be wider than the 8-inch
  // preview (7x5). A 390 px height gives both seven-column profiles the same
  // preview width and leaves room for a consistent settings panel.
  if (GRID_COLS == 7 && GRID_ROWS == 4) return kWideFourRowPreviewTargetHeight;
  return kPreviewTargetHeight;
}

int settings_panel_target_width_px() {
  return 390;
}

int admin_wrapper_target_width_px() {
  // Every device gets the same 1200 px page; four-column previews keep their
  // size with the Settings slot beside them (admin.css .compact-grid).
  return 1200;
}

// The preview is the panel's screen scaled down exactly: cells, gaps and
// margins keep the device proportions, so every tile, half-height tile and
// span has the device's shape (user 2026-10-02: the fixed 10 px editor gap
// and 12 px margin made half-height tiles a pixel lower and wide tiles wider
// than on the panel). The cell height stays a whole pixel; every other
// length follows its scale.
int preview_cell_h_px() {
  // The screen fills the target height (the black bezel around it is gone,
  // user 2026-10-10), so the preview keeps its size and the settings panel
  // beside it fits.
  const int screen_h = preview_target_height_px();
  // One scale for every layout (the layout window draws them side by side):
  // an upright screen's preview is taller.
  const int cell = (screen_h * GRID_CELL_H + Device::kScreenHeight / 2) / Device::kScreenHeight;
  return (cell < 40) ? 40 : cell;
}

int preview_scaled_exact_px(int lvgl_px) {
  const int cell_h = preview_cell_h_px();
  const int scaled = (lvgl_px * cell_h + (GRID_CELL_H / 2)) / GRID_CELL_H;
  return scaled < 1 ? 1 : scaled;
}

// Home's page fields beside a square or upright screen (page-fields.js
// pageFieldRoom: two cells, three gaps and 14 px beside its 1 px frame):
// the page widens by what Home's grid needs beyond the 1200 px page, so the
// settings panel keeps its width on every tab (user 2026-10-13: on the S3 it
// shrank to 312 px). The page's frame (admin.css .wrapper and .card padding
// and border) takes 106 px, the editor gap 24.
int admin_page_extra_px() {
  if (!grid_layout::head_bar() || grid_layout::screen_w() > grid_layout::screen_h()) return 0;
  const double scale = static_cast<double>(preview_cell_h_px()) / GRID_CELL_H;
  const double fields = 2.0 * (GRID_CELL_W + GRID_GAP) * scale + GRID_GAP * scale + 14.0;
  const double grid = grid_layout::screen_w() * scale + 2.0 + fields;
  const int needed = static_cast<int>(ceil(grid)) + 24 + settings_panel_target_width_px() + 106;
  const int extra = needed - admin_wrapper_target_width_px();
  return extra > 0 ? extra : 0;
}

}  // namespace

namespace {

// Nominal UI font sizes and the LVGL font each layout renders for them. The
// preview variables describe the rendered font, not the requested size.
struct PreviewFontSize {
  uint8_t nominal;
  uint8_t rendered;
};
#if defined(DEVICE_LAYOUT_1024X600)
// The compact layout's real LVGL font substitutions.
constexpr PreviewFontSize kPreviewFontSizes[] = {
    {16, 16}, {20, 16}, {24, 20}, {28, 24}, {32, 28}, {40, 32},
    {48, 40}, {56, 48}, {64, 56}, {72, 56}, {80, 64}, {96, 80}};
constexpr int kPreviewIconSize = 40;
constexpr int kScreensaverClockShadow[] = {2, 3, 5};
#elif defined(DEVICE_LAYOUT_480X480)
// Native 2/3 font and icon assets used by the 480x480 target.
constexpr PreviewFontSize kPreviewFontSizes[] = {
    {16, 12}, {20, 14}, {24, 16}, {28, 20}, {32, 20}, {40, 28},
    {48, 32}, {56, 40}, {64, 40}, {72, 48}, {80, 56}, {96, 64}};
constexpr int kPreviewIconSize = 32;
constexpr int kScreensaverClockShadow[] = {1, 3, 4};
#else
constexpr PreviewFontSize kPreviewFontSizes[] = {
    {16, 16}, {20, 20}, {24, 24}, {28, 28}, {32, 32}, {40, 40},
    {48, 48}, {56, 56}, {64, 64}, {72, 72}, {80, 80}, {96, 96}};
constexpr int kPreviewIconSize = 48;  // FONT_MDI_ICONS = mdi_icons_48
constexpr int kScreensaverClockShadow[] = {2, 4, 6};
#endif

// Scale LVGL pixels by the ratio of preview cell height to display cell
// height so preview fonts match the display geometry.
int preview_scaled_px(int lvgl_px) {
  const int cell_h = preview_cell_h_px();
  int v = (lvgl_px * cell_h + (GRID_CELL_H / 2)) / GRID_CELL_H;
  return (v < 6) ? 6 : v;
}

// The preview's sizes for the shown grid under `selector`: the page's own
// (:root, with the screensaver grid's), or another layout's for the layout
// window (.layout-vars-<key>).
void appendPreviewScaleVars(String& html, const char* selector = ":root", bool page = true) {
  auto emit = [&html](const char* name, int lvgl_px) {
    html += "--";
    html += name;
    html += ":";
    html += String(preview_scaled_px(lvgl_px));
    html += "px;";
  };
  auto emit_exact = [&html](const char* name, int lvgl_px) {
    html += "--";
    html += name;
    html += ":";
    html += String(preview_scaled_exact_px(lvgl_px));
    html += "px;";
  };
  auto emit_device_px = [&html](const char* name, int device_px) {
    html += "--";
    html += name;
    html += ":";
    html += String(device_px);
    html += "px;";
  };
  // Fractional pixels, for line boxes and offsets that rounding would move.
  auto emit_fraction = [&html](const char* name, float px) {
    char value[48];
    snprintf(value, sizeof(value), "--%s:%.2fpx;", name, static_cast<double>(px));
    html += value;
  };
  auto emit_scaled = [&emit_fraction](const char* name, float lvgl_px) {
    emit_fraction(name, lvgl_px * preview_cell_h_px() / GRID_CELL_H);
  };
  html += "  <style>";
  html += selector;
  html += "{";
  emit_scaled("compact-inset", compact_sensor_layout::inset());
  // Corner circles keep the bar pill's distance to the card edge
  // (tile_icon_disc::shrink_corner_disc): drawn smaller around their middle.
  emit_scaled("corner-disc-shrink", tile_icon_disc::corner_shrink());
  emit_exact("icon-disc-round", tile_icon_disc::round_diameter());
  // The fixed Circle strength (user 2026-10-11: no circle settings);
  // applyIconDiscTint derives every circle from it like the device.
  const uint8_t glow = ui_surface_style::icon_glow_percent();
  html += "--icon-glow-pct:";
  html += String(glow);
  html += ";";
  {
    // The neutral circle over the global tile color, for tiles the script
    // does not tint (the parked Settings tile); transparent at 0 %.
    const tone_color::Fill fill = tone_color::fill(tileDefaultBgColor(), 0xFFFFFF, false, glow);
    char disc[48];
    snprintf(disc, sizeof(disc), "--icon-disc-bg:rgba(%u,%u,%u,%.3f);",
             static_cast<unsigned>((fill.disc_color >> 16) & 0xFF), static_cast<unsigned>((fill.disc_color >> 8) & 0xFF),
             static_cast<unsigned>(fill.disc_color & 0xFF), fill.disc_opa / 255.0f);
    html += disc;
  }
  // Unrounded: emit_exact never goes below 1 px, which split a 0 px device
  // gap into half a pixel up for the title and down for the value.
  emit_scaled("compact-text-gap", compact_sensor_layout::text_gap());
  // Half-height texts: unrounded sizes and line boxes, and the shift that
  // puts each glyph baseline where LVGL draws it (the line box centers the
  // glyphs; LVGL puts the baseline base_line above the line bottom, as
  // --ldyNN). Rounded values left the 28 px value about 4 display px low
  // (user 2026-10-02).
  // The page measures the browser baseline and replaces each shift
  // (text-baseline.js); the LVGL baseline below the line top is the base.
  auto emit_compact_line = [&emit_scaled](const char* font_name, const char* line_name, const char* dy_name,
                                          const char* base_name, const lv_font_t* font, int size) {
    emit_scaled(font_name, size);
    emit_scaled(line_name, font->line_height);
    emit_scaled(dy_name, font->line_height / 2.0f - font->base_line - 0.364f * size);
    emit_scaled(base_name, font->line_height - font->base_line);
  };
  emit_compact_line("compact-title-font", "compact-title-line", "compact-title-dy", "compact-title-base",
                    compact_sensor_layout::title_font(), compact_sensor_layout::title_size());
  emit_compact_line("compact-value-font", "compact-value-line", "compact-value-dy", "compact-value-base",
                    compact_sensor_layout::value_font(), compact_sensor_layout::value_size());
  emit_exact("compact-value-line-20", tile_layout::content_font_20()->line_height);
  emit_exact("compact-value-line-24", tile_layout::content_font_24()->line_height);
  emit_exact("compact-value-line-32", tile_layout::content_font_32()->line_height);
  emit_exact("compact-value-line-40", tile_layout::content_font_40()->line_height);
  // Chosen half-height value sizes (compact_sensor_layout::value_font).
  emit_compact_line("compact-value-font-24", "compact-value-line-step-24", "compact-value-dy-24",
                    "compact-value-base-24", compact_sensor_layout::value_font(2),
                    compact_sensor_layout::value_size(2));
  emit_compact_line("compact-value-font-28", "compact-value-line-step-28", "compact-value-dy-28",
                    "compact-value-base-28", compact_sensor_layout::value_font(5),
                    compact_sensor_layout::value_size(5));
  // The line tops as compact_sensor_layout places them (whole device pixels).
  auto emit_compact_tops = [&emit_scaled](const char* title_name, const char* value_name, uint8_t choice) {
    const int top = compact_sensor_layout::text_top(true, choice);
    emit_scaled(title_name, top);
    emit_scaled(value_name, top + compact_sensor_layout::title_font()->line_height +
                                compact_sensor_layout::text_gap());
  };
  emit_compact_tops("compact-title-top", "compact-value-top", 0);
  emit_compact_tops("compact-title-top-24", "compact-value-top-24", 2);
  emit_compact_tops("compact-title-top-28", "compact-value-top-28", 5);
  emit_scaled("compact-title-only-top", compact_sensor_layout::text_top(false));
  emit_scaled("compact-row", compact_sensor_layout::header_height());

  for (const PreviewFontSize& size : kPreviewFontSizes) {
    char name[24];
    snprintf(name, sizeof(name), "fs%u", static_cast<unsigned>(size.nominal));
    emit(name, size.rendered);
  }
  emit("icon-size", kPreviewIconSize);
  for (const PreviewFontSize& size : kPreviewFontSizes) {
    if (size.nominal < 20) continue;
    char name[32];
    snprintf(name, sizeof(name), "screensaver-fs%u", static_cast<unsigned>(size.nominal));
    emit_device_px(name, size.rendered);
  }
  emit_device_px("screensaver-clock-gap", tile_layout::scale(6));
  // The clock tile's analog face (clock_face.h, clock/renderer.cpp
  // clock_face_layout) in device pixels.
  emit_device_px("clock-face-m", tile_layout::scale(clock_face::kTileMargin));
  emit_device_px("clock-face-m-compact", tile_layout::scale(clock_face::kCompactMargin));
  emit_device_px("clock-face-gap", tile_layout::scale(clock_face::kCompactGap));
  emit_device_px("clock-face-header", tile_icon_disc::row_height());
  // The screensaver tile shadow: LVGL blurs over the shadow width, CSS over
  // twice its blur radius.
  emit_scaled("screensaver-tile-shadow-blur", screensaver_tile_shadow::kWidth / 2.0f);
  emit_scaled("screensaver-tile-shadow-spread", screensaver_tile_shadow::kSpread);
  {
    char opacity[48];
    snprintf(opacity, sizeof(opacity), "--screensaver-tile-shadow-opa:%.3f;",
             screensaver_tile_shadow::kOpa / 255.0);
    html += opacity;
  }
  emit_device_px("screensaver-shadow-2", kScreensaverClockShadow[0]);
  emit_device_px("screensaver-shadow-4", kScreensaverClockShadow[1]);
  emit_device_px("screensaver-shadow-6", kScreensaverClockShadow[2]);
  // A label is as tall as its font's line height. CSS centers the glyphs in a
  // line box of that height; --ldy moves them onto the LVGL baseline. Inter's
  // CSS baseline lies 0.364 em below the box middle (ascent 0.969 em, content
  // 1.210 em, measured in Chrome), so only the digit-only 80 and 96 fonts with
  // their short line heights are off by more than a device pixel.
  for (const PreviewFontSize& size : kPreviewFontSizes) {
    const lv_font_t* font = ui_font_for_size(size.rendered);
    const float shift = font->line_height / 2.0f - font->base_line - 0.364f * size.rendered;
    char name[32];
    snprintf(name, sizeof(name), "lh%u", static_cast<unsigned>(size.nominal));
    emit_scaled(name, font->line_height);
    snprintf(name, sizeof(name), "ldy%u", static_cast<unsigned>(size.nominal));
    emit_scaled(name, shift);
    // The LVGL baseline below the line top; text-baseline.js measures the
    // browser one and replaces --ldy with the exact difference.
    snprintf(name, sizeof(name), "lb%u", static_cast<unsigned>(size.nominal));
    emit_scaled(name, font->line_height - font->base_line);
    if (size.nominal < 20) continue;
    // The panel's width of the clock sample "88:88 PM" in this font
    // (clock/renderer.cpp clock_text_width): the preview scales its browser
    // widths by it, so a clock line wraps exactly where the panel's does
    // (user 2026-10-11: "8:01 PM" wrapped only in the preview).
    lv_point_t sample{};
    // With the clock's fixed-width digits (tabular_digits).
    lv_text_get_size(&sample, "88:88 PM", tabular_digits::font(font), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    snprintf(name, sizeof(name), "tw%u", static_cast<unsigned>(size.nominal));
    emit_scaled(name, sample.x);
    snprintf(name, sizeof(name), "screensaver-lh%u", static_cast<unsigned>(size.nominal));
    emit_device_px(name, font->line_height);
    snprintf(name, sizeof(name), "screensaver-ldy%u", static_cast<unsigned>(size.nominal));
    emit_fraction(name, shift);
    snprintf(name, sizeof(name), "screensaver-lb%u", static_cast<unsigned>(size.nominal));
    emit_device_px(name, font->line_height - font->base_line);
  }
  emit("tile-pad-v", climate_layout::kCardPaddingVertical);
  emit("tile-pad-h", climate_layout::kCardPaddingHorizontal);
  // Clock tile (types/clock/renderer.cpp): the gap between its lines, the
  // shift of the clock below a title or icon and the half-height side pad.
  emit_scaled("clock-gap", tile_layout::scale(6));
  emit_scaled("clock-header-shift", tile_layout::scale(18));
  emit_scaled("clock-compact-pad", tile_layout::scale_480(8));
  // Clock and Text headers: the title top left, the icon top right with its
  // disc, which lifts the header until its top and side gaps match
  // (tile_icon_disc::add_round). Text cards use their own padding and center
  // the text 12 px lower below a header (types/text/renderer.cpp).
  auto emit_right_header = [&](const char* prefix, int pad_top, int pad_side) {
    const int icon_x = tile_layout::scale_480(4);
    const int icon_y = tile_layout::scale_480(-8);
    const int lift = tile_icon_disc::corner_lift(
        pad_top, pad_side, -icon_x, icon_y,
        lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0),
        lv_font_get_line_height(FONT_MDI_ICONS), tile_icon_disc::round_diameter());
    char name[48];
    snprintf(name, sizeof(name), "%s-icon-top", prefix);
    emit_scaled(name, pad_top + icon_y - lift);
    snprintf(name, sizeof(name), "%s-icon-right", prefix);
    emit_scaled(name, pad_side - icon_x);
    snprintf(name, sizeof(name), "%s-title-top", prefix);
    emit_scaled(name, pad_top + tile_layout::scale_480(4) - lift);
    snprintf(name, sizeof(name), "%s-pad-v", prefix);
    emit_scaled(name, pad_top);
    snprintf(name, sizeof(name), "%s-pad-h", prefix);
    emit_scaled(name, pad_side);
  };
  emit_right_header("clock-header", tile_layout::scale_480(24), tile_layout::scale_480(20));
  emit_right_header("text-header", tile_layout::scale_480(16), tile_layout::scale_480(18));
  emit_scaled("text-header-shift", tile_layout::scale(12));
  // The Text tile's alignment (types/text/text_options.h): the header row a
  // top-aligned text starts below, in device pixels, and a half tile's
  // padding above and below.
  emit_device_px("text-header-h", tile_icon_disc::row_height());
  emit_scaled("text-compact-pad-v", tile_layout::scale_480(text_tile::kCompactPadV480));
  // The device places a corner header's disc in the tile corner like the
  // half-height disc and centers the icon in it; the header labels move with
  // the icon (tile_icon_disc::corner_header). The preview header follows.
  const tile_icon_disc::CornerHeader header = tile_icon_disc::corner_header(
      tile_layout::scale_480(24), tile_layout::scale_480(20), tile_layout::scale_480(-8),
      lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0),
      lv_font_get_line_height(FONT_MDI_ICONS));
  // Unrounded: the disc sits at the half-height inset from the corner like
  // on the device; rounded sizes and icon positions put it up to 1.5 px too
  // close to the corner on the S3 and 4B, cut by the card corner (user
  // 2026-10-02).
  emit_scaled("icon-disc-corner", header.disc);
  // An icon over its title (tile_icon_disc::icon_title_offsets): the icon's
  // and the title's middle from the card's middle.
  {
    const tile_icon_disc::IconTitleOffsets offsets =
        tile_icon_disc::icon_title_offsets(FONT_MDI_ICONS, tile_layout::header_title_font());
    emit_scaled("icon-title-icon-dy", offsets.icon);
    emit_scaled("icon-title-title-dy", offsets.title);
  }
  emit_exact("tile-header-title-top",
             tile_layout::scale_480(24) + tile_layout::scale_480(4) + header.shift);
  emit_exact("tile-header-title-right", tile_layout::scale_480(20) - tile_layout::scale_480(4));
  emit_scaled("tile-header-icon-top", tile_layout::scale_480(24) + header.icon_top);
  emit_scaled("tile-header-icon-left", tile_layout::scale_480(20) + header.icon_side);
  // The title's distance to the end of its pill over a Camera tile's picture
  // (types/camera/picture.cpp show_head).
  emit_scaled("camera-title-pad", tile_layout::scale_480(12));
#if defined(DEVICE_LAYOUT_1024X600)
  emit("value-dy", 23);
#elif defined(DEVICE_LAYOUT_480X480)
  emit("value-dy", 19);
#else
  emit("value-dy", 28);
#endif
  {
    // The stored global default tile color (the dark theme's card). Tiles
    // without their own color paint with --tile-card, which is this color in
    // the dark theme (appendPanelThemeVars), so a change of it repaints every
    // preview grid at once.
    char color_hex[8];
    // The fixed dark card (user 2026-10-11: no global tile color).
    snprintf(color_hex, sizeof(color_hex), "#%06X", static_cast<unsigned>(tile_color::kDefault));
    html += "--tile-default-bg:";
    html += color_hex;
    html += ";";
  }
  // Unrounded: the corner disc (tile radius minus the inset) is concentric
  // with the card corner only at the exact device radius; a whole-pixel
  // radius moved the corner gap up to 0.23 px (user 2026-10-02).
  // The fixed radius (user 2026-10-11: no global radius setting).
  emit_scaled("tile-radius", tile_radius::kDefault);
  html += "--radius-preview-scale:";
  html += String(static_cast<double>(preview_cell_h_px()) / GRID_CELL_H, 8);
  html += ";--tile-radius-device:";
  html += String(tile_radius::kDefault);
  html += ";";
  // Climate tile geometry uses the exact same LVGL-to-preview scale as the
  // device. Keeping these separate from font variables avoids the 6 px
  // minimum used for readable preview text.
  emit_exact("climate-margin-x", climate_layout::kOuterInset);
  emit_exact("climate-grid-gap", climate_layout::kGap);
  emit_exact("climate-slots-top",
             climate_layout::content_top(header.disc, tile_icon_disc::inset()));
  emit_exact("climate-slots-bottom", climate_layout::kOuterInset);
  html += "--climate-control-radius:max(0px,calc(var(--tile-radius) - var(--climate-margin-x)));";
  emit_exact("climate-control-side-pad", tile_layout::scale_480(8));
  emit_exact("climate-control-caption-w", tile_layout::scale_480(96));
  emit_exact("climate-control-button-w", tile_layout::scale_480(40));
  emit_exact(
      "climate-control-single-w",
      GRID_CELL_W - climate_layout::kOuterInset * 2);
  emit_exact("climate-control-v-pad-top", tile_layout::scale_480(5));
  emit_exact("climate-control-v-pad-bottom", tile_layout::scale_480(5));
  {
    // Switch tile header layouts (types/switch/renderer.cpp): the control bar
    // one Climate gap below the corner disc, as high as in a one-row tile,
    // at the card's bottom. Title and state use the compact variables.
    emit_exact("switch-bar-height",
               GRID_CELL_H - climate_layout::kOuterInset -
                   climate_layout::content_top(header.disc, tile_icon_disc::inset()));
  }
  html += "--settings-panel-width:";
  html += String(settings_panel_target_width_px());
  html += "px;";
  html += "--admin-wrapper-width:";
  html += String(admin_wrapper_target_width_px());
  html += "px;--admin-page-extra:";
  html += String(admin_page_extra_px());
  html += "px;";
  // The shown grid (grid_layout.h): with the head bar its fewer, larger
  // cells; a stored tile outside it sits beside the screen (tile-outside).
  html += "--grid-cols:";
  html += String(GRID_SHOWN_COLS);
  html += ";--grid-rows:";
  html += String(GRID_SHOWN_ROWS);
  // An upright layout's half row: one more track half a cell high
  // (admin.css .tile-grid grid-template-rows).
  if (grid_layout::shown().half_row) {
    html += ";--grid-half-track:calc((var(--preview-cell-h) - var(--preview-gap)) / 2)";
  }
  html += ";--preview-cell-h:";
  html += String(preview_cell_h_px());
  html += "px;";
  emit_scaled("preview-cell-w", GRID_CELL_W);
  emit_scaled("preview-gap", GRID_GAP);
  emit_scaled("preview-pad", grid_layout::kEdgePad);
  // The tracks rarely fill the screen exactly; the rest sits in the margins
  // (tile_config.h GRID_PAD_*).
  emit_scaled("preview-pad-left", GRID_PAD_LEFT);
  emit_scaled("preview-pad-right", GRID_PAD_RIGHT);
  emit_scaled("preview-pad-top", GRID_PAD_TOP);
  emit_scaled("preview-pad-bottom", GRID_PAD_BOTTOM);
  {
    // The wallpaper fills the whole screen with corners of the tile radius
    // plus the tiles' distance to the edge (image_screensaver image_radius()).
    char radius[96];
    snprintf(radius, sizeof(radius), "--screensaver-image-radius:calc(var(--tile-radius) + %.2fpx);",
             static_cast<double>(grid_layout::kEdgePad * preview_cell_h_px()) / GRID_CELL_H);
    html += radius;
  }
  html += "}";
  html += "</style>\n";
}

}  // namespace

static void appendHeadBarVars(String& html);

// The panel's color roles for both themes (ui_theme.h, the one place they are
// set) and the light theme's step and tint factors: the preview CSS paints
// with --pv-*, the preview script mirrors tone_color and tile_tint with them.
// html[data-panel-theme] picks the theme; switching it repaints the preview.
static void appendPanelThemeVars(String& html) {
  // `prefix` "pv" is the page's theme; "pvd" and "pvl" keep both tables for
  // a tile on the other theme's surface (tile_surface, grid-preview.js).
  auto emit_roles = [&html](const ui_theme::Roles& roles, const char* prefix) {
    const struct {
      const char* name;
      uint32_t rgb;
    } entries[] = {
        {"screen", roles.screen},       {"card", roles.card},
        {"text", roles.text},           {"icon", roles.icon},
        {"icon-off", roles.icon_off},   {"icon-inactive", roles.icon_inactive},
        {"icon-rest", roles.icon_rest}, {"text-secondary", roles.text_secondary},
        {"text-soft", roles.text_soft}, {"icon-unavailable", roles.icon_unavailable},
    };
    char value[48];
    for (const auto& entry : entries) {
      snprintf(value, sizeof(value), "--%s-%s:#%06X;", prefix, entry.name, static_cast<unsigned>(entry.rgb));
      html += value;
    }
    snprintf(value, sizeof(value), "--%s-text-rgb:%u,%u,%u;", prefix, static_cast<unsigned>((roles.text >> 16) & 0xFF),
             static_cast<unsigned>((roles.text >> 8) & 0xFF), static_cast<unsigned>(roles.text & 0xFF));
    html += value;
  };
  html += "<style>:root{";
  emit_roles(ui_theme::kDark, "pv");
  emit_roles(ui_theme::kDark, "pvd");
  emit_roles(ui_theme::kLight, "pvl");
  emit_roles(ui_theme::kDarkTinted, "pvj");
  // The dark theme's tile card is the global tile color; a black tile stays black.
  html += "--tile-card:var(--tile-default-bg);--pv-black-tile:#000000;";
  char factors[448];
  // The filled tile of the light theme (ui_theme kJewel*, tone_color::jewel_*).
  snprintf(factors, sizeof(factors),
           "--pv-light-step:%.6f;--pv-light-tint:%.6f;--pv-light-surface:%.3f;--pv-light-circle:%.3f;"
           "--pv-jewel-below:%.3f;--pv-jewel-max:%.3f;--pv-jewel-min:%.3f;--pv-jewel-chroma:%.3f;--pv-jewel-card-share:%.3f;"
           "--pv-jewel-step:%.3f;--pv-jewel-circle-share:%.3f;--pv-jewel-circle-cap:%.3f;--pv-jewel-neutral:%.3f;--pv-jewel-idle:%.3f;",
           static_cast<double>(ui_theme::kLightStepNum) / ui_theme::kLightStepDen,
           static_cast<double>(ui_theme::kLightTintNum) / ui_theme::kLightTintDen,
           static_cast<double>(ui_theme::kLightSurfaceL), static_cast<double>(ui_theme::kLightCircleShare),
           static_cast<double>(ui_theme::kJewelBelowIcon), static_cast<double>(ui_theme::kJewelCardMaxL),
           static_cast<double>(ui_theme::kJewelCardMinL),
           static_cast<double>(ui_theme::kJewelChroma), static_cast<double>(ui_theme::kJewelCardChromaShare),
           static_cast<double>(ui_theme::kJewelCircleStep), static_cast<double>(ui_theme::kJewelCircleChromaShare),
           static_cast<double>(ui_theme::kJewelCircleChromaCap), static_cast<double>(ui_theme::kJewelNeutralL), static_cast<double>(ui_theme::kJewelIdleL));
  html += factors;
  html += "}html[data-panel-theme=\"light\"]{";
  emit_roles(ui_theme::kLight, "pv");
  // The light theme keeps its light card (tileDefaultBgColor) and shows a
  // black tile in the screen color (tileBgColorOrDefault).
  char card[24];
  snprintf(card, sizeof(card), "--tile-card:#%06X;", static_cast<unsigned>(ui_theme::kLight.card));
  html += card;
  html += "--pv-black-tile:var(--pv-screen);}</style>\n";
}

void appendAdminStyles(String& html) {
  appendPanelThemeVars(html);
  appendPreviewScaleVars(html, ":root", true);
  // The layout window shows the other layouts with their own sizes (cells,
  // circles, text places): the same variables per layout. The page is built
  // on the loop task like the UI, and nothing here draws, so the shown grid
  // is switched for these moments only.
  const grid_layout::Shown shown = grid_layout::g_shown;
  for (uint8_t i = 0; i < grid_layout::kLayoutCount; ++i) {
    const grid_layout::Layout layout = grid_layout::from_index(i);
    if (layout == shown.layout || !grid_layout::available(layout)) continue;
    grid_layout::g_shown = grid_layout::layout_grid(layout);
    appendPreviewScaleVars(html, (String(".layout-vars-") + grid_layout::key(layout)).c_str(), false);
  }
  grid_layout::g_shown = shown;
  // Always: the layout window draws the head for the bar layouts.
  appendHeadBarVars(html);
  html += R"html(
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@mdi/font@7.4.47/css/materialdesignicons.min.css">
  <link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'%3E%3Crect width='48' height='48' rx='10' fill='%2316181c'/%3E%3Crect x='4' y='4' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Crect x='27' y='4' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Crect x='4' y='27' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Cpath d='M33 26h5v6.5h6.5v5H38V44h-5v-6.5h-6.5v-5H33z' fill='%2326a69a'/%3E%3C/svg%3E">
  <link rel="stylesheet" href=")html";
  html += adminCssAssetPath();
  html += R"html(">
)html";
}

// The head bar's head (grid_layout.h, home_bar.cpp) over the preview grid's
// top margin, in the device's places, and its circle like the Settings gear's.
static void appendHeadBarVars(String& html) {
  auto emit_scaled = [&html](const char* name, float lvgl_px) {
    char value[64];
    snprintf(value, sizeof(value), "--%s:%.2fpx;", name,
             static_cast<double>(lvgl_px * preview_cell_h_px() / GRID_CELL_H));
    html += value;
  };
  const home_bar::Geometry head = home_bar::geometry();
  html += "  <style>:root{";
  emit_scaled("head-screen-w", grid_layout::screen_w());
  emit_scaled("head-top", grid_layout::layout_grid(grid_layout::Layout::kBar).pad_top);
  emit_scaled("head-disc", head.disc);
  emit_scaled("head-disc-x", head.disc_x);
  emit_scaled("head-cy", head.center_y);
  emit_scaled("head-close", head.close);
  emit_scaled("head-close-x", head.close_x);
  emit_scaled("head-title-x", head.title_x);
  emit_scaled("head-title-w", head.title_w);
  emit_scaled("head-time-x", head.time_x);
  emit_scaled("head-time-x-alone", head.time_x_alone);
  emit_scaled("head-time-w", head.time_w);
  emit_scaled("head-line", head.line);
  emit_scaled("head-time-line", head.time_line);
  // The head font's size (popup_layout::headerTitleFont), the time's
  // (home_bar::time_font) and the icons'.
#if defined(DEVICE_LAYOUT_1024X600) || defined(DEVICE_LAYOUT_480X480)
  emit_scaled("head-font", 16);
  emit_scaled("head-time-font", 20);
#else
  emit_scaled("head-font", 24);
  emit_scaled("head-time-font", 32);
#endif
  emit_scaled("head-icon", kPreviewIconSize);
  // The page dots (home_bar::dot_sizes, dot_color).
  const home_bar::DotSizes dots = home_bar::dot_sizes();
  emit_scaled("head-dot", dots.dot);
  emit_scaled("head-dot-active", dots.active);
  emit_scaled("head-dot-gap", dots.gap);
  char color[96];
  snprintf(color, sizeof(color), "--head-disc-color:#%06X;--head-dot-on:#%06X;--head-dot-off:#%06X;",
           static_cast<unsigned>(settings_style::tone(ui_theme::light() ? ui_theme::screen() : tileDefaultBgColor(),
                                                      settings_style::kGearColor).disc),
           static_cast<unsigned>(home_bar::dot_color(true)), static_cast<unsigned>(home_bar::dot_color(false)));
  html += color;
  html += "}</style>\n";
}

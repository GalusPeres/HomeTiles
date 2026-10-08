#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/core/config/config_manager.h"
#include "src/web/server/render/web_admin_styles.h"
#include "src/web/server/assets/web_admin_assets.h"
#include "src/types/climate/layout.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/screensaver/screensaver_tile_shadow.h"
#include "src/ui/tabs/settings/settings_style.h"
#include "src/ui/tabs/tiles/home_bar.h"

namespace {

constexpr int kPreviewTargetHeight = 430;
constexpr int kWideFourRowPreviewTargetHeight = 390;
// A black bezel around the scaled screen; not part of the device geometry
// (user 2026-10-02: keep the black frame of the former 12 px margin).
constexpr int kPreviewFramePx = 10;

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
  // The screen fills the target height inside the bezel, so the preview
  // keeps its former size and the settings panel beside it fits.
  const int screen_h = preview_target_height_px() - 2 * kPreviewFramePx;
  const int cell = (screen_h * GRID_CELL_H + Device::kScreenHeight / 2) / Device::kScreenHeight;
  return (cell < 40) ? 40 : cell;
}

int preview_scaled_exact_px(int lvgl_px) {
  const int cell_h = preview_cell_h_px();
  const int scaled = (lvgl_px * cell_h + (GRID_CELL_H / 2)) / GRID_CELL_H;
  return scaled < 1 ? 1 : scaled;
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
  // The global Circle strength; applyIconDiscTint derives every circle
  // from it like the device (tone_color.h).
  const uint8_t glow = icon_glow::clamp(configManager.getConfig().icon_glow);
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
  emit_exact("tile-header-title-top",
             tile_layout::scale_480(24) + tile_layout::scale_480(4) + header.shift);
  emit_exact("tile-header-title-right", tile_layout::scale_480(20) - tile_layout::scale_480(4));
  emit_scaled("tile-header-icon-top", tile_layout::scale_480(24) + header.icon_top);
  emit_scaled("tile-header-icon-left", tile_layout::scale_480(20) + header.icon_side);
#if defined(DEVICE_LAYOUT_1024X600)
  emit("value-dy", 23);
#elif defined(DEVICE_LAYOUT_480X480)
  emit("value-dy", 19);
#else
  emit("value-dy", 28);
#endif
  {
    // Tiles without their own color paint with this variable, so a change of
    // the global default tile color repaints every preview grid at once.
    char color_hex[8];
    snprintf(color_hex, sizeof(color_hex), "#%06X",
             static_cast<unsigned>(tileDefaultBgColor()));
    html += "--tile-default-bg:";
    html += color_hex;
    html += ";";
  }
  // Unrounded: the corner disc (tile radius minus the inset) is concentric
  // with the card corner only at the exact device radius; a whole-pixel
  // radius moved the corner gap up to 0.23 px (user 2026-10-02).
  emit_scaled("tile-radius", configManager.getConfig().tile_radius);
  html += "--radius-preview-scale:";
  html += String(static_cast<double>(preview_cell_h_px()) / GRID_CELL_H, 8);
  html += ";--tile-radius-device:";
  html += String(configManager.getConfig().tile_radius);
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
  html += "px;";
  // The shown grid (grid_layout.h): with the head bar its fewer, larger
  // cells; a stored tile outside it sits beside the screen (tile-outside).
  html += "--grid-cols:";
  html += String(GRID_SHOWN_COLS);
  html += ";--grid-rows:";
  html += String(GRID_SHOWN_ROWS);
  html += ";--preview-cell-h:";
  html += String(preview_cell_h_px());
  html += "px;";
  emit_scaled("preview-cell-w", GRID_CELL_W);
  emit_scaled("preview-gap", GRID_GAP);
  emit_scaled("preview-pad", GRID_PAD);
  // The tracks rarely fill the screen exactly; the rest sits in the margins
  // (tile_config.h GRID_PAD_*).
  emit_scaled("preview-pad-left", GRID_PAD_LEFT);
  emit_scaled("preview-pad-right", GRID_PAD_RIGHT);
  emit_scaled("preview-pad-top", GRID_PAD_TOP);
  emit_scaled("preview-pad-bottom", GRID_PAD_BOTTOM);
  {
    // The wallpaper fills the whole screen with corners of the tile radius
    // plus the margin up to 4 px (image_screensaver image_radius()).
    char radius[160];
    snprintf(radius, sizeof(radius),
             "--preview-frame:%dpx;--screensaver-image-inset:%dpx;"
             "--screensaver-image-radius:calc(var(--tile-radius) + %.2fpx);",
             kPreviewFramePx, kPreviewFramePx,
             static_cast<double>((GRID_PAD < 4 ? GRID_PAD : 4) * preview_cell_h_px()) / GRID_CELL_H);
    html += radius;
  }
  html += "}";
  if (page && grid_layout::head_bar()) {
    // The screensaver tab keeps the profile's grid, where its tiles are
    // stored; the panel shows them in the bottom rows of the head bar's grid
    // (image_screensaver.cpp).
    const grid_layout::Shown profile = grid_layout::profile_grid();
    html += ".screensaver-tile-grid{--grid-cols:";
    html += String(profile.cols);
    html += ";--grid-rows:";
    html += String(profile.rows);
    html += ";";
    emit_scaled("preview-cell-w", profile.cell_w);
    emit_scaled("preview-cell-h", profile.cell_h);
    emit_scaled("preview-pad-left", profile.pad_left);
    emit_scaled("preview-pad-right", profile.pad_right);
    emit_scaled("preview-pad-top", profile.pad_top);
    emit_scaled("preview-pad-bottom", profile.pad_bottom);
    html += "}";
  }
  html += "</style>\n";
}

}  // namespace

static void appendHeadBarVars(String& html);

void appendAdminStyles(String& html) {
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
  emit_scaled("head-screen-w", SCREEN_WIDTH);
  emit_scaled("head-top", grid_layout::layout_grid(grid_layout::Layout::kBar).pad_top);
  emit_scaled("head-disc", head.disc);
  emit_scaled("head-disc-x", head.disc_x);
  emit_scaled("head-cy", head.center_y);
  emit_scaled("head-close", head.close);
  emit_scaled("head-close-x", head.close_x);
  emit_scaled("head-title-x", head.title_x);
  emit_scaled("head-title-w", head.title_w);
  emit_scaled("head-time-x", head.time_x);
  emit_scaled("head-time-w", head.time_w);
  emit_scaled("head-line", head.line);
  // The head font's size (popup_layout::headerTitleFont) and the icons'.
#if defined(DEVICE_LAYOUT_1024X600) || defined(DEVICE_LAYOUT_480X480)
  emit_scaled("head-font", 16);
#else
  emit_scaled("head-font", 24);
#endif
  emit_scaled("head-icon", kPreviewIconSize);
  char color[40];
  snprintf(color, sizeof(color), "--head-disc-color:#%06X;",
           static_cast<unsigned>(settings_style::tone(tileDefaultBgColor(), settings_style::kGearColor).disc));
  html += color;
  html += "}</style>\n";
}

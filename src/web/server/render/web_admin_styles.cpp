#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/core/config/config_manager.h"
#include "src/web/server/render/web_admin_styles.h"
#include "src/web/server/assets/web_admin_assets.h"
#include "src/types/climate/layout.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/ui/shared/tone_color.h"

namespace {

constexpr int kPreviewTargetHeight = 430;
constexpr int kWideFourRowPreviewTargetHeight = 390;
constexpr int kPreviewPad = 12;
constexpr int kPreviewGap = 10;

int preview_pad_px() {
  return kPreviewPad;
}

int preview_gap_px() {
  return kPreviewGap;
}

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

int preview_cell_h_px() {
  const int pad = preview_pad_px();
  const int gap = preview_gap_px();
  int cell = (preview_target_height_px() - (2 * pad) - (gap * (GRID_ROWS - 1))) / GRID_ROWS;
  return (cell < 40) ? 40 : cell;
}

int preview_cell_w_px() {
  const int cell_h = preview_cell_h_px();
  int cell_w = (GRID_CELL_W * cell_h + (GRID_CELL_H / 2)) / GRID_CELL_H;
  return (cell_w < 40) ? 40 : cell_w;
}

int preview_scaled_exact_px(int lvgl_px) {
  const int cell_h = preview_cell_h_px();
  const int scaled = (lvgl_px * cell_h + (GRID_CELL_H / 2)) / GRID_CELL_H;
  return scaled < 1 ? 1 : scaled;
}

}  // namespace

namespace {

// Scale LVGL pixels by the ratio of preview cell height to display cell
// height so preview fonts match the display geometry.
int preview_scaled_px(int lvgl_px) {
  const int cell_h = preview_cell_h_px();
  int v = (lvgl_px * cell_h + (GRID_CELL_H / 2)) / GRID_CELL_H;
  return (v < 6) ? 6 : v;
}

void appendPreviewScaleVars(String& html) {
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
  html += "  <style>:root{";
  emit_exact("compact-inset", compact_sensor_layout::inset());
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
  emit_exact("compact-text-gap", compact_sensor_layout::text_gap());
  emit_exact("compact-title-font", compact_sensor_layout::title_size());
  emit_exact("compact-title-line", compact_sensor_layout::title_font()->line_height);
  emit_exact("compact-value-font", compact_sensor_layout::value_size());
  emit_exact("compact-value-line", compact_sensor_layout::value_font()->line_height);
  emit_exact("compact-value-line-20", tile_layout::content_font_20()->line_height);
  emit_exact("compact-value-line-24", tile_layout::content_font_24()->line_height);
  emit_exact("compact-value-line-32", tile_layout::content_font_32()->line_height);
  emit_exact("compact-value-line-40", tile_layout::content_font_40()->line_height);
  // Chosen half-height value sizes (compact_sensor_layout::value_font).
  emit_exact("compact-value-font-24", compact_sensor_layout::value_size(2));
  emit_exact("compact-value-line-step-24", compact_sensor_layout::value_font(2)->line_height);
  emit_exact("compact-value-font-28", compact_sensor_layout::value_size(5));
  emit_exact("compact-value-line-step-28", compact_sensor_layout::value_font(5)->line_height);

#if defined(DEVICE_LAYOUT_1024X600)
  // Match the compact layout's real LVGL font substitutions. The preview
  // variables describe the rendered font, not the originally requested size.
  emit("fs16", 16);
  emit("fs20", 16);
  emit("fs24", 20);
  emit("fs28", 24);
  emit("fs32", 28);
  emit("fs40", 32);
  emit("fs48", 40);
  emit("fs56", 48);
  emit("fs64", 56);
  emit("fs72", 56);
  emit("fs80", 64);
  emit("fs96", 80);
  emit("icon-size", 40);
  emit_device_px("screensaver-fs20", 16);
  emit_device_px("screensaver-fs24", 20);
  emit_device_px("screensaver-fs28", 24);
  emit_device_px("screensaver-fs32", 28);
  emit_device_px("screensaver-fs40", 32);
  emit_device_px("screensaver-fs48", 40);
  emit_device_px("screensaver-fs56", 48);
  emit_device_px("screensaver-fs64", 56);
  emit_device_px("screensaver-fs72", 56);
  emit_device_px("screensaver-fs80", 64);
  emit_device_px("screensaver-fs96", 80);
  emit_device_px("screensaver-shadow-2", 2);
  emit_device_px("screensaver-shadow-4", 3);
  emit_device_px("screensaver-shadow-6", 5);
#elif defined(DEVICE_LAYOUT_480X480)
  // Native 2/3 font and icon assets used by the 480x480 target.
  emit("fs16", 12);
  emit("fs20", 14);
  emit("fs24", 16);
  emit("fs28", 20);
  emit("fs32", 20);
  emit("fs40", 28);
  emit("fs48", 32);
  emit("fs56", 40);
  emit("fs64", 40);
  emit("fs72", 48);
  emit("fs80", 56);
  emit("fs96", 64);
  emit("icon-size", 32);
  emit_device_px("screensaver-fs20", 14);
  emit_device_px("screensaver-fs24", 16);
  emit_device_px("screensaver-fs28", 20);
  emit_device_px("screensaver-fs32", 20);
  emit_device_px("screensaver-fs40", 28);
  emit_device_px("screensaver-fs48", 32);
  emit_device_px("screensaver-fs56", 40);
  emit_device_px("screensaver-fs64", 40);
  emit_device_px("screensaver-fs72", 48);
  emit_device_px("screensaver-fs80", 56);
  emit_device_px("screensaver-fs96", 64);
  emit_device_px("screensaver-shadow-2", 1);
  emit_device_px("screensaver-shadow-4", 3);
  emit_device_px("screensaver-shadow-6", 4);
#else
  emit("fs16", 16);
  emit("fs20", 20);
  emit("fs24", 24);
  emit("fs28", 28);
  emit("fs32", 32);
  emit("fs40", 40);
  emit("fs48", 48);
  emit("fs56", 56);
  emit("fs64", 64);
  emit("fs72", 72);
  emit("fs80", 80);
  emit("fs96", 96);
  emit("icon-size", 48);      // FONT_MDI_ICONS = mdi_icons_48
  emit_device_px("screensaver-fs20", 20);
  emit_device_px("screensaver-fs24", 24);
  emit_device_px("screensaver-fs28", 28);
  emit_device_px("screensaver-fs32", 32);
  emit_device_px("screensaver-fs40", 40);
  emit_device_px("screensaver-fs48", 48);
  emit_device_px("screensaver-fs56", 56);
  emit_device_px("screensaver-fs64", 64);
  emit_device_px("screensaver-fs72", 72);
  emit_device_px("screensaver-fs80", 80);
  emit_device_px("screensaver-fs96", 96);
  emit_device_px("screensaver-shadow-2", 2);
  emit_device_px("screensaver-shadow-4", 4);
  emit_device_px("screensaver-shadow-6", 6);
#endif
  emit("tile-pad-v", climate_layout::kCardPaddingVertical);
  emit("tile-pad-h", climate_layout::kCardPaddingHorizontal);
  // The device places a corner header's disc in the tile corner like the
  // half-height disc and centers the icon in it; the header labels move with
  // the icon (tile_icon_disc::corner_header). The preview header follows.
  const tile_icon_disc::CornerHeader header = tile_icon_disc::corner_header(
      tile_layout::scale_480(24), tile_layout::scale_480(20), tile_layout::scale_480(-8),
      lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0),
      lv_font_get_line_height(FONT_MDI_ICONS));
  emit_exact("icon-disc-corner", header.disc);
  emit_exact("tile-header-title-top",
             tile_layout::scale_480(24) + tile_layout::scale_480(4) + header.shift);
  emit_exact("tile-header-title-right", tile_layout::scale_480(20) - tile_layout::scale_480(4));
  emit_exact("tile-header-icon-top", tile_layout::scale_480(24) + header.icon_top);
  emit_exact("tile-header-icon-left", tile_layout::scale_480(20) + header.icon_side);
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
  emit_exact("tile-radius", configManager.getConfig().tile_radius);
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
    // Switch tile header layouts (types/switch/renderer.cpp): the state line
    // under the title, the pair centered where the Sensor title alone sits,
    // and the control bar one Climate gap below the corner disc, as high as
    // in a one-row tile, at the card's bottom.
    const int value_line = lv_font_get_line_height(FONT_VALUE);
    const int title_line = lv_font_get_line_height(tile_layout::header_title_font());
    const int title_top =
        tile_layout::scale_480(24) + tile_layout::scale_480(4) - value_line / 2 + header.shift;
    emit_exact("switch-title-top", title_top);
    emit_exact("switch-state-top", title_top + title_line);
    emit_exact("switch-state-top-alone", title_top + value_line / 2);
    const int bar_top = tile_icon_disc::inset() + header.disc + climate_layout::kGap;
    emit_exact("switch-bar-height",
               GRID_CELL_H - climate_layout::kOuterInset -
                   (bar_top > climate_layout::kContentTop ? bar_top : climate_layout::kContentTop));
  }
  html += "--settings-panel-width:";
  html += String(settings_panel_target_width_px());
  html += "px;";
  html += "--admin-wrapper-width:";
  html += String(admin_wrapper_target_width_px());
  html += "px;";
  html += "--grid-cols:";
  html += String(GRID_COLS);
  html += ";--grid-rows:";
  html += String(GRID_ROWS);
  html += ";--preview-cell-w:";
  html += String(preview_cell_w_px());
  html += "px;--preview-cell-h:";
  html += String(preview_cell_h_px());
  html += "px;--preview-gap:";
  html += String(preview_gap_px());
  html += "px;--preview-pad:";
  html += String(preview_pad_px());
  html += "px;";
  const int image_bleed = preview_scaled_exact_px(4);
  const int image_inset = preview_pad_px() > image_bleed
                              ? preview_pad_px() - image_bleed
                              : 0;
  html += "--screensaver-image-inset:";
  html += String(image_inset);
  html += "px;--screensaver-image-radius:";
  html += "calc(var(--tile-radius) + ";
  html += String(image_bleed);
  html += "px);";
  html += "}</style>\n";
}

}  // namespace

void appendAdminStyles(String& html) {
  appendPreviewScaleVars(html);
  html += R"html(
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@mdi/font@7.4.47/css/materialdesignicons.min.css">
  <link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'%3E%3Crect width='48' height='48' rx='10' fill='%2316181c'/%3E%3Crect x='4' y='4' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Crect x='27' y='4' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Crect x='4' y='27' width='17' height='17' rx='4' fill='%23ffffff'/%3E%3Cpath d='M33 26h5v6.5h6.5v5H38V44h-5v-6.5h-6.5v-5H33z' fill='%2326a69a'/%3E%3C/svg%3E">
  <link rel="stylesheet" href=")html";
  html += adminCssAssetPath();
  html += R"html(">
)html";
}

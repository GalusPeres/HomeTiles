#include "src/web/server/render/web_admin_styles.h"
#include "src/web/server/assets/web_admin_assets.h"
#include "src/types/climate/layout.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"

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
  // On the compact B4, fit the desktop container to the preview, gap and
  // settings panel. Wide devices retain the existing 1200 px layout.
  return (GRID_COLS <= 4) ? 952 : 1200;
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
  emit_exact("tile-header-title-top", tile_layout::scale_480(24) + tile_layout::scale_480(4));
  emit_exact("tile-header-title-right", tile_layout::scale_480(20) - tile_layout::scale_480(4));
  emit_exact("tile-header-icon-top", tile_layout::scale_480(24) + tile_layout::scale_480(-8));
  emit_exact("tile-header-icon-left", tile_layout::scale_480(20) + tile_layout::scale_480(-8));
#if defined(DEVICE_LAYOUT_1024X600)
  emit("value-dy", 23);
#elif defined(DEVICE_LAYOUT_480X480)
  emit("value-dy", 19);
#else
  emit("value-dy", 28);
#endif
  emit_exact("tile-radius", tile_layout::scale_480(22));
  // Climate tile geometry uses the exact same LVGL-to-preview scale as the
  // device. Keeping these separate from font variables avoids the 6 px
  // minimum used for readable preview text.
  emit_exact("climate-margin-x", climate_layout::kOuterInset);
  emit_exact("climate-grid-gap", climate_layout::kGap);
  emit_exact("climate-slots-top", climate_layout::kContentTop);
  emit_exact("climate-slots-bottom", climate_layout::kOuterInset);
  emit_exact(
      "climate-control-radius",
      climate_layout::kControlRadius);
  emit_exact("climate-control-side-pad", tile_layout::scale_480(8));
  emit_exact("climate-control-caption-w", tile_layout::scale_480(96));
  emit_exact("climate-control-button-w", tile_layout::scale_480(40));
  emit_exact(
      "climate-control-single-w",
      GRID_CELL_W - climate_layout::kOuterInset * 2);
  emit_exact("climate-control-v-pad-top", tile_layout::scale_480(5));
  emit_exact("climate-control-v-pad-bottom", tile_layout::scale_480(5));
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
  int image_radius = preview_scaled_exact_px(26);
#if defined(DEVICE_LAYOUT_480X480)
  // The 480x480 preview has a visible black display rim. Keep the inner
  // wallpaper corner concentric with the 21px outer preview corner.
  image_radius = 21 > image_inset ? 21 - image_inset : 0;
#endif
  html += "--screensaver-image-inset:";
  html += String(image_inset);
  html += "px;--screensaver-image-radius:";
  html += String(image_radius);
  html += "px;";
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

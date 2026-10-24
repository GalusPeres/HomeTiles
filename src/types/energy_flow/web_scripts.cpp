#include "src/types/energy_flow/web_scripts.h"

#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/types/energy_flow/flow_metrics.h"

namespace {

int font_px(const lv_font_t* font) {
  struct Size {
    const lv_font_t* font;
    int px;
  };
  static const Size kSizes[] = {
#if defined(DEVICE_LAYOUT_480X480)
      {&ui_font_12, 12}, {&ui_font_14, 14},
#endif
      {&ui_font_16, 16}, {&ui_font_20, 20}, {&ui_font_24, 24},
  };
  for (const Size& size : kSizes) {
    if (size.font == font) return size.px;
  }
  return 20;
}

}  // namespace

// The editable JavaScript lives in src/types/energy_flow/admin.js, bundled
// into src/web/assets/admin.js and served as one precompressed asset. The
// preview draws with the device's sizes (flow_metrics.h) in display pixels.
void append_energy_flow_scripts(String& html) {
  const energy_flow::Metrics m = energy_flow::device_metrics();
  const lv_font_t* font = energy_flow::value_font();
  const int icon_px =
      static_cast<int>(lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0));
  const int line = static_cast<int>(lv_font_get_line_height(font));
  // The node shapes: the card corner and the tile's corner disc
  // (renderer.cpp apply_ring_shape, apply_disc_shape).
  const int ring_corner = ui_surface_style::radius(tile_layout::scale_480(22));
  const int disc_corner =
      ui_surface_style::radius(tile_icon_disc::radius_baseline() - tile_icon_disc::corner_shrink());
  char text[480];
  snprintf(text, sizeof(text),
           "  <script>\n  const ENERGY_FLOW_LAYOUT = Object.freeze({ring: %.1f, disc: %.1f, ringCorner: %d, "
           "discCorner: %d, insetX: %d, insetY: %d, "
           "spacing: %d, lineWidth: %d, dotRadius: %d, iconPx: %d, valuePx: %d, valueLine: %d, valueBase: %d, "
           "valueGap: %d, valueBelow: %d, edge: %d, titleRight: %d, titleTop: %d});\n  </script>\n",
           static_cast<double>(m.ring), static_cast<double>(m.disc), ring_corner, disc_corner,
           static_cast<int>(m.inset_x),
           static_cast<int>(m.inset_y), static_cast<int>(m.spacing), energy_flow::line_width(),
           energy_flow::dot_radius(), icon_px, font_px(font), line, static_cast<int>(font->base_line),
           energy_flow::value_gap(), energy_flow::value_below(), energy_flow::value_edge(),
           energy_flow::title_right(), energy_flow::title_baseline() - (line - static_cast<int>(font->base_line)));
  html += text;
}

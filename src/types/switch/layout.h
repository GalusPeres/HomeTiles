#pragma once

#include <stdint.h>

#include <algorithm>
#include <cmath>

// Switch tile layouts and the geometry of their control bar. Pure logic
// without LVGL, shared by the renderer and the host tests.
//
// The layout is stored in Tile::sensor_decimals (Web field switch_style):
//   0 Icon button: icon above the title, the whole tile is the button.
//   1 Switch: Sensor header, a toggle bar below (replaces the old LVGL
//     switch; tiles stored with 1 become this layout).
//   2 Dimmer: Sensor header, a brightness bar below; entities that cannot dim
//     show the toggle bar instead.
//   3 Automatic: the dimmer for lights that can dim, else the toggle.
// Half-height tiles never show a bar: they use the Sensor compact layout.
namespace switch_layout {

enum class Layout : uint8_t {
  IconButton = 0,
  Switch = 1,
  Dimmer = 2,
  Automatic = 3,
};
inline constexpr uint8_t kLayoutMax = 3;
// New tiles start with Automatic (Web Admin default).
inline constexpr Layout kNewTileLayout = Layout::Automatic;

inline Layout from_stored(uint8_t value) {
  return value <= kLayoutMax ? static_cast<Layout>(value) : Layout::IconButton;
}

inline bool horizontal(Layout layout) { return layout != Layout::IconButton; }

enum class Bar : uint8_t {
  None,
  Toggle,
  Dimmer,
};

// `dimmable`: a light whose Home Assistant supported_color_modes include
// brightness, a color or a color temperature mode.
inline Bar bar_for(Layout layout, bool half_height, bool dimmable) {
  if (half_height || !horizontal(layout)) return Bar::None;
  if (layout == Layout::Switch) return Bar::Toggle;
  return dimmable ? Bar::Dimmer : Bar::Toggle;
}

// Dimmer bar geometry in bar-local pixels (approved mockup
// switch-tiles-left, 2026-10-01). The fill always stays inside the bar's
// rounded shape (like overflow: hidden): its start is the bar's own round
// end, its moving end has one fixed rounding (a quarter of the bar height)
// that grows into the bar's radius only where it would leave the bar's round
// right end, so 100 % is exactly the bar. The smallest piece (1 %) is as wide
// as the bar radius plus the end rounding: both roundings meet tangentially,
// without an edge, and the handle line sits exactly in its middle.
struct Dimmer {
  int width = 0;
  int height = 0;
  int radius = 0;

  int end_radius() const { return height / 4; }
  int min_fill() const {
    const int fill = radius + end_radius();
    return fill < width ? fill : width;
  }
  // The handle line sits this far inside the fill end: the middle of the
  // smallest piece.
  int handle_margin() const { return min_fill() / 2; }
  int handle_width() const {
    const int w = height * 7 / 100;
    return w < 3 ? 3 : w;
  }
  int handle_height() const { return height * 42 / 100; }
  // Handle position of 1 % and 100 %.
  int handle_low() const { return min_fill() - handle_margin(); }
  int handle_high() const { return width - handle_margin(); }

  // The Light popup's brightness logic sideways
  // (light_popup.cpp brightness_value_from_point): the finger is the handle;
  // at or before the 1 % position the value is 1, and it turns off (0) only
  // beyond the bar's end, like the popup slider.
  uint8_t value_at(int x) const {
    const int low = handle_low();
    const int high = handle_high();
    if (x < 0) return 0;
    if (x <= low) return 1;
    if (x >= high || high <= low) return 100;
    const int value = 1 + ((x - low) * 99 + (high - low) / 2) / (high - low);
    return static_cast<uint8_t>(value > 100 ? 100 : value);
  }

  int handle_x(uint8_t value) const {
    const int low = handle_low();
    const int high = handle_high();
    if (value <= 1 || high <= low) return low;
    if (value >= 100) return high;
    return low + ((value - 1) * (high - low) + 49) / 99;
  }

  // Width of the fill for a value (0 = off, no fill).
  int fill_width(uint8_t value) const {
    if (value == 0) return 0;
    const int fill = handle_x(value) + handle_margin();
    return fill > width ? width : fill;
  }

  // The end rounding for a fill width: the fixed rounding, larger only where
  // its corners would leave the bar's round right end; the bar radius at the
  // full width. Checked per pixel column against both circles.
  int end_radius_for(int fill) const {
    const int base = end_radius();
    if (fill <= width - radius || base >= radius) return base;
    const float corner = static_cast<float>(width - radius);
    for (int end = base; end < radius; ++end) {
      bool inside = true;
      for (int x = fill - end; x <= fill && inside; ++x) {
        const float dx_end = static_cast<float>(x - (fill - end));
        const float end_top = end - std::sqrt(std::max(0.0f, static_cast<float>(end * end) - dx_end * dx_end));
        const float dx_bar = static_cast<float>(x) - corner;
        const float bar_top = dx_bar <= 0.0f ? 0.0f
            : radius - std::sqrt(std::max(0.0f, static_cast<float>(radius * radius) - dx_bar * dx_bar));
        if (end_top + 0.01f < bar_top) inside = false;
      }
      if (inside) return end;
    }
    return radius;
  }
};

}  // namespace switch_layout

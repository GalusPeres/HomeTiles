#pragma once

#include <stdint.h>

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

// Dimmer bar geometry in bar-local pixels. The fill starts with the bar's own
// round end (`radius`), so it never needs clipping: 1 % fills exactly that
// end, like the cap of the Light popup's brightness track. The handle line
// sits `handle_margin` inside the fill end, which is slightly rounded.
struct Dimmer {
  int width = 0;
  int height = 0;
  int radius = 0;

  int handle_margin() const { return height / 5; }
  int end_radius() const { return height * 11 / 100; }
  int handle_width() const {
    const int w = height * 7 / 100;
    return w < 3 ? 3 : w;
  }
  int handle_height() const { return height * 42 / 100; }
  int min_fill() const {
    const int cap = 2 * radius;
    return cap < width ? cap : width;
  }
  // Handle position of 1 % and 100 %.
  int handle_low() const { return min_fill() - handle_margin(); }
  int handle_high() const { return width - handle_margin(); }

  // The Light popup's brightness logic sideways
  // (light_popup.cpp brightness_value_from_point): the finger is the handle;
  // at or before the 1 % position the value is 1, and only one bar radius
  // further it turns off (0).
  uint8_t value_at(int x) const {
    const int low = handle_low();
    const int high = handle_high();
    if (x < low - radius) return 0;
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
};

}  // namespace switch_layout

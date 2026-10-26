#pragma once

#include <math.h>
#include <stdint.h>

// The Energy flow tile's numbers, without LVGL (host tests: test-energy-flow-model.mjs).
// Every node is one Home Assistant power sensor; the panel splits the powers
// into the six flows between the nodes like Home Assistant's energy
// distribution card: exports and battery charging come from solar first,
// what solar leaves comes from the battery or the grid, the rest goes home.
namespace energy_flow {

// The fixed roles of Tile::flow_entities, one entity per line. Later lines
// are kept for further consumers behind Usage (user 2026-10-24: keep it
// open, not now).
enum Role : uint8_t {
  kRolePv = 0,
  kRoleGrid = 1,
  kRoleBattery = 2,
  kRoleBatteryCharge = 3,
  kRoleHome = 4,
  kRoleCount = 5,
};

// The six lines between the nodes, in drawing order.
enum Line : uint8_t {
  kLineGridHome = 0,
  kLinePvBattery = 1,
  kLinePvHome = 2,
  kLinePvGrid = 3,
  kLineBatteryHome = 4,
  kLineBatteryGrid = 5,
  kLineCount = 6,
};

// The four nodes, by where they sit: solar on top, grid left, home right,
// battery below (Home Assistant's energy distribution layout).
enum Node : uint8_t {
  kNodePv = 0,
  kNodeGrid = 1,
  kNodeHome = 2,
  kNodeBattery = 3,
  kNodeCount = 4,
};

// Flows below this count as idle (meter noise around zero).
constexpr float kIdleWatts = 10.0f;

// The sign options (Tile key_code bits, energy_flow_options.h).
// Home Assistant's convention: grid power positive while importing, battery
// power positive while discharging; some inverters report the other way.
struct Inputs {
  bool has_pv = false;
  bool has_grid = false;
  bool has_battery = false;
  bool has_home = false;
  float pv = 0.0f;       // W produced; negative readings count as 0.
  float grid = 0.0f;     // W, + import, - export.
  float battery = 0.0f;  // W, + discharge, - charge.
  float home = 0.0f;     // W used, only with has_home.
};

struct Flows {
  float watts[kLineCount] = {};
  // The battery-grid line carries one direction at a time: true while the
  // grid charges the battery (its source is then the grid).
  bool grid_to_battery = false;
  // The home's power: its own sensor, else what flows in.
  float home = 0.0f;
  bool home_known = false;
  // The node that delivers most to the home now (kNodePv, kNodeGrid,
  // kNodeBattery), kNodeCount while nothing flows in.
  uint8_t home_source = kNodeCount;
};

inline float positive(float value) { return value > 0.0f ? value : 0.0f; }

inline Flows compute(const Inputs& in) {
  Flows out;
  const float solar = in.has_pv ? positive(in.pv) : 0.0f;
  const float grid_in = in.has_grid ? positive(in.grid) : 0.0f;
  const float grid_out = in.has_grid ? positive(-in.grid) : 0.0f;
  const float battery_out = in.has_battery ? positive(in.battery) : 0.0f;
  const float battery_in = in.has_battery ? positive(-in.battery) : 0.0f;

  // Exports come from solar first, then from the battery.
  const float pv_grid = fminf(solar, grid_out);
  const float battery_grid = fminf(battery_out, grid_out - pv_grid);
  // Charging takes what solar leaves, then the grid.
  const float solar_left = solar - pv_grid;
  const float pv_battery = fminf(solar_left, battery_in);
  const float grid_battery = fminf(grid_in, positive(battery_in - pv_battery));
  // Everything else goes home.
  const float pv_home = solar_left - pv_battery;
  const float grid_home = grid_in - grid_battery;
  const float battery_home = battery_out - battery_grid;

  out.watts[kLineGridHome] = grid_home;
  out.watts[kLinePvBattery] = pv_battery;
  out.watts[kLinePvHome] = pv_home;
  out.watts[kLinePvGrid] = pv_grid;
  out.watts[kLineBatteryHome] = battery_home;
  out.grid_to_battery = grid_battery > battery_grid;
  out.watts[kLineBatteryGrid] = out.grid_to_battery ? grid_battery : battery_grid;
  for (float& watts : out.watts) {
    if (!(watts >= kIdleWatts)) watts = 0.0f;
  }

  out.home_known = in.has_home || in.has_pv || in.has_grid || in.has_battery;
  out.home = in.has_home ? positive(in.home) : pv_home + grid_home + battery_home;

  float best = kIdleWatts;
  const float into_home[3] = {pv_home, grid_home, battery_home};
  const uint8_t sources[3] = {kNodePv, kNodeGrid, kNodeBattery};
  for (int i = 0; i < 3; ++i) {
    if (into_home[i] >= best) {
      best = into_home[i];
      out.home_source = sources[i];
    }
  }
  return out;
}

// The source and target node of a line now (the battery-grid line turns).
inline uint8_t line_source(Line line, const Flows& flows) {
  switch (line) {
    case kLineGridHome: return kNodeGrid;
    case kLinePvBattery:
    case kLinePvHome:
    case kLinePvGrid: return kNodePv;
    case kLineBatteryHome: return kNodeBattery;
    case kLineBatteryGrid: return flows.grid_to_battery ? kNodeGrid : kNodeBattery;
    default: return kNodeCount;
  }
}

// The dots' speed in px per display frame (user 2026-10-26: faster and
// slower with the power, "immer richtig aufgeteilt"): the tile's strongest
// flow runs at its power / kWattsPerPxFrame, held between kTopSpeedMin and
// kTopSpeedMax; every other flow in proportion to it, none slower than
// kSlowestSpeed so it still visibly moves.
constexpr float kWattsPerPxFrame = 2500.0f;
constexpr float kTopSpeedMin = 0.5f;
constexpr float kTopSpeedMax = 3.0f;
constexpr float kSlowestSpeed = 0.15f;

inline float strongest_flow(const Flows& flows) {
  float strongest = 0.0f;
  for (float watts : flows.watts) strongest = watts > strongest ? watts : strongest;
  return strongest;
}

inline float dot_speed(float watts, float strongest) {
  if (watts <= 0.0f || strongest <= 0.0f) return 0.0f;
  float top = strongest / kWattsPerPxFrame;
  top = top < kTopSpeedMin ? kTopSpeedMin : top > kTopSpeedMax ? kTopSpeedMax : top;
  const float speed = top * watts / strongest;
  return speed < kSlowestSpeed ? kSlowestSpeed : speed;
}

// A sensor value in watts from its unit (Home Assistant power units).
inline bool unit_factor(const char* unit, float& factor) {
  if (!unit || !*unit) {
    factor = 1.0f;
    return true;
  }
  while (*unit == ' ') ++unit;
  const char prefix = unit[0];
  const char* rest = unit;
  if ((prefix == 'k' || prefix == 'K' || prefix == 'M' || prefix == 'G' || prefix == 'm') && unit[1] == 'W') {
    rest = unit + 1;
  }
  if (rest[0] != 'W' || (rest[1] && rest[1] != ' ')) {
    factor = 1.0f;
    return false;
  }
  factor = rest == unit ? 1.0f
           : prefix == 'k' || prefix == 'K' ? 1000.0f
           : prefix == 'M' ? 1000000.0f
           : prefix == 'G' ? 1000000000.0f
           : 0.001f;
  return true;
}

}  // namespace energy_flow

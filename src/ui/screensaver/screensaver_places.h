#pragma once

#include <stddef.h>
#include <stdint.h>

#include "src/tiles/config/grid_layout.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_layouts.h"

// The screensaver's tiles in the layouts (user 2026-10-08): the screensaver
// takes the active layout's grid without the head, two rows of slots at the
// bottom (three upright). The classic places stay in the screensaver grid; the other layouts
// keep their own in the layout file (tile_layouts.h) under the screensaver's
// storage ID, keyed by slot + 1 (screensaver tiles have no view IDs; a slot
// keeps its tile while it moves).
namespace screensaver_places {

constexpr uint16_t kFolder = TileConfig::kScreensaverGridStorageId;
inline uint16_t key(size_t index) { return static_cast<uint16_t>(index + 1); }

// The first row of the screensaver's rows in a layout's grid: two at the
// bottom, three on the upright screen.
float first_row(grid_layout::Layout layout);

// A tile's place in `layout`: its own, or while the layout has none yet the
// classic place with its rows moved onto the layout's bottom rows (as far as
// it fits). False when the tile is not shown there.
bool find(grid_layout::Layout layout, size_t index, const Tile& tile, tile_layouts::Place& out);

// The grid as the active layout shows it: the places moved, a tile without
// one marked layout_hidden.
void overlay(TileGridConfig& grid);

// An edit of slot `index` made in the active (bar) layout's grid `shown`:
// every shown tile's place goes to the layout file (written by
// tile_layouts::commit(); a layout without places takes the shown ones at
// once), the tile itself into `classic` with its classic place kept. A new
// tile takes the first free classic cell of the screensaver's rows, else it
// has no classic place.
void take(const TileGridConfig& shown, TileGridConfig& classic, size_t index);

// The places of every shown tile of `shown` for the active layout (a move
// in its grid).
void take_places(const TileGridConfig& shown);

// A slot emptied: its places in every layout go.
void forget(size_t index);

}  // namespace screensaver_places

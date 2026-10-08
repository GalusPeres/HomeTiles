#include "src/ui/screensaver/screensaver_places.h"

#include "src/tiles/config/tile_geometry.h"

namespace screensaver_places {
namespace {

using grid_layout::Layout;

float rows_of(const grid_layout::Shown& grid) { return grid.rows + (grid.half_row ? 0.5f : 0.0f); }

tile_layouts::Place classic_place(const Tile& tile) {
  return {tile.col, tile.row, tile.span_w < 0.5f ? 1.0f : tile.span_w, tile.span_h < 0.5f ? 1.0f : tile.span_h};
}

bool overlaps(const tile_layouts::Place& a, const tile_layouts::Place& b) {
  return a.col < b.col + b.span_w && b.col < a.col + a.span_w && a.row < b.row + b.span_h &&
         b.row < a.row + a.span_h;
}

// The first free classic cell of the screensaver's rows for a tile of this
// size (left to right, top to bottom, half steps).
bool free_classic_place(const TileGridConfig& classic, size_t index, const Tile& tile, tile_layouts::Place& out) {
  const float first = first_row(Layout::kClassic);
  const float rows = rows_of(grid_layout::profile_grid());
  const float cols = grid_layout::profile_grid().cols;
  const float w = tile.span_w < 0.5f ? 1.0f : tile.span_w;
  const float h = tile.span_h < 0.5f ? 1.0f : tile.span_h;
  for (float row = first; row + h <= rows + 0.001f; row += 0.5f) {
    for (float col = 0; col + w <= cols + 0.001f; col += 0.5f) {
      const tile_layouts::Place candidate{col, row, w, h};
      if (!tile_geometry::supported(tile.type, col, row, w, h)) continue;
      bool free = true;
      for (size_t i = 0; i < TILES_PER_GRID && free; ++i) {
        const Tile& other = classic.tiles[i];
        if (i == index || other.type == TILE_EMPTY || tile_layouts::classic_hidden(kFolder, key(i))) continue;
        free = !overlaps(candidate, classic_place(other));
      }
      if (free) {
        out = candidate;
        return true;
      }
    }
  }
  return false;
}

}  // namespace

float first_row(Layout layout) {
  const float rows = rows_of(grid_layout::layout_grid(layout));
  return rows > 2 ? rows - 2 : 0;
}

bool find(Layout layout, size_t index, const Tile& tile, tile_layouts::Place& out) {
  if (tile.type == TILE_EMPTY) return false;
  if (layout == Layout::kClassic) {
    out = classic_place(tile);
    return !tile_layouts::classic_hidden(kFolder, key(index));
  }
  const grid_layout::Shown grid = grid_layout::layout_grid(layout);
  if (tile_layouts::has_folder(layout, kFolder)) {
    if (!tile_layouts::find(layout, kFolder, key(index), out)) return false;
  } else {
    // Not set up yet: the classic place, its bottom rows on the layout's.
    out = classic_place(tile);
    out.row -= rows_of(grid_layout::profile_grid()) - rows_of(grid);
  }
  return out.row >= first_row(layout) - 0.001f &&
         grid_layout::inside(grid, out.col, out.row, out.span_w, out.span_h);
}

void overlay(TileGridConfig& grid) {
  const Layout layout = grid_layout::active();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    Tile& tile = grid.tiles[i];
    tile.layout_hidden = false;
    if (tile.type == TILE_EMPTY) continue;
    tile_layouts::Place place{};
    if (!find(layout, i, tile, place)) {
      tile.layout_hidden = true;
      continue;
    }
    tile.col = place.col;
    tile.row = place.row;
    tile.span_w = place.span_w;
    tile.span_h = place.span_h;
  }
}

void take_places(const TileGridConfig& shown) {
  const Layout layout = grid_layout::active();
  if (layout == Layout::kClassic) return;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = shown.tiles[i];
    if (tile.type == TILE_EMPTY || tile.layout_hidden) continue;
    tile_layouts::set(layout, kFolder, key(i), classic_place(tile));
  }
}

void take(const TileGridConfig& shown, TileGridConfig& classic, size_t index) {
  if (index >= TILES_PER_GRID) return;
  Tile tile = shown.tiles[index];
  if (tile.type == TILE_EMPTY) {
    forget(index);
    classic.tiles[index] = tile;
    take_places(shown);
    return;
  }
  take_places(shown);
  tile.layout_hidden = false;
  const Tile& before = classic.tiles[index];
  if (before.type != TILE_EMPTY) {
    const tile_layouts::Place place = classic_place(before);
    tile.col = place.col;
    tile.row = place.row;
    tile.span_w = place.span_w;
    tile.span_h = place.span_h;
  } else {
    tile_layouts::Place place{};
    const bool free = free_classic_place(classic, index, tile, place);
    if (!free) place = {0, first_row(Layout::kClassic), 1, 1};
    tile_layouts::set_classic_hidden(kFolder, key(index), !free);
    tile.col = place.col;
    tile.row = place.row;
    tile.span_w = place.span_w;
    tile.span_h = place.span_h;
  }
  classic.tiles[index] = tile;
}

void forget(size_t index) {
  for (uint8_t i = 0; i < grid_layout::kLayoutCount; ++i) {
    tile_layouts::remove(grid_layout::from_index(i), kFolder, key(index));
  }
}

}  // namespace screensaver_places

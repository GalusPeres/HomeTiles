#pragma once

#include <Arduino.h>
#include <stdint.h>

#include <vector>

#include "src/tiles/config/grid_layout.h"

// The places of the tiles in the layouts besides the classic one (layouts,
// user 2026-10-08): one file, /_tile_grids/layouts.json, by folder and stable
// tile ID (Tile::view_id). The classic layout keeps its places in the tile
// data (PackedTileV7), so older firmware still finds the panel as it was; the
// file only notes which tiles have no classic place (made under a bar layout
// without room there, or left out in the "Layout ändern" window).
//
// A tile without a place in the active layout is not shown (Tile::
// layout_hidden); it keeps its places in the other layouts.
namespace tile_layouts {

using grid_layout::Layout;

struct Place {
  float col;
  float row;
  float span_w;
  float span_h;
};

// Reads the file once at boot (after the storage is mounted).
void begin();

// A tile's place in a bar layout (also beside its screen: the layout
// window's storage), or the classic storage spot of a tile without a classic
// place. False when it has none.
bool find(Layout layout, uint16_t folder_id, uint16_t view_id, Place& out);
void set(Layout layout, uint16_t folder_id, uint16_t view_id, const Place& place);
void remove(Layout layout, uint16_t folder_id, uint16_t view_id);
// True once a bar layout has places in this folder (it was set up).
bool has_folder(Layout layout, uint16_t folder_id);
// True when any layout notes something for this folder.
bool has_any(uint16_t folder_id);

// Tiles without a place in the classic layout.
bool classic_hidden(uint16_t folder_id, uint16_t view_id);
void set_classic_hidden(uint16_t folder_id, uint16_t view_id, bool hidden);

// A tile changed its ID in the same slot (its type changed): its places go
// with it.
void rekey(uint16_t folder_id, uint16_t from_view, uint16_t to_view);
// Only these tiles remain in the folder: the others' places are dropped.
void retain(uint16_t folder_id, const std::vector<uint16_t>& views);
void drop_folder(uint16_t folder_id);

// The pages of the head bar layouts (user 2026-10-12): up to five Home
// folders beside Home (six dots with it), in the order of the head's dots. In
// the bar layouts their folder tiles stay off Home; the classic layout shows
// them as folders.
inline constexpr size_t kMaxPages = 5;
// The page folders in dot order (free slots skipped); returns how many.
size_t pages(uint16_t out[kMaxPages]);
// The slots as placed in the Web Admin, 0 for a free one (user 2026-10-12:
// nothing moves up); returns how many up to the last page.
size_t page_slots(uint16_t out[kMaxPages]);
// The folder's dot: 0 Home, 1.. a page, -1 neither (or no pages at all).
int page_number(uint16_t folder_id);
// Takes the first kMaxPages slots, each folder once, 0 a free slot (the
// caller checks that they are Home folders).
void set_pages(const uint16_t* folders, size_t count);

// Writes the file when something changed. Safe to call often.
bool commit();

// Everything as JSON: {"bar":{"<folder>":{"<view>":[col,row,w,h]}},
// "portrait":{...},"classic_parked":{...},"pages":[<folder>,...]} (a classic
// entry [0,0,0,0] has no storage spot yet).
void append_json(String& out);

}  // namespace tile_layouts

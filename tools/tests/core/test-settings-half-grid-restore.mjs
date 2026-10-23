// Exercise the real Settings placement and restoration with fractional gaps.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {readRepoFile, repoRoot} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const compiler = ['clang++', 'g++'].find(name => spawnSync(name, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: Settings restoration needs a C++ compiler'); process.exit(0); }
const functions = cppFunctionDefinitions(readRepoFile('src/tiles/config/tile_config.cpp'));
const fn = name => { const found = functions.find(item => item.name === name); assert.ok(found, name); return found.source; };
const ensure = fn('TileConfig::ensureSettingsTile');
const declaration = ensure.slice(0, ensure.indexOf('{')).replace('TileConfig::', '').replace('bool shown_places)', 'bool shown_places = false)') + ';';
const parseHalfArg = readRepoFile('src/web/server/handlers/web_admin_handlers.cpp')
  .match(/    auto parse_bounded_arg = [\s\S]*?\n    };/)[0];
const source = `
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>
struct String : std::string {
  using std::string::string;
  void trim() { erase(0,find_first_not_of(" ")); const auto last=find_last_not_of(" "); if(last!=npos)erase(last+1); }
};
constexpr int GRID_COLS = 4, GRID_ROWS = 4, TILES_PER_GRID = 16;
namespace Device { constexpr int kGridCols = GRID_COLS, kGridRows = GRID_ROWS; }
${readRepoFile('src/types/tile_type.h').replace(/^#include.*$/gm,'').replace('#pragma once','')}
${readRepoFile('src/tiles/config/tile_geometry.h').replace(/^#include.*$/gm,'').replace('#pragma once','')}
${readRepoFile('src/core/config/config_manager.h').match(/struct SettingsTileSnapshot \{[\s\S]*?\};/)[0]}
struct Tile { TileType type = TILE_EMPTY; String title, icon_name; uint32_t bg_color = 0; float col=0,row=0,span_w=1,span_h=1; bool layout_hidden = false; uint16_t view_id = 0; };
struct TileGridConfig { Tile tiles[TILES_PER_GRID]; };
// The shown layout (grid_layout.h) and its places (tile_layouts.h).
namespace grid_layout {
enum class Layout : uint8_t { kClassic, kBar, kPortrait };
Layout g_active = Layout::kClassic;
inline Layout active() { return g_active; }
struct Shown { int cols; int rows; bool half_row; };
Shown g_shown{GRID_COLS, GRID_ROWS, false};
inline const Shown& shown() { return g_shown; }
inline float shown_rows() { return g_shown.rows + (g_shown.half_row ? 0.5f : 0.0f); }
inline bool inside(float c, float r, float w, float h) { return c >= 0 && r >= 0 && c + w <= g_shown.cols && r + h <= shown_rows(); }
}
namespace tile_layouts {
struct Place { float col, row, span_w, span_h; };
bool g_has = false; uint16_t g_view = 0; Place g_place{};
inline bool find(grid_layout::Layout, uint16_t, uint16_t view, Place& out) { if (!g_has || view != g_view) return false; out = g_place; return true; }
}
static inline bool shownTileLayout(const Tile& tile, float& col, float& row, float& w, float& h) {
  if (tile.layout_hidden) return false;
  col = tile.col; row = tile.row; w = tile.span_w < 0.5f ? 1 : tile.span_w; h = tile.span_h < 0.5f ? 1 : tile.span_h;
  return true;
}
struct Config { SettingsTileSnapshot settings_tile_snapshot{}; const char* language="en"; };
struct Manager { Config config; const Config& getConfig() { return config; } } configManager;
namespace i18n { struct Strings { const char* tile_type_settings="Settings"; }; Strings strings(const char*) { return {}; } }
class TileConfig { public: static constexpr uint16_t kRootFolderId = 0; ${declaration} };
template<typename P,typename S> void clamp_media_tile_layout(TileType,P&,P&,S&,S&) {}
${fn('get_tile_layout_clamped')}
${fn('mark_occupied')}
${fn('settings_tile_rect_is_free')}
${fn('find_settings_tile_rect_bottom_right')}
${fn('settings_tile_rect_is_free_shown')}
${ensure}
int main() {
  struct Server { String value; String arg(const char*) { return value; } } server;
  ${parseHalfArg}
  float parsed=-1;
  for (const auto* value : {"0.5","1.5"," 2.5 "}) {
    server.value=value;
    assert(parse_bounded_arg("coordinate",0,3.5f,parsed));
    assert(parsed==std::strtof(value,nullptr));
  }
  for (const auto* value : {"","bad","1.5bad","0.25","nan","inf","-0.5","4"}) {
    server.value=value; parsed=-1;
    assert(!parse_bounded_arg("coordinate",0,3.5f,parsed) && parsed==-1);
  }
  TileGridConfig grid;
  grid.tiles[0].type=TILE_FOLDER;
  grid.tiles[0].span_h=0.5f;
  assert(settings_tile_rect_is_free(grid,0,0.5f,1,0.5f));
  assert(!settings_tile_rect_is_free(grid,0,0,1,0.5f));
  assert(!settings_tile_rect_is_free(grid,0,0.25f,1,0.5f));
  assert(!settings_tile_rect_is_free(grid,-0.5f,1,1,0.5f));
  auto& snapshot=configManager.config.settings_tile_snapshot;
  snapshot.valid=true; snapshot.col=0; snapshot.row=0.5f;
  snapshot.span_w=1; snapshot.span_h=0.5f;
  std::strcpy(snapshot.title,"My settings"); std::strcpy(snapshot.icon_name,"cog");
  // Parked per layout (user 2026-10-13): back in the grid with its kept ID.
  snapshot.view_id=77;
  TileConfig config;
  // Out of its field it is one cell and no place is kept (user 2026-10-22:
  // it came back half high at a remembered spot): the first free cell from
  // the bottom right, whatever the snapshot holds.
  assert(config.ensureSettingsTile(grid,-1,-1));
  assert(grid.tiles[1].type==TILE_SETTINGS && grid.tiles[1].col==3 && grid.tiles[1].row==3);
  assert(grid.tiles[1].view_id==77);
  assert(grid.tiles[1].span_w==1 && grid.tiles[1].span_h==1);
  assert(grid.tiles[1].title=="My settings");
  grid.tiles[1]=Tile{};
  // Dropped: exactly there, one cell.
  assert(config.ensureSettingsTile(grid,1.5f,2.5f));
  assert(grid.tiles[1].col==1.5f && grid.tiles[1].row==2.5f && grid.tiles[1].span_h==1);
  grid.tiles[1]=Tile{};
  assert(!config.ensureSettingsTile(grid,1.25f,2.5f));
  assert(!config.ensureSettingsTile(grid,3.5f,2.5f));
  // Only a half row free: no half-high Settings tile, Home is full.
  grid.tiles[0].span_w=4; grid.tiles[0].span_h=3.5f;
  assert(!config.ensureSettingsTile(grid,-1,-1));
  // A grid with a bar layout's places (user 2026-10-13: dropped in the
  // bottom row in the layout window, the panel showed it a row higher): that
  // layout's cells; a place kept for it there is not used either.
  grid_layout::g_active = grid_layout::Layout::kBar;
  grid_layout::g_shown = {3, 3, false};
  for (auto& tile : grid.tiles) tile = Tile{};
  grid.tiles[0].type = TILE_FOLDER;
  // Hidden in this layout: no room taken there.
  grid.tiles[2].type = TILE_FOLDER; grid.tiles[2].layout_hidden = true; grid.tiles[2].row = 2;
  snapshot.col = 1; snapshot.row = 1; snapshot.span_w = 1; snapshot.span_h = 1; snapshot.view_id = 77;
  tile_layouts::g_has = true; tile_layouts::g_view = 77; tile_layouts::g_place = {0, 2, 1, 1};
  assert(config.ensureSettingsTile(grid, -1, -1, true));
  assert(grid.tiles[1].type == TILE_SETTINGS && grid.tiles[1].col == 2 && grid.tiles[1].row == 2 && grid.tiles[1].view_id == 77);
  grid.tiles[1] = Tile{};
  // A target cell of that layout: its cells, its hidden tiles take no room.
  assert(config.ensureSettingsTile(grid, 0, 2, true));
  assert(grid.tiles[1].col == 0 && grid.tiles[1].row == 2);
  grid.tiles[1] = Tile{};
  assert(!config.ensureSettingsTile(grid, 2.5f, 2, true));
  // The classic grid of the same panel: no snapshot place either.
  assert(config.ensureSettingsTile(grid, -1, -1));
  assert(grid.tiles[1].col == 3 && grid.tiles[1].row == 3);
}
`;
const directory = path.join(repoRoot, 'build/tests/settings-half-grid-restore');
fs.mkdirSync(directory, {recursive:true});
fs.writeFileSync(path.join(directory, 'test.cpp'), source);
const binary = path.join(directory, process.platform === 'win32' ? 'test.exe' : 'test');
const compiled = spawnSync(compiler, ['-std=c++17', '-D_CRT_SECURE_NO_WARNINGS', path.join(directory,'test.cpp'), '-o', binary], {encoding:'utf8'});
assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr);
const run = spawnSync(binary, [], {encoding:'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
console.log('Settings fractional placement, collision checks and snapshot restoration passed');

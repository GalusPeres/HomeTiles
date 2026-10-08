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
const declaration = ensure.slice(0, ensure.indexOf('{')).replace('TileConfig::', '') + ';';
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
struct Tile { TileType type = TILE_EMPTY; String title, icon_name; uint32_t bg_color = 0; float col=0,row=0,span_w=1,span_h=1; bool layout_hidden = false; };
struct TileGridConfig { Tile tiles[TILES_PER_GRID]; };
struct Config { SettingsTileSnapshot settings_tile_snapshot{}; const char* language="en"; };
struct Manager { Config config; const Config& getConfig() { return config; } } configManager;
namespace i18n { struct Strings { const char* tile_type_settings="Settings"; }; Strings strings(const char*) { return {}; } }
class TileConfig { public: ${declaration} };
template<typename P,typename S> void clamp_media_tile_layout(TileType,P&,P&,S&,S&) {}
${fn('get_tile_layout_clamped')}
${fn('mark_occupied')}
${fn('settings_tile_rect_is_free')}
${fn('find_settings_tile_rect_bottom_right')}
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
  TileConfig config;
  assert(config.ensureSettingsTile(grid,-1,-1));
  assert(grid.tiles[1].type==TILE_SETTINGS && grid.tiles[1].row==0.5f);
  assert(grid.tiles[1].span_w==1 && grid.tiles[1].span_h==0.5f);
  assert(grid.tiles[1].title=="My settings");
  grid.tiles[1]=Tile{};
  assert(config.ensureSettingsTile(grid,1.5f,2.5f));
  assert(grid.tiles[1].col==1.5f && grid.tiles[1].row==2.5f);
  grid.tiles[1]=Tile{};
  assert(!config.ensureSettingsTile(grid,1.25f,2.5f));
  assert(!config.ensureSettingsTile(grid,3.5f,2.5f));
  // An occupied saved location falls back into the only remaining half-row gap.
  snapshot.col=0; snapshot.row=0;
  grid.tiles[0].span_w=4; grid.tiles[0].span_h=3.5f;
  assert(config.ensureSettingsTile(grid,-1,-1));
  assert(grid.tiles[1].col==3 && grid.tiles[1].row==3.5f);
  assert(grid.tiles[1].span_h==0.5f);
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

// Moving tiles on the panel without rebuilding them (user 2026-10-02: a full
// grid rebuild took about 340 ms on the V2). A reorder or a Settings parking
// move makes the visible tiles take their new cells; any other change must
// rebuild, so tileContentEquals() has to see every Tile field but its cell.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const header = readRepoFile('src/tiles/config/tile_config.h').replace(/\r\n?/g, '\n');
const struct = header.slice(header.indexOf('struct Tile {'), header.indexOf('\n};\n', header.indexOf('struct Tile {')) + 4);
const equalsStart = header.indexOf('static inline bool tileContentEquals(');
const equals = header.slice(equalsStart, header.indexOf('\n}\n', equalsStart) + 3);
assert.ok(struct.length > 100 && equals.length > 100);

// Every field of the struct (constructor and comments aside).
const body = struct.slice(struct.indexOf('{') + 1, struct.indexOf('  Tile()'));
const fields = [...body.replace(/\/\/.*$/gm, '').matchAll(/^\s+([A-Za-z_][\w:<> ]*?)\s+(\w+)(?:\s*=\s*[^;]+)?;/gm)]
  .map(match => ({type: match[1].trim(), name: match[2]}));
assert.ok(fields.length >= 30, 'Tile fields parsed: ' + fields.map(field => field.name).join(','));
for (const {name} of fields) {
  if (name === 'col' || name === 'row') {
    assert.ok(!equals.includes(`a.${name} ==`), `${name} is the cell and must not block a move`);
  } else {
    assert.ok(equals.includes(`a.${name} == b.${name}`), `tileContentEquals misses Tile::${name}`);
  }
}

// The real struct and function on the host: a change in any field but the
// cell makes two tiles differ.
const mutate = ({type, name}) => type === 'String' ? `b.${name} += "x";`
  : type === 'bool' ? `b.${name} = !b.${name};`
  : type === 'TileType' ? `b.${name} = TILE_SETTINGS;`
  : `b.${name} = static_cast<decltype(b.${name})>(b.${name} + 1);`;
const harness = `
#include <cstdio>
#include <cstdint>
#include <string>
using String = std::string;
enum TileType : uint8_t { TILE_EMPTY = 0, TILE_SETTINGS = 7 };
constexpr uint8_t TILE_POPUP_OPEN_LONG_PRESS = 0;
${struct}
${equals}
int main() {
  int failures = 0;
  {
    Tile a; Tile b; b.col = 3; b.row = 2.5f;
    if (!tileContentEquals(a, b)) { std::printf("cell blocks a move\\n"); ++failures; }
  }
${fields.filter(field => field.name !== 'col' && field.name !== 'row').map(field => `  {
    Tile a; Tile b; ${mutate(field)}
    if (tileContentEquals(a, b)) { std::printf("missed ${field.name}\\n"); ++failures; }
  }`).join('\n')}
  std::printf(failures ? "fail\\n" : "pass\\n");
  return failures ? 1 : 0;
}
`;
const output = compileAndRun({label: 'Tile move fast path', harness});
if (output !== null) assert.match(output, /pass/);

// The panel updates the visible grid before a full rebuild: unchanged tiles
// stay, moved tiles take their new cells, changed, new or removed tiles are
// built or deleted one by one (user 2026-10-02: moving, resizing and
// recoloring must all be fast).
const tab = readRepoFile('src/ui/tabs/tiles/tab_tiles_unified.cpp').replace(/\r\n?/g, '\n');
const show = tab.slice(tab.indexOf('bool tiles_show_active_layout_now() {'));
assert.ok(show.slice(0, 500).includes('if (!update_active_layout()) tiles_reload_layout(GridType::TAB0);'));
const update = tab.slice(tab.indexOf('static bool update_active_layout() {'), tab.indexOf('bool tiles_show_active_layout_now() {'));
assert.ok(/if \(!tileContentEquals\(before, after\) \|\| !g_tiles_objs\[idx\]\[i\]\) \{\s+rebuild_tile_at_index\(GridType::TAB0, static_cast<uint8_t>\(i\)\);/.test(update),
  'a changed tile is rebuilt alone');
const removal = update.slice(update.indexOf('if (after.type == TILE_EMPTY) {'), update.indexOf('++rebuilt;'));
for (const kind of ['sensor', 'switch', 'climate', 'cover', 'binary_sensor', 'weather']) {
  assert.ok(removal.includes(`reset_${kind}_widget(GridType::TAB0, static_cast<uint8_t>(i));`), `removed tile drops its ${kind} widget`);
}
assert.ok(update.includes('if (rebuilds) hide_light_popup();'), 'a light popup bound to a slot closes like a rebuild');
// Anything but tiles and empty-cell placeholders in the grid rebuilds; the
// checks run before anything changes.
assert.ok(update.indexOf('if (lv_obj_get_child_count(child) != 0') < update.indexOf('lv_display_enable_invalidation(disp, false)'));
assert.ok(update.includes('place_tile_card(g_tiles_objs[idx][i]') && update.includes('lv_obj_remove_flag(g_tiles_objs[idx][i], LV_OBJ_FLAG_IGNORE_LAYOUT);'));
assert.ok(update.includes('lv_obj_move_to_index(render_empty_tile(grid, c, r), 0);'));
assert.ok(update.includes('g_active_cache->grid_config = next;') && update.includes('tile_renderer_snapshot_tab0(&g_active_cache->widgets);'));
assert.ok(update.includes('if (folders_changed) schedule_navigation_preload(g_active_cache->folder_id, next);'));

console.log('Tile updates: moved tiles move, changed tiles rebuild alone, the rest stays');

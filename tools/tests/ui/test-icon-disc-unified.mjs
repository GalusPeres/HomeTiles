// Every tile icon and the popup header icon sit on a background disc.
// tile_icon_disc is the single source: half-height tiles wrap the icon in a
// concentric disc; taller tiles keep their icon where it was and get a disc
// one inset larger, centered on it, with the same radius rule. The popup
// header uses a plain circle of popup_layout::scale(72). No renderer builds
// its own disc.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replace(/\r\n?/g, '\n');
const code = source => source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');

// One helper owns the disc geometry and creates every disc.
const helper = code(read('src/tiles/runtime/tile_icon_disc.h'));
for (const marker of [
  'inline int inset() { return tile_layout::scale_480(4); }',
  'inline int row_height() { return (GRID_CELL_H - GRID_GAP) / 2; }',
  'inline int diameter() { return row_height() - inset() * 2; }',
  'inline int radius_baseline() { return tile_layout::scale_480(22) - inset(); }',
  'inline int round_diameter() { return diameter() + inset(); }',
  'inline constexpr lv_opa_t kOpa = 38;',
  'const int size = shape == Shape::Round ? round_diameter() : diameter();',
  'lv_obj_set_size(disc, size, size);',
  'ui_surface_style::apply_radius(disc, radius_baseline(), 0);',
  'lv_obj_set_style_bg_opa(disc, kOpa, 0);',
  'lv_obj_t* disc = create(card, Shape::Concentric);',
  'lv_obj_t* disc = create(card, Shape::Round);',
  'lv_obj_move_to_index(disc, lv_obj_get_index(icon));',
  'inline void set_icon_hidden(lv_obj_t* icon, bool hidden)',
]) assert.ok(helper.includes(marker), `tile_icon_disc: ${marker}`);
assert.equal((helper.match(/lv_obj_create\(/g) || []).length, 1, 'One disc implementation');
// Every tile disc follows the global radius; none is a fixed circle.
assert.doesNotMatch(helper, /LV_RADIUS_CIRCLE/, 'Tile discs follow the global radius');
// The round disc moves to the icon; it never moves the icon.
const addRound = helper.slice(helper.indexOf('inline lv_obj_t* add_round('));
assert.doesNotMatch(addRound, /lv_obj_(?:align|set_pos|set_parent|center)\(icon/, 'The round disc must not move the icon');
assert.match(addRound, /lv_obj_align\(disc, align,/);
// Taller tiles get the larger disc, centered with its own diameter.
assert.match(addRound, /const int size = round_diameter\(\);/);
assert.match(addRound, /icon_size\.x, size\)/);
assert.match(addRound, /icon_size\.y, size\)/);

// Half-height tiles keep the concentric disc through the same helper.
const compact = code(read('src/tiles/runtime/compact_sensor_layout.h'));
for (const marker of [
  'inline int inset() { return tile_icon_disc::inset(); }',
  'inline int header_height() { return tile_icon_disc::row_height(); }',
  'tile_icon_disc::wrap(card, icon)',
  'tile_icon_disc::place_in_corner(card, disc);',
  'lv_obj_move_background(disc);',
]) assert.ok(compact.includes(marker), `compact layout: ${marker}`);

// Taller tiles: icon and title are unchanged from before the disc; the only
// addition is the round disc (plus its include and icon visibility helper).
const round = {
  sensor: 'tile_icon_disc::add_round(card, icon_lbl);',
  binary_sensor: 'tile_icon_disc::add_round(card, widgets.icon_label);',
  energy: 'tile_icon_disc::add_round(card, icon_lbl);',
  cover: 'tile_icon_disc::add_round(card, widget.icon_label);',
  climate: 'tile_icon_disc::add_round(card, icon_label);',
  weather: 'tile_icon_disc::add_round(card, icon_label);',
  media: 'tile_icon_disc::add_round(card, icon_label);',
  text: 'tile_icon_disc::add_round(card, icon_lbl);',
  clock: 'tile_icon_disc::add_round(card, icon_lbl);',
  switch: 'tile_icon_disc::add_round(container, icon_lbl);',
  navigate: 'tile_icon_disc::add_round(btn, icon_lbl);',
  scene: 'tile_icon_disc::add_round(btn, icon_lbl);',
  camera: 'tile_icon_disc::add_round(card, icon);',
};
const renderers = fs.readdirSync(path.join(root, 'src/types'), {withFileTypes: true})
  .filter(entry => entry.isDirectory() && fs.existsSync(path.join(root, 'src/types', entry.name, 'renderer.cpp')))
  .map(entry => entry.name);
for (const type of renderers) {
  const source = code(read(`src/types/${type}/renderer.cpp`));
  if (source.includes('FONT_MDI_ICONS') && type !== 'pixelanim') {
    assert.ok(round[type], `${type} renders an icon without the shared disc`);
  }
  if (round[type]) {
    assert.ok(source.includes(round[type]), `${type} adds the round disc`);
    assert.match(source, /#include "src\/tiles\/runtime\/tile_icon_disc\.h"/, `${type} includes the helper`);
  }
  // No renderer builds its own disc.
  assert.doesNotMatch(source, /bg_opa\([^;]*\b38\b/, `${type} must not style its own icon disc`);
  assert.doesNotMatch(source, /apply_radius\([^;]*scale_480\(22\)\s*-/, `${type} must not derive its own disc radius`);
  assert.doesNotMatch(source, /GRID_CELL_H\s*-\s*GRID_GAP\)\s*\/\s*2/, `${type} must not size its own disc`);
  // Icon and title keep their original alignment; the disc follows the icon.
  assert.doesNotMatch(source, /apply_header|apply_centered|place_in_corner/, `${type} must not move icon or title`);
}
const original = {
  sensor: /lv_obj_align\(icon_lbl, LV_ALIGN_TOP_LEFT,\s*tile_layout::scale_480\(-8\),\s*tile_layout::scale_480\(-8\)\)/,
  cover: /lv_obj_align\(widget\.icon_label, LV_ALIGN_TOP_LEFT,\s*tile_layout::scale_480\(-8\),/,
  climate: /icon_label, LV_ALIGN_TOP_LEFT,\s*-8, -8\);/,
  text: /lv_obj_align\(icon_lbl, LV_ALIGN_TOP_RIGHT,\s*tile_layout::scale_480\(4\),\s*tile_layout::scale_480\(-8\)\)/,
  clock: /lv_obj_align\(title_lbl, LV_ALIGN_TOP_LEFT, 0,\s*tile_layout::scale_480\(4\)\)/,
  navigate: /lv_obj_align\(l, LV_ALIGN_CENTER, 0, tile_layout::scale\(35\)\)/,
  camera: /lv_obj_align\(icon, LV_ALIGN_CENTER, 0, tile_layout::scale_i16\(-20\)\)/,
};
for (const [type, pattern] of Object.entries(original)) {
  assert.match(read(`src/types/${type}/renderer.cpp`), pattern, `${type} keeps its original header geometry`);
}
// Number, Select and Date/Time render through the Sensor tile.
for (const type of ['number', 'select', 'datetime']) {
  assert.match(read(`src/types/${type}/renderer.cpp`), /render_sensor_tile\(/, `${type} uses the Sensor tile`);
}
// Half-height tiles use the compact disc instead of the round one.
for (const type of ['sensor', 'binary_sensor', 'energy']) {
  assert.match(code(read(`src/types/${type}/renderer.cpp`)),
    /compact_sensor_layout::apply\([^;]*\);\s*\} else \{\s*tile_icon_disc::add_round\(/, `${type} keeps half-height tiles on the compact disc`);
}

// Runtime icon hide/show toggles the disc too, so an empty disc never shows.
for (const [file, count] of [['src/tiles/runtime/tile_renderer.cpp', 3],
                             ['src/types/binary_sensor/renderer.cpp', 2],
                             ['src/ui/tabs/tiles/tab_tiles_unified.cpp', 3]]) {
  const source = code(read(file));
  assert.equal((source.match(/tile_icon_disc::set_icon_hidden\(/g) || []).length, count, `${file} icon visibility`);
}

// Popup header: one translucent circle of scale(72) at the content's left
// edge, centered on the header line; the title starts after a scale(16) gap.
const layout = code(read('src/ui/popups/popup_layout.h'));
for (const marker of [
  'constexpr int kHeaderIconDiscSize = scale(72);',
  'constexpr int kHeaderIconDiscGap = scale(16);',
  'constexpr int kHeaderIconDiscOpa = 38;',
  'constexpr int kHeaderIconX = 0;',
  'constexpr int kHeaderTitleX = kHeaderIconDiscSize + kHeaderIconDiscGap;',
  'lv_obj_set_size(disc, kHeaderIconDiscSize, kHeaderIconDiscSize);',
  'lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);',
  'const int y = center - kHeaderIconDiscSize / 2;',
  'lv_obj_align(icon_disc, LV_ALIGN_TOP_LEFT, kHeaderIconX, y);',
  'lv_obj_set_width(icon, kHeaderIconDiscSize);',
  'lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);',
]) assert.ok(layout.includes(marker), `popup_layout: ${marker}`);
const popupDisc = layout.slice(layout.indexOf('inline lv_obj_t* createHeaderIconDisc('));
assert.doesNotMatch(popupDisc.slice(0, popupDisc.indexOf('return disc;')), /apply_radius|kCloseButton/,
  'The popup header disc is a plain circle');
// The close button itself stays unchanged.
for (const marker of [
  'lv_obj_set_size(close_btn, kCloseButtonSize, kCloseButtonSize);',
  'lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, 0);',
  'ui_surface_style::apply_radius(close_btn, kCloseButtonRadius, 0);',
  'lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, kCloseButtonOffsetX,',
]) assert.ok(layout.includes(marker), `close button: ${marker}`);

const shell = code(read('src/ui/popups/popup_shell.cpp'));
assert.ok(shell.includes('&shell.icon_disc);'), 'Only the visible shared header draws the disc');
assert.ok(shell.includes('create_header(parts.card, parts.title, parts.icon, parts.close, close_handler, context);'),
  'Invisible body headers get no disc');
assert.match(shell, /lv_obj_set_flag\(shell\.icon_disc, LV_OBJ_FLAG_HIDDEN,/, 'The popup disc follows the header icon');
assert.ok(shell.includes('popup_layout::alignHeader(shell.header, shell.title, shell.icon, shell.icon_disc);'));
for (const popup of ['camera', 'climate', 'cover', 'energy', 'light', 'media', 'pin', 'sensor', 'weather']) {
  const source = code(read(`src/ui/popups/${popup}/${popup}_popup.cpp`));
  assert.match(source, /create_popup_body\(/, `${popup} uses the shared popup body`);
  assert.doesNotMatch(source, /createCloseButton\(|createHeaderIconDisc\(/, `${popup} builds its own header`);
}

console.log('Tile icons and the popup header icon share their background discs.');

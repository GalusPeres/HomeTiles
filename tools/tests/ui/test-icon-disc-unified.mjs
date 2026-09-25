// Every tile icon and the popup header icon sit on one shared background shape.
// Tiles use tile_icon_disc (the half-height disc: row height minus the inset,
// concentric radius through the shared radius style); the popup header uses a
// plain circle of popup_layout::scale(72). No renderer builds its own disc.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replace(/\r\n?/g, '\n');
const code = source => source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');

// One shared helper owns the disc geometry.
const helper = code(read('src/tiles/runtime/tile_icon_disc.h'));
for (const marker of [
  'inline int inset() { return tile_layout::scale_480(4); }',
  'inline int row_height() { return (GRID_CELL_H - GRID_GAP) / 2; }',
  'inline int diameter() { return row_height() - inset() * 2; }',
  'inline int radius_baseline() { return tile_layout::scale_480(22) - inset(); }',
  'inline constexpr lv_opa_t kOpa = 38;',
  'lv_obj_set_size(disc, diameter(), diameter());',
  'ui_surface_style::apply_radius(disc, radius_baseline(), 0);',
  'lv_obj_set_style_bg_opa(disc, kOpa, 0);',
  'lv_obj_set_parent(icon, disc);',
  'lv_obj_center(icon);',
  'lv_obj_move_to_index(disc, lv_obj_get_index(icon));',
  'lv_obj_set_flag(disc, LV_OBJ_FLAG_HIDDEN, lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));',
  'if (lv_obj_t* existing = disc_of(icon)) return existing;',
  'inline void set_icon_hidden(lv_obj_t* icon, bool hidden)',
  'inline lv_obj_t* apply_header(',
  'inline lv_obj_t* apply_centered(lv_obj_t* card, lv_obj_t* icon, lv_obj_t* title)',
  'const int overlap = (y + diameter() / 2 + inset()) - (title_y - title_height / 2);',
  'lv_obj_set_style_max_width(title, std::max(1, max_width), 0);',
]) assert.ok(helper.includes(marker), `tile_icon_disc: ${marker}`);
assert.equal((helper.match(/lv_obj_create\(/g) || []).length, 1, 'The helper creates exactly one disc object');
// Corner discs are measured from the card's outer edge, not its padding.
assert.match(helper, /inset\(\) - lv_obj_get_style_space_top\(card, LV_PART_MAIN\)/);
assert.match(helper, /inset\(\) - lv_obj_get_style_space_left\(card, LV_PART_MAIN\)/);
assert.match(helper, /lv_obj_get_style_space_right\(card, LV_PART_MAIN\) - inset\(\)/);
// Titles are centered on the disc center.
assert.match(helper, /const int y = inset\(\) \+ diameter\(\) \/ 2 - line \/ 2 -/);

// Half-height tiles keep their look through the same helper.
const compact = code(read('src/tiles/runtime/compact_sensor_layout.h'));
assert.ok(compact.includes('inline int inset() { return tile_icon_disc::inset(); }'));
assert.ok(compact.includes('inline int header_height() { return tile_icon_disc::row_height(); }'));
assert.ok(compact.includes('tile_icon_disc::wrap(card, icon)'));
assert.ok(compact.includes('tile_icon_disc::place_in_corner(card, disc, tile_icon_disc::Corner::Left);'));
assert.ok(compact.includes('lv_obj_move_background(disc);'));

// Every tile renderer with an MDI icon uses the helper.
const header = {
  sensor: 'tile_icon_disc::apply_header(card, icon_lbl, title_label, tile);',
  binary_sensor: 'tile_icon_disc::apply_header(card, widgets.icon_label, widgets.title_label, tile);',
  energy: 'tile_icon_disc::apply_header(card, icon_lbl, title_label, tile);',
  cover: 'tile_icon_disc::apply_header(card, widget.icon_label, widget.title_label, tile);',
  climate: 'tile_icon_disc::apply_header(card, icon_label, title, tile);',
  weather: 'tile_icon_disc::apply_header(card, icon_label, location_label, tile);',
  media: 'tile_icon_disc::apply_header(card, icon_label, title_label, tile);',
  text: 'tile_icon_disc::apply_header(card, icon_lbl, title_lbl, tile,\n                               tile_icon_disc::Corner::Right);',
  clock: 'tile_icon_disc::apply_header(card, icon_lbl, title_lbl, tile,\n                                 tile_icon_disc::Corner::Right);',
  switch: 'tile_icon_disc::apply_header(container, icon_lbl, title_lbl, tile,\n                                 tile_icon_disc::Corner::Right);',
};
const centered = {
  switch: 'tile_icon_disc::apply_centered(container, icon_lbl, title_lbl);',
  navigate: 'tile_icon_disc::apply_centered(btn, icon_lbl, l);',
  scene: 'tile_icon_disc::apply_centered(btn, icon_lbl, l);',
  camera: 'tile_icon_disc::apply_centered(card, icon, title_label);',
};
const renderers = fs.readdirSync(path.join(root, 'src/types'), {withFileTypes: true})
  .filter(entry => entry.isDirectory() && fs.existsSync(path.join(root, 'src/types', entry.name, 'renderer.cpp')))
  .map(entry => entry.name);
for (const type of renderers) {
  const source = code(read(`src/types/${type}/renderer.cpp`));
  const hasIcon = source.includes('FONT_MDI_ICONS');
  if (header[type]) assert.ok(source.includes(header[type]), `${type} header uses the shared disc`);
  if (centered[type]) assert.ok(source.includes(centered[type]), `${type} centered icon uses the shared disc`);
  // Media transport buttons are controls, not tile icons.
  if (hasIcon && type !== 'media') assert.ok(header[type] || centered[type], `${type} renders an icon without the shared disc`);
  if (hasIcon) assert.match(source, /#include "src\/tiles\/runtime\/tile_icon_disc\.h"/, `${type} includes the helper`);
  // No renderer builds its own, differently sized disc or keeps the old icon offsets.
  assert.doesNotMatch(source, /bg_opa\([^;]*\b38\b/, `${type} must not style its own icon disc`);
  assert.doesNotMatch(source, /apply_radius\([^;]*scale_480\(22\)\s*-/, `${type} must not derive its own disc radius`);
  assert.doesNotMatch(source, /GRID_CELL_H\s*-\s*GRID_GAP\)\s*\/\s*2/, `${type} must not size its own disc`);
  assert.doesNotMatch(source, /lv_obj_align\(\s*(?:widgets?\.)?icon_(?:lbl|label),\s*LV_ALIGN_TOP_(?:LEFT|RIGHT)/,
    `${type} still places a header icon outside the disc`);
}
// Number, Select and Date/Time render through the Sensor tile.
for (const type of ['number', 'select', 'datetime']) {
  assert.match(read(`src/types/${type}/renderer.cpp`), /render_sensor_tile\(/, `${type} uses the Sensor header`);
}
// Graph and gauge tiles raise the disc, not the icon inside it.
const sensor = code(read('src/types/sensor/renderer.cpp'));
assert.equal((sensor.match(/lv_obj_move_foreground\(tile_icon_disc::outer\(icon_lbl\)\)/g) || []).length, 2);
// Half-height tiles keep using the compact path instead of the full header.
for (const type of ['sensor', 'binary_sensor', 'energy']) {
  assert.match(code(read(`src/types/${type}/renderer.cpp`)), /if \(!compact\)/, `${type} keeps compact tiles unchanged`);
}

// Runtime icon hide/show toggles the disc too, so an empty disc never shows.
for (const [file, count] of [['src/tiles/runtime/tile_renderer.cpp', 3],
                             ['src/types/binary_sensor/renderer.cpp', 2],
                             ['src/ui/tabs/tiles/tab_tiles_unified.cpp', 3]]) {
  const source = code(read(file));
  assert.equal((source.match(/tile_icon_disc::set_icon_hidden\(/g) || []).length, count, `${file} icon visibility`);
}
const weatherUpdate = code(read('src/tiles/runtime/tile_renderer.cpp'));
assert.doesNotMatch(weatherUpdate, /lv_obj_(?:add|clear)_flag\(widgets\.icon_label, LV_OBJ_FLAG_HIDDEN\);\s*\}\s*else/,
  'Weather icon visibility must include the disc');

// Popup header: one translucent circle of scale(72) left in the content,
// centered on the header line; the title starts after it with a scale(16) gap.
const layout = code(read('src/ui/popups/popup_layout.h'));
for (const marker of [
  'constexpr int kHeaderIconDiscSize = scale(72);',
  'constexpr int kHeaderIconDiscGap = scale(16);',
  'constexpr int kHeaderIconDiscOpa = 38;',
  'constexpr int kHeaderIconX = 0;',
  'constexpr int kHeaderTitleX = kHeaderIconDiscSize + kHeaderIconDiscGap;',
  'lv_obj_set_size(disc, kHeaderIconDiscSize, kHeaderIconDiscSize);',
  'lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);',
  'lv_obj_set_style_bg_opa(disc, static_cast<lv_opa_t>(kHeaderIconDiscOpa), 0);',
  'const int y = center - kHeaderIconDiscSize / 2;',
  'lv_obj_align(icon_disc, LV_ALIGN_TOP_LEFT, kHeaderIconX, y);',
  'lv_obj_set_width(icon, kHeaderIconDiscSize);',
  'lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);',
]) assert.ok(layout.includes(marker), `popup_layout: ${marker}`);
const discFunction = layout.slice(layout.indexOf('inline lv_obj_t* createHeaderIconDisc('));
assert.doesNotMatch(discFunction.slice(0, discFunction.indexOf('return disc;')), /apply_radius|kCloseButton/,
  'The popup header disc is a plain circle, independent of the global radius and the close button');
assert.doesNotMatch(layout, /constexpr int kHeaderTitleX = \d+;/, 'The title position follows the disc');
// The close button itself stays unchanged.
for (const marker of [
  'lv_obj_set_size(close_btn, kCloseButtonSize, kCloseButtonSize);',
  'lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, 0);',
  'lv_obj_set_style_bg_opa(close_btn, LV_OPA_20, LV_STATE_PRESSED);',
  'ui_surface_style::apply_radius(close_btn, kCloseButtonRadius, 0);',
  'lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, kCloseButtonOffsetX,',
]) assert.ok(layout.includes(marker), `close button: ${marker}`);

const shell = code(read('src/ui/popups/popup_shell.cpp'));
assert.ok(shell.includes('create_header(shell.header, shell.title, shell.icon, shell.close, close_clicked, nullptr,\n                &shell.icon_disc);'),
  'Only the visible shared header draws the disc');
assert.ok(shell.includes('create_header(parts.card, parts.title, parts.icon, parts.close, close_handler, context);'),
  'Invisible body headers get no disc');
assert.ok(shell.includes('popup_layout::styleHeaderIcon(icon);'));
assert.match(shell, /lv_obj_set_flag\(shell\.icon_disc, LV_OBJ_FLAG_HIDDEN,\s*lv_obj_has_flag\(shell\.icon, LV_OBJ_FLAG_HIDDEN\) \|\|\s*!lv_label_get_text\(shell\.icon\)\[0\]\);/,
  'The popup disc follows the header icon visibility');
assert.ok(shell.includes('popup_layout::alignHeader(shell.header, shell.title, shell.icon, shell.icon_disc);'));
assert.ok(shell.indexOf('popup_layout::createHeaderIconDisc(parent)') < shell.indexOf('icon = lv_label_create(parent);'),
  'The disc is drawn below the header icon');

// Every popup presents the shared header; none builds its own close/header.
for (const popup of ['camera', 'climate', 'cover', 'energy', 'light', 'media', 'pin', 'sensor', 'weather']) {
  const source = code(read(`src/ui/popups/${popup}/${popup}_popup.cpp`));
  assert.match(source, /create_popup_body\(/, `${popup} uses the shared popup body`);
  assert.match(source, /show_popup_shell\(/, `${popup} presents the shared header`);
  assert.doesNotMatch(source, /createCloseButton\(|createHeaderIconDisc\(/, `${popup} builds its own header`);
}
assert.match(read('src/ui/popups/binary_sensor/binary_sensor_popup.cpp'), /show_sensor_popup\(sensor_init\);/,
  'Binary Sensor uses the Sensor popup header');

console.log('Tile icons and the popup header icon share their background shapes.');

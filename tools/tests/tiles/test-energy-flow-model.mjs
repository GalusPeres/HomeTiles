// Energy flow tile numbers and geometry (types/energy_flow/flow_model.h,
// flow_geometry.h) compiled on the host: the V2's 2x2 tile is exactly the
// decided mockup (build/design-mockups/ideen-1023/energie-stand.html,
// picture 65), the six flows split like Home Assistant's energy
// distribution card, idle noise stays grey, power units become watts, and
// the Web Admin preview (types/energy_flow/admin.js) computes the same
// layout and flows as the device for every tile size.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: Energy flow model needs a native C++ compiler'); process.exit(0); }

// width, height, ring, disc, inset x, inset y, spacing, header room (the
// last two: with the header on, user 2026-10-26).
const sizes = [[404, 320, 34, 28, 24, 20, 18, 0], [352, 296, 34, 28, 24, 20, 18, 0], [232, 232, 25, 21, 16, 13, 12, 0],
  [600, 320, 34, 28, 24, 20, 18, 0], [404, 488, 34, 28, 24, 20, 18, 0], [404, 320, 34, 28, 24, 20, 18, 48],
  [298, 319, 36, 30, 18, 14, 18, 58]];
const cases = [
  // pv, grid, battery, home, has: pv grid battery home
  [0, 200, 500, 0, 1, 1, 1, 0],
  [3000, -1000, -500, 0, 1, 1, 1, 0],
  [0, 1500, -1000, 0, 1, 1, 1, 0],
  [100, -600, 800, 0, 1, 1, 1, 0],
  [0, 5, 0, 0, 1, 1, 1, 0],
  [1200, 300, 0, 1650, 1, 1, 1, 1],
  [-15, 400, 0, 0, 1, 1, 0, 0],
  [0, 0, 0, 0, 0, 0, 0, 0],
];
const cpp = `
#include <cstdio>
#include "src/types/energy_flow/flow_geometry.h"
using namespace energy_flow;
int main() {
  const float sizes[][8] = {${sizes.map(s => '{' + s.join(',') + '}').join(',')}};
  printf("{\\"layouts\\":[");
  for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
    Metrics m; m.ring = sizes[s][2]; m.disc = sizes[s][3]; m.inset_x = sizes[s][4]; m.inset_y = sizes[s][5]; m.spacing = sizes[s][6];
    const Layout l = layout(sizes[s][0], sizes[s][1], m, sizes[s][7]);
    if (sizes[s][7] && header_top(m) != 2 * m.ring - m.inset_y) return 1;
    printf("%s{\\"nodes\\":[", s ? "," : "");
    for (int n = 0; n < kNodeCount; ++n) printf("%s[%.4f,%.4f]", n ? "," : "", l.node[n].x, l.node[n].y);
    printf("],\\"lines\\":[");
    for (int i = 0; i < kLineCount; ++i) {
      printf("%s[", i ? "," : "");
      for (int p = 0; p < l.line[i].count; ++p) printf("%s[%.4f,%.4f]", p ? "," : "", l.line[i].p[p].x, l.line[i].p[p].y);
      printf("]");
    }
    printf("],\\"bends\\":[");
    for (int i = 0; i < kLineCount; ++i) {
      const Polyline& line = l.line[i];
      printf("%s[%d,%d,%d,%d,%d,%d,%d,%d]", i ? "," : "", line.bend.present ? 1 : 0, line.bend.cx, line.bend.cy,
             line.bend.radius, line.bend.start_angle, line.bend.end_angle, line.bend_first, line.bend_last);
    }
    const Point mid = point_at(l.line[kLinePvHome], polyline_length(l.line[kLinePvHome]) / 2);
    printf("],\\"mid\\":[%.4f,%.4f]}", mid.x, mid.y);
  }
  const float cases[][8] = {${cases.map(c => '{' + c.join(',') + '}').join(',')}};
  printf("],\\"flows\\":[");
  for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
    Inputs in; in.pv = cases[c][0]; in.grid = cases[c][1]; in.battery = cases[c][2]; in.home = cases[c][3];
    in.has_pv = cases[c][4]; in.has_grid = cases[c][5]; in.has_battery = cases[c][6]; in.has_home = cases[c][7];
    const Flows f = compute(in);
    printf("%s{\\"watts\\":[", c ? "," : "");
    for (int i = 0; i < kLineCount; ++i) printf("%s%.3f", i ? "," : "", f.watts[i]);
    printf("],\\"gridToBattery\\":%s,\\"home\\":%.3f,\\"homeKnown\\":%s,\\"homeSource\\":%d,\\"batteryGridSource\\":%d}",
           f.grid_to_battery ? "true" : "false", f.home, f.home_known ? "true" : "false", f.home_source,
           line_source(kLineBatteryGrid, f));
  }
  const char* units[] = {"W", "kW", "KW", "MW", "GW", "mW", "", "kWh", "%", " kW"};
  printf("],\\"units\\":[");
  for (int u = 0; u < 10; ++u) { float factor = 0; const bool ok = unit_factor(units[u], factor); printf("%s[%s,%.4f]", u ? "," : "", ok ? "true" : "false", factor); }
  const float speeds[][2] = {{5700, 5700}, {4100, 5700}, {1900, 5700}, {400, 400}, {20, 400}, {12000, 12000}, {6000, 12000}, {0, 100}};
  printf("],\\"speeds\\":[");
  for (int i = 0; i < 8; ++i) printf("%s%.4f", i ? "," : "", dot_speed(speeds[i][0], speeds[i][1]));
  printf("]}\\n");
}`;
const out = path.join(root, 'build/tests/energy-flow-model');
fs.mkdirSync(out, {recursive: true});
const cppPath = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(cppPath, cpp);
let r = spawnSync(compiler, ['-std=c++17', '-I', root, cppPath, '-o', binary], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
r = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
const device = JSON.parse(r.stdout);
const near = (a, b, label, eps = 0.02) => assert.ok(Math.abs(a - b) <= eps, `${label}: ${a} vs ${b}`);

// The dots' speed follows the power (user 2026-10-26, "immer richtig
// aufgeteilt"): the strongest flow at its power / 2500 W px per frame
// between 0.5 and 3, the others in proportion, none below 0.15, none idle.
const [top, mid, low, alone, tiny, capped, half, idle] = device.speeds;
near(top, 2.28, 'strongest 5.7 kW', 0.001); near(mid, 2.28 * 4100 / 5700, '4.1 kW of 5.7', 0.001);
near(low, 2.28 * 1900 / 5700, '1.9 kW of 5.7', 0.001);
near(alone, 0.5, 'a lone 400 W at the slowest top', 0.001); near(tiny, 0.15, '20 W still moves', 0.001);
near(capped, 3, '12 kW capped', 0.001); near(half, 1.5, 'half of the capped', 0.001); assert.equal(idle, 0, 'no flow, no dot');

// The V2 2x2 tile is the mockup: node centres, straight lines on the disc
// circles, the legs 18 beside the middle into lanes 18 off the middle, one
// quarter bend without a straight piece, ending on the target disc.
const v2 = device.layouts[0];
assert.deepEqual(v2.nodes.map(p => p.map(Math.round)), [[202, 54], [58, 160], [346, 160], [202, 266]]);
assert.deepEqual(v2.lines[0], [[86, 160], [318, 160]], 'grid -> home');
assert.deepEqual(v2.lines[1], [[202, 82], [202, 238]], 'solar -> battery');
const pvHome = v2.lines[2];
near(pvHome[0][0], 220, 'pv leg x'); near(pvHome[0][1], 54 + Math.sqrt(28 * 28 - 18 * 18), 'pv leg start');
// One true quarter circle on whole pixels (b532's ten short lines looked
// wavy, user 2026-10-24): centre (287, 75), radius 67, 90..180 degrees,
// right from the start (no straight piece), then the lane.
assert.deepEqual(v2.bends[2], [1, 287, 75, 67, 90, 180, 0, 16]);
assert.equal(pvHome.length, 18, 'start, the bend, the lane end');
for (const [x, y] of pvHome.slice(1, 17)) near(Math.hypot(x - 287, y - 75), 67, 'on the circle', 0.01);
near(pvHome[16][0], 287, 'bend ends one radius right'); near(pvHome[16][1], 142, 'bend ends in the lane');
near(pvHome[17][0], 346 - Math.sqrt(28 * 28 - 18 * 18), 'lane ends on the home disc'); near(pvHome[17][1], 142, 'lane');
assert.deepEqual(v2.bends.map(b => b.slice(4, 6)), [[0, 0], [0, 0], [90, 180], [0, 90], [180, 270], [270, 360]],
  'each leg turns through its own quarter');
assert.deepEqual(v2.bends[0].slice(0, 1).concat(v2.bends[1].slice(0, 1)), [0, 0], 'the straight lines have no bend');
const pvGrid = v2.lines[3];
near(pvGrid[0][0], 184, 'pv -> grid leg left of the middle'); near(pvGrid[pvGrid.length - 1][0], 58 + Math.sqrt(28 * 28 - 18 * 18), 'grid end');
const batHome = v2.lines[4];
near(batHome[0][1], 266 - Math.sqrt(28 * 28 - 18 * 18), 'battery leg starts on top of the battery'); near(batHome[batHome.length - 1][1], 178, 'lower lane');
// The mockup's cubic (k 0.552) and the circle lie within half a pixel.
const R = 142 - pvHome[0][1], k = 0.552;
const cubic = t => {
  const u = 1 - t, y0 = pvHome[0][1];
  return [u * u * u * 220 + 3 * u * u * t * 220 + 3 * u * t * t * (220 + (1 - k) * R) + t * t * t * (220 + R),
    u * u * u * y0 + 3 * u * u * t * (y0 + k * R) + 3 * u * t * t * 142 + t * t * t * 142];
};
for (let t = 0; t <= 1.0001; t += 0.05) {
  const [x, y] = cubic(t);
  near(Math.hypot(x - 287, y - 75), 67, `mockup cubic at ${t.toFixed(2)}`, 0.5);
}
// A square tile has no room for the full bend: a straight piece first, the
// bend never passes its target disc.
const square = device.layouts[2];
for (const line of square.lines.slice(2)) {
  for (const [x] of line) assert.ok(x >= square.nodes[1][0] - 0.01 && x <= square.nodes[2][0] + 0.01, 'inside the nodes');
}
const squareLeg = square.lines[2];
assert.ok(squareLeg[1][0] === squareLeg[0][0] && squareLeg[1][1] > squareLeg[0][1] + 0.5, 'square tile: a straight piece first');
near(squareLeg[squareLeg.length - 1][0], square.nodes[2][0] - Math.sqrt(21 * 21 - 12 * 12), 'square tile: ends on the home disc', 0.5);

// Flows like Home Assistant: 200 W from the grid and 500 W from the battery
// make 700 W at home in the battery's color (user 2026-10-24).
const [gridBat, pvAll, gridCharges, batteryExports, noise, homeSensor, nightPv, nothing] = device.flows;
assert.deepEqual(gridBat.watts, [200, 0, 0, 0, 500, 0]); assert.equal(gridBat.home, 700); assert.equal(gridBat.homeSource, 3);
assert.deepEqual(pvAll.watts, [0, 500, 1500, 1000, 0, 0]); assert.equal(pvAll.homeSource, 0);
assert.deepEqual(gridCharges.watts, [500, 0, 0, 0, 0, 1000]); assert.equal(gridCharges.gridToBattery, true);
assert.equal(gridCharges.batteryGridSource, 1, 'the grid charging the battery draws in the grid color');
assert.deepEqual(batteryExports.watts, [0, 0, 0, 100, 300, 500]); assert.equal(batteryExports.gridToBattery, false);
assert.equal(batteryExports.batteryGridSource, 3);
assert.deepEqual(noise.watts, [0, 0, 0, 0, 0, 0], 'below 10 W stays idle'); assert.equal(noise.homeSource, 4, 'no source');
assert.equal(homeSensor.home, 1650, 'a home sensor wins over the sum');
assert.deepEqual(nightPv.watts, [400, 0, 0, 0, 0, 0], 'negative solar counts as 0');
assert.equal(nothing.homeKnown, false);
assert.deepEqual(device.units, [[true, 1], [true, 1000], [true, 1000], [true, 1e6], [true, 1e9], [true, 0.001], [true, 1],
  [false, 1], [false, 1], [true, 1000]]);

// The Web Admin preview runs the same numbers (admin.js, line for line).
const admin = fs.readFileSync(path.join(root, 'src/types/energy_flow/admin.js'), 'utf8');
const context = vm.createContext({Math, Number, String, Object, Array});
vm.runInContext(admin + '\nthis.api = {energyFlowLayout, energyFlowCompute, energyFlowPointAt, energyFlowUnitFactor, ' +
  'energyFlowRecordFromInputs, energyFlowOptionBits};', context);
const {energyFlowLayout, energyFlowCompute, energyFlowPointAt} = context.api;
const order = ['gridHome', 'pvBattery', 'pvHome', 'pvGrid', 'batHome', 'batGrid'];
sizes.forEach((s, i) => {
  const l = energyFlowLayout(s[0], s[1], {ring: s[2], disc: s[3], insetX: s[4], insetY: s[5], spacing: s[6]}, s[7]);
  // With the header the flow fills the tile below its room: solar right
  // under it, grid and home in the middle of what is left, the battery at
  // the bottom as before.
  near(l.node.pv[1], s[7] + s[5] + s[2], `${s} solar under the header`);
  near(l.node.grid[1], (s[7] + s[1]) / 2, `${s} middle below the header`);
  near(l.node.battery[1], s[1] - s[5] - s[2], `${s} battery at the bottom`);
  ['pv', 'grid', 'home', 'battery'].forEach((name, n) => {
    near(l.node[name][0], device.layouts[i].nodes[n][0], `${s} ${name} x`); near(l.node[name][1], device.layouts[i].nodes[n][1], `${s} ${name} y`);
  });
  order.forEach((name, n) => {
    const want = device.layouts[i].lines[n];
    assert.equal(l.lines[name].p.length, want.length, `${s} ${name} points`);
    l.lines[name].p.forEach((p, j) => { near(p[0], want[j][0], `${s} ${name} ${j} x`); near(p[1], want[j][1], `${s} ${name} ${j} y`); });
    const bend = device.layouts[i].bends[n], b = l.lines[name].bend;
    assert.deepEqual(b ? [1, b.cx, b.cy, b.radius, l.lines[name].bendFirst, l.lines[name].bendLast] : [0],
      bend[0] ? [1, bend[1], bend[2], bend[3], bend[6], bend[7]] : [0], `${s} ${name} bend`);
  });
  const line = l.lines.pvHome;
  const mid = energyFlowPointAt(line, line.along[line.along.length - 1] / 2);
  near(mid[0], device.layouts[i].mid[0], 'mid x', 0.05); near(mid[1], device.layouts[i].mid[1], 'mid y', 0.05);
});
cases.forEach((c, i) => {
  const f = energyFlowCompute({pv: c[0], grid: c[1], battery: c[2], home: c[3], hasPv: !!c[4], hasGrid: !!c[5],
    hasBattery: !!c[6], hasHome: !!c[7]});
  order.forEach((name, n) => near(f.watts[name], device.flows[i].watts[n], `case ${i} ${name}`));
  assert.equal(f.gridToBattery, device.flows[i].gridToBattery, `case ${i} direction`);
  near(f.home, device.flows[i].home, `case ${i} home`);
  assert.equal(f.homeKnown, device.flows[i].homeKnown, `case ${i} home known`);
  assert.equal(f.homeSource, ({0: 'pv', 1: 'grid', 3: 'battery', 4: ''})[device.flows[i].homeSource], `case ${i} home source`);
});
console.log('Energy flow: the V2 2x2 tile is the mockup, Home Assistant flow split, idle noise, units, and the preview matches the device');

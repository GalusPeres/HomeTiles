// Issue #157: a Sensor tile with an empty decimals field showed a text value
// such as "4, 7, 9m" as "4" after another field of the tile was edited. The
// editor's copy of the tile took the empty field as Number('') = 0, the field
// then loaded "0", and the next save sent 0 to the panel. Empty stays "as it
// comes": -1 in the editor (as the server sends it), 0xFF on the panel.
import assert from 'node:assert/strict';

import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

// ---- The real delivered functions -------------------------------------------
const tilesData = {};
const applySnapshotToTileData = new Function('tilesData', `
  const getTilesData = tab => tilesData[tab];
  const normalizeSnapshotLayout = snapshot => ({
    col: Number(snapshot.col) || 0, row: Number(snapshot.row) || 0,
    span_w: Number(snapshot.span_w) || 1, span_h: Number(snapshot.span_h) || 1});
  const clampInt = (value, min, max, fallback) => {
    const number = Number(value);
    return Number.isFinite(number) ? Math.min(max, Math.max(min, Math.round(number))) : fallback;
  };
  const snapshotBgColorIsDefault = () => true;
  const makeTileBgValue = value => value;
  const hexToRgb = () => 0;
  ${extractDeliveredFunction('applySnapshotToTileData')}
  return applySnapshotToTileData;`)(tilesData);
// The number helpers around it as plain English formatting.
const formatSensorValue = new Function(`
  const localizeNumericText = text => text;
  const formatLocalizedNumber = (number, decimals) => number.toFixed(decimals);
  ${extractDeliveredFunction('formatSensorValue')}
  return formatSensorValue;`)();

const tile = (decimals) => ({type: '1', title: 'U-Bahn', sensor_entity: 'sensor.departures',
                             sensor_decimals: decimals, col: 0, row: 0, span_w: 1, span_h: 1});

// The user edits the title of a tile whose decimals field is empty.
tilesData.home = [{type: 1, title: 'Old', sensor_entity: 'sensor.departures', sensor_decimals: -1}];
applySnapshotToTileData('home', 0, tile(''));
assert.equal(tilesData.home[0].sensor_decimals, -1, 'an empty field stays empty, not 0');
assert.equal(formatSensorValue('4, 7, 9m', tilesData.home[0].sensor_decimals), '4, 7, 9m',
  'the text value is shown as it comes');
// Loading the tile again fills the field as loadSensorFields does: empty.
const loaded = tilesData.home[0].sensor_decimals;
assert.equal((loaded !== undefined && loaded >= 0) ? String(loaded) : '', '');

for (const blank of ['  ', null, undefined]) {
  applySnapshotToTileData('home', 0, tile(blank));
  assert.equal(tilesData.home[0].sensor_decimals, -1, `blank ${JSON.stringify(blank)} stays empty`);
}

// A number the user typed is kept, 0 included.
applySnapshotToTileData('home', 0, tile('0'));
assert.equal(tilesData.home[0].sensor_decimals, 0);
assert.equal(formatSensorValue('21.46', 0), '21');
applySnapshotToTileData('home', 0, tile('2'));
assert.equal(tilesData.home[0].sensor_decimals, 2);

console.log('Sensor decimals empty field (#157): PASS');

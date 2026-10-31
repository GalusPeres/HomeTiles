import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const toolsDir = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(toolsDir, '../../..');
const network = fs.readFileSync(
  path.join(root, 'src/network/network_manager.cpp'),
  'utf8',
);
const init = network.match(
  /void HomeTilesNetworkManager::init\(\)[\s\S]*?\r?\n}\r?\n/,
)?.[0] ?? '';

const pins = init.indexOf('WiFi.setPins(18, 19, 17, 16, 15, 14, 32);');
const transportStart = init.indexOf('networkTransport.begin();');
const stationStart = init.indexOf('ensureWifiStationStarted();');
assert.ok(pins >= 0, 'Elecrow must configure its SDIO and reset pins');
assert.ok(
  pins < transportStart && pins < stationStart,
  'Elecrow pins must be configured before ESP-Hosted starts',
);

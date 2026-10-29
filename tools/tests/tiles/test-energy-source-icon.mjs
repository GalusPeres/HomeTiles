// Energy tiles show their source's icon automatically on every panel (user
// 2026-10-15, Tab5 b468: "Automatisch funktioniert bei Energy nicht, kein
// Icon"). The Bridge configuration names only the Energy categories released
// under Energy Dashboard; with none released an Energy tile drew no icon while
// its value came. The received Energy responses carry every asked source's
// category, so the device, the live and cached Web Admin previews and the
// Bridge's search rows all use the category icon (energySourceIcon).
import assert from 'node:assert/strict';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {extractFunction, readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file);
const definition = (file, name) => {
  const found = cppFunctionDefinitions(read(file)).find(f => f.name === name);
  assert.ok(found, `${name} must exist in ${file}`);
  return found.source;
};

// One rule, used by the configuration's energy section and the responses.
const bridgeConfig = read('src/network/bridge/ha_bridge_config.cpp');
assert.match(bridgeConfig, /icon_updates\.push_back\(\{id, energySourceIcon\(category, id, unit\)\}\);/);
for (const file of ['src/network/bridge/ha_bridge_config.cpp', 'src/types/energy/energy_data.cpp']) {
  assert.doesNotMatch(read(file), /energyIconForCategory|icon_for_energy/, `${file}: no second copy of the rule`);
}

// Device: the tile, the icon refresh and the response that brings a new icon.
assert.match(read('src/types/energy/renderer.cpp'),
  /icon_name = normalizeMdiIconName\(energy_source_icon\(tile\.sensor_entity\)\);/);
assert.match(read('src/ui/tabs/tiles/tab_tiles_unified.cpp'),
  /\} else if \(tile\.type == TILE_ENERGY\) \{\s*icon_name = normalizeMdiIconName\(energy_source_icon\(tile\.sensor_entity\)\);\s*\} else if \(tile\.sensor_entity\.length\(\)\) \{/,
  'The icon refresh resolves Energy tiles like their renderer');
const parse = definition('src/types/energy/energy_data.cpp', 'parse_energy_response');
assert.match(parse, /if \(!energy_source_icon\(entry\.id\)\.length\(\)\) \{\s*new_icon = true;[\s\S]*?cache_for_period\(period\) = parsed;\s*if \(new_icon\) tiles_request_icon_refresh\(\);/,
  'A source without an icon so far redraws the tile icons once its response is cached');

// Web Admin: the energy list and the previews' icon map. The panel's own
// list (the picker's Favorites) holds the released categories and its own
// Energy tiles' sources, never every source a response named (user
// 2026-10-15: without a password and with nothing released the Favorites
// offered all 21 sources, under ids instead of names).
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
const ownList = tiles.slice(tiles.indexOf('json += ",\\"energy\\":[";'), tiles.indexOf('json += "],\\"scenes\\":[";'));
assert.match(ownList, /auto energy_ids = parseSensorList\(ha\.energy_text\);\s*energy_append_tile_entity_ids\(energy_ids\);/);
assert.doesNotMatch(ownList, /energy_append_cached_entity_ids/, 'No source just because a response named it');
assert.match(ownList, /String name = energy_source_name\(id\);\s*if \(!name\.length\(\)\) name = humanizeIdentifier\(id, true\);/);
assert.match(ownList, /appendEntry\(id, name, energy_source_icon\(id\), String\(\), first\);/);
const tileIds = definition('src/types/energy/energy_data.cpp', 'energy_append_tile_entity_ids');
assert.match(tileIds, /for \(const FolderEntry& folder : tileConfig\.getFolders\(\)\)[\s\S]*if \(slots\[i\]\.type == TILE_ENERGY\) append_unique\(ids, slots\[i\]\.entity\);/);
assert.match(tileIds, /screensaverConfig\.tileGrid\(\)[\s\S]*if \(screensaver\.tiles\[i\]\.type == TILE_ENERGY\)/);
assert.match(tiles, /json \+= "\},\\"energy_icons\\":\{";[\s\S]*?if \(haBridgeConfig\.findEntityIcon\(id\)\.length\(\)\) continue;\s*String icon = energy_source_icon\(id\);/);
const normalize = new Function(`${extractFunction('normalizeSensorMetaPayload')}; return normalizeSensorMetaPayload;`)();
const meta = normalize({values: {}, icons: {'sensor.pv': 'mdi:solar-panel'},
  energy_icons: {'sensor.pv': 'solar-power', 'sensor.bat': 'battery-charging'}});
assert.deepEqual(meta.icons, {'sensor.pv': 'mdi:solar-panel', 'sensor.bat': 'battery-charging'},
  'Energy icons fill the previews\' map, the configuration\'s icons win like on the device');
assert.deepEqual(normalize(meta).icons, meta.icons, 'A second normalization keeps them');

// The rule and the fallback on the host.
const harness = String.raw`
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string& s) : std::string(s) {}
  void trim() {
    const size_t b = find_first_not_of(" \t\r\n");
    if (b == npos) { clear(); return; }
    *this = String(substr(b, find_last_not_of(" \t\r\n") - b + 1));
  }
  void toLowerCase() { for (auto& c : *this) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
  bool endsWith(const char* s) const { const size_t n = std::strlen(s); return size() >= n && compare(size() - n, n, s) == 0; }
  bool equalsIgnoreCase(const String& o) const {
    if (size() != o.size()) return false;
    for (size_t i = 0; i < size(); ++i)
      if (std::tolower(static_cast<unsigned char>((*this)[i])) != std::tolower(static_cast<unsigned char>(o[i]))) return false;
    return true;
  }
};
struct EnergyEntryData { String id, unit, category, name; };
std::vector<EnergyEntryData> g_day_entries, g_week_entries, g_month_entries, g_year_entries;
struct FakeBridgeConfig {
  std::map<std::string, String> icons, names;
  String findEntityIcon(const String& id) const { auto it = icons.find(id); return it == icons.end() ? String() : it->second; }
  String findSensorName(const String& id) const { auto it = names.find(id); return it == names.end() ? String() : it->second; }
} haBridgeConfig;
${definition('src/network/bridge/ha_bridge_config.cpp', 'energySourceIcon')}
${definition('src/types/energy/energy_data.cpp', 'cached_entry')}
${definition('src/types/energy/energy_data.cpp', 'energy_source_icon')}
${definition('src/types/energy/energy_data.cpp', 'energy_source_name')}
int failures = 0;
void expect(const String& got, const char* want, const char* what) {
  if (got != want) { std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want); ++failures; }
}
int main() {
  expect(energy_source_icon("sensor.battery_in"), "", "No configuration icon and no response: none");
  expect(energy_source_name("sensor.bwwp_energie"), "", "No configuration name and no response: none");
  g_day_entries.push_back({"sensor.Battery_In", "kWh", "battery", "In Batterie"});
  g_day_entries.push_back({"solar_total", "kWh", "solar", "PV gesamt"});
  g_day_entries.push_back({"grid_total_cost", "", "grid", ""});
  g_day_entries.push_back({"sensor.grid_price", "EUR", "grid", ""});
  g_day_entries.push_back({"sensor.heat_pump", "kWh", "device", ""});
  g_day_entries.push_back({"sensor.bwwp_energie", "kWh", "device", "Brauchwasser WP"});
  g_week_entries.push_back({"sensor.gas", "m3", " Gas ", ""});
  expect(energy_source_name("sensor.bwwp_energie"), "Brauchwasser WP", "The response's name");
  expect(energy_source_name("SOLAR_TOTAL"), "PV gesamt", "Totals by their name, id case ignored");
  haBridgeConfig.names["sensor.bwwp_energie"] = "Warmwasser";
  expect(energy_source_name("sensor.bwwp_energie"), "Warmwasser", "The configuration's name wins");
  expect(energy_source_icon("sensor.battery_in"), "battery-charging", "Battery source from the response, id case ignored");
  expect(energy_source_icon("solar_total"), "solar-power", "The Bridge's totals by their category");
  expect(energy_source_icon("grid_total_cost"), "currency-eur", "Costs by their id");
  expect(energy_source_icon("sensor.grid_price"), "currency-eur", "Costs by their unit");
  expect(energy_source_icon("sensor.heat_pump"), "lightning-bolt", "Other categories");
  expect(energy_source_icon("sensor.gas"), "fire", "Any received period");
  haBridgeConfig.icons["sensor.battery_in"] = "mdi:home-battery";
  expect(energy_source_icon("sensor.battery_in"), "mdi:home-battery", "The configuration's icon wins");
  expect(energy_source_icon(""), "", "No source");
  if (failures) return 1;
  std::printf("ok\n");
  return 0;
}
`;
const output = compileAndRun({label: 'Energy source icons', harness});
if (output !== null) assert.equal(output.trim(), 'ok');
console.log('Energy tiles get their source\'s category icon without released categories (device, previews, list)');

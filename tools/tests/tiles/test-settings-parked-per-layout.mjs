// Settings parked per layout (user 2026-10-13: "for every layout one decides
// whether Settings is there or not"). Each layout keeps its own choice
// (DeviceConfig::settings_parked, bit n for layout n); a panel from before
// parked it in every layout or in none. The layout a boot shows applies its
// bit before the tiles load; parked there, the tile leaves Home with its
// look, place and ID in the snapshot (also when it showed in the layout
// before), and comes back with its ID, so its places in the layouts stay:
// a Home save keeps them for the parked tile. The editor parks in the
// panel's layout; the layout window sets each layout's choice and the
// panel's at once (/api/layouts). The window itself: test-layout-window-fields.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const configH = readRepoFile('src/core/config/config_manager.h');
const config = readRepoFile('src/core/config/config_manager.cpp');
assert.match(configH, /uint8_t settings_parked;/);
assert.match(configH, /constexpr uint8_t kSettingsParkedAll = 0x07;/);
assert.match(configH, /struct SettingsTileSnapshot \{[\s\S]*?uint16_t view_id;\s*\};/, 'The snapshot keeps the tile ID');
assert.match(config, /config\.settings_parked = prefs\.isKey\("set_parked"\)\s*\? static_cast<uint8_t>\(prefs\.getUChar\("set_parked", 0\) & kSettingsParkedAll\)\s*: \(config\.settings_tile_hidden \? kSettingsParkedAll : 0\);/,
  'A panel from before: parked in every layout or in none');
assert.match(config, /stored_snapshot\.view_id = stored_snapshot\.valid \? prefs\.getUShort\("set_view", 0\) : 0;/);
assert.match(config, /prefs\.putUChar\("set_parked", normalized\.settings_parked & kSettingsParkedAll\);\s*prefs\.putUShort\("set_view", normalized\.settings_tile_snapshot\.valid \? normalized\.settings_tile_snapshot\.view_id : 0\);/);
assert.match(config, /a\.settings_parked == b\.settings_parked &&/);
assert.match(config, /a\.settings_tile_snapshot\.view_id == b\.settings_tile_snapshot\.view_id;/);
assert.equal(config.match(/config\.settings_tile_hidden = false;\s*config\.settings_parked = 0;/g)?.length, 2, 'Both defaults');
assert.match(config, /void ConfigManager::applySettingsParking\(uint8_t layout\) \{\s*config\.settings_tile_hidden = layout < 3 && \(\(config\.settings_parked >> layout\) & 1\);\s*\/\/[^\n]*\n\s*if \(config\.settings_tile_hidden\) config\.settings_swipe_enabled = true;/);
// Before the tiles load, after the layout of this boot is known.
assert.match(readRepoFile('HomeTiles.ino'),
  /grid_layout::apply\(configManager\.getConfig\(\)\.layout\);[\s\S]{0,300}configManager\.applySettingsParking\(static_cast<uint8_t>\(grid_layout::active\(\)\)\);[\s\S]*?tileConfig\.load\(\);/);

const tiles = readRepoFile('src/tiles/config/tile_config.cpp');
// Parked with its ID, one cell (user 2026-10-22).
assert.match(tiles, /out\.span_w = 1;\s*out\.span_h = 1;\s*out\.view_id = tile\.view_id;/, 'Parked with its ID');
assert.match(tiles, /tile\.view_id = snapshot\.valid \? snapshot\.view_id : 0;\s*return true;/, 'Back with its ID');
const policy = tiles.slice(tiles.indexOf('bool TileConfig::applySettingsTilePolicy('), tiles.indexOf('bool TileConfig::ensureBackTile('));
assert.match(policy, /if \(kept\.valid && tile\.view_id && kept\.view_id == tile\.view_id\) break;[\s\S]*?snapshotSettingsTile\(tile, cfg->settings_tile_snapshot\);[\s\S]*?configManager\.save\(\*cfg\)[\s\S]*?return removeSettingsTiles\(grid\);/,
  'Parked in this layout, shown in the one before: its snapshot first');
// Parked before b445 (no ID in its snapshot): it gets one once, else the
// window had no tile for its field (user 2026-10-13: "where is Settings?").
assert.match(policy, /if \(!in_grid\) giveParkedSettingsTileId\(\);\s*return removeSettingsTiles\(grid\);/);
assert.match(tiles, /void TileConfig::giveParkedSettingsTileId\(\) \{\s*const SettingsTileSnapshot& kept = configManager\.getConfig\(\)\.settings_tile_snapshot;\s*if \(!kept\.valid \|\| kept\.view_id\) return;[\s\S]*?const uint16_t id = cfg \? reserveNavigationId\("tile_next", 1\) : 0;\s*if \(!id\) return;\s*cfg->settings_tile_snapshot\.view_id = id;/);
assert.match(readRepoFile('src/web/admin/tiles/page-fields.js'),
  /free\.hidden = !pageFieldsInWindow\(\) \|\| \(layoutWindowSettingsParked\(\) && layoutSettingsIndex\(\) >= 0\);/,
  'Parked without a tile to show: the free frame, never an empty spot');
assert.match(tiles, /if \(folder_id == TileConfig::rootFolderId\(\) &&\s*std::none_of\(grid\.tiles, grid\.tiles \+ TILES_PER_GRID, \[\]\(const Tile& tile\) \{ return tile\.type == TILE_SETTINGS; \}\)\) \{\s*const SettingsTileSnapshot& kept = configManager\.getConfig\(\)\.settings_tile_snapshot;\s*if \(kept\.valid && kept\.view_id\) views\.push_back\(kept\.view_id\);/,
  'A Home save keeps the parked tile\'s places (and no new tile takes them over)');
assert.match(readRepoFile('src/tiles/config/new_panel_tiles.cpp'),
  /cfg->settings_parked = \(1u << static_cast<uint8_t>\(grid_layout::Layout::kBar\)\) \|/);

// The editor parks in the panel's layout.
assert.match(readRepoFile('src/web/server/handlers/web_admin_handlers.cpp'),
  /cfg\.settings_tile_hidden = hide_tile;[\s\S]{0,300}const uint8_t shown_layout_bit = static_cast<uint8_t>\(1u << static_cast<uint8_t>\(grid_layout::active\(\)\)\);\s*cfg\.settings_parked = hide_tile \? static_cast<uint8_t>\(cfg\.settings_parked \| shown_layout_bit\)\s*: static_cast<uint8_t>\(cfg\.settings_parked & ~shown_layout_bit\);/);

// The layout window: each layout's choice, the parked tile with its ID.
const api = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp');
const get = api.slice(api.indexOf('void WebAdminServer::handleGetLayouts()'), api.indexOf('bool layoutFromKey('));
assert.match(get, /json \+= "\},\\"settings\\":\{\\"parked\\":\[";[\s\S]*?json \+= \(cfg\.settings_parked >> i\) & 1 \? "true" : "false";/);
assert.match(get, /if \(kept\.valid && kept\.view_id && tileConfig\.settingsTileIndex\(\) < 0\) \{\s*json \+= ",\\"tile\\":\{\\"type\\":7,\\"view_id\\":" \+ String\(kept\.view_id\)/);
const post = api.slice(api.indexOf('void WebAdminServer::handleSaveLayouts()'), api.indexOf('void WebAdminServer::handleSwitchLayout()'));
assert.match(post, /JsonVariantConst settings_parked_sent = doc\["settings_parked"\];/);
assert.match(post, /if \(folder_id == TileConfig::rootFolderId\(\) && parked_settings_view\) \{[\s\S]*?tile\.type = TILE_SETTINGS;\s*tile\.view_id = parked_settings_view;/,
  'The parked tile takes part with its ID: its places are checked and kept');
assert.match(post, /if \(tile\.type == TILE_SETTINGS && entry == places\.end\(\)\) continue;/, 'Parked in classic: no storage tile');
assert.match(post, /if \(folder_id == home_id && parked_settings_view\) \{[\s\S]*?parked_settings_place = \{tile\.col, tile\.row, tile\.span_w, tile\.span_h\};\s*tile = Tile\(\);/,
  'The grid stays without the parked tile, its classic place goes into its snapshot');
assert.match(post, /cfg->settings_parked = parked \? static_cast<uint8_t>\(cfg->settings_parked \| bit\)\s*: static_cast<uint8_t>\(cfg->settings_parked & ~bit\);\s*if \(layout == grid_layout::active\(\) && parked != cfg->settings_tile_hidden\) \{/);
assert.match(post, /ok = tileConfig\.setSettingsTileVisible\(!cfg->settings_tile_hidden\) == SettingsTileVisibilityResult::Success;/,
  'In the panel\'s layout it leaves Home or comes back at once');
console.log('Settings parked per layout: stored per layout, applied at boot, kept ID and places, editor and layout window');

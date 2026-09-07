import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8')
  .replace(/\r\n?/g, '\n');
const requireMarker = (source, marker, label) => {
  if (!source.includes(marker)) throw new Error(`${label} is missing: ${marker}`);
};

const pinHeader = read('src/core/config/pin_access.h');
const pinSource = read('src/core/config/pin_access.cpp');
const configHeader = read('src/core/config/config_manager.h');
const configSource = read('src/core/config/config_manager.cpp');
const settingsAccessRecord = read('src/core/config/settings_access_record.h');
const batchedNvs = read('src/core/config/batched_nvs_write.h');
const tileHeader = read('src/tiles/config/tile_config.h');
const tileSource = read('src/tiles/config/tile_config.cpp');
const handlers = read('src/web/server/handlers/web_admin_handlers.cpp') +
  read('src/web/server/handlers/web_admin_tiles.cpp');
const admin = read('src/web/assets/admin.js');

for (const marker of [
  'kUserPinMinDigits = 4',
  'kUserPinMaxDigits = 8',
  'kInputMaxDigits = 9',
]) requireMarker(pinHeader, marker, 'PIN bounds');

for (const marker of [
  'const char kRecoveryPin[] = "466384537"',
  'constexpr char kHashDomain[] = "HomeTiles-PIN-v1"',
  'esp_fill_random(salt, kSaltSize)',
  'mbedtls_sha256(input, offset, hash, 0)',
  'constantTimeEqual(candidate, hash, sizeof(candidate))',
  'secureClear(candidate, sizeof(candidate))',
]) requireMarker(pinSource, marker, 'PIN credential implementation');

if (/Serial\.|printf\(|ESP_LOG/.test(pinSource)) {
  throw new Error('PIN credential code must never log PIN processing');
}

for (const marker of [
  'struct SettingsTileSnapshot {',
  'bool valid;',
  'char title[256];',
  'char icon_name[32];',
  'uint32_t bg_color;',
  'bool settings_pin_enabled;',
  'bool settings_tile_hidden;',
  'bool settings_swipe_enabled;',
  'uint8_t settings_pin_salt[pin_access::kSaltSize];',
  'uint8_t settings_pin_hash[pin_access::kHashSize];',
  'char settings_pin_value[pin_access::kUserPinMaxDigits + 1];',
  'bool getSettingsPin(String& out) const;',
  'SettingsTileSnapshot settings_tile_snapshot;',
]) requireMarker(configHeader, marker, 'Settings access persistence');

for (const marker of [
  'SETTINGS_ACCESS_KEY = "set_access_v1"',
  'struct __attribute__((packed)) LegacySettingsAccessRecord',
  'struct __attribute__((packed)) SettingsAccessRecordV3',
  'struct __attribute__((packed)) SettingsAccessRecord',
  'kSettingsAccessVersion = 4',
  'static_assert(sizeof(LegacySettingsAccessRecord) == 60',
  'static_assert(sizeof(SettingsAccessRecordV3) == 132',
  'static_assert(sizeof(SettingsAccessRecord) == 144',
  'apply_settings_access_record_v3',
  'record.pin_length = static_cast<uint8_t>(stored_pin.length());',
  'pin_access::verifyCredential(candidate_pin.c_str(), record.salt,',
  'pin_access::secureClear(config.settings_pin_value,',
  'bool ConfigManager::getSettingsPin(String& out) const',
  'kSettingsAccessSwipe = 1U << 2',
  'kSettingsTileSnapshotValid = 1U << 0',
  'settings_access_checksum(record)',
  'apply_legacy_settings_access_record',
  'config.settings_tile_snapshot',
  'snapshot_title[32]',
  'snapshot_icon_name[32]',
  'fail_open_settings_access(config)',
  'prefs.putBytes(SETTINGS_ACCESS_KEY, &settings_access',
  'settings_access_written || !snapshot_title_written || !transaction_finished',
  'pin_access::isRecoveryPin(pin)',
]) requireMarker(configSource + settingsAccessRecord, marker, 'ConfigManager PIN contract');
requireMarker(configSource, '#include "src/core/config/settings_access_record.h"',
              'ConfigManager settings record codec dependency');
requireMarker(
  configSource,
  'if (normalized.settings_tile_hidden) {\n    normalized.settings_swipe_enabled = true;',
  'Hidden Settings recovery swipe normalization');
requireMarker(
  handlers,
  'cfg.settings_swipe_enabled = hide_tile || enable_swipe;',
  'Hidden Settings recovery swipe HTTP invariant');
requireMarker(pinSource, 'salt_combined != 0 && hash_combined != 0',
              'Complete salt and hash validation');
requireMarker(batchedNvs, 'nvs_set_blob(handle_, key, value, length)',
              'S3 batched NVS blob support');

for (const marker of [
  'static const char* kFolderAccessFile = "/_tile_grids/folder_access.bin";',
  'static constexpr uint16_t kFolderAccessVersion = 2;',
  'static_assert(sizeof(FolderAccessDiskV1) == 52',
  'static_assert(sizeof(FolderAccessDisk) == 60',
  '(header.version != 1 && header.version != kFolderAccessVersion)',
  'disk.pin_length >= pin_access::kUserPinMinDigits',
  'pin_access::verifyCredential(stored_pin.c_str(), disk.salt,',
  'copyString(pin, entry.pin_value, sizeof(entry.pin_value));',
  'pin_access::secureClear(entry.pin_value, sizeof(entry.pin_value));',
  'Missing access metadata means unlocked.',
  'bool TileConfig::setFolderPin',
  'bool TileConfig::verifyFolderPin',
  'bool TileConfig::getFolderPin',
  'SettingsTileVisibilityResult TileConfig::validateSettingsTileVisible',
  'SettingsTileVisibilityResult TileConfig::setSettingsTileVisible',
  'bool TileConfig::getSettingsTile',
  'configManager.getConfig().settings_tile_snapshot',
  'settings_tile_rect_is_free',
  'find_settings_tile_rect_bottom_right',
  'return SettingsTileVisibilityResult::NoFreeCell;',
  'if (!saveFolderAccess()) {\n    folders = original;',
]) requireMarker(tileSource, marker, 'Tile access sidecar contract');

if (!/struct FolderEntryDisk \{[\s\S]*?char icon_name\[32\];[\s\S]*?\};/.test(tileSource)) {
  throw new Error('The backward-compatible folders.bin record is missing');
}
const folderDiskStart = tileSource.indexOf('struct FolderEntryDisk {');
const folderDiskEnd = tileSource.indexOf('};', folderDiskStart);
const folderDisk = tileSource.slice(folderDiskStart, folderDiskEnd + 2);
if (/pin_[a-z]/.test(folderDisk)) {
  throw new Error('PIN fields leaked into the rollback-compatible folders.bin record');
}

for (const marker of [
  'bool getSettingsTile(Tile& out);',
  'SettingsTileVisibilityResult validateSettingsTileVisible(',
  'bool visible, int target_col = -1, int target_row = -1);',
  'SettingsTileVisibilityResult setSettingsTileVisible(',
]) requireMarker(tileHeader, marker, 'Settings tile visibility API');

for (const marker of [
  'tileConfig.validateSettingsTileVisible(',
  'settings_tile_target_col',
  'settings_tile_target_row',
  'SettingsTileSnapshot& snapshot = cfg.settings_tile_snapshot;',
  'server.hasArg("settings_tile_snapshot_present")',
  'server.arg("settings_tile_title")',
  'server.arg("settings_tile_icon")',
  'server.arg("settings_tile_bg_color")',
  'server.hasArg("settings_tile_col")',
  'server.hasArg("settings_tile_row")',
  'server.hasArg("settings_tile_span_w")',
  'server.hasArg("settings_tile_span_h")',
  'new_pin.toCharArray(cfg.settings_pin_value,',
  'configManager.getSettingsPin(stored_pin)',
  ',\\"settings_pin\\":\\"',
  'snapshot.col = static_cast<uint8_t>(snapshot_col);',
  'snapshot.row = static_cast<uint8_t>(snapshot_row);',
  'snapshot.span_w = static_cast<uint8_t>(snapshot_span_w);',
  'snapshot.span_h = static_cast<uint8_t>(snapshot_span_h);',
  'tileConfig.getSettingsTile(settings_tile)',
  'SettingsTileVisibilityResult::NoFreeCell ? 409 : 500',
  'settings_config_rolled_back = configManager.save(previous_cfg);',
  'server.on("/api/folders/access"',
]) {
  const source = marker.startsWith('server.on')
    ? read('src/web/server/web_admin.cpp') : handlers;
  requireMarker(source, marker, 'Web access transaction');
}

const saveFolderGridStart = tileSource.indexOf(
  'bool TileConfig::saveFolderGrid');
const saveFolderGridEnd = tileSource.indexOf(
  'bool TileConfig::saveScreensaverGrid', saveFolderGridStart);
const saveFolderGrid = tileSource.slice(saveFolderGridStart, saveFolderGridEnd);
for (const marker of [
  'active_grid = grid;',
  'applySettingsTilePolicy(active_grid);',
]) requireMarker(saveFolderGrid, marker, 'Canonical active Settings grid cache');
if (/TileGridConfig\s+\w+\s*=\s*grid/.test(saveFolderGrid)) {
  throw new Error(
    'saveFolderGrid must not add a full grid copy to the WebServer task stack');
}

const validatePosition = handlers.indexOf(
  'tileConfig.validateSettingsTileVisible(');
const configSavePosition = handlers.indexOf(
  'settings_config_saved = configManager.save(cfg);', validatePosition);
const visibilityCommitPosition = handlers.indexOf(
  'tileConfig.setSettingsTileVisible(', configSavePosition);
if (validatePosition < 0 || configSavePosition < validatePosition ||
    visibilityCommitPosition < configSavePosition) {
  throw new Error('Settings visibility must preflight, save config, then commit the grid');
}

for (const marker of [
  'delete exported.folder_pin_enabled;',
  'delete exported.folder_pin;',
  'icon_name: String(folder?.icon_name || \'\')',
]) requireMarker(admin, marker, 'Access export sanitization');

const saveNavigate = admin.slice(
  admin.indexOf('function saveNavigateFields'),
  admin.indexOf('function resetNavigateFields'));
if (/folder_pin|settings_pin|pin_hash|pin_salt/.test(saveNavigate)) {
  throw new Error('Folder PIN data entered the normal tile draft/save payload');
}

console.log('PIN persistence and export contract tests passed.');

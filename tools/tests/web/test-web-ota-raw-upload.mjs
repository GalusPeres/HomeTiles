import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8').replace(/\r\n/g, '\n');
}

function requireMarker(source, marker, label) {
  if (!source.includes(marker)) {
    throw new Error(`${label} is missing: ${marker}`);
  }
}

function requireOrder(source, markers, label) {
  let position = -1;
  for (const marker of markers) {
    const next = source.indexOf(marker, position + 1);
    if (next < 0) throw new Error(`${label} is missing: ${marker}`);
    position = next;
  }
}

function functionBody(source, startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start + startMarker.length);
  if (start < 0 || end < 0) {
    throw new Error(`Could not isolate ${startMarker}`);
  }
  return source.slice(start, end);
}

const server = read('src/web/server/web_admin.cpp');
const handlers = read('src/web/server/handlers/web_admin_ota.cpp');
const admin = read('src/web/assets/admin.js');
const firmwareMetadata = read('src/core/firmware/firmware_metadata.cpp');

for (const marker of [
  '#if defined(CONFIG_IDF_TARGET_ESP32P4)',
  '#define FW_META_SILICON_MIN_REV CONFIG_ESP_REV_MIN_FULL',
  '#define FW_META_SILICON_MAX_REV CONFIG_ESP_REV_MAX_FULL',
  '#define FW_META_SILICON_VARIANT "post_v3"',
  '#define FW_META_SILICON_MIN_REV 301',
  '#define FW_META_SILICON_MAX_REV 399',
  '#error "Every ESP32-P4 build must target one unambiguous silicon generation"',
  'chip_revision < kCurrentFirmwareDescriptor.silicon.minimum_revision',
  'chip_revision > kCurrentFirmwareDescriptor.silicon.maximum_revision',
  'incoming_minimum_revision >=',
  'incoming_maximum_revision <=',
  'matchesCurrentSiliconRevisionRange(incoming.minimum_revision,',
]) {
  requireMarker(firmwareMetadata, marker, 'All-P4 silicon metadata contract');
}
const siliconRangeGuard = functionBody(
  firmwareMetadata,
  'bool matchesCurrentSiliconRevisionRange(',
  'const char* expectedDeviceDisplayName()');
requireOrder(siliconRangeGuard, [
  'incoming_minimum_revision <= incoming_maximum_revision',
  'incoming_minimum_revision >=',
  'kCurrentFirmwareDescriptor.silicon.minimum_revision',
  'incoming_maximum_revision <=',
  'kCurrentFirmwareDescriptor.silicon.maximum_revision',
], 'Incoming silicon range containment guard');
const imageSiliconGuard = functionBody(
  firmwareMetadata,
  'bool imageMatchesCurrentSiliconVariant(',
  '}  // namespace firmware_meta');
requireOrder(imageSiliconGuard, [
  'matchesCurrentSiliconVariant(incoming.variant)',
  'matchesCurrentSiliconRevisionRange(incoming.minimum_revision,',
  'chip_revision >= incoming.minimum_revision',
  'chip_revision <= incoming.maximum_revision',
], 'OTA image silicon range guard');

const stagingPolicy = functionBody(
  handlers,
  'constexpr uint32_t kOtaDisplayRestoreRetryMs',
  'constexpr size_t kOtaFlashWriteChunk');
requireOrder(stagingPolicy, [
  '#if defined(CONFIG_IDF_TARGET_ESP32P4)',
  'constexpr bool kStageWebOtaInPsram = true;',
  '#else',
  'constexpr bool kStageWebOtaInPsram = false;',
  'constexpr bool kRequireWebOtaSize = kStageWebOtaInPsram;',
], 'All-P4 Web OTA staging policy');
if (stagingPolicy.includes('DEVICE_WAVESHARE') ||
    stagingPolicy.includes('DEVICE_GUITION_JC') ||
    stagingPolicy.includes('DEVICE_M5STACKS')) {
  throw new Error('P4 Web OTA staging is restricted to a device allow-list');
}

requireMarker(server, '"/api/ota/upload/raw", HTTP_POST,',
  'Raw OTA route');
// Both OTA routes pass the optional Web Admin password gate; upload chunks of
// an unauthorised request never reach the OTA writer.
requireMarker(server,
  '"/api/ota/upload", HTTP_POST,\n        guardedUploadDone([this]() { this->handleOtaUploadDone(); }),',
  'Legacy multipart OTA route');
requireMarker(server, 'guardedUpload([this]() { this->handleOtaUpdate(); }));',
  'Legacy multipart OTA callback');
requireMarker(server, 'guardedRaw([this]() { this->handleOtaRawUpdate(); }));',
  'Raw OTA callback');

const browserUpload = functionBody(
  admin,
  'async function uploadOtaFirmware()',
  '// Tile Editor State');
requireOrder(browserUpload, [
  "fetch('/api/ota/prepare?size=' + otaSize + '&filename=' + otaFilename",
  "xhr.open('POST', '/api/ota/upload/raw?size=' + otaSize + '&filename=' + otaFilename",
  "xhr.setRequestHeader('Content-Type', 'application/octet-stream');",
  "xhr.setRequestHeader('X-HomeTiles-OTA-Filename', otaFilename);",
  'xhr.send(file);',
], 'Browser raw OTA flow');
if (browserUpload.includes("formData.append('firmware', file)") ||
    browserUpload.includes('xhr.send(formData)')) {
  throw new Error('Browser OTA still sends multipart FormData');
}

const rawBegin = functionBody(
  handlers,
  'void beginRawOtaUpload(WebServer& server)',
  'void writeRawOtaChunk');
requireOrder(rawBegin, [
  'const bool was_prepared = g_ota_upload_state.upload_prepared;',
  'const size_t prepared_size = g_ota_upload_state.upload_total_bytes;',
  'parseOtaSizeValue(server.header("Content-Length"), content_length)',
  'g_ota_upload_state.upload_started = true;',
  'g_ota_upload_state.upload_total_bytes =',
  'prepared_size ? prepared_size : content_length;',
  'content_length != prepared_size',
  'allocateOtaStagingBuffer(g_ota_upload_state.upload_total_bytes)',
], 'Raw OTA prepared-size contract');
if (rawBegin.includes('server.arg(') ||
    rawBegin.includes('parseOtaExpectedSize(server)')) {
  throw new Error('Raw OTA incorrectly relies on unparsed WebServer query args');
}
for (const marker of [
  'application/octet-stream',
  'No firmware file received',
  'Please upload a .bin firmware file',
  'Please upload the update.bin, not the factory.bin',
]) {
  requireMarker(rawBegin, marker, 'Raw OTA request validation');
}

const rawMetadata = functionBody(
  handlers,
  'bool validateRawOtaImageMetadata',
  'void beginRawOtaUpload');
for (const marker of [
  'firmware_meta::parseDeviceDescriptorFromImage(',
  'firmware_meta::matchesCurrentDeviceKey(incoming_desc.device_key)',
  'firmware_meta::currentProjectKey()',
  'firmware_meta::currentSiliconRevisionDescriptor()',
  'Current firmware silicon range ',
  'firmware_meta::imageMatchesCurrentSiliconVariant(',
  'firmware_meta::matchesCurrentSiliconRevisionRange(',
  'exceeds supported range ',
  'Firmware silicon range ',
  'ESP.getChipRevision()',
]) {
  requireMarker(rawMetadata, marker, 'Raw OTA firmware metadata guard');
}

const rawWrite = functionBody(
  handlers,
  'void writeRawOtaChunk',
  'void abortRawOtaUpload');
for (const marker of [
  'hasOtaStagingBuffer()',
  'copyToOtaStagingBuffer(',
  'validateRawOtaImageMetadata(',
  'beginDirectOtaInstall()',
  'writeDirectOtaChunk(',
]) {
  requireMarker(rawWrite, marker, 'Raw OTA P4/S3 write path');
}

const rawFinish = functionBody(
  handlers,
  'void finishRawOtaUpload',
  'void WebAdminServer::handlePrepareOtaUpload()');
requireOrder(rawFinish, [
  'parser_total_bytes != g_ota_upload_state.upload_received_bytes',
  'if (hasOtaStagingBuffer())',
  'g_ota_upload_state.staged_bytes != parser_total_bytes',
  'if (!g_ota_upload_state.image_validated)',
  '[OTA] Raw upload RX finished:',
  '[OTA] Network receive complete; starting flash install',
  'beginDirectOtaInstall()',
  'while (offset < g_ota_upload_state.staged_bytes)',
  'std::min(std::min(remaining, kOtaFlashWriteChunk)',
  'Update.end(true)',
], 'P4 raw OTA receive-then-flash ordering');
for (const marker of [
  'parser_total_bytes != g_ota_upload_state.upload_received_bytes',
  'parser_total_bytes != g_ota_upload_state.upload_total_bytes',
  'while (offset < g_ota_upload_state.staged_bytes)',
  'Update.end(true)',
  '#if defined(DEVICE_ESP32_S3_RGB_480)',
  'Update.end(false)',
  'endOtaStorageGuard();',
]) {
  requireMarker(rawFinish, marker, 'Raw OTA finalization contract');
}

const rawHandler = functionBody(
  handlers,
  'void WebAdminServer::handleOtaRawUpdate()',
  'void WebAdminServer::handleOtaUploadDone()');
for (const marker of [
  'HTTPRaw& raw = server.raw();',
  'RAW_START',
  'RAW_WRITE',
  'RAW_ABORTED',
  'RAW_END',
]) {
  requireMarker(rawHandler, marker, 'Raw OTA status handler');
}

const legacyHandler = functionBody(
  handlers,
  'void WebAdminServer::handleOtaUpdate()',
  'void WebAdminServer::handleOtaRawUpdate()');
for (const marker of [
  'HTTPUpload& upload = server.upload();',
  'UPLOAD_FILE_START',
  'UPLOAD_FILE_WRITE',
  'UPLOAD_FILE_ABORTED',
  'UPLOAD_FILE_END',
  'validateRawOtaImageMetadata(',
  'copyToOtaStagingBuffer(',
  'beginDirectOtaInstall()',
  'Update.end(true)',
  '#if defined(DEVICE_ESP32_S3_RGB_480)',
  'Update.end(false)',
]) {
  requireMarker(legacyHandler, marker, 'Legacy multipart OTA contract');
}
for (const marker of [
  'firmware_meta::parseDeviceDescriptorFromImage(',
  'firmware_meta::matchesCurrentDeviceKey(',
  'firmware_meta::currentProjectKey()',
  'firmware_meta::imageMatchesCurrentSiliconVariant(',
]) {
  requireMarker(handlers, marker, 'Shared OTA firmware metadata validation');
}

const legacyFinish = functionBody(
  legacyHandler,
  'if (upload.status == UPLOAD_FILE_END)',
  'Serial.printf("[OTA] Upload finished:');
requireOrder(legacyFinish, [
  'if (hasOtaStagingBuffer())',
  'g_ota_upload_state.staged_bytes != upload.totalSize',
  'if (!g_ota_upload_state.image_validated)',
  '[OTA] Upload RX finished:',
  '[OTA] Network receive complete; starting flash install',
  'beginDirectOtaInstall()',
  'while (offset < g_ota_upload_state.staged_bytes)',
  'std::min(remaining, kOtaFlashWriteChunk)',
  'Update.end(true)',
], 'P4 legacy OTA receive-then-flash ordering');

console.log('Buffered Web Admin raw OTA upload contract: PASS');

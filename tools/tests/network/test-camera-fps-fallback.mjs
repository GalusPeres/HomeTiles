// Camera popups ask the Bridge for 30 FPS. Bridges before v0.7.1b9 reject
// anything above 24 with camera_invalid_stream_request; the popup must then
// ask once more at 24 instead of showing an error, so new firmware keeps its
// camera on an older Bridge.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (relativePath) => fs.readFileSync(path.join(repoRoot, relativePath), 'utf8');

const geometry = read('src/video/camera_geometry.h');
const popup = read('src/ui/popups/camera/camera_popup.cpp');
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
const mqttHeader = read('src/network/mqtt/mqtt_handlers.h');

assert.match(geometry, /inline constexpr uint8_t kFps = 30;/);
assert.match(geometry, /inline constexpr uint8_t kFallbackFps = 24;/);

// The open command carries the requested rate; 0 keeps kFps.
assert.match(
  mqttHeader,
  /void mqttPublishCameraCommand\(const char\* entity_id, const char\* command,\s*uint8_t fps = 0\);/,
);
assert.match(mqtt, /fps \? fps : camera_geometry::kFps\);/);

// Every open starts at kFps again.
assert.match(
  popup,
  /requested_fps = camera_geometry::kFps;\s*mqttPublishCameraCommand\(init\.entity_id\.c_str\(\), "open",\s*g_camera_popup->requested_fps\);/,
);

// Exactly one retry at the fallback rate, and only for the rate rejection.
const errorBranch = popup.slice(
  popup.indexOf('if (strcmp(status, "error") == 0) {'),
  popup.indexOf('if (strcmp(status, "stopped") == 0) {'),
);
assert.match(
  errorBranch,
  /strcmp\(error, "camera_invalid_stream_request"\) == 0 &&\s*g_camera_popup->requested_fps > camera_geometry::kFallbackFps/,
);
const retryIndex = errorBranch.indexOf('requested_fps = camera_geometry::kFallbackFps;');
const publishIndex = errorBranch.indexOf('mqttPublishCameraCommand(g_camera_popup->entity_id.c_str(), "open",');
const errorStatusIndex = errorBranch.indexOf('camera_popup_set_status(localize_camera_error(error), true);');
assert.ok(retryIndex > 0 && publishIndex > retryIndex, 'the retry must lower the rate before asking again');
assert.ok(
  errorBranch.slice(retryIndex, errorStatusIndex).includes('return;'),
  'the retry must not show the error',
);
assert.ok(
  errorBranch.slice(retryIndex, publishIndex).includes('waiting_for_bridge = true;') &&
    errorBranch.slice(retryIndex, publishIndex).includes('bridge_response_deadline_ms ='),
  'the retry must restart the Bridge response deadline',
);

// A ready answer up to kFps is accepted (24 from an older Bridge as well).
assert.match(popup, /fps < 1 \|\| fps > camera_geometry::kFps/);

console.log('Camera FPS fallback contract: PASS');

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (relativePath) => fs.readFileSync(path.join(repoRoot, relativePath), 'utf8');

const popup = read('src/ui/popups/camera/camera_popup.cpp');
const stringsHeader = read('src/core/i18n/i18n.h');
const stringsSource = read('src/core/i18n/i18n.cpp');
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');

assert.match(popup, /kBridgeResponseTimeoutMs\s*=\s*30000/);
assert.ok(
  popup.includes('camera_popup_set_status(camera_text().camera_bridge_no_response, true);'),
  'silence must be reported as a liveness failure',
);
assert.ok(
  popup.includes('camera_text().camera_bridge_update_required'),
  'an explicit incompatible protocol must still show the upgrade message',
);
assert.match(
  popup,
  /protocol_field\.isNull\(\)[\s\S]*?camera_invalid_response[\s\S]*?protocol_field\.as<uint8_t>\(\)/,
  'a missing or malformed protocol must be invalid, not an inferred upgrade requirement',
);
assert.match(
  popup,
  /void camera_popup_set_status\([\s\S]*?if \(error && g_camera_popup\)[\s\S]*?waiting_for_bridge = false;[\s\S]*?bridge_response_deadline_ms = 0;/,
  'specific MQTT and parse errors must cancel the generic response deadline',
);
assert.ok(stringsHeader.includes('camera_bridge_no_response'));
for (const translation of [
  'Keine Kamera-Antwort',
  'No camera response',
  'Aucune réponse caméra',
]) {
  assert.ok(stringsSource.includes(translation), `missing translation: ${translation}`);
}
for (const specificError of [
  'camera_mqtt_topic_missing',
  'camera_mqtt_queue_full',
]) {
  assert.ok(mqtt.includes(specificError), `missing MQTT error path: ${specificError}`);
}

// No connection to the Bridge (user 2026-11-07: "MQTT disconnected" while Home
// Assistant restarted): the popup waits without words, asks again once the
// connection is back, and tells only a connection gone for 30 s.
assert.doesNotMatch(mqtt, /camera_mqtt_disconnected/);
for (const name of ['mqttPublishCameraFullScreenOpen', 'mqttPublishCameraCommand']) {
  const body = mqtt.slice(mqtt.indexOf(`void ${name}(`), mqtt.indexOf('\n}\n', mqtt.indexOf(`void ${name}(`)));
  assert.match(body, /Without a connection the popup waits for it and asks again \(camera_popup\.cpp\)\.\s*if \(!networkManager\.isMqttConnected\(\)\) return;/,
    `${name} waits for the connection without an error`);
}
assert.match(popup, /constexpr uint32_t kBridgeLostTellMs = 30000;/);
assert.match(popup, /static void process_bridge_link\(CameraPopupContext\* ctx\) \{/);
assert.match(popup, /process_bridge_link\(g_camera_popup\);\s*if \(g_camera_popup->waiting_for_bridge &&/);
assert.match(popup, /if \(ctx->waiting_for_bridge\) ctx->bridge_response_deadline_ms = now \+ kBridgeResponseTimeoutMs;/);
assert.match(popup, /camera_popup_set_status\(camera_text\(\)\.camera_bridge_disconnected, true\);/);
assert.match(popup, /ctx->bridge_lost_told = false;\s*ctx->status_sticky = false;\s*camera_popup_set_status\(camera_text\(\)\.camera_bridge_requesting, false\);\s*reopen_view\(ctx\);/);
assert.match(popup, /const bool bridge_quiet = g_camera_popup->bridge_lost_ms && !g_camera_popup->bridge_lost_told;/);
assert.match(popup, /g_camera_popup->bridge_lost_ms = 0;\s*g_camera_popup->bridge_lost_told = false;/);
assert.ok(stringsHeader.includes('const char* camera_bridge_disconnected;'));
// The camera's connection texts name the Bridge, never MQTT, which a linked panel
// does not use; one per language.
for (const text of [
  'Keine Kamera-Antwort von der Bridge', 'Keine Verbindung zur Bridge',
  'No camera response from the Bridge', 'No connection to the Bridge',
  'Aucune réponse caméra du bridge', 'Pas de connexion au bridge',
  'Brak odpowiedzi kamery z mostu', 'Brak połączenia z mostem',
  'Žádná odpověď kamery od Bridge', 'Žádné spojení s Bridge',
  'Sin respuesta de la cámara desde el Bridge', 'Sin conexión con el Bridge',
  'Žiadna odpoveď kamery z Bridge', 'Žiadne spojenie s Bridge',
  'Нет ответа камеры от Bridge', 'Нет соединения с Bridge',
]) {
  assert.ok(stringsSource.includes(`    "${text}",\n`), `missing translation: ${text}`);
}
assert.doesNotMatch(stringsSource, /MQTT (oder|or|ou|lub|nebo|o|alebo|или) ([Bb]ridge|most)/);

console.log('Camera bridge timeout contract: PASS');

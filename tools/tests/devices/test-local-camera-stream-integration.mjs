// Firmware wiring of the built-in camera live stream (panel -> Bridge): the
// Bridge capability, the camera popup priority rule, every stop path, the
// task/memory layout, the shared acknowledged-TCP helpers (the display stream
// keeps its behaviour) and the network recovery guard. The protocol itself is
// exercised on the host in tools/tests/network/test-local-camera-stream.mjs.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const service = read('src/video/local_camera/local_camera.cpp');
const upload = read('src/video/local_camera/local_camera_upload.cpp');
const contract = read('src/video/local_camera/local_camera_stream_contract.h');
const socketCpp = read('src/video/tcp_ack_socket.cpp');
const cameraStream = read('src/video/camera_stream.cpp');
const bridgeConfig = read('src/network/bridge/ha_bridge_config.cpp');
const network = read('src/network/network_manager.cpp');

function bodyOf(source, name) {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, `${name} must exist`);
  return found.body;
}
const svc = name => bodyOf(service, name);
const up = name => bodyOf(upload, name);

// --- Capability: next to "local_camera", same conditions ------------------------
assert.match(bridgeConfig,
  /json \+= ",\\"local_camera\\":";\s*json \+= local_camera::bridgeCapability\(\) \? "true" : "false";[\s\S]{0,160}json \+= ",\\"local_camera_stream\\":";\s*json \+= local_camera::bridgeStreamCapability\(\) \? "true" : "false";/);
assert.match(svc('bridgeStreamCapability'),
  /kSupported && Device::kCapabilities\.has_builtin_camera, g_enabled\.load\(\),\s*currentState\(\) == ServiceState::Ready/,
  'The stream capability requires the supported, enabled and detected camera');

// --- Camera popup priority ----------------------------------------------------------
assert.match(up('stopNow'), /return !g_run\.load\(\) \|\| camera_stream_is_active\(\);/,
  'The sender stops at its next socket poll when a popup stream runs');
assert.match(svc('streamStopCondition'), /if \(camera_stream_is_active\(\)\) return StopReason::Popup;/,
  'The capture loop stops for a popup stream');
assert.match(svc('currentGate'), /in\.popup_active = camera_stream_is_active\(\);/);
assert.match(svc('tryStartStream'), /local_camera_stream::streamGate\(currentGate\(\)\);\s*if \(gate != StopReason::None\)/,
  'A popup stream keeps the upload from starting');
// Only a keepalive (re)starts the upload; the periodic service only stops it.
assert.doesNotMatch(svc('serviceStream'), /tryStartStream|kNotifyStream\)/);
assert.match(svc('handleStreamCommand'), /tryStartStream\(\);/);
const tryStartCallers = [...maskCpp(service).matchAll(/tryStartStream\(\);/g)].length;
assert.equal(tryStartCallers, 1, 'tryStartStream is called from the keepalive handler only');
assert.match(svc('serviceStream'),
  /if \(local_camera_stream::reasonEndsSession\(reason\)\) \{\s*endStreamSession\(reason\);\s*\} else \{\s*requestStreamStop\(reason\);/,
  'Popup/sleep/errors stop the upload but keep the session for the next keepalive');

// --- Stop paths --------------------------------------------------------------------------
assert.match(svc('setEnabled'), /if \(g_worker\) g_abort\.store\(true\);\s*endStreamSession\(StopReason::Disabled\);/);
assert.match(svc('shutdown'), /g_stream_blocked_until_ms\.store\([\s\S]*endStreamSession\(StopReason::Shutdown\);[\s\S]*notifyWorker\(kNotifyShutdown\)/,
  'OTA/restart stops the stream and blocks keepalive restarts');
assert.match(svc('streamWait'), /if \(bits & kNotifyShutdown\) return StopReason::Shutdown;\s*if \(bits & kNotifyDisable\) return StopReason::Disabled;\s*if \(bits & kNotifyRelease\) return StopReason::Sleep;/);
assert.match(svc('streamWait'), /\*leftover \|= bits & \(kNotifyShutdown \| kNotifyDisable \| kNotifyRelease \| kNotifyProbe\);/,
  'Lifecycle notifications are handed back to the worker loop');
assert.match(svc('releaseForSleep'), /notifyWorker\(kNotifyRelease\);/);
assert.match(svc('currentGate'), /in\.display_sleeping = powerManager\.isInSleep\(\);/,
  'Keepalives defer while the display sleeps, so the sleep release is not undone');
assert.match(contract, /if \(in\.display_sleeping\) return StopReason::Sleep;/);
assert.match(svc('handleStreamStop'), /endStreamSession\(StopReason::StreamStop\);/);
assert.match(contract, /if \(!in\.mqtt_connected\) return StopReason::Mqtt;/);
assert.match(contract, /if \(in\.ttl_expired\) return StopReason::Keepalive;/);
assert.match(svc('currentGate'), /in\.mqtt_connected = networkManager\.isMqttConnected\(\);/);
assert.match(svc('currentGate'), /static_cast<int32_t>\(now_ms - g_session\.deadline_ms\) >= 0/,
  'ttl expiry is wrap-safe');
assert.match(svc('handleStreamCommand'), /g_session\.deadline_ms = millis\(\) \+ command\.ttl_ms;/);
assert.match(svc('handleStreamCommand'), /local_camera_upload::setEndpoint\(endpoint\);/,
  'A new session or endpoint replaces the running connection');
assert.match(up('setEndpoint'), /strcmp\(g_endpoint\.session, endpoint\.session\) != 0[\s\S]*g_generation\.fetch_add\(1\);/);
assert.match(up('runConnection'), /if \(g_generation\.load\(\) != generation\) \{\s*end = ConnectionEnd::Reconnect;/);
// A snapshot while the stream owns the pipeline (or storage work holds the
// camera off) is answered as busy.
assert.match(svc('handleMqttMessage'), /g_capture_busy\.load\(\) \|\|\s*g_stream_running\.load\(\) \|\| g_stream_wanted\.load\(\) \|\|\s*storageHoldActive\(\)\) \{[\s\S]*?code = ErrorCode::Busy;/);
assert.match(svc('streamWait'), /publishErrorReply\(g_pending\.id, ErrorCode::Busy\);/);

// --- Mode changes reconnect within the same session -----------------------------------
assert.match(svc('runStream'), /g_stream_mode_generation\.load\(\) != run\.mode_generation[\s\S]*?applyStreamSettings\(run\)[\s\S]*?local_camera_upload::requestReconnect\(\);/);
assert.match(svc('setStreamMode'), /isKnownMode\(mode\)[\s\S]*putUChar\(kPrefsStreamModeKey, mode\)[\s\S]*g_stream_mode_generation\.fetch_add\(1\);/);

// --- Tasks and memory ------------------------------------------------------------------------
assert.match(upload, /constexpr UBaseType_t kSenderPriority = tskIDLE_PRIORITY;/);
assert.match(up('start'), /xTaskCreatePinnedToCoreWithCaps\(senderMain, "localCamUp", kSenderStackBytes, nullptr,\s*kSenderPriority, &g_sender, core,\s*MALLOC_CAP_SPIRAM\)/,
  'The sender task runs at idle priority with a PSRAM stack');
assert.match(svc('runStream'), /local_camera_upload::start\(workerCore\(\)\)/);
assert.match(svc('workerCore'), /return \(ARDUINO_RUNNING_CORE == 0\) \? 1 : 0;/);
assert.match(svc('ensureWorker'), /const BaseType_t core = \(ARDUINO_RUNNING_CORE == 0\) \? 1 : 0;/,
  'Capture worker and sender share the camera core');
assert.doesNotMatch(maskCpp(upload), /vTaskDelete|xQueueCreate/,
  'The sender task is persistent and frames never queue');
assert.match(upload, /constexpr uint32_t kSlotCount = 2;/, 'Two latest-frame-wins slots');
assert.match(up('takeReadySlot'), /slot\.sequence > best->sequence/, 'The sender takes the newest frame');
assert.match(up('publishFrame'), /slot\.state == SlotState::Free[\s\S]*slot\.state == SlotState::Ready[\s\S]*was_ready = true;/,
  'A ready frame that was never sent is replaced, the one being sent never');
assert.match(up('ensureSlots'), /heap_caps_malloc\(kSlotBytes, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT\)/);
assert.match(upload, /constexpr uint32_t kSlotBytes = kMaxFrameBytes;/);
assert.match(contract, /constexpr uint32_t kMaxFrameBytes = local_camera_contract::kPanelMaxJpegBytes;/);
// No scale buffer: the stream encodes the CSI frame, so only the full image works.
assert.match(svc('applyStreamSettings'), /if \(settings\.width != kImageWidth \|\| settings\.height != kImageHeight\) return false;/);
assert.doesNotMatch(service, /freeStreamScale|run\.scale/, 'No stream scale buffer remains');
// Stop order: sender, then sensor/CSI, then buffers.
const run = svc('runStream');
assert.ok(run.indexOf('local_camera_upload::stop();') < run.indexOf('if (sensor_started) stopStreaming();'));
assert.ok(run.indexOf('if (sensor_started) stopStreaming();') < run.indexOf('g_isr.frozen = -1;\n  // After the sender stopped'));
assert.match(run, /if \(local_camera_upload::waitIdle\(kSenderStopWaitMs\)\) \{\s*local_camera_upload::releaseBuffers\(\);/);
assert.match(up('releaseBuffers'), /if \(g_busy\.load\(\)\) return;/, 'Slots stay while the sender may read them');

// --- Continuous sensor, paced deadlines, bounded AE ---------------------------------------
const captureFrame = svc('streamCaptureFrame');
assert.doesNotMatch(captureFrame, /setStream\(|esp_cam_ctlr_start|esp_cam_ctlr_stop|stopStreaming/,
  'The sensor streams for the whole session, never per frame');
assert.match(captureFrame, /g_isr\.armed = true;/);
// The sensor turns by 180 degrees, mirrors and crops (a CPU pass took
// ~107 ms, the PPA pass 45 ms per frame on the V2 and starved the display's
// PPA rotation, hardware logs 2026-09-24; a quarter-turn PPA pass on the
// 8-inch cost 29 ms per frame and made the display sluggish): the encoder
// reads the frozen CSI buffer, which is released only after the encode. A
// quarter-turn mounting is turned by the Bridge ("rotate" in the status).
assert.match(captureFrame, /const uint8_t\* input = g_isr\.buffers\[frozen\];/);
assert.match(captureFrame, /encodeStreamFrame\(input[\s\S]*?\}\s*\/\/[^\n]*\n\s*g_isr\.frozen = -1;/,
  'The CSI buffer is released after the encoder read it');
assert.doesNotMatch(captureFrame, /downscale2x2Rgb565|compactCenterCrop|std::reverse|imageRotated180|mirror_x|ppa|kQuarterTurn/,
  'No pixel pass (CPU or PPA) in the stream path');
assert.doesNotMatch(service, /ppa_do_scale_rotate_mirror|turnFrame|g_pipe\.turned/, 'No PPA turn on the panel');
assert.match(service, /static_assert\(kMode\.frame_width == kMode\.image_width &&\s*kMode\.frame_height == kMode\.image_height,/);
// Quarter turn: 4:2:0 so the Bridge's lossless turn keeps a common format.
assert.match(service, /constexpr jpeg_down_sampling_type_t kJpegSubsampling =\s*kQuarterTurn \? JPEG_DOWN_SAMPLING_YUV420 : JPEG_DOWN_SAMPLING_YUV422;/);
assert.equal((service.match(/config\.sub_sample = kJpegSubsampling;/g) || []).length, 2, 'Snapshot and stream');
assert.doesNotMatch(service, /config\.sub_sample = JPEG_DOWN_SAMPLING/);
assert.match(service, /constexpr uint16_t kStatusRotate = local_camera_board::kMode\.quarter_turn \? 90 : 0;/);
assert.match(svc('currentStatusFields'), /fields\.rotate = kStatusRotate;/);
// ISP statistics run on the frame as delivered.
assert.match(svc('createAutoExposure'), /config\.window\.btm_right\.x = kStatsLeft \+ kStatsWidth;/);
assert.match(service, /constexpr uint32_t kStatsWidth = kMode\.frame_width \/ 5 \* 5;/);
// No frames (b28 on the 8-inch: after a quick stop/start every retry saw no
// frame until the 30 s idle release): the run ends and the whole camera path
// is rebuilt, the snapshot path likewise.
assert.match(svc('resetAfterNoFrames'), /releaseAll\(\);/);
assert.match(service, /constexpr uint16_t kNoFrameResetStreak = 12;/);
assert.match(captureFrame, /\+\+window\.noframe;\s*if \(run\.noframe_streak < UINT16_MAX\) \+\+run\.noframe_streak;\s*return;\s*\}\s*run\.noframe_streak = 0;/);
assert.match(run, /streamCaptureFrame\(run\);\s*if \(run\.noframe_streak >= kNoFrameResetStreak\) \{\s*reason = StopReason::Error;\s*no_frames = true;\s*break;/);
assert.match(run, /logCaptureError\("Stream got no CSI frame", ESP_ERR_TIMEOUT\);\s*reason = StopReason::Error;\s*no_frames = true;/);
assert.ok(run.indexOf('local_camera_upload::stop();') < run.indexOf('if (no_frames) resetAfterNoFrames("stream");') &&
  run.indexOf('if (sensor_started) stopStreaming();') < run.indexOf('if (no_frames) resetAfterNoFrames("stream");'),
  'Rebuild only after the sender and the sensor stopped');
assert.match(svc('captureJpeg'), /resetAfterNoFrames\("snapshot"\);\s*\*detail = Detail::NoFrames;/);// Orientation: display rotation and the mirror setting are sensor flips,
// written in standby before stream on and live between frames.
assert.match(run, /applyOrientation\(false\)[\s\S]*?esp_cam_ctlr_start\([\s\S]*?setStream\(true\)/,
  'The stream starts in the wanted sensor orientation');
assert.match(run, /applyImageSettingsIfChanged\(\);\s*\/\/[^\n]*\n\s*if \(!applyOrientation\(true\)\) \{\s*reason = StopReason::Error;/,
  'A rotation or mirror change during the stream turns the sensor readout');
const orientation = svc('applyOrientation');
assert.match(orientation, /desiredOrientation\(imageRotated180\(\), g_mirror\.load\(\), kQuarterTurn\)/);
assert.match(orientation, /if \(code == g_applied_orientation\) return true;/, 'No SCCB write per frame');
assert.match(orientation, /xQueueReset\(g_isr\.frames\);[\s\S]*?kOrientationSettleFrames/,
  'Frames in flight during a live change are dropped');
assert.match(captureFrame, /reducedQualityForSize\(run\.quality\)/, 'An oversized frame lowers the quality, never re-encodes');
// Noisy low-light frames: below the normal floor instead of sending nothing
// (Tab5 hardware 2026-09-25: 226 KB at q30, every frame dropped).
assert.doesNotMatch(captureFrame, /[^r]reducedQuality\(/, 'The size limit has its own lower floor');
assert.match(service, /constexpr uint8_t kJpegQualities\[\] = \{80, 65, 50, 38, 25, 15\};/,
  'Stills step down far enough for noisy frames');

// Every capture and stream starts from a pipeline that has not run yet: a stop
// could leave the CSI/ISP inside a frame (8-inch 2026-09-24: swapped colours or
// no frames after a restart, correct after the rebuild).
assert.match(service, /void releaseUsedPipeline\(\) \{\s*if \(g_pipe\.started\) releasePipeline\(\);\s*\}/);
assert.match(svc('releasePipeline'), /g_pipe\.ready = false;\s*g_pipe\.started = false;/);
assert.match(svc('captureJpeg'), /releaseUsedPipeline\(\);\s*if \(!ensurePipeline\(\)\)/,
  'A still starts from a fresh pipeline');
assert.match(run, /releaseUsedPipeline\(\);\s*if \(!ensurePipeline\(\) \|\| !applyStreamSettings\(run\)\)/,
  'A stream starts from a fresh pipeline');
for (const body of [svc('captureJpeg'), run]) {
  assert.match(body, /g_pipe\.csi_running = true;\s*g_pipe\.started = true;/, 'A started receiver marks the pipeline used');
}

// Max. gain (Web Admin) caps the sensor and the digital gain for stills, at the
// stream start and before every stream AE step (a change applies while live).
assert.match(svc('applyGainLimit'), /stages\.normal\.max_gain_x16 = limits\.sensor_gain_x16;\s*stages\.max_total_gain_x16 = limits\.sensor_total_gain_x16;\s*g_max_digital_step = limits\.max_digital_step;/);
assert.match(svc('applyGainLimit'), /if \(g_exposure\.gain_x16 > limits\.sensor_total_gain_x16\) \{\s*g_exposure\.gain_x16 = limits\.sensor_total_gain_x16;/,
  'A gain above a lowered limit drops at once');
assert.match(svc('captureJpeg'), /stages\.max_total_gain_x16 = kMode\.max_total_gain_x16;\s*applyGainLimit\(stages\);/);
assert.match(svc('applyStreamSettings'), /run\.stages\.max_total_gain_x16 = kMode\.max_total_gain_x16;\s*applyGainLimit\(run\.stages\);/);
assert.match(svc('streamAutoTune'), /run\.last_tune_ms = now_ms;\s*\/\/[^\n]*\n\s*applyGainLimit\(run\.stages\);/);
assert.match(svc('stepDigitalGain'), /sensor_at_brighter_limit, g_max_digital_step\);/);
assert.match(service, /if \(g_digital_step > currentGainLimits\(\)\.max_digital_step\) \{\s*g_digital_step = currentGainLimits\(\)\.max_digital_step;\s*\}\s*err = loadGammaCurve\(image\.contrast, g_digital_step\);/,
  'A new pipeline starts inside the limit');
assert.match(run, /if \(run\.pacer\.consume\(now_us\)\) \+\+run\.window\.late;/);
assert.match(svc('streamAutoTune'), /since_ms < kStreamTuneIntervalMs\) return;/);
assert.match(service, /constexpr uint32_t kStreamTuneIntervalMs = 250;/);
assert.match(service, /constexpr uint32_t kStreamAwbIntervalMs = 1000;/);
assert.match(service, /constexpr int kStreamStatisticsTimeoutMs = 40;/);
assert.match(service, /constexpr uint32_t kStreamSettleBudgetMs = 1000;/);
assert.match(svc('applyStreamSettings'), /maxExposureLinesForFps\(settings\.fps/,
  'Exposure stays within the mode frame interval');

// --- Diagnostics: one line per 10 s ------------------------------------------------------------
assert.match(contract, /constexpr uint32_t kDiagWindowMs = 10000;/);
assert.match(svc('streamDiagnostics'), /elapsed_ms < local_camera_stream::kDiagWindowMs\) return;/);
assert.match(svc('appendStatusJson'), /formatStatusJson\(stream_json, sizeof\(stream_json\), stream\)[\s\S]*",\\"stream\\":"/,
  '/api/status local_camera.stream carries the same numbers');
for (const [name, text] of [['streamCaptureFrame', captureFrame], ['sendJpegFrame', bodyOf(contract, 'sendJpegFrame')]]) {
  const prints = [...text.matchAll(/Serial\.print/g)].length;
  const limited = [...text.matchAll(/logDue\(/g)].length;
  assert.ok(prints <= limited, `${name} must not log per frame`);
}

// --- Sender transport ----------------------------------------------------------------------------
const connection = up('runConnection');
assert.match(connection, /options\.send_timeout = true;/, 'SO_SNDTIMEO on the upload socket');
assert.match(connection, /options\.stop = stopNowCallback;/);
assert.match(connection, /buildUploadHandshake\(line, sizeof\(line\), endpoint\.session, endpoint\.token\)/);
assert.match(connection, /helloAccepted\(hello, &chunk\)/);
assert.match(connection, /sendJpegFrame\(io, slot->sequence, slot->data, slot->bytes, chunk, &report\)/);
assert.match(connection, /io\.ignore_stop = true;\s*sendControl\(io, tcp_ack::kMessageEnd, last_sequence, kEndSendTimeoutMs\);/,
  'END is sent between frames with a short bound, then the socket closes');
assert.match(connection, /Mid-frame: an END header would be read as payload, so just close\./);
assert.match(upload, /bool waitHeadroomForChunk\(\) \{[\s\S]*?guard\.check\(headroom, millis\(\)\)/);
assert.match(socketCpp, /setsockopt\(fd, IPPROTO_TCP, TCP_NODELAY/);
assert.match(socketCpp, /setsockopt\(fd, SOL_SOCKET, SO_SNDTIMEO/);
assert.match(socketCpp, /#if defined\(CONFIG_IDF_TARGET_ESP32P4\)/);
assert.match(upload, /#if defined\(HOMETILES_LOCAL_CAMERA\)/);

// --- The display stream uses the same helpers with unchanged behaviour ---------------------
const activeCameraStream = maskCpp(cameraStream).replace(/#if 0[\s\S]*?#endif/g, '');
assert.doesNotMatch(activeCameraStream, /\bsocket\(|\brecv\(|[^_]send\(|getaddrinfo\(/,
  'camera_stream.cpp must not keep its own copy of the socket code');
const connect = bodyOf(cameraStream, 'connect_camera_socket');
assert.match(connect, /options\.log_prefix = "\[CameraStream\]";/);
assert.match(connect, /options\.receive_buffer_bytes = kCameraReceiveBufferBytes;/);
assert.doesNotMatch(connect, /options\.stop|send_timeout/,
  'The display stream keeps one uninterrupted connect select() and no send timeout');
assert.match(bodyOf(cameraStream, 'socket_send_all'), /tcp_ack::sendAll\(fd, source, bytes, camera_stop_requested, nullptr\)/,
  'No deadline: EAGAIN stays an error for the display stream');
assert.match(bodyOf(cameraStream, 'socket_receive_exact'), /tcp_ack::receiveExact\(fd, destination, bytes, camera_stop_requested,\s*nullptr\)/);
assert.match(cameraStream, /tcp_ack::DmaHeadroomGuard dma_guard\(kMinCameraDmaHeadroomBytes,\s*kDmaHeadroomGraceMs\);/);
assert.match(bodyOf(socketCpp, 'waitConnected'), /if \(options\.stop\) \{[\s\S]*if \(!options\.stop \|\| selected < 0\) return false;/,
  'Without a stop callback the connect wait is a single select()');

// --- Network recovery never tears the transport down under the upload socket --------------
assert.match(network, /if \(camera_stream_is_active\(\)\) \{\s*log_dma_wait\("Recovery waiting for camera to stop", dma_largest\);\s*return;\s*\}/);
assert.match(network,
  /if \(local_camera::streamActive\(\)\) \{\s*if \(local_camera::stopStreamForTransportRecovery\(\)\) \{[\s\S]*?\}\s*log_dma_wait\("Recovery waiting for camera upload to stop", dma_largest\);\s*return;\s*\}/,
  'DMA starvation stops the upload instead of waiting for the Bridge to end the session');
assert.match(svc('stopStreamForTransportRecovery'), /requestStreamStop\(StopReason::Error\);/,
  'The recovery stop ends the run with Error so keepalives back off');
assert.match(svc('streamActive'), /g_stream_running\.load\(\) \|\| g_stream_wanted\.load\(\) \|\| local_camera_upload::busy\(\)/);

console.log('Local camera live stream wiring, preemption, stop paths and shared transport passed.');

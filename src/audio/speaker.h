#pragma once

// Speaker playback for Home Assistant (Bridge media_player entity), on
// profiles with a speaker board file (HOMETILES_SPEAKER, speaker_select.h).
// On every other profile all functions are inexpensive stubs that report
// "not supported". MQTT contract: speaker_contract.h.
//
// Threading:
//   - begin(), service(), onMqttConnected(), handleMqttMessage(),
//     handleCommandPayload() and shutdown() run on the Arduino loop task.
//   - isCommandTopic() may be called from the MQTT worker.
//   - Codec access, the streaming download, decoding and the I2S output run on a
//     dedicated worker task created by begin().

#include <Arduino.h>

namespace speaker {

// Exact-profile capability: true only where the speaker path is compiled.
bool supported();

// Loads the stored volume and starts the worker, which probes the codec.
// Needs the I2C bus from Device init.
void begin();

// Publishes retained status changes, re-announces the Bridge capability
// when the codec probe finished and stores volume changes once playback is
// idle. Runs in the awake and the sleep loop.
void service();

// Post-connect hook: the retained {base}/stat/audio status.
void onMqttConnected();

// Command topic for the explicit subscription list; nullptr when unsupported.
const char* commandTopic();
// True when topic is {base}/cmnd/audio. Safe on the MQTT worker.
bool isCommandTopic(const char* topic);
// Returns true when the topic belonged to this module.
bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length);
// Processes a command whose origin was already checked: the plain topic or a
// Bridge message on the encrypted command channel.
void handleCommandPayload(const uint8_t* payload, size_t length);

// "audio_output" flag in the Bridge capabilities: codec found and ready.
bool bridgeCapability();

// OTA or restart: stops playback, switches the amplifier off and releases
// I2S. Waits a bounded time for the worker. Later commands are ignored.
void shutdown(const char* reason);

}  // namespace speaker

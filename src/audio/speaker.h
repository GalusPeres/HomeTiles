#pragma once

// Speaker playback for Home Assistant (Bridge media_player entity), on
// profiles with a speaker board file (HOMETILES_SPEAKER, speaker_select.h).
// On every other profile all functions are inexpensive stubs that report
// "not supported". MQTT contract: speaker_contract.h.
//
// The same worker also captures the board microphone for Home Assistant
// Assist (assist_contract.h), so one task owns I2S and both codecs: a capture
// session closes the speaker output first, and a play command ends capture.
//
// Threading:
//   - begin(), service(), onMqttConnected(), handleMqttMessage(),
//     handleCommandPayload(), handleAssistCommandPayload() and shutdown() run
//     on the Arduino loop task.
//   - isCommandTopic() and isAssistCommandTopic() may be called from the MQTT
//     worker.
//   - Codec access, the streaming download, decoding, the I2S output and the
//     microphone capture run on a dedicated worker task created by begin().

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

// Post-connect hook: the retained {base}/stat/audio and, once the mic
// answered, {base}/stat/assist status.
void onMqttConnected();

// Command topic for the explicit subscription list; nullptr when unsupported.
const char* commandTopic();
// True when topic is {base}/cmnd/audio. Safe on the MQTT worker.
bool isCommandTopic(const char* topic);
// {base}/cmnd/assist for the subscription list; nullptr when unsupported.
const char* assistCommandTopic();
// True when topic is {base}/cmnd/assist. Safe on the MQTT worker.
bool isAssistCommandTopic(const char* topic);
// Handles {base}/cmnd/audio and {base}/cmnd/assist. Returns true when the
// topic belonged to this module.
bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length);
// Processes a command whose origin was already checked: the plain topic or a
// Bridge message on the encrypted command channel.
void handleCommandPayload(const uint8_t* payload, size_t length);
// The same for Assist commands (data name "assist").
void handleAssistCommandPayload(const uint8_t* payload, size_t length);

// "audio_output" flag in the Bridge capabilities: codec found and ready.
bool bridgeCapability();
// "audio_input" flag in the Bridge capabilities: microphone ADC answered.
bool micCapability();

// OTA or restart: stops playback and capture, switches the amplifier off and
// releases I2S. Waits a bounded time for the worker. Later commands are
// ignored.
void shutdown(const char* reason);

}  // namespace speaker

#pragma once

// Speaker playback for Home Assistant (Bridge media_player entity), on
// profiles with a speaker board file (HOMETILES_SPEAKER, speaker_select.h).
// On every other profile all functions are inexpensive stubs that report
// "not supported". MQTT contract: speaker_contract.h.
//
// The same worker also captures the board microphone for Home Assistant
// Assist (assist_contract.h), so one task owns I2S and both codecs: a capture
// session closes the speaker output first, and a play command ends capture.
// On wake word profiles (HOMETILES_WAKE_WORD) the worker also listens for the
// active wake words while nothing else uses I2S, runs the detection itself
// and hands the audio after a detection to the next Assist session.
//
// Threading:
//   - begin(), service(), onMqttConnected(), handleMqttMessage(),
//     handleCommandPayload(), handleAssistCommandPayload() and shutdown() run
//     on the Arduino loop task.
//   - isCommandTopic() and isAssistCommandTopic() may be called from the MQTT
//     worker.
//   - Codec access, the streaming download, decoding, the I2S output, the
//     microphone capture and the wake word inference run on a dedicated
//     worker task created by begin().

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
// answered, {base}/stat/assist status (with the wake word fields).
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
// The same for Assist commands (data name "assist"). sealed is true for a
// Bridge message on the encrypted command channel.
void handleAssistCommandPayload(const uint8_t* payload, size_t length, bool sealed = false);

// Microphone privacy switch, stored across restarts (default on). While off,
// Assist sessions are refused and no wake word runs. Loop task only.
bool micEnabled();
void setMicEnabled(bool enabled, const char* source);

// Speaker volume 0.0..1.0, as the Bridge's media player sets it; changes
// publish the retained status and are stored. Loop task only.
float volume();
void setVolume(float volume);

// ES7210 mic gain step, stored; applies the next time the input opens.
// Steps 0..10 are 0..30 dB in 3 dB steps, then 33, 34.5, 36 and 37.5 dB
// (the es7210_mic_gain_t steps). Loop task only.
constexpr uint8_t kMaxMicGainStep = 14;
constexpr uint8_t kDefaultMicGainStep = 10;  // 30 dB, the former fixed gain
// Gain of a step in tenths of a dB (0..375); steps above the maximum clamp.
inline uint16_t micGainTenthsDb(uint8_t step) {
  if (step > kMaxMicGainStep) step = kMaxMicGainStep;
  return step <= 10 ? static_cast<uint16_t>(step * 30)
                    : static_cast<uint16_t>(330 + (step - 11) * 15);
}
uint8_t micGainStep();
void setMicGainStep(uint8_t step);

// "audio_output" flag in the Bridge capabilities: codec found and ready.
bool bridgeCapability();
// "audio_input" flag in the Bridge capabilities: microphone ADC answered.
bool micCapability();

// OTA or restart: stops playback and capture, switches the amplifier off and
// releases I2S. Waits a bounded time for the worker. Later commands are
// ignored.
void shutdown(const char* reason);

}  // namespace speaker

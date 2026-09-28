#pragma once

#include <Arduino.h>

#include "src/network/secure/command_channel_core.h"

// Encrypted, authenticated commands between this panel and the HomeTiles
// Bridge (docs-dev/command-encryption.md). Off by default: until a pairing
// code is created on the display and confirmed by the Bridge, every topic and
// payload stays exactly as before.
//
// Threads: everything runs on the loop task (Arduino setup/loop, LVGL event
// handlers and the MQTT inbound drainer). prepareOutbound() refuses a sealed
// command from any other task instead of sharing the session state.
namespace command_channel {

// Loads the stored pairing. Call once from setup().
void begin();
PairingState state();
// True while a session with the Bridge is established.
bool sessionReady();

// Device Settings: create a new code (state Pending) or turn pairing off.
bool createCode();
// Turning off tells the Bridge through the current session, so it removes
// its copy of the code as well. bridge_notified is false without a session;
// the code must then also be removed in the Bridge.
bool disable(bool* bridge_notified = nullptr);
// The current code as five groups of five; false while pairing is off.
bool displayCode(char out[kCodeDisplaySize]);

// MQTT integration on the loop task.
void onMqttConnected();
void service();
// Consumes messages on {base}/secure/bridge; returns true when handled.
bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length);
// While pairing is active, the plain Bridge-to-panel messages that carry
// stream tokens ({base}/stat/camera, {base}/cmnd/local_camera) are ignored:
// the Bridge sends them sealed instead.
bool blocksPlaintext(const char* topic);

// Retained Bridge announcement (tab5_lvgl/config/{id}/bridge): while a
// pairing code exists, returns a PSRAM copy of the payload with its signature
// (release it with heap_caps_free()); nullptr while pairing is off, so the
// announcement stays exactly as before.
char* signAnnouncement(const char* topic, const char* payload, size_t length);

// Outbound hook of the MQTT publish queues.
enum class OutboundResult : uint8_t { Plain, Sealed, Held, Dropped };

class SealedPublish {
 public:
  SealedPublish() = default;
  SealedPublish(const SealedPublish&) = delete;
  SealedPublish& operator=(const SealedPublish&) = delete;
  ~SealedPublish();
  void reset(char* block, size_t topic_length, size_t payload_length);
  const char* topic() const { return block_; }
  const uint8_t* payload() const {
    return reinterpret_cast<const uint8_t*>(block_ + topic_length_ + 1);
  }
  size_t length() const { return payload_length_; }

 private:
  char* block_ = nullptr;
  size_t topic_length_ = 0;
  size_t payload_length_ = 0;
};

// Plain: publish unchanged. Sealed: publish sealed->topic()/payload() instead
// (never retained). Held: queued until the Bridge session exists. Dropped:
// the command cannot be sent securely.
OutboundResult prepareOutbound(const char* topic, const uint8_t* payload,
                               size_t length, SealedPublish* sealed);

}  // namespace command_channel

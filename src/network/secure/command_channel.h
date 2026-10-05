#pragma once

#include <Arduino.h>

#include "src/network/secure/command_channel_core.h"

// Encrypted, authenticated commands between this panel and the HomeTiles
// Bridge (docs-dev/command-encryption.md). Off by default: until the panel is
// paired with the Bridge (a six-digit number confirmed on both sides), every
// topic and payload stays exactly as before.
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

// Number-comparison pairing, started from the Security view of the System
// popup. One attempt at a time; it ends after 120 s at the latest.
enum class PairingPhase : uint8_t {
  Idle,           // No attempt.
  // Not linked yet: Pair made the panel discoverable for two minutes; Home
  // Assistant sends the Bridge address (POST /api/link) when it is added.
  Discoverable,
  Asking,         // Start sent; waiting for the Bridge.
  Compare,        // Number ready; waiting for the user on the panel.
  Confirmed,      // Confirmed here; waiting for Home Assistant.
  Done,           // Paired; commands are encrypted.
  NoAnswer,       // Nothing within 15 s: no Bridge with this pairing.
  AlreadyPaired,  // The Bridge is still paired: remove it in HA first.
  Busy,           // The Bridge limits attempts; try again later.
  Rejected,       // Rejected in Home Assistant.
  Failed,         // Timeout, cancel on the other side or an error.
};
// False while the panel is paired (unpair first) or on an internal error.
bool startPairing();
PairingPhase pairingPhase();
// The number as "123 456" while it is shown; false otherwise.
bool pairingNumber(char out[kPairNumberDisplaySize]);
// The user confirmed the number on the panel.
void confirmPairing();
// Cancels a running attempt (the Bridge is told) or clears a finished result.
void endPairing();

// Turning off tells the Bridge through the current session, so it removes
// the pairing as well. bridge_notified is false without a session; the
// pairing must then also be removed in Home Assistant.
bool disable(bool* bridge_notified = nullptr);

// Called when the panel starts pairing by itself after the Bridge sent its
// address for the direct link (docs-dev/bridge-link.md); shows the number.
void setPairingPromptCallback(void (*callback)());

// True within two minutes after Pair was pressed on a panel without a direct
// link. Only then does POST /api/link accept a Bridge address; the press at
// the panel replaces the Web Admin password.
bool linkWindowOpen();

// Restarts the panel; the link forgets its Bridge when the pairing ends and
// the panel starts again like a new one.
void setRestartCallback(void (*callback)());

// MQTT integration on the loop task.
void onMqttConnected();
void service();
// Consumes messages on {base}/secure/bridge and {base}/pair/bridge; returns
// true when handled.
bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length);
// While pairing is active, the plain Bridge-to-panel messages that carry
// stream tokens ({base}/stat/camera, {base}/cmnd/local_camera) are ignored:
// the Bridge sends them sealed instead.
bool blocksPlaintext(const char* topic);

// Retained Bridge announcement (tab5_lvgl/config/{id}/bridge): while the panel
// is paired, returns a PSRAM copy of the payload with its signature (release
// it with heap_caps_free()); nullptr otherwise, so the announcement stays
// exactly as before.
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

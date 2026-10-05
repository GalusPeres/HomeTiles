#pragma once

#include <Arduino.h>
#include <Network.h>

#include <functional>

#include "src/network/link/bridge_link_core.h"

// Client end of the direct Bridge link (docs-dev/bridge-link.md). It offers
// the calls of PubSubClient that the network worker uses, so the worker keeps
// its single-owner model, queues, DMA guards and backoff unchanged:
// connect, connected, loop, publish, subscribe, unsubscribe and the streamed
// beginPublish/write/endPublish. Only the network worker task touches it.
//
// Pair mode (no pairing key yet) carries only the pairing topics; every other
// publish or subscription is dropped here, so the regular publishers of the
// firmware need no special case while the panel pairs.
class BridgeLinkClient {
 public:
  using Callback = std::function<void(char*, uint8_t*, unsigned int)>;

  // state() values; negative values mean "not connected", as in PubSubClient.
  static constexpr int kConnected = 0;
  static constexpr int kDisconnected = -1;
  static constexpr int kConnectFailed = -2;
  static constexpr int kHandshakeFailed = -3;
  static constexpr int kRefused = -4;
  static constexpr int kProtocolError = -5;
  static constexpr int kTimeout = -6;
  static constexpr int kWriteFailed = -7;

  // Largest message the client delivers; reported as the MQTT buffer size.
  static constexpr uint16_t kReceiveCapacity = 65535;

  BridgeLinkClient() = default;
  ~BridgeLinkClient();
  BridgeLinkClient(const BridgeLinkClient&) = delete;
  BridgeLinkClient& operator=(const BridgeLinkClient&) = delete;

  void setClient(NetworkClient& client) { client_ = &client; }
  void setServer(const char* host, uint16_t port);
  void setCallback(Callback callback) { callback_ = std::move(callback); }

  // Session mode with a pairing key (and its key id), pair mode without.
  bool connect(const char* device_id, const char* base, const uint8_t* pairing_key,
               const char* key_id);
  bool connected();
  void disconnect();
  bool publish(const char* topic, const uint8_t* payload, unsigned int length, bool retain);
  bool publish(const char* topic, const char* payload, bool retain);
  bool subscribe(const char* topic);
  bool unsubscribe(const char* topic);
  bool loop();

  bool beginPublish(const char* topic, unsigned int length, bool retain);
  size_t write(const uint8_t* data, size_t length);
  int endPublish();

  int state() const { return state_; }
  bool pairMode() const { return pair_mode_; }
  // Retain flag of the publish being delivered to the callback.
  bool lastPublishRetained() const { return last_retained_; }
  // Reason of the last refusal by the Bridge ("unknown", "pair", ...).
  const char* refuseReason() const { return refuse_reason_; }

 private:
  bool reserve(uint8_t** buffer, size_t* capacity, size_t size);
  bool sendFrame(uint8_t type, const uint8_t* payload, size_t length);
  // The payload already sits at tx_ + kHeaderSize + 1.
  bool sendBuilt(uint8_t type, size_t payload_length);
  bool readExact(uint8_t* out, size_t length, uint32_t deadline_ms);
  bool readHandshakeFrame(uint32_t deadline_ms, uint8_t* type, size_t* payload_length);
  bool handleFrame(size_t body_length);
  bool flushChunk();
  void fail(int state);
  void wipeKeys();
  bool pairTopic(const char* topic, bool subscribe) const;

  NetworkClient* client_ = nullptr;
  char host_[64] = {};
  uint16_t port_ = 0;
  Callback callback_;

  bool connected_ = false;
  bool pair_mode_ = false;
  bool sealed_ = false;
  uint8_t tx_key_[bridge_link::kKeySize] = {};
  uint8_t rx_key_[bridge_link::kKeySize] = {};
  uint64_t tx_counter_ = 0;
  uint64_t rx_counter_ = 0;
  char pair_publish_[bridge_link::kMaxBaseLength + 16] = {};
  char pair_subscribe_[bridge_link::kMaxBaseLength + 16] = {};

  // Frames are built and received in PSRAM; both buffers grow on demand.
  uint8_t* tx_ = nullptr;
  size_t tx_capacity_ = 0;
  uint8_t* rx_ = nullptr;
  size_t rx_capacity_ = 0;
  uint8_t rx_header_[bridge_link::kHeaderSize] = {};
  size_t rx_header_have_ = 0;
  size_t rx_body_need_ = 0;
  size_t rx_body_have_ = 0;

  uint32_t last_rx_ms_ = 0;
  uint32_t last_tx_ms_ = 0;
  bool last_retained_ = false;

  // Streamed publish (beginPublish/write/endPublish).
  bool streaming_ = false;
  size_t stream_total_ = 0;
  size_t stream_written_ = 0;
  uint8_t* chunk_ = nullptr;
  size_t chunk_fill_ = 0;

  int state_ = kDisconnected;
  char refuse_reason_[bridge_link::kReasonSize] = {};
};

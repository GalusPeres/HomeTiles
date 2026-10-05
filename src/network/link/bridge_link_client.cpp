#include "src/network/link/bridge_link_client.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "src/core/security/secure_random.h"

using namespace bridge_link;

namespace {

constexpr uint32_t kConnectTimeoutMs = 4000;
// Frames handled per loop() call; the worker keeps servicing its queues
// between calls, as with the vendored PubSubClient.
constexpr uint8_t kMaxFramesPerLoop = 8;
constexpr size_t kReadSlice = 4096;
// Yield inside long reads so a large frame never starves the idle task.
constexpr size_t kYieldEveryBytes = 16 * 1024;
constexpr size_t kInitialBuffer = 2048;

void* allocPreferPsram(size_t size) {
  void* block = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!block) block = heap_caps_malloc(size, MALLOC_CAP_8BIT);
  return block;
}

bool deadlinePassed(uint32_t deadline_ms) {
  return static_cast<int32_t>(millis() - deadline_ms) >= 0;
}

}  // namespace

BridgeLinkClient::~BridgeLinkClient() {
  wipeKeys();
  if (tx_) heap_caps_free(tx_);
  if (rx_) heap_caps_free(rx_);
  if (chunk_) heap_caps_free(chunk_);
}

void BridgeLinkClient::setServer(const char* host, uint16_t port) {
  memset(host_, 0, sizeof(host_));
  if (host) strncpy(host_, host, sizeof(host_) - 1);
  port_ = port;
}

bool BridgeLinkClient::reserve(uint8_t** buffer, size_t* capacity, size_t size) {
  if (*capacity >= size) return true;
  size_t grown = *capacity ? *capacity : kInitialBuffer;
  while (grown < size) grown *= 2;
  if (grown > kHeaderSize + kMaxFrameLength) grown = kHeaderSize + kMaxFrameLength;
  if (grown < size) return false;
  uint8_t* block = static_cast<uint8_t*>(allocPreferPsram(grown));
  if (!block) {
    Serial.printf("[Link] Could not allocate a %u byte frame buffer\n", static_cast<unsigned>(grown));
    return false;
  }
  if (*buffer) heap_caps_free(*buffer);
  *buffer = block;
  *capacity = grown;
  return true;
}

void BridgeLinkClient::wipeKeys() {
  ht_crypto::secureZero(tx_key_, sizeof(tx_key_));
  ht_crypto::secureZero(rx_key_, sizeof(rx_key_));
  sealed_ = false;
  tx_counter_ = 0;
  rx_counter_ = 0;
}

void BridgeLinkClient::fail(int state) {
  if (client_) client_->stop();
  connected_ = false;
  streaming_ = false;
  chunk_fill_ = 0;
  rx_header_have_ = 0;
  rx_body_need_ = 0;
  rx_body_have_ = 0;
  wipeKeys();
  state_ = state;
}

void BridgeLinkClient::disconnect() {
  fail(kDisconnected);
}

bool BridgeLinkClient::connected() {
  if (!connected_) return false;
  if (!client_ || !client_->connected()) {
    fail(kDisconnected);
    return false;
  }
  return true;
}

bool BridgeLinkClient::sendBuilt(uint8_t type, size_t payload_length) {
  const size_t size =
      finishFrame(sealed_ ? tx_key_ : nullptr, tx_counter_, type, payload_length, tx_, tx_capacity_);
  if (size == 0) return false;
  if (sealed_) ++tx_counter_;
  if (client_->write(tx_, size) != size) {
    fail(kWriteFailed);
    return false;
  }
  last_tx_ms_ = millis();
  return true;
}

bool BridgeLinkClient::sendFrame(uint8_t type, const uint8_t* payload, size_t length) {
  if (!client_ || !reserve(&tx_, &tx_capacity_, sealedFrameSize(length))) return false;
  if (length) memcpy(tx_ + kHeaderSize + 1, payload, length);
  return sendBuilt(type, length);
}

bool BridgeLinkClient::readExact(uint8_t* out, size_t length, uint32_t deadline_ms) {
  size_t have = 0;
  while (have < length) {
    if (!client_->connected() && client_->available() <= 0) return false;
    const int available = client_->available();
    if (available > 0) {
      const size_t want = length - have < static_cast<size_t>(available)
                              ? length - have
                              : static_cast<size_t>(available);
      const int read = client_->read(out + have, want);
      if (read > 0) {
        have += static_cast<size_t>(read);
        continue;
      }
    }
    if (deadlinePassed(deadline_ms)) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return true;
}

// Reads one handshake frame into rx_; plain or sealed as the state demands.
bool BridgeLinkClient::readHandshakeFrame(uint32_t deadline_ms, uint8_t* type,
                                          size_t* payload_length) {
  uint8_t header[kHeaderSize];
  if (!readExact(header, sizeof(header), deadline_ms)) return false;
  const size_t length = frameLength(header);
  if (length == 0 || length > kMaxHelloLength + kTagSize + 1 ||
      !reserve(&rx_, &rx_capacity_, length)) {
    return false;
  }
  if (!readExact(rx_, length, deadline_ms)) return false;
  if (sealed_) {
    if (!openFrame(rx_key_, rx_counter_, header, rx_, length, payload_length)) return false;
    ++rx_counter_;
  } else {
    *payload_length = length - 1;
  }
  *type = rx_[0];
  return true;
}

bool BridgeLinkClient::connect(const char* device_id, const char* base,
                               const uint8_t* pairing_key, const char* key_id) {
  if (connected_) disconnect();
  refuse_reason_[0] = '\0';
  if (!client_ || !host_[0] || port_ == 0 || !device_id || !base) {
    state_ = kConnectFailed;
    return false;
  }
  const bool session = pairing_key != nullptr && key_id != nullptr;
  uint8_t panel_nonce[kNonceSize];
  char hello[kMaxHelloLength + 1];
  if (session) secure_random::fill(panel_nonce, sizeof(panel_nonce));
  const size_t hello_length =
      buildHello(device_id, base, session ? key_id : nullptr, session ? panel_nonce : nullptr,
                 hello, sizeof(hello));
  if (hello_length == 0) {
    Serial.println("[Link] Device id or base topic cannot be used for the link");
    state_ = kConnectFailed;
    return false;
  }
  if (!client_->connect(host_, port_, static_cast<int32_t>(kConnectTimeoutMs))) {
    fail(kConnectFailed);
    return false;
  }
  client_->setNoDelay(true);
  wipeKeys();
  rx_header_have_ = rx_body_need_ = rx_body_have_ = 0;
  if (!sendFrame(kHello, reinterpret_cast<const uint8_t*>(hello), hello_length)) {
    fail(kWriteFailed);
    return false;
  }
  const uint32_t deadline = millis() + kHandshakeTimeoutMs;
  uint8_t type = 0;
  size_t payload_length = 0;
  if (!readHandshakeFrame(deadline, &type, &payload_length)) {
    fail(kHandshakeFailed);
    return false;
  }
  const char* json = reinterpret_cast<const char*>(rx_ + 1);
  if (type == kRefuse) {
    parseRefuse(json, payload_length, refuse_reason_);
    Serial.printf("[Link] The Bridge refused the connection (%s)\n", refuse_reason_);
    fail(kRefused);
    return false;
  }
  uint8_t bridge_nonce[kNonceSize];
  if (type != kWelcome || !parseWelcome(json, payload_length, session, bridge_nonce)) {
    fail(kHandshakeFailed);
    return false;
  }

  if (session) {
    sessionKeys(pairing_key, device_id, base, panel_nonce, bridge_nonce, tx_key_, rx_key_);
    sealed_ = true;
    tx_counter_ = 0;
    rx_counter_ = 0;
    if (!sendFrame(kReady, nullptr, 0)) {
      fail(kWriteFailed);
      return false;
    }
    if (!readHandshakeFrame(deadline, &type, &payload_length) || type != kReady) {
      // A frame that does not open means another pairing key.
      Serial.println("[Link] The Bridge did not prove the pairing key");
      fail(kHandshakeFailed);
      return false;
    }
  } else {
    snprintf(pair_publish_, sizeof(pair_publish_), "%s/pair/panel", base);
    snprintf(pair_subscribe_, sizeof(pair_subscribe_), "%s/pair/bridge", base);
  }
  pair_mode_ = !session;
  connected_ = true;
  state_ = kConnected;
  last_rx_ms_ = last_tx_ms_ = millis();
  return true;
}

bool BridgeLinkClient::pairTopic(const char* topic, bool subscribe) const {
  return topic && strcmp(topic, subscribe ? pair_subscribe_ : pair_publish_) == 0;
}

bool BridgeLinkClient::publish(const char* topic, const uint8_t* payload, unsigned int length,
                               bool retain) {
  if (!connected() || streaming_ || !topic || (!payload && length)) return false;
  // Pair mode: everything but the pairing topic is dropped on purpose.
  if (pair_mode_ && !pairTopic(topic, false)) return true;
  const size_t topic_length = strlen(topic);
  if (length > kMaxPayloadLength || !validTopic(topic, topic_length)) return false;
  const size_t payload_length = 3 + topic_length + length;
  if (!reserve(&tx_, &tx_capacity_, sealedFrameSize(payload_length))) return false;
  uint8_t* out = tx_ + kHeaderSize + 1;
  if (publishHeader(topic, topic_length, retain, out, payload_length) == 0) return false;
  if (length) memcpy(out + 3 + topic_length, payload, length);
  return sendBuilt(kPublish, payload_length);
}

bool BridgeLinkClient::publish(const char* topic, const char* payload, bool retain) {
  return publish(topic, reinterpret_cast<const uint8_t*>(payload),
                 payload ? static_cast<unsigned int>(strlen(payload)) : 0, retain);
}

bool BridgeLinkClient::subscribe(const char* topic) {
  if (!connected() || !topic) return false;
  if (pair_mode_ && !pairTopic(topic, true)) return true;
  const size_t length = strlen(topic);
  if (!validTopic(topic, length)) return false;
  return sendFrame(kSubscribe, reinterpret_cast<const uint8_t*>(topic), length);
}

bool BridgeLinkClient::unsubscribe(const char* topic) {
  if (!connected() || !topic) return false;
  if (pair_mode_) return true;
  const size_t length = strlen(topic);
  if (!validTopic(topic, length)) return false;
  return sendFrame(kUnsubscribe, reinterpret_cast<const uint8_t*>(topic), length);
}

bool BridgeLinkClient::beginPublish(const char* topic, unsigned int length, bool retain) {
  if (!connected() || streaming_ || pair_mode_ || !topic || length > kMaxStreamLength) return false;
  const size_t topic_length = strlen(topic);
  uint8_t begin[3 + kMaxTopicLength + 4];
  if (publishHeader(topic, topic_length, retain, begin, sizeof(begin)) == 0) return false;
  putU32(begin + 3 + topic_length, length);
  if (!chunk_) {
    chunk_ = static_cast<uint8_t*>(allocPreferPsram(kMaxStreamChunk));
    if (!chunk_) return false;
  }
  if (!sendFrame(kStreamBegin, begin, 3 + topic_length + 4)) return false;
  streaming_ = true;
  stream_total_ = length;
  stream_written_ = 0;
  chunk_fill_ = 0;
  return true;
}

bool BridgeLinkClient::flushChunk() {
  if (chunk_fill_ == 0) return true;
  const bool ok = sendFrame(kStreamData, chunk_, chunk_fill_);
  chunk_fill_ = 0;
  return ok;
}

size_t BridgeLinkClient::write(const uint8_t* data, size_t length) {
  if (!streaming_ || !connected_ || !data || stream_written_ + length > stream_total_) return 0;
  size_t done = 0;
  while (done < length) {
    const size_t room = kMaxStreamChunk - chunk_fill_;
    const size_t take = length - done < room ? length - done : room;
    memcpy(chunk_ + chunk_fill_, data + done, take);
    chunk_fill_ += take;
    done += take;
    if (chunk_fill_ == kMaxStreamChunk && !flushChunk()) return done - take;
  }
  stream_written_ += length;
  return length;
}

int BridgeLinkClient::endPublish() {
  if (!streaming_) return 0;
  streaming_ = false;
  if (stream_written_ != stream_total_ || !flushChunk() || !sendFrame(kStreamEnd, nullptr, 0)) {
    // The Bridge would read a short stream as a protocol error anyway.
    fail(kProtocolError);
    return 0;
  }
  return 1;
}

bool BridgeLinkClient::handleFrame(size_t body_length) {
  size_t payload_length = 0;
  if (sealed_) {
    if (!openFrame(rx_key_, rx_counter_, rx_header_, rx_, body_length, &payload_length)) {
      Serial.println("[Link] Frame from the Bridge did not authenticate; reconnecting");
      fail(kProtocolError);
      return false;
    }
    ++rx_counter_;
  } else {
    payload_length = body_length - 1;
  }
  const uint8_t type = rx_[0];
  uint8_t* payload = rx_ + 1;
  switch (type) {
    case kPublish: {
      Publish message;
      if (!parsePublish(payload, payload_length, message)) {
        fail(kProtocolError);
        return false;
      }
      char topic[kMaxTopicLength + 1];
      memcpy(topic, message.topic, message.topic_length);
      topic[message.topic_length] = '\0';
      if (pair_mode_ && !pairTopic(topic, true)) return true;
      last_retained_ = message.retain;
      if (callback_) {
        callback_(topic, const_cast<uint8_t*>(message.data),
                  static_cast<unsigned int>(message.data_length));
      }
      last_retained_ = false;
      return true;
    }
    case kPing:
      return sendFrame(kPong, nullptr, 0);
    case kPong:
      return true;
    case kHello:
    case kWelcome:
    case kRefuse:
    case kReady:
      fail(kProtocolError);
      return false;
    default:
      // Unknown frames from a newer Bridge are skipped.
      return true;
  }
}

bool BridgeLinkClient::loop() {
  if (!connected()) return false;
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - last_rx_ms_) >= kIdleTimeoutMs) {
    Serial.println("[Link] No data from the Bridge for 45 s; reconnecting");
    fail(kTimeout);
    return false;
  }
  if (!streaming_ && static_cast<uint32_t>(now - last_tx_ms_) >= kPingIdleMs) {
    if (!sendFrame(kPing, nullptr, 0)) return false;
  }

  uint8_t frames = 0;
  size_t read_since_yield = 0;
  while (frames < kMaxFramesPerLoop) {
    const int available = client_->available();
    if (available <= 0) break;
    if (rx_body_need_ == 0) {
      const int read = client_->read(rx_header_ + rx_header_have_, kHeaderSize - rx_header_have_);
      if (read <= 0) break;
      rx_header_have_ += static_cast<size_t>(read);
      if (rx_header_have_ < kHeaderSize) continue;
      const size_t length = frameLength(rx_header_);
      if (length == 0 || !reserve(&rx_, &rx_capacity_, length)) {
        Serial.println("[Link] Invalid frame length from the Bridge; reconnecting");
        fail(kProtocolError);
        return false;
      }
      rx_body_need_ = length;
      rx_body_have_ = 0;
      continue;
    }
    size_t want = rx_body_need_ - rx_body_have_;
    if (want > static_cast<size_t>(available)) want = static_cast<size_t>(available);
    if (want > kReadSlice) want = kReadSlice;
    const int read = client_->read(rx_ + rx_body_have_, want);
    if (read <= 0) break;
    rx_body_have_ += static_cast<size_t>(read);
    read_since_yield += static_cast<size_t>(read);
    if (read_since_yield >= kYieldEveryBytes) {
      read_since_yield = 0;
      vTaskDelay(1);
    }
    if (rx_body_have_ < rx_body_need_) continue;
    const size_t body_length = rx_body_need_;
    rx_header_have_ = 0;
    rx_body_need_ = 0;
    rx_body_have_ = 0;
    last_rx_ms_ = millis();
    ++frames;
    if (!handleFrame(body_length)) return false;
  }
  return connected();
}

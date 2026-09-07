#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hometiles_mqtt {

constexpr uint8_t kQosMask = 0x06;
constexpr uint8_t kQos0 = 0x00;
constexpr uint8_t kQos1 = 0x02;
// The callback and packet-buffer API use 16-bit lengths. Receive growth is
// bounded independently of the smaller normal transmit buffer.
constexpr uint32_t kMaxInboundPacketBytes = UINT16_MAX;
constexpr uint32_t kMaxDiscardPacketBytes = 256U * 1024U;

struct PublishPacketLayout {
  size_t topic_source_offset = 0;
  size_t topic_callback_offset = 0;
  size_t topic_length = 0;
  size_t packet_id_offset = 0;
  size_t payload_offset = 0;
  size_t payload_length = 0;
  uint8_t qos_bits = kQos0;
};

inline bool packetFitsBuffer(size_t fixed_header_bytes,
                             size_t remaining_length,
                             size_t buffer_size) {
  return fixed_header_bytes <= buffer_size &&
         remaining_length <= buffer_size - fixed_header_bytes;
}

// HomeTiles subscribes with QoS 0 or 1. Reject QoS 2 and the reserved QoS 3
// encoding instead of passing a packet through a handshake this client does
// not implement.
inline bool publishRemainingLengthIsValid(size_t remaining_length,
                                          size_t topic_length,
                                          uint8_t qos_bits) {
  if (topic_length == 0 || (qos_bits != kQos0 && qos_bits != kQos1)) {
    return false;
  }

  const size_t packet_id_bytes = qos_bits == kQos1 ? 2U : 0U;
  const size_t fixed_variable_header = 2U + packet_id_bytes;
  if (remaining_length < fixed_variable_header) return false;

  return topic_length <= remaining_length - fixed_variable_header;
}

// Validate every offset before PubSubClient moves the topic or derives the
// callback payload. packet_length includes the fixed header and encoded
// remaining-length bytes.
inline bool computePublishPacketLayout(const uint8_t* packet,
                                       size_t packet_length,
                                       size_t buffer_size,
                                       uint8_t remaining_length_bytes,
                                       PublishPacketLayout* layout) {
  if (!packet || !layout || remaining_length_bytes == 0 ||
      remaining_length_bytes > 4 || packet_length > buffer_size) {
    return false;
  }

  const size_t fixed_header_size = 1U + remaining_length_bytes;
  if (packet_length < fixed_header_size + 2U) return false;

  const uint8_t qos_bits = packet[0] & kQosMask;
  const size_t topic_length_offset = fixed_header_size;
  const size_t topic_length =
      (static_cast<size_t>(packet[topic_length_offset]) << 8U) |
      packet[topic_length_offset + 1U];
  const size_t remaining_length = packet_length - fixed_header_size;
  if (!publishRemainingLengthIsValid(remaining_length, topic_length,
                                     qos_bits)) {
    return false;
  }

  const size_t topic_source_offset = topic_length_offset + 2U;
  const size_t packet_id_offset = topic_source_offset + topic_length;
  const size_t packet_id_bytes = qos_bits == kQos1 ? 2U : 0U;
  const size_t payload_offset = packet_id_offset + packet_id_bytes;
  if (payload_offset > packet_length) return false;

  layout->topic_source_offset = topic_source_offset;
  layout->topic_callback_offset = topic_source_offset - 1U;
  layout->topic_length = topic_length;
  layout->packet_id_offset = packet_id_offset;
  layout->payload_offset = payload_offset;
  layout->payload_length = packet_length - payload_offset;
  layout->qos_bits = qos_bits;
  return true;
}

// Compute the queue allocation without allowing either size_t wraparound or
// a callback to request more data than the packet buffer could have held.
inline bool checkedInboundAllocationSize(size_t header_bytes,
                                         size_t topic_bytes,
                                         size_t payload_bytes,
                                         size_t packet_capacity,
                                         size_t* total_bytes) {
  if (!total_bytes) return false;
  *total_bytes = 0;

  if (packet_capacity == 0 || topic_bytes > packet_capacity ||
      payload_bytes > packet_capacity ||
      topic_bytes > packet_capacity - payload_bytes) {
    return false;
  }

  if (header_bytes > SIZE_MAX - topic_bytes) return false;
  size_t total = header_bytes + topic_bytes;
  if (total == SIZE_MAX) return false;
  ++total;  // Topic terminator.
  if (payload_bytes > SIZE_MAX - total) return false;

  *total_bytes = total + payload_bytes;
  return true;
}

}  // namespace hometiles_mqtt

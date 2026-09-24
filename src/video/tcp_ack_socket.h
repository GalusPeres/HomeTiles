#pragma once

// Blocking lwIP socket helpers of the acknowledged camera transport, shared by
// the display stream (camera_stream.cpp) and the built-in camera upload
// (local_camera_upload.cpp). The caller's stop condition is a callback so each
// direction keeps its own stop flags. Only compiled on ESP32-P4, the only
// target with a camera transport.

#include <stddef.h>
#include <stdint.h>

#include "src/video/tcp_ack_wire.h"

namespace tcp_ack {

// Returns true when the caller wants the socket operation to end now.
using StopFn = bool (*)(void* context);

struct ConnectOptions {
  const char* log_prefix = "[TcpAck]";  // Prefix of the DNS failure line.
  int receive_buffer_bytes = 4 * 1024;  // SO_RCVBUF hint.
  uint32_t connect_timeout_ms = 4000;
  uint32_t poll_timeout_ms = 250;       // SO_RCVTIMEO (and SO_SNDTIMEO below).
  bool send_timeout = false;            // Also set SO_SNDTIMEO to poll_timeout_ms.
  // nullptr keeps one uninterrupted select() for the whole connect timeout;
  // otherwise select() runs in poll_timeout_ms slices and stops early.
  StopFn stop = nullptr;
  void* stop_context = nullptr;
};

// Non-blocking connect with a select() timeout, then blocking mode with
// TCP_NODELAY and the receive timeout. Returns the socket or -1 (errno set).
int connectTo(const char* host, uint16_t port, const ConnectOptions& options);

// Sends everything unless stop() fires first. deadline_ms == 0: EAGAIN is an
// error (the display stream's behaviour). Otherwise EAGAIN from SO_SNDTIMEO is
// retried until millis() reaches deadline_ms.
bool sendAll(int fd, const void* source, size_t bytes, StopFn stop, void* context,
             uint32_t deadline_ms = 0);

// Receives exactly bytes. EAGAIN from SO_RCVTIMEO yields and retries; with a
// non-zero deadline_ms it returns Timeout once millis() reaches it.
SocketReadResult receiveExact(int fd, void* destination, size_t bytes, StopFn stop,
                              void* context, uint32_t deadline_ms = 0);

// shutdown() and close(); ignores a negative descriptor.
void closeSocket(int fd);

}  // namespace tcp_ack

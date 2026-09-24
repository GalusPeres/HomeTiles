#include "src/video/tcp_ack_socket.h"

#include <sdkconfig.h>

#if defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace tcp_ack {
namespace {

bool stopRequested(StopFn stop, void* context) {
  return stop && stop(context);
}

bool deadlinePassed(uint32_t deadline_ms) {
  return deadline_ms != 0 &&
         static_cast<int32_t>(millis() - deadline_ms) >= 0;
}

timeval toTimeval(uint32_t ms) {
  return timeval{static_cast<time_t>(ms / 1000U),
                 static_cast<suseconds_t>((ms % 1000U) * 1000U)};
}

// Waits until the non-blocking connect finished. Without a stop callback this
// is one select() over the whole timeout, as the display stream always did.
bool waitConnected(int fd, const ConnectOptions& options) {
  const uint32_t started_ms = millis();
  for (;;) {
    uint32_t wait_ms = options.connect_timeout_ms;
    if (options.stop) {
      const uint32_t elapsed = millis() - started_ms;
      if (elapsed >= options.connect_timeout_ms) return false;
      wait_ms = options.connect_timeout_ms - elapsed;
      if (wait_ms > options.poll_timeout_ms) wait_ms = options.poll_timeout_ms;
    }
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(fd, &writable);
    timeval timeout = toTimeval(wait_ms);
    const int selected = select(fd + 1, nullptr, &writable, nullptr, &timeout);
    if (selected > 0) {
      int socket_error = 0;
      socklen_t error_size = sizeof(socket_error);
      const int result =
          getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size);
      const bool connected = result == 0 && socket_error == 0;
      if (!connected && socket_error != 0) errno = socket_error;
      return connected;
    }
    if (!options.stop || selected < 0) return false;
    if (stopRequested(options.stop, options.stop_context)) return false;
  }
}

}  // namespace

int connectTo(const char* host, uint16_t port, const ConnectOptions& options) {
  char port_text[8] = {};
  snprintf(port_text, sizeof(port_text), "%u", static_cast<unsigned>(port));
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  addrinfo* addresses = nullptr;
  const int lookup = getaddrinfo(host, port_text, &hints, &addresses);
  if (lookup != 0 || !addresses) {
    Serial.printf("%s TCP DNS failed: %d\n", options.log_prefix, lookup);
    return -1;
  }

  int connected_fd = -1;
  for (addrinfo* address = addresses; address; address = address->ai_next) {
    const int fd =
        socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (fd < 0) continue;

    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &options.receive_buffer_bytes,
               sizeof(options.receive_buffer_bytes));
    const int tcp_no_delay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay,
               sizeof(tcp_no_delay));
    const int original_flags = fcntl(fd, F_GETFL, 0);
    if (original_flags < 0 ||
        fcntl(fd, F_SETFL, original_flags | O_NONBLOCK) < 0) {
      close(fd);
      continue;
    }

    const int result = ::connect(fd, address->ai_addr, address->ai_addrlen);
    bool connected = result == 0;
    if (!connected && result < 0 && errno == EINPROGRESS) {
      connected = waitConnected(fd, options);
    }
    if (!connected) {
      close(fd);
      continue;
    }

    fcntl(fd, F_SETFL, original_flags & ~O_NONBLOCK);
    timeval poll_timeout = toTimeval(options.poll_timeout_ms);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &poll_timeout,
               sizeof(poll_timeout));
    if (options.send_timeout) {
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &poll_timeout,
                 sizeof(poll_timeout));
    }
    connected_fd = fd;
    break;
  }
  freeaddrinfo(addresses);
  return connected_fd;
}

bool sendAll(int fd, const void* source, size_t bytes, StopFn stop, void* context,
             uint32_t deadline_ms) {
  const uint8_t* data = static_cast<const uint8_t*>(source);
  while (bytes > 0 && !stopRequested(stop, context)) {
    const int sent = send(fd, data, bytes, 0);
    if (sent > 0) {
      data += sent;
      bytes -= static_cast<size_t>(sent);
      continue;
    }
    if (sent < 0 && errno == EINTR) continue;
    if (deadline_ms != 0 && sent < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK) && !deadlinePassed(deadline_ms)) {
      taskYIELD();
      continue;
    }
    return false;
  }
  return bytes == 0;
}

SocketReadResult receiveExact(int fd, void* destination, size_t bytes, StopFn stop,
                              void* context, uint32_t deadline_ms) {
  uint8_t* output = static_cast<uint8_t*>(destination);
  size_t received_bytes = 0;
  while (received_bytes < bytes) {
    if (stopRequested(stop, context)) return SocketReadResult::Stopped;
    const int received =
        recv(fd, output + received_bytes, bytes - received_bytes, 0);
    if (received > 0) {
      received_bytes += static_cast<size_t>(received);
      continue;
    }
    if (received == 0) return SocketReadResult::Closed;
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (deadlinePassed(deadline_ms)) return SocketReadResult::Timeout;
      taskYIELD();
      continue;
    }
    return SocketReadResult::Error;
  }
  return SocketReadResult::Ok;
}

void closeSocket(int fd) {
  if (fd < 0) return;
  shutdown(fd, SHUT_RDWR);
  close(fd);
}

}  // namespace tcp_ack

#endif  // defined(CONFIG_IDF_TARGET_ESP32P4)

#include "src/core/firmware/github_update.h"

#include <HTTPClient.h>
#include <Network.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <lwip/sockets.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>  // MBEDTLS_ERR_SSL_ALLOC_FAILED
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "src/core/firmware/firmware_metadata.h"
#include "src/core/firmware/firmware_version.h"
#include "src/devices/device.h"
#include "src/devices/guition_esp32_4848s040/s3_diagnostics.h"
#include "src/network/transport/network_transport.h"

namespace {

constexpr size_t kInstallReadChunk = 2048;
constexpr size_t kInstallWriteSliceBytes = 16 * 1024;
constexpr size_t kSocketRxBufferBytes = 4 * 1024;
constexpr size_t kStageRangeBytes = 512 * 1024;
constexpr size_t kStageBlockBytes = kStageRangeBytes;
constexpr size_t kMaxStageBlocks = 32;
constexpr uint8_t kStageRangeAttempts = 3;
// In the v0.5.4 to v0.5.5 field test, about 4 MB of sustained reception
// filled the ESP-Hosted RX queue ("Failed to push data to rx queue") and
// the C6 failed. Allow recovery between ranges and pace reads at 2 ms
// per 2 KB chunk: about 1 MB/s instead of 2 MB/s.
constexpr uint32_t kStageRangePauseMs = 750;
constexpr size_t kMaxHttpLineLen = 4096;
constexpr uint32_t kConnectTimeoutMs = 10000;
constexpr uint32_t kReadTimeoutMs = 20000;
constexpr uint32_t kReadPaceMs = 2;

#if defined(DEVICE_ESP32_S3_RGB_480)
bool isRetryableTransportError(const String& error) {
  return error.startsWith("connect failed") ||
         error == "no http status" || error == "bad http status" ||
         error == "header timeout" || error == "timeout" ||
         error == "connection lost" || error == "HTTP 408" ||
         error == "HTTP 429" || error == "HTTP 500" ||
         error == "HTTP 502" || error == "HTTP 503" ||
         error == "HTTP 504";
}
#endif

#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
String diagnosticUrl(const String& url) {
  const int query = url.indexOf('?');
  return query >= 0 ? url.substring(0, query) : url;
}
#endif

#if defined(DEVICE_ESP32_S3_RGB_480)
// Native WiFi does not need the ESP32-P4/ESP-Hosted separation between the
// complete HTTPS download and flash writes. Keeping only one verified range
// in PSRAM avoids trying to reserve a ~5.4 MB firmware image on an 8 MB board.
constexpr bool kStageCompleteImage = false;
#else
constexpr bool kStageCompleteImage = true;
#endif

class PsramStageBuffer {
 public:
  ~PsramStageBuffer() { release(); }

  bool allocate(size_t capacity) {
    release();
    if (capacity == 0) return true;

    const size_t required_blocks =
        capacity / kStageBlockBytes +
        ((capacity % kStageBlockBytes) != 0 ? 1 : 0);
    if (required_blocks == 0 || required_blocks > kMaxStageBlocks) {
      return false;
    }

    capacity_ = capacity;
    for (size_t i = 0; i < required_blocks; ++i) {
      const size_t offset = i * kStageBlockBytes;
      const size_t block_bytes =
          std::min(kStageBlockBytes, capacity - offset);
      blocks_[i] = static_cast<uint8_t*>(heap_caps_malloc(
          block_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!blocks_[i]) {
        release();
        return false;
      }
      ++block_count_;
    }
    return true;
  }

  void release() {
    for (size_t i = 0; i < block_count_; ++i) {
      heap_caps_free(blocks_[i]);
      blocks_[i] = nullptr;
    }
    block_count_ = 0;
    capacity_ = 0;
  }

  uint8_t* block(size_t index) const {
    return index < block_count_ ? blocks_[index] : nullptr;
  }

  size_t blockSize(size_t index) const {
    if (index >= block_count_) return 0;
    const size_t offset = index * kStageBlockBytes;
    return std::min(kStageBlockBytes, capacity_ - offset);
  }

  size_t blockCount() const { return block_count_; }

 private:
  uint8_t* blocks_[kMaxStageBlocks] = {};
  size_t block_count_ = 0;
  size_t capacity_ = 0;
};

// P4 needs its scarce internal RAM kept free during HTTPS, so it retains the
// established PSRAM-first policy. On S3 RGB boards, TLS and the continuously
// scanned framebuffers would otherwise compete on the same PSRAM bus; prefer
// internal RAM there and retain PSRAM only as an allocation fallback.
void* checkTlsInternalCalloc(size_t count, size_t size) {
  return heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void* checkTlsPreferredCalloc(size_t count, size_t size) {
#if defined(DEVICE_ESP32_S3_RGB_480)
  void* ptr = checkTlsInternalCalloc(count, size);
  return ptr ? ptr : heap_caps_calloc(
                         count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  void* ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return ptr ? ptr : checkTlsInternalCalloc(count, size);
#endif
}

void checkTlsHeapFree(void* ptr) {
  heap_caps_free(ptr);
}

class ScopedCheckTlsAllocator {
 public:
  ScopedCheckTlsAllocator()
      : active_(mbedtls_platform_set_calloc_free(checkTlsPreferredCalloc,
                                                 checkTlsHeapFree) == 0) {}

  ~ScopedCheckTlsAllocator() {
    if (active_) {
      // This matches CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC in the selected cores.
      mbedtls_platform_set_calloc_free(checkTlsInternalCalloc,
                                       checkTlsHeapFree);
    }
  }

  bool active() const { return active_; }

 private:
  bool active_;
};

struct ParsedHttpsUrl {
  String host;
  String path;
  uint16_t port = 443;
};

// "v0.3.1" / "0.3.1" -> {0,3,1}; missing components remain zero.
void parseVersion(const char* s, int out[3]) {
  out[0] = out[1] = out[2] = 0;
  if (!s) return;
  while (*s && !isdigit(static_cast<unsigned char>(*s))) ++s;
  for (int i = 0; i < 3 && *s; ++i) {
    out[i] = atoi(s);
    while (*s && isdigit(static_cast<unsigned char>(*s))) ++s;
    if (*s != '.') break;
    ++s;
  }
}

bool isNewerThanCurrent(const char* tag) {
  int latest[3];
  int current[3];
  parseVersion(tag, latest);
  parseVersion(FW_VERSION, current);
  for (int i = 0; i < 3; ++i) {
    if (latest[i] != current[i]) return latest[i] > current[i];
  }
  return false;
}

bool parseHttpsUrl(const String& url, ParsedHttpsUrl& out) {
  constexpr const char* kPrefix = "https://";
  if (!url.startsWith(kPrefix)) return false;

  const int host_start = strlen(kPrefix);
  int path_start = url.indexOf('/', host_start);
  if (path_start < 0) path_start = url.length();

  String host_port = url.substring(host_start, path_start);
  if (!host_port.length()) return false;

  out.port = 443;
  const int colon = host_port.lastIndexOf(':');
  if (colon > 0) {
    out.host = host_port.substring(0, colon);
    const int parsed_port = atoi(host_port.substring(colon + 1).c_str());
    if (parsed_port <= 0 || parsed_port > 65535) return false;
    out.port = static_cast<uint16_t>(parsed_port);
  } else {
    out.host = host_port;
  }

  out.path = (path_start < url.length()) ? url.substring(path_start) : "/";
  return out.host.length() && out.path.length();
}

bool readHttpLine(NetworkClientSecure& client, String& line, uint32_t timeout_ms) {
  line = "";
  const uint32_t start_ms = millis();
  while (millis() - start_ms < timeout_ms) {
    while (client.available() > 0) {
      const char c = static_cast<char>(client.read());
      if (c == '\r') continue;
      if (c == '\n') return true;
      if (line.length() < kMaxHttpLineLen) line += c;
    }
    if (!client.connected() && !client.available()) return line.length() > 0;
    delay(1);
  }
  return false;
}

bool parseStatusCode(const String& status_line, int& code) {
  const int first_space = status_line.indexOf(' ');
  if (first_space < 0 || first_space + 4 > status_line.length()) return false;
  code = atoi(status_line.substring(first_space + 1, first_space + 4).c_str());
  return code > 0;
}

size_t parseSizeT(const String& value) {
  return static_cast<size_t>(strtoul(value.c_str(), nullptr, 10));
}

bool parseContentRangeTotal(const String& value, size_t& total) {
  const int slash = value.lastIndexOf('/');
  if (slash < 0 || slash + 1 >= value.length()) return false;
  const String total_part = value.substring(slash + 1);
  if (total_part == "*") return false;
  total = parseSizeT(total_part);
  return total > 0;
}

String resolveRedirectUrl(const String& current_url,
                          const ParsedHttpsUrl& current,
                          const String& location) {
  if (location.startsWith("https://")) return location;
  if (location.startsWith("//")) return String("https:") + location;
  if (location.startsWith("/")) {
    return String("https://") + current.host + location;
  }

  const int slash = current_url.lastIndexOf('/');
  if (slash > strlen("https://")) {
    return current_url.substring(0, slash + 1) + location;
  }
  return String("https://") + current.host + "/" + location;
}

String releaseDownloadUrl(const char* tag, const String& asset_name) {
  return String(GithubUpdate::kRepoUrl) + "/releases/download/" + tag + "/" + asset_name;
}

String legacyDeviceSlug() {
  String slug = Device::profile().key;
  slug.replace('_', '-');
  return slug;
}

bool releaseAssetDeviceKey(String& key_out, String& error_out) {
  key_out = Device::profile().key;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  const uint16_t chip_revision = ESP.getChipRevision();
  const auto& silicon = firmware_meta::currentSiliconRevisionDescriptor();
  if (chip_revision < silicon.minimum_revision ||
      chip_revision > silicon.maximum_revision) {
    error_out = String("unsupported ESP32-P4 revision ") + chip_revision;
    return false;
  }
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)
  if (strcmp(silicon.variant, "rev3_1") == 0) {
    key_out = "waveshare_touch_lcd_7b_rev3_1";
  } else if (strcmp(silicon.variant, "pre_v3") != 0) {
    error_out = String("unknown firmware silicon variant ") + silicon.variant;
    return false;
  }
#endif
  Serial.printf("[Update] ESP32-P4 silicon revision=%u variant=%s asset=%s\n",
                static_cast<unsigned>(chip_revision), silicon.variant, key_out.c_str());
#endif
  return true;
}

void logCheckNetworkState(const char* label, const String& url) {
  const uint32_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const uint32_t dma_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  const uint32_t dma_largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

  Serial.printf("[Update/Diag] %s url=%s\n",
                label ? label : "?",
                url.c_str());
  const long rssi = networkTransport.activeKind() == NetworkTransportKind::Wifi
                        ? static_cast<long>(WiFi.RSSI())
                        : 0;
  Serial.printf(
      "[Update/Diag] transport=%s connected=%d ip=%s gw=%s dns=%s rssi=%ld\n",
      networkTransport.activeName(),
      networkTransport.isConnected() ? 1 : 0,
      networkTransport.localIP().toString().c_str(),
      networkTransport.gatewayIP().toString().c_str(),
      networkTransport.dnsIP(0).toString().c_str(),
      rssi);
  Serial.printf("[Update/Diag] mem int=%u KB largest=%u KB dma=%u KB dma_largest=%u KB psram=%u KB\n",
                static_cast<unsigned>(int_free / 1024),
                static_cast<unsigned>(int_largest / 1024),
                static_cast<unsigned>(dma_free / 1024),
                static_cast<unsigned>(dma_largest / 1024),
                static_cast<unsigned>(ESP.getFreePsram() / 1024));
}

// A safe restart after failed installation leaves no core dump. install()
// collects range, attempt, error, memory and Wi-Fi details here; the caller
// appends them to /crashlog.txt before restarting.
String g_install_diag;
constexpr size_t kInstallDiagMaxBytes = 1536;
// Set after asset and firmware metadata validation. Subsequent transport
// failures, such as an ESP-Hosted disconnect or CDN failure, can justify
// a retry after restarting; invalid image metadata cannot.
bool g_install_retryable = false;

void installDiagLine(const String& line) {
  if (g_install_diag.length() + line.length() + 1 > kInstallDiagMaxBytes) {
    return;  // Bound the report; the first errors are the most useful.
  }
  g_install_diag += line;
  g_install_diag += '\n';
}

String memSnapshotLine() {
  char buf[112];
  snprintf(buf, sizeof(buf),
           "mem int=%uKB largest=%uKB dma_largest=%uKB psram_largest=%uKB",
           static_cast<unsigned>(
               heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           static_cast<unsigned>(
               heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
           static_cast<unsigned>(heap_caps_get_largest_free_block(
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) /
                                 1024),
           static_cast<unsigned>(
               heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
  return String(buf);
}

// Return the mbedTLS error code (zero means none) so callers can recognize
// allocation failures such as MBEDTLS_ERR_SSL_ALLOC_FAILED.
int logCheckFailureDetails(int code, NetworkClientSecure& client, const String& url) {
  char tls_error[96] = {};
  const int tls_code = client.lastError(tls_error, sizeof(tls_error));

  IPAddress github_ip;
  const int dns_ok = Network.hostByName("github.com", github_ip) ? 1 : 0;

  if (code < 0) {
    Serial.printf("[Update/Diag] http=%d (%s) tls=%d (%s) fd=%d\n",
                  code,
                  HTTPClient::errorToString(code).c_str(),
                  tls_code,
                  tls_error,
                  client.fd());
  } else {
    Serial.printf("[Update/Diag] http=%d tls=%d (%s) fd=%d\n",
                  code,
                  tls_code,
                  tls_error,
                  client.fd());
  }
  Serial.printf("[Update/Diag] dns github.com: ok=%d ip=%s | url=%s\n",
                dns_ok,
                github_ip.toString().c_str(),
                url.c_str());
  return tls_code;
}

typedef bool (*RangeDataFn)(const uint8_t* data, size_t len, void* ctx);

bool fetchHttpRange(const String& start_url, size_t from, size_t to,
                    uint8_t* buf, size_t buf_len, RangeDataFn on_data,
                    void* ctx, size_t& content_total, String& error_out,
                    String* final_url_out = nullptr) {
  if (!buf || !buf_len || !on_data || to < from) {
    error_out = "invalid range request";
    return false;
  }

#if defined(DEVICE_ESP32_S3_RGB_480)
  // OTA preparation releases mostly PSRAM on these boards. Keep the same TLS
  // fallback as the update check when internal RAM cannot fit a handshake.
  // All redirect/range clients must release their blocks before restoration.
  ScopedCheckTlsAllocator tls_allocator;
#endif
  String url = start_url;
  for (int redirect = 0; redirect < 5; ++redirect) {
    ParsedHttpsUrl parsed;
    if (!parseHttpsUrl(url, parsed)) {
      error_out = "bad https url";
      return false;
    }

    NetworkClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);
    if (!client.connect(parsed.host.c_str(), parsed.port, kConnectTimeoutMs)) {
      error_out = String("connect failed: ") + parsed.host;
#if defined(DEVICE_ESP32_S3_RGB_480)
      char tls_error[96] = {};
      const int tls_code = client.lastError(tls_error, sizeof(tls_error));
      char detail[224];
      snprintf(detail, sizeof(detail), "HTTPS host=%s tls=%d (%s) allocator=%s",
               parsed.host.c_str(), tls_code, tls_error,
               tls_allocator.active() ? "internal/PSRAM" : "core default");
      Serial.printf("[Update/Diag] %s\n", detail);
      installDiagLine(String(detail) + " | " + memSnapshotLine());
#endif
      return false;
    }

    int rcvbuf = static_cast<int>(kSocketRxBufferBytes);
    if (client.fd() >= 0) {
      client.setSocketOption(SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    }

    client.printf("GET %s HTTP/1.1\r\n", parsed.path.c_str());
    client.printf("Host: %s\r\n", parsed.host.c_str());
    client.print("User-Agent: hometiles\r\n");
    client.print("Accept: application/octet-stream\r\n");
    client.print("Accept-Encoding: identity\r\n");
    client.printf("Range: bytes=%u-%u\r\n",
                  static_cast<unsigned>(from), static_cast<unsigned>(to));
    client.print("Connection: close\r\n\r\n");

    String line;
    if (!readHttpLine(client, line, kReadTimeoutMs)) {
      client.stop();
      error_out = "no http status";
      return false;
    }

    int status_code = 0;
    if (!parseStatusCode(line, status_code)) {
      client.stop();
      error_out = "bad http status";
      return false;
    }

    String location;
    size_t content_length = 0;
    bool has_content_length = false;
    size_t range_total = 0;

    bool header_complete = false;
    while (readHttpLine(client, line, kReadTimeoutMs)) {
      if (!line.length()) {
        header_complete = true;
        break;
      }
      const int colon = line.indexOf(':');
      if (colon <= 0) continue;
      String name = line.substring(0, colon);
      name.toLowerCase();
      String value = line.substring(colon + 1);
      value.trim();
      if (name == "location") {
        location = value;
      } else if (name == "content-length") {
        content_length = parseSizeT(value);
        has_content_length = content_length > 0;
      } else if (name == "content-range") {
        parseContentRangeTotal(value, range_total);
      }
    }
    if (!header_complete) {
      client.stop();
      error_out = "header timeout";
      return false;
    }

#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
    const String safe_url = diagnosticUrl(url);
    Serial.printf(
        "[S3Diag/GitHubOTA] phase=http url=%s status=%d range=%u-%u "
        "content_length=%u expected_total=%u redirect=%u\n",
        safe_url.c_str(), status_code, static_cast<unsigned>(from),
        static_cast<unsigned>(to), static_cast<unsigned>(content_length),
        static_cast<unsigned>(range_total),
        (status_code >= 300 && status_code < 400) ? 1U : 0U);
#endif

    if (status_code >= 300 && status_code < 400 && location.length()) {
      client.stop();
      url = resolveRedirectUrl(url, parsed, location);
      continue;
    }

    if (status_code != 206) {
      client.stop();
      error_out = String("HTTP ") + status_code;
      return false;
    }
    if (!range_total) {
      client.stop();
      error_out = "missing content-range";
      return false;
    }
    content_total = range_total;

    const size_t expected = to - from + 1;
    if (has_content_length && content_length != expected) {
      client.stop();
      error_out = "range length mismatch";
      return false;
    }

    size_t received = 0;
    uint32_t last_data_ms = millis();
    while (received < expected) {
      const int avail = client.available();
      if (avail > 0) {
        size_t want = expected - received;
        if (want > buf_len) want = buf_len;
        if (want > static_cast<size_t>(avail)) want = static_cast<size_t>(avail);

        const int r = client.read(buf, want);
        if (r <= 0) {
          delay(1);
          continue;
        }
        if (!on_data(buf, static_cast<size_t>(r), ctx)) {
          client.stop();
          if (!error_out.length()) error_out = "write failed";
          return false;
        }
        received += static_cast<size_t>(r);
        last_data_ms = millis();
        if (kReadPaceMs > 0) {
          delay(kReadPaceMs);
        } else {
          yield();
        }
      } else {
        if (!client.connected() || millis() - last_data_ms > kReadTimeoutMs) {
          client.stop();
          error_out = client.connected() ? "timeout" : "connection lost";
          return false;
        }
        delay(10);
      }
    }

    client.stop();
    if (final_url_out) *final_url_out = url;
    return true;
  }

  error_out = "too many redirects";
  return false;
}

struct HeadBufferCtx {
  uint8_t* data = nullptr;
  size_t capacity = 0;
  size_t len = 0;
  // Set only for staged downloads. Report progress while receiving so the
  // caller can keep its activity timer and status current.
  size_t progress_base = 0;
  size_t progress_total = 0;
  uint32_t last_progress_ms = 0;
  GithubUpdate::ProgressFn progress = nullptr;
};

bool storeHeadBytes(const uint8_t* data, size_t len, void* raw_ctx) {
  auto* ctx = static_cast<HeadBufferCtx*>(raw_ctx);
  if (!ctx || !ctx->data || ctx->len + len > ctx->capacity) return false;
  memcpy(ctx->data + ctx->len, data, len);
  ctx->len += len;
  return true;
}

bool storeBufferBytes(const uint8_t* data, size_t len, void* raw_ctx) {
  auto* ctx = static_cast<HeadBufferCtx*>(raw_ctx);
  if (!ctx || !ctx->data || ctx->len + len > ctx->capacity) return false;
  memcpy(ctx->data + ctx->len, data, len);
  ctx->len += len;
  if (ctx->progress) {
    const uint32_t now_ms = millis();
    if (now_ms - ctx->last_progress_ms >= 500) {
      ctx->last_progress_ms = now_ms;
      ctx->progress(ctx->progress_base + ctx->len, ctx->progress_total);
    }
  }
  return true;
}

struct UpdateWriteCtx {
  size_t written = 0;
  size_t total = 0;
  size_t next_progress_log = 512 * 1024;
  uint32_t last_progress_ms = 0;
  GithubUpdate::ProgressFn progress = nullptr;
  String* error = nullptr;
  uint8_t update_error = 0;
};

void reportInstallProgress(UpdateWriteCtx& ctx, bool force) {
  if (!ctx.progress) return;
  const uint32_t now_ms = millis();
  if (force || now_ms - ctx.last_progress_ms >= 500 ||
      (ctx.total && ctx.written >= ctx.total)) {
    ctx.last_progress_ms = now_ms;
    ctx.progress(ctx.written, ctx.total);
  }
}

bool writeUpdateBytes(const uint8_t* data, size_t len, void* raw_ctx) {
  auto* ctx = static_cast<UpdateWriteCtx*>(raw_ctx);
  if (!ctx || !data) return false;
  // Write the stage in chunks. A single Update.write() can block the loop
  // for seconds during flash erase/write, preventing the ESP-Hosted SDIO
  // driver from storing incoming frames and causing pkt_rxbuff assertions
  // (sdio_drv.c:928). Pauses let it drain the RX queue. The old 32 KB stages
  // kept that window short enough; 512 KB stages did not.
  size_t offset = 0;
  while (offset < len) {
    size_t slice = len - offset;
    if (slice > kInstallWriteSliceBytes) slice = kInstallWriteSliceBytes;
    const size_t bytes_written =
        Update.write(const_cast<uint8_t*>(data + offset), slice);
    if (bytes_written != slice) {
      ctx->update_error = Update.getError();
      const String update_error_text = Update.errorString();
      if (ctx->error) *ctx->error = update_error_text;
      Serial.printf(
          "[Update] Update.write failed: requested=%u wrote=%u "
          "total=%u/%u code=%u -> %s\n",
          static_cast<unsigned>(slice),
          static_cast<unsigned>(bytes_written),
          static_cast<unsigned>(ctx->written + bytes_written),
          static_cast<unsigned>(ctx->total),
          static_cast<unsigned>(ctx->update_error),
          update_error_text.c_str());
      return false;
    }
    ctx->written += bytes_written;
    offset += slice;
    if (offset < len) delay(2);
  }
  if (ctx->written >= ctx->next_progress_log ||
      (ctx->total && ctx->written >= ctx->total)) {
    Serial.printf("[Update] Install progress: %u / %u bytes\n",
                  static_cast<unsigned>(ctx->written),
                  static_cast<unsigned>(ctx->total));
    ctx->next_progress_log += 512 * 1024;
  }
  reportInstallProgress(*ctx, false);
  return true;
}

}  // namespace

namespace GithubUpdate {

CheckResult checkLatest() {
  CheckResult result;
  if (!networkTransport.isConnected()) {
    Serial.println("[Update] Check skipped: no network");
    return result;
  }

  // The guard outlives NetworkClientSecure and HTTPClient, so every temporary
  // TLS block is released before the global allocator is restored.
  ScopedCheckTlsAllocator tls_allocator;
#if defined(DEVICE_ESP32_S3_RGB_480)
  constexpr const char* kTlsPreference =
      "internal RAM first, PSRAM fallback";
#else
  constexpr const char* kTlsPreference =
      "PSRAM first, internal RAM fallback";
#endif
  Serial.printf("[Update] Check: TLS allocator %s\n",
                tls_allocator.active() ? kTlsPreference : "core default");

  // The existing update connection skips certificate verification to avoid
  // failures when GitHub/CDN certificates rotate. Image magic, size and
  // device checks validate format and compatibility, not server identity.
  NetworkClientSecure client;
  client.setInsecure();

  // Do not follow every redirect automatically: Location carries the release
  // tag. A repository rename first redirects to the new /releases/latest
  // path, so follow that chain manually until a tag URL appears. Otherwise
  // installed devices would lose update discovery after a repository rename.
  String url = String(kRepoUrl) + "/releases/latest";
  String tag;
  logCheckNetworkState("check-start", url);
  for (int hop = 0; hop < 4; ++hop) {
    client.stop();
    client.setTimeout(8000);
    client.setHandshakeTimeout(12);

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setReuse(false);
    if (!http.begin(client, url)) {
      Serial.printf("[Update] Check failed: HTTP begin (%s)\n", url.c_str());
      logCheckFailureDetails(0, client, url);
      client.stop();
      return result;
    }
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.addHeader("Connection", "close");
    const char* kCollect[] = {"Location"};
    http.collectHeaders(kCollect, 1);

    const int code = http.GET();
    const String location = http.header("Location");
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
    const String safe_check_url = diagnosticUrl(url);
    const String safe_location = diagnosticUrl(location);
    Serial.printf(
        "[S3Diag/GitHubOTA] phase=latest-check url=%s status=%d "
        "location=%s\n",
        safe_check_url.c_str(), code,
        safe_location.length() ? safe_location.c_str() : "-");
#endif
    http.end();
    client.stop();
    delay(10);

    if (code < 300 || code >= 400 || !location.length()) {
      if (code < 0) {
        Serial.printf("[Update] Check failed: HTTP %d (%s)\n",
                      code,
                      HTTPClient::errorToString(code).c_str());
      } else {
        Serial.printf("[Update] Check failed: HTTP %d\n", code);
      }
      result.tls_alloc_failed =
          logCheckFailureDetails(code, client, url) == MBEDTLS_ERR_SSL_ALLOC_FAILED;
      return result;
    }

    const int tag_pos = location.indexOf("/releases/tag/");
    if (tag_pos >= 0) {
      tag = location.substring(tag_pos + strlen("/releases/tag/"));
      break;
    }
    if (location.indexOf("/releases/latest") < 0) {
      // Neither a tag nor a latest path: the repository may have no releases
      // and redirect to /releases, or the response is unexpected.
      Serial.printf("[Update] No release found (%s)\n", location.c_str());
      return result;
    }
    url = location;  // Repository rename redirect: request the new URL.
  }

  if (!tag.length() || tag.length() >= sizeof(result.latest_tag)) {
    Serial.printf("[Update] Unexpected tag: '%s'\n", tag.c_str());
    return result;
  }

  snprintf(result.latest_tag, sizeof(result.latest_tag), "%s", tag.c_str());
  result.ok = true;
  result.update_available = isNewerThanCurrent(result.latest_tag);
  Serial.printf("[Update] Installed %s, latest release %s -> %s\n",
                FW_VERSION, result.latest_tag,
                result.update_available ? "update available" : "up to date");
  return result;
}

bool install(const char* tag, ProgressFn progress, String& error_out) {
  error_out = "";
  if (!tag || !*tag) {
    error_out = "no tag";
    return false;
  }
  if (!networkTransport.isConnected()) {
    error_out = "no network";
    return false;
  }
  // Clean up any unfinished Web OTA session.
  if (Update.isRunning()) Update.abort();

  String asset_device_key;
  if (!releaseAssetDeviceKey(asset_device_key, error_out)) {
    return false;
  }
  String url = releaseDownloadUrl(
      tag, String("hometiles_") + tag + "_" + asset_device_key + ".bin");
  Serial.printf("[Update] Downloading %s\n", url.c_str());
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
  Serial.printf(
      "[S3Diag/GitHubOTA] phase=asset-select tag=%s url=%s\n", tag,
      diagnosticUrl(url).c_str());
#endif

  g_install_diag = "";
  g_install_retryable = false;
  String transport_diag =
      String("Start: Uptime ") + (millis() / 1000) + " s | " +
      networkTransport.activeName();
  if (networkTransport.activeKind() == NetworkTransportKind::Wifi) {
    transport_diag += " RSSI ";
    transport_diag += WiFi.RSSI();
    transport_diag += " dBm";
  }
  installDiagLine(transport_diag + " | " + memSnapshotLine());

  // Network read buffers and OTA staging use PSRAM. Download the complete
  // P4 image before Update.begin(), then end TLS and release its internal
  // AES/DMA memory before writing flash. ESP-Hosted RX, mbedTLS and the
  // encrypted OTA writer must not compete simultaneously for internal RAM.
  uint8_t* net_buf = static_cast<uint8_t*>(
      heap_caps_malloc(kInstallReadChunk, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!net_buf) net_buf = static_cast<uint8_t*>(malloc(kInstallReadChunk));
  PsramStageBuffer stage;
  if (!net_buf) {
    error_out = "alloc failed";
    return false;
  }

  bool failed = false;
  bool update_started = false;
  size_t total_sz = 0;

  uint8_t image_head[firmware_meta::kDeviceDescriptorImageBytes] = {0};
  HeadBufferCtx head_ctx{image_head, sizeof(image_head), 0};
  String resolved_asset_url;
  if (!fetchHttpRange(url, 0, sizeof(image_head) - 1, net_buf, kInstallReadChunk,
                      storeHeadBytes, &head_ctx, total_sz, error_out,
                      &resolved_asset_url)) {
    const String first_error = error_out;
    const bool allow_legacy_fallback =
        asset_device_key == Device::profile().key;
    const String legacy_url =
        allow_legacy_fallback
            ? releaseDownloadUrl(
                  tag,
                  String("esp32-p4-homeassistant-display-") + tag + "-" +
                      legacyDeviceSlug() + "-update.bin")
            : String();
    memset(image_head, 0, sizeof(image_head));
    head_ctx.len = 0;
    total_sz = 0;
    error_out = "";
    resolved_asset_url = "";
    if (!allow_legacy_fallback) {
      error_out = first_error;
      failed = true;
    } else {
      Serial.printf("[Update] Asset unavailable (%s), trying %s\n",
                    first_error.c_str(), legacy_url.c_str());
    }
    if (!failed && !fetchHttpRange(legacy_url, 0, sizeof(image_head) - 1,
                                   net_buf, kInstallReadChunk, storeHeadBytes,
                                   &head_ctx, total_sz, error_out,
                                   &resolved_asset_url)) {
#if defined(DEVICE_ESP32_S3_RGB_480)
      g_install_retryable =
          isRetryableTransportError(first_error) ||
          isRetryableTransportError(error_out);
#endif
      error_out = first_error + "; fallback: " + error_out;
      failed = true;
    } else if (!failed) {
      url = legacy_url;
    }
  }
  if (!failed && resolved_asset_url.length()) {
    // Reuse the resolved CDN URL for later ranges to avoid another GitHub
    // redirect and TLS connection for each block.
    url = resolved_asset_url;
  }
  if (!failed) {
    delay(20);
  }

  if (!failed) {
    if (total_sz < firmware_meta::kDeviceDescriptorImageBytes) {
      error_out = "image too small";
      failed = true;
    }
  }

  if (!failed) {
    firmware_meta::DeviceDescriptor incoming_desc{};
    firmware_meta::SiliconRevisionDescriptor incoming_silicon{};
    bool accepted_legacy_silicon = false;
    if (!firmware_meta::parseDeviceDescriptorFromImage(
            image_head, head_ctx.len, incoming_desc)) {
      error_out = "firmware metadata missing or invalid";
      failed = true;
    } else if (!firmware_meta::matchesCurrentDeviceKey(incoming_desc.device_key)) {
      error_out = String("device mismatch: got ") +
                  incoming_desc.display_name + ", expected " +
                  firmware_meta::expectedDeviceDisplayName();
      failed = true;
    } else if (strcmp(incoming_desc.project_key,
                      firmware_meta::currentProjectKey()) != 0) {
      error_out = String("project mismatch: got ") +
                  incoming_desc.project_key + ", expected " +
                  firmware_meta::currentProjectKey();
      failed = true;
    } else if (!firmware_meta::imageMatchesCurrentSiliconVariant(
                   image_head, head_ctx.len, &accepted_legacy_silicon)) {
      if (firmware_meta::parseSiliconRevisionDescriptorFromImage(
              image_head, head_ctx.len, incoming_silicon)) {
        if (firmware_meta::matchesCurrentSiliconVariant(
                incoming_silicon.variant)) {
          if (!firmware_meta::matchesCurrentSiliconRevisionRange(
                  incoming_silicon.minimum_revision,
                  incoming_silicon.maximum_revision)) {
            const auto& current_silicon =
                firmware_meta::currentSiliconRevisionDescriptor();
            error_out = String("firmware silicon range ") +
                        incoming_silicon.minimum_revision + "-" +
                        incoming_silicon.maximum_revision +
                        " exceeds supported range " +
                        current_silicon.minimum_revision + "-" +
                        current_silicon.maximum_revision;
          } else {
            error_out = String("firmware silicon range ") +
                        incoming_silicon.minimum_revision + "-" +
                        incoming_silicon.maximum_revision +
                        " does not support chip revision " +
                        ESP.getChipRevision();
          }
        } else {
          error_out = String("silicon variant mismatch: got ") +
                      incoming_silicon.variant + ", expected " +
                      firmware_meta::currentSiliconVariant();
        }
      } else {
        error_out = String("legacy firmware is not safe for silicon variant ") +
                    firmware_meta::currentSiliconVariant();
      }
      failed = true;
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
    } else {
      Serial.printf(
          "[S3Diag/GitHubOTA] phase=metadata result=ok expected_size=%u "
          "device=%s project=%s\n",
          static_cast<unsigned>(total_sz), incoming_desc.device_key,
          incoming_desc.project_key);
#endif
    }
    if (!failed && accepted_legacy_silicon) {
      Serial.printf(
          "[Update] Accepted legacy firmware metadata for silicon variant %s\n",
          firmware_meta::currentSiliconVariant());
    }
  }

#if !defined(DEVICE_ESP32_S3_RGB_480)
  // Preserve the established P4 retry behaviour. The stricter distinction
  // between transport and deterministic OTA failures is S3-only.
  if (!failed) g_install_retryable = true;
#endif

  size_t image_remainder = 0;
  size_t stage_capacity = 0;
  size_t staged_len = 0;
  UpdateWriteCtx write_ctx;
  if (!failed) {
    image_remainder = total_sz - head_ctx.len;
    // P4/ESP-Hosted retains complete separation between download and flash
    // writing. Native S3 Wi-Fi needs only one verified 512 KB range buffer
    // and then writes that range directly to the inactive OTA partition.
    stage_capacity =
        kStageCompleteImage
            ? image_remainder
            : std::min(image_remainder, kStageRangeBytes);
    if (!stage.allocate(stage_capacity)) {
      error_out = "not enough PSRAM for safe OTA staging";
      Serial.printf(
          "[Update] PSRAM staging failed: need=%uKB free=%uKB "
          "largest=%uKB block=%uKB\n",
          static_cast<unsigned>(stage_capacity / 1024),
          static_cast<unsigned>(
              heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
          static_cast<unsigned>(
              heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024),
          static_cast<unsigned>(kStageBlockBytes / 1024));
      failed = true;
    } else {
      Serial.printf("[Update] Safe %s staging: %uB TCP RX, %uB read chunk, %uKB ranges, %uKB PSRAM in %u blocks\n",
                    kStageCompleteImage ? "full-image" : "rolling-range",
                    static_cast<unsigned>(kSocketRxBufferBytes),
                    static_cast<unsigned>(kInstallReadChunk),
                    static_cast<unsigned>(kStageRangeBytes / 1024),
                    static_cast<unsigned>(stage_capacity / 1024),
                    static_cast<unsigned>(stage.blockCount()));
    }
  }

  if (!failed && !kStageCompleteImage) {
    GuitionS3Diagnostics::logOtaPartitions("github-begin");
    const esp_partition_t* next_partition =
        esp_ota_get_next_update_partition(nullptr);
    if (!next_partition) {
      error_out = "no next OTA partition";
      g_install_retryable = false;
      failed = true;
      Serial.println(
          "[Update] Update.begin failed: no next OTA partition");
    } else if (total_sz > next_partition->size) {
      error_out = "image exceeds next OTA partition";
      g_install_retryable = false;
      failed = true;
      Serial.printf(
          "[Update] Update.begin rejected: image=%u target=%s size=%u\n",
          static_cast<unsigned>(total_sz), next_partition->label,
          static_cast<unsigned>(next_partition->size));
    } else if (!Update.begin(total_sz, U_FLASH)) {
      const uint8_t update_error = Update.getError();
      error_out = String("Updater [") +
                  String(static_cast<unsigned>(update_error)) + "]: " +
                  Update.errorString();
      g_install_retryable = false;
      failed = true;
      Serial.printf(
          "[Update] Update.begin failed: target=%s size=%u "
          "code=%u -> %s\n",
          next_partition->label, static_cast<unsigned>(total_sz),
          static_cast<unsigned>(update_error), Update.errorString());
    } else {
      update_started = true;
      write_ctx.total = total_sz;
      write_ctx.progress = progress;
      write_ctx.error = &error_out;
      Serial.printf("[Update] Update.begin OK, size: %u\n",
                    static_cast<unsigned>(total_sz));
      if (!writeUpdateBytes(image_head, head_ctx.len, &write_ctx)) {
        if (!error_out.length()) error_out = Update.errorString();
        g_install_retryable = false;
        failed = true;
      } else {
        reportInstallProgress(write_ctx, true);
      }
    }
  }

  // Bounded range requests prevent ESP-Hosted from receiving one continuous
  // stream for minutes. On S3, each complete range is verified in PSRAM
  // before it is written to the inactive OTA partition; an interrupted
  // range can therefore be downloaded again without writing it twice.
  if (!failed) {
    while (staged_len < image_remainder && !failed) {
      const size_t range_start = head_ctx.len + staged_len;
      size_t range_len = image_remainder - staged_len;
      if (range_len > kStageRangeBytes) range_len = kStageRangeBytes;
      const size_t range_end = range_start + range_len - 1;
      bool range_ok = false;
      bool range_failure_retryable = false;
      const size_t block_index =
          kStageCompleteImage ? staged_len / kStageBlockBytes : 0;
      uint8_t* const stage_block = stage.block(block_index);
      if (!stage_block || range_len > stage.blockSize(block_index)) {
        error_out = "invalid OTA staging block";
        failed = true;
        break;
      }

      for (uint8_t attempt = 1; attempt <= kStageRangeAttempts; ++attempt) {
        size_t range_total = 0;
        HeadBufferCtx stage_ctx{stage_block, range_len, 0};
        stage_ctx.progress_base = range_start;
        stage_ctx.progress_total = total_sz;
        stage_ctx.progress = kStageCompleteImage ? progress : nullptr;
        error_out = "";

        Serial.printf("[Update] Downloading range %u-%u (attempt %u/%u)\n",
                      static_cast<unsigned>(range_start),
                      static_cast<unsigned>(range_end),
                      static_cast<unsigned>(attempt),
                      static_cast<unsigned>(kStageRangeAttempts));
        if (fetchHttpRange(url, range_start, range_end, net_buf,
                           kInstallReadChunk, storeBufferBytes, &stage_ctx,
                           range_total, error_out) &&
            range_total == total_sz && stage_ctx.len == range_len) {
          range_ok = true;
          break;
        }

        if (!error_out.length()) {
          error_out = (range_total && range_total != total_sz)
                          ? "image size changed"
                          : "range incomplete";
        }
#if defined(DEVICE_ESP32_S3_RGB_480)
        if (isRetryableTransportError(error_out)) {
          range_failure_retryable = true;
        }
#endif
        Serial.printf("[Update] Range failed: %s\n", error_out.c_str());
        installDiagLine(String("Range ") + range_start + "-" + range_end +
                        " attempt " + attempt + ": " + error_out + " | " +
                        memSnapshotLine());
        // After a stream failure, ESP-Hosted needs more than a few hundred
        // milliseconds to drain queues and the memory pool. Immediate reconnects
        // otherwise fail again ("connect failed" in the field test).
        if (attempt < kStageRangeAttempts) delay(3000);
      }

      if (!range_ok) {
#if defined(DEVICE_ESP32_S3_RGB_480)
        g_install_retryable = range_failure_retryable;
#endif
        failed = true;
        break;
      }

      if (!kStageCompleteImage &&
          !writeUpdateBytes(stage_block, range_len, &write_ctx)) {
        if (!error_out.length()) error_out = Update.errorString();
        g_install_retryable = false;
        failed = true;
        break;
      }

      staged_len += range_len;
      if (kStageCompleteImage && progress) {
        progress(head_ctx.len + staged_len, total_sz);
      }
      Serial.printf("[Update] Download progress: %u / %u bytes\n",
                    static_cast<unsigned>(head_ctx.len + staged_len),
                    static_cast<unsigned>(total_sz));
      if (staged_len < image_remainder) {
        delay(kStageCompleteImage ? kStageRangePauseMs : 20);
      }
    }

    if (!failed) {
      if (kStageCompleteImage) {
        Serial.printf(
            "[Update] Download complete in PSRAM: %u / %u bytes\n",
            static_cast<unsigned>(head_ctx.len + staged_len),
            static_cast<unsigned>(total_sz));
      } else {
        reportInstallProgress(write_ctx, true);
        Serial.printf(
            "[Update] Range download and installation complete: %u / %u bytes\n",
            static_cast<unsigned>(head_ctx.len + staged_len),
            static_cast<unsigned>(total_sz));
      }
    }
  }

  // fetchHttpRange has already destroyed the HTTPS client. Release the last
  // read buffer before Update.begin() and briefly let SDIO process packets
  // that were already received.
  free(net_buf);
  net_buf = nullptr;
  if (!failed) delay(50);

  if (!failed && kStageCompleteImage) {
    if (!Update.begin(total_sz, U_FLASH)) {
      const uint8_t update_error = Update.getError();
      error_out = String("Updater [") +
                  String(static_cast<unsigned>(update_error)) + "]: " +
                  Update.errorString();
#if defined(DEVICE_ESP32_S3_RGB_480)
      g_install_retryable = false;
#endif
      failed = true;
      Serial.printf(
          "[Update] Update.begin failed: size=%u code=%u -> %s\n",
          static_cast<unsigned>(total_sz),
          static_cast<unsigned>(update_error), Update.errorString());
    } else {
      update_started = true;
      Serial.printf("[Update] Update.begin OK, size: %u\n",
                    static_cast<unsigned>(total_sz));
      write_ctx.total = total_sz;
      write_ctx.progress = progress;
      write_ctx.error = &error_out;
      if (!writeUpdateBytes(image_head, head_ctx.len, &write_ctx)) {
        if (!error_out.length()) error_out = Update.errorString();
#if defined(DEVICE_ESP32_S3_RGB_480)
        g_install_retryable = false;
#endif
        failed = true;
      } else {
        reportInstallProgress(write_ctx, true);
      }

      for (size_t i = 0; !failed && i < stage.blockCount(); ++i) {
        if (!writeUpdateBytes(stage.block(i), stage.blockSize(i),
                              &write_ctx)) {
          if (!error_out.length()) error_out = Update.errorString();
#if defined(DEVICE_ESP32_S3_RGB_480)
          g_install_retryable = false;
#endif
          failed = true;
        }
      }
    }
  }

  stage.release();
  if (net_buf) free(net_buf);

#if defined(DEVICE_ESP32_S3_RGB_480)
  if (!failed && write_ctx.written != total_sz) {
    error_out = String("OTA byte count mismatch: written ") +
                write_ctx.written + " expected " + total_sz;
    g_install_retryable = false;
    failed = true;
    Serial.printf(
        "[Update] Installation incomplete: written=%u expected=%u\n",
        static_cast<unsigned>(write_ctx.written),
        static_cast<unsigned>(total_sz));
  }
#endif

  if (failed) {
    if (update_started) Update.abort();
    Serial.printf("[Update] Install failed: %s\n", error_out.c_str());
    installDiagLine(String("Aborted at ") + (head_ctx.len + staged_len) +
                    " / " + total_sz + " Bytes: " + error_out);
    return false;
  }

#if defined(DEVICE_ESP32_S3_RGB_480)
  const bool update_end_ok = Update.end(false);
  constexpr const char* kUpdateEndMode = "false";
#else
  const bool update_end_ok = Update.end(true);
  constexpr const char* kUpdateEndMode = "true";
#endif
  if (!update_end_ok) {
    const uint8_t update_error = Update.getError();
    const String update_error_text = Update.errorString();
    error_out = String("Updater [") +
                String(static_cast<unsigned>(update_error)) + "]: " +
                update_error_text;
#if defined(DEVICE_ESP32_S3_RGB_480)
    g_install_retryable = false;
#endif
    Serial.printf(
        "[Update] Update.end(%s) failed: written=%u expected=%u "
        "code=%u -> %s\n",
        kUpdateEndMode,
        static_cast<unsigned>(write_ctx.written),
        static_cast<unsigned>(total_sz),
        static_cast<unsigned>(update_error), update_error_text.c_str());
    if (Update.isRunning()) Update.abort();
    return false;
  }
  GuitionS3Diagnostics::logOtaPartitions("github-end");
  Serial.printf("[Update] %u bytes installed - ready to restart\n",
                static_cast<unsigned>(total_sz));
  return true;
}

String lastInstallDiag() { return g_install_diag; }

bool lastInstallRetryable() { return g_install_retryable; }

}  // namespace GithubUpdate

#include "src/ui/shared/ui_surface_style.h"
#include "src/video/camera_stream.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/soc_caps.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/network_manager.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4) && \
    defined(SOC_JPEG_DECODE_SUPPORTED) && SOC_JPEG_DECODE_SUPPORTED

#include <driver/jpeg_decode.h>
#include <fcntl.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "src/core/display/dma2d_arbiter.h"
#include "src/devices/device.h"
#include "src/video/camera_geometry.h"

namespace {

constexpr uint16_t kWidth = camera_geometry::kWidth;
constexpr uint16_t kHeight = camera_geometry::kHeight;
constexpr uint16_t kDecodedWidth = camera_geometry::kDecodedWidth;
constexpr uint16_t kDecodedHeight = camera_geometry::kDecodedHeight;
constexpr size_t kPixelBytes =
    static_cast<size_t>(kDecodedWidth) * kDecodedHeight * sizeof(uint16_t);
constexpr size_t kMaxJpegBytes = 256U * 1024U;
// One buffer remains the protected LVGL fallback while PPA presents a second.
// A third buffer lets the decoder reserve the next frame instead of silently
// dropping it during that ~25 ms presentation window.
constexpr uint8_t kFrameBufferCount = 3;
constexpr int kCameraReceiveBufferBytes = 4 * 1024;
constexpr size_t kCameraChunkBytes = 8 * 1024;
constexpr uint32_t kCameraConnectTimeoutMs = 4000;
constexpr uint32_t kSocketPollTimeoutMs = 250;
constexpr size_t kMinCameraDmaHeadroomBytes = 24 * 1024;
constexpr uint32_t kDmaHeadroomGraceMs = 250;
constexpr char kCameraScheme[] = "tcp://";
constexpr char kCameraRequestPrefix[] = "HTCAM/1 ";
constexpr uint8_t kCameraHelloMagic[] = {'H', 'T', 'C', '1'};
constexpr uint8_t kCameraFrameMagic[] = {'H', 'T', 'F', '1'};
constexpr uint8_t kCameraAckMagic[] = {'H', 'T', 'A', '1'};
constexpr uint8_t kCameraMessageFrame = 1;
constexpr uint8_t kCameraMessageFlush = 2;
constexpr uint8_t kCameraMessageEnd = 3;
constexpr size_t kCameraHelloBytes = 8;
constexpr size_t kCameraFrameHeaderBytes = 16;
constexpr size_t kCameraAckBytes = 12;

enum class FrameState : uint8_t {
  Free,
  Writing,
  Ready,
  Presenting,
  Displayed,
};

portMUX_TYPE g_state_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_task = nullptr;
volatile bool g_stop_requested = false;
bool g_release_buffers = false;
String g_url;
uint16_t* g_pixels[kFrameBufferCount] = {};
lv_image_dsc_t g_images[kFrameBufferCount] = {};
FrameState g_frame_states[kFrameBufferCount] = {};
int8_t g_displayed_index = -1;
uint32_t g_status_sequence = 0;
uint32_t g_ui_status_sequence = 0;
char g_status[96] = "";
bool g_status_error = false;
uint32_t g_worker_frame_count = 0;
uint32_t g_no_write_buffer_drops = 0;
uint32_t g_presented_frame_count = 0;
uint32_t g_presented_fps_ms = 0;
uint64_t g_present_wait_total_us = 0;
uint64_t g_present_copy_total_us = 0;
uint32_t g_present_timed_frames = 0;
uint16_t g_corner_fill_swapped = 0;
bool g_direct_preview_logged = false;
bool g_direct_fallback_logged = false;

// Keep the engine alive across popup opens. Creating/deleting JPEG engines
// repeatedly churns the 2D-DMA pool shared with the display PPA.
jpeg_decoder_handle_t g_jpeg_decoder = nullptr;

static const i18n::Strings& camera_text() {
  return i18n::strings(configManager.getConfig().language);
}

static const char* status_log_name(const char* text) {
  if (!text || !text[0]) return "empty";
  const i18n::Strings& strings = camera_text();
  if (strcmp(text, strings.camera_frame_memory_failed) == 0) return "frame-memory-failed";
  if (strcmp(text, strings.camera_decoder_start_failed) == 0) return "decoder-start-failed";
  if (strcmp(text, strings.camera_invalid_response) == 0) return "invalid-response";
  if (strcmp(text, strings.camera_decoder_error) == 0) return "decoder-error";
  if (strcmp(text, strings.camera_ready) == 0) return "ready";
  if (strcmp(text, strings.camera_input_buffer_full) == 0) return "input-buffer-full";
  if (strcmp(text, strings.camera_connection_ended) == 0) return "connection-ended";
  if (strcmp(text, strings.camera_http_connecting) == 0) return "connecting";
  if (strcmp(text, strings.camera_url_open_failed) == 0) return "url-open-failed";
  if (strcmp(text, strings.camera_input_memory_failed) == 0) return "input-memory-failed";
  if (strcmp(text, strings.camera_buffering) == 0) return "buffering";
  if (strcmp(text, strings.camera_stream_stopped) == 0) return "stream-stopped";
  if (strcmp(text, strings.camera_empty_url) == 0) return "empty-url";
  if (strcmp(text, strings.camera_already_running) == 0) return "already-running";
  if (strcmp(text, strings.camera_task_start_failed) == 0) return "task-start-failed";
  return "dynamic";
}

static void set_image_source_without_redraw(lv_obj_t* image,
                                            const lv_image_dsc_t* source) {
  if (!image || !source) return;
  lv_display_t* display = lv_obj_get_display(image);
  const bool invalidation_was_enabled =
      display && lv_display_is_invalidation_enabled(display);
  if (invalidation_was_enabled) {
    lv_display_enable_invalidation(display, false);
  }
  lv_image_set_src(image, source);
  if (invalidation_was_enabled) {
    lv_display_enable_invalidation(display, true);
  }
}

static void set_status(const char* text, bool error = false) {
  Serial.printf("[CameraStream] Status%s: %s\n",
                error ? " ERROR" : "",
                status_log_name(text));
  portENTER_CRITICAL(&g_state_mux);
  snprintf(g_status, sizeof(g_status), "%s", text ? text : "");
  g_status_error = error;
  ++g_status_sequence;
  portEXIT_CRITICAL(&g_state_mux);
}

static bool task_is_active() {
  portENTER_CRITICAL(&g_state_mux);
  const bool active = g_task != nullptr;
  portEXIT_CRITICAL(&g_state_mux);
  return active;
}

static void release_frame_buffers() {
  uint16_t* buffers[kFrameBufferCount] = {};
  portENTER_CRITICAL(&g_state_mux);
  for (uint8_t i = 0; i < kFrameBufferCount; ++i) {
    buffers[i] = g_pixels[i];
    g_pixels[i] = nullptr;
    memset(&g_images[i], 0, sizeof(g_images[i]));
    g_frame_states[i] = FrameState::Free;
  }
  g_displayed_index = -1;
  portEXIT_CRITICAL(&g_state_mux);

  for (uint8_t i = 0; i < kFrameBufferCount; ++i) {
    free(buffers[i]);
  }
}

static bool ensure_frame_buffers() {
  for (uint8_t i = 0; i < kFrameBufferCount; ++i) {
    if (g_pixels[i]) continue;

    jpeg_decode_memory_alloc_cfg_t memory_config{};
    memory_config.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    size_t allocated_bytes = 0;
    g_pixels[i] = static_cast<uint16_t*>(
        jpeg_alloc_decoder_mem(kPixelBytes, &memory_config, &allocated_bytes));
    if (!g_pixels[i] || allocated_bytes < kPixelBytes) {
      free(g_pixels[i]);
      g_pixels[i] = nullptr;
      set_status(camera_text().camera_frame_memory_failed, true);
      release_frame_buffers();
      return false;
    }

    memset(g_pixels[i], 0, kPixelBytes);
    memset(&g_images[i], 0, sizeof(g_images[i]));
    g_images[i].header.magic = LV_IMAGE_HEADER_MAGIC;
    g_images[i].header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
    g_images[i].header.w = kWidth;
    g_images[i].header.h = kHeight;
    g_images[i].header.stride = kDecodedWidth * sizeof(uint16_t);
    g_images[i].data_size = kPixelBytes;
    g_images[i].data =
        reinterpret_cast<const uint8_t*>(g_pixels[i]);
    g_frame_states[i] = FrameState::Free;
  }
  return true;
}

static int8_t acquire_write_buffer() {
  int8_t selected = -1;
  portENTER_CRITICAL(&g_state_mux);
  for (uint8_t i = 0; i < kFrameBufferCount; ++i) {
    if (g_frame_states[i] == FrameState::Free) {
      selected = static_cast<int8_t>(i);
      break;
    }
  }
  if (selected < 0) {
    // A frame not yet shown by LVGL is safe to replace. The buffer currently
    // displayed is never written by the decoder task.
    for (uint8_t i = 0; i < kFrameBufferCount; ++i) {
      if (g_frame_states[i] == FrameState::Ready) {
        selected = static_cast<int8_t>(i);
        break;
      }
    }
  }
  if (selected >= 0) {
    g_frame_states[selected] = FrameState::Writing;
  }
  portEXIT_CRITICAL(&g_state_mux);
  return selected;
}

static void publish_write_buffer(int8_t index) {
  if (index < 0 || index >= static_cast<int8_t>(kFrameBufferCount)) return;
  portENTER_CRITICAL(&g_state_mux);
  g_frame_states[index] = FrameState::Ready;
  portEXIT_CRITICAL(&g_state_mux);
}

static void release_write_buffer(int8_t index) {
  if (index < 0 || index >= static_cast<int8_t>(kFrameBufferCount)) return;
  portENTER_CRITICAL(&g_state_mux);
  if (g_frame_states[index] == FrameState::Writing) {
    g_frame_states[index] = FrameState::Free;
  }
  portEXIT_CRITICAL(&g_state_mux);
}

static void note_presented_frame() {
  ++g_presented_frame_count;
  const uint32_t now = millis();
  if (now - g_presented_fps_ms < 2000) return;

  const float fps =
      static_cast<float>(g_presented_frame_count) * 1000.0f /
      static_cast<float>(now - g_presented_fps_ms);
  char message[64];
  snprintf(message, sizeof(message), camera_text().camera_fps_fmt, fps);
  set_status(message);
  if (g_present_timed_frames > 0) {
    Serial.printf(
        "[CameraStream] Present: %.1f FPS wait=%.1fms ppa+cache+swap=%.1fms\n",
        fps,
        static_cast<double>(g_present_wait_total_us) /
            static_cast<double>(g_present_timed_frames) / 1000.0,
        static_cast<double>(g_present_copy_total_us) /
            static_cast<double>(g_present_timed_frames) / 1000.0);
  }
  g_presented_frame_count = 0;
  g_presented_fps_ms = now;
  g_present_wait_total_us = 0;
  g_present_copy_total_us = 0;
  g_present_timed_frames = 0;
}

static bool ensure_jpeg_decoder() {
  if (g_jpeg_decoder) return true;

  jpeg_decode_engine_cfg_t engine_config{};
  engine_config.intr_priority = 0;
  engine_config.timeout_ms = 500;
  const esp_err_t result =
      jpeg_new_decoder_engine(&engine_config, &g_jpeg_decoder);
  if (result != ESP_OK || !g_jpeg_decoder) {
    g_jpeg_decoder = nullptr;
    Serial.printf("[CameraStream] JPEG engine could not start: %s\n",
                  esp_err_to_name(result));
    set_status(camera_text().camera_decoder_start_failed, true);
    return false;
  }
  Serial.println("[CameraStream] JPEG hardware decoder ready");
  return true;
}

static uint16_t rgb888_to_swapped_rgb565(uint32_t rgb) {
  const uint16_t native = static_cast<uint16_t>(
      (((rgb >> 16U) & 0xFFU) >> 3U) << 11U |
      (((rgb >> 8U) & 0xFFU) >> 2U) << 5U |
      ((rgb & 0xFFU) >> 3U));
  return static_cast<uint16_t>((native << 8U) | (native >> 8U));
}

static void apply_rounded_frame_corners(uint16_t* pixels) {
  const uint16_t kCornerRadius = ui_surface_style::radius(camera_geometry::kCornerRadius);
  if (!pixels || kCornerRadius == 0 ||
      kCornerRadius * 2U > kWidth || kCornerRadius * 2U > kHeight) {
    return;
  }

  // LVGL's generic rounded child clipping blends the entire 752x424 image.
  // Masking only these four tiny corner quadrants preserves the same solid
  // popup surface while keeping the regular cache-coherent LVGL render path.
  const int32_t diameter = static_cast<int32_t>(kCornerRadius) * 2;
  const int32_t radius_squared = diameter * diameter;
  for (uint16_t y = 0; y < kCornerRadius; ++y) {
    const int32_t dy = diameter - 1 - static_cast<int32_t>(y) * 2;
    uint16_t* top = pixels + static_cast<size_t>(y) * kDecodedWidth;
    uint16_t* bottom =
        pixels + static_cast<size_t>(kHeight - 1U - y) * kDecodedWidth;
    for (uint16_t x = 0; x < kCornerRadius; ++x) {
      const int32_t dx = diameter - 1 - static_cast<int32_t>(x) * 2;
      if ((dx * dx) + (dy * dy) <= radius_squared) continue;
      top[x] = g_corner_fill_swapped;
      top[kWidth - 1U - x] = g_corner_fill_swapped;
      bottom[x] = g_corner_fill_swapped;
      bottom[kWidth - 1U - x] = g_corner_fill_swapped;
    }
  }
}

static bool decode_jpeg_frame(const uint8_t* jpeg, size_t jpeg_bytes) {
  if (!jpeg || jpeg_bytes < 4 || jpeg_bytes > UINT32_MAX ||
      jpeg[0] != 0xFF || jpeg[1] != 0xD8 ||
      jpeg[jpeg_bytes - 2] != 0xFF || jpeg[jpeg_bytes - 1] != 0xD9) {
    Serial.printf("[CameraStream] Invalid JPEG frame: %u bytes\n",
                  static_cast<unsigned>(jpeg_bytes));
    set_status(camera_text().camera_invalid_response, true);
    return false;
  }

  jpeg_decode_picture_info_t picture_info{};
  esp_err_t result = jpeg_decoder_get_info(
      jpeg, static_cast<uint32_t>(jpeg_bytes), &picture_info);
  if (result != ESP_OK ||
      picture_info.width != kWidth || picture_info.height != kHeight) {
    Serial.printf(
        "[CameraStream] Invalid JPEG format: decode=%s size=%ux%u\n",
        esp_err_to_name(result),
        static_cast<unsigned>(picture_info.width),
        static_cast<unsigned>(picture_info.height));
    set_status(camera_text().camera_invalid_response, true);
    return false;
  }

  if (!ensure_jpeg_decoder()) return false;
  const int8_t index = acquire_write_buffer();
  if (index < 0) {
    ++g_no_write_buffer_drops;
    return true;
  }

  jpeg_decode_cfg_t decode_config{};
  decode_config.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  // RGB order produces the byte layout represented by
  // LV_COLOR_FORMAT_RGB565_SWAPPED on the little-endian P4.
  decode_config.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
  decode_config.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

  const uint32_t decode_started_ms = millis();
  uint32_t decoded_bytes = 0;
  {
    // JPEG and display PPA share the P4's 2D-DMA pool. Skipping a frame on
    // timeout is safe; decoding concurrently is not.
    Dma2dArbiterGuard dma2d_guard(500);
    if (!dma2d_guard.locked()) {
      release_write_buffer(index);
      Serial.println("[CameraStream] JPEG skipped: 2D DMA busy");
      return true;
    }
    result = jpeg_decoder_process(
        g_jpeg_decoder,
        &decode_config,
        jpeg,
        static_cast<uint32_t>(jpeg_bytes),
        reinterpret_cast<uint8_t*>(g_pixels[index]),
        static_cast<uint32_t>(kPixelBytes),
        &decoded_bytes);
    if (result != ESP_OK || decoded_bytes < kPixelBytes) {
      // Discard a potentially wedged decoder while the DMA arbiter is still
      // held. The next popup/frame creates a clean engine.
      jpeg_del_decoder_engine(g_jpeg_decoder);
      g_jpeg_decoder = nullptr;
    }
  }

  if (result != ESP_OK || decoded_bytes < kPixelBytes) {
    release_write_buffer(index);
    Serial.printf(
        "[CameraStream] JPEG decode failed: %s bytes=%u/%u\n",
        esp_err_to_name(result),
        static_cast<unsigned>(decoded_bytes),
        static_cast<unsigned>(kPixelBytes));
    set_status(camera_text().camera_decoder_error, true);
    return false;
  }

  apply_rounded_frame_corners(g_pixels[index]);
  publish_write_buffer(index);
  ++g_worker_frame_count;
  if (g_worker_frame_count == 1) {
    Serial.printf(
        "[CameraStream] First image decoded: jpeg=%u bytes decode=%ums "
        "int=%uKB largest=%uKB psram=%uKB\n",
        static_cast<unsigned>(jpeg_bytes),
        static_cast<unsigned>(millis() - decode_started_ms),
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U),
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));
    set_status(camera_text().camera_ready);
  }
  return true;
}

// Kept out of the binary for reference while the bridge/firmware transition
// is tested. The acknowledged TCP protocol below replaces HTTP chunking.
#if 0
// Receives the HomeTiles record stream after HttpChunkedBodyDecoder has
// removed HTTP/1.1 chunk framing. Each record is a big-endian uint32 length
// followed by exactly one complete JPEG frame.
class JpegRecordStream final : public Stream {
 public:
  explicit JpegRecordStream(uint8_t* input)
      : input_(input),
        last_fps_ms_(millis()) {}

  size_t write(uint8_t value) override {
    return write(&value, 1);
  }

  size_t write(const uint8_t* data, size_t len) override {
    if (!data || len == 0) return 0;
    if (g_stop_requested || failed_) return 0;
    const size_t accepted = len;

    while (len > 0 && !failed_ && !g_stop_requested) {
      if (length_bytes_ < sizeof(length_header_)) {
        const size_t take =
            std::min(len, sizeof(length_header_) - length_bytes_);
        memcpy(length_header_ + length_bytes_, data, take);
        length_bytes_ += take;
        data += take;
        len -= take;
        if (length_bytes_ < sizeof(length_header_)) continue;

        expected_bytes_ =
            (static_cast<size_t>(length_header_[0]) << 24) |
            (static_cast<size_t>(length_header_[1]) << 16) |
            (static_cast<size_t>(length_header_[2]) << 8) |
            static_cast<size_t>(length_header_[3]);
        buffered_ = 0;
        if (expected_bytes_ == 0) {
          length_bytes_ = 0;
          if (!received_flush_record_) {
            received_flush_record_ = true;
            Serial.println("[CameraStream] Bridge flush record received");
          }
          continue;
        }
        if (expected_bytes_ > kMaxJpegBytes) {
          Serial.printf(
              "[CameraStream] JPEG frame too large: %u > %u bytes\n",
              static_cast<unsigned>(expected_bytes_),
              static_cast<unsigned>(kMaxJpegBytes));
          set_status(camera_text().camera_input_buffer_full, true);
          failed_ = true;
          break;
        }
      }

      const size_t take =
          std::min(len, expected_bytes_ - buffered_);
      memcpy(input_ + buffered_, data, take);
      buffered_ += take;
      data += take;
      len -= take;
      if (buffered_ < expected_bytes_) continue;

      if (!received_first_frame_) {
        received_first_frame_ = true;
        Serial.printf(
            "[CameraStream] First complete JPEG frame: %u bytes "
            "int=%uKB largest=%uKB psram=%uKB\n",
            static_cast<unsigned>(expected_bytes_),
            static_cast<unsigned>(
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
            static_cast<unsigned>(
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) /
                1024U),
            static_cast<unsigned>(
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));
      }

      const uint32_t frames_before = g_worker_frame_count;
      if (!decode_jpeg_frame(input_, expected_bytes_)) {
        failed_ = true;
        break;
      }
      frame_count_ += g_worker_frame_count - frames_before;
      length_bytes_ = 0;
      expected_bytes_ = 0;
      buffered_ = 0;

      const uint32_t now = millis();
      if (now - last_fps_ms_ >= 2000) {
        const float fps =
            static_cast<float>(frame_count_) * 1000.0f /
            static_cast<float>(now - last_fps_ms_);
        Serial.printf("[CameraStream] Decode: %.1f FPS\n", fps);
        frame_count_ = 0;
        last_fps_ms_ = now;
      }
      taskYIELD();
    }
    return failed_ ? 0 : accepted;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  uint8_t* input_;
  uint8_t length_header_[4] = {};
  size_t length_bytes_ = 0;
  size_t expected_bytes_ = 0;
  size_t buffered_ = 0;
  uint32_t frame_count_ = 0;
  uint32_t last_fps_ms_ = 0;
  bool received_flush_record_ = false;
  bool received_first_frame_ = false;
  bool failed_ = false;
};

class HttpChunkedBodyDecoder {
 public:
  bool feed(const uint8_t* raw, size_t len, JpegRecordStream& output) {
    size_t offset = 0;
    while (offset < len && state_ != State::Done) {
      switch (state_) {
        case State::SizeLine: {
          const char ch = static_cast<char>(raw[offset++]);
          if (ch == '\n') {
            if (!parse_size_line()) return fail("invalid chunk size");
            size_line_len_ = 0;
            state_ = chunk_remaining_ == 0 ? State::Done : State::Data;
          } else if (ch != '\r') {
            if (size_line_len_ + 1 >= sizeof(size_line_)) {
              return fail("chunk size line too long");
            }
            size_line_[size_line_len_++] = ch;
          }
          break;
        }
        case State::Data: {
          const size_t take =
              std::min(chunk_remaining_, len - offset);
          if (output.write(raw + offset, take) != take) return false;
          offset += take;
          chunk_remaining_ -= take;
          if (chunk_remaining_ == 0) state_ = State::DataCr;
          break;
        }
        case State::DataCr:
          if (raw[offset++] != '\r') {
            return fail("missing CR after chunk");
          }
          state_ = State::DataLf;
          break;
        case State::DataLf:
          if (raw[offset++] != '\n') {
            return fail("missing LF after chunk");
          }
          state_ = State::SizeLine;
          break;
        case State::Done:
          break;
      }
    }
    return state_ != State::Done;
  }

 private:
  enum class State : uint8_t {
    SizeLine,
    Data,
    DataCr,
    DataLf,
    Done,
  };

  bool parse_size_line() {
    size_t index = 0;
    while (index < size_line_len_ &&
           (size_line_[index] == ' ' || size_line_[index] == '\t')) {
      ++index;
    }
    uint32_t value = 0;
    bool have_digit = false;
    for (; index < size_line_len_; ++index) {
      const char ch = size_line_[index];
      if (ch == ';' || ch == ' ' || ch == '\t') break;
      uint8_t digit = 0;
      if (ch >= '0' && ch <= '9') {
        digit = static_cast<uint8_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        digit = static_cast<uint8_t>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        digit = static_cast<uint8_t>(ch - 'A' + 10);
      } else {
        return false;
      }
      if (value > (UINT32_MAX - digit) / 16U) return false;
      value = value * 16U + digit;
      have_digit = true;
    }
    if (!have_digit) return false;
    chunk_remaining_ = value;
    return true;
  }

  bool fail(const char* reason) {
    Serial.printf("[CameraStream] HTTP chunk error: %s\n", reason);
    set_status(camera_text().camera_connection_ended, true);
    state_ = State::Done;
    return false;
  }

  State state_ = State::SizeLine;
  char size_line_[32] = {};
  size_t size_line_len_ = 0;
  size_t chunk_remaining_ = 0;
};
#endif

struct CameraEndpoint {
  char host[128] = {};
  char token[128] = {};
  uint16_t port = 0;
};

static bool parse_camera_url(const char* url, CameraEndpoint& endpoint) {
  if (!url ||
      strncmp(url, kCameraScheme, sizeof(kCameraScheme) - 1) != 0) {
    return false;
  }
  const char* authority = url + sizeof(kCameraScheme) - 1;
  const char* path = strchr(authority, '/');
  const char* authority_end = path ? path : authority + strlen(authority);
  if (authority == authority_end ||
      memchr(authority, '@', authority_end - authority)) {
    return false;
  }

  const char* host_begin = authority;
  const char* host_end = authority_end;
  const char* port_begin = nullptr;
  if (*host_begin == '[') {
    ++host_begin;
    host_end = static_cast<const char*>(
        memchr(host_begin, ']', authority_end - host_begin));
    if (!host_end) return false;
    const char* after_bracket = host_end + 1;
    if (after_bracket < authority_end) {
      if (*after_bracket != ':') return false;
      port_begin = after_bracket + 1;
    }
  } else {
    const char* colon = static_cast<const char*>(
        memchr(authority, ':', authority_end - authority));
    if (colon) {
      host_end = colon;
      port_begin = colon + 1;
    }
  }

  const size_t host_len = static_cast<size_t>(host_end - host_begin);
  if (host_len == 0 || host_len >= sizeof(endpoint.host)) return false;
  memcpy(endpoint.host, host_begin, host_len);
  endpoint.host[host_len] = '\0';

  if (port_begin) {
    if (port_begin >= authority_end) return false;
    char port_text[8] = {};
    const size_t port_len =
        static_cast<size_t>(authority_end - port_begin);
    if (port_len == 0 || port_len >= sizeof(port_text)) return false;
    memcpy(port_text, port_begin, port_len);
    char* port_end = nullptr;
    const unsigned long parsed = strtoul(port_text, &port_end, 10);
    if (!port_end || *port_end || parsed == 0 || parsed > UINT16_MAX) {
      return false;
    }
    endpoint.port = static_cast<uint16_t>(parsed);
  }

  if (!path || path[0] != '/' || path[1] == '\0') return false;
  const char* token = path + 1;
  if (strchr(token, '/') || strchr(token, '?') || strchr(token, '#') ||
      strlen(token) >= sizeof(endpoint.token)) {
    return false;
  }
  snprintf(endpoint.token, sizeof(endpoint.token), "%s", token);
  return true;
}

static int connect_camera_socket(const CameraEndpoint& endpoint) {
  char port_text[8] = {};
  snprintf(port_text, sizeof(port_text), "%u",
           static_cast<unsigned>(endpoint.port));
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  addrinfo* addresses = nullptr;
  const int lookup = getaddrinfo(endpoint.host, port_text, &hints, &addresses);
  if (lookup != 0 || !addresses) {
    Serial.printf("[CameraStream] TCP DNS failed: %d\n", lookup);
    return -1;
  }

  int connected_fd = -1;
  for (addrinfo* address = addresses; address; address = address->ai_next) {
    const int fd =
        socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (fd < 0) continue;

    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kCameraReceiveBufferBytes,
               sizeof(kCameraReceiveBufferBytes));
    const int tcp_no_delay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay,
               sizeof(tcp_no_delay));
    const int original_flags = fcntl(fd, F_GETFL, 0);
    if (original_flags < 0 ||
        fcntl(fd, F_SETFL, original_flags | O_NONBLOCK) < 0) {
      close(fd);
      continue;
    }

    int result = connect(fd, address->ai_addr, address->ai_addrlen);
    bool connected = result == 0;
    if (!connected && result < 0 && errno == EINPROGRESS) {
      fd_set writable;
      FD_ZERO(&writable);
      FD_SET(fd, &writable);
      timeval timeout{
          static_cast<time_t>(kCameraConnectTimeoutMs / 1000U),
          static_cast<suseconds_t>(
              (kCameraConnectTimeoutMs % 1000U) * 1000U)};
      const int selected =
          select(fd + 1, nullptr, &writable, nullptr, &timeout);
      if (selected > 0) {
        int socket_error = 0;
        socklen_t error_size = sizeof(socket_error);
        result = getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                            &error_size);
        connected = result == 0 && socket_error == 0;
        if (!connected && socket_error != 0) errno = socket_error;
      }
    }
    if (!connected) {
      close(fd);
      continue;
    }

    fcntl(fd, F_SETFL, original_flags & ~O_NONBLOCK);
    timeval poll_timeout{
        static_cast<time_t>(kSocketPollTimeoutMs / 1000U),
        static_cast<suseconds_t>(
            (kSocketPollTimeoutMs % 1000U) * 1000U)};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &poll_timeout,
               sizeof(poll_timeout));
    connected_fd = fd;
    break;
  }
  freeaddrinfo(addresses);
  return connected_fd;
}

static bool socket_send_all(int fd, const void* source, size_t bytes) {
  const uint8_t* data = static_cast<const uint8_t*>(source);
  while (bytes > 0 && !g_stop_requested) {
    const int sent = send(fd, data, bytes, 0);
    if (sent > 0) {
      data += sent;
      bytes -= static_cast<size_t>(sent);
      continue;
    }
    if (sent < 0 && errno == EINTR) continue;
    return false;
  }
  return bytes == 0;
}

enum class SocketReadResult : uint8_t {
  Ok,
  Closed,
  Error,
  Stopped,
};

static SocketReadResult socket_receive_exact(int fd,
                                             void* destination,
                                             size_t bytes) {
  uint8_t* output = static_cast<uint8_t*>(destination);
  size_t received_bytes = 0;
  while (received_bytes < bytes) {
    if (g_stop_requested) return SocketReadResult::Stopped;
    const int received =
        recv(fd, output + received_bytes, bytes - received_bytes, 0);
    if (received > 0) {
      received_bytes += static_cast<size_t>(received);
      continue;
    }
    if (received == 0) return SocketReadResult::Closed;
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      taskYIELD();
      continue;
    }
    return SocketReadResult::Error;
  }
  return SocketReadResult::Ok;
}

static uint16_t read_be16(const uint8_t* data) {
  return static_cast<uint16_t>(
      (static_cast<uint16_t>(data[0]) << 8) |
      static_cast<uint16_t>(data[1]));
}

static uint32_t read_be32(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

static void write_be32(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value >> 24);
  data[1] = static_cast<uint8_t>(value >> 16);
  data[2] = static_cast<uint8_t>(value >> 8);
  data[3] = static_cast<uint8_t>(value);
}

#if 0
static size_t find_http_header_end(const char* data, size_t bytes) {
  if (!data || bytes < 4) return SIZE_MAX;
  for (size_t i = 0; i + 3 < bytes; ++i) {
    if (data[i] == '\r' && data[i + 1] == '\n' &&
        data[i + 2] == '\r' && data[i + 3] == '\n') {
      return i;
    }
  }
  return SIZE_MAX;
}

static bool read_http_header_value(const char* header,
                                   const char* name,
                                   char* output,
                                   size_t output_bytes) {
  if (!header || !name || !output || output_bytes == 0) return false;
  const size_t name_len = strlen(name);
  const char* line = strstr(header, "\r\n");
  if (!line) return false;
  line += 2;
  while (*line) {
    const char* line_end = strstr(line, "\r\n");
    if (!line_end) line_end = line + strlen(line);
    const char* colon = static_cast<const char*>(
        memchr(line, ':', static_cast<size_t>(line_end - line)));
    if (colon && static_cast<size_t>(colon - line) == name_len &&
        strncasecmp(line, name, name_len) == 0) {
      const char* value = colon + 1;
      while (value < line_end && (*value == ' ' || *value == '\t')) ++value;
      while (line_end > value &&
             (line_end[-1] == ' ' || line_end[-1] == '\t')) {
        --line_end;
      }
      const size_t value_len = static_cast<size_t>(line_end - value);
      if (value_len >= output_bytes) return false;
      memcpy(output, value, value_len);
      output[value_len] = '\0';
      return true;
    }
    if (!*line_end) break;
    line = line_end + 2;
  }
  return false;
}
#endif

static void finish_camera_task(int socket_fd, uint8_t* input) {
  Serial.printf(
      "[CameraStream] Task end: stop=%s frames=%u int=%uKB "
      "largest=%uKB psram=%uKB\n",
      g_stop_requested ? "yes" : "no",
      static_cast<unsigned>(g_worker_frame_count),
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
      static_cast<unsigned>(
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U),
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));
  free(input);
  if (socket_fd >= 0) {
    shutdown(socket_fd, SHUT_RDWR);
    close(socket_fd);
  }
}

#if 0
static void run_camera_task_http_legacy() {
  set_status(camera_text().camera_http_connecting);
  const String url = g_url;
  HttpEndpoint endpoint;
  if (!parse_http_url(url.c_str(), endpoint)) {
    Serial.println(
        "[CameraStream] Rejected: camera transport must use local HTTP");
    set_status(camera_text().camera_invalid_response, true);
    return;
  }

  Serial.printf(
      "[CameraStream] Start: transport=bounded-tcp-http url_len=%u "
      "int=%uKB largest=%uKB psram=%uKB\n",
      static_cast<unsigned>(url.length()),
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
      static_cast<unsigned>(
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U),
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));
  const uint32_t get_started_ms = millis();
  int socket_fd = connect_http_socket(endpoint);
  if (socket_fd < 0) {
    Serial.printf("[CameraStream] HTTP connection failed: errno=%d\n",
                  errno);
    set_status(camera_text().camera_url_open_failed, true);
    return;
  }

  char request[512] = {};
  const int request_bytes = snprintf(
      request, sizeof(request),
      "GET %s HTTP/1.1\r\nHost: %s:%u\r\n"
      "Accept: application/x-hometiles-jpeg-stream\r\n"
      "Connection: close\r\nUser-Agent: HomeTiles-P4/1\r\n\r\n",
      endpoint.path, endpoint.host, static_cast<unsigned>(endpoint.port));
  if (request_bytes <= 0 ||
      static_cast<size_t>(request_bytes) >= sizeof(request) ||
      !socket_send_all(socket_fd, request,
                       static_cast<size_t>(request_bytes))) {
    Serial.printf("[CameraStream] HTTP GET send failed: errno=%d\n",
                  errno);
    set_status(camera_text().camera_url_open_failed, true);
    finish_camera_task(socket_fd, nullptr);
    return;
  }

  char header[kHttpHeaderBytes] = {};
  size_t header_bytes = 0;
  size_t header_marker = SIZE_MAX;
  uint8_t raw[2048];
  while (!g_stop_requested &&
         static_cast<uint32_t>(millis() - get_started_ms) <
             kHttpHeaderTimeoutMs) {
    const int received = recv(socket_fd, raw, sizeof(raw), 0);
    if (received > 0) {
      if (header_bytes + static_cast<size_t>(received) >
          sizeof(header) - 1) {
        break;
      }
      memcpy(header + header_bytes, raw, static_cast<size_t>(received));
      header_bytes += static_cast<size_t>(received);
      header_marker = find_http_header_end(header, header_bytes);
      if (header_marker != SIZE_MAX) break;
      continue;
    }
    if (received == 0) break;
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
    break;
  }
  if (header_marker == SIZE_MAX) {
    Serial.printf("[CameraStream] HTTP header incomplete: %u bytes\n",
                  static_cast<unsigned>(header_bytes));
    set_status(camera_text().camera_url_open_failed, true);
    finish_camera_task(socket_fd, nullptr);
    return;
  }

  header[header_marker] = '\0';
  int code = 0;
  if (sscanf(header, "HTTP/%*u.%*u %d", &code) != 1) code = -1;
  Serial.printf("[CameraStream] HTTP GET=%d after %ums\n", code,
                static_cast<unsigned>(millis() - get_started_ms));
  if (code != 200) {
    char message[64];
    snprintf(message, sizeof(message), camera_text().camera_http_error_fmt,
             code);
    set_status(message, true);
    finish_camera_task(socket_fd, nullptr);
    return;
  }

  char transfer_encoding[32] = {};
  char framing[32] = {};
  read_http_header_value(header, "Transfer-Encoding", transfer_encoding,
                         sizeof(transfer_encoding));
  read_http_header_value(header, "X-HomeTiles-Framing", framing,
                         sizeof(framing));
  const bool chunked = strcasecmp(transfer_encoding, "chunked") == 0;
  // lwIP treats SO_RCVBUF only as a socket hint for TCP; the SDK-wide TCP
  // window remains independent. ESP-Hosted transport blocks therefore live
  // in DMA-capable PSRAM instead of relying on this value as a RAM limit.
  Serial.printf("[CameraStream] HTTP RX buffer hint: %d bytes\n",
                kHttpReceiveBufferBytes);
  Serial.printf("[CameraStream] HTTP transfer: %s\n",
                chunked ? "chunked" : "identity");
  Serial.printf("[CameraStream] JPEG framing: %s\n", framing);
  if (strcasecmp(framing, kJpegFraming) != 0) {
    Serial.println("[CameraStream] Bridge did not provide JPEG framing");
    set_status(camera_text().camera_invalid_response, true);
    finish_camera_task(socket_fd, nullptr);
    return;
  }

  uint8_t* input = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      64, kMaxJpegBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!input) {
    Serial.printf("[CameraStream] JPEG input buffer (%u bytes) missing\n",
                  static_cast<unsigned>(kMaxJpegBytes));
    set_status(camera_text().camera_input_memory_failed, true);
    finish_camera_task(socket_fd, nullptr);
    return;
  }

  set_status(camera_text().camera_buffering);
  JpegRecordStream jpeg_stream(input);
  HttpChunkedBodyDecoder chunk_decoder;
  bool stream_ok = true;
  uint32_t low_dma_headroom_since_ms = 0;
  const size_t body_offset = header_marker + 4;
  if (body_offset < header_bytes) {
    const uint8_t* body =
        reinterpret_cast<const uint8_t*>(header) + body_offset;
    const size_t body_bytes = header_bytes - body_offset;
    stream_ok = chunked
        ? chunk_decoder.feed(body, body_bytes, jpeg_stream)
        : jpeg_stream.write(body, body_bytes) == body_bytes;
  }
  while (!g_stop_requested && stream_ok) {
    const size_t dma_free = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    const size_t mqtt_dma_reserve = networkManager.mqttDmaReserveBytes();
    const size_t dma_headroom = dma_free + mqtt_dma_reserve;
    if (dma_headroom < kMinCameraDmaHeadroomBytes) {
      const uint32_t now = millis();
      if (low_dma_headroom_since_ms == 0) {
        low_dma_headroom_since_ms = now ? now : 1;
      } else if (
          static_cast<uint32_t>(now - low_dma_headroom_since_ms) >=
          kDmaHeadroomGraceMs) {
        Serial.printf(
            "[CameraStream] Safety stop: DMA free=%u KB reserve=%u KB "
            "headroom=%u KB for %u ms; MQTT/Wi-Fi remain active\n",
            static_cast<unsigned>(dma_free / 1024U),
            static_cast<unsigned>(mqtt_dma_reserve / 1024U),
            static_cast<unsigned>(dma_headroom / 1024U),
            static_cast<unsigned>(now - low_dma_headroom_since_ms));
        set_status(camera_text().camera_connection_ended, true);
        stream_ok = false;
        break;
      }
    } else {
      low_dma_headroom_since_ms = 0;
    }
    const int received = recv(socket_fd, raw, sizeof(raw), 0);
    if (received == 0) break;
    if (received < 0) {
      if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
        stream_ok = false;
        Serial.printf("[CameraStream] TCP receive error: errno=%d\n", errno);
        break;
      }
      taskYIELD();
      continue;
    }
    if (chunked) {
      if (!chunk_decoder.feed(raw, static_cast<size_t>(received),
                              jpeg_stream)) {
        stream_ok = false;
        break;
      }
    } else if (jpeg_stream.write(
                   raw, static_cast<size_t>(received)) !=
               static_cast<size_t>(received)) {
      stream_ok = false;
      break;
    }
  }
  Serial.printf("[CameraStream] HTTP stream ended: ok=%s\n",
                stream_ok ? "yes" : "no");

  if (g_stop_requested) {
    set_status(camera_text().camera_stream_stopped);
  } else {
    bool had_error = false;
    portENTER_CRITICAL(&g_state_mux);
    had_error = g_status_error;
    portEXIT_CRITICAL(&g_state_mux);
    if (!had_error) set_status(camera_text().camera_connection_ended, true);
  }
  finish_camera_task(socket_fd, input);
}
#endif

static void run_camera_task() {
  set_status(camera_text().camera_http_connecting);
  const String url = g_url;
  CameraEndpoint endpoint;
  if (!parse_camera_url(url.c_str(), endpoint)) {
    Serial.println(
        "[CameraStream] Rejected: camera transport must use tcp-ack-v1");
    set_status(camera_text().camera_invalid_response, true);
    return;
  }

  Serial.printf(
      "[CameraStream] Start: transport=tcp-ack-v1 url_len=%u "
      "int=%uKB largest=%uKB psram=%uKB\n",
      static_cast<unsigned>(url.length()),
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
      static_cast<unsigned>(
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U),
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));

  const uint32_t connect_started_ms = millis();
  int socket_fd = connect_camera_socket(endpoint);
  if (socket_fd < 0) {
    Serial.printf("[CameraStream] TCP connection failed: errno=%d\n",
                  errno);
    set_status(camera_text().camera_url_open_failed, true);
    return;
  }

  char request[192] = {};
  const int request_bytes = snprintf(
      request, sizeof(request), "%s%s\n",
      kCameraRequestPrefix, endpoint.token);
  if (request_bytes <= 0 ||
      static_cast<size_t>(request_bytes) >= sizeof(request) ||
      !socket_send_all(socket_fd, request,
                       static_cast<size_t>(request_bytes))) {
    Serial.printf("[CameraStream] TCP handshake failed: errno=%d\n",
                  errno);
    set_status(camera_text().camera_url_open_failed, true);
    finish_camera_task(socket_fd, nullptr);
    return;
  }

  uint8_t hello[kCameraHelloBytes] = {};
  const SocketReadResult hello_result =
      socket_receive_exact(socket_fd, hello, sizeof(hello));
  const uint16_t hello_status = read_be16(hello + 4);
  const uint16_t chunk_bytes = read_be16(hello + 6);
  if (hello_result != SocketReadResult::Ok ||
      memcmp(hello, kCameraHelloMagic, sizeof(kCameraHelloMagic)) != 0 ||
      hello_status != 0 ||
      chunk_bytes == 0 ||
      chunk_bytes > kCameraChunkBytes) {
    Serial.printf(
        "[CameraStream] Invalid TCP handshake: result=%u status=%u "
        "chunk=%u\n",
        static_cast<unsigned>(hello_result),
        static_cast<unsigned>(hello_status),
        static_cast<unsigned>(chunk_bytes));
    set_status(camera_text().camera_invalid_response, true);
    finish_camera_task(socket_fd, nullptr);
    return;
  }
  Serial.printf(
      "[CameraStream] TCP ACK connected after %ums, block=%u bytes\n",
      static_cast<unsigned>(millis() - connect_started_ms),
      static_cast<unsigned>(chunk_bytes));

  uint8_t* input = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      64, kMaxJpegBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!input) {
    Serial.printf("[CameraStream] JPEG input buffer (%u bytes) missing\n",
                  static_cast<unsigned>(kMaxJpegBytes));
    set_status(camera_text().camera_input_memory_failed, true);
    finish_camera_task(socket_fd, nullptr);
    return;
  }

  set_status(camera_text().camera_buffering);
  bool stream_ok = true;
  bool received_first_frame = false;
  uint32_t frame_count = 0;
  uint32_t last_fps_ms = millis();
  uint32_t low_dma_headroom_since_ms = 0;

  auto dma_headroom_available = [&]() -> bool {
    const size_t dma_free = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    const size_t mqtt_dma_reserve = networkManager.mqttDmaReserveBytes();
    const size_t dma_headroom = dma_free + mqtt_dma_reserve;
    if (dma_headroom >= kMinCameraDmaHeadroomBytes) {
      low_dma_headroom_since_ms = 0;
      return true;
    }

    const uint32_t now = millis();
    if (low_dma_headroom_since_ms == 0) {
      low_dma_headroom_since_ms = now ? now : 1;
      return true;
    }
    if (static_cast<uint32_t>(now - low_dma_headroom_since_ms) <
        kDmaHeadroomGraceMs) {
      return true;
    }

    Serial.printf(
        "[CameraStream] Safety stop: DMA free=%u KB reserve=%u KB "
        "headroom=%u KB for %u ms; MQTT/Wi-Fi remain active\n",
        static_cast<unsigned>(dma_free / 1024U),
        static_cast<unsigned>(mqtt_dma_reserve / 1024U),
        static_cast<unsigned>(dma_headroom / 1024U),
        static_cast<unsigned>(now - low_dma_headroom_since_ms));
    set_status(camera_text().camera_connection_ended, true);
    return false;
  };

  while (!g_stop_requested && stream_ok) {
    if (!dma_headroom_available()) {
      stream_ok = false;
      break;
    }

    uint8_t header[kCameraFrameHeaderBytes] = {};
    const SocketReadResult header_result =
        socket_receive_exact(socket_fd, header, sizeof(header));
    if (header_result != SocketReadResult::Ok) {
      if (header_result == SocketReadResult::Error) {
        Serial.printf("[CameraStream] TCP receive error: errno=%d\n", errno);
      }
      stream_ok = header_result == SocketReadResult::Stopped;
      break;
    }
    if (memcmp(header, kCameraFrameMagic,
               sizeof(kCameraFrameMagic)) != 0) {
      Serial.println("[CameraStream] Invalid TCP message identifier");
      set_status(camera_text().camera_invalid_response, true);
      stream_ok = false;
      break;
    }

    const uint8_t message_type = header[4];
    const uint32_t sequence = read_be32(header + 8);
    const uint32_t jpeg_bytes = read_be32(header + 12);
    if (message_type == kCameraMessageFlush) {
      if (jpeg_bytes != 0) {
        set_status(camera_text().camera_invalid_response, true);
        stream_ok = false;
        break;
      }
      Serial.println("[CameraStream] Bridge flush message received");
      continue;
    }
    if (message_type == kCameraMessageEnd) {
      break;
    }
    if (message_type != kCameraMessageFrame ||
        jpeg_bytes == 0 ||
        jpeg_bytes > kMaxJpegBytes) {
      Serial.printf(
          "[CameraStream] Invalid TCP image header: type=%u bytes=%u\n",
          static_cast<unsigned>(message_type),
          static_cast<unsigned>(jpeg_bytes));
      set_status(
          jpeg_bytes > kMaxJpegBytes
              ? camera_text().camera_input_buffer_full
              : camera_text().camera_invalid_response,
          true);
      stream_ok = false;
      break;
    }

    size_t received_bytes = 0;
    while (received_bytes < jpeg_bytes &&
           !g_stop_requested && stream_ok) {
      if (!dma_headroom_available()) {
        stream_ok = false;
        break;
      }
      const size_t receive_now = std::min(
          static_cast<size_t>(chunk_bytes),
          static_cast<size_t>(jpeg_bytes) - received_bytes);
      const SocketReadResult chunk_result = socket_receive_exact(
          socket_fd, input + received_bytes, receive_now);
      if (chunk_result != SocketReadResult::Ok) {
        if (chunk_result == SocketReadResult::Error) {
          Serial.printf(
              "[CameraStream] TCP block error: errno=%d offset=%u\n",
              errno, static_cast<unsigned>(received_bytes));
        }
        stream_ok = false;
        break;
      }
      received_bytes += receive_now;

      uint8_t ack[kCameraAckBytes] = {};
      memcpy(ack, kCameraAckMagic, sizeof(kCameraAckMagic));
      write_be32(ack + 4, sequence);
      write_be32(ack + 8, static_cast<uint32_t>(received_bytes));
      if (!socket_send_all(socket_fd, ack, sizeof(ack))) {
        Serial.printf("[CameraStream] TCP ACK send failed: errno=%d\n",
                      errno);
        stream_ok = false;
        break;
      }

      // Camera and MQTT share the second P4 core. After every acknowledged
      // 8 KB block, give the equal-priority MQTT worker a scheduling point
      // so its socket stays active even under continuous camera traffic.
      taskYIELD();
    }
    if (!stream_ok || g_stop_requested) break;

    if (!received_first_frame) {
      received_first_frame = true;
      Serial.printf(
          "[CameraStream] First acknowledged JPEG frame: %u bytes "
          "int=%uKB largest=%uKB psram=%uKB\n",
          static_cast<unsigned>(jpeg_bytes),
          static_cast<unsigned>(
              heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
          static_cast<unsigned>(
              heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) /
              1024U),
          static_cast<unsigned>(
              heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));
    }

    const uint32_t frames_before = g_worker_frame_count;
    if (!decode_jpeg_frame(input, jpeg_bytes)) {
      stream_ok = false;
      break;
    }
    frame_count += g_worker_frame_count - frames_before;
    const uint32_t now = millis();
    if (now - last_fps_ms >= 2000) {
      const float fps =
          static_cast<float>(frame_count) * 1000.0f /
          static_cast<float>(now - last_fps_ms);
      Serial.printf(
          "[CameraStream] Decode: %.1f FPS (buffer_drops=%u)\n",
          fps,
          static_cast<unsigned>(g_no_write_buffer_drops));
      frame_count = 0;
      g_no_write_buffer_drops = 0;
      last_fps_ms = now;
    }
    // Block for at least one tick. taskYIELD() alone never schedules lower
    // priority tasks and starved MQTT keepalive during a long 24 FPS camera test.
    vTaskDelay(1);
  }

  Serial.printf("[CameraStream] TCP ACK stream ended: ok=%s\n",
                stream_ok ? "yes" : "no");
  if (g_stop_requested) {
    set_status(camera_text().camera_stream_stopped);
  } else {
    bool had_error = false;
    portENTER_CRITICAL(&g_state_mux);
    had_error = g_status_error;
    portEXIT_CRITICAL(&g_state_mux);
    if (!had_error) set_status(camera_text().camera_connection_ended, true);
  }
  finish_camera_task(socket_fd, input);
}

static void camera_task(void*) {
  // Keep all worker-owned objects inside this scope so cleanup completes
  // before FreeRTOS releases the PSRAM-backed task stack.
  run_camera_task();

  bool release_buffers = false;
  portENTER_CRITICAL(&g_state_mux);
  release_buffers = g_release_buffers;
  // Keep the task visible as active until requested buffer cleanup finishes.
  // Otherwise a reopen can allocate/reuse the globals while this old task is
  // still about to free them. If no cleanup was requested, clearing the task
  // under this lock lets a concurrent stop observe the inactive state and do
  // the release itself.
  if (!release_buffers) g_task = nullptr;
  portEXIT_CRITICAL(&g_state_mux);
  if (release_buffers) {
    release_frame_buffers();
    portENTER_CRITICAL(&g_state_mux);
    g_task = nullptr;
    portEXIT_CRITICAL(&g_state_mux);
  }
  vTaskDelete(nullptr);
}

}  // namespace

bool camera_stream_start(const char* url, uint32_t corner_rgb) {
  if (!url || !*url) {
    set_status(camera_text().camera_empty_url, true);
    return false;
  }
  if (task_is_active()) {
    set_status(camera_text().camera_already_running, true);
    return false;
  }
  if (!ensure_frame_buffers()) return false;
  Serial.printf(
      "[CameraStream] JPEG image buffers ready: %u x %u bytes, int=%uKB "
      "largest=%uKB psram=%uKB\n",
      static_cast<unsigned>(kFrameBufferCount),
      static_cast<unsigned>(kPixelBytes),
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U),
      static_cast<unsigned>(
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U),
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));

  portENTER_CRITICAL(&g_state_mux);
  for (uint8_t i = 0; i < kFrameBufferCount; ++i) {
    g_frame_states[i] =
        (static_cast<int8_t>(i) == g_displayed_index)
            ? FrameState::Displayed
            : FrameState::Free;
  }
  g_release_buffers = false;
  portEXIT_CRITICAL(&g_state_mux);

  g_url = url;
  g_corner_fill_swapped = rgb888_to_swapped_rgb565(corner_rgb);
  g_stop_requested = false;
  g_worker_frame_count = 0;
  g_no_write_buffer_drops = 0;
  g_presented_frame_count = 0;
  g_presented_fps_ms = millis();
  g_present_wait_total_us = 0;
  g_present_copy_total_us = 0;
  g_present_timed_frames = 0;
  g_direct_preview_logged = false;
  g_direct_fallback_logged = false;
  const BaseType_t task_core = (ARDUINO_RUNNING_CORE == 0) ? 1 : 0;
  // Never outrank the MQTT worker on this core. Both tasks run at idle
  // priority; explicit yields within each frame share CPU time without
  // slowing the JPEG/PPA path.
  constexpr UBaseType_t kCameraTaskPriority = tskIDLE_PRIORITY;
  if (xTaskCreatePinnedToCoreWithCaps(
          camera_task, "cameraJpeg", 16384, nullptr, kCameraTaskPriority,
          &g_task, task_core, MALLOC_CAP_SPIRAM) != pdPASS) {
    portENTER_CRITICAL(&g_state_mux);
    g_task = nullptr;
    g_release_buffers = true;
    portEXIT_CRITICAL(&g_state_mux);
    release_frame_buffers();
    set_status(camera_text().camera_task_start_failed, true);
    return false;
  }
  Serial.printf("[CameraStream] Task started: core=%d priority=%u\n",
                static_cast<int>(task_core),
                static_cast<unsigned>(kCameraTaskPriority));
  return true;
}

void camera_stream_stop() {
  const bool was_active = task_is_active();
  g_stop_requested = true;
  Device::displayEndFullFramePreview();
  Serial.printf("[CameraStream] Stop requested (active=%s)\n",
                was_active ? "yes" : "no");
  bool active = false;
  portENTER_CRITICAL(&g_state_mux);
  g_release_buffers = true;
  active = g_task != nullptr;
  portEXIT_CRITICAL(&g_state_mux);
  if (!active) release_frame_buffers();
}

void camera_stream_process_ui(lv_obj_t* image,
                              lv_obj_t* placeholder,
                              lv_obj_t* status_label) {
  if (!image) return;
  int8_t ready = -1;
  bool direct_preview = false;
  bool can_try_direct_preview = false;
  bool drop_unpresented_frame = false;
  // If the display PPA has entered its short self-healing cooldown, keep the
  // last frame. Repainting the full video area during recovery would make
  // every other LVGL interaction sluggish and fight the recovery.
  if (!Device::ppaCooldownActive()) {
    portENTER_CRITICAL(&g_state_mux);
    for (uint8_t i = 0; i < kFrameBufferCount; ++i) {
      if (g_frame_states[i] == FrameState::Ready) {
        ready = static_cast<int8_t>(i);
      }
    }
    if (ready >= 0) {
      for (uint8_t i = 0; i < kFrameBufferCount; ++i) {
        if (static_cast<int8_t>(i) != ready &&
            g_frame_states[i] == FrameState::Ready) {
          g_frame_states[i] = FrameState::Free;
        }
      }
      // Claim the source before dropping the lock. Otherwise the decoder may
      // select this Ready buffer for its next frame while PPA is still reading
      // it, which previously produced intermittent square corruption.
      g_frame_states[ready] = FrameState::Presenting;
      can_try_direct_preview = true;
    }
    portEXIT_CRITICAL(&g_state_mux);
  }

  if (ready >= 0 && can_try_direct_preview) {
    lv_area_t area{};
    lv_obj_get_coords(image, &area);
    const uint32_t wait_started_us = micros();
    Device::displayWaitFrameStart();
    const uint32_t copy_started_us = micros();
    direct_preview = Device::displayTryFullFramePreview(
        area.x1, area.y1, kWidth, kHeight, kDecodedWidth,
        g_pixels[ready], kPixelBytes,
        true);  // JPEG output is RGB565 byte-swapped for LVGL.
    if (direct_preview) {
      const uint32_t copy_finished_us = micros();
      g_present_wait_total_us += copy_started_us - wait_started_us;
      g_present_copy_total_us += copy_finished_us - copy_started_us;
      ++g_present_timed_frames;
    }
    if (direct_preview && !g_direct_preview_logged) {
      g_direct_preview_logged = true;
      Serial.println(
          "[CameraStream] Presentation path: direct device output "
          "(source buffer and LVGL synchronized)");
    } else if (!direct_preview && !g_direct_fallback_logged) {
      g_direct_fallback_logged = true;
      Serial.println(
          "[CameraStream] Direct device output unavailable; "
          "frame skipped to keep LVGL responsive");
    }
    drop_unpresented_frame = !direct_preview;
  }

  // Direct PPA bypasses LVGL, but LVGL can still repaint the popup after a
  // Bridge/UI update. Point it at this exact presented frame without scheduling
  // another expensive image redraw. The buffer then stays Displayed/protected
  // until the next successful presentation, so an incidental LVGL repaint can
  // never copy pieces of the old first frame over the live camera picture.
  if (ready >= 0 && direct_preview) {
    set_image_source_without_redraw(image, &g_images[ready]);
  }

  if (direct_preview) {
    lv_obj_clear_flag(image, LV_OBJ_FLAG_HIDDEN);
    if (placeholder) lv_obj_add_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
  }

  if (ready >= 0) {
    portENTER_CRITICAL(&g_state_mux);
    if (drop_unpresented_frame) {
      g_frame_states[ready] = FrameState::Free;
    } else {
      if (g_displayed_index >= 0 &&
          g_displayed_index < static_cast<int8_t>(kFrameBufferCount) &&
          g_frame_states[g_displayed_index] == FrameState::Displayed) {
        g_frame_states[g_displayed_index] = FrameState::Free;
      }
      g_frame_states[ready] = FrameState::Displayed;
      g_displayed_index = ready;
    }
    portEXIT_CRITICAL(&g_state_mux);
  }

  if (ready >= 0 && !drop_unpresented_frame) note_presented_frame();

  char status[sizeof(g_status)] = "";
  bool status_error = false;
  bool status_changed = false;
  portENTER_CRITICAL(&g_state_mux);
  if (g_status_sequence != g_ui_status_sequence) {
    snprintf(status, sizeof(status), "%s", g_status);
    status_error = g_status_error;
    g_ui_status_sequence = g_status_sequence;
    status_changed = true;
  }
  portEXIT_CRITICAL(&g_state_mux);
  if (status_changed && status_label) {
    lv_label_set_text(status_label, status);
    lv_obj_set_style_text_color(
        status_label,
        status_error ? lv_color_hex(0xFF6B6B) : lv_color_hex(0xD8DEE9), 0);
  }
}

void camera_stream_set_external_status(const char* text, bool error) {
  set_status(text, error);
}

bool camera_stream_is_active() {
  return task_is_active();
}

#else

bool camera_stream_start(const char*, uint32_t) {
  camera_stream_set_external_status(
      i18n::strings(configManager.getConfig().language).camera_device_only,
      true);
  return false;
}

void camera_stream_stop() {}
void camera_stream_process_ui(lv_obj_t*, lv_obj_t*, lv_obj_t*) {}
void camera_stream_set_external_status(const char*, bool) {}
bool camera_stream_is_active() { return false; }

#endif

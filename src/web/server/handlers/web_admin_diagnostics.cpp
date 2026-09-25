#include "src/web/server/web_admin.h"
#include "src/web/server/web_admin_utils.h"
#include "src/core/diagnostics/crash_log.h"
#include "src/core/firmware/firmware_version.h"
#include "src/devices/guition_esp32_4848s040/s3_diagnostics.h"
#include <LittleFS.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <esp_vfs_fat.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <lvgl.h>
#include <algorithm>
#include <cerrno>
#include <stdlib.h>
#include <string.h>
#if defined(DEVICE_GUITION_JC1060P470C)
#include "src/devices/guition_jc1060p470c/vendor/guition_sdmmc.h"
#elif defined(DEVICE_GUITION_JC1060P470C_V2)
#include "src/devices/guition_jc1060p470c_v2/vendor/guition_sdmmc.h"
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include <img_converters.h>
#else
#include <driver/jpeg_encode.h>
#include "src/core/display/dma2d_arbiter.h"
#endif
#include "src/web/server/handlers/web_admin_handler_utils.h"

using namespace web_admin_handlers;

namespace {

constexpr const char* kScreenshotPath = "/ui_screenshot.jpg";
constexpr const char* kLegacyScreenshotPath = "/ui_screenshot.bmp";
constexpr uint32_t kScreenshotJpegQuality = 92;
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
// Other 2D-DMA users hold the arbiter for at most one frame or decode, so
// waiting this long lets a requested screenshot finish instead of failing.
constexpr uint32_t kScreenshotArbiterTimeoutMs = 2000;
#endif

bool saveDrawBufferAsJpeg(const lv_draw_buf_t* draw_buf, const String& path, String& error) {
  if (!draw_buf || !draw_buf->data) {
    error = "Screenshot buffer missing";
    return false;
  }
  if (draw_buf->header.cf != LV_COLOR_FORMAT_RGB565) {
    error = "Unsupported screenshot color format";
    return false;
  }

  const int32_t width = draw_buf->header.w;
  const int32_t height = draw_buf->header.h;
  const uint32_t src_stride = draw_buf->header.stride;
  if (width <= 0 || height <= 0) {
    error = "Invalid screenshot size";
    return false;
  }
  const size_t row_bytes = static_cast<size_t>(width) * sizeof(uint16_t);
  const size_t raw_size = row_bytes * static_cast<size_t>(height);
  if (src_stride < row_bytes || raw_size > UINT32_MAX) {
    error = "Invalid screenshot size";
    return false;
  }

  size_t input_capacity = raw_size;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  uint8_t* input = static_cast<uint8_t*>(
      heap_caps_malloc(raw_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  jpeg_encode_memory_alloc_cfg_t input_mem_cfg = {};
  input_mem_cfg.buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER;
  input_capacity = 0;
  uint8_t* input = static_cast<uint8_t*>(
      jpeg_alloc_encoder_mem(raw_size, &input_mem_cfg, &input_capacity));
#endif
  if (!input || input_capacity < raw_size) {
    if (input) free(input);
    error = "JPEG input buffer allocation failed";
    return false;
  }
  for (int32_t y = 0; y < height; ++y) {
    memcpy(input + static_cast<size_t>(y) * row_bytes,
           draw_buf->data + static_cast<size_t>(y) * src_stride,
           row_bytes);
  }

  const uint32_t started_ms = millis();
  uint8_t* output = nullptr;
  size_t jpeg_size = 0;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  // ESP32-S3 has no hardware JPEG encoder. The Apache-2.0 esp32-camera
  // converter included with the Arduino core provides the software fallback.
  // fmt2jpg interprets RGB565 as big-endian by default, while LVGL's native
  // RGB565 snapshot buffer on this little-endian target stores the low byte
  // first. Tell the encoder the actual layout before converting.
  jpgSetRgb565BE(false);
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
  Serial.printf(
      "[S3Diag/Screenshot] input=%ldx%ld cf=RGB565 stride=%lu "
      "rgb565_be=0 bytes=%u\n",
      static_cast<long>(width), static_cast<long>(height),
      static_cast<unsigned long>(src_stride),
      static_cast<unsigned>(raw_size));
#endif
  // Its quality scale is 0..63 (lower is better); 7 is close to the P4's 92%.
  if (!fmt2jpg(input, raw_size, static_cast<uint16_t>(width),
               static_cast<uint16_t>(height), PIXFORMAT_RGB565, 7, &output,
               &jpeg_size)) {
    free(input);
    error = "JPEG software encoding failed";
    return false;
  }
#else
  jpeg_encode_memory_alloc_cfg_t output_mem_cfg = {};
  output_mem_cfg.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER;
  size_t output_capacity = 0;
  output = static_cast<uint8_t*>(
      jpeg_alloc_encoder_mem(raw_size, &output_mem_cfg, &output_capacity));
  if (!output || output_capacity == 0) {
    free(input);
    if (output) free(output);
    error = "JPEG output buffer allocation failed";
    return false;
  }

  esp_err_t result = ESP_FAIL;
  {
    // The JPEG engine shares the 2D-DMA pool with display PPA rotation and
    // the camera, cover and screensaver decoders; never overlap them. Keep
    // the engine lifecycle and encoding under one lock, as the decoders do.
    Dma2dArbiterGuard dma2d_guard(kScreenshotArbiterTimeoutMs);
    if (!dma2d_guard.locked()) {
      free(output);
      free(input);
      Serial.println("[Screenshot] 2D DMA arbiter timeout, JPEG not encoded");
      error = "JPEG encoder busy (2D DMA arbiter timeout)";
      return false;
    }

    jpeg_encode_engine_cfg_t engine_cfg = {};
    engine_cfg.timeout_ms = 1000;
    jpeg_encoder_handle_t encoder = nullptr;
    result = jpeg_new_encoder_engine(&engine_cfg, &encoder);
    if (result != ESP_OK) {
      free(output);
      free(input);
      error = String("Could not start JPEG encoder: ") + esp_err_to_name(result);
      return false;
    }

    jpeg_encode_cfg_t encode_cfg = {};
    encode_cfg.height = static_cast<uint32_t>(height);
    encode_cfg.width = static_cast<uint32_t>(width);
    encode_cfg.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
    encode_cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV444;
    encode_cfg.image_quality = kScreenshotJpegQuality;

    uint32_t hardware_jpeg_size = 0;
    result = jpeg_encoder_process(
        encoder, &encode_cfg, input, static_cast<uint32_t>(raw_size), output,
        static_cast<uint32_t>(output_capacity), &hardware_jpeg_size);
    jpeg_size = hardware_jpeg_size;
    jpeg_del_encoder_engine(encoder);
  }

  if (result != ESP_OK) {
    free(input);
    free(output);
    error = String("JPEG encoding failed: ") + esp_err_to_name(result);
    return false;
  }
  if (jpeg_size == 0 || jpeg_size > output_capacity) {
    free(input);
    free(output);
    error = "JPEG encoder returned an invalid output size";
    return false;
  }
#endif
  free(input);

  if (sdFS().exists(path)) sdFS().remove(path);
  File file = sdFS().open(path, FILE_WRITE);
  if (!file) {
    free(output);
    error = "Could not open screenshot file";
    return false;
  }

  constexpr size_t kWriteChunkBytes = 32 * 1024;
  size_t written = 0;
  while (written < jpeg_size) {
    const size_t chunk = std::min(kWriteChunkBytes, static_cast<size_t>(jpeg_size) - written);
    if (file.write(output + written, chunk) != chunk) {
      file.close();
      sdFS().remove(path);
      free(output);
      error = "Could not write JPEG screenshot";
      return false;
    }
    written += chunk;
    yield();
  }
  file.close();
  free(output);
  if (sdFS().exists(kLegacyScreenshotPath)) sdFS().remove(kLegacyScreenshotPath);
  Serial.printf("[Screenshot] JPEG %ldx%ld quality=%u: %u KB in %u ms\n",
                static_cast<long>(width),
                static_cast<long>(height),
                static_cast<unsigned>(kScreenshotJpegQuality),
                static_cast<unsigned>((jpeg_size + 1023u) / 1024u),
                static_cast<unsigned>(millis() - started_ms));
  return true;
}

void getSnapshotAreaForObject(lv_obj_t* obj, lv_area_t& area) {
  lv_obj_update_layout(obj);
  lv_obj_get_coords(obj, &area);
}

bool hasVisibleDirectChildren(const lv_obj_t* parent) {
  if (!parent) return false;
  const uint32_t child_count = lv_obj_get_child_count(parent);
  for (uint32_t i = 0; i < child_count; ++i) {
    const lv_obj_t* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
    if (child && !lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
      return true;
    }
  }
  return false;
}

uint16_t packRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((static_cast<uint16_t>(r) & 0xF8u) << 8) |
                               ((static_cast<uint16_t>(g) & 0xFCu) << 3) |
                               (static_cast<uint16_t>(b) >> 3));
}

bool blendArgb8888OverRgb565(lv_draw_buf_t* base,
                             const lv_area_t& base_area,
                             const lv_draw_buf_t* overlay,
                             const lv_area_t& overlay_area,
                             String& error) {
  if (!base || !base->data || !overlay || !overlay->data) {
    error = "Screenshot buffers missing";
    return false;
  }
  if (base->header.cf != LV_COLOR_FORMAT_RGB565) {
    error = "Unsupported base screenshot format";
    return false;
  }
  if (overlay->header.cf != LV_COLOR_FORMAT_ARGB8888) {
    error = "Unsupported overlay screenshot format";
    return false;
  }

  const int32_t x1 = std::max(base_area.x1, overlay_area.x1);
  const int32_t y1 = std::max(base_area.y1, overlay_area.y1);
  const int32_t x2 = std::min(base_area.x2, overlay_area.x2);
  const int32_t y2 = std::min(base_area.y2, overlay_area.y2);
  if (x1 > x2 || y1 > y2) {
    return true;
  }

  const uint32_t base_stride = base->header.stride;
  const uint32_t overlay_stride = overlay->header.stride;

  for (int32_t y = y1; y <= y2; ++y) {
    uint16_t* dst = reinterpret_cast<uint16_t*>(
        base->data + static_cast<uint32_t>(y - base_area.y1) * base_stride) + (x1 - base_area.x1);
    const lv_color32_t* src = reinterpret_cast<const lv_color32_t*>(
        overlay->data + static_cast<uint32_t>(y - overlay_area.y1) * overlay_stride) + (x1 - overlay_area.x1);

    for (int32_t x = x1; x <= x2; ++x, ++dst, ++src) {
      const uint8_t alpha = src->alpha;
      if (alpha == 0) continue;

      const uint8_t src_r = src->red;
      const uint8_t src_g = src->green;
      const uint8_t src_b = src->blue;

      if (alpha >= 255) {
        *dst = packRgb565(src_r, src_g, src_b);
        continue;
      }

      const uint16_t dst565 = *dst;
      const uint8_t dst_r = static_cast<uint8_t>((((dst565 >> 11) & 0x1Fu) * 255u) / 31u);
      const uint8_t dst_g = static_cast<uint8_t>((((dst565 >> 5) & 0x3Fu) * 255u) / 63u);
      const uint8_t dst_b = static_cast<uint8_t>(((dst565 & 0x1Fu) * 255u) / 31u);
      const uint16_t inv_alpha = static_cast<uint16_t>(255u - alpha);

      const uint8_t out_r = static_cast<uint8_t>((static_cast<uint16_t>(src_r) * alpha +
                                                  static_cast<uint16_t>(dst_r) * inv_alpha + 127u) / 255u);
      const uint8_t out_g = static_cast<uint8_t>((static_cast<uint16_t>(src_g) * alpha +
                                                  static_cast<uint16_t>(dst_g) * inv_alpha + 127u) / 255u);
      const uint8_t out_b = static_cast<uint8_t>((static_cast<uint16_t>(src_b) * alpha +
                                                  static_cast<uint16_t>(dst_b) * inv_alpha + 127u) / 255u);
      *dst = packRgb565(out_r, out_g, out_b);
    }
  }

  return true;
}

bool createUiScreenshot(String& error) {
  if (!sdReady()) {
    error = "microSD card not available for screenshots";
    return false;
  }

  lv_display_t* disp = displayManager.getDisplay();
  lv_obj_t* screen = lv_screen_active();
  if (!disp || !screen) {
    error = "Display not ready";
    return false;
  }

  lv_refr_now(disp);
  Device::displayWaitDisplay();

  lv_area_t screen_area;
  getSnapshotAreaForObject(screen, screen_area);
  lv_draw_buf_t* draw_buf = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
  if (!draw_buf) {
    error = "LVGL snapshot failed";
    return false;
  }

  lv_obj_t* top_layer = lv_display_get_layer_top(disp);
  if (top_layer && hasVisibleDirectChildren(top_layer)) {
    lv_area_t top_layer_area;
    getSnapshotAreaForObject(top_layer, top_layer_area);
    lv_draw_buf_t* overlay_buf = lv_snapshot_take(top_layer, LV_COLOR_FORMAT_ARGB8888);
    if (!overlay_buf) {
      lv_draw_buf_destroy(draw_buf);
      error = "Popup overlay snapshot failed";
      return false;
    }

    const bool blended = blendArgb8888OverRgb565(draw_buf, screen_area, overlay_buf, top_layer_area, error);
    lv_draw_buf_destroy(overlay_buf);
    if (!blended) {
      lv_draw_buf_destroy(draw_buf);
      return false;
    }
  }

  const bool ok = saveDrawBufferAsJpeg(draw_buf, String(kScreenshotPath), error);
  lv_draw_buf_destroy(draw_buf);
  return ok;
}

}  // namespace

void WebAdminServer::handleCreateScreenshot() {
  String error;
  if (!createUiScreenshot(error)) {
    String json = "{\"success\":false,\"error\":\"";
    appendJsonEscaped(json, error);
    json += "\"}";
    server.send(500, "application/json", json);
    return;
  }

  String json = "{\"success\":true,\"path\":\"";
  appendJsonEscaped(json, kScreenshotPath);
  json += "\"}";
  sendChunkedResponse(server, 200, "application/json", json);
}

void WebAdminServer::handleDownloadScreenshot() {
  if (!sdReady()) {
    server.send(503, "text/plain", "microSD card not available for screenshots");
    return;
  }
  if (!sdFS().exists(kScreenshotPath)) {
    server.send(404, "text/plain", "Screenshot not found");
    return;
  }

  File file = sdFS().open(kScreenshotPath, FILE_READ);
  if (!file) {
    server.send(500, "text/plain", "Could not open screenshot");
    return;
  }

  String filename = Device::profile().key;
  filename += "-ui-screenshot.jpg";
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(file, "image/jpeg");
  file.close();
}

// ========== Crash diagnostics ==========
// The IDF panic handler writes each core dump to the coredump partition
// (see src/core/diagnostics/crash_log.h). These endpoints expose it without USB.
// The separate crashlog.txt lives in LittleFS and is not visible in the
// microSD-only file manager.

void WebAdminServer::handleCoreDumpDownload() {
  webAdminMarkActivity();
  size_t dump_addr = 0;
  size_t dump_size = 0;
  if (esp_core_dump_image_get(&dump_addr, &dump_size) != ESP_OK ||
      dump_size == 0) {
    server.send(404, "text/plain", "No core dump stored");
    return;
  }
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  if (!part || dump_addr < part->address ||
      dump_addr - part->address + dump_size > part->size) {
    server.send(500, "text/plain", "Core dump partition mismatch");
    return;
  }

  String filename = Device::profile().key;
  filename += "-coredump.bin";
  server.sendHeader("Content-Disposition",
                    "attachment; filename=\"" + filename + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(dump_size);
  server.send(200, "application/octet-stream", "");

  uint8_t buf[1024];
  size_t offset = dump_addr - part->address;
  size_t remaining = dump_size;
  while (remaining > 0) {
    const size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
    if (esp_partition_read(part, offset, buf, chunk) != ESP_OK) break;
    server.sendContent(reinterpret_cast<const char*>(buf), chunk);
    offset += chunk;
    remaining -= chunk;
    yield();
  }
}

void WebAdminServer::handleCoreDumpErase() {
  webAdminMarkActivity();
#if defined(DEVICE_ESP32_S3_RGB_480)
  Device::ScopedStorageWrite storage_write;
#endif
  const esp_err_t err = esp_core_dump_image_erase();
  if (err != ESP_OK) {
    sendJsonError(server, 500, String("Erase failed: ") + esp_err_to_name(err));
    return;
  }
  server.send(200, "application/json", "{\"success\":true}");
}

void WebAdminServer::handleCrashLogDownload() {
  webAdminMarkActivity();
  File file = LittleFS.open(CrashLog::kLogPath, FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "No crash log stored");
    return;
  }
  server.sendHeader("Content-Disposition",
                    "attachment; filename=\"crashlog.txt\"");
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(file, "text/plain");
  file.close();
}

void WebAdminServer::handleSdDiagnosticsDownload() {
  webAdminMarkActivity();

  String report;
  report.reserve(1400);
  report += "HomeTiles SD diagnostic report\n";
  report += "Firmware: hometiles-";
  report += FW_VERSION;
  report += '\n';
  report += "Device profile: ";
  report += Device::profile().key;
  report += '\n';
  report += "Generated at uptime: ";
  report += String(millis());
  report += " ms\n";
  report += "Mode: live on-demand check; no diagnostic history is kept in RAM or flash\n";
  report += "Test: create directory, write/read 16 bytes, remove file and directory\n\n";

  auto send_report = [&]() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain; charset=utf-8", report);
  };
  auto append_errno = [&](const char* phase, int error_number) {
    report += "Result: FAIL\nPhase: ";
    report += phase ? phase : "unknown";
    report += "\nerrno: ";
    report += String(error_number);
    report += " (";
    report += error_number ? strerror(error_number) : "not provided by filesystem";
    report += ")\n";
  };

  const bool ready = Device::sdReady();
#if defined(DEVICE_GUITION_JC1060P470C_FAMILY)
  report += "SD bus: SDMMC slot 0, 4-bit, CLK=43 CMD=44 D0-D3=39-42\n";
  report += "SD power: LDO VO4, GPIO45 active-low, 200 ms reset before each mount attempt\n";
  report += "Last driver phase: ";
  report += JC1060SDMMC.lastErrorPhase();
  report += "\nLast driver result: ";
  report += esp_err_to_name(JC1060SDMMC.lastError());
  report += " (0x";
  report += String(static_cast<unsigned>(JC1060SDMMC.lastError()), HEX);
  report += ")\n";
#endif
  report += "Mounted: ";
  report += ready ? "yes\n" : "no\n";
  if (!ready) {
    report += "Result: FAIL\nPhase: mount/card availability\n";
    send_report();
    return;
  }

  fs::FS& fs = Device::sdFS();
  uint64_t capacity = 0;
  uint64_t free_bytes = 0;
  const char* mountpoint = fs.mountpoint();
  const esp_err_t info_result = mountpoint
                                    ? esp_vfs_fat_info(mountpoint, &capacity,
                                                       &free_bytes)
                                    : ESP_ERR_INVALID_STATE;
  if (info_result == ESP_OK) {
    report += "Capacity: ";
    report += String(static_cast<unsigned long long>(capacity));
    report += " bytes\nUsed: ";
    report += String(static_cast<unsigned long long>(capacity - free_bytes));
    report += " bytes\n";
  } else {
    report += "Capacity: unavailable (";
    report += esp_err_to_name(info_result);
    report += ")\n";
  }

  char directory[56] = {};
  char file_path[80] = {};
  const unsigned long nonce = static_cast<unsigned long>(esp_random());
  snprintf(directory, sizeof(directory), "/.hometiles-sd-check-%08lx", nonce);
  snprintf(file_path, sizeof(file_path), "%s/probe.bin", directory);
  static constexpr uint8_t kProbeData[] = {
      0x48, 0x6f, 0x6d, 0x65, 0x54, 0x69, 0x6c, 0x65,
      0x73, 0x2d, 0x53, 0x44, 0x2d, 0x52, 0x57, 0x0a,
  };

  errno = 0;
  if (!fs.mkdir(directory)) {
    append_errno("mkdir", errno);
    send_report();
    return;
  }

  errno = 0;
  File output = fs.open(file_path, FILE_WRITE);
  if (!output) {
    const int saved_errno = errno;
    fs.rmdir(directory);
    append_errno("open for write", saved_errno);
    send_report();
    return;
  }

  errno = 0;
  const size_t written = output.write(kProbeData, sizeof(kProbeData));
  output.flush();
  const int write_error = output.getWriteError();
  output.close();
  if (written != sizeof(kProbeData) || write_error != 0) {
    const int saved_errno = errno;
    fs.remove(file_path);
    fs.rmdir(directory);
    append_errno("write/flush", saved_errno);
    report += "Written: ";
    report += String(written);
    report += "/";
    report += String(sizeof(kProbeData));
    report += " bytes\nPrint error: ";
    report += String(write_error);
    report += '\n';
    send_report();
    return;
  }

  errno = 0;
  File input = fs.open(file_path, FILE_READ);
  if (!input) {
    const int saved_errno = errno;
    fs.remove(file_path);
    fs.rmdir(directory);
    append_errno("open for readback", saved_errno);
    send_report();
    return;
  }

  uint8_t readback[sizeof(kProbeData)] = {};
  const size_t read_count = input.read(readback, sizeof(readback));
  input.close();
  if (read_count != sizeof(readback) ||
      memcmp(readback, kProbeData, sizeof(readback)) != 0) {
    const int saved_errno = errno;
    fs.remove(file_path);
    fs.rmdir(directory);
    append_errno("readback verification", saved_errno);
    report += "Read: ";
    report += String(read_count);
    report += "/";
    report += String(sizeof(readback));
    report += " bytes\n";
    send_report();
    return;
  }

  errno = 0;
  const bool file_removed = fs.remove(file_path);
  const int remove_errno = errno;
  errno = 0;
  const bool directory_removed = fs.rmdir(directory);
  const int rmdir_errno = errno;
  if (!file_removed || !directory_removed) {
    append_errno(!file_removed ? "remove probe file" : "remove probe directory",
                 !file_removed ? remove_errno : rmdir_errno);
    send_report();
    return;
  }

  report += "Result: PASS\n";
  report += "The card completed mkdir/write/read/remove successfully.\n";

  send_report();
}

#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/ui_manager.h"

#include <Arduino.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <libs/tjpgd/tjpgd.h>
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <driver/jpeg_decode.h>
#include <soc/soc_caps.h>
#endif
#include <string.h>

// lvgl.h no longer exports lv_image_cache_drop() in 9.5; its declaration
// is available only in the instance header (see tile_renderer.cpp).
#include <misc/cache/instance/lv_image_cache.h>

#include "src/core/display/dma2d_arbiter.h"
#include "src/core/memory/psram_budget.h"
#include "src/core/config/config_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/power/power_manager.h"
#include "src/devices/device.h"
#include "src/devices/guition_esp32_4848s040/s3_diagnostics.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/clock/clock_format.h"
#include "src/types/clock/renderer.h"
#include "src/types/energy/energy_data.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/core/config/tile_radius.h"

namespace {

constexpr char kImageDir[] = "/images";
constexpr char kLegacyWallpaperDir[] = "/wallpapers";
constexpr size_t kMaxFileBytes = 8U * 1024U * 1024U;

// Limit the source pixel count accepted by software decoding. The S3
// writes MCU blocks directly into the 480x480 cover, so it does not need
// a source-sized RGB565 intermediate buffer.
constexpr uint32_t kMaxDecodePixels = 2048U * 2048U;

// Leave enough PSRAM after allocating decode buffers for the rest of the
// UI: the cover worker, popups and LVGL intermediate buffers.
constexpr size_t kMaxPsramReserveBytes = 4U * 1024U * 1024U;
constexpr size_t kPpaBufferAlignment = 64;
uint16_t image_radius() {
  return ui_surface_style::radius(tile_radius::kMinimum) + (GRID_PAD < 4 ? GRID_PAD : 4);
}
// After interaction, let visible tile/MQTT state settle first. Otherwise
// a decode/composite pass due at the same time can block the loop before
// LVGL has flushed the switch change to the panel.
constexpr uint32_t kInteractionSettleBeforeSlideMs = 1500;
#if defined(DEVICE_ESP32_S3_RGB_480)
constexpr uint32_t kFailedWallpaperRetryMs = 60000;
uint32_t g_wallpaper_retry_after_ms[kMaxScreensaverWallpapers]{};
#endif

struct ScreensaverState {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* image = nullptr;
  lv_obj_t* clock_box = nullptr;
  lv_obj_t* slot_grid = nullptr;
  lv_timer_t* timer = nullptr;
  int active_wallpaper = -1;
  String active_wallpaper_name;
  uint32_t next_wallpaper_ms = 0;
  uint32_t next_slot_refresh_ms = 0;
  String slot_payloads[TILES_PER_GRID];
  // Track the unit separately: a retained state may deliver the value before
  // the first Bridge sync supplies HA metadata such as kWh. Comparing only
  // the payload would permanently miss the unit when it arrives later.
  String slot_units[TILES_PER_GRID];
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  // Prepare a complete LVGL frame for smooth slide transitions: wallpaper,
  // clock, tiles and any open popup are rendered off-screen in PSRAM, then
  // presented together through the device's full-frame path (PPA rotation on
  // Tab5/8-inch, direct copy on B4).
  uint8_t* composite_storage = nullptr;
  size_t composite_storage_size = 0;
  lv_draw_buf_t composite_draw_buf{};
  bool composite_draw_buf_ready = false;
#endif
};

ScreensaverState* g_state = nullptr;
// Web Admin saves run in loopTask, but LVGL changes wait until the next
// LVGL timer tick. The HTTP handler therefore never changes object
// lifetimes directly, and the visible overlay state is never recreated.
bool g_live_config_refresh_requested = false;
bool g_live_grid_refresh_requested = false;
String g_live_preview_wallpaper;

void apply_configured_screensaver_brightness() {
  powerManager.setDisplayBrightness(Device::backlightRawFromPercent(
      configManager.getConfig().screensaver_brightness_pct));
}

void restore_configured_display_brightness() {
  powerManager.setDisplayBrightness(
      configManager.getConfig().display_brightness);
}

// Single-slot cache: keep the decoded wallpaper in PSRAM (~2 MB) so later
// opens are immediate. Replace the slot only when another image or target
// size is needed. New content under the same filename is not detected
// until restart; the cache does not track file content changes.
String g_cache_name;
uint16_t g_cache_w = 0;
uint16_t g_cache_h = 0;
uint16_t g_cache_focus_x = 500;
uint16_t g_cache_focus_y = 500;
uint16_t g_cache_zoom = 1000;
uint16_t g_cache_radius = 0;
lv_image_dsc_t* g_cache_dsc = nullptr;

// Preload the image a few seconds after tile construction so the first
// tap can open it without waiting for SD access or decoding.
ScreensaverWallpaperConfig g_preload_wallpaper;
lv_timer_t* g_preload_timer = nullptr;
scene_publish_cb_t g_scene_callback = nullptr;

void* alloc_prefer_psram(size_t bytes) {
  if (!bytes) return nullptr;
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  return p;
}

void* alloc_aligned_prefer_psram(size_t bytes) {
  if (!bytes) return nullptr;
  void* p = heap_caps_aligned_alloc(
      kPpaBufferAlignment, bytes,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (!p) {
    p = heap_caps_aligned_alloc(
        kPpaBufferAlignment, bytes,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  }
  // Preview is optional. If only its stricter alignment requirement fails,
  // the established LVGL path must still receive an image.
  if (!p) p = alloc_prefer_psram(bytes);
  return p;
}

bool psram_budget_ok(size_t needed) {
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  const size_t free_total = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t reserve =
      psram_budget::fractionCapped(kMaxPsramReserveBytes);
  return largest >= needed && free_total >= needed + reserve;
}

void free_screensaver_dsc(lv_image_dsc_t*& dsc) {
  if (!dsc) return;
  // LVGL caches decode results by pointer address. Without dropping the
  // entry, a later malloc at the same address can reuse the old image;
  // see the same media-cover issue in free_media_cover_dsc.
  lv_image_cache_drop(dsc);
  if (dsc->data) {
    free(const_cast<uint8_t*>(dsc->data));
  }
  free(dsc);
  dsc = nullptr;
}

void set_image_src_without_invalidation(lv_obj_t* image, const void* src) {
  if (!image) return;
  lv_display_t* display = lv_obj_get_display(image);
  if (display) lv_display_enable_invalidation(display, false);
  lv_image_set_src(image, src);
  if (display) lv_display_enable_invalidation(display, true);
}

#if defined(CONFIG_IDF_TARGET_ESP32P4)
bool ensure_composite_draw_buf(ScreensaverState* st) {
  if (!st) return false;
  const uint32_t width = Device::kScreenWidth;
  const uint32_t height = Device::kScreenHeight;
  const uint32_t stride = width * sizeof(uint16_t);
  const size_t needed = static_cast<size_t>(stride) * height;
  if (needed > UINT32_MAX) return false;

  if (st->composite_draw_buf_ready && st->composite_storage &&
      st->composite_storage_size >= needed) {
    return true;
  }

  if (st->composite_storage) {
    free(st->composite_storage);
    st->composite_storage = nullptr;
  }
  st->composite_storage_size = 0;
  st->composite_draw_buf_ready = false;

  st->composite_storage = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      kPpaBufferAlignment, needed,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (!st->composite_storage) {
    Serial.printf("[Screensaver] Composite buffer missing: %u bytes\n",
                  static_cast<unsigned>(needed));
    return false;
  }

  if (lv_draw_buf_init(&st->composite_draw_buf, width, height,
                       LV_COLOR_FORMAT_RGB565, stride,
                       st->composite_storage,
                       static_cast<uint32_t>(needed)) != LV_RESULT_OK) {
    free(st->composite_storage);
    st->composite_storage = nullptr;
    return false;
  }
  st->composite_storage_size = needed;
  st->composite_draw_buf_ready = true;
  return true;
}

bool present_composited_screensaver_frame(ScreensaverState* st) {
  if (!st || !st->overlay || !ensure_composite_draw_buf(st)) return false;
  lv_display_t* display = lv_obj_get_display(st->overlay);
  lv_obj_t* top_layer = display ? lv_display_get_layer_top(display) : nullptr;
  if (!top_layer) return false;

  const uint32_t snapshot_started = millis();
  // Use the entire top layer intentionally so the clock and tiles remain
  // visible during slide transitions and are presented together.
  if (lv_snapshot_take_to_draw_buf(top_layer, LV_COLOR_FORMAT_RGB565,
                                   &st->composite_draw_buf) != LV_RESULT_OK) {
    Serial.println("[Screensaver] Composite snapshot failed");
    return false;
  }

  const bool preview_ok = Device::displayTryFullFramePreview(
      0, 0, Device::kScreenWidth, Device::kScreenHeight,
      Device::kScreenWidth,
      reinterpret_cast<const uint16_t*>(st->composite_draw_buf.data),
      st->composite_draw_buf.data_size,
      false);  // The snapshot uses native RGB565, not RGB565_SWAPPED.
  // A slideshow frame needs one atomic swap only. Do not leave the persistent
  // camera mirroring state active while the normal UI resumes underneath it.
  Device::displayEndFullFramePreview();
  Serial.printf("[Screensaver] Composite-Preview %s in %u ms\n",
                preview_ok ? "OK" : "skipped",
                static_cast<unsigned>(millis() - snapshot_started));
  return preview_ok;
}
#endif

bool is_jpeg(const uint8_t* data, size_t len) {
  return data && len >= 3 && data[0] == 0xFF && data[1] == 0xD8;
}

// Read an SD file into a PSRAM buffer in chunks.
uint8_t* read_wallpaper_file(const String& file_name, size_t& out_len) {
  out_len = 0;
  if (!Device::sdReadyCached()) {
    Serial.println("[Screensaver] microSD not ready");
    return nullptr;
  }
  String path = String(kImageDir) + "/" + file_name;
  // v0.5.0 uses /images. Existing cards/configurations from older releases
  // continue to work without moving files immediately.
  if (!Device::sdFS().exists(path)) {
    const String legacy_path = String(kLegacyWallpaperDir) + "/" + file_name;
    if (Device::sdFS().exists(legacy_path)) path = legacy_path;
  }
  fs::File f = Device::sdFS().open(path, FILE_READ);
  if (!f) {
    Serial.printf("[Screensaver] open fail: '%s'\n", path.c_str());
    return nullptr;
  }
  const size_t len = f.size();
  if (len < 32 || len > kMaxFileBytes) {
    Serial.printf("[Screensaver] Invalid file size: %u bytes\n",
                  static_cast<unsigned>(len));
    f.close();
    return nullptr;
  }
  uint8_t* buf = static_cast<uint8_t*>(alloc_prefer_psram(len));
  if (!buf) {
    Serial.println("[Screensaver] File buffer allocation failed");
    f.close();
    return nullptr;
  }
  size_t pos = 0;
  while (pos < len) {
    size_t chunk = len - pos;
    if (chunk > 64U * 1024U) chunk = 64U * 1024U;
    const size_t got = f.read(buf + pos, chunk);
    if (got == 0) break;
    pos += got;
  }
  f.close();
  if (pos != len) {
    Serial.printf("[Screensaver] Short read: %u/%u bytes\n",
                  static_cast<unsigned>(pos), static_cast<unsigned>(len));
    free(buf);
    return nullptr;
  }
  out_len = len;
  return buf;
}

#if defined(CONFIG_IDF_TARGET_ESP32P4) && SOC_JPEG_DECODE_SUPPORTED
// Hardware decoding follows media covers (tile_renderer.cpp): dimensions
// aligned to 16 pixels, RGB order = big-endian RGB565 =
// LV_COLOR_FORMAT_RGB565_SWAPPED. Unsupported images fall back to TJpgDec.
// Returns a malloc buffer of out_w*out_h pixels; the caller must free it.
uint16_t* hw_decode_jpeg(const uint8_t* data, size_t len,
                         uint16_t& out_w, uint16_t& out_h) {
  if (len > UINT32_MAX) return nullptr;

  jpeg_decode_picture_info_t info{};
  esp_err_t err = jpeg_decoder_get_info(data, static_cast<uint32_t>(len), &info);
  if (err != ESP_OK || info.width == 0 || info.height == 0) return nullptr;
  if ((info.width & 15U) != 0 || (info.height & 15U) != 0) return nullptr;

  const uint32_t pixels = static_cast<uint32_t>(info.width) * info.height;
  if (pixels > kMaxDecodePixels) return nullptr;

  const size_t requested_bytes = static_cast<size_t>(pixels) * sizeof(uint16_t);
  if (!psram_budget_ok(requested_bytes)) {
    Serial.printf("[Screensaver] PSRAM budget too small for %u bytes\n",
                  static_cast<unsigned>(requested_bytes));
    return nullptr;
  }

  jpeg_decode_memory_alloc_cfg_t mem_cfg{};
  mem_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
  size_t allocated_bytes = 0;
  uint16_t* decoded = static_cast<uint16_t*>(
      jpeg_alloc_decoder_mem(requested_bytes, &mem_cfg, &allocated_bytes));
  if (!decoded || allocated_bytes < requested_bytes) {
    free(decoded);
    Serial.printf("[Screensaver] HW JPEG PSRAM buffer missing: %u bytes\n",
                  static_cast<unsigned>(requested_bytes));
    return nullptr;
  }

  // Keep the engine alive as for media covers. Creating/destroying it per
  // image acquires/releases the 2D DMA pool shared by JPEG and PPA.
  // This churn was suspected of losing transactions on Tab5
  // (the reported PPA stall).
  static jpeg_decoder_handle_t s_engine = nullptr;
  jpeg_decode_cfg_t decode_cfg{};
  decode_cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  decode_cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
  decode_cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
  uint32_t decoded_bytes = 0;
  {
    // Never decode concurrently with a PPA rotation; they share the 2D DMA
    // pool (see dma2d_arbiter.h).
    Dma2dArbiterGuard dma2d_guard(2000);
    if (!dma2d_guard.locked()) {
      free(decoded);
      Serial.println("[Screensaver] 2D DMA arbiter timeout, using software fallback");
      return nullptr;
    }
    if (!s_engine) {
      jpeg_decode_engine_cfg_t engine_cfg{};
      engine_cfg.intr_priority = 0;
      // Full-screen images need more time than 240-pixel covers.
      engine_cfg.timeout_ms = 500;
      err = jpeg_new_decoder_engine(&engine_cfg, &s_engine);
      if (err != ESP_OK || !s_engine) {
        s_engine = nullptr;
        free(decoded);
        Serial.printf("[Screensaver] HW JPEG engine unavailable: %s\n",
                      esp_err_to_name(err));
        return nullptr;
      }
    }
    err = jpeg_decoder_process(s_engine, &decode_cfg, data,
                               static_cast<uint32_t>(len),
                               reinterpret_cast<uint8_t*>(decoded),
                               static_cast<uint32_t>(allocated_bytes),
                               &decoded_bytes);
    if (err != ESP_OK || decoded_bytes < requested_bytes) {
      free(decoded);
      Serial.printf("[Screensaver] HW JPEG decode failed: %s bytes=%u/%u\n",
                    esp_err_to_name(err),
                    static_cast<unsigned>(decoded_bytes),
                    static_cast<unsigned>(requested_bytes));
      // Destroy the engine under the same lock required by its shared-pool
      // lifecycle.
      jpeg_del_decoder_engine(s_engine);
      s_engine = nullptr;
      return nullptr;
    }
  }

  out_w = static_cast<uint16_t>(info.width);
  out_h = static_cast<uint16_t>(info.height);
  return decoded;
}
#endif

// TJpgDec fallback for dimensions not aligned to 16 pixels or very large images.
struct SwJpegCtx {
  const uint8_t* data = nullptr;
  size_t len = 0;
  size_t pos = 0;
  uint16_t* pixels = nullptr;
  uint16_t w = 0;
  uint16_t h = 0;
};

size_t sw_jpeg_input(JDEC* jd, uint8_t* buff, size_t ndata) {
  SwJpegCtx* ctx = static_cast<SwJpegCtx*>(jd->device);
  if (!ctx || !ctx->data || ctx->pos >= ctx->len) return 0;
  size_t remain = ctx->len - ctx->pos;
  size_t take = (ndata < remain) ? ndata : remain;
  if (buff && take) {
    memcpy(buff, ctx->data + ctx->pos, take);
  }
  ctx->pos += take;
  return take;
}

int sw_jpeg_output(JDEC* jd, void* bitmap, JRECT* rect) {
  SwJpegCtx* ctx = static_cast<SwJpegCtx*>(jd->device);
  if (!ctx || !ctx->pixels || !bitmap) return 0;

  // Use the same byte order as media covers:
  // LV_COLOR_FORMAT_RGB565_SWAPPED.
  const uint8_t* src = static_cast<const uint8_t*>(bitmap);
  const uint16_t rw = rect->right - rect->left + 1;
  for (uint16_t y = rect->top; y <= rect->bottom && y < ctx->h; ++y) {
    for (uint16_t x = rect->left; x <= rect->right && x < ctx->w; ++x) {
      const size_t si = ((y - rect->top) * rw + (x - rect->left)) * 3;
      const uint8_t b = src[si];
      const uint8_t g = src[si + 1];
      const uint8_t r = src[si + 2];
      const uint16_t c = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      ctx->pixels[static_cast<size_t>(y) * ctx->w + x] =
          static_cast<uint16_t>((c >> 8) | (c << 8));
    }
  }
  return 1;
}

uint16_t* sw_decode_jpeg(const uint8_t* data, size_t len,
                         uint16_t& out_w, uint16_t& out_h) {
  uint8_t* work = static_cast<uint8_t*>(heap_caps_malloc(4096, MALLOC_CAP_8BIT));
  if (!work) return nullptr;

  SwJpegCtx ctx{};
  ctx.data = data;
  ctx.len = len;

  JDEC jd;
  JRESULT rc = jd_prepare(&jd, sw_jpeg_input, work, 4096, &ctx);
  if (rc != JDR_OK || jd.width == 0 || jd.height == 0 ||
      jd.width > UINT16_MAX || jd.height > UINT16_MAX) {
    Serial.printf("[Screensaver] SW JPEG prepare failed: %d\n",
                  static_cast<int>(rc));
    free(work);
    return nullptr;
  }

  // TJpgDec can scale by 1/2, 1/4 or 1/8 during decoding, bringing even
  // large images within the pixel budget.
  uint8_t scale = 0;
  while (scale < 3 &&
         (static_cast<uint32_t>(jd.width >> scale) *
          static_cast<uint32_t>(jd.height >> scale)) > kMaxDecodePixels) {
    ++scale;
  }
  const uint16_t w = static_cast<uint16_t>(jd.width >> scale);
  const uint16_t h = static_cast<uint16_t>(jd.height >> scale);
  const uint32_t pixels = static_cast<uint32_t>(w) * h;
  if (w == 0 || h == 0 || pixels > kMaxDecodePixels) {
    Serial.printf("[Screensaver] JPEG too large: %ux%u\n",
                  static_cast<unsigned>(jd.width),
                  static_cast<unsigned>(jd.height));
    free(work);
    return nullptr;
  }

  const size_t bytes = static_cast<size_t>(pixels) * sizeof(uint16_t);
  if (!psram_budget_ok(bytes)) {
    Serial.printf("[Screensaver] PSRAM budget too small for %u bytes\n",
                  static_cast<unsigned>(bytes));
    free(work);
    return nullptr;
  }
  uint16_t* out = static_cast<uint16_t*>(alloc_prefer_psram(bytes));
  if (!out) {
    free(work);
    return nullptr;
  }
  memset(out, 0, bytes);

  ctx.pixels = out;
  ctx.w = w;
  ctx.h = h;
  rc = jd_decomp(&jd, sw_jpeg_output, scale);
  free(work);
  if (rc != JDR_OK) {
    Serial.printf("[Screensaver] SW JPEG decode failed: %d\n",
                  static_cast<int>(rc));
    free(out);
    return nullptr;
  }

  out_w = w;
  out_h = h;
  return out;
}

// Pixel coverage inside a rounded corner, using 4x4 supersampling:
// 16 means fully visible, 0 means entirely the border color.
uint8_t rounded_pixel_coverage(uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h,
                               uint16_t radius) {
  if (radius == 0 || w < radius * 2 || h < radius * 2) return 16;
  const bool at_left = x < radius;
  const bool at_right = x >= w - radius;
  const bool at_top = y < radius;
  const bool at_bottom = y >= h - radius;
  if ((!at_left && !at_right) || (!at_top && !at_bottom)) return 16;

  const uint16_t edge_x = at_left ? x : static_cast<uint16_t>(w - 1 - x);
  const uint16_t edge_y = at_top ? y : static_cast<uint16_t>(h - 1 - y);
  constexpr int32_t kSamples = 4;
  constexpr int32_t kUnitsPerPixel = kSamples * 2;
  const int32_t center = static_cast<int32_t>(radius) * kUnitsPerPixel;
  const int32_t radius_sq = center * center;
  uint8_t covered = 0;
  for (int32_t sample_y = 0; sample_y < kSamples; ++sample_y) {
    const int32_t py = static_cast<int32_t>(edge_y) * kUnitsPerPixel +
                       sample_y * 2 + 1;
    const int32_t dy = center - py;
    for (int32_t sample_x = 0; sample_x < kSamples; ++sample_x) {
      const int32_t px = static_cast<int32_t>(edge_x) * kUnitsPerPixel +
                         sample_x * 2 + 1;
      const int32_t dx = center - px;
      if (dx * dx + dy * dy <= radius_sq) ++covered;
    }
  }
  return covered;
}

uint16_t blend_swapped_rgb565_with_black(uint16_t swapped, uint8_t coverage) {
  if (coverage == 0) return 0;
  if (coverage >= 16) return swapped;
  const uint16_t color =
      static_cast<uint16_t>((swapped >> 8) | (swapped << 8));
  const uint16_t red =
      static_cast<uint16_t>((((color >> 11) & 0x1F) * coverage + 8) / 16);
  const uint16_t green =
      static_cast<uint16_t>((((color >> 5) & 0x3F) * coverage + 8) / 16);
  const uint16_t blue =
      static_cast<uint16_t>(((color & 0x1F) * coverage + 8) / 16);
  const uint16_t blended =
      static_cast<uint16_t>((red << 11) | (green << 5) | blue);
  return static_cast<uint16_t>((blended >> 8) | (blended << 8));
}

#if defined(DEVICE_ESP32_S3_RGB_480)
struct S3DirectJpegCtx {
  const uint8_t* data = nullptr;
  size_t len = 0;
  size_t pos = 0;
  uint16_t* output = nullptr;
  uint16_t target_w = 0;
  uint16_t target_h = 0;
  uint16_t image_inset = 0;
  uint16_t image_w = 0;
  uint16_t image_h = 0;
  uint32_t crop_x = 0;
  uint32_t crop_y = 0;
  uint32_t crop_w = 0;
  uint32_t crop_h = 0;
  uint32_t written_pixels = 0;
  uint16_t corner_radius = 0;
};

size_t s3_direct_jpeg_input(JDEC* jd, uint8_t* buffer, size_t bytes) {
  S3DirectJpegCtx* ctx = static_cast<S3DirectJpegCtx*>(jd->device);
  if (!ctx || !ctx->data || ctx->pos >= ctx->len) return 0;
  const size_t remaining = ctx->len - ctx->pos;
  const size_t take = min(bytes, remaining);
  if (buffer && take) memcpy(buffer, ctx->data + ctx->pos, take);
  ctx->pos += take;
  return take;
}

uint16_t s3_first_mapped_destination(uint32_t source_offset,
                                     uint32_t source_span,
                                     uint16_t destination_span) {
  if (source_span == 0 || destination_span == 0) return destination_span;
  const uint64_t numerator =
      static_cast<uint64_t>(source_offset) * destination_span;
  const uint64_t result =
      (numerator + source_span - 1U) / source_span;
  return static_cast<uint16_t>(
      result < destination_span ? result : destination_span);
}

int s3_direct_jpeg_output(JDEC* jd, void* bitmap, JRECT* rect) {
  S3DirectJpegCtx* ctx = static_cast<S3DirectJpegCtx*>(jd->device);
  if (!ctx || !ctx->output || !bitmap || !rect) return 0;

  const uint32_t crop_right = ctx->crop_x + ctx->crop_w - 1U;
  const uint32_t crop_bottom = ctx->crop_y + ctx->crop_h - 1U;
  const uint32_t source_left =
      rect->left > ctx->crop_x ? rect->left : ctx->crop_x;
  const uint32_t source_right =
      rect->right < crop_right ? rect->right : crop_right;
  const uint32_t source_top =
      rect->top > ctx->crop_y ? rect->top : ctx->crop_y;
  const uint32_t source_bottom =
      rect->bottom < crop_bottom ? rect->bottom : crop_bottom;
  if (source_left > source_right || source_top > source_bottom) return 1;

  const uint16_t dx_begin = s3_first_mapped_destination(
      source_left - ctx->crop_x, ctx->crop_w, ctx->image_w);
  const uint16_t dx_end = s3_first_mapped_destination(
      source_right - ctx->crop_x + 1U, ctx->crop_w, ctx->image_w);
  const uint16_t dy_begin = s3_first_mapped_destination(
      source_top - ctx->crop_y, ctx->crop_h, ctx->image_h);
  const uint16_t dy_end = s3_first_mapped_destination(
      source_bottom - ctx->crop_y + 1U, ctx->crop_h, ctx->image_h);
  if (dx_begin >= dx_end || dy_begin >= dy_end) return 1;

  const uint8_t* source = static_cast<const uint8_t*>(bitmap);
  const uint32_t rect_w =
      static_cast<uint32_t>(rect->right) - rect->left + 1U;
  for (uint16_t dy = dy_begin; dy < dy_end; ++dy) {
    const uint32_t sy = ctx->crop_y +
        (static_cast<uint32_t>(dy) * ctx->crop_h) / ctx->image_h;
    uint16_t* destination = ctx->output +
        static_cast<size_t>(dy + ctx->image_inset) * ctx->target_w +
        ctx->image_inset;
    for (uint16_t dx = dx_begin; dx < dx_end; ++dx) {
      const uint32_t sx = ctx->crop_x +
          (static_cast<uint32_t>(dx) * ctx->crop_w) / ctx->image_w;
      const size_t source_index =
          (static_cast<size_t>(sy - rect->top) * rect_w +
           (sx - rect->left)) * 3U;
      const uint8_t blue = source[source_index];
      const uint8_t green = source[source_index + 1U];
      const uint8_t red = source[source_index + 2U];
      const uint16_t native = static_cast<uint16_t>(
          ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
      const uint16_t swapped =
          static_cast<uint16_t>((native >> 8) | (native << 8));
      destination[dx] = blend_swapped_rgb565_with_black(
          swapped, rounded_pixel_coverage(dx, dy, ctx->image_w,
                                           ctx->image_h, ctx->corner_radius));
      ++ctx->written_pixels;
    }
  }
  return 1;
}

lv_image_dsc_t* s3_decode_jpeg_direct_cover(
    const uint8_t* data, size_t len, uint16_t target_w, uint16_t target_h,
    uint16_t focus_x, uint16_t focus_y, uint16_t zoom,
    uint16_t& source_w, uint16_t& source_h, uint16_t corner_radius) {
  source_w = 0;
  source_h = 0;
  uint8_t* work =
      static_cast<uint8_t*>(heap_caps_malloc(4096, MALLOC_CAP_8BIT));
  if (!work) return nullptr;

  S3DirectJpegCtx ctx{};
  ctx.corner_radius = corner_radius;
  ctx.data = data;
  ctx.len = len;
  JDEC decoder{};
  const JRESULT prepare_result =
      jd_prepare(&decoder, s3_direct_jpeg_input, work, 4096, &ctx);
  if (prepare_result != JDR_OK || decoder.width == 0 || decoder.height == 0 ||
      decoder.width > UINT16_MAX || decoder.height > UINT16_MAX) {
    Serial.printf("[Screensaver] S3 direct JPEG prepare failed: %d\n",
                  static_cast<int>(prepare_result));
    free(work);
    return nullptr;
  }

  source_w = static_cast<uint16_t>(decoder.width);
  source_h = static_cast<uint16_t>(decoder.height);
  const uint32_t source_pixels =
      static_cast<uint32_t>(source_w) * source_h;
  if (source_pixels > kMaxDecodePixels) {
    Serial.printf("[Screensaver] S3 direct JPEG too large: %ux%u\n",
                  static_cast<unsigned>(source_w),
                  static_cast<unsigned>(source_h));
    free(work);
    return nullptr;
  }

  ctx.image_inset = GRID_PAD > 4 ? GRID_PAD - 4 : 0;
  if (target_w <= ctx.image_inset * 2 ||
      target_h <= ctx.image_inset * 2) {
    free(work);
    return nullptr;
  }
  ctx.target_w = target_w;
  ctx.target_h = target_h;
  ctx.image_w = target_w - ctx.image_inset * 2;
  ctx.image_h = target_h - ctx.image_inset * 2;

  ctx.crop_w = source_w;
  ctx.crop_h = static_cast<uint32_t>(
      (static_cast<uint64_t>(ctx.crop_w) * ctx.image_h) / ctx.image_w);
  if (ctx.crop_h > source_h || ctx.crop_h == 0) {
    ctx.crop_h = source_h;
    ctx.crop_w = static_cast<uint32_t>(
        (static_cast<uint64_t>(ctx.crop_h) * ctx.image_w) / ctx.image_h);
    if (ctx.crop_w > source_w) ctx.crop_w = source_w;
  }
  if (ctx.crop_w == 0 || ctx.crop_h == 0) {
    free(work);
    return nullptr;
  }
  if (zoom < 1000U) zoom = 1000U;
  if (zoom > 3000U) zoom = 3000U;
  ctx.crop_w = (ctx.crop_w * 1000U) / zoom;
  ctx.crop_h = (ctx.crop_h * 1000U) / zoom;
  if (ctx.crop_w < 1U) ctx.crop_w = 1U;
  if (ctx.crop_h < 1U) ctx.crop_h = 1U;
  if (ctx.crop_w > source_w) ctx.crop_w = source_w;
  if (ctx.crop_h > source_h) ctx.crop_h = source_h;
  if (focus_x > 1000U) focus_x = 1000U;
  if (focus_y > 1000U) focus_y = 1000U;
  ctx.crop_x = ((source_w - ctx.crop_w) * focus_x) / 1000U;
  ctx.crop_y = ((source_h - ctx.crop_h) * focus_y) / 1000U;

  const size_t output_bytes =
      static_cast<size_t>(target_w) * target_h * sizeof(uint16_t);
  if (!psram_budget_ok(output_bytes)) {
    Serial.printf(
        "[Screensaver] S3 direct JPEG PSRAM budget too small: %u bytes\n",
        static_cast<unsigned>(output_bytes));
    free(work);
    return nullptr;
  }
  ctx.output = static_cast<uint16_t*>(
      alloc_aligned_prefer_psram(output_bytes));
  if (!ctx.output) {
    free(work);
    return nullptr;
  }
  memset(ctx.output, 0, output_bytes);

  const JRESULT decode_result =
      jd_decomp(&decoder, s3_direct_jpeg_output, 0);
  free(work);
  const uint32_t expected_pixels =
      static_cast<uint32_t>(ctx.image_w) * ctx.image_h;
  if (decode_result != JDR_OK || ctx.written_pixels != expected_pixels) {
    Serial.printf(
        "[Screensaver] S3 direct JPEG decode failed: result=%d "
        "pixels=%lu/%lu\n",
        static_cast<int>(decode_result),
        static_cast<unsigned long>(ctx.written_pixels),
        static_cast<unsigned long>(expected_pixels));
    free(ctx.output);
    return nullptr;
  }

  lv_image_dsc_t* descriptor =
      static_cast<lv_image_dsc_t*>(malloc(sizeof(lv_image_dsc_t)));
  if (!descriptor) {
    free(ctx.output);
    return nullptr;
  }
  memset(descriptor, 0, sizeof(*descriptor));
  descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
  descriptor->header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  descriptor->header.w = target_w;
  descriptor->header.h = target_h;
  descriptor->header.stride = target_w * sizeof(uint16_t);
  descriptor->data_size = output_bytes;
  descriptor->data = reinterpret_cast<const uint8_t*>(ctx.output);
  return descriptor;
}
#endif

// Build the final screen image with the same outer black border as the
// normal tile grid. The image edge sits exactly 4 px outside the tiles
// (GRID_PAD - 4), with the configured tile radius plus the
// extra 4 px outside them. PPA and LVGL share this full-frame buffer,
// so opening needs neither additional clipping nor rendering in bands.
lv_image_dsc_t* make_cover_dsc(const uint16_t* src, uint16_t src_w,
                               uint16_t src_h, uint16_t target_w,
                               uint16_t target_h, uint16_t focus_x,
                               uint16_t focus_y, uint16_t zoom, uint16_t corner_radius) {
  if (!src || src_w == 0 || src_h == 0 || target_w == 0 || target_h == 0) {
    return nullptr;
  }
  const uint16_t image_inset = GRID_PAD > 4 ? GRID_PAD - 4 : 0;
  if (target_w <= image_inset * 2 || target_h <= image_inset * 2) {
    return nullptr;
  }
  const uint16_t image_w = target_w - image_inset * 2;
  const uint16_t image_h = target_h - image_inset * 2;

  uint32_t crop_w = src_w;
  uint32_t crop_h =
      static_cast<uint32_t>((static_cast<uint64_t>(crop_w) * image_h) / image_w);
  if (crop_h > src_h || crop_h == 0) {
    crop_h = src_h;
    crop_w =
        static_cast<uint32_t>((static_cast<uint64_t>(crop_h) * image_w) / image_h);
    if (crop_w > src_w) crop_w = src_w;
  }
  if (crop_w == 0 || crop_h == 0) return nullptr;
  if (zoom < 1000) zoom = 1000;
  if (zoom > 3000) zoom = 3000;
  crop_w = (crop_w * 1000U) / zoom;
  crop_h = (crop_h * 1000U) / zoom;
  if (crop_w == 0) crop_w = 1;
  if (crop_h == 0) crop_h = 1;
  if (crop_w > src_w) crop_w = src_w;
  if (crop_h > src_h) crop_h = src_h;
  if (focus_x > 1000) focus_x = 1000;
  if (focus_y > 1000) focus_y = 1000;
  const uint32_t x0 = ((src_w - crop_w) * focus_x) / 1000U;
  const uint32_t y0 = ((src_h - crop_h) * focus_y) / 1000U;

  const size_t bytes = static_cast<size_t>(target_w) * target_h * sizeof(uint16_t);
  // The completed cache can feed PPA directly on the 8-inch device.
  // 64-byte alignment also meets the cache/DMA requirement; the format
  // stays unchanged for LVGL and the other targets.
  uint16_t* out = static_cast<uint16_t*>(alloc_aligned_prefer_psram(bytes));
  if (!out) return nullptr;
  memset(out, 0, bytes);

  for (uint16_t y = 0; y < image_h; ++y) {
    const uint32_t sy = y0 + (static_cast<uint32_t>(y) * crop_h) / image_h;
    const uint16_t* src_row = src + static_cast<size_t>(sy) * src_w;
    uint16_t* dst_row = out +
        static_cast<size_t>(y + image_inset) * target_w + image_inset;
    for (uint16_t x = 0; x < image_w; ++x) {
      const uint32_t sx = x0 + (static_cast<uint32_t>(x) * crop_w) / image_w;
      const uint8_t coverage =
          rounded_pixel_coverage(x, y, image_w, image_h, corner_radius);
      dst_row[x] = blend_swapped_rgb565_with_black(src_row[sx], coverage);
    }
  }

  lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(malloc(sizeof(lv_image_dsc_t)));
  if (!dsc) {
    free(out);
    return nullptr;
  }
  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  dsc->header.w = target_w;
  dsc->header.h = target_h;
  dsc->header.stride = target_w * 2;
  dsc->data_size = bytes;
  dsc->data = reinterpret_cast<const uint8_t*>(out);
  return dsc;
}

lv_image_dsc_t* decode_wallpaper_to_size(const String& file_name,
                                         uint16_t target_w, uint16_t target_h,
                                         uint16_t focus_x, uint16_t focus_y,
                                         uint16_t zoom, uint16_t corner_radius) {
  const uint32_t pipeline_started_ms = millis();
  size_t len = 0;
  uint8_t* file = read_wallpaper_file(file_name, len);
  if (!file) {
    GuitionS3Diagnostics::logSlideshowDecode(
        file_name.c_str(), 0, 0, 0, target_w, target_h,
        static_cast<uint32_t>(target_w) * sizeof(uint16_t), "none", false,
        millis() - pipeline_started_ms, 0, 0);
    return nullptr;
  }
  const uint32_t read_ms = millis() - pipeline_started_ms;
  if (!is_jpeg(file, len)) {
    Serial.println("[Screensaver] File is not a JPEG");
    GuitionS3Diagnostics::logSlideshowDecode(
        file_name.c_str(), len, 0, 0, target_w, target_h,
        static_cast<uint32_t>(target_w) * sizeof(uint16_t), "none", false,
        read_ms, 0, 0);
    free(file);
    return nullptr;
  }

  const uint32_t decode_started_ms = millis();
  uint16_t w = 0;
  uint16_t h = 0;
#if defined(DEVICE_ESP32_S3_RGB_480)
  lv_image_dsc_t* dsc = s3_decode_jpeg_direct_cover(
      file, len, target_w, target_h, focus_x, focus_y, zoom, w, h, corner_radius);
  const uint32_t decode_ms = millis() - decode_started_ms;
  free(file);
  GuitionS3Diagnostics::logSlideshowDecode(
      file_name.c_str(), len, w, h, target_w, target_h,
      static_cast<uint32_t>(target_w) * sizeof(uint16_t),
      "SW-direct", dsc != nullptr, read_ms, decode_ms, 0);
  if (dsc) {
    Serial.printf(
        "[Screensaver] SW-direct Decode %ux%u -> %ux%u in %u ms\n",
        static_cast<unsigned>(w), static_cast<unsigned>(h),
        static_cast<unsigned>(target_w), static_cast<unsigned>(target_h),
        static_cast<unsigned>(millis() - pipeline_started_ms));
  }
  return dsc;
#else
  uint16_t* pixels = nullptr;
  bool hw = false;
  const char* decoder = "SW";
#if defined(CONFIG_IDF_TARGET_ESP32P4) && SOC_JPEG_DECODE_SUPPORTED
  pixels = hw_decode_jpeg(file, len, w, h);
  hw = pixels != nullptr;
  if (hw) decoder = "HW";
#endif
  if (!pixels) {
    pixels = sw_decode_jpeg(file, len, w, h);
  }
  const uint32_t decode_ms = millis() - decode_started_ms;
  free(file);
  if (!pixels) {
    GuitionS3Diagnostics::logSlideshowDecode(
        file_name.c_str(), len, w, h, target_w, target_h,
        static_cast<uint32_t>(target_w) * sizeof(uint16_t),
        decoder, false, read_ms, decode_ms, 0);
    return nullptr;
  }

  const uint32_t cover_started_ms = millis();
  lv_image_dsc_t* dsc = make_cover_dsc(pixels, w, h, target_w, target_h,
                                       focus_x, focus_y, zoom, corner_radius);
  const uint32_t cover_ms = millis() - cover_started_ms;
  free(pixels);
  GuitionS3Diagnostics::logSlideshowDecode(
      file_name.c_str(), len, w, h, target_w, target_h,
      static_cast<uint32_t>(target_w) * sizeof(uint16_t),
      decoder, dsc != nullptr, read_ms, decode_ms, cover_ms);
  if (dsc) {
    Serial.printf("[Screensaver] %s-Decode %ux%u -> %ux%u in %u ms\n",
                  decoder,
                  static_cast<unsigned>(w), static_cast<unsigned>(h),
                  static_cast<unsigned>(target_w), static_cast<unsigned>(target_h),
                  static_cast<unsigned>(millis() - pipeline_started_ms));
  }
  return dsc;
#endif
}

lv_image_dsc_t* get_or_decode_cached(const ScreensaverWallpaperConfig& wallpaper,
                                     uint16_t w, uint16_t h,
                                     lv_obj_t* visible_image = nullptr) {
  const String& name = wallpaper.file_name;
  if (g_cache_dsc && g_cache_name == name && g_cache_w == w && g_cache_h == h &&
      g_cache_focus_x == wallpaper.focus_x &&
      g_cache_focus_y == wallpaper.focus_y && g_cache_zoom == wallpaper.zoom && g_cache_radius == image_radius()) {
    return g_cache_dsc;
  }
  const uint16_t corner_radius = image_radius();
  lv_image_dsc_t* dsc = decode_wallpaper_to_size(
      name, w, h, wallpaper.focus_x, wallpaper.focus_y, wallpaper.zoom, corner_radius);
  if (!dsc) return nullptr;
  // Detach the old source from the visible LVGL object only now, immediately
  // before swapping caches. The previous slide stays visible during decoding
  // but never references already freed memory.
  if (visible_image && g_cache_dsc) {
    set_image_src_without_invalidation(visible_image, nullptr);
  }
  // Replace the old slot only after successful decoding. Callers ensure
  // that no overlay still displays the old buffer.
  free_screensaver_dsc(g_cache_dsc);
  g_cache_dsc = dsc;
  g_cache_name = name;
  g_cache_w = w;
  g_cache_h = h;
  g_cache_focus_x = wallpaper.focus_x;
  g_cache_focus_y = wallpaper.focus_y;
  g_cache_zoom = wallpaper.zoom;
  g_cache_radius = corner_radius;
  return dsc;
}


bool is_wallpaper_file(const String& file_name) {
  if (!file_name.length() || file_name.indexOf('/') >= 0 ||
      file_name.indexOf('\\') >= 0 || file_name.indexOf("..") >= 0) {
    return false;
  }
  String lower = file_name;
  lower.toLowerCase();
  return lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

// is_wallpaper_file checks the filename pattern, not existence on SD.
// Without this check, an image deleted since being configured would leave
// from_config true, so apply_wallpaper() would skip the fallback search
// and fail even if other valid images remained on the card.
bool sd_wallpaper_file_exists(const String& file_name) {
  if (!is_wallpaper_file(file_name) || !Device::sdReadyCached()) return false;
  return Device::sdFS().exists(String(kImageDir) + "/" + file_name) ||
         Device::sdFS().exists(String(kLegacyWallpaperDir) + "/" + file_name);
}

bool find_first_sd_wallpaper(ScreensaverWallpaperConfig& out) {
  if (!Device::sdReadyCached()) return false;
  const char* directories[] = {kImageDir, kLegacyWallpaperDir};
  for (const char* directory : directories) {
    fs::File dir = Device::sdFS().open(directory, FILE_READ);
    // Waveshare SDMMC can report isDirectory() false for a successfully
    // opened directory. As in the file manager, openNextFile() is the
    // reliable check here.
    if (!dir) continue;
    fs::File entry = dir.openNextFile();
    while (entry) {
      String name = entry.name();
      const int slash = max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
      if (slash >= 0) name = name.substring(slash + 1);
      const bool usable = !entry.isDirectory() && is_wallpaper_file(name);
      entry.close();
      if (usable) {
        out = ScreensaverWallpaperConfig{};
        out.file_name = name;
        dir.close();
        return true;
      }
      entry = dir.openNextFile();
    }
    dir.close();
  }
  return false;
}

int first_enabled_wallpaper() {
  const auto& wallpapers = screensaverConfig.get().wallpapers;
  for (size_t i = 0; i < wallpapers.size(); ++i) {
    if (wallpapers[i].enabled && is_wallpaper_file(wallpapers[i].file_name)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int next_enabled_wallpaper(int current) {
  const auto& config = screensaverConfig.get();
  const size_t count = config.wallpapers.size();
  if (count == 0) return -1;
  if (config.shuffle) {
    int choices[kMaxScreensaverWallpapers];
    size_t choice_count = 0;
    for (size_t i = 0; i < count; ++i) {
      if (config.wallpapers[i].enabled &&
          is_wallpaper_file(config.wallpapers[i].file_name) &&
          (static_cast<int>(i) != current || count == 1)) {
        choices[choice_count++] = static_cast<int>(i);
      }
    }
    if (choice_count) return choices[random(choice_count)];
  }
  for (size_t step = 1; step <= count; ++step) {
    const size_t i = (static_cast<size_t>(current < 0 ? 0 : current) + step) % count;
    if (config.wallpapers[i].enabled && is_wallpaper_file(config.wallpapers[i].file_name)) {
      return static_cast<int>(i);
    }
  }
  return first_enabled_wallpaper();
}

void position_global_clock(ScreensaverState* st) {
  if (!st || !st->clock_box) return;
  const ScreensaverConfigData& config = screensaverConfig.get();
  const int x = (static_cast<int>(config.clock_x) * Device::kScreenWidth) / 1000;
  const int y = (static_cast<int>(config.clock_y) * Device::kScreenHeight) / 1000;
  lv_obj_set_pos(st->clock_box, x - lv_obj_get_width(st->clock_box) / 2,
                 y - lv_obj_get_height(st->clock_box) / 2);
}

void on_global_clock_size_changed(lv_event_t* e) {
  ScreensaverState* st =
      static_cast<ScreensaverState*>(lv_event_get_user_data(e));
  if (!st || lv_event_get_target(e) != st->clock_box) return;
  // When opened very early, system time may still be unknown and the clock
  // initially has almost no width. Once NTP supplies time and its labels
  // grow, reapply the stored center so the text does not extend rightward
  // past the display edge.
  position_global_clock(st);
}

void rebuild_global_clock(ScreensaverState* st) {
  if (!st || !st->overlay) return;

  // The clock is an independent child tree. Synchronous deletion inside
  // this LVGL timer is safe and leaves the global overlay and its
  // click/popup lifecycle intact.
  if (st->clock_box) {
    lv_obj_delete(st->clock_box);
    st->clock_box = nullptr;
  }

  const ScreensaverConfigData& config = screensaverConfig.get();
  if (!config.show_time && !config.show_date && !config.show_weekday) return;

  ClockWidgetConfig widget_config;
  widget_config.show_time = config.show_time;
  widget_config.show_date = config.show_date;
  widget_config.show_weekday = config.show_weekday;
  widget_config.weekday_language = configManager.getConfig().language;
  widget_config.fill_parent = false;
  widget_config.text_shadow = config.clock_shadow;
  widget_config.time_font_size = config.time_font_size;
  widget_config.date_font_size = config.date_font_size;
  widget_config.time_alignment = config.time_alignment;
  widget_config.date_alignment = config.date_alignment;
  widget_config.time_format = clock_tile::resolve_time_format(
      config.time_format, configManager.getConfig().global_time_format,
      configManager.getConfig().language);
  widget_config.date_format = clock_tile::resolve_date_format(
      config.date_format, configManager.getConfig().global_date_format,
      configManager.getConfig().language);
  st->clock_box = create_clock_widget(st->overlay, widget_config);
  if (!st->clock_box) return;
  lv_obj_add_event_cb(st->clock_box, on_global_clock_size_changed,
                      LV_EVENT_SIZE_CHANGED, st);
  lv_obj_update_layout(st->clock_box);
  position_global_clock(st);
}

bool apply_wallpaper(ScreensaverState* st, int index, bool allow_fallback,
                     bool allow_disabled = false) {
  if (!st || !st->image) return false;
  ScreensaverWallpaperConfig wallpaper;
  bool from_config = false;
  const auto& config = screensaverConfig.get();
  if (config.use_wallpapers && index >= 0 &&
      static_cast<size_t>(index) < config.wallpapers.size()) {
    wallpaper = config.wallpapers[static_cast<size_t>(index)];
    from_config = (wallpaper.enabled || allow_disabled) &&
                  sd_wallpaper_file_exists(wallpaper.file_name);
  }
  if (!from_config && allow_fallback && config.use_wallpapers) {
    if (!find_first_sd_wallpaper(wallpaper)) return false;
    index = -1;
  } else if (!from_config) {
    return false;
  }

#if defined(DEVICE_ESP32_S3_RGB_480)
  const uint32_t retry_now_ms = millis();
  if (!allow_disabled && index >= 0 &&
      static_cast<size_t>(index) < kMaxScreensaverWallpapers &&
      g_wallpaper_retry_after_ms[index] != 0 &&
      static_cast<int32_t>(retry_now_ms -
                           g_wallpaper_retry_after_ms[index]) < 0) {
    Serial.printf(
        "[S3Diag/Slide] decode=skipped reason=cooldown file=%s retry_in_ms=%lu\n",
        wallpaper.file_name.c_str(),
        static_cast<unsigned long>(g_wallpaper_retry_after_ms[index] -
                                   retry_now_ms));
    return false;
  }
#endif

  const bool cache_hit = g_cache_dsc && g_cache_name == wallpaper.file_name &&
                         g_cache_w == Device::kScreenWidth &&
                         g_cache_h == Device::kScreenHeight &&
                         g_cache_focus_x == wallpaper.focus_x &&
                         g_cache_focus_y == wallpaper.focus_y &&
                         g_cache_zoom == wallpaper.zoom && g_cache_radius == image_radius();
  lv_image_dsc_t* dsc = get_or_decode_cached(
      wallpaper, Device::kScreenWidth, Device::kScreenHeight, st->image);
  if (!dsc) {
#if defined(DEVICE_ESP32_S3_RGB_480)
    if (index >= 0 && static_cast<size_t>(index) < kMaxScreensaverWallpapers) {
      g_wallpaper_retry_after_ms[index] = millis() + kFailedWallpaperRetryMs;
    }
#endif
    return false;
  }
#if defined(DEVICE_ESP32_S3_RGB_480)
  if (index >= 0 && static_cast<size_t>(index) < kMaxScreensaverWallpapers) {
    g_wallpaper_retry_after_ms[index] = 0;
  }
#endif
  // Change the source internally without triggering a full-screen LVGL
  // redraw yet. On rotated P4 devices, LVGL first renders the complete
  // frame off-screen; only that frame is then passed to PPA.
  set_image_src_without_invalidation(st->image, dsc);
  lv_obj_remove_flag(st->image, LV_OBJ_FLAG_HIDDEN);
  st->active_wallpaper = index;
  st->active_wallpaper_name = wallpaper.file_name;
  st->next_wallpaper_ms = millis() +
      static_cast<uint32_t>(screensaverConfig.get().duration_seconds) * 1000U;

  // On rotated P4 devices, PPA must not initially write just the wallpaper
  // to the visible buffer: the clock and tiles would disappear until their
  // LVGL redraw, causing visible blinking. Instead, composite the entire
  // top layer off-screen and present the completed frame. On B4, write it
  // directly into the native DSI framebuffer without rotation.
  const uint32_t started = millis();
  bool preview_ok = false;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  preview_ok = present_composited_screensaver_frame(st);
#endif
  Serial.printf("[Screensaver] Hardware-Preview %s (%s) in %u ms\n",
                preview_ok ? "OK" : "skipped",
                cache_hit ? "cache" : "decode",
                static_cast<unsigned>(millis() - started));
  if (!preview_ok) {
    // If the device-specific full-frame path is unavailable, LVGL continues
    // to draw the new image safely through its established path.
#if defined(DEVICE_ESP32_S3_RGB_480)
    const bool atomic_redraw =
        DeviceImpl::displayBeginAtomicFrame("screensaver");
#endif
    GuitionS3Diagnostics::beginSlideshowPresentation(
        wallpaper.file_name.c_str(), cache_hit, Device::kScreenWidth,
        Device::kScreenHeight, dsc->header.stride);
#if defined(DEVICE_ESP32_S3_RGB_480)
    // The inactive framebuffer deliberately isn't copied first: that large
    // PSRAM read/write burst can starve RGB EDMA. Invalidate the whole screen
    // so LVGL fully replaces it before the atomic swap instead.
    if (atomic_redraw) {
      lv_obj_invalidate(lv_screen_active());
    } else {
      lv_obj_invalidate(st->image);
    }
#else
    lv_obj_invalidate(st->image);
#endif
  }
  return true;
}

void refresh_slot_values(ScreensaverState* st) {
  if (!st) return;
  const TileGridConfig& grid = screensaverConfig.tileGrid();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = grid.tiles[i];
    if (!tile.sensor_entity.length()) continue;
    String payload;
    if (!tiles_get_cached_entity_payload(tile.sensor_entity.c_str(), payload)) {
      payload = haBridgeConfig.findSensorInitialValue(tile.sensor_entity);
    }
    if (!payload.length()) continue;
    if (tile.type == TILE_SENSOR || tile.type == TILE_ENERGY) {
      String unit = tile.sensor_unit;
      if (!unit.length()) unit = haBridgeConfig.findSensorUnit(tile.sensor_entity);
      if (!unit.length() && tile.type == TILE_ENERGY) {
        unit = energy_find_cached_unit(tile.sensor_entity);
      }
      if (payload == st->slot_payloads[i] && unit == st->slot_units[i]) continue;
      st->slot_payloads[i] = payload;
      st->slot_units[i] = unit;
      queue_sensor_tile_update(GridType::SCREENSAVER, static_cast<uint8_t>(i),
                               payload.c_str(), unit.length() ? unit.c_str() : nullptr);
    } else if (tile.type == TILE_SWITCH) {
      if (payload == st->slot_payloads[i]) continue;
      st->slot_payloads[i] = payload;
      queue_switch_tile_update(GridType::SCREENSAVER, static_cast<uint8_t>(i),
                               payload.c_str());
    } else if (tile.type == TILE_MEDIA) {
      if (payload == st->slot_payloads[i]) continue;
      st->slot_payloads[i] = payload;
      queue_media_tile_update(GridType::SCREENSAVER, static_cast<uint8_t>(i),
                               payload.c_str());
    } else if (tile.type == TILE_COVER) {
      if (payload == st->slot_payloads[i]) continue;
      st->slot_payloads[i] = payload;
      queue_cover_tile_update(GridType::SCREENSAVER, static_cast<uint8_t>(i),
                              payload.c_str());
    } else if (tile.type == TILE_BINARY_SENSOR) {
      if (payload == st->slot_payloads[i]) continue;
      st->slot_payloads[i] = payload;
      queue_binary_sensor_tile_update(
          GridType::SCREENSAVER, static_cast<uint8_t>(i), payload.c_str());
    }
  }
  process_sensor_update_queue();
  process_switch_update_queue();
  process_cover_update_queue();
  process_binary_sensor_update_queue();
  process_media_update_queue();
}

// Apply the global shadow option to existing screensaver tiles so a
// live save needs no grid rebuild. Fully transparent tiles retain no
// shadow, which would otherwise frame an invisible surface.
void apply_slot_tile_shadows(ScreensaverState* st) {
  if (!st || !st->slot_grid) return;
  const bool enabled = screensaverConfig.get().tile_shadow;
  const uint32_t count = lv_obj_get_child_count(st->slot_grid);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* card = lv_obj_get_child(st->slot_grid, i);
    if (!card) continue;
    const bool visible_bg =
        lv_obj_get_style_bg_opa(card, LV_PART_MAIN) != LV_OPA_TRANSP;
    if (enabled && visible_bg) {
      lv_obj_set_style_shadow_width(card, 32, 0);
      lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
      lv_obj_set_style_shadow_opa(card, LV_OPA_60, 0);
      lv_obj_set_style_shadow_spread(card, 3, 0);
    } else {
      lv_obj_set_style_shadow_width(card, 0, 0);
      lv_obj_set_style_shadow_opa(card, LV_OPA_TRANSP, 0);
    }
  }
}

void apply_slot_tile_borders(ScreensaverState* st) {
  if (!st || !st->slot_grid) return;
  const bool enabled = screensaverConfig.get().tile_border;
  const uint32_t count = lv_obj_get_child_count(st->slot_grid);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* card = lv_obj_get_child(st->slot_grid, i);
    if (!card) continue;
    ui_surface_style::apply_tile_border(card, enabled);
  }
}

void rebuild_slot_grid(ScreensaverState* st) {
  if (!st || !st->overlay) return;

  reset_sensor_widgets(GridType::SCREENSAVER);
  reset_switch_widgets(GridType::SCREENSAVER);
  reset_cover_widgets(GridType::SCREENSAVER);
  reset_binary_sensor_widgets(GridType::SCREENSAVER);
  reset_media_widgets(GridType::SCREENSAVER);
  if (st->slot_grid) {
    // Keep the transparent full-screen container. Deleting it would invalidate
    // the entire underlying image and could redraw the wallpaper through the
    // slow LVGL path. Cleaning it marks only the old tile regions dirty.
    lv_obj_clean(st->slot_grid);
  }
  for (String& payload : st->slot_payloads) payload = String();

  // Use exactly the normal tile system's tracks, gaps and outer padding.
  // The prepared full-frame image starts at GRID_PAD - 4, placing it
  // exactly 4 px outside the tiles on every side.
  static lv_coord_t col_dsc[GRID_COLS + 1];
  static lv_coord_t row_dsc[GRID_ROWS + 1];
  static bool grid_dsc_ready = false;
  if (!grid_dsc_ready) {
    for (uint8_t i = 0; i < GRID_COLS; ++i) col_dsc[i] = GRID_CELL_W;
    col_dsc[GRID_COLS] = LV_GRID_TEMPLATE_LAST;
    for (uint8_t i = 0; i < GRID_ROWS; ++i) row_dsc[i] = GRID_CELL_H;
    row_dsc[GRID_ROWS] = LV_GRID_TEMPLATE_LAST;
    grid_dsc_ready = true;
  }

  if (!st->slot_grid) {
    st->slot_grid = lv_obj_create(st->overlay);
    lv_obj_set_size(st->slot_grid, Device::kScreenWidth, Device::kScreenHeight);
    lv_obj_set_pos(st->slot_grid, 0, 0);
    lv_obj_set_style_bg_opa(st->slot_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st->slot_grid, 0, 0);
    lv_obj_set_style_pad_all(st->slot_grid, GRID_PAD, 0);
    lv_obj_set_style_pad_column(st->slot_grid, GRID_GAP, 0);
    lv_obj_set_style_pad_row(st->slot_grid, GRID_GAP, 0);
    lv_obj_set_layout(st->slot_grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(st->slot_grid, col_dsc, row_dsc);
    lv_obj_remove_flag(st->slot_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(st->slot_grid, LV_OBJ_FLAG_CLICKABLE);
  }

  const TileGridConfig& tile_grid = screensaverConfig.tileGrid();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    Tile tile = tile_grid.tiles[i];
    if (tile.type == TILE_EMPTY) continue;
    lv_obj_t* tile_obj = render_tile(st->slot_grid, tile.col, tile.row, tile,
                                     static_cast<uint8_t>(i),
                                     GridType::SCREENSAVER, g_scene_callback);
    if (!tile_obj) continue;
    const lv_opa_t opacity = tile.background_opacity;
    lv_obj_set_style_bg_opa(tile_obj, opacity,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(tile_obj, opacity,
                            LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_remove_flag(tile_obj, LV_OBJ_FLAG_EVENT_BUBBLE);
  }

  apply_slot_tile_shadows(st);
  apply_slot_tile_borders(st);

  // Match the Web preview: tiles at z=2, freely positioned clock at z=3.
  if (st->clock_box) lv_obj_move_foreground(st->clock_box);
}

int find_config_wallpaper(const String& name, bool enabled_only) {
  if (!name.length()) return -1;
  const auto& wallpapers = screensaverConfig.get().wallpapers;
  for (size_t i = 0; i < wallpapers.size(); ++i) {
    if (wallpapers[i].file_name == name &&
        (!enabled_only || wallpapers[i].enabled)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void clear_live_wallpaper(ScreensaverState* st) {
  if (!st || !st->image) return;
  lv_image_set_src(st->image, nullptr);
  lv_obj_add_flag(st->image, LV_OBJ_FLAG_HIDDEN);
  st->active_wallpaper = -1;
  st->active_wallpaper_name = String();
  st->next_wallpaper_ms = 0;
  // The previous PPA frame resides directly in the panel buffer. Invalidate
  // the entire overlay so LVGL immediately covers it in black and redraws
  // the clock/tiles in the same frame.
  lv_obj_invalidate(st->overlay);
}

void refresh_live_background_and_clock(ScreensaverState* st,
                                       const String& preview_wallpaper) {
  if (!st) return;
  rebuild_global_clock(st);
  apply_slot_tile_shadows(st);
  apply_slot_tile_borders(st);

  const ScreensaverConfigData& config = screensaverConfig.get();
  if (!config.use_wallpapers) {
    clear_live_wallpaper(st);
    return;
  }

  bool allow_disabled = false;
  int desired = -1;
  if (preview_wallpaper.length()) {
    desired = find_config_wallpaper(preview_wallpaper, false);
    allow_disabled = desired >= 0;
  }
  if (desired < 0) {
    desired = find_config_wallpaper(st->active_wallpaper_name, true);
  }
  if (desired < 0) desired = first_enabled_wallpaper();

  if (desired >= 0 && static_cast<size_t>(desired) < config.wallpapers.size()) {
    const ScreensaverWallpaperConfig& wallpaper =
        config.wallpapers[static_cast<size_t>(desired)];
    const bool same_pixels = st->active_wallpaper_name == wallpaper.file_name &&
                             g_cache_dsc && g_cache_name == wallpaper.file_name &&
                             g_cache_w == Device::kScreenWidth &&
                             g_cache_h == Device::kScreenHeight &&
                             g_cache_focus_x == wallpaper.focus_x &&
                             g_cache_focus_y == wallpaper.focus_y &&
                             g_cache_zoom == wallpaper.zoom && g_cache_radius == image_radius() &&
                             !lv_obj_has_flag(st->image, LV_OBJ_FLAG_HIDDEN);
    if (same_pixels) {
      // Clock settings and slide duration may change live without rewriting
      // the unchanged image across the entire screen through PPA.
      st->active_wallpaper = desired;
      st->next_wallpaper_ms = millis() +
          static_cast<uint32_t>(config.duration_seconds) * 1000U;
    } else {
      apply_wallpaper(st, desired, false, allow_disabled);
    }
  } else {
    apply_wallpaper(st, -1, true);
  }

  if (st->clock_box) lv_obj_move_foreground(st->clock_box);
}

void global_screensaver_timer_cb(lv_timer_t* timer) {
  ScreensaverState* st = static_cast<ScreensaverState*>(lv_timer_get_user_data(timer));
  if (!st || st != g_state) return;
  if (g_live_config_refresh_requested) {
    g_live_config_refresh_requested = false;
    String preview = g_live_preview_wallpaper;
    g_live_preview_wallpaper = String();
    refresh_live_background_and_clock(st, preview);
    Serial.println("[Screensaver] Image/clock updated live");
  }
  if (g_live_grid_refresh_requested) {
    g_live_grid_refresh_requested = false;
    rebuild_slot_grid(st);
    refresh_slot_values(st);
    Serial.println("[Screensaver] Tiles updated live");
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - st->next_slot_refresh_ms) >= 0) {
    refresh_slot_values(st);
    st->next_slot_refresh_ms = now + 1000U;
  }
  const auto& config = screensaverConfig.get();
  if (config.use_wallpapers && st->active_wallpaper >= 0 &&
      static_cast<int32_t>(now - st->next_wallpaper_ms) >= 0) {
    const uint32_t since_activity =
        static_cast<uint32_t>(now - displayManager.getLastActivityTime());
    if (since_activity < kInteractionSettleBeforeSlideMs) {
      st->next_wallpaper_ms =
          now + (kInteractionSettleBeforeSlideMs - since_activity);
      return;
    }
    const int next = next_enabled_wallpaper(st->active_wallpaper);
    if (next >= 0 && next != st->active_wallpaper) {
      if (!apply_wallpaper(st, next, false)) {
        // For a deleted or corrupt file, advance the slideshow pointer anyway
        // and retry the following image at a limited rate. Keep the current
        // image visible in the meantime.
        st->active_wallpaper = next;
        st->next_wallpaper_ms = now + 3000U;
      }
    } else {
      st->next_wallpaper_ms = now + 1000U;
    }
  }
}

void on_global_screensaver_clicked(lv_event_t* e) {
  if (g_state && lv_event_get_target(e) == g_state->overlay) {
    hide_image_screensaver();
  }
}

void on_global_overlay_delete(lv_event_t* e) {
  ScreensaverState* st = static_cast<ScreensaverState*>(lv_event_get_user_data(e));
  if (!st) return;
  // The overlay is deleted with lv_obj_delete_async(), so its state must
  // survive until the actual LV_EVENT_DELETE. Deleting it earlier makes
  // the later event access freed memory (0xbaad5678). If LVGL removed the
  // overlay itself, also clear global state and widget references here.
  if (g_state == st) {
    reset_sensor_widgets(GridType::SCREENSAVER);
    reset_switch_widgets(GridType::SCREENSAVER);
    reset_cover_widgets(GridType::SCREENSAVER);
    reset_binary_sensor_widgets(GridType::SCREENSAVER);
    reset_media_widgets(GridType::SCREENSAVER);
    g_state = nullptr;
    restore_configured_display_brightness();
  }
  if (st->timer) lv_timer_delete(st->timer);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (st->composite_storage) {
    free(st->composite_storage);
    st->composite_storage = nullptr;
  }
#endif
  delete st;
}

void global_preload_timer_cb(lv_timer_t*) {
  g_preload_timer = nullptr;
  if (g_state || !g_preload_wallpaper.file_name.length()) return;
  get_or_decode_cached(g_preload_wallpaper,
                       Device::kScreenWidth, Device::kScreenHeight);
}

}  // namespace

void preload_image_screensaver() {
  if (!screensaverConfig.get().use_wallpapers) return;
  ScreensaverWallpaperConfig wallpaper;
  const int index = first_enabled_wallpaper();
  if (index >= 0) wallpaper = screensaverConfig.get().wallpapers[index];
  else if (!find_first_sd_wallpaper(wallpaper)) return;
  g_preload_wallpaper = wallpaper;
  if (g_preload_timer) lv_timer_delete(g_preload_timer);
  g_preload_timer = lv_timer_create(global_preload_timer_cb, 4000, nullptr);
  if (g_preload_timer) lv_timer_set_repeat_count(g_preload_timer, 1);
}

void show_image_screensaver() {
  // Do not build another screensaver while one is already visible. The old
  // overlay is deleted asynchronously; creating a second state during its
  // teardown is unnecessary and obscures widget/cache lifetimes.
  if (g_state || powerManager.isInSleep()) return;
  uiManager.lockProtectedAccess();
  const uint32_t started_ms = millis();
  Serial.printf("[Screensaver] Setup starting | idle=%u ms | dim=%u%%\n",
                static_cast<unsigned>(
                    started_ms - displayManager.getLastActivityTime()),
                static_cast<unsigned>(
                    configManager.getConfig().screensaver_brightness_pct));
  ScreensaverState* st = new ScreensaverState();
  if (!st) return;

#if defined(DEVICE_ESP32_S3_RGB_480)
  // Everything created below becomes visible in one completed RGB frame.
  // Arming this before building the overlay also covers the no-wallpaper and
  // decode-failure paths, which otherwise expose LVGL's partial render bands.
  const bool atomic_show =
      DeviceImpl::displayBeginAtomicFrame("screensaver-show");
#endif

  st->overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(st->overlay, Device::kScreenWidth, Device::kScreenHeight);
  lv_obj_set_pos(st->overlay, 0, 0);
  lv_obj_set_style_bg_color(st->overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(st->overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(st->overlay, 0, 0);
  lv_obj_set_style_pad_all(st->overlay, 0, 0);
  lv_obj_remove_flag(st->overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(st->overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(st->overlay, on_global_screensaver_clicked,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(st->overlay, on_global_overlay_delete,
                      LV_EVENT_DELETE, st);

  st->image = lv_image_create(st->overlay);
  lv_obj_set_pos(st->image, 0, 0);
  lv_obj_remove_flag(st->image, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(st->image, LV_OBJ_FLAG_HIDDEN);

  rebuild_global_clock(st);
  rebuild_slot_grid(st);

  g_state = st;
  g_live_config_refresh_requested = false;
  g_live_grid_refresh_requested = false;
  g_live_preview_wallpaper = String();
  // Apply known entity values to newly created widgets before the first
  // complete PPA frame. Otherwise that first visible frame contains "--"
  // and is corrected only later.
  refresh_slot_values(st);
  const int wallpaper = first_enabled_wallpaper();
  const bool wallpaper_visible = apply_wallpaper(st, wallpaper, true);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  // The wallpaper path already presents the complete overlay through the
  // fast full-frame path. Do the same for the black fallback so a missing SD
  // card never leaves the first clock/tile frame to a slower banded redraw.
  if (!wallpaper_visible) present_composited_screensaver_frame(st);
#endif
  st->next_slot_refresh_ms = millis() + 1000U;
  st->timer = lv_timer_create(global_screensaver_timer_cb, 1000, st);
  apply_configured_screensaver_brightness();
#if defined(DEVICE_ESP32_S3_RGB_480)
  if (atomic_show) lv_obj_invalidate(lv_screen_active());
#endif
  Serial.printf("[Screensaver] Visible after %u ms\n",
                static_cast<unsigned>(millis() - started_ms));
}

void hide_image_screensaver() {
  ScreensaverState* st = g_state;
  if (!st) return;
#if defined(DEVICE_ESP32_S3_RGB_480)
  DeviceImpl::displayBeginAtomicFrame("screensaver-exit");
#endif
  g_state = nullptr;
  g_live_config_refresh_requested = false;
  g_live_grid_refresh_requested = false;
  g_live_preview_wallpaper = String();
  reset_sensor_widgets(GridType::SCREENSAVER);
  reset_switch_widgets(GridType::SCREENSAVER);
  reset_cover_widgets(GridType::SCREENSAVER);
  reset_binary_sensor_widgets(GridType::SCREENSAVER);
  reset_media_widgets(GridType::SCREENSAVER);
  if (st->timer) {
    lv_timer_delete(st->timer);
    st->timer = nullptr;
  }
  if (st->image) lv_image_set_src(st->image, nullptr);
  if (st->overlay) {
    lv_obj_add_flag(st->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(st->overlay);
    // Draw the normal UI completely while the backlight is still at the
    // screensaver level. On the S3 this also completes the armed atomic swap.
    // Only then raise the backlight, so the last screensaver frame never
    // flashes at normal brightness during the transition.
    lv_obj_invalidate(lv_screen_active());
    if (lv_display_t* display = lv_display_get_default()) {
      lv_refr_now(display);
    }
  } else {
    delete st;
  }
  restore_configured_display_brightness();
}

bool is_image_screensaver_visible() {
  return g_state != nullptr;
}

void image_screensaver_brightness_changed() {
  if (!g_state) return;
  apply_configured_screensaver_brightness();
}

void service_image_screensaver_auto(uint32_t last_activity_ms) {
  if (g_state || powerManager.isInSleep()) return;
  const auto& config = configManager.getConfig();
  if (!config.auto_screensaver_enabled || config.auto_screensaver_seconds == 0) return;
  if (static_cast<uint32_t>(millis() - last_activity_ms) >=
      static_cast<uint32_t>(config.auto_screensaver_seconds) * 1000U) {
    show_image_screensaver();
  }
}

void image_screensaver_config_changed(const String& preview_wallpaper) {
  // The HTTP handler only sets flags. The LVGL timer updates the existing
  // overlay synchronously in the LVGL context, avoiding both an overlay
  // replacement and the former async-delete/use-after-free path.
#if defined(DEVICE_ESP32_S3_RGB_480)
  memset(g_wallpaper_retry_after_ms, 0,
         sizeof(g_wallpaper_retry_after_ms));
#endif
  if (g_state) {
    g_live_preview_wallpaper = preview_wallpaper;
    g_live_config_refresh_requested = true;
    if (g_state->timer) lv_timer_ready(g_state->timer);
    return;
  }
  // Do not discard the image cache unconditionally: clock position, formats
  // and slide duration do not change the decoded image. get_or_decode_cached()
  // checks filename/focus/zoom and replaces it only when needed.
  preload_image_screensaver();
}

void image_screensaver_tiles_changed() {
  if (!g_state) return;
  g_live_grid_refresh_requested = true;
  if (g_state->timer) lv_timer_ready(g_state->timer);
}

void image_screensaver_set_scene_callback(void (*callback)(const char*)) {
  g_scene_callback = callback;
}

const Tile* image_screensaver_get_slot_tile(uint8_t index) {
  return screensaverConfig.tile(index);
}

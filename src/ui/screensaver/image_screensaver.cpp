#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/screensaver/screensaver_tile_shadow.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/ui_manager.h"

#include <Arduino.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <lvgl_private.h>
#include <libs/tjpgd/tjpgd.h>
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <driver/jpeg_decode.h>
#include <soc/soc_caps.h>
#endif
#include <string.h>
#include <memory>

// lvgl.h no longer exports lv_image_cache_drop() in 9.5; its declaration
// is available only in the instance header (see tile_renderer.cpp).
#include <misc/cache/instance/lv_image_cache.h>

#include "src/core/display/dma2d_arbiter.h"
#include "src/core/display/jpeg_padded_output.h"
#include "src/ui/screensaver/wallpaper_cover.h"
#include "src/ui/screensaver/composite_over_image.h"
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
#include "src/ui/screensaver/screensaver_places.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
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
// The whole screen, its corners concentric with the tiles one grid gap
// inside (grid_layout::kEdgePad).
uint16_t image_radius() {
  return ui_surface_style::radius(tile_radius::kMinimum) + grid_layout::kEdgePad;
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
  // Rendered slot cards, for icon-and-title tiles that follow their icon
  // colors' source entity. Cleared with every rebuild.
  lv_obj_t* slot_objs[TILES_PER_GRID] = {};
  // Last rule entity payload per slot (the rules reapply on changes only).
  String slot_rule_payloads[TILES_PER_GRID];
  // The grid the slot cards were built from, so an edit rebuilds only the
  // slots that differ (update_slot_grid).
  TileGridConfig* shown_grid = nullptr;
  // The global tile opacity the cards were built with.
  uint8_t built_opacity = 0;
  ~ScreensaverState() { delete shown_grid; }
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
// show_image_screensaver() is building the overlay (image_screensaver_covers_ui).
bool g_opening = false;
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
  const uint32_t width = grid_layout::screen_w();
  const uint32_t height = grid_layout::screen_h();
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

// The wallpaper copied into the frame, LVGL drawing only what lies above it
// (composite_over_image.h); false when LVGL would not draw it as a plain
// copy (the caller takes the snapshot).
bool composite_over_wallpaper(ScreensaverState* st, lv_display_t* display,
                              lv_obj_t* top_layer) {
  return composite_over_image::render(display, top_layer, st->image, g_cache_dsc,
                                      &st->composite_draw_buf);
}

bool present_composited_screensaver_frame(ScreensaverState* st) {
  if (!st || !st->overlay || !ensure_composite_draw_buf(st)) return false;
  lv_display_t* display = lv_obj_get_display(st->overlay);
  lv_obj_t* top_layer = display ? lv_display_get_layer_top(display) : nullptr;
  if (!top_layer) return false;

  const uint32_t snapshot_started = millis();
  // Use the entire top layer intentionally so the clock and tiles remain
  // visible during slide transitions and are presented together.
  if (!composite_over_wallpaper(st, display, top_layer) &&
      lv_snapshot_take_to_draw_buf(top_layer, LV_COLOR_FORMAT_RGB565,
                                   &st->composite_draw_buf) != LV_RESULT_OK) {
    Serial.println("[Screensaver] Composite snapshot failed");
    return false;
  }
  const uint32_t snapshot_ms = millis() - snapshot_started;

  const bool preview_ok = Device::displayTryFullFramePreview(
      0, 0, grid_layout::screen_w(), grid_layout::screen_h(),
      grid_layout::screen_w(),
      reinterpret_cast<const uint16_t*>(st->composite_draw_buf.data),
      st->composite_draw_buf.data_size,
      false);  // The snapshot uses native RGB565, not RGB565_SWAPPED.
  // A slideshow frame needs one atomic swap only. Do not leave the persistent
  // camera mirroring state active while the normal UI resumes underneath it.
  Device::displayEndFullFramePreview();
  Serial.printf("[Screensaver] Composite-Preview %s in %u ms (snapshot %u, present %u)\n",
                preview_ok ? "OK" : "skipped",
                static_cast<unsigned>(millis() - snapshot_started),
                static_cast<unsigned>(snapshot_ms),
                static_cast<unsigned>(millis() - snapshot_started - snapshot_ms));
  if (preview_ok) {
    // The presented frame is the whole top layer as it stands, in the
    // framebuffer LVGL draws its next areas into (every P4 driver's
    // full-frame path writes the active buffer). The areas invalidated while
    // the overlay was built are on screen already: drop them instead of
    // rendering the whole screen a second time in bands (about 600 ms on the
    // V2 after every opening, b303). Later changes invalidate as usual.
    lv_inv_area(display, nullptr);
  }
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
// Hardware decoding like media covers (tile_renderer.cpp): RGB order =
// big-endian RGB565 = LV_COLOR_FORMAT_RGB565_SWAPPED. Any image size: the
// decoder writes whole MCUs and the padding is cut off afterwards
// (jpeg_padded_output.h); a 1280x854 wallpaper took 727 ms in TJpgDec when
// only multiples of 16 used the hardware (V2 b300). An output that does not
// match the expected MCU layout falls back to TJpgDec.
// Returns a malloc buffer of out_w*out_h pixels; the caller must free it.
uint16_t* hw_decode_jpeg(const uint8_t* data, size_t len,
                         uint16_t& out_w, uint16_t& out_h) {
  if (len > UINT32_MAX) return nullptr;

  jpeg_decode_picture_info_t info{};
  esp_err_t err = jpeg_decoder_get_info(data, static_cast<uint32_t>(len), &info);
  if (err != ESP_OK || info.width == 0 || info.height == 0) return nullptr;

  // MCU size of the luma component: 4:2:0 = 16x16, 4:2:2 = 16x8, 4:4:4 and
  // gray = 8x8.
  uint32_t mcu_w = 8, mcu_h = 8;
  if (info.sample_method == JPEG_DOWN_SAMPLING_YUV420) {
    mcu_w = 16;
    mcu_h = 16;
  } else if (info.sample_method == JPEG_DOWN_SAMPLING_YUV422) {
    mcu_w = 16;
  }
  const uint32_t stride = jpeg_padded_output::align_up(info.width, mcu_w);
  const uint32_t rows = jpeg_padded_output::align_up(info.height, mcu_h);
  const uint32_t pixels = stride * rows;
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
  // The decoder reports the size it wrote; another MCU layout than the one
  // assumed above would shift every row.
  if (decoded_bytes != requested_bytes) {
    free(decoded);
    Serial.printf("[Screensaver] HW JPEG layout %ux%u unexpected: bytes=%u/%u, using software fallback\n",
                  static_cast<unsigned>(stride), static_cast<unsigned>(rows),
                  static_cast<unsigned>(decoded_bytes),
                  static_cast<unsigned>(requested_bytes));
    return nullptr;
  }
  jpeg_padded_output::crop_in_place(decoded, stride, info.width, info.height);

  out_w = static_cast<uint16_t>(info.width);
  out_h = static_cast<uint16_t>(info.height);
  return decoded;
}
#endif

// TJpgDec fallback for targets without the JPEG hardware, very large images
// and decoder failures.
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

// The corner coverage and black blend live in wallpaper_cover.h (shared with
// make_cover_dsc and the host test); the S3 direct decoder uses them too.
using wallpaper_cover::blend_swapped_rgb565_with_black;
using wallpaper_cover::rounded_pixel_coverage;

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

  ctx.image_inset = 0;  // The whole screen (image_radius()).
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

// Build the final screen image over the whole screen, its corners the
// configured tile radius plus the tiles' distance to the edge (concentric,
// image_radius()). PPA and LVGL share this full-frame buffer,
// so opening needs neither additional clipping nor rendering in bands.
lv_image_dsc_t* make_cover_dsc(const uint16_t* src, uint16_t src_w,
                               uint16_t src_h, uint16_t target_w,
                               uint16_t target_h, uint16_t focus_x,
                               uint16_t focus_y, uint16_t zoom, uint16_t corner_radius) {
  if (!src || src_w == 0 || src_h == 0 || target_w == 0 || target_h == 0) {
    return nullptr;
  }
  const uint16_t image_inset = 0;
  if (target_w <= image_inset * 2 || target_h <= image_inset * 2) {
    return nullptr;
  }
  const uint16_t image_w = target_w - image_inset * 2;
  const uint16_t image_h = target_h - image_inset * 2;

  wallpaper_cover::Crop crop;
  if (!wallpaper_cover::crop_for(src_w, src_h, image_w, image_h, focus_x, focus_y,
                                 zoom, crop)) {
    return nullptr;
  }

  const size_t bytes = static_cast<size_t>(target_w) * target_h * sizeof(uint16_t);
  // The completed cache can feed PPA directly on the 8-inch device.
  // 64-byte alignment also meets the cache/DMA requirement; the format
  // stays unchanged for LVGL and the other targets.
  uint16_t* out = static_cast<uint16_t*>(alloc_aligned_prefer_psram(bytes));
  if (!out) return nullptr;
  // Every image pixel is written below; only an inset leaves a black frame.
  if (image_inset) memset(out, 0, bytes);

  // The source column per target column, computed once (internal RAM; the
  // per-pixel division is the fallback without it).
  uint16_t* column_map = static_cast<uint16_t*>(
      heap_caps_malloc(static_cast<size_t>(image_w) * sizeof(uint16_t),
                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  // Native RGB565 (to_native), the composite frame's own format: the frame
  // takes the picture with one copy (composite_over_wallpaper).
  wallpaper_cover::cover_pixels(
      src, src_w, crop, out + static_cast<size_t>(image_inset) * target_w + image_inset,
      target_w, image_w, image_h, corner_radius, column_map, true);
  heap_caps_free(column_map);

  lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(malloc(sizeof(lv_image_dsc_t)));
  if (!dsc) {
    free(out);
    return nullptr;
  }
  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = LV_COLOR_FORMAT_RGB565;
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
    Serial.printf("[Screensaver] %s-Decode %ux%u -> %ux%u in %u ms (read %u, decode %u, cover %u)\n",
                  decoder,
                  static_cast<unsigned>(w), static_cast<unsigned>(h),
                  static_cast<unsigned>(target_w), static_cast<unsigned>(target_h),
                  static_cast<unsigned>(millis() - pipeline_started_ms),
                  static_cast<unsigned>(read_ms), static_cast<unsigned>(decode_ms),
                  static_cast<unsigned>(cover_ms));
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

// A slide is shown only when it is checked and its file is still on the card.
// Entries of deleted files stay in the stored list until the next Web Admin
// save; they must neither be shown nor stall the slideshow.
bool wallpaper_usable(const ScreensaverWallpaperConfig& wallpaper) {
  return wallpaper.enabled && sd_wallpaper_file_exists(wallpaper.file_name);
}

bool any_configured_wallpaper_on_card() {
  for (const auto& wallpaper : screensaverConfig.get().wallpapers) {
    if (sd_wallpaper_file_exists(wallpaper.file_name)) return true;
  }
  return false;
}

// The first-image fallback is only for a card whose images were never
// configured or were all deleted since. Unchecked images stay hidden.
bool find_first_sd_wallpaper(ScreensaverWallpaperConfig& out) {
  if (!Device::sdReadyCached() || any_configured_wallpaper_on_card()) {
    return false;
  }
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
    if (wallpaper_usable(wallpapers[i])) return static_cast<int>(i);
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
      if (wallpaper_usable(config.wallpapers[i]) &&
          (static_cast<int>(i) != current || count == 1)) {
        choices[choice_count++] = static_cast<int>(i);
      }
    }
    if (choice_count) return choices[random(choice_count)];
  }
  for (size_t step = 1; step <= count; ++step) {
    const size_t i = (static_cast<size_t>(current < 0 ? 0 : current) + step) % count;
    if (wallpaper_usable(config.wallpapers[i])) return static_cast<int>(i);
  }
  return first_enabled_wallpaper();
}

void position_global_clock(ScreensaverState* st) {
  if (!st || !st->clock_box) return;
  const ScreensaverConfigData& config = screensaverConfig.get();
  const int x = (static_cast<int>(config.clock_x) * grid_layout::screen_w()) / 1000;
  const int y = (static_cast<int>(config.clock_y) * grid_layout::screen_h()) / 1000;
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
                         g_cache_w == grid_layout::screen_w() &&
                         g_cache_h == grid_layout::screen_h() &&
                         g_cache_focus_x == wallpaper.focus_x &&
                         g_cache_focus_y == wallpaper.focus_y &&
                         g_cache_zoom == wallpaper.zoom && g_cache_radius == image_radius();
  lv_image_dsc_t* dsc = get_or_decode_cached(
      wallpaper, grid_layout::screen_w(), grid_layout::screen_h(), st->image);
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
        wallpaper.file_name.c_str(), cache_hit, grid_layout::screen_w(),
        grid_layout::screen_h(), dsc->header.stride);
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
    // Rules follow their entity (own or other) like the tile states below.
    if (tile.icon_colors.length() && st->slot_objs[i]) {
      const String rule_entity = tile_icon_source::rule_entity(tile);
      if (rule_entity.length()) {
        String rule_payload;
        tile_icon_source::cached_payload(rule_entity, rule_payload);
        if (rule_payload != st->slot_rule_payloads[i]) {
          st->slot_rule_payloads[i] = rule_payload;
          tile_icon_source::refresh_card(st->slot_objs[i], tile);
        }
      }
    }
    if (tileTypeHasFixedIconColorOnly(tile.type)) continue;
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
void apply_slot_card_shadow(lv_obj_t* card, bool enabled) {
  if (!card) return;
  const bool visible_bg =
      lv_obj_get_style_bg_opa(card, LV_PART_MAIN) != LV_OPA_TRANSP;
  if (enabled && visible_bg) {
    lv_obj_set_style_shadow_width(card, screensaver_tile_shadow::kWidth, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, screensaver_tile_shadow::kOpa, 0);
    lv_obj_set_style_shadow_spread(card, screensaver_tile_shadow::kSpread, 0);
  } else {
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_TRANSP, 0);
  }
}

void apply_slot_tile_shadows(ScreensaverState* st) {
  if (!st || !st->slot_grid) return;
  const bool enabled = screensaverConfig.get().tile_shadow;
  const uint32_t count = lv_obj_get_child_count(st->slot_grid);
  for (uint32_t i = 0; i < count; ++i) {
    apply_slot_card_shadow(lv_obj_get_child(st->slot_grid, i), enabled);
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

// One slot card as the screensaver shows it: see-through background, veiled
// circles and controls; the caller adds the global shadow and border.
lv_obj_t* build_slot_tile(ScreensaverState* st, size_t i, const Tile& tile) {
  lv_obj_t* tile_obj = render_tile(st->slot_grid, tile.col, tile.row, tile,
                                   static_cast<uint8_t>(i),
                                   GridType::SCREENSAVER, g_scene_callback);
  if (!tile_obj) return nullptr;
  st->slot_objs[i] = tile_obj;
  // One opacity for all screensaver tiles (ScreensaverConfigData).
  const lv_opa_t opacity = screensaverConfig.get().tile_opacity;
  lv_obj_set_style_bg_opa(tile_obj, opacity,
                          LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(tile_obj, opacity,
                          LV_PART_MAIN | LV_STATE_PRESSED);
  // Circles and controls of a see-through tile stay a veil over the
  // wallpaper (tone_color.h); the tile was built opaque.
  if (opacity < LV_OPA_COVER) {
    tile_icon_disc::refresh_fills(tile_obj);
    tile_icon_source::refresh_controls(tile_obj);
  }
  lv_obj_remove_flag(tile_obj, LV_OBJ_FLAG_EVENT_BUBBLE);
  return tile_obj;
}

void remember_shown_grid(ScreensaverState* st) {
  if (!st->shown_grid) st->shown_grid = new (std::nothrow) TileGridConfig();
  if (!st->shown_grid) return;
  // As the active layout shows them (screensaver_places.h).
  *st->shown_grid = screensaverConfig.tileGrid();
  screensaver_places::overlay(*st->shown_grid);
  st->built_opacity = screensaverConfig.get().tile_opacity;
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
  for (lv_obj_t*& obj : st->slot_objs) obj = nullptr;
  for (String& payload : st->slot_rule_payloads) payload = String();

  // Use exactly the normal tile system's tracks, gaps and outer padding.
  // The prepared full-frame image fills the screen, its corners concentric
  // with the tiles one grid gap inside.
  // The shown grid (grid_layout.h): with the head bar its larger cells, an
  // upright layout's half row as a last track half a cell high.
  static lv_coord_t col_dsc[GRID_SPACE_COLS + 1];
  static lv_coord_t row_dsc[GRID_SPACE_ROWS + 1];
  static bool grid_dsc_ready = false;
  if (!grid_dsc_ready) {
    for (uint8_t i = 0; i < GRID_SHOWN_COLS; ++i) col_dsc[i] = GRID_CELL_W;
    col_dsc[GRID_SHOWN_COLS] = LV_GRID_TEMPLATE_LAST;
    for (uint8_t i = 0; i < GRID_SHOWN_ROWS; ++i) row_dsc[i] = GRID_CELL_H;
    uint8_t rows = GRID_SHOWN_ROWS;
    if (grid_layout::shown().half_row) {
      row_dsc[rows++] = tile_geometry::extent(GRID_SHOWN_ROWS, 0.5f, GRID_CELL_H, GRID_GAP);
    }
    row_dsc[rows] = LV_GRID_TEMPLATE_LAST;
    grid_dsc_ready = true;
  }

  if (!st->slot_grid) {
    st->slot_grid = lv_obj_create(st->overlay);
    lv_obj_set_size(st->slot_grid, grid_layout::screen_w(), grid_layout::screen_h());
    lv_obj_set_pos(st->slot_grid, 0, 0);
    lv_obj_set_style_bg_opa(st->slot_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st->slot_grid, 0, 0);
    // Same margins as the tile grid, so screensaver tiles sit where tiles do.
    lv_obj_set_style_pad_left(st->slot_grid, GRID_PAD_LEFT, 0);
    lv_obj_set_style_pad_right(st->slot_grid, GRID_PAD_RIGHT, 0);
    lv_obj_set_style_pad_top(st->slot_grid, GRID_PAD_TOP, 0);
    lv_obj_set_style_pad_bottom(st->slot_grid, GRID_PAD_BOTTOM, 0);
    lv_obj_set_style_pad_column(st->slot_grid, GRID_GAP, 0);
    lv_obj_set_style_pad_row(st->slot_grid, GRID_GAP, 0);
    lv_obj_set_layout(st->slot_grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(st->slot_grid, col_dsc, row_dsc);
    lv_obj_remove_flag(st->slot_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(st->slot_grid, LV_OBJ_FLAG_CLICKABLE);
  }

  // The active layout's places (screensaver_places.h): a bar layout's own,
  // two rows at the bottom of its grid (three upright); a tile without one
  // is left out.
  remember_shown_grid(st);
  if (st->shown_grid) {
    const TileGridConfig& shown = *st->shown_grid;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      const Tile& tile = shown.tiles[i];
      if (tile.type == TILE_EMPTY || tile.layout_hidden) continue;
      build_slot_tile(st, i, tile);
    }
  }

  apply_slot_tile_shadows(st);
  apply_slot_tile_borders(st);

  // Match the Web preview: tiles at z=2, freely positioned clock at z=3.
  if (st->clock_box) lv_obj_move_foreground(st->clock_box);
}

// A Web Admin edit changes one or a few tiles: only changed, moved, new or
// removed slots are built again. Rebuilding every card and redrawing all of
// them with their shadows over the wallpaper made each screensaver edit slow
// (user 2026-10-02), as on the Home grid before b187. False, with nothing
// changed, when the shown grid is unknown; the caller then rebuilds.
bool update_slot_grid(ScreensaverState* st) {
  if (!st || !st->slot_grid || !st->shown_grid) return false;
  uint32_t cards = 0;
  for (lv_obj_t* obj : st->slot_objs) {
    if (obj) ++cards;
  }
  // Only the slot cards live in the grid.
  if (cards != lv_obj_get_child_count(st->slot_grid)) return false;

  const uint32_t started_ms = millis();
  // Both as the active layout shows them (screensaver_places.h).
  std::unique_ptr<TileGridConfig> next_storage(new (std::nothrow) TileGridConfig(screensaverConfig.tileGrid()));
  if (!next_storage) return false;
  screensaver_places::overlay(*next_storage);
  const TileGridConfig& next = *next_storage;
  const TileGridConfig& shown = *st->shown_grid;
  auto visible = [](const Tile& tile) { return tile.type != TILE_EMPTY && !tile.layout_hidden; };
  const bool shadows = screensaverConfig.get().tile_shadow;
  const bool borders = screensaverConfig.get().tile_border;
  unsigned rebuilt = 0;
  uint32_t position = 0;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& before = shown.tiles[i];
    const Tile& after = next.tiles[i];
    if (!visible(before) && !visible(after)) continue;
    if (visible(after) && st->slot_objs[i] &&
        tileContentEquals(before, after) && before.col == after.col &&
        before.row == after.row) {
      ++position;
      continue;
    }
    // Deleting a card marks its old area, shadow included, for the redraw.
    if (st->slot_objs[i]) lv_obj_delete(st->slot_objs[i]);
    st->slot_objs[i] = nullptr;
    const uint8_t slot = static_cast<uint8_t>(i);
    reset_sensor_widget(GridType::SCREENSAVER, slot);
    reset_switch_widget(GridType::SCREENSAVER, slot);
    reset_cover_widget(GridType::SCREENSAVER, slot);
    reset_binary_sensor_widget(GridType::SCREENSAVER, slot);
    reset_media_widget(GridType::SCREENSAVER, slot);
    st->slot_payloads[i] = String();
    st->slot_units[i] = String();
    st->slot_rule_payloads[i] = String();
    ++rebuilt;
    if (!visible(after)) continue;
    lv_obj_t* card = build_slot_tile(st, i, after);
    if (!card) continue;
    apply_slot_card_shadow(card, shadows);
    ui_surface_style::apply_tile_border(card, borders);
    // Slot order, as after a full rebuild: a later card's shadow lies over
    // its neighbours. lv_obj_move_to_index invalidates the parent, here the
    // full-screen grid: one edit redrew the whole screen with the wallpaper,
    // clock and every shadow (b196 log: 1.08 M pixels, 730 ms). Only the card
    // is drawn again.
    lv_display_t* display = lv_obj_get_display(card);
    const bool invalidation = display && lv_display_is_invalidation_enabled(display);
    if (invalidation) lv_display_enable_invalidation(display, false);
    lv_obj_move_to_index(card, static_cast<int32_t>(position++));
    if (invalidation) {
      lv_display_enable_invalidation(display, true);
      lv_obj_invalidate(card);
    }
  }
  remember_shown_grid(st);
  if (rebuilt) refresh_slot_values(st);
  Serial.printf("[Screensaver] Tiles updated: %u rebuilt in %lu ms\n", rebuilt,
                static_cast<unsigned long>(millis() - started_ms));
  return true;
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
  // A new tile opacity rebuilds the cards: their circles and controls are a
  // veil only on see-through cards (build_slot_tile).
  if (st->built_opacity != screensaverConfig.get().tile_opacity) {
    rebuild_slot_grid(st);
    refresh_slot_values(st);
  }
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
                             g_cache_w == grid_layout::screen_w() &&
                             g_cache_h == grid_layout::screen_h() &&
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
                       grid_layout::screen_w(), grid_layout::screen_h());
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
  // Covers the UI from the overlay's creation on, before g_state is set after
  // the ~0.5 s setup: the camera pill leaves as soon as the overlay appears.
  g_opening = true;

#if defined(DEVICE_ESP32_S3_RGB_480)
  // Everything created below becomes visible in one completed RGB frame.
  // Arming this before building the overlay also covers the no-wallpaper and
  // decode-failure paths, which otherwise expose LVGL's partial render bands.
  const bool atomic_show =
      DeviceImpl::displayBeginAtomicFrame("screensaver-show");
#endif

  st->overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(st->overlay, grid_layout::screen_w(), grid_layout::screen_h());
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
  g_opening = false;
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

bool image_screensaver_covers_ui() {
  return g_opening || g_state != nullptr;
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

bool image_screensaver_show_tiles_now() {
  ScreensaverState* st = g_state;
  if (!st || g_opening || !st->slot_grid) return false;
  const uint32_t started_ms = millis();
  if (!update_slot_grid(st)) {
    rebuild_slot_grid(st);
    refresh_slot_values(st);
  }
  if (st->clock_box) lv_obj_move_foreground(st->clock_box);
  if (lv_display_t* display = lv_obj_get_display(st->slot_grid)) {
    // One line per edit: what the frame redraws and how long it takes
    // (user 2026-10-02: show=780 ms for one rebuilt tile in 11 ms).
    uint32_t pixels = 0;
    for (uint32_t i = 0; i < display->inv_p; ++i) {
      if (!display->inv_area_joined[i]) pixels += lv_area_get_size(&display->inv_areas[i]);
    }
    const uint16_t areas = display->inv_p;
    const uint32_t draw_started_ms = millis();
    lv_refr_now(display);
    Serial.printf("[Screensaver] Shown: build=%lu ms draw=%lu ms areas=%u pixels=%lu\n",
                  static_cast<unsigned long>(draw_started_ms - started_ms),
                  static_cast<unsigned long>(millis() - draw_started_ms),
                  static_cast<unsigned>(areas), static_cast<unsigned long>(pixels));
  }
  return true;
}

void image_screensaver_set_scene_callback(void (*callback)(const char*)) {
  g_scene_callback = callback;
}

const Tile* image_screensaver_get_slot_tile(uint8_t index) {
  return screensaverConfig.tile(index);
}

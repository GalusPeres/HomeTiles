#include "src/types/pixelanim/renderer.h"

#include <Arduino.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <new>

#include "src/core/memory/psram_budget.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/devices/device.h"
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X)
#include "src/devices/active_device.h"
#endif

// ---------------------------------------------------------------------------
// .panim binary format (little-endian header, then raw frames):
//
//   off  size  field
//   0    4     magic  'P','A','N','1'  (opaque RGB565) or 'P','A','N','2' (RGBA)
//   4    2     width        (uint16)
//   6    2     height       (uint16)
//   8    2     frame_count  (uint16)
//   10   2     frame_ms     (uint16)  per-frame duration in ms (fallback)
//   12   ...   frame_count * width*height * bpp bytes
//                PAN1: bpp=2, each pixel RGB565 big-endian (opaque)
//                PAN2: bpp=4, each pixel R,G,B,A bytes (A=0 -> transparent)
//
// The art is intentionally tiny. The renderer pre-renders each frame with
// nearest-neighbour into an ARGB8888 PSRAM buffer (crisp pixels + per-pixel
// alpha so transparent areas show the tile/dashboard behind). The on-screen
// frame is a plain blit (no runtime scaling).
// ---------------------------------------------------------------------------

namespace {

constexpr char kAnimDir[] = "/animations";
constexpr uint16_t kMaxSide = 128;
constexpr uint16_t kMaxFrames = 64;
constexpr size_t kMaxUpscaledBytes = 5u * 1024u * 1024u;  // PSRAM budget per tile (ARGB)
constexpr size_t kMaxPsramReserveBytes = 8u * 1024u * 1024u;
constexpr uint16_t kHeaderBytes = 12;
constexpr uint16_t kMinZoomPercent = 25;
constexpr uint16_t kMaxZoomPercent = 300;

enum PixelAnimFitMode : uint8_t {
  kFitContain = 0,
  kFitCover = 1,
  kFitStretch = 2
};

struct PixelAnimState {
  lv_obj_t* img = nullptr;
  lv_timer_t* timer = nullptr;
  uint8_t* frames = nullptr;       // PSRAM: frame_count contiguous ARGB8888 frames
  lv_image_dsc_t* dscs = nullptr;  // one descriptor per frame (distinct src ptrs)
  uint16_t frame_count = 0;
  uint16_t cur_frame = 0;
};

void pixelanim_timer_cb(lv_timer_t* t) {
  PixelAnimState* st = static_cast<PixelAnimState*>(lv_timer_get_user_data(t));
  if (!st || !st->img || !st->dscs || st->frame_count < 2) return;
  // Diagnostic: track the actual gap between ticks so a stall shows up as a
  // large delta regardless of cause (blocked lv_timer_handler(), PPA hold,
  // whatever) -- lets us correlate against the bridge-reload log window.
  static uint32_t s_last_tick_ms = 0;
  uint32_t now_ms = millis();
  uint32_t gap_ms = s_last_tick_ms ? (now_ms - s_last_tick_ms) : 0;
  s_last_tick_ms = now_ms;
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X)
  // A media-tile update briefly forces every flush onto the slow CPU-rotate
  // path (see pausePpaFor in tile_renderer.cpp). Swapping our frame into that
  // window would visibly hitch and adds more work to an already-loaded
  // pipeline. Hold the current frame instead; it resumes smoothly once the
  // cooldown clears, which reads better than an uneven stutter.
  if (DeviceImpl::ppaCooldownActive()) {
    if (gap_ms >= 150) {
      Serial.printf("[PixelAnim] tick HELD (ppa cooldown) gap=%ums\n", (unsigned)gap_ms);
    }
    return;
  }
#endif
  if (gap_ms >= 150) {
    Serial.printf("[PixelAnim] tick gap=%ums (advancing anyway)\n", (unsigned)gap_ms);
  }
  st->cur_frame = (st->cur_frame + 1) % st->frame_count;
  // Distinct descriptor per frame -> distinct image-cache key -> no stale frame,
  // using only public API.
  lv_image_set_src(st->img, &st->dscs[st->cur_frame]);
}

void pixelanim_delete_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  PixelAnimState* st = static_cast<PixelAnimState*>(lv_event_get_user_data(e));
  if (!st) return;
  if (st->timer) lv_timer_delete(st->timer);
  if (st->frames) heap_caps_free(st->frames);
  delete[] st->dscs;
  delete st;
}

uint16_t read_u16le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

// Expand a 5/6-bit channel to 8-bit.
inline uint8_t exp5(uint8_t v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }
inline uint8_t exp6(uint8_t v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }

// Decode one source pixel (PAN1: 2 bytes RGB565 BE; PAN2: 4 bytes RGBA) into an
// LVGL ARGB8888 word (0xAARRGGBB, which is B,G,R,A in little-endian memory).
inline uint32_t decode_pixel(const uint8_t* p, bool rgba) {
  if (rgba) {
    return (static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) | static_cast<uint32_t>(p[2]);
  }
  uint16_t c = (static_cast<uint16_t>(p[0]) << 8) | p[1];
  uint8_t r = exp5((c >> 11) & 0x1F);
  uint8_t g = exp6((c >> 5) & 0x3F);
  uint8_t b = exp5(c & 0x1F);
  return 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

PixelAnimFitMode normalize_fit_mode(uint8_t raw) {
  return (raw <= kFitStretch) ? static_cast<PixelAnimFitMode>(raw) : kFitContain;
}

uint16_t normalize_zoom_percent(int32_t raw) {
  if (raw < kMinZoomPercent || raw > kMaxZoomPercent) return 100;
  return static_cast<uint16_t>(raw);
}

const char* fit_mode_name(PixelAnimFitMode mode) {
  switch (mode) {
    case kFitCover: return "cover";
    case kFitStretch: return "stretch";
    case kFitContain:
    default: return "contain";
  }
}

static inline uint16_t clamp_u16(uint32_t v, uint16_t lo, uint16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return static_cast<uint16_t>(v);
}

static inline uint16_t max_u16(uint16_t a, uint16_t b) {
  return (a > b) ? a : b;
}

void compute_display_size(uint16_t src_w, uint16_t src_h,
                          uint16_t box_w, uint16_t box_h,
                          PixelAnimFitMode fit_mode,
                          uint16_t zoom_percent,
                          uint16_t frames,
                          size_t buffer_budget,
                          uint16_t& disp_w,
                          uint16_t& disp_h) {
  box_w = max_u16(box_w, 1);
  box_h = max_u16(box_h, 1);

  if (fit_mode == kFitCover || fit_mode == kFitStretch) {
    disp_w = box_w;
    disp_h = box_h;
  } else {
    const float sx = static_cast<float>(box_w) / static_cast<float>(src_w);
    const float sy = static_cast<float>(box_h) / static_cast<float>(src_h);
    float scale = (sx < sy) ? sx : sy;
    if (scale <= 0.0f) scale = 1.0f;
    scale *= static_cast<float>(zoom_percent) / 100.0f;
    uint32_t raw_w = static_cast<uint32_t>(roundf(static_cast<float>(src_w) * scale));
    uint32_t raw_h = static_cast<uint32_t>(roundf(static_cast<float>(src_h) * scale));
    disp_w = clamp_u16(raw_w, 1, box_w);
    disp_h = clamp_u16(raw_h, 1, box_h);
  }

  const size_t max_frame_px =
      buffer_budget / (static_cast<size_t>(4) * max_u16(frames, 1));
  if (max_frame_px == 0) {
    disp_w = 1;
    disp_h = 1;
    return;
  }

  bool downscaled_for_budget = false;
  while (static_cast<size_t>(disp_w) * disp_h > max_frame_px && (disp_w > 1 || disp_h > 1)) {
    disp_w = max_u16(static_cast<uint16_t>((static_cast<uint32_t>(disp_w) * 15u) / 16u), 1);
    disp_h = max_u16(static_cast<uint16_t>((static_cast<uint32_t>(disp_h) * 15u) / 16u), 1);
    downscaled_for_budget = true;
  }
  if (downscaled_for_budget) {
    Serial.printf("[PixelAnim] display budget reduced target to %ux%u\n", disp_w, disp_h);
  }
}

static inline uint16_t src_index_from_ratio(uint16_t dst, uint16_t dst_size, uint16_t src_size) {
  if (dst_size <= 1) return 0;
  uint32_t v = (static_cast<uint32_t>(dst) * src_size) / dst_size;
  if (v >= src_size) v = src_size - 1;
  return static_cast<uint16_t>(v);
}

void resample_frame_nearest(const uint32_t* src,
                            uint16_t src_w,
                            uint16_t src_h,
                            uint32_t* dst,
                            uint16_t dst_w,
                            uint16_t dst_h,
                            PixelAnimFitMode fit_mode,
                            uint16_t zoom_percent) {
  if (!src || !dst || src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) return;

  if (fit_mode == kFitCover) {
    const float base_x = static_cast<float>(dst_w) / static_cast<float>(src_w);
    const float base_y = static_cast<float>(dst_h) / static_cast<float>(src_h);
    float scale = (base_x > base_y) ? base_x : base_y;
    const uint16_t cover_zoom = (zoom_percent < 100) ? 100 : zoom_percent;
    scale *= static_cast<float>(cover_zoom) / 100.0f;
    if (scale <= 0.0f) scale = 1.0f;

    const float visible_w = static_cast<float>(dst_w) / scale;
    const float visible_h = static_cast<float>(dst_h) / scale;
    const float src_x0 = (static_cast<float>(src_w) - visible_w) * 0.5f;
    const float src_y0 = (static_cast<float>(src_h) - visible_h) * 0.5f;

    for (uint16_t y = 0; y < dst_h; ++y) {
      int sy = static_cast<int>(src_y0 + (static_cast<float>(y) + 0.5f) / scale);
      if (sy < 0) sy = 0;
      if (sy >= src_h) sy = src_h - 1;
      const uint32_t* src_row = src + static_cast<size_t>(sy) * src_w;
      uint32_t* dst_row = dst + static_cast<size_t>(y) * dst_w;
      for (uint16_t x = 0; x < dst_w; ++x) {
        int sx = static_cast<int>(src_x0 + (static_cast<float>(x) + 0.5f) / scale);
        if (sx < 0) sx = 0;
        if (sx >= src_w) sx = src_w - 1;
        dst_row[x] = src_row[sx];
      }
    }
    return;
  }

  for (uint16_t y = 0; y < dst_h; ++y) {
    const uint16_t sy = src_index_from_ratio(y, dst_h, src_h);
    const uint32_t* src_row = src + static_cast<size_t>(sy) * src_w;
    uint32_t* dst_row = dst + static_cast<size_t>(y) * dst_w;
    for (uint16_t x = 0; x < dst_w; ++x) {
      const uint16_t sx = src_index_from_ratio(x, dst_w, src_w);
      dst_row[x] = src_row[sx];
    }
  }
}

// Loads the file, upscales every frame into a fresh PSRAM ARGB8888 buffer and
// fills `st`. Returns false (nothing allocated) on any error.
bool load_panim(const String& file_name, uint16_t avail_w, uint16_t avail_h,
                PixelAnimFitMode fit_mode, uint16_t zoom_percent,
                PixelAnimState& st, uint16_t& out_frame_ms) {
  if (!Device::sdReady()) return false;

  String path = String(kAnimDir) + "/" + file_name;
  File f = Device::sdFS().open(path, FILE_READ);
  if (!f) {
    Serial.printf("[PixelAnim] open fail: '%s'\n", path.c_str());
    return false;
  }

  uint8_t header[kHeaderBytes];
  if (f.read(header, kHeaderBytes) != kHeaderBytes ||
      header[0] != 'P' || header[1] != 'A' || header[2] != 'N' ||
      (header[3] != '1' && header[3] != '2')) {
    Serial.println("[PixelAnim] bad header");
    f.close();
    return false;
  }
  const bool rgba = (header[3] == '2');
  const uint8_t src_bpp = rgba ? 4 : 2;

  const uint16_t w = read_u16le(header + 4);
  const uint16_t h = read_u16le(header + 6);
  const uint16_t frames = read_u16le(header + 8);
  uint16_t frame_ms = read_u16le(header + 10);

  if (w == 0 || h == 0 || w > kMaxSide || h > kMaxSide || frames == 0 || frames > kMaxFrames) {
    Serial.printf("[PixelAnim] bad dims %ux%u frames=%u\n", w, h, frames);
    f.close();
    return false;
  }
  if (frame_ms < 20) frame_ms = 120;
  if (frame_ms > 2000) frame_ms = 2000;

  const size_t native_frame_bytes = static_cast<size_t>(w) * h * src_bpp;
  if (f.size() < static_cast<size_t>(kHeaderBytes) + native_frame_bytes * frames) {
    Serial.println("[PixelAnim] file too short for frame count");
    f.close();
    return false;
  }

  uint16_t disp_w = 1;
  uint16_t disp_h = 1;
  const size_t buffer_budget =
      psram_budget::fractionCapped(kMaxUpscaledBytes);
  compute_display_size(w, h, avail_w, avail_h, fit_mode, zoom_percent,
                       frames, buffer_budget, disp_w, disp_h);
  const size_t frame_px = static_cast<size_t>(disp_w) * disp_h;
  const size_t frame_bytes = frame_px * 4;  // ARGB8888
  const size_t total_bytes = frame_bytes * frames;

  const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  const size_t reserve =
      psram_budget::fractionCapped(kMaxPsramReserveBytes);
  if (psram_largest < total_bytes || psram_free < total_bytes + reserve) {
    Serial.printf("[PixelAnim] PSRAM budget too low (%u bytes needed, free=%u, largest=%u)\n",
                  static_cast<unsigned>(total_bytes),
                  static_cast<unsigned>(psram_free),
                  static_cast<unsigned>(psram_largest));
    f.close();
    return false;
  }

  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    Serial.printf("[PixelAnim] alloc fail (%u bytes)\n", static_cast<unsigned>(total_bytes));
    f.close();
    return false;
  }

  uint32_t* native_frame = static_cast<uint32_t*>(
      heap_caps_malloc(static_cast<size_t>(w) * h * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  bool native_frame_caps_alloc = true;
  if (!native_frame) {
    native_frame = static_cast<uint32_t*>(malloc(static_cast<size_t>(w) * h * 4));
    native_frame_caps_alloc = false;
  }
  if (!native_frame) {
    heap_caps_free(buf);
    f.close();
    return false;
  }

  // Per-row scratch for the native source row (<= 512 bytes).
  uint8_t* native_row = static_cast<uint8_t*>(malloc(static_cast<size_t>(w) * src_bpp));
  if (!native_row) {
    if (native_frame_caps_alloc) heap_caps_free(native_frame);
    else free(native_frame);
    heap_caps_free(buf);
    f.close();
    return false;
  }

  bool ok = true;
  const int row_bytes = static_cast<int>(w) * src_bpp;
  uint32_t* buf32 = reinterpret_cast<uint32_t*>(buf);
  for (uint16_t fr = 0; fr < frames && ok; ++fr) {
    for (uint16_t ny = 0; ny < h && ok; ++ny) {
      if (f.read(native_row, row_bytes) != row_bytes) { ok = false; break; }
      for (uint16_t nx = 0; nx < w; ++nx) {
        native_frame[static_cast<size_t>(ny) * w + nx] =
            decode_pixel(native_row + static_cast<size_t>(nx) * src_bpp, rgba);
      }
    }
    if (ok) {
      uint32_t* slot = buf32 + static_cast<size_t>(fr) * frame_px;
      resample_frame_nearest(native_frame, w, h, slot, disp_w, disp_h, fit_mode, zoom_percent);
    }
  }

  free(native_row);
  if (native_frame_caps_alloc) heap_caps_free(native_frame);
  else free(native_frame);
  f.close();
  if (!ok) {
    heap_caps_free(buf);
    Serial.println("[PixelAnim] frame read failed");
    return false;
  }

  lv_image_dsc_t* dscs = new (std::nothrow) lv_image_dsc_t[frames];
  if (!dscs) {
    heap_caps_free(buf);
    Serial.println("[PixelAnim] dsc alloc fail");
    return false;
  }
  for (uint16_t i = 0; i < frames; ++i) {
    lv_image_dsc_t& d = dscs[i];
    memset(&d, 0, sizeof(d));
    d.header.magic = LV_IMAGE_HEADER_MAGIC;
    d.header.cf = LV_COLOR_FORMAT_ARGB8888;
    d.header.w = disp_w;
    d.header.h = disp_h;
    d.header.stride = disp_w * 4;
    d.data_size = frame_bytes;
    d.data = buf + static_cast<size_t>(i) * frame_bytes;
  }

  st.frames = buf;
  st.dscs = dscs;
  st.frame_count = frames;
  st.cur_frame = 0;

  out_frame_ms = frame_ms;
  Serial.printf("[PixelAnim] '%s' %ux%u x%u %s -> %ux%u %s zoom=%u (%u bytes)\n",
                file_name.c_str(), w, h, frames, rgba ? "rgba" : "rgb565", disp_w, disp_h,
                fit_mode_name(fit_mode), static_cast<unsigned>(zoom_percent),
                static_cast<unsigned>(total_bytes));
  return true;
}

}  // namespace

lv_obj_t* render_pixelanim_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index) {
  (void)index;
  if (!parent) return nullptr;

  lv_obj_t* card = lv_obj_create(parent);
  if (!card) return nullptr;
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  const PixelAnimFitMode fit_mode = normalize_fit_mode(tile.sensor_display_mode);
  const uint16_t zoom_percent = normalize_zoom_percent(tile.sensor_gauge_max);
  const bool edge_to_edge = (fit_mode == kFitCover || fit_mode == kFitStretch);

  // Unset bg_color -> fully transparent: the tile looks like empty space (no visible
  // card), and a transparent sprite shows the dashboard behind it. A real colour
  // fills a normal rounded card that the sprite's alpha blends over.
  if (!tileBgColorIsSet(tile) && !edge_to_edge) {
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(card, 0, 0);
  } else {
    lv_obj_set_style_bg_color(card, lv_color_hex(tileBgColorOrDefault(tile, 0x000000)), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, tile_layout::scale_480(22), 0);
    lv_obj_set_style_clip_corner(card, true, 0);
  }
  set_tile_grid_cell(card, col, row, tile.span_w, tile.span_h);

  const bool has_title = tile.title.length() > 0;
  if (has_title) {
    lv_obj_t* title_lbl = lv_label_create(card);
    if (title_lbl) {
      set_label_style(title_lbl, lv_color_white(),
                      tile_layout::header_title_font());
      lv_obj_set_style_pad_right(title_lbl, tile_layout::scale(28), 0);
      hometiles_title::tile(title_lbl, tile.title.c_str(), true);
      lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT,
                   tile_layout::scale(14), tile_layout::scale(12));
    }
  }

  String file_name = tile.scene_alias;
  file_name.trim();
  if (!file_name.length()) {
    return card;  // no animation chosen yet
  }

  const uint16_t card_w = static_cast<uint16_t>(GRID_CELL_W * tile.span_w + GRID_GAP * (tile.span_w - 1));
  const uint16_t card_h = static_cast<uint16_t>(GRID_CELL_H * tile.span_h + GRID_GAP * (tile.span_h - 1));
  uint16_t avail_w = card_w;
  uint16_t avail_h = card_h;
  int16_t image_y_offset = 0;
  if (edge_to_edge) {
    if (has_title && avail_h > tile_layout::scale_u16(44)) {
      avail_h -= tile_layout::scale_u16(40);
      image_y_offset = tile_layout::scale(20);
    }
  } else {
    if (avail_w > tile_layout::scale_u16(20)) {
      avail_w -= tile_layout::scale_u16(16);
    }
    if (avail_h > tile_layout::scale_u16(20)) {
      avail_h -= tile_layout::scale_u16(has_title ? 40 : 16);
    }
    image_y_offset = has_title ? tile_layout::scale(12) : 0;
  }

  PixelAnimState* st = new PixelAnimState();
  uint16_t frame_ms = 120;
  if (!load_panim(file_name, avail_w, avail_h, fit_mode, zoom_percent, *st, frame_ms)) {
    delete st;
    return card;
  }

  // Speed override: image_slideshow_sec doubles as frames-per-second for this
  // tile type (set by the web speed slider). 0 -> use the file's own timing.
  const uint16_t fps = tile.image_slideshow_sec;
  if (fps >= 1 && fps <= 60) frame_ms = static_cast<uint16_t>(1000 / fps);

  lv_obj_t* img = lv_img_create(card);
  if (!img) {
    heap_caps_free(st->frames);
    delete[] st->dscs;
    delete st;
    return card;
  }
  lv_image_set_src(img, &st->dscs[0]);
  lv_obj_set_size(img, st->dscs[0].header.w, st->dscs[0].header.h);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, image_y_offset);
  lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);

  st->img = img;
  st->timer = (st->frame_count > 1) ? lv_timer_create(pixelanim_timer_cb, frame_ms, st) : nullptr;

  lv_obj_add_event_cb(card, pixelanim_delete_cb, LV_EVENT_DELETE, st);
  return card;
}

#include "src/core/diagnostics/tile_frame_probe.h"

#include <Arduino.h>
#include <esp_cpu.h>
#include <lvgl_private.h>
#include <string.h>

#include "src/fonts/glyph_cache.h"
#include "src/tiles/config/tile_config.h"
#include "src/types/types_registry.h"
#include "src/ui/shared/rounded_corners.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <esp_memory_utils.h>
#include <sdkconfig.h>
#include <soc/cache_reg.h>
#include <soc/soc.h>
#endif

namespace tile_frame_probe {
namespace {

constexpr uint32_t kFrameSharePercent = 40;
constexpr uint32_t kLogIntervalMs = 1000;
constexpr int kShownTiles = 8;

struct TileTime {
  uint32_t start_us;
  uint32_t sum_us;
  uint16_t width;
  uint16_t height;
  uint8_t type;
  lv_obj_t* card;
};

// The texts by font (user 2026-10-28: what in a tile costs the time): each
// label's own drawing summed per font, with its line height and whether its
// glyphs are compressed (unpacked again for every band they cross).
constexpr int kFonts = 8;
struct FontTime {
  const lv_font_t* font;
  uint32_t us;
  uint32_t count;
};
FontTime g_fonts[kFonts] = {};
glyph_cache_stats_t g_glyphs_start = {};
rounded_corners::Stats g_corners_start = {};

void note_font(lv_obj_t* label, uint32_t us) {
  const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  for (FontTime& slot : g_fonts) {
    if (slot.font && slot.font != font) continue;
    slot.font = font;
    slot.us += us;
    ++slot.count;
    return;
  }
}

// 'z' compressed LVGL font, 'c' compressed through the glyph cache, 'p' plain,
// 'x' another kind.
char font_kind(const lv_font_t* font) {
  if (font && reinterpret_cast<const void*>(font->get_glyph_bitmap) ==
                  reinterpret_cast<const void*>(&glyph_cache_bitmap)) {
    return 'c';
  }
  if (!font || reinterpret_cast<const void*>(font->get_glyph_bitmap) !=
                   reinterpret_cast<const void*>(&lv_font_get_bitmap_fmt_txt)) {
    return 'x';
  }
  const lv_font_fmt_txt_dsc_t* dsc = static_cast<const lv_font_fmt_txt_dsc_t*>(font->dsc);
  return dsc && dsc->bitmap_format != LV_FONT_FMT_TXT_PLAIN ? 'z' : 'p';
}

TileTime g_tiles[TILES_PER_GRID];
lv_display_t* g_display = nullptr;
bool g_measuring = false;
uint32_t g_frame_start_us = 0;
uint32_t g_frame_pixels = 0;
uint32_t g_flush_start_us = 0;
uint32_t g_flush_us = 0;
uint32_t g_flushes = 0;
uint32_t g_last_log_ms = 0;

// What the tiles' parts cost: each element's own drawing (its main draw,
// its children apart), summed by kind over the frame.
enum Part : uint8_t { kCard, kLabel, kObj, kImage, kArc, kOther, kParts };
constexpr const char* kPartNames[kParts] = {"card", "label", "obj", "image", "arc", "other"};
uint32_t g_part_us[kParts] = {};
uint32_t g_part_count[kParts] = {};
uint32_t g_part_start_us[kParts] = {};
// The styles LVGL walks for every property of a drawn piece, summed by kind.
uint32_t g_part_styles[kParts] = {};

// Every other logged frame runs without the element and task timing (a lite
// frame, user 2026-10-28): the same page in both shows what the probe itself
// costs. Frames that are not logged are not measured at all.
bool g_lite = true;
// The pixel work of all draw tasks so far in the frame, in CPU cycles.
uint32_t g_task_cycles_total = 0;

// The single elements that cost the most (user 2026-10-28: which elements make
// LVGL's work around the pixels): each watched element's main draws over the
// frame and the pixel work of the tasks drawn inside them.
constexpr int kElements = 256;
struct ElementTime {
  lv_obj_t* obj;
  uint32_t us;
  uint32_t task_cycles;
  uint16_t pieces;
  uint8_t part;
};
ElementTime g_elements[kElements] = {};
uint32_t g_element_start_cycles = 0;

void note_element(lv_obj_t* obj, uint8_t part, uint32_t us, uint32_t task_cycles) {
  uint32_t slot = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(obj)) >> 3) % kElements;
  for (int probe = 0; probe < kElements; ++probe, slot = (slot + 1) % kElements) {
    ElementTime& element = g_elements[slot];
    if (element.obj && element.obj != obj) continue;
    element.obj = obj;
    element.part = part;
    element.us += us;
    element.task_cycles += task_cycles;
    ++element.pieces;
    return;
  }
}

#if defined(CONFIG_IDF_TARGET_ESP32P4)
// The chip's own cache counters (user 2026-10-28: "if too much comes from
// PSRAM it gets slow"): per frame, how often the cores missed the fast L1
// cache and how often the shared 128 KB L2 cache had to read PSRAM or flash.
// L1 bus 0 is core 0 (network), bus 1 core 1 (the loop that draws).
constexpr uint32_t kL1Counters =
    CACHE_L1_IBUS0_CNT_ENA | CACHE_L1_IBUS1_CNT_ENA | CACHE_L1_DBUS0_CNT_ENA | CACHE_L1_DBUS1_CNT_ENA;
constexpr uint32_t kL1Clear =
    CACHE_L1_IBUS0_CNT_CLR | CACHE_L1_IBUS1_CNT_CLR | CACHE_L1_DBUS0_CNT_CLR | CACHE_L1_DBUS1_CNT_CLR;
constexpr uint32_t kL2Counters =
    CACHE_L2_IBUS0_CNT_ENA | CACHE_L2_IBUS1_CNT_ENA | CACHE_L2_DBUS0_CNT_ENA | CACHE_L2_DBUS1_CNT_ENA;
constexpr uint32_t kL2Clear =
    CACHE_L2_IBUS0_CNT_CLR | CACHE_L2_IBUS1_CNT_CLR | CACHE_L2_DBUS0_CNT_CLR | CACHE_L2_DBUS1_CNT_CLR;

struct CacheCounts {
  uint32_t l1i_hit[2], l1i_miss[2], l1d_hit[2], l1d_miss[2];
  uint32_t l2_hit, l2_miss, ext_read, ext_write;
};
CacheCounts g_cache = {};
uint32_t g_frame_areas = 0;

void cache_counters_start() {
  REG_WRITE(CACHE_L1_CACHE_ACS_CNT_CTRL_REG, kL1Counters | kL1Clear);
  REG_WRITE(CACHE_L2_CACHE_ACS_CNT_CTRL_REG, kL2Counters | kL2Clear);
}

void cache_counters_read() {
  g_cache.l1i_hit[0] = REG_READ(CACHE_L1_IBUS0_ACS_HIT_CNT_REG);
  g_cache.l1i_miss[0] = REG_READ(CACHE_L1_IBUS0_ACS_MISS_CNT_REG);
  g_cache.l1i_hit[1] = REG_READ(CACHE_L1_IBUS1_ACS_HIT_CNT_REG);
  g_cache.l1i_miss[1] = REG_READ(CACHE_L1_IBUS1_ACS_MISS_CNT_REG);
  g_cache.l1d_hit[0] = REG_READ(CACHE_L1_DBUS0_ACS_HIT_CNT_REG);
  g_cache.l1d_miss[0] = REG_READ(CACHE_L1_DBUS0_ACS_MISS_CNT_REG);
  g_cache.l1d_hit[1] = REG_READ(CACHE_L1_DBUS1_ACS_HIT_CNT_REG);
  g_cache.l1d_miss[1] = REG_READ(CACHE_L1_DBUS1_ACS_MISS_CNT_REG);
  g_cache.l2_hit = REG_READ(CACHE_L2_IBUS0_ACS_HIT_CNT_REG) + REG_READ(CACHE_L2_IBUS1_ACS_HIT_CNT_REG) +
                   REG_READ(CACHE_L2_DBUS0_ACS_HIT_CNT_REG) + REG_READ(CACHE_L2_DBUS1_ACS_HIT_CNT_REG);
  g_cache.l2_miss = REG_READ(CACHE_L2_IBUS0_ACS_MISS_CNT_REG) + REG_READ(CACHE_L2_IBUS1_ACS_MISS_CNT_REG) +
                    REG_READ(CACHE_L2_DBUS0_ACS_MISS_CNT_REG) + REG_READ(CACHE_L2_DBUS1_ACS_MISS_CNT_REG);
  g_cache.ext_read = REG_READ(CACHE_L2_IBUS0_ACS_NXTLVL_RD_CNT_REG) + REG_READ(CACHE_L2_IBUS1_ACS_NXTLVL_RD_CNT_REG) +
                     REG_READ(CACHE_L2_DBUS0_ACS_NXTLVL_RD_CNT_REG) + REG_READ(CACHE_L2_DBUS1_ACS_NXTLVL_RD_CNT_REG);
  g_cache.ext_write = REG_READ(CACHE_L2_DBUS0_ACS_NXTLVL_WR_CNT_REG) + REG_READ(CACHE_L2_DBUS1_ACS_NXTLVL_WR_CNT_REG);
}

// Misses per thousand accesses.
unsigned long per_mille(uint32_t miss, uint32_t hit) {
  const uint64_t all = static_cast<uint64_t>(miss) + hit;
  return all ? static_cast<unsigned long>(static_cast<uint64_t>(miss) * 1000U / all) : 0;
}

// Where the draw band lives, how many areas the frame drew and what it read
// from slow memory. The band's rows at full screen width come from its size:
// LVGL reshapes the buffer's header to each band it draws.
void log_cache() {
  const lv_draw_buf_t* band = g_display->buf_act;
  const bool band_psram = band && esp_ptr_external_ram(band->data);
  const uint32_t row_bytes = lv_draw_buf_width_to_stride(lv_display_get_horizontal_resolution(g_display),
                                                         lv_display_get_color_format(g_display));
  const uint32_t band_rows = band && row_bytes ? band->data_size / row_bytes : 0;
  Serial.printf(
      "[FrameProbe] cache band=%s %lu lines, %lu areas | draw core L1i miss %lu/%lu (%lu per mille) L1d miss %lu/%lu"
      " (%lu per mille) | network core L1i miss %lu L1d miss %lu | L2 miss %lu/%lu (%lu per mille), PSRAM/flash reads"
      " %lu (%lu KB) writes %lu\n",
      band_psram ? "PSRAM" : "SRAM", static_cast<unsigned long>(band_rows), static_cast<unsigned long>(g_frame_areas),
      static_cast<unsigned long>(g_cache.l1i_miss[1]),
      static_cast<unsigned long>(g_cache.l1i_miss[1] + g_cache.l1i_hit[1]),
      per_mille(g_cache.l1i_miss[1], g_cache.l1i_hit[1]), static_cast<unsigned long>(g_cache.l1d_miss[1]),
      static_cast<unsigned long>(g_cache.l1d_miss[1] + g_cache.l1d_hit[1]),
      per_mille(g_cache.l1d_miss[1], g_cache.l1d_hit[1]), static_cast<unsigned long>(g_cache.l1i_miss[0]),
      static_cast<unsigned long>(g_cache.l1d_miss[0]), static_cast<unsigned long>(g_cache.l2_miss),
      static_cast<unsigned long>(g_cache.l2_miss + g_cache.l2_hit), per_mille(g_cache.l2_miss, g_cache.l2_hit),
      static_cast<unsigned long>(g_cache.ext_read),
      static_cast<unsigned long>(static_cast<uint64_t>(g_cache.ext_read) * CONFIG_CACHE_L2_CACHE_LINE_SIZE / 1024U),
      static_cast<unsigned long>(g_cache.ext_write));
}
#endif

void part_cb(lv_event_t* e) {
  if (!g_measuring || g_lite) return;
  const size_t part = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (part >= kParts) return;
  if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN_BEGIN) {
    g_part_start_us[part] = micros();
    g_element_start_cycles = g_task_cycles_total;
    return;
  }
  if (!g_part_start_us[part]) return;
  const uint32_t us = micros() - g_part_start_us[part];
  g_part_us[part] += us;
  g_part_start_us[part] = 0;
  ++g_part_count[part];
  lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  g_part_styles[part] += obj->style_cnt;
  if (part == kLabel) note_font(obj, us);
  note_element(obj, static_cast<uint8_t>(part), us, g_task_cycles_total - g_element_start_cycles);
}

// What LVGL's software renderer spends on each kind of draw task (fill,
// border, text, image ...) and for which tile part (user 2026-10-28: is a
// card slow because of its pixels or of everything around them): the pixel
// work, timed around the renderer's dispatch with the CPU cycle counter. The
// rest of a frame is walking the widgets with their events and styles, and
// the flush.
constexpr int kTaskTypes = LV_DRAW_TASK_TYPE_BLUR + 1;
constexpr const char* kTaskNames[kTaskTypes] = {"none", "fill",  "border",   "shadow", "letter",
                                                "label", "image", "layer", "line",   "arc",
                                                "tri", "mask", "mask_bitmap", "blur"};
constexpr int kTaskKinds = kParts + 1;  // the tile parts, then everything else (head bar, screen ...)
struct TaskTime {
  uint32_t cycles;
  uint32_t count;
};
TaskTime g_tasks[kTaskKinds][kTaskTypes] = {};
int32_t (*g_sw_dispatch)(lv_draw_unit_t*, lv_layer_t*) = nullptr;

// The tile part a draw task belongs to: the part its widget is watched as.
size_t task_kind(const lv_draw_task_t* task) {
  const lv_draw_dsc_base_t* base = static_cast<const lv_draw_dsc_base_t*>(task->draw_dsc);
  lv_obj_t* obj = base ? base->obj : nullptr;
  if (!obj) return kParts;
  const uint32_t events = lv_obj_get_event_count(obj);
  for (uint32_t i = 0; i < events; ++i) {
    lv_event_dsc_t* dsc = lv_obj_get_event_dsc(obj, i);
    if (dsc && lv_event_dsc_get_cb(dsc) == part_cb) {
      const size_t part = reinterpret_cast<uintptr_t>(lv_event_dsc_get_user_data(dsc));
      return part < kParts ? part : kParts;
    }
  }
  return kParts;
}

// With one draw unit (LV_USE_OS none) LVGL hands it the layer's first waiting
// task and draws it before dispatch returns.
int32_t timed_dispatch(lv_draw_unit_t* unit, lv_layer_t* layer) {
  if (!g_measuring || g_lite) return g_sw_dispatch(unit, layer);
  lv_draw_task_t* task = lv_draw_get_available_task(layer, nullptr, 0);
  const size_t kind = task ? task_kind(task) : kParts;
  const int type = task && task->type < kTaskTypes ? task->type : 0;
  const uint32_t start = esp_cpu_get_cycle_count();
  const int32_t taken = g_sw_dispatch(unit, layer);
  const uint32_t cycles = esp_cpu_get_cycle_count() - start;
  g_task_cycles_total += cycles;
  if (taken == 1 && task) {
    TaskTime& slot = g_tasks[kind][type];
    slot.cycles += cycles;
    ++slot.count;
  }
  return taken;
}

void time_draw_tasks() {
  lv_draw_unit_t* unit = LV_GLOBAL_DEFAULT()->draw_info.unit_head;
  if (g_sw_dispatch || !unit || unit->next || !unit->dispatch_cb) {
    Serial.printf("[FrameProbe] draw tasks not timed: %lu draw units\n",
                  static_cast<unsigned long>(LV_GLOBAL_DEFAULT()->draw_info.unit_cnt));
    return;
  }
  g_sw_dispatch = unit->dispatch_cb;
  unit->dispatch_cb = timed_dispatch;
}

Part part_of(lv_obj_t* obj) {
  const lv_obj_class_t* cls = lv_obj_get_class(obj);
  if (cls == &lv_label_class) return kLabel;
  if (cls == &lv_image_class) return kImage;
  if (cls == &lv_arc_class) return kArc;
  if (cls == &lv_obj_class) return kObj;
  return kOther;
}

void watch_part(lv_obj_t* obj, Part part) {
  void* data = reinterpret_cast<void*>(static_cast<uintptr_t>(part));
  lv_obj_add_event_cb(obj, part_cb, LV_EVENT_DRAW_MAIN_BEGIN, data);
  lv_obj_add_event_cb(obj, part_cb, LV_EVENT_DRAW_MAIN_END, data);
}

void watch_children(lv_obj_t* parent) {
  const uint32_t count = lv_obj_get_child_count(parent);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(parent, i);
    watch_part(child, part_of(child));
    watch_children(child);
  }
}

// One card's drawing in one band: from its main draw to the end of its
// children and post draw.
void tile_cb(lv_event_t* e) {
  if (!g_measuring || g_lite) return;
  const uintptr_t packed = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  const size_t index = packed & 0xFF;
  if (index >= TILES_PER_GRID) return;
  TileTime& tile = g_tiles[index];
  if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN_BEGIN) {
    tile.start_us = micros();
    return;
  }
  if (!tile.start_us) return;
  tile.sum_us += micros() - tile.start_us;
  tile.start_us = 0;
  lv_obj_t* card = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  tile.card = card;
  tile.type = static_cast<uint8_t>(packed >> 8);
  tile.width = static_cast<uint16_t>(lv_obj_get_width(card));
  tile.height = static_cast<uint16_t>(lv_obj_get_height(card));
}

void log_frame(uint32_t frame_us) {
  uint32_t tiles_us = 0;
  for (const TileTime& tile : g_tiles) tiles_us += tile.sum_us;
  char line[640];
  int used = snprintf(line, sizeof(line),
                      "[FrameProbe] frame %.1f ms px=%lu flushes=%lu flush=%.1f ms tiles=%.1f ms rest=%.1f ms |",
                      frame_us / 1000.0f, static_cast<unsigned long>(g_frame_pixels),
                      static_cast<unsigned long>(g_flushes), g_flush_us / 1000.0f, tiles_us / 1000.0f,
                      (frame_us > tiles_us + g_flush_us ? frame_us - tiles_us - g_flush_us : 0) / 1000.0f);
  bool shown[TILES_PER_GRID] = {};
  for (int rank = 0; rank < kShownTiles && used > 0 && used < static_cast<int>(sizeof(line)); ++rank) {
    int best = -1;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      if (shown[i] || !g_tiles[i].sum_us) continue;
      if (best < 0 || g_tiles[i].sum_us > g_tiles[best].sum_us) best = static_cast<int>(i);
    }
    if (best < 0) break;
    shown[best] = true;
    const TileTime& tile = g_tiles[best];
    const char* kind = get_tile_type_preview_kind(static_cast<TileType>(tile.type));
    // Several types share a preview kind ("none": scene, folder, animation,
    // settings, back): the type number follows it.
    used += snprintf(line + used, sizeof(line) - used, " t%d %s/%u %ux%u %.1f", best, kind ? kind : "?",
                     static_cast<unsigned>(tile.type), static_cast<unsigned>(tile.width),
                     static_cast<unsigned>(tile.height), tile.sum_us / 1000.0f);
  }
  Serial.println(line);
  // The parts, each kind as its time and how many elements were drawn
  // (per band: an element over three bands counts three times).
  used = snprintf(line, sizeof(line), "[FrameProbe] parts");
  for (size_t part = 0; part < kParts && used > 0 && used < static_cast<int>(sizeof(line)); ++part) {
    used += snprintf(line + used, sizeof(line) - used, " %s=%.1f ms/%lu", kPartNames[part], g_part_us[part] / 1000.0f,
                     static_cast<unsigned long>(g_part_count[part]));
  }
  Serial.println(line);
  // How many styles each kind carries on average (LVGL looks every property
  // up in all of them, in every band).
  used = snprintf(line, sizeof(line), "[FrameProbe] styles per piece");
  for (size_t part = 0; part < kParts && used > 0 && used < static_cast<int>(sizeof(line)); ++part) {
    if (!g_part_count[part]) continue;
    used += snprintf(line + used, sizeof(line) - used, " %s=%.1f", kPartNames[part],
                     static_cast<float>(g_part_styles[part]) / static_cast<float>(g_part_count[part]));
  }
  Serial.println(line);
  // The texts by font, the most expensive first: line height, kind, time and
  // label pieces drawn.
  used = snprintf(line, sizeof(line), "[FrameProbe] fonts");
  bool font_shown[kFonts] = {};
  for (int rank = 0; rank < kFonts && used > 0 && used < static_cast<int>(sizeof(line)); ++rank) {
    int best = -1;
    for (int i = 0; i < kFonts; ++i) {
      if (font_shown[i] || !g_fonts[i].font) continue;
      if (best < 0 || g_fonts[i].us > g_fonts[best].us) best = i;
    }
    if (best < 0) break;
    font_shown[best] = true;
    const FontTime& font = g_fonts[best];
    used += snprintf(line + used, sizeof(line) - used, " h%ld%c=%.1f ms/%lu",
                     static_cast<long>(lv_font_get_line_height(font.font)), font_kind(font.font), font.us / 1000.0f,
                     static_cast<unsigned long>(font.count));
  }
  // The icon glyphs of this frame: copied from the cache or unpacked.
  const glyph_cache_stats_t glyphs = glyph_cache_stats();
  if (used > 0 && used < static_cast<int>(sizeof(line))) {
    snprintf(line + used, sizeof(line) - used, " | glyph cache hits=%lu unpacked=%lu clears=%lu kept=%lu (%lu KB)%s",
             static_cast<unsigned long>(glyphs.hits - g_glyphs_start.hits),
             static_cast<unsigned long>(glyphs.misses - g_glyphs_start.misses),
             static_cast<unsigned long>(glyphs.clears - g_glyphs_start.clears),
             static_cast<unsigned long>(glyphs.glyphs), static_cast<unsigned long>(glyphs.bytes / 1024),
             glyphs.off ? " off" : "");
  }
  Serial.println(line);
  // What the most expensive tile's card draws in each band: the style parts
  // behind a rounded card and how many styles and event handlers LVGL walks
  // for every draw event.
  int slowest = -1;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (g_tiles[i].card && (slowest < 0 || g_tiles[i].sum_us > g_tiles[slowest].sum_us)) slowest = static_cast<int>(i);
  }
  if (slowest < 0) return;
  lv_obj_t* card = g_tiles[slowest].card;
  // What resolving the card's styles costs LVGL for each piece it draws: the
  // rectangle descriptor built from its styles, timed over a few rounds after
  // the frame (once per logged frame, not while drawing).
  constexpr int kStyleRounds = 20;
  const uint32_t style_start_us = micros();
  for (int i = 0; i < kStyleRounds; ++i) {
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    lv_obj_init_draw_rect_dsc(card, LV_PART_MAIN, &rect);
  }
  const float style_us = static_cast<float>(micros() - style_start_us) / kStyleRounds;
  Serial.printf(
      "[FrameProbe] card t%d radius=%ld bg_opa=%u grad=%u outline=%ld/%u border=%ld/%u post=%u shadow=%ld image=%u"
      " styles=%u events=%lu children=%lu rect_dsc=%.1f us\n",
      slowest, static_cast<long>(lv_obj_get_style_radius(card, LV_PART_MAIN)),
      static_cast<unsigned>(lv_obj_get_style_bg_opa(card, LV_PART_MAIN)),
      static_cast<unsigned>(lv_obj_get_style_bg_grad_dir(card, LV_PART_MAIN)),
      static_cast<long>(lv_obj_get_style_outline_width(card, LV_PART_MAIN)),
      static_cast<unsigned>(lv_obj_get_style_outline_opa(card, LV_PART_MAIN)),
      static_cast<long>(lv_obj_get_style_border_width(card, LV_PART_MAIN)),
      static_cast<unsigned>(lv_obj_get_style_border_opa(card, LV_PART_MAIN)),
      static_cast<unsigned>(lv_obj_get_style_border_post(card, LV_PART_MAIN)),
      static_cast<long>(lv_obj_get_style_shadow_width(card, LV_PART_MAIN)),
      static_cast<unsigned>(lv_obj_get_style_bg_image_src(card, LV_PART_MAIN) != nullptr),
      static_cast<unsigned>(card->style_cnt), static_cast<unsigned long>(lv_obj_get_event_count(card)),
      static_cast<unsigned long>(lv_obj_get_child_count(card)), style_us);
}

// The renderer's pixel work by tile part and task kind (part.task=ms/tasks),
// then the time inside it and outside of it and the flush.
void log_tasks(uint32_t frame_us) {
  if (!g_sw_dispatch) return;
  const float cycles_per_ms = getCpuFrequencyMhz() * 1000.0f;
  char line[640];
  int used = snprintf(line, sizeof(line), "[FrameProbe] tasks");
  uint64_t inside = 0;
  for (int kind = 0; kind < kTaskKinds; ++kind) {
    for (int type = 0; type < kTaskTypes; ++type) {
      const TaskTime& slot = g_tasks[kind][type];
      if (!slot.count) continue;
      inside += slot.cycles;
      if (used <= 0 || used >= static_cast<int>(sizeof(line))) continue;
      used += snprintf(line + used, sizeof(line) - used, " %s.%s=%.1f/%lu", kind < kParts ? kPartNames[kind] : "rest",
                       kTaskNames[type], slot.cycles / cycles_per_ms, static_cast<unsigned long>(slot.count));
    }
  }
  const float inside_ms = inside / cycles_per_ms;
  const float outside_ms = frame_us / 1000.0f - g_flush_us / 1000.0f - inside_ms;
  if (used > 0 && used < static_cast<int>(sizeof(line))) {
    used += snprintf(line + used, sizeof(line) - used, " | pixels=%.1f ms widgets=%.1f ms flush=%.1f ms", inside_ms,
                     outside_ms > 0 ? outside_ms : 0.0f, g_flush_us / 1000.0f);
  }
  // Fills and lines drawn from kept corners in this frame (rounded_corners.h).
  const rounded_corners::Stats corners = rounded_corners::stats();
  if (used > 0 && used < static_cast<int>(sizeof(line))) {
    snprintf(line + used, sizeof(line) - used, " | corners fills=%lu borders=%lu sets=%lu (%lu KB) built=%lu declined=%lu",
             static_cast<unsigned long>(corners.fills - g_corners_start.fills),
             static_cast<unsigned long>(corners.borders - g_corners_start.borders),
             static_cast<unsigned long>(corners.masks), static_cast<unsigned long>(corners.bytes / 1024),
             static_cast<unsigned long>(corners.built), static_cast<unsigned long>(corners.declined));
  }
  Serial.println(line);
}

// The tile an element belongs to: the index its card is watched with.
int tile_of(lv_obj_t* obj) {
  for (int depth = 0; obj && depth < 8; ++depth, obj = lv_obj_get_parent(obj)) {
    const uint32_t events = lv_obj_get_event_count(obj);
    for (uint32_t i = 0; i < events; ++i) {
      lv_event_dsc_t* dsc = lv_obj_get_event_dsc(obj, i);
      if (dsc && lv_event_dsc_get_cb(dsc) == tile_cb) {
        return static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_dsc_get_user_data(dsc)) & 0xFF);
      }
    }
  }
  return -1;
}

// The most expensive single elements: tile, kind, size, styles, event
// handlers, pieces drawn, then time / pixel work of its tasks in ms.
void log_elements() {
  const float cycles_per_ms = getCpuFrequencyMhz() * 1000.0f;
  char line[640];
  int used = snprintf(line, sizeof(line), "[FrameProbe] top elements");
  bool shown[kElements] = {};
  for (int rank = 0; rank < 8 && used > 0 && used < static_cast<int>(sizeof(line)); ++rank) {
    int best = -1;
    for (int i = 0; i < kElements; ++i) {
      if (shown[i] || !g_elements[i].obj) continue;
      if (best < 0 || g_elements[i].us > g_elements[best].us) best = i;
    }
    if (best < 0) break;
    shown[best] = true;
    const ElementTime& element = g_elements[best];
    used += snprintf(line + used, sizeof(line) - used, " t%d.%s %ldx%ld s%u e%lu n%u %.2f/%.2f", tile_of(element.obj),
                     element.part < kParts ? kPartNames[element.part] : "?",
                     static_cast<long>(lv_obj_get_width(element.obj)), static_cast<long>(lv_obj_get_height(element.obj)),
                     static_cast<unsigned>(element.obj->style_cnt),
                     static_cast<unsigned long>(lv_obj_get_event_count(element.obj)),
                     static_cast<unsigned>(element.pieces), element.us / 1000.0f, element.task_cycles / cycles_per_ms);
  }
  Serial.println(line);
}

// A frame is measured when its dirty areas cover at least kFrameSharePercent
// of the screen (a page turn, a popup closing, a theme change).
void display_cb(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_REFR_START) {
    const uint32_t screen = static_cast<uint32_t>(lv_display_get_horizontal_resolution(g_display)) *
                            static_cast<uint32_t>(lv_display_get_vertical_resolution(g_display));
    uint32_t pixels = 0;
    for (uint32_t i = 0; i < g_display->inv_p; ++i) pixels += lv_area_get_size(&g_display->inv_areas[i]);
    if (pixels > screen) pixels = screen;
    g_measuring = screen && pixels * 100U >= screen * kFrameSharePercent;
    // Only a frame that will be logged is measured.
    const uint32_t start_ms = millis();
    if (g_last_log_ms && start_ms - g_last_log_ms < kLogIntervalMs) g_measuring = false;
    if (!g_measuring) return;
    g_lite = !g_lite;
    g_task_cycles_total = 0;
    memset(g_elements, 0, sizeof(g_elements));
    memset(g_tiles, 0, sizeof(g_tiles));
    memset(g_part_us, 0, sizeof(g_part_us));
    memset(g_part_count, 0, sizeof(g_part_count));
    memset(g_part_start_us, 0, sizeof(g_part_start_us));
    memset(g_fonts, 0, sizeof(g_fonts));
    memset(g_part_styles, 0, sizeof(g_part_styles));
    memset(g_tasks, 0, sizeof(g_tasks));
    g_glyphs_start = glyph_cache_stats();
    g_corners_start = rounded_corners::stats();
    g_frame_pixels = pixels;
    g_flush_us = 0;
    g_flushes = 0;
    g_frame_start_us = micros();
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    g_frame_areas = g_display->inv_p;
    cache_counters_start();
#endif
    return;
  }
  if (!g_measuring) return;
  if (code == LV_EVENT_FLUSH_START) {
    g_flush_start_us = micros();
  } else if (code == LV_EVENT_FLUSH_FINISH) {
    g_flush_us += micros() - g_flush_start_us;
    ++g_flushes;
  } else if (code == LV_EVENT_REFR_READY) {
    g_measuring = false;
    const uint32_t frame_us = micros() - g_frame_start_us;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    // Read before anything is logged, so the log line is not counted.
    cache_counters_read();
#endif
    const uint32_t now = millis();
    if (g_last_log_ms && now - g_last_log_ms < kLogIntervalMs) return;
    g_last_log_ms = now;
    if (g_lite) {
      Serial.printf("[FrameProbe] frame %.1f ms px=%lu flushes=%lu flush=%.1f ms | lite (no element timing)\n",
                    frame_us / 1000.0f, static_cast<unsigned long>(g_frame_pixels),
                    static_cast<unsigned long>(g_flushes), g_flush_us / 1000.0f);
      return;
    }
    log_frame(frame_us);
    log_tasks(frame_us);
    log_elements();
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    log_cache();
#endif
  }
}

}  // namespace

void watch(lv_obj_t* card, uint8_t index, uint8_t type) {
  if (!card || index >= TILES_PER_GRID) return;
  if (!g_display) {
    g_display = lv_obj_get_display(card);
    if (!g_display) return;
    for (const lv_event_code_t code :
         {LV_EVENT_REFR_START, LV_EVENT_FLUSH_START, LV_EVENT_FLUSH_FINISH, LV_EVENT_REFR_READY}) {
      lv_display_add_event_cb(g_display, display_cb, code, nullptr);
    }
    time_draw_tasks();
  }
  void* packed = reinterpret_cast<void*>(static_cast<uintptr_t>(index) | (static_cast<uintptr_t>(type) << 8));
  lv_obj_add_event_cb(card, tile_cb, LV_EVENT_DRAW_MAIN_BEGIN, packed);
  lv_obj_add_event_cb(card, tile_cb, LV_EVENT_DRAW_POST_END, packed);
  // Its parts as built now (an element made later is not counted).
  watch_part(card, kCard);
  watch_children(card);
}

}  // namespace tile_frame_probe

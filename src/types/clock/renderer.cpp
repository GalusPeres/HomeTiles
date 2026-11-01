#include "src/ui/shared/ui_surface_style.h"
#include "src/types/clock/renderer.h"
#include "src/types/clock/clock_format.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/shared/tone_color.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/fonts/ui_fonts.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/types/clock/clock_shadow_paint.h"
#include <Arduino.h>
#include <lvgl_private.h>
#include <misc/cache/instance/lv_image_cache.h>
#include <new>
#include <time.h>
#include "src/ui/shared/ui_theme.h"
#include "src/ui/shared/tabular_digits.h"
#include "src/types/clock/clock_face.h"

static uint8_t layout_clock_font_size(uint8_t size) {
#if defined(DEVICE_LAYOUT_1024X600)
  switch (size) {
    case 20: return 16;
    case 24: return 20;
    case 28: return 24;
    case 32: return 28;
    case 40: return 32;
    case 48: return 40;
    case 56: return 48;
    case 64: return 56;
    case 72: return 56;
    case 80: return 64;
    case 96: return 80;
    default: return size;
  }
#elif defined(DEVICE_LAYOUT_480X480)
  switch (size) {
    case 20: return 14;
    case 24: return 16;
    case 28: return 20;
    case 32: return 20;
    case 40: return 28;
    case 48: return 32;
    case 56: return 40;
    case 64: return 40;
    case 72: return 48;
    case 80: return 56;
    case 96: return 64;
    default: return size;
  }
#else
  return size;
#endif
}

static uint8_t resolve_clock_time_format(const Tile& tile) {
  const DeviceConfig& cfg = configManager.getConfig();
  return clock_tile::resolve_time_format(tile.sensor_gauge_min, cfg.global_time_format, cfg.language);
}

static uint8_t resolve_clock_date_format(const Tile& tile) {
  const DeviceConfig& cfg = configManager.getConfig();
  return clock_tile::resolve_date_format(clock_tile::unpack_date_options(tile.sensor_gauge_max).date_format,
                                         cfg.global_date_format, cfg.language);
}

static uint8_t normalize_clock_alignment(uint8_t raw) {
  return raw <= 2 ? raw : 1;
}

static lv_text_align_t clock_text_align(uint8_t raw) {
  switch (normalize_clock_alignment(raw)) {
    case 0:
      return LV_TEXT_ALIGN_LEFT;
    case 2:
      return LV_TEXT_ALIGN_RIGHT;
    default:
      return LV_TEXT_ALIGN_CENTER;
  }
}

// Retain the soft shadow from the stable screensaver: nine faint copies
// approximate a smooth blur. A later expansion to 21 copies was reverted
// because of the additional PPA load.
static constexpr uint8_t kClockShadowCopies = clock_shadow_paint::kCopies;

// The copies' offsets and opacity.
using ClockShadowCopy = clock_shadow_paint::Copy;
static void clock_shadow_copies(ClockShadowCopy (&copies)[kClockShadowCopies]) {
  const int16_t a = tile_layout::scale(2), b = tile_layout::scale(4), c = tile_layout::scale(6);
  const ClockShadowCopy table[kClockShadowCopies] = {
      {b, b, 34}, {a, b, 14}, {c, b, 14}, {b, a, 14}, {b, c, 14},
      {a, a, 8},  {c, a, 8},  {a, c, 8},  {c, c, 8},
  };
  memcpy(copies, table, sizeof(table));
}

// Each copy was a label of the large compressed clock font: every frame
// unpacked its glyphs ten times (about 110 ms of each screensaver opening on
// the V2, b304). The line's text is now drawn once into an A8 mask and the
// nine copies are copies of it in black at their offset and opacity, drawn
// by one object (clock_shadow_paint.h) with LVGL's own A8 blend: they darken
// exactly as before (also the per-copy rounding of RGB565 panels, which
// makes the shadow darker than its opacities; one composed picture came out
// visibly lighter, b306 host test). The masks of the last lines are kept
// across openings: the screensaver builds its clock anew for each opening,
// and an unchanged line's mask is copied instead of drawn.
struct ClockShadowCacheEntry {
  const lv_font_t* font = nullptr;
  lv_coord_t width = 0;
  lv_coord_t height = 0;
  uint8_t alignment = 0;
  char text[48] = "";
  int32_t ext = 0;
  lv_draw_buf_t* mask = nullptr;
  uint32_t used = 0;
};
static ClockShadowCacheEntry g_clock_shadow_cache[2];
static uint32_t g_clock_shadow_cache_tick = 0;

// The hidden mask label's text as an A8 mask; the caller owns the returned
// copy, nullptr when LVGL memory is short. ext_out: the label's extra draw
// size, by which the mask reaches beyond the label on every side.
static lv_draw_buf_t* clock_shadow_mask(lv_obj_t* mask_label, const lv_font_t* font,
                                        uint8_t alignment, int32_t& ext_out) {
  const char* text = lv_label_get_text(mask_label);
  const lv_coord_t width = lv_obj_get_width(mask_label);
  const lv_coord_t height = lv_obj_get_height(mask_label);
  ClockShadowCacheEntry* slot = &g_clock_shadow_cache[0];
  for (ClockShadowCacheEntry& entry : g_clock_shadow_cache) {
    if (entry.mask && entry.font == font && entry.width == width && entry.height == height &&
        entry.alignment == alignment && strcmp(entry.text, text) == 0) {
      entry.used = ++g_clock_shadow_cache_tick;
      ext_out = entry.ext;
      return lv_draw_buf_dup(entry.mask);
    }
    if (entry.used < slot->used) slot = &entry;
  }

  lv_draw_buf_t* mask = lv_snapshot_take(mask_label, LV_COLOR_FORMAT_A8);
  if (!mask) return nullptr;
  if (slot->mask) lv_draw_buf_destroy(slot->mask);
  slot->font = font;
  slot->width = width;
  slot->height = height;
  slot->alignment = alignment;
  strlcpy(slot->text, text, sizeof(slot->text));
  slot->ext = lv_obj_get_ext_draw_size(mask_label);
  slot->mask = mask;
  slot->used = ++g_clock_shadow_cache_tick;
  ext_out = slot->ext;
  return lv_draw_buf_dup(mask);
}

struct ClockShadowSet {
  lv_obj_t* line = nullptr;
  lv_obj_t* main_label = nullptr;
  const lv_font_t* font = nullptr;
  bool container = false;
  bool fill_parent = false;
  uint8_t alignment = 1;
  lv_coord_t text_width = 0;
  lv_coord_t text_height = 0;
  lv_obj_t* labels[kClockShadowCopies] = {};
  // The copies of one mask (clock_shadow_mask): a hidden label holding the
  // text for the mask, the object drawing the nine copies behind the white
  // text and the line's own copy of the mask. Without them (fill_parent) the
  // copy labels above draw themselves. Lives in ClockTileData: the painter
  // draws from `paint`.
  lv_obj_t* mask_label = nullptr;
  lv_obj_t* painter = nullptr;
  clock_shadow_paint::Shadow paint;
  lv_draw_buf_t* mask_buf = nullptr;
  bool shadow_dirty = false;

  ClockShadowSet() = default;
  ClockShadowSet(const ClockShadowSet&) = delete;
  ClockShadowSet& operator=(const ClockShadowSet&) = delete;

  // Returns false for unchanged text. The clock ticks every second but shows
  // minutes; rewriting the same text redrew it and its nine shadow copies
  // each second (screensaver b88: 120 ms S3, 265 ms V2 per second).
  bool set_text(const char* text) {
    if (!text) text = "";
    if (main_label && strcmp(lv_label_get_text(main_label), text) == 0) return false;
    if (main_label) lv_label_set_text(main_label, text);
    for (lv_obj_t* label : labels) {
      if (label) lv_label_set_text(label, text);
    }
    if (mask_label) lv_label_set_text(mask_label, text);
    shadow_dirty = true;
    if (!line || !font) return true;

    lv_point_t text_size{};
    lv_text_get_size(&text_size, text ? text : "", font, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    text_width = text_size.x > 0 ? text_size.x : 1;
    text_height = text_size.y > 0 ? text_size.y : font->line_height;
    if (fill_parent) return true;

    // Reset to the actual text width before recalculating, then give both
    // clock lines the width of the longer one.
    lv_obj_set_size(line, text_width, text_height);
    if (container) {
      if (main_label) lv_obj_set_size(main_label, text_width, text_height);
      for (lv_obj_t* label : labels) {
        if (label) lv_obj_set_size(label, text_width, text_height);
      }
      if (mask_label) lv_obj_set_size(mask_label, text_width, text_height);
    }
    return true;
  }

  void set_box_width(lv_coord_t width) {
    if (!line || width < 1) return;
    lv_obj_set_width(line, width);
    const lv_text_align_t align = clock_text_align(alignment);
    if (main_label) lv_obj_set_style_text_align(main_label, align, 0);
    for (lv_obj_t* label : labels) {
      if (label) lv_obj_set_style_text_align(label, align, 0);
    }
    if (mask_label) lv_obj_set_style_text_align(mask_label, align, 0);
    if (!container) return;
    if (main_label) lv_obj_set_width(main_label, width);
    for (lv_obj_t* label : labels) {
      if (label) lv_obj_set_width(label, width);
    }
    if (mask_label) {
      lv_obj_set_width(mask_label, width);
      shadow_dirty = true;
    }
  }

  // Gives the copies the mask of the current text and width, after a change.
  void refresh_shadow() {
    if (!mask_label || !painter || !shadow_dirty) return;
    shadow_dirty = false;
    lv_obj_update_layout(mask_label);
    int32_t ext = 0;
    lv_draw_buf_t* mask = clock_shadow_mask(mask_label, font, alignment, ext);
    ClockShadowCopy copies[kClockShadowCopies];
    clock_shadow_copies(copies);
    clock_shadow_paint::place(painter, paint, mask, copies, ext);
    // The old mask: LVGL may hold it in its image cache from a fallback draw.
    if (mask_buf) {
      lv_image_cache_drop(mask_buf);
      lv_draw_buf_destroy(mask_buf);
    }
    mask_buf = mask;
  }

  void release_shadow() {
    clock_shadow_paint::release(paint);
    if (!mask_buf) return;
    lv_image_cache_drop(mask_buf);
    lv_draw_buf_destroy(mask_buf);
    mask_buf = nullptr;
  }
};

// Rolling digits (clock option, user 2026-10-11): the time as one cell per
// character; at a new minute a changed digit rolls up once (an LVGL animation
// of the cell's two digits: the old one leaves at the top, the new one comes
// in from below), the others stay. The fixed-width digits keep every cell in
// place. The cell clips its two labels and the animation moves them; a strip
// object between them cost one more object in every draw band (b594).
struct ClockRollLine {
  static constexpr uint8_t kMaxChars = 12;
  lv_obj_t* row = nullptr;
  lv_obj_t* cells[kMaxChars] = {};
  uint8_t count = 0;
  char text[kMaxChars + 1] = "";
  const lv_font_t* font = nullptr;
  lv_coord_t line_h = 0;
};

struct ClockTileData {
  uint8_t time_format = clock_tile::TIME_FORMAT_24H;
  uint8_t date_format = clock_tile::DATE_FORMAT_DMY;
  uint8_t flags = 1;
  bool show_date_text = true;
  bool show_weekday = false;
  bool weekday_short = false;
  // Rolling digits: the time line is `time_roll`, animated from the second
  // update on.
  bool roll = false;
  bool roll_started = false;
  ClockRollLine time_roll;
  const char* weekday_language = nullptr;
  bool fill_parent = false;
  bool horizontal = false;
  lv_obj_t* stack = nullptr;
  lv_obj_t* time_label = nullptr;
  lv_obj_t* date_label = nullptr;
  ClockShadowSet time_shadows;
  ClockShadowSet date_shadows;
  lv_timer_t* timer = nullptr;
};

static void apply_clock_line_alignment(ClockTileData* data) {
  if (!data || data->fill_parent || data->horizontal) return;
  lv_coord_t width = 0;
  if (data->time_label) width = max(width, data->time_shadows.text_width);
  if (data->date_label) width = max(width, data->date_shadows.text_width);
  if (width < 1) return;
  if (data->time_label) data->time_shadows.set_box_width(width);
  if (data->date_label) data->date_shadows.set_box_width(width);
  if (data->stack) lv_obj_update_layout(data->stack);
}

static uint8_t get_clock_flags(const Tile& tile) {
  uint8_t flags = tile.sensor_decimals;
  if (flags == 0xFF) flags = 1;
  flags &= 0x03;
  if (flags == 0) flags = 1;
  return flags;
}

// The cell's shown digit at `value`, the incoming one a line below it.
static void clock_roll_y(void* var, int32_t value) {
  lv_obj_t* cell = static_cast<lv_obj_t*>(var);
  lv_obj_t* shown = lv_obj_get_child(cell, 0);
  lv_obj_t* incoming = lv_obj_get_child(cell, 1);
  if (shown) lv_obj_set_y(shown, value);
  if (incoming) lv_obj_set_y(incoming, value + lv_obj_get_style_height(cell, LV_PART_MAIN));
}

// The roll ended: the incoming digit becomes the shown one, both back in place.
static void clock_roll_done(lv_anim_t* anim) {
  lv_obj_t* cell = static_cast<lv_obj_t*>(anim->var);
  lv_obj_t* shown = lv_obj_get_child(cell, 0);
  lv_obj_t* incoming = lv_obj_get_child(cell, 1);
  if (shown && incoming) lv_label_set_text(shown, lv_label_get_text(incoming));
  clock_roll_y(cell, 0);
}

static void clock_roll_label(lv_obj_t* cell, const lv_font_t* font, lv_coord_t width, lv_coord_t y, const char* text) {
  lv_obj_t* label = lv_label_create(cell);
  if (!label) return;
  set_label_style(label, lv_color_hex(ui_theme::text()), font);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(label, 0, y);
  lv_label_set_text(label, text);
}

// One cell per character, each as wide as its glyph (a fixed-width digit).
static void clock_roll_rebuild(ClockRollLine& roll, const char* text) {
  lv_obj_clean(roll.row);
  memset(roll.cells, 0, sizeof(roll.cells));
  size_t count = strlen(text);
  if (count > ClockRollLine::kMaxChars) count = ClockRollLine::kMaxChars;
  for (size_t i = 0; i < count; ++i) {
    const char ch[2] = {text[i], '\0'};
    const uint32_t next = i + 1 < count ? static_cast<uint8_t>(text[i + 1]) : 0;
    const lv_coord_t width = lv_font_get_glyph_width(roll.font, static_cast<uint8_t>(text[i]), next);
    lv_obj_t* cell = lv_obj_create(roll.row);
    if (!cell) break;
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, width, roll.line_h);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    clock_roll_label(cell, roll.font, width, 0, ch);
    clock_roll_label(cell, roll.font, width, roll.line_h, ch);
    roll.cells[i] = cell;
  }
  roll.count = static_cast<uint8_t>(count);
  memcpy(roll.text, text, count);
  roll.text[count] = '\0';
}

// Returns false for unchanged text. A changed digit rolls when `animate`;
// another length (12-hour "9:59" to "10:00") lays the cells out anew.
static bool clock_roll_set_text(ClockRollLine& roll, const char* text, bool animate) {
  if (!roll.row || !text) return false;
  if (strcmp(roll.text, text) == 0) return false;
  const size_t count = strlen(text);
  if (count != roll.count || count > ClockRollLine::kMaxChars) {
    clock_roll_rebuild(roll, text);
    return true;
  }
  for (size_t i = 0; i < count; ++i) {
    if (text[i] == roll.text[i] || !roll.cells[i]) continue;
    lv_obj_t* cell = roll.cells[i];
    lv_obj_t* shown = lv_obj_get_child(cell, 0);
    lv_obj_t* incoming = lv_obj_get_child(cell, 1);
    if (!shown || !incoming) continue;
    // A roll still running ends at once.
    if (lv_anim_get(cell, clock_roll_y)) {
      lv_anim_delete(cell, clock_roll_y);
      lv_label_set_text(shown, lv_label_get_text(incoming));
      clock_roll_y(cell, 0);
    }
    const char ch[2] = {text[i], '\0'};
    lv_label_set_text(incoming, ch);
    if (!animate) {
      lv_label_set_text(shown, ch);
      continue;
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, cell);
    lv_anim_set_exec_cb(&anim, clock_roll_y);
    lv_anim_set_values(&anim, 0, -roll.line_h);
    lv_anim_set_duration(&anim, 450);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&anim, clock_roll_done);
    lv_anim_start(&anim);
  }
  memcpy(roll.text, text, count);
  roll.text[count] = '\0';
  return true;
}

static void update_clock_labels(ClockTileData* data) {
  if (!data) return;
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    bool changed = false;
    if (data->time_label) {
      char buf[16];
      clock_tile::format_time(buf, sizeof(buf), timeinfo.tm_hour, timeinfo.tm_min, data->time_format);
      changed |= data->roll ? clock_roll_set_text(data->time_roll, buf, data->roll_started)
                            : data->time_shadows.set_text(buf);
    }
    if (data->date_label) {
      const uint8_t weekday = !data->show_weekday   ? clock_tile::WEEKDAY_NONE
                              : data->weekday_short ? clock_tile::WEEKDAY_SHORT
                                                    : clock_tile::WEEKDAY_LONG;
      char buf[96];
      clock_tile::format_date_line(buf, sizeof(buf), timeinfo.tm_wday, timeinfo.tm_mday, timeinfo.tm_mon,
                                   timeinfo.tm_year + 1900, data->show_date_text, data->date_format, weekday,
                                   data->weekday_language);
      changed |= data->date_shadows.set_text(buf);
    }
    if (changed) {
      apply_clock_line_alignment(data);
      // After both lines have their final width.
      data->time_shadows.refresh_shadow();
      data->date_shadows.refresh_shadow();
    }
  }
}

static void clock_timer_cb(lv_timer_t* timer) {
  ClockTileData* data = static_cast<ClockTileData*>(lv_timer_get_user_data(timer));
  if (!data) return;
  update_clock_labels(data);
}

// Create a clock line. With text_shadow, several slightly offset dark copies
// sit behind the text. A layout-neutral container lets the flex stack center
// the complete group like a single label.
static lv_obj_t* create_clock_line(lv_obj_t* stack,
                                   const ClockWidgetConfig& config,
                                   uint8_t raw_font_size, uint8_t fallback,
                                   bool date_line,
                                   uint8_t alignment,
                                   ClockShadowSet* shadow_out) {
  const uint8_t font_size = layout_clock_font_size(
      date_line ? clock_tile::normalize_date_font_size(raw_font_size, fallback)
                : clock_tile::normalize_font_size(raw_font_size, fallback));
  // Fixed-width digits: the time no longer shifts with every minute.
  const lv_font_t* font = tabular_digits::font(ui_font_for_size(font_size));
  if (!config.text_shadow) {
    lv_obj_t* label = lv_label_create(stack);
    if (!label) return nullptr;
    set_label_style(label, lv_color_hex(ui_theme::text()), font);
    if (config.fill_parent) lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, clock_text_align(alignment), 0);
    lv_label_set_text(label, "");
    if (shadow_out) {
      shadow_out->line = label;
      shadow_out->main_label = label;
      shadow_out->font = font;
      shadow_out->fill_parent = config.fill_parent;
      shadow_out->alignment = normalize_clock_alignment(alignment);
    }
    return label;
  }

  lv_obj_t* line = lv_obj_create(stack);
  if (!line) return nullptr;
  lv_obj_remove_style_all(line);
  if (config.fill_parent) {
    lv_obj_set_size(line, LV_PCT(100), font->line_height);
  } else {
    lv_obj_set_size(line, 1, font->line_height);
  }
  lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(line, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  ClockShadowCopy copies[kClockShadowCopies];
  clock_shadow_copies(copies);
  if (!config.fill_parent && shadow_out) {
    // The copies of one mask (clock_shadow_mask): the hidden label holds the
    // text for the mask, the painter draws the nine copies behind the white
    // text, each at its opacity in black.
    lv_obj_t* mask = lv_label_create(line);
    if (mask) {
      set_label_style(mask, lv_color_white(), font);
      lv_obj_set_style_text_align(mask, clock_text_align(alignment), 0);
      lv_obj_set_pos(mask, 0, 0);
      lv_label_set_text(mask, "");
      lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
      lv_obj_t* painter = clock_shadow_paint::create(line, &shadow_out->paint);
      if (painter) {
        shadow_out->mask_label = mask;
        shadow_out->painter = painter;
      } else {
        lv_obj_delete(mask);
      }
    }
  }
  if (!shadow_out || !shadow_out->mask_label) {
    for (uint8_t i = 0; i < kClockShadowCopies; ++i) {
      lv_obj_t* shadow = lv_label_create(line);
      if (!shadow) break;
      set_label_style(shadow, lv_color_black(), font);
      lv_obj_set_style_text_opa(shadow, copies[i].opa, 0);
      lv_obj_set_style_text_align(shadow, clock_text_align(alignment), 0);
      if (config.fill_parent) lv_obj_set_width(shadow, LV_PCT(100));
      lv_obj_set_pos(shadow, copies[i].x, copies[i].y);
      lv_label_set_text(shadow, "");
      if (shadow_out) shadow_out->labels[i] = shadow;
    }
  }
  if (shadow_out) {
    shadow_out->line = line;
    shadow_out->font = font;
    shadow_out->container = true;
    shadow_out->fill_parent = config.fill_parent;
    shadow_out->alignment = normalize_clock_alignment(alignment);
  }
  lv_obj_t* label = lv_label_create(line);
  if (!label) return nullptr;  // The stack owns and cleans up line.
  // White over its dark shadow in both themes: the shadowed clock is the
  // screensaver's, on its picture or black (a photo frame, user 2026-10-10);
  // a clock tile on its card takes the theme's text color.
  set_label_style(label, lv_color_white(), font);
  lv_obj_set_style_text_align(label, clock_text_align(alignment), 0);
  if (config.fill_parent) lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_pos(label, 0, 0);
  lv_label_set_text(label, "");
  if (shadow_out) shadow_out->main_label = label;
  return label;
}

// The time line of rolling digits: a row of cells (clock_roll_rebuild), set
// out like the label it replaces.
static lv_obj_t* create_clock_roll_line(lv_obj_t* stack, const ClockWidgetConfig& config, ClockRollLine* roll) {
  const uint8_t font_size = layout_clock_font_size(clock_tile::normalize_font_size(config.time_font_size, 40));
  roll->font = tabular_digits::font(ui_font_for_size(font_size));
  roll->line_h = lv_font_get_line_height(roll->font);
  lv_obj_t* row = lv_obj_create(stack);
  if (!row) return nullptr;
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, config.fill_parent ? LV_PCT(100) : LV_SIZE_CONTENT, roll->line_h);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  const uint8_t alignment = normalize_clock_alignment(config.time_alignment);
  lv_obj_set_flex_align(row,
                        alignment == 0   ? LV_FLEX_ALIGN_START
                        : alignment == 2 ? LV_FLEX_ALIGN_END
                                         : LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_gap(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  roll->row = row;
  return row;
}

lv_obj_t* create_clock_widget(lv_obj_t* parent,
                              const ClockWidgetConfig& config) {
  if (!parent ||
      (!config.show_time && !config.show_date && !config.show_weekday)) {
    return nullptr;
  }

  lv_obj_t* stack = lv_obj_create(parent);
  if (!stack) return nullptr;
  lv_obj_remove_style_all(stack);
  if (config.fill_parent) {
    lv_obj_set_size(stack, LV_PCT(100), LV_PCT(100));
  } else {
    lv_obj_set_size(stack, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  }
  lv_obj_set_flex_flow(stack, config.horizontal ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
  // Stacked lines of one alignment line up by it (the analog clock's lines
  // beside its face start together; user 2026-10-12: the rolling time stood
  // centered over the date); mixed alignments center them on each other.
  const uint8_t line_align = !config.horizontal && config.time_alignment == config.date_alignment
                                 ? normalize_clock_alignment(config.time_alignment)
                                 : 1;
  lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER,
                        line_align == 0   ? LV_FLEX_ALIGN_START
                        : line_align == 2 ? LV_FLEX_ALIGN_END
                                          : LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(stack, 0, 0);
  lv_obj_set_style_pad_gap(stack, tile_layout::scale(6), 0);
  lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(stack, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  // The lines' shadow state lives in the data from the start: the shadow
  // painters draw from it (clock_shadow_paint.h).
  ClockTileData* data = new (std::nothrow) ClockTileData{};
  if (!data) {
    lv_obj_delete(stack);
    return nullptr;
  }

  lv_obj_t* time_label = nullptr;
  if (config.show_time) {
    if (config.roll && !config.text_shadow) {
      time_label = create_clock_roll_line(stack, config, &data->time_roll);
      data->roll = time_label != nullptr;
    } else {
      time_label =
          create_clock_line(stack, config, config.time_font_size, 40, false,
                            config.time_alignment,
                            &data->time_shadows);
    }
  }

  // The date line also holds a standalone weekday.
  lv_obj_t* date_label = nullptr;
  if (config.show_date || config.show_weekday) {
    date_label =
        create_clock_line(stack, config, config.date_font_size, 20, true,
                          config.date_alignment,
                          &data->date_shadows);
  }

  data->time_format = config.time_format;
  data->date_format = config.date_format;
  data->flags = (config.show_time ? 1 : 0) | (config.show_date ? 2 : 0);
  data->show_date_text = config.show_date;
  data->show_weekday = config.show_weekday;
  data->weekday_short = config.weekday_short;
  data->weekday_language = config.weekday_language;
  data->fill_parent = config.fill_parent;
  data->horizontal = config.horizontal;
  data->stack = stack;
  data->time_label = time_label;
  data->date_label = date_label;
  update_clock_labels(data);
  // The first text appears at once; later minutes roll.
  data->roll_started = true;
  data->timer = lv_timer_create(clock_timer_cb, 1000, data);

  lv_obj_add_event_cb(
      stack,
      [](lv_event_t* e) {
        ClockTileData* data =
            static_cast<ClockTileData*>(lv_event_get_user_data(e));
        if (!data) return;
        if (data->timer) lv_timer_delete(data->timer);
        // The stack's delete event comes before its children's: the
        // painters lose their masks, then the masks are freed (the cache
        // keeps its own).
        data->time_shadows.release_shadow();
        data->date_shadows.release_shadow();
        delete data;
      },
      LV_EVENT_DELETE,
      data);
  return stack;
}

static constexpr uint8_t kClockFontSizes[] = {20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96};

static lv_coord_t clock_text_width(uint8_t size, const char* text) {
  lv_point_t text_size{};
  lv_text_get_size(&text_size, text, tabular_digits::font(ui_font_for_size(layout_clock_font_size(size))), 0, 0,
                   LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return text_size.x;
}

// Largest configured-or-smaller size whose rendered size stays within cap_px
// and whose sample text fits next to used_w. Returns 0 when nothing fits.
static uint8_t fit_clock_size(uint8_t configured, lv_coord_t cap_px, lv_coord_t avail_w,
                              lv_coord_t used_w, const char* sample, lv_coord_t* width_out) {
  for (int i = static_cast<int>(sizeof(kClockFontSizes)) - 1; i >= 0; --i) {
    const uint8_t size = kClockFontSizes[i];
    if (size > configured || layout_clock_font_size(size) > cap_px) continue;
    const lv_coord_t width = clock_text_width(size, sample);
    if (used_w + width <= avail_w) {
      if (width_out) *width_out = width;
      return size;
    }
  }
  return 0;
}

// Half-height clocks use one row: the largest configured-or-smaller size whose
// rendered size fits 80% of the tile height and whose text fits the width.
// The date follows only from width 2 and only when it still fits. The Web
// Admin preview applies the same rule (fitCompactClockPreview in admin.js).
static void fit_compact_clock(const Tile& tile, lv_coord_t pad, ClockWidgetConfig& config) {
  const lv_coord_t width = tile_geometry::extent(tile.col, tile.span_w, GRID_CELL_W, GRID_GAP) - pad * 2;
  const lv_coord_t cap = tile_geometry::extent(tile.row, tile.span_h, GRID_CELL_H, GRID_GAP) * 4 / 5;
  const char* time_sample = config.time_format == clock_tile::TIME_FORMAT_12H ? "88:88 PM" : "88:88";
  // Today's date line with every digit an 8: the widest of its kind, like
  // the preview's sample (clockPreviewSample).
  char date_sample[96] = "88.88.8888";
  struct tm now_tm;
  if (getLocalTime(&now_tm, 0)) {
    const uint8_t weekday = !config.show_weekday   ? clock_tile::WEEKDAY_NONE
                            : config.weekday_short ? clock_tile::WEEKDAY_SHORT
                                                   : clock_tile::WEEKDAY_LONG;
    clock_tile::format_date_line(date_sample, sizeof(date_sample), now_tm.tm_wday, now_tm.tm_mday, now_tm.tm_mon,
                                 now_tm.tm_year + 1900, config.show_date, config.date_format, weekday,
                                 config.weekday_language);
    for (char* p = date_sample; *p; ++p) {
      if (*p >= '0' && *p <= '9') *p = '8';
    }
  }
  const bool date_line = config.show_date || config.show_weekday;
  const bool date_is_primary = !config.show_time;
  uint8_t& primary_size = date_is_primary ? config.date_font_size : config.time_font_size;
  lv_coord_t used = 0;
  primary_size = fit_clock_size(primary_size, cap, width, 0,
                                date_is_primary ? date_sample : time_sample, &used);
  if (primary_size == 0) primary_size = kClockFontSizes[0];
  if (date_is_primary || !date_line) return;
  const uint8_t date_size =
      tile.span_w >= 2 ? fit_clock_size(min(config.date_font_size, config.time_font_size),
                                        layout_clock_font_size(config.time_font_size), width,
                                        used + tile_layout::scale(6), date_sample, nullptr)
                       : 0;
  if (date_size == 0) {
    config.show_date = false;
    config.show_weekday = false;
  } else {
    config.date_font_size = date_size;
  }
}

// The analog and the wall clock (clock_face): the face and, where there is
// room, the time and the date beside or below it, as in the mockup
// (build/design-mockups/clock "Analog", user 2026-10-12): a half tile a small
// face with its lines beside it (the date only when the text has twice the
// face's width, like the digital clock's date from width 2); a tile at least
// twice as wide as its area below the header the face left, the lines right;
// a tall tile the face on top, the lines below; else the face alone, the
// date below it when the face keeps 70 % of its size (the face shows the
// time). The time takes the largest size whose clock sample fits beside or
// below the face; without one the face stays alone (the time and date size
// options are hidden for the faces, user 2026-10-12). Places in card pixels;
// the preview mirrors it (clock/admin.js clockFaceLayout). render_clock_face
// moves the face and its lines together by the tile's alignment.
struct ClockFaceLayout {
  lv_coord_t x = 0, y = 0, size = 0;
  bool show_time = false;
  bool show_date = false;
  // The lines start at text_x (beside the face) or center in the text box.
  bool beside = false;
  lv_coord_t text_x = 0, text_y = 0, text_w = 0, text_h = 0;
  uint8_t time_size = 0, date_size = 0;
};

// The time sizes the lines may take (nominal, largest first) with the
// panel's width of the clock sample "88:88 PM" in them and their line
// heights; the date's line heights at 20 and 24. The preview reads the same
// as --tw<size> and --lh<size> (web_admin_styles.cpp).
constexpr uint8_t kClockFaceTimeSizes[] = {56, 40, 32, 24, 20};
constexpr int kClockFaceTimeCount = sizeof(kClockFaceTimeSizes) / sizeof(kClockFaceTimeSizes[0]);
struct ClockFaceFonts {
  lv_coord_t time_w[kClockFaceTimeCount] = {};
  lv_coord_t time_h[kClockFaceTimeCount] = {};
  lv_coord_t date20_h = 0;
  lv_coord_t date24_h = 0;
};

// The largest time size up to `max_size` whose sample fits `width` and whose
// line, with `below` under it, fits `height`; -1 without one.
static int clock_face_time_fit(const ClockFaceFonts& fonts, uint8_t max_size, lv_coord_t width, lv_coord_t height,
                               lv_coord_t below) {
  for (int i = 0; i < kClockFaceTimeCount; ++i) {
    if (kClockFaceTimeSizes[i] > max_size) continue;
    if (fonts.time_w[i] <= width && fonts.time_h[i] + below <= height) return i;
  }
  return -1;
}

static ClockFaceLayout clock_face_layout(lv_coord_t w, lv_coord_t h, lv_coord_t top, bool compact, bool time_line,
                                         bool date_line, const ClockFaceFonts& fonts) {
  ClockFaceLayout l;
  const lv_coord_t m = tile_layout::scale(compact ? clock_face::kCompactMargin : clock_face::kTileMargin);
  const lv_coord_t area_h = h - top;
  l.y = top + m;
  if (compact) {
    l.size = area_h - 2 * m;
    l.x = m;
    l.beside = true;
    l.text_x = l.size + 2 * m + tile_layout::scale(clock_face::kCompactGap);
    l.text_y = top;
    l.text_w = w - l.text_x - m;
    l.text_h = area_h;
    l.date_size = 20;
    const bool date = date_line && l.text_w >= 2 * l.size;
    const int fit = time_line ? clock_face_time_fit(fonts, 24, l.text_w, area_h, date ? fonts.date20_h : 0) : -1;
    l.show_time = fit >= 0;
    l.time_size = l.show_time ? kClockFaceTimeSizes[fit] : 0;
    l.show_date = date && (l.show_time || fonts.date20_h <= area_h);
    return l;
  }
  if ((time_line || date_line) && w >= 2 * area_h) {
    const lv_coord_t size = area_h - 2 * m;
    const lv_coord_t text_w = w - size - 3 * m;
    const lv_coord_t date_h = date_line ? fonts.date20_h : 0;
    const int fit = time_line ? clock_face_time_fit(fonts, 56, text_w, area_h, date_h) : -1;
    if (time_line ? fit >= 0 : date_h <= area_h) {
      l.size = size;
      l.x = m;
      l.beside = true;
      l.text_x = size + 2 * m;
      l.text_y = top;
      l.text_w = text_w;
      l.text_h = area_h;
      l.show_time = time_line;
      l.show_date = date_line;
      l.time_size = fit >= 0 ? kClockFaceTimeSizes[fit] : 0;
      l.date_size = 20;
      return l;
    }
  }
  if ((time_line || date_line) && area_h * 2 >= w * 3) {
    const lv_coord_t size = w - 2 * m;
    const lv_coord_t text_y = l.y + size;
    const lv_coord_t date_h = date_line ? fonts.date20_h : 0;
    const int fit = time_line ? clock_face_time_fit(fonts, 40, w - 2 * m, h - text_y, date_h) : -1;
    if (time_line ? fit >= 0 : date_h <= h - text_y) {
      l.size = size;
      l.x = m;
      l.text_y = text_y;
      l.text_w = w;
      l.text_h = h - text_y;
      l.show_time = time_line;
      l.show_date = date_line;
      l.time_size = fit >= 0 ? kClockFaceTimeSizes[fit] : 0;
      l.date_size = 20;
      return l;
    }
  }
  const lv_coord_t full = LV_MIN(w, area_h) - 2 * m;
  l.size = full;
  if (date_line) {
    const lv_coord_t with_date = LV_MIN(w - 2 * m, area_h - 3 * m - fonts.date24_h);
    if (with_date * 10 >= full * 7) {
      l.size = with_date;
      l.show_date = true;
      l.date_size = 24;
      l.text_y = l.y + l.size;
      l.text_w = w;
      l.text_h = h - l.text_y;
    }
  }
  l.x = (w - l.size) / 2;
  return l;
}

// The panel's measures for clock_face_layout (tabular digits, like the
// clock lines).
static ClockFaceFonts clock_face_fonts() {
  ClockFaceFonts fonts;
  for (int i = 0; i < kClockFaceTimeCount; ++i) {
    const lv_font_t* font = tabular_digits::font(ui_font_for_size(layout_clock_font_size(kClockFaceTimeSizes[i])));
    lv_point_t sample{};
    lv_text_get_size(&sample, "88:88 PM", font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    fonts.time_w[i] = sample.x;
    fonts.time_h[i] = lv_font_get_line_height(font);
  }
  fonts.date20_h = lv_font_get_line_height(ui_font_for_size(layout_clock_font_size(20)));
  fonts.date24_h = lv_font_get_line_height(ui_font_for_size(layout_clock_font_size(24)));
  return fonts;
}

static void render_clock_face(lv_obj_t* card, const Tile& tile, bool compact, bool has_header, uint32_t card_color,
                              const clock_tile::TileDateOptions& options, ClockWidgetConfig config) {
  const lv_coord_t w = tile_geometry::extent(tile.col, tile.span_w, GRID_CELL_W, GRID_GAP);
  const lv_coord_t h = tile_geometry::extent(tile.row, tile.span_h, GRID_CELL_H, GRID_GAP);
  // Card pixels from the card's top left; children sit in its padding box.
  const lv_coord_t pad_x = lv_obj_get_style_pad_left(card, LV_PART_MAIN);
  const lv_coord_t pad_y = lv_obj_get_style_pad_top(card, LV_PART_MAIN);
  const bool date_line = config.show_date || config.show_weekday;
  const ClockFaceLayout l =
      clock_face_layout(w, h, has_header ? tile_icon_disc::row_height() : 0, compact, config.show_time, date_line,
                        clock_face_fonts());
  clock_face::Colors colors;
  clock_face::Marks marks = clock_face::Marks::kDots;
  if (options.style == clock_tile::STYLE_WALL) {
    const uint32_t ink = clock_face::kHandColors[options.hands < clock_face::kHandColorCount ? options.hands : 0];
    colors.dial = clock_face::kDialColors[options.dial < clock_face::kDialColorCount ? options.dial : 0];
    colors.major = colors.minor = colors.hour = colors.minute = colors.center = ink;
    marks = clock_face::Marks::kBatons;
  } else {
    // The theme's icon circle on this card (an untinted tile icon's). A
    // Transparent clock has no card: its dial is the circle on the theme's
    // card in the dark theme (on the black screen it vanished, user
    // 2026-10-12) and on the screen in the light one (on the white card it
    // would match the light screen).
    const uint32_t dial_card =
        tileTransparent(tile) ? (ui_theme::theme_light() ? ui_theme::screen() : ui_theme::card()) : card_color;
    colors.dial = tile_icon_disc::tile_fill(card, dial_card, ui_theme::icon(), false, false, false,
                                            ui_surface_style::icon_glow_percent())
                      .disc;
    colors.major = colors.hour = colors.minute = colors.center = ui_theme::text();
    colors.minor = ui_theme::icon_off();
  }
  // The face and its lines are one block (user 2026-10-12: the lines stood
  // far right of the face and the alignment did nothing): beside the face
  // the lines start one gap after it, left-aligned; below it they center on
  // it, as far below as the text box centers them. The tile's alignment moves
  // the block between the margins; LVGL keeps it there when a line's width
  // changes (the time arrives after the start, the date at midnight). The
  // preview mirrors it (clock/admin.js layoutClockFacePreview).
  lv_obj_t* block = lv_obj_create(card);
  if (!block) return;
  lv_obj_remove_style_all(block);
  lv_obj_set_size(block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_remove_flag(block, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(block, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(block, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_flex_flow(block, l.beside ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  clock_face::create(block, 0, 0, l.size, colors, marks, compact);
  const lv_coord_t m = tile_layout::scale(compact ? clock_face::kCompactMargin : clock_face::kTileMargin);
  const lv_coord_t pad_r = lv_obj_get_style_pad_right(card, LV_PART_MAIN);
  const uint8_t align = options.align;
  // Card pixels to offsets from the content box (lv_obj_align).
  const lv_coord_t x_ofs = align == 1   ? m - pad_x
                           : align == 2 ? pad_r - m
                                        : w / 2 - (pad_x + (w - pad_x - pad_r) / 2);
  const bool lines = l.show_time || l.show_date;
  if (l.beside) {
    const lv_coord_t content_cy = pad_y + (h - pad_y - lv_obj_get_style_pad_bottom(card, LV_PART_MAIN)) / 2;
    lv_obj_set_style_pad_column(block, l.text_x - l.x - l.size, 0);
    lv_obj_align(block,
                 align == 1   ? LV_ALIGN_LEFT_MID
                 : align == 2 ? LV_ALIGN_RIGHT_MID
                              : LV_ALIGN_CENTER,
                 x_ofs, l.text_y + l.text_h / 2 - content_cy);
  } else {
    if (lines) {
      auto line_h = [](uint8_t size) {
        return static_cast<lv_coord_t>(lv_font_get_line_height(ui_font_for_size(layout_clock_font_size(size))));
      };
      const lv_coord_t lines_h = (l.show_time ? line_h(l.time_size) : 0) + (l.show_date ? line_h(l.date_size) : 0) +
                                 (l.show_time && l.show_date ? tile_layout::scale(6) : 0);
      lv_obj_set_style_pad_row(block, LV_MAX(0, (l.text_h - lines_h) / 2), 0);
    }
    lv_obj_align(block,
                 align == 1   ? LV_ALIGN_TOP_LEFT
                 : align == 2 ? LV_ALIGN_TOP_RIGHT
                              : LV_ALIGN_TOP_MID,
                 x_ofs, l.y - pad_y);
  }
  if (!lines) return;
  config.fill_parent = false;
  config.horizontal = false;
  config.text_shadow = false;
  config.roll = config.roll && l.show_time;
  config.show_time = l.show_time;
  if (!l.show_date) config.show_date = config.show_weekday = false;
  config.time_font_size = l.time_size ? l.time_size : config.time_font_size;
  config.date_font_size = l.date_size ? l.date_size : config.date_font_size;
  config.time_alignment = config.date_alignment = l.beside ? 0 : 1;
  create_clock_widget(block, config);
}

lv_obj_t* render_clock_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index) {
  (void)index;
  lv_obj_t* card = lv_button_create(parent);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);

  uint32_t card_color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  uint32_t pressed_color = tone_color::pressed_card(card_color);
  lv_obj_set_style_bg_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);

  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_hor(card, tile_layout::scale_480(20), 0);
  lv_obj_set_style_pad_ver(card, tile_layout::scale_480(24), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);

  place_tile_card(card, col, row, tile);

  const bool compact = tile_geometry::compact_clock(tile.type, tile.span_w, tile.span_h);
  const lv_coord_t compact_pad = tile_layout::scale_480(8);
  if (compact) {
    lv_obj_set_style_pad_hor(card, compact_pad, 0);
    lv_obj_set_style_pad_ver(card, 0, 0);
  }

  // Icon Label (optional). Half-height clocks show only the time row.
  String iconChar;
  if (!compact && tile.icon_name.length() > 0 && FONT_MDI_ICONS != nullptr) {
    iconChar = getMdiChar(tile.icon_name);
  }
  const bool has_icon = iconChar.length() > 0;
  lv_obj_t* header_icon = nullptr;
  if (has_icon) {
    lv_obj_t* icon_lbl = lv_label_create(card);
    if (icon_lbl) {
      set_label_style(icon_lbl, lv_color_hex(ui_theme::icon()), FONT_MDI_ICONS);
      lv_label_set_text(icon_lbl, iconChar.c_str());
      lv_obj_align(icon_lbl, LV_ALIGN_TOP_RIGHT,
                   tile_layout::scale_480(4),
                   tile_layout::scale_480(-8));
      header_icon = icon_lbl;
    }
  }

  // Title Label (optional)
  if (!compact && tile.title.length() > 0) {
    lv_obj_t* title_lbl = lv_label_create(card);
    if (title_lbl) {
      set_label_style(title_lbl, lv_color_hex(ui_theme::text()),
                      tile_layout::header_title_font());
      lv_obj_set_width(title_lbl, LV_PCT(has_icon ? 70 : 100));
      hometiles_title::tile(title_lbl, tile.title.c_str(), true);
      lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 0,
                   tile_layout::scale_480(4));
    }
  }
  // After the title exists, so the disc can lift the whole header.
  if (header_icon) tile_icon_disc::add_round(card, header_icon);

  uint8_t flags = get_clock_flags(tile);
  const bool show_time = (flags & 1) != 0;
  const bool show_date = (flags & 2) != 0;
  const bool has_header = !compact && (tile.title.length() > 0 || has_icon);

  ClockWidgetConfig widget_config;
  widget_config.show_time = show_time;
  widget_config.show_date = show_date;
  widget_config.fill_parent = true;
  widget_config.time_font_size = clock_tile::normalize_font_size(tile.key_code, 40);
  widget_config.date_font_size =
      clock_tile::normalize_date_font_size(tile.key_modifier, 20);
  widget_config.time_format = resolve_clock_time_format(tile);
  widget_config.date_format = resolve_clock_date_format(tile);
  // The weekday and rolling digits (clock_tile::TileDateOptions).
  const clock_tile::TileDateOptions date_options = clock_tile::unpack_date_options(tile.sensor_gauge_max);
  widget_config.show_weekday = date_options.weekday != clock_tile::WEEKDAY_NONE;
  widget_config.weekday_short = date_options.weekday == clock_tile::WEEKDAY_SHORT;
  widget_config.weekday_language = configManager.getConfig().language;
  widget_config.roll = date_options.roll;
  widget_config.time_alignment = clock_tile::tile_alignment(date_options.align);
  widget_config.date_alignment = widget_config.time_alignment;
  // Without a title or icon the lines use the whole card height, centered:
  // its top and bottom padding only cut large clock sizes (user 2026-10-12).
  if (!compact && !has_header) lv_obj_set_style_pad_ver(card, 0, 0);
  if (date_options.style == clock_tile::STYLE_ANALOG || date_options.style == clock_tile::STYLE_WALL) {
    render_clock_face(card, tile, compact, has_header, card_color, date_options, widget_config);
  } else {
  if (compact) {
    widget_config.fill_parent = false;
    widget_config.horizontal = true;
    fit_compact_clock(tile, compact_pad, widget_config);
  }
  lv_obj_t* stack = create_clock_widget(card, widget_config);
  if (stack) {
    // A half tile's row moves to the chosen side; a full tile's lines align
    // their text (fill_parent).
    const lv_align_t place = date_options.align == 1   ? LV_ALIGN_LEFT_MID
                             : date_options.align == 2 ? LV_ALIGN_RIGHT_MID
                                                       : LV_ALIGN_CENTER;
    lv_obj_align(stack, place, 0,
                 has_header ? tile_layout::scale(18) : 0);
  }
  }

  // Every clock tile opens the same globally configured screensaver.
  lv_obj_add_event_cb(
      card,
      [](lv_event_t*) { show_image_screensaver(); },
      LV_EVENT_CLICKED,
      nullptr);

  return card;
}

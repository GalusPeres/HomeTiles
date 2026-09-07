#include "src/types/clock/renderer.h"
#include "src/types/clock/clock_format.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/fonts/ui_fonts.h"
#include "src/ui/screensaver/image_screensaver.h"
#include <Arduino.h>
#include <new>
#include <time.h>

static uint8_t normalize_clock_font_size(uint8_t raw, uint8_t fallback) {
  switch (raw) {
    case 20:
    case 24:
    case 28:
    case 32:
    case 40:
    case 48:
    case 56:
    case 64:
    case 72:
    case 80:
    case 96:
      return raw;
    default:
      return fallback;
  }
}

static uint8_t normalize_clock_date_font_size(uint8_t raw,
                                              uint8_t fallback) {
  const uint8_t normalized = normalize_clock_font_size(raw, fallback);
  return normalized > 72 ? 72 : normalized;
}

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
  return clock_tile::resolve_date_format(tile.sensor_gauge_max, cfg.global_date_format, cfg.language);
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
static constexpr uint8_t kClockShadowCopies = 9;

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

  void set_text(const char* text) {
    if (main_label) lv_label_set_text(main_label, text ? text : "");
    for (lv_obj_t* label : labels) {
      if (label) lv_label_set_text(label, text ? text : "");
    }
    if (!line || !font) return;

    lv_point_t text_size{};
    lv_text_get_size(&text_size, text ? text : "", font, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    text_width = text_size.x > 0 ? text_size.x : 1;
    text_height = text_size.y > 0 ? text_size.y : font->line_height;
    if (fill_parent) return;

    // Reset to the actual text width before recalculating, then give both
    // clock lines the width of the longer one.
    lv_obj_set_size(line, text_width, text_height);
    if (container) {
      if (main_label) lv_obj_set_size(main_label, text_width, text_height);
      for (lv_obj_t* label : labels) {
        if (label) lv_obj_set_size(label, text_width, text_height);
      }
    }
  }

  void set_box_width(lv_coord_t width) {
    if (!line || width < 1) return;
    lv_obj_set_width(line, width);
    const lv_text_align_t align = clock_text_align(alignment);
    if (main_label) lv_obj_set_style_text_align(main_label, align, 0);
    for (lv_obj_t* label : labels) {
      if (label) lv_obj_set_style_text_align(label, align, 0);
    }
    if (!container) return;
    if (main_label) lv_obj_set_width(main_label, width);
    for (lv_obj_t* label : labels) {
      if (label) lv_obj_set_width(label, width);
    }
  }
};

struct ClockTileData {
  uint8_t time_format = clock_tile::TIME_FORMAT_24H;
  uint8_t date_format = clock_tile::DATE_FORMAT_DMY;
  uint8_t flags = 1;
  bool show_date_text = true;
  bool show_weekday = false;
  const char* weekday_language = nullptr;
  bool fill_parent = false;
  lv_obj_t* stack = nullptr;
  lv_obj_t* time_label = nullptr;
  lv_obj_t* date_label = nullptr;
  ClockShadowSet time_shadows;
  ClockShadowSet date_shadows;
  lv_timer_t* timer = nullptr;
};

static void apply_clock_line_alignment(ClockTileData* data) {
  if (!data || data->fill_parent) return;
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

static void update_clock_labels(ClockTileData* data) {
  if (!data) return;
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    if (data->time_label) {
      char buf[16];
      if (data->time_format == clock_tile::TIME_FORMAT_12H) {
        int hour12 = timeinfo.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;
        snprintf(buf, sizeof(buf), "%d:%02d %s", hour12, timeinfo.tm_min, timeinfo.tm_hour < 12 ? "AM" : "PM");
      } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      }
      data->time_shadows.set_text(buf);
    }
    if (data->date_label) {
      char date_buf[16] = "";
      if (data->show_date_text) {
        switch (data->date_format) {
          case clock_tile::DATE_FORMAT_MDY:
            snprintf(date_buf, sizeof(date_buf), "%02d/%02d/%04d",
                     timeinfo.tm_mon + 1,
                     timeinfo.tm_mday,
                     timeinfo.tm_year + 1900);
            break;
          case clock_tile::DATE_FORMAT_YMD:
            snprintf(date_buf, sizeof(date_buf), "%04d/%02d/%02d",
                     timeinfo.tm_year + 1900,
                     timeinfo.tm_mon + 1,
                     timeinfo.tm_mday);
            break;
          default:
            snprintf(date_buf, sizeof(date_buf), "%02d.%02d.%04d",
                     timeinfo.tm_mday,
                     timeinfo.tm_mon + 1,
                     timeinfo.tm_year + 1900);
            break;
        }
      }
      const char* weekday =
          data->show_weekday
              ? clock_tile::weekday_name(timeinfo.tm_wday,
                                         data->weekday_language)
              : "";
      char buf[40];
      if (weekday[0] && date_buf[0]) {
        snprintf(buf, sizeof(buf), "%s, %s", weekday, date_buf);
      } else {
        snprintf(buf, sizeof(buf), "%s", weekday[0] ? weekday : date_buf);
      }
      data->date_shadows.set_text(buf);
    }
    apply_clock_line_alignment(data);
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
      date_line ? normalize_clock_date_font_size(raw_font_size, fallback)
                : normalize_clock_font_size(raw_font_size, fallback));
  const lv_font_t* font = ui_font_for_size(font_size);
  if (!config.text_shadow) {
    lv_obj_t* label = lv_label_create(stack);
    if (!label) return nullptr;
    set_label_style(label, lv_color_white(), font);
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

  const struct {
    int16_t x;
    int16_t y;
    lv_opa_t opa;
  } copies[kClockShadowCopies] = {
      {tile_layout::scale(4), tile_layout::scale(4), static_cast<lv_opa_t>(34)},
      {tile_layout::scale(2), tile_layout::scale(4), static_cast<lv_opa_t>(14)},
      {tile_layout::scale(6), tile_layout::scale(4), static_cast<lv_opa_t>(14)},
      {tile_layout::scale(4), tile_layout::scale(2), static_cast<lv_opa_t>(14)},
      {tile_layout::scale(4), tile_layout::scale(6), static_cast<lv_opa_t>(14)},
      {tile_layout::scale(2), tile_layout::scale(2), static_cast<lv_opa_t>(8)},
      {tile_layout::scale(6), tile_layout::scale(2), static_cast<lv_opa_t>(8)},
      {tile_layout::scale(2), tile_layout::scale(6), static_cast<lv_opa_t>(8)},
      {tile_layout::scale(6), tile_layout::scale(6), static_cast<lv_opa_t>(8)},
  };
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
  if (shadow_out) {
    shadow_out->line = line;
    shadow_out->font = font;
    shadow_out->container = true;
    shadow_out->fill_parent = config.fill_parent;
    shadow_out->alignment = normalize_clock_alignment(alignment);
  }
  lv_obj_t* label = lv_label_create(line);
  if (!label) return nullptr;  // The stack owns and cleans up line.
  set_label_style(label, lv_color_white(), font);
  lv_obj_set_style_text_align(label, clock_text_align(alignment), 0);
  if (config.fill_parent) lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_pos(label, 0, 0);
  lv_label_set_text(label, "");
  if (shadow_out) shadow_out->main_label = label;
  return label;
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
  lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(stack, 0, 0);
  lv_obj_set_style_pad_gap(stack, tile_layout::scale(6), 0);
  lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(stack, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  lv_obj_t* time_label = nullptr;
  ClockShadowSet time_shadows;
  if (config.show_time) {
    time_label =
        create_clock_line(stack, config, config.time_font_size, 40, false,
                          config.time_alignment,
                          &time_shadows);
  }

  // The date line also holds a standalone weekday.
  lv_obj_t* date_label = nullptr;
  ClockShadowSet date_shadows;
  if (config.show_date || config.show_weekday) {
    date_label =
        create_clock_line(stack, config, config.date_font_size, 20, true,
                          config.date_alignment,
                          &date_shadows);
  }

  ClockTileData* data = new (std::nothrow) ClockTileData{};
  if (!data) {
    lv_obj_delete(stack);
    return nullptr;
  }
  data->time_format = config.time_format;
  data->date_format = config.date_format;
  data->flags = (config.show_time ? 1 : 0) | (config.show_date ? 2 : 0);
  data->show_date_text = config.show_date;
  data->show_weekday = config.show_weekday;
  data->weekday_language = config.weekday_language;
  data->fill_parent = config.fill_parent;
  data->stack = stack;
  data->time_label = time_label;
  data->date_label = date_label;
  data->time_shadows = time_shadows;
  data->date_shadows = date_shadows;
  update_clock_labels(data);
  data->timer = lv_timer_create(clock_timer_cb, 1000, data);

  lv_obj_add_event_cb(
      stack,
      [](lv_event_t* e) {
        ClockTileData* data =
            static_cast<ClockTileData*>(lv_event_get_user_data(e));
        if (!data) return;
        if (data->timer) lv_timer_delete(data->timer);
        delete data;
      },
      LV_EVENT_DELETE,
      data);
  return stack;
}

lv_obj_t* render_clock_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index) {
  (void)index;
  lv_obj_t* card = lv_button_create(parent);
  lv_obj_set_style_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);

  uint32_t card_color = tileBgColorOrDefault(tile, 0x2A2A2A);
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  uint32_t pressed_color = brighten_rgb_color(card_color, 0x10);
  lv_obj_set_style_bg_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);

  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_hor(card, tile_layout::scale_480(20), 0);
  lv_obj_set_style_pad_ver(card, tile_layout::scale_480(24), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);

  set_tile_grid_cell(card, col, row, tile.span_w, tile.span_h);

  // Icon Label (optional)
  String iconChar;
  if (tile.icon_name.length() > 0 && FONT_MDI_ICONS != nullptr) {
    iconChar = getMdiChar(tile.icon_name);
  }
  const bool has_icon = iconChar.length() > 0;
  if (has_icon) {
    lv_obj_t* icon_lbl = lv_label_create(card);
    if (icon_lbl) {
      set_label_style(icon_lbl, lv_color_white(), FONT_MDI_ICONS);
      lv_label_set_text(icon_lbl, iconChar.c_str());
      lv_obj_align(icon_lbl, LV_ALIGN_TOP_RIGHT,
                   tile_layout::scale_480(4),
                   tile_layout::scale_480(-8));
    }
  }

  // Title Label (optional)
  if (tile.title.length() > 0) {
    lv_obj_t* title_lbl = lv_label_create(card);
    if (title_lbl) {
      set_label_style(title_lbl, lv_color_white(),
                      tile_layout::header_title_font());
      lv_obj_set_width(title_lbl, LV_PCT(has_icon ? 70 : 100));
      hometiles_title::tile(title_lbl, tile.title.c_str(), true);
      lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 0,
                   tile_layout::scale_480(4));
    }
  }

  uint8_t flags = get_clock_flags(tile);
  const bool show_time = (flags & 1) != 0;
  const bool show_date = (flags & 2) != 0;
  const bool has_header = tile.title.length() > 0 || has_icon;

  ClockWidgetConfig widget_config;
  widget_config.show_time = show_time;
  widget_config.show_date = show_date;
  widget_config.fill_parent = true;
  widget_config.time_font_size = normalize_clock_font_size(tile.key_code, 40);
  widget_config.date_font_size =
      normalize_clock_date_font_size(tile.key_modifier, 20);
  widget_config.time_format = resolve_clock_time_format(tile);
  widget_config.date_format = resolve_clock_date_format(tile);
  lv_obj_t* stack = create_clock_widget(card, widget_config);
  if (stack) {
    lv_obj_align(stack, LV_ALIGN_CENTER, 0,
                 has_header ? tile_layout::scale(18) : 0);
  }

  // Every clock tile opens the same globally configured screensaver.
  lv_obj_add_event_cb(
      card,
      [](lv_event_t*) { show_image_screensaver(); },
      LV_EVENT_CLICKED,
      nullptr);

  return card;
}

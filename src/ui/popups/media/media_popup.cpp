// The Media popup (user 2026-10-19, build/design-mockups/media-player and the
// simulator build): two layouts per tile ("Cover links": the cover left, the
// playing bars, title, artist and album beside it; "Cover Mitte": the cover
// centred, title and "artist · album" under it), the times always under the
// time bar. Every distance around the controls is the card's left edge
// (media_popup_layout.h). Shuffle, repeat, the source, the speakers and the
// library show only with their Home Assistant feature; the source, the
// speakers and the library are sub-views of the card (back arrow in the head
// disc, the title names the level, the X closes). The library protocol is
// memooox3's (PR #87, media_browse.cpp).
#include "src/ui/popups/media/media_popup.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "src/core/config/config_manager.h"
#include "src/core/config/tile_radius.h"
#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/media/artwork_scale.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/device/device_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/media/media_browse.h"
#include "src/ui/popups/media/media_popup_layout.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_nav_style.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_surface.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/tabs/settings/settings_style.h"
#include "src/ui/shared/ui_surface_style.h"

// lvgl.h no longer exports lv_image_cache_drop() in 9.5; its declaration
// is available only in the instance header.
#include <misc/cache/instance/lv_image_cache.h>
#include "src/ui/shared/ui_theme.h"

namespace {

namespace ml = media_popup_layout;

// Home Assistant's MediaPlayerEntityFeature bits the popup reads.
constexpr uint32_t kFeaturePause = 1;
constexpr uint32_t kFeatureSeek = 2;
constexpr uint32_t kFeatureVolumeSet = 4;
constexpr uint32_t kFeatureVolumeMute = 8;
constexpr uint32_t kFeaturePrevious = 16;
constexpr uint32_t kFeatureNext = 32;
constexpr uint32_t kFeatureSelectSource = 2048;
constexpr uint32_t kFeaturePlay = 16384;
constexpr uint32_t kFeatureShuffle = 32768;
constexpr uint32_t kFeatureSoundMode = 65536;
constexpr uint32_t kFeatureBrowse = 131072;
constexpr uint32_t kFeatureRepeat = 262144;
constexpr uint32_t kFeatureGrouping = 524288;

// Bounds: players with extra fields, list lengths (the Bridge sends at most
// 24 sources, 12 sound modes, 8 candidates).
constexpr size_t kMaxExtras = 8;
constexpr size_t kMaxSources = 24;
constexpr size_t kMaxSoundModes = 12;
constexpr size_t kMaxMembers = 9;
constexpr size_t kMaxCandidates = 8;
// The library builds its rows in steps while scrolling (a level has up to
// 256 entries).
constexpr size_t kLibraryRowsStep = 40;
// Long texts: one line moves at a time (pause, across, pause, back).
constexpr uint32_t kMarqueePauseMs = 2000;
constexpr int kMarqueePxPerSecond = 18;
constexpr uint32_t kMotionPeriodMs = 40;
constexpr uint32_t kLibraryTickMs = 150;
// The library's status pill: loading only after this long, an error this
// long over a list that is still there.
constexpr uint32_t kLoadingDelayMs = 400;
constexpr uint32_t kErrorShownMs = 4000;

// ---- Player fields beyond the tile's (Bridge v0.9.4b1, media_controls.py) --
struct MediaGroupCandidate {
  String entity_id;
  String name;
  String state;
};

struct MediaExtras {
  String entity_id;
  uint32_t hash = 0;
  uint32_t used_ms = 0;
  bool has_features = false;
  uint32_t features = 0;
  String state;
  String artist;
  String album;
  bool has_shuffle = false;
  bool shuffle = false;
  String repeat;
  String source;
  String sound_mode;
  std::vector<String> sources;
  std::vector<String> sound_modes;
  std::vector<String> members;
  std::vector<MediaGroupCandidate> candidates;
};

std::vector<MediaExtras> g_extras;

uint32_t text_hash(const char* text) {
  uint32_t hash = 2166136261u;
  for (const char* p = text; p && *p; ++p) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 16777619u;
  }
  return hash;
}

MediaExtras* find_extras(const String& entity_id) {
  for (MediaExtras& entry : g_extras) {
    if (entry.entity_id.equalsIgnoreCase(entity_id)) return &entry;
  }
  return nullptr;
}

const MediaExtras& extras_of(const String& entity_id) {
  static const MediaExtras kNone;
  const MediaExtras* entry = find_extras(entity_id);
  return entry ? *entry : kNone;
}

void read_names(JsonVariantConst list, std::vector<String>& out, size_t limit) {
  out.clear();
  for (JsonVariantConst item : list.as<JsonArrayConst>()) {
    const char* name = item | "";
    if (!*name) continue;
    out.emplace_back(name);
    if (out.size() >= limit) break;
  }
}

bool contains(const std::vector<String>& list, const String& value) {
  for (const String& item : list) {
    if (item.equalsIgnoreCase(value)) return true;
  }
  return false;
}

// ---- Small helpers ----------------------------------------------------------
lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  return obj;
}

lv_obj_t* add_text(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_label_set_text(label, text ? text : "");
  lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
  return label;
}

lv_obj_t* add_icon(lv_obj_t* parent, const char* name, uint32_t color) {
  return add_text(parent, FONT_MDI_ICONS, color, getMdiChar(name).c_str());
}

void set_icon(lv_obj_t* label, const char* name) {
  if (label) lv_label_set_text(label, getMdiChar(name).c_str());
}

int line_height(const lv_font_t* font) { return lv_font_get_line_height(font); }

// The text without the characters `font` lacks (emoji in a playlist name
// showed as boxes, user 2026-10-19): left out, double spaces joined.
String drop_missing_glyphs(const char* text, const lv_font_t* font) {
  String out;
  if (!text) return out;
  uint32_t i = 0;
  const uint32_t length = static_cast<uint32_t>(strlen(text));
  bool space = false;
  while (i < length) {
    const uint32_t start = i;
    const uint32_t letter = lv_text_encoded_next(text, &i);
    if (i <= start) break;
    lv_font_glyph_dsc_t glyph;
    const bool shown = letter < 0x80 || lv_font_get_glyph_dsc(font, &glyph, letter, 0);
    if (!shown) continue;
    const bool is_space = letter == ' ';
    if (is_space && (space || out.length() == 0)) continue;
    space = is_space;
    for (uint32_t k = start; k < i; ++k) out += text[k];
  }
  out.trim();
  return out;
}

// One line, cut with "..." (LV_LABEL_LONG_DOT cuts only at a fixed height;
// without one a long text wrapped: settings_parts.cpp one_line).
void one_line_dots(lv_obj_t* label) {
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_height(label, line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
}

int text_width(const lv_font_t* font, const char* text) {
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

int text_height(const lv_font_t* font, const char* text, int width) {
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, width, LV_TEXT_FLAG_NONE);
  return size.y;
}

void set_shown(lv_obj_t* obj, bool shown) {
  if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == !shown) return;
  if (shown) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static bool popup_text_same(String a, String b) {
  a.trim();
  b.trim();
  return a.length() && b.length() && a.equalsIgnoreCase(b);
}

const i18n::Strings& texts() { return i18n::strings(configManager.getConfig().language); }

// The player's state on the value line under the title (empty: the classic
// header).
const char* media_state_text(const String& raw_state) {
  String state = raw_state;
  state.trim();
  state.toLowerCase();
  const auto& tr = texts();
  if (state == "playing") return tr.media_state_playing;
  if (state == "paused") return tr.media_state_paused;
  if (state == "idle") return tr.media_state_idle;
  if (state == "standby") return tr.media_state_standby;
  if (state == "off") return tr.media_state_off;
  if (state == "on") return tr.light_on;
  return i18n::entity_state_label(configManager.getConfig().language, state);
}

// ---- Colors (the popup control rule, popup_nav_style.h) ----------------------
struct MediaColors {
  uint32_t card = 0;
  uint32_t icon = 0;
  uint32_t text = 0;
  uint32_t secondary = 0;
  // The media controls, the play circle and the slider knobs: the theme's
  // icon role (ui_theme.h Roles; the old popup's too). White in the dark
  // theme and on filled cards, #464646 on the light theme's white card
  // (user 2026-10-19: white knobs vanished there, controls mixed black and
  // grey).
  uint32_t control_icon = 0;
  lv_color_t control = {};
  lv_opa_t control_opa = 0;
  bool tinted = false;
  lv_color_t raised = {};
  lv_opa_t raised_opa = 0;
};

MediaColors compute_colors(uint32_t card, uint32_t icon) {
  MediaColors c;
  c.card = card;
  c.icon = icon;
  c.text = ui_theme::text();
  c.secondary = ui_theme::text_secondary();
  c.control_icon = ui_theme::icon();
  popup_shell_control_fill(card, icon, c.control, c.control_opa, &c.tinted);
  popup_shell_control_raised_fill(card, icon, c.raised, c.raised_opa);
  return c;
}

uint32_t colors_key(const MediaColors& c) {
  uint32_t key = 2166136261u;
  for (uint32_t part : {c.card, c.icon, c.text, c.secondary, c.control_icon, lv_color_to_u32(c.control), static_cast<uint32_t>(c.control_opa),
                        lv_color_to_u32(c.raised), static_cast<uint32_t>(c.raised_opa), static_cast<uint32_t>(c.tinted)}) {
    key = (key ^ part) * 16777619u;
  }
  return key;
}

// ---- Context ------------------------------------------------------------------
enum class MediaView : uint8_t { Player, Library, Source, Group };

enum class MediaAction : uint8_t { PlayPause, Previous, Next, Shuffle, Repeat, Group, Source, Library, Back, Menu };

// The v0.9.3 popup's lv_slider (user 2026-10-22: the b487-b494 sliders that
// drew their own knob jittered; "wieder wie vorher"): LVGL draws the filled
// part and the knob and owns the drag. The look stays: a bar from edge to
// edge, the knob travelling inside it (the slider's padding is the knob's
// radius); a static track under it and a static round cap at its left end
// (the filled part starts at the padding; the knob covers the cap at 0).
struct MediaSlider {
  lv_obj_t* track = nullptr;
  lv_obj_t* cap = nullptr;
  lv_obj_t* input = nullptr;
  float value = 0.0f;
  int32_t steps = 100;
};

struct MarqueeLine {
  lv_obj_t* label = nullptr;
  int over = 0;
};

struct MediaPopupContext {
  String entity_id;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* close_button = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* value_label = nullptr;  // the shell's value line (hidden source)
  lv_obj_t* root = nullptr;         // the whole card in card coordinates
  lv_obj_t* player = nullptr;       // stays under the open menu
  lv_obj_t* list = nullptr;         // the open menu's list
  lv_obj_t* list_box = nullptr;     // the menu's box (with its outside layer, list_shield)
  lv_obj_t* list_shield = nullptr;  // a tap outside the open menu closes it
  lv_obj_t* library_status = nullptr;
  lv_obj_t* menu_button = nullptr;  // bottom right: opens and closes the menu
  // The menu's parts, built once with the player (build_menu) and only shown
  // and hidden: its head (name, the library's back button), the switch.
  lv_obj_t* menu_title = nullptr;
  lv_obj_t* menu_shadow = nullptr;  // under the box: its shadow as pictures
  lv_obj_t* menu_back = nullptr;
  lv_obj_t* menu_tabs[3] = {};
  uint32_t menu_rows_key = 0;  // what the rows show (menu_rows_of); 0: none
  lv_obj_t* volume_value_label = nullptr;
  MediaView last_menu_view = MediaView::Player;  // the menu opens at its last list
  lv_obj_t* cover_clip = nullptr;
  lv_obj_t* cover_image = nullptr;
  // The popup's cover copy has its rounded corners in its pixels, over this
  // card color (round_cover_corners); false for a copy LVGL scales.
  bool cover_baked = false;
  uint32_t cover_corner_card = 0;
  lv_obj_t* fallback_icon = nullptr;
  lv_obj_t* bars = nullptr;
  lv_obj_t* title_clip = nullptr;
  lv_obj_t* media_title_label = nullptr;
  lv_obj_t* artist_clip = nullptr;
  lv_obj_t* artist_label = nullptr;
  lv_obj_t* album_clip = nullptr;
  lv_obj_t* album_label = nullptr;
  lv_obj_t* seek_current_label = nullptr;
  lv_obj_t* seek_duration_label = nullptr;
  lv_obj_t* shuffle_label = nullptr;
  lv_obj_t* previous_label = nullptr;
  lv_obj_t* play_pause_label = nullptr;
  lv_obj_t* next_label = nullptr;
  lv_obj_t* repeat_label = nullptr;
  lv_obj_t* volume_icon_label = nullptr;
  MediaSlider seek_slider;
  MediaSlider volume_slider;
  lv_image_dsc_t* cover_dsc = nullptr;
  uint32_t cover_hash = 0;
  int cover_side = 0;
  bool centered = false;   // the layout the player was built in
  uint32_t built_colors = 0;
  MediaColors colors;
  MediaView view = MediaView::Player;
  String title;
  String icon_char;
  uint32_t icon_color = 0xFFFFFF;
  uint32_t bg_color = 0;
  String shown_title;
  String shown_line2;
  String shown_line3;
  bool available = true;
  bool is_playing = false;
  bool has_media_position = false;
  float media_position = 0.0f;
  float media_duration = 0.0f;
  uint32_t media_position_received_ms = 0;
  bool seek_dragging = false;
  bool volume_dragging = false;
  // The playing bars move only while Home Assistant reports "playing" (not
  // on the play button's own immediate switch).
  bool reported_playing = false;
  String icon_name;
  // The open source or speaker list: what it was built from, and a change
  // that waits for the end of a scroll.
  uint32_t list_signature = 0;
  bool list_dirty = false;
  // Lists taken down from one of their own rows' clicks: hidden at once,
  // deleted after the event (delete_dead_lists).
  std::vector<lv_obj_t*> dead_lists;
  bool has_volume = false;
  bool is_muted = false;
  int32_t last_volume_pct = 35;
  int32_t shown_volume_pct = -1;
  uint8_t shown_volume_icon = 255;
  std::vector<MarqueeLine> lines;
  size_t line_index = 0;
  uint8_t line_phase = 0;
  uint32_t line_phase_ms = 0;
  lv_timer_t* progress_timer = nullptr;
  lv_timer_t* motion_timer = nullptr;
  lv_timer_t* library_timer = nullptr;
  uint32_t library_items_version = 0;
  size_t library_rows = 0;
  MediaBrowseStatus library_status_value = MediaBrowseStatus::Idle;
  uint32_t library_status_ms = 0;
};

static MediaPopupContext* g_media_popup_ctx = nullptr;

bool popup_visible(const MediaPopupContext* ctx) {
  return ctx && ctx->card && !lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
}

// The surface the body's colors belong to: the one it was last shown with.
// The next opening's show_popup_shell maps them to that opener's surface
// (popup_follow_surface); while shown both are the same.
ui_theme::SurfaceScope body_scope(const MediaPopupContext* ctx) {
  return ui_theme::SurfaceScope(ctx && ctx->card ? popup_drawn_surface(ctx->card) : ui_theme::kThemeSurface);
}

static void* alloc_popup_memory(size_t bytes, bool prefer_psram = false) {
  if (!bytes) return nullptr;
  void* data = nullptr;
  if (prefer_psram) {
    data = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (!data) {
    data = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  }
  return data;
}

static void free_cover_dsc(lv_image_dsc_t*& dsc) {
  if (!dsc) return;
  // Also discard the cache entry keyed by the descriptor address; otherwise
  // a later descriptor at the same malloc address can display the old image.
  lv_image_cache_drop(dsc);
  if (dsc->data) {
    free(const_cast<uint8_t*>(dsc->data));
  }
  free(dsc);
  dsc = nullptr;
}

// The cover's rounded corners drawn into its own pixels over the card's
// color, once per picture and card color: with clip_corner LVGL drew the
// cover through two ARGB8888 layers on every frame (V2 b492: the player's
// block ~33 ms of a ~100 ms opening, ~9 ms without a cover). The edge pixels
// are mixed by how much of them lies inside the arc, like LVGL's own radius.
static uint16_t cover_pixel(const uint8_t* at, bool swapped) {
  return swapped ? static_cast<uint16_t>((at[0] << 8) | at[1]) : static_cast<uint16_t>(at[0] | (at[1] << 8));
}

static void set_cover_pixel(uint8_t* at, uint16_t value, bool swapped) {
  at[swapped ? 1 : 0] = static_cast<uint8_t>(value & 0xFF);
  at[swapped ? 0 : 1] = static_cast<uint8_t>(value >> 8);
}

static bool round_cover_corners(lv_image_dsc_t* dsc, int radius, uint32_t card_rgb) {
  if (!dsc || !dsc->data || radius <= 0) return false;
  const lv_color_format_t cf = static_cast<lv_color_format_t>(dsc->header.cf);
  if (cf != LV_COLOR_FORMAT_RGB565 && cf != LV_COLOR_FORMAT_RGB565_SWAPPED) return false;
  const bool swapped = cf == LV_COLOR_FORMAT_RGB565_SWAPPED;
  const int w = dsc->header.w;
  const int h = dsc->header.h;
  const int stride = dsc->header.stride ? dsc->header.stride : w * 2;
  if (w <= 0 || h <= 0 || static_cast<uint32_t>(stride) * h > dsc->data_size) return false;
  radius = std::min(radius, std::min(w, h) / 2);
  const lv_color_t card = lv_color_hex(card_rgb);
  uint8_t* data = const_cast<uint8_t*>(dsc->data);
  for (int y = 0; y < radius; ++y) {
    for (int x = 0; x < radius; ++x) {
      const float dx = radius - (x + 0.5f);
      const float dy = radius - (y + 0.5f);
      const float inside = radius - sqrtf(dx * dx + dy * dy) + 0.5f;
      if (inside >= 1.0f) continue;
      const uint8_t keep = inside <= 0.0f ? 0 : static_cast<uint8_t>(inside * 255.0f);
      const int xs[2] = {x, w - 1 - x};
      const int ys[2] = {y, h - 1 - y};
      for (int yi : ys) {
        for (int xi : xs) {
          uint8_t* at = data + yi * stride + xi * 2;
          const uint16_t v = cover_pixel(at, swapped);
          const lv_color_t pixel = lv_color_make(static_cast<uint8_t>(((v >> 11) & 0x1F) << 3),
                                                 static_cast<uint8_t>(((v >> 5) & 0x3F) << 2),
                                                 static_cast<uint8_t>((v & 0x1F) << 3));
          set_cover_pixel(at, lv_color_to_u16(lv_color_mix(pixel, card, keep)), swapped);
        }
      }
    }
  }
  return true;
}

// The clip around the cover: a baked cover is a plain picture (no
// clip_corner, no fill under it); a scaled copy keeps the clip; without a
// cover the clip shows the fallback icon on the control fill.
static void style_cover_clip(MediaPopupContext* ctx, bool cover_shown) {
  if (!ctx->cover_clip) return;
  const bool baked = cover_shown && ctx->cover_baked;
  lv_obj_set_style_clip_corner(ctx->cover_clip, cover_shown && !baked, 0);
  lv_obj_set_style_bg_opa(ctx->cover_clip, baked ? LV_OPA_TRANSP : ctx->colors.control_opa, 0);
}

static lv_image_dsc_t* clone_cover_dsc(const lv_image_dsc_t* src) {
  if (!src || !src->data || src->data_size == 0) return nullptr;

  uint8_t* data = static_cast<uint8_t*>(alloc_popup_memory(src->data_size, true));
  if (!data) return nullptr;
  memcpy(data, src->data, src->data_size);

  lv_image_dsc_t* clone = static_cast<lv_image_dsc_t*>(alloc_popup_memory(sizeof(lv_image_dsc_t)));
  if (!clone) {
    free(data);
    return nullptr;
  }
  memcpy(clone, src, sizeof(lv_image_dsc_t));
  clone->data = data;
  return clone;
}

// The cover in the popup's size: the Bridge sends it in the size of "Cover
// links" (bridge_images.cpp coverEdge), the centred cover takes a smaller
// copy made once per picture.
static void update_cover(MediaPopupContext* ctx, const lv_image_dsc_t* cover_dsc, uint32_t cover_hash) {
  if (!ctx || !ctx->cover_image || !ctx->fallback_icon) return;
  if (cover_dsc && ctx->cover_hash == cover_hash && ctx->cover_dsc) {
    lv_obj_clear_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx->fallback_icon, LV_OBJ_FLAG_HIDDEN);
    style_cover_clip(ctx, true);
    return;
  }

  lv_image_dsc_t* old = ctx->cover_dsc;
  ctx->cover_dsc = nullptr;
  ctx->cover_hash = 0;

  if (cover_dsc) {
    bool exact = true;
    ctx->cover_dsc = make_media_popup_cover_dsc(cover_dsc, static_cast<uint16_t>(ctx->cover_side));
    if (!ctx->cover_dsc) {
      // Another format: LVGL scales the copy while drawing.
      ctx->cover_dsc = clone_cover_dsc(cover_dsc);
      exact = false;
    }
    ctx->cover_baked = exact && ctx->cover_dsc &&
                       round_cover_corners(ctx->cover_dsc, lv_obj_get_style_radius(ctx->cover_clip, LV_PART_MAIN),
                                           ctx->colors.card);
    ctx->cover_corner_card = ctx->colors.card;
    if (ctx->cover_dsc) {
      ctx->cover_hash = cover_hash;
      style_cover_clip(ctx, true);
      lv_image_set_inner_align(ctx->cover_image, exact ? LV_IMAGE_ALIGN_CENTER : LV_IMAGE_ALIGN_COVER);
      lv_image_set_src(ctx->cover_image, ctx->cover_dsc);
      lv_obj_clear_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ctx->fallback_icon, LV_OBJ_FLAG_HIDDEN);
      free_cover_dsc(old);
      return;
    }
  }

  lv_obj_add_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->fallback_icon, LV_OBJ_FLAG_HIDDEN);
  style_cover_clip(ctx, false);
  free_cover_dsc(old);
}

// ---- Sliders ---------------------------------------------------------------------
// Steps as in v0.9.3: 0..100 volume, 0..1000 position.
void slider_show(MediaSlider& slider, float fraction) {
  if (!slider.input) return;
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;
  slider.value = fraction;
  // A value from outside never moves the knob under the finger.
  if (!lv_slider_is_dragged(slider.input)) {
    lv_slider_set_value(slider.input, static_cast<int32_t>(fraction * slider.steps + 0.5f), LV_ANIM_OFF);
  }
}

void slider_set_shown(MediaSlider& slider, bool shown) {
  set_shown(slider.track, shown);
  set_shown(slider.cap, shown);
  set_shown(slider.input, shown);
}

void slider_set_enabled(MediaSlider& slider, bool enabled) {
  for (lv_obj_t* obj : {slider.track, slider.cap, slider.input}) {
    if (!obj || lv_obj_has_state(obj, LV_STATE_DISABLED) == !enabled) continue;
    if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
  }
}

// The slider's value as 0..1.
float slider_input_fraction(const MediaSlider& slider) {
  if (!slider.input || slider.steps <= 0) return 0.0f;
  return static_cast<float>(lv_slider_get_value(slider.input)) / static_cast<float>(slider.steps);
}

void make_slider(MediaSlider& slider, lv_obj_t* parent, const MediaColors& c, int x, int mid_y, int width, int height,
                 int knob, int32_t steps, int click_pad, lv_event_cb_t handler, MediaPopupContext* ctx) {
  const int radius = knob / 2;
  const lv_color_t fill = lv_color_hex(c.tinted ? c.icon : c.control_icon);
  slider.steps = steps;
  // The unused bar like an info pill (the control fill), the used part in
  // the icon color when the controls take its hue, else the text color.
  slider.track = plain(parent);
  lv_obj_set_pos(slider.track, x, mid_y - height / 2);
  lv_obj_set_size(slider.track, width, height);
  lv_obj_set_style_radius(slider.track, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(slider.track, c.control, 0);
  lv_obj_set_style_bg_opa(slider.track, c.control_opa, 0);
  lv_obj_set_style_opa(slider.track, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);
  // The filled part's round left end, as wide as the knob that covers it at 0.
  slider.cap = plain(parent);
  lv_obj_set_pos(slider.cap, x, mid_y - height / 2);
  lv_obj_set_size(slider.cap, knob, height);
  lv_obj_set_style_radius(slider.cap, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(slider.cap, fill, 0);
  lv_obj_set_style_bg_opa(slider.cap, LV_OPA_COVER, 0);
  lv_obj_set_style_opa(slider.cap, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);
  // The slider itself: its padding is the knob's radius, so the knob's
  // middle runs from x + r to x + width - r.
  slider.input = lv_slider_create(parent);
  lv_obj_remove_style_all(slider.input);
  lv_obj_set_pos(slider.input, x, mid_y - height / 2);
  lv_obj_set_size(slider.input, width, height);
  lv_obj_set_style_pad_left(slider.input, radius, LV_PART_MAIN);
  lv_obj_set_style_pad_right(slider.input, radius, LV_PART_MAIN);
  lv_slider_set_range(slider.input, 0, steps);
  lv_slider_set_value(slider.input, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider.input, fill, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider.input, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider.input, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  // The knob in the icon role (ui_control_style.h): white in the dark theme
  // and on filled cards, dark on the light theme's white card. LVGL makes it
  // as high as the bar plus its padding.
  lv_obj_set_style_pad_all(slider.input, (knob - height) / 2, LV_PART_KNOB);
  lv_obj_set_style_bg_color(slider.input, lv_color_hex(c.control_icon), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(slider.input, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(slider.input, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_opa(slider.input, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);
  lv_obj_set_ext_click_area(slider.input, click_pad);
  lv_obj_remove_flag(slider.input, LV_OBJ_FLAG_SCROLLABLE);
  for (lv_event_code_t code : {LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
    lv_obj_add_event_cb(slider.input, handler, code, ctx);
  }
}

// ---- Volume -----------------------------------------------------------------------
static uint8_t volume_icon_bucket_for_percent(int32_t pct, bool muted) {
  if (muted || pct <= 0) return 0;
  if (pct < 35) return 1;
  if (pct < 70) return 2;
  return 3;
}

static const char* volume_icon_for_bucket(uint8_t bucket) {
  switch (bucket) {
    case 1: return "volume-low";
    case 2: return "volume-medium";
    case 3: return "volume-high";
    default: return "volume-off";
  }
}

static void set_volume_widgets(MediaPopupContext* ctx, int32_t pct, bool muted, bool update_slider = true) {
  if (!ctx) return;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  ctx->is_muted = muted || pct == 0;
  if (pct > 0) ctx->last_volume_pct = pct;
  if (update_slider) slider_show(ctx->volume_slider, ctx->is_muted ? 0.0f : static_cast<float>(pct) / 100.0f);
  if (ctx->volume_icon_label) {
    const uint8_t bucket = volume_icon_bucket_for_percent(pct, ctx->is_muted);
    if (ctx->shown_volume_icon != bucket) {
      ctx->shown_volume_icon = bucket;
      lv_label_set_text(ctx->volume_icon_label, getMdiChar(volume_icon_for_bucket(bucket)).c_str());
    }
  }
  // The number in its fixed place right of the bar, as in v0.9.3.
  if (ctx->volume_value_label && ctx->shown_volume_pct != pct) {
    ctx->shown_volume_pct = pct;
    char text[8];
    snprintf(text, sizeof(text), "%ld%%", static_cast<long>(pct));
    lv_label_set_text(ctx->volume_value_label, text);
  }
}

// As in v0.9.3: the reported volume shows at once; only a drag in progress
// keeps the finger's value.
static void update_volume(MediaPopupContext* ctx, const MediaPopupInit& init) {
  if (!ctx || !ctx->volume_slider.input) return;
  ctx->has_volume = init.has_volume;
  slider_set_enabled(ctx->volume_slider, init.has_volume);
  if (ctx->volume_dragging) return;

  float volume = init.volume_level;
  if (volume < 0.0f) volume = 0.0f;
  if (volume > 1.0f) volume = 1.0f;
  int32_t pct = static_cast<int32_t>(volume * 100.0f + 0.5f);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  set_volume_widgets(ctx, pct, init.is_muted);
}

// ---- Position ----------------------------------------------------------------------
static void format_media_time(float seconds, char* out, size_t out_len) {
  if (!out || out_len == 0) return;
  if (seconds < 0.0f) seconds = 0.0f;
  uint32_t total = static_cast<uint32_t>(seconds + 0.5f);
  const uint32_t hours = total / 3600U;
  const uint32_t minutes = (total % 3600U) / 60U;
  const uint32_t secs = total % 60U;
  if (hours > 0) {
    snprintf(out, out_len, "%lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(secs));
  } else {
    snprintf(out, out_len, "%lu:%02lu",
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(secs));
  }
}

static float current_media_position(const MediaPopupContext* ctx) {
  if (!ctx || !ctx->has_media_position) return 0.0f;
  float position = ctx->media_position;
  if (ctx->is_playing && !ctx->seek_dragging && ctx->media_position_received_ms != 0) {
    position += static_cast<float>(static_cast<uint32_t>(millis() - ctx->media_position_received_ms)) / 1000.0f;
  }
  if (position < 0.0f) position = 0.0f;
  // "Repeat one" starts the song again; Home Assistant does not always report
  // it (the time bar stopped at the end until the next report).
  if (ctx->is_playing && ctx->media_duration > 0.0f && position >= ctx->media_duration &&
      extras_of(ctx->entity_id).repeat.equalsIgnoreCase("one")) {
    position = fmodf(position, ctx->media_duration);
  }
  if (position > ctx->media_duration) position = ctx->media_duration;
  return position;
}

static void set_seek_widgets(MediaPopupContext* ctx, float position, bool update_slider) {
  if (!ctx || !ctx->seek_slider.input) return;
  const bool known = ctx->has_media_position && ctx->media_duration > 0.0f;
  if (!known) {
    slider_set_enabled(ctx->seek_slider, false);
    slider_set_shown(ctx->seek_slider, false);
    set_shown(ctx->seek_current_label, false);
    set_shown(ctx->seek_duration_label, false);
    if (update_slider) slider_show(ctx->seek_slider, 0.0f);
    return;
  }

  slider_set_enabled(ctx->seek_slider, true);
  slider_set_shown(ctx->seek_slider, true);
  set_shown(ctx->seek_current_label, true);
  set_shown(ctx->seek_duration_label, true);
  if (position < 0.0f) position = 0.0f;
  if (position > ctx->media_duration) position = ctx->media_duration;
  if (update_slider) slider_show(ctx->seek_slider, position / ctx->media_duration);

  char current_text[16];
  char duration_text[16];
  format_media_time(position, current_text, sizeof(current_text));
  format_media_time(ctx->media_duration, duration_text, sizeof(duration_text));
  if (ctx->seek_current_label) lv_label_set_text(ctx->seek_current_label, current_text);
  if (ctx->seek_duration_label) lv_label_set_text(ctx->seek_duration_label, duration_text);
}

// As in v0.9.3: the reported position shows at once, except during a drag.
static void update_seek(MediaPopupContext* ctx, const MediaPopupInit& init) {
  if (!ctx || !ctx->seek_slider.input) return;
  ctx->is_playing = init.is_playing;
  const bool known = init.has_media_position && init.media_duration > 0.0f;
  if (ctx->seek_dragging) {
    // A position that went away ends the drag without a command.
    ctx->has_media_position = known;
    ctx->media_duration = known ? init.media_duration : 0.0f;
    return;
  }
  ctx->has_media_position = known;
  ctx->media_duration = known ? init.media_duration : 0.0f;
  ctx->media_position = known ? init.media_position : 0.0f;
  ctx->media_position_received_ms = known
                                        ? (init.media_position_received_ms != 0 ? init.media_position_received_ms
                                                                                : millis())
                                        : 0;
  set_seek_widgets(ctx, current_media_position(ctx), true);
}

static void media_progress_timer_cb(lv_timer_t* timer) {
  MediaPopupContext* ctx = timer ? static_cast<MediaPopupContext*>(lv_timer_get_user_data(timer)) : nullptr;
  if (!ctx || !ctx->card || lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN) || ctx->seek_dragging) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  set_seek_widgets(ctx, current_media_position(ctx), true);
}

// ---- Playing bars: their height moves, the round ends keep their radius ----------
// One object draws all four bars, so a frame redraws one small area.
float bar_level(uint32_t now, int index, bool playing) {
  static const uint16_t kPeriods[4] = {900, 600, 1100, 750};
  static const uint16_t kOffsets[4] = {0, 300, 700, 150};
  static const uint8_t kStill[4] = {35, 70, 50, 85};
  if (!playing) return kStill[index] / 100.0f;
  const uint32_t period = kPeriods[index];
  const uint32_t t = (now + kOffsets[index]) % (2 * period);
  float x = t < period ? static_cast<float>(t) / period : 2.0f - static_cast<float>(t) / period;
  x = x * x * (3.0f - 2.0f * x);
  return 0.25f + 0.75f * x;
}

void bars_draw_cb(lv_event_t* e) {
  lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!obj || !layer) return;
  lv_area_t area;
  lv_obj_get_coords(obj, &area);
  const int height = lv_area_get_height(&area);
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
  dsc.bg_opa = LV_OPA_COVER;
  dsc.radius = ml::kBarWidth / 2;
  const uint32_t now = lv_tick_get();
  const bool playing = ctx && ctx->reported_playing;
  for (int i = 0; i < 4; ++i) {
    const int bar = std::max(ml::kBarWidth, static_cast<int>(height * bar_level(now, i, playing) + 0.5f));
    lv_area_t a;
    a.x1 = area.x1 + i * (ml::kBarWidth + ml::kBarGap);
    a.x2 = a.x1 + ml::kBarWidth - 1;
    a.y2 = area.y2;
    a.y1 = area.y2 - bar + 1;
    lv_draw_rect(layer, &dsc, &a);
  }
}

// ---- Long texts ------------------------------------------------------------------
void marquee_step(MediaPopupContext* ctx, uint32_t now) {
  if (ctx->lines.empty()) return;
  MarqueeLine& line = ctx->lines[ctx->line_index % ctx->lines.size()];
  const uint32_t move = static_cast<uint32_t>(line.over) * 1000U / kMarqueePxPerSecond;
  const uint32_t elapsed = now - ctx->line_phase_ms;
  const uint32_t length = (ctx->line_phase == 0 || ctx->line_phase == 2) ? kMarqueePauseMs : move;
  const float f = length ? std::min(1.0f, static_cast<float>(elapsed) / length) : 1.0f;
  if (ctx->line_phase == 1) lv_obj_set_x(line.label, -static_cast<int>(line.over * f));
  if (ctx->line_phase == 3) lv_obj_set_x(line.label, -static_cast<int>(line.over * (1.0f - f)));
  if (elapsed < length) return;
  ctx->line_phase_ms = now;
  if (++ctx->line_phase == 4) {
    ctx->line_phase = 0;
    lv_obj_set_x(line.label, 0);
    ctx->line_index = (ctx->line_index + 1) % ctx->lines.size();
  }
}

static void media_motion_timer_cb(lv_timer_t* timer) {
  MediaPopupContext* ctx = timer ? static_cast<MediaPopupContext*>(lv_timer_get_user_data(timer)) : nullptr;
  if (!popup_visible(ctx) || ctx->view != MediaView::Player) return;
  if (ctx->bars && ctx->reported_playing) lv_obj_invalidate(ctx->bars);
  marquee_step(ctx, lv_tick_get());
}

// One line in its clip; a longer one joins the moving lines.
void set_line(MediaPopupContext* ctx, lv_obj_t* clip, lv_obj_t* label, const String& raw, int width, bool centred) {
  if (!clip || !label) return;
  const String text = drop_missing_glyphs(raw.c_str(), lv_obj_get_style_text_font(label, LV_PART_MAIN));
  set_shown(clip, text.length() > 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(label, LV_SIZE_CONTENT);
  lv_label_set_text(label, text.c_str());
  const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  const int w = text_width(font, text.c_str());
  lv_obj_set_height(clip, line_height(font));
  const int over = w - width;
  lv_obj_set_pos(label, over < 0 && centred ? -over / 2 : 0, 0);
  if (over > 0) ctx->lines.push_back({label, over});
}

static void apply_media_text(MediaPopupContext* ctx, const MediaPopupInit& init) {
  if (!ctx || !ctx->media_title_label) return;
  String media_title = init.media_title;
  media_title.trim();
  if (!media_title.length()) {
    media_title = texts().media_no_playback;
  }
  const MediaExtras& extras = extras_of(init.entity_id);
  String artist = extras.artist;
  String album = extras.album;
  artist.trim();
  album.trim();
  String line2;
  String line3;
  if (ctx->centered) {
    line2 = artist;
    if (album.length() && !popup_text_same(album, artist)) {
      line2 = line2.length() ? line2 + " \xC2\xB7 " + album : album;
    }
  } else {
    line2 = artist;
    line3 = popup_text_same(album, artist) ? String() : album;
  }
  if (!line2.length() && !line3.length()) {
    // No artist or album: the tile's line (app or source).
    line2 = init.media_subtitle;
    line2.trim();
  }
  if (popup_text_same(line2, media_title)) line2 = "";
  if (popup_text_same(line3, media_title)) line3 = "";
  if (media_title == ctx->shown_title && line2 == ctx->shown_line2 && line3 == ctx->shown_line3) return;
  ctx->shown_title = media_title;
  ctx->shown_line2 = line2;
  ctx->shown_line3 = line3;

  for (const MarqueeLine& line : ctx->lines) lv_obj_set_x(line.label, 0);
  ctx->lines.clear();
  ctx->line_index = 0;
  ctx->line_phase = 0;
  ctx->line_phase_ms = lv_tick_get();
  const int width = lv_obj_get_style_width(ctx->title_clip, LV_PART_MAIN);
  if (!ctx->centered) {
    // Beside the cover the title wraps to two lines first; a longer title
    // moves on one line.
    const lv_font_t* font = popup_layout::font32();
    const String clean = drop_missing_glyphs(media_title.c_str(), font);
    const int wrapped = text_height(font, clean.c_str(), width);
    if (wrapped <= 2 * line_height(font) + 1) {
      lv_label_set_long_mode(ctx->media_title_label, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(ctx->media_title_label, width);
      lv_obj_set_pos(ctx->media_title_label, 0, 0);
      lv_label_set_text(ctx->media_title_label, clean.c_str());
      lv_obj_set_height(ctx->title_clip, wrapped);
      set_shown(ctx->title_clip, true);
    } else {
      set_line(ctx, ctx->title_clip, ctx->media_title_label, media_title, width, false);
    }
  } else {
    set_line(ctx, ctx->title_clip, ctx->media_title_label, media_title, width, true);
  }
  set_line(ctx, ctx->artist_clip, ctx->artist_label, line2, width, ctx->centered);
  if (!ctx->centered) set_line(ctx, ctx->album_clip, ctx->album_label, line3, width, false);
}

// ---- Buttons ----------------------------------------------------------------------
lv_obj_t* round_button(lv_obj_t* parent, const MediaColors& c, int size, const char* icon, uint32_t icon_color,
                       lv_obj_t** label_out) {
  lv_obj_t* btn = plain(parent);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(btn, size, size);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_opa(btn, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);
  // A press shows the control fill, like the pressed close button.
  popup_nav_style::style_press_fill(btn, c.control, c.control_opa);
  disable_pressed_button_animation(btn);
  lv_obj_t* label = add_icon(btn, icon, icon_color);
  lv_obj_center(label);
  if (label_out) *label_out = label;
  return btn;
}

// Shuffle and repeat: on with the control fill (a press one step more), off
// only their icon in the secondary color.
void style_toggle(lv_obj_t* label, const MediaColors& c, bool on) {
  if (!label) return;
  lv_obj_t* btn = lv_obj_get_parent(label);
  popup_nav_style::set_bg(btn, c.control, on ? c.control_opa : LV_OPA_TRANSP, LV_PART_MAIN);
  popup_nav_style::set_bg(btn, on ? c.raised : c.control, on ? c.raised_opa : c.control_opa,
                          LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(label, lv_color_hex(on ? c.control_icon : c.secondary), 0);
}

static void set_control_enabled(lv_obj_t* obj, bool enabled) {
  if (!obj || lv_obj_has_state(obj, LV_STATE_DISABLED) == !enabled) return;
  if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
  else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

// Home Assistant disables the controls of an unavailable player; the buttons
// dim (LV_OPA_30, set at creation) and the handlers check `available`.
static void apply_availability(MediaPopupContext* ctx) {
  for (lv_obj_t* label : {ctx->shuffle_label, ctx->previous_label, ctx->play_pause_label, ctx->next_label,
                          ctx->repeat_label, ctx->volume_icon_label}) {
    set_control_enabled(label ? lv_obj_get_parent(label) : nullptr, ctx->available);
  }
  set_control_enabled(ctx->menu_button, ctx->available);
  if (!ctx->available) {
    slider_set_enabled(ctx->volume_slider, false);
    slider_set_enabled(ctx->seek_slider, false);
  }
}

bool seek_allowed(const MediaPopupContext* ctx) {
  const MediaExtras& extras = extras_of(ctx->entity_id);
  return !extras.has_features || (extras.features & kFeatureSeek);
}

// The menu's lists the player offers (its feature bits): speakers to group,
// the source or sound mode, the library.
bool menu_view_offered(const MediaPopupContext* ctx, MediaView view) {
  const MediaExtras& x = extras_of(ctx->entity_id);
  if (!x.has_features) return false;
  const uint32_t f = x.features;
  switch (view) {
    case MediaView::Group: return (f & kFeatureGrouping) && !x.candidates.empty();
    case MediaView::Source:
      return ((f & kFeatureSelectSource) && !x.sources.empty()) || ((f & kFeatureSoundMode) && !x.sound_modes.empty());
    case MediaView::Library: return (f & kFeatureBrowse) != 0;
    default: return false;
  }
}

// The switch's order (user 2026-10-22 mockup): speakers, source, library.
constexpr MediaView kMenuViews[] = {MediaView::Group, MediaView::Source, MediaView::Library};

// The controls the player offers (its feature bits; an older Bridge sends
// none: previous, play and next as before, nothing more).
static void apply_extras(MediaPopupContext* ctx) {
  const MediaExtras& x = extras_of(ctx->entity_id);
  const bool known = x.has_features;
  const uint32_t f = x.features;
  auto button = [](lv_obj_t* label) { return label ? lv_obj_get_parent(label) : nullptr; };
  set_shown(button(ctx->shuffle_label), known && (f & kFeatureShuffle) && x.has_shuffle);
  style_toggle(ctx->shuffle_label, ctx->colors, x.shuffle);
  const bool repeat_on = x.repeat.length() && !x.repeat.equalsIgnoreCase("off");
  set_shown(button(ctx->repeat_label), known && (f & kFeatureRepeat) && x.repeat.length());
  style_toggle(ctx->repeat_label, ctx->colors, repeat_on);
  set_icon(ctx->repeat_label, x.repeat.equalsIgnoreCase("one") ? "repeat-once" : "repeat");
  set_shown(button(ctx->previous_label), !known || (f & kFeaturePrevious));
  set_shown(button(ctx->next_label), !known || (f & kFeatureNext));
  set_shown(button(ctx->play_pause_label), !known || (f & (kFeaturePlay | kFeaturePause)));
  set_shown(button(ctx->volume_icon_label), !known || (f & (kFeatureVolumeMute | kFeatureVolumeSet)));
  slider_set_shown(ctx->volume_slider, !known || (f & kFeatureVolumeSet));
  set_shown(ctx->volume_value_label, !known || (f & kFeatureVolumeSet));
  // The menu button only while the player offers one of the menu's lists.
  bool menu = false;
  for (MediaView view : kMenuViews) menu = menu || menu_view_offered(ctx, view);
  set_shown(ctx->menu_button, menu || ctx->view != MediaView::Player);
}

// ---- The player ------------------------------------------------------------------
static void on_media_command(lv_event_t* e);
static void on_volume_mute_click(lv_event_t* e);
static void on_volume_slider_event(lv_event_t* e);
static void on_seek_slider_event(lv_event_t* e);

lv_obj_t* control_button(MediaPopupContext* ctx, int x, int y, const char* icon, MediaAction action, lv_obj_t** label) {
  lv_obj_t* btn = round_button(ctx->player, ctx->colors, ml::kButton, icon, ctx->colors.control_icon, label);
  lv_obj_set_pos(btn, x, y);
  lv_obj_add_event_cb(btn, on_media_command, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(action)));
  return btn;
}

void reset_player_parts(MediaPopupContext* ctx) {
  ctx->player = nullptr;
  ctx->cover_clip = ctx->cover_image = ctx->fallback_icon = ctx->bars = nullptr;
  ctx->title_clip = ctx->media_title_label = ctx->artist_clip = ctx->artist_label = nullptr;
  ctx->album_clip = ctx->album_label = nullptr;
  ctx->seek_current_label = ctx->seek_duration_label = nullptr;
  ctx->shuffle_label = ctx->previous_label = ctx->play_pause_label = ctx->next_label = nullptr;
  ctx->repeat_label = ctx->volume_icon_label = ctx->volume_value_label = nullptr;
  ctx->seek_slider = MediaSlider();
  ctx->volume_slider = MediaSlider();
  ctx->lines.clear();
  ctx->shown_title = ctx->shown_line2 = ctx->shown_line3 = "";
  ctx->shown_volume_pct = -1;
  ctx->shown_volume_icon = 255;
}

static void build_menu(MediaPopupContext* ctx);
void delete_menu(MediaPopupContext* ctx);

static void build_player(MediaPopupContext* ctx, bool centered) {
  static uint32_t last_log_ms = 0;
  if (!last_log_ms || millis() - last_log_ms > 5000) {
    last_log_ms = millis();
    Serial.printf("[MediaPopup] Player built: %s, colors %08lx\n", centered ? "cover centred" : "cover left",
                  static_cast<unsigned long>(colors_key(ctx->colors)));
  }
  const int side_before = ctx->cover_side;
  if (ctx->player) lv_obj_delete(ctx->player);
  // The menu and its button live on the card's root (over the player).
  delete_menu(ctx);
  if (ctx->menu_button) lv_obj_delete(ctx->menu_button);
  ctx->menu_button = nullptr;
  reset_player_parts(ctx);
  ctx->centered = centered;
  ctx->built_colors = colors_key(ctx->colors);
  const MediaColors& c = ctx->colors;
  const int W = popup_layout::kCardWidth;
  const int H = popup_layout::kCardHeight;
  const int L = ml::kEdge;
  const int g = Device::kGridGap;
  const int title_block = line_height(popup_layout::font32()) + ml::kTextGap + line_height(popup_layout::font24());
  int cover_x = L;
  int cover_y = ml::kLeftCoverTop;
  int side = ml::kLeftCoverSide;
  if (centered) {
    // Today's popup: the cover centred under the head, title and artist
    // one gap over the time knob.
    const int top = ml::kHeadTop + ml::kDisc + ml::kHeadGap;
    const int title_top = ml::kSeekKnobTop - g - title_block;
    side = std::max(popup_layout::scale(80), title_top - g - top);
    cover_y = top;
    cover_x = (W - side) / 2;
  }
  ctx->cover_side = side;
  // The other layout's cover size, or corners rounded over another card
  // color: made again from the next picture.
  if (side != side_before || (ctx->cover_baked && ctx->cover_corner_card != c.card)) {
    free_cover_dsc(ctx->cover_dsc);
    ctx->cover_hash = 0;
    ctx->cover_baked = false;
  }

  ctx->player = plain(ctx->root);
  lv_obj_set_size(ctx->player, W, H);
  if (ctx->view != MediaView::Player) lv_obj_add_flag(ctx->player, LV_OBJ_FLAG_HIDDEN);

  // Cover: the picture in a clip with the popup's cover radius.
  ctx->cover_clip = plain(ctx->player);
  lv_obj_set_pos(ctx->cover_clip, cover_x, cover_y);
  lv_obj_set_size(ctx->cover_clip, side, side);
  ui_surface_style::apply_radius(ctx->cover_clip, popup_layout::scale480(18), 0);
  lv_obj_set_style_clip_corner(ctx->cover_clip, true, 0);
  lv_obj_set_style_bg_color(ctx->cover_clip, c.control, 0);
  lv_obj_set_style_bg_opa(ctx->cover_clip, c.control_opa, 0);
  ctx->cover_image = lv_image_create(ctx->cover_clip);
  lv_obj_set_size(ctx->cover_image, side, side);
  lv_obj_center(ctx->cover_image);
  lv_obj_add_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
  ctx->fallback_icon = add_text(ctx->cover_clip, FONT_MDI_ICONS, ui_theme::icon(), ctx->icon_char.c_str());
  lv_obj_center(ctx->fallback_icon);
  if (ctx->cover_dsc) {
    lv_image_set_src(ctx->cover_image, ctx->cover_dsc);
    lv_obj_remove_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx->fallback_icon, LV_OBJ_FLAG_HIDDEN);
  }
  style_cover_clip(ctx, ctx->cover_dsc != nullptr);

  // Text: beside the cover (vertically centred, bars on top) or under it.
  lv_obj_t* info = plain(ctx->player);
  lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(info, ml::kTextGap, 0);
  int text_w = W - 2 * L;
  if (!centered) {
    const int x = cover_x + side + L;
    text_w = W - L - x;
    lv_obj_set_pos(info, x, cover_y);
    lv_obj_set_size(info, text_w, side);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ctx->bars = plain(info);
    lv_obj_set_size(ctx->bars, 4 * ml::kBarWidth + 3 * ml::kBarGap, ml::kBarsHeight);
    // The bars' color; the object draws them itself (bars_draw_cb).
    lv_obj_set_style_bg_color(ctx->bars, lv_color_hex(c.icon), 0);
    lv_obj_set_style_margin_bottom(ctx->bars, g - ml::kTextGap, 0);
    lv_obj_add_event_cb(ctx->bars, bars_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
  } else {
    lv_obj_set_pos(info, L, ml::kSeekKnobTop - g - title_block);
    lv_obj_set_size(info, text_w, title_block);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  }
  auto clip_line = [&](lv_obj_t*& clip, lv_obj_t*& label, const lv_font_t* font, uint32_t color) {
    clip = plain(info);
    lv_obj_set_size(clip, text_w, line_height(font));
    label = add_text(clip, font, color, "");
  };
  clip_line(ctx->title_clip, ctx->media_title_label, popup_layout::font32(), c.text);
  clip_line(ctx->artist_clip, ctx->artist_label, popup_layout::font24(), c.secondary);
  if (!centered) clip_line(ctx->album_clip, ctx->album_label, popup_layout::font20(), c.secondary);

  // Time bar from edge to edge, the times under it on the edges.
  make_slider(ctx->seek_slider, ctx->player, c, L, ml::kSeekMid, W - 2 * L, ml::kSeekHeight, ml::kSeekKnob, 1000,
              popup_layout::scale(18), on_seek_slider_event, ctx);
  const int time_top = ml::kSeekMid + ml::kSeekHeight / 2 + ml::kTimeGap;
  ctx->seek_current_label = add_text(ctx->player, popup_layout::font20(), c.secondary, "--:--");
  lv_obj_set_pos(ctx->seek_current_label, L, time_top);
  ctx->seek_duration_label = add_text(ctx->player, popup_layout::font20(), c.secondary, "--:--");
  lv_obj_align(ctx->seek_duration_label, LV_ALIGN_TOP_RIGHT, -L, time_top);

  // Transport at today's spacing; shuffle and repeat outside.
  auto cx = [&](int k) { return W / 2 + (k - 2) * ml::kStep - ml::kButton / 2; };
  control_button(ctx, cx(0), ml::kTransportTop, "shuffle-variant", MediaAction::Shuffle, &ctx->shuffle_label);
  control_button(ctx, cx(1), ml::kTransportTop, "skip-previous", MediaAction::Previous, &ctx->previous_label);
  lv_obj_t* play = control_button(ctx, cx(2), ml::kTransportTop, "play", MediaAction::PlayPause, &ctx->play_pause_label);
  control_button(ctx, cx(3), ml::kTransportTop, "skip-next", MediaAction::Next, &ctx->next_label);
  control_button(ctx, cx(4), ml::kTransportTop, "repeat", MediaAction::Repeat, &ctx->repeat_label);
  // Play stays the round button in the icon role with the card's color.
  lv_obj_set_style_bg_color(play, lv_color_hex(c.control_icon), 0);
  lv_obj_set_style_bg_opa(play, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(play, lv_color_hex(ui_theme::text_soft()), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(play, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(ctx->play_pause_label, lv_color_hex(c.card), 0);

  // Volume (user 2026-10-22 mockup): the mute icon, the bar and the number
  // symmetric around play, moved in by kVolumeInward (the bar a little
  // shorter) so the menu button has room in the corner; the number fixed and
  // centred in its column like the mute icon in its own.
  const int mute_x = cx(0) + ml::kVolumeInward;
  const int value_x = cx(4) - ml::kVolumeInward;
  lv_obj_t* mute = round_button(ctx->player, c, ml::kButton, "volume-medium", c.control_icon, &ctx->volume_icon_label);
  lv_obj_set_pos(mute, mute_x, ml::kVolumeTop);
  lv_obj_add_event_cb(mute, on_volume_mute_click, LV_EVENT_CLICKED, ctx);
  const int volume_x = mute_x + ml::kButton + g;
  make_slider(ctx->volume_slider, ctx->player, c, volume_x, ml::kVolumeTop + ml::kButton / 2, value_x - g - volume_x,
              ml::kVolumeHeight, ml::kVolumeKnob, 100, popup_layout::scale(20), on_volume_slider_event, ctx);
  ctx->volume_value_label = add_text(ctx->player, popup_layout::font20(), c.secondary, "");
  lv_obj_set_width(ctx->volume_value_label, ml::kButton);
  lv_obj_set_style_text_align(ctx->volume_value_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(ctx->volume_value_label, value_x,
                 ml::kVolumeTop + ml::kButton / 2 - line_height(popup_layout::font20()) / 2);
  build_menu(ctx);
  // The menu button in the bottom right corner on the edge L: three dots, a
  // full button, the circle only while pressed. On the card's root, so it
  // stays over the open menu.
  ctx->menu_button = round_button(ctx->root, c, ml::kButton, "dots-horizontal", c.control_icon, nullptr);
  lv_obj_set_pos(ctx->menu_button, W - L - ml::kButton, ml::kVolumeTop);
  lv_obj_add_event_cb(ctx->menu_button, on_media_command, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<intptr_t>(MediaAction::Menu)));
}

// The menu's heading: its list, in the library the opened level.
String view_title(MediaPopupContext* ctx) {
  const auto& tr = texts();
  switch (ctx->view) {
    case MediaView::Source: {
      const MediaExtras& x = extras_of(ctx->entity_id);
      return x.sources.empty() && !x.sound_modes.empty() ? tr.media_sound_mode : tr.media_source;
    }
    case MediaView::Group: return tr.media_speakers;
    case MediaView::Library: {
      const MediaBrowseLevel* level = media_browse_current_level();
      return level && level->title.length() && media_browse_depth() > 1 ? level->title : String(tr.media_library);
    }
    default: return ctx->title;
  }
}

// The player's head shows the tile icon's state form like Home Assistant
// (speaker-play, speaker-pause, speaker-off, television-play ...) where the
// icon has one (user 2026-10-19, as in the draft); else the tile's icon.
String head_icon(const MediaPopupContext* ctx, const String& state) {
  if (!ctx->icon_name.length() || getMdiChar(ctx->icon_name) != ctx->icon_char) return ctx->icon_char;
  const char* suffix = state.equalsIgnoreCase("playing")  ? "-play"
                       : state.equalsIgnoreCase("paused") ? "-pause"
                       : (state.equalsIgnoreCase("off") || state.equalsIgnoreCase("standby")) ? "-off"
                                                                                               : nullptr;
  if (!suffix) return ctx->icon_char;
  const String variant = ctx->icon_name + suffix;
  return getMdiCodepoint(variant) != mdiUnknownIconCodepoint() ? getMdiChar(variant) : ctx->icon_char;
}

// The head always shows the player (user 2026-10-22: no buttons there, the
// open menu leaves it free); the menu names its list itself.
static void show_head(MediaPopupContext* ctx) {
  hometiles_title::set(ctx->title_label, ctx->title.c_str());
  lv_label_set_text(ctx->icon_label, head_icon(ctx, extras_of(ctx->entity_id).state).c_str());
  lv_obj_set_style_text_color(ctx->icon_label, lv_color_hex(ctx->icon_color), 0);
  lv_label_set_text(ctx->value_label, media_state_text(extras_of(ctx->entity_id).state));
  popup_layout::alignHeader(ctx->card, ctx->title_label, ctx->icon_label);
  apply_extras(ctx);
  if (popup_visible(ctx)) sync_popup_shell();
}

// ---- Sub-views: the Settings dropdown's options under the head --------------
// Every list is the Settings option list (settings_parts.cpp open_options):
// its height, inset, radius, font and press; the active entry (the source,
// a joined speaker) is filled like the selected option, a folder has the
// Settings chevron. Text only (user 2026-10-19: covers, round buttons and
// switches in every row made the list slow and busy; a picture with rounded
// corners cost LVGL two extra layers per frame).
constexpr int kOptionGap = 2;  // settings_parts.cpp kOptionGap

// The rows' height (user 2026-10-19: more room between the entries than the
// dropdown's 56 px).
constexpr int kListRowHeight = popup_layout::scale(72);

lv_obj_t* list_row(MediaPopupContext* ctx, const char* text, bool active, bool folder, lv_event_cb_t cb,
                   intptr_t index, const char* icon = nullptr) {
  const MediaColors& c = ctx->colors;
  lv_obj_t* row = plain(ctx->list);
  if (cb) lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, LV_PCT(100), kListRowHeight - kOptionGap);
  settings_style::apply_radius(row, settings_style::kGroupRadius - settings_style::kOptionInset);
  // Selected and pressed in the control fill, like a pressed button: the
  // menu is the card's own color (user 2026-10-22).
  lv_obj_set_style_bg_color(row, c.control, 0);
  lv_obj_set_style_bg_opa(row, active ? c.control_opa : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(row, c.control_opa, LV_PART_MAIN | LV_STATE_PRESSED);
  popup_nav_style::no_press_filter(row, LV_PART_MAIN | LV_STATE_PRESSED);
  disable_pressed_button_animation(row);
  // The chevron as far from the right edge as the kind icon from the left
  // (user 2026-10-19).
  const int pad = settings_style::kRowPadLeft - settings_style::kOptionInset;
  lv_obj_set_style_pad_left(row, pad, 0);
  lv_obj_set_style_pad_right(row, pad, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, Device::kGridGap, 0);
  if (icon) add_icon(row, icon, c.secondary);
  lv_obj_t* label = add_text(row, settings_style::row_font(), c.text, "");
  lv_label_set_text(label, drop_missing_glyphs(text, settings_style::row_font()).c_str());
  one_line_dots(label);
  lv_obj_set_flex_grow(label, 1);
  if (folder) add_icon(row, "chevron-right", c.secondary);
  if (cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(index));
  return row;
}

void section(MediaPopupContext* ctx, const char* title) {
  lv_obj_t* label = add_text(ctx->list, settings_style::small_font(), ctx->colors.secondary, title);
  lv_obj_set_style_pad_top(label, settings_style::kSectionBottom, 0);
  lv_obj_set_style_pad_bottom(label, settings_style::kSectionBottom, 0);
  lv_obj_set_style_pad_left(label, settings_style::kRowPadLeft - settings_style::kOptionInset, 0);
}

// ---- The library ----------------------------------------------------------------
// A library entry's kind as a small icon (Home Assistant's media classes).
const char* class_icon(const String& media_class, bool folder) {
  if (media_class == "album") return "album";
  if (media_class == "artist" || media_class == "composer" || media_class == "contributing_artist") return "account-music";
  if (media_class == "playlist") return "playlist-music";
  if (media_class == "podcast") return "podcast";
  if (media_class == "channel") return "radio";
  if (media_class == "movie") return "movie";
  if (media_class == "tv_show" || media_class == "season" || media_class == "episode") return "television-play";
  if (media_class == "video") return "video";
  if (media_class == "image") return "image";
  if (media_class == "track" || media_class == "music") return "music-note";
  return folder ? "folder" : "music-note";
}

static void open_view(MediaPopupContext* ctx, MediaView view);
static void rebuild_list(MediaPopupContext* ctx, bool keep_scroll);

static void on_library_row(lv_event_t* e) {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!ctx || lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  const size_t index = static_cast<size_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  const auto& items = media_browse_items();
  if (index >= items.size()) return;
  if (items[index].can_expand) {
    media_browse_open(index);
  } else if (items[index].can_play && media_browse_play(index)) {
    open_view(ctx, MediaView::Player);
  }
}

static void on_play_all_row(lv_event_t* e) {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!ctx || lv_event_get_code(e) != LV_EVENT_CLICKED || !ctx->available) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  if (media_browse_play_level()) open_view(ctx, MediaView::Player);
}

void append_library_rows(MediaPopupContext* ctx) {
  const auto& items = media_browse_items();
  const size_t end = std::min(items.size(), ctx->library_rows + kLibraryRowsStep);
  for (size_t i = ctx->library_rows; i < end; ++i) {
    const MediaBrowseItem& item = items[i];
    list_row(ctx, item.title.c_str(), false, item.can_expand, on_library_row, static_cast<intptr_t>(i),
             class_icon(item.media_class, item.can_expand));
  }
  ctx->library_rows = end;
}

// The library's state over the list, never in it (a line at its top pushed
// every row down): loading only when an answer takes a moment, an error for
// a few seconds, "nothing here" while the level is empty.
void show_library_status(MediaPopupContext* ctx) {
  if (!ctx->library_status) return;
  const auto& tr = texts();
  const MediaBrowseStatus status = media_browse_status();
  if (status != ctx->library_status_value) {
    ctx->library_status_value = status;
    ctx->library_status_ms = millis();
  }
  const uint32_t since = millis() - ctx->library_status_ms;
  const bool empty = media_browse_items().empty();
  const char* text = "";
  if (status == MediaBrowseStatus::Loading || status == MediaBrowseStatus::Idle) {
    if (since >= kLoadingDelayMs) text = tr.loading;
  } else if (status == MediaBrowseStatus::Error) {
    if (empty || since < kErrorShownMs) text = tr.media_library_failed;
  } else if (empty) {
    text = tr.media_library_empty;
  }
  if (strcmp(lv_label_get_text(ctx->library_status), text) != 0) lv_label_set_text(ctx->library_status, text);
  const lv_align_t align = ctx->library_rows ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_CENTER;
  if (lv_obj_get_style_align(ctx->library_status, LV_PART_MAIN) != align) lv_obj_align(ctx->library_status, align, 0, 0);
  set_shown(ctx->library_status, *text != '\0');
}

static void rebuild_dirty_list(void*) {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!ctx || !ctx->list_dirty || !ctx->list || !popup_visible(ctx)) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  rebuild_list(ctx, true);
}

static void on_list_scroll_end(lv_event_t* e) {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!ctx || !ctx->list || lv_event_get_code(e) != LV_EVENT_SCROLL_END) return;
  // A change that came while scrolling, after the scroll (outside the
  // list's own event).
  if (ctx->list_dirty) {
    lv_async_call(rebuild_dirty_list, nullptr);
    return;
  }
  if (ctx->view != MediaView::Library) return;
  if (ctx->library_rows >= media_browse_items().size()) return;
  if (lv_obj_get_scroll_bottom(ctx->list) > 3 * kListRowHeight) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  append_library_rows(ctx);
}

void build_library(MediaPopupContext* ctx) {
  ctx->library_rows = 0;
  ctx->library_items_version = media_browse_items_version();
  // An opened playlist or album that plays as a whole (like Home Assistant's
  // and Sonos' play button at its top; a track inside plays alone).
  const MediaBrowseLevel* level = media_browse_current_level();
  if (level && level->can_play && level->content_id.length() && media_browse_status() == MediaBrowseStatus::Ready) {
    list_row(ctx, texts().media_play_all, false, false, on_play_all_row, 0, "play");
  }
  append_library_rows(ctx);
  // The status pill: like the volume percentage over the knob.
  ctx->library_status = add_text(ctx->list, popup_layout::font20(), ctx->colors.text, "");
  lv_obj_add_flag(ctx->library_status, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_style_bg_color(ctx->library_status, ctx->colors.control, 0);
  lv_obj_set_style_bg_opa(ctx->library_status, ctx->colors.control_opa, 0);
  lv_obj_set_style_radius(ctx->library_status, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(ctx->library_status, popup_layout::scale(14), 0);
  lv_obj_set_style_pad_ver(ctx->library_status, popup_layout::scale(6), 0);
  lv_obj_add_flag(ctx->library_status, LV_OBJ_FLAG_HIDDEN);
  show_library_status(ctx);
}

// ---- Source and sound mode -------------------------------------------------------
constexpr intptr_t kSoundModeRow = 100;

static void on_source_row(lv_event_t* e) {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!ctx || lv_event_get_code(e) != LV_EVENT_CLICKED || !ctx->available) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  const intptr_t index = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  MediaExtras* x = find_extras(ctx->entity_id);
  if (!x) return;
  const bool sound_mode = index >= kSoundModeRow;
  const std::vector<String>& list = sound_mode ? x->sound_modes : x->sources;
  const size_t at = static_cast<size_t>(sound_mode ? index - kSoundModeRow : index);
  if (at >= list.size()) return;
  const String value = list[at];
  if (mqttPublishMediaSelect(ctx->entity_id.c_str(), sound_mode, value.c_str())) {
    (sound_mode ? x->sound_mode : x->source) = value;
  }
  open_view(ctx, MediaView::Player);
}

void build_sources(MediaPopupContext* ctx) {
  const auto& tr = texts();
  const MediaExtras& x = extras_of(ctx->entity_id);
  const bool both = !x.sources.empty() && !x.sound_modes.empty();
  const struct {
    const std::vector<String>* list;
    const String* current;
    const char* title;
    intptr_t base;
    uint32_t feature;
  } groups[] = {{&x.sources, &x.source, tr.media_source, 0, kFeatureSelectSource},
                {&x.sound_modes, &x.sound_mode, tr.media_sound_mode, kSoundModeRow, kFeatureSoundMode}};
  for (const auto& group : groups) {
    if (group.list->empty() || !(x.features & group.feature)) continue;
    if (both) section(ctx, group.title);
    for (size_t i = 0; i < group.list->size(); ++i) {
      const bool current = (*group.list)[i] == *group.current;
      list_row(ctx, (*group.list)[i].c_str(), current, false, on_source_row, group.base + static_cast<intptr_t>(i));
    }
  }
}

// ---- Speakers: players of the same integration with GROUPING --------------------
// As in the approved draft: the speaker, its name, its state below and a
// switch on the right; this player first, always in its group. Two-line rows
// like the Settings rows with a sub line (settings_style::kRowHeight).
static void on_group_row(lv_event_t* e) {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!ctx || lv_event_get_code(e) != LV_EVENT_CLICKED || !ctx->available) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  const size_t index = static_cast<size_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  MediaExtras* x = find_extras(ctx->entity_id);
  if (!x || index >= x->candidates.size()) return;
  const String member = x->candidates[index].entity_id;
  const bool joined = contains(x->members, member);
  // Join and leave at once on every tap; Home Assistant's next state
  // confirms the group.
  if (!mqttPublishMediaGroup(ctx->entity_id.c_str(), !joined, member.c_str())) return;
  if (joined) {
    for (size_t i = 0; i < x->members.size(); ++i) {
      if (x->members[i].equalsIgnoreCase(member)) {
        x->members.erase(x->members.begin() + i);
        break;
      }
    }
  } else {
    if (x->members.empty()) x->members.push_back(ctx->entity_id);
    if (x->members.size() < kMaxMembers) x->members.push_back(member);
  }
  rebuild_list(ctx, true);
  apply_extras(ctx);
}

void group_row(MediaPopupContext* ctx, const char* icon, const char* name, const String& sub, bool on, bool fixed,
               intptr_t index) {
  const MediaColors& c = ctx->colors;
  lv_obj_t* row = plain(ctx->list);
  if (!fixed) lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, LV_PCT(100), settings_style::kRowHeight);
  settings_style::apply_radius(row, settings_style::kGroupRadius - settings_style::kOptionInset);
  lv_obj_set_style_bg_color(row, c.control, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  // Pressed like a list row: the control fill (list_row).
  lv_obj_set_style_bg_opa(row, c.control_opa, LV_PART_MAIN | LV_STATE_PRESSED);
  popup_nav_style::no_press_filter(row, LV_PART_MAIN | LV_STATE_PRESSED);
  disable_pressed_button_animation(row);
  const int pad = settings_style::kRowPadLeft - settings_style::kOptionInset;
  lv_obj_set_style_pad_left(row, pad, 0);
  lv_obj_set_style_pad_right(row, pad, 0);
  lv_obj_set_style_pad_column(row, Device::kGridGap, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  add_icon(row, icon, c.secondary);
  lv_obj_t* texts_col = plain(row);
  lv_obj_set_flex_grow(texts_col, 1);
  lv_obj_set_height(texts_col, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(texts_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* title = add_text(texts_col, settings_style::row_font(), c.text, "");
  lv_label_set_text(title, drop_missing_glyphs(name, settings_style::row_font()).c_str());
  one_line_dots(title);
  lv_obj_set_width(title, LV_PCT(100));
  if (sub.length()) {
    lv_obj_t* line = add_text(texts_col, settings_style::small_font(), c.secondary, "");
    lv_label_set_text(line, drop_missing_glyphs(sub.c_str(), settings_style::small_font()).c_str());
    one_line_dots(line);
    lv_obj_set_width(line, LV_PCT(100));
  }
  // A check circle like Sonos (user 2026-10-19, instead of a switch): marked
  // in the group, an empty ring outside, in the text color like the
  // sub-view's other icons (a switch in the cover's color stood out next to
  // the white head icon). This player's is marked and dimmed (it cannot
  // leave its own group here).
  lv_obj_t* check = add_icon(row, on ? "checkbox-marked-circle" : "checkbox-blank-circle-outline", c.text);
  if (fixed) lv_obj_set_style_opa(check, LV_OPA_40, 0);
  if (!fixed) lv_obj_add_event_cb(row, on_group_row, LV_EVENT_CLICKED, reinterpret_cast<void*>(index));
}

void build_group(MediaPopupContext* ctx) {
  const auto& tr = texts();
  const MediaExtras& x = extras_of(ctx->entity_id);
  const bool playing = x.state.equalsIgnoreCase("playing");
  const bool paused = x.state.equalsIgnoreCase("paused");
  // What the group plays: its state and, playing or paused, the song.
  String now = media_state_text(x.state);
  if ((playing || paused) && ctx->shown_title.length()) {
    now += now.length() ? " \xC2\xB7 " : "";
    now += ctx->shown_title;
  }
  const char* group_icon = playing ? "speaker-play" : paused ? "speaker-pause" : "speaker";
  String self = tr.media_this_player;
  if (now.length()) self += String(" \xC2\xB7 ") + now;
  group_row(ctx, group_icon, ctx->title.c_str(), self, true, true, 0);
  for (size_t i = 0; i < x.candidates.size(); ++i) {
    const MediaGroupCandidate& candidate = x.candidates[i];
    const bool joined = contains(x.members, candidate.entity_id);
    group_row(ctx, joined ? group_icon : "speaker", candidate.name.c_str(),
              joined ? now : String(media_state_text(candidate.state)), joined, false, static_cast<intptr_t>(i));
  }
}

// ---- Views -------------------------------------------------------------------------
// What the source or speaker list shows: a state update rebuilds it only
// when this changed (every update rebuilt it, also while scrolling).
uint32_t list_signature(const MediaPopupContext* ctx) {
  const MediaExtras& x = extras_of(ctx->entity_id);
  String key = x.state;
  key += '\x1f';
  key += String(x.features);
  key += '\x1f';
  key += x.source;
  key += '\x1f';
  key += x.sound_mode;
  for (const std::vector<String>* list : {&x.sources, &x.sound_modes, &x.members}) {
    key += '\x1d';
    for (const String& item : *list) {
      key += item;
      key += '\x1e';
    }
  }
  for (const MediaGroupCandidate& candidate : x.candidates) {
    key += candidate.entity_id;
    key += candidate.name;
    key += candidate.state;
    key += '\x1e';
  }
  key += ctx->title;
  key += ctx->shown_title;
  return text_hash(key.c_str());
}

void delete_dead_lists(void*) {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!ctx) return;  // deleted with the popup
  for (lv_obj_t* list : ctx->dead_lists) lv_obj_delete(list);
  ctx->dead_lists.clear();
}

// An object that may go from one of its own clicks (a row, a button): hidden
// now, deleted after the event; a popup deleted first takes it along.
void drop_later(MediaPopupContext* ctx, lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  if (ctx->dead_lists.empty()) lv_async_call(delete_dead_lists, nullptr);
  ctx->dead_lists.push_back(obj);
}

// The menu's rows only; the menu itself stays (build_menu).
void delete_list(MediaPopupContext* ctx) {
  drop_later(ctx, ctx->list);
  ctx->list = nullptr;
  ctx->list_dirty = false;
  ctx->library_status = nullptr;
  ctx->library_rows = 0;
  ctx->menu_rows_key = 0;
}

// The whole menu (a new player's colors or layout build it again).
void delete_menu(MediaPopupContext* ctx) {
  delete_list(ctx);
  drop_later(ctx, ctx->list_box);
  drop_later(ctx, ctx->menu_shadow);
  drop_later(ctx, ctx->list_shield);
  ctx->list_box = ctx->menu_shadow = ctx->list_shield = ctx->menu_title = ctx->menu_back = nullptr;
  for (lv_obj_t*& tab : ctx->menu_tabs) tab = nullptr;
}

// The list fills its own area with the menu's color (the card's), square and
// opaque: LVGL then starts every scroll frame at the list and draws neither
// the menu nor the card under it (the screensaver's trick, b307: only an
// object without a radius covers). Its scrollbar is the Sensor popup's
// (text color, half).
static void show_list_scrollbar_color(MediaPopupContext* ctx) {
  if (!ctx->list) return;
  lv_obj_set_style_bg_color(ctx->list, lv_color_hex(ctx->colors.card), 0);
  lv_obj_set_style_bg_opa(ctx->list, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(ctx->list, lv_color_hex(ctx->colors.text), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(ctx->list, LV_OPA_50, LV_PART_SCROLLBAR);
}

static void on_menu_shield(lv_event_t* e) {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!ctx || lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  open_view(ctx, MediaView::Player);
}

static const char* menu_view_icon(MediaView view) {
  switch (view) {
    case MediaView::Group: return "speaker-multiple";
    case MediaView::Source: return "login-variant";
    default: return "play-box-multiple";
  }
}

// The menu's geometry (user 2026-10-22, build/design-mockups/media-popup-simple):
// a box inside the popup, its edge halfway in the edge L (a pressed button to
// its edge as far as its edge to the card's); inside on the edge L a head row
// as high as a button, a hairline a grid gap under it, five rows, the switch
// in the volume row.
struct MenuGeometry {
  int box_offset, box_pad, head_height, list_height, top, content_width;
};
MenuGeometry menu_geometry() {
  const int L = ml::kEdge;
  const int g = Device::kGridGap;
  MenuGeometry m{};
  m.box_offset = L / 2;
  m.box_pad = L - m.box_offset;
  m.head_height = ml::kButton;
  m.list_height = ml::kMenuRows * kListRowHeight;
  m.top = ml::kVolumeTop - g - m.list_height - g - m.head_height - m.box_pad;
  m.content_width = popup_layout::kCardWidth - 2 * L;
  return m;
}

// The menu box's shadow as pictures. Drawn by LVGL at the box's own radius,
// it pushed the card's shadow corner out of LVGL's one-entry shadow cache,
// and every 28-line band of an opening frame computed both corners again
// (V2 b524: 157-180 ms per frame); a layer with the card's radius was fast,
// but its corners did not follow the box (b525). So LVGL draws the box's
// shadow once into an alpha snapshot, and its four corners and four edges
// stay as alpha pictures drawn in black under the box. Box and card have the
// same size for every player: made once, kept for the panel's run (~50 KB).
struct MenuShadowParts {
  int32_t width = 0, height = 0, radius = 0, blur = 0;
  int32_t ext = 0;               // the shadow's reach beyond the box
  lv_draw_buf_t* part[8] = {};   // corners, then top, bottom, left, right
  lv_area_t area[8] = {};        // in the snapshot (the box at ext, ext)
};
MenuShadowParts g_menu_shadow_parts;

const MenuShadowParts* menu_shadow_parts(lv_obj_t* parent, int32_t w, int32_t h, int32_t radius, int32_t blur) {
  MenuShadowParts& s = g_menu_shadow_parts;
  if (s.part[0] && s.width == w && s.height == h && s.radius == radius && s.blur == blur) return &s;
  // A probe like the box, shown only to the snapshot (never drawn on screen:
  // deleted before the next refresh).
  lv_obj_t* probe = plain(parent);
  lv_obj_set_size(probe, w, h);
  lv_obj_set_style_radius(probe, radius, 0);
  popup_layout::apply_card_shadow(probe);
  lv_obj_set_style_shadow_width(probe, blur, 0);
  lv_obj_update_layout(probe);
  const int32_t ext = lv_obj_get_ext_draw_size(probe);
  lv_draw_buf_t* snap = lv_snapshot_take(probe, LV_COLOR_FORMAT_A8);
  lv_obj_delete(probe);
  if (!snap) return nullptr;
  for (lv_draw_buf_t*& part : s.part) {
    if (part) lv_draw_buf_destroy(part);
    part = nullptr;
  }
  const int32_t sw = snap->header.w;
  const int32_t sh = snap->header.h;
  const int32_t c = std::min(ext + radius, std::min(sw, sh) / 2);
  const lv_area_t areas[8] = {{0, 0, c - 1, c - 1},           {sw - c, 0, sw - 1, c - 1},
                              {0, sh - c, c - 1, sh - 1},      {sw - c, sh - c, sw - 1, sh - 1},
                              {c, 0, sw - c - 1, ext - 1},     {c, sh - ext, sw - c - 1, sh - 1},
                              {0, c, ext - 1, sh - c - 1},     {sw - ext, c, sw - 1, sh - c - 1}};
  for (int i = 0; i < 8; ++i) {
    const lv_area_t& a = areas[i];
    const int32_t pw = lv_area_get_width(&a);
    const int32_t ph = lv_area_get_height(&a);
    lv_draw_buf_t* part = pw > 0 && ph > 0 ? lv_draw_buf_create(pw, ph, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO) : nullptr;
    if (part) {
      for (int32_t y = 0; y < ph; ++y) {
        memcpy(part->data + y * part->header.stride, snap->data + (a.y1 + y) * snap->header.stride + a.x1, pw);
      }
    }
    s.part[i] = part;
    s.area[i] = a;
  }
  lv_draw_buf_destroy(snap);
  s.width = w;
  s.height = h;
  s.radius = radius;
  s.blur = blur;
  s.ext = ext;
  return &s;
}

// The menu, built once with the player and kept hidden (user 2026-10-22: the
// menu opened slowly; the player popup itself is built once too). It opens
// from the menu button as a box in the card's color with the big popup's
// shadow, its radius concentric with the card's. A tap outside it closes it.
static void build_menu(MediaPopupContext* ctx) {
  const MediaColors& c = ctx->colors;
  const int W = popup_layout::kCardWidth;
  const int H = popup_layout::kCardHeight;
  const int g = Device::kGridGap;
  const MenuGeometry m = menu_geometry();

  // The layer outside the box catches taps only while the menu is open; it
  // stays shown and draws nothing (showing it redrew the whole card on every
  // opening and closing, 640000 px on V2).
  ctx->list_shield = plain(ctx->root);
  lv_obj_set_size(ctx->list_shield, W, H);
  lv_obj_add_event_cb(ctx->list_shield, on_menu_shield, LV_EVENT_CLICKED, nullptr);

  // The big popup's shadow rule at the box's concentric radius, as pictures
  // (menu_shadow_parts) under the box.
  const int32_t card_radius = lv_obj_get_style_radius(ctx->card, LV_PART_MAIN);
  const int32_t box_w = W - 2 * m.box_offset;
  const int32_t box_h = H - m.box_offset - m.top;
  const int32_t box_radius = std::max<int32_t>(0, card_radius - m.box_offset);
  ctx->menu_shadow = plain(ctx->root);
  if (const MenuShadowParts* parts = menu_shadow_parts(ctx->root, box_w, box_h, box_radius,
                                                       popup_layout::shadow_width_for(card_radius))) {
    lv_obj_set_pos(ctx->menu_shadow, m.box_offset - parts->ext, m.top - parts->ext);
    lv_obj_set_size(ctx->menu_shadow, box_w + 2 * parts->ext, box_h + 2 * parts->ext);
    for (int i = 0; i < 8; ++i) {
      if (!parts->part[i]) continue;
      lv_obj_t* piece = lv_image_create(ctx->menu_shadow);
      lv_image_set_src(piece, parts->part[i]);
      lv_obj_set_pos(piece, parts->area[i].x1, parts->area[i].y1);
      lv_obj_set_style_image_recolor(piece, lv_color_black(), 0);
    }
  }
  lv_obj_add_flag(ctx->menu_shadow, LV_OBJ_FLAG_HIDDEN);

  ctx->list_box = plain(ctx->root);
  lv_obj_add_flag(ctx->list_box, LV_OBJ_FLAG_CLICKABLE);  // a tap inside stays inside
  lv_obj_set_pos(ctx->list_box, m.box_offset, m.top);
  lv_obj_set_size(ctx->list_box, box_w, box_h);
  lv_obj_set_style_radius(ctx->list_box, box_radius, 0);
  lv_obj_set_style_bg_color(ctx->list_box, lv_color_hex(c.card), 0);
  lv_obj_set_style_bg_opa(ctx->list_box, LV_OPA_COVER, 0);
  lv_obj_add_flag(ctx->list_box, LV_OBJ_FLAG_HIDDEN);

  // The head row: the library's back button in the corner, the name.
  ctx->menu_back = round_button(ctx->list_box, c, ml::kButton, "arrow-left", c.control_icon, nullptr);
  lv_obj_set_pos(ctx->menu_back, m.box_pad, m.box_pad);
  lv_obj_add_event_cb(ctx->menu_back, on_media_command, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<intptr_t>(MediaAction::Back)));
  lv_obj_add_flag(ctx->menu_back, LV_OBJ_FLAG_HIDDEN);
  ctx->menu_title = add_text(ctx->list_box, settings_style::row_font(), c.text, "");
  one_line_dots(ctx->menu_title);
  // The hairline like the Settings rows' separator, from L to W - L.
  lv_obj_t* rule = plain(ctx->list_box);
  lv_obj_set_pos(rule, m.box_pad, m.box_pad + m.head_height + g / 2);
  lv_obj_set_size(rule, m.content_width, 1);
  lv_obj_set_style_bg_color(rule, lv_color_hex(c.text), 0);
  lv_obj_set_style_bg_opa(rule, settings_style::kSeparatorOpa, 0);

  // The switch in the volume row: speakers, source, library in the previous,
  // play and next columns.
  auto cx = [&](int k) { return W / 2 + (k - 2) * ml::kStep - ml::kButton / 2; };
  for (size_t k = 0; k < sizeof(kMenuViews) / sizeof(kMenuViews[0]); ++k) {
    const MediaView view = kMenuViews[k];
    lv_obj_t* tab = round_button(ctx->list_box, c, ml::kButton, menu_view_icon(view), c.control_icon, nullptr);
    lv_obj_set_pos(tab, cx(static_cast<int>(k) + 1) - m.box_offset, ml::kVolumeTop - m.top);
    const MediaAction action = view == MediaView::Group    ? MediaAction::Group
                               : view == MediaView::Source ? MediaAction::Source
                                                           : MediaAction::Library;
    lv_obj_add_event_cb(tab, on_media_command, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(action)));
    ctx->menu_tabs[k] = tab;
  }
}

// The open list's head and switch: its name (in the library the level), the
// back button in a library level, the offered lists' buttons with the shown
// one on the control fill like shuffle that is on.
static void show_menu_head(MediaPopupContext* ctx) {
  const MediaColors& c = ctx->colors;
  const MenuGeometry m = menu_geometry();
  const bool back = ctx->view == MediaView::Library && media_browse_can_go_back();
  set_shown(ctx->menu_back, back);
  const int text_inset = settings_style::kRowPadLeft - settings_style::kOptionInset;
  // The name over the rows' texts (their kind icon, then a gap) after the
  // back button.
  const int title_x = m.box_pad + text_inset + (back ? lv_font_get_line_height(FONT_MDI_ICONS) + Device::kGridGap : 0);
  lv_label_set_text(ctx->menu_title, drop_missing_glyphs(view_title(ctx).c_str(), settings_style::row_font()).c_str());
  lv_obj_set_width(ctx->menu_title, m.box_pad + m.content_width - title_x);
  lv_obj_set_pos(ctx->menu_title, title_x, m.box_pad + m.head_height / 2 - line_height(settings_style::row_font()) / 2);
  for (size_t k = 0; k < sizeof(kMenuViews) / sizeof(kMenuViews[0]); ++k) {
    lv_obj_t* tab = ctx->menu_tabs[k];
    if (!tab) continue;
    set_shown(tab, menu_view_offered(ctx, kMenuViews[k]));
    const bool on = kMenuViews[k] == ctx->view;
    popup_nav_style::set_bg(tab, c.control, on ? c.control_opa : LV_OPA_TRANSP, LV_PART_MAIN);
    popup_nav_style::set_bg(tab, on ? c.raised : c.control, on ? c.raised_opa : c.control_opa,
                            LV_PART_MAIN | LV_STATE_PRESSED);
  }
  if (ctx->menu_button) lv_obj_move_foreground(ctx->menu_button);
}

// What the open list's rows show: a change builds them again, else they stay
// as they are (the menu opens at once).
uint32_t menu_rows_of(const MediaPopupContext* ctx) {
  uint32_t key = static_cast<uint32_t>(ctx->view) << 28;
  if (ctx->view == MediaView::Library) {
    const MediaBrowseLevel* level = media_browse_current_level();
    key ^= media_browse_items_version() * 2654435761u;
    if (level) key ^= text_hash(level->content_id.c_str());
  } else {
    key ^= list_signature(ctx);
  }
  return key ? key : 1;
}

// Shows the open list (or hides the menu for the player). The rows are built
// again only when what they show changed.
static void rebuild_list(MediaPopupContext* ctx, bool keep_scroll) {
  if (!ctx->list_box) return;
  if (ctx->view == MediaView::Player) {
    set_shown(ctx->list_box, false);
    set_shown(ctx->menu_shadow, false);
    lv_obj_remove_flag(ctx->list_shield, LV_OBJ_FLAG_CLICKABLE);
    return;
  }
  show_menu_head(ctx);
  lv_obj_add_flag(ctx->list_shield, LV_OBJ_FLAG_CLICKABLE);
  set_shown(ctx->menu_shadow, true);
  set_shown(ctx->list_box, true);
  const uint32_t rows = menu_rows_of(ctx);
  if (ctx->list && ctx->menu_rows_key == rows) {
    show_library_status(ctx);
    return;
  }
  const int32_t scroll = keep_scroll && ctx->list ? lv_obj_get_scroll_y(ctx->list) : 0;
  delete_list(ctx);

  const MenuGeometry m = menu_geometry();
  ctx->list = plain(ctx->list_box);
  lv_obj_set_pos(ctx->list, m.box_pad, m.box_pad + m.head_height + Device::kGridGap);
  lv_obj_set_size(ctx->list, m.content_width, m.list_height);
  lv_obj_set_style_radius(ctx->list, 0, 0);
  // The Sensor popup's activity list scrollbar (sensor_popup.cpp): thin and
  // round, shown while the list is longer than the box (user 2026-10-19);
  // the rows end one inset before it (user 2026-10-22: a marked row touched
  // it).
  const int bar_width = popup_layout::scale(5);
  lv_obj_set_style_width(ctx->list, bar_width, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(ctx->list, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(ctx->list, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(ctx->list, bar_width + settings_style::kOptionInset, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(ctx->list, LV_SCROLLBAR_MODE_AUTO);
  show_list_scrollbar_color(ctx);
  lv_obj_set_flex_flow(ctx->list, LV_FLEX_FLOW_COLUMN);
  // Scrolling like the Sensor popup's activity list (sensor_popup.cpp).
  lv_obj_clear_flag(ctx->list, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_add_flag(ctx->list, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                                        LV_OBJ_FLAG_SCROLL_MOMENTUM));
  lv_obj_set_scroll_dir(ctx->list, LV_DIR_VER);
  lv_obj_add_event_cb(ctx->list, on_list_scroll_end, LV_EVENT_SCROLL_END, nullptr);

  if (ctx->view == MediaView::Library) build_library(ctx);
  else if (ctx->view == MediaView::Source) build_sources(ctx);
  else if (ctx->view == MediaView::Group) build_group(ctx);
  ctx->list_signature = list_signature(ctx);
  ctx->menu_rows_key = rows;
  if (scroll) {
    lv_obj_update_layout(ctx->list);
    lv_obj_scroll_to_y(ctx->list, scroll, LV_ANIM_OFF);
  }
}

static void open_view(MediaPopupContext* ctx, MediaView view) {
  const MediaView before = ctx->view;
  ctx->view = view;
  // The library keeps its level while the popup is open (user 2026-10-22:
  // "Laden" on every opening); the first opening starts at its root.
  if (view == MediaView::Library && before != MediaView::Library &&
      (!media_browse_current_level() || !media_browse_entity().equalsIgnoreCase(ctx->entity_id))) {
    media_browse_start(ctx->entity_id.c_str());
  }
  if (view != MediaView::Player) ctx->last_menu_view = view;
  rebuild_list(ctx, false);
  if (ctx->library_timer) {
    if (view == MediaView::Library) lv_timer_resume(ctx->library_timer);
    else lv_timer_pause(ctx->library_timer);
  }
  show_head(ctx);
}

// The menu button: closes the open menu, else opens it at its last list (the
// first the player offers the first time).
static void toggle_menu(MediaPopupContext* ctx) {
  if (ctx->view != MediaView::Player) {
    open_view(ctx, MediaView::Player);
    return;
  }
  MediaView view = menu_view_offered(ctx, ctx->last_menu_view) ? ctx->last_menu_view : MediaView::Player;
  for (MediaView candidate : kMenuViews) {
    if (view == MediaView::Player && menu_view_offered(ctx, candidate)) view = candidate;
  }
  if (view != MediaView::Player) open_view(ctx, view);
}

static void on_browse_changed() {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!popup_visible(ctx) || ctx->view != MediaView::Library) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  if (media_browse_items_version() != ctx->library_items_version) {
    rebuild_list(ctx, false);
  } else {
    show_library_status(ctx);
  }
  show_head(ctx);
}

static void media_library_timer_cb(lv_timer_t* timer) {
  MediaPopupContext* ctx = timer ? static_cast<MediaPopupContext*>(lv_timer_get_user_data(timer)) : nullptr;
  if (!popup_visible(ctx) || ctx->view != MediaView::Library) return;
  media_browse_poll(millis());
  show_library_status(ctx);
}

// ---- Commands -------------------------------------------------------------------
static void go_back(MediaPopupContext* ctx) {
  if (ctx->view == MediaView::Library && media_browse_can_go_back()) {
    media_browse_back();
    return;
  }
  open_view(ctx, MediaView::Player);
}

static void on_media_command(lv_event_t* e) {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !ctx || !ctx->entity_id.length()) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  const MediaAction action = static_cast<MediaAction>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (action == MediaAction::Back) {
    go_back(ctx);
    return;
  }
  if (action == MediaAction::Menu) {
    toggle_menu(ctx);
    return;
  }
  if (!ctx->available) return;
  MediaExtras* x = find_extras(ctx->entity_id);
  switch (action) {
    case MediaAction::PlayPause: {
      // Play/pause is safe to reflect immediately. HA remains authoritative
      // and the next state packet corrects the icon if the service call
      // fails, but the button no longer appears frozen while MQTT/HA
      // round-trips.
      const float position = current_media_position(ctx);
      ctx->is_playing = !ctx->is_playing;
      ctx->media_position = position;
      ctx->media_position_received_ms = millis();
      set_icon(ctx->play_pause_label, ctx->is_playing ? "pause" : "play");
      mqttPublishMediaCommand(ctx->entity_id.c_str(), "play_pause");
      break;
    }
    case MediaAction::Previous: mqttPublishMediaCommand(ctx->entity_id.c_str(), "previous"); break;
    case MediaAction::Next: mqttPublishMediaCommand(ctx->entity_id.c_str(), "next"); break;
    case MediaAction::Shuffle:
      if (x && mqttPublishMediaShuffle(ctx->entity_id.c_str(), !x->shuffle)) {
        x->shuffle = !x->shuffle;
        apply_extras(ctx);
      }
      break;
    case MediaAction::Repeat: {
      if (!x) break;
      // off -> all -> one -> off, like Home Assistant's own card.
      const char* next = x->repeat.equalsIgnoreCase("off") ? "all" : x->repeat.equalsIgnoreCase("all") ? "one" : "off";
      if (mqttPublishMediaRepeat(ctx->entity_id.c_str(), next)) {
        x->repeat = next;
        apply_extras(ctx);
      }
      break;
    }
    case MediaAction::Group: open_view(ctx, MediaView::Group); break;
    case MediaAction::Source: open_view(ctx, MediaView::Source); break;
    case MediaAction::Library: open_view(ctx, MediaView::Library); break;
    default: break;
  }
}

// One step of a volume drag (0..1 of the bar), as in v0.9.3: LVGL moves the
// knob, the number and the icon follow, the lift publishes.
static void apply_volume_drag(MediaPopupContext* ctx, float fraction, bool released) {
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;
  const int32_t raw = static_cast<int32_t>(fraction * 100.0f + 0.5f);
  ctx->volume_dragging = !released;
  const bool was_muted = ctx->is_muted;
  set_volume_widgets(ctx, raw, raw == 0, false);
  if (!released) return;
  mqttPublishMediaVolume(ctx->entity_id.c_str(), static_cast<float>(raw) / 100.0f);
  const MediaExtras& x = extras_of(ctx->entity_id);
  // A muted player plays again at the chosen volume.
  if (raw > 0 && x.has_features && (x.features & kFeatureVolumeMute) && was_muted) {
    mqttPublishMediaMute(ctx->entity_id.c_str(), false);
  }
}

static void on_volume_slider_event(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  const bool released = code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST;
  if (code != LV_EVENT_VALUE_CHANGED && !released) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->volume_slider.input) return;
  if (!ctx->has_volume || !ctx->available) {
    ctx->volume_dragging = false;
    return;
  }
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  apply_volume_drag(ctx, slider_input_fraction(ctx->volume_slider), released);
}

// One step of a position drag, as in v0.9.3: LVGL moves the knob, the times
// follow, the lift publishes.
static void apply_seek_drag(MediaPopupContext* ctx, float fraction, bool released) {
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;
  const float position = ctx->media_duration * fraction;
  ctx->seek_dragging = !released;
  set_seek_widgets(ctx, position, false);
  if (released) {
    ctx->media_position = position;
    ctx->media_position_received_ms = millis();
    mqttPublishMediaSeek(ctx->entity_id.c_str(), position);
  }
}

static void on_seek_slider_event(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  const bool released = code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST;
  if (code != LV_EVENT_VALUE_CHANGED && !released) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->seek_slider.input) return;
  // End the gesture even if playback became unavailable while dragging.
  if (!ctx->has_media_position || !ctx->available || !seek_allowed(ctx)) {
    ctx->seek_dragging = false;
    return;
  }
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  apply_seek_drag(ctx, slider_input_fraction(ctx->seek_slider), released);
}

static void on_volume_mute_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->entity_id.length() || !ctx->volume_slider.input || !ctx->available) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  const MediaExtras& x = extras_of(ctx->entity_id);
  if (x.has_features && (x.features & kFeatureVolumeMute)) {
    // The player mutes itself and keeps its volume.
    const bool mute = !ctx->is_muted;
    mqttPublishMediaMute(ctx->entity_id.c_str(), mute);
    set_volume_widgets(ctx, mute ? 0 : ctx->last_volume_pct, mute);
    return;
  }
  int32_t current = static_cast<int32_t>(ctx->volume_slider.value * 100.0f + 0.5f);
  int32_t next = 0;
  if (current <= 0 || ctx->is_muted) {
    next = ctx->last_volume_pct > 0 ? ctx->last_volume_pct : 35;
  }
  set_volume_widgets(ctx, next, next == 0);
  mqttPublishMediaVolume(ctx->entity_id.c_str(), static_cast<float>(next) / 100.0f);
}

// ---- Close --------------------------------------------------------------------
static void close_media_popup(MediaPopupContext* ctx) {
  if (!ctx || !ctx->overlay || !ctx->card) return;
  ctx->seek_dragging = false;
  ctx->volume_dragging = false;
  hide_popup_shell(ctx->card);
  cancel_popup_open(ctx->card);
  lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
  // The next opening starts at the player, the library at its root; the
  // library's rows go with its level (only they have a status line), the
  // other lists stay for the next opening.
  media_browse_reset();
  if (ctx->library_status) delete_list(ctx);
  if (ctx->view != MediaView::Player) {
    ctx->view = MediaView::Player;
    rebuild_list(ctx, false);
  }
  if (ctx->library_timer) lv_timer_pause(ctx->library_timer);
  if (ctx->motion_timer) lv_timer_pause(ctx->motion_timer);
}

static void on_close_click(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_RELEASED) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  // Closes from every view (a sub-view's back arrow is its own head button).
  close_media_popup(ctx);
}

static void on_overlay_delete(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  if (!ctx) return;
  for (lv_timer_t** timer : {&ctx->progress_timer, &ctx->motion_timer, &ctx->library_timer}) {
    if (*timer) lv_timer_delete(*timer);
    *timer = nullptr;
  }
  media_browse_set_listener(nullptr);
  media_browse_reset();
  ctx->dead_lists.clear();  // deleted with the card
  free_cover_dsc(ctx->cover_dsc);
  if (g_media_popup_ctx == ctx) g_media_popup_ctx = nullptr;
  delete ctx;
}

// ---- Applying the tile's state ---------------------------------------------------
static void apply_init_to_context(MediaPopupContext* ctx, const MediaPopupInit& init) {
  if (!ctx) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  ctx->entity_id = init.entity_id;
  ctx->available = init.available;
  if (!ctx->available) {
    ctx->seek_dragging = false;
    ctx->volume_dragging = false;
  }
  ctx->bg_color = init.bg_color ? init.bg_color : ui_theme::popup_card();
  ctx->icon_color = init.icon_color;
  String title = init.title;
  title.trim();
  ctx->title = title.length() ? title : init.entity_id;
  String icon_char = init.icon_char;
  icon_char.trim();
  if (!icon_char.length()) {
    String icon_name = init.icon_name;
    icon_name.trim();
    if (!icon_name.length()) icon_name = "television";
    icon_char = getMdiChar(icon_name);
    if (!icon_char.length()) icon_char = getMdiChar("television");
  }
  ctx->icon_char = icon_char;
  ctx->icon_name = normalizeMdiIconName(init.icon_name);
  if (ctx->card) lv_obj_set_style_bg_color(ctx->card, lv_color_hex(ctx->bg_color), 0);

  // The player is built again only for another layout or other colors.
  ctx->colors = compute_colors(ctx->bg_color, ctx->icon_color);
  if (!ctx->player || ctx->centered != init.cover_centered || ctx->built_colors != colors_key(ctx->colors)) {
    build_player(ctx, init.cover_centered);
    if (ctx->view != MediaView::Player) rebuild_list(ctx, true);
  }
  if (ctx->fallback_icon) lv_label_set_text(ctx->fallback_icon, ctx->icon_char.c_str());

  const bool was_playing = ctx->reported_playing;
  ctx->reported_playing = init.is_playing;
  apply_media_text(ctx, init);
  set_icon(ctx->play_pause_label, init.is_playing ? "pause" : "play");
  update_seek(ctx, init);
  update_volume(ctx, init);
  update_cover(ctx, init.cover_dsc, init.cover_hash);
  if (ctx->bars && was_playing != ctx->reported_playing) lv_obj_invalidate(ctx->bars);
  if ((ctx->view == MediaView::Source || ctx->view == MediaView::Group) && ctx->list &&
      list_signature(ctx) != ctx->list_signature) {
    if (lv_obj_is_scrolling(ctx->list)) ctx->list_dirty = true;
    else rebuild_list(ctx, true);
  }
  show_head(ctx);
  apply_availability(ctx);
  if (ctx->motion_timer && popup_visible(ctx)) lv_timer_resume(ctx->motion_timer);
  if (popup_visible(ctx)) popup_shell_follow_tile_color(ctx->bg_color);
}

static MediaPopupContext* create_media_popup(uint32_t card_color) {
  MediaPopupContext* ctx = new MediaPopupContext();
  g_media_popup_ctx = ctx;
  const auto parts = create_popup_body(on_close_click, ctx, card_color);
  ctx->overlay = parts.overlay;
  ctx->card = parts.card;
  ctx->title_label = parts.title;
  ctx->icon_label = parts.icon;
  ctx->close_button = parts.close;
  ctx->value_label = lv_label_create(ctx->card);
  lv_obj_add_flag(ctx->value_label, LV_OBJ_FLAG_HIDDEN);
  // The whole card in card coordinates (the card's content box is padded).
  ctx->root = plain(ctx->card);
  lv_obj_add_flag(ctx->root, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(ctx->root, popup_layout::kCardWidth, popup_layout::kCardHeight);
  lv_obj_set_pos(ctx->root, -popup_layout::kCardPad, -popup_layout::kCardPad);
  lv_obj_move_foreground(ctx->icon_label);
  lv_obj_move_foreground(ctx->title_label);
  lv_obj_move_foreground(ctx->close_button);
  ctx->progress_timer = lv_timer_create(media_progress_timer_cb, 1000, ctx);
  ctx->motion_timer = lv_timer_create(media_motion_timer_cb, kMotionPeriodMs, ctx);
  lv_timer_pause(ctx->motion_timer);
  ctx->library_timer = lv_timer_create(media_library_timer_cb, kLibraryTickMs, ctx);
  lv_timer_pause(ctx->library_timer);
  media_browse_set_listener(on_browse_changed);
  lv_obj_add_event_cb(ctx->overlay, on_overlay_delete, LV_EVENT_DELETE, ctx);
  return ctx;
}

}  // namespace

static void finish_media_popup_open(const MediaPopupInit& init) {
  if (!g_media_popup_ctx) return;
  MediaPopupInit current = init;
  // Resolve pixels on the UI thread when content is actually applied. A tile
  // may have been removed since the press; never retain its borrowed pointer.
  current.cover_dsc = tile_renderer_find_media_cover(init.entity_id, current.cover_hash);
  apply_init_to_context(g_media_popup_ctx, current);
}

static void prepare_media_popup_open(const MediaPopupInit& init) {
  auto* ctx = g_media_popup_ctx;
  const ui_theme::SurfaceScope popup_scope = body_scope(ctx);
  String title = init.title;
  title.trim();
  if (!title.length()) title = init.entity_id;
  ctx->title = title;
  hometiles_title::set(ctx->title_label, title.c_str());
  String icon = init.icon_char;
  icon.trim();
  if (!icon.length()) icon = getMdiChar(init.icon_name);
  if (!icon.length()) icon = getMdiChar("television");
  ctx->icon_char = icon;
  ctx->icon_name = normalizeMdiIconName(init.icon_name);
  ctx->icon_color = init.icon_color;
  lv_label_set_text(ctx->icon_label, head_icon(ctx, extras_of(init.entity_id).state).c_str());
  lv_obj_set_style_text_color(ctx->icon_label, lv_color_hex(ctx->icon_color), 0);
  lv_obj_set_style_bg_color(ctx->card,
      lv_color_hex(init.bg_color ? init.bg_color : ui_theme::popup_card()), 0);
  const bool same_entity = ctx->entity_id == init.entity_id && ctx->centered == init.cover_centered;
  if (same_entity) {
    // The kept body shows in the first frame: a song that changed while the
    // popup was closed showed the previous cover and title until the body
    // was applied (user 2026-10-08, every device). The tile's current cover
    // is decoded already; take it and the song's text now. Applying the body
    // later leaves them as they are.
    uint32_t cover_hash = 0;
    const lv_image_dsc_t* cover = tile_renderer_find_media_cover(init.entity_id, cover_hash);
    update_cover(ctx, cover, cover_hash);
    apply_media_text(ctx, init);
  }
  // The state line is known before the body: the header keeps its shape.
  lv_label_set_text(ctx->value_label, media_state_text(extras_of(init.entity_id).state));
  MediaPopupInit pending = init;
  pending.cover_dsc = nullptr;
  if (!defer_popup_body(ctx->card, ctx->title_label, ctx->icon_label,
                        ctx->close_button, pending, finish_media_popup_open,
                        same_entity))
    apply_init_to_context(ctx, init);
}

void show_media_popup(const MediaPopupInit& init) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  hide_pin_popup();
  hide_camera_popup();
  hide_device_popup();
  hide_climate_popup();
  hide_cover_popup();
  if (!init.entity_id.length()) return;
  hide_light_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_energy_popup();

  if (g_media_popup_ctx && g_media_popup_ctx->overlay && g_media_popup_ctx->card) {
    prepare_media_popup_open(init);
    lv_obj_clear_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_media_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
    if (g_media_popup_ctx && g_media_popup_ctx->card) viewNavigationPopupShown(g_media_popup_ctx->card, init.entity_id.c_str());
    show_popup_shell(g_media_popup_ctx->overlay, g_media_popup_ctx->card, g_media_popup_ctx->title_label,
                     g_media_popup_ctx->icon_label, g_media_popup_ctx->close_button, nullptr,
                     g_media_popup_ctx->value_label);
    if (g_media_popup_ctx->motion_timer) lv_timer_resume(g_media_popup_ctx->motion_timer);
    return;
  }

  create_media_popup(init.bg_color ? init.bg_color : ui_theme::popup_card());
  if (g_media_popup_ctx->card) viewNavigationPopupShown(g_media_popup_ctx->card, init.entity_id.c_str());
  show_popup_shell(g_media_popup_ctx->overlay, g_media_popup_ctx->card, g_media_popup_ctx->title_label,
                   g_media_popup_ctx->icon_label, g_media_popup_ctx->close_button, nullptr,
                   g_media_popup_ctx->value_label);
  // After the shell: the body's colors belong to the surface it shows with.
  apply_init_to_context(g_media_popup_ctx, init);
  if (g_media_popup_ctx->motion_timer) lv_timer_resume(g_media_popup_ctx->motion_timer);
}

void preload_media_popup() {
  const ui_theme::SurfaceScope popup_scope = popup_surface::theme_scope();
  if (g_media_popup_ctx && g_media_popup_ctx->overlay && g_media_popup_ctx->card) return;
  MediaPopupInit init;
  init.entity_id = "__preload__";
  init.title = "";
  init.icon_name = "television";
  init.media_title = "";
  init.media_subtitle = "";
  init.bg_color = ui_theme::popup_card();
  show_media_popup(init);
  if (g_media_popup_ctx && g_media_popup_ctx->card && g_media_popup_ctx->overlay) {
    close_media_popup(g_media_popup_ctx);
  }
}

bool media_popup_showing(const String& entity_id) {
  if (!g_media_popup_ctx || !g_media_popup_ctx->overlay || !g_media_popup_ctx->card) return false;
  if (!g_media_popup_ctx->entity_id.length()) return false;
  if (!g_media_popup_ctx->entity_id.equalsIgnoreCase(entity_id)) return false;
  return !lv_obj_has_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
}

void update_media_popup(const MediaPopupInit& init) {
  if (!media_popup_showing(init.entity_id)) return;
  apply_init_to_context(g_media_popup_ctx, init);
}

void update_media_popup_cover(const char* entity_id, const lv_image_dsc_t* cover_dsc, uint32_t cover_hash) {
  if (!entity_id || !g_media_popup_ctx || !g_media_popup_ctx->card) return;
  if (!g_media_popup_ctx->entity_id.equalsIgnoreCase(entity_id)) return;
  if (lv_obj_has_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN)) return;
  const ui_theme::SurfaceScope popup_scope = body_scope(g_media_popup_ctx);
  update_cover(g_media_popup_ctx, cover_dsc, cover_hash);
}

void hide_media_popup() {
  if (!g_media_popup_ctx || !g_media_popup_ctx->card || !g_media_popup_ctx->overlay) return;
  close_media_popup(g_media_popup_ctx);
}

void media_popup_note_state(const String& entity_id, const char* payload) {
  if (!entity_id.length() || !payload || *payload != '{') return;
  const uint32_t hash = text_hash(payload);
  MediaExtras* entry = find_extras(entity_id);
  if (entry && entry->hash == hash) return;

  static JsonDocument filter;
  if (filter.isNull()) {
    for (const char* key : {"state", "media_artist", "media_album_name", "supported_features", "shuffle", "repeat",
                            "source", "source_list", "sound_mode", "sound_mode_list", "group_members"}) {
      filter[key] = true;
    }
    filter["group_candidates"][0]["entity_id"] = true;
    filter["group_candidates"][0]["name"] = true;
    filter["group_candidates"][0]["state"] = true;
  }
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) {
    static uint32_t last_log_ms = 0;
    if (millis() - last_log_ms > 10000) {
      last_log_ms = millis();
      Serial.printf("[MediaPopup] State fields of %s not read: %s\n", entity_id.c_str(), error.c_str());
    }
    return;
  }
  if (!entry) {
    if (g_extras.size() >= kMaxExtras) {
      size_t oldest = 0;
      for (size_t i = 1; i < g_extras.size(); ++i) {
        if (g_extras[i].used_ms < g_extras[oldest].used_ms) oldest = i;
      }
      g_extras.erase(g_extras.begin() + oldest);
    }
    g_extras.emplace_back();
    entry = &g_extras.back();
    entry->entity_id = entity_id;
  }
  MediaExtras& x = *entry;
  x.hash = hash;
  x.used_ms = millis();
  x.state = doc["state"] | "";
  x.artist = doc["media_artist"] | "";
  x.album = doc["media_album_name"] | "";
  x.has_features = doc["supported_features"].is<uint32_t>();
  x.features = doc["supported_features"] | 0u;
  x.has_shuffle = doc["shuffle"].is<bool>();
  x.shuffle = doc["shuffle"] | false;
  x.repeat = doc["repeat"] | "";
  x.source = doc["source"] | "";
  x.sound_mode = doc["sound_mode"] | "";
  read_names(doc["source_list"], x.sources, kMaxSources);
  read_names(doc["sound_mode_list"], x.sound_modes, kMaxSoundModes);
  read_names(doc["group_members"], x.members, kMaxMembers);
  x.candidates.clear();
  for (JsonObjectConst candidate : doc["group_candidates"].as<JsonArrayConst>()) {
    const char* id = candidate["entity_id"] | "";
    if (!*id) continue;
    x.candidates.push_back({id, candidate["name"] | id, candidate["state"] | ""});
    if (x.candidates.size() >= kMaxCandidates) break;
  }
}

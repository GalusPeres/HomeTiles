#include "src/ui/popups/popup_shell.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/ui/popups/popup_surface.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_pulse.h"
#include "src/ui/shared/icon_lock_mark.h"
#include "src/tiles/icons/mdi_icons.h"
#include <esp_heap_caps.h>
#include <lvgl_private.h>
#include <algorithm>
#include <new>
#include <cstring>
#include <string>
#include "src/ui/shared/ui_theme.h"

namespace {
struct Binding {
  lv_obj_t* owner = nullptr;
  lv_obj_t* body = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* close = nullptr;
  // Optional hidden label holding the current value for the header.
  lv_obj_t* value = nullptr;
  int32_t border_width = 0, shadow_width = 0, shadow_spread = 0;
  lv_color_t border_color{}, shadow_color{};
  void (*dismiss)() = nullptr;
  bool owner_deleting = false;
  lv_opa_t border_opa = LV_OPA_TRANSP, shadow_opa = LV_OPA_TRANSP;
};
// Header disc options from the opening tile (popup_shell_use_tile_disc).
struct HeaderDisc {
  bool from_tile = false;
  bool off = false;
  bool follows_global = false;
  bool glow = true;
  // The popup shows the tile color "From icon" (header_fill).
  bool from_icon = false;
  // The strength of the tile's color "From icon", 0 for another tile color
  // (header_fill).
  uint8_t tile_tint = 0;
};
HeaderDisc g_next_disc;
// Every popup body's overlay (create_popup_body), for popup_shell_delete_popups.
constexpr size_t kMaxPopupOverlays = 16;
lv_obj_t* g_popup_overlays[kMaxPopupOverlays] = {};
// popup_shell_hold_close_fill: a drag holds the close button's press color.
bool g_hold_close_fill = false;
struct Shell {
  HeaderDisc disc;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* frame = nullptr;
  lv_obj_t* header = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* icon_disc = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* close = nullptr;
  lv_obj_t* value = nullptr;
  // Last source text copied into `value`; the visible label holds fitted text.
  std::string value_source;
  Binding* active = nullptr;
} shell;

class SceneChange {
 public:
  SceneChange() : display_(lv_obj_get_display(shell.overlay)) {
    lv_display_enable_invalidation(display_, false);
  }
  ~SceneChange() { lv_display_enable_invalidation(display_, true); }
 private:
  lv_display_t* display_;
};

// `area` without `cut`: up to four rectangles (above, below, left, right).
int area_without(const lv_area_t& area, const lv_area_t& cut, lv_area_t* out) {
  if (!lv_area_is_on(&area, &cut)) {
    out[0] = area;
    return 1;
  }
  int count = 0;
  if (area.y1 < cut.y1) out[count++] = {area.x1, area.y1, area.x2, cut.y1 - 1};
  if (area.y2 > cut.y2) out[count++] = {area.x1, cut.y2 + 1, area.x2, area.y2};
  const int32_t y1 = LV_MAX(area.y1, cut.y1), y2 = LV_MIN(area.y2, cut.y2);
  if (area.x1 < cut.x1) out[count++] = {area.x1, y1, cut.x1 - 1, y2};
  if (area.x2 > cut.x2) out[count++] = {cut.x2 + 1, y1, area.x2, y2};
  return count;
}

// The frame with its shadow around it.
lv_area_t shell_outer_area() {
  lv_area_t outer;
  lv_obj_get_coords(shell.frame, &outer);
  const int32_t ext = lv_obj_get_ext_draw_size(shell.frame);
  lv_area_increase(&outer, ext, ext);
  return outer;
}

// The popup's pieces: the opaque frame without its rounded corners, its top
// and bottom edge between the corners, the four corner squares and the
// shadow's strips around it.
constexpr int kShellPieces = 11;
int shell_pieces(lv_area_t* out) {
  lv_area_t frame;
  lv_obj_get_coords(shell.frame, &frame);
  const int32_t r = LV_MIN(lv_obj_get_style_radius(shell.frame, LV_PART_MAIN),
                           LV_MIN(lv_area_get_width(&frame), lv_area_get_height(&frame)) / 2);
  out[0] = {frame.x1, frame.y1 + r, frame.x2, frame.y2 - r};
  out[1] = {frame.x1 + r, frame.y1, frame.x2 - r, frame.y1 + r - 1};
  out[2] = {frame.x1 + r, frame.y2 - r + 1, frame.x2 - r, frame.y2};
  out[3] = {frame.x1, frame.y1, frame.x1 + r - 1, frame.y1 + r - 1};
  out[4] = {frame.x2 - r + 1, frame.y1, frame.x2, frame.y1 + r - 1};
  out[5] = {frame.x1, frame.y2 - r + 1, frame.x1 + r - 1, frame.y2};
  out[6] = {frame.x2 - r + 1, frame.y2 - r + 1, frame.x2, frame.y2};
  return 7 + area_without(shell_outer_area(), frame, out + 7);
}

// Just before LVGL draws (its dirty areas joined), every dirty area reaching
// into the open popup is cut along the popup's pieces. As one area with the
// shadow, or joined with the released tile or a label laid out late, LVGL's
// refresh bands were wider than the frame or reached into its corners: no band
// was covered by it and everything under the popup was drawn first, 60-90 ms
// of every opening on the V2. Cut, LVGL starts each covered band at the frame.
// The popup's parts go first: the tapped tile marks itself on release, before
// its popup opens, so its area was drawn first and the part under the popup
// showed a flat, popup-colored square until the popup's own area reached it
// (V2 draws 28-line bands top to bottom; video of b92).
void cut_at_popup(lv_event_t* event) {
  if (!shell.active || lv_obj_get_style_bg_opa(shell.overlay, LV_PART_MAIN) != LV_OPA_TRANSP ||
      !lv_obj_is_visible(shell.frame))
    return;
  auto* display = static_cast<lv_display_t*>(lv_event_get_target(event));
  lv_area_t pieces[kShellPieces];
  const int piece_count = shell_pieces(pieces);
  const lv_area_t outer = shell_outer_area();
  struct Part {
    lv_area_t area;
    int piece;
  };
  Part inside[LV_INV_BUF_SIZE];
  lv_area_t outside[LV_INV_BUF_SIZE];
  uint32_t in = 0, out = 0;
  for (uint32_t i = 0; i < display->inv_p; ++i) {
    if (display->inv_area_joined[i]) continue;
    const lv_area_t& area = display->inv_areas[i];
    lv_area_t rest[4];
    const int rest_count = area_without(area, outer, rest);
    // Too many areas: LVGL's own list stays as it is.
    if (out + rest_count > LV_INV_BUF_SIZE) return;
    for (int k = 0; k < rest_count; ++k) outside[out++] = rest[k];
    for (int p = 0; p < piece_count; ++p) {
      if (in == LV_INV_BUF_SIZE) return;
      if (lv_area_intersect(&inside[in].area, &area, &pieces[p])) inside[in++].piece = p;
    }
  }
  // Within a piece, overlapping parts join the way LVGL joins its areas (no
  // part drawn twice); joined, they stay inside the piece.
  for (bool joined = true; joined;) {
    joined = false;
    for (uint32_t a = 0; a < in && !joined; ++a) {
      for (uint32_t b = a + 1; b < in && !joined; ++b) {
        if (inside[a].piece != inside[b].piece || !lv_area_is_on(&inside[a].area, &inside[b].area)) continue;
        lv_area_t both;
        lv_area_join(&both, &inside[a].area, &inside[b].area);
        if (lv_area_get_size(&both) >= lv_area_get_size(&inside[a].area) + lv_area_get_size(&inside[b].area))
          continue;
        inside[a].area = both;
        inside[b] = inside[--in];
        joined = true;
      }
    }
  }
  // The popup top to bottom, before anything beside it.
  std::sort(inside, inside + in, [](const Part& l, const Part& r) { return l.area.y1 < r.area.y1; });
  if (in + out > LV_INV_BUF_SIZE) return;
  display->inv_p = 0;
  for (uint32_t k = 0; k < in; ++k) display->inv_areas[display->inv_p++] = inside[k].area;
  for (uint32_t k = 0; k < out; ++k) display->inv_areas[display->inv_p++] = outside[k];
  for (uint32_t k = 0; k < display->inv_p; ++k) display->inv_area_joined[k] = 0;
}

void invalidate_shell() {
  // Transparent input overlays cover the display for hit testing only. Drawing
  // their full bounds would repaint every cached tile behind a small popup.
  if (lv_obj_get_style_bg_opa(shell.overlay, LV_PART_MAIN) != LV_OPA_TRANSP)
    lv_obj_invalidate(shell.overlay);
  else lv_obj_invalidate(shell.frame);
}

// Only the shared shell header draws, so only it gets the icon disc. The
// resident bodies keep invisible header labels as metadata holders.
void create_header(lv_obj_t* parent, lv_obj_t*& title, lv_obj_t*& icon,
                   lv_obj_t*& close, lv_event_cb_t handler, void* context,
                   lv_obj_t** icon_disc = nullptr) {
  title = lv_label_create(parent);
  lv_obj_set_width(title, LV_PCT(62));
  lv_obj_set_style_text_font(title, popup_layout::headerTitleFont(), 0);
  lv_obj_set_style_text_color(title, lv_color_hex(ui_theme::text()), 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_label_set_text(title, "");
  lv_obj_t* disc = icon_disc ? popup_layout::createHeaderIconDisc(parent) : nullptr;
  if (icon_disc) *icon_disc = disc;
  icon = lv_label_create(parent);
  lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(icon);
  popup_layout::styleHeaderIcon(icon);
  lv_obj_set_style_text_color(icon, lv_color_hex(ui_theme::text()), 0);
  lv_label_set_text(icon, "");
  close = popup_layout::createCloseButton(parent, handler, context);
  for (auto* object : {title, disc, icon, close})
    if (object) lv_obj_add_flag(object, LV_OBJ_FLAG_IGNORE_LAYOUT);
  popup_layout::alignHeader(parent, title, icon, disc);
}

// popup_shell_pulse_icon: opacity 1 -> 0 -> 1 in 1 s (state-control-styles),
// in step with the popup's control and the tile (ui_pulse.h).
void icon_pulse_exec(void* obj, int32_t) {
  lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), ui_pulse::opa_now(), 0);
}

void stop_icon_pulse() {
  if (!shell.icon || !lv_anim_get(shell.icon, icon_pulse_exec)) return;
  lv_anim_delete(shell.icon, icon_pulse_exec);
  lv_obj_set_style_opa(shell.icon, LV_OPA_COVER, 0);
}

// popup_shell_icon_lock: the header icon of a PIN-protected Folder or
// Settings carries the lock of its tile (icon_lock_mark.h); the rim takes the
// header disc over the popup card.
void header_lock_event_cb(lv_event_t* event) {
  lv_obj_t* icon = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  if (lv_event_get_code(event) == LV_EVENT_REFR_EXT_DRAW_SIZE) {
    icon_lock_mark::ext_draw_size(event, icon);
    return;
  }
  if (lv_event_get_code(event) != LV_EVENT_DRAW_POST || !shell.active) return;
  const lv_color_t under = lv_obj_get_style_bg_color(shell.active->body, LV_PART_MAIN);
  icon_lock_mark::draw(lv_event_get_layer(event), icon, icon_lock_mark::behind(shell.icon_disc, under));
}

void set_header_lock(bool on) {
  if (!shell.icon) return;
  bool had = false;
  while (lv_obj_remove_event_cb(shell.icon, header_lock_event_cb)) had = true;
  if (!on && !had) return;
  lv_obj_invalidate(shell.icon);
  if (on) {
    lv_obj_add_event_cb(shell.icon, header_lock_event_cb, LV_EVENT_DRAW_POST, nullptr);
    lv_obj_add_event_cb(shell.icon, header_lock_event_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
  }
  lv_obj_refresh_ext_draw_size(shell.icon);
  lv_obj_invalidate(shell.icon);
}

void detach() {
  stop_icon_pulse();
  set_header_lock(false);
  auto* binding = shell.active;
  shell.active = nullptr;
  if (!binding) return;
  cancel_popup_open(binding->body);
  lv_obj_add_flag(binding->body, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_parent(binding->body, binding->owner);
  lv_obj_center(binding->body);
  lv_obj_remove_flag(binding->owner, LV_OBJ_FLAG_CLICKABLE);
}

void owner_deleted(lv_event_t* event) {
  auto* binding = static_cast<Binding*>(lv_event_get_user_data(event));
  binding->owner_deleting = true;
  if (lv_obj_get_parent(binding->body) != binding->owner)
    lv_obj_delete(binding->body);
}

void body_deleted(lv_event_t* event) {
  auto* binding = static_cast<Binding*>(lv_event_get_user_data(event));
  if (shell.active == binding) {
    shell.active = nullptr;
    if (shell.overlay) lv_obj_add_flag(shell.overlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (!binding->owner_deleting)
    lv_obj_remove_event_cb_with_user_data(binding->owner, owner_deleted, binding);
  binding->~Binding();
  heap_caps_free(binding);
}

void popup_overlay_deleted(lv_event_t* event) {
  lv_obj_t* overlay = static_cast<lv_obj_t*>(lv_event_get_target(event));
  for (lv_obj_t*& slot : g_popup_overlays) {
    if (slot == overlay) slot = nullptr;
  }
}

void shell_deleted(lv_event_t*) {
  // Preserve cached bodies if the active screen itself is replaced.
  detach();
  shell = {};
}

void close_clicked(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !shell.active) return;
  auto* close = shell.active->close;
  // Existing callbacks still own command completion, PIN handling and cleanup.
  if (close) lv_obj_send_event(close, LV_EVENT_CLICKED, nullptr);
  // The original handler may refuse closing (PIN) or navigate back (Wi-Fi).
  sync_popup_shell();
}

Binding* bind(lv_obj_t* owner, lv_obj_t* body, lv_obj_t* title,
               lv_obj_t* icon, lv_obj_t* close) {
  for (uint32_t i = 0; i < lv_obj_get_event_count(body); ++i) {
    auto* event = lv_obj_get_event_dsc(body, i);
    if (lv_event_dsc_get_cb(event) == body_deleted)
      return static_cast<Binding*>(lv_event_dsc_get_user_data(event));
  }
  void* memory = heap_caps_malloc(sizeof(Binding), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) return nullptr;
  auto* b = new (memory) Binding{};
  b->owner = owner; b->body = body; b->title = title; b->icon = icon; b->close = close;
  b->border_width = lv_obj_get_style_border_width(body, LV_PART_MAIN);
  b->border_color = lv_obj_get_style_border_color(body, LV_PART_MAIN);
  b->border_opa = lv_obj_get_style_border_opa(body, LV_PART_MAIN);
  b->shadow_width = lv_obj_get_style_shadow_width(body, LV_PART_MAIN);
  b->shadow_spread = lv_obj_get_style_shadow_spread(body, LV_PART_MAIN);
  b->shadow_color = lv_obj_get_style_shadow_color(body, LV_PART_MAIN);
  b->shadow_opa = lv_obj_get_style_shadow_opa(body, LV_PART_MAIN);
  lv_obj_add_event_cb(body, body_deleted, LV_EVENT_DELETE, b);
  lv_obj_add_event_cb(owner, owner_deleted, LV_EVENT_DELETE, b);
  // Original header labels remain lightweight metadata holders for existing
  // cached-state paths. Their geometry is unchanged; only the shared header draws.
  for (auto* object : {title, icon, close})
    if (object) lv_obj_set_style_opa(object, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(body, LV_OPA_TRANSP, 0);
  return b;
}

void ensure_shell() {
  if (shell.overlay) return;
  shell.overlay = lv_obj_create(lv_layer_top());
  // Once per display: the shell is built again after popup_shell_delete_popups.
  static lv_display_t* cut_display = nullptr;
  lv_display_t* display = lv_obj_get_display(shell.overlay);
  if (display != cut_display) {
    lv_display_add_event_cb(display, cut_at_popup, LV_EVENT_RENDER_START, nullptr);
    cut_display = display;
  }
  lv_obj_remove_style_all(shell.overlay);
  lv_obj_set_size(shell.overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(shell.overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(shell.overlay, LV_OBJ_FLAG_CLICKABLE);
  shell.frame = lv_obj_create(shell.overlay);
  lv_obj_remove_style_all(shell.frame);
  lv_obj_set_style_bg_opa(shell.frame, LV_OPA_COVER, 0);
  lv_obj_remove_flag(shell.frame, LV_OBJ_FLAG_SCROLLABLE);
  shell.header = lv_obj_create(shell.overlay);
  lv_obj_remove_style_all(shell.header);
  lv_obj_remove_flag(shell.header, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(shell.header, LV_OBJ_FLAG_SCROLLABLE);
  create_header(shell.header, shell.title, shell.icon, shell.close, close_clicked, nullptr,
                &shell.icon_disc);
  // Second header line with the current value; only popups that pass a value
  // source show it.
  shell.value = lv_label_create(shell.header);
  lv_obj_set_width(shell.value, LV_PCT(62));
  lv_obj_set_style_text_font(shell.value, popup_layout::headerValueFont(), 0);
  lv_obj_set_style_text_color(shell.value, lv_color_hex(ui_theme::text()), 0);
  lv_label_set_text(shell.value, "");
  lv_obj_add_flag(shell.value, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_IGNORE_LAYOUT |
                                                          LV_OBJ_FLAG_HIDDEN));
  lv_obj_remove_flag(shell.value, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(shell.overlay, shell_deleted, LV_EVENT_DELETE, nullptr);
}

void copy_label(lv_obj_t* target, lv_obj_t* source, bool title,
                const lv_font_t* font_override = nullptr, bool copy_color = true) {
  if (!source || lv_obj_has_flag(source, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(target, LV_OBJ_FLAG_HIDDEN);
  const auto* font = font_override ? font_override
                                   : lv_obj_get_style_text_font(source, LV_PART_MAIN);
  if (font != lv_obj_get_style_text_font(target, LV_PART_MAIN))
    lv_obj_set_style_text_font(target, font, 0);
  // Colored icons (weather) draw their layers with recolor commands.
  if (!title && lv_label_get_recolor(source) != lv_label_get_recolor(target))
    lv_label_set_recolor(target, lv_label_get_recolor(source));
  const char* text = title ? hometiles_title::text(source) : lv_label_get_text(source);
  const char* current = title ? hometiles_title::text(target) : lv_label_get_text(target);
  if (strcmp(text, current) != 0) {
    if (title) hometiles_title::set(target, text, true);
    else lv_label_set_text(target, text);
  }
  if (!copy_color) return;
  const auto color = lv_obj_get_style_text_color(source, LV_PART_MAIN);
  if (!lv_color_eq(color, lv_obj_get_style_text_color(target, LV_PART_MAIN)))
    lv_obj_set_style_text_color(target, color, 0);
}

// The header circle for a card and icon color (tone_color::fill): the icon
// hue with "Circle in icon color" (or without a tile) when the icon is
// colored, else neutral.
tone_color::Fill header_fill(const HeaderDisc& options, uint32_t card, uint32_t rgb) {
  const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  const bool tinted = (r != g || g != b) && (!options.from_tile || options.glow);
  // A popup on a filled tile of the light theme: the tile's circle, one step
  // above the card in the icon's hue (tone_color::jewel_fill).
  if (tinted && ui_theme::g_surface == ui_theme::kDarkTintedSurface) {
    return tone_color::jewel_fill(card, rgb, ui_surface_style::icon_glow_percent(), options.tile_tint);
  }
  // The light theme's popup card: the circle and controls of a tile on a
  // white card (tile_icon_disc::tile_fill, Home Assistant's 20 % of the icon),
  // so the header circle, the selected button and the tile's circle match
  // (user 2026-10-10).
  if (ui_theme::light()) {
    const uint32_t shown = tone_color::readable_icon(rgb);
    const uint32_t over = tone_color::circle_source(tinted || !tile_tint::has_hue(shown) ? shown : ui_theme::icon());
    return tone_color::tile_fill(card, over, tinted, ui_surface_style::icon_glow_percent());
  }
  // The circle of the tile color "From icon" like on the tiles
  // (tile_icon_disc::circle_card): only a popup that shows that tile color
  // computes it for its own card; every other one for the card "From icon"
  // gives at the opening tile's strength (its default for another tile
  // color), exactly the tile's circle.
  const uint32_t circle_card = tinted && !options.from_icon && tone_color::g_from_icon_card
                                   ? tone_color::g_from_icon_card(rgb, false, options.tile_tint)
                                   : card;
  return tone_color::fill(circle_card, rgb, tinted, ui_surface_style::icon_glow_percent());
}

// The controls around it (pressed close, footer toggles, pills, arrows,
// editors, keys) take the circle's color whenever the circle is tinted, in
// every tile color and popup (user 2026-10-01); else the neutral step.
tone_color::Fill controls_fill(const HeaderDisc& options, uint32_t card, uint32_t rgb) {
  return header_fill(options, card, rgb);
}

void control_fill(const HeaderDisc& options, uint32_t card, uint32_t rgb, lv_color_t& color, lv_opa_t& opa) {
  const tone_color::Fill fill = controls_fill(options, card, rgb);
  color = lv_color_hex(fill.control_color);
  opa = fill.control_opa;
}

// The header disc looks like the opening tile's disc: shown or hidden by its
// Icon circle mode, tinted like it (header_fill) at the Circle strength.
// Popups opened without a tile tint by a colored icon and always show the
// disc. The header icon shows the source icon's color, lighter only where it
// would be hard to read, like on the tile (tone_color::readable_icon).
void apply_header_disc_tint(lv_obj_t* disc, lv_obj_t* icon, lv_obj_t* source) {
  if (!disc || !icon || !source) return;
  const uint32_t rgb =
      lv_color_to_u32(lv_obj_get_style_text_color(source, LV_PART_MAIN)) & 0xFFFFFFu;
  const uint32_t card = lv_color_to_u32(lv_obj_get_style_bg_color(shell.frame, LV_PART_MAIN)) & 0xFFFFFFu;
  const HeaderDisc& options = shell.disc;
  const bool shown = !options.from_tile ||
                     (!options.off && (!options.follows_global || ui_surface_style::icon_discs_shown()));
  tone_color::Fill fill = header_fill(options, card, rgb);
  uint32_t tile_circle = 0;
  uint8_t tile_circle_opa = 0;
  // The icon as its popup gives it (a light's icon is resolved by the Light
  // popup, tone_color::light_icon), readable like a tile's; on a tile's
  // circle exactly the tile's icon.
  uint32_t shown_icon = tone_color::readable_icon(rgb);
  // A popup on its tile's card (its color, popup_surface::g_open_surface)
  // shows exactly the tile's circle and icon (user 2026-10-10: "immer das,
  // was sie draußen haben").
  const bool on_tile_card = popup_surface::g_open_surface != ui_theme::kThemeSurface;
  if (options.from_tile && on_tile_card &&
      tile_icon_source::popup_tile_circle(rgb, tile_circle, tile_circle_opa, shown_icon)) {
    fill.disc_color = tile_circle;
    fill.disc_opa = tile_circle_opa;
  } else if (ui_theme::light()) {
    // On the light theme's neutral popup card (Light, Climate, Cover) the
    // circle is exactly its controls' fill, the selected mode button's color
    // (user 2026-10-10), also while a color is dragged.
    fill.disc_color = fill.control_color;
    fill.disc_opa = fill.control_opa;
  }
  const lv_color_t color = lv_color_hex(fill.disc_color);
  const lv_opa_t opa = shown ? fill.disc_opa : static_cast<lv_opa_t>(LV_OPA_TRANSP);
  if (!lv_color_eq(lv_obj_get_style_bg_color(disc, LV_PART_MAIN), color))
    lv_obj_set_style_bg_color(disc, color, 0);
  if (lv_obj_get_style_bg_opa(disc, LV_PART_MAIN) != opa) lv_obj_set_style_bg_opa(disc, opa, 0);
  const lv_color_t readable = lv_color_hex(shown_icon);
  if (!lv_color_eq(lv_obj_get_style_text_color(icon, LV_PART_MAIN), readable))
    lv_obj_set_style_text_color(icon, readable, 0);
  // The pressed close button has exactly the control color of the popup
  // (controls_fill, popup_nav_style.h): the same color at rest (transparent),
  // so the press fades from the card to it, and no theme darkening. Only a
  // change restyles it, and not during a drag (popup_shell_hold_close_fill).
  static lv_color_t close_color = lv_color_hex(ui_theme::text());
  static lv_opa_t close_opa = LV_OPA_20;
  const tone_color::Fill controls = controls_fill(options, card, rgb);
  const lv_color_t press = lv_color_hex(controls.control_color);
  const lv_opa_t press_opa = controls.control_opa;
  if (shell.close && !g_hold_close_fill && (!lv_color_eq(close_color, press) || close_opa != press_opa)) {
    close_color = press;
    close_opa = press_opa;
    lv_obj_set_style_bg_color(shell.close, press, 0);
    lv_obj_set_style_bg_color(shell.close, press, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(shell.close, press_opa, LV_STATE_PRESSED);
    lv_obj_set_style_color_filter_opa(shell.close, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_recolor_opa(shell.close, LV_OPA_TRANSP, LV_STATE_PRESSED);
  }
  // The card hairline is the plain lighter tile border. It never follows the
  // icon color: a hairline change redraws the whole popup, and a Light popup
  // changes its icon color on every step of a dragged color or Kelvin value.
  ui_surface_style::apply_popup_border(shell.frame, lv_color_hex(ui_theme::text()),
                                       static_cast<lv_opa_t>(popup_layout::kPopupBorderOpa));
}

// A header with a value line shows the title on one line; the classic header
// keeps the configured two-line title. Only a change re-renders the title.
void set_title_single_line(lv_obj_t* title, bool single_line) {
  auto* state = hometiles_title::state_for(title);
  if (!state || state->single_line == single_line) return;
  state->single_line = single_line;
  hometiles_title::render(title, state);
}

// Copy the current value text from the popup's hidden holder. The holder is
// never drawn, so its own changes do not repaint anything.
bool sync_header_value(lv_obj_t* source) {
  const char* text = source ? lv_label_get_text(source) : nullptr;
  const bool visible = text && text[0];
  const bool hidden = lv_obj_has_flag(shell.value, LV_OBJ_FLAG_HIDDEN);
  if (!visible) {
    if (!hidden) lv_obj_add_flag(shell.value, LV_OBJ_FLAG_HIDDEN);
    return false;
  }
  if (shell.value_source != text) {
    shell.value_source = text;
    hometiles_title::set(shell.value, text, true);
    set_title_single_line(shell.value, true);
  }
  if (hidden) lv_obj_remove_flag(shell.value, LV_OBJ_FLAG_HIDDEN);
  return true;
}
}

namespace {
// The surface each popup root shows now (popup_follow_surface); cleared with
// the popups (popup_shell_delete_popups).
struct SurfaceMark {
  lv_obj_t* root;
  int8_t surface;
};
constexpr int kSurfaceMarks = 16;
SurfaceMark g_surface_marks[kSurfaceMarks] = {};

// A deleted body frees its mark (a new body at the same address must not
// inherit the old surface, and the table must not fill up).
void surface_root_deleted(lv_event_t* event) {
  const lv_obj_t* root = static_cast<const lv_obj_t*>(lv_event_get_target(event));
  for (SurfaceMark& entry : g_surface_marks) {
    if (entry.root == root) entry = {};
  }
}
}  // namespace

void popup_follow_surface(lv_obj_t* root) {
  if (!root) return;
  const int8_t want = popup_surface::g_open_surface == ui_theme::kThemeSurface ? -1 : popup_surface::g_open_surface;
  SurfaceMark* mark = nullptr;
  for (SurfaceMark& entry : g_surface_marks) {
    if (entry.root == root) mark = &entry;
  }
  if (!mark) {
    for (SurfaceMark& entry : g_surface_marks) {
      if (!entry.root) {
        entry = {root, -1};
        lv_obj_remove_event_cb(root, surface_root_deleted);  // once, after popup_shell_delete_popups
        lv_obj_add_event_cb(root, surface_root_deleted, LV_EVENT_DELETE, nullptr);
        mark = &entry;
        break;
      }
    }
  }
  if (!mark || mark->surface == want) return;
  tile_icon_source::recolor_surface(root, mark->surface, want);
  mark->surface = want;
}

int8_t popup_drawn_surface(lv_obj_t* root) {
  for (const SurfaceMark& entry : g_surface_marks) {
    if (root && entry.root == root) return entry.surface < 0 ? ui_theme::kThemeSurface : entry.surface;
  }
  return ui_theme::kThemeSurface;
}

PopupShellParts create_popup_body(lv_event_cb_t close_handler, void* context,
                                 uint32_t color) {
  PopupShellParts parts{};
  parts.overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(parts.overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(parts.overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(parts.overlay, 0, 0);
  lv_obj_remove_flag(parts.overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(parts.overlay, LV_OBJ_FLAG_CLICKABLE);
  for (lv_obj_t*& slot : g_popup_overlays) {
    if (slot) continue;
    slot = parts.overlay;
    lv_obj_add_event_cb(parts.overlay, popup_overlay_deleted, LV_EVENT_DELETE, nullptr);
    break;
  }

  parts.card = lv_obj_create(parts.overlay);
  lv_obj_set_size(parts.card, popup_layout::kCardWidth, popup_layout::kCardHeight);
  lv_obj_center(parts.card);
  lv_obj_set_style_bg_color(parts.card, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(parts.card, LV_OPA_COVER, 0);
  ui_surface_style::apply_radius(parts.card, popup_layout::kCardRadius + Device::kGridGap, 0);
  lv_obj_set_style_border_width(parts.card, 0, 0);
  // No hairline on the body: the shell frame draws the only card border
  // (apply_popup_border); a second one on the body doubled its strength.
  lv_obj_set_style_pad_all(parts.card, popup_layout::kCardPad, 0);
  // No shadow where the card fills a square screen (nothing to see).
  if (!popup_layout::kCardFillsScreen) popup_layout::apply_card_shadow(parts.card);
  lv_obj_remove_flag(parts.card, LV_OBJ_FLAG_SCROLLABLE);
  create_header(parts.card, parts.title, parts.icon, parts.close, close_handler, context);
  return parts;
}

void show_popup_shell(lv_obj_t* owner, lv_obj_t* body, lv_obj_t* title,
                       lv_obj_t* icon, lv_obj_t* close, void (*dismiss)(),
                       lv_obj_t* value) {
  if (!owner || !body || !close) return;
  Binding* binding = bind(owner, body, title, icon, close);
  if (!binding) return;
  // The popup's fixed texts take the open surface (a filled tile's color).
  popup_follow_surface(body);
  binding->value = value;
  ensure_shell();
  // A newly shown popup takes the disc options of the tile that opened it (or
  // the default without one); a re-show keeps them unless a tile passed new ones.
  if (shell.active != binding || g_next_disc.from_tile) shell.disc = g_next_disc;
  g_next_disc = {};
  if (shell.active && shell.active != binding) {
    auto dismiss_previous = shell.active->dismiss;
    if (dismiss_previous) dismiss_previous();
  }
  invalidate_shell();
  {
    // Reparenting and stacking also invalidate the full parent in LVGL. These
    // ownership changes are invisible; redraw just the old and new popup bounds.
    SceneChange change;
    if (shell.active && shell.active != binding) detach();
    binding->dismiss = dismiss;
    shell.active = binding;
    lv_obj_set_parent(owner, lv_layer_top());
    lv_obj_add_flag(owner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(owner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_parent(shell.overlay, lv_screen_active());
    lv_obj_set_style_pad_all(shell.overlay, lv_obj_get_style_pad_top(owner, LV_PART_MAIN), 0);
    lv_obj_set_style_bg_color(shell.overlay, lv_obj_get_style_bg_color(owner, LV_PART_MAIN), 0);
    lv_obj_set_style_bg_opa(shell.overlay, lv_obj_get_style_bg_opa(owner, LV_PART_MAIN), 0);
    lv_obj_set_parent(body, shell.overlay);
    lv_obj_center(body);
    lv_obj_clear_flag(shell.overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(shell.overlay);
    lv_obj_move_foreground(shell.header);
    for (auto* object : {shell.frame, shell.header}) {
      lv_obj_set_size(object, lv_obj_get_style_width(body, LV_PART_MAIN),
                       lv_obj_get_style_height(body, LV_PART_MAIN));
      lv_obj_center(object);
      lv_obj_set_style_pad_all(object, lv_obj_get_style_pad_top(body, LV_PART_MAIN), 0);
      // Apply a new size now, while invalidation is off. Left to the next
      // layout pass, a switch from the full-screen Settings card to a tile
      // popup repainted the whole old area (every tile behind it, ~100 ms on
      // P4) although the hidden shell had already left it.
      lv_obj_refr_size(object);
      lv_obj_refr_pos(object);
    }
    lv_obj_set_style_border_width(shell.header, binding->border_width, 0);
    lv_obj_set_style_border_opa(shell.header, LV_OPA_TRANSP, 0);
    ui_surface_style::apply_radius(shell.frame, popup_layout::kCardRadius + Device::kGridGap, 0);
    lv_obj_set_style_border_width(shell.frame, binding->border_width, 0);
    lv_obj_set_style_border_color(shell.frame, binding->border_color, 0);
    lv_obj_set_style_border_opa(shell.frame, binding->border_opa, 0);
    // The blur follows the frame's current radius (popup_layout.h).
    lv_obj_set_style_shadow_width(
        shell.frame,
        binding->shadow_width > 0
            ? popup_layout::shadow_width_for(lv_obj_get_style_radius(shell.frame, LV_PART_MAIN))
            : 0,
        0);
    lv_obj_set_style_shadow_spread(shell.frame, binding->shadow_spread, 0);
    lv_obj_set_style_shadow_color(shell.frame, binding->shadow_color, 0);
    lv_obj_set_style_shadow_opa(shell.frame, binding->shadow_opa, 0);
    sync_popup_shell();
  }
  invalidate_shell();
}

bool popup_shell_active() { return shell.active != nullptr; }

lv_obj_t* popup_shell_body() { return shell.active ? shell.active->body : nullptr; }

void popup_shell_pulse_icon(lv_obj_t* body, bool on) {
  if (!shell.active || shell.active->body != body || !shell.icon) return;
  if (!on) {
    stop_icon_pulse();
    return;
  }
  // A refresh keeps a running pulse instead of restarting it.
  if (lv_anim_get(shell.icon, icon_pulse_exec)) return;
  ui_pulse::start(shell.icon, icon_pulse_exec);
}

void popup_shell_icon_lock(lv_obj_t* body, bool on) {
  if (!shell.active || shell.active->body != body) return;
  set_header_lock(on);
}

void popup_shell_hold_close_fill(bool hold) { g_hold_close_fill = hold; }

void popup_shell_use_tile_disc(bool off, bool follows_global, bool glow, bool from_icon, uint8_t tile_tint) {
  g_next_disc.from_tile = true;
  g_next_disc.off = off;
  g_next_disc.follows_global = follows_global;
  g_next_disc.glow = glow;
  g_next_disc.from_icon = from_icon;
  g_next_disc.tile_tint = tile_tint;
}

void popup_shell_use_no_tile_disc() { g_next_disc = {}; }

void popup_shell_control_fill(uint32_t card_rgb, uint32_t icon_rgb, lv_color_t& color, lv_opa_t& opa,
                              bool* tinted) {
  // A popup styles its controls before show_popup_shell() takes the options.
  const HeaderDisc& options = g_next_disc.from_tile ? g_next_disc : shell.disc;
  control_fill(options, card_rgb & 0xFFFFFFu, icon_rgb & 0xFFFFFFu, color, opa);
  if (tinted) *tinted = controls_fill(options, card_rgb & 0xFFFFFFu, icon_rgb & 0xFFFFFFu).tinted;
}

bool popup_shell_disc_fill(lv_color_t& color, lv_opa_t& opa) {
  if (!shell.icon_disc || !shell.active) return false;
  color = lv_obj_get_style_bg_color(shell.icon_disc, LV_PART_MAIN);
  opa = lv_obj_get_style_bg_opa(shell.icon_disc, LV_PART_MAIN);
  return true;
}

void popup_shell_control_raised_fill(uint32_t card_rgb, uint32_t icon_rgb, lv_color_t& color, lv_opa_t& opa) {
  const HeaderDisc& options = g_next_disc.from_tile ? g_next_disc : shell.disc;
  const tone_color::Fill fill = controls_fill(options, card_rgb & 0xFFFFFFu, icon_rgb & 0xFFFFFFu);
  color = lv_color_hex(fill.raised_color);
  opa = fill.control_opa;
}

void popup_shell_follow_tile_color(uint32_t color) {
  if (!shell.active || !shell.active->body) return;
  const lv_color_t value = lv_color_hex(color);
  if (lv_color_eq(lv_obj_get_style_bg_color(shell.active->body, LV_PART_MAIN), value)) return;
  lv_obj_set_style_bg_color(shell.active->body, value, 0);
  // The tile behind the popup was recolored and marked first, and the frame
  // takes the color only on the next sync. Drawn in that order, the tile's
  // part under the popup showed a flat square in the new color until the
  // popup's bands reached it (V2 video, cover change). As on opening, the
  // frame is marked now and drawn first (cut_at_popup).
  lv_obj_invalidate(shell.frame);
}

void popup_shell_delete_popups() {
  detach();
  // Rebuilt popups start with the theme's colors.
  for (SurfaceMark& mark : g_surface_marks) mark = {};
  for (lv_obj_t*& slot : g_popup_overlays) {
    lv_obj_t* overlay = slot;
    slot = nullptr;
    if (overlay) lv_obj_delete(overlay);
  }
  // shell_deleted resets the shell; ensure_shell builds it on the next show.
  if (shell.overlay) lv_obj_delete(shell.overlay);
}

void hide_popup_shell(lv_obj_t* body) {
  if (!shell.active || shell.active->body != body) return;
  g_hold_close_fill = false;
  invalidate_shell();
  SceneChange change;
  detach();
  lv_obj_add_flag(shell.overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_parent(shell.overlay, lv_layer_top());
}

void sync_popup_shell() {
  if (!shell.active) return;
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  auto* body = shell.active->body;
  if (lv_obj_has_flag(body, LV_OBJ_FLAG_HIDDEN)) { hide_popup_shell(body); return; }
  const auto color = lv_obj_get_style_bg_color(body, LV_PART_MAIN);
  if (!lv_color_eq(color, lv_obj_get_style_bg_color(shell.frame, LV_PART_MAIN)))
    lv_obj_set_style_bg_color(shell.frame, color, 0);
  const bool with_value = sync_header_value(shell.active->value);
  copy_label(shell.title, shell.active->title, true,
             with_value ? popup_layout::headerCompactTitleFont() : nullptr);
  set_title_single_line(shell.title, with_value);
  // The header's title and value take the text of the open popup's surface
  // (white on a filled tile's color in the light theme).
  const lv_color_t header_text = lv_color_hex(ui_theme::text());
  if (lv_color_eq(lv_obj_get_style_text_color(shell.title, LV_PART_MAIN),
                  lv_color_hex(ui_theme::roles_of(ui_theme::kThemeSurface).text)) &&
      !lv_color_eq(header_text, lv_obj_get_style_text_color(shell.title, LV_PART_MAIN))) {
    lv_obj_set_style_text_color(shell.title, header_text, 0);
  }
  if (!lv_color_eq(lv_obj_get_style_text_color(shell.value, LV_PART_MAIN), header_text)) {
    lv_obj_set_style_text_color(shell.value, header_text, 0);
  }
  copy_label(shell.icon, shell.active->icon, false, nullptr, false);
  apply_header_disc_tint(shell.icon_disc, shell.icon, shell.active->icon);
  // The disc appears only behind a visible header icon.
  lv_obj_set_flag(shell.icon_disc, LV_OBJ_FLAG_HIDDEN,
                  lv_obj_has_flag(shell.icon, LV_OBJ_FLAG_HIDDEN) ||
                      !lv_label_get_text(shell.icon)[0]);
  if (with_value)
    popup_layout::alignHeaderWithValue(shell.header, shell.title, shell.value, shell.icon,
                                       shell.icon_disc);
  else
    popup_layout::alignHeader(shell.header, shell.title, shell.icon, shell.icon_disc);
  copy_label(lv_obj_get_child(shell.close, 0), lv_obj_get_child(shell.active->close, 0), false);
  if (lv_obj_has_state(shell.active->close, LV_STATE_DISABLED))
    lv_obj_add_state(shell.close, LV_STATE_DISABLED);
  else lv_obj_remove_state(shell.close, LV_STATE_DISABLED);
}

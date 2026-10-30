#include "src/tiles/runtime/tile_icon_source.h"
#include "src/ui/shared/tone_color.h"

#include <ArduinoJson.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_icon_colors.h"
#include "src/tiles/config/tile_tint.h"
#include "src/types/weather/weather_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_color_rules.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/types/binary_sensor/renderer.h"
#include "src/types/cover/renderer.h"
#include "src/types/value/value_control.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/device/device_popup.h"
#include "src/ui/popups/vacuum/vacuum_popup.h"
#include "src/ui/popups/calendar/calendar_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_surface.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/ui/shared/ui_theme.h"

namespace tile_icon_source {
namespace {

// Domains shown by the Switch tile (Switch aliases, lights included).
bool switch_domain(const String& domain) {
  return domain == "light" || domain == "switch" || domain == "input_boolean" ||
         domain == "automation" || domain == "fan" || domain == "humidifier" ||
         domain == "remote" || domain == "siren";
}

// The raw state of a payload: the JSON "state" field, else the plain text.
String payload_state(const char* payload) {
  String state;
  if (!payload) return state;
  while (*payload == ' ' || *payload == '\t' || *payload == '\r' || *payload == '\n') ++payload;
  if (*payload != '{') {
    state = payload;
    state.trim();
    return state;
  }
  StaticJsonDocument<32> filter;
  filter["state"] = true;
  StaticJsonDocument<192> doc;
  if (deserializeJson(doc, payload, DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
    state = doc["state"] | "";
    state.trim();
  }
  return state;
}

bool state_known(const String& state) {
  String lower = state;
  lower.toLowerCase();
  return lower.length() && lower != "unavailable" && lower != "unknown" && lower != "none" &&
         lower != "null";
}

// "auto": the entity's own icon color as its tile shows it. `active` is
// false while the entity is off, closed or not running (its grey color).
bool auto_color(const String& domain, const char* payload, uint32_t& rgb, bool& active) {
  if (switch_domain(domain)) return switch_payload_icon_color(payload, rgb, &active);
  if (domain == "climate") return climate_payload_icon_color(payload, rgb, &active);
  if (domain == "cover") return cover_payload_icon_color(payload, rgb, &active);
  if (domain == "binary_sensor") {
    const BinarySensorState state = parse_binary_sensor_payload(payload);
    if (!state.valid || !state.available ||
        (state.value != BinarySensorValue::On && state.value != BinarySensorValue::Off)) {
      return false;
    }
    rgb = binary_sensor_visual_color(state);
    active = state.value == BinarySensorValue::On;
    return true;
  }
  return false;
}

// "rules": the bar and state colors on the entity state; Binary sensors also
// match their translated On/Off label like the Binary sensor tile.
bool rules_color(const String& record, const String& domain, const char* payload, uint32_t& rgb,
                 tile_icon_colors::RowFill* fill, tile_icon_colors::Icon* icon) {
  const String state = payload_state(payload);
  if (!state_known(state)) return false;
  const char* display = nullptr;
  if (domain == "binary_sensor") {
    const BinarySensorState parsed = parse_binary_sensor_payload(payload);
    if (parsed.value == BinarySensorValue::On || parsed.value == BinarySensorValue::Off) {
      display = i18n::binary_sensor_state_label(configManager.getConfig().language,
                                                binary_sensor_state_name(parsed.value),
                                                String(parsed.device_class));
    }
  }
  return tile_icon_colors::resolve(record.c_str(), state.c_str(), display, rgb, false, fill, icon);
}

// Tile types whose own state path already applies the fixed icon color
// (tile_icon_color_rules.h) or that have no state colors at all.
bool type_applies_fixed_icon_color(int type) {
  return tileTypeIconColorsByValue(type) || tileTypeIconColorsByState(type) ||
         tileTypeIsEditableValue(type) || tileTypeHasFixedIconColorOnly(type);
}

constexpr lv_style_selector_t kTintStore = tile_icon_disc::kCardTintStore;
static_assert(tile_icon_disc::kRuleFillStrength == tile_icon_colors::kTintDefault,
              "a rule row fills at the default strength");

// tone_color::g_from_icon_card: the card set_tile_tint gives "From icon" at
// `percent` (0 = its default strength), pressed like apply_card_background
// (0x10 lighter).
// The popup header asks on every loop pass, twice: the last few cards are
// kept (tile_tint::background runs double math in software on the panels).
uint32_t from_icon_card(uint32_t icon, bool pressed, uint8_t percent) {
  if (!percent) percent = tile_icon_colors::kTintDefault;
  const uint32_t base = tileDefaultBgColor();
  // Per theme: the tint mixes differently on the light card.
  const bool light = ui_theme::light();
  struct Entry {
    uint32_t base, icon, card;
    uint8_t percent;
    bool light, used;
  };
  static Entry cache[4] = {};
  static uint8_t next = 0;
  const Entry* found = nullptr;
  for (const Entry& entry : cache) {
    if (entry.used && entry.base == base && entry.icon == icon && entry.percent == percent && entry.light == light)
      found = &entry;
  }
  if (!found) {
    Entry& slot = cache[next];
    next = static_cast<uint8_t>((next + 1) % 4);
    slot = {base, icon, tile_tint::background(base, icon, percent), percent, light, true};
    found = &slot;
  }
  return pressed ? tone_color::pressed_card(found->card) : found->card;
}
[[maybe_unused]] const bool g_from_icon_card_registered = (tone_color::g_from_icon_card = &from_icon_card, true);

// "From cover" of a Media card (tile_icon_colors.h "cover" line), kept in an
// unused state selector of the card like the tint store: the cover color the
// Media renderer found (bg color; absent without one) and what refresh_card
// lets it color right now (bg opa: the tile tint percent; border opa: 1 for
// the icon). Cards without "From cover" never carry these values.
constexpr lv_style_selector_t kCoverStore = tile_icon_disc::kCardCoverStore;

// The opener object of the popup opened last and its parents. Only compared
// with cards, never dereferenced, so a deleted card cannot be touched.
constexpr int kPopupSourceDepth = 5;
lv_obj_t* g_popup_source[kPopupSourceDepth] = {};

void remember_popup_source(lv_obj_t* obj) {
  for (int i = 0; i < kPopupSourceDepth; ++i) {
    g_popup_source[i] = obj;
    obj = obj ? lv_obj_get_parent(obj) : nullptr;
  }
}

// Tile color "From icon" of a card: its strength (0 = off), kept as a local
// style value in the unused tint selector so the icon color hook finds it
// without a lookup table.

void set_tile_tint(lv_obj_t* card, uint32_t color, uint8_t percent);
void clear_tile_tint(lv_obj_t* card, bool plain = false);

bool icon_fill_marker(lv_obj_t* obj, uint8_t& marker) {
  lv_style_value_t value;
  if (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_OPA, &value, kTintStore) != LV_STYLE_RES_FOUND) return false;
  marker = static_cast<uint8_t>(value.num);
  return true;
}

void set_icon_fill_marker(lv_obj_t* card, uint8_t marker) {
  uint8_t current = 0;
  const bool found = icon_fill_marker(card, current);
  if (!marker) {
    if (found) lv_obj_remove_local_style_prop(card, LV_STYLE_BG_OPA, kTintStore);
    return;
  }
  if (!found || current != marker) lv_obj_set_style_bg_opa(card, marker, kTintStore);
}

bool cover_color(lv_obj_t* card, uint32_t& rgb) {
  lv_style_value_t value;
  if (!card || lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &value, kCoverStore) != LV_STYLE_RES_FOUND) {
    return false;
  }
  rgb = lv_color_to_u32(value.color) & 0xFFFFFF;
  return true;
}

bool cover_icon(lv_obj_t* card) {
  lv_style_value_t value;
  return card && lv_obj_get_local_style_prop(card, LV_STYLE_BORDER_OPA, &value, kCoverStore) == LV_STYLE_RES_FOUND;
}

// Whether "From cover" may color the icon now (refresh_card): unless an
// active rule colors it. The cover never tints the tile itself; "Fill"
// follows the icon (user 2026-10-11).
void set_cover_permissions(lv_obj_t* card, bool icon) {
  if (cover_icon(card) == icon) return;
  if (icon) lv_obj_set_style_border_opa(card, 1, kCoverStore);
  else lv_obj_remove_local_style_prop(card, LV_STYLE_BORDER_OPA, kCoverStore);
}

// The first icon disc of a card, directly or in a content container.
lv_obj_t* find_disc(lv_obj_t* card) {
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (tile_icon_disc::is_disc(child)) return child;
  }
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    const uint32_t inner = lv_obj_get_child_count(child);
    for (uint32_t j = 0; j < inner; ++j) {
      lv_obj_t* grandchild = lv_obj_get_child(child, static_cast<int32_t>(j));
      if (tile_icon_disc::is_disc(grandchild)) return grandchild;
    }
  }
  return nullptr;
}

// The color the icon of a disc was given; white without an icon.
uint32_t disc_icon_rgb(lv_obj_t* disc) {
  lv_obj_t* icon = disc ? tile_icon_disc::icon_of(disc) : nullptr;
  return icon ? tile_icon_disc::icon_color(icon) : ui_theme::text();
}

// A tile "From icon" fills in the neutral grey while its icon shows white or
// almost white light: the light theme's filled tiles (user 2026-10-10) and,
// since #130 (2026-10-26), every tile with "Ausfuellen" of the dark theme, so
// a light that is on always fills; tile_tint::choose gives no tint for a
// color without a hue.
tile_tint::Choice with_white(lv_obj_t* card, tile_tint::Choice choice, uint8_t fill, lv_obj_t* disc) {
  if (choice.percent || !fill || !disc || !tile_icon_disc::icon_of(disc)) return choice;
  uint8_t base = 0, flags = 0;
  if (ui_theme::theme_light() && (!tile_surface::rule_of(card, base, flags) || !(flags & tile_surface::kFollowTint))) {
    return choice;
  }
  const uint32_t icon = disc_icon_rgb(disc);
  // Home Assistant's light grey of a white light; a pure white icon (a
  // renderer's own white) has no color: the idle grey.
  if (tone_color::no_color(icon) && tone_color::from_rgb(icon).L <= tone_color::kWhiteIconL) {
    choice.color = icon;
    choice.percent = fill;
  }
  return choice;
}

// A tint of white or almost white is none (tone_color::no_color: Home
// Assistant reports white light as 255,255,251, which tinted the dark
// theme's card grey); the light theme's neutral fill follows (with_white).
tile_tint::Choice without_white(tile_tint::Choice choice) {
  if (choice.percent && tone_color::no_color(choice.color)) return tile_tint::Choice{};
  return choice;
}

// Shows the tint tile_tint::choose() picked, or the card's own color.
void apply_tint_choice(lv_obj_t* card, const tile_tint::Choice& choice) {
  if (choice.percent) set_tile_tint(card, choice.color, choice.percent);
  else clear_tile_tint(card);
}

// The fill a card shows (tile_icon_disc::shown_fill): the matching rule
// row's own "Fuellen", else "Ausfuellen", in the color its icon shows
// (tile_tint::choose). A row without "Fuellen" shows the plain card in
// either theme (user 2026-10-11).
void show_fill(lv_obj_t* card, lv_obj_t* disc) {
  const uint8_t fill = tile_icon_disc::shown_fill(card);
  if (!fill) {
    clear_tile_tint(card, tile_icon_disc::rule_fill(card) == tile_icon_disc::kRuleFillOff);
    return;
  }
  apply_tint_choice(card, with_white(card, without_white(tile_tint::choose(false, 0, 0, fill, disc_icon_rgb(disc))),
                                     fill, disc));
}

void follow_open_popup(lv_obj_t* card);

// Disc opacity and glow follow the (tinted) background, and so do the
// card-colored cut-outs (the Media play glyph, tile_icon_disc::mark_cutout):
// the renderer set them before a tint arrived (user 2026-10-10: the play
// glyph stayed black on a tinted Media tile).
void refresh_discs(lv_obj_t* card) {
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (tile_icon_disc::is_disc(child)) tile_icon_disc::apply_fill(child);
  }
  tile_icon_disc::refresh_cutouts(card);
}

// tile_icon_disc::g_icon_color_hook: an icon color change retints a card with
// Tile color "From icon": the tile always follows its icon. The card's
// buttons take the new press fill first; a retint reaches them again through
// refresh_discs with the new contrast step.
void on_icon_color(lv_obj_t* disc) {
  refresh_controls(lv_obj_get_parent(disc));
  lv_obj_t* card = tile_icon_disc::fill_card_of(lv_obj_get_parent(disc));
  if (!card) return;
  const uint32_t before = lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF;
  show_fill(card, disc);
  if ((lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF) == before) return;
  follow_open_popup(card);
  refresh_discs(card);
}

// An open popup of this card takes the card's current background.
void follow_open_popup(lv_obj_t* card) {
  if (!card || !popup_shell_active()) return;
  // Its popup keeps the neutral card (popup_background).
  const uint8_t tile_side = tile_surface::find(card);
  const int8_t surface = tile_surface::theme_of(tile_side);
  if (tile_side != tile_surface::kJewel && surface >= 0 && (surface == 1) != ui_theme::theme_light()) return;
  bool opened_here = false;
  for (lv_obj_t* source : g_popup_source) opened_here = opened_here || source == card;
  if (!opened_here) return;
  const uint32_t color = lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF;
  climate_popup_follow_tile_color(color);
  light_popup_follow_tile_color(color);
  cover_popup_follow_tile_color(color);
  device_popup_follow_tile_color(color);
  vacuum_popup_follow_tile_color(color);
  calendar_popup_follow_tile_color(color);
  sensor_popup_follow_tile_color(color);
  energy_popup_follow_tile_color(color);
  weather_popup_follow_tile_color(color);
  popup_shell_follow_tile_color(color);
}

// Sets the card background in its normal and pressed states
// (tone_color::pressed_card, like the tile renderers).
void apply_card_background(lv_obj_t* card, uint32_t rgb) {
  const uint32_t pressed = tone_color::pressed_card(rgb);
  for (const lv_style_selector_t selector : {static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_DEFAULT),
                                              static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_FOCUSED)}) {
    lv_obj_set_style_bg_color(card, lv_color_hex(rgb), selector);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(rgb), selector);
  }
  for (const lv_style_selector_t selector : {static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED),
                                              static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_FOCUSED | LV_STATE_PRESSED)}) {
    lv_obj_set_style_bg_color(card, lv_color_hex(pressed), selector);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed), selector);
  }
  // The frame line follows the card (ui_surface_style frame_line).
  ui_surface_style::refresh_tile_border(card);
}
// Tints a tile card for its rules (tile_tint.h). The tint replaces the card's
// own color and always starts from the global default tile color, so an own
// tile color never mixes with the rule color. The first tint keeps the card's
// own resting color in an unused state selector; clear restores it. A tap on
// a Light tile tints while the card is still pressed or fading back, so the
// shown color is the lighter press color: storing that made the tile one
// press step lighter after every on/off.
// A card with a rule (tile_surface::set_rule; a tile without an own color in
// the light theme) is filled while it is tinted (tile_surface::kJewel).
// The surface a popup draws with when `obj` (a tile) opens it
// (popup_surface::g_open_surface): a filled tile's rules, else the theme.
void set_open_popup_surface(lv_obj_t* obj) {
  popup_surface::g_open_surface = tile_surface::find(obj) == tile_surface::kJewel ? ui_theme::kDarkTintedSurface
                                                                                   : ui_theme::kThemeSurface;
}

void follow_tint(lv_obj_t* card, bool tinted) {
  uint8_t base = 0, flags = 0;
  if (!tile_surface::rule_of(card, base, flags)) return;
  change_surface(card, tile_surface::wanted(base, flags, tinted));
}

// Tints `card` with `color` at `percent`; `idle`: a filled tile without a
// color (kNeutralIdle) in the idle grey (tone_color::jewel_idle_card).
void tint_card(lv_obj_t* card, uint32_t color, uint8_t percent, bool idle) {
  if (!card) return;
  // A tile filled while tinted (light theme) enters that surface first.
  follow_tint(card, true);
  // A tile on its own surface tints with that surface's rules.
  const ui_theme::SurfaceScope surface = tile_surface::scope(card);
  lv_style_value_t stored;
  if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &stored, kTintStore) != LV_STYLE_RES_FOUND) {
    lv_obj_set_style_bg_color(card, lv_color_hex(tile_icon_disc::card_state_color(card, false)), kTintStore);
  }
  uint32_t tint = tile_tint::background(tileDefaultBgColor(), color, percent);
  if (tile_surface::get(card) == tile_surface::kJewel) {
    // Filled in the light theme: the card from the icon's own color, white
    // light in the neutral grey (the dark theme's recipe, user 2026-10-10).
    tint = idle ? tone_color::jewel_idle_card() : tone_color::jewel_card(color, percent);
    tile_surface::set_strength(card, percent);
  }
  if (tile_icon_disc::card_state_color(card, false) == tint) return;
  apply_card_background(card, tint);
}

void set_tile_tint(lv_obj_t* card, uint32_t color, uint8_t percent) { tint_card(card, color, percent, false); }

void clear_tile_tint(lv_obj_t* card, bool plain) {
  if (!card) return;
  // A Media tile of the light theme shows the neutral grey without a tint
  // (user 2026-10-10: "wenn nix abgespielt ... das Grau"), unless a rule row
  // keeps the tile unfilled (`plain`, user 2026-10-11).
  uint8_t base = 0, flags = 0;
  const bool rule = tile_surface::rule_of(card, base, flags);
  if (rule && (flags & tile_surface::kNeutralIdle) && !plain) {
    tint_card(card, 0xFFFFFF, 0, true);
    return;
  }
  // A tile filled while tinted leaves that surface with its tint; a tile
  // filled from the start leaves it for the theme's card.
  if (plain && rule) change_surface(card, tile_surface::kNone);
  else follow_tint(card, false);
  lv_style_value_t stored;
  if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &stored, kTintStore) != LV_STYLE_RES_FOUND) return;
  lv_obj_remove_local_style_prop(card, LV_STYLE_BG_COLOR, kTintStore);
  apply_card_background(card, lv_color_to_u32(stored.color) & 0xFFFFFF);
}


// Applies "From cover" as refresh_card allowed it: the icon takes the cover
// color (without one its default color); grey, white and black covers give
// no color. A tile with "Fill" follows the icon (on_icon_color).
void apply_cover(lv_obj_t* card) {
  if (!cover_icon(card)) return;
  uint32_t rgb = 0;
  const bool known = cover_color(card, rgb) && tile_tint::has_hue(rgb);
  if (lv_obj_t* icon = card_icon(card)) {
    if (known) tile_icon_disc::force_icon_color(icon, lv_color_hex(rgb));
    else tile_icon_disc::release_icon_color(icon);
  }
}

String layer_entity(const Tile& tile, const tile_icon_colors::Source& layer) {
  if (layer.mode == tile_icon_colors::SourceMode::None || !layer.enabled) return String();
  if (layer.self) return tile.sensor_entity;
  String entity;
  entity.reserve(layer.entity_len);
  for (size_t i = 0; i < layer.entity_len; ++i) entity += layer.entity[i];
  return entity;
}

}  // namespace

namespace {

// An MDI glyph (Supplementary Private Use Area: UTF-8 lead byte 0xF3).
bool icon_glyph(lv_obj_t* obj) {
  if (!lv_obj_check_type(obj, &lv_label_class)) return false;
  const char* text = lv_label_get_text(obj);
  return text && static_cast<uint8_t>(text[0]) == 0xF3;
}

// Every text color of `obj` and its children that is a role of `from` takes
// that role of `to`; the graph line of a Sensor tile too. Colors that are no
// role (an entity's color, a rule's) stay.
void recolor_roles(lv_obj_t* obj, const ui_theme::Roles& from, const ui_theme::Roles& to, int depth,
                   int max_depth = 5) {
  lv_style_value_t value;
  // The shown color and the colors an icon keeps (given, shown, requested,
  // forced: tile_icon_disc kIcon*), so no later refresh brings back a role
  // of the old surface (user 2026-10-10: dark icons on the grey tiles).
  // Also a control's selected row, arrow and items (a roller, a dropdown).
  static constexpr lv_style_selector_t kTexts[] = {
      LV_PART_MAIN, tile_icon_disc::kIconGiven, tile_icon_disc::kIconShown, tile_icon_disc::kIconRequested,
      tile_icon_disc::kIconForced, LV_PART_SELECTED, LV_PART_INDICATOR, LV_PART_ITEMS};
  const bool glyph = icon_glyph(obj);
  for (const lv_style_selector_t selector : kTexts) {
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_TEXT_COLOR, &value, selector) != LV_STYLE_RES_FOUND) continue;
    const uint32_t rgb = lv_color_to_u32(value.color) & 0xFFFFFF;
    const uint32_t mapped = ui_theme::map_role(rgb, from, to, glyph);
    if (mapped != rgb) lv_obj_set_style_text_color(obj, lv_color_hex(mapped), selector);
  }
  // Fills in a role (the Media play circle, a popup's play button and slider
  // knobs: the icon role, pressed the soft text) follow too; the card itself
  // and the circles keep theirs (refresh_discs).
  if (depth > 0 && !tile_icon_disc::is_disc(obj)) {
    static constexpr lv_style_selector_t kFills[] = {
        LV_PART_MAIN | LV_STATE_DEFAULT, LV_PART_MAIN | LV_STATE_PRESSED, LV_PART_KNOB | LV_STATE_DEFAULT,
        LV_PART_KNOB | LV_STATE_PRESSED};
    for (const lv_style_selector_t selector : kFills) {
      if (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_COLOR, &value, selector) != LV_STYLE_RES_FOUND) continue;
      const uint32_t rgb = lv_color_to_u32(value.color) & 0xFFFFFF;
      const uint32_t mapped = rgb == from.icon ? to.icon : rgb == from.text ? to.text
                              : rgb == from.text_soft ? to.text_soft : rgb;
      if (mapped != rgb) lv_obj_set_style_bg_color(obj, lv_color_hex(mapped), selector);
    }
  }
  if (lv_obj_check_type(obj, &lv_chart_class)) {
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_LINE_COLOR, &value, LV_PART_ITEMS) == LV_STYLE_RES_FOUND) {
      const uint32_t rgb = lv_color_to_u32(value.color) & 0xFFFFFF;
      const uint32_t mapped = ui_theme::map_role(rgb, from, to, false);
      if (mapped != rgb) lv_obj_set_style_line_color(obj, lv_color_hex(mapped), LV_PART_ITEMS);
    }
    for (lv_chart_series_t* series = lv_chart_get_series_next(obj, nullptr); series;
         series = lv_chart_get_series_next(obj, series)) {
      const uint32_t rgb = lv_color_to_u32(lv_chart_get_series_color(obj, series)) & 0xFFFFFF;
      const uint32_t mapped = ui_theme::map_role(rgb, from, to, false);
      if (mapped != rgb) lv_chart_set_series_color(obj, series, lv_color_hex(mapped));
    }
  }
  if (depth >= max_depth) return;
  const uint32_t count = lv_obj_get_child_count(obj);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(obj, static_cast<int32_t>(i));
    // Circles keep their own fills (refresh_discs); their icons are labels.
    recolor_roles(child, from, to, depth + 1, max_depth);
  }
}

}  // namespace

void change_surface(lv_obj_t* card, uint8_t surface, const Tile* tile) {
  if (!card || !tile_surface::marked(card)) return;
  const uint8_t before = tile_surface::get(card);
  if (before == surface) return;
  const ui_theme::Roles& from = ui_theme::roles_of(tile_surface::theme_of(before));
  tile_surface::set(card, surface);
  const int8_t theme = tile_surface::theme_of(surface);
  // An update of this card that is running draws on with the new surface.
  ui_theme::follow_surface(card, theme);
  const ui_theme::SurfaceScope scope(theme >= 0 ? theme : ui_theme::kThemeSurface, card);
  // The card's base behind the tint (the tint itself follows in
  // set_tile_tint); its hairline follows the surface.
  set_card_base(card, tile ? tileBgColorOrDefault(*tile, tileDefaultBgColor()) : tileDefaultBgColor());
  ui_surface_style::refresh_tile_border(card);
  recolor_roles(card, from, ui_theme::roles(), 0);
  // Circles, controls and card-colored cut-outs: refresh_discs and
  // refresh_controls of the caller (the tint that changed the surface).
}

void set_card_base(lv_obj_t* card, uint32_t rgb) {
  if (!card) return;
  lv_style_value_t stored;
  if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &stored, kTintStore) == LV_STYLE_RES_FOUND) {
    lv_obj_set_style_bg_color(card, lv_color_hex(rgb & 0xFFFFFF), kTintStore);
  } else {
    apply_card_background(card, rgb & 0xFFFFFF);
  }
}

bool cached_payload(const String& entity, String& payload) {
  payload = "";
  if (!entity.length()) return false;
  if (tiles_get_cached_entity_payload(entity.c_str(), payload) && payload.length()) return true;
  payload = haBridgeConfig.findSensorInitialValue(entity);
  return payload.length() > 0;
}

String rule_entity(const Tile& tile) {
  if (!tileTypeHasIconColors(tile.type) || !tile.icon_colors.length()) return String();
  return layer_entity(tile, tile_icon_colors::source_of(tile.icon_colors.c_str()));
}

bool rule_color(const Tile& tile, uint32_t& rgb, tile_icon_colors::RowFill* fill, tile_icon_colors::Icon* icon) {
  if (fill) *fill = tile_icon_colors::RowFill::Inherit;
  if (icon) *icon = tile_icon_colors::Icon{};
  if (!tileTypeHasIconColors(tile.type) || !tile.icon_colors.length()) return false;
  const tile_icon_colors::Source layer = tile_icon_colors::source_of(tile.icon_colors.c_str());
  const String entity = layer_entity(tile, layer);
  if (!entity.length()) return false;
  const bool automatic = layer.mode == tile_icon_colors::SourceMode::Auto;
  if (layer.self && tileTypeIsEditableValue(tile.type)) {
    // Number, Select and Date/Time keep their state in the editable cache.
    if (automatic) return false;
    const EditableValue value = parse_editable_value(haBridgeConfig.findEditableValue(entity));
    if (!value.valid || !value.has_state || !value.available || value.state == "unknown") return false;
    return tile_icon_colors::resolve(tile.icon_colors.c_str(), value.state.c_str(),
                                     editable_display_value(value).c_str(), rgb, false, fill, icon);
  }
  String payload;
  if (!cached_payload(entity, payload)) return false;
  String domain;
  const int dot = entity.indexOf('.');
  if (dot > 0) domain = entity.substring(0, dot);
  if (!automatic) return rules_color(tile.icon_colors, domain, payload.c_str(), rgb, fill, icon);
  bool running = true;
  return auto_color(domain, payload.c_str(), rgb, running);
}

void apply_initial(lv_obj_t* icon, const Tile& tile) {
  if (!icon) return;
  uint32_t fixed = 0xFFFFFF;
  if (tile.icon_colors.length()) tile_icon_colors::resolve(tile.icon_colors.c_str(), "", nullptr, fixed);
  if (tile_icon_disc::icon_color(icon) != (fixed & 0xFFFFFF)) {
    tile_icon_disc::set_icon_color(icon, lv_color_hex(fixed));
  }
}

lv_obj_t* card_icon(lv_obj_t* card) {
  if (!card) return nullptr;
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (tile_icon_disc::is_disc(child)) return tile_icon_disc::icon_of(child);
  }
  return nullptr;
}

namespace {
// The strength of the tile color "From icon" of the card `obj` belongs to
// (`obj` or up to three of its parents), 0 without it.
uint8_t tile_color_from_icon(lv_obj_t* obj) {
  for (int depth = 0; obj && depth < 4; ++depth, obj = lv_obj_get_parent(obj)) {
    uint8_t marker = 0, rule = 0;
    if (icon_fill_marker(obj, marker) || tile_icon_disc::has_rule_fill(obj, rule)) {
      return tile_icon_disc::shown_fill(obj);
    }
  }
  return 0;
}

// Hands the opening tile's circle options to the popup header. `obj` is the
// tile card or its icon label. A popup that shows the tile color "From icon"
// computes the circle for its own card; every other one (Global, Custom, and
// Climate, Light and Cover with the global background) for the card "From
// icon" gives at the tile's strength, so header circle and controls are
// exactly the tile's (popup_shell.cpp header_fill).
// The circle of the tile that opened the popup (popup_tile_circle): null or
// a live object. A grid rebuild deletes it while the pointer is kept (a PIN
// change, a folder switch), and LVGL 9.6's lv_obj_is_valid walks the
// object's own parents, so it cannot check a freed one: the circle clears
// the pointer when it is deleted.
lv_obj_t* g_popup_disc = nullptr;

void forget_popup_disc(lv_event_t* e) {
  if (lv_event_get_target(e) == g_popup_disc) g_popup_disc = nullptr;
}

void set_popup_disc(lv_obj_t* disc) {
  if (g_popup_disc == disc) return;
  if (g_popup_disc) lv_obj_remove_event_cb(g_popup_disc, forget_popup_disc);
  g_popup_disc = disc;
  if (disc) lv_obj_add_event_cb(disc, forget_popup_disc, LV_EVENT_DELETE, nullptr);
}

void pass_popup_disc(lv_obj_t* obj, bool popup_shows_tile_color) {
  lv_obj_t* disc = obj ? tile_icon_disc::disc_of(obj) : nullptr;
  if (!disc && obj) disc = find_disc(obj);
  set_popup_disc(disc);
  if (!disc) return;
  const tile_icon_disc::Mode mode = tile_icon_disc::mode_of(disc);
  const uint8_t from_icon = tile_color_from_icon(obj);
  popup_shell_use_tile_disc(mode == tile_icon_disc::Mode::Off, mode == tile_icon_disc::Mode::Global,
                            tile_icon_disc::glow_of(disc), popup_shows_tile_color && from_icon > 0,
                            from_icon);
}
}  // namespace

void recolor_surface(lv_obj_t* root, int8_t from, int8_t to) {
  if (!root || from == to) return;
  recolor_roles(root, ui_theme::roles_of(from), ui_theme::roles_of(to), 0, 16);
}

void forget_popup_source(lv_obj_t* obj) {
  // Light, Climate and Cover popups keep the neutral card: the theme.
  popup_surface::g_open_surface = ui_theme::kThemeSurface;
  remember_popup_source(nullptr);
  pass_popup_disc(obj, false);
}

void settle_filled(lv_obj_t* card) {
  uint8_t base = 0, flags = 0;
  if (!card || !tile_surface::rule_of(card, base, flags) || !(flags & tile_surface::kNeutralIdle)) return;
  if (tile_icon_disc::rule_fill(card) == tile_icon_disc::kRuleFillOff) return;
  lv_style_value_t stored;
  if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &stored, kTintStore) == LV_STYLE_RES_FOUND) return;
  clear_tile_tint(card);
}

bool popup_tile_circle(uint32_t rgb, uint32_t& circle, uint8_t& opa, uint32_t& icon) {
  lv_obj_t* disc = g_popup_disc;
  if (!disc || !tile_icon_disc::is_disc(disc)) return false;
  const ui_theme::SurfaceScope surface = tile_surface::scope(disc);
  // The popup's color is one of the theme (the popup draws on it): on the
  // tile's surface a role means that surface's role (surface_icon_color).
  rgb = ui_theme::map_role(rgb & 0xFFFFFF, ui_theme::roles_of(ui_theme::kThemeSurface), ui_theme::roles(), true);
  lv_obj_t* host = tile_icon_disc::card_of(disc);
  const uint32_t card = tile_icon_disc::card_state_color(host, false);
  const bool see_through = tile_icon_disc::see_through(host);
  const bool tinted = tile_icon_disc::glow_of(disc) && tile_icon_disc::icon_color_tints(rgb);
  const tone_color::Fill fill = tile_icon_disc::tile_fill(host, card, rgb & 0xFFFFFF, tinted, false, see_through,
                                                          ui_surface_style::icon_glow_percent());
  circle = fill.disc_color;
  opa = fill.disc_opa;
  icon = tone_color::readable_icon(rgb);
  return true;
}

void open_popup_without_tile() {
  popup_surface::g_open_surface = ui_theme::kThemeSurface;
  set_popup_disc(nullptr);
  remember_popup_source(nullptr);
  popup_shell_use_no_tile_disc();
}

uint32_t popup_background(lv_obj_t* obj, uint32_t fallback) {
  set_open_popup_surface(obj);
  remember_popup_source(obj);
  pass_popup_disc(obj, true);
  // A tile in its own color on the other theme's side (an own dark color in
  // the light theme) opens its popup on the theme's neutral card. A filled
  // tile (kJewel) passes its color like in the dark theme; its popup draws on
  // it with that surface's rules (popup_surface::follow).
  const uint8_t tile_side = tile_surface::find(obj);
  const int8_t surface = tile_surface::theme_of(tile_side);
  if (tile_side != tile_surface::kJewel && surface >= 0 && (surface == 1) != ui_theme::theme_light()) {
    return ui_theme::popup_card();
  }
  for (int depth = 0; obj && depth < 4; ++depth, obj = lv_obj_get_parent(obj)) {
    lv_style_value_t value;
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_COLOR, &value, kTintStore) != LV_STYLE_RES_FOUND) continue;
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_COLOR, &value, LV_PART_MAIN | LV_STATE_DEFAULT) !=
        LV_STYLE_RES_FOUND) {
      return fallback;
    }
    return lv_color_to_u32(value.color) & 0xFFFFFF;
  }
  return fallback;
}

namespace {
// Styles a card's controls from its resting and pressed color.
void style_controls(lv_obj_t* card) {
  const ui_theme::SurfaceScope surface_scope = tile_surface::scope(card);
  bool known = false;
  bool see_through = false;
  lv_color_t color = lv_color_hex(ui_theme::text());
  lv_color_t raised = lv_color_hex(ui_theme::text());
  uint32_t surface_pressed = 0;
  // Controls sit on the card or one level deeper (Climate - and + inside
  // their target pill); a button on a surface presses one control step above
  // it (tone_color::Fill::raised_color).
  auto style = [&](lv_obj_t* obj) {
    const bool press = tile_icon_disc::is_control(obj);
    if (!press && !tile_icon_disc::is_surface(obj)) return;
    if (!known) {
      // The controls take the circle's color whenever "Circle in icon color"
      // tints it, in every tile color (user 2026-10-01): on Global and Custom
      // cards the From icon circle (tile_icon_disc::circle_card). Without
      // the option, or with a white, grey or black icon, the neutral step.
      known = true;
      lv_obj_t* disc = find_disc(card);
      const uint32_t rgb = disc_icon_rgb(disc);
      const bool tinted = disc && tile_icon_disc::glow_of(disc) && tile_icon_disc::icon_color_tints(rgb);
      see_through = tile_icon_disc::see_through(card);
      const uint8_t percent = ui_surface_style::icon_glow_percent();
      const uint32_t rest_card = tile_icon_disc::card_state_color(card, false);
      const uint32_t down_card = tile_icon_disc::card_state_color(card, true);
      // Like the circle (tile_icon_disc::disc_fill): the light theme's tile
      // fill, else the steps.
      const tone_color::Fill fill = tile_icon_disc::tile_fill(card, rest_card, rgb, tinted, false, see_through,
                                                              percent);
      color = lv_color_hex(fill.control_color);
      raised = lv_color_hex(fill.raised_color);
      // A resting surface keeps its step above the pressed card too.
      surface_pressed =
          tile_icon_disc::tile_fill(card, down_card, rgb, tinted, true, see_through, percent).control_color;
    }
    const bool on_surface = press && tile_icon_disc::is_surface(lv_obj_get_parent(obj));
    ui_surface_style::apply_control_fill(obj, on_surface ? raised : color,
                                         press ? LV_PART_MAIN | LV_STATE_PRESSED : LV_PART_MAIN, see_through);
    if (!press) {
      // A surface touched itself (the Switch bar) keeps its resting color;
      // only its card's press shows the pressed step.
      const uint32_t rest = lv_color_to_u32(color) & 0xFFFFFF;
      const bool own_press = lv_obj_has_state(obj, LV_STATE_PRESSED) && !lv_obj_has_state(card, LV_STATE_PRESSED);
      tile_icon_disc::set_fill_colors(obj, rest, own_press ? rest : surface_pressed);
      tile_icon_disc::fade_with_card(obj);
    }
  };
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    style(child);
    const uint32_t inner = lv_obj_get_child_count(child);
    for (uint32_t j = 0; j < inner; ++j) style(lv_obj_get_child(child, static_cast<int32_t>(j)));
  }
}

// tile_icon_disc::g_card_state_hook: a card's resting controls (the Climate
// target pill) take its pressed state and fade with it like the circles
// (tile_icon_disc::fade_with_card); the buttons keep their own presses.
void follow_card_press(lv_obj_t* card) {
  if (!card) return;
  const bool pressed = lv_obj_has_state(card, LV_STATE_PRESSED);
  auto follow = [pressed](lv_obj_t* obj) {
    if (tile_icon_disc::is_surface(obj) && lv_obj_has_state(obj, LV_STATE_PRESSED) != pressed) {
      lv_obj_set_state(obj, LV_STATE_PRESSED, pressed);
    }
  };
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    follow(child);
    const uint32_t inner = lv_obj_get_child_count(child);
    for (uint32_t j = 0; j < inner; ++j) follow(lv_obj_get_child(child, static_cast<int32_t>(j)));
  }
}
}  // namespace

void refresh_controls(lv_obj_t* card) {
  if (card) style_controls(card);
}

bool card_cover_color(lv_obj_t* card, uint32_t& rgb) {
  uint32_t stored = 0;
  if (!cover_color(card, stored) || !tile_tint::has_hue(stored)) return false;
  rgb = stored;
  return true;
}

void set_cover_color(lv_obj_t* card, bool known, uint32_t rgb) {
  if (!card) return;
  uint32_t stored = 0;
  const bool had = cover_color(card, stored);
  rgb &= 0xFFFFFF;
  if (had == known && (!known || stored == rgb)) return;
  if (known) lv_obj_set_style_bg_color(card, lv_color_hex(rgb), kCoverStore);
  else lv_obj_remove_local_style_prop(card, LV_STYLE_BG_COLOR, kCoverStore);
  apply_cover(card);
}

void refresh_card(lv_obj_t* card, const Tile& tile) {
  tile_icon_disc::g_icon_color_hook = &on_icon_color;
  tile_icon_disc::g_card_state_hook = &follow_card_press;
  if (!card || !tileTypeHasIconColors(tile.type)) return;
  const tile_icon_colors::Source layer = tile_icon_colors::source_of(tile.icon_colors.c_str());
  uint32_t rgb = 0;
  tile_icon_colors::RowFill row_fill = tile_icon_colors::RowFill::Inherit;
  tile_icon_colors::Icon rule_glyph;
  const bool colored = layer.mode != tile_icon_colors::SourceMode::None && layer.enabled &&
                       rule_color(tile, rgb, &row_fill, &rule_glyph);
  // "From cover" exists on Media tiles only; every other type keeps its path.
  const tile_icon_colors::Cover cover =
      tile.type == TILE_MEDIA ? tile_icon_colors::cover_of(tile.icon_colors.c_str()) : tile_icon_colors::Cover{};
  // A rule colors the icon; "Ausfuellen" fills the tile with that color
  // (tile_icon_colors::rule_colors_icon: a former "Tint tile" rule too).
  const bool rule_icon = colored && tile_icon_colors::rule_colors_icon(layer);
  const bool cover_icon = tile_icon_colors::cover_colors_icon(cover);
  if (lv_obj_t* icon = card_icon(card)) {
    uint32_t fixed = 0;
    const bool force_fixed = !type_applies_fixed_icon_color(tile.type) && tile.icon_colors.length() &&
                             tile_icon_colors::resolve(tile.icon_colors.c_str(), "", nullptr, fixed);
    if (rule_icon) {
      tile_icon_disc::force_icon_color(icon, lv_color_hex(rgb));
    } else if (cover_icon) {
      // "From cover": apply_cover below shows the cover color or the default.
    } else if (force_fixed) {
      tile_icon_disc::force_icon_color(icon, lv_color_hex(fixed));
    } else {
      tile_icon_disc::release_icon_color(icon);
    }
    // A colored weather icon shows a forced color on all its layers.
    if (tile.type == TILE_WEATHER && weatherColoredIcons(tile)) {
      weather_icons::follow_icon_color(icon, rule_icon || force_fixed);
    }
    // The layer's matching rule shows its own icon (tile_rule_icon.h); rules
    // on the tile's own state without a layer line set it on their state
    // updates (tile_icon_color_rules::apply_icon). Weather icons keep their
    // own layered glyphs.
    if (layer.mode != tile_icon_colors::SourceMode::None && tile.type != TILE_WEATHER) {
      tile_rule_icon::show(icon, rule_icon ? rule_glyph : tile_icon_colors::Icon{});
    }
  }
  // "Ausfuellen" (tile_icon_colors::fills) fills the tile with the color the
  // icon shows, whatever gives it (Home Assistant, the own icon color, a
  // rule, a cover); grey, white and almost white give no color (the icon
  // still shows them). See tile_tint::choose. Nothing else tints a tile.
  const uint32_t before = lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF;
  set_icon_fill_marker(card, tileFills(tile) ? tile_icon_colors::kTintDefault : 0);
  // A rule layer's matching row may fill the tile or keep it plain ("Fuellen"
  // per row); rules on the tile's own state without a layer line set it on
  // their state updates (tile_icon_color_rules::note_fill).
  if (layer.mode != tile_icon_colors::SourceMode::None) {
    tile_icon_disc::set_rule_fill(card, tile_icon_color_rules::rule_fill_of(colored ? row_fill
                                                                                    : tile_icon_colors::RowFill::Inherit));
  }
  // "From cover" colors the icon only; the fill follows the icon.
  if (tile.type == TILE_MEDIA) set_cover_permissions(card, cover_icon && !rule_icon);
  show_fill(card, find_disc(card));
  if (tile.type == TILE_MEDIA) apply_cover(card);
  if ((lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF) != before) {
    follow_open_popup(card);
  }
  refresh_discs(card);
}

}  // namespace tile_icon_source

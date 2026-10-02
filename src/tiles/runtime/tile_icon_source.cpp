#include "src/tiles/runtime/tile_icon_source.h"

#include <ArduinoJson.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_icon_colors.h"
#include "src/tiles/config/tile_tint.h"
#include "src/types/weather/weather_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/types/binary_sensor/renderer.h"
#include "src/types/cover/renderer.h"
#include "src/types/value/value_control.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/device/device_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"

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
bool rules_color(const String& record, const String& domain, const char* payload, uint32_t& rgb) {
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
  return tile_icon_colors::resolve(record.c_str(), state.c_str(), display, rgb, false);
}

// Tile types whose own state path already applies the fixed icon color
// (tile_icon_color_rules.h) or that have no state colors at all.
bool type_applies_fixed_icon_color(int type) {
  return tileTypeIconColorsByValue(type) || tileTypeIconColorsByState(type) ||
         tileTypeIsEditableValue(type) || tileTypeHasFixedIconColorOnly(type);
}

constexpr lv_style_selector_t kTintStore = tile_icon_disc::kCardTintStore;

// tone_color::g_from_icon_card: the card set_tile_tint gives "From icon" at
// `percent` (0 = its default strength), pressed like apply_card_background
// (0x10 lighter).
// The popup header asks on every loop pass, twice: the last few cards are
// kept (tile_tint::background runs double math in software on the panels).
uint32_t from_icon_card(uint32_t icon, bool pressed, uint8_t percent) {
  if (!percent) percent = tile_icon_colors::kTintDefault;
  const uint32_t base = tileDefaultBgColor();
  struct Entry {
    uint32_t base, icon, card;
    uint8_t percent;
    bool used;
  };
  static Entry cache[4] = {};
  static uint8_t next = 0;
  const Entry* found = nullptr;
  for (const Entry& entry : cache) {
    if (entry.used && entry.base == base && entry.icon == icon && entry.percent == percent) found = &entry;
  }
  if (!found) {
    Entry& slot = cache[next];
    next = static_cast<uint8_t>((next + 1) % 4);
    slot = {base, icon, tile_tint::background(base, icon, percent), percent, true};
    found = &slot;
  }
  return pressed ? brighten_rgb_color(found->card, 0x10) : found->card;
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
void clear_tile_tint(lv_obj_t* card);

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

uint8_t cover_tile(lv_obj_t* card) {
  lv_style_value_t value;
  if (!card || lv_obj_get_local_style_prop(card, LV_STYLE_BG_OPA, &value, kCoverStore) != LV_STYLE_RES_FOUND) {
    return 0;
  }
  return static_cast<uint8_t>(value.num);
}

bool cover_icon(lv_obj_t* card) {
  lv_style_value_t value;
  return card && lv_obj_get_local_style_prop(card, LV_STYLE_BORDER_OPA, &value, kCoverStore) == LV_STYLE_RES_FOUND;
}

// A card tinted "From cover" with a cover color: its controls and its popup's
// controls take the circle's color like "From icon".
bool cover_tints(lv_obj_t* card) {
  uint32_t rgb = 0;
  return cover_tile(card) && cover_color(card, rgb) && tile_tint::has_hue(rgb);
}

// What "From cover" may color now (refresh_card): the icon unless an active
// rule colors it, the tile unless "From icon" or an active rule tints it.
void set_cover_permissions(lv_obj_t* card, bool icon, uint8_t tile) {
  if (cover_icon(card) != icon) {
    if (icon) lv_obj_set_style_border_opa(card, 1, kCoverStore);
    else lv_obj_remove_local_style_prop(card, LV_STYLE_BORDER_OPA, kCoverStore);
  }
  if (cover_tile(card) != tile) {
    if (tile) lv_obj_set_style_bg_opa(card, tile, kCoverStore);
    else lv_obj_remove_local_style_prop(card, LV_STYLE_BG_OPA, kCoverStore);
  }
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
  return icon ? tile_icon_disc::icon_color(icon) : 0xFFFFFF;
}

// Shows the tint tile_tint::choose() picked, or the card's own color.
void apply_tint_choice(lv_obj_t* card, const tile_tint::Choice& choice) {
  if (choice.percent) set_tile_tint(card, choice.color, choice.percent);
  else clear_tile_tint(card);
}

void follow_open_popup(lv_obj_t* card);

// Disc opacity and glow follow the (tinted) background.
void refresh_discs(lv_obj_t* card) {
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (tile_icon_disc::is_disc(child)) tile_icon_disc::apply_fill(child);
  }
}

// tile_icon_disc::g_icon_color_hook: an icon color change retints a card with
// Tile color "From icon": the tile always follows its icon. The card's
// buttons take the new press fill first; a retint reaches them again through
// refresh_discs with the new contrast step.
void on_icon_color(lv_obj_t* disc) {
  refresh_controls(lv_obj_get_parent(disc));
  lv_obj_t* card = lv_obj_get_parent(disc);
  uint8_t marker = 0;
  for (int depth = 0; card && depth < 3 && !icon_fill_marker(card, marker); ++depth) {
    card = lv_obj_get_parent(card);
  }
  if (!card || !marker) return;
  const uint32_t before = lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF;
  apply_tint_choice(card, tile_tint::choose(false, 0, 0, marker, disc_icon_rgb(disc)));
  if ((lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF) == before) return;
  follow_open_popup(card);
  refresh_discs(card);
}

// An open popup of this card takes the card's current background.
void follow_open_popup(lv_obj_t* card) {
  if (!card || !popup_shell_active()) return;
  bool opened_here = false;
  for (lv_obj_t* source : g_popup_source) opened_here = opened_here || source == card;
  if (!opened_here) return;
  const uint32_t color = lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF;
  climate_popup_follow_tile_color(color);
  light_popup_follow_tile_color(color);
  cover_popup_follow_tile_color(color);
  device_popup_follow_tile_color(color);
  sensor_popup_follow_tile_color(color);
  energy_popup_follow_tile_color(color);
  weather_popup_follow_tile_color(color);
  popup_shell_follow_tile_color(color);
}

// Sets the card background in its normal and pressed states (pressed about
// 6 % brighter, like the tile renderers).
void apply_card_background(lv_obj_t* card, uint32_t rgb) {
  uint32_t pressed = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const uint32_t channel = ((rgb >> shift) & 0xFF) + 0x10;
    pressed |= (channel > 0xFF ? 0xFF : channel) << shift;
  }
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
}
// Tints a tile card for its rules (tile_tint.h). The tint replaces the card's
// own color and always starts from the global default tile color, so an own
// tile color never mixes with the rule color. The first tint keeps the card's
// own resting color in an unused state selector; clear restores it. A tap on
// a Light tile tints while the card is still pressed or fading back, so the
// shown color is the lighter press color: storing that made the tile one
// press step lighter after every on/off.
void set_tile_tint(lv_obj_t* card, uint32_t color, uint8_t percent) {
  if (!card) return;
  lv_style_value_t stored;
  if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &stored, kTintStore) != LV_STYLE_RES_FOUND) {
    lv_obj_set_style_bg_color(card, lv_color_hex(tile_icon_disc::card_state_color(card, false)), kTintStore);
  }
  const uint32_t tint = tile_tint::background(tileDefaultBgColor(), color, percent);
  if (tile_icon_disc::card_state_color(card, false) == tint) return;
  apply_card_background(card, tint);
}

void clear_tile_tint(lv_obj_t* card) {
  if (!card) return;
  lv_style_value_t stored;
  if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &stored, kTintStore) != LV_STYLE_RES_FOUND) return;
  lv_obj_remove_local_style_prop(card, LV_STYLE_BG_COLOR, kTintStore);
  apply_card_background(card, lv_color_to_u32(stored.color) & 0xFFFFFF);
}


// Applies "From cover" as refresh_card allowed it: the icon takes the cover
// color (without one its default color), the tile the cover tint (without
// one its own color). Grey, white and black covers give no color.
void apply_cover(lv_obj_t* card) {
  uint32_t rgb = 0;
  const bool known = cover_color(card, rgb) && tile_tint::has_hue(rgb);
  if (cover_icon(card)) {
    if (lv_obj_t* icon = card_icon(card)) {
      if (known) tile_icon_disc::force_icon_color(icon, lv_color_hex(rgb));
      else tile_icon_disc::release_icon_color(icon);
    }
  }
  const uint8_t tile = cover_tile(card);
  if (!tile) return;
  const uint32_t before = lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF;
  if (known) set_tile_tint(card, rgb, tile);
  else clear_tile_tint(card);
  if ((lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF) != before) {
    follow_open_popup(card);
  }
  refresh_discs(card);
  refresh_controls(card);
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

bool rule_color(const Tile& tile, uint32_t& rgb, bool* active) {
  // Own rules only give a color while one matches, which counts as active.
  if (active) *active = true;
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
                                     editable_display_value(value).c_str(), rgb, false);
  }
  String payload;
  if (!cached_payload(entity, payload)) return false;
  String domain;
  const int dot = entity.indexOf('.');
  if (dot > 0) domain = entity.substring(0, dot);
  if (!automatic) return rules_color(tile.icon_colors, domain, payload.c_str(), rgb);
  bool running = true;
  if (!auto_color(domain, payload.c_str(), rgb, running)) return false;
  if (active) *active = running;
  return true;
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
  uint8_t marker = 0;
  for (int depth = 0; obj && depth < 4; ++depth, obj = lv_obj_get_parent(obj)) {
    if (icon_fill_marker(obj, marker)) return marker;
    // "From cover" tints like "From icon".
    if (cover_tints(obj)) return cover_tile(obj);
  }
  return 0;
}

// Hands the opening tile's circle options to the popup header. `obj` is the
// tile card or its icon label. A popup that shows the tile color "From icon"
// computes the circle for its own card; every other one (Global, Custom, and
// Climate, Light and Cover with the global background) for the card "From
// icon" gives at the tile's strength, so header circle and controls are
// exactly the tile's (popup_shell.cpp header_fill).
void pass_popup_disc(lv_obj_t* obj, bool popup_shows_tile_color) {
  lv_obj_t* disc = obj ? tile_icon_disc::disc_of(obj) : nullptr;
  if (!disc && obj) disc = find_disc(obj);
  if (!disc) return;
  const tile_icon_disc::Mode mode = tile_icon_disc::mode_of(disc);
  const uint8_t from_icon = tile_color_from_icon(obj);
  popup_shell_use_tile_disc(mode == tile_icon_disc::Mode::Off, mode == tile_icon_disc::Mode::Global,
                            tile_icon_disc::glow_of(disc), popup_shows_tile_color && from_icon > 0,
                            from_icon);
}
}  // namespace

void forget_popup_source(lv_obj_t* obj) {
  remember_popup_source(nullptr);
  pass_popup_disc(obj, false);
}

uint32_t popup_background(lv_obj_t* obj, uint32_t fallback) {
  remember_popup_source(obj);
  pass_popup_disc(obj, true);
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
  bool known = false;
  bool see_through = false;
  lv_color_t color = lv_color_white();
  lv_color_t raised = lv_color_white();
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
      const tone_color::Fill fill = tone_color::fill(
          tile_icon_disc::circle_card(card, rest_card, rgb, tinted, false, see_through), rgb, tinted, percent,
          see_through);
      color = lv_color_hex(fill.control_color);
      raised = lv_color_hex(fill.raised_color);
      // A resting surface keeps its step above the pressed card too.
      surface_pressed = tone_color::fill(tile_icon_disc::circle_card(card, down_card, rgb, tinted, true, see_through),
                                         rgb, tinted, percent, see_through)
                            .control_color;
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
  bool active = false;
  const bool colored = layer.mode != tile_icon_colors::SourceMode::None && layer.enabled &&
                       rule_color(tile, rgb, &active);
  // "From cover" exists on Media tiles only; every other type keeps its path.
  const tile_icon_colors::Cover cover =
      tile.type == TILE_MEDIA ? tile_icon_colors::cover_of(tile.icon_colors.c_str()) : tile_icon_colors::Cover{};
  if (lv_obj_t* icon = card_icon(card)) {
    uint32_t fixed = 0;
    const bool force_fixed = !type_applies_fixed_icon_color(tile.type) && tile.icon_colors.length() &&
                             tile_icon_colors::resolve(tile.icon_colors.c_str(), "", nullptr, fixed);
    if (colored && layer.icon) {
      tile_icon_disc::force_icon_color(icon, lv_color_hex(rgb));
    } else if (cover.icon) {
      // "From cover": apply_cover below shows the cover color or the default.
    } else if (force_fixed) {
      tile_icon_disc::force_icon_color(icon, lv_color_hex(fixed));
    } else {
      tile_icon_disc::release_icon_color(icon);
    }
    // A colored weather icon shows a forced color on all its layers.
    if (tile.type == TILE_WEATHER && weatherColoredIcons(tile)) {
      weather_icons::follow_icon_color(icon, (colored && layer.icon) || force_fixed);
    }
  }
  // Tile color "From icon" (fill) follows the color the icon shows, rules
  // included; otherwise a rule "Tint tile" tints while it applies. Entity
  // color tints only while the entity is active; grey off colors never tint
  // (the icon still shows the grey). See tile_tint::choose.
  const uint32_t before = lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF;
  const uint8_t fill = tile_icon_colors::fill_of(tile.icon_colors.c_str());
  const tile_tint::Choice choice =
      tile_tint::choose(colored && active, rgb, layer.tile, fill, disc_icon_rgb(find_disc(card)));
  set_icon_fill_marker(card, fill);
  // "From cover" tints below an active rule and never with "From icon".
  const uint8_t cover_tint = cover.tile && !choice.percent ? cover.tile : 0;
  if (tile.type == TILE_MEDIA) set_cover_permissions(card, cover.icon && !(colored && layer.icon), cover_tint);
  if (!cover_tint) apply_tint_choice(card, choice);
  if (tile.type == TILE_MEDIA) apply_cover(card);
  if ((lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF) != before) {
    follow_open_popup(card);
  }
  refresh_discs(card);
}

}  // namespace tile_icon_source

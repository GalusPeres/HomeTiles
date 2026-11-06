#include "src/ui/shared/ui_surface_style.h"
#include "src/types/camera/renderer.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/shared/tone_color.h"
#include "src/types/camera/picture.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/shared/ui_theme.h"

#include <algorithm>
#include <vector>

namespace {

static const i18n::Strings& camera_text() {
  return i18n::strings(configManager.getConfig().language);
}

struct CameraEventData {
  String entity_id;
  String title;
  String icon_name;
  uint32_t bg_color = 0;  // 0: the popup takes its default card
  // A tap opens the full screen straight away (camera_picture::tap_full_screen).
  bool full_screen = false;
  // The tile's entries (camera_entries.h) for the popup's menu.
  String entries;
  // With "last chosen": the record's hash, and the parts that follow a choice.
  uint32_t remember_key = 0;
  lv_obj_t* card = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* title_label = nullptr;
};

// "Zuletzt gewaehlt" lives only in RAM (user 2026-11-06, no flash write, nothing
// in the Bridge): the first entry until something is chosen and after every
// restart. Per record, so an edited tile starts over.
struct LastChoice {
  uint32_t key;
  uint8_t index;
};
std::vector<LastChoice> g_last_choices;
// The tiles on screen with "last chosen".
std::vector<CameraEventData*> g_remembering;

static size_t last_choice(uint32_t key, size_t count) {
  for (const LastChoice& choice : g_last_choices) {
    if (choice.key == key) return choice.index < count ? choice.index : 0;
  }
  return 0;
}

static String friendly_camera_name(const String& entity_id) {
  String name = haBridgeConfig.findSensorName(entity_id);
  if (name.length()) return name;
  const int dot = entity_id.indexOf('.');
  name = dot >= 0 ? entity_id.substring(dot + 1) : entity_id;
  name.replace('_', ' ');
  if (name.length() && name[0] >= 'a' && name[0] <= 'z') {
    name.setCharAt(0, static_cast<char>(name[0] - ('a' - 'A')));
  }
  return name;
}

static void camera_tile_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) return;
  CameraEventData* data =
      static_cast<CameraEventData*>(lv_event_get_user_data(event));
  if (!data || !data->entity_id.length()) return;
  finish_press_before_popup(event);
  CameraPopupInit init;
  init.entity_id = data->entity_id;
  init.title = data->title;
  init.icon_name = data->icon_name;
  init.bg_color = tile_icon_source::popup_background(static_cast<lv_obj_t*>(lv_event_get_current_target(event)), data->bg_color);
  // The header icon takes the color the tile icon shows right now (fixed or
  // from its rules).
  lv_obj_t* icon = tile_icon_source::card_icon(static_cast<lv_obj_t*>(lv_event_get_current_target(event)));
  init.icon_color =
      icon ? lv_color_to_u32(lv_obj_get_style_text_color(icon, LV_PART_MAIN)) & 0xFFFFFF : 0xFFFFFF;
  init.full_screen = data->full_screen;
  init.entries = data->entries;
  init.remember_key = data->remember_key;
  show_camera_popup(init);
}

static void camera_tile_delete_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) return;
  CameraEventData* data = static_cast<CameraEventData*>(lv_event_get_user_data(event));
  g_remembering.erase(std::remove(g_remembering.begin(), g_remembering.end(), data), g_remembering.end());
  delete data;
}

}  // namespace

String camera_entry_title(const camera_entries::Entry& entry) {
  if (!entry.title.empty()) return entry.title.c_str();
  if (!entry.name.empty()) return entry.name.c_str();
  return friendly_camera_name(entry.entity.c_str());
}

String camera_entry_icon(const camera_entries::Entry& entry) {
  if (isMdiIconDisabled(entry.icon.c_str())) return "";
  String icon = normalizeMdiIconName(entry.icon.c_str());
  if (icon.length()) return icon;
  if (entry.kind == camera_entries::kWebcam) return "webcam";
  if (entry.kind != camera_entries::kHomeAssistant) return "weather-pouring";
  icon = normalizeMdiIconName(haBridgeConfig.findEntityIcon(entry.entity.c_str()));
  if (icon.length()) return icon;
  return camera_picture::is_image(entry.entity.c_str()) ? "image" : "video";
}

// In the popup's menu a radar place without an own icon is a place.
String camera_entry_list_icon(const camera_entries::Entry& entry) {
  if (entry.icon.empty() && (entry.kind == camera_entries::kRadarPlace || entry.kind == camera_entries::kRadarCoordinates)) {
    return "map-marker";
  }
  const String icon = camera_entry_icon(entry);
  return icon.length() ? icon : String("video");
}

void camera_tile_entry_chosen(uint32_t key, size_t index) {
  bool known = false;
  for (LastChoice& choice : g_last_choices) {
    if (choice.key != key) continue;
    choice.index = static_cast<uint8_t>(index);
    known = true;
  }
  // A bounded list: one per edited tile at most until the next restart.
  if (!known) {
    if (g_last_choices.size() >= 64) g_last_choices.erase(g_last_choices.begin());
    g_last_choices.push_back(LastChoice{key, static_cast<uint8_t>(index)});
  }
  for (CameraEventData* data : g_remembering) {
    if (data->remember_key != key) continue;
    const std::vector<camera_entries::Entry> entries = camera_entries::parse(data->entries.c_str());
    if (index >= entries.size()) continue;
    const camera_entries::Entry& entry = entries[index];
    data->entity_id = entry.entity.c_str();
    data->title = camera_entry_title(entry);
    data->icon_name = camera_entry_icon(entry);
    if (data->icon && data->icon_name.length()) lv_label_set_text(data->icon, getMdiChar(data->icon_name).c_str());
    if (data->title_label) hometiles_title::tile(data->title_label, data->title.c_str(), false);
    camera_picture::show_entity(data->card, data->entity_id);
  }
}

lv_obj_t* render_camera_tile(lv_obj_t* parent,
                             int col,
                             int row,
                             const Tile& tile,
                             uint8_t,
                             GridType grid_type) {
  if (!parent) return nullptr;

  lv_obj_t* card = lv_button_create(parent);
  if (!card) return nullptr;
  // The head over a picture (picture.h) draws on its fixed surface
  // (tile_renderer.cpp tile_surface_of): its card takes that theme's tile
  // color, which this scope gives, whatever the tile's own color.
  const uint32_t card_color = camera_picture::head_over_picture(tile)
                                  ? tileDefaultBgColor()
                                  : tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(card,
                            lv_color_hex(tone_color::pressed_card(card_color)),
                            LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);
  place_tile_card(card, col, row, tile);

  // The entry the tile shows: its stored one, or the one last chosen in the
  // popup (RAM only). All entries of a tile are in its record.
  const std::vector<camera_entries::Entry> entries = camera_entries::parse(tile.flow_entities.c_str());
  const bool remembers = entries.size() >= 2 && camera_picture::last_chosen(tile);
  const uint32_t remember_key = remembers ? camera_entries::fnv1a(tile.flow_entities.c_str()) : 0;
  const camera_entries::Entry* chosen = remembers ? &entries[last_choice(remember_key, entries.size())] : nullptr;
  const String shown_entity = chosen ? String(chosen->entity.c_str()) : tile.sensor_entity;

  String icon_name = chosen ? camera_entry_icon(*chosen) : tile.icon_name;
  const bool icon_disabled = chosen ? !icon_name.length() : isMdiIconDisabled(icon_name);
  icon_name = normalizeMdiIconName(icon_name);
  if (!icon_disabled && !icon_name.length() && shown_entity.length()) {
    icon_name =
        normalizeMdiIconName(haBridgeConfig.findEntityIcon(shown_entity));
  }
  if (!icon_disabled && !icon_name.length()) icon_name = "video";

  String title = chosen ? camera_entry_title(*chosen) : tile.title;
  title.trim();
  if (!title.length()) title = friendly_camera_name(shown_entity);
  if (!title.length()) title = camera_text().camera_tile_type;

  // A half-height Camera uses the half-height Sensor header: the icon in the
  // concentric corner disc and the title beside it.
  const bool compact = tile_geometry::compact_icon_title(tile.type, tile.span_w, tile.span_h);
  lv_obj_t* icon = nullptr;
  String icon_char;
  if (icon_name.length() && FONT_MDI_ICONS != nullptr) {
    icon_char = getMdiChar(icon_name);
  }
  if (icon_char.length()) {
    icon = lv_label_create(card);
    set_label_style(icon, lv_color_hex(ui_theme::icon()), FONT_MDI_ICONS);
    lv_label_set_text(icon, icon_char.c_str());
    tile_icon_source::apply_initial(icon, tile);
    if (!compact) {
      lv_obj_align(icon, LV_ALIGN_CENTER, 0,
                   tile_icon_disc::icon_title_offsets(FONT_MDI_ICONS, tile_layout::header_title_font()).icon);
      tile_icon_disc::add_round(card, icon);
    }
  }

  lv_obj_t* title_label = lv_label_create(card);
  set_label_style(title_label, lv_color_hex(ui_theme::text()),
                  tile_layout::header_title_font());
  hometiles_title::tile(title_label, title.c_str(), false);
  if (compact) {
    compact_sensor_layout::apply(card, icon, title_label, nullptr, tile);
  } else if (icon) {
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0,
                 tile_icon_disc::icon_title_offsets(FONT_MDI_ICONS, tile_layout::header_title_font()).title);
  } else {
    lv_obj_center(title_label);
  }

  // The Bridge's picture instead of the icon (picture.h), not at half
  // height: it shows the icon look until the first picture arrives.
  if (!compact && grid_type != GridType::SCREENSAVER) {
    camera_picture::attach(card, icon, title_label, tile, shown_entity);
  }

  if (grid_type != GridType::SCREENSAVER && shown_entity.length()) {
    CameraEventData* event_data = new CameraEventData();
    event_data->entity_id = shown_entity;
    event_data->title = title;
    event_data->icon_name = icon_name;
    event_data->bg_color = card_color;
    event_data->full_screen = camera_picture::tap_full_screen(tile);
    if (entries.size() >= 2) event_data->entries = tile.flow_entities;
    event_data->remember_key = remember_key;
    event_data->card = card;
    event_data->icon = icon;
    event_data->title_label = title_label;
    if (remembers) g_remembering.push_back(event_data);
    lv_obj_add_event_cb(card, camera_tile_event_cb, LV_EVENT_SHORT_CLICKED,
                        event_data);
    lv_obj_add_event_cb(card, camera_tile_delete_cb, LV_EVENT_DELETE,
                        event_data);
  }
  return card;
}

#include "src/ui/navigation/view_navigation.h"
#include "src/ui/ui_manager.h"

#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/ui/tabs/settings/tab_settings.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/core/display/display_manager.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/devices/device_select.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/config/tile_config.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/network/transport/network_transport.h"
#include "src/fonts/ui_fonts.h"
#include "src/ui/popups/popup_layout.h"

#include <time.h>
#include <string.h>

#include "src/core/hardware/board_hal.h"

static const lv_font_t* get_status_time_font() {
  const DeviceConfig& cfg = configManager.getConfig();
  return (cfg.status_time_font_size == 24)
             ? popup_layout::font24()
             : popup_layout::font48();
}

static const lv_font_t* get_status_date_font() {
  const DeviceConfig& cfg = configManager.getConfig();
  return (cfg.status_date_font_size == 20)
             ? popup_layout::font20()
             : popup_layout::font24();
}



// Global instance

UIManager uiManager;



// Timezone

const char* UIManager::TZ_EUROPE_BERLIN = "CET-1CEST,M3.5.0/02,M10.5.0/03";

static const char* timezone_spec_for_code(const char* code) {
  static const char* kBerlinSpec = "CET-1CEST,M3.5.0/02,M10.5.0/03";
  if (!code || !code[0]) return kBerlinSpec;
  if (strcasecmp(code, "berlin") == 0) return kBerlinSpec;
  if (strcasecmp(code, "london") == 0) return "GMT0BST,M3.5.0/01,M10.5.0/02";
  if (strcasecmp(code, "utc") == 0) return "UTC0";
  if (strcasecmp(code, "athens") == 0) return "EET-2EEST,M3.5.0/03,M10.5.0/04";
  if (strcasecmp(code, "istanbul") == 0) return "TRT-3";
  if (strcasecmp(code, "moscow") == 0) return "MSK-3";
  if (strcasecmp(code, "johannesburg") == 0) return "SAST-2";
  if (strcasecmp(code, "nairobi") == 0) return "EAT-3";
  if (strcasecmp(code, "dubai") == 0) return "GST-4";
  if (strcasecmp(code, "karachi") == 0) return "PKT-5";
  if (strcasecmp(code, "kolkata") == 0) return "IST-5:30";
  if (strcasecmp(code, "dhaka") == 0) return "BDT-6";
  if (strcasecmp(code, "bangkok") == 0) return "ICT-7";
  if (strcasecmp(code, "singapore") == 0) return "SGT-8";
  if (strcasecmp(code, "perth") == 0) return "AWST-8";
  if (strcasecmp(code, "new_york") == 0) return "EST5EDT,M3.2.0/02,M11.1.0/02";
  if (strcasecmp(code, "chicago") == 0) return "CST6CDT,M3.2.0/02,M11.1.0/02";
  if (strcasecmp(code, "denver") == 0) return "MST7MDT,M3.2.0/02,M11.1.0/02";
  if (strcasecmp(code, "phoenix") == 0) return "MST7";
  if (strcasecmp(code, "los_angeles") == 0) return "PST8PDT,M3.2.0/02,M11.1.0/02";
  if (strcasecmp(code, "honolulu") == 0) return "HST10";
  if (strcasecmp(code, "buenos_aires") == 0) return "ART3";
  if (strcasecmp(code, "sao_paulo") == 0) return "BRT3";
  if (strcasecmp(code, "tokyo") == 0) return "JST-9";
  if (strcasecmp(code, "darwin") == 0) return "ACST-9:30";
  if (strcasecmp(code, "sydney") == 0) return "AEST-10AEDT,M10.1.0/02,M4.1.0/03";
  if (strcasecmp(code, "auckland") == 0) return "NZST-12NZDT,M9.5.0/02,M4.1.0/03";
  return kBerlinSpec;
}



// Build the UI.
void UIManager::buildUI(scene_publish_cb_t scene_cb, hotspot_start_cb_t hotspot_cb) {
  Serial.println("[UI] Building UI...");

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);


  for (uint8_t i = 0; i < TAB_COUNT; ++i) {
    tab_panels[i] = nullptr;
    tab_buttons[i] = nullptr;
    tab_labels[i] = nullptr;
  }
  active_tab_index = UINT8_MAX;
  nav_container = nullptr;
  tab_content_container = nullptr;
  settings_gesture_enabled = false;
  settings_gesture_edge = UINT8_MAX;


  tab_content_container = lv_obj_create(scr);
  lv_obj_set_style_bg_color(tab_content_container, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(tab_content_container, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tab_content_container, 0, 0);
  lv_obj_set_style_pad_all(tab_content_container, 0, 0);
  lv_obj_clear_flag(tab_content_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(tab_content_container, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(tab_content_container, 0, 0);

  tab_panels[0] = createTabPanel(tab_content_container);
  tab_panels[1] = createTabPanel(tab_content_container);
  tab_panels[2] = createTabPanel(tab_content_container);
  tab_panels[3] = createTabPanel(tab_content_container);

  build_tiles_tab(tab_panels[0], GridType::TAB0, scene_cb);
  build_settings_tab(tab_panels[3], hotspot_cb);
  createSettingsGestureZone();
  for (uint8_t i = 0; i < TAB_COUNT; ++i) {
    if (tab_panels[i]) {
      lv_obj_add_flag(tab_panels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  switchToTab(0);

  // Keep popup bodies resident; the shared shell presents before content work.
  preload_light_popup();
  preload_sensor_popup();
  preload_weather_popup();
  preload_energy_popup();
  preload_media_popup();
  preload_climate_popup();
  preload_cover_popup();
  preload_pin_popup();
  preload_camera_popup();

  access_gesture_eligible = false;
  mqttPublishDeviceSettings();

  Serial.println("[UI] UI built");
}

// Initialize the status bar.

void UIManager::statusbarInit(lv_obj_t *tab_bar) {
  if (status_container || !tab_bar) return;

  status_container = lv_obj_create(tab_bar);
  lv_obj_set_size(status_container, LV_PCT(100), LV_SIZE_CONTENT);

  lv_obj_set_style_bg_color(status_container, lv_color_hex(0x2A2A2A), 0);

  lv_obj_set_style_bg_opa(status_container, LV_OPA_COVER, 0);

  lv_obj_set_style_border_width(status_container, 0, 0);

  lv_obj_set_style_radius(status_container, popup_layout::scale(12), 0);

  lv_obj_set_style_pad_all(status_container, popup_layout::scale(12), 0);

  lv_obj_set_style_pad_row(status_container,
                           popup_layout::scale(10), 0);
  lv_obj_set_flex_flow(status_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(status_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_clear_flag(status_container, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_clear_flag(status_container, LV_OBJ_FLAG_CLICKABLE);



  status_time_label = lv_label_create(status_container);
  lv_obj_set_width(status_time_label, LV_PCT(100));
  lv_obj_set_style_min_width(status_time_label,
                             popup_layout::scale(200), 0);
  lv_obj_set_style_max_width(status_time_label,
                             popup_layout::scale(200), 0);
  lv_obj_set_style_align(status_time_label, LV_ALIGN_CENTER, 0);
  lv_label_set_long_mode(status_time_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(status_time_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(status_time_label, lv_color_white(), 0);

  lv_obj_set_style_text_font(status_time_label, get_status_time_font(), 0);
  // Use placeholder characters supported by the digit font.
  lv_label_set_text(status_time_label, "");



  status_date_label = lv_label_create(status_container);

  lv_obj_set_width(status_date_label, LV_PCT(100));

  lv_label_set_long_mode(status_date_label, LV_LABEL_LONG_CLIP);

  lv_obj_set_style_text_align(status_date_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_set_style_text_color(status_date_label, lv_color_hex(0xC8C8C8), 0);

  lv_obj_set_style_text_font(status_date_label, get_status_date_font(), 0);
  // Use placeholder characters supported by the digit font.
  lv_label_set_text(status_date_label, "");
}

lv_obj_t* UIManager::setupTabButton(lv_obj_t *btn, uint8_t tab_index, const char *icon_name, const char *tab_name) {
  if (!btn) return nullptr;

  // Button Styling (Normal State: Transparent)
  lv_obj_set_width(btn, LV_PCT(100));
  lv_obj_set_height(btn, popup_layout::scale(100));
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xE38422), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_outline_width(btn, 0, 0);

  // Use bright orange while pressed.
  lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xE38422), LV_STATE_PRESSED);

  // Disable animation.
  lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);

  lv_obj_set_style_radius(btn, popup_layout::scale(24), 0);
  lv_obj_set_style_pad_all(btn, popup_layout::scale(8), 0);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

  // Column flex layout: icon above text
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(btn, popup_layout::scale(12), 0);

  lv_obj_t *icon_label = nullptr;
  lv_obj_t *text_label = nullptr;

  bool has_icon = (icon_name && strlen(icon_name) > 0);
  bool has_name = (tab_name && strlen(tab_name) > 0);

  // Icon, if configured
  if (has_icon) {
    String iconChar = getMdiChar(String(icon_name));
    if (iconChar.length() > 0) {
      icon_label = lv_label_create(btn);
      lv_label_set_text(icon_label, iconChar.c_str());
      lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
      lv_obj_set_style_text_font(icon_label, FONT_MDI_ICONS, 0);
      popup_layout::applyIconScale(icon_label);
    }
  }

  // Name or fallback number
  if (has_name) {
    text_label = lv_label_create(btn);
    lv_label_set_text(text_label, tab_name);
    lv_obj_set_style_text_color(text_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(text_label, popup_layout::font24(), 0);
    lv_label_set_long_mode(text_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(text_label, LV_PCT(90));
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
  } else if (!has_icon) {
    // Fallback when both are empty
    text_label = lv_label_create(btn);
    const char* fallback = (tab_index == 3) ? "Settings" : "";
    if (fallback[0] == '\0') {
      char num_fallback[4];
      snprintf(num_fallback, sizeof(num_fallback), "%u", static_cast<unsigned>(tab_index + 1));
      lv_label_set_text(text_label, num_fallback);
    } else {
      lv_label_set_text(text_label, fallback);
    }
    lv_obj_set_style_text_color(text_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(text_label, popup_layout::font24(), 0);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
  }

  // Return text label (for potential later updates)
  return text_label;
}

lv_obj_t* UIManager::createTabPanel(lv_obj_t *parent) {
  if (!parent) return nullptr;

  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  return panel;
}

void UIManager::switchToTab(uint8_t index) {
  viewNavigationClosePopups();
  if (index >= TAB_COUNT) return;
  if (active_tab_index == index) return;

  lv_display_t* disp = displayManager.getDisplay();
  // The settings page is already built at boot. Suppress the two full-panel
  // invalidations while swapping tabs; the physical framebuffer is cleared
  // explicitly below and only the five visible settings controls need LVGL
  // rasterization. Keeping the synchronous refresh preserves the 8-inch
  // ghosting protection without redrawing all 1280x800 pixels.
  const bool partial_settings_switch =
      index == 3 && active_tab_index != UINT8_MAX && disp &&
      lv_display_is_invalidation_enabled(disp);
  if (partial_settings_switch) {
    lv_display_enable_invalidation(disp, false);
  }

  static constexpr size_t kTilesBufferLines = SCREEN_HEIGHT / GRID_ROWS;
  static constexpr size_t kSettingsBufferLines = kTilesBufferLines;
  if (index == 3) {
    displayManager.setBufferLines(kSettingsBufferLines);
  } else {
    displayManager.setBufferLines(kTilesBufferLines);
  }

  // Deactivate the previous tab, if present.
  if (active_tab_index != UINT8_MAX && active_tab_index < TAB_COUNT) {
    if (tab_panels[active_tab_index]) {
      lv_obj_add_flag(tab_panels[active_tab_index], LV_OBJ_FLAG_HIDDEN);
    }
    if (tab_buttons[active_tab_index]) {
      lv_obj_set_style_bg_opa(tab_buttons[active_tab_index], LV_OPA_TRANSP, 0);
    }
    // Keep the label white in every state.
  }

  if (index <= 2) {
    GridType grid_type = static_cast<GridType>(index);
    if (!tiles_is_loaded(grid_type)) {
      tiles_request_reload(grid_type);
    }
  }

  // Activate the new tab.
  if (tab_panels[index]) {
    lv_obj_clear_flag(tab_panels[index], LV_OBJ_FLAG_HIDDEN);
  }
  if (tab_buttons[index]) {
    lv_obj_set_style_bg_opa(tab_buttons[index], LV_OPA_COVER, 0);
  }
  // Keep the label white in every state.

  active_tab_index = index;

  if (partial_settings_switch) {
    lv_display_enable_invalidation(disp, true);

    const uint32_t switch_started_ms = millis();
#if defined(DEVICE_ESP32_S3_RGB_480)
    // The S3 log showed 70 small flushes and about 172 ms for the settings
    // switch. Invalidating each child separately fragments the refresh and
    // repeatedly rotates/copies into the live PSRAM framebuffer. One panel
    // invalidation lets LVGL merge the work into the configured display bands.
    lv_obj_invalidate(tab_panels[index]);
    const uint32_t cleared_ms = switch_started_ms;
    const uint32_t child_count = lv_obj_get_child_count(tab_panels[index]);
#else
    BoardHAL::displayFillScreen(0x0000);
    const uint32_t cleared_ms = millis();

    const uint32_t child_count = lv_obj_get_child_count(tab_panels[index]);
    for (uint32_t i = 0; i < child_count; ++i) {
      lv_obj_t* child = lv_obj_get_child(tab_panels[index], static_cast<int32_t>(i));
      if (child && !lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_invalidate(child);
      }
    }
#endif
    lv_refr_now(disp);

    const uint32_t finished_ms = millis();
    Serial.printf(
        "[UI] Settings switch: clear=%lu ms draw=%lu ms total=%lu ms "
        "children=%lu mode=%s\n",
        static_cast<unsigned long>(cleared_ms - switch_started_ms),
        static_cast<unsigned long>(finished_ms - cleared_ms),
        static_cast<unsigned long>(finished_ms - switch_started_ms),
        static_cast<unsigned long>(child_count),
#if defined(DEVICE_ESP32_S3_RGB_480)
        "s3-panel"
#else
        "children"
#endif
    );
    return;
  }

  lv_obj_invalidate(lv_scr_act());
  if (disp) {
    lv_refr_now(disp);
  }
}

void UIManager::switchToFolder(uint16_t folder_id) {
  // Prioritize navigation by briefly pausing thumbnail/URL background work.
  tiles_switch_to_folder(folder_id);
  switchToTab(0);
}

static String make_unlock_title(const char* format, const String& tile_title) {
  String result = format ? String(format) : String("%s");
  result.replace("%s", tile_title);
  return result;
}

static constexpr int kSettingsGestureCaptureWidth = popup_layout::scale(56);
static constexpr int kSettingsGestureThreshold = popup_layout::scale(48);

void UIManager::requestSettingsAccess(const String& title,
                                      const String& icon_name,
                                      uint32_t bg_color) {
  const DeviceConfig& config = configManager.getConfig();
  if (!config.settings_pin_enabled) {
    switchToTab(3);
    return;
  }

  pending_access_kind = PendingAccessKind::Settings;
  pending_access_folder_id = 0;
  const auto& tr = i18n::strings(config.language);
  const String source_title =
      title.length() ? title : String(tr.tile_type_settings);
  PinPopupInit init;
  init.title = make_unlock_title(tr.pin_popup_unlock_format, source_title);
  init.icon_name = icon_name.length() ? icon_name : String("cog");
  init.bg_color = bg_color;
  init.verify = verify_pending_access;
  init.success = complete_pending_access;
  init.context = this;
  show_pin_popup(init);
}

void UIManager::requestFolderAccess(uint16_t folder_id, const String& title,
                                    const String& icon_name,
                                    uint32_t bg_color) {
  if (!tileConfig.isFolderPinEnabled(folder_id)) {
    switchToFolder(folder_id);
    return;
  }

  pending_access_kind = PendingAccessKind::Folder;
  pending_access_folder_id = folder_id;
  const auto& tr = i18n::strings(configManager.getConfig().language);
  const String source_title =
      title.length() ? title : String(tr.tile_type_folder);
  PinPopupInit init;
  init.title = make_unlock_title(tr.pin_popup_unlock_format, source_title);
  init.icon_name = icon_name.length() ? icon_name : String("folder");
  init.bg_color = bg_color;
  init.hide_on_success = false;
  init.verify = verify_pending_access;
  init.success = complete_pending_access;
  init.context = this;
  show_pin_popup(init);
}

void UIManager::lockProtectedAccess() {
  if (pending_access_kind == PendingAccessKind::Folder) {
    tiles_cancel_folder_switch(pending_access_folder_id);
  }
  hide_pin_popup();
  pending_access_kind = PendingAccessKind::None;
  pending_access_folder_id = 0;
  access_gesture_eligible = false;
  detachSettingsGesturePressedObject();

  const DeviceConfig& config = configManager.getConfig();
  const bool settings_open =
      config.settings_pin_enabled && active_tab_index == 3;

  bool protected_folder_open = false;
  uint16_t folder_id = tileConfig.getActiveFolderId();
  for (size_t depth = 0;
       folder_id != TileConfig::rootFolderId() && depth < 128; ++depth) {
    if (tileConfig.isFolderPinEnabled(folder_id)) {
      protected_folder_open = true;
      break;
    }
    const uint16_t parent = tileConfig.getFolderParent(folder_id);
    if (parent == folder_id) break;
    folder_id = parent;
  }

  if (protected_folder_open) {
    tiles_switch_to_folder(TileConfig::rootFolderId());
  }
  if (settings_open || protected_folder_open) {
    switchToTab(0);
  }
}

bool UIManager::verify_pending_access(const char* pin, void* context) {
  UIManager* self = static_cast<UIManager*>(context);
  if (!self) return false;
  if (self->pending_access_kind == PendingAccessKind::Settings) {
    return configManager.verifySettingsPin(pin);
  }
  if (self->pending_access_kind == PendingAccessKind::Folder) {
    return tileConfig.verifyFolderPin(self->pending_access_folder_id, pin);
  }
  return false;
}

void UIManager::complete_pending_access(void* context) {
  UIManager* self = static_cast<UIManager*>(context);
  if (!self) return;
  const PendingAccessKind kind = self->pending_access_kind;
  const uint16_t folder_id = self->pending_access_folder_id;
  if (kind == PendingAccessKind::Settings) {
    self->pending_access_kind = PendingAccessKind::None;
    self->pending_access_folder_id = 0;
    self->switchToTab(3);
  } else if (kind == PendingAccessKind::Folder) {
    // Keep the access request alive until the asynchronous cache switch has
    // committed. The PIN popup continues to cover the previous folder.
    self->switchToFolder(folder_id);
  } else {
    self->pending_access_kind = PendingAccessKind::None;
    self->pending_access_folder_id = 0;
  }
}

void UIManager::finishFolderSwitch(uint16_t folder_id, bool success) {
  if (pending_access_kind != PendingAccessKind::Folder ||
      pending_access_folder_id != folder_id) {
    return;
  }
  if (!success) {
    resume_pin_popup_after_failed_success();
    return;
  }
  pending_access_kind = PendingAccessKind::None;
  pending_access_folder_id = 0;
  hide_pin_popup();
}

void UIManager::createSettingsGestureZone() {
  if (lv_indev_t* input = displayManager.getInput()) {
    lv_indev_remove_event_cb_with_user_data(
        input, settings_gesture_indev_event_cb, this);
    lv_indev_add_event_cb(input, settings_gesture_indev_event_cb,
                          LV_EVENT_PRESSED, this);
    lv_indev_add_event_cb(input, settings_gesture_indev_event_cb,
                          LV_EVENT_RELEASED, this);
  }
  syncSettingsGestureZone();
}

void UIManager::refreshSettingsGestureZone() {
  syncSettingsGestureZone();
}

void UIManager::setSettingsGestureStyle(const String& title,
                                        const String& icon_name,
                                        uint32_t bg_color) {
  settings_gesture_title = title;
  settings_gesture_icon_name = icon_name;
  settings_gesture_bg_color = bg_color & TILE_BG_COLOR_RGB_MASK;
}

void UIManager::syncSettingsGestureZone() {
  const DeviceConfig& config = configManager.getConfig();
  const bool enabled = config.settings_swipe_enabled;
  const uint8_t edge = config.settings_reveal_edge <=
                               static_cast<uint8_t>(SettingsRevealEdge::Bottom)
                           ? config.settings_reveal_edge
                           : static_cast<uint8_t>(SettingsRevealEdge::Left);

  if (!enabled || settings_gesture_edge != edge) {
    access_gesture_eligible = false;
    detachSettingsGesturePressedObject();
  }
  settings_gesture_enabled = enabled;
  settings_gesture_edge = edge;
}

void UIManager::detachSettingsGesturePressedObject() {
  if (!access_gesture_pressed_object) return;
  lv_obj_remove_event_cb_with_user_data(
      access_gesture_pressed_object,
      settings_gesture_pressed_object_event_cb, this);
  access_gesture_pressed_object = nullptr;
}

void UIManager::processSettingsGestureMotion(lv_indev_t* input,
                                             bool released) {
  if (!input || !access_gesture_eligible || access_gesture_triggered) {
    if (released) {
      access_gesture_eligible = false;
      detachSettingsGesturePressedObject();
    }
    return;
  }

  const DeviceConfig& config = configManager.getConfig();
  if (active_tab_index != 0 || !settings_gesture_enabled ||
      !config.settings_swipe_enabled) {
    access_gesture_eligible = false;
    detachSettingsGesturePressedObject();
    return;
  }

  lv_point_t current{};
  lv_indev_get_point(input, &current);
  const int32_t dx = current.x - access_gesture_start.x;
  const int32_t dy = current.y - access_gesture_start.y;
  const int32_t abs_dx = dx < 0 ? -dx : dx;
  const int32_t abs_dy = dy < 0 ? -dy : dy;
  const int32_t threshold = kSettingsGestureThreshold;

  bool matches = false;
  switch (static_cast<SettingsRevealEdge>(config.settings_reveal_edge)) {
    case SettingsRevealEdge::Left:
      matches = dx >= threshold && abs_dx > abs_dy;
      break;
    case SettingsRevealEdge::Right:
      matches = -dx >= threshold && abs_dx > abs_dy;
      break;
    case SettingsRevealEdge::Top:
      matches = dy >= threshold && abs_dy > abs_dx;
      break;
    case SettingsRevealEdge::Bottom:
      matches = -dy >= threshold && abs_dy > abs_dx;
      break;
  }
  if (!matches) {
    if (released) {
      access_gesture_eligible = false;
      detachSettingsGesturePressedObject();
    }
    return;
  }

  access_gesture_triggered = true;
  access_gesture_eligible = false;
  detachSettingsGesturePressedObject();

  // Arm suppression before resetting an active contact so the confirmed
  // swipe cannot turn into a click on the newly opened popup.
  if (!released) lv_indev_wait_release(input);
  lv_indev_reset(input, nullptr);

  const SettingsTileSnapshot& snapshot = config.settings_tile_snapshot;
  const uint32_t snapshot_color = snapshot.bg_color;
  const bool use_snapshot = config.settings_tile_hidden && snapshot.valid;
  const uint32_t bg_color =
      use_snapshot
          ? (snapshot_color != 0
                 ? (snapshot_color & TILE_BG_COLOR_RGB_MASK)
                 : 0x2A2A2A)
          : settings_gesture_bg_color;
  const String title = use_snapshot ? String(snapshot.title)
                                    : settings_gesture_title;
  const String icon_name = use_snapshot ? String(snapshot.icon_name)
                                        : settings_gesture_icon_name;
  requestSettingsAccess(title, icon_name, bg_color);
}

void UIManager::settings_gesture_pressed_object_event_cb(lv_event_t* event) {
  UIManager* self = static_cast<UIManager*>(lv_event_get_user_data(event));
  if (!self) return;

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_DELETE) {
    if (lv_event_get_current_target(event) ==
        self->access_gesture_pressed_object) {
      self->access_gesture_pressed_object = nullptr;
      self->access_gesture_eligible = false;
    }
    return;
  }
  if (code != LV_EVENT_PRESSING) return;

  lv_indev_t* input = static_cast<lv_indev_t*>(lv_event_get_param(event));
  self->processSettingsGestureMotion(input, false);
}

void UIManager::settings_gesture_indev_event_cb(lv_event_t* event) {
  UIManager* self = static_cast<UIManager*>(lv_event_get_user_data(event));
  if (!self) return;
  lv_indev_t* input = static_cast<lv_indev_t*>(
      lv_event_get_current_target(event));
  if (!input) return;
  const lv_event_code_t code = lv_event_get_code(event);

  const DeviceConfig& config = configManager.getConfig();
  if (code == LV_EVENT_PRESSED) {
    self->detachSettingsGesturePressedObject();
    self->access_gesture_eligible = false;
    self->access_gesture_triggered = false;
    if (self->active_tab_index != 0 ||
        !self->settings_gesture_enabled ||
        !config.settings_swipe_enabled) {
      return;
    }

    lv_obj_t* pressed_object =
        static_cast<lv_obj_t*>(lv_event_get_param(event));
    bool started_on_home = false;
    for (lv_obj_t* object = pressed_object; object;
         object = lv_obj_get_parent(object)) {
      if (object == self->tab_panels[0]) {
        started_on_home = true;
        break;
      }
    }
    if (!started_on_home) return;

    lv_point_t point{};
    lv_indev_get_point(input, &point);
    const int32_t edge_zone = kSettingsGestureCaptureWidth;
    bool starts_at_edge = false;
    switch (static_cast<SettingsRevealEdge>(config.settings_reveal_edge)) {
      case SettingsRevealEdge::Left:
        starts_at_edge = point.x < edge_zone;
        break;
      case SettingsRevealEdge::Right:
        starts_at_edge = point.x >= SCREEN_WIDTH - edge_zone;
        break;
      case SettingsRevealEdge::Top:
        starts_at_edge = point.y < edge_zone;
        break;
      case SettingsRevealEdge::Bottom:
        starts_at_edge = point.y >= SCREEN_HEIGHT - edge_zone;
        break;
    }
    if (!starts_at_edge) return;
    self->access_gesture_start = point;
    self->access_gesture_eligible = true;
    self->access_gesture_pressed_object = pressed_object;
    lv_obj_add_event_cb(pressed_object,
                        settings_gesture_pressed_object_event_cb,
                        LV_EVENT_PRESSING, self);
    lv_obj_add_event_cb(pressed_object,
                        settings_gesture_pressed_object_event_cb,
                        LV_EVENT_DELETE, self);
    return;
  }
  if (code == LV_EVENT_RELEASED) {
    self->processSettingsGestureMotion(input, true);
  }
}

void UIManager::nav_button_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED) return;
  if (!e) return;

  UIManager *self = static_cast<UIManager*>(lv_event_get_user_data(e));
  if (!self) return;

  lv_obj_t *target = static_cast<lv_obj_t*>(lv_event_get_target(e));

  // Usually only four tabs, but stop immediately after finding the match.
  for (uint8_t i = 0; i < TAB_COUNT; ++i) {
    if (self->tab_buttons[i] == target) {
      self->switchToTab(i);
      return;  // Return immediately instead of breaking before returning.
    }
  }
}

// Update the status bar.
void UIManager::updateStatusbar() {
  if (!status_time_label || !status_date_label) return;

  char buf[48];

  lv_obj_set_style_text_font(status_time_label, get_status_time_font(), 0);
  lv_obj_set_style_text_font(status_date_label, get_status_date_font(), 0);

  static uint32_t last_rtc_sync_ms = 0;

  bool have_time = false;

  int hour = 0, minute = 0, day = 0, month = 0, year = 0;

  auto is_valid_datetime = [](int y, int m, int d, int h, int min) {
    return (y >= 2023 && y <= 2099) &&
           (m >= 1 && m <= 12) &&
           (d >= 1 && d <= 31) &&
           (h >= 0 && h < 24) &&
           (min >= 0 && min < 60);
  };

  struct tm timeinfo;

  if (getLocalTime(&timeinfo, 0)) {
    hour = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
    day = timeinfo.tm_mday;
    month = timeinfo.tm_mon + 1;
    year = timeinfo.tm_year + 1900;
    have_time = is_valid_datetime(year, month, day, hour, minute);
  }

  // Waveshare P4 has no RTC — time comes from NTP only.
  (void)last_rtc_sync_ms;



  if (have_time) {

    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);

  } else {

    // Without a valid timestamp, use a neutral digit-only fallback to avoid missing glyphs.
    snprintf(buf, sizeof(buf), "");

  }

  lv_label_set_text(status_time_label, buf);



  if (have_time) {

    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", day, month, year);

  } else {

    // Fallback without missing glyphs
    snprintf(buf, sizeof(buf), "");

  }

  lv_label_set_text(status_date_label, buf);



  // Trigger NTP whenever any shared network transport is connected.

  if (!have_time && networkTransport.isConnected() && !tz_configured) {

    scheduleNtpSync(0);

  }

}



// ========== NTP-Sync ==========

void UIManager::scheduleNtpSync(uint32_t delay_ms) {

  next_ntp_sync_ms = millis() + delay_ms;
  tz_configured = false;

}



void UIManager::serviceNtpSync() {

  if (!networkTransport.isConnected() || tz_configured) return;



  uint32_t now_ms = millis();

  if ((int32_t)(now_ms - next_ntp_sync_ms) < 0) return;



  const DeviceConfig& cfg = configManager.getConfig();
  const uint32_t started_ms = millis();
  Serial.println("[NTP] Configuration starting");
  configTzTime(timezone_spec_for_code(cfg.timezone), "pool.ntp.org", "time.nist.gov", "time.cloudflare.com");

  tz_configured = true;

  Serial.printf("[NTP] Configuration complete (%u ms)\n",
                static_cast<unsigned>(millis() - started_ms));

}

// Live tab-button update
void UIManager::refreshTabButton(uint8_t tab_index) {
  (void)tab_index;
  // Device UI tabs are disabled; navigation uses folder tiles.
}







#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <lvgl.h>
#include <Arduino.h>

// Forward Declarations
typedef void (*scene_publish_cb_t)(const char* scene_name);
typedef void (*hotspot_start_cb_t)(bool enable);

// UI Manager: owns the device interface.
class UIManager {
public:
  // Build the UI.
  void buildUI(scene_publish_cb_t scene_cb, hotspot_start_cb_t hotspot_cb = nullptr);

  // Statusbar
  void updateStatusbar();

  // NTP Sync
  void scheduleNtpSync(uint32_t delay_ms = 0);
  void serviceNtpSync();

  // Live tab-button update
  void refreshTabButton(uint8_t tab_index);

  // Switch tabs; public for navigation tiles.
  uint8_t activeTab() const { return active_tab_index; }
  void switchToTab(uint8_t index);
  void switchToFolder(uint16_t folder_id);
  void requestSettingsAccess(const String& title = String(),
                             const String& icon_name = String(),
                             uint32_t bg_color = 0x2A2A2A);
  void requestFolderAccess(uint16_t folder_id, const String& title,
                           const String& icon_name,
                           uint32_t bg_color = 0x2A2A2A,
                           uint32_t icon_color = 0xFFFFFF);
  void lockProtectedAccess();
  void finishFolderSwitch(uint16_t folder_id, bool success);
  void refreshSettingsGestureZone();
  void setSettingsGestureStyle(const String& title, const String& icon_name,
                               uint32_t bg_color);

private:
  static constexpr uint8_t TAB_COUNT = 4;

  // Status-bar elements
  lv_obj_t *status_container = nullptr;
  lv_obj_t *status_time_label = nullptr;
  lv_obj_t *status_date_label = nullptr;

  // Tab content and navigation
  lv_obj_t *tab_panels[TAB_COUNT] = {nullptr};
  lv_obj_t *tab_buttons[TAB_COUNT] = {nullptr};
  lv_obj_t *tab_button_overlays[TAB_COUNT] = {nullptr};  // Active-state overlays
  lv_obj_t *tab_labels[TAB_COUNT] = {nullptr};
  lv_obj_t *tab_content_container = nullptr;
  lv_obj_t *nav_container = nullptr;
  uint8_t active_tab_index = UINT8_MAX;
  lv_point_t access_gesture_start = {0, 0};
  lv_obj_t *access_gesture_pressed_object = nullptr;
  bool access_gesture_eligible = false;
  bool access_gesture_triggered = false;
  bool settings_gesture_enabled = false;
  uint8_t settings_gesture_edge = UINT8_MAX;
  String settings_gesture_title;
  String settings_gesture_icon_name;
  uint32_t settings_gesture_bg_color = 0x2A2A2A;

  enum class PendingAccessKind : uint8_t {
    None,
    Settings,
    Folder,
  };
  PendingAccessKind pending_access_kind = PendingAccessKind::None;
  uint16_t pending_access_folder_id = 0;
  // NTP-Sync
  uint32_t next_ntp_sync_ms = 0;
  bool tz_configured = false;
  static const char* TZ_EUROPE_BERLIN;

  // Internal functions
  void statusbarInit(lv_obj_t *tab_bar);
  lv_obj_t* setupTabButton(lv_obj_t *btn, uint8_t tab_index, const char *icon_name, const char *tab_name);
  lv_obj_t* createTabPanel(lv_obj_t *parent);
  static void nav_button_event_cb(lv_event_t *e);
  void createSettingsGestureZone();
  void syncSettingsGestureZone();
  void detachSettingsGesturePressedObject();
  void processSettingsGestureMotion(lv_indev_t *input, bool released);
  static void settings_gesture_indev_event_cb(lv_event_t *e);
  static void settings_gesture_pressed_object_event_cb(lv_event_t *e);
  static bool verify_pending_access(const char* pin, void* context);
  static void complete_pending_access(void* context);
};

// Global instance
extern UIManager uiManager;

#endif // UI_MANAGER_H

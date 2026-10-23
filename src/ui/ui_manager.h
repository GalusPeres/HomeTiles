#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <lvgl.h>
#include <Arduino.h>
#include <atomic>
#include "src/ui/shared/ui_theme.h"

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
  void requestSettingsAccess();
  void requestFolderAccess(uint16_t folder_id, const String& title,
                           const String& icon_name,
                           uint32_t bg_color = ui_theme::popup_card(),
                           uint32_t icon_color = 0xFFFFFF);
  void lockProtectedAccess();
  void finishFolderSwitch(uint16_t folder_id, bool success);
  // Home (0) or one of its pages (1.., tile_layouts::pages): the swipes and
  // the page dots' preview open them.
  void showPage(int number);
  void refreshSettingsGestureZone();
  // The touch under way turns no page and opens no edge action (the head's
  // dots opened the pages' overview under the finger).
  void cancelTouchGesture();

  // The theme (dark/light) applies while running, like the global tile
  // color: a save only requests it (any task), the loop applies the stored
  // theme once the camera and popup openings allow it.
  void requestThemeChange() { theme_change_pending.store(true); }
  void processThemeChange();

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
  // The edge an armed swipe started at and its action (swipe_gestures.h).
  uint8_t access_gesture_edge = UINT8_MAX;
  uint8_t access_gesture_action = 0;
  // The tab the swipe started on: the tiles (0) or Settings (kSettingsTab).
  uint8_t access_gesture_tab = UINT8_MAX;
  // The swipe started where a sideways move turns the page (the head
  // beside its buttons, a tile or the grid, a side edge, the overview).
  bool access_page_swipe = false;
  // A touch on Home that may become a swipe, and whether it moved further
  // than a tap (touch_rules.h): then its tile gets no click or long press.
  bool touch_watched = false;
  bool touch_moved = false;
  // The ways a swipe can start from it (touch_rules::far_from_tap).
  bool touch_sideways = false;
  bool touch_upright = false;
  // Readings beyond a tap's distance in a row, and how far it went (log).
  uint8_t touch_far_readings = 0;
  int16_t touch_moved_dx = 0;
  int16_t touch_moved_dy = 0;
  // The touched object's press shows once the finger rested (touch_rules.h):
  // the object, when the finger came down, whether its press shows, and
  // whether a quick tap got it only at the lift (it then shows a moment).
  lv_obj_t* touch_press_obj = nullptr;
  uint32_t touch_press_ms = 0;
  bool touch_press_shown = false;
  bool touch_press_late = false;

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

  std::atomic<bool> theme_change_pending{false};
  // The light theme's black screen corners (applyScreenCorners).
  lv_obj_t* screen_corners[4] = {};

  // Internal functions
  void preloadPopups();
  void applyScreenCorners();
  void statusbarInit(lv_obj_t *tab_bar);
  lv_obj_t* setupTabButton(lv_obj_t *btn, uint8_t tab_index, const char *icon_name, const char *tab_name);
  lv_obj_t* createTabPanel(lv_obj_t *parent);
  static void nav_button_event_cb(lv_event_t *e);
  void createSettingsGestureZone();
  void syncSettingsGestureZone();
  void detachSettingsGesturePressedObject();
  void processSettingsGestureMotion(lv_indev_t *input, bool released);
  void processPageSwipeMotion(lv_indev_t *input, bool released);
  void processScreensaverSwipeMotion(lv_indev_t *input, bool released);
  void noteTouchMotion(lv_indev_t *input);
  void updateTouchPress(lv_indev_t *input);
  void runSwipeAction(uint8_t action);
  void turnPage(int step);
  void openPage(int number);
  static void settings_gesture_indev_event_cb(lv_event_t *e);
  static void settings_gesture_pressed_object_event_cb(lv_event_t *e);
  static bool verify_pending_access(const char* pin, void* context);
  static void complete_pending_access(void* context);
};

// Global instance
extern UIManager uiManager;

#endif // UI_MANAGER_H

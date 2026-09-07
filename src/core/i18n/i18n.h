#ifndef I18N_H
#define I18N_H

#include <Arduino.h>

namespace i18n {

struct Strings {
  const char* code;
  const char* html_lang;
  const char* language_label;
  const char* timezone_label;
  const char* time_format_label;
  const char* date_format_label;
  const char* format_auto_language;
  const char* format_auto_localization;
  const char* format_24_hour;
  const char* format_12_hour;

  const char* home;
  const char* folder_prefix;

  const char* admin_window_title;
  const char* admin_panel_title;
  const char* admin_subtitle;
  const char* admin_tile_hint;
  const char* admin_delete_folder_tab;
  const char* admin_tile_settings;
  const char* admin_type;
  const char* admin_folder_type_locked;
  const char* admin_title;
  const char* admin_tile_title_placeholder;
  const char* admin_icon_label;
  const char* admin_icon_placeholder;
  const char* admin_icon_list;
  const char* admin_color;
  const char* admin_column;
  const char* admin_row;
  const char* admin_width_cells;
  const char* admin_height_cells;
  const char* admin_autosave;
  const char* admin_copy;
  const char* admin_paste;
  const char* admin_delete;
  const char* admin_import_export;
  const char* admin_export;
  const char* admin_import;
  const char* admin_import_overwrite;
  const char* admin_settings_wifi;
  const char* admin_settings_mqtt;
  const char* admin_settings_language;
  const char* admin_settings_screenshot;
  const char* admin_settings_ota;
  const char* screenshot_create_download;
  const char* screenshot_saved_note;
  const char* ota_firmware_file;
  const char* ota_current_version;
  const char* ota_upload_install;
  const char* ota_update_note;
  const char* ota_choose_file;
  const char* ota_no_file_selected;

  const char* wifi_status;
  const char* wifi_connected;
  const char* wifi_disconnected;
  const char* wifi_offline;
  const char* wifi_ap_active;
  const char* wifi_label;
  const char* ssid_label;
  const char* ip_label;
  const char* wifi_password_label;
  const char* wifi_static_ip_label;
  const char* wifi_gateway_label;
  const char* wifi_subnet_label;
  const char* wifi_dns_label;
  const char* wifi_dhcp_hint;
  const char* mqtt_not_configured;
  const char* ap_enable;
  const char* ap_disable;
  const char* yes;
  const char* no;

  const char* mqtt_host;
  const char* mqtt_port;
  const char* mqtt_username;
  const char* mqtt_password;
  const char* mqtt_client_id;
  const char* mqtt_client_id_placeholder;
  const char* mqtt_client_id_hint;
  const char* mqtt_base_topic;
  const char* ha_prefix;
  const char* status_time_font;
  const char* status_date_font;
  const char* save;
  const char* restart_confirm;
  const char* restart_button;
  const char* mqtt_saved_title;
  const char* mqtt_saved_message;
  const char* bridge_saved_title;
  const char* bridge_saved_message;
  const char* save_failed;

  const char* ap_window_title;
  const char* ap_heading;
  const char* ap_subtitle;
  const char* ap_wifi_section;
  const char* ap_wifi_ssid_label;
  const char* ap_wifi_ssid_placeholder;
  const char* ap_wifi_password_label;
  const char* ap_wifi_password_placeholder;
  const char* ap_wifi_open_hint;
  const char* ap_info_notice;
  const char* ap_info_message;
  const char* ap_save_connect;
  const char* ap_success_title;
  const char* ap_success_message;
  const char* ap_success_notice;
  const char* ap_wifi_required;

  const char* display_label;
  const char* brightness_label;
  const char* screensaver_brightness_label;
  const char* hue_label;
  const char* saturation_label;
  const char* sleep_label;
  const char* sleep_after;
  const char* sleep_never;
  const char* screensaver_label;
  const char* touch_label;
  const char* no_imu_hint;

  const char* tile_type_empty;
  const char* tile_type_sensor;
  const char* tile_type_energy;
  const char* tile_type_weather;
  const char* tile_type_scene;
  const char* tile_type_folder;
  const char* tile_type_switch;
  const char* tile_type_media;
  const char* tile_type_clock;
  const char* tile_type_text;
  const char* tile_type_settings;
  const char* tile_type_back;

  const char* no_selection;
  const char* sensor_entity;
  const char* sensor_unit;
  const char* sensor_decimals;
  const char* sensor_value_size;
  const char* sensor_display_mode;
  const char* sensor_display_none;
  const char* sensor_display_gauge;
  const char* sensor_display_graph;
  const char* sensor_gauge_min;
  const char* sensor_gauge_max;
  const char* sensor_arc_degree;
  const char* sensor_gauge_size;
  const char* sensor_y_offset;
  const char* sensor_graph_height;
  const char* popup_open;
  const char* short_press;
  const char* long_press;
  const char* sensor_value_y_offset;
  const char* weather_entity;
  const char* energy_entity;
  const char* switch_light;
  const char* switch_display;
  const char* switch_icon_button;
  const char* switch_lvgl_switch;
  const char* media_entity;
  const char* show_time;
  const char* show_date;
  const char* time_font_size;
  const char* date_font_size;
  const char* text_label;
  const char* text_placeholder;
  const char* text_size;
  const char* text_max_chars;
  const char* target_folder;
  const char* new_folder;
  const char* scene_label;

  const char* js_select_tile_first;
  const char* js_tile_copied;
  const char* js_no_copied_tile;
  const char* js_tile_pasted;
  const char* js_settings_tile_fixed;
  const char* js_back_tile_fixed;
  const char* js_tile_cannot_delete;
  const char* js_folder_cannot_delete;
  const char* js_delete_folder_confirm;
  const char* js_folder_deleted;
  const char* js_delete_failed;
  const char* js_folder_not_found;
  const char* js_tile_saved;
  const char* js_unknown_error;
  const char* js_network_error;
  const char* js_network_error_save;
  const char* js_export_created;
  const char* js_export_failed;
  const char* js_import_invalid_json;
  const char* js_import_failed;
  const char* js_import_running;
  const char* js_import_complete;
  const char* js_tile_does_not_fit;
  const char* js_no_layout_found;
  const char* js_tiles_moved_saved;
  const char* js_move_failed;
  const char* js_move_up;
  const char* js_move_down;
  const char* js_network_error_move;
  const char* js_screenshot_creating;
  const char* js_screenshot_saved;
  const char* js_screenshot_failed;
  const char* js_ota_select_file;
  const char* js_ota_uploading;
  const char* js_ota_installing;
  const char* js_ota_reconnecting;
  const char* js_ota_success;
  const char* js_ota_failed;

  // On-device Wi-Fi selection (Settings popup, see tab_settings.cpp).
  const char* wifi_scan_searching;
  const char* wifi_scan_none;
  const char* wifi_scan_retry;
  const char* wifi_manual_entry;
  const char* wifi_open_network;
  const char* wifi_password_for_fmt;
  const char* wifi_back_btn;
  const char* wifi_connect_btn;
  const char* wifi_saved_restarting;
  const char* wifi_save_failed;

  // Keyboard layout localization. The option names "Deutsch (QWERTZ)" and
  // "English (QWERTY)" identify the layouts and remain untranslated.
  const char* keyboard_layout_label;

  // Settings tiles: short descriptions of the controls behind each tile.
  const char* settings_tile_desc_display;
  const char* settings_tile_desc_wifi;
  const char* settings_tile_desc_locale;
  const char* settings_tile_desc_firmware_fmt;  // %s = firmware version

  // Display popup: rotation button label.
  const char* display_rotate_btn_text;

  // System popup (formerly Firmware): device row, GitHub update check and OTA.
  const char* system_device_label;
  const char* system_check_updates_btn;
  const char* system_checking;
  const char* system_up_to_date;
  const char* system_update_available_fmt;  // %s = new version (release tag)
  const char* system_update_restart_note;
  const char* system_install_btn_fmt;       // %s = new version (release tag)
  const char* system_check_failed;
  const char* system_downloading;
  const char* system_install_failed;
  const char* system_installed_restarting;

  // Wi-Fi popup disconnect button; System popup HA pairing button and status.
  const char* wifi_disconnect_btn;
  const char* system_pair_btn;
  const char* system_pair_status;

  // Dedicated screensaver editor in Web Admin.
  const char* screensaver_use_wallpapers;
  const char* screensaver_shuffle;
  const char* screensaver_wallpapers_heading;
  const char* screensaver_duration_seconds;
  const char* screensaver_zoom;
  const char* screensaver_focus_x;
  const char* screensaver_focus_y;
  const char* screensaver_clock_heading;
  const char* screensaver_show_weekday;
  const char* screensaver_clock_shadow;
  const char* screensaver_time_alignment;
  const char* screensaver_date_alignment;
  const char* alignment_left;
  const char* alignment_center;
  const char* alignment_right;
  const char* screensaver_tile_shadow;
  const char* screensaver_tile_border;
  const char* screensaver_background_opacity;
  const char* screensaver_hint;
  const char* screensaver_storage_hint;
  const char* screensaver_no_wallpapers;
  const char* js_screensaver_saved;
  const char* js_screensaver_save_failed;
  const char* js_screensaver_load_failed;

  // Network mode switch in the Wi-Fi popup and Web Admin. Select fixed
  // Wi-Fi or Ethernet mode; a change takes effect after restarting.
  const char* net_mode_to_ethernet;
  const char* net_mode_to_wifi;
  const char* net_mode_restart_note;
  const char* admin_ethernet_mode;

  // Static network addressing on the device and in Web Admin.
  const char* ethernet_dhcp_reset;
  const char* ethernet_static_restore;
  const char* ethernet_dhcp_selected;
  const char* ethernet_static_selected;
  const char* admin_ip_configuration;
  const char* admin_ip_dhcp;
  const char* admin_ip_static;
  const char* admin_ip_dhcp_note;
  const char* admin_ip_static_note;
  const char* admin_ip_invalid;

  // Web Admin: heading ("<Device> Admin Panel") and slideshow editor.
  const char* admin_panel_word;
  const char* admin_slideshow;

  // Web Admin: network section on Ethernet-capable devices.
  const char* admin_network_section;
  const char* admin_connection_type;
  const char* admin_connection_type_note;
  const char* admin_ip_use_static;

  // Web Admin: Wi-Fi and MQTT password visibility toggles.
  const char* password_show;
  const char* password_hide;

  // Web Admin: crash log download and core dump management.
  const char* crash_log_download;
  const char* sd_diagnostics_open;
  const char* coredump_stored;
  const char* coredump_download;
  const char* coredump_delete;
  const char* coredump_decode_note;

  // Web Admin: microSD file manager.
  const char* file_manager_title;
  const char* file_manager_checking;
  const char* file_manager_refresh;
  const char* file_manager_new_folder;
  const char* file_manager_choose_files;
  const char* file_manager_upload;
  const char* file_manager_no_selection;
  const char* file_manager_open;
  const char* file_manager_rename;
  const char* file_manager_name;
  const char* file_manager_modified;
  const char* file_manager_size;
  const char* file_manager_not_loaded;

  // System popup: status during a manual restart.
  const char* system_restarting;

  // Device popups: light switch state and loading placeholder.
  const char* light_on;
  const char* light_off;
  const char* loading;

  // Media tile/popup states when no track is playing. Wording follows
  // official HA translations, such as "Leerlauf"/"Idle" for idle.
  const char* media_state_playing;
  const char* media_state_paused;
  const char* media_state_idle;
  const char* media_state_standby;
  const char* media_state_off;
  const char* media_no_playback;

  // Camera tile, popup and video runtime status.
  const char* camera_tile_type;
  const char* camera_entity;
  const char* camera_ready;
  const char* camera_preparing;
  const char* camera_bridge_requesting;
  const char* camera_bridge_update_required;
  const char* camera_bridge_no_response;
  const char* camera_invalid_response;
  const char* camera_no_stream_url;
  const char* camera_connecting;
  const char* camera_unavailable;
  const char* camera_stream_stopped;
  const char* camera_empty_url;
  const char* camera_already_running;
  const char* camera_task_start_failed;
  const char* camera_frame_memory_failed;
  const char* camera_decoder_error;
  const char* camera_http_connecting;
  const char* camera_url_open_failed;
  const char* camera_decoder_start_failed;
  const char* camera_input_memory_failed;
  const char* camera_buffering;
  const char* camera_connection_ended;
  const char* camera_resolution_error_fmt;  // %u = decoded frame size
  const char* camera_http_error_fmt;        // %d = HTTP status
  const char* camera_device_only;
  const char* camera_unknown;
  const char* camera_no_source;
  const char* camera_ha_url_unavailable;
  const char* camera_setup_failed;
  const char* camera_input_buffer_full;
  const char* camera_fps_fmt;  // %.1f = measured FPS
  const char* camera_mqtt_disconnected;
  const char* camera_mqtt_topic_missing;
  const char* camera_mqtt_queue_full;

  // Web Admin: local I/O assignments. Even shared technical terms use the
  // central language schema; the UI has no language branches or hardcoded
  // display strings.
  const char* admin_io;
  const char* admin_io_temperature;
  const char* admin_io_name;
  const char* admin_io_gpio;
  const char* admin_io_no_free_gpio;
  const char* admin_io_output_logic;
  const char* admin_io_active_high;
  const char* admin_io_active_low;
  const char* admin_io_high;
  const char* admin_io_low;
  const char* admin_io_after_restart;
  const char* admin_io_precision;
  const char* admin_io_decimals_zero;
  const char* admin_io_decimal_one;
  const char* admin_io_decimals_two;
  const char* admin_io_decimals_three;
  const char* admin_io_remove_assignment;
  const char* admin_io_remove_confirm_fmt;
  const char* admin_io_empty;
  const char* admin_io_no_profile;
  const char* admin_io_unsaved_changes;
  const char* admin_io_no_compatible_gpio;
  const char* admin_io_name_required;
  const char* admin_io_saving;
  const char* admin_io_saved;
  const char* admin_io_load_failed;
  const char* admin_io_could_not_load;
  const char* admin_io_restart_unsaved_confirm;
  const char* admin_io_restarting;

  // Web Admin and device UI: local parental-control PIN protection.
  const char* admin_access_control;
  const char* settings_lock_enable;
  const char* settings_hide_tile;
  const char* settings_pin_label;
  const char* settings_pin_placeholder;
  const char* settings_pin_configured;
  const char* settings_pin_not_configured;
  const char* settings_swipe_enable;
  const char* settings_reveal_edge;
  const char* settings_swipe_note;
  const char* edge_left;
  const char* edge_right;
  const char* edge_top;
  const char* edge_bottom;
  const char* factory_pin_label;
  const char* factory_pin_note;
  const char* settings_home_full;
  const char* pin_invalid;
  const char* folder_pin_enable;
  const char* folder_pin_label;
  const char* folder_pin_placeholder;
  const char* folder_pin_apply;
  const char* folder_pin_saved;
  const char* folder_pin_save_failed;
  const char* folder_pin_create_first;
  const char* pin_popup_settings_title;
  const char* pin_popup_incorrect;
  const char* pin_popup_unlock_format;
  const char* settings_tile_parking;

};

// Locale-specific display rules and short runtime strings shared by
// LVGL and Web Admin. Register each language as its own profile so
// display code needs no "is_german" branches.
struct LocaleProfile {
  const char* code;
  const char* native_name;
  const char* decimal_separator;

  const char* weather_today;
  const char* weather_tomorrow;
  const char* weather_weekdays_short[7];
  const char* weather_months_short[12];
  const char* weather_conditions[15];

  const char* tile_type_climate;
  const char* climate_entity;
  const char* climate_target_temperature;
  const char* climate_target_humidity;
  const char* climate_heating_target;
  const char* climate_cooling_target;
  // heating, preheating, cooling, drying, fan, defrosting, idle, off,
  // heat, cool, heat_cool, auto, dry, fan_only, generic, unavailable,
  // unknown.
  const char* climate_states[17];
  const char* climate_value_labels[3];
  const char* climate_control_labels[5];
  const char* climate_option_labels[26];
  const char* climate_mini_labels[7];

  // Cover type, Web fields, actions, and runtime states. State order:
  // open, opening, closed, closing, unavailable, unknown.
  const char* cover_labels[7];
  const char* cover_states[6];

  // Display defaults for automatic locale formatting. Values match the
  // clock_tile::TimeFormat/DateFormat enums: time 1=24h, 2=12h;
  // date 1=DMY, 2=MDY, 3=YMD.
  uint8_t default_time_format;
  uint8_t default_date_format;

  // Suffix for whole hours on chart axes, such as "8 Uhr" or "8:00".
  const char* hour_axis_suffix;

  // Short date with a month name: {d}=day, {m}=abbreviated month
  // ("15. Jul." or "Jul 15"); see i18n::format_short_date.
  const char* short_date_pattern;

  // Full weekday names, indexed by tm_wday (0=Sunday).
  const char* weekday_names[7];

  // Timezone selection in Web Admin and device Settings. Entries follow
  // i18n::timezone_option(); TimezoneOptionInfo::group selects the group.
  const char* timezone_group_labels[6];
  const char* timezone_labels[27];

  // Binary sensor UI labels. Order: type, entity, history, activity,
  // history unavailable, no activity, 24-hour range, 7-day range.
  const char* binary_sensor_labels[8];

  // Canonical binary sensor states shared by the device and Web Admin.
  // Order: on, off, low, normal, charging, not charging, detected, clear,
  // cold, connected, disconnected, open, closed, hot, unlocked, locked,
  // wet, dry, moving, stopped, plugged in, unplugged, power, no power,
  // home, away, problem, OK, running, not running, unsafe, safe,
  // update available, up to date, unavailable, unknown.
  const char* binary_sensor_states[36];

  // Types, entity labels, apply, invalid value, pending, failed, input formats.
  const char* editable_labels[19];
};

// Locale-independent timezone catalog with codes and group assignments.
// LocaleProfile::timezone_labels supplies display names in the same order.
constexpr size_t kTimezoneOptionCount = 27;
constexpr size_t kTimezoneGroupCount = 6;
struct TimezoneOptionInfo {
  uint8_t group;  // Index in LocaleProfile::timezone_group_labels
  const char* code;
};
const TimezoneOptionInfo& timezone_option(size_t index);

// Registered languages for selection lists; order matches dropdown indices.
size_t language_count();
const char* language_code_at(size_t index);
const char* language_native_name_at(size_t index);
size_t language_index(const char* language_code);
String build_language_dropdown_options();  // Native language names separated by \n.

const Strings& strings(const char* language_code);
const LocaleProfile& locale(const char* language_code);
const char* normalize_language_code(const char* language_code);
String build_language_options_html(const char* selected_code);
String localize_numeric_text(const char* language_code, const String& numeric_text);
String format_short_date(const char* language_code, int day, const char* month_short);
String format_number(const char* language_code,
                     float value,
                     uint8_t decimals,
                     bool trim_trailing_zeros = false);
String weather_condition_label(const char* language_code, const String& condition);
String weather_weekday_short(const char* language_code, const String& iso);
const char* weather_month_short(const char* language_code, int month);
const char* weather_today_label(const char* language_code);
const char* weather_tomorrow_label(const char* language_code);
const char* climate_tile_type_label(const char* language_code);
const char* climate_entity_label(const char* language_code);
const char* climate_target_temperature_label(const char* language_code);
const char* climate_target_humidity_label(const char* language_code);
const char* climate_heating_target_label(const char* language_code);
const char* climate_cooling_target_label(const char* language_code);
const char* climate_target_heat_label(const char* language_code);
const char* climate_target_cool_label(const char* language_code);
const char* climate_state_label(const char* language_code,
                                const String& mode,
                                const String& action);
const char* entity_state_label(const char* language_code,
                               const String& state);
const char* climate_value_label(const char* language_code, uint8_t index);
const char* climate_control_label(const char* language_code, uint8_t index);
String climate_option_label(const char* language_code, const String& option);
const char* climate_mini_label(const char* language_code, uint8_t index);
// Cover UI labels: 0 type, 1 entity, 2 position, 3 tilt,
// 4 open, 5 stop, 6 close.
const char* cover_label(const char* language_code, uint8_t index);
const char* cover_state_label(const char* language_code,
                              const String& state);
// Binary sensor labels: 0 type, 1 entity, 2 history, 3 activity,
// 4 history unavailable, 5 no activity, 6 24-hour range, 7 7-day range.
const char* binary_sensor_label(const char* language_code, uint8_t index);
const char* binary_sensor_state_label(const char* language_code,
                                      const String& state,
                                      const String& device_class);

}  // namespace i18n

#endif

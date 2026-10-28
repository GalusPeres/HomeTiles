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

  const char* admin_panel_title;
  const char* admin_tile_hint;
  const char* admin_delete_folder_tab;
  const char* admin_type;
  const char* admin_title;
  const char* admin_tile_title_placeholder;
  const char* admin_icon_placeholder;
  const char* admin_color;
  const char* admin_copy;
  const char* admin_paste;
  const char* admin_delete;
  const char* admin_import_export;
  const char* admin_export;
  const char* admin_import;
  const char* admin_import_overwrite;
  const char* admin_settings_language;
  const char* admin_settings_screenshot;
  const char* screenshot_create_download;
  const char* screenshot_saved_note;
  const char* ota_firmware_file;
  const char* ota_upload_install;
  const char* ota_update_note;
  const char* ota_choose_file;
  const char* ota_no_file_selected;

  const char* wifi_connected;
  const char* wifi_disconnected;
  const char* wifi_label;
  const char* ssid_label;
  const char* wifi_password_label;
  const char* wifi_static_ip_label;
  const char* wifi_gateway_label;
  const char* wifi_subnet_label;
  const char* wifi_dns_label;
  const char* mqtt_not_configured;
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
  const char* save;
  const char* restart_confirm;
  const char* restart_button;
  const char* mqtt_saved_title;
  const char* mqtt_saved_message;
  const char* bridge_saved_title;
  const char* bridge_saved_message;
  const char* save_failed;

  const char* display_label;
  const char* sleep_never;

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
  const char* sensor_value_size_default;
  const char* sensor_display_mode;
  const char* sensor_display_none;
  const char* sensor_display_gauge;
  const char* sensor_display_graph;
  // The Sensor menu (types/sensor/web_html.cpp): layouts, forms, an open or
  // closed ring, the value's place and the range (display modes 3-14).
  const char* sensor_layout_value;
  const char* sensor_layout_bar;
  const char* sensor_layout_ring;
  const char* sensor_form;
  const char* sensor_form_level;
  const char* sensor_form_line;
  const char* sensor_form_stripes;
  const char* sensor_ring_open;
  const char* sensor_ring_closed;
  const char* sensor_value_top;
  const char* sensor_value_inside;
  const char* sensor_gauge_range;
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
  // Switch tile Layout field and its options (switch_layout::Layout):
  // icon button, switch, dimmer, automatic.
  const char* switch_display;
  const char* switch_icon_button;
  const char* switch_layout_switch;
  const char* switch_layout_dimmer;
  const char* switch_layout_automatic;
  // Climate tile Layout field: the title only (the title top right) or with
  // the value pair beside the disc (climateTileShowsValue).
  const char* climate_layout_title_only;
  const char* climate_layout_with_value;
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
  const char* scene_label;

  const char* js_select_tile_first;
  const char* js_tile_copied;
  const char* js_no_copied_tile;
  const char* js_tile_pasted;
  const char* js_settings_tile_fixed;
  const char* js_back_tile_fixed;
  const char* js_tile_cannot_delete;
  const char* js_paste_empty_only;
  const char* js_paste_no_space;
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
  // The import's progress line: {done} and {total} are filled in by the page.
  const char* js_import_progress;
  // Import checks: {tile} and {folder} are filled in by the page.
  const char* js_import_conflict;
  const char* js_import_stopped;
  const char* js_import_screensaver;
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

  // Wi-Fi selection texts (wifi_back_btn is Back in the first-start setup).
  const char* wifi_back_btn;

  // System: GitHub update check and OTA.
  const char* system_check_updates_btn;
  const char* system_checking;
  const char* system_up_to_date;
  const char* system_update_available_fmt;  // %s = new version (release tag)
  const char* system_install_btn_fmt;       // %s = new version (release tag)
  const char* system_check_failed;
  const char* system_downloading;
  const char* system_install_failed;
  const char* system_installed_restarting;

  // Wi-Fi disconnect button.
  const char* wifi_disconnect_btn;

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
  const char* tile_fractional_type_hint;
  const char* screensaver_tile_border;
  // The gesture menu in the Global tab (swipe_gestures.h).
  const char* swipe_gestures_heading;
  // The theme field (ui_theme.h) and its choices; a tile's own Design field
  // offers the same three.
  const char* theme_label;
  const char* theme_dark;
  const char* theme_light;
  const char* theme_auto;
  // Web Admin: the tabs on top of the settings panel (tile settings, the
  // global tile settings; on the screensaver page its picture settings).
  const char* panel_tab_tile;
  const char* panel_tab_global;
  const char* panel_tab_screensaver;
  // The layouts (grid_layout.h) and their "Layout ändern" window: the field
  // in the global settings, the three layouts, the window's buttons and
  // status ("**...**" is shown bold, {n} a count, {layout}/{tile} a name).
  const char* layout_label;
  const char* layout_change;
  const char* layout_classic;
  const char* layout_bar;
  const char* layout_portrait;
  const char* layout_active;
  const char* layout_copy_from;
  const char* layout_undo;
  const char* layout_saved;
  const char* layout_switch;
  const char* layout_switch_confirm;
  const char* layout_folders_missing;
  const char* layout_folder_missing;
  const char* layout_tiles_missing;
  const char* layout_tile_missing;
  // The Settings tile outside the layout's screen (it must be reached).
  const char* layout_settings_missing;
  const char* layout_all_fit;
  const char* layout_unsaved_confirm;
  const char* layout_storage;
  const char* layout_storage_hint;
  const char* layout_parked_tiles;
  const char* layout_parked_tile;
  // A layout's storage holds more tiles than it has entries ({n} of {max}).
  const char* layout_storage_full;
  // A free cell of a folder whose every entry is used ({n} of {max}).
  const char* folder_full;
  // Web Admin System page: the Icons card's title and its icon file row.
  const char* icons_section;
  const char* icon_file_label;
  const char* layout_delete_tile;
  const char* layout_delete_confirm;
  // Color of a tile without an icon (Clock, Text, Pixel animation): the
  // theme's card or an own color.
  const char* tile_color_mode_global;
  const char* tile_color_mode_custom;
  // Media tiles: "From cover", the color of the album cover as icon color.
  const char* tile_color_mode_from_cover;
  // Off/On choice buttons (the rules' switch in the tile editor).
  const char* icon_disc_on;
  const char* icon_disc_off;
  // Web Admin tile editor: per-tile icon color, color bar for numeric states
  // ("by value"), state colors for text states ("by state") and the On/Off
  // colors of Binary sensors (Sensor family, Binary sensor, Energy).
  const char* tile_icon_color;
  const char* tile_icon_color_by_value;
  const char* tile_icon_color_bar_off;
  const char* tile_icon_color_smooth;
  const char* tile_icon_color_steps;
  const char* tile_icon_color_min;
  const char* tile_icon_color_max;
  const char* tile_icon_color_preset_cold_warm;
  const char* tile_icon_color_preset_traffic;
  const char* tile_icon_color_preset_battery;
  const char* tile_icon_color_preset_humidity;
  const char* tile_icon_color_preset_single;
  const char* tile_icon_color_bar_hint;
  const char* tile_icon_color_by_state;
  const char* tile_icon_color_state;
  const char* tile_icon_color_contains;
  const char* tile_icon_color_add_state;
  const char* tile_icon_color_remove;
  const char* tile_icon_color_state_hint;
  const char* tile_icon_color_state_on;
  const char* tile_icon_color_state_off;
  // Icon-and-title tiles: the source entity of their icon color.
  const char* tile_icon_color_source_auto;
  const char* tile_icon_color_source_rules;
  const char* tile_icon_color_source_hint;
  // The rule layer of every tile: its source entity (the rules color the icon).
  const char* tile_rules;
  const char* tile_rules_own_entity;
  const char* tile_rules_other_entity;
  const char* tile_rules_priority_hint;
  // The icon picker's label beside the title, and the one Tile Settings
  // caption above the colors and the type's options (user 2026-10-11: one
  // section instead of Icon and Tile).
  const char* tile_group_icon;
  const char* tile_group_advanced;
  const char* screensaver_background_opacity;
  const char* screensaver_storage_hint;
  // The slideshow list without a card.
  const char* screensaver_no_sd;
  const char* screensaver_no_wallpapers;
  const char* js_screensaver_saved;
  const char* js_screensaver_save_failed;
  const char* js_screensaver_load_failed;

  // Static network addressing on the device and in Web Admin.
  const char* admin_ip_configuration;
  const char* admin_ip_dhcp_note;
  const char* admin_ip_static_note;

  // Web Admin: heading ("<Device> Admin Panel") and slideshow editor.
  const char* admin_panel_word;
  const char* admin_slideshow;

  // Web Admin: network section on Ethernet-capable devices.
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
  // View select (Home Assistant): a camera tile opened straight in full screen.
  const char* camera_view_full_screen;

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
  const char* settings_lock_enable;
  const char* settings_hide_tile;
  const char* settings_pin_label;
  const char* settings_pin_placeholder;
  const char* settings_pin_configured;
  const char* settings_pin_not_configured;
  // With a bar layout: the gear in Home's head opens Settings too.
  const char* settings_head_gear;
  // The settings panel's head bar tab (user 2026-10-13): its name, the
  // gear (the same setting as settings_head_gear), the time and the pages'
  // overview on a tap on the dots.
  const char* head_bar_heading;
  const char* head_bar_gear;
  const char* head_bar_clock;
  const char* head_bar_overview;
  // Home's own title in its head and the hint of its Auto switch ("Home" in
  // the panel's language).
  const char* head_bar_home_title;
  const char* head_bar_home_auto_hint;
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
  // PIN popup: the header state of a protected folder or Settings, and the
  // prompt shown below the lock until the first digit.
  const char* pin_popup_locked;
  const char* pin_popup_incorrect;
  const char* pin_popup_enter;
  const char* settings_tile_parking;
  // Built-in camera opt-in (Web Admin Settings, camera profiles only).
  const char* local_camera_section;
  const char* local_camera_enable;
  const char* local_camera_note;
  const char* local_camera_status_label;
  const char* local_camera_status_disabled;
  const char* local_camera_status_probing;
  const char* local_camera_status_ready;
  const char* local_camera_status_not_found;
  const char* local_camera_status_error;
  // Live-stream mode select; the numeric mode names stay untranslated.
  const char* local_camera_stream_mode;
  const char* local_camera_stream_mode_auto;
  // Mirrors snapshots and the live stream horizontally.
  const char* local_camera_mirror;
  // User image controls; the slider numbers stay untranslated.
  const char* local_camera_image_section;
  const char* local_camera_brightness;
  const char* local_camera_contrast;
  const char* local_camera_saturation;
  const char* local_camera_red;
  const char* local_camera_blue;
  const char* local_camera_image_reset;
  // On-display camera pill: title while capturing and the tap hint.
  const char* local_camera_indicator_active;
  const char* local_camera_indicator_end;
  // Web Admin: indicator style checkboxes and their note (the sub-block
  // heading, marked experimental, is local_camera_indicator_section).
  const char* local_camera_indicator_line;
  const char* local_camera_indicator_pill;
  const char* local_camera_indicator_note;
  // Custom stream mode: select entry and its two sliders (numbers untranslated).
  const char* local_camera_stream_mode_custom;
  const char* local_camera_custom_fps;
  const char* local_camera_custom_quality;
  // Heading of the indicator sub-block (marked experimental).
  const char* local_camera_indicator_section;
  // Heading of the live stream sub-block (mode, Custom values).
  const char* local_camera_stream_section;
  // Image control: upper limit of the sensor plus digital gain (percent).
  const char* local_camera_gain;
  // Collapsed Web Admin block with the fine-tuning controls (rotation,
  // mirror, red/blue swap, image controls).
  const char* local_camera_advanced;
  // Clockwise rotation select, one for every layout; the degree values stay
  // untranslated.
  const char* local_camera_rotation;
  // Red/blue swap checkbox and its note (applies from the next start).
  const char* local_camera_rb_swap;
  const char* local_camera_rb_swap_note;

  // Optional Web Admin password: login page, Web Admin settings section and
  // the Security view in the device System popup. %s is a number of seconds.
  const char* web_auth_section;
  const char* web_auth_new_password;
  const char* web_auth_repeat_password;
  const char* web_auth_set;
  const char* web_auth_remove;
  const char* web_auth_logout;
  const char* web_auth_too_short;
  const char* web_auth_mismatch;
  const char* web_auth_saved;
  const char* web_auth_removed;
  const char* web_auth_change_failed;
  const char* web_auth_remove_confirm;
  const char* web_auth_note;
  const char* web_auth_login_title;
  const char* web_auth_password_label;
  const char* web_auth_login_button;
  const char* web_auth_checking;
  const char* web_auth_wrong_password;
  const char* web_auth_wait_fmt;
  const char* web_auth_login_failed;
  const char* web_auth_forgot;
  const char* security_btn;
  const char* security_state_on;
  // Stored Wi-Fi/MQTT passwords and PINs are not sent to browsers while a
  // Web Admin password is set; they can only be replaced.
  const char* secret_hidden_placeholder;
  const char* web_auth_secrets_note;
  const char* ap_wifi_keep_password_hint;
  // Settings System page: Home Assistant connected or offline, the pairing
  // (the panel and Home Assistant show the same number) and the Web Admin
  // password.
  const char* security_value_connected;
  const char* security_value_offline;
  const char* security_password_question;
  const char* security_password_question_hint;
  const char* security_remove;
  const char* security_cancel;
  const char* security_confirm;
  const char* security_close;
  const char* security_unpaired_offline;
  const char* pairing_asking;
  const char* pairing_compare;
  const char* pairing_compare_hint;
  const char* pairing_waiting;
  const char* pairing_no_answer;
  const char* pairing_already_paired;
  const char* pairing_busy;
  const char* pairing_rejected;
  const char* pairing_failed;
  // Pair on a panel without a direct Bridge link: two minutes to add it.
  const char* pairing_discoverable;
  // Web Admin header badge: whether a Web Admin password protects the page.
  const char* web_auth_badge_on;
  const char* web_auth_badge_off;
  // Web Admin header: "<prefix> <stars> <or> Buy Me a Coffee"; both are links.
  const char* web_support_prefix;
  const char* web_support_stars;
  const char* web_support_or;
  // Weather tile setting: filled, colored weather icons (off: white outlines).
  const char* weather_colored_icons;
  // Web Admin navigation: the menu with every folder besides Home.
  const char* admin_folders;
  // Icon color choice: Automatic (the type's and entity's own colors).
  const char* tile_icon_color_auto;
  // Web Admin: the first password needs a tap on the display first.
  const char* web_auth_panel_tap_required;
  // Title switch: the tile takes its entity's name from Home Assistant
  const char* tile_title_auto;
  // Hint of the title switch
  const char* tile_title_auto_hint;
  // Settings Display page: section of the screen controls.
  const char* settings_screen;
  // Settings row labels (no colon).
  const char* settings_brightness;
  const char* settings_sleep;
  const char* settings_rotation;
  const char* settings_screensaver;
  const char* settings_starts_after;
  // Settings Display page, Sound group (#123, P4 boards with a speaker):
  // its heading, the volume slider and the microphone switch.
  const char* settings_sound;
  const char* settings_volume;
  const char* settings_microphone;
  // The Display category's name on the boards with the Sound group.
  const char* settings_display_sound;
  // Display category line: %s = sleep time ("5 min", "never").
  const char* settings_sleep_summary_fmt;
  // WiFi category line.
  const char* settings_not_connected;
  const char* settings_access_point_on;
  // System category line.
  const char* settings_update_available;
  // Settings Localization page: row labels (no colon).
  const char* settings_language;
  const char* settings_time_zone;
  const char* settings_time_format;
  const char* settings_date_format;
  const char* settings_keyboard;
  // Settings System page: section headings and rows (no colon).
  const char* settings_connection;
  // The way to Home Assistant under its row: %s = the Bridge address on the
  // direct link, the broker address over MQTT (panel and Web Admin).
  const char* settings_link_direct_fmt;
  const char* settings_link_mqtt_fmt;
  const char* settings_pairing;
  const char* settings_commands_encrypted;
  // Paired over the direct link: states are sealed as well as commands (the
  // camera video is not: it has its own ports).
  const char* settings_states_encrypted;
  // Web Admin MQTT row while the direct link is used: its line and the
  // note above the kept broker fields.
  const char* settings_mqtt_unused;
  const char* settings_mqtt_unused_note;
  const char* settings_paired;
  const char* settings_not_paired;
  const char* settings_pair;
  const char* settings_password_asks;
  const char* settings_password_none;
  const char* settings_allow;
  const char* settings_set_password_now;
  const char* settings_setup;
  // Settings System dialogs.
  const char* settings_unpair_question;
  const char* settings_unpair_text;
  const char* settings_unpair;
  const char* settings_restart_question;
  const char* settings_restart_text;
  const char* settings_github_text;
  // Settings WiFi page (no colon).
  const char* settings_choose_network;
  const char* settings_hotspot;
  const char* settings_hotspot_off_sub;
  const char* settings_hotspot_on_sub;
  // Hotspot details and the category title on Ethernet panels.
  const char* settings_network;
  const char* settings_address;
  const char* settings_networks;
  const char* settings_search;
  const char* settings_searching;
  const char* settings_add_network;
  const char* settings_join_network;
  const char* settings_network_name;
  const char* settings_connecting;
  const char* settings_connect_failed;
  const char* settings_ethernet;
  const char* settings_restart_to_switch;
  const char* settings_ip_address;
  const char* settings_automatic;
  const char* settings_static;
  const char* settings_static_address;
  const char* settings_set_in_web_admin;
  // First-start setup (no colon).
  // The step line: %d = step, %d = number of steps.
  const char* setup_step_fmt;
  const char* setup_language_title;
  const char* setup_wifi_title;
  const char* setup_ha_title;
  const char* setup_tiles_title;
  const char* setup_next;
  const char* setup_finish;
  const char* setup_later;
  const char* setup_other_network;
  const char* setup_with_phone;
  // The Home Assistant step in the order of the work (user 2026-10-12):
  // 1 install HomeTiles in Home Assistant, 2 pair this panel, 3 add it there.
  const char* setup_install_hometiles;
  const char* setup_install_where;
  const char* setup_pair_panel;
  const char* setup_add_in_ha;
  // Home Assistant's own menu names.
  const char* setup_add_where;
  const char* setup_back_to_pairing;
  const char* setup_install_bridge;
  const char* setup_pairing_code;
  const char* setup_paired;
  const char* setup_wifi_first;
  const char* setup_scan_tiles;
  const char* setup_password_hint;
  const char* setup_password_hint_long;
  const char* setup_leave_question;
  // The path to the setup: %s = Settings, %s = Setup (their translations).
  const char* setup_leave_text_fmt;
  const char* setup_leave;
  // The panel's update button beside the magnifier (Settings > System),
  // short so the device name keeps its room.
  const char* settings_update_short;
  // The screensaver picture's source in Web Admin: the SD card slideshow or
  // a picture Home Assistant sends (an image or a camera entity).
  const char* screensaver_source_sd;
  const char* screensaver_source_ha;
  const char* screensaver_picture_entity;
  const char* screensaver_picture_hint;
  const char* screensaver_picture_fit;
  const char* screensaver_fit_fill;
  const char* screensaver_fit_contain;
  const char* screensaver_fit_original;
  const char* screensaver_picture_every;
  // The Camera tile's picture (types/camera/picture.h): what the tile shows,
  // its two choices, and the transparency of the circle and the title pill
  // over the picture. Its entity, placement, interval and hint reuse the
  // screensaver picture's texts above.
  const char* camera_show;
  const char* camera_show_icon;
  const char* camera_show_picture;
  const char* camera_head_transparency;
  // The colors of circle and title over the picture (the design's, dark or
  // light),
  // and what a tap on a camera opens (the popup or straight the full screen).
  const char* camera_head;
  const char* camera_head_auto;
  const char* camera_head_dark;
  const char* camera_head_light;
  const char* camera_tap;
  const char* camera_tap_popup;
  const char* camera_tap_full;
  // The one tile color option (user 2026-10-11): the tile filled with its
  // icon's color.
  const char* tile_fill;
  // A rule row's own fill: the tile is filled while the row matches (user 2026-10-11).
  const char* tile_rule_fill;
  // The Tile Settings caption above the icon color, the tile color and Fill (user 2026-10-11).
  const char* tile_group_color;
  // Clock tile (user 2026-10-11): the weekday choice and its options (none, short, long).
  const char* clock_weekday_label;
  // Clock tile weekday option: no weekday.
  const char* clock_weekday_none;
  // Clock tile weekday option: the short name (followed by an example).
  const char* clock_weekday_short;
  // Clock tile weekday option: the full name (followed by an example).
  const char* clock_weekday_long;
  // Clock tile: a changed digit rolls up once (user 2026-10-11).
  const char* clock_roll_digits;
  // Clock tile: the alignment of time and date (left, center, right; user 2026-10-12).
  const char* clock_alignment_label;
  // Clock tile style (user 2026-10-12): digital, analog, wall clock.
  const char* clock_style_label;
  // Clock style: the digital time (the default).
  const char* clock_style_digital;
  // Clock style: hands on a round dial in the theme's circle color.
  const char* clock_style_analog;
  // Clock style: a designer wall clock (batons, slim hands, chosen colors).
  const char* clock_style_wall;
  // Wall clock: the dial color select.
  const char* clock_dial_label;
  // Wall clock: the color of the hands and batons.
  const char* clock_hands_label;
  // Wall clock color: slate blue.
  const char* clock_color_slate;
  // Wall clock color: black.
  const char* clock_color_black;
  // Wall clock color: white.
  const char* clock_color_white;
  // Wall clock color: sage green.
  const char* clock_color_sage;
  // Wall clock color: navy blue.
  const char* clock_color_navy;
  // Wall clock color: terracotta.
  const char* clock_color_terracotta;
  // Wall clock color: gold.
  const char* clock_color_gold;
  // Wall clock color: silver.
  const char* clock_color_silver;
  // Text tile: the horizontal alignment select.
  const char* text_align_horizontal;
  // Text tile: the vertical alignment select.
  const char* text_align_vertical;
  // Vertical alignment: at the top.
  const char* alignment_top;
  // Vertical alignment: in the middle.
  const char* alignment_middle;
  // Vertical alignment: at the bottom.
  const char* alignment_bottom;
  // Tile color: no card at all (Clock and Text, black stored).
  const char* tile_color_mode_transparent;
  // A new panel's first page (new_panel_tiles.h): three folders and the
  // welcome text with the hint to swipe, two lines that fit a Text tile's 31
  // bytes (tile_config.cpp SCENE_MAX).
  const char* new_panel_lights;
  const char* new_panel_climate;
  const char* new_panel_scenes;
  const char* new_panel_welcome;
  // The setup's first step: the head bar layout landscape or upright (a new
  // panel's first setup only).
  const char* setup_layout_landscape;
  const char* setup_layout_upright;
  // Web Admin: a locked folder dropped on a page slot (pages have no PIN).
  const char* js_page_unlock_confirm;
  const char* js_pages_saved;
  // A layout switch refused: %u folders without a place there, %s the
  // layout window's button ("Layout ändern").
  const char* layout_switch_refused;
  // Web Admin: a PIN for a page's folder is refused (pages have no PIN).
  const char* folder_pin_page;
  // Web Admin: the one word over the page slots under Home.
  const char* pages_title;
  // Web Admin: the tooltip on a full folder's greyed-out type list.
  const char* js_folder_type_locked;
  // Switch and Fan tile Orientation field (bar_orientation.h): the bar
  // horizontal or upright.
  const char* bar_orientation;
  const char* bar_horizontal;
  const char* bar_upright;
  // Media tile: the popup's layout (Web Admin) and the popup's sub-views
  // (source and sound mode, the speakers to group, the library).
  const char* media_popup_layout;
  const char* media_layout_cover_left;
  const char* media_layout_cover_center;
  const char* media_source;
  const char* media_sound_mode;
  const char* media_speakers;
  const char* media_library;
  const char* media_library_empty;
  const char* media_library_failed;
  // Media popup: the first speaker of the group list.
  const char* media_this_player;
  // Media library: the row that plays the opened playlist or album.
  const char* media_play_all;
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
  // Today on the round footer buttons of the Weather and Energy popups, the
  // standard short form where the word is long (French "Auj.").
  const char* weather_today_button;
  const char* weather_weekdays_short[7];
  const char* weather_months_short[12];
  const char* weather_conditions[15];

  // Optional sensors shown instead of the weather entity's own current
  // temperature and humidity: temperature sensor, humidity sensor and the
  // empty "use the weather entity's value" option.
  const char* weather_sensor_labels[3];

  const char* tile_type_climate;
  const char* climate_entity;
  const char* climate_target_temperature;
  const char* climate_target_humidity;
  // Caption above the target humidity control in a Climate mini tile.
  const char* climate_humidity_caption;
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

  // Lock, Alarm panel and Fan tiles and their popups, indexed by
  // i18n::DeviceLabel.
  const char* device_labels[42];
  // Lock states: locked, unlocked, locking, unlocking, open, opening,
  // jammed, unavailable, unknown.
  const char* lock_states[9];
  // Alarm panel states: disarmed, armed_home, armed_away, armed_night,
  // armed_vacation, armed_custom_bypass, pending, arming, disarming,
  // triggered, unavailable, unknown.
  const char* alarm_states[12];

  // Calendar tile and popup (types/calendar), indexed by i18n::CalendarLabel.
  const char* calendar_labels[25];
  // Month names standing alone, for headings such as "October 2026"
  // (month_names_in_date holds them as they stand in a date), January first.
  const char* month_names[12];
  // The first day of the week the Calendar tile uses unless the tile sets
  // one (tm_wday: 0 Sunday, 1 Monday).
  uint8_t first_weekday;
  // The Energy flow tile (types/energy_flow), EnergyFlowLabel order.
  const char* energy_flow_labels[9];

  // Shared Web Admin entity picker (tiles/entity-picker.js). Order: choose,
  // search, no match ({query}), load failed, try again, clear, none available,
  // released only (no Web Admin password), and the switch under the search:
  // recent, favorites (the panel's own entities), all.
  const char* entity_picker_labels[11];
  // Entity type shown on the right of a picker row, by Home Assistant
  // domain. Order: sensor, binary_sensor, light, switch/input_boolean, fan,
  // cover, climate, media_player, weather, camera, lock,
  // alarm_control_panel, number/input_number, select/input_select,
  // datetime/date/time/input_datetime, scene, script, button/input_button,
  // automation, humidifier, siren, remote, calendar.
  const char* entity_kind_labels[23];
  // Icon picker of the tile head (tiles/tile-head.js). Order: automatic,
  // automatic hint, no icon, no icon hint, no match ({query}).
  const char* icon_picker_labels[5];

  // The clock's long dates (clock_tile::DATE_FORMAT_D_MONTH*, user
  // 2026-10-11, issue #13): the month names as they stand in a date
  // (Polish in the genitive), January first, and the order with
  // {d}=day and {M}=month ("{d}. {M}" = "21. August").
  const char* month_names_in_date[12];
  const char* long_date_pattern;
  // The same date with the year, {Y}=year ("{d} de {M} de {Y}" =
  // "21 de agosto de 2026").
  const char* long_date_year_pattern;

  // The panel keyboard (settings_keyboard), as data so a language brings
  // its own: the letter layout under Auto (0 QWERTY, 1 QWERTZ, 2 AZERTY,
  // settings_keyboard::Layout) and the language's own letters and signs in
  // lower case ("áčďéěíňóřšťúůýž"), first on the accent page and first
  // among the forms of a held letter (keyboard_letters.h makes the
  // capitals). Only characters of the panel fonts.
  uint8_t keyboard_layout;
  const char* keyboard_letters;

};

// Index into LocaleProfile::energy_flow_labels.
enum class EnergyFlowLabel : uint8_t {
  Type,
  SolarPower,
  GridPower,
  BatteryPower,
  BatteryCharge,
  HomePower,
  ShowHeader,
  ShowCard,
  InvertSign,
  Count,
};
// Index into LocaleProfile::calendar_labels.
enum class CalendarLabel : uint8_t {
  TypeCalendar,
  EntityCalendar,
  CalendarN,  // printf pattern with one %u (the field number)
  View,
  ViewAgenda,
  ViewWeek,
  ViewMonth,
  WeekStart,
  Automatic,
  AllDay,
  NoEvents,
  NothingPlanned,
  InDays,          // printf pattern with one %u
  InHours,         // printf pattern with one %u
  InHoursMinutes,  // printf pattern with two %u (hours, minutes)
  InMinutes,       // printf pattern with one %u
  Now,
  Until,  // printf pattern with one %s (a time)
  UpNext,
  EventCount,  // printf pattern with one %u (two or more)
  OneEvent,
  ViewList,
  NoData,
  ViewToday,
  Next,
  Count
};
static_assert(static_cast<int>(CalendarLabel::Count) == 25, "LocaleProfile::calendar_labels size");

// Index into LocaleProfile::device_labels.
enum class DeviceLabel : uint8_t {
  TypeLock,
  TypeAlarm,
  TypeFan,
  EntityLock,
  EntityAlarm,
  EntityFan,
  OpenDoor,
  ReallyOpen,
  DoorOpen,
  Unlock,
  Lock,
  Disarm,
  ModeHome,
  ModeAway,
  ModeNight,
  ModeVacation,
  ModeCustom,
  FanPreset,
  FanNone,
  FanOscillate,
  FanDirection,
  FanForward,
  FanReverse,
  FanSpeed,  // printf pattern with one %u
  EnterCode,
  WrongCode,
  TooManyCodes,  // printf pattern with one %u (seconds)
  NeedPairAndPassword,
  NeedPair,
  NeedPassword,
  UnlockOff,
  DisarmOff,
  TextCodeUnsupported,
  ResultNotAllowed,
  ResultBusy,
  ResultUnsupported,
  ResultUnavailable,
  ResultExpired,
  ResultNoAnswer,
  ResultFailed,
  ClockNotSet,
  NoReaction,  // a sent Lock command the lock never answered with a state
  Count
};
static_assert(static_cast<int>(DeviceLabel::Count) == 42, "LocaleProfile::device_labels size");

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
const char* weather_today_button_label(const char* language_code);
const char* weather_tomorrow_label(const char* language_code);
// 0 temperature sensor, 1 humidity sensor, 2 the empty "use the weather
// entity's value" option.
const char* weather_sensor_label(const char* language_code, uint8_t index);
const char* energy_flow_label(const char* language_code, EnergyFlowLabel label);
const char* climate_tile_type_label(const char* language_code);
const char* climate_entity_label(const char* language_code);
const char* climate_target_temperature_label(const char* language_code);
const char* climate_target_humidity_label(const char* language_code);
const char* climate_humidity_caption_label(const char* language_code);
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
// Lock, Alarm panel and Fan texts (DeviceLabel) and the translated Home
// Assistant lock and alarm panel states (raw states stay English; an unknown
// raw state shows as Unknown).
const char* device_label(const char* language_code, DeviceLabel label);
const char* lock_state_label(const char* language_code, const char* state);
const char* alarm_state_label(const char* language_code, const char* state);
// Calendar tile and popup texts (CalendarLabel).
const char* calendar_label(const char* language_code, CalendarLabel label);
// A month standing alone (1 = January), for headings; "" outside 1..12.
const char* month_name(const char* language_code, int month);

}  // namespace i18n

#endif

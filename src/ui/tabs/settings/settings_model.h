#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

#include "src/core/i18n/i18n.h"

// What the Settings screen shows and does, apart from its look. The firmware
// reads and saves the real configuration and state (tab_settings.cpp); the
// host preview (tools/settings-preview.mjs) fills in the mockup's example
// values, so it renders exactly the screen code the panel runs.
// settings_screen.cpp only draws and reports touches through these.
namespace settings_model {

const i18n::Strings& text();
// Every registered language's texts, for sizes that must not change with the
// language.
uint8_t language_count();
const i18n::Strings& text_of(uint8_t language);
// The global tile color: the card and every surface on it.
uint32_t card_color();

// ---------- Display page ----------
struct DisplayValues {
  int brightness;  // the current backlight, percent
  int brightness_min;
  int brightness_max;
  int sleep_index;  // sleep_steps() = never
  int saver_index;
  int saver_brightness;
  int saver_brightness_min;
  int saver_brightness_max;
};
DisplayValues display_values();
// The stored brightness and sleep step (the Display category line).
int saved_brightness();
int saved_sleep_index();
// The step index that means "never"; the steps before it are timeouts.
int sleep_steps();
unsigned sleep_step_seconds(int index);
// Quarter turns (0, 90, 180, 270) or normal and flipped.
bool quarter_turns();
uint8_t rotation_index();
// Touches: `final` once the finger lifts (saved then).
void brightness_changed(int percent, bool final);
void sleep_changed(int index, bool final);
void saver_changed(int index, bool final);
void saver_brightness_changed(int percent, bool final);
void rotation_selected(uint8_t index);

// ---------- Localization page ----------
// The option lists, in the page's order: language, time zone, time format,
// date format, keyboard.
enum class LocaleList : uint8_t { Language = 0, TimeZone, TimeFormat, DateFormat, Keyboard };
constexpr uint8_t kLocaleListCount = 5;
uint8_t locale_option_count(LocaleList list);
const char* locale_option(LocaleList list, uint8_t index);
uint8_t locale_selected(LocaleList list);
// Saved right away (no Save button); a new language renews every text.
void locale_selected_changed(LocaleList list, uint8_t index);

// ---------- System page ----------
enum class UpdateState : uint8_t {
  Idle,
  Checking,
  Available,  // latest_version() names it
  UpToDate,
  CheckFailed,
  Downloading,
  Installed,
  InstallFailed,
  Restarting,
};
// The direct link to Home Assistant (command_channel): findable after Pair,
// the number dialog, the result of a failed attempt.
enum class PairState : uint8_t {
  NotPaired,
  Discoverable,
  Asking,
  Compare,
  Confirmed,
  Paired,
  NoAnswer,
  AlreadyPaired,
  Busy,
  Rejected,
  Failed,
};
struct SystemValues {
  UpdateState update;
  int progress;  // percent while Downloading
  bool connected;  // Home Assistant reaches the panel
  PairState pairing;
  int pair_seconds;  // left while Discoverable, 0 when unknown
  bool password_on;
  bool password_window;  // the first password may be set in Web Admin now
  int password_seconds;  // left in that window, 0 when unknown
};
// Also lets an older Bridge find the panel again after a pairing without an
// answer, as the former Security view did; the System page polls it.
SystemValues system_values();
const char* latest_version();
const char* device_name();
// The panel's address (the hotspot's while it is on); false without one.
bool panel_address(char* buf, size_t len);
// The way to Home Assistant under its row, in the panel and the Web Admin:
// "Direct · Bridge <host>" on the direct link, "MQTT · Broker <host>" over
// MQTT; false while neither is set up.
bool bridge_route(char* buf, size_t len);
// True while the panel uses the direct Bridge link, where pairing seals the
// states as well as the commands.
bool link_active();
// The pairing number as "123 456" while it is shown.
bool pairing_number(char* buf, size_t len);
// A note after unpairing without the Bridge's answer (remove it in Home
// Assistant too); nullptr otherwise. Pair clears it.
const char* pairing_note();
const char* repo_url();
// Touches: check for updates, or install the update that was found.
void update_pressed();
void restart();
void pair();
void confirm_pairing();
void cancel_pairing();
void unpair();
void allow_password();
void remove_password();

// ---------- WiFi page ----------
struct WifiNetwork {
  char ssid[33];
  uint8_t bars;  // signal strength 1..4
  bool locked;   // needs a password
};
struct WifiValues {
  bool ethernet_panel;     // the panel can switch to Ethernet
  bool ethernet_selected;  // the saved mode (applies after a restart)
  bool ethernet_active;    // the running mode
  bool access_point;       // the hotspot is on
  bool hotspot_switching;  // the hotspot is being switched
  bool connected;          // the running network is up
  uint8_t bars;            // the connected WiFi's signal 1..4
  bool scanning;
  bool connecting;         // a connection was just requested
  bool connect_failed;     // ... and did not come up in time
  bool static_ip;          // the saved IP mode
  bool static_ip_available;  // static values are set in Web Admin
  bool ip_mode_offered;    // Automatic | Static shows (static in use or changed)
  bool restart_needed;     // the network or IP mode differs from the running one
};
// Also finishes a running scan.
WifiValues wifi_values();
// The keyboard's letters: 0 QWERTY, 1 QWERTZ, 2 AZERTY (the Keyboard
// setting, or the language's on Auto).
uint8_t keyboard_layout();
// The keyboard's accent page: 0 German and other European, 1 French,
// 2 Polish (the language's).
uint8_t keyboard_accents();
// For the category: "Network" on panels that can use Ethernet.
bool ethernet_panel();
bool ethernet_active();
// The networks found, strongest first, without the connected one; the
// saved network is included even when the scan missed it.
uint8_t wifi_network_count();
const WifiNetwork& wifi_network(uint8_t index);
// The saved password when `ssid` is the saved network, else "".
const char* wifi_saved_password(const char* ssid);
void hotspot_details(char* ssid, size_t ssid_len, char* password, size_t password_len);
bool static_address(char* buf, size_t len);
// Touches.
void wifi_scan();
void wifi_connect(const char* ssid, const char* password);
void wifi_disconnect();
void hotspot_selected(bool on);
void network_mode_selected(bool ethernet);
void ip_mode_selected(bool static_ip);
// Ends the connection attempt's state (the entry closed).
void wifi_connect_done();

// ---------- First-start setup ----------
// The step the setup shows (0 language, 1 WiFi, 2 Home Assistant, 3 tiles),
// or -1 while it does not run. Kept in flash, so a restart resumes it.
int setup_step();
void setup_step_changed(uint8_t step);
// System > Setup: from the first step.
void setup_start();
// Leave or Finish: the setup ends and Home shows.
void setup_end();
// The panel's Web Admin address ("http://192.168.1.50"); false without one.
bool web_admin_url(char* buf, size_t len);
// The setup guide (Home Assistant and the Bridge).
const char* setup_guide_url();

// ---------- Category lines ----------
bool access_point_on();
bool network_connected();
void network_name(char* buf, size_t len);
const char* language_name();
// The time in the selected format; false without a valid time.
bool time_text(char* buf, size_t len);
bool update_available();
const char* firmware_version();

// ---------- Navigation ----------
void close_settings();

}  // namespace settings_model

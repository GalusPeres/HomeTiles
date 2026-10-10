#pragma once

#include <Arduino.h>
#include <vector>

// Media browse (catalog navigation) for media_player entities.
//
// Protocol (HomeTiles Bridge, custom_components/tab5_lvgl):
//   Request : {base}/cmnd/media
//             {"entity_id","command":"browse_media","session","request_id",
//              "revision","media_content_id","media_content_type"}
//   Catalog : {base}/stat/media/catalog/{page}   (one message per page)
//   Error   : {base}/stat/media                  ({"status":"error",...})
//
// The Bridge only delivers catalog data. Navigation, pagination and error
// handling live here on the panel. Everything in this module runs on the loop
// task (MQTT drain, LVGL timers and UI events all run there), so no locking.
//
// Navigation model:
//   - A browse session starts at the root catalog (empty id and type) and
//     gets a fresh random session id. Answers of older sessions are ignored.
//   - Every request gets a new request_id. Only the answer to the newest
//     request is accepted; late, duplicate or foreign answers are dropped.
//   - The visible level only changes once ALL pages of the answer arrived.
//     Until then the previous items stay visible (status Loading).
//   - An error answer keeps the previous level and items (status Error).

struct MediaBrowseItem {
  String title;
  String media_class;
  String content_id;
  String content_type;
  String thumbnail;
  bool can_play = false;
  bool can_expand = false;
  bool can_search = false;
};

// One entry of the navigation stack: the catalog node that was browsed.
struct MediaBrowseLevel {
  String content_id;
  String content_type;
  String title;  // Title of the item that was opened; empty for the root.
};

enum class MediaBrowseStatus : uint8_t {
  Idle,     // No browse session.
  Loading,  // A request is in flight; items() still shows the last level.
  Ready,    // items() belongs to the current level.
  Error,    // The last request failed; items() still shows the last level.
};

// --- Navigation ---------------------------------------------------------
// Starts a new session at the root catalog of entity_id.
bool media_browse_start(const char* entity_id);
// Opens items()[index] (must have can_expand). Returns false if not possible.
bool media_browse_open(size_t index);
// Plays items()[index] on the session's player (must have can_play).
// Does not change the browse state. Returns false if not possible.
bool media_browse_play(size_t index);
// Goes up one level. Returns false at the root.
bool media_browse_back();
// Requests the current level again (for example after an error).
bool media_browse_reload();
// Ends the session; later answers are ignored.
void media_browse_reset();

// --- State for the UI ---------------------------------------------------
MediaBrowseStatus media_browse_status();
const String& media_browse_entity();
// Current session id (empty while idle); thumbnail requests carry it.
const String& media_browse_session();
const std::vector<MediaBrowseItem>& media_browse_items();
// Number of levels on the stack; 1 = root. 0 while no level was loaded yet.
size_t media_browse_depth();
// Current level, or nullptr before the root has been loaded.
const MediaBrowseLevel* media_browse_current_level();
bool media_browse_can_go_back();
// Error code from the Bridge ("browse_media_not_supported", ...) or a local
// one ("mqtt_unavailable"). Empty unless status is Error.
const String& media_browse_error();
// True when the Bridge announced more pages than the panel keeps.
bool media_browse_truncated();
// Increments on every visible change (status, items, level, error).
uint32_t media_browse_generation();
// Increments only when items() was replaced (new level, new session, reset).
// The list view rebuilds its rows only when this changes.
uint32_t media_browse_items_version();
// Optional change callback (one listener, for the popup in step 3).
void media_browse_set_listener(void (*listener)());

// --- MQTT entry points --------------------------------------------------
// Both run on the loop task (processMqttMessage()) with a NUL-terminated copy
// of the payload.
void media_browse_handle_catalog_message(const char* topic,
                                         const char* payload,
                                         size_t length);
void media_browse_handle_status_message(const char* payload, size_t length);

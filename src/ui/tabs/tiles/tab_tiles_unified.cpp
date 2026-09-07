#include "src/types/value/value_control.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/core/display/display_manager.h"
#include "src/core/power/power_manager.h"
#include "src/core/display/lvgl_tick_service.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/binary_sensor/binary_sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/ui_manager.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/types/cover/renderer.h"
#include "src/types/binary_sensor/renderer.h"
#include "src/types/energy/energy_data.h"
#include "src/web/server/web_admin.h"
#include "src/tiles/icons/mdi_icons.h"
#include <misc/cache/instance/lv_image_cache.h>
#include <Arduino.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <new>

// Layout constants
static const int GAP = GRID_GAP;
static const int OUTER = 0;
static const int GRID_PAD_PX = GRID_PAD;

// Global state shared by all three grids
static lv_obj_t* g_tiles_grids[3] = {nullptr};           // [TAB0, TAB1, TAB2]
static lv_obj_t* g_tiles_roots[3] = {nullptr};
static scene_publish_cb_t g_tiles_scene_cbs[3] = {nullptr};
static lv_obj_t* g_tiles_objs[3][TILES_PER_GRID] = {nullptr};
static bool g_tiles_loaded[3] = {false, false, false};
static bool g_tiles_reload_requested[3] = {false, false, false};
static bool g_tiles_reload_only_if_loaded[3] = {true, true, true};
static bool g_tiles_release_requested[3] = {false, false, false};
static bool g_tiles_icon_refresh_requested = false;
static uint32_t g_tiles_icon_generation = 1;

static constexpr uint16_t kInvalidFolderId = 0xFFFF;
// ESP32-P4 devices keep the visible folder plus up to five direct navigation
// targets. Non-P4 devices retain the previous root + three recent working set.
// FolderCacheEntry storage itself lives in PSRAM; slots beyond the guaranteed
// first three are admitted only while the measured post-build reserves below
// remain healthy.
static constexpr size_t kMinResidentFolderUiCaches = 3;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
static constexpr size_t kMaxResidentFolderUiCaches = 6;
static constexpr size_t kMaxNavigationPreloadTargets = 5;
static constexpr uint32_t kNavigationPreloadIdleMs = 750;
static constexpr uint32_t kNavigationPreloadInitialDelayMs = 3000;
static constexpr uint32_t kNavigationPreloadStepGapMs = 250;
// Measured ESP32-P4 logs show that creating or deleting a folder grid changes
// total internal RAM by only a few kilobytes while the largest block is usually
// constrained by the LVGL draw band. Keep real post-build reserves and budget
// the largest observed cost of a previous grid before admitting the next one.
static constexpr uint32_t kFolderCacheMinInternalAfterBuild = 96UL * 1024UL;
static constexpr uint32_t kFolderCacheMinDmaAfterBuild = 48UL * 1024UL;
static constexpr uint32_t kFolderCacheMinLargestDmaBlock = 24UL * 1024UL;
static constexpr uint32_t kFolderCacheInitialEstimatedCost = 8UL * 1024UL;
#else
static constexpr size_t kMaxResidentFolderUiCaches = 4;
static constexpr uint32_t kFolderCacheGrowMinInternalFreeBytes = 112UL * 1024UL;
static constexpr uint32_t kFolderCacheGrowMinLargestInternalBytes = 72UL * 1024UL;
#endif

struct FolderCacheEntry {
  uint16_t folder_id = kInvalidFolderId;
  lv_obj_t* grid = nullptr;
  lv_obj_t* tile_objs[TILES_PER_GRID] = {};
  bool loaded = false;
  bool dirty = false;
  bool grid_loaded = false;
  bool widgets_valid = false;
  TileGridConfig grid_config{};
  TileWidgetCache widgets{};
  uint32_t last_used_ms = 0;
  uint32_t icon_generation = 0;
};

// FolderCacheEntry is large (~28 KB for config Strings + widget snapshots).
// Keeping even three slots in static DRAM consumed 86 KB before setup. Allocate
// the address-stable bounded slot array explicitly in PSRAM instead.
static FolderCacheEntry* g_folder_cache = nullptr;
static size_t g_folder_cache_slot_count = 0;
static bool g_folder_cache_in_psram = false;
static FolderCacheEntry* g_active_cache = nullptr;
// Set from the web-server task, drained in the render loop: dropping cached
// folder grids and LVGL is not safe to do off the loop thread.
static volatile bool g_folder_cache_invalidate_requested = false;
static TileWidgetCache* g_cache_build_saved_widgets = nullptr;
static bool g_folder_switch_pending = false;
static uint16_t g_pending_folder_id = kInvalidFolderId;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
static uint16_t g_navigation_preload_targets[kMaxNavigationPreloadTargets] = {};
static size_t g_navigation_preload_target_count = 0;
static size_t g_navigation_preload_cursor = 0;
static uint32_t g_navigation_preload_not_before_ms = 0;
static uint16_t g_navigation_parent_folder_id = kInvalidFolderId;
static uint32_t g_folder_cache_observed_internal_cost =
    kFolderCacheInitialEstimatedCost;
static uint32_t g_folder_cache_observed_dma_cost =
    kFolderCacheInitialEstimatedCost;
#endif
static bool g_visible_cache_refresh_requested = false;
static bool g_bridge_cache_refresh_requested = false;
static uint32_t g_bridge_cache_refresh_snapshot_ms = 0;
static constexpr uint32_t kFolderPreloadMinHeapBytes = 384UL * 1024UL;
static constexpr uint32_t kFolderPreloadMinPsramBytes = 4UL * 1024UL * 1024UL;

static bool ensure_folder_cache_storage() {
  if (g_folder_cache && g_folder_cache_slot_count > 0) return true;

  // Allocate the hard maximum once. Folder creation from WebAdmin must not
  // require moving live slots or leave a pool that was sized to "root only".
  const size_t requested_slots = kMaxResidentFolderUiCaches;

  void* storage = nullptr;
  size_t allocated_slots = requested_slots;
  for (; allocated_slots > 0; --allocated_slots) {
    storage = heap_caps_malloc(sizeof(FolderCacheEntry) * allocated_slots,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (storage) break;
  }

  if (!storage) {
    // Devices without usable PSRAM still need one active folder. Never reserve
    // several 28-KB slots from the scarce internal heap as a fallback.
    allocated_slots = 1;
    storage = heap_caps_malloc(sizeof(FolderCacheEntry),
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    g_folder_cache_in_psram = false;
  } else {
    g_folder_cache_in_psram = true;
  }
  if (!storage) {
    Serial.printf("[Tiles] ERROR: No memory for folder cache slot (%u bytes)\n",
                  static_cast<unsigned>(sizeof(FolderCacheEntry)));
    return false;
  }

  g_folder_cache = static_cast<FolderCacheEntry*>(storage);
  g_folder_cache_slot_count = allocated_slots;
  for (size_t i = 0; i < g_folder_cache_slot_count; ++i) {
    new (&g_folder_cache[i]) FolderCacheEntry();
  }
  Serial.printf("[Tiles] Folder cache: %u slots x %u bytes in %s\n",
                static_cast<unsigned>(g_folder_cache_slot_count),
                static_cast<unsigned>(sizeof(FolderCacheEntry)),
                g_folder_cache_in_psram ? "PSRAM" : "internal RAM");
  return true;
}

static bool ensure_cache_build_snapshot() {
  if (g_cache_build_saved_widgets) return true;

  bool internal_fallback = false;
  g_cache_build_saved_widgets = static_cast<TileWidgetCache*>(heap_caps_calloc(
      1, sizeof(TileWidgetCache), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_cache_build_saved_widgets) {
    // PSRAM failure must not prevent the active home page from opening.
    // Use the internal fallback only when external RAM is exhausted or
    // unavailable.
    g_cache_build_saved_widgets = static_cast<TileWidgetCache*>(heap_caps_calloc(
        1, sizeof(TileWidgetCache), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    internal_fallback = g_cache_build_saved_widgets != nullptr;
  }
  if (!g_cache_build_saved_widgets) {
    Serial.printf("[Tiles] ERROR: No memory for cache-build snapshot (%u bytes)\n",
                  static_cast<unsigned>(sizeof(TileWidgetCache)));
    return false;
  }
  if (internal_fallback) {
    Serial.printf("[Tiles] WARN: Cache-build snapshot uses internal RAM (%u bytes)\n",
                  static_cast<unsigned>(sizeof(TileWidgetCache)));
  }
  return true;
}

static void log_folder_switch_memory(const char* phase, uint16_t folder_id) {
  Serial.printf("[Tiles] %s folder=%u | heap=%lu KB | heap_min=%lu KB | psram=%lu KB\n",
                phase ? phase : "switch",
                static_cast<unsigned>(folder_id),
                ESP.getFreeHeap() / 1024,
                ESP.getMinFreeHeap() / 1024,
                ESP.getFreePsram() / 1024);
}

static bool can_preload_more_folders() {
  const uint32_t free_heap = ESP.getFreeHeap();
  const uint32_t psram_total = ESP.getPsramSize();
  const uint32_t free_psram = ESP.getFreePsram();
  if (free_heap < kFolderPreloadMinHeapBytes) {
    return false;
  }
  if (psram_total > 0 && free_psram < kFolderPreloadMinPsramBytes) {
    return false;
  }
  return true;
}

#if defined(CONFIG_IDF_TARGET_ESP32P4)
static void clear_navigation_preload_plan() {
  for (size_t i = 0; i < kMaxNavigationPreloadTargets; ++i) {
    g_navigation_preload_targets[i] = kInvalidFolderId;
  }
  g_navigation_preload_target_count = 0;
  g_navigation_preload_cursor = 0;
  g_navigation_preload_not_before_ms = 0;
  g_navigation_parent_folder_id = kInvalidFolderId;
}

static bool navigation_preload_add_target(uint16_t source_folder_id,
                                          uint16_t target_folder_id,
                                          bool is_parent = false) {
  if (target_folder_id == kInvalidFolderId ||
      target_folder_id == source_folder_id ||
      !tileConfig.folderExists(target_folder_id)) {
    return false;
  }
  for (size_t i = 0; i < g_navigation_preload_target_count; ++i) {
    if (g_navigation_preload_targets[i] == target_folder_id) return false;
  }
  if (g_navigation_preload_target_count >= kMaxNavigationPreloadTargets) {
    return false;
  }
  g_navigation_preload_targets[g_navigation_preload_target_count++] =
      target_folder_id;
  if (is_parent) g_navigation_parent_folder_id = target_folder_id;
  return true;
}

static uint16_t navigation_folder_id_from_tile(const Tile& tile) {
  return static_cast<uint16_t>(
      (static_cast<uint16_t>(tile.key_modifier) << 8) | tile.key_code);
}

static size_t navigation_preload_priority(uint16_t folder_id) {
  for (size_t i = 0; i < g_navigation_preload_target_count; ++i) {
    if (g_navigation_preload_targets[i] == folder_id) return i;
  }
  return kMaxNavigationPreloadTargets;
}

static bool navigation_folder_is_parent(uint16_t folder_id) {
  return g_navigation_parent_folder_id != kInvalidFolderId &&
         folder_id == g_navigation_parent_folder_id;
}

static void schedule_navigation_preload(
    uint16_t source_folder_id, const TileGridConfig& config,
    uint32_t initial_delay_ms = kNavigationPreloadIdleMs) {
  clear_navigation_preload_plan();

  // Back is always the most useful warm target and is admitted before normal
  // folder buttons even if it appears later in the grid layout.
  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    if (config.tiles[i].type != TILE_BACK) continue;
    navigation_preload_add_target(
        source_folder_id, tileConfig.getFolderParent(source_folder_id), true);
    break;
  }

  struct NavigationCandidate {
    uint16_t folder_id = kInvalidFolderId;
    uint8_t row = 0;
    uint8_t col = 0;
    uint8_t config_index = 0;
  };
  NavigationCandidate candidates[TILES_PER_GRID]{};
  size_t candidate_count = 0;
  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = config.tiles[i];
    if (tile.type != TILE_FOLDER) continue;
    candidates[candidate_count++] = {
        navigation_folder_id_from_tile(tile), tile.row, tile.col, i};
  }
  // Config entries are not guaranteed to be stored in visual order. A stable
  // insertion sort keeps preload priority equal to what the user sees: top to
  // bottom, left to right, with config index as deterministic tie breaker.
  for (size_t i = 1; i < candidate_count; ++i) {
    const NavigationCandidate value = candidates[i];
    size_t pos = i;
    while (pos > 0) {
      const NavigationCandidate& prev = candidates[pos - 1];
      const bool before = value.row < prev.row ||
                          (value.row == prev.row && value.col < prev.col) ||
                          (value.row == prev.row && value.col == prev.col &&
                           value.config_index < prev.config_index);
      if (!before) break;
      candidates[pos] = prev;
      --pos;
    }
    candidates[pos] = value;
  }
  for (size_t i = 0; i < candidate_count; ++i) {
    navigation_preload_add_target(source_folder_id, candidates[i].folder_id);
    if (g_navigation_preload_target_count >=
        kMaxNavigationPreloadTargets) {
      break;
    }
  }

  g_navigation_preload_not_before_ms = millis() + initial_delay_ms;
  Serial.printf("[Tiles] nav-preload plan folder=%u parent=%u targets=%u",
                static_cast<unsigned>(source_folder_id),
                static_cast<unsigned>(g_navigation_parent_folder_id),
                static_cast<unsigned>(g_navigation_preload_target_count));
  for (size_t i = 0; i < g_navigation_preload_target_count; ++i) {
    Serial.printf(" %u",
                  static_cast<unsigned>(g_navigation_preload_targets[i]));
  }
  Serial.println();
}

#endif

static void build_grid_track_descriptors(lv_coord_t* dsc, uint8_t count, lv_coord_t cell_size) {
  if (!dsc) return;
  for (uint8_t i = 0; i < count; ++i) {
    dsc[i] = cell_size;
  }
  dsc[count] = LV_GRID_TEMPLATE_LAST;
}

/* === Entity-State Cache (for lazy-loaded tabs) === */
struct EntityCacheEntry {
  String entity_id;
  String payload;
  uint32_t payload_hash = 2166136261u;
  uint32_t updated_ms = 0;
  bool valid = false;
};

static constexpr size_t kEntityCacheSize = TILES_PER_GRID * 8;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
static EntityCacheEntry* g_entity_cache = nullptr;
static bool g_entity_cache_init_attempted = false;
#else
static EntityCacheEntry g_entity_cache[kEntityCacheSize];
#endif
static size_t g_entity_cache_cursor = 0;

static bool ensure_entity_cache_storage() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (g_entity_cache) return true;
  if (g_entity_cache_init_attempted) return false;
  g_entity_cache_init_attempted = true;
  void* memory = heap_caps_malloc(
      sizeof(EntityCacheEntry) * kEntityCacheSize,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) {
    Serial.printf(
        "[Tiles/Mem] ERROR: Entity cache (%u bytes) not allocated in PSRAM; "
        "cache disabled\n",
        static_cast<unsigned>(sizeof(EntityCacheEntry) * kEntityCacheSize));
    return false;
  }
  g_entity_cache = static_cast<EntityCacheEntry*>(memory);
  for (size_t i = 0; i < kEntityCacheSize; ++i) {
    new (&g_entity_cache[i]) EntityCacheEntry();
  }
  Serial.printf("[Tiles/Mem] Entity cache=%u bytes in PSRAM\n",
                static_cast<unsigned>(sizeof(EntityCacheEntry) *
                                      kEntityCacheSize));
#endif
  return true;
}

static uint32_t hash_entity_payload(const char* payload, size_t length) {
  uint32_t hash = 2166136261u;
  if (!payload) return hash;
  for (size_t pos = 0; pos < length; ++pos) {
    hash ^= static_cast<uint8_t>(payload[pos]);
    hash *= 16777619u;
  }
  return hash;
}

static void refresh_entity_payload_signature(EntityCacheEntry& entry) {
  entry.payload_hash =
      hash_entity_payload(entry.payload.c_str(), entry.payload.length());
}

// Media cache updates need special handling. Bridge sends cover-free
// state_fast first for each change and often skips the full payload
// for rapid position ticks ("stale media payload"). Replacing the cached
// entry outright would discard embedded artwork, leaving covers empty
// after a grid reload/Web Admin save until the next track change.
// Bridge appends the three entity_picture_* fields as one block at the
// end of the JSON; carry that block into the new payload.
static bool merge_cached_cover_fields(const String& old_payload, String& new_payload) {
  // Require length > 2: inserting ",..." into empty "{}" would be invalid.
  if (new_payload.length() <= 2 || new_payload[0] != '{' ||
      new_payload[new_payload.length() - 1] != '}') return false;
  if (new_payload.indexOf("\"entity_picture_data\"") >= 0) return false;
  const int cover_start = old_payload.indexOf("\"entity_picture_data\"");
  if (cover_start < 1) return false;
  const int cover_end = old_payload.lastIndexOf('}');
  if (cover_end <= cover_start) return false;

  String merged;
  merged.reserve(new_payload.length() + (cover_end - cover_start) + 2);
  merged = new_payload.substring(0, new_payload.length() - 1);
  merged += ',';
  merged += old_payload.substring(cover_start, cover_end);
  merged += '}';
  new_payload = merged;
  return true;
}

static void cache_entity_payload_at(const char* entity_id, const char* payload, uint32_t updated_ms, bool keep_newer) {
  if (!entity_id || !payload || entity_id[0] == '\0') return;
  if (!ensure_entity_cache_storage()) return;
  if (updated_ms == 0) updated_ms = millis();

  for (size_t i = 0; i < kEntityCacheSize; ++i) {
    EntityCacheEntry& entry = g_entity_cache[i];
    if (entry.valid && entry.entity_id.equalsIgnoreCase(entity_id)) {
      if (keep_newer && entry.updated_ms != 0 && (int32_t)(entry.updated_ms - updated_ms) > 0) {
        return;
      }
      if (entry.payload.equals(payload)) {
        entry.updated_ms = updated_ms;
        return;
      }
      // Apply cheap filters before the merge's String work: sensors are not
      // JSON objects, and only media entries contain artwork.
      if (payload[0] == '{' && strstr(payload, "\"entity_picture_data\"") == nullptr &&
          entry.payload.indexOf("\"entity_picture_data\"") >= 0) {
        String incoming = payload;
        if (merge_cached_cover_fields(entry.payload, incoming)) {
          entry.payload = incoming;
          refresh_entity_payload_signature(entry);
          entry.updated_ms = updated_ms;
          return;
        }
      }
      entry.payload = payload;
      refresh_entity_payload_signature(entry);
      entry.updated_ms = updated_ms;
      return;
    }
  }

  for (size_t i = 0; i < kEntityCacheSize; ++i) {
    if (!g_entity_cache[i].valid) {
      g_entity_cache[i].entity_id = entity_id;
      g_entity_cache[i].payload = payload;
      refresh_entity_payload_signature(g_entity_cache[i]);
      g_entity_cache[i].updated_ms = updated_ms;
      g_entity_cache[i].valid = true;
      return;
    }
  }

  size_t idx = g_entity_cache_cursor++ % kEntityCacheSize;
  g_entity_cache[idx].entity_id = entity_id;
  g_entity_cache[idx].payload = payload;
  refresh_entity_payload_signature(g_entity_cache[idx]);
  g_entity_cache[idx].updated_ms = updated_ms;
  g_entity_cache[idx].valid = true;
}

static void cache_entity_payload(const char* entity_id, const char* payload) {
  cache_entity_payload_at(entity_id, payload, millis(), false);
}

static void cache_entity_payload_from_bridge(const char* entity_id, const char* payload, uint32_t snapshot_ms) {
  cache_entity_payload_at(entity_id, payload, snapshot_ms, true);
}

static bool get_cached_entity_payload(const char* entity_id, String& out) {
  if (!entity_id || entity_id[0] == '\0') return false;
  if (!ensure_entity_cache_storage()) return false;
  for (size_t i = 0; i < kEntityCacheSize; ++i) {
    const EntityCacheEntry& entry = g_entity_cache[i];
    if (entry.valid && entry.entity_id.equalsIgnoreCase(entity_id)) {
      out = entry.payload;
      return true;
    }
  }
  return false;
}

bool tiles_get_cached_entity_payload(const char* entity_id, String& out) {
  return get_cached_entity_payload(entity_id, out);
}

bool tiles_get_cached_entity_payload_signature(const char* entity_id,
                                               uint32_t& hash_out,
                                               size_t& length_out) {
  if (!entity_id || entity_id[0] == '\0') return false;
  if (!ensure_entity_cache_storage()) return false;
  for (size_t i = 0; i < kEntityCacheSize; ++i) {
    const EntityCacheEntry& entry = g_entity_cache[i];
    if (!entry.valid || !entry.entity_id.equalsIgnoreCase(entity_id)) continue;

    hash_out = entry.payload_hash;
    length_out = entry.payload.length();
    return true;
  }
  return false;
}

void tiles_cache_entity_payload(const char* entity_id, const char* payload) {
  cache_entity_payload(entity_id, payload);
}

static bool get_cached_or_initial_payload(const Tile& tile, String& out) {
  if (!tile.sensor_entity.length()) return false;
  if (tileTypeIsEditableValue(tile.type)) {
    out = haBridgeConfig.findEditableValue(tile.sensor_entity);
    return out.length() > 0;
  }
  if (get_cached_entity_payload(tile.sensor_entity.c_str(), out)) return true;
  String initial = haBridgeConfig.findSensorInitialValue(tile.sensor_entity);
  if (!initial.length()) return false;
  out = initial;
  cache_entity_payload(tile.sensor_entity.c_str(), initial.c_str());
  return true;
}

static bool is_url_path_local(const String& value) {
  return value.startsWith("http://") || value.startsWith("https://");
}

static bool is_slideshow_token_local(const String& value) {
  return value.startsWith("__slideshow");
}

static bool is_disabled_token(const String& value) {
  if (!value.length()) return false;
  String t = value;
  t.trim();
  if (!t.length()) return true;
  t.toLowerCase();
  return t == "-" || t == "none" || t == "null" || t == "no" || t == "off";
}

static String resolve_tile_sensor_unit(const Tile& tile) {
  String unit = tile.sensor_unit;
  if (is_disabled_token(unit)) return String();

  unit.trim();
  if (!unit.length() && tile.sensor_entity.length()) {
    unit = haBridgeConfig.findSensorUnit(tile.sensor_entity);
  }
  // Energy popups already use this period cache as a fallback. Apply the
  // same fallback on every folder/widget cache restore; otherwise folder
  // navigation replaces "17.55 kWh" with "17.55".
  if (!unit.length() && tile.type == TILE_ENERGY && tile.sensor_entity.length()) {
    unit = energy_find_cached_unit(tile.sensor_entity);
  }
  unit.trim();
  return unit;
}

static String normalize_preview_key_local(String raw) {
  raw.trim();
  if (raw.startsWith("S:")) raw = raw.substring(2);
  if (!raw.length()) return raw;
  if (is_url_path_local(raw)) return raw;
  if (!raw.startsWith("/")) raw = "/" + raw;
  return raw;
}

static lv_obj_t* find_preview_image_child(lv_obj_t* parent) {
  if (!parent) return nullptr;
  uint32_t count = lv_obj_get_child_count(parent);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
    if (!child) continue;
    if (lv_obj_has_flag(child, LV_OBJ_FLAG_USER_1)) return child;
    lv_obj_t* nested = find_preview_image_child(child);
    if (nested) return nested;
  }
  return nullptr;
}

static lv_obj_t* find_mdi_label_child(lv_obj_t* parent) {
  if (!parent) return nullptr;
  uint32_t count = lv_obj_get_child_count(parent);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
    if (!child) continue;
    const lv_font_t* font = lv_obj_get_style_text_font(child, LV_PART_MAIN);
    if (font == FONT_MDI_ICONS) return child;
    lv_obj_t* nested = find_mdi_label_child(child);
    if (nested) return nested;
  }
  return nullptr;
}

static lv_obj_t* create_tiles_grid(lv_obj_t* parent);
static void apply_cached_states(GridType grid_type, const TileGridConfig& config, bool include_media = true);

/* === Deferred image preview loading === */
static lv_timer_t* g_preview_timer = nullptr;
static GridType g_preview_timer_grid = GridType::TAB0;
static constexpr uint32_t kPreviewDelayMs = 30;
static constexpr uint32_t kPreviewStepMs = 12;
static constexpr uint8_t kPreviewLoadsPerStep = 1;
static uint32_t g_preview_block_until_ms[3] = {0, 0, 0};
static uint8_t g_preview_next_index = 0;
static bool g_preview_only_missing = true;

static void tiles_refresh_all_image_previews(GridType grid_type, bool only_missing);
static void hide_preview_images(GridType grid_type);
static void tiles_refresh_icons_for_grid(GridType grid_type);
static bool process_preview_step(GridType grid_type, bool only_missing, uint8_t max_loads);

static void refresh_active_folder_icons_if_stale(FolderCacheEntry& entry) {
  if (entry.icon_generation == g_tiles_icon_generation) return;

  const uint32_t started_ms = millis();
  tiles_refresh_icons_for_grid(GridType::TAB0);
  entry.icon_generation = g_tiles_icon_generation;
  Serial.printf(
      "[Tiles] folder-icons refreshed folder=%u generation=%lu in %lu ms\n",
      static_cast<unsigned>(entry.folder_id),
      static_cast<unsigned long>(entry.icon_generation),
      static_cast<unsigned long>(millis() - started_ms));
}

static void stop_preview_timer() {
  if (g_preview_timer) {
    lv_timer_del(g_preview_timer);
    g_preview_timer = nullptr;
  }
}

static void preview_timer_cb(lv_timer_t* timer) {
  (void)timer;
  const uint8_t idx = static_cast<uint8_t>(g_preview_timer_grid);
  if (idx >= 3 || !g_tiles_grids[idx] || !g_tiles_loaded[idx]) {
    if (idx < 3) g_preview_block_until_ms[idx] = 0;
    stop_preview_timer();
    return;
  }

  const uint32_t now = millis();
  if (g_preview_block_until_ms[idx] != 0 &&
      (int32_t)(now - g_preview_block_until_ms[idx]) < 0) {
    return;
  }
  g_preview_block_until_ms[idx] = 0;

  if (process_preview_step(g_preview_timer_grid, g_preview_only_missing, kPreviewLoadsPerStep)) {
    stop_preview_timer();
  }
}

static void schedule_preview_load(GridType grid_type) {
  stop_preview_timer();
  const uint8_t idx = static_cast<uint8_t>(grid_type);
  if (idx < 3) {
    g_preview_block_until_ms[idx] = millis() + kPreviewDelayMs;
  }
  hide_preview_images(grid_type);
  g_preview_timer_grid = grid_type;
  g_preview_only_missing = true;
  g_preview_next_index = 0;
  g_preview_timer = lv_timer_create(preview_timer_cb, kPreviewStepMs, nullptr);
  if (!g_preview_timer) {
    if (idx < 3) g_preview_block_until_ms[idx] = 0;
    tiles_refresh_all_image_previews(grid_type, true);
  }
}

/* === Helper: Get grid config by type === */
static const TileGridConfig& getGridConfig(GridType type) {
  (void)type;
  return tileConfig.getActiveGrid();
}

/* === Helper: Get grid name for logging === */
static const char* getGridName(GridType type) {
  (void)type;
  return "TilesFolder";
}

static bool get_tile_layout(const Tile& tile, uint8_t& col, uint8_t& row, uint8_t& span_w, uint8_t& span_h) {
  if (tile.col >= GRID_COLS || tile.row >= GRID_ROWS) return false;
  col = tile.col;
  row = tile.row;
  span_w = tile.span_w < 1 ? 1 : tile.span_w;
  span_h = tile.span_h < 1 ? 1 : tile.span_h;
  clamp_media_tile_layout(tile.type, col, row, span_w, span_h);
  if (span_w > GRID_COLS - col) span_w = GRID_COLS - col;
  if (span_h > GRID_ROWS - row) span_h = GRID_ROWS - row;
  return true;
}

static void mark_occupied(bool occupied[GRID_ROWS][GRID_COLS], uint8_t col, uint8_t row, uint8_t span_w, uint8_t span_h) {
  for (uint8_t r = row; r < row + span_h; ++r) {
    for (uint8_t c = col; c < col + span_w; ++c) {
      if (r < GRID_ROWS && c < GRID_COLS) {
        occupied[r][c] = true;
      }
    }
  }
}

static FolderCacheEntry* find_folder_cache(uint16_t folder_id) {
  for (size_t i = 0; i < g_folder_cache_slot_count; ++i) {
    FolderCacheEntry& entry = g_folder_cache[i];
    if (entry.folder_id == folder_id) {
      return &entry;
    }
  }
  return nullptr;
}

static void reconstruct_cache_entry(FolderCacheEntry& entry,
                                    uint16_t folder_id = kInvalidFolderId) {
  // Assignment from an empty Arduino String can retain the old capacity. Run
  // the real destructors so all per-tile String buffers are actually freed,
  // then reconstruct the address-stable slot in place.
  entry.~FolderCacheEntry();
  new (&entry) FolderCacheEntry();
  entry.folder_id = folder_id;
}

static void release_cache_entry(FolderCacheEntry& entry, uint16_t next_folder_id) {
  lv_obj_t* grid = entry.grid;
  entry.grid = nullptr;
  if (grid) {
    // All callers run in the main loop, outside an LVGL event callback. A
    // synchronous delete cannot be silently lost on lv_async_call OOM and
    // completes every child LV_EVENT_DELETE before the slot is reused.
    lv_obj_delete(grid);
  }
  reconstruct_cache_entry(entry, next_folder_id);
}

static void clear_cache_entry(FolderCacheEntry& entry) {
  release_cache_entry(entry, entry.folder_id);
}

static void reset_cache_entry(FolderCacheEntry& entry) {
  release_cache_entry(entry, kInvalidFolderId);
}

static FolderCacheEntry* allocate_folder_cache(uint16_t folder_id) {
  if (FolderCacheEntry* existing = find_folder_cache(folder_id)) {
    return existing;
  }
  for (size_t i = 0; i < g_folder_cache_slot_count; ++i) {
    FolderCacheEntry& entry = g_folder_cache[i];
    if (entry.folder_id == kInvalidFolderId) {
      entry.folder_id = folder_id;
      return &entry;
    }
  }
  // A failed build or invalidation may leave a non-resident slot associated
  // with an old folder. Reclaim it without growing metadata per folder.
  for (size_t i = 0; i < g_folder_cache_slot_count; ++i) {
    FolderCacheEntry& entry = g_folder_cache[i];
    if (&entry != g_active_cache && !entry.grid) {
      reconstruct_cache_entry(entry, folder_id);
      return &entry;
    }
  }
  return nullptr;
}

static size_t resident_folder_cache_count() {
  size_t count = 0;
  for (size_t i = 0; i < g_folder_cache_slot_count; ++i) {
    const FolderCacheEntry& entry = g_folder_cache[i];
    if (entry.grid) ++count;
  }
  return count;
}

#if defined(CONFIG_IDF_TARGET_ESP32P4)
struct FolderCacheMemorySnapshot {
  uint32_t internal_free = 0;
  uint32_t dma_free = 0;
  uint32_t dma_largest = 0;
  uint32_t psram_free = 0;
};

static FolderCacheMemorySnapshot folder_cache_memory_snapshot() {
  FolderCacheMemorySnapshot snapshot;
  snapshot.internal_free =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snapshot.dma_free = heap_caps_get_free_size(
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  snapshot.dma_largest = heap_caps_get_largest_free_block(
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  snapshot.psram_free =
      heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return snapshot;
}

static bool folder_cache_post_build_reserve_ok(
    const FolderCacheMemorySnapshot& snapshot) {
  return snapshot.internal_free >= kFolderCacheMinInternalAfterBuild &&
         snapshot.dma_free >= kFolderCacheMinDmaAfterBuild &&
         snapshot.dma_largest >= kFolderCacheMinLargestDmaBlock &&
         snapshot.psram_free >= kFolderPreloadMinPsramBytes;
}

static void observe_folder_cache_build_cost(
    const FolderCacheMemorySnapshot& before,
    const FolderCacheMemorySnapshot& after) {
  const uint32_t internal_cost = before.internal_free > after.internal_free
                                     ? before.internal_free - after.internal_free
                                     : 0;
  const uint32_t dma_cost =
      before.dma_free > after.dma_free ? before.dma_free - after.dma_free : 0;
  if (internal_cost > g_folder_cache_observed_internal_cost) {
    g_folder_cache_observed_internal_cost = internal_cost;
  }
  if (dma_cost > g_folder_cache_observed_dma_cost) {
    g_folder_cache_observed_dma_cost = dma_cost;
  }
}
#endif

static bool folder_cache_has_growth_headroom() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  const FolderCacheMemorySnapshot snapshot = folder_cache_memory_snapshot();
  return snapshot.internal_free >=
             kFolderCacheMinInternalAfterBuild +
                 g_folder_cache_observed_internal_cost &&
         snapshot.dma_free >=
             kFolderCacheMinDmaAfterBuild + g_folder_cache_observed_dma_cost &&
         snapshot.dma_largest >= kFolderCacheMinLargestDmaBlock &&
         snapshot.psram_free >= kFolderPreloadMinPsramBytes;
#else
  const uint32_t internal_free =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t largest_internal =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return internal_free >= kFolderCacheGrowMinInternalFreeBytes &&
         largest_internal >= kFolderCacheGrowMinLargestInternalBytes;
#endif
}

static bool folder_cache_requires_eviction_before_build() {
  const size_t resident = resident_folder_cache_count();
  if (g_folder_cache_slot_count == 0 || resident >= g_folder_cache_slot_count) {
    return true;
  }
  if (resident < kMinResidentFolderUiCaches) return false;
  if (folder_cache_has_growth_headroom()) return false;

#if defined(CONFIG_IDF_TARGET_ESP32P4)
  const FolderCacheMemorySnapshot snapshot = folder_cache_memory_snapshot();
  Serial.printf(
      "[Tiles] folder-cache growth stopped at %u grids | int=%lu KB "
      "dma=%lu KB largest_dma=%lu KB psram=%lu KB estimate=%lu/%lu KB\n",
      static_cast<unsigned>(resident),
      static_cast<unsigned long>(snapshot.internal_free / 1024),
      static_cast<unsigned long>(snapshot.dma_free / 1024),
      static_cast<unsigned long>(snapshot.dma_largest / 1024),
      static_cast<unsigned long>(snapshot.psram_free / 1024),
      static_cast<unsigned long>(g_folder_cache_observed_internal_cost / 1024),
      static_cast<unsigned long>(g_folder_cache_observed_dma_cost / 1024));
#else
  Serial.printf(
      "[Tiles] folder-cache growth stopped at %u grids | int=%lu KB largest=%lu KB\n",
      static_cast<unsigned>(resident),
      static_cast<unsigned long>(
          heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) /
                                 1024));
#endif
  return true;
}

static FolderCacheEntry* find_folder_cache_eviction_candidate(
    uint16_t requested_folder_id, bool for_preload) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  FolderCacheEntry* best = nullptr;
  bool best_outside_working_set = false;
  size_t best_priority = 0;
  uint32_t best_age = 0;
  const uint32_t now = millis();
  const size_t requested_priority =
      navigation_preload_priority(requested_folder_id);

  for (size_t i = 0; i < g_folder_cache_slot_count; ++i) {
    FolderCacheEntry& entry = g_folder_cache[i];
    if (!entry.grid || &entry == g_active_cache ||
        entry.folder_id == requested_folder_id) {
      continue;
    }
    // Parent/back is the one target that must survive every lower-priority
    // preload and every normal child navigation. The active grid is excluded
    // above and becomes the new parent after entering a child.
    if (navigation_folder_is_parent(entry.folder_id)) continue;

    const uint32_t age = now - entry.last_used_ms;
    const size_t priority = navigation_preload_priority(entry.folder_id);
    const bool outside_working_set =
        priority >= g_navigation_preload_target_count;

    // A background preload may replace stale/outside entries or a strictly
    // lower-priority target only. It must never evict an already resident
    // higher-priority target to make room for a later one.
    if (for_preload && !outside_working_set &&
        requested_priority < g_navigation_preload_target_count &&
        priority <= requested_priority) {
      continue;
    }

    bool prefer = !best;
    if (best) {
      if (outside_working_set != best_outside_working_set) {
        prefer = outside_working_set;
      } else if (!outside_working_set && priority != best_priority) {
        prefer = priority > best_priority;
      } else {
        prefer = age > best_age;
      }
    }
    if (!prefer) continue;
    best = &entry;
    best_outside_working_set = outside_working_set;
    best_priority = priority;
    best_age = age;
  }
  return best;
#else
  (void)for_preload;
  static constexpr uint16_t kRootFolderId = 0;
  FolderCacheEntry* oldest = nullptr;
  FolderCacheEntry* root_fallback = nullptr;
  uint32_t oldest_age = 0;
  const uint32_t now = millis();

  for (size_t i = 0; i < g_folder_cache_slot_count; ++i) {
    FolderCacheEntry& entry = g_folder_cache[i];
    if (!entry.grid || &entry == g_active_cache ||
        entry.folder_id == requested_folder_id) {
      continue;
    }
    if (entry.folder_id == kRootFolderId) {
      root_fallback = &entry;
      continue;
    }
    const uint32_t age = now - entry.last_used_ms;
    if (!oldest || age > oldest_age) {
      oldest = &entry;
      oldest_age = age;
    }
  }
  return oldest ? oldest : root_fallback;
#endif
}

static bool evict_folder_cache_before_build(uint16_t requested_folder_id,
                                            bool for_preload = false) {
  FolderCacheEntry* victim =
      find_folder_cache_eviction_candidate(requested_folder_id, for_preload);
  if (!victim) return false;

  const uint16_t victim_id = victim->folder_id;
  const uint32_t started_ms = millis();
  const size_t resident_before = resident_folder_cache_count();
  const uint32_t int_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t largest_before =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const uint32_t children_before = g_tiles_roots[0]
                                       ? lv_obj_get_child_count(g_tiles_roots[0])
                                       : 0;
  uint32_t lvgl_used_before = 0;
  if (lv_is_initialized()) {
    lv_mem_monitor_t monitor{};
    lv_mem_monitor(&monitor);
    lvgl_used_before = monitor.total_size - monitor.free_size;
  }

  reset_cache_entry(*victim);

  uint32_t lvgl_used_after = 0;
  if (lv_is_initialized()) {
    lv_mem_monitor_t monitor{};
    lv_mem_monitor(&monitor);
    lvgl_used_after = monitor.total_size - monitor.free_size;
  }
  Serial.printf(
      "[Tiles] folder-cache evict folder=%u before folder=%u mode=%s in %lu ms | "
      "resident=%u->%u | int=%lu->%lu KB largest=%lu->%lu KB | "
      "LVGL=%lu->%lu KB | root-children=%lu->%lu\n",
      static_cast<unsigned>(victim_id),
      static_cast<unsigned>(requested_folder_id),
      for_preload ? "preload" : "click",
      static_cast<unsigned long>(millis() - started_ms),
      static_cast<unsigned>(resident_before),
      static_cast<unsigned>(resident_folder_cache_count()),
      static_cast<unsigned long>(int_before / 1024),
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
      static_cast<unsigned long>(largest_before / 1024),
      static_cast<unsigned long>(
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
      static_cast<unsigned long>(lvgl_used_before / 1024),
      static_cast<unsigned long>(lvgl_used_after / 1024),
      static_cast<unsigned long>(children_before),
      static_cast<unsigned long>(g_tiles_roots[0]
                                     ? lv_obj_get_child_count(g_tiles_roots[0])
                                     : 0));
  return true;
}

static bool rebuild_folder_cache_index() {
  if (!ensure_folder_cache_storage()) return false;
  for (size_t i = 0; i < g_folder_cache_slot_count; ++i) {
    reset_cache_entry(g_folder_cache[i]);
  }
  return true;
}

static void snapshot_active_cache() {
  if (!g_active_cache) return;
  tile_renderer_snapshot_tab0(&g_active_cache->widgets);
  memcpy(g_active_cache->tile_objs, g_tiles_objs[static_cast<uint8_t>(GridType::TAB0)], sizeof(g_active_cache->tile_objs));
  g_active_cache->widgets_valid = true;
  g_active_cache->grid = g_tiles_grids[static_cast<uint8_t>(GridType::TAB0)];
  g_active_cache->loaded = (g_active_cache->grid != nullptr) && g_tiles_loaded[static_cast<uint8_t>(GridType::TAB0)];
  g_active_cache->grid_config = tileConfig.getActiveGrid();
  g_active_cache->grid_loaded = true;
  g_active_cache->last_used_ms = millis();
}

static void restore_active_cache(FolderCacheEntry& entry) {
  const uint8_t idx = static_cast<uint8_t>(GridType::TAB0);
  g_active_cache = &entry;
  g_tiles_grids[idx] = entry.grid;
  g_tiles_loaded[idx] = entry.loaded && (entry.grid != nullptr);
  memcpy(g_tiles_objs[idx], entry.tile_objs, sizeof(entry.tile_objs));
  if (entry.widgets_valid) {
    tile_renderer_restore_tab0(&entry.widgets);
  }
}

static void build_folder_cache_entry(FolderCacheEntry& entry, GridType grid_type) {
  const uint8_t idx = static_cast<uint8_t>(grid_type);
  if (!g_tiles_roots[idx]) return;
  if (!ensure_cache_build_snapshot()) return;

  // Apply pending live light and Binary Sensor states to the active widget set
  // before the TAB0 pointers are swapped to a hidden folder. Otherwise the
  // full-grid layout generation change below would discard those updates.
  process_switch_update_queue();
  process_binary_sensor_update_queue();
  clear_cache_entry(entry);

  if (!entry.grid_loaded || entry.dirty) {
    if (entry.folder_id == tileConfig.getActiveFolderId()) {
      entry.grid_config = tileConfig.getActiveGrid();
    } else if (!tileConfig.loadFolderGrid(entry.folder_id, entry.grid_config)) {
      Serial.printf("[Tiles] ERROR: Grid configuration for folder=%u not loaded\n",
                    static_cast<unsigned>(entry.folder_id));
      return;
    }
    entry.grid_loaded = true;
  }
  TileGridConfig& config = entry.grid_config;

  *g_cache_build_saved_widgets = {};
  tile_renderer_snapshot_tab0(g_cache_build_saved_widgets);

  entry.grid = create_tiles_grid(g_tiles_roots[idx]);
  if (!entry.grid) {
    tile_renderer_restore_tab0(g_cache_build_saved_widgets);
    return;
  }
  lv_obj_add_flag(entry.grid, LV_OBJ_FLAG_HIDDEN);

  render_tile_grid(entry.grid, config, grid_type, g_tiles_scene_cbs[idx], entry.tile_objs);

  // Warm hidden folder caches with lightweight states only. Media payloads can
  // include cover data and are applied when the folder becomes visible.
  apply_cached_states(grid_type, config, false);
  process_sensor_update_queue();
  process_switch_update_queue();
  process_climate_update_queue();
  process_cover_update_queue();
  process_binary_sensor_update_queue();
  process_weather_update_queue();
  lv_obj_update_layout(entry.grid);

  tile_renderer_snapshot_tab0(&entry.widgets);
  entry.widgets_valid = true;
  tile_renderer_restore_tab0(g_cache_build_saved_widgets);

  entry.loaded = true;
  entry.dirty = false;
  entry.icon_generation = g_tiles_icon_generation;
  entry.last_used_ms = millis();
}

#if defined(CONFIG_IDF_TARGET_ESP32P4)
static void process_navigation_preload() {
  if (g_navigation_preload_cursor >= g_navigation_preload_target_count ||
      g_folder_switch_pending || powerManager.isInSleep() ||
      is_image_screensaver_visible()) {
    return;
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - g_navigation_preload_not_before_ms) < 0) {
    return;
  }
  if (now - displayManager.getLastActivityTime() < kNavigationPreloadIdleMs) {
    return;
  }

  while (g_navigation_preload_cursor < g_navigation_preload_target_count) {
    const uint16_t folder_id =
        g_navigation_preload_targets[g_navigation_preload_cursor];
    FolderCacheEntry* target = find_folder_cache(folder_id);
    if (target && target->grid && target->loaded && target->widgets_valid &&
        target->grid_loaded && !target->dirty) {
      ++g_navigation_preload_cursor;
      continue;
    }

    if (!target) target = allocate_folder_cache(folder_id);
    if (!target) {
      if (evict_folder_cache_before_build(folder_id, true)) {
        g_navigation_preload_not_before_ms =
            millis() + kNavigationPreloadStepGapMs;
        return;
      }
      Serial.printf("[Tiles] nav-preload no slot for folder=%u\n",
                    static_cast<unsigned>(folder_id));
      // Later targets are lower priority and must not displace anything that
      // was kept for this one.
      g_navigation_preload_cursor = g_navigation_preload_target_count;
      return;
    }

    if (!target->grid && folder_cache_requires_eviction_before_build()) {
      if (evict_folder_cache_before_build(folder_id, true)) {
        g_navigation_preload_not_before_ms =
            millis() + kNavigationPreloadStepGapMs;
        return;
      }
      Serial.printf(
          "[Tiles] nav-preload stopped for folder=%u: no safe victim\n",
          static_cast<unsigned>(folder_id));
      g_navigation_preload_cursor = g_navigation_preload_target_count;
      return;
    }

    const FolderCacheMemorySnapshot memory_before =
        folder_cache_memory_snapshot();
    const uint32_t started_ms = millis();
    build_folder_cache_entry(*target, GridType::TAB0);
    const FolderCacheMemorySnapshot memory_after =
        folder_cache_memory_snapshot();
    observe_folder_cache_build_cost(memory_before, memory_after);
    if (target->grid && target->loaded && target->widgets_valid) {
      if (!folder_cache_post_build_reserve_ok(memory_after)) {
        Serial.printf(
            "[Tiles] nav-preload rollback folder=%u: reserve below threshold | "
            "int=%lu KB dma=%lu KB largest_dma=%lu KB psram=%lu KB\n",
            static_cast<unsigned>(folder_id),
            static_cast<unsigned long>(memory_after.internal_free / 1024),
            static_cast<unsigned long>(memory_after.dma_free / 1024),
            static_cast<unsigned long>(memory_after.dma_largest / 1024),
            static_cast<unsigned long>(memory_after.psram_free / 1024));
        reset_cache_entry(*target);
        g_navigation_preload_cursor = g_navigation_preload_target_count;
        return;
      }
      Serial.printf(
          "[Tiles] nav-preload built folder=%u in %lu ms | resident=%u/%u "
          "int=%lu KB dma=%lu KB largest_dma=%lu KB cost=%lu/%lu KB\n",
          static_cast<unsigned>(folder_id),
          static_cast<unsigned long>(millis() - started_ms),
          static_cast<unsigned>(resident_folder_cache_count()),
          static_cast<unsigned>(g_folder_cache_slot_count),
          static_cast<unsigned long>(memory_after.internal_free / 1024),
          static_cast<unsigned long>(memory_after.dma_free / 1024),
          static_cast<unsigned long>(memory_after.dma_largest / 1024),
          static_cast<unsigned long>(
              (memory_before.internal_free > memory_after.internal_free
                   ? memory_before.internal_free - memory_after.internal_free
                   : 0) /
              1024),
          static_cast<unsigned long>(
              (memory_before.dma_free > memory_after.dma_free
                   ? memory_before.dma_free - memory_after.dma_free
                   : 0) /
              1024));
    } else {
      Serial.printf("[Tiles] nav-preload build failed for folder=%u\n",
                    static_cast<unsigned>(folder_id));
      reset_cache_entry(*target);
    }
    ++g_navigation_preload_cursor;
    g_navigation_preload_not_before_ms =
        millis() + kNavigationPreloadStepGapMs;
    return;  // At most one expensive hidden-grid build per loop iteration.
  }
}
#endif

/* === Create tiles grid === */
static lv_obj_t* create_tiles_grid(lv_obj_t* parent) {
  if (!parent) return nullptr;
  lv_obj_t* grid = lv_obj_create(parent);
  lv_obj_set_style_bg_color(grid, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, GRID_PAD_PX, 0);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_column(grid, GAP, 0);
  lv_obj_set_style_pad_row(grid, GAP, 0);

  static lv_coord_t col_dsc[GRID_COLS + 1];
  static lv_coord_t row_dsc[GRID_ROWS + 1];
  static bool dsc_ready = false;
  if (!dsc_ready) {
    build_grid_track_descriptors(col_dsc, GRID_COLS, GRID_CELL_W);
    build_grid_track_descriptors(row_dsc, GRID_ROWS, GRID_CELL_H);
    dsc_ready = true;
  }
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);
  lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
  return grid;
}

// Hidden cache builds omit Media until its widgets become visible.
static inline void enqueue_cached_tile_state(GridType grid_type, const Tile& tile,
                                             uint8_t index, bool include_media) {
  if (!tileTypeUsesCachedEntityState(tile.type)) return;
  if (!include_media && tile.type == TILE_MEDIA) return;
  if (tile.sensor_entity.length() == 0) return;
  if (tileTypeIsEditableValue(tile.type)) { refresh_editable_tile(grid_type, index); return; }

  String payload;
  if (!get_cached_or_initial_payload(tile, payload)) return;

  if (tile.type == TILE_SENSOR || tile.type == TILE_ENERGY) {
    String unit = resolve_tile_sensor_unit(tile);
    const char* unit_cstr = unit.length() > 0 ? unit.c_str() : nullptr;
    queue_sensor_tile_update(grid_type, index, payload.c_str(), unit_cstr);
  } else if (tile.type == TILE_SWITCH) {
    queue_switch_tile_update(grid_type, index, payload.c_str());
  } else if (tile.type == TILE_WEATHER) {
    queue_weather_tile_update(grid_type, index, payload.c_str());
  } else if (tile.type == TILE_MEDIA) {
    queue_media_tile_update(grid_type, index, payload.c_str());
  } else if (tile.type == TILE_CLIMATE) {
    queue_climate_tile_update(grid_type, index, payload.c_str());
  } else if (tile.type == TILE_COVER) {
    queue_cover_tile_update(grid_type, index, payload.c_str());
  } else if (tile.type == TILE_BINARY_SENSOR) {
    queue_binary_sensor_tile_update(grid_type, index, payload.c_str());
  }
}

static void apply_cached_states(GridType grid_type, const TileGridConfig& config, bool include_media) {
  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    enqueue_cached_tile_state(grid_type, config.tiles[i], i, include_media);
  }
}

void tiles_refresh_visible_from_cache() {
  g_visible_cache_refresh_requested = false;
  const GridType grid_type = GridType::TAB0;
  const uint8_t idx = static_cast<uint8_t>(grid_type);
  if (idx >= 3 || !g_tiles_grids[idx] || !g_tiles_loaded[idx]) return;

  const TileGridConfig& config = getGridConfig(grid_type);
  // Include media as on folder navigation below, using the same cache
  // application and queue processing. Previously, false skipped media tiles
  // until the normal loop after waking, causing a visible delayed update
  // that this cache refresh should complete before the display wakes.
  apply_cached_states(grid_type, config, true);
  // update_sensor_tile_value()/update_switch_tile_state()/update_weather_tile_state()
  // (called from these three queues) all update a specific tile's widgets via
  // lv_label_set_text()/lv_arc_set_value()/etc., which already mark just that
  // widget's own small area dirty -- no other code in tile_renderer.cpp calls
  // lv_obj_invalidate() directly. The blanket lv_obj_invalidate(whole grid)
  // that used to sit here forced a full 35-tile redraw+flush on every single
  // bridge value update, even when only 1-2 tiles actually changed -- visible
  // as the whole screen "blocking with all the tiles" every time a bridge
  // update landed. Let each widget's own targeted invalidate do its job.
  process_sensor_update_queue();
  process_switch_update_queue();
  process_climate_update_queue();
  process_cover_update_queue();
  process_binary_sensor_update_queue();
  process_weather_update_queue();
  process_media_update_queue();
}

void tiles_request_visible_cache_refresh() {
  g_visible_cache_refresh_requested = true;
}

void tiles_process_visible_cache_refresh(bool allow_now) {
  if (!g_visible_cache_refresh_requested || !allow_now) return;
  tiles_refresh_visible_from_cache();
}

// Time-sliced Bridge cache refresh.
// The entire active-grid/all-folder refresh previously ran in one loop
// pass, measured at 671-733 ms continuously in the bridge_cache [LoopGap]
// bucket and visibly slowing 30 fps animations. Now each pass processes
// one step: the active grid first, then one background folder per pass.
// lv_timer_handler() renders normally between steps.
// Folders use TileConfig's PSRAM entity cache, so flash reads (~20 ms per
// folder) occur only on the first refresh or after grid changes.
// Timing counters: work = active processing across all steps; wall =
// elapsed start-to-finish time; load = slot acquisition from cache/flash;
// lookup = index lookups; store = entity-cache upsert, including the
// 280-slot linear scan in cache_entity_payload_at suspected of the
// previously unexplained ~280 ms remainder; other = remaining work.
static bool g_bridge_cache_refresh_active = false;
static size_t g_bridge_cache_refresh_step = 0;
static uint32_t g_bridge_cache_active_snapshot_ms = 0;
static constexpr size_t kBridgeRefreshMaxFolders = 64;
static uint16_t g_bridge_cache_folder_ids[kBridgeRefreshMaxFolders];
static size_t g_bridge_cache_folder_count = 0;
static uint32_t g_bridge_cache_wall_start_ms = 0;
static uint32_t g_bridge_cache_work_ms = 0;
static uint32_t g_bridge_cache_load_ms = 0;
static uint32_t g_bridge_cache_lookup_ms = 0;
static uint32_t g_bridge_cache_store_ms = 0;

static void refresh_cache_from_grid_config(const TileGridConfig& config, uint32_t snapshot_ms) {
  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = config.tiles[i];
    if (!tileTypeUsesCachedEntityState(tile.type)) continue;
    if (!tile.sensor_entity.length()) continue;

    uint32_t t_lookup0 = millis();
    String payload = haBridgeConfig.findSensorInitialValue(tile.sensor_entity);
    uint32_t t_store0 = millis();
    g_bridge_cache_lookup_ms += t_store0 - t_lookup0;
    if (!payload.length()) continue;
    cache_entity_payload_from_bridge(tile.sensor_entity.c_str(), payload.c_str(), snapshot_ms);
    g_bridge_cache_store_ms += millis() - t_store0;
  }
}

// Like refresh_cache_from_grid_config(), using the PSRAM cache view for
// background folders: type and entity ID only, without Arduino Strings.
static void refresh_cache_from_entity_views(const FolderEntitySlotView* slots, size_t count, uint32_t snapshot_ms) {
  for (size_t i = 0; i < count; ++i) {
    const FolderEntitySlotView& slot = slots[i];
    if (!tileTypeUsesCachedEntityState(slot.type)) continue;
    if (!slot.entity[0]) continue;

    uint32_t t_lookup0 = millis();
    String payload = haBridgeConfig.findSensorInitialValue(slot.entity);
    uint32_t t_store0 = millis();
    g_bridge_cache_lookup_ms += t_store0 - t_lookup0;
    if (!payload.length()) continue;
    cache_entity_payload_from_bridge(slot.entity, payload.c_str(), snapshot_ms);
    g_bridge_cache_store_ms += millis() - t_store0;
  }
}

static void bridge_cache_refresh_begin() {
  g_bridge_cache_refresh_active = true;
  g_bridge_cache_refresh_step = 0;
  g_bridge_cache_active_snapshot_ms =
      g_bridge_cache_refresh_snapshot_ms ? g_bridge_cache_refresh_snapshot_ms : millis();
  g_bridge_cache_wall_start_ms = millis();
  g_bridge_cache_work_ms = 0;
  g_bridge_cache_load_ms = 0;
  g_bridge_cache_lookup_ms = 0;
  g_bridge_cache_store_ms = 0;

  // Snapshot folder IDs because refresh spans several loop passes, during
  // which Web operations may add/delete folders and rebuild getFolders()'s
  // vector. Iterate IDs rather than live iterators. A folder deleted in
  // the meantime is skipped by the cache access folderExists check.
  g_bridge_cache_folder_count = 0;
  const uint16_t active_id = tileConfig.getActiveFolderId();
  for (const auto& folder : tileConfig.getFolders()) {
    if (folder.id == active_id) continue;
    if (g_bridge_cache_folder_count >= kBridgeRefreshMaxFolders) break;
    g_bridge_cache_folder_ids[g_bridge_cache_folder_count++] = folder.id;
  }
}

static void bridge_cache_refresh_finish() {
  g_bridge_cache_refresh_active = false;
  // Reset snapshot time only if no newer request, carrying a fresh
  // timestamp, arrived during processing.
  if (!g_bridge_cache_refresh_requested) {
    g_bridge_cache_refresh_snapshot_ms = 0;
  }

  const uint32_t accounted = g_bridge_cache_load_ms + g_bridge_cache_lookup_ms + g_bridge_cache_store_ms;
  const uint32_t other = (g_bridge_cache_work_ms > accounted) ? (g_bridge_cache_work_ms - accounted) : 0;
  Serial.printf("[Bridge] cache refresh split: work=%ums wall=%ums steps=%u load=%ums lookup=%ums store=%ums other=%ums\n",
                (unsigned)g_bridge_cache_work_ms,
                (unsigned)(millis() - g_bridge_cache_wall_start_ms),
                (unsigned)(g_bridge_cache_folder_count + 1),
                (unsigned)g_bridge_cache_load_ms,
                (unsigned)g_bridge_cache_lookup_ms,
                (unsigned)g_bridge_cache_store_ms,
                (unsigned)other);
  tiles_request_visible_cache_refresh();
}

void tiles_request_bridge_cache_refresh() {
  g_bridge_cache_refresh_requested = true;
  g_bridge_cache_refresh_snapshot_ms = millis();
}

void tiles_process_bridge_cache_refresh(bool allow_now) {
  if (!allow_now) return;
  if (!g_bridge_cache_refresh_active) {
    if (!g_bridge_cache_refresh_requested) return;
    g_bridge_cache_refresh_requested = false;
    bridge_cache_refresh_begin();
  }

  const uint32_t t_step0 = millis();
  if (g_bridge_cache_refresh_step == 0) {
    refresh_cache_from_grid_config(tileConfig.getActiveGrid(), g_bridge_cache_active_snapshot_ms);
  } else {
    const size_t folder_idx = g_bridge_cache_refresh_step - 1;
    if (folder_idx < g_bridge_cache_folder_count) {
      const uint16_t folder_id = g_bridge_cache_folder_ids[folder_idx];
      FolderEntitySlotView slots[TILES_PER_GRID];
      uint32_t t_load0 = millis();
      const bool loaded = tileConfig.getFolderEntitiesCached(folder_id, slots, TILES_PER_GRID);
      g_bridge_cache_load_ms += millis() - t_load0;
      if (loaded) {
        refresh_cache_from_entity_views(slots, TILES_PER_GRID, g_bridge_cache_active_snapshot_ms);
      }
    }
  }
  g_bridge_cache_work_ms += millis() - t_step0;
  ++g_bridge_cache_refresh_step;

  if (g_bridge_cache_refresh_step > g_bridge_cache_folder_count) {
    bridge_cache_refresh_finish();
  }
}

static void apply_cached_state_for_index(GridType grid_type, const TileGridConfig& config, uint8_t index) {
  if (index >= TILES_PER_GRID) return;
  enqueue_cached_tile_state(grid_type, config.tiles[index], index, true);
}

// Build the unified tiles tab.
void build_tiles_tab(lv_obj_t *parent, GridType grid_type, scene_publish_cb_t scene_cb) {
  uint8_t idx = (uint8_t)grid_type;
  if (!tile_renderer_init_cold_storage()) {
    Serial.println("[Tiles/Mem] ERROR: Renderer state unavailable");
    return;
  }
  if (grid_type == GridType::TAB0) {
    // Allocate before folder grids so PSRAM use and the freed internal bytes are
    // deterministic in the cache-build measurements below.
    ensure_entity_cache_storage();
  }
  g_tiles_scene_cbs[idx] = scene_cb;

  lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_set_scroll_dir(parent, LV_DIR_VER);
  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_anim_duration(parent, 0, 0);
  lv_obj_set_style_pad_all(parent, OUTER, 0);

  g_tiles_roots[idx] = parent;
  g_tiles_grids[idx] = nullptr;
  g_tiles_loaded[idx] = false;

  if (grid_type == GridType::TAB0) {
    const uint32_t preload_heap_before = ESP.getFreeHeap();
    const uint32_t preload_psram_before = ESP.getFreePsram();
    size_t preloaded_folder_count = 0;
    if (!rebuild_folder_cache_index()) return;
    g_active_cache = allocate_folder_cache(tileConfig.getActiveFolderId());
    if (g_active_cache) {
      build_folder_cache_entry(*g_active_cache, grid_type);
      restore_active_cache(*g_active_cache);
      if (g_tiles_grids[idx]) {
        lv_obj_clear_flag(g_tiles_grids[idx], LV_OBJ_FLAG_HIDDEN);
        apply_cached_states(grid_type, g_active_cache->grid_config);
        process_sensor_update_queue();
        process_switch_update_queue();
        process_climate_update_queue();
        process_cover_update_queue();
        process_binary_sensor_update_queue();
        process_weather_update_queue();
        process_media_update_queue();
        g_active_cache->last_used_ms = millis();
        preloaded_folder_count = 1;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
        schedule_navigation_preload(g_active_cache->folder_id,
                                    tileConfig.getActiveGrid(),
                                    kNavigationPreloadInitialDelayMs);
#endif
      }
    }
#if !defined(CONFIG_IDF_TARGET_ESP32P4)
    for (const auto& folder : tileConfig.getFolders()) {
      if (g_active_cache && folder.id == g_active_cache->folder_id) continue;
      // UI setup runs before network/SDIO/MQTT allocations. Preload only the
      // guaranteed working set; slot four is admitted later from a real
      // runtime heap measurement on the first miss.
      if (resident_folder_cache_count() >= kMinResidentFolderUiCaches) {
        Serial.printf("[Tiles] TAB0 folder preload capped at %u resident grids\n",
                      static_cast<unsigned>(kMinResidentFolderUiCaches));
        break;
      }
      if (!can_preload_more_folders()) {
        Serial.printf("[Tiles] TAB0 folder preload stopped: heap=%lu KB, psram=%lu KB\n",
                      ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
        break;
      }
      FolderCacheEntry* entry = allocate_folder_cache(folder.id);
      if (!entry) break;
      build_folder_cache_entry(*entry, grid_type);
      if (entry->loaded) {
        ++preloaded_folder_count;
      }
    }
#endif
    Serial.printf("[Tiles] TAB0 folder preload cached %u/%u folders (heap %lu -> %lu KB, psram %lu -> %lu KB)\n",
                  static_cast<unsigned>(preloaded_folder_count),
                  static_cast<unsigned>(tileConfig.getFolders().size()),
                  preload_heap_before / 1024, ESP.getFreeHeap() / 1024,
                  preload_psram_before / 1024, ESP.getFreePsram() / 1024);
    if (g_tiles_grids[idx]) {
      schedule_preview_load(grid_type);
    }
  } else {
    g_tiles_grids[idx] = create_tiles_grid(parent);
    g_tiles_loaded[idx] = (g_tiles_grids[idx] != nullptr);
    if (g_tiles_grids[idx]) {
      tiles_reload_layout(grid_type);
    }
  }
}

/* === Reload layout (unified) === */
void tiles_reload_layout(GridType grid_type) {
  // A light popup is bound to a concrete grid slot. Close it before replacing
  // that slot so later entity updates cannot target a stale widget binding.
  hide_light_popup();
  uint8_t idx = (uint8_t)grid_type;
  if (grid_type == GridType::TAB0 && g_active_cache) {
    g_tiles_grids[idx] = g_active_cache->grid;
  }
  if (!g_tiles_grids[idx]) return;

  // displayManager.debugFlushNext(40);

  lv_display_t* disp = lv_obj_get_display(g_tiles_grids[idx]);
  if (disp) {
    lv_display_enable_invalidation(disp, false);
  }

  reset_sensor_widgets(grid_type);
  reset_switch_widgets(grid_type);
  reset_climate_widgets(grid_type);
  reset_cover_widgets(grid_type);
  reset_binary_sensor_widgets(grid_type);
  reset_weather_widgets(grid_type);
  reset_weather_widgets(grid_type);
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    g_tiles_objs[idx][i] = nullptr;
  }
  lv_obj_clean(g_tiles_grids[idx]);

  const TileGridConfig& config = getGridConfig(grid_type);
  bool occupied[GRID_ROWS][GRID_COLS] = {};
  struct TileLayout {
    uint8_t col = 0;
    uint8_t row = 0;
    uint8_t span_w = 1;
    uint8_t span_h = 1;
    bool valid = false;
  };
  TileLayout layouts[TILES_PER_GRID]{};

  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = config.tiles[i];
    if (tile.type == TILE_EMPTY) continue;
    uint8_t col = 0;
    uint8_t row = 0;
    uint8_t span_w = 1;
    uint8_t span_h = 1;
    if (!get_tile_layout(tile, col, row, span_w, span_h)) continue;
    layouts[i] = {col, row, span_w, span_h, true};
    mark_occupied(occupied, col, row, span_w, span_h);
  }

  for (uint8_t r = 0; r < GRID_ROWS; ++r) {
    for (uint8_t c = 0; c < GRID_COLS; ++c) {
      if (!occupied[r][c]) {
        render_empty_tile(g_tiles_grids[idx], c, r);
      }
    }
    yield();
  }

  size_t render_count = 0;
  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    if (!layouts[i].valid) continue;
    Tile layout_tile = config.tiles[i];
    layout_tile.col = layouts[i].col;
    layout_tile.row = layouts[i].row;
    layout_tile.span_w = layouts[i].span_w;
    layout_tile.span_h = layouts[i].span_h;
    g_tiles_objs[idx][i] = render_tile(g_tiles_grids[idx], layouts[i].col, layouts[i].row, layout_tile, i, grid_type, g_tiles_scene_cbs[idx]);
    // Yield after every tile instead of only every GRID_COLS tiles. A full
    // reload previously blocked for more than 200 ms in the worst case
    // (LoopGap), overflowing the SDIO receive queue under Wi-Fi load.
    // Frequent short yields let the Wi-Fi task run without noticeably
    // slowing reload. Keep delay(1) less frequent to avoid unnecessary delay.
    yield();
    if ((++render_count % GRID_COLS) == 0) {
      delay(1);
    }
  }

  g_tiles_loaded[idx] = true;
  apply_cached_states(grid_type, config);
  process_sensor_update_queue();
  process_switch_update_queue();
  process_climate_update_queue();
  process_cover_update_queue();
  process_binary_sensor_update_queue();
  process_weather_update_queue();
  process_media_update_queue();
  if (disp) {
    lv_display_enable_invalidation(disp, true);
    lv_obj_invalidate(g_tiles_grids[idx]);
    lv_refr_now(disp);
  }
  if (grid_type == GridType::TAB0 && g_active_cache) {
    g_active_cache->grid = g_tiles_grids[idx];
    g_active_cache->loaded = g_tiles_loaded[idx];
    g_active_cache->grid_config = tileConfig.getActiveGrid();
    g_active_cache->grid_loaded = true;
    tile_renderer_snapshot_tab0(&g_active_cache->widgets);
    memcpy(g_active_cache->tile_objs, g_tiles_objs[idx], sizeof(g_active_cache->tile_objs));
    g_active_cache->widgets_valid = true;
    g_active_cache->dirty = false;
    g_active_cache->icon_generation = g_tiles_icon_generation;
    g_active_cache->last_used_ms = millis();
  }
  Serial.printf("[%s] Layout reloaded\n", getGridName(grid_type));
  schedule_preview_load(grid_type);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (grid_type == GridType::TAB0 && g_active_cache) {
    schedule_navigation_preload(g_active_cache->folder_id,
                                tileConfig.getActiveGrid());
  }
#endif
}

void tiles_release_layout(GridType grid_type) {
  uint8_t idx = (uint8_t)grid_type;
  if (!g_tiles_grids[idx] || !g_tiles_loaded[idx]) return;

  if (grid_type == GridType::TAB0 && g_active_cache) {
    reset_media_widgets(grid_type);
    reset_climate_widgets(grid_type);
    reset_cover_widgets(grid_type);
    reset_binary_sensor_widgets(grid_type);
    clear_cache_entry(*g_active_cache);
    g_active_cache = nullptr;
    g_tiles_grids[idx] = nullptr;
    g_tiles_loaded[idx] = false;
    return;
  }

  reset_sensor_widgets(grid_type);
  reset_switch_widgets(grid_type);
  reset_climate_widgets(grid_type);
  reset_cover_widgets(grid_type);
  reset_binary_sensor_widgets(grid_type);
  reset_media_widgets(grid_type);
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    g_tiles_objs[idx][i] = nullptr;
  }
  lv_obj_clean(g_tiles_grids[idx]);
  g_tiles_loaded[idx] = false;

  Serial.printf("[%s] Layout released\n", getGridName(grid_type));
}

void tiles_release_all() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  clear_navigation_preload_plan();
#endif
  // Invalidate queued TAB0 Binary Sensor work and clear its LVGL bindings
  // before the folder-cache grids that own those objects are deleted.
  reset_binary_sensor_widgets(GridType::TAB0);
  for (size_t i = 0; i < g_folder_cache_slot_count; ++i) {
    reset_cache_entry(g_folder_cache[i]);
  }
  g_active_cache = nullptr;
  g_tiles_grids[0] = nullptr;
  g_tiles_loaded[0] = false;
  tiles_release_layout(GridType::TAB0);
  tiles_release_layout(GridType::TAB1);
  tiles_release_layout(GridType::TAB2);
}

bool tiles_is_loaded(GridType grid_type) {
  uint8_t idx = (uint8_t)grid_type;
  if (idx >= 3) return false;
  return g_tiles_loaded[idx];
}

void tiles_request_reload(GridType grid_type) {
  uint8_t idx = (uint8_t)grid_type;
  if (idx >= 3) return;
  g_tiles_reload_requested[idx] = true;
  g_tiles_reload_only_if_loaded[idx] = false;
}

void tiles_request_reload_if_loaded(GridType grid_type) {
  uint8_t idx = (uint8_t)grid_type;
  if (idx >= 3) return;
  if (!g_tiles_reload_requested[idx]) {
    g_tiles_reload_only_if_loaded[idx] = true;
  }
  g_tiles_reload_requested[idx] = true;
}

void tiles_request_reload_all() {
  for (uint8_t i = 0; i < 3; ++i) {
    if (!g_tiles_reload_requested[i]) {
      g_tiles_reload_only_if_loaded[i] = true;
    }
    g_tiles_reload_requested[i] = true;
  }
}

void tiles_request_icon_refresh() {
  ++g_tiles_icon_generation;
  if (g_tiles_icon_generation == 0) g_tiles_icon_generation = 1;
  g_tiles_icon_refresh_requested = true;
}

void tiles_request_release(GridType grid_type) {
  uint8_t idx = (uint8_t)grid_type;
  if (idx >= 3) return;
  g_tiles_release_requested[idx] = true;
}

void tiles_request_release_all() {
  for (uint8_t i = 0; i < 3; ++i) {
    g_tiles_release_requested[i] = true;
  }
}

void tiles_switch_to_folder(uint16_t folder_id) {
  viewNavigationClosePopups();
  const uint8_t idx = static_cast<uint8_t>(GridType::TAB0);
  if (!g_tiles_roots[idx] || !tileConfig.folderExists(folder_id)) {
    uiManager.finishFolderSwitch(folder_id, false);
    return;
  }

  // Do not block an LVGL event callback while switching.
  stop_preview_timer();
  g_pending_folder_id = folder_id;
  g_folder_switch_pending = true;
}

void tiles_cancel_folder_switch(uint16_t folder_id) {
  if (!g_folder_switch_pending || g_pending_folder_id != folder_id) return;
  g_folder_switch_pending = false;
  g_pending_folder_id = kInvalidFolderId;
}

void tiles_invalidate_folder(uint16_t folder_id) {
  // Called from the web-server task. Do NOT touch g_folder_cache or LVGL here:
  // iterating the cache and deleting LVGL objects would race the render loop
  // (and previously crashed). Just flag it; the loop drops the stale caches in
  // process_folder_cache_invalidation(). Coarse (all non-active caches) is fine
  // -- they simply reload from NVS the next time they are opened.
  (void)folder_id;
  g_folder_cache_invalidate_requested = true;
}

// Loop-only: actually drop the cached folder grids so they rebuild from NVS.
static void process_folder_cache_invalidation() {
  if (!g_folder_cache_invalidate_requested) return;
  g_folder_cache_invalidate_requested = false;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  clear_navigation_preload_plan();
#endif
  for (size_t i = 0; i < g_folder_cache_slot_count; ++i) {
    FolderCacheEntry& entry = g_folder_cache[i];
    entry.dirty = true;
    entry.grid_loaded = false;
    if (&entry != g_active_cache) {
      reset_cache_entry(entry);
    }
  }
}

void tiles_process_reload_requests() {
  ui_surface_style::process_pending_updates();
  bool did_reload = false;

  process_folder_cache_invalidation();

  if (g_folder_switch_pending) {
    const uint16_t folder_id = g_pending_folder_id;
    const uint8_t idx = static_cast<uint8_t>(GridType::TAB0);

    if (!g_tiles_roots[idx] || !tileConfig.folderExists(folder_id)) {
      g_folder_switch_pending = false;
      g_pending_folder_id = kInvalidFolderId;
      uiManager.finishFolderSwitch(folder_id, false);
      return;
    }

    if (g_active_cache && g_active_cache->folder_id == folder_id &&
        g_active_cache->grid) {
      g_folder_switch_pending = false;
      g_pending_folder_id = kInvalidFolderId;
      uiManager.finishFolderSwitch(folder_id, true);
      return;
    }

    if (g_tiles_roots[idx]) {
      FolderCacheEntry* target = find_folder_cache(folder_id);
      if (!target) {
        target = allocate_folder_cache(folder_id);
      }
      if (!target) {
        if (g_folder_cache_slot_count == 1 && g_active_cache &&
            g_active_cache->grid) {
          // Last-resort mode for a device where only one internal slot could
          // be allocated: reuse the visible grid in place. There is no hidden
          // victim to evict because the sole slot is necessarily active.
          g_folder_switch_pending = false;
          g_pending_folder_id = kInvalidFolderId;
          if (!tileConfig.setActiveFolder(folder_id)) {
            uiManager.finishFolderSwitch(folder_id, false);
            return;
          }
          g_active_cache->folder_id = folder_id;
          g_active_cache->grid_config = tileConfig.getActiveGrid();
          g_active_cache->grid_loaded = true;
          g_active_cache->dirty = false;
          tiles_reload_layout(GridType::TAB0);
          log_folder_switch_memory("folder-switch-single-slot", folder_id);
          uiManager.finishFolderSwitch(folder_id, true);
          return;
        }
        if (evict_folder_cache_before_build(folder_id)) {
          // Keep the request pending and build on the next loop iteration. This
          // avoids combining a full LVGL delete and a full grid render in one
          // long input-service gap.
          return;
        }
        g_folder_switch_pending = false;
        g_pending_folder_id = kInvalidFolderId;
        Serial.printf("[Tiles] ERROR: No cache slot for folder=%u\n",
                      static_cast<unsigned>(folder_id));
        uiManager.finishFolderSwitch(folder_id, false);
        return;
      }

      FolderCacheEntry* previous = g_active_cache;
      const bool can_reuse = target->grid && target->loaded && target->widgets_valid &&
                             target->grid_loaded && !target->dirty;

      if (!can_reuse && !target->grid &&
          folder_cache_requires_eviction_before_build() &&
          evict_folder_cache_before_build(folder_id)) {
        // Delete and render stay in separate loop iterations.
        return;
      }

      g_folder_switch_pending = false;
      g_pending_folder_id = kInvalidFolderId;
      snapshot_active_cache();

      if (can_reuse) {
        if (!tileConfig.setActiveFolderCached(folder_id, target->grid_config)) {
          if (target != previous) reset_cache_entry(*target);
          if (previous && previous->grid) {
            restore_active_cache(*previous);
            lv_obj_clear_flag(previous->grid, LV_OBJ_FLAG_HIDDEN);
          }
          uiManager.finishFolderSwitch(folder_id, false);
          return;
        }
        restore_active_cache(*target);
        // Restored widgets carry their old payload hash; without clearing it the
        // weather tile's hash short-circuit skips the re-apply and can stay blank.
        tile_renderer_invalidate_weather_payload(GridType::TAB0);
        // Hidden folder grids may have been built before Home Assistant sent
        // its icon metadata. Refresh a stale cache while it is still hidden so
        // automatically resolved light/switch icons are present on first draw.
        refresh_active_folder_icons_if_stale(*target);
        apply_cached_states(GridType::TAB0, target->grid_config);
        process_sensor_update_queue();
        process_switch_update_queue();
        process_climate_update_queue();
        process_cover_update_queue();
        process_binary_sensor_update_queue();
        process_weather_update_queue();
        process_media_update_queue();
        if (previous && previous != target && previous->grid) {
          lv_obj_add_flag(previous->grid, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(target->grid, LV_OBJ_FLAG_HIDDEN);
        // The regular path is followed by the normal LVGL refresh. The
        // post-input fast path refreshes once after this function returns, so
        // never nest a full-screen refresh inside the cache transaction.
        lv_obj_invalidate(target->grid);
        target->last_used_ms = millis();
        schedule_preview_load(GridType::TAB0);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
        schedule_navigation_preload(folder_id, tileConfig.getActiveGrid());
#endif
        log_folder_switch_memory("folder-switch-cached", folder_id);
        uiManager.finishFolderSwitch(folder_id, true);
        return;
      }

      build_folder_cache_entry(*target, GridType::TAB0);
      if (!target->grid || !target->loaded || !target->widgets_valid) {
        if (previous && previous->grid) {
          restore_active_cache(*previous);
          lv_obj_clear_flag(previous->grid, LV_OBJ_FLAG_HIDDEN);
        }
        uiManager.finishFolderSwitch(folder_id, false);
        return;
      }
      if (!tileConfig.setActiveFolderCached(folder_id, target->grid_config)) {
        if (target != previous) reset_cache_entry(*target);
        if (previous && previous->grid) {
          restore_active_cache(*previous);
          lv_obj_clear_flag(previous->grid, LV_OBJ_FLAG_HIDDEN);
        }
        uiManager.finishFolderSwitch(folder_id, false);
        return;
      }

      restore_active_cache(*target);
      if (target->grid) {
        tile_renderer_invalidate_weather_payload(GridType::TAB0);
        refresh_active_folder_icons_if_stale(*target);
        apply_cached_states(GridType::TAB0, target->grid_config);
        process_sensor_update_queue();
        process_switch_update_queue();
        process_climate_update_queue();
        process_cover_update_queue();
        process_binary_sensor_update_queue();
        process_weather_update_queue();
        process_media_update_queue();
        if (previous && previous != target && previous->grid) {
          lv_obj_add_flag(previous->grid, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(target->grid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(target->grid);
        target->last_used_ms = millis();
        schedule_preview_load(GridType::TAB0);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
        schedule_navigation_preload(folder_id, tileConfig.getActiveGrid());
#endif
        log_folder_switch_memory("folder-switch-built", folder_id);
        uiManager.finishFolderSwitch(folder_id, true);
      } else if (previous && previous->grid) {
        restore_active_cache(*previous);
        lv_obj_clear_flag(previous->grid, LV_OBJ_FLAG_HIDDEN);
        uiManager.finishFolderSwitch(folder_id, false);
      }
    }
    return;
  }

  for (uint8_t i = 0; i < 3; ++i) {
    if (!g_tiles_release_requested[i]) continue;
    g_tiles_release_requested[i] = false;
    GridType grid_type = static_cast<GridType>(i);
    if (g_tiles_grids[i] && g_tiles_loaded[i]) {
      tiles_release_layout(grid_type);
    }
    return;  // Release at most one grid per loop pass.
  }

  if (webAdminRecentlyActive(1000)) {
    return;
  }

  for (uint8_t i = 0; i < 3; ++i) {
    if (!g_tiles_reload_requested[i]) continue;
    GridType grid_type = static_cast<GridType>(i);
    bool only_if_loaded = g_tiles_reload_only_if_loaded[i];
    if (only_if_loaded && !g_tiles_loaded[i]) {
      g_tiles_reload_requested[i] = false;
      g_tiles_reload_only_if_loaded[i] = true;
      continue;
    }
    g_tiles_reload_requested[i] = false;
    g_tiles_reload_only_if_loaded[i] = true;
    if (g_tiles_grids[i]) {
      tiles_reload_layout(grid_type);
    }
    did_reload = true;
    break;  // Reload at most one grid per loop pass.
  }

  if (g_tiles_icon_refresh_requested && !did_reload) {
    g_tiles_icon_refresh_requested = false;
    for (uint8_t i = 0; i < 3; ++i) {
      GridType grid_type = static_cast<GridType>(i);
      if (!g_tiles_grids[i] || !g_tiles_loaded[i]) continue;
      if (grid_type == GridType::TAB0 && g_active_cache &&
          g_active_cache->icon_generation == g_tiles_icon_generation) {
        continue;
      }
      tiles_refresh_icons_for_grid(grid_type);
      if (grid_type == GridType::TAB0 && g_active_cache) {
        g_active_cache->icon_generation = g_tiles_icon_generation;
      }
    }
  }

#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!did_reload) process_navigation_preload();
#endif
}

void tiles_process_pending_folder_switch() {
  if (!g_folder_switch_pending) return;

  // Touch callbacks run inside lv_timer_handler(). Commit only after that
  // handler has returned, before unrelated Web/MQTT work can delay navigation.
  tiles_process_reload_requests();
  if (g_folder_switch_pending) return;

  // The cache transaction invalidates the new grid and the PIN completion
  // invalidates its overlay. Flush both together so the old folder is never
  // exposed between a correct PIN and the requested folder.
  if (lv_display_t* display = displayManager.getDisplay()) {
    yield();
    lv_refr_now(display);
    yield();
  }
}

static void tiles_refresh_all_image_previews(GridType grid_type, bool only_missing) {
  uint8_t idx = static_cast<uint8_t>(grid_type);
  if (!g_tiles_grids[idx]) return;
  if (!g_tiles_loaded[idx]) return;
  if (idx < 3) {
    uint32_t now = millis();
    if (g_preview_block_until_ms[idx] != 0 &&
        (int32_t)(now - g_preview_block_until_ms[idx]) < 0) {
      return;
    }
  }

  g_preview_next_index = 0;
  while (!process_preview_step(grid_type, only_missing, TILES_PER_GRID)) {
    // continue until all previews in this grid are processed
  }
}

static bool apply_preview_for_tile(GridType, uint8_t, bool, bool) {
  return false;
}

static bool process_preview_step(GridType grid_type, bool only_missing, uint8_t max_loads) {
  if (max_loads < 1) max_loads = 1;
  uint8_t loaded = 0;
  while (g_preview_next_index < TILES_PER_GRID) {
    if (apply_preview_for_tile(grid_type, g_preview_next_index, only_missing, false)) {
      ++loaded;
      ++g_preview_next_index;
      if (loaded >= max_loads) {
        return false;
      }
      continue;
    }
    ++g_preview_next_index;
  }
  return true;
}

void tiles_refresh_image_previews_for_key(GridType, const String&) {
}

static void hide_preview_images(GridType grid_type) {
  uint8_t idx = static_cast<uint8_t>(grid_type);
  if (!g_tiles_grids[idx]) return;
  if (!g_tiles_loaded[idx]) return;

  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    lv_obj_t* btn = g_tiles_objs[idx][i];
    if (!btn) continue;
    lv_obj_t* img = find_preview_image_child(btn);
    if (!img) continue;
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_USER_2);
  }
}

static void rebuild_tile_at_index(GridType grid_type, uint8_t index) {
  uint8_t idx = static_cast<uint8_t>(grid_type);
  if (!g_tiles_grids[idx] || !g_tiles_loaded[idx]) return;
  if (index >= TILES_PER_GRID) return;

  const TileGridConfig& config = getGridConfig(grid_type);
  const Tile& tile = config.tiles[index];
  if (tile.type == TILE_EMPTY) return;

  uint8_t col = 0;
  uint8_t row = 0;
  uint8_t span_w = 1;
  uint8_t span_h = 1;
  if (!get_tile_layout(tile, col, row, span_w, span_h)) return;

  if (g_tiles_objs[idx][index]) {
    lv_obj_del(g_tiles_objs[idx][index]);
    g_tiles_objs[idx][index] = nullptr;
  }
  reset_sensor_widget(grid_type, index);
  reset_switch_widget(grid_type, index);
  reset_climate_widget(grid_type, index);
  reset_cover_widget(grid_type, index);
  reset_binary_sensor_widget(grid_type, index);
  reset_weather_widget(grid_type, index);

  Tile layout_tile = tile;
  layout_tile.col = col;
  layout_tile.row = row;
  layout_tile.span_w = span_w;
  layout_tile.span_h = span_h;
  g_tiles_objs[idx][index] = render_tile(g_tiles_grids[idx], col, row, layout_tile, index, grid_type, g_tiles_scene_cbs[idx]);

  apply_cached_state_for_index(grid_type, config, index);
}

static void tiles_refresh_icons_for_grid(GridType grid_type) {
  uint8_t idx = static_cast<uint8_t>(grid_type);
  if (!g_tiles_grids[idx] || !g_tiles_loaded[idx]) return;

  const TileGridConfig& config = getGridConfig(grid_type);
  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = config.tiles[i];
    if (!tileTypeRefreshesEntityIcon(tile.type)) continue;
    lv_obj_t* tile_obj = g_tiles_objs[idx][i];
    if (!tile_obj) continue;

    String icon_name = tile.icon_name;
    bool icon_disabled = isMdiIconDisabled(icon_name);
    icon_name = normalizeMdiIconName(icon_name);
    // Climate tiles with an empty icon field use Home Assistant's entity icon
    // as their base and only override it for an active HVAC action. Recompute
    // that visual here as well so a live bridge icon refresh can never hide
    // the Climate label or replace an active flame/snowflake with stale meta.
    if (tile.type == TILE_CLIMATE && !icon_disabled && !icon_name.length()) {
      const String base_icon = climate_tile_base_icon(tile);
      ClimateState* states = tile_renderer_get_climate_states(grid_type);
      icon_name =
          states && states[i].valid
              ? climate_visual_icon(states[i], base_icon)
              : base_icon;
    }
    if (!icon_disabled && !icon_name.length()) {
      if (tile.type == TILE_COVER) {
        CoverState* states = tile_renderer_get_cover_states(grid_type);
        CoverTileWidgets* widgets =
            tile_renderer_get_cover_widgets(grid_type);
        bool dynamic_icon = false;
        icon_name = cover_resolve_icon(
            tile, states ? states[i] : CoverState{}, &dynamic_icon);
        if (widgets) widgets[i].dynamic_icon = dynamic_icon;
      } else if (tile.type == TILE_BINARY_SENSOR) {
        BinarySensorState* states =
            tile_renderer_get_binary_sensor_states(grid_type);
        BinarySensorTileWidgets* widgets =
            tile_renderer_get_binary_sensor_widgets(grid_type);
        bool dynamic_icon = false;
        icon_name = binary_sensor_resolve_icon(
            tile, states ? states[i] : BinarySensorState{}, &dynamic_icon);
        if (widgets) widgets[i].dynamic_icon = dynamic_icon;
      } else if (tile.type == TILE_SCENE) {
        if (tile.scene_alias.length()) {
          String scene_entity = haBridgeConfig.findSceneEntity(tile.scene_alias);
          if (scene_entity.length()) {
            icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(scene_entity));
          }
        }
      } else if (tile.sensor_entity.length()) {
        icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
      }
    }

    if (tile.type == TILE_COVER) {
      refresh_cover_popup_for_tile(grid_type, i);
    }

    String iconChar;
    if (icon_name.length() > 0 && FONT_MDI_ICONS != nullptr) {
      iconChar = getMdiChar(icon_name);
    }

    lv_obj_t* icon_lbl = find_mdi_label_child(tile_obj);
    if (!icon_lbl) {
      if (!icon_disabled && iconChar.length()) {
        rebuild_tile_at_index(grid_type, i);
      }
      continue;
    }

    if (icon_disabled || !icon_name.length()) {
      lv_obj_add_flag(icon_lbl, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    if (!iconChar.length()) {
      lv_obj_add_flag(icon_lbl, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_label_set_text(icon_lbl, iconChar.c_str());
    lv_obj_clear_flag(icon_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(icon_lbl);
  }
}

/* === Update single tile (unified) === */
void tiles_update_tile(GridType grid_type, uint8_t index) {
  uint8_t idx = (uint8_t)grid_type;
  if (!g_tiles_grids[idx]) return;
  if (!g_tiles_loaded[idx]) return;
  if (index >= TILES_PER_GRID) return;

  // Layout changes (position/span) require a full rebuild to keep placeholders in sync.
  tiles_reload_layout(grid_type);
}

/* === Update sensor by entity (unified) === */
void tiles_update_sensor_by_entity(GridType grid_type, const char* entity_id, const char* value) {
  if (!entity_id || !value) return;

  cache_entity_payload(entity_id, value);
  // Previously, returning immediately during sleep cached only the latest
  // state and left UI queues empty. Waking then required a special
  // catch-up in tiles_refresh_visible_from_cache(), which could omit tile
  // types, as happened with Media. Queue continuously instead: the sleep
  // loop processes inbound MQTT and drains these queues too, keeping
  // widget state current throughout sleep. The paused refresh timer
  // prevents rendering to the sleeping panel; a label change only marks
  // its small region dirty.
  if (!tiles_is_loaded(grid_type)) return;

  const TileGridConfig& config = getGridConfig(grid_type);
  bool popup_queued = false;
  uint64_t switch_indices = 0;
  uint64_t binary_sensor_indices = 0;
  bool binary_popup_queued = false;

  // Find tile with matching sensor_entity
  for (uint8_t i = 0; i < TILES_PER_GRID; i++) {
    const Tile& tile = config.tiles[i];
    if ((tile.type == TILE_SENSOR || tile.type == TILE_ENERGY) &&
        tile.sensor_entity.equalsIgnoreCase(entity_id)) {
      String unit = resolve_tile_sensor_unit(tile);
      const char* unit_cstr = unit.length() > 0 ? unit.c_str() : nullptr;
      queue_sensor_tile_update(grid_type, i, value, unit_cstr);
      Serial.printf("[%s] %s %s@%u queued: %s %s\n",
                    getGridName(grid_type),
                    tile.type == TILE_ENERGY ? "Energy" : "Sensor",
                    entity_id, i, value, unit_cstr ? unit_cstr : "");
      // Sensor popups have their own live queue. Energy popups update from
      // the energy period cache and must not accidentally take the sensor
      // popup path here.
      if (tile.type == TILE_SENSOR && !popup_queued) {
        queue_sensor_popup_value(entity_id, value, unit.length() ? unit.c_str() : nullptr, tile.sensor_decimals);
        popup_queued = true;
      }
    }
    if (tile.type == TILE_SWITCH && tile.sensor_entity.equalsIgnoreCase(entity_id)) {
      switch_indices |= uint64_t{1} << i;
    }
    if (tile.type == TILE_MEDIA && tile.sensor_entity.equalsIgnoreCase(entity_id)) {
      queue_media_tile_update(grid_type, i, value);
      Serial.printf("[%s] Media %s@%u queued\n", getGridName(grid_type), entity_id, i);
    }
    if (tile.type == TILE_CLIMATE && tile.sensor_entity.equalsIgnoreCase(entity_id)) {
      queue_climate_tile_update(grid_type, i, value);
      Serial.printf("[%s] Climate %s@%u queued\n",
                    getGridName(grid_type), entity_id, i);
    }
    if (tile.type == TILE_COVER && tile.sensor_entity.equalsIgnoreCase(entity_id)) {
      queue_cover_tile_update(grid_type, i, value);
      Serial.printf("[%s] Cover %s@%u queued\n",
                    getGridName(grid_type), entity_id, i);
    }
    if (tile.type == TILE_BINARY_SENSOR &&
        tile.sensor_entity.equalsIgnoreCase(entity_id)) {
      binary_sensor_indices |= uint64_t{1} << i;
      if (!binary_popup_queued) {
        const BinarySensorState state = parse_binary_sensor_payload(value);
        if (state.valid) {
          const String icon = binary_sensor_visual_icon(
              state, haBridgeConfig.findEntityIcon(entity_id));
          queue_binary_sensor_popup_state(
              entity_id, binary_sensor_state_name(state.value),
              state.available, state.device_class,
              state.has_last_changed ? state.last_changed : 0, icon);
          binary_popup_queued = true;
        }
      }
    }
  }

  // One entity state can feed several tiles. Keep one queue payload and parse
  // it once in the render loop, then fan the parsed state out to every slot.
  if (switch_indices != 0) {
    queue_switch_tile_updates(grid_type, switch_indices, value);
  }
  if (binary_sensor_indices != 0) {
    queue_binary_sensor_tile_updates(
        grid_type, binary_sensor_indices, value);
  }
}

void tiles_update_weather_by_entity(GridType grid_type, const char* entity_id, const char* payload) {
  if (!entity_id || !payload) return;

  cache_entity_payload(entity_id, payload);
  // During sleep, cache only the latest state; leave UI queues empty.
  if (powerManager.isInSleep()) return;
  if (!tiles_is_loaded(grid_type)) return;

  const TileGridConfig& config = getGridConfig(grid_type);

  for (uint8_t i = 0; i < TILES_PER_GRID; i++) {
    const Tile& tile = config.tiles[i];
    if (tile.type == TILE_WEATHER && tile.sensor_entity.equalsIgnoreCase(entity_id)) {
      queue_weather_tile_update(grid_type, i, payload);
      queue_weather_popup_payload(entity_id, payload);
      Serial.printf("[%s] Weather %s@%u queued\n", getGridName(grid_type), entity_id, i);
    }
  }
}

uint16_t tiles_view_id_for_object(lv_obj_t* object) {
  while (object) {
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      if (g_tiles_objs[0][i] == object) return tileConfig.getActiveGrid().tiles[i].view_id;
    }
    object = lv_obj_get_parent(object);
  }
  return 0;
}

bool tiles_folder_switch_pending() { return g_folder_switch_pending; }

bool tiles_open_view_popup(uint16_t view_id) {
  if (!view_id || !g_tiles_loaded[0] || g_folder_switch_pending ||
      g_tiles_reload_requested[0]) return false;
  const auto& grid = tileConfig.getActiveGrid();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = grid.tiles[i];
    if (tile.view_id != view_id || !g_tiles_objs[0][i]) continue;
    lv_obj_t* object = g_tiles_objs[0][i];
    const lv_event_code_t event =
        tile.type == TILE_MEDIA || tile.type == TILE_CAMERA || tile.type == TILE_WEATHER ||
        getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS
            ? LV_EVENT_SHORT_CLICKED : LV_EVENT_LONG_PRESSED;
    // Deliver the same popup-only release gesture as a local user. Switches
    // register their toggle action on the other gesture, never on this one.
    viewNavigationSource(object);
    lv_obj_send_event(object, LV_EVENT_PRESSED, nullptr);
    lv_obj_send_event(object, event, nullptr);
    return true;
  }
  return false;
}

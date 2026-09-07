#include "src/core/text/title_text.h"
#include "src/tiles/config/tile_config.h"
#include "src/devices/device.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <memory>
#include <new>
#include <esp_heap_caps.h>

static const char* PREF_NAMESPACE = "tab5_tiles";
static constexpr uint8_t PACKED_GRID_VERSION = 7;
static constexpr uint16_t IMAGE_SLIDESHOW_DEFAULT_SEC = 10;
static constexpr uint16_t IMAGE_SLIDESHOW_MAX_SEC = 3600;
static constexpr size_t OLD_TILES_PER_GRID = 12;  // For V1-V5 migration
static constexpr uint8_t LEGACY_NAV_KIND_SETTINGS = 1;
static constexpr uint8_t LEGACY_NAV_KIND_BACK = 2;
static constexpr uint8_t LEGACY_TAB_SETTINGS = 3;

class ScopedStorageWriteDisplayGuard {
 public:
  ScopedStorageWriteDisplayGuard() { Device::storageWriteBegin(); }
  ~ScopedStorageWriteDisplayGuard() { Device::storageWriteEnd(); }

  ScopedStorageWriteDisplayGuard(const ScopedStorageWriteDisplayGuard&) = delete;
  ScopedStorageWriteDisplayGuard& operator=(
      const ScopedStorageWriteDisplayGuard&) = delete;
};

template <typename T>
struct HeapCapsDeleter {
  void operator()(T* ptr) const {
    if (ptr) heap_caps_free(ptr);
  }
};

template <typename T>
using HeapCapsPtr = std::unique_ptr<T, HeapCapsDeleter<T>>;

template <typename T>
static HeapCapsPtr<T> allocPackedGridScratch(size_t count, const char* operation) {
  T* ptr = static_cast<T*>(heap_caps_calloc(
      count, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!ptr) {
    // Keep storage operations functional on a board without usable PSRAM.
    // This fallback is temporary and released again when the call returns.
    ptr = static_cast<T*>(heap_caps_calloc(
        count, sizeof(T), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (ptr) {
      Serial.printf("[TileConfig] WARN: %s scratch temporarily uses internal RAM (%u bytes)\n",
                    operation ? operation : "Grid",
                    static_cast<unsigned>(count * sizeof(T)));
    }
  }
  if (!ptr) {
    Serial.printf("[TileConfig] ERROR: No memory for %s scratch (%u bytes)\n",
                  operation ? operation : "Grid",
                  static_cast<unsigned>(count * sizeof(T)));
  }
  return HeapCapsPtr<T>(ptr);
}

// Fixed lengths for packed strings, including the null terminator.
static constexpr size_t TITLE_MAX     = 32;
static constexpr size_t ICON_MAX      = 32;  // MDI icon name (e.g. "thermometer")
static constexpr size_t ENTITY_MAX    = 64;
static constexpr size_t ENTITY_MAX_V4 = 128;
static constexpr size_t UNIT_MAX      = 16;
static constexpr size_t SCENE_MAX     = 32;
static constexpr size_t MACRO_MAX     = 32;

struct PackedTileV1 {
  uint8_t type;
  uint8_t sensor_decimals;
  uint8_t key_code;
  uint8_t key_modifier;
  uint32_t bg_color;
  char title[TITLE_MAX];
  char icon_name[ICON_MAX];        // MDI Icon Name
  char sensor_entity[ENTITY_MAX];
  char sensor_unit[UNIT_MAX];
  char scene_alias[SCENE_MAX];
  char key_macro[MACRO_MAX];
};

struct PackedGridV1 {
  uint8_t version;
  uint8_t reserved[3];  // Alignment / future use
  PackedTileV1 tiles[OLD_TILES_PER_GRID];
};

struct PackedTileV2 {
  uint8_t type;
  uint8_t sensor_decimals;
  uint8_t key_code;
  uint8_t key_modifier;
  uint32_t bg_color;
  char title[TITLE_MAX];
  char icon_name[ICON_MAX];        // MDI Icon Name
  char sensor_entity[ENTITY_MAX];
  char sensor_unit[UNIT_MAX];
  char scene_alias[SCENE_MAX];
  char key_macro[MACRO_MAX];
  uint8_t sensor_value_font;
  uint8_t reserved[3];             // Alignment / future use
};

struct PackedGridV2 {
  uint8_t version;
  uint8_t reserved[3];  // Alignment / future use
  PackedTileV2 tiles[OLD_TILES_PER_GRID];
};

struct PackedTileV4 {
  uint8_t type;
  uint8_t sensor_decimals;
  uint8_t key_code;
  uint8_t key_modifier;
  uint32_t bg_color;
  char title[TITLE_MAX];
  char icon_name[ICON_MAX];        // MDI Icon Name
  char sensor_entity[ENTITY_MAX_V4];
  char sensor_unit[UNIT_MAX];
  char scene_alias[SCENE_MAX];
  char key_macro[MACRO_MAX];
  uint8_t sensor_value_font;
  uint16_t image_slideshow_sec;
  uint8_t reserved[1];             // Alignment / future use
};

struct PackedGridV4 {
  uint8_t version;
  uint8_t reserved[3];  // Alignment / future use
  PackedTileV4 tiles[OLD_TILES_PER_GRID];
};

struct PackedTileV3 {
  uint8_t type;
  uint8_t sensor_decimals;
  uint8_t key_code;
  uint8_t key_modifier;
  uint32_t bg_color;
  char title[TITLE_MAX];
  char icon_name[ICON_MAX];        // MDI Icon Name
  char sensor_entity[ENTITY_MAX];
  char sensor_unit[UNIT_MAX];
  char scene_alias[SCENE_MAX];
  char key_macro[MACRO_MAX];
  uint8_t sensor_value_font;
  uint16_t image_slideshow_sec;
  uint8_t reserved[1];             // Alignment / future use
};

struct PackedGridV3 {
  uint8_t version;
  uint8_t reserved[3];  // Alignment / future use
  PackedTileV3 tiles[OLD_TILES_PER_GRID];
};

struct PackedTileV5 {
  uint8_t type;
  uint8_t sensor_decimals;
  uint8_t key_code;
  uint8_t key_modifier;
  uint32_t bg_color;
  char title[TITLE_MAX];
  char icon_name[ICON_MAX];        // MDI Icon Name
  char sensor_entity[ENTITY_MAX];
  char sensor_unit[UNIT_MAX];
  char scene_alias[SCENE_MAX];
  char key_macro[MACRO_MAX];
  uint8_t sensor_value_font;
  uint16_t image_slideshow_sec;
  uint8_t sensor_gauge_enabled;
  int32_t sensor_gauge_min;
  int32_t sensor_gauge_max;
  uint8_t reserved[1];             // Alignment / future use
};

struct PackedGridV5 {
  uint8_t version;
  uint8_t reserved[3];  // Alignment / future use
  PackedTileV5 tiles[OLD_TILES_PER_GRID];
};

// V6: Added col/row/span_w/span_h for flexible grid layout
struct PackedTileV6 {
  uint8_t type;
  uint8_t sensor_decimals;
  uint8_t key_code;
  uint8_t key_modifier;
  uint32_t bg_color;
  uint8_t col;                       // Grid column (0-3)
  uint8_t row;                       // Grid row (0-3)
  uint8_t span_w;                    // Width in cells (1-4)
  uint8_t span_h;                    // Height in cells (1-4)
  char title[TITLE_MAX];
  char icon_name[ICON_MAX];
  char sensor_entity[ENTITY_MAX];
  char sensor_unit[UNIT_MAX];
  char scene_alias[SCENE_MAX];
  char key_macro[MACRO_MAX];
  uint8_t sensor_value_font;
  uint16_t image_slideshow_sec;
  uint8_t sensor_gauge_enabled;
  int32_t sensor_gauge_min;
  int32_t sensor_gauge_max;
};

struct PackedTileV7 {
  uint8_t type;
  uint8_t sensor_decimals;
  uint8_t key_code;
  uint8_t key_modifier;
  uint32_t bg_color;
  uint8_t col;                       // Grid column (0-3)
  uint8_t row;                       // Grid row (0-3)
  uint8_t span_w;                    // Width in cells (1-4)
  uint8_t span_h;                    // Height in cells (1-4)
  char title[TITLE_MAX];
  char icon_name[ICON_MAX];
  char sensor_entity[ENTITY_MAX];
  char sensor_unit[UNIT_MAX];
  char scene_alias[SCENE_MAX];
  char key_macro[MACRO_MAX];
  uint8_t sensor_value_font;
  uint16_t image_slideshow_sec;
  uint8_t sensor_gauge_enabled;
  int32_t sensor_gauge_min;
  int32_t sensor_gauge_max;
  uint8_t popup_open_mode;
  uint8_t reserved[3];
};

// Split grid into small fixed-size chunks to fit NVS size limits.
// The last chunk may be only partially used on non-4x4 layouts.
static constexpr size_t TILES_PER_QUARTER = 4;
static constexpr size_t QUARTERS_PER_GRID =
    (TILES_PER_GRID + TILES_PER_QUARTER - 1) / TILES_PER_QUARTER;
static_assert(QUARTERS_PER_GRID > 0, "Grid must contain at least one tile");

static constexpr size_t quarterGridIndex(size_t quarter, size_t tile_index) {
  return quarter * TILES_PER_QUARTER + tile_index;
}

struct PackedQuarterGridV6 {
  uint8_t version;
  uint8_t quarter_index;  // 0-3
  uint8_t reserved[2];
  PackedTileV6 tiles[TILES_PER_QUARTER];
};

struct PackedQuarterGridV7 {
  uint8_t version;
  uint8_t quarter_index;  // 0-3
  uint8_t reserved[2];
  PackedTileV7 tiles[TILES_PER_QUARTER];
};

struct FolderIndexHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
};

struct FolderEntryDisk {
  uint16_t id;
  uint16_t parent_id;
  char name[32];
  char icon_name[32];
};

struct FolderAccessHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
};

struct FolderAccessDiskV1 {
  uint16_t folder_id;
  uint8_t enabled;
  uint8_t reserved;
  uint8_t salt[pin_access::kSaltSize];
  uint8_t hash[pin_access::kHashSize];
};

struct FolderAccessDisk {
  uint16_t folder_id;
  uint8_t enabled;
  uint8_t pin_length;
  uint8_t salt[pin_access::kSaltSize];
  uint8_t hash[pin_access::kHashSize];
  char pin_digits[pin_access::kUserPinMaxDigits];
};

static_assert(sizeof(FolderAccessDiskV1) == 52,
              "Legacy folder access record layout changed");
static_assert(sizeof(FolderAccessDisk) == 60,
              "Folder access record layout changed");

TileConfig tileConfig;

TileConfig::TileConfig() = default;

static void copyString(const String& src, char* dst, size_t max_len) {
  if (!dst || max_len == 0) return;
  memset(dst, 0, max_len);
  if (src.length() == 0) return;
  size_t n = src.length();
  if (n >= max_len) n = max_len - 1;
  memcpy(dst, src.c_str(), n);
}

static uint8_t clampDecimals(uint8_t val) {
  if (val == 0xFF) return 0xFF;
  if (val > 6) return 6;
  return val;
}

static uint8_t clampSensorValueFont(uint8_t val) {
  if (val > 4) return 0;
  return val;
}

static uint16_t clampImageSlideshowSeconds(uint16_t val) {
  if (val == 0) return IMAGE_SLIDESHOW_DEFAULT_SEC;
  if (val > IMAGE_SLIDESHOW_MAX_SEC) return IMAGE_SLIDESHOW_MAX_SEC;
  return val;
}

static uint16_t getNavigateTargetId(const Tile& tile) {
  return static_cast<uint16_t>((static_cast<uint16_t>(tile.key_modifier) << 8) | tile.key_code);
}

static void setNavigateTargetId(Tile& tile, uint16_t folder_id) {
  tile.key_code = static_cast<uint8_t>(folder_id & 0xFF);
  tile.key_modifier = static_cast<uint8_t>((folder_id >> 8) & 0xFF);
}

static void normalizeGaugeRange(int32_t& min_val, int32_t& max_val) {
  if (max_val <= min_val) {
    min_val = 0;
    max_val = 100;
  }
}

static bool shouldNormalizeGaugeRange(TileType type) {
  return type != TILE_CLOCK;
}

static bool looksLikeImagePath(const String& value);
static const char* kImagePathDir = "/_tile_links";
static const char* kEntityPathDir = "/_tile_entities";
static const char* kTitlePathDir = "/_tile_titles";
static const char* kTileGridDir = "/_tile_grids";
static const char* kFolderIndexFile = "/_tile_grids/folders.bin";
static constexpr uint32_t kFolderIndexMagic = 0x54464C44;  // 'TFLD'
static constexpr uint16_t kFolderIndexVersion = 1;
static const char* kFolderAccessFile = "/_tile_grids/folder_access.bin";
static constexpr uint32_t kFolderAccessMagic = 0x54464143;  // 'TFAC'
static constexpr uint16_t kFolderAccessVersion = 2;

static fs::FS& storageFS() {
  return Device::storageFS();
}

static bool storageReady() {
  return Device::storageReady();
}

static bool ensureImagePathDir() {
  if (!storageReady()) return false;
  if (storageFS().exists(kImagePathDir)) return true;
  return storageFS().mkdir(kImagePathDir);
}

static bool ensureEntityPathDir() {
  if (!storageReady()) return false;
  if (storageFS().exists(kEntityPathDir)) return true;
  return storageFS().mkdir(kEntityPathDir);
}

static bool ensureTileGridDir() {
  if (!storageReady()) return false;
  if (storageFS().exists(kTileGridDir)) return true;
  return storageFS().mkdir(kTileGridDir);
}

static bool ensureIconDir() {
  if (!storageReady()) return false;
  if (storageFS().exists("/icons")) return true;
  return storageFS().mkdir("/icons");
}

static String tmpPathFor(const String& filePath) {
  return filePath + ".tmp";
}

static String backupPathFor(const String& filePath) {
  return filePath + ".bak";
}

static bool replaceFileWithPreparedTmp(const String& tmpPath, const String& filePath) {
  if (!storageReady()) return false;
  if (!storageFS().exists(tmpPath)) return false;

  const String backupPath = backupPathFor(filePath);
  if (storageFS().exists(backupPath)) storageFS().remove(backupPath);

  if (storageFS().rename(tmpPath, filePath)) {
    return true;
  }

  // Some FS backends do not replace an existing destination on rename(). Keep
  // the old file as .bak first, so a reset between both renames is recoverable.
  if (storageFS().exists(filePath)) {
    if (!storageFS().rename(filePath, backupPath)) {
      return false;
    }
  }

  if (storageFS().rename(tmpPath, filePath)) {
    if (storageFS().exists(backupPath)) storageFS().remove(backupPath);
    return true;
  }

  if (storageFS().exists(backupPath) && !storageFS().exists(filePath)) {
    storageFS().rename(backupPath, filePath);
  }
  return false;
}

static void promoteRecoveryFile(const String& candidatePath, const String& filePath) {
  if (!storageReady()) return;
  if (!storageFS().exists(candidatePath)) return;

#if defined(DEVICE_ESP32_S3_RGB_480)
  Device::ScopedStorageWrite storage_write;
#endif
  if (storageFS().exists(filePath)) {
    const String badPath = filePath + ".bad";
    if (storageFS().exists(badPath)) storageFS().remove(badPath);
    if (!storageFS().rename(filePath, badPath)) {
      return;
    }
  }

  if (!storageFS().rename(candidatePath, filePath)) {
    const String badPath = filePath + ".bad";
    if (storageFS().exists(badPath) && !storageFS().exists(filePath)) {
      storageFS().rename(badPath, filePath);
    }
    return;
  }

  const String badPath = filePath + ".bad";
  if (storageFS().exists(badPath)) storageFS().remove(badPath);
}

static String imagePathFile(uint16_t folder_id, size_t index) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s/f%u_%02u.url", kImagePathDir, static_cast<unsigned>(folder_id), static_cast<unsigned>(index));
  return String(buf);
}

static String imagePathFileLegacy(const char* prefix, size_t index) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s/%s_%02u.url", kImagePathDir, prefix, static_cast<unsigned>(index));
  return String(buf);
}

static String entityPathFile(uint16_t folder_id, size_t index) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s/f%u_%02u.ent", kEntityPathDir, static_cast<unsigned>(folder_id), static_cast<unsigned>(index));
  return String(buf);
}

// Per-tile sidecar files hold entity IDs / image paths too long for the packed
// binary tile struct (ENTITY_MAX=64 bytes -- real HA entity ids never get
// close). Almost no tile actually has one, but readLongEntityIdSd()/
// readImagePathSd() used to do a storageFS().exists() flash lookup for EVERY
// entity-storing tile on EVERY grid load. With ~9 folders x ~15 entity tiles
// reloaded on each HA bridge update, that measured out to 40-140ms of blocking
// flash I/O per grid (visible UI freeze). Build the "which folder/index pairs
// actually have an override" list once via directory listing, then just check
// this in-memory list instead of hitting flash for tiles that never have one.
static bool g_sidecar_index_built = false;
static std::vector<uint32_t> g_image_sidecar_keys;
static std::vector<uint32_t> g_entity_sidecar_keys;
static std::vector<uint32_t> g_title_sidecar_keys;

static uint32_t sidecarKey(uint16_t folder_id, size_t index) {
  return (static_cast<uint32_t>(folder_id) << 8) | static_cast<uint8_t>(index);
}

static bool sidecarKeyPresent(const std::vector<uint32_t>& keys, uint32_t key) {
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

static void sidecarKeyAdd(std::vector<uint32_t>& keys, uint32_t key) {
  if (!sidecarKeyPresent(keys, key)) keys.push_back(key);
}

static void sidecarKeyRemove(std::vector<uint32_t>& keys, uint32_t key) {
  keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
}

static void scanSidecarDir(const char* dir, std::vector<uint32_t>& out) {
  File root = storageFS().open(dir);
  if (!root) return;
  for (File file = root.openNextFile(); file; file = root.openNextFile()) {
    if (file.isDirectory()) continue;
    const char* name_c = file.name();
    if (!name_c) continue;
    unsigned folder_id = 0, index = 0;
    if (sscanf(name_c, "f%u_%u.", &folder_id, &index) == 2) {
      out.push_back(sidecarKey(static_cast<uint16_t>(folder_id), index));
    }
  }
}

static void ensureSidecarIndexBuilt() {
  if (g_sidecar_index_built) return;
  g_sidecar_index_built = true;
  if (!storageReady()) return;
  scanSidecarDir(kImagePathDir, g_image_sidecar_keys);
  scanSidecarDir(kEntityPathDir, g_entity_sidecar_keys);
  scanSidecarDir(kTitlePathDir, g_title_sidecar_keys);
}

static String entityPathFileLegacy(const char* prefix, size_t index) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s/%s_%02u.ent", kEntityPathDir, prefix, static_cast<unsigned>(index));
  return String(buf);
}

static String tileGridFile(uint16_t folder_id) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s/f%05u_v7.bin", kTileGridDir, static_cast<unsigned>(folder_id));
  return String(buf);
}

static String tileGridFileLegacyV6(uint16_t folder_id) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s/f%05u_v6.bin", kTileGridDir, static_cast<unsigned>(folder_id));
  return String(buf);
}

static String tileGridFileLegacy(const char* prefix) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s/%s_v6.bin", kTileGridDir, prefix);
  return String(buf);
}

static bool writeGridSd(uint16_t folder_id, const PackedQuarterGridV7* packed, size_t count) {
  if (!storageReady()) return false;
  if (!ensureTileGridDir()) return false;

  String filePath = tileGridFile(folder_id);
  // Write the complete .tmp file before renaming it over the destination
  // (LittleFS replaces the destination atomically). Previously, deleting the
  // old file first let a crash or power loss leave an empty or partial file,
  // preventing the grid from loading and blocking the folder on the next boot.
  String tmpPath = tmpPathFor(filePath);
  if (storageFS().exists(tmpPath)) storageFS().remove(tmpPath);

  // Yield before writing: internal flash writes briefly lock the flash cache
  // on both cores, also blocking the Wi-Fi/SDIO task. Under heavy MQTT traffic,
  // its receive queue can overflow (sdio_push_data_to_queue assertion), as
  // reproduced when rapidly moving or renaming many tiles. Yielding immediately
  // beforehand gives the Wi-Fi task a final chance to drain its queue.
  yield();

  File f = storageFS().open(tmpPath, FILE_WRITE);
  if (!f) {
    return false;
  }

  const size_t expected = count * sizeof(PackedQuarterGridV7);
  const size_t written = f.write(reinterpret_cast<const uint8_t*>(packed), expected);
  f.flush();
  f.close();
  yield();

  if (written != expected) {
    Serial.printf("[TileConfig] Storage short write: folder=%u written=%u expected=%u\n",
                  static_cast<unsigned>(folder_id),
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(expected));
    storageFS().remove(tmpPath);
    return false;
  }

  if (!replaceFileWithPreparedTmp(tmpPath, filePath)) {
    Serial.printf("[TileConfig] Storage rename failed: folder=%u\n",
                  static_cast<unsigned>(folder_id));
    storageFS().remove(tmpPath);
    return false;
  }

  return true;
}

#if defined(DEVICE_ESP32_S3_RGB_480)
static bool packedGridMatchesStored(uint16_t folder_id,
                                    const PackedQuarterGridV7* packed,
                                    size_t count) {
  if (!storageReady() || !packed || count == 0) return false;
  const String file_path = tileGridFile(folder_id);
  File file = storageFS().open(file_path, FILE_READ);
  if (!file) return false;

  const size_t expected = count * sizeof(PackedQuarterGridV7);
  if (static_cast<size_t>(file.size()) != expected) {
    file.close();
    return false;
  }

  const uint8_t* source = reinterpret_cast<const uint8_t*>(packed);
  uint8_t chunk[256];
  size_t offset = 0;
  while (offset < expected) {
    const size_t wanted = min(sizeof(chunk), expected - offset);
    const size_t read = file.read(chunk, wanted);
    if (read != wanted || memcmp(chunk, source + offset, wanted) != 0) {
      file.close();
      return false;
    }
    offset += wanted;
  }
  file.close();
  return true;
}
#endif

static bool readPackedGridFileV7(const String& filePath, PackedQuarterGridV7* packed, size_t count) {
  // No separate exists() pre-check: open() already returns a falsy File when
  // the path doesn't exist (checked right below), and exists()+open() were
  // each doing a full LittleFS directory lookup -- measured at ~30ms combined
  // per grid, now ~half that with just the one open() lookup.
  File f = storageFS().open(filePath, FILE_READ);
  if (!f) return false;
  const size_t expected = count * sizeof(PackedQuarterGridV7);
  if (static_cast<size_t>(f.size()) < expected) {
    f.close();
    return false;
  }
  size_t read = f.read(reinterpret_cast<uint8_t*>(packed), expected);
  f.close();
  if (read != expected) return false;
  for (size_t q = 0; q < count; ++q) {
    if (packed[q].version != 7 || packed[q].quarter_index != static_cast<uint8_t>(q)) {
      return false;
    }
  }
  return true;
}

static bool readPackedGridFileV6(const String& filePath, PackedQuarterGridV6* packed, size_t count) {
  // See readPackedGridFileV7() above: open() alone already handles "doesn't exist".
  File f = storageFS().open(filePath, FILE_READ);
  if (!f) return false;
  const size_t expected = count * sizeof(PackedQuarterGridV6);
  if (static_cast<size_t>(f.size()) < expected) {
    f.close();
    return false;
  }
  size_t read = f.read(reinterpret_cast<uint8_t*>(packed), expected);
  f.close();
  if (read != expected) return false;
  for (size_t q = 0; q < count; ++q) {
    if (packed[q].version != 6 || packed[q].quarter_index != static_cast<uint8_t>(q)) {
      return false;
    }
  }
  return true;
}

static bool readGridSd(uint16_t folder_id, PackedQuarterGridV7* packed, size_t count) {
  if (!storageReady()) return false;
  const String filePath = tileGridFile(folder_id);
  const String tmpPath = tmpPathFor(filePath);
  if (readPackedGridFileV7(tmpPath, packed, count)) {
    Serial.printf("[TileConfig] Grid %u restored from .tmp\n",
                  static_cast<unsigned>(folder_id));
    promoteRecoveryFile(tmpPath, filePath);
    return true;
  }
  if (readPackedGridFileV7(filePath, packed, count)) {
    return true;
  }
  const String backupPath = backupPathFor(filePath);
  if (readPackedGridFileV7(backupPath, packed, count)) {
    Serial.printf("[TileConfig] Grid %u restored from .bak\n",
                  static_cast<unsigned>(folder_id));
    promoteRecoveryFile(backupPath, filePath);
    return true;
  }
  return false;
}

static bool readGridSdV6(uint16_t folder_id, PackedQuarterGridV6* packed, size_t count) {
  if (!storageReady()) return false;
  return readPackedGridFileV6(tileGridFileLegacyV6(folder_id), packed, count);
}

// Distinguish a new folder with no file yet (the caller's defaults are valid)
// from an existing file that could not be read (an error that must prevent
// overwriting it). Without this distinction, the first save to a new folder,
// such as folder 0 on a fresh device, incorrectly failed with a grid-load error.
static bool anyGridFileExists(uint16_t folder_id, bool is_root) {
  if (!storageReady()) return false;
  const String filePath = tileGridFile(folder_id);
  if (storageFS().exists(filePath)) return true;
  if (storageFS().exists(tmpPathFor(filePath))) return true;
  if (storageFS().exists(backupPathFor(filePath))) return true;
  if (storageFS().exists(tileGridFileLegacyV6(folder_id))) return true;
  if (is_root && storageFS().exists(tileGridFileLegacy("tab0"))) return true;
  return false;
}

static bool writeImagePathSd(uint16_t folder_id, size_t index, const String& path) {
  if (!storageReady()) return false;
  ensureSidecarIndexBuilt();
  uint32_t key = sidecarKey(folder_id, index);
  String filePath = imagePathFile(folder_id, index);
  const bool has_sidecar = sidecarKeyPresent(g_image_sidecar_keys, key);
  if (path.length() == 0) {
    if (!has_sidecar) return true;
    if (storageFS().exists(filePath)) storageFS().remove(filePath);
    sidecarKeyRemove(g_image_sidecar_keys, key);
    return true;
  }

  if (has_sidecar) {
    File current_file = storageFS().open(filePath, FILE_READ);
    if (current_file) {
      String current = current_file.readString();
      current_file.close();
      current.trim();
      if (current == path) return true;
    }
  }

  if (!ensureImagePathDir()) return false;
  if (has_sidecar && storageFS().exists(filePath)) storageFS().remove(filePath);
  File f = storageFS().open(filePath, FILE_WRITE);
  if (!f) return false;
  f.print(path);
  f.close();
  sidecarKeyAdd(g_image_sidecar_keys, key);
  return true;
}

static bool readImagePathSd(uint16_t folder_id, size_t index, String& out) {
  out = "";
  if (!storageReady()) return false;
  ensureSidecarIndexBuilt();
  if (sidecarKeyPresent(g_image_sidecar_keys, sidecarKey(folder_id, index))) {
    File f = storageFS().open(imagePathFile(folder_id, index), FILE_READ);
    if (f) {
      out = f.readString();
      f.close();
      out.trim();
      if (out.length() > 0) return true;
    }
  }
  if (folder_id == 0) {
    String legacyPath = imagePathFileLegacy("tab0", index);
    if (storageFS().exists(legacyPath)) {
      File f = storageFS().open(legacyPath, FILE_READ);
      if (f) {
        out = f.readString();
        f.close();
        out.trim();
        return out.length() > 0;
      }
    }
  }
  return false;
}

static bool writeLongEntityIdSd(uint16_t folder_id, size_t index, const String& entity) {
  if (!storageReady()) return false;
  ensureSidecarIndexBuilt();
  uint32_t key = sidecarKey(folder_id, index);
  String filePath = entityPathFile(folder_id, index);
  const bool has_sidecar = sidecarKeyPresent(g_entity_sidecar_keys, key);
  if (entity.length() < ENTITY_MAX) {
    if (!has_sidecar) return true;
    if (storageFS().exists(filePath)) storageFS().remove(filePath);
    sidecarKeyRemove(g_entity_sidecar_keys, key);
    return true;
  }

  if (has_sidecar) {
    File current_file = storageFS().open(filePath, FILE_READ);
    if (current_file) {
      String current = current_file.readString();
      current_file.close();
      current.trim();
      if (current == entity) return true;
    }
  }

  if (!ensureEntityPathDir()) return false;
  if (has_sidecar && storageFS().exists(filePath)) storageFS().remove(filePath);
  File f = storageFS().open(filePath, FILE_WRITE);
  if (!f) return false;
  f.print(entity);
  f.close();
  sidecarKeyAdd(g_entity_sidecar_keys, key);
  return true;
}

static String titlePathFile(uint16_t folder_id, size_t index) {
  char path[64];
  snprintf(path, sizeof(path), "%s/f%u_%02u.txt", kTitlePathDir,
           static_cast<unsigned>(folder_id), static_cast<unsigned>(index));
  return String(path);
}

static bool readLongTitleSd(uint16_t folder_id, size_t index, String& out) {
  if (!storageReady()) return false;
  ensureSidecarIndexBuilt();
  if (!sidecarKeyPresent(g_title_sidecar_keys, sidecarKey(folder_id, index))) return false;
  const String path = titlePathFile(folder_id, index);
  for (const String& candidate : {path, tmpPathFor(path), backupPathFor(path)}) {
    File file = storageFS().open(candidate, FILE_READ);
    if (!file) continue;
    const size_t size = file.size();
    if (size < TITLE_MAX || size > hometiles_title::kMaxBytes) { file.close(); continue; }
    String value = file.readString();
    file.close();
    if (value.length() != size) continue;
    out = value;
    return true;
  }
  return false;
}

static bool writeLongTitleSd(uint16_t folder_id, size_t index, const String& title) {
  if (!storageReady()) return false;
  if (title.length() > hometiles_title::kMaxBytes) return false;
  ensureSidecarIndexBuilt();
  const uint32_t key = sidecarKey(folder_id, index);
  const String path = titlePathFile(folder_id, index);
  const bool present = sidecarKeyPresent(g_title_sidecar_keys, key);
  if (title.length() < TITLE_MAX) {
    if (!present) return true;
    for (const String& candidate : {path, tmpPathFor(path), backupPathFor(path)})
      if (storageFS().exists(candidate) && !storageFS().remove(candidate)) return false;
    sidecarKeyRemove(g_title_sidecar_keys, key);
    return true;
  }
  String current;
  if (readLongTitleSd(folder_id, index, current) && current == title) return true;
  if (!storageFS().exists(kTitlePathDir) && !storageFS().mkdir(kTitlePathDir)) return false;
  const String temporary = tmpPathFor(path);
  if (storageFS().exists(temporary)) storageFS().remove(temporary);
  File file = storageFS().open(temporary, FILE_WRITE);
  if (!file) return false;
  const size_t written = file.print(title);
  file.flush(); file.close();
  if (written != title.length() || !replaceFileWithPreparedTmp(temporary, path)) {
    storageFS().remove(temporary);
    return false;
  }
  sidecarKeyAdd(g_title_sidecar_keys, key);
  return true;
}

static void applyLongTitlesFromSd(uint16_t folder_id, TileGridConfig& grid) {
  for (size_t index = 0; index < TILES_PER_GRID; ++index) {
    Tile& tile = grid.tiles[index];
    if (tile.type == TILE_EMPTY) continue;
    String title;
    // Ignore an orphan from a failed save if it does not match the packed prefix.
    if (tile.title.length() == TITLE_MAX - 1 &&
        readLongTitleSd(folder_id, index, title) && title.startsWith(tile.title))
      tile.title = title;
  }
}

#if defined(DEVICE_ESP32_S3_RGB_480)
static bool sidecarTextMatches(bool has_sidecar, const String& file_path,
                               const String& expected,
                               bool sidecar_required) {
  if (!sidecar_required) return !has_sidecar;
  if (!has_sidecar) return false;
  File file = storageFS().open(file_path, FILE_READ);
  if (!file) return false;
  String current = file.readString();
  file.close();
  current.trim();
  return current == expected;
}

static bool gridSidecarsMatchStored(uint16_t folder_id,
                                    const TileGridConfig& grid) {
  ensureSidecarIndexBuilt();
  for (size_t index = 0; index < TILES_PER_GRID; ++index) {
    const Tile& tile = grid.tiles[index];
    const uint32_t key = sidecarKey(folder_id, index);
    if (tile.type == TILE_IMAGE || tile.type == TILE_SCENE) {
      if (!sidecarTextMatches(
              sidecarKeyPresent(g_image_sidecar_keys, key),
              imagePathFile(folder_id, index), tile.image_path,
              tile.image_path.length() != 0)) {
        return false;
      }
    }

    String stored_title;
    const bool title_required = tile.type != TILE_EMPTY && tile.title.length() >= TITLE_MAX;
    if (title_required ? (!readLongTitleSd(folder_id, index, stored_title) || stored_title != tile.title)
                       : sidecarKeyPresent(g_title_sidecar_keys, key)) return false;

    const bool entity_required =
        entityTileStoresSensorEntity(tile.type) &&
        tile.sensor_entity.length() >= ENTITY_MAX;
    if (!sidecarTextMatches(
            sidecarKeyPresent(g_entity_sidecar_keys, key),
            entityPathFile(folder_id, index), tile.sensor_entity,
            entity_required)) {
      return false;
    }
  }
  return true;
}
#endif

static bool readLongEntityIdSd(uint16_t folder_id, size_t index, String& out) {
  out = "";
  if (!storageReady()) return false;
  ensureSidecarIndexBuilt();
  if (sidecarKeyPresent(g_entity_sidecar_keys, sidecarKey(folder_id, index))) {
    File f = storageFS().open(entityPathFile(folder_id, index), FILE_READ);
    if (f) {
      out = f.readString();
      f.close();
      out.trim();
      if (out.length() > 0) return true;
    }
  }
  if (folder_id == 0) {
    String legacyPath = entityPathFileLegacy("tab0", index);
    if (storageFS().exists(legacyPath)) {
      File f = storageFS().open(legacyPath, FILE_READ);
      if (f) {
        out = f.readString();
        f.close();
        out.trim();
        return out.length() > 0;
      }
    }
  }
  return false;
}

static void packTile(const Tile& in, PackedTileV7& out) {
  memset(&out, 0, sizeof(out));
  out.type = static_cast<uint8_t>(in.type);
  uint8_t decimals = clampDecimals(in.sensor_decimals);
  if (in.type == TILE_FOLDER || in.type == TILE_SETTINGS || in.type == TILE_BACK) {
    decimals = 0xFF;
  }
  out.sensor_decimals = decimals;
  out.key_code = in.key_code;
  out.key_modifier = in.key_modifier;
  out.bg_color = in.bg_color;
  out.col = (in.col < GRID_COLS) ? in.col : 0;
  out.row = (in.row < GRID_ROWS) ? in.row : 0;
  uint8_t span_w = (in.span_w < 1) ? 1 : ((in.span_w > GRID_COLS) ? GRID_COLS : in.span_w);
  uint8_t span_h = (in.span_h < 1) ? 1 : ((in.span_h > GRID_ROWS) ? GRID_ROWS : in.span_h);
  clamp_media_tile_layout(in.type, out.col, out.row, span_w, span_h);
  if (span_w > GRID_COLS - out.col) span_w = GRID_COLS - out.col;
  if (span_h > GRID_ROWS - out.row) span_h = GRID_ROWS - out.row;
  out.span_w = span_w;
  out.span_h = span_h;
  out.sensor_value_font = clampSensorValueFont(in.sensor_value_font);
  out.image_slideshow_sec = clampImageSlideshowSeconds(in.image_slideshow_sec);
  out.sensor_gauge_enabled = (in.sensor_display_mode <= 2) ? in.sensor_display_mode : 0;
  out.sensor_gauge_min = in.sensor_gauge_min;
  out.sensor_gauge_max = in.sensor_gauge_max;
  if (shouldNormalizeGaugeRange(in.type)) {
    normalizeGaugeRange(out.sensor_gauge_min, out.sensor_gauge_max);
  }
  out.popup_open_mode = (tileTypeStoresPopupMode(in.type) &&
                         getTilePopupOpenMode(in) == TILE_POPUP_OPEN_SHORT_PRESS)
                            ? TILE_POPUP_OPEN_SHORT_PRESS
                            : TILE_POPUP_OPEN_LONG_PRESS;
  out.reserved[0] = in.background_opacity;
  out.reserved[1] = static_cast<uint8_t>(in.view_id);
  out.reserved[2] = static_cast<uint8_t>(in.view_id >> 8);
  copyString(in.title, out.title, sizeof(out.title));
  copyString(in.icon_name, out.icon_name, sizeof(out.icon_name));
  copyString(in.sensor_unit, out.sensor_unit, sizeof(out.sensor_unit));
  // For TILE_SENSOR, store gauge appearance in scene_alias with magic prefix
  if (in.type == TILE_SENSOR) {
    memset(out.scene_alias, 0, sizeof(out.scene_alias));
    out.scene_alias[0] = 0x01;  // Magic byte
    // Clamp and store arc degrees (90-359, default 100)
    uint16_t arc = in.sensor_gauge_arc;
    if (arc < 90) arc = 90;
    if (arc > 359) arc = 359;
    out.scene_alias[1] = static_cast<char>(arc & 0xFF);
    out.scene_alias[2] = static_cast<char>((arc >> 8) & 0xFF);
    // Clamp and store gauge size (100-800, default 350)
    uint16_t size = in.sensor_gauge_size;
    if (size < 100) size = 100;
    if (size > 800) size = 800;
    out.scene_alias[3] = static_cast<char>(size & 0xFF);
    out.scene_alias[4] = static_cast<char>((size >> 8) & 0xFF);
    // Clamp and store y offset (-100 to 200, default 12)
    int16_t y_off = in.sensor_gauge_y_offset;
    if (y_off < -100) y_off = -100;
    if (y_off > 200) y_off = 200;
    out.scene_alias[5] = static_cast<char>(y_off & 0xFF);
    out.scene_alias[6] = static_cast<char>((y_off >> 8) & 0xFF);
    // Clamp and store value y offset (-100 to 200, default 0)
    int16_t val_y_off = in.sensor_value_y_offset;
    if (val_y_off < -100) val_y_off = -100;
    if (val_y_off > 200) val_y_off = 200;
    out.scene_alias[7] = static_cast<char>(val_y_off & 0xFF);
    out.scene_alias[8] = static_cast<char>((val_y_off >> 8) & 0xFF);
    // Clamp and store graph height (20-200, default 60)
    uint16_t graph_h = in.sensor_graph_height;
    if (graph_h < 20) graph_h = 20;
    if (graph_h > 200) graph_h = 200;
    out.scene_alias[9] = static_cast<char>(graph_h & 0xFF);
    out.scene_alias[10] = static_cast<char>((graph_h >> 8) & 0xFF);
  } else {
    copyString(in.scene_alias, out.scene_alias, sizeof(out.scene_alias));
  }
  if (in.type == TILE_IMAGE) {
    // TILE_IMAGE keeps image_path in file-based storage, not NVS.
    out.sensor_entity[0] = '\0';
    out.key_macro[0] = '\0';
    Serial.printf("[TileConfig] packTile - TILE_IMAGE: image_path='%s' (storage)\n",
                  in.image_path.c_str());
  } else {
    copyString(in.sensor_entity, out.sensor_entity, sizeof(out.sensor_entity));
    copyString(in.key_macro, out.key_macro, sizeof(out.key_macro));
  }
}

static bool looksLikeImagePath(const String& value) {
  if (value.length() == 0) return false;
  if (value.startsWith("/") || value.startsWith("__")) return true;
  if (value.startsWith("http://") || value.startsWith("https://")) return true;
  return false;
}

static void applyImagePathsFromSd(uint16_t folder_id, TileGridConfig& grid) {
  const bool have_storage = storageReady();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    Tile& tile = grid.tiles[i];
    if (tile.type != TILE_IMAGE && tile.type != TILE_SCENE) continue;
    String sd_path;
    if (have_storage && readImagePathSd(folder_id, i, sd_path)) {
      tile.image_path = sd_path;
      Serial.printf("[TileConfig] applyImagePaths folder=%u idx=%u type=%d -> '%s'\n",
        folder_id, (unsigned)i, tile.type, sd_path.c_str());
      continue;
    }
    if (have_storage && tile.image_path.length() > 0) {
      writeImagePathSd(folder_id, i, tile.image_path);
    }
    if (!have_storage) {
      tile.image_path = "";
    }
  }
}

static void applyLongEntityIdsFromSd(uint16_t folder_id, TileGridConfig& grid) {
  const bool have_storage = storageReady();
  if (!have_storage) return;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    Tile& tile = grid.tiles[i];
    if (!entityTileStoresSensorEntity(tile.type)) continue;
    String full_entity;
    if (readLongEntityIdSd(folder_id, i, full_entity)) {
      tile.sensor_entity = full_entity;
      Serial.printf("[TileConfig] applyLongEntity folder=%u idx=%u -> '%s'\n",
                    static_cast<unsigned>(folder_id),
                    static_cast<unsigned>(i),
                    full_entity.c_str());
    }
  }
}

static void unpackTileV7(const PackedTileV7& in, Tile& out) {
  TileType type = static_cast<TileType>(in.type);
  if (type == TILE_FOLDER) {
    if (in.sensor_decimals == LEGACY_NAV_KIND_SETTINGS) {
      type = TILE_SETTINGS;
    } else if (in.sensor_decimals == LEGACY_NAV_KIND_BACK) {
      type = TILE_BACK;
    }
  }
  out.type = type;
  out.bg_color = in.bg_color;
  out.background_opacity = in.reserved[0];
  out.view_id = static_cast<uint16_t>(in.reserved[1]) |
                (static_cast<uint16_t>(in.reserved[2]) << 8);
  out.col = (in.col < GRID_COLS) ? in.col : 0;
  out.row = (in.row < GRID_ROWS) ? in.row : 0;
  uint8_t span_w = (in.span_w < 1) ? 1 : in.span_w;
  uint8_t span_h = (in.span_h < 1) ? 1 : in.span_h;
  clamp_media_tile_layout(out.type, out.col, out.row, span_w, span_h);
  if (span_w > GRID_COLS - out.col) span_w = GRID_COLS - out.col;
  if (span_h > GRID_ROWS - out.row) span_h = GRID_ROWS - out.row;
  out.span_w = span_w;
  out.span_h = span_h;
  out.sensor_decimals = clampDecimals(in.sensor_decimals);
  if (out.type == TILE_FOLDER || out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.sensor_decimals = 0xFF;
  }
  out.sensor_value_font = clampSensorValueFont(in.sensor_value_font);
  out.sensor_display_mode = (in.sensor_gauge_enabled <= 2) ? in.sensor_gauge_enabled : 0;
  out.sensor_gauge_min = in.sensor_gauge_min;
  out.sensor_gauge_max = in.sensor_gauge_max;
  if (shouldNormalizeGaugeRange(out.type)) {
    normalizeGaugeRange(out.sensor_gauge_min, out.sensor_gauge_max);
  }
  out.sensor_gauge_arc = 100;
  out.sensor_gauge_size = 350;
  out.sensor_gauge_y_offset = 12;
  out.sensor_value_y_offset = 0;
  out.sensor_graph_height = 60;
  out.popup_open_mode = TILE_POPUP_OPEN_LONG_PRESS;
  if (out.type == TILE_SENSOR && in.scene_alias[0] == 0x01) {
    uint16_t arc = static_cast<uint8_t>(in.scene_alias[1]) |
                   (static_cast<uint8_t>(in.scene_alias[2]) << 8);
    if (arc >= 90 && arc <= 359) out.sensor_gauge_arc = arc;
    uint16_t size = static_cast<uint8_t>(in.scene_alias[3]) |
                    (static_cast<uint8_t>(in.scene_alias[4]) << 8);
    if (size >= 100 && size <= 800) out.sensor_gauge_size = size;
    int16_t y_off = static_cast<int16_t>(
        static_cast<uint8_t>(in.scene_alias[5]) |
        (static_cast<uint8_t>(in.scene_alias[6]) << 8));
    if (y_off >= -100 && y_off <= 200) out.sensor_gauge_y_offset = y_off;
    int16_t val_y_off = static_cast<int16_t>(
        static_cast<uint8_t>(in.scene_alias[7]) |
        (static_cast<uint8_t>(in.scene_alias[8]) << 8));
    if (val_y_off >= -100 && val_y_off <= 200) out.sensor_value_y_offset = val_y_off;
    uint16_t graph_h = static_cast<uint8_t>(in.scene_alias[9]) |
                       (static_cast<uint8_t>(in.scene_alias[10]) << 8);
    if (graph_h >= 20 && graph_h <= 200) out.sensor_graph_height = graph_h;
  }
  if (tileTypeStoresPopupMode(out.type) &&
      in.popup_open_mode == TILE_POPUP_OPEN_SHORT_PRESS) {
    out.popup_open_mode = TILE_POPUP_OPEN_SHORT_PRESS;
  }
  out.key_code = in.key_code;
  out.key_modifier = in.key_modifier;
  if (tileTypeStoresPopupModeDirectly(out.type)) {
    out.key_code = 0;
    out.key_modifier = 0;
  } else if (out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.key_code = 0;
    out.key_modifier = 0;
  }
  out.image_slideshow_sec = clampImageSlideshowSeconds(in.image_slideshow_sec);
  out.title = String(in.title);
  out.icon_name = String(in.icon_name);
  out.sensor_entity = String(in.sensor_entity);
  out.sensor_unit = String(in.sensor_unit);
  if (out.type == TILE_SENSOR) {
    out.scene_alias = "";
  } else {
    out.scene_alias = String(in.scene_alias);
  }
  out.key_macro = String(in.key_macro);
  if (out.type == TILE_IMAGE) {
    if (looksLikeImagePath(out.sensor_entity)) {
      out.image_path = out.sensor_entity;
    } else {
      out.image_path = out.key_macro;
    }
    out.key_macro = "";
    out.sensor_entity = "";
  } else {
    out.image_path = "";
  }
}

// Subset of unpackTileV7() for callers that only need type + entity id (see
// TileConfig::loadFolderGridEntitiesOnly). Skips title/icon_name/sensor_unit/
// scene_alias/key_macro/image_path -- the String allocations that dominate a
// full per-tile unpack (measured: ~44ms/grid across 35 tiles on this device's
// fragmented internal heap).
static void unpackTileEntityOnlyV7(const PackedTileV7& in, TileType& type, String& sensor_entity) {
  TileType t = static_cast<TileType>(in.type);
  if (t == TILE_FOLDER) {
    if (in.sensor_decimals == LEGACY_NAV_KIND_SETTINGS) {
      t = TILE_SETTINGS;
    } else if (in.sensor_decimals == LEGACY_NAV_KIND_BACK) {
      t = TILE_BACK;
    }
  }
  type = t;
  sensor_entity = String(in.sensor_entity);
}

static void unpackTileV6(const PackedTileV6& in, Tile& out) {
  TileType type = static_cast<TileType>(in.type);
  if (type == TILE_FOLDER) {
    if (in.sensor_decimals == LEGACY_NAV_KIND_SETTINGS) {
      type = TILE_SETTINGS;
    } else if (in.sensor_decimals == LEGACY_NAV_KIND_BACK) {
      type = TILE_BACK;
    }
  }
  out.type = type;
  out.bg_color = in.bg_color;
  out.col = (in.col < GRID_COLS) ? in.col : 0;
  out.row = (in.row < GRID_ROWS) ? in.row : 0;
  uint8_t span_w = (in.span_w < 1) ? 1 : in.span_w;
  uint8_t span_h = (in.span_h < 1) ? 1 : in.span_h;
  clamp_media_tile_layout(out.type, out.col, out.row, span_w, span_h);
  if (span_w > GRID_COLS - out.col) span_w = GRID_COLS - out.col;
  if (span_h > GRID_ROWS - out.row) span_h = GRID_ROWS - out.row;
  out.span_w = span_w;
  out.span_h = span_h;
  out.sensor_decimals = clampDecimals(in.sensor_decimals);
  if (out.type == TILE_FOLDER || out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.sensor_decimals = 0xFF;
  }
  out.sensor_value_font = clampSensorValueFont(in.sensor_value_font);
  out.sensor_display_mode = (in.sensor_gauge_enabled <= 2) ? in.sensor_gauge_enabled : 0;
  out.sensor_gauge_min = in.sensor_gauge_min;
  out.sensor_gauge_max = in.sensor_gauge_max;
  if (shouldNormalizeGaugeRange(out.type)) {
    normalizeGaugeRange(out.sensor_gauge_min, out.sensor_gauge_max);
  }
  // Read gauge appearance from scene_alias for TILE_SENSOR
  out.sensor_gauge_arc = 100;     // Default
  out.sensor_gauge_size = 350;    // Default
  out.sensor_gauge_y_offset = 12; // Default
  out.sensor_value_y_offset = 0;  // Default
  out.sensor_graph_height = 60;   // Default
  out.popup_open_mode = TILE_POPUP_OPEN_LONG_PRESS;
  if (out.type == TILE_SENSOR && in.scene_alias[0] == 0x01) {
    // Magic byte found, extract gauge appearance data
    uint16_t arc = static_cast<uint8_t>(in.scene_alias[1]) |
                   (static_cast<uint8_t>(in.scene_alias[2]) << 8);
    if (arc >= 90 && arc <= 359) out.sensor_gauge_arc = arc;
    uint16_t size = static_cast<uint8_t>(in.scene_alias[3]) |
                    (static_cast<uint8_t>(in.scene_alias[4]) << 8);
    if (size >= 100 && size <= 800) out.sensor_gauge_size = size;
    int16_t y_off = static_cast<int16_t>(
        static_cast<uint8_t>(in.scene_alias[5]) |
        (static_cast<uint8_t>(in.scene_alias[6]) << 8));
    if (y_off >= -100 && y_off <= 200) out.sensor_gauge_y_offset = y_off;
    int16_t val_y_off = static_cast<int16_t>(
        static_cast<uint8_t>(in.scene_alias[7]) |
        (static_cast<uint8_t>(in.scene_alias[8]) << 8));
    if (val_y_off >= -100 && val_y_off <= 200) out.sensor_value_y_offset = val_y_off;
    uint16_t graph_h = static_cast<uint8_t>(in.scene_alias[9]) |
                       (static_cast<uint8_t>(in.scene_alias[10]) << 8);
    if (graph_h >= 20 && graph_h <= 200) out.sensor_graph_height = graph_h;
  }
  out.key_code = in.key_code;
  out.key_modifier = in.key_modifier;
  if (out.type == TILE_SENSOR || out.type == TILE_WEATHER ||
      out.type == TILE_ENERGY || out.type == TILE_CLIMATE ||
      out.type == TILE_COVER) {
    if (in.key_code == TILE_POPUP_OPEN_SHORT_PRESS ||
        in.key_modifier == TILE_POPUP_OPEN_SHORT_PRESS) {
      out.popup_open_mode = TILE_POPUP_OPEN_SHORT_PRESS;
    }
    out.key_code = 0;
    out.key_modifier = 0;
  } else if (out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.key_code = 0;
    out.key_modifier = 0;
  }
  out.image_slideshow_sec = clampImageSlideshowSeconds(in.image_slideshow_sec);
  out.title = String(in.title);
  out.icon_name = String(in.icon_name);
  out.sensor_entity = String(in.sensor_entity);
  out.sensor_unit = String(in.sensor_unit);
  // For TILE_SENSOR, scene_alias stores gauge appearance, not a scene alias
  if (out.type == TILE_SENSOR) {
    out.scene_alias = "";
  } else {
    out.scene_alias = String(in.scene_alias);
  }
  out.key_macro = String(in.key_macro);
  if (out.type == TILE_IMAGE) {
    if (looksLikeImagePath(out.sensor_entity)) {
      out.image_path = out.sensor_entity;
    } else {
      out.image_path = out.key_macro;
    }
    out.key_macro = "";
    out.sensor_entity = "";
  } else {
    out.image_path = "";
  }
}

static void unpackTileV5(const PackedTileV5& in, Tile& out, uint8_t index) {
  out.type = static_cast<TileType>(in.type);
  if (out.type == TILE_FOLDER && in.sensor_decimals == LEGACY_TAB_SETTINGS) {
    out.type = TILE_SETTINGS;
  }
  out.bg_color = in.bg_color;
  // V5 had no position - migrate to grid position based on index (3x4 grid -> 4x4)
  uint8_t old_col = index % 3;
  uint8_t old_row = index / 3;
  out.col = old_col;
  out.row = old_row;
  out.span_w = 1;
  out.span_h = 1;
  out.sensor_decimals = clampDecimals(in.sensor_decimals);
  if (out.type == TILE_FOLDER || out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.sensor_decimals = 0xFF;
  }
  out.sensor_value_font = clampSensorValueFont(in.sensor_value_font);
  out.sensor_display_mode = (in.sensor_gauge_enabled != 0) ? 1 : 0;  // Legacy: map bool to mode
  out.sensor_gauge_min = in.sensor_gauge_min;
  out.sensor_gauge_max = in.sensor_gauge_max;
  if (shouldNormalizeGaugeRange(out.type)) {
    normalizeGaugeRange(out.sensor_gauge_min, out.sensor_gauge_max);
  }
  out.sensor_gauge_arc = 100;
  out.sensor_gauge_size = 350;
  out.sensor_gauge_y_offset = 12;
  out.sensor_value_y_offset = 0;
  out.sensor_graph_height = 60;
  out.key_code = in.key_code;
  out.key_modifier = in.key_modifier;
  if (out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.key_code = 0;
    out.key_modifier = 0;
  }
  out.image_slideshow_sec = clampImageSlideshowSeconds(in.image_slideshow_sec);
  out.title = String(in.title);
  out.icon_name = String(in.icon_name);
  out.sensor_entity = String(in.sensor_entity);
  out.sensor_unit = String(in.sensor_unit);
  out.scene_alias = String(in.scene_alias);
  out.key_macro = String(in.key_macro);
  // Element pool: TILE_IMAGE uses sensor_entity for image_path, falling back to legacy key_macro.
  if (out.type == TILE_IMAGE) {
    if (looksLikeImagePath(out.sensor_entity)) {
      out.image_path = out.sensor_entity;
    } else {
      out.image_path = out.key_macro;
    }
    out.key_macro = "";
    out.sensor_entity = "";
    Serial.printf("[TileConfig] unpackTile - TILE_IMAGE: packed(sensor)='%s', packed(macro)='%s', image_path='%s'\n",
                  in.sensor_entity, in.key_macro, out.image_path.c_str());
  } else {
    out.image_path = "";
  }
}

static void unpackTileV3(const PackedTileV3& in, Tile& out, uint8_t index) {
  out.type = static_cast<TileType>(in.type);
  if (out.type == TILE_FOLDER && in.sensor_decimals == LEGACY_TAB_SETTINGS) {
    out.type = TILE_SETTINGS;
  }
  out.bg_color = in.bg_color;
  out.col = index % 3;
  out.row = index / 3;
  out.span_w = 1;
  out.span_h = 1;
  out.sensor_decimals = clampDecimals(in.sensor_decimals);
  if (out.type == TILE_FOLDER || out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.sensor_decimals = 0xFF;
  }
  out.sensor_value_font = clampSensorValueFont(in.sensor_value_font);
  out.sensor_display_mode = 0;
  out.sensor_gauge_min = 0;
  out.sensor_gauge_max = 100;
  out.sensor_gauge_arc = 100;
  out.sensor_gauge_size = 350;
  out.sensor_gauge_y_offset = 12;
  out.sensor_value_y_offset = 0;
  out.sensor_graph_height = 60;
  out.key_code = in.key_code;
  out.key_modifier = in.key_modifier;
  if (out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.key_code = 0;
    out.key_modifier = 0;
  }
  out.image_slideshow_sec = clampImageSlideshowSeconds(in.image_slideshow_sec);
  out.title = String(in.title);
  out.icon_name = String(in.icon_name);
  out.sensor_entity = String(in.sensor_entity);
  out.sensor_unit = String(in.sensor_unit);
  out.scene_alias = String(in.scene_alias);
  out.key_macro = String(in.key_macro);
  // Element pool: TILE_IMAGE uses sensor_entity for image_path, falling back to legacy key_macro.
  if (out.type == TILE_IMAGE) {
    if (looksLikeImagePath(out.sensor_entity)) {
      out.image_path = out.sensor_entity;
    } else {
      out.image_path = out.key_macro;
    }
    out.key_macro = "";
    out.sensor_entity = "";
    Serial.printf("[TileConfig] unpackTileV3 - TILE_IMAGE: packed(sensor)='%s', packed(macro)='%s', image_path='%s'\n",
                  in.sensor_entity, in.key_macro, out.image_path.c_str());
  } else {
    out.image_path = "";
  }
}

static void unpackTileV2(const PackedTileV2& in, Tile& out, uint8_t index) {
  out.type = static_cast<TileType>(in.type);
  if (out.type == TILE_FOLDER && in.sensor_decimals == LEGACY_TAB_SETTINGS) {
    out.type = TILE_SETTINGS;
  }
  out.bg_color = in.bg_color;
  out.col = index % 3;
  out.row = index / 3;
  out.span_w = 1;
  out.span_h = 1;
  out.sensor_decimals = clampDecimals(in.sensor_decimals);
  if (out.type == TILE_FOLDER || out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.sensor_decimals = 0xFF;
  }
  out.sensor_value_font = clampSensorValueFont(in.sensor_value_font);
  out.sensor_display_mode = 0;
  out.sensor_gauge_min = 0;
  out.sensor_gauge_max = 100;
  out.sensor_gauge_arc = 100;
  out.sensor_gauge_size = 350;
  out.sensor_gauge_y_offset = 12;
  out.sensor_value_y_offset = 0;
  out.sensor_graph_height = 60;
  out.key_code = in.key_code;
  out.key_modifier = in.key_modifier;
  if (out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.key_code = 0;
    out.key_modifier = 0;
  }
  out.image_slideshow_sec = IMAGE_SLIDESHOW_DEFAULT_SEC;
  out.title = String(in.title);
  out.icon_name = String(in.icon_name);
  out.sensor_entity = String(in.sensor_entity);
  out.sensor_unit = String(in.sensor_unit);
  out.scene_alias = String(in.scene_alias);
  out.key_macro = String(in.key_macro);
  // Element pool: TILE_IMAGE uses sensor_entity for image_path, falling back to legacy key_macro.
  if (out.type == TILE_IMAGE) {
    if (looksLikeImagePath(out.sensor_entity)) {
      out.image_path = out.sensor_entity;
    } else {
      out.image_path = out.key_macro;
    }
    out.key_macro = "";
    out.sensor_entity = "";
    Serial.printf("[TileConfig] unpackTile - TILE_IMAGE: packed(sensor)='%s', packed(macro)='%s', image_path='%s'\n",
                  in.sensor_entity, in.key_macro, out.image_path.c_str());
  } else {
    out.image_path = "";
  }
}

static void unpackTileV4(const PackedTileV4& in, Tile& out, uint8_t index) {
  out.type = static_cast<TileType>(in.type);
  if (out.type == TILE_FOLDER && in.sensor_decimals == LEGACY_TAB_SETTINGS) {
    out.type = TILE_SETTINGS;
  }
  out.bg_color = in.bg_color;
  out.col = index % 3;
  out.row = index / 3;
  out.span_w = 1;
  out.span_h = 1;
  out.sensor_decimals = clampDecimals(in.sensor_decimals);
  if (out.type == TILE_FOLDER || out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.sensor_decimals = 0xFF;
  }
  out.sensor_value_font = clampSensorValueFont(in.sensor_value_font);
  out.sensor_display_mode = 0;
  out.sensor_gauge_min = 0;
  out.sensor_gauge_max = 100;
  out.sensor_gauge_arc = 100;
  out.sensor_gauge_size = 350;
  out.sensor_gauge_y_offset = 12;
  out.sensor_value_y_offset = 0;
  out.sensor_graph_height = 60;
  out.key_code = in.key_code;
  out.key_modifier = in.key_modifier;
  if (out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.key_code = 0;
    out.key_modifier = 0;
  }
  out.image_slideshow_sec = clampImageSlideshowSeconds(in.image_slideshow_sec);
  out.title = String(in.title);
  out.icon_name = String(in.icon_name);
  out.sensor_entity = String(in.sensor_entity);
  out.sensor_unit = String(in.sensor_unit);
  out.scene_alias = String(in.scene_alias);
  out.key_macro = String(in.key_macro);
  // Element pool: TILE_IMAGE uses sensor_entity for image_path, falling back to legacy key_macro.
  if (out.type == TILE_IMAGE) {
    if (looksLikeImagePath(out.sensor_entity)) {
      out.image_path = out.sensor_entity;
    } else {
      out.image_path = out.key_macro;
    }
    out.key_macro = "";
    out.sensor_entity = "";
    Serial.printf("[TileConfig] unpackTile - TILE_IMAGE: packed(sensor)='%s', packed(macro)='%s', image_path='%s'\n",
                  in.sensor_entity, in.key_macro, out.image_path.c_str());
  } else {
    out.image_path = "";
  }
}

static void unpackTileV1(const PackedTileV1& in, Tile& out, uint8_t index) {
  out.type = static_cast<TileType>(in.type);
  if (out.type == TILE_FOLDER && in.sensor_decimals == LEGACY_TAB_SETTINGS) {
    out.type = TILE_SETTINGS;
  }
  out.bg_color = in.bg_color;
  out.col = index % 3;
  out.row = index / 3;
  out.span_w = 1;
  out.span_h = 1;
  out.sensor_decimals = clampDecimals(in.sensor_decimals);
  if (out.type == TILE_FOLDER || out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.sensor_decimals = 0xFF;
  }
  out.sensor_value_font = 0;
  out.sensor_display_mode = 0;
  out.sensor_gauge_min = 0;
  out.sensor_gauge_max = 100;
  out.sensor_gauge_arc = 100;
  out.sensor_gauge_size = 350;
  out.sensor_gauge_y_offset = 12;
  out.sensor_value_y_offset = 0;
  out.sensor_graph_height = 60;
  out.key_code = in.key_code;
  out.key_modifier = in.key_modifier;
  if (out.type == TILE_SETTINGS || out.type == TILE_BACK) {
    out.key_code = 0;
    out.key_modifier = 0;
  }
  out.image_slideshow_sec = IMAGE_SLIDESHOW_DEFAULT_SEC;
  out.title = String(in.title);
  out.icon_name = String(in.icon_name);
  out.sensor_entity = String(in.sensor_entity);
  out.sensor_unit = String(in.sensor_unit);
  out.scene_alias = String(in.scene_alias);
  out.key_macro = String(in.key_macro);
  // Element pool: TILE_IMAGE uses sensor_entity for image_path, falling back to legacy key_macro.
  if (out.type == TILE_IMAGE) {
    if (looksLikeImagePath(out.sensor_entity)) {
      out.image_path = out.sensor_entity;
    } else {
      out.image_path = out.key_macro;
    }
    out.key_macro = "";
    out.sensor_entity = "";
  } else {
    out.image_path = "";
  }
}
static bool get_tile_layout_clamped(const Tile& tile, uint8_t& col, uint8_t& row, uint8_t& span_w, uint8_t& span_h) {
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

static void initGridDefaults(TileGridConfig& grid) {
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    grid.tiles[i] = Tile();
    grid.tiles[i].col = i % GRID_COLS;
    grid.tiles[i].row = i / GRID_COLS;
    grid.tiles[i].span_w = 1;
    grid.tiles[i].span_h = 1;
  }
}

static bool find_free_cell_top_left(const TileGridConfig& grid, uint8_t& out_col, uint8_t& out_row) {
  bool occupied[GRID_ROWS][GRID_COLS] = {};
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = grid.tiles[i];
    if (tile.type == TILE_EMPTY) continue;
    uint8_t col = 0;
    uint8_t row = 0;
    uint8_t span_w = 1;
    uint8_t span_h = 1;
    if (!get_tile_layout_clamped(tile, col, row, span_w, span_h)) continue;
    mark_occupied(occupied, col, row, span_w, span_h);
  }

  for (int r = 0; r < GRID_ROWS; ++r) {
    for (int c = 0; c < GRID_COLS; ++c) {
      if (!occupied[r][c]) {
        out_col = static_cast<uint8_t>(c);
        out_row = static_cast<uint8_t>(r);
        return true;
      }
    }
  }
  return false;
}

static bool settings_tile_rect_is_free(const TileGridConfig& grid,
                                       uint8_t col, uint8_t row,
                                       uint8_t span_w, uint8_t span_h) {
  if (span_w < 1 || span_h < 1 || col >= GRID_COLS || row >= GRID_ROWS ||
      span_w > GRID_COLS - col || span_h > GRID_ROWS - row) {
    return false;
  }
  bool occupied[GRID_ROWS][GRID_COLS] = {};
  for (const auto& tile : grid.tiles) {
    if (tile.type == TILE_EMPTY || tile.type == TILE_SETTINGS) continue;
    uint8_t tile_col = 0;
    uint8_t tile_row = 0;
    uint8_t tile_span_w = 1;
    uint8_t tile_span_h = 1;
    if (!get_tile_layout_clamped(tile, tile_col, tile_row, tile_span_w,
                                 tile_span_h)) {
      continue;
    }
    mark_occupied(occupied, tile_col, tile_row, tile_span_w, tile_span_h);
  }
  for (uint8_t check_row = row; check_row < row + span_h; ++check_row) {
    for (uint8_t check_col = col; check_col < col + span_w; ++check_col) {
      if (occupied[check_row][check_col]) return false;
    }
  }
  return true;
}

static bool find_settings_tile_rect_bottom_right(
    const TileGridConfig& grid, uint8_t span_w, uint8_t span_h,
    uint8_t& out_col, uint8_t& out_row) {
  if (span_w < 1 || span_h < 1 || span_w > GRID_COLS ||
      span_h > GRID_ROWS) {
    return false;
  }
  for (int row = GRID_ROWS - span_h; row >= 0; --row) {
    for (int col = GRID_COLS - span_w; col >= 0; --col) {
      if (settings_tile_rect_is_free(
              grid, static_cast<uint8_t>(col), static_cast<uint8_t>(row),
              span_w, span_h)) {
        out_col = static_cast<uint8_t>(col);
        out_row = static_cast<uint8_t>(row);
        return true;
      }
    }
  }
  return false;
}

static void collectFolderSubtree(const std::vector<FolderEntry>& entries, uint16_t parent_id, std::vector<uint16_t>& out) {
  for (const auto& entry : entries) {
    if (entry.parent_id != parent_id) continue;
    if (entry.id == parent_id) continue;
    out.push_back(entry.id);
    collectFolderSubtree(entries, entry.id, out);
  }
}

bool TileConfig::ensureSettingsTile(TileGridConfig& grid, int target_col,
                                    int target_row) {
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = grid.tiles[i];
    if (tile.type == TILE_SETTINGS) {
      return false;
    }
  }

  const SettingsTileSnapshot& snapshot =
      configManager.getConfig().settings_tile_snapshot;
  uint8_t span_w = snapshot.valid && snapshot.span_w >= 1
                       ? snapshot.span_w
                       : 1;
  uint8_t span_h = snapshot.valid && snapshot.span_h >= 1
                       ? snapshot.span_h
                       : 1;
  if (span_w > GRID_COLS) span_w = 1;
  if (span_h > GRID_ROWS) span_h = 1;

  uint8_t col = 0;
  uint8_t row = 0;
  const bool explicit_target = target_col >= 0 || target_row >= 0;
  if (explicit_target) {
    if (target_col < 0 || target_row < 0 || target_col >= GRID_COLS ||
        target_row >= GRID_ROWS ||
        !settings_tile_rect_is_free(
            grid, static_cast<uint8_t>(target_col),
            static_cast<uint8_t>(target_row), span_w, span_h)) {
      return false;
    }
    col = static_cast<uint8_t>(target_col);
    row = static_cast<uint8_t>(target_row);
  } else if (snapshot.valid && snapshot.col < GRID_COLS &&
             snapshot.row < GRID_ROWS &&
             settings_tile_rect_is_free(grid, snapshot.col, snapshot.row,
                                        span_w, span_h)) {
    col = snapshot.col;
    row = snapshot.row;
  } else if (!find_settings_tile_rect_bottom_right(grid, span_w, span_h,
                                                    col, row)) {
    return false;
  }

  size_t empty_index = TILES_PER_GRID;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (grid.tiles[i].type == TILE_EMPTY) {
      empty_index = i;
      break;
    }
  }
  if (empty_index >= TILES_PER_GRID) {
    return false;
  }

  Tile& tile = grid.tiles[empty_index];
  tile = Tile();
  tile.type = TILE_SETTINGS;
  tile.title = snapshot.valid && snapshot.title[0]
                   ? String(snapshot.title)
                   : String(i18n::strings(configManager.getConfig().language)
                                .tile_type_settings);
  tile.icon_name = snapshot.valid && snapshot.icon_name[0]
                       ? String(snapshot.icon_name)
                       : String("cog");
  tile.bg_color = snapshot.valid ? snapshot.bg_color : 0;
  tile.col = col;
  tile.row = row;
  tile.span_w = span_w;
  tile.span_h = span_h;
  return true;
}

bool TileConfig::removeSettingsTiles(TileGridConfig& grid) {
  bool changed = false;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (grid.tiles[i].type != TILE_SETTINGS) continue;
    grid.tiles[i] = Tile{};
    changed = true;
  }
  return changed;
}

bool TileConfig::applySettingsTilePolicy(TileGridConfig& grid) {
  if (configManager.getConfig().settings_tile_hidden) {
    return removeSettingsTiles(grid);
  }
  return ensureSettingsTile(grid);
}

bool TileConfig::ensureBackTile(uint16_t folder_id, TileGridConfig& grid) {
  if (folder_id == kRootFolderId) return false;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = grid.tiles[i];
    if (tile.type == TILE_BACK) {
      return false;
    }
  }

  uint8_t col = 0;
  uint8_t row = 0;
  if (!find_free_cell_top_left(grid, col, row)) {
    return false;
  }

  size_t empty_index = TILES_PER_GRID;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (grid.tiles[i].type == TILE_EMPTY) {
      empty_index = i;
      break;
    }
  }
  if (empty_index >= TILES_PER_GRID) {
    return false;
  }

  Tile& tile = grid.tiles[empty_index];
  tile = Tile();
  tile.type = TILE_BACK;
  tile.title = "";
  tile.icon_name = "arrow-left";
  tile.col = col;
  tile.row = row;
  tile.span_w = 1;
  tile.span_h = 1;
  return true;
}

static bool buildBlobKey(const char* prefix, char* out, size_t out_len) {
  if (!prefix || !out || out_len < 8) return false;
  int written = snprintf(out, out_len, "%s_blob", prefix);
  return written > 0 && static_cast<size_t>(written) < out_len;
}

static bool buildQuarterBlobKey(const char* prefix, uint8_t quarter, char* out, size_t out_len) {
  if (!prefix || !out || out_len < 8) return false;
  int written = snprintf(out, out_len, "%s_q%u", prefix, static_cast<unsigned>(quarter));
  return written > 0 && static_cast<size_t>(written) < out_len;
}

// Legacy loader for old key/value entries.
static bool loadGridLegacy(const char* prefix, TileGridConfig& grid) {
  Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, true)) {
    return false;
  }

  for (size_t i = 0; i < OLD_TILES_PER_GRID; ++i) {
    char key[32];

    snprintf(key, sizeof(key), "%s_t%u_type", prefix, static_cast<unsigned>(i));
    grid.tiles[i].type = static_cast<TileType>(prefs.getUChar(key, TILE_EMPTY));

    // Migrate position from index (3x4 grid)
    grid.tiles[i].col = i % 3;
    grid.tiles[i].row = i / 3;
    grid.tiles[i].span_w = 1;
    grid.tiles[i].span_h = 1;

    snprintf(key, sizeof(key), "%s_t%u_title", prefix, static_cast<unsigned>(i));
    grid.tiles[i].title = prefs.getString(key, "");

    snprintf(key, sizeof(key), "%s_t%u_color", prefix, static_cast<unsigned>(i));
    grid.tiles[i].bg_color = prefs.getUInt(key, 0);

    snprintf(key, sizeof(key), "%s_t%u_ent", prefix, static_cast<unsigned>(i));
    grid.tiles[i].sensor_entity = prefs.getString(key, "");

    snprintf(key, sizeof(key), "%s_t%u_unit", prefix, static_cast<unsigned>(i));
    grid.tiles[i].sensor_unit = prefs.getString(key, "");

    snprintf(key, sizeof(key), "%s_t%u_prec", prefix, static_cast<unsigned>(i));
    grid.tiles[i].sensor_decimals = clampDecimals(prefs.getUChar(key, 0xFF));
    grid.tiles[i].sensor_value_font = 0;
    grid.tiles[i].sensor_display_mode = 0;
    grid.tiles[i].sensor_gauge_min = 0;
    grid.tiles[i].sensor_gauge_max = 100;
    grid.tiles[i].sensor_gauge_arc = 100;
    grid.tiles[i].sensor_gauge_size = 350;
    grid.tiles[i].sensor_gauge_y_offset = 12;
    grid.tiles[i].sensor_value_y_offset = 0;
    grid.tiles[i].sensor_graph_height = 60;

    snprintf(key, sizeof(key), "%s_t%u_scene", prefix, static_cast<unsigned>(i));
    grid.tiles[i].scene_alias = prefs.getString(key, "");

    snprintf(key, sizeof(key), "%s_t%u_macro", prefix, static_cast<unsigned>(i));
    grid.tiles[i].key_macro = prefs.getString(key, "");

    snprintf(key, sizeof(key), "%s_t%u_code", prefix, static_cast<unsigned>(i));
    grid.tiles[i].key_code = prefs.getUChar(key, 0);

    snprintf(key, sizeof(key), "%s_t%u_mod", prefix, static_cast<unsigned>(i));
    grid.tiles[i].key_modifier = prefs.getUChar(key, 0);
    grid.tiles[i].image_slideshow_sec = IMAGE_SLIDESHOW_DEFAULT_SEC;
  }

  prefs.end();
  Serial.printf("[TileConfig] Grid '%s' loaded (legacy)\n", prefix);
  return true;
}

static void clearLegacyKeys(Preferences& prefs, const char* prefix) {
  char key[32];
  for (size_t i = 0; i < OLD_TILES_PER_GRID; ++i) {
    snprintf(key, sizeof(key), "%s_t%u_type", prefix, static_cast<unsigned>(i)); prefs.remove(key);
    snprintf(key, sizeof(key), "%s_t%u_title", prefix, static_cast<unsigned>(i)); prefs.remove(key);
    snprintf(key, sizeof(key), "%s_t%u_color", prefix, static_cast<unsigned>(i)); prefs.remove(key);
    snprintf(key, sizeof(key), "%s_t%u_ent", prefix, static_cast<unsigned>(i)); prefs.remove(key);
    snprintf(key, sizeof(key), "%s_t%u_unit", prefix, static_cast<unsigned>(i)); prefs.remove(key);
    snprintf(key, sizeof(key), "%s_t%u_prec", prefix, static_cast<unsigned>(i)); prefs.remove(key);
    snprintf(key, sizeof(key), "%s_t%u_scene", prefix, static_cast<unsigned>(i)); prefs.remove(key);
    snprintf(key, sizeof(key), "%s_t%u_macro", prefix, static_cast<unsigned>(i)); prefs.remove(key);
    snprintf(key, sizeof(key), "%s_t%u_code", prefix, static_cast<unsigned>(i)); prefs.remove(key);
    snprintf(key, sizeof(key), "%s_t%u_mod", prefix, static_cast<unsigned>(i)); prefs.remove(key);
  }
}

static void clearGridStorage(const char* prefix) {
  if (!prefix || !*prefix) return;
  Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    Serial.println("[TileConfig] WARN: NVS unavailable for deletion");
    return;
  }
  clearLegacyKeys(prefs, prefix);

  char key[16];
  for (size_t q = 0; q < QUARTERS_PER_GRID; ++q) {
    if (buildQuarterBlobKey(prefix, static_cast<uint8_t>(q), key, sizeof(key))) {
      prefs.remove(key);
    }
  }
  if (buildBlobKey(prefix, key, sizeof(key))) {
    prefs.remove(key);
  }
  prefs.end();
  Serial.printf("[TileConfig] Storage for '%s' deleted\n", prefix);
}

static void clearAllLegacyKeys() {
  Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    return;
  }
  clearLegacyKeys(prefs, "tab0");
  clearLegacyKeys(prefs, "tab1");
  clearLegacyKeys(prefs, "tab2");
  clearLegacyKeys(prefs, "home");   // Clean up old naming
  clearLegacyKeys(prefs, "game");
  clearLegacyKeys(prefs, "weather");
  prefs.end();
}

// Migrate old blob keys to new naming
static bool migrateOldBlobs() {
  Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    Serial.println("[TileConfig] Error: Could not open NVS for migration");
    return false;
  }

  // Always clean up old blob keys to free space
  bool had_old_blobs = false;
  if (prefs.isKey("home_blob")) {
    had_old_blobs = true;
    prefs.remove("home_blob");
    Serial.println("[TileConfig] Removed old home_blob");
  }
  if (prefs.isKey("game_blob")) {
    had_old_blobs = true;
    prefs.remove("game_blob");
    Serial.println("[TileConfig] Removed old game_blob");
  }
  if (prefs.isKey("weather_blob")) {
    had_old_blobs = true;
    prefs.remove("weather_blob");
    Serial.println("[TileConfig] Removed old weather_blob");
  }

  prefs.end();

  if (had_old_blobs) {
    Serial.println("[TileConfig] Old blobs cleaned up - NVS space freed");
  }

  return had_old_blobs;
}

#if 0
bool TileConfig::load() {
  migrateOldBlobs();  // Migrate old home/game/weather to tab0/tab1/tab2
  clearAllLegacyKeys();  // Clean up previous key/value layouts.
  bool tab0_ok = loadGrid("tab0", tab0_grid);
  bool tab1_ok = true;
  bool tab2_ok = true;
  if (ACTIVE_TILE_TABS > 1) {
    tab1_ok = loadGrid("tab1", tab1_grid);
  } else {
    initGridDefaults(tab1_grid);
    clearGridStorage("tab1");
  }
  if (ACTIVE_TILE_TABS > 2) {
    tab2_ok = loadGrid("tab2", tab2_grid);
  } else {
    initGridDefaults(tab2_grid);
    clearGridStorage("tab2");
  }
  loadTabNames();  // Load custom tab names
  if (add_settings_tile_if_missing(tab0_grid)) {
    if (saveGrid("tab0", tab0_grid)) {
      Serial.println("[TileConfig] Settings tile added");
    } else {
      Serial.println("[TileConfig] WARN: Settings tile could not be saved");
    }
  }
  return tab0_ok && tab1_ok && tab2_ok;
}

bool TileConfig::save(const TileGridConfig& tab0, const TileGridConfig& tab1, const TileGridConfig& tab2) {
  bool tab0_ok = saveGrid("tab0", tab0);
  bool tab1_ok = true;
  bool tab2_ok = true;
  if (ACTIVE_TILE_TABS > 1) {
    tab1_ok = saveGrid("tab1", tab1);
  } else {
    clearGridStorage("tab1");
  }
  if (ACTIVE_TILE_TABS > 2) {
    tab2_ok = saveGrid("tab2", tab2);
  } else {
    clearGridStorage("tab2");
  }

  if (tab0_ok && tab1_ok && tab2_ok) {
    tab0_grid = tab0;
    tab1_grid = tab1;
    tab2_grid = tab2;
    Serial.println("[TileConfig] Configuration saved");
    return true;
  }

  return false;
}

bool TileConfig::saveSingleGrid(const char* grid_name, const TileGridConfig& grid) {
  if (!grid_name || !*grid_name) {
    return false;
  }

  bool ok = false;
  if (strcmp(grid_name, "tab0") == 0) {
    ok = saveGrid("tab0", grid);
    if (ok) tab0_grid = grid;
  } else if (strcmp(grid_name, "tab1") == 0) {
    if (ACTIVE_TILE_TABS > 1) {
      ok = saveGrid("tab1", grid);
    } else {
      clearGridStorage("tab1");
      ok = true;
      Serial.println("[TileConfig] Tab1 disabled - storage skipped");
    }
    if (ok) tab1_grid = grid;
  } else if (strcmp(grid_name, "tab2") == 0) {
    if (ACTIVE_TILE_TABS > 2) {
      ok = saveGrid("tab2", grid);
    } else {
      clearGridStorage("tab2");
      ok = true;
      Serial.println("[TileConfig] Tab2 disabled - storage skipped");
    }
    if (ok) tab2_grid = grid;
  } else {
    return false;
  }

  if (ok) {
    Serial.printf("[TileConfig] Grid '%s' saved (single)\n", grid_name);
  }
  return ok;
}

bool TileConfig::loadGrid(const char* prefix, TileGridConfig& grid) {
  // Initialize all tiles to empty with default positions
  initGridDefaults(grid);

  // Try file-based storage first
  {
    PackedQuarterGridV6 packed_sd[QUARTERS_PER_GRID]{};
    if (readGridSd(prefix, packed_sd, QUARTERS_PER_GRID)) {
      for (size_t q = 0; q < QUARTERS_PER_GRID; ++q) {
        for (size_t i = 0; i < TILES_PER_QUARTER; ++i) {
          size_t grid_idx = quarterGridIndex(q, i);
          if (grid_idx >= TILES_PER_GRID) {
            continue;
          }
          unpackTileV6(packed_sd[q].tiles[i], grid.tiles[grid_idx]);
        }
      }
      Serial.printf("[TileConfig] Grid '%s' loaded (storage v%u)\n",
                    prefix, static_cast<unsigned>(packed_sd[0].version));
      applyImagePathsFromSd(prefix, grid);
      return true;
    }
  }

  // Try V6 split format first (four quarter blobs)
  char quarter_keys[QUARTERS_PER_GRID][16];
  bool have_quarter_keys = true;
  for (size_t q = 0; q < QUARTERS_PER_GRID; ++q) {
    if (!buildQuarterBlobKey(prefix, static_cast<uint8_t>(q), quarter_keys[q], sizeof(quarter_keys[q]))) {
      have_quarter_keys = false;
      break;
    }
  }

  if (have_quarter_keys) {
    Preferences prefs;
    if (prefs.begin(PREF_NAMESPACE, true)) {
      PackedQuarterGridV6 packed[QUARTERS_PER_GRID]{};
      bool ok = true;

      for (size_t q = 0; q < QUARTERS_PER_GRID; ++q) {
        size_t len = prefs.getBytesLength(quarter_keys[q]);
        if (len < sizeof(PackedQuarterGridV6)) {
          ok = false;
          break;
        }
        size_t read = prefs.getBytes(quarter_keys[q], &packed[q], sizeof(packed[q]));
        if (read != sizeof(packed[q]) ||
            packed[q].version != PACKED_GRID_VERSION ||
            packed[q].quarter_index != static_cast<uint8_t>(q)) {
          ok = false;
          break;
        }
      }

      if (ok) {
        for (size_t q = 0; q < QUARTERS_PER_GRID; ++q) {
          for (size_t i = 0; i < TILES_PER_QUARTER; ++i) {
            size_t grid_idx = quarterGridIndex(q, i);
            if (grid_idx >= TILES_PER_GRID) {
              continue;
            }
            unpackTileV6(packed[q].tiles[i], grid.tiles[grid_idx]);
          }
        }
        prefs.end();
        Serial.printf("[TileConfig] Grid '%s' loaded (quarters v%u)\n",
                      prefix, static_cast<unsigned>(packed[0].version));
        applyImagePathsFromSd(prefix, grid);
        if (saveGrid(prefix, grid)) {
          clearGridStorage(prefix);
        }
        return true;
      }
      prefs.end();
    }
  }

  // Fallback: Try old single-blob formats (V1-V5) for migration
  char blob_key[16];
  if (!buildBlobKey(prefix, blob_key, sizeof(blob_key))) {
    return false;
  }

  {
    Preferences prefs;
    if (prefs.begin(PREF_NAMESPACE, true)) {
      size_t blob_len = prefs.getBytesLength(blob_key);

      // V5 migration (old 12-tile format)
      if (blob_len >= sizeof(PackedGridV5)) {
        PackedGridV5 packed{};
        size_t read = prefs.getBytes(blob_key, &packed, sizeof(packed));
        if (read == sizeof(packed) && packed.version == 5) {
          for (size_t i = 0; i < OLD_TILES_PER_GRID; ++i) {
            unpackTileV5(packed.tiles[i], grid.tiles[i], static_cast<uint8_t>(i));
          }
          prefs.end();
          Serial.printf("[TileConfig] Grid '%s' loaded (blob v5, migrating to v6 quarters)\n", prefix);
          applyImagePathsFromSd(prefix, grid);
          if (saveGrid(prefix, grid)) {
            clearGridStorage(prefix);
          }
          return true;
        }
      }

      if (blob_len >= sizeof(PackedGridV4)) {
        PackedGridV4 packed{};
        size_t read = prefs.getBytes(blob_key, &packed, sizeof(packed));
        if (read == sizeof(packed) && packed.version == 4) {
          for (size_t i = 0; i < OLD_TILES_PER_GRID; ++i) {
            unpackTileV4(packed.tiles[i], grid.tiles[i], static_cast<uint8_t>(i));
          }
          prefs.end();
          Serial.printf("[TileConfig] Grid '%s' loaded (blob v4, migrating)\n", prefix);
          applyImagePathsFromSd(prefix, grid);
          if (saveGrid(prefix, grid)) {
            clearGridStorage(prefix);
          }
          return true;
        }
      }

      if (blob_len >= sizeof(PackedGridV3)) {
        PackedGridV3 packed{};
        size_t read = prefs.getBytes(blob_key, &packed, sizeof(packed));
        if (read == sizeof(packed) && packed.version == 3) {
          for (size_t i = 0; i < OLD_TILES_PER_GRID; ++i) {
            unpackTileV3(packed.tiles[i], grid.tiles[i], static_cast<uint8_t>(i));
          }
          prefs.end();
          Serial.printf("[TileConfig] Grid '%s' loaded (blob v3, migrating)\n", prefix);
          applyImagePathsFromSd(prefix, grid);
          if (saveGrid(prefix, grid)) {
            clearGridStorage(prefix);
          }
          return true;
        }
      }

      if (blob_len >= sizeof(PackedGridV2)) {
        PackedGridV2 packed{};
        size_t read = prefs.getBytes(blob_key, &packed, sizeof(packed));
        if (read == sizeof(packed) && packed.version == 2) {
          for (size_t i = 0; i < OLD_TILES_PER_GRID; ++i) {
            unpackTileV2(packed.tiles[i], grid.tiles[i], static_cast<uint8_t>(i));
          }
          prefs.end();
          Serial.printf("[TileConfig] Grid '%s' loaded (blob v2, migrating)\n", prefix);
          applyImagePathsFromSd(prefix, grid);
          if (saveGrid(prefix, grid)) {
            clearGridStorage(prefix);
          }
          return true;
        }
      }

      if (blob_len >= sizeof(PackedGridV1)) {
        PackedGridV1 packed{};
        size_t read = prefs.getBytes(blob_key, &packed, sizeof(packed));
        if (read == sizeof(packed) && packed.version == 1) {
          for (size_t i = 0; i < OLD_TILES_PER_GRID; ++i) {
            unpackTileV1(packed.tiles[i], grid.tiles[i], static_cast<uint8_t>(i));
          }
          prefs.end();
          Serial.printf("[TileConfig] Grid '%s' loaded (blob v1, migrating)\n", prefix);
          applyImagePathsFromSd(prefix, grid);
          if (saveGrid(prefix, grid)) {
            clearGridStorage(prefix);
          }
          return true;
        }
      }

      prefs.end();
    }
  }

  // Fall back to legacy keys and migrate them immediately.
  bool legacy_ok = loadGridLegacy(prefix, grid);
  if (legacy_ok) {
    applyImagePathsFromSd(prefix, grid);
    if (saveGrid(prefix, grid)) {
      clearGridStorage(prefix);
    }
  }
  return legacy_ok;
}

bool TileConfig::saveGrid(const char* prefix, const TileGridConfig& grid) {
  // V6 stored in file-based storage (single file with 4 quarters)
  if (!storageReady()) {
    Serial.println("[TileConfig] WARN: Storage unavailable, grid cannot be saved");
    return false;
  }

  Serial.printf("[TileConfig] Saving grid '%s' (storage, %u x %u bytes)\n",
                prefix,
                static_cast<unsigned>(QUARTERS_PER_GRID),
                static_cast<unsigned>(sizeof(PackedQuarterGridV6)));

  auto packed_storage =
      allocPackedGridScratch<PackedQuarterGridV6>(QUARTERS_PER_GRID, "Legacy-Save");
  PackedQuarterGridV6* packed = packed_storage.get();
  if (!packed) return false;
  ScopedStorageWriteDisplayGuard storage_write_guard;
  for (size_t q = 0; q < QUARTERS_PER_GRID; ++q) {
    packed[q].version = PACKED_GRID_VERSION;
    packed[q].quarter_index = static_cast<uint8_t>(q);
    for (size_t i = 0; i < TILES_PER_QUARTER; ++i) {
      size_t grid_idx = quarterGridIndex(q, i);
      if (grid_idx >= TILES_PER_GRID) {
        packed[q].tiles[i] = PackedTileV6{};
        continue;
      }
      if (grid.tiles[grid_idx].type == TILE_IMAGE || grid.tiles[grid_idx].type == TILE_SCENE) {
        if (!storageReady()) {
          Serial.println("[TileConfig] WARN: Storage unavailable, image_path will not be saved");
        } else if (!writeImagePathSd(prefix, grid_idx, grid.tiles[grid_idx].image_path)) {
          Serial.println("[TileConfig] WARN: image_path could not be saved to storage");
        }
      }
      packTile(grid.tiles[grid_idx], packed[q].tiles[i]);
    }
  }

  if (!writeGridSd(prefix, packed, QUARTERS_PER_GRID)) {
    Serial.printf("[TileConfig] Error saving grid '%s' (storage write failed)\n", prefix);
    return false;
  }

  Serial.printf("[TileConfig] Grid '%s' saved (storage, %u x %u bytes)\n",
                prefix,
                static_cast<unsigned>(QUARTERS_PER_GRID),
                static_cast<unsigned>(sizeof(PackedQuarterGridV6)));
  return true;
}
// ========== Tab Names (configurable via web interface) ==========

const char* TileConfig::getTabName(uint8_t tab_index) const {
  if (tab_index >= 4) return "";

  // Return the custom name, which may be empty.
  return tab_configs[tab_index].name;
}

void TileConfig::setTabName(uint8_t tab_index, const char* name) {
  if (tab_index >= 4 || !name) return;

  size_t len = strlen(name);
  if (len >= sizeof(tab_configs[0].name)) {
    len = sizeof(tab_configs[0].name) - 1;
  }

  memcpy(tab_configs[tab_index].name, name, len);
  tab_configs[tab_index].name[len] = '\0';
}

const char* TileConfig::getTabIcon(uint8_t tab_index) const {
  if (tab_index >= 4) return "";
  return tab_configs[tab_index].icon_name;
}

void TileConfig::setTabIcon(uint8_t tab_index, const char* icon_name) {
  if (tab_index >= 4 || !icon_name) return;

  size_t len = strlen(icon_name);
  if (len >= sizeof(tab_configs[0].icon_name)) {
    len = sizeof(tab_configs[0].icon_name) - 1;
  }

  memcpy(tab_configs[tab_index].icon_name, icon_name, len);
  tab_configs[tab_index].icon_name[len] = '\0';
}

bool TileConfig::loadTabNames() {
  Preferences prefs;
  if (!prefs.begin("tab5_config", true)) {  // Read-only
    return false;
  }

  for (uint8_t i = 0; i < 4; i++) {
    char key[16];

    // Load tab name
    snprintf(key, sizeof(key), "tab_name_%u", i);
    String name = prefs.getString(key, "");
    if (name.length() > 0) {
      setTabName(i, name.c_str());
    }

    // Load tab icon
    snprintf(key, sizeof(key), "tab_icon_%u", i);
    String icon = prefs.getString(key, "");
    if (icon.length() > 0) {
      setTabIcon(i, icon.c_str());
    }
  }

  prefs.end();
  Serial.println("[TileConfig] Tab names and icons loaded");
  return true;
}

bool TileConfig::saveTabNames() {
  Preferences prefs;
  if (!prefs.begin("tab5_config", false)) {  // Read-write
    return false;
  }

  for (uint8_t i = 0; i < 4; i++) {
    char key[16];

    // Save tab name
    snprintf(key, sizeof(key), "tab_name_%u", i);
    if (tab_configs[i].name[0] != '\0') {
      prefs.putString(key, tab_configs[i].name);
    } else {
      prefs.remove(key);  // Remove if empty (use default)
    }

    // Save tab icon
    snprintf(key, sizeof(key), "tab_icon_%u", i);
    if (tab_configs[i].icon_name[0] != '\0') {
      prefs.putString(key, tab_configs[i].icon_name);
    } else {
      prefs.remove(key);  // Remove if empty (no icon)
    }
  }

  prefs.end();
  Serial.println("[TileConfig] Tab names and icons saved");
  return true;
}
#endif

static FolderEntry makeFolderEntry(uint16_t id, uint16_t parent_id, const String& name, const String& icon) {
  FolderEntry entry{};
  entry.id = id;
  entry.parent_id = parent_id;
  String safe_name = name;
  safe_name.trim();
  if (safe_name.length() == 0) safe_name = "Ordner";
  String safe_icon = icon;
  safe_icon.trim();
  copyString(safe_name, entry.name, sizeof(entry.name));
  copyString(safe_icon, entry.icon_name, sizeof(entry.icon_name));
  return entry;
}

bool TileConfig::load() {
  folders.clear();
  bool folders_ok = loadFolders();
  bool had_root = folderExists(kRootFolderId);
  ensureRootFolder();
  if (folders_ok && !had_root) {
    saveFolders();
  }
  loadFolderAccess();

  active_folder_id = kRootFolderId;
  return loadGrid(active_folder_id, active_grid);
}

bool TileConfig::loadFolderGrid(uint16_t folder_id, TileGridConfig& out) {
  if (!folderExists(folder_id)) return false;
  bool ok = loadGrid(folder_id, out);
  if (ok && folder_id == active_folder_id) {
    active_grid = out;
  }
  return ok;
}

bool TileConfig::loadScreensaverGrid(TileGridConfig& out) {
  return loadGrid(kScreensaverGridStorageId, out, false);
}

bool TileConfig::loadFolderGridEntitiesOnly(uint16_t folder_id, TileEntitySlot* out, size_t count) {
  if (!folderExists(folder_id)) return false;
  if (count < TILES_PER_GRID) return false;

  auto packed_storage =
      allocPackedGridScratch<PackedQuarterGridV7>(QUARTERS_PER_GRID, "Entity-Load");
  PackedQuarterGridV7* packed_v7 = packed_storage.get();
  if (!packed_v7) return false;
  uint32_t t_read0 = millis();
  bool read_ok = readGridSd(folder_id, packed_v7, QUARTERS_PER_GRID);
  uint32_t read_ms = millis() - t_read0;
  if (!read_ok) {
    // Legacy/older grid version: fall back to the full loader. Rare in
    // practice (grids migrate to v7 on first save) -- correctness over
    // speed for this edge case.
    TileGridConfig full;
    if (!loadGrid(folder_id, full)) return false;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      out[i].type = full.tiles[i].type;
      out[i].sensor_entity = full.tiles[i].sensor_entity;
    }
    return true;
  }

  uint32_t t_unpack0 = millis();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    out[i] = TileEntitySlot{};
  }
  for (size_t q = 0; q < QUARTERS_PER_GRID; ++q) {
    for (size_t i = 0; i < TILES_PER_QUARTER; ++i) {
      size_t grid_idx = quarterGridIndex(q, i);
      if (grid_idx >= TILES_PER_GRID) continue;
      unpackTileEntityOnlyV7(packed_v7[q].tiles[i], out[grid_idx].type, out[grid_idx].sensor_entity);
    }
  }
  uint32_t unpack_ms = millis() - t_unpack0;
  uint32_t t_sidecar0 = millis();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (!entityTileStoresSensorEntity(out[i].type)) continue;
    String full_entity;
    if (readLongEntityIdSd(folder_id, i, full_entity)) {
      out[i].sensor_entity = full_entity;
    }
  }
  uint32_t sidecar_ms = millis() - t_sidecar0;
  if (read_ms + unpack_ms + sidecar_ms >= 5) {
    Serial.printf("[Bridge]     loadFolderGridEntitiesOnly(%u) split: read=%ums unpack=%ums sidecar=%ums\n",
                  static_cast<unsigned>(folder_id), (unsigned)read_ms, (unsigned)unpack_ms, (unsigned)sidecar_ms);
  }
  return true;
}

// === Folder entity cache (PSRAM) ===
// Keep each folder's TileEntitySlot projection (type + entity ID) in PSRAM.
// Background bridge-cache refreshes and MQTT route rebuilds then avoid reading
// every folder grid from flash on every pass (~20 ms per folder, measured as
// load=199 ms per refresh). Entries, pointer arrays and entity strings all live
// in PSRAM; no Arduino String or internal heap is used here.
struct FolderEntityCacheEntry {
  uint16_t folder_id;
  uint32_t built_gen;
  TileType types[TILES_PER_GRID];
  char* entities[TILES_PER_GRID];  // PSRAM copies; nullptr means empty.
};

// 128 entries x ~184 B = ~24 KB of PSRAM. Beyond 128 live folders,
// getFolderEntitiesCached returns false; callers treat this as a failed load.
static constexpr size_t kFolderEntityCacheMax = 128;

static char* psramStrdupLocal(const String& s) {
  if (!s.length()) return nullptr;
  char* p = static_cast<char*>(heap_caps_malloc(s.length() + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!p) return nullptr;
  memcpy(p, s.c_str(), s.length() + 1);
  return p;
}

FolderEntityCacheEntry* TileConfig::findFolderEntityCacheEntry(uint16_t folder_id) {
  for (size_t i = 0; i < folder_entity_cache_count_; ++i) {
    if (folder_entity_cache_[i].folder_id == folder_id) return &folder_entity_cache_[i];
  }
  return nullptr;
}

FolderEntityCacheEntry* TileConfig::storeFolderEntityCache(uint16_t folder_id,
                                                           const TileEntitySlot* slots,
                                                           uint32_t built_gen) {
  if (!folder_entity_cache_) {
    folder_entity_cache_ = static_cast<FolderEntityCacheEntry*>(heap_caps_calloc(
        kFolderEntityCacheMax, sizeof(FolderEntityCacheEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!folder_entity_cache_) {
      Serial.println("[TileConfig] WARN: Folder entity cache: PSRAM allocation failed");
      return nullptr;
    }
    folder_entity_cache_count_ = 0;
  }

  FolderEntityCacheEntry* e = findFolderEntityCacheEntry(folder_id);
  if (!e) {
    if (folder_entity_cache_count_ < kFolderEntityCacheMax) {
      e = &folder_entity_cache_[folder_entity_cache_count_];
      e->folder_id = folder_id;
      ++folder_entity_cache_count_;
    } else {
      // Full: reuse the entry of a folder that has since been deleted.
      for (size_t i = 0; i < folder_entity_cache_count_ && !e; ++i) {
        if (!folderExists(folder_entity_cache_[i].folder_id)) e = &folder_entity_cache_[i];
      }
      if (!e) {
        Serial.println("[TileConfig] WARN: Folder entity cache full, folder remains uncached");
        return nullptr;
      }
      e->folder_id = folder_id;
    }
  }

  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (e->entities[i]) {
      heap_caps_free(e->entities[i]);
      e->entities[i] = nullptr;
    }
    e->types[i] = slots[i].type;
    e->entities[i] = psramStrdupLocal(slots[i].sensor_entity);
  }
  e->built_gen = built_gen;
  return e;
}

bool TileConfig::getFolderEntitiesCached(uint16_t folder_id, FolderEntitySlotView* out, size_t count) {
  if (!out || count < TILES_PER_GRID) return false;
  if (!folderExists(folder_id)) return false;

  // Snapshot the generation BEFORE reading flash. If a writer invalidates
  // during the read, built_gen will differ from the current generation and
  // the next access will reload it.
  const uint32_t gen_now = folder_entity_cache_gen_;
  FolderEntityCacheEntry* e = findFolderEntityCacheEntry(folder_id);
  if (!e || e->built_gen != gen_now) {
    TileEntitySlot slots[TILES_PER_GRID];
    if (!loadFolderGridEntitiesOnly(folder_id, slots, TILES_PER_GRID)) return false;
    e = storeFolderEntityCache(folder_id, slots, gen_now);
    if (!e) return false;
  }

  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    out[i].type = e->types[i];
    out[i].entity = e->entities[i] ? e->entities[i] : "";
  }
  return true;
}

void TileConfig::invalidateFolderEntityCache() {
  // Cross-task invalidation only increments the counter; it frees or rebuilds
  // nothing, so the Web task may call it through saveFolderGrid as well.
  // Read and assign explicitly: ++ on volatile is deprecated in C++20 onward.
  folder_entity_cache_gen_ = folder_entity_cache_gen_ + 1;
}

// IDs are reserved durably before a grid write. A failed save may leave a gap,
// but deletion, restart and slot reuse can never redirect an old target.
static uint16_t reserveNavigationId(const char* key, uint16_t minimum = 1) {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(2000)) != pdTRUE) return 0;
  ScopedStorageWriteDisplayGuard guard;
  Preferences prefs;
  if (!prefs.begin("ht_view_ids", false)) { xSemaphoreGive(mutex); return 0; }
  const uint32_t next = std::max(prefs.getUInt(key, 1), uint32_t(minimum));
  const bool valid = next < TileConfig::kScreensaverGridStorageId;
  const bool saved = valid && prefs.putUInt(key, next + 1) == sizeof(uint32_t);
  prefs.end();
  xSemaphoreGive(mutex);
  if (!saved) Serial.println("[View] Cannot reserve a persistent navigation ID");
  return saved ? static_cast<uint16_t>(next) : 0;
}

static bool ensureNavigationIds(TileGridConfig& grid, bool& changed) {
  for (auto& tile : grid.tiles) {
    if (tile.type == TILE_EMPTY) {
      if (tile.view_id) changed = true;
      tile.view_id = 0;
    } else if (!tile.view_id) {
      tile.view_id = reserveNavigationId("tile_next");
      if (!tile.view_id) return false;
      changed = true;
    }
  }
  return true;
}

bool TileConfig::saveFolderGrid(uint16_t folder_id, TileGridConfig& grid) {
  if (!folderExists(folder_id)) return false;
  bool ids_changed = false;
  if (!ensureNavigationIds(grid, ids_changed)) return false;
  bool ok = saveGrid(folder_id, grid);
  if (ok && folder_id == active_folder_id) {
    // Keep the runtime cache identical to the policy-normalized grid that was
    // written. Normalize the existing member in place so this storage call
    // does not add another full TileGridConfig to the WebServer task stack.
    active_grid = grid;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      if (isRetiredTileType(active_grid.tiles[i].type)) {
        active_grid.tiles[i] = Tile{};
      }
    }
    if (folder_id == kRootFolderId) {
      applySettingsTilePolicy(active_grid);
    } else {
      ensureBackTile(folder_id, active_grid);
    }
  }
  return ok;
}

bool TileConfig::saveScreensaverGrid(const TileGridConfig& grid) {
  return saveGrid(kScreensaverGridStorageId, grid, false);
}

bool TileConfig::setActiveFolder(uint16_t folder_id) {
  if (!folderExists(folder_id)) return false;
  const uint16_t previous_folder_id = active_folder_id;
  if (!loadGrid(folder_id, active_grid)) {
    if (previous_folder_id != folder_id && folderExists(previous_folder_id)) {
      loadGrid(previous_folder_id, active_grid);
    }
    return false;
  }
  active_folder_id = folder_id;
  return true;
}

bool TileConfig::setActiveFolderCached(uint16_t folder_id, const TileGridConfig& grid) {
  if (!folderExists(folder_id)) return false;
  active_folder_id = folder_id;
  active_grid = grid;
  return true;
}

const FolderEntry* TileConfig::getFolder(uint16_t folder_id) const {
  for (const auto& entry : folders) {
    if (entry.id == folder_id) return &entry;
  }
  return nullptr;
}

uint16_t TileConfig::getFolderParent(uint16_t folder_id) const {
  const FolderEntry* entry = getFolder(folder_id);
  if (!entry) return kRootFolderId;
  return entry->parent_id;
}

bool TileConfig::folderExists(uint16_t folder_id) const {
  return getFolder(folder_id) != nullptr;
}

uint16_t TileConfig::nextFolderId() const {
  uint16_t max_id = kRootFolderId;
  for (const auto& entry : folders) {
    if (entry.id > max_id) max_id = entry.id;
  }
  if (max_id == 0xFFFF) return kInvalidFolderId;
  const uint16_t id = reserveNavigationId("folder_next", static_cast<uint16_t>(max_id + 1));
  return id ? id : kInvalidFolderId;
}

void TileConfig::ensureRootFolder() {
  if (folderExists(kRootFolderId)) return;
  folders.insert(folders.begin(), makeFolderEntry(kRootFolderId, kRootFolderId, "Home", "home-analytics"));
}

bool TileConfig::loadFolders() {
  if (!storageReady()) {
    Serial.println("[TileConfig] WARN: Storage unavailable, folder list cannot be loaded");
    return false;
  }

  auto read_folder_index = [](const String& path, std::vector<FolderEntry>& out) -> bool {
    File f = storageFS().open(path, FILE_READ);
    if (!f) return false;

    FolderIndexHeader header{};
    if (f.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
      f.close();
      return false;
    }
    if (header.magic != kFolderIndexMagic || header.version != kFolderIndexVersion) {
      f.close();
      return false;
    }
    if (header.count > 128) {
      f.close();
      return false;
    }

    out.clear();
    for (uint16_t i = 0; i < header.count; ++i) {
      FolderEntryDisk disk{};
      if (f.read(reinterpret_cast<uint8_t*>(&disk), sizeof(disk)) != sizeof(disk)) {
        f.close();
        out.clear();
        return false;
      }
      if (disk.id == kInvalidFolderId) {
        f.close();
        out.clear();
        return false;
      }
      FolderEntry entry{};
      entry.id = disk.id;
      entry.parent_id = disk.parent_id;
      memcpy(entry.name, disk.name, sizeof(entry.name));
      entry.name[sizeof(entry.name) - 1] = '\0';
      memcpy(entry.icon_name, disk.icon_name, sizeof(entry.icon_name));
      entry.icon_name[sizeof(entry.icon_name) - 1] = '\0';
      out.push_back(entry);
    }

    f.close();
    return true;
  };

  const String filePath(kFolderIndexFile);
  const String tmpPath = tmpPathFor(filePath);
  const String backupPath = backupPathFor(filePath);
  std::vector<FolderEntry> loaded;

  if (read_folder_index(tmpPath, loaded)) {
    folders = loaded;
    Serial.println("[TileConfig] Folder list restored from .tmp");
    promoteRecoveryFile(tmpPath, filePath);
    return true;
  }

  if (read_folder_index(filePath, loaded)) {
    folders = loaded;
    return true;
  }

  if (read_folder_index(backupPath, loaded)) {
    folders = loaded;
    Serial.println("[TileConfig] Folder list restored from .bak");
    promoteRecoveryFile(backupPath, filePath);
    return true;
  }

  return false;
}

bool TileConfig::saveFolders() const {
  if (!storageReady()) {
    Serial.println("[TileConfig] WARN: Storage unavailable, folder list cannot be saved");
    return false;
  }
  ScopedStorageWriteDisplayGuard storage_write_guard;
  if (!ensureTileGridDir()) return false;

  // Write atomically, as in writeGridSd: a partially written folder index
  // loses ALL folders. Complete the .tmp file before renaming it into place.
  const String filePath(kFolderIndexFile);
  const String tmpPath = tmpPathFor(filePath);
  if (storageFS().exists(tmpPath)) storageFS().remove(tmpPath);
  // See writeGridSd(): yield before writing flash to give the Wi-Fi/SDIO task
  // a chance to drain its receive queue under load.
  yield();
  File f = storageFS().open(tmpPath, FILE_WRITE);
  if (!f) return false;

  bool write_ok = true;
  FolderIndexHeader header{};
  header.magic = kFolderIndexMagic;
  header.version = kFolderIndexVersion;
  header.count = static_cast<uint16_t>(folders.size());
  if (f.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    write_ok = false;
  }

  for (const auto& entry : folders) {
    if (!write_ok) break;
    FolderEntryDisk disk{};
    disk.id = entry.id;
    disk.parent_id = entry.parent_id;
    memcpy(disk.name, entry.name, sizeof(disk.name));
    disk.name[sizeof(disk.name) - 1] = '\0';
    memcpy(disk.icon_name, entry.icon_name, sizeof(disk.icon_name));
    disk.icon_name[sizeof(disk.icon_name) - 1] = '\0';
    if (f.write(reinterpret_cast<const uint8_t*>(&disk), sizeof(disk)) != sizeof(disk)) {
      write_ok = false;
      break;
    }
  }

  f.flush();
  f.close();
  yield();

  if (!write_ok) {
    storageFS().remove(tmpPath);
    return false;
  }

  if (!replaceFileWithPreparedTmp(tmpPath, filePath)) {
    storageFS().remove(tmpPath);
    return false;
  }
  return true;
}

bool TileConfig::loadFolderAccess() {
  for (auto& entry : folders) {
    entry.pin_enabled = false;
    pin_access::clearCredential(entry.pin_salt, entry.pin_hash);
    pin_access::secureClear(entry.pin_value, sizeof(entry.pin_value));
  }
  if (!storageReady()) return false;

  auto read_access = [](const String& path,
                        std::vector<FolderAccessDisk>& records) -> bool {
    File file = storageFS().open(path, FILE_READ);
    if (!file) return false;
    FolderAccessHeader header{};
    if (file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) !=
            sizeof(header) ||
        header.magic != kFolderAccessMagic ||
        (header.version != 1 && header.version != kFolderAccessVersion) ||
        header.count > 128) {
      file.close();
      return false;
    }

    records.clear();
    records.reserve(header.count);
    for (uint16_t i = 0; i < header.count; ++i) {
      FolderAccessDisk disk{};
      if (header.version == 1) {
        FolderAccessDiskV1 legacy{};
        if (file.read(reinterpret_cast<uint8_t*>(&legacy), sizeof(legacy)) !=
            sizeof(legacy)) {
          file.close();
          return false;
        }
        disk.folder_id = legacy.folder_id;
        disk.enabled = legacy.enabled;
        memcpy(disk.salt, legacy.salt, sizeof(disk.salt));
        memcpy(disk.hash, legacy.hash, sizeof(disk.hash));
      } else if (file.read(reinterpret_cast<uint8_t*>(&disk), sizeof(disk)) !=
                 sizeof(disk)) {
          file.close();
          return false;
      }
      records.push_back(disk);
    }
    file.close();
    return true;
  };

  const String path(kFolderAccessFile);
  const String tmp_path = tmpPathFor(path);
  const String backup_path = backupPathFor(path);
  std::vector<FolderAccessDisk> records;
  bool loaded = false;
  if (read_access(tmp_path, records)) {
    promoteRecoveryFile(tmp_path, path);
    loaded = true;
  } else if (read_access(path, records)) {
    loaded = true;
  } else if (read_access(backup_path, records)) {
    promoteRecoveryFile(backup_path, path);
    loaded = true;
  }

  if (loaded) {
    for (const auto& disk : records) {
      for (auto& entry : folders) {
        if (entry.id != disk.folder_id || entry.id == kRootFolderId) continue;
        if (disk.enabled &&
            pin_access::credentialIsSet(disk.salt, disk.hash)) {
          entry.pin_enabled = true;
          memcpy(entry.pin_salt, disk.salt, sizeof(entry.pin_salt));
          memcpy(entry.pin_hash, disk.hash, sizeof(entry.pin_hash));
          char stored_pin_value[pin_access::kUserPinMaxDigits + 1]{};
          const size_t stored_pin_length =
              disk.pin_length >= pin_access::kUserPinMinDigits &&
                      disk.pin_length <= pin_access::kUserPinMaxDigits
                  ? disk.pin_length
                  : 0;
          if (stored_pin_length != 0) {
            memcpy(stored_pin_value, disk.pin_digits, stored_pin_length);
          }
          const String stored_pin(stored_pin_value);
          if (pin_access::isValidUserPin(stored_pin) &&
              pin_access::verifyCredential(stored_pin.c_str(), disk.salt,
                                           disk.hash)) {
            copyString(stored_pin, entry.pin_value,
                       sizeof(entry.pin_value));
          }
          pin_access::secureClear(stored_pin_value,
                                  sizeof(stored_pin_value));
        }
        break;
      }
    }
    return true;
  }

  // Missing access metadata means unlocked. A corrupt optional sidecar must
  // never make folders permanently inaccessible.
  return !storageFS().exists(path) && !storageFS().exists(tmp_path) &&
         !storageFS().exists(backup_path);
}

bool TileConfig::saveFolderAccess() const {
  if (!storageReady()) return false;
  ScopedStorageWriteDisplayGuard storage_write_guard;
  if (!ensureTileGridDir()) return false;

  uint16_t count = 0;
  for (const auto& entry : folders) {
    if (entry.id != kRootFolderId && entry.pin_enabled &&
        pin_access::credentialIsSet(entry.pin_salt, entry.pin_hash)) {
      ++count;
    }
  }

  const String path(kFolderAccessFile);
  const String tmp_path = tmpPathFor(path);
  if (storageFS().exists(tmp_path)) storageFS().remove(tmp_path);
  yield();
  File file = storageFS().open(tmp_path, FILE_WRITE);
  if (!file) return false;

  FolderAccessHeader header{kFolderAccessMagic, kFolderAccessVersion, count};
  bool ok = file.write(reinterpret_cast<const uint8_t*>(&header),
                       sizeof(header)) == sizeof(header);
  for (const auto& entry : folders) {
    if (!ok || entry.id == kRootFolderId || !entry.pin_enabled ||
        !pin_access::credentialIsSet(entry.pin_salt, entry.pin_hash)) {
      continue;
    }
    FolderAccessDisk disk{};
    disk.folder_id = entry.id;
    disk.enabled = 1;
    memcpy(disk.salt, entry.pin_salt, sizeof(disk.salt));
    memcpy(disk.hash, entry.pin_hash, sizeof(disk.hash));
    const size_t pin_length = strnlen(entry.pin_value,
                                     sizeof(entry.pin_value));
    if (pin_length >= pin_access::kUserPinMinDigits &&
        pin_length <= pin_access::kUserPinMaxDigits) {
      disk.pin_length = static_cast<uint8_t>(pin_length);
      memcpy(disk.pin_digits, entry.pin_value, pin_length);
    }
    ok = file.write(reinterpret_cast<const uint8_t*>(&disk), sizeof(disk)) ==
         sizeof(disk);
  }
  file.flush();
  file.close();
  yield();

  if (!ok || !replaceFileWithPreparedTmp(tmp_path, path)) {
    storageFS().remove(tmp_path);
    return false;
  }
  return true;
}

bool TileConfig::createFolder(uint16_t parent_id, const String& name, const String& icon, uint16_t& out_id) {
  if (!storageReady()) return false;
  ScopedStorageWriteDisplayGuard storage_write_guard;
  if (!folderExists(parent_id)) parent_id = kRootFolderId;
  uint16_t next_id = nextFolderId();
  if (next_id == kInvalidFolderId) return false;

  const std::vector<FolderEntry> original = folders;
  FolderEntry entry = makeFolderEntry(next_id, parent_id, name, icon);
  folders.push_back(entry);
  // Rewrite the access sidecar before accepting a reused folder ID. This
  // prevents stale credentials from a previously deleted highest ID from
  // being attached to the new, unlocked folder after a restart.
  if (!saveFolderAccess()) {
    folders = original;
    return false;
  }
  if (!saveFolders()) {
    folders = original;
    saveFolderAccess();
    return false;
  }

  std::unique_ptr<TileGridConfig> grid(new (std::nothrow) TileGridConfig{});
  if (!grid) return false;

  initGridDefaults(*grid);
  ensureBackTile(next_id, *grid);
  if (!saveGrid(next_id, *grid)) {
    return false;
  }

  out_id = next_id;
  return true;
}

bool TileConfig::updateFolder(uint16_t folder_id, const String& name, const String& icon) {
  for (auto& entry : folders) {
    if (entry.id != folder_id) continue;
    String safe_name = name;
    safe_name.trim();
    if (!safe_name.length()) safe_name = "Ordner";
    String safe_icon = icon;
    safe_icon.trim();
    copyString(safe_name, entry.name, sizeof(entry.name));
    copyString(safe_icon, entry.icon_name, sizeof(entry.icon_name));
    const bool saved = saveFolders();
    if (saved) view_revision_ = view_revision_ + 1;
    return saved;
  }
  return false;
}

bool TileConfig::isFolderPinEnabled(uint16_t folder_id) const {
  const FolderEntry* entry = getFolder(folder_id);
  return entry && entry->id != kRootFolderId && entry->pin_enabled &&
         pin_access::credentialIsSet(entry->pin_salt, entry->pin_hash);
}

bool TileConfig::setFolderPin(uint16_t folder_id, const String& pin) {
  if (folder_id == kRootFolderId || !pin_access::isValidUserPin(pin)) {
    return false;
  }
  for (auto& entry : folders) {
    if (entry.id != folder_id) continue;
    const FolderEntry original = entry;
    if (!pin_access::makeCredential(pin, entry.pin_salt, entry.pin_hash)) {
      return false;
    }
    copyString(pin, entry.pin_value, sizeof(entry.pin_value));
    entry.pin_enabled = true;
    if (!saveFolderAccess()) {
      entry = original;
      return false;
    }
    view_revision_ = view_revision_ + 1;
    return true;
  }
  return false;
}

bool TileConfig::clearFolderPin(uint16_t folder_id) {
  if (folder_id == kRootFolderId) return false;
  for (auto& entry : folders) {
    if (entry.id != folder_id) continue;
    if (!entry.pin_enabled) return true;
    const FolderEntry original = entry;
    entry.pin_enabled = false;
    pin_access::clearCredential(entry.pin_salt, entry.pin_hash);
    pin_access::secureClear(entry.pin_value, sizeof(entry.pin_value));
    if (!saveFolderAccess()) {
      entry = original;
      return false;
    }
    view_revision_ = view_revision_ + 1;
    return true;
  }
  return false;
}

bool TileConfig::verifyFolderPin(uint16_t folder_id, const char* pin) const {
  const FolderEntry* entry = getFolder(folder_id);
  if (!entry || !entry->pin_enabled) return true;
  return pin_access::isRecoveryPin(pin) ||
         pin_access::verifyCredential(pin, entry->pin_salt, entry->pin_hash);
}

bool TileConfig::getFolderPin(uint16_t folder_id, String& out) const {
  out = "";
  const FolderEntry* entry = getFolder(folder_id);
  if (!entry || !isFolderPinEnabled(folder_id) ||
      !pin_access::isValidUserPin(String(entry->pin_value)) ||
      !pin_access::verifyCredential(entry->pin_value, entry->pin_salt,
                                    entry->pin_hash)) {
    return false;
  }
  out = entry->pin_value;
  return true;
}

bool TileConfig::getSettingsTile(Tile& out) {
  TileGridConfig grid{};
  if (!loadGrid(kRootFolderId, grid, false)) return false;
  for (const auto& tile : grid.tiles) {
    if (tile.type != TILE_SETTINGS) continue;
    out = tile;
    return true;
  }
  return false;
}

SettingsTileVisibilityResult TileConfig::setSettingsTileVisible(
    bool visible, int target_col, int target_row) {
  TileGridConfig grid{};
  if (!loadGrid(kRootFolderId, grid, false)) {
    return SettingsTileVisibilityResult::StorageError;
  }

  bool changed = false;
  if (visible) {
    bool already_present = false;
    for (const auto& tile : grid.tiles) {
      if (tile.type == TILE_SETTINGS) {
        already_present = true;
        break;
      }
    }
    if (!already_present) {
      if (!ensureSettingsTile(grid, target_col, target_row)) {
        return SettingsTileVisibilityResult::NoFreeCell;
      }
      changed = true;
    }
  } else {
    changed = removeSettingsTiles(grid);
  }

  if (changed && !saveGrid(kRootFolderId, grid, false)) {
    return SettingsTileVisibilityResult::StorageError;
  }
  if (active_folder_id == kRootFolderId) active_grid = grid;
  return SettingsTileVisibilityResult::Success;
}

SettingsTileVisibilityResult TileConfig::validateSettingsTileVisible(
    bool visible, int target_col, int target_row) {
  TileGridConfig grid{};
  if (!loadGrid(kRootFolderId, grid, false)) {
    return SettingsTileVisibilityResult::StorageError;
  }
  if (!visible) return SettingsTileVisibilityResult::Success;

  for (const auto& tile : grid.tiles) {
    if (tile.type == TILE_SETTINGS) {
      return SettingsTileVisibilityResult::Success;
    }
  }
  return ensureSettingsTile(grid, target_col, target_row)
             ? SettingsTileVisibilityResult::Success
             : SettingsTileVisibilityResult::NoFreeCell;
}

bool TileConfig::deleteFolder(uint16_t folder_id) {
  view_revision_ = view_revision_ + 1;
  if (folder_id == kRootFolderId) return false;
  if (!folderExists(folder_id)) return false;
  if (!storageReady()) {
    Serial.println("[TileConfig] WARN: Storage unavailable, folder cannot be deleted");
    return false;
  }
  ScopedStorageWriteDisplayGuard storage_write_guard;

  std::vector<uint16_t> to_delete;
  to_delete.push_back(folder_id);
  collectFolderSubtree(folders, folder_id, to_delete);

  const std::vector<FolderEntry> original = folders;
  auto should_delete = [&](const FolderEntry& entry) {
    for (uint16_t id : to_delete) {
      if (entry.id == id) return true;
    }
    return false;
  };
  folders.erase(std::remove_if(folders.begin(), folders.end(), should_delete), folders.end());

  // Access metadata is committed first. If this fails, the still-persisted
  // folder index remains untouched and no folder ID can be reused with stale
  // credentials.
  if (!saveFolderAccess()) {
    folders = original;
    Serial.println("[TileConfig] WARN: Folder access metadata could not be updated");
    return false;
  }
  if (!saveFolders()) {
    folders = original;
    if (!saveFolderAccess()) {
      Serial.println("[TileConfig] ERROR: Folder access rollback failed");
    }
    Serial.println("[TileConfig] WARN: Folder index could not be saved");
    return false;
  }

  ensureSidecarIndexBuilt();
  for (uint16_t id : to_delete) {
    String grid_path = tileGridFile(id);
    if (storageFS().exists(grid_path)) storageFS().remove(grid_path);
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      String link_path = imagePathFile(id, i);
      if (storageFS().exists(link_path)) storageFS().remove(link_path);
      sidecarKeyRemove(g_image_sidecar_keys, sidecarKey(id, i));
      String entity_path = entityPathFile(id, i);
      if (storageFS().exists(entity_path)) storageFS().remove(entity_path);
      sidecarKeyRemove(g_entity_sidecar_keys, sidecarKey(id, i));
      writeLongTitleSd(id, i, "");
    }
  }

  for (uint16_t id : to_delete) {
    if (active_folder_id == id) {
      setActiveFolder(kRootFolderId);
      break;
    }
  }

  invalidateFolderEntityCache();
  return true;
}

bool TileConfig::loadGrid(uint16_t folder_id, TileGridConfig& grid,
                          bool ensure_navigation_tile) {
  initGridDefaults(grid);

  bool ok = false;
  bool needs_migration_save = false;

  auto packed_v7_storage =
      allocPackedGridScratch<PackedQuarterGridV7>(QUARTERS_PER_GRID, "Grid-Load-v7");
  PackedQuarterGridV7* packed_v7 = packed_v7_storage.get();
  if (!packed_v7) return false;
  if (readGridSd(folder_id, packed_v7, QUARTERS_PER_GRID)) {
    ok = true;
    for (size_t q = 0; q < QUARTERS_PER_GRID; ++q) {
      for (size_t i = 0; i < TILES_PER_QUARTER; ++i) {
        size_t grid_idx = quarterGridIndex(q, i);
        if (grid_idx >= TILES_PER_GRID) {
          continue;
        }
        unpackTileV7(packed_v7[q].tiles[i], grid.tiles[grid_idx]);
        // Early V7 files could still carry TILE_IMAGE paths inline. Promote
        // them to the sidecar format once, under the same guarded migration
        // transaction used for V6, instead of writing LittleFS during an
        // otherwise read-only folder load.
        if (grid.tiles[grid_idx].type == TILE_IMAGE &&
            grid.tiles[grid_idx].image_path.length() > 0) {
          needs_migration_save = true;
        }
      }
    }
    Serial.printf("[TileConfig] Grid %u loaded (storage v%u)\n",
                  static_cast<unsigned>(folder_id),
                  static_cast<unsigned>(packed_v7[0].version));
  } else {
    packed_v7_storage.reset();
    auto packed_v6_storage =
        allocPackedGridScratch<PackedQuarterGridV6>(QUARTERS_PER_GRID, "Grid-Load-v6");
    PackedQuarterGridV6* packed_v6 = packed_v6_storage.get();
    if (!packed_v6) return false;
    if (readGridSdV6(folder_id, packed_v6, QUARTERS_PER_GRID) ||
        (folder_id == kRootFolderId && storageReady() &&
         readPackedGridFileV6(tileGridFileLegacy("tab0"), packed_v6, QUARTERS_PER_GRID))) {
      ok = true;
      needs_migration_save = true;
      for (size_t q = 0; q < QUARTERS_PER_GRID; ++q) {
        for (size_t i = 0; i < TILES_PER_QUARTER; ++i) {
          size_t grid_idx = quarterGridIndex(q, i);
          if (grid_idx >= TILES_PER_GRID) {
            continue;
          }
          unpackTileV6(packed_v6[q].tiles[i], grid.tiles[grid_idx]);
        }
      }
      Serial.printf("[TileConfig] Grid %u loaded (storage v6)\n",
                    static_cast<unsigned>(folder_id));
    }
  }

  bool changed = false;
  if (ensure_navigation_tile) {
    if (folder_id == kRootFolderId) {
      changed = applySettingsTilePolicy(grid);
    } else {
      changed = ensureBackTile(folder_id, grid);
    }
  }

  if (!ok) {
    if (!anyGridFileExists(folder_id, folder_id == kRootFolderId)) {
      // A new folder has never been saved; the defaults above are valid.
      Serial.printf("[TileConfig] Grid %u is new (no file present), using defaults\n",
                    static_cast<unsigned>(folder_id));
    } else {
      Serial.printf("[TileConfig] WARN: Grid %u could not be loaded, storage contents remain unchanged\n",
                    static_cast<unsigned>(folder_id));
      return false;
    }
  }

#if defined(DEVICE_ESP32_S3_RGB_480)
  // V6 stores image paths inline. Migrating an old folder can therefore write
  // LittleFS sidecars before saveGrid() opens its own (nested) guard. Keep the
  // complete rare migration transaction protected without adding any cost to
  // normal V7 folder opens or popups.
  Device::ScopedStorageWrite migration_write(needs_migration_save);
#endif
  applyImagePathsFromSd(folder_id, grid);
  applyLongEntityIdsFromSd(folder_id, grid);
  applyLongTitlesFromSd(folder_id, grid);

  // Retired tile types become empty without renumbering the persisted enum.
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    if (isRetiredTileType(grid.tiles[i].type)) {
      grid.tiles[i] = Tile{};
      changed = true;
    }
  }

  if (folder_id != kScreensaverGridStorageId &&
      !ensureNavigationIds(grid, changed)) return false;
  if (needs_migration_save || changed) {
    if (!saveGrid(folder_id, grid, ensure_navigation_tile)) return false;
  }
  return true;
}

bool TileConfig::saveGrid(uint16_t folder_id, const TileGridConfig& grid,
                          bool ensure_navigation_tile) {
  if (!storageReady()) {
    Serial.println("[TileConfig] WARN: Storage unavailable, grid cannot be saved");
    return false;
  }

  TileGridConfig working = grid;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    working.tiles[i].title = hometiles_title::normalize(working.tiles[i].title.c_str()).c_str();
    if (isRetiredTileType(working.tiles[i].type)) {
      working.tiles[i] = Tile{};
    }
  }
  if (ensure_navigation_tile) {
    if (folder_id == kRootFolderId) {
      applySettingsTilePolicy(working);
    } else {
      ensureBackTile(folder_id, working);
    }
  }

  Serial.printf("[TileConfig] Saving grid %u (storage, %u x %u bytes)\n",
                static_cast<unsigned>(folder_id),
                static_cast<unsigned>(QUARTERS_PER_GRID),
                static_cast<unsigned>(sizeof(PackedQuarterGridV7)));

  auto packed_storage =
      allocPackedGridScratch<PackedQuarterGridV7>(QUARTERS_PER_GRID, "Grid-Save");
  PackedQuarterGridV7* packed = packed_storage.get();
  if (!packed) return false;
  for (size_t q = 0; q < QUARTERS_PER_GRID; ++q) {
    packed[q].version = PACKED_GRID_VERSION;
    packed[q].quarter_index = static_cast<uint8_t>(q);
    for (size_t i = 0; i < TILES_PER_QUARTER; ++i) {
      size_t grid_idx = quarterGridIndex(q, i);
      if (grid_idx >= TILES_PER_GRID) {
        packed[q].tiles[i] = PackedTileV7{};
        continue;
      }
      packTile(working.tiles[grid_idx], packed[q].tiles[i]);
    }
  }

#if defined(DEVICE_ESP32_S3_RGB_480)
  const bool legacy_v6_present =
      storageFS().exists(tileGridFileLegacyV6(folder_id));
  const bool legacy_root_present =
      folder_id == kRootFolderId &&
      storageFS().exists(tileGridFileLegacy("tab0"));
  if (!legacy_v6_present && !legacy_root_present &&
      packedGridMatchesStored(folder_id, packed, QUARTERS_PER_GRID) &&
      gridSidecarsMatchStored(folder_id, working)) {
    Serial.printf("[TileConfig] Grid %u unchanged, write skipped\n",
                  static_cast<unsigned>(folder_id));
    return true;
  }
#endif

  ScopedStorageWriteDisplayGuard storage_write_guard;
  for (size_t grid_idx = 0; grid_idx < TILES_PER_GRID; ++grid_idx) {
    const Tile& tile = working.tiles[grid_idx];
    if (!writeLongTitleSd(folder_id, grid_idx, tile.type == TILE_EMPTY ? String() : tile.title)) {
      Serial.println("[TileConfig] Error saving full tile title");
      invalidateFolderEntityCache();
      return false;
    }
    if (working.tiles[grid_idx].type == TILE_IMAGE ||
        working.tiles[grid_idx].type == TILE_SCENE) {
      if (!storageReady()) {
        Serial.println("[TileConfig] WARN: Storage unavailable, image_path will not be saved");
      } else if (!writeImagePathSd(folder_id, grid_idx,
                                   working.tiles[grid_idx].image_path)) {
        Serial.println("[TileConfig] WARN: image_path could not be saved to storage");
      }
    }
    if (entityTileStoresSensorEntity(working.tiles[grid_idx].type)) {
      if (!writeLongEntityIdSd(folder_id, grid_idx,
                               working.tiles[grid_idx].sensor_entity)) {
        Serial.println("[TileConfig] WARN: Long sensor_entity could not be saved to storage");
      }
    } else {
      writeLongEntityIdSd(folder_id, grid_idx, "");
    }
  }

  if (!writeGridSd(folder_id, packed, QUARTERS_PER_GRID)) {
    Serial.printf("[TileConfig] Error saving grid %u (storage write failed)\n",
                  static_cast<unsigned>(folder_id));
    // Entity sidecars above may already be written; invalidate the cache anyway.
    invalidateFolderEntityCache();
    return false;
  }
  // Invalidate AFTER the write completes. If the loop task concurrently builds
  // a cache entry from old data, its generation will differ and force a reload.
  invalidateFolderEntityCache();
  view_revision_ = view_revision_ + 1;

  String legacy_v6 = tileGridFileLegacyV6(folder_id);
  if (storageFS().exists(legacy_v6)) {
    storageFS().remove(legacy_v6);
  }
  if (folder_id == kRootFolderId) {
    String legacy_root = tileGridFileLegacy("tab0");
    if (storageFS().exists(legacy_root)) {
      storageFS().remove(legacy_root);
    }
  }

  Serial.printf("[TileConfig] Grid %u saved (storage, %u x %u bytes)\n",
                static_cast<unsigned>(folder_id),
                static_cast<unsigned>(QUARTERS_PER_GRID),
                static_cast<unsigned>(sizeof(PackedQuarterGridV7)));
  return true;
}

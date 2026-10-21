#include "src/tiles/icons/mdi_icon_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include <algorithm>

#include "src/core/firmware/firmware_version.h"
#include "src/core/firmware/github_update.h"
#include "src/devices/device.h"
#include "src/tiles/icons/mdi_icon_cache.h"
#include "src/tiles/icons/mdi_icons.h"

namespace mdi_icon_store {
namespace {

constexpr const char* kDownloadNvsNamespace = "icon_dl";
constexpr uint8_t kMaxDownloadAttempts = 3;
constexpr size_t kWriteSliceBytes = 16 * 1024;
constexpr size_t kVerifyChunkBytes = 4 * 1024;

State g_state = State::Missing;
File g_file;
char g_path[32] = "";
char g_part_path[40] = "";
char g_release_name[32] = "";

// The file being written.
File g_part;
uint32_t g_write_total = 0;
uint32_t g_written = 0;
uint32_t g_write_crc = 0;
uint8_t g_write_header[mdi_icon_cache::kHeaderBytes] = {};
String g_write_error;

// The Icons card: a requested download and an uploaded file in PSRAM.
bool g_download_requested = false;
uint8_t* g_upload = nullptr;
uint32_t g_upload_len = 0;
bool g_upload_ready = false;

const mdi_icon_font_data_t& font_data() { return *MDI_ICON_LOADER->data; }

// Runs on the LVGL thread when a glyph is drawn the first time.
bool read_bitmap(void*, uint32_t offset, uint8_t* out, uint32_t len) {
  if (!g_file) return false;
  if (!g_file.seek(mdi_icon_cache::kHeaderBytes + offset)) return false;
  return g_file.read(out, len) == len;
}

const mdi_icon_cache::Source kFileSource = {read_bitmap, nullptr};

// The bitmaps' CRC of a written file, read back from flash.
bool file_bitmap_crc(const char* path, uint32_t& crc_out) {
  File f = LittleFS.open(path, "r");
  if (!f || !f.seek(mdi_icon_cache::kHeaderBytes)) return false;
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kVerifyChunkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) return false;
  uint32_t crc = 0;
  size_t n = 0;
  while ((n = f.read(buf, kVerifyChunkBytes)) > 0) crc = mdi_icon_cache::crc32(crc, buf, n);
  heap_caps_free(buf);
  crc_out = crc;
  return true;
}

}  // namespace

void begin() {
  mdi_icon_loader_t* loader = MDI_ICON_LOADER;
  const mdi_icon_font_data_t& data = *loader->data;
  snprintf(g_path, sizeof(g_path), "/_mdi_icons_%u.bin", static_cast<unsigned>(data.px));
  snprintf(g_part_path, sizeof(g_part_path), "%s.part", g_path);
  snprintf(g_release_name, sizeof(g_release_name), "hometiles-icons-%u.bin", static_cast<unsigned>(data.px));

  // One block for the whole runtime, allocated before the UI so it never
  // splits the PSRAM that pictures and camera frames need later.
  const size_t bytes = mdi_icon_cache::block_bytes(data);
  void* block = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!block || !mdi_icon_cache::attach(loader, block, bytes, mdiUnknownIconCodepoint())) {
    if (block) heap_caps_free(block);
    Serial.printf("[Icons] No PSRAM for the icon cache (%u KB); %u built-in icons only\n",
                  static_cast<unsigned>(bytes / 1024), static_cast<unsigned>(data.core_count));
    g_state = State::Missing;
    return;
  }

  g_file = LittleFS.open(g_path, "r");
  if (!g_file) {
    Serial.printf("[Icons] No icon file %s; %u built-in icons\n", g_path,
                  static_cast<unsigned>(data.core_count));
    g_state = State::Missing;
    return;
  }
  uint8_t header[mdi_icon_cache::kHeaderBytes];
  const size_t got = g_file.read(header, sizeof(header));
  const mdi_icon_cache::HeaderCheck check =
      mdi_icon_cache::check_header(header, got, static_cast<uint32_t>(g_file.size()), data);
  if (check != mdi_icon_cache::HeaderCheck::Ok) {
    Serial.printf("[Icons] Icon file %s not used: %s; %u built-in icons\n", g_path,
                  mdi_icon_cache::header_check_text(check), static_cast<unsigned>(data.core_count));
    g_file.close();
    g_state = State::Invalid;
    return;
  }
  mdi_icon_cache::set_source(loader, &kFileSource);
  g_state = State::Ready;
  Serial.printf("[Icons] Icon file %s ready: %u icons, %u KB; cache %u KB in PSRAM\n", g_path,
                static_cast<unsigned>(data.glyph_count - 1), static_cast<unsigned>(data.bitmap_bytes / 1024),
                static_cast<unsigned>(bytes / 1024));
}

State state() { return g_state; }

const char* file_path() { return g_path; }

const char* release_file_name() { return g_release_name; }

uint32_t file_bytes() { return mdi_icon_cache::kHeaderBytes + font_data().bitmap_bytes; }

uint32_t icon_count() { return font_data().glyph_count - 1; }

uint32_t builtin_count() { return font_data().core_count; }

String release_file_url() {
  return String(GithubUpdate::kReleaseFilesRepoUrl) + "/releases/download/" + FW_VERSION + "/" + g_release_name;
}

void request_download() { g_download_requested = true; }

bool take_download_request() {
  const bool requested = g_download_requested;
  g_download_requested = false;
  return requested;
}

void upload_discard() {
  if (g_upload) heap_caps_free(g_upload);
  g_upload = nullptr;
  g_upload_len = 0;
  g_upload_ready = false;
}

bool upload_begin(String& error) {
  upload_discard();
  g_upload = static_cast<uint8_t*>(heap_caps_malloc(file_bytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_upload) {
    error = "not enough PSRAM";
    return false;
  }
  return true;
}

bool upload_write(const uint8_t* data, size_t len, String& error) {
  if (!g_upload) {
    error = "no icon upload started";
    return false;
  }
  if (g_upload_len + len > file_bytes()) {
    error = String("icon file has more than ") + file_bytes() + " bytes";
    return false;
  }
  const bool had_header = g_upload_len >= mdi_icon_cache::kHeaderBytes;
  memcpy(g_upload + g_upload_len, data, len);
  g_upload_len += len;
  if (!had_header && g_upload_len >= mdi_icon_cache::kHeaderBytes) {
    const mdi_icon_cache::HeaderCheck check =
        mdi_icon_cache::check_header(g_upload, mdi_icon_cache::kHeaderBytes, file_bytes(), font_data());
    if (check != mdi_icon_cache::HeaderCheck::Ok) {
      error = String("icon file not usable: ") + mdi_icon_cache::header_check_text(check);
      return false;
    }
  }
  return true;
}

bool upload_finish(String& error) {
  if (!g_upload || g_upload_len != file_bytes()) {
    error = String("icon file has ") + g_upload_len + " bytes, expected " + file_bytes();
    return false;
  }
  const uint32_t crc = mdi_icon_cache::crc32(0, g_upload + mdi_icon_cache::kHeaderBytes,
                                             g_upload_len - mdi_icon_cache::kHeaderBytes);
  if (crc != mdi_icon_cache::header_bitmap_crc(g_upload)) {
    error = "icon file damaged in transfer";
    return false;
  }
  g_upload_ready = true;
  return true;
}

bool upload_ready() { return g_upload_ready; }

bool install_upload(String& error) {
  if (!g_upload_ready) {
    error = "no icon file uploaded";
    return false;
  }
  // finish_write() reports a failed write() too.
  const bool begun = begin_write(g_upload_len, error);
  if (begun) write(g_upload, g_upload_len);
  const bool ok = begun && finish_write(error);
  upload_discard();
  return ok;
}

void release_file() {
  // The font shows the built-in icons until the restart.
  mdi_icon_cache::set_source(MDI_ICON_LOADER, nullptr);
  if (g_file) g_file.close();
  g_state = State::Missing;
}

bool begin_write(uint32_t total, String& error) {
  if (total != file_bytes()) {
    error = String("icon file has ") + total + " bytes, expected " + file_bytes();
    return false;
  }
  release_file();
  LittleFS.remove(g_path);
  LittleFS.remove(g_part_path);
  g_part = LittleFS.open(g_part_path, "w");
  if (!g_part) {
    error = "cannot create the icon file";
    return false;
  }
  g_write_total = total;
  g_written = 0;
  g_write_crc = 0;
  g_write_error = "";
  return true;
}

bool write(const uint8_t* data, size_t len) {
  if (!g_part || g_write_error.length()) return false;
  if (g_written + len > g_write_total) {
    g_write_error = "icon file too long";
    return false;
  }
  size_t body = 0;
  if (g_written < mdi_icon_cache::kHeaderBytes) {
    const size_t n = std::min<size_t>(len, mdi_icon_cache::kHeaderBytes - g_written);
    memcpy(g_write_header + g_written, data, n);
    body = n;
    if (g_written + n == mdi_icon_cache::kHeaderBytes) {
      const mdi_icon_cache::HeaderCheck check =
          mdi_icon_cache::check_header(g_write_header, sizeof(g_write_header), g_write_total, font_data());
      if (check != mdi_icon_cache::HeaderCheck::Ok) {
        g_write_error = String("icon file not usable: ") + mdi_icon_cache::header_check_text(check);
        return false;
      }
    }
  }
  if (len > body) g_write_crc = mdi_icon_cache::crc32(g_write_crc, data + body, len - body);
  // Short slices with a pause keep the network driver's reception going.
  for (size_t done = 0; done < len;) {
    const size_t slice = std::min(len - done, kWriteSliceBytes);
    if (g_part.write(data + done, slice) != slice) {
      g_write_error = "LittleFS write failed";
      return false;
    }
    done += slice;
    delay(2);
  }
  g_written += len;
  return true;
}

bool finish_write(String& error) {
  if (!g_part) {
    error = "no icon file being written";
    return false;
  }
  g_part.close();
  uint32_t written_crc = 0;
  if (g_write_error.length()) {
    error = g_write_error;
  } else if (g_written != g_write_total) {
    error = "icon file incomplete";
  } else if (g_write_crc != mdi_icon_cache::header_bitmap_crc(g_write_header)) {
    error = "icon file damaged in transfer";
  } else if (!file_bitmap_crc(g_part_path, written_crc) || written_crc != g_write_crc) {
    error = "icon file damaged on flash";
  } else if (!LittleFS.rename(g_part_path, g_path)) {
    error = "cannot rename the icon file";
  } else {
    Serial.printf("[Icons] Icon file %s written: %u bytes, CRC ok\n", g_path,
                  static_cast<unsigned>(g_written));
    return true;
  }
  LittleFS.remove(g_part_path);
  return false;
}

bool download_due() {
  if (g_state == State::Ready) return false;
  Preferences prefs;
  if (!prefs.begin(kDownloadNvsNamespace, true)) return true;
  const String version = prefs.getString("ver", "");
  const uint8_t attempts = prefs.getUChar("att", 0);
  prefs.end();
  return version != FW_VERSION || attempts < kMaxDownloadAttempts;
}

void note_download_attempt() {
  Device::ScopedStorageWrite storage_write;
  Preferences prefs;
  if (!prefs.begin(kDownloadNvsNamespace, false)) return;
  const bool same_version = prefs.getString("ver", "") == FW_VERSION;
  const uint8_t attempts = same_version ? prefs.getUChar("att", 0) : 0;
  // A reason from an older firmware no longer applies.
  if (!same_version && prefs.isKey("err")) prefs.remove("err");
  prefs.putString("ver", FW_VERSION);
  prefs.putUChar("att", attempts + 1);
  prefs.end();
  Serial.printf("[Icons] Download attempt %u of %u for %s\n", static_cast<unsigned>(attempts + 1),
                static_cast<unsigned>(kMaxDownloadAttempts), FW_VERSION);
}

void clear_download_attempts() {
  Device::ScopedStorageWrite storage_write;
  Preferences prefs;
  if (!prefs.begin(kDownloadNvsNamespace, false)) return;
  prefs.clear();
  prefs.end();
}

void note_failure(const String& reason) {
  Device::ScopedStorageWrite storage_write;
  Preferences prefs;
  if (!prefs.begin(kDownloadNvsNamespace, false)) return;
  prefs.putString("err", reason.substring(0, 96));
  prefs.end();
}

String last_failure() {
  Preferences prefs;
  if (!prefs.begin(kDownloadNvsNamespace, true)) return String();
  // isKey first: a missing key would log an NVS error on every page load.
  const String reason = prefs.isKey("err") ? prefs.getString("err", "") : String();
  prefs.end();
  return reason;
}

}  // namespace mdi_icon_store

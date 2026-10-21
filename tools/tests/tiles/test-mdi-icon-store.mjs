// The icon store writes a downloaded icon file to LittleFS: it must accept
// only a complete file of this panel's icon set whose bitmaps arrive and land
// on flash undamaged, replace the old file, and count download attempts per
// firmware version (three, then the panel stops trying until the next
// version). Runs the production mdi_icon_store.cpp against an in-memory
// LittleFS and NVS.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the icon store test needs LVGL and a host compiler');
  process.exit(0);
}

const out = path.join(root, 'build/tests/mdi-icon-store');
const shims = path.join(out, 'shims');
const shim = (relative, text) => {
  fs.mkdirSync(path.dirname(path.join(shims, relative)), {recursive: true});
  fs.writeFileSync(path.join(shims, relative), text);
};

shim('Arduino.h', String.raw`
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
class String : public std::string {
 public:
  String() = default;
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(unsigned v) : std::string(std::to_string(v)) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(unsigned long v) : std::string(std::to_string(v)) {}
  String(unsigned long long v) : std::string(std::to_string(v)) {}
  unsigned length() const { return static_cast<unsigned>(size()); }
  String substring(unsigned from, unsigned to) const {
    return from >= size() ? String() : String(substr(from, to > size() ? npos : to - from));
  }
};
inline String operator+(const String& a, const String& b) { return String(std::string(a) + std::string(b)); }
inline String operator+(const String& a, const char* b) { return String(std::string(a) + b); }
inline String operator+(const String& a, unsigned b) { return a + String(b); }
inline String operator+(const String& a, unsigned long b) { return a + String(b); }
inline String operator+(const String& a, unsigned long long b) { return a + String(b); }
struct SerialShim { template <typename... A> void printf(const char* f, A... a) { std::printf(f, a...); } };
inline SerialShim Serial;
inline void delay(unsigned) {}
`);
shim('LittleFS.h', String.raw`
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
struct MemFs {
  std::map<std::string, std::vector<uint8_t>> files;
  long flip_on_write = -1;  // byte offset damaged by the next writes ("flash damage")
};
inline MemFs g_fs;
class File {
 public:
  File() = default;
  File(std::string path, bool writing) : path_(std::move(path)), open_(true), writing_(writing) {}
  explicit operator bool() const { return open_; }
  size_t read(uint8_t* out, size_t len) {
    if (!open_) return 0;
    const auto& d = g_fs.files[path_];
    const size_t n = pos_ < d.size() ? std::min(len, d.size() - pos_) : 0;
    memcpy(out, d.data() + pos_, n);
    pos_ += n;
    return n;
  }
  size_t write(const uint8_t* data, size_t len) {
    if (!open_ || !writing_) return 0;
    auto& d = g_fs.files[path_];
    for (size_t i = 0; i < len; i++) {
      uint8_t b = data[i];
      if (static_cast<long>(d.size()) == g_fs.flip_on_write) b ^= 0x40;
      d.push_back(b);
    }
    return len;
  }
  bool seek(uint32_t pos) { pos_ = pos; return open_ && pos <= g_fs.files[path_].size(); }
  size_t size() const { return open_ ? g_fs.files[path_].size() : 0; }
  void close() { open_ = false; }
 private:
  std::string path_;
  bool open_ = false;
  bool writing_ = false;
  size_t pos_ = 0;
};
struct LittleFSShim {
  File open(const char* path, const char* mode) {
    if (mode[0] == 'w') { g_fs.files[path].clear(); return File(path, true); }
    if (!g_fs.files.count(path)) return File();
    return File(path, false);
  }
  bool remove(const char* path) { return g_fs.files.erase(path) > 0; }
  bool rename(const char* from, const char* to) {
    if (!g_fs.files.count(from)) return false;
    g_fs.files[to] = g_fs.files[from];
    g_fs.files.erase(from);
    return true;
  }
};
inline LittleFSShim LittleFS;
`);
shim('Preferences.h', String.raw`
#pragma once
#include <map>
#include <string>
inline std::map<std::string, std::map<std::string, std::string>> g_nvs;
class Preferences {
 public:
  bool begin(const char* ns, bool read_only) {
    if (read_only && !g_nvs.count(ns)) return false;
    ns_ = ns;
    g_nvs[ns_];
    return true;
  }
  String getString(const char* key, const char* fallback) {
    auto& n = g_nvs[ns_];
    return n.count(key) ? String(n[key]) : String(fallback);
  }
  uint8_t getUChar(const char* key, uint8_t fallback) {
    auto& n = g_nvs[ns_];
    return n.count(key) ? static_cast<uint8_t>(std::stoi(n[key])) : fallback;
  }
  size_t putString(const char* key, const char* value) { g_nvs[ns_][key] = value; return 1; }
  size_t putString(const char* key, const String& value) { g_nvs[ns_][key] = value; return 1; }
  bool isKey(const char* key) { return g_nvs[ns_].count(key) > 0; }
  bool remove(const char* key) { return g_nvs[ns_].erase(key) > 0; }
  size_t putUChar(const char* key, uint8_t value) { g_nvs[ns_][key] = std::to_string(value); return 1; }
  bool clear() { g_nvs[ns_].clear(); return true; }
  void end() {}
 private:
  std::string ns_;
};
`);
shim('esp_heap_caps.h', String.raw`
#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t n, unsigned) { return malloc(n); }
inline void heap_caps_free(void* p) { free(p); }
`);
shim('src/devices/device.h', String.raw`
#pragma once
inline int g_storage_depth = 0;
inline int g_storage_guards = 0;
namespace Device {
struct ScopedStorageWrite {
  ScopedStorageWrite() { ++g_storage_depth; ++g_storage_guards; }
  ~ScopedStorageWrite() { --g_storage_depth; }
};
}
`);
shim('src/tiles/icons/mdi_icons.h', String.raw`
#pragma once
#include "src/tiles/icons/mdi_icon_loader.h"
extern mdi_icon_loader_t g_test_loader;
#define MDI_ICON_LOADER (&g_test_loader)
inline uint32_t mdiUnknownIconCodepoint() { return 0xF02D8; }
`);

const harness = String.raw`
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <vector>
#include "src/core/firmware/firmware_version.h"
#include "src/devices/device.h"
#include "src/tiles/icons/mdi_icon_cache.h"
#include "src/tiles/icons/mdi_icon_store.h"

// A four-glyph icon set of 32 px.
static const lv_font_fmt_txt_glyph_dsc_t kGlyphs[4] = {
    {0, 0, 0, 0, 0, 0}, {0, 512, 8, 8, 0, 0}, {40, 512, 8, 8, 0, 0}, {75, 512, 8, 8, 0, 0}};
static const uint16_t kCoreIds[1] = {1};
static const uint32_t kCoreOffsets[2] = {0, 40};
static uint8_t kCoreBitmap[40];
static mdi_icon_font_data_t g_data = {32, 4, 120, 0, kGlyphs, 1, kCoreIds, kCoreOffsets, kCoreBitmap};
mdi_icon_loader_t g_test_loader = {{nullptr, 0}, &g_data, nullptr};

static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(v >> (8 * i)); }

static std::vector<uint8_t> icon_file(uint16_t px) {
  std::vector<uint8_t> f(32 + g_data.bitmap_bytes);
  for (uint32_t i = 0; i < g_data.bitmap_bytes; i++) f[32 + i] = static_cast<uint8_t>(i * 7 + 3);
  memcpy(f.data(), "HTIF", 4);
  f[4] = 1; f[5] = 0; f[6] = static_cast<uint8_t>(px); f[7] = 0;
  put32(&f[8], g_data.glyph_count);
  put32(&f[12], g_data.bitmap_bytes);
  put32(&f[16], g_data.table_crc);
  put32(&f[20], mdi_icon_cache::crc32(0, f.data() + 32, g_data.bitmap_bytes));
  put32(&f[24], 0);
  put32(&f[28], mdi_icon_cache::crc32(0, f.data(), 28));
  return f;
}

// Writes f in uneven pieces like downloaded stage blocks; the result of finish_write.
static bool install(const std::vector<uint8_t>& f, String& error) {
  if (!mdi_icon_store::begin_write(static_cast<uint32_t>(f.size()), error)) return false;
  const size_t pieces[] = {7, 30, 1000};
  size_t pos = 0;
  for (size_t piece : pieces) {
    const size_t n = std::min(piece, f.size() - pos);
    if (n && !mdi_icon_store::write(f.data() + pos, n)) break;
    pos += n;
  }
  if (pos < f.size()) mdi_icon_store::write(f.data() + pos, f.size() - pos);
  return mdi_icon_store::finish_write(error);
}

int main() {
  g_data.table_crc = 0x12345678;
  mdi_icon_store::begin();
  const std::string path = mdi_icon_store::file_path();
  const std::string part = path + ".part";
  int r = 0;
  auto check = [&](bool ok, const char* what) { if (!ok) { std::printf("FAIL %s\n", what); r = 1; } };

  check(mdi_icon_store::state() == mdi_icon_store::State::Missing, "missing without a file");
  check(std::string(mdi_icon_store::release_file_name()) == "hometiles-icons-32.bin", "release file name");
  check(mdi_icon_store::file_bytes() == 152, "file bytes");

  // Attempts: three per firmware version, counted under the storage guard.
  check(mdi_icon_store::download_due(), "due without a record");
  for (int i = 0; i < 3; i++) mdi_icon_store::note_download_attempt();
  check(!mdi_icon_store::download_due(), "not due after three attempts");
  check(g_storage_depth == 0 && g_storage_guards == 3, "NVS writes under the storage guard");
  g_nvs["icon_dl"]["ver"] = "v0.0.1";
  check(mdi_icon_store::download_due(), "due again for a new firmware version");
  mdi_icon_store::note_download_attempt();
  check(g_nvs["icon_dl"]["att"] == "1" && g_nvs["icon_dl"]["ver"] == FW_VERSION, "count restarts per version");
  mdi_icon_store::clear_download_attempts();
  check(mdi_icon_store::download_due(), "due after clearing");

  // The Icons card's reason: kept until a written file clears it, dropped by
  // a new firmware version.
  check(mdi_icon_store::last_failure().empty(), "no reason without a failure");
  mdi_icon_store::note_failure("Download failed: no network");
  check(mdi_icon_store::last_failure() == "Download failed: no network", "reason kept");
  g_nvs["icon_dl"]["ver"] = "v0.0.1";
  mdi_icon_store::note_download_attempt();
  check(mdi_icon_store::last_failure().empty(), "a new firmware drops an old reason");
  mdi_icon_store::note_failure("Install failed: LittleFS write failed");
  mdi_icon_store::clear_download_attempts();
  check(mdi_icon_store::last_failure().empty(), "a written file clears the reason");

  String error;
  // Wrong length, foreign size, transfer damage, flash damage: nothing stays.
  g_fs.files[path] = {1, 2, 3};
  check(!mdi_icon_store::begin_write(151, error), "wrong length refused");
  check(g_fs.files.count(path) == 1, "old file kept when the length is wrong");
  check(!install(icon_file(48), error) && error.find("another size") != std::string::npos, "48 px file refused");
  check(!g_fs.files.count(part) && !g_fs.files.count(path), "nothing left after a refused file");
  auto damaged = icon_file(32);
  damaged[100] ^= 1;
  check(!install(damaged, error) && error == "icon file damaged in transfer", "transfer damage found");
  g_fs.flip_on_write = 90;
  check(!install(icon_file(32), error) && error == "icon file damaged on flash", "flash damage found");
  g_fs.flip_on_write = -1;
  check(!g_fs.files.count(part) && !g_fs.files.count(path), "damaged files removed");

  // An upload from the Icons card: held in PSRAM, the header checked as soon
  // as it arrived, the CRC and length at the end, then installed.
  check(mdi_icon_store::upload_begin(error), "upload starts");
  auto foreign = icon_file(48);
  check(!mdi_icon_store::upload_write(foreign.data(), 40, error) && error.find("another size") != std::string::npos,
        "a foreign header is refused at once");
  mdi_icon_store::upload_discard();
  auto upload_all = [&](const std::vector<uint8_t>& f) {
    if (!mdi_icon_store::upload_begin(error)) return false;
    const size_t pieces[] = {10, 30, 1000};
    size_t pos = 0;
    for (size_t piece : pieces) {
      const size_t n = std::min(piece, f.size() - pos);
      if (n && !mdi_icon_store::upload_write(f.data() + pos, n, error)) return false;
      pos += n;
    }
    return mdi_icon_store::upload_finish(error);
  };
  auto broken = icon_file(32);
  broken[120] ^= 1;
  check(!upload_all(broken) && error == "icon file damaged in transfer", "upload damage found");
  check(!mdi_icon_store::upload_ready(), "a damaged upload is not installed");
  auto good_upload = icon_file(32);
  check(mdi_icon_store::upload_begin(error) && mdi_icon_store::upload_write(good_upload.data(), 100, error) &&
            !mdi_icon_store::upload_finish(error) && error.find("expected 152") != std::string::npos,
        "a short upload is refused");
  check(mdi_icon_store::upload_begin(error) && mdi_icon_store::upload_write(good_upload.data(), 152, error) &&
            !mdi_icon_store::upload_write(good_upload.data(), 1, error),
        "a long upload is refused");
  g_fs.files[path] = {7, 7};
  check(upload_all(good_upload) && mdi_icon_store::upload_ready(), "a complete upload is ready");
  check(mdi_icon_store::install_upload(error) && g_fs.files[path] == good_upload, "the upload is installed");
  check(!mdi_icon_store::upload_ready(), "the installed upload is released");

  // A good file replaces the old one and is used after the restart.
  g_fs.files[path] = {9, 9, 9};
  check(install(icon_file(32), error), "good file installed");
  check(g_fs.files.count(path) && !g_fs.files.count(part) && g_fs.files[path] == icon_file(32), "file in place");
  mdi_icon_store::begin();
  check(mdi_icon_store::state() == mdi_icon_store::State::Ready, "ready after the restart");
  check(!mdi_icon_store::download_due(), "not due with a valid file");
  check(mdi_icon_cache::has_source(&g_test_loader), "font reads the file");
  std::printf("%s\n", r ? "FAILED" : "PASS");
  return r;
}
`;

fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'harness.cpp');
fs.writeFileSync(source, harness);
const binary = path.join(out, process.platform === 'win32' ? 'harness.exe' : 'harness');
const build = spawnSync(host.cxx, ['-std=c++17', '-O1', '-I', shims, ...host.flags, source,
  path.join(root, 'src/tiles/icons/mdi_icon_store.cpp'), path.join(root, 'src/tiles/icons/mdi_icon_cache.cpp'),
  host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(build.status, 0, build.stdout + build.stderr);
const run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
assert.match(run.stdout, /PASS/);
console.log('PASS: icon store installs only complete, undamaged icon files and counts download attempts per version');

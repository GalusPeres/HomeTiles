#include "src/web/server/web_admin.h"
#include "src/web/server/web_admin_utils.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <vector>
#include <cerrno>
#include <stdlib.h>
#include <string.h>
#include <lwip/sockets.h>
#include <LittleFS.h>
#include "src/core/firmware/firmware_version.h"
#include "src/tiles/icons/mdi_icon_store.h"
#include "src/web/server/handlers/web_admin_handler_utils.h"

using namespace web_admin_handlers;

namespace {

String joinPath(const String& dir, const String& name) {
  if (name.startsWith("/")) return name;
  if (dir == "/") return String("/") + name;
  return dir + "/" + name;
}

String normalizeFileManagerPath(const String& raw) {
  String path = raw;
  path.trim();
  path.replace("\\", "/");
  const int query = path.indexOf('?');
  if (query >= 0) path = path.substring(0, query);
  const int hash = path.indexOf('#');
  if (hash >= 0) path = path.substring(0, hash);
  if (!path.length()) path = "/";
  if (!path.startsWith("/")) path = "/" + path;
  while (path.indexOf("//") >= 0) {
    path.replace("//", "/");
  }
  while (path.length() > 1 && path.endsWith("/")) {
    path.remove(path.length() - 1);
  }
  return path;
}

bool isSafeFileManagerName(const String& name) {
  if (!name.length() || name.length() > 96) return false;
  if (name == "." || name == "..") return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name.charAt(i);
    if (c == '/' || c == '\\' || static_cast<uint8_t>(c) < 32) return false;
  }
  return true;
}

bool isSafeFileManagerPath(const String& path) {
  if (!path.length() || path.charAt(0) != '/' || path.length() > 192) return false;
  if (path == "/") return true;

  int start = 1;
  while (start < static_cast<int>(path.length())) {
    int slash = path.indexOf('/', start);
    if (slash < 0) slash = path.length();
    const String part = path.substring(start, slash);
    if (!isSafeFileManagerName(part)) return false;
    start = slash + 1;
  }
  return true;
}

String fileManagerBaseName(const String& path) {
  String normalized = normalizeFileManagerPath(path);
  if (normalized == "/") return "";
  const int slash = normalized.lastIndexOf('/');
  return slash >= 0 ? normalized.substring(slash + 1) : normalized;
}

String fileManagerParentPath(const String& path) {
  String normalized = normalizeFileManagerPath(path);
  if (normalized == "/") return "/";
  const int slash = normalized.lastIndexOf('/');
  if (slash <= 0) return "/";
  return normalized.substring(0, slash);
}

String fileManagerEntryName(const char* raw_name) {
  String name = raw_name ? String(raw_name) : String();
  name.replace("\\", "/");
  while (name.endsWith("/") && name.length() > 1) {
    name.remove(name.length() - 1);
  }
  const int slash = name.lastIndexOf('/');
  if (slash >= 0) {
    name = name.substring(slash + 1);
  }
  name.trim();
  return name;
}

String sanitizeFileManagerUploadName(const String& raw_name) {
  String name = fileManagerEntryName(raw_name.c_str());
  if (!name.length() || name == "." || name == "..") {
    return "";
  }
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name.charAt(i);
    if (c == '/' || c == '\\' || static_cast<uint8_t>(c) < 32) {
      name.setCharAt(i, '_');
    }
  }
  if (name.length() > 96) {
    name = name.substring(0, 96);
  }
  return isSafeFileManagerName(name) ? name : "";
}

bool resolveFileManagerFsByKey(const String& raw_key, fs::FS*& out_fs, String& out_key, String& error) {
  String key = raw_key;
  key.trim();
  key.toLowerCase();
  if (!key.length()) key = "sd";

  if (key == "sd" || key == "sdcard") {
    if (!sdReady()) {
      error = "microSD card is not available";
      return false;
    }
    out_fs = &sdFS();
    out_key = "sd";
    return true;
  }
#if defined(HOMETILES_BETA_BUILD)
  // Test builds also show the panel's own storage (tile data, icon file) to
  // look at it and delete files while testing; release builds offer microSD
  // only.
  if (key == "internal") {
    out_fs = &storageFS();
    out_key = "internal";
    return true;
  }
#endif
  error = "Only microSD is available in the file manager";
  return false;
}

// The internal storage of a test build only lists, downloads and deletes:
// no upload, new folder or rename next to the panel's own files.
bool rejectInternalStorageChange(WebServer& server, const String& fs_key) {
  if (fs_key != "internal") return false;
  sendJsonError(server, 403, "The internal storage only allows deleting");
  return true;
}

bool resolveFileManagerFsFromRequest(WebServer& server, fs::FS*& out_fs, String& out_key, String& error) {
  return resolveFileManagerFsByKey(server.hasArg("fs") ? server.arg("fs") : String("sd"),
                                   out_fs,
                                   out_key,
                                   error);
}

void sendJsonOk(WebServer& server) {
  server.send(200, "application/json", "{\"success\":true}");
}

bool requireFileManagerWriteAccess(WebServer& server, const String& fs_key) {
  if (fs_key != "sd" || Device::sdWritable()) {
    return true;
  }
  Serial.println("[FileManager/SD] write rejected: mounted card did not pass the write test");
  sendJsonError(server,
                423,
                "microSD is readable but not writable; check the serial SD write-test error");
  return false;
}

void logFileManagerWriteFailure(const char* operation,
                                const String& fs_key,
                                const String& path,
                                int error_number) {
  Serial.printf("[FileManager/SD] operation=%s fs=%s path=%s errno=%d (%s)\n",
                operation ? operation : "unknown",
                fs_key.c_str(),
                path.c_str(),
                error_number,
                error_number ? strerror(error_number) : "none");
}

String fileManagerWriteError(const char* message, int error_number) {
  String result = message ? String(message) : String("microSD write failed");
  if (error_number) {
    result += " (errno ";
    result += String(error_number);
    result += ": ";
    result += strerror(error_number);
    result += ")";
  }
  return result;
}

String fileManagerContentType(const String& path) {
  String lowered = path;
  lowered.toLowerCase();
  if (lowered.endsWith(".gif")) return "image/gif";
  if (lowered.endsWith(".png")) return "image/png";
  if (lowered.endsWith(".jpg") || lowered.endsWith(".jpeg")) return "image/jpeg";
  if (lowered.endsWith(".bmp")) return "image/bmp";
  if (lowered.endsWith(".json")) return "application/json";
  if (lowered.endsWith(".txt") || lowered.endsWith(".log") || lowered.endsWith(".url")) return "text/plain";
  if (lowered.endsWith(".html") || lowered.endsWith(".htm")) return "text/html";
  if (lowered.endsWith(".css")) return "text/css";
  if (lowered.endsWith(".js")) return "application/javascript";
  return "application/octet-stream";
}

bool removeFileManagerPathRecursive(fs::FS& fs, const String& path, String& error) {
  if (path == "/") {
    error = "Root folder cannot be deleted";
    return false;
  }

  File entry = fs.open(path, FILE_READ);
  if (!entry) {
    error = "Path not found";
    return false;
  }

  if (!entry.isDirectory()) {
    entry.close();
    if (fs.remove(path)) {
      return true;
    }
    error = "Could not delete file";
    return false;
  }

  File child = entry.openNextFile();
  while (child) {
    const String child_name = fileManagerEntryName(child.name());
    child.close();
    if (child_name.length()) {
      const String child_path = joinPath(path, child_name);
      if (!removeFileManagerPathRecursive(fs, child_path, error)) {
        entry.close();
        return false;
      }
    }
    child = entry.openNextFile();
  }
  entry.close();

  if (fs.rmdir(path)) {
    return true;
  }
  error = "Could not delete folder";
  return false;
}

struct FileManagerEntry {
  String name;
  String path;
  bool directory = false;
  size_t size = 0;
  uint32_t modified = 0;
};

File g_file_manager_upload_file;
String g_file_manager_upload_fs_key;
String g_file_manager_upload_path;
String g_file_manager_upload_error;
bool g_file_manager_upload_started = false;
bool g_file_manager_upload_finished = false;
bool g_file_manager_upload_is_append = false;
size_t g_file_manager_upload_bytes = 0;
size_t g_file_manager_upload_next_heap_log = 0;

// The Wi-Fi SDIO driver asserts (pkt_rxbuff) when internal DMA heap runs
// out; uploads put the most pressure on reception. Log free internal heap
// and the largest contiguous DMA block for diagnosing failures.
void logFileManagerUploadHeap(const char* phase) {
  Serial.printf("[FileManager] Upload heap (%s, %u KB written): int free=%u KB, DMA largest=%u KB\n",
                phase,
                static_cast<unsigned>(g_file_manager_upload_bytes / 1024),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA) / 1024));
}

void resetFileManagerUploadState() {
  if (g_file_manager_upload_file) {
    g_file_manager_upload_file.close();
  }
  g_file_manager_upload_fs_key = "";
  g_file_manager_upload_path = "";
  g_file_manager_upload_error = "";
  g_file_manager_upload_started = false;
  g_file_manager_upload_finished = false;
  g_file_manager_upload_is_append = false;
  g_file_manager_upload_bytes = 0;
  g_file_manager_upload_next_heap_log = 0;
}

void collectImageFiles(const String& dir, std::vector<String>& out, size_t max_entries, uint8_t depth, bool allow_bin, bool allow_jpeg, bool allow_png) {
  if (out.size() >= max_entries) return;
  if (!storageReady()) return;
  File root = storageFS().open(dir);
  if (!root) return;

  File file = root.openNextFile();
  while (file) {
    if (out.size() >= max_entries) break;
    const char* name_c = file.name();
    String name = name_c ? String(name_c) : String();
    if (file.isDirectory()) {
      if (depth > 0 && name.length()) {
        collectImageFiles(joinPath(dir, name), out, max_entries, depth - 1, allow_bin, allow_jpeg, allow_png);
      }
    } else if (name.length()) {
      const bool is_bin = endsWithIgnoreCase(name, ".bin");
      const bool is_jpeg = endsWithIgnoreCase(name, ".jpg") || endsWithIgnoreCase(name, ".jpeg");
      const bool is_png = endsWithIgnoreCase(name, ".png");
      if ((allow_bin && is_bin) || (allow_jpeg && is_jpeg) || (allow_png && is_png)) {
        out.push_back(joinPath(dir, name));
      }
    }
    file = root.openNextFile();
  }
}

}  // namespace

void WebAdminServer::handleGetSdImages() {
  std::vector<String> files;
  collectImageFiles("/", files, 200, 3, true, true, false);

  String json = "[";
  for (size_t i = 0; i < files.size(); ++i) {
    if (i > 0) json += ",";
    json += "\"";
    appendJsonEscaped(json, files[i]);
    json += "\"";
  }
  json += "]";
  sendChunkedResponse(server, 200, "application/json", json);
}

void WebAdminServer::handleFileManagerList() {
  webAdminMarkActivity();
  fs::FS* fs = nullptr;
  String fs_key;
  String error;
  if (!resolveFileManagerFsFromRequest(server, fs, fs_key, error)) {
    sendJsonError(server, 503, error);
    return;
  }

  const String path = normalizeFileManagerPath(server.hasArg("path") ? server.arg("path") : String("/"));
  if (!isSafeFileManagerPath(path)) {
    sendJsonError(server, 400, "Invalid path");
    return;
  }

  File dir = fs->open(path, FILE_READ);
  if (!dir) {
    sendJsonError(server, 404, "Folder not found");
    return;
  }
  if (!dir.isDirectory()) {
    dir.close();
    sendJsonError(server, 400, "Path is not a folder");
    return;
  }

  std::vector<FileManagerEntry> entries;
  entries.reserve(32);
  File child = dir.openNextFile();
  while (child && entries.size() < 250) {
    FileManagerEntry info;
    info.name = fileManagerEntryName(child.name());
    info.directory = child.isDirectory();
    info.size = info.directory ? 0 : static_cast<size_t>(child.size());
    info.modified = static_cast<uint32_t>(child.getLastWrite());
    child.close();
    if (isSafeFileManagerName(info.name)) {
      info.path = joinPath(path, info.name);
      entries.push_back(info);
    }
    child = dir.openNextFile();
  }
  dir.close();

  std::sort(entries.begin(), entries.end(), [](const FileManagerEntry& a, const FileManagerEntry& b) {
    if (a.directory != b.directory) return a.directory && !b.directory;
    String an = a.name;
    String bn = b.name;
    an.toLowerCase();
    bn.toLowerCase();
    return an.compareTo(bn) < 0;
  });

  String json = "{\"success\":true,\"fs\":\"";
  appendJsonEscaped(json, fs_key);
  if (fs_key == "internal") {
    // Every profile mounts LittleFS as its internal storage.
    json += "\",\"total\":";
    json += String(static_cast<unsigned long>(LittleFS.totalBytes()));
    json += ",\"used\":";
    json += String(static_cast<unsigned long>(LittleFS.usedBytes()));
    json += ",\"path\":\"";
  } else {
    json += "\",\"path\":\"";
  }
  appendJsonEscaped(json, path);
  json += "\",\"parent\":\"";
  appendJsonEscaped(json, fileManagerParentPath(path));
  json += "\",\"entries\":[";
  for (size_t i = 0; i < entries.size(); ++i) {
    if (i > 0) json += ",";
    json += "{\"name\":\"";
    appendJsonEscaped(json, entries[i].name);
    json += "\",\"path\":\"";
    appendJsonEscaped(json, entries[i].path);
    json += "\",\"dir\":";
    json += entries[i].directory ? "true" : "false";
    json += ",\"size\":";
    json += String(static_cast<unsigned long>(entries[i].size));
    json += ",\"modified\":";
    json += String(static_cast<unsigned long>(entries[i].modified));
    json += "}";
  }
  json += "]}";
  sendChunkedResponse(server, 200, "application/json", json);
}

void WebAdminServer::handleFileManagerDownload() {
  webAdminMarkActivity();
  fs::FS* fs = nullptr;
  String fs_key;
  String error;
  if (!resolveFileManagerFsFromRequest(server, fs, fs_key, error)) {
    server.send(503, "text/plain", error);
    return;
  }

  const String path = normalizeFileManagerPath(server.hasArg("path") ? server.arg("path") : String());
  if (!isSafeFileManagerPath(path) || path == "/") {
    server.send(400, "text/plain", "Invalid path");
    return;
  }
  if (!fs->exists(path)) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  File file = fs->open(path, FILE_READ);
  if (!file) {
    server.send(500, "text/plain", "Could not open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server.send(400, "text/plain", "Folders cannot be downloaded");
    return;
  }

  String filename = fileManagerBaseName(path);
  filename.replace("\"", "_");
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(file, fileManagerContentType(path));
  file.close();
}

void WebAdminServer::handleFileManagerDelete() {
  webAdminMarkActivity();
  fs::FS* fs = nullptr;
  String fs_key;
  String error;
  if (!resolveFileManagerFsFromRequest(server, fs, fs_key, error)) {
    sendJsonError(server, 503, error);
    return;
  }
  if (!requireFileManagerWriteAccess(server, fs_key)) return;

  const String path = normalizeFileManagerPath(server.hasArg("path") ? server.arg("path") : String());
  if (!isSafeFileManagerPath(path) || path == "/") {
    sendJsonError(server, 400, "Invalid path");
    return;
  }

  if (!fs->exists(path)) {
    sendJsonError(server, 404, "Path not found");
    return;
  }

  const bool internal = fs_key == "internal";
  // LittleFS writes need the S3 display guard; the open icon file is closed
  // first, the font then shows the built-in icons until the restart.
  Device::ScopedStorageWrite storage_write(internal);
  const bool icon_file = internal && path == mdi_icon_store::file_path();
  if (icon_file) mdi_icon_store::release_file();

  if (!removeFileManagerPathRecursive(*fs, path, error)) {
    sendJsonError(server, 500, error.length() ? error : String("Delete failed"));
    return;
  }
  if (icon_file) {
    Serial.printf("[FileManager] Icon file %s deleted; built-in icons until the restart\n", path.c_str());
  }
  sendJsonOk(server);
}

void WebAdminServer::handleFileManagerRename() {
  webAdminMarkActivity();
  fs::FS* fs = nullptr;
  String fs_key;
  String error;
  if (!resolveFileManagerFsFromRequest(server, fs, fs_key, error)) {
    sendJsonError(server, 503, error);
    return;
  }
  if (rejectInternalStorageChange(server, fs_key)) return;
  if (!requireFileManagerWriteAccess(server, fs_key)) return;

  const String path = normalizeFileManagerPath(server.hasArg("path") ? server.arg("path") : String());
  String new_name = server.hasArg("name") ? server.arg("name") : String();
  new_name.trim();
  if (!isSafeFileManagerPath(path) || path == "/" || !isSafeFileManagerName(new_name)) {
    sendJsonError(server, 400, "Invalid name or path");
    return;
  }
  if (!fs->exists(path)) {
    sendJsonError(server, 404, "Path not found");
    return;
  }

  const String target = joinPath(fileManagerParentPath(path), new_name);
  if (!isSafeFileManagerPath(target)) {
    sendJsonError(server, 400, "Invalid target path");
    return;
  }
  if (fs->exists(target)) {
    sendJsonError(server, 409, "Target already exists");
    return;
  }
  errno = 0;
  if (!fs->rename(path, target)) {
    const int saved_errno = errno;
    logFileManagerWriteFailure("rename", fs_key, target, saved_errno);
    sendJsonError(server, 500, fileManagerWriteError("Rename failed", saved_errno));
    return;
  }

  String json = "{\"success\":true,\"path\":\"";
  appendJsonEscaped(json, target);
  json += "\"}";
  sendChunkedResponse(server, 200, "application/json", json);
}

void WebAdminServer::handleFileManagerMkdir() {
  webAdminMarkActivity();
  fs::FS* fs = nullptr;
  String fs_key;
  String error;
  if (!resolveFileManagerFsFromRequest(server, fs, fs_key, error)) {
    sendJsonError(server, 503, error);
    return;
  }
  if (rejectInternalStorageChange(server, fs_key)) return;
  if (!requireFileManagerWriteAccess(server, fs_key)) return;

  const String path = normalizeFileManagerPath(server.hasArg("path") ? server.arg("path") : String("/"));
  String name = server.hasArg("name") ? server.arg("name") : String();
  name.trim();
  if (!isSafeFileManagerPath(path) || !isSafeFileManagerName(name)) {
    sendJsonError(server, 400, "Invalid name or path");
    return;
  }

  File dir = fs->open(path, FILE_READ);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    sendJsonError(server, 404, "Folder not found");
    return;
  }
  dir.close();

  const String target = joinPath(path, name);
  if (!isSafeFileManagerPath(target)) {
    sendJsonError(server, 400, "Invalid target path");
    return;
  }
  if (fs->exists(target)) {
    sendJsonError(server, 409, "Folder already exists");
    return;
  }
  errno = 0;
  if (!fs->mkdir(target)) {
    const int saved_errno = errno;
    logFileManagerWriteFailure("mkdir", fs_key, target, saved_errno);
    sendJsonError(server,
                  500,
                  fileManagerWriteError("Could not create folder", saved_errno));
    return;
  }
  sendJsonOk(server);
}

void WebAdminServer::handleFileManagerUpload() {
  HTTPUpload& upload = server.upload();

  // Keep the media cover worker paused throughout the upload through
  // webAdminRecentlyActive. A concurrent cover download would add pressure
  // to the already heavily loaded internal DMA buffer pool of the Wi-Fi driver.
  webAdminMarkActivity();

  if (upload.status == UPLOAD_FILE_START) {
    resetFileManagerUploadState();
    g_file_manager_upload_started = true;

    // Keep this connection receive window small. The default lwIP window
    // (CONFIG_LWIP_TCP_WND_DEFAULT) permits 64 KB of unacknowledged browser
    // data in internal DMA buffers, which caused an sdio_rx_get_buffer assert
    // during a measured 1 MB upload.
    int rcvbuf = 8 * 1024;
    server.client().setSocketOption(SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    fs::FS* fs = nullptr;
    String fs_key;
    String error;
    if (!resolveFileManagerFsFromRequest(server, fs, fs_key, error)) {
      g_file_manager_upload_error = error;
      return;
    }
    if (fs_key == "internal") {
      g_file_manager_upload_error = "The internal storage only allows deleting";
      return;
    }
    if (!Device::sdWritable()) {
      g_file_manager_upload_error =
          "microSD is readable but not writable; check the serial SD write-test error";
      Serial.println("[FileManager/SD] upload rejected: mounted card did not pass the write test");
      return;
    }

    // append=1 continues an upload split into small requests by
    // uploadFileManagerFile in Admin JavaScript; append instead of recreating.
    g_file_manager_upload_is_append = server.hasArg("append") && server.arg("append") == "1";

    const String dir_path = normalizeFileManagerPath(server.hasArg("path") ? server.arg("path") : String("/"));
    const String filename = sanitizeFileManagerUploadName(upload.filename);
    if (!isSafeFileManagerPath(dir_path) || !filename.length()) {
      g_file_manager_upload_error = "Invalid upload path or filename";
      return;
    }

    File dir = fs->open(dir_path, FILE_READ);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      g_file_manager_upload_error = "Upload folder not found";
      return;
    }
    dir.close();

    const String target = joinPath(dir_path, filename);
    if (!isSafeFileManagerPath(target)) {
      g_file_manager_upload_error = "Invalid target path";
      return;
    }

    if (g_file_manager_upload_is_append) {
      if (!fs->exists(target)) {
        g_file_manager_upload_error = "Append target missing";
        return;
      }
      errno = 0;
      g_file_manager_upload_file = fs->open(target, FILE_APPEND);
    } else {
      if (fs->exists(target)) {
        File existing = fs->open(target, FILE_READ);
        const bool existing_is_dir = existing && existing.isDirectory();
        if (existing) existing.close();
        if (existing_is_dir || !fs->remove(target)) {
          g_file_manager_upload_error = "Could not replace existing file";
          return;
        }
      }
      errno = 0;
      g_file_manager_upload_file = fs->open(target, FILE_WRITE);
    }
    if (!g_file_manager_upload_file) {
      const int saved_errno = errno;
      logFileManagerWriteFailure("open-upload", fs_key, target, saved_errno);
      g_file_manager_upload_error =
          fileManagerWriteError("Could not open target file", saved_errno);
      return;
    }

    g_file_manager_upload_fs_key = fs_key;
    g_file_manager_upload_path = target;
    g_file_manager_upload_bytes = 0;
    g_file_manager_upload_next_heap_log = 512u * 1024u;
    if (!g_file_manager_upload_is_append) {
      Serial.printf("[FileManager] Upload started: %s:%s\n", fs_key.c_str(), target.c_str());
      logFileManagerUploadHeap("start");
    }
    return;
  }

  if (g_file_manager_upload_error.length() > 0 || !g_file_manager_upload_started) {
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!g_file_manager_upload_file) {
      g_file_manager_upload_error = "Upload file is not open";
      return;
    }
    errno = 0;
    const size_t written = g_file_manager_upload_file.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      const int saved_errno = errno;
      logFileManagerWriteFailure("write-upload",
                                 g_file_manager_upload_fs_key,
                                 g_file_manager_upload_path,
                                 saved_errno);
      g_file_manager_upload_error =
          fileManagerWriteError("Could not write upload chunk", saved_errno);
      logFileManagerUploadHeap("write-error");
      g_file_manager_upload_file.close();
      fs::FS* fs = nullptr;
      String fs_key;
      String error;
      if (resolveFileManagerFsByKey(g_file_manager_upload_fs_key, fs, fs_key, error)) {
        fs->remove(g_file_manager_upload_path);
      }
      return;
    }
    g_file_manager_upload_bytes += written;
    if (g_file_manager_upload_bytes >= g_file_manager_upload_next_heap_log) {
      g_file_manager_upload_next_heap_log += 512u * 1024u;
      logFileManagerUploadHeap("write");
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    g_file_manager_upload_error = "Upload aborted";
    logFileManagerUploadHeap("abort");
    if (g_file_manager_upload_file) {
      g_file_manager_upload_file.close();
    }
    fs::FS* fs = nullptr;
    String fs_key;
    String error;
    if (resolveFileManagerFsByKey(g_file_manager_upload_fs_key, fs, fs_key, error)) {
      fs->remove(g_file_manager_upload_path);
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (g_file_manager_upload_file) {
      g_file_manager_upload_file.close();
    }
    g_file_manager_upload_finished = true;
    // Do not log each append chunk (up to 64 requests per file).
    // Always retain error and abort logs.
    if (!g_file_manager_upload_is_append) {
      Serial.printf("[FileManager] Uploaded %s (%u bytes)\n",
                    g_file_manager_upload_path.c_str(),
                    static_cast<unsigned>(g_file_manager_upload_bytes));
      logFileManagerUploadHeap("end");
    }
  }
}

void WebAdminServer::handleFileManagerUploadDone() {
  webAdminMarkActivity();
  if (!g_file_manager_upload_started) {
    sendJsonError(server, 400, "No upload started");
    return;
  }

  if (g_file_manager_upload_error.length() > 0) {
    String error = g_file_manager_upload_error;
    resetFileManagerUploadState();
    sendJsonError(server, 500, error);
    return;
  }

  if (!g_file_manager_upload_finished) {
    resetFileManagerUploadState();
    sendJsonError(server, 500, "Upload did not finish");
    return;
  }

  String json = "{\"success\":true,\"path\":\"";
  appendJsonEscaped(json, g_file_manager_upload_path);
  json += "\",\"size\":";
  json += String(static_cast<unsigned long>(g_file_manager_upload_bytes));
  json += "}";
  resetFileManagerUploadState();
  sendChunkedResponse(server, 200, "application/json", json);
}

#pragma once

#include <Arduino.h>
#include <stdint.h>

// The icon file on LittleFS (tools/generate-icon-files.mjs) for this panel's
// icon size, and the cache the icon font reads it through. begin() runs once
// after LittleFS is mounted and before the UI is built: it allocates the
// cache in one PSRAM block and opens the file. Without a valid file the panel
// shows its built-in core icons and the unknown-name icon for the others.

namespace mdi_icon_store {

enum class State : uint8_t { Missing, Ready, Invalid };

void begin();
State state();

// "/_mdi_icons_48.bin" on LittleFS, the release file of the same icons and
// its exact size.
const char* file_path();
const char* release_file_name();
uint32_t file_bytes();

// Writing a new icon file, in order: begin_write() closes and deletes the
// current one (LittleFS has room for one), write() takes the bytes, header
// first, and finish_write() reads the file back, checks the bitmaps' CRC and
// moves it in place. The panel restarts afterwards to read it. The caller
// holds the storage write guard.
bool begin_write(uint32_t total, String& error);
bool write(const uint8_t* data, size_t len);
bool finish_write(String& error);

// Closes the icon file before it is deleted (the file manager of a test
// build): the font shows the built-in icons until the restart.
void release_file();

// The System page's Icons card: all icons with the file, the built-in ones
// without it, and the address of this firmware's own icon file (its release;
// beta builds: the staging repository).
uint32_t icon_count();
uint32_t builtin_count();
String release_file_url();

// "Update" on the Icons card: the panel downloads its icon file now, also
// after three failed attempts; the loop takes the request.
void request_download();
bool take_download_request();

// An icon file uploaded on the Icons card: held in PSRAM, its header checked
// as soon as it arrived and its bitmaps' CRC at the end. The loop installs a
// complete upload (install_upload, under the storage write guard) and
// restarts the panel.
bool upload_begin(String& error);
bool upload_write(const uint8_t* data, size_t len, String& error);
bool upload_finish(String& error);
void upload_discard();
bool upload_ready();
bool install_upload(String& error);

// The automatic download after an update: due while the panel has no valid
// file and fewer than three attempts were made with this firmware version.
// note_download_attempt() counts one before the download starts, so a hang
// counts too; clear_download_attempts() after a written file.
bool download_due();
void note_download_attempt();
void clear_download_attempts();

// Why the last download or install failed, shown on the Icons card while the
// file is missing; a written file clears it (clear_download_attempts).
void note_failure(const String& reason);
String last_failure();

}  // namespace mdi_icon_store

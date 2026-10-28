#ifndef WEB_ADMIN_UTILS_H
#define WEB_ADMIN_UTILS_H

#include <Arduino.h>
#include <WebServer.h>
#include <vector>

// Structure for scene options (alias and entity)
struct SceneOption {
  String alias;
  String entity;
};

// Utility functions for web admin operations

// Safely copy a String to a char buffer with null termination
void copyToBuffer(char* dest, size_t max_len, const String& value);

// Append HTML-escaped version of value to out
void appendHtmlEscaped(String& out, const String& value);
void appendTileTitleHtml(String& out, const String& value);

// Entity field of a tile type. The value lives in a hidden input with the
// usual field id, so loading, saving, drafts and previews keep reading it;
// the shared Web Admin entity picker (tiles/entity-picker.js) draws the
// control and lists the entities of `list` from /api/entity_options.
void appendEntityPickerField(String& out, const String& tab_id, const char* field,
                             const char* label, const char* list);
// An optional entity: `empty_label` says what an empty field means, shown in
// place of the generic "choose" text.
void appendEntityPickerField(String& out, const String& tab_id, const char* field,
                             const char* label, const char* list, const char* empty_label);

// Convert identifier to human-readable format (e.g., "sensor.temp" -> "Sensor Temp")
String humanizeIdentifier(const String& raw, bool strip_domain);

// Parse newline-separated sensor list
std::vector<String> parseSensorList(const String& raw);

// Parse scene list with alias=entity format
std::vector<SceneOption> parseSceneList(const String& raw);

// Normalize sensor selection against available options
String normalizeSensorSelection(const String& selection,
                                const std::vector<String>& options);

// Normalize scene selection against available options
String normalizeSceneSelection(const String& selection,
                               const std::vector<SceneOption>& options);

// Lookup key-value pair in text (key=value format, one per line)
String lookupKeyValue(const String& text, const String& key);

// Send larger WebUI payloads in small TCP chunks. This is gentler on ESP32-P4
// SDIO/esp-hosted WiFi than one large server.send() call.
void sendChunkedResponse(WebServer& server,
                         int code,
                         const char* content_type,
                         const String& body,
                         size_t chunk_size = 512);

// lwIP keeps a socket's unsent and unacknowledged data in internal RAM, up to
// 64 KB per socket (CONFIG_LWIP_TCP_SND_BUF_DEFAULT; the Arduino libraries do
// not move lwIP buffers to PSRAM). A large response written faster than Wi-Fi
// sends it took the internal heap from 92 to 14 KB (V2 b598, the Web Admin's
// /api/tiles; 183 -> 57 KB for the page in b597) and the panel flashed blue
// for a moment. Between the chunks of a response, wait() pauses while the
// response holds more than kInFlightBytes of internal RAM, so the network
// drains first. A response that had to wait logs it once, in English.
class WebSendPacer {
 public:
  static constexpr uint32_t kInFlightBytes = 24U * 1024U;
  WebSendPacer();
  ~WebSendPacer();
  void wait();

 private:
  uint32_t start_free_;
  uint32_t lowest_free_;
  uint32_t waited_ms_ = 0;
};

#endif // WEB_ADMIN_UTILS_H

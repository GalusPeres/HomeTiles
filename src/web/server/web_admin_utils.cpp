#include "src/web/server/web_admin_utils.h"
#include "src/core/text/title_text.h"
#include <WiFi.h>
#include <algorithm>
#include <vector>
#include <ctype.h>

void appendTileTitleHtml(String& out, const String& value) {
  const std::string title = hometiles_title::normalize(value.c_str());
  out += "<span class=\"tile-title-lines\">";
  size_t start = 0;
  do {
    const size_t end = title.find('\n', start);
    out += "<span class=\"tile-title-line\">";
    appendHtmlEscaped(out, String(title.substr(start, end - start).c_str()));
    out += "</span>";
    if (end == std::string::npos) break;
    start = end + 1;
  } while (start <= title.size());
  out += "</span>";
}

void copyToBuffer(char* dest, size_t max_len, const String& value) {
  if (!dest || !max_len) return;
  size_t copy_len = std::min(value.length(), max_len - 1);
  memcpy(dest, value.c_str(), copy_len);
  dest[copy_len] = '\0';
}

void appendHtmlEscaped(String& out, const String& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c; break;
    }
  }
}

String humanizeIdentifier(const String& raw, bool strip_domain) {
  if (!raw.length()) return String("--");
  String text = raw;
  if (strip_domain) {
    int dot = text.indexOf('.');
    if (dot >= 0) text = text.substring(dot + 1);
  }
  text.replace('_', ' ');
  bool new_word = true;
  for (size_t i = 0; i < text.length(); ++i) {
    char c = text.charAt(i);
    if (isalpha(static_cast<unsigned char>(c))) {
      char mapped = new_word ? toupper(static_cast<unsigned char>(c))
                             : tolower(static_cast<unsigned char>(c));
      text.setCharAt(i, mapped);
      new_word = false;
    } else {
      new_word = (c == ' ' || c == '-' || c == '/');
    }
  }
  text.trim();
  return text.length() ? text : raw;
}

std::vector<String> parseSensorList(const String& raw) {
  std::vector<String> out;
  int start = 0;
  while (start < raw.length()) {
    int end = raw.indexOf('\n', start);
    if (end < 0) end = raw.length();
    String line = raw.substring(start, end);
    line.trim();
    if (line.length()) out.push_back(line);
    start = end + 1;
  }
  return out;
}

std::vector<SceneOption> parseSceneList(const String& raw) {
  std::vector<SceneOption> out;
  int start = 0;
  while (start < raw.length()) {
    int end = raw.indexOf('\n', start);
    if (end < 0) end = raw.length();
    String line = raw.substring(start, end);
    int eq = line.indexOf('=');
    if (eq > 0) {
      SceneOption opt;
      opt.alias = line.substring(0, eq);
      opt.alias.trim();
      opt.alias.toLowerCase();
      opt.entity = line.substring(eq + 1);
      opt.entity.trim();
      if (opt.alias.length() && opt.entity.length()) {
        out.push_back(opt);
      }
    }
    start = end + 1;
  }
  return out;
}

String normalizeSensorSelection(const String& selection,
                                const std::vector<String>& options) {
  String trimmed = selection;
  trimmed.trim();
  for (const auto& opt : options) {
    if (opt.equalsIgnoreCase(trimmed)) {
      return opt;
    }
  }
  return "";
}

String normalizeSceneSelection(const String& selection,
                               const std::vector<SceneOption>& options) {
  String trimmed = selection;
  trimmed.trim();
  trimmed.toLowerCase();
  for (const auto& opt : options) {
    if (opt.alias == trimmed) {
      return opt.alias;
    }
  }
  return "";
}

String lookupKeyValue(const String& text, const String& key) {
  if (!key.length()) return "";
  int start = 0;
  while (start < text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    String line = text.substring(start, end);
    int eq = line.indexOf('=');
    if (eq > 0) {
      String lhs = line.substring(0, eq);
      lhs.trim();
      if (lhs.equalsIgnoreCase(key)) {
        String rhs = line.substring(eq + 1);
        rhs.trim();
        return rhs;
      }
    }
    start = end + 1;
  }
  return "";
}

void sendChunkedResponse(WebServer& server,
                         int code,
                         const char* content_type,
                         const String& body,
                         size_t chunk_size) {
  if (chunk_size < 256) chunk_size = 256;
  if (body.length() <= chunk_size) {
    server.send(code, content_type, body);
    return;
  }

  server.sendHeader("Connection", "close");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(code, content_type, "");

  const size_t len = body.length();
  for (size_t offset = 0; offset < len; offset += chunk_size) {
    const size_t end = std::min(offset + chunk_size, len);
    server.sendContent(body.substring(offset, end));
    delay(2);
    yield();
  }

  server.sendContent("");
  delay(2);
  yield();
}

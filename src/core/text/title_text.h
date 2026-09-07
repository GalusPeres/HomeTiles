#pragma once

#include <string>

namespace hometiles_title {

constexpr size_t kMaxBytes = 255;

inline std::string normalize(const char* text) {
  std::string result;
  bool second_line = false;
  for (const char* p = text ? text : ""; *p; ++p) {
    char c = *p;
    if (c == '\r') { if (p[1] == '\n') ++p; c = '\n'; }
    else if (c == '\\' && p[1] == 'n') { ++p; c = '\n'; }
    if (c == '\n') {
      c = second_line ? ' ' : '\n';
      second_line = true;
    }
    result += c;
  }
  if (result.size() > kMaxBytes) {
    size_t end = kMaxBytes;
    while (end && (static_cast<unsigned char>(result[end]) & 0xC0) == 0x80) --end;
    result.resize(end);
  }
  return result;
}

}  // namespace hometiles_title

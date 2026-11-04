#pragma once

#include <cstddef>
#include <cstring>

// Home Assistant's time zone in the Bridge's configuration:
// "time_zone_rule": "<POSIX TZ rule>" (Bridge time_zone_rule.py, issue #145).
namespace time_zone_rule {

inline bool valid_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '-' ||
         c == ':' || c == ',' || c == '.' || c == '/';
}

// Copies the rule to out. False when the configuration has none (a Bridge
// without it) or it is not a rule: the panel keeps the last one it has.
inline bool extract(const char* json, char* out, size_t size) {
  static const char kKey[] = "\"time_zone_rule\"";
  if (!json || !out || size == 0) return false;
  for (const char* key = strstr(json, kKey); key; key = strstr(key + 1, kKey)) {
    const char* at = key + sizeof(kKey) - 1;
    while (*at == ' ') ++at;
    // An entity of this name is a value, not the key.
    if (*at != ':') continue;
    ++at;
    while (*at == ' ') ++at;
    if (*at != '"') return false;
    ++at;
    size_t length = 0;
    while (at[length] && at[length] != '"') {
      if (!valid_char(at[length]) || length + 1 >= size) return false;
      ++length;
    }
    if (!at[length] || length == 0) return false;
    // A POSIX rule starts with the zone's name.
    if (!((at[0] >= 'A' && at[0] <= 'Z') || (at[0] >= 'a' && at[0] <= 'z'))) return false;
    memcpy(out, at, length);
    out[length] = '\0';
    return true;
  }
  return false;
}

}  // namespace time_zone_rule

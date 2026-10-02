#pragma once

#include <string.h>

#include "src/core/json_scan.h"

// The Web Admin preview polls the cached entity payloads. Members only a popup
// or the device needs (the hourly forecast, embedded artwork) would multiply
// every poll, so the handler cuts them out first.
namespace preview_payload {

// Span [from, to) of the top-level member `"key": value` together with one
// separating comma, so that removing it leaves valid JSON. Finds the key like
// hometiles_json::valueOffset (first quoted occurrence). False when absent.
inline bool member_span(const char* json, int length, const char* key, int* from, int* to) {
  const int value = hometiles_json::valueOffset(json, length, key);
  if (value < 0 || !from || !to) return false;
  const size_t key_length = strlen(key);
  int key_start = -1;
  for (int i = 0; i + static_cast<int>(key_length) + 1 < length; ++i) {
    if (json[i] == '"' && memcmp(json + i + 1, key, key_length) == 0 &&
        json[i + 1 + key_length] == '"') {
      key_start = i;
      break;
    }
  }
  if (key_start < 0 || key_start >= value) return false;

  int end = value;
  if (json[value] == '[' || json[value] == '{') {
    int depth = 0;
    bool in_string = false;
    for (; end < length; ++end) {
      if (hometiles_json::isUnescapedQuote(json, end)) in_string = !in_string;
      if (in_string) continue;
      if (json[end] == '[' || json[end] == '{') ++depth;
      if (json[end] == ']' || json[end] == '}') {
        if (--depth == 0) {
          ++end;
          break;
        }
      }
    }
    if (depth != 0) return false;
  } else if (json[value] == '"') {
    for (end = value + 1; end < length && !hometiles_json::isUnescapedQuote(json, end); ++end) {}
    if (end >= length) return false;
    ++end;
  } else {
    while (end < length && json[end] != ',' && json[end] != '}' && json[end] != ']') ++end;
  }

  int start = key_start;
  int before = key_start - 1;
  while (before >= 0 && (json[before] == ' ' || json[before] == '\t' ||
                         json[before] == '\r' || json[before] == '\n')) {
    --before;
  }
  if (before >= 0 && json[before] == ',') {
    start = before;
  } else {
    // The first member drops the comma behind it instead.
    int after = end;
    while (after < length && (json[after] == ' ' || json[after] == '\t' ||
                              json[after] == '\r' || json[after] == '\n')) {
      ++after;
    }
    if (after < length && json[after] == ',') end = after + 1;
  }
  *from = start;
  *to = end;
  return true;
}

}  // namespace preview_payload

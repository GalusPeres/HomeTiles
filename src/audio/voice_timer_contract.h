#pragma once

// Voice timers: Home Assistant's built-in timer intents ("set a timer for 10
// minutes") shown and controlled on the panel. Home Assistant sends timer
// events only to the device that started the timer; the Bridge registers the
// panel's Assist satellite for them and passes the timers on.
//
// Pure, host-testable part: the messages shared with the Bridge, parsing,
// the requests the panel sends and the countdown text. No Arduino, LVGL or
// FreeRTOS.
//
// Contract v1 (base = the configured device base topic):
//   {base}/cmnd/assist   Bridge -> panel, the Assist command topic (sealed on
//       the encrypted command channel once pairing is active):
//       {"v":1,"op":"timers","timers":[{"id":"<id>","total":600,"left":272,
//           "active":true,"name":"Pasta"},...]}
//           Every running or paused timer of this panel, at most kMaxTimers,
//           sent on every change and when the panel connects. "left" is the
//           seconds left when the Bridge sent it; "active" false = paused;
//           "name" is optional (UTF-8, cut to kMaxNameBytes).
//       {"v":1,"op":"timer_done","id":"<id>","total":600,"name":"Pasta"}
//           A timer of this panel finished: the panel rings.
//       {"v":1,"op":"timer_sound","sound":"rising_three","volume":60}
//           The sound for finished timers (kSoundIds) and the timer volume
//           in percent (optional; the chimes play at it, unmuted, whatever
//           the media volume is; 0 = silent). "bridge" = the Bridge
//           plays the user's own file when the panel asks ("play_sound"); "off" =
//           silent. Kept in RAM; the Bridge sends it on every connect.
//   {base}/stat/assist_timer   panel -> Bridge, not retained:
//       {"v":1,"op":"start","seconds":300}           a touch preset
//       {"v":1,"op":"start","seconds":60,"name":"Pasta"}  +1 min on a finished one
//       {"v":1,"op":"pause"|"resume"|"cancel","id":"<id>"}
//       {"v":1,"op":"add","id":"<id>","seconds":60}
//       {"v":1,"op":"speak","id":"<id>"}   say "<name> timer is done" now
//       {"v":1,"op":"play_sound","id":"<id>"}    play the user's own sound now
//   Timer ids are Home Assistant's (ULIDs): [0-9A-Za-z]{1,32}.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/core/json_scan.h"

namespace voice_timer {

constexpr int kProtocolVersion = 1;
constexpr const char* kRequestLeaf = "/stat/assist_timer";
constexpr size_t kMaxTimers = 8;
constexpr size_t kMaxIdChars = 32;
constexpr size_t kMaxNameBytes = 48;
// Longest Bridge message the panel takes (the sealed channel's body limit).
constexpr size_t kMaxPayloadBytes = 2048;
// Longest timer Home Assistant can hold here: 24 h (longer is clamped).
constexpr uint32_t kMaxSeconds = 24u * 3600u;

// Sound ids, stable on the wire; the first kChimeCount are the built-in
// chimes in timer_chime::Chime order.
constexpr const char* kSoundIds[] = {"rising_three", "kitchen_beep", "soft_bell", "marimba",
                                     "westminster",  "off",          "bridge"};
constexpr size_t kSoundCount = sizeof(kSoundIds) / sizeof(kSoundIds[0]);
constexpr uint8_t kSoundOff = 5;
constexpr uint8_t kSoundBridge = 6;
constexpr uint8_t kDefaultSound = 0;
constexpr uint8_t kDefaultVolumePercent = 60;

struct Timer {
  char id[kMaxIdChars + 1];
  char name[kMaxNameBytes + 1];
  uint32_t total_s;
  uint32_t left_s;
  bool active;
};

struct TimerList {
  uint8_t count;
  Timer timers[kMaxTimers];
};

enum class Op : uint8_t { None, Timers, Done, Sound };

inline bool validId(const char* id, size_t length) {
  if (!id || length == 0 || length > kMaxIdChars) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = id[i];
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (!ok) return false;
  }
  return true;
}

inline bool spanEquals(const char* json, int begin, int end, const char* text) {
  const size_t length = strlen(text);
  return end - begin == static_cast<int>(length) && memcmp(json + begin, text, length) == 0;
}

// Which timer message a NUL-terminated Assist command is; None for the
// microphone commands (assist_contract.h) and anything else.
inline Op opOf(const char* json, size_t length) {
  if (!json || length == 0 || length > kMaxPayloadBytes) return Op::None;
  int begin = 0, end = 0;
  if (!hometiles_json::stringSpan(json, static_cast<int>(length), "op", &begin, &end)) return Op::None;
  if (spanEquals(json, begin, end, "timers")) return Op::Timers;
  if (spanEquals(json, begin, end, "timer_done")) return Op::Done;
  if (spanEquals(json, begin, end, "timer_sound")) return Op::Sound;
  return Op::None;
}

// Appends one code point as UTF-8; false when it does not fit.
inline bool putUtf8(uint32_t cp, char* out, size_t size, size_t* used) {
  char bytes[4];
  size_t n = 0;
  if (cp < 0x80) {
    bytes[n++] = static_cast<char>(cp);
  } else if (cp < 0x800) {
    bytes[n++] = static_cast<char>(0xC0 | (cp >> 6));
    bytes[n++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    bytes[n++] = static_cast<char>(0xE0 | (cp >> 12));
    bytes[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    bytes[n++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    bytes[n++] = static_cast<char>(0xF0 | (cp >> 18));
    bytes[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    bytes[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    bytes[n++] = static_cast<char>(0x80 | (cp & 0x3F));
  }
  if (*used + n >= size) return false;
  memcpy(out + *used, bytes, n);
  *used += n;
  return true;
}

inline int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Copies a JSON string body (between its quotes) unescaped into out, cut
// at a whole UTF-8 character when it does not fit. Control characters
// become spaces.
inline void unescape(const char* json, int begin, int end, char* out, size_t size) {
  size_t used = 0;
  out[0] = '\0';
  for (int i = begin; i < end;) {
    uint32_t cp = static_cast<unsigned char>(json[i]);
    if (cp == '\\' && i + 1 < end) {
      const char e = json[i + 1];
      i += 2;
      switch (e) {
        case 'n': case 'r': case 't': case 'b': case 'f': cp = ' '; break;
        case 'u': {
          cp = 0;
          for (int k = 0; k < 4; ++k) {
            const int h = i + k < end ? hexValue(json[i + k]) : -1;
            if (h < 0) { cp = '?'; break; }
            cp = cp * 16 + static_cast<uint32_t>(h);
          }
          i += 4;
          // A surrogate pair: the second half follows as another \u.
          if (cp >= 0xD800 && cp < 0xDC00 && i + 5 < end && json[i] == '\\' && json[i + 1] == 'u') {
            uint32_t low = 0;
            bool ok = true;
            for (int k = 0; k < 4; ++k) {
              const int h = hexValue(json[i + 2 + k]);
              if (h < 0) { ok = false; break; }
              low = low * 16 + static_cast<uint32_t>(h);
            }
            if (ok && low >= 0xDC00 && low < 0xE000) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
              i += 6;
            }
          }
          if (cp >= 0xD800 && cp < 0xE000) cp = '?';
          if (cp < 0x20) cp = ' ';
          break;
        }
        default: cp = static_cast<unsigned char>(e); break;
      }
      if (!putUtf8(cp, out, size, &used)) break;
      continue;
    }
    // A raw UTF-8 sequence is copied whole or not at all.
    size_t n = 1;
    if ((cp & 0xE0) == 0xC0) n = 2;
    else if ((cp & 0xF0) == 0xE0) n = 3;
    else if ((cp & 0xF8) == 0xF0) n = 4;
    if (i + static_cast<int>(n) > end || used + n >= size) break;
    // A stray continuation byte, a 0xF8+ byte or a broken sequence never
    // reaches the labels: it becomes '?'.
    bool broken = n == 1 && cp >= 0x80;
    for (size_t k = 1; k < n && !broken; ++k) {
      broken = (static_cast<unsigned char>(json[i + static_cast<int>(k)]) & 0xC0) != 0x80;
    }
    if (broken) {
      out[used++] = '?';
      ++i;
      continue;
    }
    if (n == 1 && cp < 0x20) {
      out[used++] = ' ';
    } else {
      memcpy(out + used, json + i, n);
      used += n;
    }
    i += static_cast<int>(n);
  }
  out[used] = '\0';
}

inline uint32_t clampSeconds(float value) {
  if (!(value > 0.0f)) return 0;
  if (value >= static_cast<float>(kMaxSeconds)) return kMaxSeconds;
  return static_cast<uint32_t>(value + 0.5f);
}

// One timer object (its span in json). False without a valid id.
inline bool parseTimer(const char* json, int begin, int end, Timer* out) {
  memset(out, 0, sizeof(*out));
  const char* object = json + begin;
  const int length = end - begin;
  int b = 0, e = 0;
  if (!hometiles_json::stringSpan(object, length, "id", &b, &e) || !validId(object + b, static_cast<size_t>(e - b))) {
    return false;
  }
  memcpy(out->id, object + b, static_cast<size_t>(e - b));
  float value = 0;
  if (hometiles_json::number(object, length, "total", &value)) out->total_s = clampSeconds(value);
  if (hometiles_json::number(object, length, "left", &value)) out->left_s = clampSeconds(value);
  if (out->left_s > out->total_s) out->total_s = out->left_s;
  out->active = true;
  const int active = hometiles_json::valueOffset(object, length, "active");
  if (active >= 0 && strncmp(object + active, "false", 5) == 0) out->active = false;
  if (hometiles_json::stringSpan(object, length, "name", &b, &e)) {
    unescape(object, b, e, out->name, sizeof(out->name));
  }
  return true;
}

// {"op":"timers"}: false when the list is missing (the old list stays).
inline bool parseTimers(const char* json, size_t length, TimerList* out) {
  out->count = 0;
  const int len = static_cast<int>(length);
  int cursor = hometiles_json::arrayStart(json, len, "timers");
  if (cursor < 0) return false;
  ++cursor;  // past the bracket
  int b = 0, e = 0;
  while (out->count < kMaxTimers && hometiles_json::nextObjectInArray(json, len, &cursor, &b, &e)) {
    Timer timer;
    if (!parseTimer(json, b, e, &timer)) continue;
    out->timers[out->count++] = timer;
  }
  return true;
}

// {"op":"timer_done"}: the finished timer (left 0).
inline bool parseDone(const char* json, size_t length, Timer* out) {
  if (!parseTimer(json, 0, static_cast<int>(length), out)) return false;
  out->left_s = 0;
  out->active = false;
  return true;
}

inline int soundIndex(const char* id, size_t length) {
  for (size_t i = 0; i < kSoundCount; ++i) {
    if (strlen(kSoundIds[i]) == length && memcmp(kSoundIds[i], id, length) == 0) return static_cast<int>(i);
  }
  return -1;
}

// {"op":"timer_sound"}: the sound index, -1 for an unknown id.
inline int parseSound(const char* json, size_t length) {
  int b = 0, e = 0;
  if (!hometiles_json::stringSpan(json, static_cast<int>(length), "sound", &b, &e)) return -1;
  return soundIndex(json + b, static_cast<size_t>(e - b));
}

// {"op":"timer_sound"}: the timer volume in percent (0..100), -1 without one.
inline int parseSoundVolume(const char* json, size_t length) {
  float value = 0;
  if (!hometiles_json::number(json, static_cast<int>(length), "volume", &value)) return -1;
  if (!(value >= 0.0f)) return -1;  // also NaN
  return value > 100.0f ? 100 : static_cast<int>(value + 0.5f);
}

// Seconds left now for a timer received `elapsed_ms` ago.
inline uint32_t secondsLeft(const Timer& timer, uint32_t elapsed_ms) {
  if (!timer.active) return timer.left_s;
  const uint32_t gone = elapsed_ms / 1000u;
  return gone >= timer.left_s ? 0 : timer.left_s - gone;
}

// Soonest first: running timers by the time left, paused ones after them.
// The order then holds while the list counts down, since every running
// timer loses the same seconds.
inline void sortSoonest(TimerList* list) {
  if (!list) return;
  for (uint8_t i = 1; i < list->count; ++i) {
    const Timer timer = list->timers[i];
    uint8_t at = i;
    while (at > 0) {
      const Timer& before = list->timers[at - 1];
      const bool sooner = timer.active != before.active ? timer.active : timer.left_s < before.left_s;
      if (!sooner) break;
      list->timers[at] = before;
      --at;
    }
    list->timers[at] = timer;
  }
}

// "4:32", "12:05" or "1:02:03".
inline void formatClock(uint32_t seconds, char* out, size_t size) {
  const uint32_t h = seconds / 3600u, m = (seconds / 60u) % 60u, s = seconds % 60u;
  if (h) snprintf(out, size, "%lu:%02lu:%02lu", static_cast<unsigned long>(h), static_cast<unsigned long>(m),
                  static_cast<unsigned long>(s));
  else snprintf(out, size, "%lu:%02lu", static_cast<unsigned long>(m), static_cast<unsigned long>(s));
}

// Panel -> Bridge requests. Each returns the JSON length, 0 when it does
// not fit or an argument is invalid.
inline size_t finish(int written, size_t size) {
  return written > 0 && static_cast<size_t>(written) < size ? static_cast<size_t>(written) : 0;
}

// Copies a name as a JSON string body (quotes and backslashes escaped,
// control characters dropped).
inline bool escapeName(const char* name, char* out, size_t size) {
  size_t used = 0;
  for (const char* p = name; p && *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20) continue;
    const bool escape = c == '"' || c == '\\';
    if (used + (escape ? 2 : 1) >= size) return false;
    if (escape) out[used++] = '\\';
    out[used++] = static_cast<char>(c);
  }
  out[used] = '\0';
  return true;
}

inline size_t buildStart(uint32_t seconds, const char* name, char* out, size_t size) {
  if (seconds == 0 || seconds > kMaxSeconds) return 0;
  if (name && *name) {
    char escaped[kMaxNameBytes * 2 + 1];
    if (!escapeName(name, escaped, sizeof(escaped))) return 0;
    return finish(snprintf(out, size, "{\"v\":%d,\"op\":\"start\",\"seconds\":%lu,\"name\":\"%s\"}", kProtocolVersion,
                           static_cast<unsigned long>(seconds), escaped),
                  size);
  }
  return finish(snprintf(out, size, "{\"v\":%d,\"op\":\"start\",\"seconds\":%lu}", kProtocolVersion,
                         static_cast<unsigned long>(seconds)),
                size);
}

// op: pause, resume, cancel, speak or play_sound.
inline size_t buildAction(const char* op, const char* id, char* out, size_t size) {
  static const char* const kOps[] = {"pause", "resume", "cancel", "speak", "play_sound"};
  bool known = false;
  for (const char* candidate : kOps) known = known || (op && strcmp(op, candidate) == 0);
  if (!known || !id || !validId(id, strlen(id))) return 0;
  return finish(snprintf(out, size, "{\"v\":%d,\"op\":\"%s\",\"id\":\"%s\"}", kProtocolVersion, op, id), size);
}

inline size_t buildAdd(const char* id, uint32_t seconds, char* out, size_t size) {
  if (!id || !validId(id, strlen(id)) || seconds == 0 || seconds > kMaxSeconds) return 0;
  return finish(snprintf(out, size, "{\"v\":%d,\"op\":\"add\",\"id\":\"%s\",\"seconds\":%lu}", kProtocolVersion, id,
                         static_cast<unsigned long>(seconds)),
                size);
}

}  // namespace voice_timer

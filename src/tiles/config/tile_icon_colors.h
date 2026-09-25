#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Per-tile icon colors for Sensor, Number, Select, Date/Time, Binary sensor
// and Energy tiles, and for the icon-and-title tiles (Scene, Folder, Back,
// Camera). A tile keeps one canonical text record, which is also the
// /_tile_icon_colors sidecar content, the Web Admin field "icon_colors" and
// the import/export value (format v2):
//
//   line 1:  "v2"
//   line 2:  fixed icon color "RRGGBB", or empty for the type's default
//   then optionally "fill NN": the fixed color also tints the tile at NN
//            percent (the "Tint tile" option of the icon color); only with
//            a fixed color, below an active rule tint
//   then at most one rule layer ("Rules" in the Web Admin):
//            "src <auto|rules> <self|entity_id> [tile=NN] [noicon] [off]"
//            auto takes the entity's own icon color (light color, on/off,
//            climate mode, cover state), rules evaluates the bar and state
//            lines below on that entity's state; self is the tile's own
//            entity. tile=NN also tints the tile background with the color
//            at NN percent (readable, see tile_tint.h), noicon leaves the
//            icon alone, off keeps the settings without effect. Without a
//            "src" line, tiles with their own entity evaluate the bar and
//            state lines on their own state (the b40 records).
//   then at most one color bar for numeric states:
//            "bar <smooth|steps> <min> <max> <P>:<RRGGBB> ..." with 2 to 6
//            stops in ascending order; P is the stop position on the bar in
//            thousandths (0..1000), min < max are plain decimal numbers
//   then up to six state lines for text states:
//            "is RRGGBB <text>" (equals) or "has RRGGBB <text>" (contains),
//            compared case-insensitively, first match wins
//
// A state with a leading decimal number takes the bar color when there is a
// bar. Smooth bars interpolate linearly in 8-bit RGB between neighbouring
// stops; steps bars give a value the color of the last stop at or below it.
// Other states take the first matching state line. Without a result the
// fixed color applies, else the type's default. The Web Admin preview
// (src/web/admin/tiles/icon-colors.js) mirrors every formula exactly.
//
// Records without the "v2" line come from the first test format (b39): line 1
// is the fixed color, then up to three "<ge|le|eq|is|has> RRGGBB <value>"
// rules. normalize() keeps the fixed color, turns text rules into state lines
// and turns numeric rules into a steps bar only when they are plain ">="
// thresholds on whole numbers that map exactly onto bar positions; other
// numeric rules are dropped.
//
// Values never contain line breaks or control characters, so the record needs
// no escaping. Evaluation walks the record in place and never allocates. This
// header has no Arduino/LVGL dependency so host tests can compile it
// unchanged.
namespace tile_icon_colors {

inline constexpr size_t kMaxStops = 6;
inline constexpr size_t kMaxRows = 6;
inline constexpr size_t kMaxValueBytes = 32;
inline constexpr size_t kMaxNumberBytes = 12;
inline constexpr unsigned kPositionScale = 1000;
inline constexpr size_t kLegacyMaxRules = 3;
inline constexpr size_t kMaxEntityBytes = 128;
// "\nbar smooth <min> <max>" plus " PPPP:RRGGBB" per stop.
inline constexpr size_t kMaxBarBytes = 5 + 6 + 1 + kMaxNumberBytes + 1 + kMaxNumberBytes + kMaxStops * 12;
// "\nhas RRGGBB <text>".
inline constexpr size_t kMaxRowBytes = 1 + 3 + 1 + 6 + 1 + kMaxValueBytes;
// Tile tint strength of the rule layer in percent.
inline constexpr uint8_t kTintMinimum = 10;
inline constexpr uint8_t kTintMaximum = 50;
inline constexpr uint8_t kTintDefault = 20;
// "\nsrc rules <entity_id> tile=50 noicon off".
inline constexpr size_t kMaxSourceBytes = 1 + 3 + 1 + 5 + 1 + kMaxEntityBytes + 8 + 7 + 4;
// "\nfill 50".
inline constexpr size_t kMaxFillBytes = 8;
// "v2\nRRGGBB", the fill, the source, the bar and the state lines.
inline constexpr size_t kMaxRecordBytes =
    2 + 1 + 6 + kMaxFillBytes + kMaxSourceBytes + kMaxBarBytes + kMaxRows * kMaxRowBytes;

enum class Op : uint8_t { None, Ge, Le, Eq, Is, Has };
enum class BarMode : uint8_t { Smooth, Steps };
enum class SourceMode : uint8_t { None, Auto, Rules };

inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

inline Op parse_op(const char* begin, const char* end) {
  const size_t n = static_cast<size_t>(end - begin);
  if (n == 2 && begin[0] == 'g' && begin[1] == 'e') return Op::Ge;
  if (n == 2 && begin[0] == 'l' && begin[1] == 'e') return Op::Le;
  if (n == 2 && begin[0] == 'e' && begin[1] == 'q') return Op::Eq;
  if (n == 2 && begin[0] == 'i' && begin[1] == 's') return Op::Is;
  if (n == 3 && begin[0] == 'h' && begin[1] == 'a' && begin[2] == 's') return Op::Has;
  return Op::None;
}

inline bool text_op(Op op) { return op == Op::Is || op == Op::Has; }

inline int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parses "RRGGBB" or "#RRGGBB" spanning exactly [begin, end).
inline bool parse_color(const char* begin, const char* end, uint32_t& rgb) {
  if (begin < end && *begin == '#') ++begin;
  if (end - begin != 6) return false;
  uint32_t value = 0;
  for (const char* p = begin; p < end; ++p) {
    const int digit = hex_digit(*p);
    if (digit < 0) return false;
    value = (value << 4) | static_cast<uint32_t>(digit);
  }
  rgb = value;
  return true;
}

inline void trim(const char*& begin, const char*& end) {
  while (begin < end && is_space(*begin)) ++begin;
  while (end > begin && is_space(end[-1])) --end;
}

inline const char* line_end(const char* p) {
  while (*p && *p != '\n') ++p;
  return p;
}

inline bool starts_with(const char* begin, const char* end, const char* prefix) {
  const size_t n = strlen(prefix);
  return static_cast<size_t>(end - begin) >= n && memcmp(begin, prefix, n) == 0;
}

inline bool is_v2(const char* record) {
  return record && record[0] == 'v' && record[1] == '2' &&
         (record[2] == '\n' || record[2] == '\r' || record[2] == '\0');
}

// Start of the line after the first one (the fixed color line of a v2
// record), or the terminating zero.
inline const char* second_line(const char* record) {
  const char* end = line_end(record);
  return *end == '\n' ? end + 1 : end;
}

// Next space-separated token of [p, end).
inline bool next_token(const char*& p, const char* end, const char*& begin, const char*& stop) {
  while (p < end && is_space(*p)) ++p;
  if (p >= end) return false;
  begin = p;
  while (p < end && !is_space(*p)) ++p;
  stop = p;
  return true;
}

inline bool token_is(const char* begin, const char* end, const char* word) {
  const size_t n = strlen(word);
  return static_cast<size_t>(end - begin) == n && memcmp(begin, word, n) == 0;
}

// Plain decimal "[-]digits[.digits]" (comma or dot) spanning [begin, end)
// after trimming, at most kMaxNumberBytes long. `text` receives the canonical
// spelling with a dot.
inline bool parse_decimal(const char* begin, const char* end, double& value,
                          char (&text)[kMaxNumberBytes + 1]) {
  trim(begin, end);
  const size_t n = static_cast<size_t>(end - begin);
  if (n == 0 || n > kMaxNumberBytes) return false;
  size_t digits = 0;
  bool separator = false;
  for (size_t i = 0; i < n; ++i) {
    const char c = begin[i];
    if (c >= '0' && c <= '9') {
      ++digits;
      text[i] = c;
    } else if (c == '-' && i == 0) {
      text[i] = c;
    } else if ((c == '.' || c == ',') && !separator) {
      separator = true;
      text[i] = '.';
    } else {
      return false;
    }
  }
  if (!digits) return false;
  text[n] = '\0';
  value = strtod(text, nullptr);
  return isfinite(value);
}

// The leading decimal number of a state ("21.5", "21,5", "21.5 kWh" give
// 21.5), without exponent. False for text states.
inline bool leading_number(const char* state, double& out) {
  if (!state) return false;
  while (is_space(*state)) ++state;
  char buffer[32];
  size_t n = 0;
  size_t digits = 0;
  const char* p = state;
  if (*p == '-') buffer[n++] = *p++;
  while (*p >= '0' && *p <= '9') {
    if (n + 1 >= sizeof(buffer)) return false;
    buffer[n++] = *p++;
    ++digits;
  }
  if (*p == '.' || *p == ',') {
    if (n + 1 >= sizeof(buffer)) return false;
    buffer[n++] = '.';
    ++p;
    while (*p >= '0' && *p <= '9') {
      if (n + 1 >= sizeof(buffer)) return false;
      buffer[n++] = *p++;
      ++digits;
    }
  }
  if (!digits) return false;
  buffer[n] = '\0';
  out = strtod(buffer, nullptr);
  return isfinite(out);
}

// Case folding for ASCII and the Latin-1 letters of two-byte UTF-8 (C3 80-9E
// except the multiplication sign), which covers the translated state labels.
inline unsigned char fold_at(const char* s, size_t i) {
  const unsigned char c = static_cast<unsigned char>(s[i]);
  if (c >= 'A' && c <= 'Z') return static_cast<unsigned char>(c + 32);
  if (i > 0 && static_cast<unsigned char>(s[i - 1]) == 0xC3 && c >= 0x80 && c <= 0x9E && c != 0x97)
    return static_cast<unsigned char>(c + 0x20);
  return c;
}

inline bool fold_equal(const char* a, size_t a_len, const char* b, size_t b_len) {
  if (a_len != b_len) return false;
  for (size_t i = 0; i < a_len; ++i) {
    if (fold_at(a, i) != fold_at(b, i)) return false;
  }
  return true;
}

inline bool fold_contains(const char* haystack, size_t h_len, const char* needle, size_t n_len) {
  if (n_len == 0 || n_len > h_len) return false;
  for (size_t start = 0; start + n_len <= h_len; ++start) {
    size_t i = 0;
    while (i < n_len && fold_at(haystack, start + i) == fold_at(needle, i)) ++i;
    if (i == n_len) return true;
  }
  return false;
}

inline bool text_matches(Op op, const char* value, size_t value_len, const char* state) {
  if (!state) return false;
  const char* begin = state;
  const char* end = state + strlen(state);
  trim(begin, end);
  const size_t len = static_cast<size_t>(end - begin);
  return op == Op::Is ? fold_equal(begin, len, value, value_len)
                      : fold_contains(begin, len, value, value_len);
}

// One parsed "<op> RRGGBB <value>" line; value points into the record.
struct Rule {
  Op op = Op::None;
  uint32_t color = 0;
  const char* value = nullptr;
  size_t value_len = 0;
};

inline bool parse_rule(const char* begin, const char* end, Rule& rule) {
  trim(begin, end);
  const char* space = begin;
  while (space < end && *space != ' ') ++space;
  rule.op = parse_op(begin, space);
  if (rule.op == Op::None || space >= end) return false;
  const char* color = space + 1;
  const char* color_end = color;
  while (color_end < end && *color_end != ' ') ++color_end;
  if (!parse_color(color, color_end, rule.color)) return false;
  const char* value = color_end;
  trim(value, end);
  rule.value = value;
  rule.value_len = static_cast<size_t>(end - value);
  return rule.value_len > 0;
}

struct Stop {
  uint16_t position = 0;
  uint32_t color = 0;
};

struct Bar {
  BarMode mode = BarMode::Smooth;
  double min = 0;
  double max = 0;
  char min_text[kMaxNumberBytes + 1] = {};
  char max_text[kMaxNumberBytes + 1] = {};
  size_t count = 0;
  Stop stops[kMaxStops] = {};
};

// Keeps the stops in ascending order; equal positions keep their order.
inline void sort_stops(Bar& bar) {
  for (size_t i = 1; i < bar.count; ++i) {
    const Stop stop = bar.stops[i];
    size_t j = i;
    while (j > 0 && bar.stops[j - 1].position > stop.position) {
      bar.stops[j] = bar.stops[j - 1];
      --j;
    }
    bar.stops[j] = stop;
  }
}

// "P:RRGGBB" with P as 1-4 digits, clamped to 0..1000.
inline bool parse_stop(const char* begin, const char* end, Stop& stop) {
  const char* colon = begin;
  while (colon < end && *colon != ':') ++colon;
  const size_t digits = static_cast<size_t>(colon - begin);
  if (colon >= end || digits == 0 || digits > 4) return false;
  unsigned position = 0;
  for (const char* p = begin; p < colon; ++p) {
    if (*p < '0' || *p > '9') return false;
    position = position * 10 + static_cast<unsigned>(*p - '0');
  }
  if (!parse_color(colon + 1, end, stop.color)) return false;
  stop.position = static_cast<uint16_t>(position > kPositionScale ? kPositionScale : position);
  return true;
}

// Parses a "bar ..." line; invalid stops are skipped, stops beyond six are
// ignored, and a bar needs a valid range and at least two stops.
inline bool parse_bar(const char* p, const char* end, Bar& bar) {
  const char* begin = nullptr;
  const char* stop = nullptr;
  if (!next_token(p, end, begin, stop) || !token_is(begin, stop, "bar")) return false;
  if (!next_token(p, end, begin, stop)) return false;
  if (token_is(begin, stop, "smooth")) bar.mode = BarMode::Smooth;
  else if (token_is(begin, stop, "steps")) bar.mode = BarMode::Steps;
  else return false;
  if (!next_token(p, end, begin, stop) || !parse_decimal(begin, stop, bar.min, bar.min_text)) return false;
  if (!next_token(p, end, begin, stop) || !parse_decimal(begin, stop, bar.max, bar.max_text)) return false;
  if (!(bar.min < bar.max)) return false;
  bar.count = 0;
  while (bar.count < kMaxStops && next_token(p, end, begin, stop)) {
    Stop parsed;
    if (parse_stop(begin, stop, parsed)) bar.stops[bar.count++] = parsed;
  }
  if (bar.count < 2) return false;
  sort_stops(bar);
  return true;
}

// Mixes two colors per 8-bit channel with a weight of 0..1024 for `b`,
// rounding to the nearest value.
inline uint32_t mix_colors(uint32_t a, uint32_t b, int weight) {
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const int from = static_cast<int>((a >> shift) & 0xFF);
    const int to = static_cast<int>((b >> shift) & 0xFF);
    const int channel = (from * (1024 - weight) + to * weight + 512) >> 10;
    out |= static_cast<uint32_t>(channel) << shift;
  }
  return out;
}

// Color at bar position t (0..1, clamped).
inline uint32_t bar_color_at(const Bar& bar, double t) {
  if (!(t > 0)) t = 0;
  if (t > 1) t = 1;
  int index = -1;
  for (size_t i = 0; i < bar.count; ++i) {
    if (bar.stops[i].position / 1000.0 <= t) index = static_cast<int>(i);
  }
  if (index < 0) return bar.stops[0].color;
  if (bar.mode == BarMode::Steps || static_cast<size_t>(index) + 1 == bar.count)
    return bar.stops[index].color;
  const double from = bar.stops[index].position / 1000.0;
  const double to = bar.stops[index + 1].position / 1000.0;
  const double fraction = (t - from) / (to - from);
  const int weight = static_cast<int>(floor(fraction * 1024 + 0.5));
  return mix_colors(bar.stops[index].color, bar.stops[index + 1].color, weight);
}

inline uint32_t bar_color(const Bar& bar, double value) {
  return bar_color_at(bar, (value - bar.min) / (bar.max - bar.min));
}

// Home Assistant entity id "<domain>.<object>": lowercase letters, digits and
// underscores with exactly one inner dot, at most kMaxEntityBytes.
inline bool valid_entity(const char* begin, size_t length) {
  if (length < 3 || length > kMaxEntityBytes) return false;
  size_t dots = 0;
  for (size_t i = 0; i < length; ++i) {
    const char c = begin[i];
    if (c == '.') {
      if (i == 0 || i + 1 == length) return false;
      ++dots;
    } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return dots == 1;
}

// The rule layer of a record (its "src" line).
struct Source {
  SourceMode mode = SourceMode::None;
  bool self = false;             // the tile's own entity
  const char* entity = nullptr;  // other entity, points into the record
  size_t entity_len = 0;
  uint8_t tile = 0;              // tile tint in percent, 0 = no tint
  bool icon = true;              // the rule colors the icon
  bool enabled = true;           // "off" keeps the settings without effect
};

inline uint8_t clamp_tint(unsigned percent) {
  if (percent < kTintMinimum) return kTintMinimum;
  if (percent > kTintMaximum) return kTintMaximum;
  return static_cast<uint8_t>(percent);
}

// The fixed icon color (line 2 of a v2 record).
inline bool fixed_color(const char* record, uint32_t& rgb) {
  if (!record || !is_v2(record)) return false;
  const char* begin = second_line(record);
  const char* end = line_end(begin);
  trim(begin, end);
  return parse_color(begin, end, rgb);
}

// The "fill NN" tint of the fixed icon color in percent, 0 without one.
inline uint8_t fill_of(const char* record) {
  uint32_t fixed = 0;
  if (!fixed_color(record, fixed)) return 0;
  for (const char* p = line_end(second_line(record)); *p == '\n';) {
    const char* begin = p + 1;
    const char* end = line_end(begin);
    p = end;
    if (!starts_with(begin, end, "fill ")) continue;
    const char* token = begin + 5;
    const char* stop = end;
    trim(token, stop);
    unsigned value = 0;
    if (token == stop) return 0;
    for (const char* c = token; c < stop; ++c) {
      if (*c < '0' || *c > '9' || value > 1000) return 0;
      value = value * 10 + static_cast<unsigned>(*c - '0');
    }
    return clamp_tint(value);
  }
  return 0;
}

// Parses "src <auto|rules> <self|entity_id> [tile=NN] [noicon] [off]"
// spanning [begin, end); options may come in any order, unknown tokens make
// the line invalid.
inline bool parse_source_line(const char* begin, const char* end, Source& out) {
  Source parsed;
  const char* p = begin;
  const char* token = nullptr;
  const char* stop = nullptr;
  if (!next_token(p, end, token, stop) || !token_is(token, stop, "src")) return false;
  if (!next_token(p, end, token, stop)) return false;
  if (token_is(token, stop, "auto")) parsed.mode = SourceMode::Auto;
  else if (token_is(token, stop, "rules")) parsed.mode = SourceMode::Rules;
  else return false;
  if (!next_token(p, end, token, stop)) return false;
  if (token_is(token, stop, "self")) {
    parsed.self = true;
  } else if (valid_entity(token, static_cast<size_t>(stop - token))) {
    parsed.entity = token;
    parsed.entity_len = static_cast<size_t>(stop - token);
  } else {
    return false;
  }
  while (next_token(p, end, token, stop)) {
    const size_t length = static_cast<size_t>(stop - token);
    if (token_is(token, stop, "noicon")) {
      parsed.icon = false;
    } else if (token_is(token, stop, "off")) {
      parsed.enabled = false;
    } else if (length >= 6 && length <= 7 && memcmp(token, "tile=", 5) == 0) {
      unsigned percent = 0;
      for (const char* d = token + 5; d < stop; ++d) {
        if (*d < '0' || *d > '9') return false;
        percent = percent * 10 + static_cast<unsigned>(*d - '0');
      }
      parsed.tile = clamp_tint(percent);
    } else {
      return false;
    }
  }
  out = parsed;
  return true;
}

// The rule layer of a v2 record (the first "src" line, if valid).
inline Source source_of(const char* record) {
  Source out;
  if (!is_v2(record)) return out;
  const char* p = line_end(second_line(record));
  while (*p == '\n') {
    const char* begin = p + 1;
    const char* end = line_end(begin);
    p = end;
    if (!starts_with(begin, end, "src ")) continue;
    Source parsed;
    if (parse_source_line(begin, end, parsed)) out = parsed;
    return out;
  }
  return out;
}

// The other entity of an enabled rule layer (subscriptions and dispatch), or
// None for no layer, the tile's own entity or a switched-off layer.
inline SourceMode source(const char* record, const char*& entity, size_t& entity_len) {
  const Source layer = source_of(record);
  if (layer.mode == SourceMode::None || layer.self || !layer.enabled) {
    entity = nullptr;
    entity_len = 0;
    return SourceMode::None;
  }
  entity = layer.entity;
  entity_len = layer.entity_len;
  return layer.mode;
}

// True when the color bar and state lines color the icon from the tile's own
// state: no rule layer (b40 records) or an enabled "src rules self" that
// colors the icon.
inline bool own_state_colors_icon(const char* record) {
  const Source layer = source_of(record);
  return layer.mode == SourceMode::None ||
         (layer.mode == SourceMode::Rules && layer.self && layer.enabled && layer.icon);
}

// Icon color for a known entity state. `state` is the raw Home Assistant
// state; `display`, when set, is the displayed text (for example the
// translated Binary sensor state) that state lines may also match. Returns
// false when the type's default color applies. Callers skip unavailable or
// unknown states, which always use the default. Only v2 records are
// evaluated; stored records are always normalized first.
inline bool resolve(const char* record, const char* state, const char* display, uint32_t& rgb,
                    bool fixed_fallback = true) {
  if (!is_v2(record) || !state) return false;
  const char* fixed_begin = second_line(record);
  const char* fixed_end = line_end(fixed_begin);
  const char* p = fixed_end;
  double number = 0;
  const bool numeric = leading_number(state, number);
  bool bar_seen = false;
  while (*p == '\n') {
    const char* begin = p + 1;
    const char* end = line_end(begin);
    p = end;
    if (starts_with(begin, end, "bar ")) {
      if (bar_seen) continue;
      bar_seen = true;
      Bar bar;
      if (numeric && parse_bar(begin, end, bar)) {
        rgb = bar_color(bar, number);
        return true;
      }
      continue;
    }
    Rule rule;
    if (!parse_rule(begin, end, rule) || !text_op(rule.op)) continue;
    if (text_matches(rule.op, rule.value, rule.value_len, state) ||
        (display && text_matches(rule.op, rule.value, rule.value_len, display))) {
      rgb = rule.color;
      return true;
    }
  }
  // Rules alone (the tile tint) do not fall back to the fixed icon color.
  if (!fixed_fallback) return false;
  trim(fixed_begin, fixed_end);
  return parse_color(fixed_begin, fixed_end, rgb);
}

// Copies at most `max_bytes` of [begin, end) without splitting a UTF-8
// sequence and without control characters.
inline size_t copy_value(const char* begin, const char* end, char* out, size_t max_bytes) {
  size_t n = 0;
  for (const char* p = begin; p < end && n < max_bytes; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20 || c == 0x7F) continue;
    out[n++] = static_cast<char>(c);
  }
  // Drop a trailing partial UTF-8 sequence.
  size_t lead = n;
  while (lead > 0 && (static_cast<unsigned char>(out[lead - 1]) & 0xC0) == 0x80) --lead;
  if (lead > 0) {
    const unsigned char c = static_cast<unsigned char>(out[lead - 1]);
    const size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (n - (lead - 1) < need) n = lead - 1;
  }
  return n;
}

inline void append_hex(char* out, size_t& n, uint32_t rgb) {
  static const char kHex[] = "0123456789ABCDEF";
  for (int shift = 20; shift >= 0; shift -= 4) out[n++] = kHex[(rgb >> shift) & 0xF];
}

inline void append_text(char* out, size_t& n, const char* text) {
  while (*text) out[n++] = *text++;
}

// Whole number "[-]digits" of at most seven digits (b39 thresholds).
inline bool parse_whole(const char* begin, const char* end, long long& value) {
  trim(begin, end);
  bool negative = false;
  if (begin < end && *begin == '-') {
    negative = true;
    ++begin;
  }
  const size_t digits = static_cast<size_t>(end - begin);
  if (digits == 0 || digits > 7) return false;
  long long result = 0;
  for (const char* p = begin; p < end; ++p) {
    if (*p < '0' || *p > '9') return false;
    result = result * 10 + (*p - '0');
  }
  value = negative ? -result : result;
  return true;
}

// A b39 numeric rule counts only when its value is a complete number, like
// the b39 normalizer required.
inline bool legacy_numeric_valid(const Rule& rule) {
  char buffer[kMaxValueBytes + 1];
  const size_t n = rule.value_len < kMaxValueBytes ? rule.value_len : kMaxValueBytes;
  if (n != rule.value_len) return false;
  for (size_t i = 0; i < n; ++i) buffer[i] = rule.value[i] == ',' ? '.' : rule.value[i];
  buffer[n] = '\0';
  char* stop = nullptr;
  const double value = strtod(buffer, &stop);
  return stop && stop != buffer && !*stop && isfinite(value);
}

// Converts the numeric b39 rules of a legacy record into a steps bar when
// they are ">=" thresholds on whole numbers in descending order (first match
// wins) whose positions are exact thousandths. Values below the lowest
// threshold keep the fixed color, or white as the numeric types' default.
inline bool migrate_legacy_bar(const char* record, bool has_fixed, uint32_t fixed, Bar& bar) {
  long long thresholds[kLegacyMaxRules];
  uint32_t colors[kLegacyMaxRules];
  size_t count = 0;
  size_t rules = 0;
  const char* p = line_end(record);
  while (*p == '\n' && rules < kLegacyMaxRules) {
    const char* begin = p + 1;
    const char* end = line_end(begin);
    p = end;
    Rule rule;
    if (!parse_rule(begin, end, rule)) continue;
    if (text_op(rule.op)) {
      ++rules;
      continue;
    }
    if (!legacy_numeric_valid(rule)) continue;
    ++rules;
    long long value = 0;
    if (rule.op != Op::Ge || !parse_whole(rule.value, rule.value + rule.value_len, value)) return false;
    if (count > 0 && value >= thresholds[count - 1]) return false;
    thresholds[count] = value;
    colors[count] = rule.color;
    ++count;
  }
  if (count == 0) return false;
  // Ascending order: the last rule holds the lowest threshold.
  const long long lowest = thresholds[count - 1];
  const long long highest = thresholds[0];
  const long long min = count == 1 ? lowest - 1 : lowest - (highest - lowest);
  const long long range = highest - min;
  bar.mode = BarMode::Steps;
  bar.count = 0;
  bar.stops[bar.count++] = {0, has_fixed ? fixed : 0xFFFFFFu};
  for (size_t i = count; i-- > 0;) {
    const long long scaled = (thresholds[i] - min) * static_cast<long long>(kPositionScale);
    if (scaled % range) return false;
    bar.stops[bar.count++] = {static_cast<uint16_t>(scaled / range), colors[i]};
  }
  snprintf(bar.min_text, sizeof(bar.min_text), "%lld", min);
  snprintf(bar.max_text, sizeof(bar.max_text), "%lld", highest);
  bar.min = static_cast<double>(min);
  bar.max = static_cast<double>(highest);
  return true;
}

// Appends one state line when it is valid; returns true when appended.
inline bool append_row(const Rule& rule, char* out, size_t& n) {
  char value[kMaxValueBytes + 1];
  const size_t length = copy_value(rule.value, rule.value + rule.value_len, value, kMaxValueBytes);
  const char* begin = value;
  const char* end = value + length;
  trim(begin, end);
  if (begin == end) return false;
  out[n++] = '\n';
  append_text(out, n, rule.op == Op::Is ? "is" : "has");
  out[n++] = ' ';
  append_hex(out, n, rule.color);
  out[n++] = ' ';
  memcpy(out + n, begin, static_cast<size_t>(end - begin));
  n += static_cast<size_t>(end - begin);
  return true;
}

// Normalizes any input record (Web Admin, import, sidecar, b39 format) into
// the canonical v2 form: uppercase colors, one valid source entity when
// `allow_source`, one valid bar with sorted stops when `allow_bar`, up to six
// non-empty state lines when `allow_rows`, values trimmed and clipped to
// kMaxValueBytes. A "rules" source allows the bar and the state lines, an
// "auto" source takes the entity's own color and drops them. Invalid lines
// are dropped. Returns the length written to `out` (0 = no icon colors);
// `out` is always terminated.
inline size_t normalize(const char* in, char* out, size_t out_size, bool allow_bar, bool allow_rows,
                        bool allow_source = false, bool allow_self = false) {
  if (!out || out_size == 0) return 0;
  out[0] = '\0';
  if (!in || out_size < kMaxRecordBytes + 1) return 0;
  const bool v2 = is_v2(in);
  Source layer = allow_source ? source_of(in) : Source{};
  if (layer.self && !allow_self) layer = Source{};
  if (layer.mode == SourceMode::Rules) {
    // Another entity, or a type without its own state colors, can use both
    // the bar and the state lines; the Sensor family keeps its type's choice.
    if (!layer.self || (!allow_bar && !allow_rows)) {
      allow_bar = true;
      allow_rows = true;
    }
  } else if (layer.mode == SourceMode::Auto) {
    allow_bar = false;
    allow_rows = false;
  }
  // "src rules self" that colors the icon only is the b40 default of tiles
  // with their own entity; it stays implicit so existing records keep their
  // exact form.
  const bool emit_layer = layer.mode != SourceMode::None &&
                          !(layer.mode == SourceMode::Rules && layer.self && layer.icon &&
                            layer.tile == 0 && layer.enabled);
  // Fixed color: line 2 of a v2 record, line 1 of a b39 record.
  const char* fixed_begin = v2 ? second_line(in) : in;
  const char* fixed_end = line_end(fixed_begin);
  const char* body = fixed_end;
  trim(fixed_begin, fixed_end);
  uint32_t fixed = 0;
  const bool has_fixed = parse_color(fixed_begin, fixed_end, fixed);

  Bar bar;
  bool has_bar = false;
  if (allow_bar && v2) {
    for (const char* p = body; *p == '\n' && !has_bar;) {
      const char* begin = p + 1;
      const char* end = line_end(begin);
      p = end;
      if (starts_with(begin, end, "bar ")) {
        has_bar = parse_bar(begin, end, bar);
        break;
      }
    }
  } else if (allow_bar) {
    has_bar = migrate_legacy_bar(in, has_fixed, fixed, bar);
  }

  size_t n = 0;
  append_text(out, n, "v2\n");
  if (has_fixed) append_hex(out, n, fixed);
  const uint8_t fill = v2 ? fill_of(in) : 0;
  if (fill) n += static_cast<size_t>(snprintf(out + n, out_size - n, "\nfill %u", static_cast<unsigned>(fill)));
  if (emit_layer) {
    append_text(out, n, layer.mode == SourceMode::Auto ? "\nsrc auto " : "\nsrc rules ");
    if (layer.self) {
      append_text(out, n, "self");
    } else {
      memcpy(out + n, layer.entity, layer.entity_len);
      n += layer.entity_len;
    }
    if (layer.tile) n += static_cast<size_t>(snprintf(out + n, out_size - n, " tile=%u", static_cast<unsigned>(layer.tile)));
    if (!layer.icon) append_text(out, n, " noicon");
    if (!layer.enabled) append_text(out, n, " off");
  }
  if (has_bar) {
    append_text(out, n, "\nbar ");
    append_text(out, n, bar.mode == BarMode::Steps ? "steps" : "smooth");
    out[n++] = ' ';
    append_text(out, n, bar.min_text);
    out[n++] = ' ';
    append_text(out, n, bar.max_text);
    for (size_t i = 0; i < bar.count; ++i) {
      n += static_cast<size_t>(snprintf(out + n, out_size - n, " %u:", static_cast<unsigned>(bar.stops[i].position)));
      append_hex(out, n, bar.stops[i].color);
    }
  }
  size_t rows = 0;
  if (allow_rows) {
    // b39 records count their first three valid rules, numeric or text.
    size_t legacy_rules = 0;
    for (const char* p = body; *p == '\n' && rows < kMaxRows;) {
      const char* begin = p + 1;
      const char* end = line_end(begin);
      p = end;
      if (v2 && (starts_with(begin, end, "bar ") || starts_with(begin, end, "src "))) continue;
      Rule rule;
      if (!parse_rule(begin, end, rule)) continue;
      if (!v2) {
        if (legacy_rules >= kLegacyMaxRules) break;
        if (!text_op(rule.op)) {
          if (legacy_numeric_valid(rule)) ++legacy_rules;
          continue;
        }
        ++legacy_rules;
      } else if (!text_op(rule.op)) {
        continue;
      }
      if (append_row(rule, out, n)) ++rows;
    }
  }
  if (!has_fixed && !emit_layer && !has_bar && rows == 0) n = 0;
  out[n] = '\0';
  return n;
}

}  // namespace tile_icon_colors

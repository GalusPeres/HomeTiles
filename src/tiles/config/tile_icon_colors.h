#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Per-tile icon colors for Sensor, Number, Select, Date/Time, Binary sensor
// and Energy tiles: an optional fixed icon color and up to three color
// rules. A tile keeps one canonical text record, which
// is also the /_tile_icon_colors sidecar content, the Web Admin field
// "icon_colors" and the import/export value:
//
//   line 1:    fixed icon color "RRGGBB", or empty for the type's default
//   lines 2-4: "<op> RRGGBB <value>", evaluated in order, first match wins;
//              op ge, le, eq compare numbers, is (equals) and has (contains)
//              compare text case-insensitively.
//
// Values never contain line breaks or control characters, so the record
// needs no escaping. Evaluation walks the record in place and never
// allocates. This header has no Arduino/LVGL dependency so host tests can
// compile it unchanged.
namespace tile_icon_colors {

inline constexpr size_t kMaxRules = 3;
inline constexpr size_t kMaxValueBytes = 32;
// "RRGGBB" plus three "has RRGGBB <value>" lines with their line breaks.
inline constexpr size_t kMaxRecordBytes = 6 + kMaxRules * (1 + 3 + 1 + 6 + 1 + kMaxValueBytes);

enum class Op : uint8_t { None, Ge, Le, Eq, Is, Has };

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

inline const char* op_name(Op op) {
  switch (op) {
    case Op::Ge: return "ge";
    case Op::Le: return "le";
    case Op::Eq: return "eq";
    case Op::Is: return "is";
    case Op::Has: return "has";
    default: return "";
  }
}

inline bool numeric_op(Op op) { return op == Op::Ge || op == Op::Le || op == Op::Eq; }

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

// Parses a leading decimal number (comma or dot) without allocating, like
// the sensor gauge does: "21.5", "21,5" and "21.5 kWh" all give 21.5.
inline bool parse_number(const char* begin, const char* end, double& out) {
  while (begin < end && is_space(*begin)) ++begin;
  while (end > begin && is_space(end[-1])) --end;
  char buffer[40];
  const size_t n = static_cast<size_t>(end - begin);
  if (n == 0 || n >= sizeof(buffer)) return false;
  for (size_t i = 0; i < n; ++i) buffer[i] = begin[i] == ',' ? '.' : begin[i];
  buffer[n] = '\0';
  char* stop = nullptr;
  const double value = strtod(buffer, &stop);
  if (!stop || stop == buffer || !isfinite(value)) return false;
  out = value;
  return true;
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

inline void trim(const char*& begin, const char*& end) {
  while (begin < end && is_space(*begin)) ++begin;
  while (end > begin && is_space(end[-1])) --end;
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

// One parsed rule line; value points into the record.
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

inline const char* line_end(const char* p) {
  while (*p && *p != '\n') ++p;
  return p;
}

// Fixed icon color of a record (line 1).
inline bool fixed_color(const char* record, uint32_t& rgb) {
  if (!record || !*record) return false;
  const char* begin = record;
  const char* end = line_end(record);
  trim(begin, end);
  return parse_color(begin, end, rgb);
}

// Icon color for a known entity state: the first matching rule in order,
// else the fixed icon color. `state` is the raw Home Assistant state;
// `display`, when set, is the displayed text (for example the translated
// Binary sensor state) that text rules may also match. Returns false when
// the type's default color applies. Callers skip unavailable or unknown
// states, which always use the default.
inline bool resolve(const char* record, const char* state, const char* display, uint32_t& rgb) {
  if (!record || !*record || !state) return false;
  const char* p = strchr(record, '\n');
  bool number_parsed = false;
  bool has_number = false;
  double number = 0;
  for (size_t index = 0; p && *p == '\n' && index < kMaxRules; ++index) {
    const char* begin = p + 1;
    const char* end = line_end(begin);
    p = end;
    Rule rule;
    if (!parse_rule(begin, end, rule)) continue;
    bool match = false;
    if (numeric_op(rule.op)) {
      if (!number_parsed) {
        number_parsed = true;
        has_number = parse_number(state, state + strlen(state), number);
      }
      double limit = 0;
      if (has_number && parse_number(rule.value, rule.value + rule.value_len, limit)) {
        if (rule.op == Op::Ge) match = number >= limit;
        else if (rule.op == Op::Le) match = number <= limit;
        else match = fabs(number - limit) <= 1e-6 * (fabs(limit) > 1 ? fabs(limit) : 1);
      }
    } else {
      match = text_matches(rule.op, rule.value, rule.value_len, state) ||
              (display && text_matches(rule.op, rule.value, rule.value_len, display));
    }
    if (match) {
      rgb = rule.color;
      return true;
    }
  }
  return fixed_color(record, rgb);
}

// Appends at most `max_bytes` of [begin, end) without splitting a UTF-8
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

// Normalizes any input record (Web Admin, import, sidecar) into the canonical
// form: uppercase colors, known operators, at most three non-empty rules,
// numeric rules with a valid number (comma becomes dot), values trimmed and
// clipped to kMaxValueBytes. Invalid lines are dropped. Returns the length
// written to `out` (0 = no icon colors); `out` is always terminated.
inline size_t normalize(const char* in, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  out[0] = '\0';
  if (!in || out_size < kMaxRecordBytes + 1) return 0;
  size_t n = 0;
  uint32_t rgb = 0;
  const char* end = line_end(in);
  const char* begin = in;
  trim(begin, end);
  if (parse_color(begin, end, rgb)) append_hex(out, n, rgb);
  size_t rules = 0;
  const char* p = line_end(in);
  while (*p == '\n' && rules < kMaxRules) {
    const char* line = p + 1;
    const char* line_stop = line_end(line);
    p = line_stop;
    Rule rule;
    if (!parse_rule(line, line_stop, rule)) continue;
    char value[kMaxValueBytes + 1];
    size_t value_len = copy_value(rule.value, rule.value + rule.value_len, value, kMaxValueBytes);
    const char* value_begin = value;
    const char* value_end = value + value_len;
    trim(value_begin, value_end);
    value_len = static_cast<size_t>(value_end - value_begin);
    if (value_len == 0) continue;
    if (numeric_op(rule.op)) {
      // A numeric rule holds only its number, with a dot as separator.
      char buffer[kMaxValueBytes + 1];
      for (size_t i = 0; i < value_len; ++i) buffer[i] = value_begin[i] == ',' ? '.' : value_begin[i];
      buffer[value_len] = '\0';
      char* stop = nullptr;
      const double number = strtod(buffer, &stop);
      if (!stop || stop == buffer || *stop || !isfinite(number)) continue;
      memcpy(value, buffer, value_len + 1);
      value_begin = value;
    }
    out[n++] = '\n';
    const char* name = op_name(rule.op);
    while (*name) out[n++] = *name++;
    out[n++] = ' ';
    append_hex(out, n, rule.color);
    out[n++] = ' ';
    memmove(out + n, value_begin, value_len);
    n += value_len;
    ++rules;
  }
  out[n] = '\0';
  return n;
}

}  // namespace tile_icon_colors

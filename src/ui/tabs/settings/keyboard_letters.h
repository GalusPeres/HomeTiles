#pragma once

#include <stddef.h>
#include <stdint.h>

// The letters of the panel keyboard (settings_keyboard) as data: a language
// names its own letters in its LocaleProfile (keyboard_letters), and the
// accent page and the forms of a held letter are made from them, the common
// European letters after. Capitals and the plain letter an accented one
// belongs to follow Unicode's Latin-1, Latin Extended-A and Cyrillic blocks,
// the letters the panel fonts carry, so a new language brings only its data.
namespace keyboard_letters {

// The accent page's rows: ten, ten, and seven between Shift and Backspace.
inline constexpr uint8_t kRowCapacity[3] = {10, 10, 7};
inline constexpr uint8_t kPageCapacity = 27;
// The forms shown above a held letter.
inline constexpr uint8_t kVariantCapacity = 7;

// After a language's own letters, in this order (the former keyboard's
// European page).
inline constexpr const char* kCommon = "äöüßéèêàâçëîïôûùñœæáíóúÿøå";
// Also offered when a letter is held, after the common ones.
inline constexpr const char* kFurther = "ąćęłńśźżčďěňřšťůýžĺľŕőű";

// One code point from UTF-8; `text` moves past it. 0 at the end or on a
// broken sequence (which ends the text).
inline uint32_t next(const char*& text) {
  const auto* p = reinterpret_cast<const uint8_t*>(text);
  if (!p || !p[0]) return 0;
  uint32_t cp = p[0];
  int extra = 0;
  if (cp >= 0xF0) {
    cp &= 0x07;
    extra = 3;
  } else if (cp >= 0xE0) {
    cp &= 0x0F;
    extra = 2;
  } else if (cp >= 0xC0) {
    cp &= 0x1F;
    extra = 1;
  } else if (cp >= 0x80) {
    return 0;
  }
  for (int i = 1; i <= extra; ++i) {
    if ((p[i] & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  text += extra + 1;
  return cp;
}

// `cp` as UTF-8 into `out` (always terminated); the bytes written.
inline size_t encode(uint32_t cp, char* out, size_t size) {
  if (!out || size == 0) return 0;
  uint8_t bytes[4];
  size_t count = 0;
  if (cp < 0x80) {
    bytes[count++] = static_cast<uint8_t>(cp);
  } else if (cp < 0x800) {
    bytes[count++] = static_cast<uint8_t>(0xC0 | (cp >> 6));
    bytes[count++] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    bytes[count++] = static_cast<uint8_t>(0xE0 | (cp >> 12));
    bytes[count++] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
    bytes[count++] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
  } else {
    bytes[count++] = static_cast<uint8_t>(0xF0 | (cp >> 18));
    bytes[count++] = static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F));
    bytes[count++] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
    bytes[count++] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
  }
  if (count + 1 > size) count = 0;
  for (size_t i = 0; i < count; ++i) out[i] = static_cast<char>(bytes[i]);
  out[count] = '\0';
  return count;
}

// The capital of a small letter; signs and letters without one (ß) stay.
inline uint32_t upper(uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') return cp - 32;
  // Cyrillic: а..я after А..Я, ѐ..џ after Ѐ..Џ, ґ after Ґ.
  if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;
  if (cp >= 0x450 && cp <= 0x45F) return cp - 0x50;
  if (cp == 0x491) return 0x490;
  if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;
  if (cp == 0xFF) return 0x178;
  // Latin Extended-A pairs a capital with the small letter after it; from
  // U+0139 to U+0148 and from U+0179 to U+017E the pairs start one later.
  const bool later = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
  if (later || (cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177)) {
    const bool small = later ? cp % 2 == 0 : cp % 2 == 1;
    return small ? cp - 1 : cp;
  }
  return cp;
}

// The small letter of a capital on a key (Shift): ASCII and Cyrillic, the
// letters of the letter pages; anything else stays.
inline uint32_t lower(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + 32;
  if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
  if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;
  if (cp == 0x490) return 0x491;
  return cp;
}

// The plain letter an accented small letter is typed from by holding it
// ('a' for 'ą', 's' for 'ß', Cyrillic е for ё and ь for ъ like the common
// Russian keyboards); 0 for anything else.
inline uint32_t plain(uint32_t cp) {
  if (cp == 0x451) return 0x435;
  if (cp == 0x44A) return 0x44C;
  struct Range {
    uint16_t first;
    uint16_t last;
    char letter;
  };
  static constexpr Range kRanges[] = {
      {0xDF, 0xDF, 's'},   {0xE0, 0xE6, 'a'},   {0xE7, 0xE7, 'c'},   {0xE8, 0xEB, 'e'},   {0xEC, 0xEF, 'i'},
      {0xF0, 0xF0, 'd'},   {0xF1, 0xF1, 'n'},   {0xF2, 0xF6, 'o'},   {0xF8, 0xF8, 'o'},   {0xF9, 0xFC, 'u'},
      {0xFD, 0xFD, 'y'},   {0xFF, 0xFF, 'y'},   {0x100, 0x105, 'a'}, {0x106, 0x10D, 'c'}, {0x10E, 0x111, 'd'},
      {0x112, 0x11B, 'e'}, {0x11C, 0x123, 'g'}, {0x124, 0x127, 'h'}, {0x128, 0x131, 'i'}, {0x134, 0x135, 'j'},
      {0x136, 0x138, 'k'}, {0x139, 0x142, 'l'}, {0x143, 0x14B, 'n'}, {0x14C, 0x153, 'o'}, {0x154, 0x159, 'r'},
      {0x15A, 0x161, 's'}, {0x162, 0x167, 't'}, {0x168, 0x173, 'u'}, {0x174, 0x175, 'w'}, {0x176, 0x178, 'y'},
      {0x179, 0x17E, 'z'}, {0x17F, 0x17F, 's'},
  };
  for (const Range& range : kRanges) {
    if (cp >= range.first && cp <= range.last) return static_cast<uint32_t>(range.letter);
  }
  return 0;
}

inline bool contains(const uint32_t* list, uint8_t count, uint32_t cp) {
  for (uint8_t i = 0; i < count; ++i) {
    if (list[i] == cp) return true;
  }
  return false;
}

// Adds the code points of `text` that `out` does not hold yet and that pass
// `wanted`, up to `capacity`.
template <typename Wanted>
inline uint8_t add_letters(const char* text, uint32_t* out, uint8_t count, uint8_t capacity, Wanted wanted) {
  const char* p = text ? text : "";
  for (uint32_t cp = next(p); cp && count < capacity; cp = next(p)) {
    if (cp >= 0x80 && wanted(cp) && !contains(out, count, cp)) out[count++] = cp;
  }
  return count;
}

// The accent page: the language's own letters, then the common ones it
// lacks, up to `capacity`. Returns how many `out` holds.
inline uint8_t accent_page(const char* own, uint32_t* out, uint8_t capacity) {
  const auto any = [](uint32_t) { return true; };
  const uint8_t count = add_letters(own, out, 0, capacity, any);
  return add_letters(kCommon, out, count, capacity, any);
}

// How many of `count` page letters each row takes, in the letter rows'
// shape: the third row (between Shift and Backspace) full first, the rest
// shared by the first two (26 letters = 10, 9, 7 like QWERTY's rows).
inline void row_counts(uint8_t count, uint8_t out[3]) {
  if (count > kPageCapacity) count = kPageCapacity;
  out[2] = count < kRowCapacity[2] ? count : kRowCapacity[2];
  const uint8_t rest = static_cast<uint8_t>(count - out[2]);
  const uint8_t half = static_cast<uint8_t>((rest + 1) / 2);
  out[0] = half < kRowCapacity[0] ? half : kRowCapacity[0];
  out[1] = static_cast<uint8_t>(rest - out[0]);
}

// The forms offered when the small `letter` of a key is held: the language's
// own first, then the common and further letters. Returns how many `out`
// holds.
inline uint8_t variants(const char* own, uint32_t letter, uint32_t* out, uint8_t capacity) {
  const auto same = [letter](uint32_t cp) { return plain(cp) == letter; };
  uint8_t count = add_letters(own, out, 0, capacity, same);
  count = add_letters(kCommon, out, count, capacity, same);
  return add_letters(kFurther, out, count, capacity, same);
}

}  // namespace keyboard_letters

#pragma once

// The Timer tile's icon from a voice timer's name ("Pasta" -> a pot). The
// words are data in the central translations (i18n Strings
// timer_icon_words): "icon=word, word; icon=word", one group per icon,
// the first match wins; spaces after the separators let the texts wrap. A
// word matches where a word of the name starts (eggs for egg); a trailing
// '!' asks for the whole word ("bad!" not in "Badminton"). Words are lower
// case; ASCII letters of the name are compared without case. Pure: no
// Arduino or LVGL, host-tested.

#include <stddef.h>
#include <string.h>

namespace timer_icons {

constexpr const char* kFallback = "timer-outline";
constexpr size_t kMaxIconChars = 40;

inline char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }

// UTF-8 bytes of other letters count as letters, so a word never starts
// inside "Wäsche".
inline bool letter(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u >= 0x80;
}

inline bool word_in(const char* name, const char* word, size_t length, bool whole) {
  if (!name || !word || !length) return false;
  for (size_t i = 0; name[i]; ++i) {
    if (i > 0 && letter(name[i - 1])) continue;
    size_t j = 0;
    while (j < length && name[i + j] && lower(name[i + j]) == word[j]) ++j;
    if (j < length) continue;
    if (whole && letter(name[i + length])) continue;
    return true;
  }
  return false;
}

constexpr size_t kMaxNameBytes = 128;

// Lower-cases ASCII and the Cyrillic capitals А-Я and Ё in place: each keeps
// its two UTF-8 bytes, so word positions stay the same.
inline void fold_cyrillic(char* text) {
  for (size_t i = 0; text[i]; ++i) {
    unsigned char lead = static_cast<unsigned char>(text[i]);
    if (lead != 0xD0 || !text[i + 1]) {
      text[i] = lower(text[i]);
      continue;
    }
    unsigned char next = static_cast<unsigned char>(text[i + 1]);
    if (next >= 0x90 && next <= 0x9F) {
      next = static_cast<unsigned char>(next + 0x20);
    } else if (next >= 0xA0 && next <= 0xAF) {
      lead = 0xD1;
      next = static_cast<unsigned char>(next - 0x20);
    } else if (next == 0x81) {
      lead = 0xD1;
      next = 0x91;
    }
    text[i] = static_cast<char>(lead);
    text[i + 1] = static_cast<char>(next);
    ++i;
  }
}

// The icon of the first group with a word in `name`, copied to `icon`;
// false without a match (or for an empty name). Names are compared lower
// case (ASCII and Cyrillic), cut to kMaxNameBytes on a character boundary.
inline bool match(const char* raw_name, const char* words, char* icon, size_t size) {
  if (!raw_name || !raw_name[0] || !words || !icon || size < 2) return false;
  char name[kMaxNameBytes];
  const size_t raw_length = strlen(raw_name);
  size_t name_length = raw_length < kMaxNameBytes - 1 ? raw_length : kMaxNameBytes - 1;
  while (name_length > 0 && name_length < raw_length &&
         (static_cast<unsigned char>(raw_name[name_length]) & 0xC0) == 0x80) {
    --name_length;
  }
  memcpy(name, raw_name, name_length);
  name[name_length] = '\0';
  fold_cyrillic(name);
  const char* group = words;
  while (*group) {
    while (*group == ' ') ++group;
    const char* end = strchr(group, ';');
    if (!end) end = group + strlen(group);
    const char* equals = static_cast<const char*>(memchr(group, '=', static_cast<size_t>(end - group)));
    if (equals && equals > group) {
      const char* word = equals + 1;
      while (word < end) {
        while (word < end && *word == ' ') ++word;
        const char* stop = static_cast<const char*>(memchr(word, ',', static_cast<size_t>(end - word)));
        if (!stop) stop = end;
        size_t length = static_cast<size_t>(stop - word);
        const bool whole = length > 0 && word[length - 1] == '!';
        if (whole) --length;
        if (word_in(name, word, length, whole)) {
          const size_t icon_length = static_cast<size_t>(equals - group);
          if (icon_length >= size) return false;
          memcpy(icon, group, icon_length);
          icon[icon_length] = '\0';
          return true;
        }
        word = stop + 1;
      }
    }
    group = *end ? end + 1 : end;
  }
  return false;
}

// The panel language's words first, then English (Home Assistant may name
// a timer in another language than the panel's).
inline bool match_any(const char* name, const char* words, const char* english, char* icon, size_t size) {
  if (match(name, words, icon, size)) return true;
  return english && english != words && match(name, english, icon, size);
}

}  // namespace timer_icons

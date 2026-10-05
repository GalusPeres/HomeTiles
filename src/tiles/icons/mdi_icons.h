#ifndef MDI_ICONS_H
#define MDI_ICONS_H

#include <Arduino.h>
#include <lvgl.h>

#include "src/devices/device_select.h"

#if defined(DEVICE_LAYOUT_1024X600)
extern const lv_font_t mdi_icons_40;
#define FONT_MDI_ICONS (&mdi_icons_40)
#elif defined(DEVICE_LAYOUT_480X480)
extern const lv_font_t mdi_icons_32;
#define FONT_MDI_ICONS (&mdi_icons_32)
#else
extern const lv_font_t mdi_icons_48;
#define FONT_MDI_ICONS (&mdi_icons_48)
#endif

// Maps an icon name to its Unicode codepoint, for example
// "home" -> 0xF02DC.
uint32_t getMdiCodepoint(const String& iconName);

// Returns true if icon name explicitly disables icon rendering (e.g. "-", "none").
bool isMdiIconDisabled(const String& iconName);

// Normalizes MDI icon names (lowercase, trim, strip mdi: prefix, honor disable token).
String normalizeMdiIconName(const String& iconName);

// Returns the Unicode character as a String, for lv_label_set_text().
String getMdiChar(const String& iconName);

// The supported icon names in lexical order, for the Web Admin icon picker.
size_t mdiIconCount();
const char* mdiIconName(size_t index);

#endif // MDI_ICONS_H

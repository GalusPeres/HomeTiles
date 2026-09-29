#include "src/types/weather/weather_icons.h"

#include "src/devices/device_select.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/types/weather/weather_icon_table.h"

// Same size as FONT_MDI_ICONS; each font falls back to that MDI font.
#if defined(DEVICE_LAYOUT_1024X600)
extern "C" { LV_FONT_DECLARE(weather_icons_40) }
#define WEATHER_ICON_FONT (&weather_icons_40)
#elif defined(DEVICE_LAYOUT_480X480)
extern "C" { LV_FONT_DECLARE(weather_icons_32) }
#define WEATHER_ICON_FONT (&weather_icons_32)
#else
extern "C" { LV_FONT_DECLARE(weather_icons_48) }
#define WEATHER_ICON_FONT (&weather_icons_48)
#endif

namespace weather_icons {

const lv_font_t* font() { return WEATHER_ICON_FONT; }

void style_label(lv_obj_t* label) {
  if (!label) return;
  lv_obj_set_style_text_font(label, WEATHER_ICON_FONT, 0);
  lv_label_set_recolor(label, true);
}

String text(const String& icon_name) {
  const String name = normalizeMdiIconName(icon_name);
  for (const auto& entry : weather_icon_table::kEntries) {
    if (name == entry.name) return String(entry.text);
  }
  return getMdiChar(icon_name);
}

}  // namespace weather_icons

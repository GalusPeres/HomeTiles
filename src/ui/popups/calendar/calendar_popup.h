#pragma once

#include <Arduino.h>
#include <lvgl.h>

// The Calendar tile's popup on the shared popup shell, laid out like the
// Weather popup (header, content, footer with a range pill, a view toggle,
// Today and the arrows): a month grid with a dot per calendar and the chosen
// day's events below, or a list of the coming days. One cached popup,
// refilled for the tile that opens it.
struct CalendarPopupTarget {
  // The tile's calendars ("calendar.a,calendar.b"), in color order.
  String entities;
  String title;
  // The tile's configured MDI icon; "" shows the calendar icon.
  String icon_name;
  bool icon_visible = true;
  // The tile's stored first day of the week (calendar_model::kWeekStartAuto).
  uint8_t week_start = 0xFF;
  // The opening tile card (popup color and header disc), may be null.
  lv_obj_t* source = nullptr;
};

void show_calendar_popup(const CalendarPopupTarget& target);
void hide_calendar_popup();
void preload_calendar_popup();
void calendar_popup_follow_tile_color(uint32_t color);
// The events of `entity` changed ("*": the day or the minute changed).
void calendar_popup_refresh(const String& entity);

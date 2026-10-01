#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "src/types/tile_type.h"

// The popup of the Lock, Alarm panel and Fan tiles on the shared popup shell
// (user 01.10., simulator build/ha-dummy-sim/panel/panel_ui.inc): Lock and
// Fan in the Light popup's frame (name in the header, the value big above
// the vertical switch or slider, a pill row below), Alarm with its state in
// the header like the PIN entry and the modes as keys in the PIN keypad's
// block. One cached popup, refilled for the tile that opens it.
struct DevicePopupTarget {
  TileType type = TILE_EMPTY;
  String entity;
  String title;
  // The tile's configured MDI icon; "" shows the state icon.
  String icon_name;
  bool icon_visible = true;
  // The opening tile card (popup color and header disc), may be null.
  lv_obj_t* source = nullptr;
};

void show_device_popup(const DevicePopupTarget& target);
void hide_device_popup();
void preload_device_popup();
void device_popup_follow_tile_color(uint32_t color);
// The detail state or the optimistic target of `entity` changed.
void device_popup_refresh(const String& entity);

// A Lock or Alarm action from a tile bar (`from_tile`) or the popup: explains
// a blocked action in the popup, asks for a code when Home Assistant needs
// one, otherwise sends.
void device_request(const DevicePopupTarget& target, const char* action, bool from_tile);
// The Bridge's answer to a Lock or Alarm command (device_control).
void device_popup_on_result(TileType type, const String& entity, const String& id, const char* status,
                            int retry_after);

#pragma once

#include <Arduino.h>

using PinPopupVerifyCallback = bool (*)(const char* pin, void* context);
using PinPopupSuccessCallback = void (*)(void* context);

struct PinPopupInit {
  String title;
  String icon_name;
  uint32_t bg_color = 0x2A2A2A;
  // Header icon color: a Folder's PIN popup takes the folder tile's icon color.
  uint32_t icon_color = 0xFFFFFF;
  bool hide_on_success = true;
  PinPopupVerifyCallback verify = nullptr;
  PinPopupSuccessCallback success = nullptr;
  void* context = nullptr;
};

void show_pin_popup(const PinPopupInit& init);
void preload_pin_popup();
void hide_pin_popup();
void resume_pin_popup_after_failed_success();
bool is_pin_popup_visible();

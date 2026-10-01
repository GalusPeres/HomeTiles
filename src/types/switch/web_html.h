#pragma once

#include <Arduino.h>
#include <vector>
#include "src/web/server/web_admin_utils.h"

void append_switch_fields_html(String& html, const String& tab_id, const std::vector<String>& switchOptions);

struct SwitchChoice {
  const char* value;
  const char* label;
};

// One choice as segmented buttons over a hidden select (setSwitchChoice in
// types/switch/admin.js); the Switch and Climate tiles use it.
void append_switch_choice(String& html, const String& tab_id, const char* field, const char* label,
                          const SwitchChoice* choices, size_t count);

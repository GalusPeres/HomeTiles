#pragma once

#include <Arduino.h>
#include "src/web/server/web_admin_utils.h"

void append_sensor_fields_html(String& html, const String& tab_id);

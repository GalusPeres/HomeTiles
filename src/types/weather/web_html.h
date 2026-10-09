#pragma once

#include <Arduino.h>
#include <vector>

void append_weather_fields_html(String& html, const String& tab_id, const std::vector<String>& weatherOptions,
                                const std::vector<String>& sensorOptions);

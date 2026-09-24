#include "src/types/clock/web_html.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

void append_clock_fields_html(String& html, const String& tab_id) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  html += R"html(
            <!-- Clock Fields -->
            <div id=")html";
  html += tab_id;
  html += R"html(_clock_fields" class="type-fields">
              <label class="inline-checkbox"><input type="checkbox" id=")html";
  html += tab_id;
  html += R"html(_clock_tile_border" checked>)html";
  html += tr.screensaver_tile_border;
  html += R"html(</label>
              <div class="clock-toggle-row">
              <label class="inline-checkbox">
                <input type="checkbox" id=")html";
  html += tab_id;
  html += R"html(_clock_show_time" checked>
                )html";
  html += tr.show_time;
  html += R"html(
              </label>
              <label class="inline-checkbox">
                <input type="checkbox" id=")html";
  html += tab_id;
  html += R"html(_clock_show_date">
                )html";
  html += tr.show_date;
  html += R"html(
              </label>
              </div>
              <label>)html";
  html += tr.time_font_size;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_clock_time_font">
                <option value="40">40 (Default)</option>
                <option value="20">20</option>
                <option value="24">24</option>
                <option value="28">28</option>
                <option value="32">32</option>
                <option value="48">48</option>
                <option value="56">56</option>
                <option value="64">64</option>
                <option value="72">72</option>
                <option value="80">80</option>
                <option value="96">96</option>
              </select>
              <label>)html";
  html += tr.date_font_size;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_clock_date_font">
                <option value="20">20 (Default)</option>
                <option value="24">24</option>
                <option value="28">28</option>
                <option value="32">32</option>
                <option value="40">40</option>
                <option value="48">48</option>
                <option value="56">56</option>
                <option value="64">64</option>
                <option value="72">72</option>
              </select>
              <label>)html";
  html += tr.time_format_label;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_clock_time_format">
                <option value="0">)html";
  html += tr.format_auto_localization;
  html += R"html(</option>
                <option value="1">)html";
  html += tr.format_24_hour;
  html += R"html(</option>
                <option value="2">)html";
  html += tr.format_12_hour;
  html += R"html(</option>
              </select>
              <label>)html";
  html += tr.date_format_label;
  html += R"html(</label>
              <select id=")html";
  html += tab_id;
  html += R"html(_clock_date_format">
                <option value="0">)html";
  html += tr.format_auto_localization;
  html += R"html(</option>
                <option value="1">DD.MM.YYYY</option>
                <option value="2">MM/DD/YYYY</option>
                <option value="3">YYYY/MM/DD</option>
              </select>
            </div>
)html";
}


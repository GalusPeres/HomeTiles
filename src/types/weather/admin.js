
function maybeFillTitleFromWeather(tab) {
    maybeFillTitleFromEntity(tab, '_weather_entity');
  }

  function loadWeatherFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const el = document.getElementById(prefix + '_weather_entity');
    if (el) el.value = data.sensor_entity || data.weather_entity || '';
    const popupModeEl = document.getElementById(prefix + '_weather_popup_open_mode');
    if (popupModeEl) popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    const colored = document.getElementById(prefix + '_weather_colored_icons');
    if (colored) colored.checked = data?.weather_colored_icons !== undefined ? !['0', 'false'].includes(String(data.weather_colored_icons)) : Number(data?.sensor_display_mode) !== 1;
    maybeFillTitleFromWeather(tab);
  }

  function saveWeatherFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    formData.append('weather_entity', document.getElementById(prefix + '_weather_entity')?.value || '');
    formData.append('popup_open_mode', document.getElementById(prefix + '_weather_popup_open_mode')?.value || '1');
    const colored = document.getElementById(prefix + '_weather_colored_icons');
    if (colored) formData.append('weather_colored_icons', colored.checked ? '1' : '0');
  }

  function resetWeatherFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const el = document.getElementById(prefix + '_weather_entity');
    if (el) el.value = '';
    const popupModeEl = document.getElementById(prefix + '_weather_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
    const colored = document.getElementById(prefix + '_weather_colored_icons');
    if (colored) colored.checked = true;
  }

  // --- Tile preview -------------------------------------------------------
  // What types/weather/renderer.cpp builds and update_weather_tile_state()
  // (tiles/runtime/tile_renderer.cpp) fills from the cached payload, at the
  // device positions (WEATHER_TILE_LAYOUT, display pixels).
  const WEATHER_CONDITION_ICONS = Object.freeze({
    'clear-night': 'weather-night', cloudy: 'weather-cloudy', exceptional: 'alert-circle-outline',
    fog: 'weather-fog', hail: 'weather-hail', lightning: 'weather-lightning',
    'lightning-rainy': 'weather-lightning-rainy', partlycloudy: 'weather-partly-cloudy',
    pouring: 'weather-pouring', rainy: 'weather-rainy', snowy: 'weather-snowy',
    'snowy-rainy': 'weather-snowy-rainy', sunny: 'weather-sunny', windy: 'weather-windy',
    'windy-variant': 'weather-windy-variant'
  });

  // A number or a numeric string (extract_json_number_or_string_field).
  function weatherPreviewNumber(value) {
    if (typeof value === 'number') return Number.isFinite(value) ? value : null;
    const text = String(value ?? '').trim().replace(',', '.');
    const number = text ? parseFloat(text) : NaN;
    return Number.isFinite(number) ? number : null;
  }

  function weatherPreviewString(value) {
    return typeof value === 'string' ? value : '';
  }

  function weatherIsoParts(iso) {
    const match = /^(\d{4})-(\d{2})-(\d{2})/.exec(String(iso || ''));
    if (!match) return null;
    const [y, m, d] = match.slice(1).map(Number);
    return y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31 ? Date.UTC(y, m - 1, d) : null;
  }

  function weatherIsoDate(utcMs) {
    const date = new Date(utcMs);
    return date.getUTCFullYear() + '-' + String(date.getUTCMonth() + 1).padStart(2, '0') + '-' +
      String(date.getUTCDate()).padStart(2, '0');
  }

  function weatherLocalToday(now) {
    return now.getFullYear() + '-' + String(now.getMonth() + 1).padStart(2, '0') + '-' +
      String(now.getDate()).padStart(2, '0');
  }

  // i18n::weather_weekday_short: the first ten characters as a date.
  function weatherWeekdayShort(iso) {
    const day = weatherIsoParts(iso);
    return day === null ? '' : (WEATHER_I18N.weekdaysShort[new Date(day).getUTCDay()] || '');
  }

  // i18n::weather_condition_label.
  function weatherConditionLabel(condition) {
    const key = String(condition || '').trim().toLowerCase();
    if (!key) return '--';
    if (WEATHER_I18N.conditions[key]) return WEATHER_I18N.conditions[key];
    const text = String(condition).replaceAll('-', ' ').replaceAll('_', ' ').trim();
    return text || '--';
  }

  // weather_icons::for_now: partly cloudy and sunny turn into their night
  // icons between the bridge's sunset and sunrise of today.
  function weatherIconForNow(name, sun, now) {
    if (!Array.isArray(sun)) return name;
    const today = weatherLocalToday(now);
    const minute = now.getHours() * 60 + now.getMinutes();
    const days = sun.filter(day => day && typeof day.d === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(day.d) &&
      (day.up !== undefined || (Number.isInteger(day.r) && Number.isInteger(day.s) &&
                                day.r >= 0 && day.r < day.s && day.s <= 1440))).slice(0, 8);
    const day = days.find(entry => entry.d === today);
    if (!day) return name;
    const night = day.up !== undefined ? day.up !== true : (minute < day.r || minute >= day.s);
    if (!night) return name;
    if (name === 'weather-partly-cloudy') return 'weather-night-partly-cloudy';
    if (name === 'weather-sunny') return 'weather-night';
    return name;
  }

  function parseWeatherPreviewPayload(raw) {
    let data = raw;
    if (typeof raw === 'string') {
      if (!raw.trim()) return null;
      try { data = JSON.parse(raw); } catch (_) { return null; }
    }
    if (!data || typeof data !== 'object' || Array.isArray(data)) return null;
    // An empty state falls back to the condition (extract_json_string_field).
    const condition = weatherPreviewString(data.state).trim() || weatherPreviewString(data.condition).trim();
    let icon = normalizeMdiIconName(weatherPreviewString(data.icon));
    if (!icon) icon = WEATHER_CONDITION_ICONS[condition.trim().toLowerCase()] || '';
    const units = data.units && typeof data.units === 'object' ? data.units : null;
    return {
      condition,
      icon: icon ? weatherIconForNow(icon, data.sun, new Date()) : '',
      temperature: weatherPreviewNumber(data.temperature),
      unit: units ? weatherPreviewString(units.temperature) : weatherPreviewString(data.temperature_unit),
      forecast: Array.isArray(data.forecast) ? data.forecast.filter(entry => entry && typeof entry === 'object') : []
    };
  }

  function weatherPreviewTemp(value) {
    return formatLocalizedNumber(value, 1, true);
  }

  // The device's filled weather icon as SVG layers (admin-icons.js); single
  // draws every layer in the label color (an icon color the user chose).
  function weatherIconSvg(name, single) {
    const entry = WEATHER_ICONS[name];
    if (!entry) return '';
    return '<svg class="weather-icon-svg" viewBox="0 0 24 24" aria-hidden="true">' +
      entry.layers.map(([color, index]) => '<g fill="' + (single ? 'currentColor' : color) + '">' +
        WEATHER_ICON_LAYER_PATHS[index] + '</g>').join('') + '</svg>';
  }

  // weather_forecast_count(): days per whole width; a half step shows as many
  // days as fit at the density of the next whole width.
  function weatherForecastCount(spanW, cardW, nextW) {
    const whole = Math.floor(Math.max(0, spanW));
    const count = span => {
      const w = Math.floor(Math.max(0, span));
      return [0, 1, 2, 4, 5, 6, 8][Math.min(w, 6)] ?? 8;
    };
    const fixed = count(spanW);
    if (spanW < 1 || spanW === whole || nextW <= 0) return fixed;
    const next = count(spanW + 0.5);
    const fits = Math.trunc(cardW * next / nextW);
    return fits <= fixed ? fixed : Math.min(fits, next);
  }

  // tile_geometry::extent in display pixels.
  function weatherExtent(position, span, cell, gap) {
    const edge = value => Math.round(value * (cell + gap));
    return edge(position + span) - edge(position) - gap;
  }

  // Fills a rendered weather preview tile: the header icon, condition |
  // temperature and the forecast columns. iconName is the tile's initial icon
  // (before a state arrives), forcedColor an icon color of the user or a rule.
  // A text's width in display pixels as LVGL lays it out: whole-pixel glyph
  // advances plus kerning (WEATHER_TILE_LAYOUT adv/kern, lv_text_get_size);
  // null when the table lacks a glyph.
  function weatherDeviceTextWidth(text, font) {
    if (!font?.adv) return null;
    const chars = [...String(text)];
    let width = 0;
    for (let i = 0; i < chars.length; ++i) {
      const advance = font.adv[chars[i]];
      if (advance === undefined) return null;
      width += advance + ((i + 1 < chars.length && font.kern?.[chars[i] + chars[i + 1]]) || 0);
    }
    return width;
  }

  function applyWeatherPreview(el, state, tile, iconName, forcedColor) {
    if (!el || typeof WEATHER_TILE_LAYOUT === 'undefined') return;
    const L = WEATHER_TILE_LAYOUT;
    const rootStyle = getComputedStyle(document.documentElement);
    const scale = parseFloat(rootStyle.getPropertyValue('--radius-preview-scale')) || 0.5;
    const colored = Number(tile?.sensor_display_mode) !== 1;
    const now = new Date();

    // Header icon: the current weather icon, colored in its layers with the
    // weather color as the label color (disc and Tile color From icon).
    let icon = el.querySelector(':scope > .tile-icon');
    const name = state ? state.icon : normalizeMdiIconName(iconName);
    // A state shows its icon even when the tile's own icon is off.
    if (!icon && name) {
      el.insertAdjacentHTML('afterbegin', '<i class="mdi tile-icon"></i>');
      icon = el.querySelector(':scope > .tile-icon');
    }
    if (icon && !name) icon.remove();
    else if (icon) {
      const entry = WEATHER_ICONS[name];
      if (colored && entry) {
        icon.className = 'mdi tile-icon tile-weather-icon';
        icon.innerHTML = weatherIconSvg(name, !!forcedColor);
        icon.style.color = forcedColor || entry.tint;
      } else {
        icon.className = 'mdi mdi-' + name + ' tile-icon';
        icon.innerHTML = '';
        icon.style.color = forcedColor || '';
      }
    }

    // Display pixels of the card and the preview's (border box) size.
    const col = Number(tile?.col) || 0;
    const spanW = Math.max(1, Number(tile?.span_w) || 1);
    const spanH = Math.max(1, Number(tile?.span_h) || 1);
    const cardW = weatherExtent(col, spanW, L.cellW, L.gap);
    const cellW = parseFloat(rootStyle.getPropertyValue('--preview-cell-w'));
    const cellH = parseFloat(rootStyle.getPropertyValue('--preview-cell-h'));
    const previewGap = parseFloat(rootStyle.getPropertyValue('--preview-gap')) || 0;
    // The tile's size like the grid gives it (.fractional-tile), never
    // measured: a re-rendered tile at a half position is placed only after
    // it is filled.
    const width = cellW > 0 ? spanW * (cellW + previewGap) - previewGap : el.offsetWidth;
    const height = cellH > 0 ? Math.max(1, Number(tile?.span_h) || 1) * (cellH + previewGap) - previewGap
      : el.offsetHeight;
    const px = value => (value * scale).toFixed(2) + 'px';
    // Unrounded sizes, the glyphs on the LVGL baseline as this browser draws
    // them (text-baseline.js).
    const font = f => {
      const size = Math.max(6, f.px * scale);
      const line = f.line * scale;
      return {size, line, shift: typeof previewBaselineShift === 'function'
        ? previewBaselineShift(size, line, (f.line - f.base) * scale)
        : (f.line / 2 - f.base - 0.364 * f.px) * scale};
    };
    const fontCss = f => 'font-size:' + f.size.toFixed(2) + 'px;line-height:' + f.line.toFixed(2) + 'px;';
    const family = getComputedStyle(el).fontFamily || 'sans-serif';
    const measure = (text, f) => {
      const context = (weatherPreviewMeasure.context ||= document.createElement('canvas').getContext('2d'));
      if (!context) return 0;
      context.font = '400 ' + f.size + 'px ' + family;
      return context.measureText(text).width;
    };
    // Positions below are in the border box; absolute children start inside
    // the 3 px editor border.
    const at = (left, top) => 'left:' + (left - 3).toFixed(2) + 'px;top:' + (top - 3).toFixed(2) + 'px;';

    // Condition | temperature.
    const valueFont = font(L.value);
    const hasTemp = !!state && state.temperature !== null;
    const tempText = hasTemp ? weatherPreviewTemp(state.temperature) + (state.unit ? ' ' + state.unit : '') : '--';
    const conditionText = state ? weatherConditionLabel(state.condition) : '--';
    let showCondition = spanW > 1 && conditionText !== '--';
    let room = 0;
    if (showCondition) {
      const gap = L.valueGap * scale;
      room = width - 2 * L.padH * scale - measure(tempText, valueFont) - measure('|', valueFont) - 2 * gap;
      const conditionWidth = measure(conditionText, valueFont);
      if (spanW < 2 ? conditionWidth > room : room < L.minConditionRoom * scale) showCondition = false;
    }
    const showForecast = Math.floor(Number(tile?.span_h) || 1) >= 2;
    const rowCenter = showForecast ? (L.cellH / 2 + L.valueDy) * scale : height / 2 + L.valueDy * scale;
    const textSpan = (cls, text, f, extra = '') => '<span class="' + cls + '" style="' + fontCss(f) +
      'top:' + f.shift.toFixed(2) + 'px;' + extra + '">' + escapeHtml(text) + '</span>';
    let html = '<div class="weather-preview-row" style="top:' + (rowCenter - valueFont.line / 2 - 3).toFixed(2) +
      'px;height:' + valueFont.line.toFixed(2) + 'px;gap:' + px(L.valueGap) + '">';
    if (showCondition) {
      html += textSpan('weather-preview-condition', conditionText, valueFont, 'max-width:' + Math.max(0, room).toFixed(2) + 'px;');
      if (hasTemp) html += textSpan('weather-preview-separator', '|', valueFont);
    }
    html += textSpan('weather-preview-temp', tempText, valueFont) + '</div>';

    // Forecast columns, spread evenly over the card, the row anchored to the
    // card's bottom like on the device.
    const count = showForecast
      ? weatherForecastCount(spanW, cardW, weatherExtent(col, spanW + 0.5, L.cellW, L.gap)) : 0;
    if (count > 0) {
      const today = weatherLocalToday(now);
      const slots = Array.from({length: count}, () => null);
      let base = today;
      let fallback = 0;
      for (const entry of state ? state.forecast : []) {
        const datetime = weatherPreviewString(entry.datetime);
        let dateLocal = weatherPreviewString(entry.date_local);
        if (!dateLocal && datetime.length >= 10) dateLocal = datetime.slice(0, 10);
        const low = ['templow', 'temperature_low', 'temp_low', 'low']
          .map(key => weatherPreviewNumber(entry[key])).find(value => value !== null) ?? null;
        const conditionIcon = WEATHER_CONDITION_ICONS[weatherPreviewString(entry.condition).trim().toLowerCase()] || '';
        const slot = {
          dateLocal,
          day: datetime ? weatherWeekdayShort(datetime) : '',
          icon: normalizeMdiIconName(weatherPreviewString(entry.icon)) || conditionIcon,
          high: weatherPreviewNumber(entry.temperature),
          low
        };
        let index = -1;
        const baseDay = weatherIsoParts(base);
        const day = weatherIsoParts(dateLocal);
        if (baseDay !== null && day !== null) {
          const offset = Math.round((day - baseDay) / 86400000);
          if (offset >= 0 && offset < count) index = offset;
        }
        if (index < 0) {
          while (fallback < count && slots[fallback]) ++fallback;
          if (fallback < count) index = fallback++;
        }
        if (index >= 0) slots[index] = slot;
      }

      const colW = L.colW * scale;
      // In display pixels with the device's integer division, so the columns
      // do not drift apart from the device's.
      const spacing = Math.trunc((Math.round(width / scale) - count * L.colW) / (count + 1)) * scale;
      const rowTop = height - (L.cellH + L.headroom - L.yOffset) * scale;
      const contentTop = rowTop + L.padV * scale;
      const contentW = (L.colW - 2 * L.padH) * scale;
      const dayFont = font(L.day);
      const tempFont = font(L.temp);
      const unitFont = font(L.unit);
      const unitText = L.unitGap + (state?.unit || '°C');
      const baseDay = weatherIsoParts(base);
      for (let i = 0; i < count; ++i) {
        const slot = slots[i];
        const left = spacing + i * (colW + spacing) + L.padH * scale;
        const displayDate = slot?.dateLocal || (baseDay !== null ? weatherIsoDate(baseDay + i * 86400000) : '');
        let dayText = slot?.day || '';
        if ((i === 0 && (slot || displayDate)) || displayDate === today) dayText = WEATHER_I18N.today;
        else if (!dayText && displayDate) dayText = weatherWeekdayShort(displayDate);
        // Until a first state arrives the device shows white placeholders.
        if (!state) dayText = '';
        html += '<div class="weather-preview-day" style="' + at(left, contentTop + L.dayTop * scale + dayFont.shift) +
          'width:' + contentW.toFixed(2) + 'px;' + fontCss(dayFont) +
          'color:' + (slot || !state ? '#FFFFFF' : '#7F8BAA') + '">' + escapeHtml(dayText || '--') + '</div>';
        if (!slot) continue;
        if (slot.icon) {
          const svg = colored ? weatherIconSvg(slot.icon, false) : '';
          html += '<div class="weather-preview-icon" style="' +
            at(left, contentTop + (L.iconTop + L.iconEmDy) * scale) + 'width:' + contentW.toFixed(2) + 'px;">' +
            (svg || '<i class="mdi mdi-' + escapeHtml(slot.icon) + '"></i>') + '</div>';
        }
        for (const [value, top] of [[slot.high, L.tempTop], [slot.low, L.lowTop]]) {
          if (value === null) continue;
          const text = weatherPreviewTemp(value);
          let valueWidth = measure(text, tempFont);
          let x;
          const valueDevice = weatherDeviceTextWidth(text, L.temp);
          const unitDevice = weatherDeviceTextWidth(unitText, L.unit);
          if (valueDevice !== null && unitDevice !== null) {
            // The device's whole-pixel layout (position_tile_value_unit_centered).
            const contentDevice = L.colW - 2 * L.padH;
            const total = valueDevice + unitDevice;
            let xDevice = Math.trunc(contentDevice / 2) - Math.trunc(total / 2);
            if (xDevice < 0) xDevice = 0;
            if (xDevice + total > contentDevice) xDevice = contentDevice - total;
            x = xDevice * scale;
            valueWidth = valueDevice * scale;
          } else {
            const total = valueWidth + measure(unitText, unitFont);
            x = contentW / 2 - total / 2;
            if (x < 0) x = 0;
            if (x + total > contentW) x = contentW - total;
          }
          const y = contentTop + top * scale;
          html += '<div class="weather-preview-temp-value" style="' + at(left + x, y + tempFont.shift) + fontCss(tempFont) + '">' +
            escapeHtml(text) + '</div>';
          html += '<div class="weather-preview-temp-unit" style="' +
            at(left + x + valueWidth, y + L.unitDy * scale + unitFont.shift) + fontCss(unitFont) + '">' +
            escapeHtml(unitText) + '</div>';
        }
      }
    }
    el.querySelectorAll(':scope > .weather-preview-row, :scope > .weather-preview-day, ' +
      ':scope > .weather-preview-icon, :scope > .weather-preview-temp-value, ' +
      ':scope > .weather-preview-temp-unit').forEach(node => node.remove());
    const handles = el.querySelector(':scope > .tile-resize-handle');
    if (handles) handles.insertAdjacentHTML('beforebegin', html);
    else el.insertAdjacentHTML('beforeend', html);
  }
  const weatherPreviewMeasure = {context: null};

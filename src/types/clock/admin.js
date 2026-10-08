
function getClockPreviewLanguage() {
    return document.getElementById('language')?.value || document.documentElement.lang || 'en';
  }

  function normalizeClockTimeFormat(raw) {
    const num = Number(raw);
    return (num === 1 || num === 2) ? num : 0;
  }

  function normalizeClockDateFormat(raw) {
    const num = Number(raw);
    return (num === 1 || num === 2 || num === 3) ? num : 0;
  }

  function resolveClockTimeFormat(raw) {
    const safe = normalizeClockTimeFormat(raw);
    if (safe !== 0) return safe;
    return getClockPreviewLanguage().toLowerCase().startsWith('de') ? 1 : 2;
  }

  function resolveClockDateFormat(raw) {
    const safe = normalizeClockDateFormat(raw);
    if (safe !== 0) return safe;
    return getClockPreviewLanguage().toLowerCase().startsWith('de') ? 1 : 2;
  }

  function getClockPreviewTime(rawFormat) {
    const now = new Date();
    const format = resolveClockTimeFormat(rawFormat);
    if (format === 2) {
      let hh = now.getHours() % 12;
      if (hh === 0) hh = 12;
      const mm = String(now.getMinutes()).padStart(2, '0');
      return String(hh) + ':' + mm + (now.getHours() < 12 ? ' AM' : ' PM');
    }
    const hh = String(now.getHours()).padStart(2, '0');
    const mm = String(now.getMinutes()).padStart(2, '0');
    return hh + ':' + mm;
  }

  function getClockPreviewDate(rawFormat) {
    const now = new Date();
    const format = resolveClockDateFormat(rawFormat);
    const dd = String(now.getDate()).padStart(2, '0');
    const mm = String(now.getMonth() + 1).padStart(2, '0');
    const yyyy = String(now.getFullYear());
    if (format === 2) return mm + '/' + dd + '/' + yyyy;
    if (format === 3) return yyyy + '/' + mm + '/' + dd;
    return dd + '.' + mm + '.' + yyyy;
  }

  function normalizeClockPreviewFont(raw, fallback) {
    const num = Number(raw);
    switch (num) {
      case 20:
      case 24:
      case 28:
      case 32:
      case 40:
      case 48:
      case 56:
      case 64:
      case 72:
      case 80:
      case 96:
        return num;
      default:
        return fallback;
    }
  }

  function getClockPreviewCssPx(raw, fallback, el) {
    const n = normalizeClockPreviewFont(raw, fallback);
    // Same scaling as the CSS variables (LVGL pixels * preview factor), as the
    // tile's own grid sets them (a grid may scale its preview).
    const v = parseFloat(getComputedStyle(el?.isConnected ? el : document.documentElement).getPropertyValue('--fs' + n));
    return (v > 0) ? v : Math.round(n / 2);
  }

  // Each clock line is as tall as its LVGL font's line height, with the
  // glyphs on the LVGL baseline (--lh/--ldy, web_admin_styles.cpp).
  function clockPreviewLineCss(size) {
    return 'line-height:var(--lh' + size + '); top:var(--ldy' + size + ', 0px);';
  }

  function applyClockPreviewLine(el, size, px) {
    el.style.fontSize = px + 'px';
    el.style.lineHeight = 'var(--lh' + size + ')';
    el.style.top = 'var(--ldy' + size + ', 0px)';
  }

  function getClockPreviewTextStyle(raw, fallback, color) {
    const font = normalizeClockPreviewFont(raw, fallback);
    const size = getClockPreviewCssPx(raw, fallback);
    const safeColor = color || '#fff';
    return 'data-clock-font="' + font + '" style="font-size:' + size + 'px; ' +
      clockPreviewLineCss(font) + ' color:' + safeColor + ';"';
  }

  function applyClockPreviewTextStyle(el, raw, fallback, color) {
    if (!el) return;
    const font = normalizeClockPreviewFont(raw, fallback);
    el.dataset.clockFont = String(font);
    applyClockPreviewLine(el, font, getClockPreviewCssPx(raw, fallback, el));
    el.style.color = color || '#fff';
  }

  function normalizeClockFlags(raw) {
    const num = Number(raw);
    if (!Number.isFinite(num) || num < 0) return 1;
    const flags = num & 3;
    return flags === 0 ? 1 : flags;
  }

  function getClockFlagsFromInputs(prefix) {
    const timeEl = document.getElementById(prefix + '_clock_show_time');
    const dateEl = document.getElementById(prefix + '_clock_show_date');
    let flags = 0;
    if (timeEl && timeEl.checked) flags |= 1;
    if (dateEl && dateEl.checked) flags |= 2;
    if (flags === 0) flags = 1;
    return flags;
  }

  function applyClockFlagsToInputs(prefix, flags) {
    const safe = normalizeClockFlags(flags);
    const timeEl = document.getElementById(prefix + '_clock_show_time');
    const dateEl = document.getElementById(prefix + '_clock_show_date');
    if (timeEl) timeEl.checked = (safe & 1) !== 0;
    if (dateEl) dateEl.checked = (safe & 2) !== 0;
  }

  function ensureClockSelection(prefix) {
    const timeEl = document.getElementById(prefix + '_clock_show_time');
    const dateEl = document.getElementById(prefix + '_clock_show_date');
    if (!timeEl || !dateEl) return;
    if (!timeEl.checked && !dateEl.checked) timeEl.checked = true;
  }

  function loadClockFields(tab, data) {
    loadIconColorFields(tab, data);
    const border = document.getElementById(tab + '_clock_tile_border');
    if (border) border.checked = data?.tile_border !== undefined ? !['0','false'].includes(String(data.tile_border)) : Number(data?.sensor_display_mode) !== 1;
    const timeFontEl = document.getElementById(tab + '_clock_time_font');
    if (timeFontEl) {
      const timeFont = (data && data.key_code !== undefined) ? Number(data.key_code) : 40;
      timeFontEl.value = String(timeFont);
    }
    const dateFontEl = document.getElementById(tab + '_clock_date_font');
    if (dateFontEl) {
      const storedDateFont = (data && data.key_modifier !== undefined)
        ? Number(data.key_modifier) : 20;
      const dateFont = Math.min(72, storedDateFont || 20);
      dateFontEl.value = String(dateFont);
    }
    const timeFormatEl = document.getElementById(tab + '_clock_time_format');
    if (timeFormatEl) {
      const timeFormat = (data && data.sensor_gauge_min !== undefined) ? data.sensor_gauge_min : (data ? data.clock_time_format : 0);
      timeFormatEl.value = String(timeFormat !== undefined ? timeFormat : 0);
    }
    const dateFormatEl = document.getElementById(tab + '_clock_date_format');
    if (dateFormatEl) {
      const dateFormat = (data && data.sensor_gauge_max !== undefined) ? data.sensor_gauge_max : (data ? data.clock_date_format : 0);
      dateFormatEl.value = String(dateFormat !== undefined ? dateFormat : 0);
    }
    if (data && (data.clock_show_time !== undefined || data.clock_show_date !== undefined)) {
      const showTime = String(data.clock_show_time || '0') === '1';
      const showDate = String(data.clock_show_date || '0') === '1';
      let flags = 0;
      if (showTime) flags |= 1;
      if (showDate) flags |= 2;
      if (flags === 0) flags = 1;
      applyClockFlagsToInputs(tab, flags);
      return;
    }
    const flags = (data && data.clock_flags !== undefined && data.clock_flags !== null)
      ? data.clock_flags
      : (data ? data.sensor_decimals : 1);
    applyClockFlagsToInputs(tab, flags);
  }

  function updateClockValuePreview(tab) {
    if (currentTileIndex === -1) return;
    const prefix = tab;
    const tileId = tab + '-tile-' + currentTileIndex;
    const tileElem = document.getElementById(tileId);
    if (!tileElem) return;

    const flags = getClockFlagsFromInputs(prefix);
    const timeFont = document.getElementById(prefix + '_clock_time_font')?.value || '40';
    const dateFont = Math.min(72,
      Number(document.getElementById(prefix + '_clock_date_font')?.value || 20));
    const timeFormat = document.getElementById(prefix + '_clock_time_format')?.value || '0';
    const dateFormat = document.getElementById(prefix + '_clock_date_format')?.value || '0';
    const timeEl = tileElem.querySelector('.tile-clock-time');
    const dateEl = tileElem.querySelector('.tile-clock-date');

    const needsTime = (flags & 1) !== 0;
    const needsDate = (flags & 2) !== 0;
    if ((needsTime && !timeEl) || (needsDate && !dateEl) || (!needsTime && timeEl) || (!needsDate && dateEl)) {
      updateTilePreview(tab);
      return;
    }

    if (timeEl) {
      timeEl.textContent = getClockPreviewTime(timeFormat);
      applyClockPreviewTextStyle(timeEl, timeFont, 40, '#fff');
    }
    if (dateEl) {
      dateEl.textContent = getClockPreviewDate(dateFormat);
      applyClockPreviewTextStyle(dateEl, dateFont, 20, '#fff');
    }
    fitCompactClockPreview(tileElem);
  }

  const CLOCK_PREVIEW_FONT_SIZES = [20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96];
  let clockPreviewMeasureContext = null;

  function measureClockPreviewText(el, text, px) {
    clockPreviewMeasureContext = clockPreviewMeasureContext ||
      document.createElement('canvas').getContext('2d');
    if (!clockPreviewMeasureContext) return 0;
    const style = getComputedStyle(el);
    clockPreviewMeasureContext.font = style.fontWeight + ' ' + px + 'px ' + style.fontFamily;
    return clockPreviewMeasureContext.measureText(text).width;
  }

  // Worst-case samples keep the chosen size stable while the time changes.
  function clockPreviewSample(el, isTime) {
    return isTime ? (/[AP]M/.test(el.textContent) ? '88:88 PM' : '88:88')
      : el.textContent.replace(/[0-9]/g, '8');
  }

  // Half-height clocks use one row: the largest configured-or-smaller size whose
  // rendered size fits 80% of the tile height and whose text fits the width.
  // The date follows only from width 2 and only when it still fits.
  // The firmware applies the same rule (fit_compact_clock in clock/renderer.cpp).
  function fitCompactClockPreview(tileElem) {
    if (!tileElem) return;
    const lines = [tileElem.querySelector('.tile-clock-time'), tileElem.querySelector('.tile-clock-date')];
    lines.forEach(el => {
      if (!el) return;
      el.hidden = false;
      const font = normalizeClockPreviewFont(el.dataset.clockFont, 40);
      applyClockPreviewLine(el, font, getClockPreviewCssPx(font, 40, tileElem));
    });
    if (!tileElem.classList.contains('clock-compact')) return;
    const style = getComputedStyle(tileElem);
    // The tile's own grid sizes (inherited variables), not the page's.
    const root = tileElem.isConnected ? style : getComputedStyle(document.documentElement);
    const cellW = parseFloat(root.getPropertyValue('--preview-cell-w'));
    const cellH = parseFloat(root.getPropertyValue('--preview-cell-h'));
    const gridGap = parseFloat(root.getPropertyValue('--preview-gap')) || 0;
    const span = (value, cell) => (Number(value) || 1) * (cell + gridGap) - gridGap;
    // Hidden folder tabs have no layout yet; the grid variables still hold the size.
    const tileW = cellW > 0 ? span(tileElem.dataset.spanW, cellW) : tileElem.clientWidth;
    const tileH = cellH > 0 ? span(tileElem.dataset.spanH, cellH) : tileElem.clientHeight;
    const availW = tileW - parseFloat(style.paddingLeft || 0) - parseFloat(style.paddingRight || 0);
    const maxPx = tileH * 0.8;
    const gap = parseFloat(style.columnGap || 0) || 0;
    const [time, date] = lines;
    const primary = time || date;
    const secondary = time && date && Number(tileElem.dataset.spanW) >= 2 ? date : null;
    if (date && date !== primary && date !== secondary) date.hidden = true;
    if (!primary) return;
    const fit = (el, capPx, usedW) => {
      const sample = clockPreviewSample(el, el === time);
      for (const size of [...CLOCK_PREVIEW_FONT_SIZES].reverse()) {
        const px = getClockPreviewCssPx(size, size, tileElem);
        if (size > Number(el.dataset.clockFont || 40) || px > capPx) continue;
        const width = measureClockPreviewText(el, sample, px);
        if (usedW + width <= availW) return { size, px, width };
      }
      return null;
    };
    const first = fit(primary, maxPx, 0) || { size: 20, px: getClockPreviewCssPx(20, 20, tileElem), width: 0 };
    applyClockPreviewLine(primary, first.size, first.px);
    if (!secondary) return;
    const second = fit(secondary, first.px, first.width + gap);
    if (second) applyClockPreviewLine(secondary, second.size, second.px);
    else secondary.hidden = true;
  }

  function saveClockFields(tab, formData) {
    saveIconColorFields(tab, formData);
    formData.append('tile_border', document.getElementById(tab + '_clock_tile_border')?.checked === false ? '0' : '1');
    ensureClockSelection(tab);
    const flags = getClockFlagsFromInputs(tab);
    formData.append('clock_show_time', (flags & 1) ? '1' : '0');
    formData.append('clock_show_date', (flags & 2) ? '1' : '0');
    formData.append('key_code', document.getElementById(tab + '_clock_time_font')?.value || '40');
    formData.append('key_modifier', document.getElementById(tab + '_clock_date_font')?.value || '20');
    formData.append('clock_time_format', document.getElementById(tab + '_clock_time_format')?.value || '0');
    formData.append('clock_date_format', document.getElementById(tab + '_clock_date_format')?.value || '0');
  }

  function resetClockFields(tab) {
    resetIconColorFields(tab);
    const border = document.getElementById(tab + '_clock_tile_border');
    if (border) border.checked = true;
    applyClockFlagsToInputs(tab, 1);
    const timeFontEl = document.getElementById(tab + '_clock_time_font');
    if (timeFontEl) timeFontEl.value = '40';
    const dateFontEl = document.getElementById(tab + '_clock_date_font');
    if (dateFontEl) dateFontEl.value = '24';
    const timeFormatEl = document.getElementById(tab + '_clock_time_format');
    if (timeFormatEl) timeFormatEl.value = '0';
    const dateFormatEl = document.getElementById(tab + '_clock_date_format');
    if (dateFormatEl) dateFormatEl.value = '0';
  }

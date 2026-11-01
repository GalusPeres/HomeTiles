
function maybeFillTitleFromSensor(tab) {
    maybeFillTitleFromEntity(tab, '_sensor_entity');
  }

  // A finished text with its unit after a number ("21,5 °C", "Open · 58 %")
  // as HTML, the unit a step smaller (.tile-unit), like the panel's
  // value_unit::show_text (value_unit_rules.h split_unit): the last word, or
  // the letters after the last digit, without a digit, right after a digit
  // (one space allowed), on one line; any other text escaped as it is.
  function unitTextHtml(text) {
    const value = String(text ?? '');
    const match = /^([^\n]*\d) ?([^ \d\n]+)$/.exec(value);
    const bytes = match ? [...match[2]].reduce((n, c) => n + (c.codePointAt(0) < 0x80 ? 1 : c.codePointAt(0) < 0x800 ? 2 : c.codePointAt(0) < 0x10000 ? 3 : 4), 0) : 0;
    if (!match || bytes > 12) return escapeHtml(value);
    return escapeHtml(match[1]) + '<span class="tile-unit">' + escapeHtml(match[2]) + '</span>';
  }

  // A unit's inline size for a number of `valuePx` panel pixels, the step
  // the panel takes (value_unit_rules.h unit_px over VALUE_UNIT_FONTS, its
  // thin gap and the digits' tops meeting); the .tile-unit rule (80 %)
  // stands in where the number's size is not known.
  function valueUnitStyle(valuePx) {
    const sizes = typeof VALUE_UNIT_FONTS !== 'undefined' && VALUE_UNIT_FONTS.length ? VALUE_UNIT_FONTS : [16, 20, 24, 28, 32];
    let unit = sizes[0];
    for (const size of sizes) if (size < valuePx && size * 10 <= valuePx * 8) unit = size;
    const ratio = unit / valuePx;
    return 'font-size:' + ratio.toFixed(4) + 'em;margin-left:' + (Math.floor((unit + 3) / 6) / unit).toFixed(4) +
      'em;vertical-align:' + (0.727 * (1 - ratio) / ratio).toFixed(4) + 'em';
  }

  function formatSensorValue(value, decimals) {
    if (value === undefined || value === null) return '--';
    let text = String(value).trim();
    if (!text.length) return '--';
    const lower = text.toLowerCase();
    if (lower === 'unavailable' || lower === 'unknown' || lower === 'none') return '--';
    if (decimals === undefined || decimals === null || decimals === '' || Number(decimals) === -1) {
      return localizeNumericText(text);
    }
    const num = parseFloat(text.replace(',', '.'));
    if (isNaN(num) || !isFinite(num)) return text;
    const d = Math.max(0, Math.min(6, parseInt(decimals, 10) || 0));
    return formatLocalizedNumber(num, d, false);
  }

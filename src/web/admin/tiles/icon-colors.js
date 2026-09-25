  // Per-tile icon colors for the Sensor family (Sensor, Number, Select,
  // Date/Time), Binary sensor and Energy: a fixed icon color, a color bar for
  // numeric states ("Icon color by value") and up to six state colors for
  // text states ("Icon color by state"); Binary sensors show one On and one
  // Off color, stored as the state lines "is RRGGBB on" and "is RRGGBB off".
  // The editor keeps the canonical v2 record of
  // src/tiles/config/tile_icon_colors.h in the "icon_colors" field. Parsing,
  // normalization and the color formulas below mirror that header step by
  // step, so the Web Admin previews show the device colors. The six type
  // modules call the load/save/reset helpers from their own field handlers,
  // so drafts, copy/paste, autosave and import/export carry the record like
  // any other type field.
  const ICON_COLOR_TYPES = ['1', '14', '20', '21', '22', '23'];
  const ICON_COLOR_BAR_TYPES = ['1', '14', '21'];
  const ICON_COLOR_ROW_TYPES = ['1', '20', '22', '23'];
  const ICON_COLOR_MAX_STOPS = 6;
  const ICON_COLOR_MAX_ROWS = 6;
  const ICON_COLOR_MAX_VALUE_BYTES = 32;
  const ICON_COLOR_MAX_NUMBER_BYTES = 12;
  const ICON_COLOR_LEGACY_MAX_RULES = 3;
  const ICON_COLOR_ROW_DEFAULT = '#22C55E';
  // The Binary sensor state colors without per-tile colors.
  const ICON_COLOR_BINARY_DEFAULTS = { on: '#FFC107', off: '#9E9E9E' };
  // Stop positions are thousandths of the bar.
  const ICON_COLOR_PRESETS = {
    cold_warm: [[0, 0x3B82F6], [450, 0x22C55E], [700, 0xF59E0B], [1000, 0xEF4444]],
    traffic: [[0, 0x22C55E], [500, 0xEAB308], [1000, 0xEF4444]],
    battery: [[0, 0xEF4444], [250, 0xF59E0B], [600, 0x22C55E], [1000, 0x22C55E]],
    humidity: [[0, 0xF59E0B], [400, 0x22C55E], [700, 0x22C55E], [1000, 0x3B82F6]],
    single: [[0, 0xFFFFFF], [1000, 0xFFFFFF]]
  };

  function tileTypeHasIconColors(typeValue) {
    return ICON_COLOR_TYPES.includes(String(typeValue ?? '0'));
  }

  // ---- Record model (mirrors tile_icon_colors.h) ----

  function iconColorTrim(text) {
    return String(text).replace(/^[ \t\r]+|[ \t\r]+$/g, '');
  }

  // "RRGGBB" or "#RRGGBB", exactly.
  function iconColorParseHex(text) {
    const match = /^#?([0-9a-fA-F]{6})$/.exec(String(text));
    return match ? parseInt(match[1], 16) : null;
  }

  function iconColorHex(rgb) {
    return (rgb >>> 0).toString(16).toUpperCase().padStart(6, '0');
  }

  function normalizeIconColorHex(value) {
    const rgb = iconColorParseHex(String(value ?? '').trim());
    return rgb === null ? '' : '#' + iconColorHex(rgb);
  }

  function iconColorIsV2(text) {
    return text.startsWith('v2') && (text.length === 2 || text[2] === '\n' || text[2] === '\r');
  }

  // parse_decimal(): "[-]digits[.digits]" with comma or dot, at most twelve
  // characters; returns the value and the canonical text.
  function iconColorParseDecimal(text) {
    const trimmed = iconColorTrim(text);
    if (!trimmed || trimmed.length > ICON_COLOR_MAX_NUMBER_BYTES ||
        !/^-?[0-9]*[.,]?[0-9]*$/.test(trimmed) || !/[0-9]/.test(trimmed)) return null;
    const canonical = trimmed.replace(',', '.');
    const value = Number(canonical);
    return Number.isFinite(value) ? { value, text: canonical } : null;
  }

  // leading_number(): the leading decimal number of a state, without exponent.
  function iconColorLeadingNumber(state) {
    const match = /^[ \t\r]*(-?[0-9]*(?:[.,][0-9]*)?)/.exec(String(state));
    const text = match ? match[1] : '';
    if (!/[0-9]/.test(text) || text.length > 31) return null;
    const value = Number(text.replace(',', '.'));
    return Number.isFinite(value) ? value : null;
  }

  // fold_at(): ASCII and the Latin-1 letters U+00C0-U+00DE except U+00D7.
  function iconColorFold(text) {
    let out = '';
    for (const ch of String(text)) {
      const code = ch.codePointAt(0);
      if (code >= 0x41 && code <= 0x5A) out += String.fromCharCode(code + 32);
      else if (code >= 0xC0 && code <= 0xDE && code !== 0xD7) out += String.fromCharCode(code + 0x20);
      else out += ch;
    }
    return out;
  }

  function iconColorTextMatches(op, value, state) {
    if (state === null || state === undefined) return false;
    const haystack = iconColorFold(iconColorTrim(String(state)));
    const needle = iconColorFold(value);
    if (op === 'is') return haystack === needle;
    return needle.length > 0 && haystack.includes(needle);
  }

  // parse_rule(): "<op> RRGGBB <value>".
  function iconColorParseRule(line) {
    const text = iconColorTrim(line);
    const space = text.indexOf(' ');
    const op = space < 0 ? '' : text.slice(0, space);
    if (!['ge', 'le', 'eq', 'is', 'has'].includes(op)) return null;
    const rest = text.slice(space + 1);
    const colorEnd = rest.indexOf(' ');
    const color = iconColorParseHex(colorEnd < 0 ? rest : rest.slice(0, colorEnd));
    if (color === null) return null;
    const value = colorEnd < 0 ? '' : iconColorTrim(rest.slice(colorEnd));
    return value ? { op, color, value } : null;
  }

  function iconColorSortStops(stops) {
    return stops.sort((a, b) => a.position - b.position);
  }

  // parse_stop(): "P:RRGGBB", P with 1-4 digits, clamped to 0..1000.
  function iconColorParseStop(token) {
    const colon = token.indexOf(':');
    if (colon < 1 || colon > 4 || !/^[0-9]+$/.test(token.slice(0, colon))) return null;
    const color = iconColorParseHex(token.slice(colon + 1));
    if (color === null) return null;
    return { position: Math.min(Number(token.slice(0, colon)), 1000), color };
  }

  // parse_bar(): "bar <smooth|steps> <min> <max> <P>:<RRGGBB> ...".
  function iconColorParseBar(line) {
    const tokens = String(line).split(/[ \t\r]+/).filter(Boolean);
    if (tokens[0] !== 'bar' || !['smooth', 'steps'].includes(tokens[1])) return null;
    const min = iconColorParseDecimal(tokens[2] ?? '');
    const max = iconColorParseDecimal(tokens[3] ?? '');
    if (!min || !max || !(min.value < max.value)) return null;
    const stops = [];
    for (const token of tokens.slice(4)) {
      if (stops.length >= ICON_COLOR_MAX_STOPS) break;
      const stop = iconColorParseStop(token);
      if (stop) stops.push(stop);
    }
    if (stops.length < 2) return null;
    return { mode: tokens[1], min: min.value, max: max.value, minText: min.text, maxText: max.text,
      stops: iconColorSortStops(stops) };
  }

  // mix_colors(): per 8-bit channel with a weight of 0..1024, rounded.
  function iconColorMix(a, b, weight) {
    let out = 0;
    for (let shift = 16; shift >= 0; shift -= 8) {
      const from = (a >> shift) & 0xFF;
      const to = (b >> shift) & 0xFF;
      out |= ((from * (1024 - weight) + to * weight + 512) >> 10) << shift;
    }
    return out >>> 0;
  }

  // bar_color_at(): smooth bars interpolate between neighbouring stops;
  // steps bars take the last stop at or below t; t is clamped to 0..1.
  function iconColorBarColorAt(bar, t) {
    if (!(t > 0)) t = 0;
    if (t > 1) t = 1;
    let index = -1;
    bar.stops.forEach((stop, i) => { if (stop.position / 1000 <= t) index = i; });
    if (index < 0) return bar.stops[0].color;
    if (bar.mode === 'steps' || index + 1 === bar.stops.length) return bar.stops[index].color;
    const from = bar.stops[index].position / 1000;
    const to = bar.stops[index + 1].position / 1000;
    const fraction = (t - from) / (to - from);
    return iconColorMix(bar.stops[index].color, bar.stops[index + 1].color, Math.floor(fraction * 1024 + 0.5));
  }

  function iconColorBarColor(bar, value) {
    return iconColorBarColorAt(bar, (value - bar.min) / (bar.max - bar.min));
  }

  // resolve(): the icon color for a known state as "#RRGGBB", or '' for the
  // type default. Only v2 records are evaluated.
  function resolveIconColorRecord(record, state, display) {
    const text = String(record ?? '');
    if (!iconColorIsV2(text) || state === undefined || state === null) return '';
    const lines = text.split('\n');
    const number = iconColorLeadingNumber(state);
    let barSeen = false;
    for (const line of lines.slice(2)) {
      if (line.startsWith('bar ')) {
        if (barSeen) continue;
        barSeen = true;
        const bar = number !== null ? iconColorParseBar(line) : null;
        if (bar) return '#' + iconColorHex(iconColorBarColor(bar, number));
        continue;
      }
      const rule = iconColorParseRule(line);
      if (!rule || (rule.op !== 'is' && rule.op !== 'has')) continue;
      if (iconColorTextMatches(rule.op, rule.value, state) ||
          (display !== undefined && display !== null && iconColorTextMatches(rule.op, rule.value, display))) {
        return '#' + iconColorHex(rule.color);
      }
    }
    const fixed = iconColorParseHex(iconColorTrim(lines[1] ?? ''));
    return fixed === null ? '' : '#' + iconColorHex(fixed);
  }

  // copy_value() plus trim: no control characters, at most 32 UTF-8 bytes
  // without a split character.
  function iconColorClipValue(value) {
    let bytes = new TextEncoder().encode(String(value).replace(/[\u0000-\u001f\u007f]/g, ''));
    if (bytes.length > ICON_COLOR_MAX_VALUE_BYTES) {
      let n = ICON_COLOR_MAX_VALUE_BYTES;
      let lead = n;
      while (lead > 0 && (bytes[lead - 1] & 0xC0) === 0x80) lead--;
      if (lead > 0) {
        const c = bytes[lead - 1];
        const need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (n - (lead - 1) < need) n = lead - 1;
      }
      bytes = bytes.slice(0, n);
    }
    return iconColorTrim(new TextDecoder().decode(bytes));
  }

  // A b39 numeric rule counts only with a complete number.
  function iconColorLegacyNumericValid(value) {
    return new TextEncoder().encode(value).length <= ICON_COLOR_MAX_VALUE_BYTES &&
      /^[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?$/.test(value.replace(/,/g, '.'));
  }

  // migrate_legacy_bar(): b39 ">=" thresholds on whole numbers in descending
  // order become a steps bar when their positions are exact thousandths.
  function iconColorMigrateLegacyBar(body, fixed) {
    const thresholds = [];
    const colors = [];
    let rules = 0;
    for (const line of body) {
      if (rules >= ICON_COLOR_LEGACY_MAX_RULES) break;
      const rule = iconColorParseRule(line);
      if (!rule) continue;
      if (rule.op === 'is' || rule.op === 'has') { rules++; continue; }
      if (!iconColorLegacyNumericValid(rule.value)) continue;
      rules++;
      if (rule.op !== 'ge' || !/^-?[0-9]{1,7}$/.test(rule.value)) return null;
      const value = Number(rule.value);
      if (thresholds.length && value >= thresholds[thresholds.length - 1]) return null;
      thresholds.push(value);
      colors.push(rule.color);
    }
    if (!thresholds.length) return null;
    const lowest = thresholds[thresholds.length - 1];
    const highest = thresholds[0];
    const min = thresholds.length === 1 ? lowest - 1 : lowest - (highest - lowest);
    const range = highest - min;
    const stops = [{ position: 0, color: fixed === null ? 0xFFFFFF : fixed }];
    for (let i = thresholds.length - 1; i >= 0; i--) {
      const scaled = (thresholds[i] - min) * 1000;
      if (scaled % range) return null;
      stops.push({ position: scaled / range, color: colors[i] });
    }
    return { mode: 'steps', min, max: highest, minText: String(min), maxText: String(highest), stops };
  }

  // normalize(): any record (editor, import, b39) in the canonical v2 form;
  // numeric types keep only the bar, text types only the state lines.
  function normalizeIconColorRecord(record, allowBar, allowRows) {
    const text = String(record ?? '');
    const v2 = iconColorIsV2(text);
    const lines = text.split('\n');
    const fixedIndex = v2 ? 1 : 0;
    const fixed = iconColorParseHex(iconColorTrim(lines[fixedIndex] ?? ''));
    const body = lines.slice(fixedIndex + 1);
    let bar = null;
    if (allowBar && v2) {
      const line = body.find(candidate => candidate.startsWith('bar '));
      if (line !== undefined) bar = iconColorParseBar(line);
    } else if (allowBar) {
      bar = iconColorMigrateLegacyBar(body, fixed);
    }
    let out = 'v2\n' + (fixed === null ? '' : iconColorHex(fixed));
    if (bar) {
      out += '\nbar ' + bar.mode + ' ' + bar.minText + ' ' + bar.maxText +
        bar.stops.map(stop => ' ' + stop.position + ':' + iconColorHex(stop.color)).join('');
    }
    let rows = 0;
    if (allowRows) {
      let legacyRules = 0;
      for (const line of body) {
        if (rows >= ICON_COLOR_MAX_ROWS) break;
        if (v2 && line.startsWith('bar ')) continue;
        const rule = iconColorParseRule(line);
        if (!rule) continue;
        const textRule = rule.op === 'is' || rule.op === 'has';
        if (!v2) {
          if (legacyRules >= ICON_COLOR_LEGACY_MAX_RULES) break;
          if (!textRule) {
            if (iconColorLegacyNumericValid(rule.value)) legacyRules++;
            continue;
          }
          legacyRules++;
        } else if (!textRule) {
          continue;
        }
        const value = iconColorClipValue(rule.value);
        if (!value) continue;
        out += '\n' + rule.op + ' ' + iconColorHex(rule.color) + ' ' + value;
        rows++;
      }
    }
    return fixed === null && !bar && rows === 0 ? '' : out;
  }

  // Editor view of a record: fixed color, bar and state rows.
  function parseIconColorRecord(record) {
    const lines = normalizeIconColorRecord(record, true, true).split('\n');
    const fixed = iconColorParseHex(lines[1] ?? '');
    let bar = null;
    const rows = [];
    for (const line of lines.slice(2)) {
      if (line.startsWith('bar ')) { bar = iconColorParseBar(line); continue; }
      const rule = iconColorParseRule(line);
      if (rule) rows.push({ has: rule.op === 'has', color: '#' + iconColorHex(rule.color), value: rule.value });
    }
    return { color: fixed === null ? '' : '#' + iconColorHex(fixed), bar, rows };
  }

  // ---- Editor ----

  const iconColorEl = (tab, suffix) => document.getElementById(tab + suffix);

  function iconColorTypeOf(tab) {
    return String(iconColorEl(tab, '_tile_type')?.value || '0');
  }

  // Bar state lives in a hidden input: "" (off) or "<mode> P:RRGGBB ...".
  function readIconColorBar(tab) {
    const tokens = String(iconColorEl(tab, '_tile_icon_bar')?.value || '').split(' ').filter(Boolean);
    const mode = ['smooth', 'steps'].includes(tokens[0]) ? tokens[0] : 'off';
    const stops = mode === 'off' ? [] : tokens.slice(1).map(iconColorParseStop).filter(Boolean);
    return { mode, stops };
  }

  function writeIconColorBar(tab, mode, stops) {
    const input = iconColorEl(tab, '_tile_icon_bar');
    if (!input) return;
    input.value = mode === 'off' ? ''
      : [mode].concat(stops.map(stop => stop.position + ':' + iconColorHex(stop.color))).join(' ');
  }

  function iconColorSelectedStop(tab) {
    const value = Number(iconColorEl(tab, '_tile_icon_bar_handles')?.dataset.selected ?? -1);
    return Number.isInteger(value) ? value : -1;
  }

  function setIconColorSelectedStop(tab, index) {
    const handles = iconColorEl(tab, '_tile_icon_bar_handles');
    if (handles) handles.dataset.selected = String(index);
  }

  function iconColorBarRange(tab) {
    const min = iconColorParseDecimal(iconColorEl(tab, '_tile_icon_bar_min')?.value ?? '');
    const max = iconColorParseDecimal(iconColorEl(tab, '_tile_icon_bar_max')?.value ?? '');
    return min && max && min.value < max.value ? { min: min.value, max: max.value } : null;
  }

  function ensureIconColorRange(tab) {
    if (iconColorBarRange(tab)) return;
    const min = iconColorEl(tab, '_tile_icon_bar_min');
    const max = iconColorEl(tab, '_tile_icon_bar_max');
    if (min) min.value = '0';
    if (max) max.value = '100';
  }

  function iconColorFormatValue(value, span) {
    const decimals = span >= 20 ? 0 : (span >= 2 ? 1 : 2);
    return String(Number(value.toFixed(decimals)));
  }

  function iconColorGradient(mode, stops) {
    const css = rgb => '#' + iconColorHex(rgb);
    if (mode === 'steps') {
      const parts = [];
      stops.forEach((stop, index) => {
        const from = index === 0 ? 0 : stop.position / 10;
        const to = index + 1 < stops.length ? stops[index + 1].position / 10 : 100;
        parts.push(css(stop.color) + ' ' + from + '%', css(stop.color) + ' ' + to + '%');
      });
      return 'linear-gradient(90deg, ' + parts.join(', ') + ')';
    }
    return 'linear-gradient(90deg, ' +
      stops.map(stop => css(stop.color) + ' ' + stop.position / 10 + '%').join(', ') + ')';
  }

  // Draws the bar, the stop handles and their value labels. Existing handles
  // are updated in place so a drag keeps its pointer capture.
  function renderIconColorBar(tab) {
    const section = iconColorEl(tab, '_tile_icon_bar_section');
    if (!section) return;
    const bar = readIconColorBar(tab);
    section.querySelectorAll('[data-icon-color="mode"]').forEach(button => {
      const active = button.dataset.mode === bar.mode;
      button.classList.toggle('active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
    iconColorEl(tab, '_tile_icon_bar_editor')?.classList.toggle('hidden', bar.mode === 'off');
    const strip = iconColorEl(tab, '_tile_icon_bar_strip');
    const handles = iconColorEl(tab, '_tile_icon_bar_handles');
    if (!strip || !handles || bar.mode === 'off') return;
    strip.style.background = iconColorGradient(bar.mode, iconColorSortStops(bar.stops.map(stop => ({ ...stop }))));
    let knobs = Array.from(handles.querySelectorAll('.icon-color-stop'));
    let labels = Array.from(handles.querySelectorAll('.icon-color-stop-label'));
    if (knobs.length !== bar.stops.length) {
      knobs.concat(labels).forEach(el => el.remove());
      knobs = bar.stops.map((stop, index) => {
        const knob = document.createElement('button');
        knob.type = 'button';
        knob.className = 'icon-color-stop';
        knob.dataset.iconColor = 'stop';
        knob.dataset.stop = String(index);
        handles.appendChild(knob);
        return knob;
      });
      labels = bar.stops.map(() => {
        const label = document.createElement('span');
        label.className = 'icon-color-stop-label';
        handles.appendChild(label);
        return label;
      });
    }
    const selected = iconColorSelectedStop(tab);
    const range = iconColorBarRange(tab);
    bar.stops.forEach((stop, index) => {
      const left = stop.position / 10 + '%';
      knobs[index].style.left = left;
      knobs[index].style.background = '#' + iconColorHex(stop.color);
      knobs[index].classList.toggle('selected', index === selected);
      labels[index].style.left = left;
      labels[index].textContent = range
        ? iconColorFormatValue(range.min + stop.position / 1000 * (range.max - range.min), range.max - range.min)
        : '';
    });
    const remove = handles.querySelector('[data-icon-color="stop-remove"]');
    const removable = selected >= 0 && selected < bar.stops.length && bar.stops.length > 2;
    if (remove) {
      remove.classList.toggle('hidden', !removable);
      if (removable) remove.style.left = bar.stops[selected].position / 10 + '%';
    }
  }

  function iconColorRuleRow(tab, index) {
    return iconColorEl(tab, '_tile_icon_rule_' + index);
  }

  function readIconColorRows(tab) {
    const rows = [];
    for (let index = 0; index < ICON_COLOR_MAX_ROWS; index++) {
      const row = iconColorRuleRow(tab, index);
      if (!row || row.classList.contains('hidden')) continue;
      rows.push({
        value: document.getElementById(row.id + '_value')?.value || '',
        has: !!document.getElementById(row.id + '_has')?.checked,
        color: document.getElementById(row.id + '_color')?.value || ICON_COLOR_ROW_DEFAULT
      });
    }
    return rows;
  }

  function writeIconColorRows(tab, rows) {
    for (let index = 0; index < ICON_COLOR_MAX_ROWS; index++) {
      const row = iconColorRuleRow(tab, index);
      if (!row) continue;
      const entry = rows[index];
      row.classList.toggle('hidden', !entry);
      const value = document.getElementById(row.id + '_value');
      const has = document.getElementById(row.id + '_has');
      const color = document.getElementById(row.id + '_color');
      if (value) value.value = entry ? entry.value : '';
      if (has) has.checked = !!entry?.has;
      if (color) color.value = entry ? (normalizeIconColorHex(entry.color) || ICON_COLOR_ROW_DEFAULT) : ICON_COLOR_ROW_DEFAULT;
    }
    iconColorEl(tab, '_tile_icon_rule_add')?.classList.toggle('hidden', rows.length >= ICON_COLOR_MAX_ROWS);
  }

  // Binary sensor On/Off pickers; unset pickers keep the type color.
  function setIconColorBinaryInput(tab, state, color) {
    const input = iconColorEl(tab, '_tile_icon_' + state);
    if (!input) return;
    const hex = normalizeIconColorHex(color);
    input.value = hex || input.dataset.default || ICON_COLOR_BINARY_DEFAULTS[state];
    input.dataset.unset = hex ? '0' : '1';
  }

  function readIconColorBinaryRows(tab) {
    const rows = [];
    for (const state of ['on', 'off']) {
      const input = iconColorEl(tab, '_tile_icon_' + state);
      if (input && input.dataset.unset === '0') rows.push({ value: state, has: false, color: input.value });
    }
    return rows;
  }

  // An unset icon color keeps the type's default (white or state color).
  function setIconColorInput(tab, color) {
    const input = iconColorEl(tab, '_tile_icon_color');
    if (!input) return;
    const hex = normalizeIconColorHex(color);
    input.value = hex || '#FFFFFF';
    input.dataset.unset = hex ? '0' : '1';
  }

  function collectIconColorRecord(tab) {
    const type = iconColorTypeOf(tab);
    const input = iconColorEl(tab, '_tile_icon_color');
    const fixed = input && input.dataset.unset !== '1' ? normalizeIconColorHex(input.value).slice(1) : '';
    const lines = ['v2', fixed];
    const bar = readIconColorBar(tab);
    if (bar.mode !== 'off') {
      // A number with inner spaces is invalid rather than a second token.
      const number = suffix => {
        const text = iconColorTrim(iconColorEl(tab, suffix)?.value ?? '');
        return /^[^ \t\r]+$/.test(text) ? text : '-';
      };
      lines.push(['bar', bar.mode, number('_tile_icon_bar_min'), number('_tile_icon_bar_max')]
        .concat(bar.stops.map(stop => stop.position + ':' + iconColorHex(stop.color))).join(' '));
    }
    const rows = type === '20' ? readIconColorBinaryRows(tab) : readIconColorRows(tab);
    for (const row of rows) {
      const color = normalizeIconColorHex(row.color) || ICON_COLOR_ROW_DEFAULT;
      const value = row.value.replace(/[\u0000-\u001f\u007f]/g, '');
      lines.push((row.has ? 'has ' : 'is ') + color.slice(1) + ' ' + value);
    }
    return normalizeIconColorRecord(lines.join('\n'),
      ICON_COLOR_BAR_TYPES.includes(type), ICON_COLOR_ROW_TYPES.includes(type));
  }

  // Sensor states can be numbers or text: the current state picks the bar or
  // the state list while neither holds colors.
  function iconColorSensorIsText(tab) {
    const entity = iconColorEl(tab, '_sensor_entity')?.value || '';
    const meta = typeof sensorMetaCache === 'object' ? sensorMetaCache : null;
    const raw = String(meta?.values?.[entity] ?? '').trim();
    if (!raw || ['unavailable', 'unknown', 'none', 'null', '--'].includes(raw.toLowerCase())) return false;
    return iconColorLeadingNumber(raw) === null;
  }

  // Suggestions for the state text: raw states the entity is known to take.
  function fillIconColorStates(tab, type) {
    const list = iconColorEl(tab, '_tile_icon_states');
    if (!list) return;
    // No object literal with numeric keys: the delivery formatter would print
    // them as numbers and fail its AST equivalence check.
    const field = type === '1' ? '_sensor_entity' : type === '20' ? '_binary_sensor_entity'
      : type === '22' ? '_select_entity' : type === '23' ? '_datetime_entity' : '';
    const entity = field ? (iconColorEl(tab, field)?.value || '') : '';
    const meta = typeof sensorMetaCache === 'object' ? sensorMetaCache : null;
    let states = [];
    if (type === '20') {
      states = ['on', 'off'];
    } else if (type === '22' || type === '23') {
      let value = meta?.editableValues?.[entity];
      if (typeof value === 'string') { try { value = JSON.parse(value); } catch (_) { value = null; } }
      if (Array.isArray(value?.options)) states = value.options.map(String);
      else if (value?.state !== undefined && value?.state !== null) states = [String(value.state)];
    } else if (entity && meta?.values?.[entity] !== undefined) {
      states = [String(meta.values[entity])];
    }
    states = states.filter(state => state && !['unavailable', 'unknown'].includes(state)).slice(0, 50);
    if (list.dataset.states === states.join('\n')) return;
    list.dataset.states = states.join('\n');
    list.replaceChildren(...states.map(state => {
      const option = document.createElement('option');
      option.value = state;
      return option;
    }));
  }

  function syncIconColorFields(tab) {
    const block = iconColorEl(tab, '_tile_icon_color_fields');
    if (!block) return;
    const type = iconColorTypeOf(tab);
    const visible = tileTypeHasIconColors(type);
    block.classList.toggle('hidden', !visible);
    if (!visible) return;
    const showBinary = type === '20';
    let showBar = ICON_COLOR_BAR_TYPES.includes(type);
    let showRows = ICON_COLOR_ROW_TYPES.includes(type) && !showBinary;
    if (showBar && showRows) {
      const hasBar = readIconColorBar(tab).mode !== 'off';
      const hasRows = readIconColorRows(tab).length > 0;
      if (hasBar || hasRows) {
        showBar = hasBar;
        showRows = hasRows;
      } else {
        const text = iconColorSensorIsText(tab);
        showBar = !text;
        showRows = text;
        if (typeof isSensorMetaCacheLoaded === 'function' && !isSensorMetaCacheLoaded() &&
            typeof fetchSensorMetaCache === 'function' && block.dataset.metaRequested !== '1') {
          block.dataset.metaRequested = '1';
          fetchSensorMetaCache().then(() => syncIconColorFields(tab)).catch(() => {});
        }
      }
    }
    iconColorEl(tab, '_tile_icon_bar_section')?.classList.toggle('hidden', !showBar);
    iconColorEl(tab, '_tile_icon_state_section')?.classList.toggle('hidden', !showRows);
    iconColorEl(tab, '_tile_icon_binary_section')?.classList.toggle('hidden', !showBinary);
    if (showRows) fillIconColorStates(tab, type);
    if (showBar) renderIconColorBar(tab);
  }

  function loadIconColorFields(tab, data) {
    const parsed = parseIconColorRecord(data?.icon_colors);
    setIconColorInput(tab, parsed.color);
    const barInput = iconColorEl(tab, '_tile_icon_bar');
    if (barInput) barInput.dataset.last = '';
    writeIconColorBar(tab, parsed.bar ? parsed.bar.mode : 'off', parsed.bar ? parsed.bar.stops : []);
    const min = iconColorEl(tab, '_tile_icon_bar_min');
    const max = iconColorEl(tab, '_tile_icon_bar_max');
    if (min) min.value = parsed.bar ? parsed.bar.minText : '';
    if (max) max.value = parsed.bar ? parsed.bar.maxText : '';
    setIconColorSelectedStop(tab, -1);
    writeIconColorRows(tab, parsed.rows);
    for (const state of ['on', 'off']) {
      const row = parsed.rows.find(entry => !entry.has && iconColorFold(entry.value) === state);
      setIconColorBinaryInput(tab, state, row ? row.color : '');
    }
    syncIconColorFields(tab);
  }

  function saveIconColorFields(tab, formData) {
    formData.append('icon_colors', collectIconColorRecord(tab));
  }

  function resetIconColorFields(tab) {
    loadIconColorFields(tab, {});
  }

  // Delegated listeners survive folder-tab HTML replacement without stale or
  // duplicate handlers. Every change takes the shared live-editor path:
  // preview, draft snapshot and autosave.
  function commitIconColorChange(tab) {
    syncIconColorFields(tab);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }

  function iconColorEventTab(element) {
    return element?.closest?.('.tile-icon-color-fields')?.dataset.tab || '';
  }

  function iconColorBarPosition(element, clientX) {
    const rect = element.getBoundingClientRect();
    if (!rect.width) return null;
    return Math.max(0, Math.min(1000, Math.round((clientX - rect.left) / rect.width * 1000)));
  }

  function addIconColorStop(tab, position) {
    const bar = readIconColorBar(tab);
    if (bar.mode === 'off' || bar.stops.length >= ICON_COLOR_MAX_STOPS || position === null) return false;
    const stops = iconColorSortStops(bar.stops);
    const stop = { position, color: iconColorBarColorAt({ mode: bar.mode, stops }, position / 1000) };
    stops.push(stop);
    iconColorSortStops(stops);
    writeIconColorBar(tab, bar.mode, stops);
    setIconColorSelectedStop(tab, stops.indexOf(stop));
    return true;
  }

  function openIconColorStopPicker(tab, index) {
    const stop = readIconColorBar(tab).stops[index];
    const picker = iconColorEl(tab, '_tile_icon_stop_color');
    if (!stop || !picker) return;
    picker.value = '#' + iconColorHex(stop.color);
    try {
      if (typeof picker.showPicker === 'function') picker.showPicker();
      else picker.click();
    } catch (_) {
      picker.click();
    }
  }

  document.addEventListener('input', event => {
    const target = event.target;
    const role = target?.dataset?.iconColor;
    if (!['color', 'binary', 'value', 'rule-color', 'min', 'max', 'stop-color'].includes(role)) return;
    const tab = iconColorEventTab(target);
    if (!tab) return;
    if (role === 'color' || role === 'binary') target.dataset.unset = '0';
    if (role === 'stop-color') {
      const bar = readIconColorBar(tab);
      const stop = bar.stops[iconColorSelectedStop(tab)];
      const rgb = iconColorParseHex(target.value);
      if (!stop || rgb === null) return;
      stop.color = rgb;
      writeIconColorBar(tab, bar.mode, bar.stops);
    }
    commitIconColorChange(tab);
  });

  document.addEventListener('change', event => {
    const target = event.target;
    const id = target?.id || '';
    if (target?.dataset?.iconColor === 'has') {
      const tab = iconColorEventTab(target);
      if (tab) commitIconColorChange(tab);
      return;
    }
    for (const suffix of ['_tile_type', '_sensor_entity', '_select_entity', '_datetime_entity']) {
      if (id.endsWith(suffix)) syncIconColorFields(id.slice(0, -suffix.length));
    }
  });

  document.addEventListener('click', event => {
    const button = event.target?.closest?.('button[data-icon-color]');
    const tab = iconColorEventTab(button);
    if (!button || !tab) return;
    const role = button.dataset.iconColor;
    if (role === 'stop') {
      // A finished drag is not a click on the stop.
      if (button.dataset.dragged === '1') {
        delete button.dataset.dragged;
        return;
      }
      const index = Number(button.dataset.stop);
      setIconColorSelectedStop(tab, index);
      renderIconColorBar(tab);
      openIconColorStopPicker(tab, index);
      return;
    }
    if (role === 'clear') {
      setIconColorInput(tab, '');
    } else if (role === 'binary-clear') {
      setIconColorBinaryInput(tab, button.dataset.state, '');
    } else if (role === 'remove') {
      const rows = readIconColorRows(tab);
      rows.splice(Number(button.dataset.rule), 1);
      writeIconColorRows(tab, rows);
    } else if (role === 'add') {
      const rows = readIconColorRows(tab);
      if (rows.length >= ICON_COLOR_MAX_ROWS) return;
      rows.push({ value: '', has: false, color: ICON_COLOR_ROW_DEFAULT });
      writeIconColorRows(tab, rows);
      document.getElementById(tab + '_tile_icon_rule_' + (rows.length - 1) + '_value')?.focus();
    } else if (role === 'mode') {
      const mode = button.dataset.mode;
      const input = iconColorEl(tab, '_tile_icon_bar');
      const bar = readIconColorBar(tab);
      if (mode === 'off') {
        if (bar.mode !== 'off' && input) input.dataset.last = input.value;
        writeIconColorBar(tab, 'off', []);
      } else if (bar.mode === 'off') {
        // Turning the bar on restores the last stops, else Cold -> Warm.
        const last = String(input?.dataset.last || '').split(' ').slice(1).map(iconColorParseStop).filter(Boolean);
        const stops = last.length >= 2 ? last
          : ICON_COLOR_PRESETS.cold_warm.map(([position, color]) => ({ position, color }));
        writeIconColorBar(tab, mode, stops);
        ensureIconColorRange(tab);
      } else {
        writeIconColorBar(tab, mode, bar.stops);
      }
      setIconColorSelectedStop(tab, -1);
    } else if (role === 'preset') {
      const preset = ICON_COLOR_PRESETS[button.dataset.preset];
      if (!preset) return;
      const bar = readIconColorBar(tab);
      writeIconColorBar(tab, bar.mode === 'off' ? 'smooth' : bar.mode,
        preset.map(([position, color]) => ({ position, color })));
      ensureIconColorRange(tab);
      setIconColorSelectedStop(tab, -1);
    } else if (role === 'stop-remove') {
      const bar = readIconColorBar(tab);
      const selected = iconColorSelectedStop(tab);
      if (bar.stops.length <= 2 || !bar.stops[selected]) return;
      bar.stops.splice(selected, 1);
      writeIconColorBar(tab, bar.mode, bar.stops);
      setIconColorSelectedStop(tab, -1);
    } else {
      return;
    }
    commitIconColorChange(tab);
  });

  // Double-click on the bar adds a stop with the color already shown there.
  document.addEventListener('dblclick', event => {
    const strip = event.target?.closest?.('[data-icon-color="bar"]');
    const tab = iconColorEventTab(strip);
    if (!strip || !tab) return;
    if (addIconColorStop(tab, iconColorBarPosition(strip, event.clientX))) commitIconColorChange(tab);
  });

  // Dragging a stop handle (mouse, pen or touch) moves it along the bar; the
  // preview follows live, the draft and autosave follow on release.
  let iconColorDrag = null;

  document.addEventListener('pointerdown', event => {
    const knob = event.target?.closest?.('[data-icon-color="stop"]');
    const tab = iconColorEventTab(knob);
    if (!knob || !tab || (event.button !== undefined && event.button !== 0)) return;
    delete knob.dataset.dragged;
    const index = Number(knob.dataset.stop);
    setIconColorSelectedStop(tab, index);
    renderIconColorBar(tab);
    const start = readIconColorBar(tab).stops[index];
    iconColorDrag = { tab, index, knob, pointerId: event.pointerId, startX: event.clientX,
      startPosition: start ? start.position : 0, moved: false };
    try { knob.setPointerCapture?.(event.pointerId); } catch (_) {}
    event.preventDefault();
  });

  document.addEventListener('pointermove', event => {
    const drag = iconColorDrag;
    if (!drag || event.pointerId !== drag.pointerId) return;
    if (!drag.moved && Math.abs(event.clientX - drag.startX) < 3) return;
    drag.moved = true;
    const handles = iconColorEl(drag.tab, '_tile_icon_bar_handles');
    const position = handles ? iconColorBarPosition(handles, event.clientX) : null;
    const bar = readIconColorBar(drag.tab);
    if (position === null || !bar.stops[drag.index]) return;
    bar.stops[drag.index].position = position;
    writeIconColorBar(drag.tab, bar.mode, bar.stops);
    renderIconColorBar(drag.tab);
    updateTilePreview(drag.tab);
  });

  function endIconColorDrag(event) {
    const drag = iconColorDrag;
    if (!drag || event.pointerId !== drag.pointerId) return;
    iconColorDrag = null;
    if (!drag.moved) return;
    drag.knob.dataset.dragged = '1';
    const bar = readIconColorBar(drag.tab);
    const moved = bar.stops[drag.index];
    if (!moved) return;
    // Re-sort on release; a stop dragged onto a neighbour's position lands on
    // the side it came from, so dragging past a neighbour puts it behind it.
    const stops = iconColorSortStops(bar.stops.filter(stop => stop !== moved));
    const right = moved.position >= drag.startPosition;
    let at = stops.findIndex(stop => right ? stop.position > moved.position : stop.position >= moved.position);
    if (at < 0) at = stops.length;
    stops.splice(at, 0, moved);
    writeIconColorBar(drag.tab, bar.mode, stops);
    setIconColorSelectedStop(drag.tab, at);
    commitIconColorChange(drag.tab);
  }

  document.addEventListener('pointerup', endIconColorDrag);
  document.addEventListener('pointercancel', endIconColorDrag);

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
  // Rules (every tile type, "src ..." in the record): the tile's own entity
  // or another one, entity color or own rules, coloring the icon and/or
  // tinting the tile (tile_icon_source.cpp). Scene, Folder, Back, Camera,
  // Clock, Text and Timer have no entity of their own and use another entity only
  // (tileTypeHasFixedIconColorOnly / tileTypeRulesUseOwnEntity in
  // tile_type_policy.h).
  const ICON_COLOR_FIXED_TYPES = ['2', '4', '8', '9', '10', '18', '30'];
  const ICON_COLOR_OWN_TYPES = ['1', '5', '12', '14', '15', '17', '19', '20', '21', '22', '23', '24', '25', '26', '29'];
  // The own entity field of each type (pairs, not an object with numeric keys).
  const ICON_COLOR_ENTITY_FIELDS = [['1', '_sensor_entity'], ['5', '_switch_entity'], ['12', '_weather_entity'],
    ['14', '_energy_entity'], ['15', '_media_entity'], ['17', '_climate_entity'], ['19', '_cover_entity'],
    ['20', '_binary_sensor_entity'], ['21', '_number_entity'], ['22', '_select_entity'], ['23', '_datetime_entity'],
    ['24', '_lock_entity'], ['25', '_alarm_entity'], ['26', '_fan_entity'], ['29', '_vacuum_entity']];
  // Domains shown by the Switch tile (tile_icon_source.cpp switch_domain).
  const ICON_COLOR_SWITCH_DOMAINS = ['light', 'switch', 'input_boolean', 'automation', 'fan',
    'humidifier', 'remote', 'siren'];
  const ICON_COLOR_TYPES = ICON_COLOR_OWN_TYPES.concat(ICON_COLOR_FIXED_TYPES);
  const ICON_COLOR_BAR_TYPES = ['1', '14', '21'];
  const ICON_COLOR_ROW_TYPES = ['1', '20', '22', '23'];
  // Media: icon color and tile color "From cover" (the "cover" line).
  const ICON_COLOR_MEDIA_TYPE = '15';
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

  function tileTypeHasFixedIconColorOnly(typeValue) {
    return ICON_COLOR_FIXED_TYPES.includes(String(typeValue ?? '0'));
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

  // valid_icon(): a rule's own icon (user 2026-10-28) is an MDI name of
  // lowercase letters, digits and dashes, at most 48 characters.
  function iconColorValidIcon(name) {
    return /^[a-z0-9-]{1,48}$/.test(String(name ?? ''));
  }

  // parse_color_icon(): "RRGGBB" or "RRGGBB,<icon>".
  function iconColorParseHexIcon(text) {
    const value = String(text);
    const comma = value.indexOf(',');
    const icon = comma < 0 ? '' : value.slice(comma + 1);
    if (comma >= 0 && !iconColorValidIcon(icon)) return null;
    const color = iconColorParseHex(comma < 0 ? value : value.slice(0, comma));
    return color === null ? null : { color, icon };
  }

  // append_icon(): ",<icon>" after a color, nothing without one.
  function iconColorIconSuffix(icon) {
    return iconColorValidIcon(icon) ? ',' + icon : '';
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

  // parse_rule(): "<op>[+|-] RRGGBB[,<icon>] <value>"; `fill` is the state
  // line's own "Fuellen" (1 "is+", 0 "is-", -1 a plain "is" following the
  // "fill" line), `icon` its own icon ('' without one).
  function iconColorParseRule(line) {
    const text = iconColorTrim(line);
    const space = text.indexOf(' ');
    let op = space < 0 ? '' : text.slice(0, space);
    let fill = -1;
    if (/^(is|has)[+-]$/.test(op)) {
      fill = op.endsWith('+') ? 1 : 0;
      op = op.slice(0, -1);
    }
    if (!['ge', 'le', 'eq', 'is', 'has'].includes(op)) return null;
    const rest = text.slice(space + 1);
    const colorEnd = rest.indexOf(' ');
    const parsed = iconColorParseHexIcon(colorEnd < 0 ? rest : rest.slice(0, colorEnd));
    if (!parsed) return null;
    const value = colorEnd < 0 ? '' : iconColorTrim(rest.slice(colorEnd));
    return value ? { op, color: parsed.color, icon: parsed.icon, value, fill } : null;
  }

  // tile_icon_colors::rows_fill(): a state line with its own "Fuellen".
  function iconColorRowsFill(record) {
    const text = String(record ?? '');
    if (!iconColorIsV2(text)) return false;
    return text.split('\n').slice(2).some(line => iconColorParseRule(line)?.fill === 1);
  }

  function iconColorSortStops(stops) {
    return stops.sort((a, b) => a.position - b.position);
  }

  // parse_stop(): "P:RRGGBB[,<icon>]", P with 1-4 digits, clamped to 0..1000.
  function iconColorParseStop(token) {
    const colon = token.indexOf(':');
    if (colon < 1 || colon > 4 || !/^[0-9]+$/.test(token.slice(0, colon))) return null;
    const parsed = iconColorParseHexIcon(token.slice(colon + 1));
    if (!parsed) return null;
    return { position: Math.min(Number(token.slice(0, colon)), 1000), color: parsed.color, icon: parsed.icon };
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

  // valid_entity(): "<domain>.<object>" in lowercase letters, digits and
  // underscores, at most 128 characters.
  function iconColorValidEntity(entity) {
    const text = String(entity ?? '');
    return text.length <= 128 && /^[a-z0-9_]+\.[a-z0-9_]+$/.test(text);
  }

  // parse_source_line(): "src <auto|rules> <self|entity_id> [tile=NN]
  // [noicon] [off]", options in any order.
  function iconColorParseSource(line) {
    const tokens = String(line).split(/[ \t\r]+/).filter(Boolean);
    if (tokens.length < 3 || tokens[0] !== 'src' || !['auto', 'rules'].includes(tokens[1])) return null;
    const layer = { mode: tokens[1], self: tokens[2] === 'self', entity: '', tile: 0, icon: true, enabled: true };
    if (!layer.self) {
      if (!iconColorValidEntity(tokens[2])) return null;
      layer.entity = tokens[2];
    }
    for (const token of tokens.slice(3)) {
      if (token === 'noicon') layer.icon = false;
      else if (token === 'off') layer.enabled = false;
      else if (/^tile=[0-9]{1,2}$/.test(token)) layer.tile = Math.min(50, Math.max(10, Number(token.slice(5))));
      else return null;
    }
    return layer;
  }

  // tile_icon_colors::rule_colors_icon(): a rule colors the icon, a former
  // "Tint tile" rule too (user 2026-10-11: rules only color the icon).
  function iconColorRuleColorsIcon(layer) {
    return !!layer && (!!layer.icon || Number(layer.tile || 0) > 0);
  }

  // own_state_colors_icon(): no rule layer (b40 records) or an enabled
  // "src rules self" that colors the icon.
  function iconColorOwnStateColorsIcon(record) {
    const layer = iconColorRecordSource(record);
    return !layer || (layer.mode === 'rules' && layer.self && layer.enabled && layer.icon);
  }

  // source_of(): the first "src" line of a v2 record, or null.
  function iconColorRecordSource(record) {
    const text = String(record ?? '');
    if (!iconColorIsV2(text)) return null;
    const line = text.split('\n').slice(2).find(candidate => candidate.startsWith('src '));
    return line === undefined ? null : iconColorParseSource(line);
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

  // bar_icon(): the icon of the last stop at or below the value (the first
  // stop below it), on smooth bars too: an icon cannot blend.
  function iconColorBarIcon(bar, value) {
    let t = (value - bar.min) / (bar.max - bar.min);
    if (!(t > 0)) t = 0;
    if (t > 1) t = 1;
    let index = 0;
    bar.stops.forEach((stop, i) => { if (stop.position / 1000 <= t) index = i; });
    return bar.stops[index]?.icon || '';
  }

  // resolve(): the icon color for a known state as "#RRGGBB", or '' for the
  // type default. Only v2 records are evaluated.
  // `out`, when given, receives the matching state line's own fill (`fill`:
  // 1, 0 or -1 like iconColorParseRule) and the matching stop's or line's
  // own icon (`icon`, '' without one).
  function resolveIconColorRecord(record, state, display, fixedFallback = true, out = null) {
    if (out) {
      out.fill = -1;
      out.icon = '';
    }
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
        if (bar) {
          if (out) out.icon = iconColorBarIcon(bar, number);
          return '#' + iconColorHex(iconColorBarColor(bar, number));
        }
        continue;
      }
      const rule = iconColorParseRule(line);
      if (!rule || (rule.op !== 'is' && rule.op !== 'has')) continue;
      if (iconColorTextMatches(rule.op, rule.value, state) ||
          (display !== undefined && display !== null && iconColorTextMatches(rule.op, rule.value, display))) {
        if (out) {
          out.fill = rule.fill;
          out.icon = rule.icon;
        }
        return '#' + iconColorHex(rule.color);
      }
    }
    if (!fixedFallback) return '';
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

  // has_rule_lines(): a v2 record with a color bar or a state line.
  function iconColorHasRuleLines(text) {
    if (!iconColorIsV2(text)) return false;
    return text.split('\n').slice(2).some(line => line.startsWith('bar ') ||
      ['is', 'has'].includes(iconColorParseRule(line)?.op));
  }

  // normalize(): any record (editor, import, b39) in the canonical v2 form;
  // numeric types keep only the bar, text types only the state lines.
  function normalizeIconColorRecord(record, allowBar, allowRows, allowSource = false, allowSelf = false,
    allowCover = false) {
    const text = String(record ?? '');
    const v2 = iconColorIsV2(text);
    // Only the Sensor family and the Binary sensor evaluate rules on their
    // own state themselves; every other type with an entity of its own keeps
    // its "src rules self" line (records before b575 lost it, issue #102).
    const ownRules = allowBar || allowRows;
    let source = allowSource ? iconColorRecordSource(text) : null;
    if (source?.self && !allowSelf) source = null;
    if (!source && v2 && allowSource && allowSelf && !ownRules && iconColorHasRuleLines(text)) {
      source = { mode: 'rules', self: true, entity: '', tile: 0, icon: true, enabled: true };
    }
    if (source?.mode === 'rules') {
      if (!source.self || (!allowBar && !allowRows)) {
        allowBar = true;
        allowRows = true;
      }
    } else if (source?.mode === 'auto') {
      allowBar = false;
      allowRows = false;
    }
    // "src rules self" coloring the icon only is implicit for the types that
    // evaluate their own state (b40 records).
    const emitLayer = !!source &&
      !(ownRules && source.mode === 'rules' && source.self && source.icon && !source.tile && source.enabled);
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
    const fill = v2 ? iconColorFillOf(body) : 0;
    if (fill) out += '\nfill ' + fill;
    const cover = allowCover && v2 ? iconColorCoverOf(body) : { icon: false, tile: 0 };
    const hasCover = cover.icon || cover.tile > 0;
    if (hasCover) out += '\ncover' + (cover.icon ? ' icon' : '') + (cover.tile ? ' tile=' + cover.tile : '');
    if (emitLayer) {
      out += '\nsrc ' + source.mode + ' ' + (source.self ? 'self' : source.entity) +
        (source.tile ? ' tile=' + source.tile : '') + (source.icon ? '' : ' noicon') + (source.enabled ? '' : ' off');
    }
    if (bar) {
      out += '\nbar ' + bar.mode + ' ' + bar.minText + ' ' + bar.maxText +
        bar.stops.map(stop => ' ' + stop.position + ':' + iconColorHex(stop.color) + iconColorIconSuffix(stop.icon)).join('');
    }
    let rows = 0;
    if (allowRows) {
      let legacyRules = 0;
      for (const line of body) {
        if (rows >= ICON_COLOR_MAX_ROWS) break;
        if (v2 && (line.startsWith('bar ') || line.startsWith('src ') || line.startsWith('cover'))) continue;
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
        const mark = rule.fill === 1 ? '+' : rule.fill === 0 ? '-' : '';
        out += '\n' + rule.op + mark + ' ' + iconColorHex(rule.color) + iconColorIconSuffix(rule.icon) + ' ' + value;
        rows++;
      }
    }
    return fixed === null && !fill && !hasCover && !emitLayer && !bar && rows === 0 ? '' : out;
  }

  // tile_icon_colors::fill_of(): the "fill NN" tint of the fixed color in
  // percent (clamped like the rule tint), 0 without one.
  function iconColorFillOf(lines) {
    const line = lines.find(candidate => candidate.startsWith('fill '));
    if (line === undefined) return 0;
    const text = iconColorTrim(line.slice(5));
    if (!/^[0-9]+$/.test(text) || text.length > 4) return 0;
    return Math.min(50, Math.max(10, Number(text)));
  }

  // tile_icon_colors::cover_of(): "From cover" of a Media tile, the first
  // "cover [icon] [tile=NN]" line; an unknown token makes it invalid.
  function iconColorCoverOf(lines) {
    const none = { icon: false, tile: 0 };
    const line = lines.find(candidate => candidate.startsWith('cover'));
    if (line === undefined) return none;
    const tokens = line.split(/[ \t\r]+/).filter(Boolean);
    if (tokens[0] !== 'cover') return none;
    const out = { icon: false, tile: 0 };
    for (const token of tokens.slice(1)) {
      if (token === 'icon') out.icon = true;
      else if (/^tile=[0-9]{1,2}$/.test(token)) out.tile = Math.min(50, Math.max(10, Number(token.slice(5))));
      else return none;
    }
    return out;
  }

  // Editor view of a record: fixed color, bar and state rows.
  function parseIconColorRecord(record) {
    const lines = normalizeIconColorRecord(record, true, true, true, true, true).split('\n');
    const fixed = iconColorParseHex(lines[1] ?? '');
    let bar = null;
    const rows = [];
    for (const line of lines.slice(2)) {
      if (line.startsWith('bar ')) { bar = iconColorParseBar(line); continue; }
      const rule = iconColorParseRule(line);
      if (rule) {
        rows.push({ has: rule.op === 'has', color: '#' + iconColorHex(rule.color), value: rule.value, fill: rule.fill,
          icon: rule.icon });
      }
    }
    return { color: fixed === null ? '' : '#' + iconColorHex(fixed), bar, rows,
      fill: iconColorFillOf(lines.slice(2)), cover: iconColorCoverOf(lines.slice(2)),
      source: iconColorRecordSource(lines.join('\n')) };
  }

  // ---- Source entity (icon-and-title tiles), mirrors tile_icon_source.cpp ----

  // The raw state of a payload: the JSON "state" field, else the plain text.
  function iconColorPayloadState(payload) {
    const text = String(payload ?? '').trim();
    if (!text.startsWith('{')) return text;
    try {
      const value = JSON.parse(text);
      return value && value.state !== undefined && value.state !== null ? String(value.state).trim() : '';
    } catch (_) {
      return '';
    }
  }

  function iconColorStateKnown(state) {
    const text = String(state ?? '').trim().toLowerCase();
    return !!text && !['unavailable', 'unknown', 'none', 'null'].includes(text);
  }

  // "auto": the entity's own icon color as its tile preview shows it.
  function iconColorSourceAutoColor(entity, payload) {
    const domain = String(entity).split('.')[0];
    const text = String(payload ?? '').trim();
    if (!text) return '';
    if (ICON_COLOR_SWITCH_DOMAINS.includes(domain) && typeof parseSwitchPayload === 'function') {
      const state = parseSwitchPayload(text);
      if (!state || state.available === false || !state.hasState) return '';
      const onColor = state.hasColor ? state.color : '#FFD54F';
      return state.isOn ? (typeof toneLightShown === 'function' ? toneLightShown(onColor) : onColor)
        : panelRoleHex('icon-off', '#B0B0B0');
    }
    if (domain === 'binary_sensor' && typeof parseBinarySensorPreviewPayload === 'function') {
      const state = parseBinarySensorPreviewPayload(text);
      if (!state?.valid || state.available !== true || !['on', 'off'].includes(state.state)) return '';
      return binarySensorPreviewColor(state);
    }
    if (domain === 'climate' && typeof parseClimatePreviewPayload === 'function') {
      const state = parseClimatePreviewPayload(text);
      return state && state.available !== false ? climatePreviewColor(state) : '';
    }
    if (domain === 'cover' && typeof parseCoverPreviewPayload === 'function') {
      const state = parseCoverPreviewPayload(text);
      return state && state.available !== false ? coverPreviewColor(state) : '';
    }
    return '';
  }

  // tile_icon_source::rule_color(): the rule color of a layer from the preview
  // states of its entity (own or other); '' without a known state or result.
  // Rules never fall back to the fixed color here.
  function iconColorLayerColor(record, layer, ownEntity, meta, typeValue, out = null) {
    if (out) {
      out.fill = -1;
      out.icon = '';
    }
    if (!layer || !layer.enabled) return '';
    const entity = layer.self ? String(ownEntity || '') : layer.entity;
    if (!entity) return '';
    if (layer.self && ['21', '22', '23'].includes(String(typeValue))) {
      if (layer.mode === 'auto' || typeof iconColorRuleState !== 'function') return '';
      const rule = iconColorRuleState(typeValue, entity, meta, null);
      return rule ? resolveIconColorRecord(record, rule.state, rule.display, false, out) : '';
    }
    const payload = meta?.values?.[entity];
    if (payload === undefined || payload === null || !String(payload).trim()) return '';
    if (layer.mode === 'auto') return normalizeIconColorHex(iconColorSourceAutoColor(entity, payload));
    const state = iconColorPayloadState(payload);
    if (!iconColorStateKnown(state)) return '';
    let display = null;
    if (entity.startsWith('binary_sensor.') && typeof parseBinarySensorPreviewPayload === 'function' &&
        typeof binarySensorPreviewStateText === 'function') {
      const parsed = parseBinarySensorPreviewPayload(String(payload));
      if (parsed?.valid && ['on', 'off'].includes(parsed.state)) display = binarySensorPreviewStateText(parsed);
    }
    return resolveIconColorRecord(record, state, display, false, out);
  }

  // The icon color of a tile with rules on another entity, else the fixed
  // color ('' = white).
  function iconColorSourcePreview(record, meta) {
    const layer = iconColorRecordSource(record);
    const color = layer && !layer.self && iconColorRuleColorsIcon(layer) ? iconColorLayerColor(record, layer, '', meta, '') : '';
    return color || resolveIconColorRecord(record, '', null);
  }

  // tile_tint::has_hue(): white, grey and black never tint a tile.
  function tileTintHasHue(color) {
    const hex = normalizeIconColorHex(color);
    return !!hex && !(hex.slice(1, 3) === hex.slice(3, 5) && hex.slice(3, 5) === hex.slice(5, 7));
  }

  // tile_tint::choose(), the one tint rule of device and preview: with Tile
  // color "From icon" the tile follows the real color the icon shows (rules
  // included), else an applying rule "Tint tile" with a real color tints it;
  // null without a tint.
  function tileTintChoice(ruleActive, ruleColor, rulePercent, fillPercent, iconColor) {
    if (fillPercent) {
      return tileTintHasHue(iconColor) ? { color: normalizeIconColorHex(iconColor), percent: fillPercent } : null;
    }
    if (ruleActive && rulePercent && tileTintHasHue(ruleColor)) {
      return { color: normalizeIconColorHex(ruleColor), percent: rulePercent };
    }
    return null;
  }

  // tile_tint::background(): the base mixed with the color, darkened in 5 %
  // steps until white text keeps a contrast of at least 4.5:1.
  function tileTintBackground(base, color, percent) {
    const parse = value => {
      const hex = normalizeIconColorHex(value);
      return hex ? [1, 3, 5].map(i => parseInt(hex.slice(i, i + 2), 16)) : [42, 42, 42];
    };
    const channel = v => { const c = v / 255; return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
    const contrast = rgb => 1.05 / (0.2126 * channel(rgb[0]) + 0.7152 * channel(rgb[1]) + 0.0722 * channel(rgb[2]) + 0.05);
    const from = parse(base);
    const to = parse(color);
    const mix = (under, p) => under.map((v, i) => Math.floor((v * (100 - p) + to[i] * p + 50) / 100));
    const hex = rgb => '#' + rgb.map(v => v.toString(16).toUpperCase().padStart(2, '0')).join('');
    if (typeof panelLight === 'function' && panelLight()) {
      // The light theme: the tint on the light card at --pv-light-tint of its
      // strength, never darkened (its text is dark); a default grey base is
      // the light card.
      const greyDefault = ['#1A1A1A', '#2A2A2A', '#222222'].includes(hex(from));
      const scale = Number(panelVar('--pv-light-tint', '0.8'));
      const p = Math.floor(percent * (Number.isFinite(scale) ? scale : 0.8) + 1e-6);
      return hex(mix(greyDefault ? (panelRole('card') || [255, 255, 255]) : from, p));
    }
    let out = mix(from, percent);
    for (let i = 0; i < 40 && contrast(out) < 4.5; i++) out = out.map(v => Math.floor((v * 95 + 50) / 100));
    return hex(out);
  }
  // Tints a preview card from the global tile color. A screensaver card keeps
  // its own opacity (data-bg-opacity): the panel sets bg_opa after the tint
  // (image_screensaver build_slot_tile); the opaque tint hid the wallpaper
  // in the preview (user 2026-10-02).
  // `idle`: the idle grey of a tile without a color (kNeutralIdle).
  function setTileTintBackground(el, color, percent, idle = false) {
    const given = [1, 3, 5].map(i => parseInt(String(color).slice(i, i + 2), 16));
    // A tile filled while tinted (light theme) enters that surface first
    // (tile_icon_source::change_surface in set_tile_tint).
    if (el?.dataset?.followTint === '1' && el.dataset.surface !== 'jewel' && typeof applyTileSurface === 'function') {
      applyTileSurface(el, 'jewel');
    }
    // A tile on its own surface tints with that surface's rules, from that
    // theme's card (set_tile_tint).
    const surface = el?.dataset?.surface || '';
    const theme = typeof surfaceTheme === 'function' ? surfaceTheme(surface) : -1;
    const pageLight = typeof panelThemeLight === 'function' ? panelThemeLight() : false;
    let base = String(getComputedStyle(document.documentElement).getPropertyValue('--tile-card') || '').trim();
    if (theme === 0 && pageLight) base = String(getComputedStyle(document.documentElement).getPropertyValue('--tile-default-bg') || '').trim();
    if (theme === 1 && !pageLight) base = String(getComputedStyle(document.documentElement).getPropertyValue('--pvl-card') || '').trim();
    let hex = typeof withSurface === 'function'
      ? withSurface(theme, () => tileTintBackground(base || '#1A1A1A', color, percent))
      : tileTintBackground(base || '#1A1A1A', color, percent);
    if (surface === 'jewel' && typeof toneJewelCard === 'function') {
      // Filled (tone_color::jewel_card): from the icon's own color, white
      // light in the neutral grey; its strength for the circle.
      const card = idle ? toneJewelIdleCard() : toneJewelCard(given, Number(percent) || 20);
      hex = toneHex(card).toUpperCase();
      el.dataset.jewelStrength = String(Number(percent) || 20);
    }
    const opacity = el.dataset.bgOpacity;
    el.style.background = opacity === undefined ? hex : hex + Number(opacity).toString(16).padStart(2, '0');
    // Cut-outs (the Media play glyph) take the new card color.
    el.style.setProperty?.('--tile-cut', hex);
  }

  // Entities offered as a source: the states the Bridge publishes to tiles.
  function iconColorSourceEntries(data) {
    const seen = new Set();
    const entries = [];
    for (const list of [data?.sensors, data?.binary_sensors, data?.switches, data?.climates, data?.covers]) {
      for (const entry of Array.isArray(list) ? list : []) {
        const value = String(entry?.v ?? '');
        if (!iconColorValidEntity(value) || seen.has(value)) continue;
        seen.add(value);
        entries.push(entry);
      }
    }
    return entries;
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
      : [mode].concat(stops.map(stop => stop.position + ':' + iconColorHex(stop.color) + iconColorIconSuffix(stop.icon)))
        .join(' ');
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
    let icons = Array.from(handles.querySelectorAll('.icon-color-stop-icon'));
    if (knobs.length !== bar.stops.length || icons.length !== bar.stops.length) {
      knobs.concat(labels, icons).forEach(el => el.remove());
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
      // Each stop's own icon below its value: the tile head's icon picker
      // for the selected stop (tile-head.js data-icon-input).
      icons = bar.stops.map((stop, index) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'icon-color-stop-icon';
        button.dataset.iconColor = 'stop-icon-button';
        button.dataset.stop = String(index);
        button.dataset.iconInput = tab + '_tile_icon_stop_icon';
        button.setAttribute('aria-haspopup', 'listbox');
        button.title = typeof t === 'function' ? t('iconPickerAuto') : '';
        handles.appendChild(button);
        return button;
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
      icons[index].style.left = left;
      renderIconColorIconButton(tab, icons[index], stop.icon || '', '#' + iconColorHex(stop.color));
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

  // A rule's icon button: its own icon in the rule's color, else the tile's
  // own icon dimmed (data-icon-value: the icon the picker marks).
  function renderIconColorIconButton(tab, button, icon, color) {
    if (!button) return;
    const own = iconColorValidIcon(icon) ? icon : '';
    const shown = own || (typeof tileAutomaticIcon === 'function' ? tileAutomaticIcon(tab) : 'shape-outline');
    const html = '<i class="mdi mdi-' + escapeHtml(shown) + '"></i>';
    if (button.innerHTML !== html) button.innerHTML = html;
    button.classList.toggle('none', !own);
    button.style.color = own ? (normalizeIconColorHex(color) || '') : '';
    button.dataset.iconValue = own;
  }

  function renderIconColorIconButtons(tab) {
    for (let index = 0; index < ICON_COLOR_MAX_ROWS; index++) {
      const row = iconColorRuleRow(tab, index);
      if (!row || row.classList.contains('hidden')) continue;
      renderIconColorIconButton(tab, document.getElementById(row.id + '_icon_button'),
        document.getElementById(row.id + '_icon')?.value || '', document.getElementById(row.id + '_color')?.value || '');
    }
    for (const state of ['on', 'off']) {
      const input = iconColorEl(tab, '_tile_icon_' + state);
      renderIconColorIconButton(tab, iconColorEl(tab, '_tile_icon_' + state + '_icon_button'),
        iconColorEl(tab, '_tile_icon_' + state + '_icon')?.value || '', input?.value || '');
    }
  }

  // A row's "Fuellen" box: its own mark, else (a new row, or a line from
  // before b382 that follows the "fill" line) the tile's "Ausfuellen". The
  // box keeps the mark in data-mark ('+', '-', '' = follows "Ausfuellen"):
  // only a click gives a row its own mark, so a loaded record stays as it is.
  function iconColorRowFills(tab, fill) {
    return fill === 1 || (fill !== 0 && !!iconColorEl(tab, '_tile_icon_fill')?.checked);
  }
  function iconColorMarkOf(fill) { return fill === 1 ? '+' : fill === 0 ? '-' : ''; }
  function iconColorFillOfBox(box) { return box?.dataset.mark === '+' ? 1 : box?.dataset.mark === '-' ? 0 : -1; }
  function setIconColorFillBox(tab, box, fill) {
    if (!box) return;
    box.dataset.mark = iconColorMarkOf(fill);
    box.checked = iconColorRowFills(tab, fill);
  }

  function readIconColorRows(tab) {
    const rows = [];
    for (let index = 0; index < ICON_COLOR_MAX_ROWS; index++) {
      const row = iconColorRuleRow(tab, index);
      if (!row || row.classList.contains('hidden')) continue;
      rows.push({
        value: document.getElementById(row.id + '_value')?.value || '',
        has: !!document.getElementById(row.id + '_has')?.checked,
        color: document.getElementById(row.id + '_color')?.value || ICON_COLOR_ROW_DEFAULT,
        fill: iconColorFillOfBox(document.getElementById(row.id + '_fill')),
        icon: document.getElementById(row.id + '_icon')?.value || ''
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
      const fill = document.getElementById(row.id + '_fill');
      const icon = document.getElementById(row.id + '_icon');
      if (icon) icon.value = entry && iconColorValidIcon(entry.icon) ? entry.icon : '';
      if (value) value.value = entry ? entry.value : '';
      if (has) has.checked = !!entry?.has;
      setIconColorFillBox(tab, fill, entry ? (entry.fill ?? -1) : -1);
      if (color) color.value = entry ? (normalizeIconColorHex(entry.color) || ICON_COLOR_ROW_DEFAULT) : ICON_COLOR_ROW_DEFAULT;
    }
    iconColorEl(tab, '_tile_icon_rule_add')?.classList.toggle('hidden', rows.length >= ICON_COLOR_MAX_ROWS);
    renderIconColorIconButtons(tab);
  }

  // Binary sensor On/Off pickers; unset pickers keep the type color and
  // follow "Ausfuellen" (their "Fuellen" box shows it).
  function setIconColorBinaryInput(tab, state, color, fill = -1, icon = '') {
    const input = iconColorEl(tab, '_tile_icon_' + state);
    if (!input) return;
    const hex = normalizeIconColorHex(color);
    input.value = hex || input.dataset.default || ICON_COLOR_BINARY_DEFAULTS[state];
    input.dataset.unset = hex ? '0' : '1';
    setIconColorFillBox(tab, iconColorEl(tab, '_tile_icon_' + state + '_fill'), hex ? fill : -1);
    const iconInput = iconColorEl(tab, '_tile_icon_' + state + '_icon');
    if (iconInput) iconInput.value = hex && iconColorValidIcon(icon) ? icon : '';
  }

  function readIconColorBinaryRows(tab) {
    const rows = [];
    for (const state of ['on', 'off']) {
      const input = iconColorEl(tab, '_tile_icon_' + state);
      if (input && input.dataset.unset === '0') {
        rows.push({ value: state, has: false, color: input.value,
          fill: iconColorFillOfBox(iconColorEl(tab, '_tile_icon_' + state + '_fill')),
          icon: iconColorEl(tab, '_tile_icon_' + state + '_icon')?.value || '' });
      }
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

  function iconColorOwnEntity(tab, type) {
    const field = ICON_COLOR_ENTITY_FIELDS.find(pair => pair[0] === type);
    return field ? String(iconColorEl(tab, field[1])?.value || '') : '';
  }

  // The rule layer in the editor, or null when there is nothing to keep
  // (another entity without a choice, or switched off at the defaults).
  function readIconColorSource(tab) {
    const type = iconColorTypeOf(tab);
    const own = ICON_COLOR_OWN_TYPES.includes(type) && iconColorEl(tab, '_tile_icon_source_kind')?.value !== 'other';
    const entity = String(iconColorEl(tab, '_tile_icon_source')?.value || '').trim();
    if (!own && !iconColorValidEntity(entity)) return null;
    return {
      mode: iconColorEl(tab, '_tile_icon_source_mode')?.value === 'auto' ? 'auto' : 'rules',
      self: own,
      entity: own ? '' : entity,
      tile: 0,
      icon: true,
      enabled: iconColorEl(tab, '_tile_icon_rules_on')?.value === '1'
    };
  }

  function writeIconColorSource(tab, layer) {
    const select = iconColorEl(tab, '_tile_icon_source');
    const entity = layer && !layer.self ? layer.entity : '';
    if (select) select.value = entity;
    const set = (suffix, value) => { const el = iconColorEl(tab, suffix); if (el) el.value = value; };
    // Without a stored layer the rules start at Own entity and Own rules, also
    // when the cell is still Empty and only gets its type afterwards; types
    // without an entity of their own show Other entity regardless.
    set('_tile_icon_source_kind', layer && !layer.self ? 'other' : 'self');
    set('_tile_icon_source_mode', layer?.mode === 'auto' ? 'auto' : 'rules');
  }

  function collectIconColorRecord(tab) {
    const type = iconColorTypeOf(tab);
    const input = iconColorEl(tab, '_tile_icon_color');
    // Media "From cover": the icon takes the cover color instead of its own.
    const media = type === ICON_COLOR_MEDIA_TYPE;
    const coverIcon = media && !!iconColorEl(tab, '_tile_icon_cover')?.checked;
    const fixed = input && input.dataset.unset !== '1' && !coverIcon ? normalizeIconColorHex(input.value).slice(1) : '';
    const lines = ['v2', fixed];
    // "Ausfuellen": the fixed strength (tile_icon_colors::kTintDefault).
    if (iconColorEl(tab, '_tile_icon_fill')?.checked) lines.push('fill 20');
    if (coverIcon) lines.push('cover icon');
    const layer = ICON_COLOR_TYPES.includes(type) ? readIconColorSource(tab) : null;
    const bar = readIconColorBar(tab);
    if (bar.mode !== 'off') {
      // A number with inner spaces is invalid rather than a second token.
      const number = suffix => {
        const text = iconColorTrim(iconColorEl(tab, suffix)?.value ?? '');
        return /^[^ \t\r]+$/.test(text) ? text : '-';
      };
      lines.push(['bar', bar.mode, number('_tile_icon_bar_min'), number('_tile_icon_bar_max')]
        .concat(bar.stops.map(stop => stop.position + ':' + iconColorHex(stop.color) + iconColorIconSuffix(stop.icon)))
        .join(' '));
    }
    const binaryOwn = type === '20' && (!layer || layer.self);
    const rows = binaryOwn ? readIconColorBinaryRows(tab) : readIconColorRows(tab);
    for (const row of rows) {
      const color = normalizeIconColorHex(row.color) || ICON_COLOR_ROW_DEFAULT;
      const value = row.value.replace(/[\u0000-\u001f\u007f]/g, '');
      // A row's own "Fuellen" (user 2026-10-11), else it follows "Ausfuellen".
      lines.push((row.has ? 'has' : 'is') + iconColorMarkOf(row.fill) + ' ' + color.slice(1) +
        iconColorIconSuffix(row.icon) + ' ' + value);
    }
    // A switched-off own layer at its defaults without rules is no layer.
    const idle = layer && layer.self && !layer.enabled && layer.mode === 'rules' && layer.icon && !layer.tile &&
      bar.mode === 'off' && rows.length === 0;
    if (layer && !idle) {
      lines.splice(2, 0, 'src ' + layer.mode + ' ' + (layer.self ? 'self' : layer.entity) +
        (layer.tile ? ' tile=' + layer.tile : '') + (layer.icon ? '' : ' noicon') + (layer.enabled ? '' : ' off'));
    }
    return normalizeIconColorRecord(lines.join('\n'), ICON_COLOR_BAR_TYPES.includes(type),
      ICON_COLOR_ROW_TYPES.includes(type), true, ICON_COLOR_OWN_TYPES.includes(type), media);
  }

  // States can be numbers or text: the current state picks the bar or the
  // state list while neither holds colors.
  function iconColorSensorIsText(tab, sourceEntity = null) {
    const entity = sourceEntity ?? (iconColorEl(tab, '_sensor_entity')?.value || '');
    const meta = typeof sensorMetaCache === 'object' ? sensorMetaCache : null;
    const raw = iconColorPayloadState(meta?.values?.[entity] ?? '');
    if (!raw || ['unavailable', 'unknown', 'none', 'null', '--'].includes(raw.toLowerCase())) return false;
    return iconColorLeadingNumber(raw) === null;
  }

  function iconColorMarkActive(tab, role, value) {
    iconColorEl(tab, '_tile_icon_source_section')?.querySelectorAll('[data-icon-color="' + role + '"]').forEach(button => {
      const active = button.dataset.mode === value;
      button.classList.toggle('active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
  }

  function syncIconColorFields(tab) {
    const block = iconColorEl(tab, '_tile_icon_color_fields');
    if (!block) return;
    const type = iconColorTypeOf(tab);
    const visible = tileTypeHasIconColors(type);
    // Tile color "From icon color" lives with the tile color (grid-preview.js).
    if (typeof syncTileColorMode === 'function') syncTileColorMode(tab);
    block.classList.toggle('hidden', !visible);
    iconColorEl(tab, '_tile_icon_color_fixed')?.classList.toggle('hidden', !visible);
    // Icon color: Automatic (unset), Custom (the color field) or, on Media
    // only, From cover.
    const media = type === ICON_COLOR_MEDIA_TYPE;
    const coverIconBox = iconColorEl(tab, '_tile_icon_cover');
    if (coverIconBox && !media) coverIconBox.checked = false;
    const coverIcon = media && !!coverIconBox?.checked;
    const unset = iconColorEl(tab, '_tile_icon_color')?.dataset.unset !== '0';
    const iconMode = coverIcon ? 'cover' : unset ? 'auto' : 'own';
    iconColorEl(tab, '_tile_icon_color_row')?.classList.toggle('hidden', iconMode !== 'own');
    iconColorEl(tab, '_tile_icon_color_modes')?.querySelectorAll('[data-icon-color="icon-color-mode"]').forEach(button => {
      const active = button.dataset.mode === iconMode;
      button.classList.toggle('active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
      if (button.dataset.mode === 'cover') button.classList.toggle('hidden', !media);
    });
    if (typeof fitChoiceRows === 'function') fitChoiceRows(tab);
    if (!visible) return;
    const own = ICON_COLOR_OWN_TYPES.includes(type);
    const kindInput = iconColorEl(tab, '_tile_icon_source_kind');
    const kind = own && kindInput?.value !== 'other' ? 'self' : 'other';
    const on = iconColorEl(tab, '_tile_icon_rules_on')?.value === '1';
    const mode = iconColorEl(tab, '_tile_icon_source_mode')?.value === 'auto' ? 'auto' : 'rules';
    iconColorEl(tab, '_tile_icon_source_section')?.classList.remove('hidden');
    iconColorEl(tab, '_tile_icon_rules_body')?.classList.toggle('hidden', !on);
    iconColorEl(tab, '_tile_icon_source_kinds')?.classList.toggle('hidden', !own);
    iconColorEl(tab, '_tile_icon_source')?.classList.toggle('hidden', kind !== 'other');
    iconColorMarkActive(tab, 'rules-on', on ? '1' : '0');
    iconColorMarkActive(tab, 'source-kind', kind);
    iconColorMarkActive(tab, 'source-mode', mode);
    const layer = readIconColorSource(tab);
    let showBinary = false;
    let showBar = false;
    let showRows = false;
    if (on && mode === 'rules' && layer) {
      const sensorBar = ICON_COLOR_BAR_TYPES.includes(type);
      const sensorRows = ICON_COLOR_ROW_TYPES.includes(type);
      if (kind === 'self' && type === '20') {
        showBinary = true;
      } else if (kind === 'self' && (sensorBar || sensorRows) && !(sensorBar && sensorRows)) {
        showBar = sensorBar;
        showRows = sensorRows;
      } else {
        showBar = true;
        showRows = true;
      }
    }
    if (showBar && showRows) {
      const hasBar = readIconColorBar(tab).mode !== 'off';
      const hasRows = readIconColorRows(tab).length > 0;
      if (hasBar || hasRows) {
        showBar = hasBar;
        showRows = hasRows;
      } else {
        const text = iconColorSensorIsText(tab, kind === 'self' ? iconColorOwnEntity(tab, type) : layer.entity);
        showBar = !text;
        showRows = text;
        if (typeof isSensorMetaCacheLoaded === 'function' && !isSensorMetaCacheLoaded() &&
            typeof fetchSensorMetaCache === 'function' && block.dataset.metaRequested !== '1') {
          block.dataset.metaRequested = '1';
          fetchSensorMetaCache().then(() => syncIconColorFields(tab)).catch(() => {});
        }
      }
    }
    // Boxes without their own mark show "Ausfuellen".
    block.querySelectorAll('[data-icon-color="row-fill"], [data-icon-color="binary-fill"]').forEach(box => {
      if (!box.dataset.mark) box.checked = iconColorRowFills(tab, -1);
    });
    iconColorEl(tab, '_tile_icon_bar_section')?.classList.toggle('hidden', !showBar);
    iconColorEl(tab, '_tile_icon_state_section')?.classList.toggle('hidden', !showRows);
    iconColorEl(tab, '_tile_icon_binary_section')?.classList.toggle('hidden', !showBinary);
    if (showBar) renderIconColorBar(tab);
    renderIconColorIconButtons(tab);
  }

  function loadIconColorFields(tab, data) {
    const type = iconColorTypeOf(tab);
    const parsed = parseIconColorRecord(data?.icon_colors);
    setIconColorInput(tab, parsed.color);
    const media = type === ICON_COLOR_MEDIA_TYPE;
    const fill = iconColorEl(tab, '_tile_icon_fill');
    if (fill) fill.checked = parsed.fill > 0 || (media && parsed.cover.tile > 0) || Number(parsed.source?.tile || 0) > 0;
    const coverIcon = iconColorEl(tab, '_tile_icon_cover');
    if (coverIcon) coverIcon.checked = media && (parsed.cover.icon || parsed.cover.tile > 0);
    const barInput = iconColorEl(tab, '_tile_icon_bar');
    if (barInput) barInput.dataset.last = '';
    writeIconColorBar(tab, parsed.bar ? parsed.bar.mode : 'off', parsed.bar ? parsed.bar.stops : []);
    const min = iconColorEl(tab, '_tile_icon_bar_min');
    const max = iconColorEl(tab, '_tile_icon_bar_max');
    if (min) min.value = parsed.bar ? parsed.bar.minText : '';
    if (max) max.value = parsed.bar ? parsed.bar.maxText : '';
    setIconColorSelectedStop(tab, -1);
    writeIconColorRows(tab, parsed.rows);
    writeIconColorSource(tab, parsed.source);
    // Records without a layer (b40) keep their own rules switched on.
    const own = ICON_COLOR_OWN_TYPES.includes(type);
    const on = parsed.source ? parsed.source.enabled : own && (!!parsed.bar || parsed.rows.length > 0);
    const onInput = iconColorEl(tab, '_tile_icon_rules_on');
    if (onInput) onInput.value = on ? '1' : '0';
    for (const state of ['on', 'off']) {
      const row = parsed.rows.find(entry => !entry.has && iconColorFold(entry.value) === state);
      setIconColorBinaryInput(tab, state, row ? row.color : '', row ? row.fill : -1, row ? row.icon : '');
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
    if (!['color', 'binary', 'value', 'rule-color', 'min', 'max', 'stop-color', 'rule-strength', 'fill-strength',
      'rule-icon', 'binary-icon', 'stop-icon'].includes(role)) return;
    const tab = iconColorEventTab(target);
    if (!tab) return;
    if (role === 'color' || role === 'binary') target.dataset.unset = '0';
    // An On/Off icon keeps its row with the color it shows.
    if (role === 'binary-icon') {
      const color = document.getElementById(target.id.replace(/_icon$/, ''));
      if (color) color.dataset.unset = '0';
    }
    if (role === 'stop-icon') {
      const bar = readIconColorBar(tab);
      const stop = bar.stops[iconColorSelectedStop(tab)];
      if (!stop) return;
      stop.icon = iconColorValidIcon(target.value) ? target.value : '';
      writeIconColorBar(tab, bar.mode, bar.stops);
    }
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
    if (['has', 'source', 'fill-target', 'row-fill', 'binary-fill'].includes(target?.dataset?.iconColor)) {
      const tab = iconColorEventTab(target);
      if (!tab) return;
      // A clicked box gives its row its own mark; an On/Off box keeps that
      // row with the color it shows.
      if (['row-fill', 'binary-fill'].includes(target.dataset.iconColor)) target.dataset.mark = target.checked ? '+' : '-';
      if (target.dataset.iconColor === 'binary-fill') {
        const input = iconColorEl(tab, '_tile_icon_' + target.dataset.state);
        if (input) input.dataset.unset = '0';
      }
      commitIconColorChange(tab);
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
    if (role === 'stop-icon-button') {
      // Its icon picker (tile-head.js) sets this stop's icon.
      setIconColorSelectedStop(tab, Number(button.dataset.stop));
      renderIconColorBar(tab);
      return;
    }
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
    if (role === 'icon-color-mode') {
      const mode = button.dataset.mode;
      const cover = iconColorEl(tab, '_tile_icon_cover');
      if (cover) cover.checked = mode === 'cover';
      // Custom starts from the color the field shows; Automatic forgets it.
      const input = iconColorEl(tab, '_tile_icon_color');
      if (mode === 'auto') setIconColorInput(tab, '');
      else if (mode === 'own' && input) input.dataset.unset = '0';
    } else if (role === 'source-mode') {
      const mode = iconColorEl(tab, '_tile_icon_source_mode');
      if (mode) mode.value = button.dataset.mode === 'rules' ? 'rules' : 'auto';
    } else if (role === 'rules-on') {
      const on = iconColorEl(tab, '_tile_icon_rules_on');
      if (on) on.value = button.dataset.mode === '1' ? '1' : '0';
    } else if (role === 'source-kind') {
      const kind = iconColorEl(tab, '_tile_icon_source_kind');
      if (kind) kind.value = button.dataset.mode === 'other' ? 'other' : 'self';
    } else if (role === 'binary-clear') {
      setIconColorBinaryInput(tab, button.dataset.state, '');
    } else if (role === 'remove') {
      const rows = readIconColorRows(tab);
      rows.splice(Number(button.dataset.rule), 1);
      writeIconColorRows(tab, rows);
    } else if (role === 'add') {
      const rows = readIconColorRows(tab);
      if (rows.length >= ICON_COLOR_MAX_ROWS) return;
      rows.push({ value: '', has: false, color: ICON_COLOR_ROW_DEFAULT, fill: -1 });
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

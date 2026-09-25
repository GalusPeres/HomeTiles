
  // Per-tile icon colors for the Sensor family (Sensor, Number, Select,
  // Date/Time), Binary sensor and Energy: an optional fixed icon color and up
  // to three color rules, first match wins. The editor keeps the canonical
  // record of src/tiles/config/tile_icon_colors.h in the "icon_colors" field:
  // line 1 is the fixed color "RRGGBB" or empty, then one
  // "<op> RRGGBB <value>" line per rule. The six type modules call the
  // load/save/reset helpers below from their own field handlers, so drafts,
  // copy/paste, autosave and import/export carry the record like any other
  // type field.
  const ICON_COLOR_TYPES = ['1', '14', '20', '21', '22', '23'];
  const ICON_COLOR_NUMERIC_TYPES = ['14', '21'];
  const ICON_COLOR_TEXT_TYPES = ['20', '22', '23'];
  const ICON_COLOR_NUMERIC_OPS = ['ge', 'le', 'eq'];
  const ICON_COLOR_TEXT_OPS = ['is', 'has'];
  const ICON_COLOR_MAX_RULES = 3;
  const ICON_COLOR_MAX_VALUE_BYTES = 32;
  const ICON_COLOR_RULE_DEFAULT = '#F44336';

  function tileTypeHasIconColors(typeValue) {
    return ICON_COLOR_TYPES.includes(String(typeValue ?? '0'));
  }

  // Sensor states can be numbers or text; the other types have one kind.
  function iconColorOpsForType(typeValue) {
    const type = String(typeValue ?? '0');
    if (ICON_COLOR_NUMERIC_TYPES.includes(type)) return ICON_COLOR_NUMERIC_OPS;
    if (ICON_COLOR_TEXT_TYPES.includes(type)) return ICON_COLOR_TEXT_OPS;
    return ICON_COLOR_NUMERIC_OPS.concat(ICON_COLOR_TEXT_OPS);
  }

  function normalizeIconColorHex(value) {
    const match = /^#?([0-9a-fA-F]{6})$/.exec(String(value ?? '').trim());
    return match ? '#' + match[1].toUpperCase() : '';
  }

  // Clips to the firmware's byte limit without splitting a character.
  function clipIconRuleValue(value) {
    let text = String(value ?? '').replace(/[\u0000-\u001f\u007f]/g, '').trim();
    const encoder = new TextEncoder();
    while (encoder.encode(text).length > ICON_COLOR_MAX_VALUE_BYTES) text = Array.from(text).slice(0, -1).join('');
    return text.trim();
  }

  function parseIconColorRecord(record) {
    const lines = String(record ?? '').replace(/\r/g, '').split('\n');
    const rules = [];
    for (const line of lines.slice(1)) {
      const match = /^(ge|le|eq|is|has) #?([0-9a-fA-F]{6}) (.+)$/.exec(line.trim());
      if (!match || rules.length >= ICON_COLOR_MAX_RULES) continue;
      rules.push({ op: match[1], color: '#' + match[2].toUpperCase(), value: match[3].trim() });
    }
    return { color: normalizeIconColorHex(lines[0]), rules };
  }

  // Same rules as tile_icon_colors::normalize(): numeric rules need a number,
  // empty values are dropped, at most three rules.
  function buildIconColorRecord(color, rules) {
    const lines = [normalizeIconColorHex(color).slice(1)];
    for (const rule of rules || []) {
      if (lines.length > ICON_COLOR_MAX_RULES) break;
      if (!ICON_COLOR_NUMERIC_OPS.includes(rule.op) && !ICON_COLOR_TEXT_OPS.includes(rule.op)) continue;
      let value = clipIconRuleValue(rule.value);
      if (ICON_COLOR_NUMERIC_OPS.includes(rule.op)) {
        value = value.replace(',', '.');
        if (!value || !Number.isFinite(Number(value))) continue;
      }
      const ruleColor = normalizeIconColorHex(rule.color);
      if (!value || !ruleColor) continue;
      lines.push(rule.op + ' ' + ruleColor.slice(1) + ' ' + value);
    }
    return lines.length > 1 || lines[0] ? lines.join('\n') : '';
  }

  // Rule matching used by the Web Admin preview; mirrors resolve() in
  // tile_icon_colors.h. Returns the icon color or '' for the type default.
  function resolveIconColorRecord(record, state, display) {
    const parsed = parseIconColorRecord(record);
    if (state === undefined || state === null) return '';
    const fold = text => String(text ?? '').trim().toLowerCase();
    const number = parseFloat(String(state).trim().replace(',', '.'));
    for (const rule of parsed.rules) {
      let match = false;
      if (ICON_COLOR_NUMERIC_OPS.includes(rule.op)) {
        const limit = Number(rule.value);
        if (Number.isFinite(number) && Number.isFinite(limit)) {
          if (rule.op === 'ge') match = number >= limit;
          else if (rule.op === 'le') match = number <= limit;
          else match = Math.abs(number - limit) <= 1e-6 * Math.max(1, Math.abs(limit));
        }
      } else {
        const needle = fold(rule.value);
        const test = text => rule.op === 'is' ? fold(text) === needle : fold(text).includes(needle);
        match = test(state) || (display !== undefined && display !== null && test(display));
      }
      if (match) return rule.color;
    }
    return parsed.color;
  }

  function iconColorRuleRow(tab, index) {
    return document.getElementById(tab + '_tile_icon_rule_' + index);
  }

  function readIconColorRules(tab) {
    const rules = [];
    for (let index = 0; index < ICON_COLOR_MAX_RULES; index++) {
      const row = iconColorRuleRow(tab, index);
      if (!row || row.classList.contains('hidden')) continue;
      rules.push({
        op: document.getElementById(row.id + '_op')?.value || 'ge',
        value: document.getElementById(row.id + '_value')?.value || '',
        color: document.getElementById(row.id + '_color')?.value || ICON_COLOR_RULE_DEFAULT
      });
    }
    return rules;
  }

  function writeIconColorRules(tab, rules) {
    for (let index = 0; index < ICON_COLOR_MAX_RULES; index++) {
      const row = iconColorRuleRow(tab, index);
      if (!row) continue;
      const rule = rules[index];
      row.classList.toggle('hidden', !rule);
      const op = document.getElementById(row.id + '_op');
      const value = document.getElementById(row.id + '_value');
      const color = document.getElementById(row.id + '_color');
      if (op) op.value = rule ? rule.op : 'ge';
      if (value) value.value = rule ? rule.value : '';
      if (color) color.value = rule ? (normalizeIconColorHex(rule.color) || ICON_COLOR_RULE_DEFAULT) : ICON_COLOR_RULE_DEFAULT;
    }
    document.getElementById(tab + '_tile_icon_rule_add')
      ?.classList.toggle('hidden', rules.length >= ICON_COLOR_MAX_RULES);
  }

  // An unset icon color keeps the type's default (white or state color).
  function setIconColorInput(tab, color) {
    const input = document.getElementById(tab + '_tile_icon_color');
    if (!input) return;
    const hex = normalizeIconColorHex(color);
    input.value = hex || '#FFFFFF';
    input.dataset.unset = hex ? '0' : '1';
  }

  function collectIconColorRecord(tab) {
    const input = document.getElementById(tab + '_tile_icon_color');
    const color = input && input.dataset.unset !== '1' ? input.value : '';
    return buildIconColorRecord(color, readIconColorRules(tab));
  }

  function syncIconColorFields(tab) {
    const block = document.getElementById(tab + '_tile_icon_color_fields');
    if (!block) return;
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const visible = tileTypeHasIconColors(typeValue);
    block.classList.toggle('hidden', !visible);
    if (!visible) return;
    const ops = iconColorOpsForType(typeValue);
    block.querySelectorAll('select[data-icon-color="op"]').forEach(select => {
      Array.from(select.options).forEach(option => {
        option.hidden = !ops.includes(option.value);
        option.disabled = option.hidden;
      });
      if (!ops.includes(select.value)) select.value = ops[0];
    });
  }

  function loadIconColorFields(tab, data) {
    const parsed = parseIconColorRecord(data?.icon_colors);
    setIconColorInput(tab, parsed.color);
    writeIconColorRules(tab, parsed.rules);
    syncIconColorFields(tab);
  }

  function saveIconColorFields(tab, formData) {
    formData.append('icon_colors', collectIconColorRecord(tab));
  }

  function resetIconColorFields(tab) {
    setIconColorInput(tab, '');
    writeIconColorRules(tab, []);
    syncIconColorFields(tab);
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

  document.addEventListener('input', event => {
    const role = event.target?.dataset?.iconColor;
    if (!['color', 'op', 'value', 'rule-color'].includes(role)) return;
    const tab = iconColorEventTab(event.target);
    if (!tab) return;
    if (role === 'color') event.target.dataset.unset = '0';
    commitIconColorChange(tab);
  });

  document.addEventListener('change', event => {
    const id = event.target?.id || '';
    if (id.endsWith('_tile_type')) syncIconColorFields(id.slice(0, -'_tile_type'.length));
  });

  document.addEventListener('click', event => {
    const button = event.target?.closest?.('button[data-icon-color]');
    const tab = iconColorEventTab(button);
    if (!button || !tab) return;
    const role = button.dataset.iconColor;
    if (role === 'clear') {
      setIconColorInput(tab, '');
    } else if (role === 'remove') {
      const rules = readIconColorRules(tab);
      rules.splice(Number(button.dataset.rule), 1);
      writeIconColorRules(tab, rules);
    } else if (role === 'add') {
      const rules = readIconColorRules(tab);
      if (rules.length >= ICON_COLOR_MAX_RULES) return;
      const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
      rules.push({ op: iconColorOpsForType(typeValue)[0], value: '', color: ICON_COLOR_RULE_DEFAULT });
      writeIconColorRules(tab, rules);
      document.getElementById(tab + '_tile_icon_rule_' + (rules.length - 1) + '_value')?.focus();
    } else {
      return;
    }
    commitIconColorChange(tab);
  });

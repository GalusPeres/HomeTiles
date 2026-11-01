  function isEditablePreview(kind) { return ['number', 'select', 'datetime'].includes(kind); }
  // The preview's value as HTML: a number's unit a step smaller after it
  // (.tile-unit, like the panel's value_unit::show).
  function editablePreviewHtml(entity, kind, meta = sensorMetaCache) {
    let value = meta?.editableValues?.[entity];
    if (typeof value === 'string') { try { value = JSON.parse(value); } catch (_) { value = null; } }
    const text = editablePreviewText(entity, kind, meta);
    const unit = value && value.kind === 'number' && value.unit && value.available && value.state !== 'unavailable' &&
      value.state !== 'unknown' && String(value.state).trim() && Number.isFinite(Number(value.state)) ? String(value.unit) : '';
    if (!unit || !text.endsWith(' ' + unit)) return escapeHtml(text);
    return escapeHtml(text.slice(0, -unit.length - 1)) + '<span class="tile-unit">' + escapeHtml(unit) + '</span>';
  }

  function editablePreviewText(entity, kind, meta = sensorMetaCache) {
    let value = meta?.editableValues?.[entity];
    if (typeof value === 'string') { try { value = JSON.parse(value); } catch (_) { return '--'; } }
    if (!value || value.version !== 1 || value.state === null || value.state === undefined) return '--';
    const tr = kind === 'number' ? NUMBER_I18N : kind === 'select' ? SELECT_I18N : DATETIME_I18N;
    if (!value.available || value.state === 'unavailable') return tr.unavailable;
    if (value.state === 'unknown') return tr.unknown;
    if (value.kind === 'number') {
      if (!String(value.state).trim() || !Number.isFinite(Number(value.state))) return tr.unknown;
      return formatSensorValue(String(value.state), undefined) + (value.unit ? ' ' + value.unit : '');
    }
    return String(value.state);
  }

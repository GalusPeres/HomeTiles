  function isEditablePreview(kind) { return ['number', 'select', 'datetime'].includes(kind); }
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

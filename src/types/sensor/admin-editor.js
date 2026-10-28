
  function updateSensorValuePreview(tab) {
    if (currentTileIndex === -1) return;
    const prefix = tab;
    const entitySelect = document.getElementById(prefix + '_sensor_entity');
    const unitInput = document.getElementById(prefix + '_sensor_unit');
    const decimalsInput = document.getElementById(prefix + '_sensor_decimals');
    const valueFontSelect = document.getElementById(prefix + '_sensor_value_font');
    if (!entitySelect) return;
    const entity = entitySelect.value;
    if (!entity) {
      const valueElem = document.getElementById(tab + '-tile-' + currentTileIndex + '-value');
      if (valueElem) {
        const unit = resolveUnitValue(unitInput ? unitInput.value : '', '', sensorMetaCache.units);
        valueElem.innerHTML = '--' +
          (unit ? '<span class="tile-unit">' + escapeHtml(unit) + '</span>' : '');
        applySensorValueFontClass(valueElem, valueFontSelect ? valueFontSelect.value : '0');
      }
      return;
    }
    const applyMeta = (meta) => {
      const values = (meta && meta.values) || {};
      const valueElem = document.getElementById(tab + '-tile-' + currentTileIndex + '-value');
      if (valueElem) {
        const decimals = decimalsInput ? decimalsInput.value : '';
        const value = formatSensorValue(values[entity] ?? '--', decimals);
        const unit = resolveUnitValue(unitInput ? unitInput.value : '', entity, (meta && meta.units) || {});
        valueElem.innerHTML = escapeHtml(value) +
          (unit ? '<span class="tile-unit">' + escapeHtml(unit) + '</span>' : '');
        applySensorValueFontClass(valueElem, valueFontSelect ? valueFontSelect.value : '0');
      }
    };
    const metaPromise = isSensorMetaCacheLoaded() ? Promise.resolve(sensorMetaCache) : fetchSensorMetaCache();
    metaPromise
      .then(meta => applyMeta(meta))
      .catch(err => console.error('Sensor value load failed:', err));
  }

  function normalizeSensorValueFont(value) {
    const v = String(value || '0');
    return (['1','2','3','4','5'].includes(v)) ? v : '0';
  }

  function getSensorValueFontClass(value) {
    const v = normalizeSensorValueFont(value);
    if (v === '1') return 'sensor-value-size-20';
    if (v === '2') return 'sensor-value-size-24';
    if (v === '3') return 'sensor-value-size-32';
    if (v === '4') return 'sensor-value-size-40';
    return 'sensor-value-size-default';
  }

  function applySensorValueFontClass(el, value) {
    if (!el) return;
    el.classList.remove('sensor-value-size-20', 'sensor-value-size-24', 'sensor-value-size-32', 'sensor-value-size-40', 'sensor-value-size-default');
    el.classList.add(getSensorValueFontClass(value));
  }

  // The Sensor menu's choices (web_html.cpp, mockup sensor-menu) and the
  // stored Tile::sensor_display_mode (sensor_look.h). Layout 0 value,
  // 1 graph, 2 bar, 3 ring, 4 the old gauge; Form 0 fill or line, 1 stripes;
  // Orientation 0 horizontal, 1 upright; Ring 0 open, 1 closed; Value 0 in
  // the head, 1 inside the ring.
  const SENSOR_LAYOUT_FIELDS = ['sensor_layout', 'sensor_form', 'sensor_orientation', 'sensor_ring', 'sensor_place'];

  function sensorModeFromChoices(c) {
    if (c.layout === 1) return 2;
    if (c.layout === 4) return 1;
    if (c.layout === 2) return 3 + (c.form ? 2 : 0) + (c.orientation ? 1 : 0);
    if (c.layout === 3) {
      if (c.ring) return (c.place ? 11 : 13) + (c.form ? 1 : 0);
      return 7 + (c.form ? 2 : 0) + (c.place ? 1 : 0);
    }
    return 0;
  }

  function sensorChoicesFromMode(mode) {
    mode = Number(mode) || 0;
    const c = {layout: 0, form: 0, orientation: 0, ring: 0, place: 1};
    if (mode === 1) c.layout = 4;
    else if (mode === 2) c.layout = 1;
    else if (mode >= 3 && mode <= 6) Object.assign(c, {layout: 2, form: mode >= 5 ? 1 : 0, orientation: (mode - 3) % 2});
    else if (mode >= 7 && mode <= 10) Object.assign(c, {layout: 3, form: mode >= 9 ? 1 : 0, place: (mode - 7) % 2});
    else if (mode === 11 || mode === 12) Object.assign(c, {layout: 3, ring: 1, form: mode - 11, place: 1});
    else if (mode === 13 || mode === 14) Object.assign(c, {layout: 3, ring: 1, form: mode - 13, place: 0});
    return c;
  }

  // Shows a stored mode in the choices; the old gauge's button only on a
  // tile that has it (`gauge`).
  function setSensorLayoutChoices(tab, mode, gauge) {
    const c = sensorChoicesFromMode(mode);
    const values = {sensor_layout: c.layout, sensor_form: c.form, sensor_orientation: c.orientation,
      sensor_ring: c.ring, sensor_place: c.place};
    for (const field of SENSOR_LAYOUT_FIELDS) {
      const select = document.getElementById(tab + '_' + field);
      if (select) select.value = String(values[field]);
    }
    const layout = document.getElementById(tab + '_sensor_layout');
    if (layout?.dataset) layout.dataset.gauge = gauge ? '1' : '0';
  }

  // A choice changed: the stored mode follows and its change event runs the
  // preview, draft and autosave.
  function applySensorLayoutChoices(tab) {
    const read = field => Number(document.getElementById(tab + '_' + field)?.value || 0);
    const mode = String(sensorModeFromChoices({layout: read('sensor_layout'), form: read('sensor_form'),
      orientation: read('sensor_orientation'), ring: read('sensor_ring'), place: read('sensor_place')}));
    const select = document.getElementById(tab + '_sensor_display_mode');
    if (select && String(select.value) !== mode) {
      select.value = mode;
      if (typeof select.dispatchEvent === 'function') select.dispatchEvent(new Event('change', {bubbles: true}));
    }
    syncGaugeUi(tab);
  }

  function syncGaugeUi(tab) {
    const prefix = tab;
    const typeValue = document.getElementById(prefix + '_tile_type')?.value || '0';
    const gaugeWrap = document.getElementById(prefix + '_sensor_gauge_fields');
    const graphWrap = document.getElementById(prefix + '_sensor_graph_fields');
    // The graph fills its box; its old height field stays hidden.
    if (graphWrap) graphWrap.classList.add('hidden');
    if (typeValue !== '1') {
      if (gaugeWrap) gaugeWrap.classList.add('hidden');
      return;
    }
    const displayMode = document.getElementById(prefix + '_sensor_display_mode')?.value || '0';
    const c = sensorChoicesFromMode(displayMode);
    const row = (field, shown) => {
      const label = typeof document.querySelector === 'function'
        ? document.querySelector('label[for="' + prefix + '_' + field + '"]') : null;
      if (label) label.classList.toggle('hidden', !shown);
      document.getElementById(prefix + '_' + field + '_choices')?.classList.toggle('hidden', !shown);
    };
    const bar = c.layout === 2, ring = c.layout === 3, gauge = c.layout === 4;
    row('sensor_form', bar || ring);
    row('sensor_orientation', bar);
    row('sensor_ring', ring);
    row('sensor_place', ring);
    // A bar's first form is its fill, a ring's its line.
    const form = document.getElementById(prefix + '_sensor_form');
    const first = form?.options?.[0];
    const firstName = bar ? form?.dataset?.level : form?.dataset?.line;
    if (first && firstName) first.textContent = firstName;
    // The old gauge stays offered on a tile that has it.
    const layout = document.getElementById(prefix + '_sensor_layout');
    const gaugeOption = layout?.options ? Array.from(layout.options).find(o => o.value === '4') : null;
    if (gaugeOption) gaugeOption.hidden = layout.dataset?.gauge !== '1' && !gauge;
    // The range belongs to the gauge and every look; the arc, size and
    // offset to the gauge alone.
    if (gaugeWrap) gaugeWrap.classList.toggle('hidden', !(bar || ring || gauge));
    const shapeWrap = document.getElementById(prefix + '_sensor_gauge_shape_fields');
    if (shapeWrap) shapeWrap.classList.toggle('hidden', !gauge);
    // The value size everywhere: alone, in the head, inside an arc or ring
    // (its Default there the look's 20 or 24).
    row('sensor_value_font', true);
    const offsetShown = c.layout === 0 || gauge;
    const offsetLabel = typeof document.querySelector === 'function'
      ? document.querySelector('label[for="' + prefix + '_sensor_value_y_offset"]') : null;
    if (offsetLabel) offsetLabel.classList.toggle('hidden', !offsetShown);
    document.getElementById(prefix + '_sensor_value_y_offset')?.classList.toggle('hidden', !offsetShown);
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
  }

  function loadSensorFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_sensor_entity');
    if (entityEl) entityEl.value = data.sensor_entity || '';
    const unitEl = document.getElementById(prefix + '_sensor_unit');
    if (unitEl) unitEl.value = data.sensor_unit || '';
    const decEl = document.getElementById(prefix + '_sensor_decimals');
    if (decEl) decEl.value = (data.sensor_decimals !== undefined && data.sensor_decimals >= 0) ? data.sensor_decimals : '';
    const fontEl = document.getElementById(prefix + '_sensor_value_font');
    if (fontEl) fontEl.value = (data.sensor_value_font !== undefined) ? String(data.sensor_value_font) : '0';
    const popupModeEl = document.getElementById(prefix + '_sensor_popup_open_mode');
    if (popupModeEl) popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    const displayModeEl = document.getElementById(prefix + '_sensor_display_mode');
    if (displayModeEl) displayModeEl.value = (data.sensor_display_mode !== undefined) ? String(data.sensor_display_mode) : '0';
    setSensorLayoutChoices(tab, displayModeEl?.value || 0, Number(data.sensor_display_mode) === 1);
    const gaugeMinEl = document.getElementById(prefix + '_sensor_gauge_min');
    if (gaugeMinEl) gaugeMinEl.value = (data.sensor_gauge_min !== undefined && data.sensor_gauge_min !== null) ? String(data.sensor_gauge_min) : '';
    const gaugeMaxEl = document.getElementById(prefix + '_sensor_gauge_max');
    if (gaugeMaxEl) gaugeMaxEl.value = (data.sensor_gauge_max !== undefined && data.sensor_gauge_max !== null) ? String(data.sensor_gauge_max) : '';
    const gaugeArcEl = document.getElementById(prefix + '_sensor_gauge_arc');
    if (gaugeArcEl) gaugeArcEl.value = (data.sensor_gauge_arc !== undefined && data.sensor_gauge_arc !== null) ? String(data.sensor_gauge_arc) : '';
    const gaugeSizeEl = document.getElementById(prefix + '_sensor_gauge_size');
    if (gaugeSizeEl) gaugeSizeEl.value = (data.sensor_gauge_size !== undefined && data.sensor_gauge_size !== null) ? String(data.sensor_gauge_size) : '';
    const gaugeYOffsetEl = document.getElementById(prefix + '_sensor_gauge_y_offset');
    if (gaugeYOffsetEl) gaugeYOffsetEl.value = (data.sensor_gauge_y_offset !== undefined && data.sensor_gauge_y_offset !== null) ? String(data.sensor_gauge_y_offset) : '';
    const valueYOffsetEl = document.getElementById(prefix + '_sensor_value_y_offset');
    if (valueYOffsetEl) valueYOffsetEl.value = (data.sensor_value_y_offset !== undefined && data.sensor_value_y_offset !== null) ? String(data.sensor_value_y_offset) : '';
    const graphHeightEl = document.getElementById(prefix + '_sensor_graph_height');
    if (graphHeightEl) graphHeightEl.value = (data.sensor_graph_height !== undefined && data.sensor_graph_height !== null) ? String(data.sensor_graph_height) : '';
    syncGaugeUi(tab);
    // The entity is known now: numeric states show the color bar, text
    // states the state list.
    syncIconColorFields(tab);
  }

  function saveSensorFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    formData.append('sensor_entity', document.getElementById(prefix + '_sensor_entity')?.value || '');
    formData.append('sensor_unit', document.getElementById(prefix + '_sensor_unit')?.value || '');
    formData.append('sensor_decimals', document.getElementById(prefix + '_sensor_decimals')?.value || '');
    formData.append('sensor_value_font', document.getElementById(prefix + '_sensor_value_font')?.value || '0');
    formData.append('popup_open_mode', document.getElementById(prefix + '_sensor_popup_open_mode')?.value || '1');
    formData.append('sensor_display_mode', document.getElementById(prefix + '_sensor_display_mode')?.value || '0');
    formData.append('sensor_gauge_min', document.getElementById(prefix + '_sensor_gauge_min')?.value || '');
    formData.append('sensor_gauge_max', document.getElementById(prefix + '_sensor_gauge_max')?.value || '');
    formData.append('sensor_gauge_arc', document.getElementById(prefix + '_sensor_gauge_arc')?.value || '');
    formData.append('sensor_gauge_size', document.getElementById(prefix + '_sensor_gauge_size')?.value || '');
    formData.append('sensor_gauge_y_offset', document.getElementById(prefix + '_sensor_gauge_y_offset')?.value || '');
    formData.append('sensor_value_y_offset', document.getElementById(prefix + '_sensor_value_y_offset')?.value || '');
    formData.append('sensor_graph_height', document.getElementById(prefix + '_sensor_graph_height')?.value || '');
  }

  function resetSensorFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_sensor_entity');
    if (entityEl) entityEl.value = '';
    const unitEl = document.getElementById(prefix + '_sensor_unit');
    if (unitEl) unitEl.value = '';
    const decEl = document.getElementById(prefix + '_sensor_decimals');
    if (decEl) decEl.value = '';
    const fontEl = document.getElementById(prefix + '_sensor_value_font');
    if (fontEl) fontEl.value = '0';
    const popupModeEl = document.getElementById(prefix + '_sensor_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
    const displayModeEl = document.getElementById(prefix + '_sensor_display_mode');
    if (displayModeEl) displayModeEl.value = '0';
    setSensorLayoutChoices(tab, 0, false);
    const gaugeMinEl = document.getElementById(prefix + '_sensor_gauge_min');
    if (gaugeMinEl) gaugeMinEl.value = '';
    const gaugeMaxEl = document.getElementById(prefix + '_sensor_gauge_max');
    if (gaugeMaxEl) gaugeMaxEl.value = '';
    const gaugeArcEl = document.getElementById(prefix + '_sensor_gauge_arc');
    if (gaugeArcEl) gaugeArcEl.value = '';
    const gaugeSizeEl = document.getElementById(prefix + '_sensor_gauge_size');
    if (gaugeSizeEl) gaugeSizeEl.value = '';
    const gaugeYOffsetEl = document.getElementById(prefix + '_sensor_gauge_y_offset');
    if (gaugeYOffsetEl) gaugeYOffsetEl.value = '';
    const valueYOffsetEl = document.getElementById(prefix + '_sensor_value_y_offset');
    if (valueYOffsetEl) valueYOffsetEl.value = '';
    const graphHeightEl = document.getElementById(prefix + '_sensor_graph_height');
    if (graphHeightEl) graphHeightEl.value = '';
  }

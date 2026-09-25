
function maybeFillTitleFromEnergy(tab) {
    maybeFillTitleFromEntity(tab, '_energy_entity');
  }

  function updateEnergyValuePreview(tab) {
    if (currentTileIndex === -1) return;
    const prefix = tab;
    const entitySelect = document.getElementById(prefix + '_energy_entity');
    const unitInput = document.getElementById(prefix + '_energy_unit');
    const decimalsInput = document.getElementById(prefix + '_energy_decimals');
    const valueFontSelect = document.getElementById(prefix + '_energy_value_font');
    if (!entitySelect) return;
    const entity = entitySelect.value;
    const valueElem = document.getElementById(tab + '-tile-' + currentTileIndex + '-value');
    if (!valueElem) return;
    const applyMeta = (meta) => {
      const values = (meta && meta.values) || {};
      const decimals = decimalsInput ? decimalsInput.value : '1';
      const value = entity ? formatSensorValue(values[entity] ?? '--', decimals) : '--';
      const unit = resolveUnitValue(unitInput ? unitInput.value : '', entity, (meta && meta.units) || {});
      valueElem.innerHTML = escapeHtml(value) +
        (unit && value !== '--'
          ? '<span class="tile-unit">' + escapeHtml(unit) + '</span>' : '');
      applySensorValueFontClass(valueElem, valueFontSelect ? valueFontSelect.value : '0');
    };
    const metaPromise = isSensorMetaCacheLoaded() ? Promise.resolve(sensorMetaCache) : fetchSensorMetaCache();
    metaPromise
      .then(meta => applyMeta(meta))
      .catch(err => console.error('Energy value load failed:', err));
  }

  function loadEnergyFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_energy_entity');
    if (entityEl) {
      const configuredEntity = data.sensor_entity || data.energy_entity || '';
      entityEl.dataset.configuredValue = configuredEntity;
      entityEl.value = configuredEntity;
      if (configuredEntity && entityEl.value !== configuredEntity) {
        const opt = document.createElement('option');
        opt.value = configuredEntity;
        opt.textContent = configuredEntity;
        entityEl.appendChild(opt);
        entityEl.value = configuredEntity;
      }
    }
    const unitEl = document.getElementById(prefix + '_energy_unit');
    if (unitEl) unitEl.value = data.sensor_unit || '';
    const decEl = document.getElementById(prefix + '_energy_decimals');
    if (decEl) decEl.value = (data.sensor_decimals !== undefined && data.sensor_decimals >= 0) ? data.sensor_decimals : '1';
    const fontEl = document.getElementById(prefix + '_energy_value_font');
    if (fontEl) fontEl.value = (data.sensor_value_font !== undefined) ? String(data.sensor_value_font) : '0';
    const popupModeEl = document.getElementById(prefix + '_energy_popup_open_mode');
    if (popupModeEl) popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    const valueYOffsetEl = document.getElementById(prefix + '_energy_value_y_offset');
    if (valueYOffsetEl) valueYOffsetEl.value = (data.sensor_value_y_offset !== undefined && data.sensor_value_y_offset !== null) ? String(data.sensor_value_y_offset) : '';
    maybeFillTitleFromEnergy(tab);
  }

  function saveEnergyFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_energy_entity');
    const entity = entityEl ? (entityEl.value || entityEl.dataset.configuredValue || '') : '';
    formData.append('energy_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('sensor_unit', document.getElementById(prefix + '_energy_unit')?.value || '');
    formData.append('sensor_decimals', document.getElementById(prefix + '_energy_decimals')?.value || '1');
    formData.append('sensor_value_font', document.getElementById(prefix + '_energy_value_font')?.value || '0');
    formData.append('popup_open_mode', document.getElementById(prefix + '_energy_popup_open_mode')?.value || '1');
    formData.append('sensor_value_y_offset', document.getElementById(prefix + '_energy_value_y_offset')?.value || '');
  }

  function resetEnergyFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_energy_entity');
    if (entityEl) entityEl.value = '';
    const unitEl = document.getElementById(prefix + '_energy_unit');
    if (unitEl) unitEl.value = '';
    const decEl = document.getElementById(prefix + '_energy_decimals');
    if (decEl) decEl.value = '1';
    const fontEl = document.getElementById(prefix + '_energy_value_font');
    if (fontEl) fontEl.value = '0';
    const popupModeEl = document.getElementById(prefix + '_energy_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
    const valueYOffsetEl = document.getElementById(prefix + '_energy_value_y_offset');
    if (valueYOffsetEl) valueYOffsetEl.value = '';
  }

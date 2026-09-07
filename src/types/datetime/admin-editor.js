
  function loadDateTimeFields(tab, data) {
    const font = document.getElementById(tab + '_datetime_value_font');
    if (font) font.value = String(data.sensor_value_font ?? 2);
    const entity = document.getElementById(tab + '_datetime_entity');
    const configured = data.sensor_entity || data.datetime_entity || '';
    if (entity) {
      if (configured) {
        entity.dataset.configuredValue = configured;
        if (!Array.from(entity.options).some(option => option.value === configured)) {
          const option = document.createElement('option');
          option.value = configured;
          option.textContent = configured;
          entity.appendChild(option);
        }
      } else {
        delete entity.dataset.configuredValue;
      }
      entity.value = configured;
    }
    const popup = document.getElementById(
      tab + '_datetime_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
  }

  function saveDateTimeFields(tab, formData) {
    formData.append('sensor_value_font', document.getElementById(tab + '_datetime_value_font')?.value ?? '2');
    const entityEl = document.getElementById(tab + '_datetime_entity');
    const entity = entityEl
      ? (entityEl.value || entityEl.dataset.configuredValue || '') : '';
    formData.append('datetime_entity', entity);
    formData.append('sensor_entity', entity);
    const popup = document.getElementById(
      tab + '_datetime_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetDateTimeFields(tab) {
    const font = document.getElementById(tab + '_datetime_value_font');
    if (font) font.value = '2';
    const entity = document.getElementById(tab + '_datetime_entity');
    if (entity) {
      entity.value = '';
      delete entity.dataset.configuredValue;
    }
    const popup = document.getElementById(
      tab + '_datetime_popup_open_mode');
    if (popup) popup.value = '1';
  }

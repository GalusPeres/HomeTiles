
  function loadBinarySensorFields(tab, data) {
    loadIconColorFields(tab, data);
    const entity = document.getElementById(tab + '_binary_sensor_entity');
    const configured = data.sensor_entity || data.binary_sensor_entity || '';
    if (entity) {
      entity.value = configured;
    }
    const font = document.getElementById(tab + '_binary_sensor_value_font');
    if (font) font.value = normalizeSensorValueFont(data.sensor_value_font);
    const popup = document.getElementById(
      tab + '_binary_sensor_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
  }

  function saveBinarySensorFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const entityEl = document.getElementById(tab + '_binary_sensor_entity');
    const entity = entityEl
      ? entityEl.value : '';
    formData.append('binary_sensor_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('sensor_value_font', document.getElementById(tab + '_binary_sensor_value_font')?.value || '0');
    const popup = document.getElementById(
      tab + '_binary_sensor_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetBinarySensorFields(tab) {
    resetIconColorFields(tab);
    const font = document.getElementById(tab + '_binary_sensor_value_font');
    if (font) font.value = '0';
    const entity = document.getElementById(tab + '_binary_sensor_entity');
    if (entity) entity.value = '';
    const popup = document.getElementById(
      tab + '_binary_sensor_popup_open_mode');
    if (popup) popup.value = '1';
  }

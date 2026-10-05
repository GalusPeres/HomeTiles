
  function loadSelectFields(tab, data) {
    loadIconColorFields(tab, data);
    const font = document.getElementById(tab + '_select_value_font');
    if (font) font.value = String(data.sensor_value_font ?? 2);
    const entity = document.getElementById(tab + '_select_entity');
    const configured = data.sensor_entity || data.select_entity || '';
    if (entity) {
      entity.value = configured;
    }
    const popup = document.getElementById(
      tab + '_select_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
    // The entity is known now: the state color section follows it.
    syncIconColorFields(tab);
  }

  function saveSelectFields(tab, formData) {
    saveIconColorFields(tab, formData);
    formData.append('sensor_value_font', document.getElementById(tab + '_select_value_font')?.value ?? '2');
    const entityEl = document.getElementById(tab + '_select_entity');
    const entity = entityEl
      ? entityEl.value : '';
    formData.append('select_entity', entity);
    formData.append('sensor_entity', entity);
    const popup = document.getElementById(
      tab + '_select_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetSelectFields(tab) {
    resetIconColorFields(tab);
    const font = document.getElementById(tab + '_select_value_font');
    if (font) font.value = '2';
    const entity = document.getElementById(tab + '_select_entity');
    if (entity) entity.value = '';
    const popup = document.getElementById(
      tab + '_select_popup_open_mode');
    if (popup) popup.value = '1';
  }

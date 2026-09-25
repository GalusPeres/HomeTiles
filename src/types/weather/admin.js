
function maybeFillTitleFromWeather(tab) {
    maybeFillTitleFromEntity(tab, '_weather_entity');
  }

  function loadWeatherFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const el = document.getElementById(prefix + '_weather_entity');
    if (el) el.value = data.sensor_entity || data.weather_entity || '';
    const popupModeEl = document.getElementById(prefix + '_weather_popup_open_mode');
    if (popupModeEl) popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    maybeFillTitleFromWeather(tab);
  }

  function saveWeatherFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    formData.append('weather_entity', document.getElementById(prefix + '_weather_entity')?.value || '');
    formData.append('popup_open_mode', document.getElementById(prefix + '_weather_popup_open_mode')?.value || '1');
  }

  function resetWeatherFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const el = document.getElementById(prefix + '_weather_entity');
    if (el) el.value = '';
    const popupModeEl = document.getElementById(prefix + '_weather_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
  }


function maybeFillTitleFromMedia(tab) {
    maybeFillTitleFromEntity(tab, '_media_entity');
  }

  function updateMediaValuePreview(tab) {
    // Media tiles stay intentionally simple in the WebUI preview:
    // only icon and configured tile title are shown.
  }

  function loadMediaFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const el = document.getElementById(prefix + '_media_entity');
    if (el) el.value = data.sensor_entity || data.media_entity || '';
    maybeFillTitleFromMedia(tab);
    updateMediaValuePreview(tab);
  }

  function saveMediaFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    const entity = document.getElementById(prefix + '_media_entity')?.value || '';
    formData.append('media_entity', entity);
    formData.append('sensor_entity', entity);
  }

  function resetMediaFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const el = document.getElementById(prefix + '_media_entity');
    if (el) el.value = '';
  }

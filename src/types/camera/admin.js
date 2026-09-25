
function loadCameraFields(tab, data) {
    loadIconColorFields(tab, data);
    const el = document.getElementById(tab + '_camera_entity');
    const configured = data.sensor_entity || data.camera_entity || '';
    if (el) {
      if (configured) {
        el.dataset.configuredValue = configured;
        if (!Array.from(el.options).some(opt => opt.value === configured)) {
          const opt = document.createElement('option');
          opt.value = configured;
          opt.textContent = configured;
          el.appendChild(opt);
        }
      } else {
        delete el.dataset.configuredValue;
      }
      el.value = configured;
    }
    maybeFillTitleFromEntity(tab, '_camera_entity');
  }
  function saveCameraFields(tab, formData) {
    const entity =
      document.getElementById(tab + '_camera_entity')?.value || '';
    formData.append('camera_entity', entity);
    formData.append('sensor_entity', entity);
    saveIconColorFields(tab, formData);
  }
  function resetCameraFields(tab) {
    resetIconColorFields(tab);
    const el = document.getElementById(tab + '_camera_entity');
    if (el) {
      el.value = '';
      delete el.dataset.configuredValue;
    }
  }

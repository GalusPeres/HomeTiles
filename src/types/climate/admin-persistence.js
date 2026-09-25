
  function saveClimateFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const packed = packClimateSlotConfig(tab);
    const packedLayouts = packClimateTargetLayouts(tab);
    const geometry = document.getElementById(
      tab + '_climate_geometry')?.value || '';
    formData.append('climate_entity',
      document.getElementById(tab + '_climate_entity')?.value || '');
    formData.append('popup_open_mode',
      document.getElementById(tab + '_climate_popup_open_mode')?.value || '1');
    formData.append('climate_slots_packed', String(packed));
    formData.append('climate_layouts_packed', String(packedLayouts));
    formData.append('climate_geometry', geometry);
    formData.append('scene_alias', geometry);
    // Keep the local tile/draft representation in sync with the V7 storage
    // field used by the firmware.
    formData.append('sensor_gauge_min', String(packed));
    formData.append(
      'sensor_gauge_max',
      String((CLIMATE_LAYOUT_MAGIC | packedLayouts) >>> 0));
  }

  function resetClimateFields(tab) {
    resetIconColorFields(tab);
    const entity = document.getElementById(tab + '_climate_entity');
    if (entity) {
      entity.value = '';
      delete entity.dataset.configuredValue;
    }
    const popup = document.getElementById(tab + '_climate_popup_open_mode');
    if (popup) popup.value = '1';
    const geometry = document.getElementById(
      tab + '_climate_geometry');
    if (geometry) geometry.value = '';
    for (let index = 0; index < 6; ++index) {
      const select = document.getElementById(
        tab + '_climate_slot_' + index);
      if (select) select.value = String(CLIMATE_TILE_CONTENT.AUTO);
      const layout = document.getElementById(
        tab + '_climate_layout_' + index);
      if (layout) {
        layout.value = String(CLIMATE_TARGET_LAYOUT.AUTO);
      }
    }
    syncClimateSlotFields(tab);
  }
  bindClimatePreviewSelection();


  function selectTile(index, tab) {
    if (currentTileTab &&
        typeof parkClimateMiniEditor === 'function') {
      parkClimateMiniEditor(currentTileTab);
    }
    currentTileIndex = index;
    currentTileTab = tab;
    document.getElementById('settingsHiddenTile')?.classList.remove('active');
    persistSelectedTileState();
    document.querySelectorAll(
      '#tab-tiles-' + tab + ' .tile-grid > .tile')
      .forEach(t => delete t.dataset.selected);
    document.querySelectorAll(
      '.tile-grid > .tile, .screensaver-tile-grid > .tile')
      .forEach(t =>
        t.classList.remove(
          'active', 'drop-target', 'dragging'));
    const tileId = tab + '-tile-' + index;
    const selectedTile = document.getElementById(tileId);
    if (selectedTile) {
      selectedTile.dataset.selected = '1';
      selectedTile.classList.add('active');
    }
    const settingsId = tab + 'Settings';
    const settingsPanel = document.getElementById(settingsId);
    if (settingsPanel) {
      if (isScreensaverTileTab(tab)) {
        screensaverSelected = { kind: 'tile', index };
        document.getElementById('screensaverBackgroundSettings')?.classList.add('hidden');
        document.getElementById('screensaverClockSettings')?.classList.add('hidden');
        document.getElementById('screensaverGrid')?.classList.remove('selected-background');
        document.getElementById('screensaverClock')?.classList.remove('selected-clock');
      }
      const tileSpecific = settingsPanel.querySelector('.tile-specific-settings');
      if (tileSpecific) {
        tileSpecific.classList.remove('hidden');
      }
    }
    // Bind the live handlers right away. This used to happen only after the
    // asynchronous GET of the tile data, and until then size, position and style
    // visibly did not react in the screensaver preview.
    setupLivePreview(tab);
    loadTileData(index, tab);
  }

  function selectHiddenSettingsTile() {
    const hiddenTile = document.getElementById('settingsHiddenTile');
    if (!hiddenTile || hiddenTile.dataset.hidden !== '1') return;
    if (currentTileTab && typeof parkClimateMiniEditor === 'function') {
      parkClimateMiniEditor(currentTileTab);
    }
    currentTileIndex = -2;
    currentTileTab = 'folder0';
    document.querySelectorAll('.tile-grid > .tile').forEach(tile => {
      tile.classList.remove('active');
      delete tile.dataset.selected;
    });
    hiddenTile.classList.add('active');
    const panel = document.getElementById('folder0Settings');
    const specific = panel?.querySelector('.tile-specific-settings');
    if (specific) {
      specific.classList.remove('hidden');
    }
    const snapshot = normalizeHiddenSettingsSnapshot();
    if (!applyDraft('folder0', HIDDEN_SETTINGS_TILE_INDEX)) {
      applyTileFormData('folder0', snapshot);
      const col = document.getElementById('folder0_tile_col');
      const row = document.getElementById('folder0_tile_row');
      if (col) col.value = snapshot.col;
      if (row) row.value = snapshot.row;
      updateTileType('folder0');
      updateTilePreview('folder0');
    }
    setupLivePreview('folder0');
    toggleSettingsAccessFields();
    const settingsBody = specific?.querySelector('.tile-settings-body');
    if (settingsBody) settingsBody.scrollTop = 0;
    updateTileSettingsMaxHeight();
  }

  function titleFromOption(option) {
    if (!option) return '';
    // The first option is the translated "No selection" placeholder.  It is
    // not an entity name and must never become a persisted tile title.
    if (!String(option.value || '').trim().length) return '';
    const label = String(option.textContent || option.innerText || '').trim();
    if (!label.length) return '';
    const sep = label.indexOf(' - ');
    if (sep > 0) return label.substring(0, sep).trim();
    return label;
  }

  function titleFromEntity(entity) {
    let name = String(entity || '').trim();
    if (!name.length) return '';
    const dot = name.indexOf('.');
    if (dot !== -1) name = name.substring(dot + 1);
    name = name.replace(/[_-]+/g, ' ').trim();
    if (!name.length) return '';
    return name.replace(/\b\w/g, (m) => m.toUpperCase());
  }

  function maybeFillTitleFromEntity(tab, selectSuffix) {
    const prefix = tab;
    const titleInput = document.getElementById(prefix + '_tile_title');
    const selectEl = document.getElementById(prefix + selectSuffix);
    if (!titleInput || !selectEl) return;
    if (titleInput.value && titleInput.value.trim().length) return;
    const opt = selectEl.selectedOptions && selectEl.selectedOptions[0];
    let title = titleFromOption(opt);
    if (!title.length) title = titleFromEntity(selectEl.value);
    if (title.length) titleInput.value = title;
  }

  function setupLivePreview(tab) {
    const prefix = tab;
    const bindLive = (el, eventName, key, handler) => {
      if (!el) return;
      // Settings panels can be restored/replaced from the folder-tab HTML
      // cache.  A serialized data-live-bound flag can survive that operation,
      // while the JavaScript listener itself cannot.  Keep the actual handler
      // on the live DOM node and always replace it deterministically.
      const slot = '__homeTilesLive_' + key + '_' + eventName;
      if (typeof el[slot] === 'function') {
        el.removeEventListener(eventName, el[slot]);
      }
      el.addEventListener(eventName, handler);
      el[slot] = handler;
    };

    const titleInput = document.getElementById(prefix + '_tile_title');
    const iconInput = document.getElementById(prefix + '_tile_icon');
    const colorInput = document.getElementById(prefix + '_tile_color');
    const colInput = document.getElementById(prefix + '_tile_col');
    const rowInput = document.getElementById(prefix + '_tile_row');
    const spanWInput = document.getElementById(prefix + '_tile_span_w');
    const spanHInput = document.getElementById(prefix + '_tile_span_h');
    const typeSelect = document.getElementById(prefix + '_tile_type');
    const opacityInput = isScreensaverTileTab(tab)
      ? document.getElementById('screensaver_tile_opacity') : null;
    const entitySelect = document.getElementById(prefix + '_sensor_entity');
    const binarySensorSelect = document.getElementById(
      prefix + '_binary_sensor_entity');
    const binarySensorPopupModeSelect = document.getElementById(
      prefix + '_binary_sensor_popup_open_mode');
      const unitInput = document.getElementById(prefix + '_sensor_unit');
      const decimalsInput = document.getElementById(prefix + '_sensor_decimals');
      const valueFontSelect = document.getElementById(prefix + '_sensor_value_font');
      const sensorPopupModeSelect = document.getElementById(prefix + '_sensor_popup_open_mode');
      const displayModeSelect = document.getElementById(prefix + '_sensor_display_mode');
      const gaugeMinInput = document.getElementById(prefix + '_sensor_gauge_min');
      const gaugeMaxInput = document.getElementById(prefix + '_sensor_gauge_max');
      const gaugeArcInput = document.getElementById(prefix + '_sensor_gauge_arc');
      const gaugeSizeInput = document.getElementById(prefix + '_sensor_gauge_size');
      const gaugeYOffsetInput = document.getElementById(prefix + '_sensor_gauge_y_offset');
      const valueYOffsetInput = document.getElementById(prefix + '_sensor_value_y_offset');
      const graphHeightInput = document.getElementById(prefix + '_sensor_graph_height');
      const weatherSelect = document.getElementById(prefix + '_weather_entity');
      const weatherPopupModeSelect = document.getElementById(prefix + '_weather_popup_open_mode');
      const energySelect = document.getElementById(prefix + '_energy_entity');
      const energyUnitInput = document.getElementById(prefix + '_energy_unit');
      const energyDecimalsInput = document.getElementById(prefix + '_energy_decimals');
      const energyValueFontSelect = document.getElementById(prefix + '_energy_value_font');
      const energyPopupModeSelect = document.getElementById(prefix + '_energy_popup_open_mode');
      const energyValueYOffsetInput = document.getElementById(prefix + '_energy_value_y_offset');
      const sceneInput = document.getElementById(prefix + '_scene_alias');
    const textInput = document.getElementById(prefix + '_text_value');
    const textFontInput = document.getElementById(prefix + '_text_value_font');
    const navigateSelect = document.getElementById(prefix + '_navigate_target');
    const folderPinToggle = document.getElementById(prefix + '_folder_pin_enabled');
    const folderPinApply = document.getElementById(prefix + '_folder_pin_apply');
    const switchSelect = document.getElementById(prefix + '_switch_entity');
    const switchStyleSelect = document.getElementById(prefix + '_switch_style');
    const switchPopupModeSelect = document.getElementById(prefix + '_switch_popup_open_mode');
    const mediaSelect = document.getElementById(prefix + '_media_entity');
    const climateSelect = document.getElementById(prefix + '_climate_entity');
    const coverSelect = document.getElementById(prefix + '_cover_entity');
    const coverPopupModeSelect = document.getElementById(prefix + '_cover_popup_open_mode');
    const cameraSelect = document.getElementById(prefix + '_camera_entity');
    const climatePopupModeSelect = document.getElementById(prefix + '_climate_popup_open_mode');
    const climateSlotSelects = Array.from(
      { length: 6 },
      (_, index) => document.getElementById(
        prefix + '_climate_slot_' + index));
    const climateLayoutSelects = Array.from(
      { length: 6 },
      (_, index) => document.getElementById(
        prefix + '_climate_layout_' + index));
    const animationSelect = document.getElementById(prefix + '_animation_file');
    const animationFpsInput = document.getElementById(prefix + '_animation_fps');
    const animationFitSelect = document.getElementById(prefix + '_animation_fit');
    const animationZoomInput = document.getElementById(prefix + '_animation_zoom');
    const clockTimeCheck = document.getElementById(prefix + '_clock_show_time');
    const clockDateCheck = document.getElementById(prefix + '_clock_show_date');
    const clockTimeFontSelect = document.getElementById(prefix + '_clock_time_font');
    const clockDateFontSelect = document.getElementById(prefix + '_clock_date_font');
    const clockTimeFormatSelect = document.getElementById(prefix + '_clock_time_format');
    const clockDateFormatSelect = document.getElementById(prefix + '_clock_date_format');
    const settingsPanel = document.getElementById(prefix + 'Settings');

    bindLive(titleInput, 'input', 'tileTitle', () => {
      const normalized = normalizeTileTitle(titleInput.value);
      if (normalized !== titleInput.value) titleInput.value = normalized;
      updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab);
    });
    bindLive(iconInput, 'input', 'tileIcon', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(colorInput, 'input', 'tileColor', () => { markTileColorInputExplicit(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(opacityInput, 'input', 'tileOpacity', () => { updateTilePreview(tab); updateDraft(tab); });
    bindLive(opacityInput, 'change', 'tileOpacitySave', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(colInput, 'input', 'tileCol', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(rowInput, 'input', 'tileRow', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(spanWInput, 'input', 'tileSpanW', () => { syncClimateSlotFields(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(spanHInput, 'input', 'tileSpanH', () => { syncClimateSlotFields(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(typeSelect, 'change', 'tileType', () => {
      const tileEl = document.getElementById(tab + '-tile-' + currentTileIndex);
      const previousType = Number(tileEl?.dataset.type ?? 0);
      const nextType = Number(typeSelect.value);
      // A freshly created tile must start with the selected type's real
      // default colour. Do not inherit an explicit colour state from the empty
      // editor placeholder.
      if (previousType === 0 && nextType !== 0) {
        const nextMeta = getTileTypeMeta(typeSelect.value);
        setTileColorInputFromStored(
          tab, 0, nextMeta.defaultBg || '#2A2A2A');
      }
      if (isScreensaverTileTab(tab) && previousType === 0 &&
          nextType !== 0 && opacityInput) {
        opacityInput.value = String(SCREENSAVER_TILE_DEFAULT_OPACITY);
      }
      updateTileType(tab);
      normalizeLayoutInputs(tab);
      updateLayoutFromInputs(tab);
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(entitySelect, 'change', 'sensorEntity', () => { maybeFillTitleFromSensor(tab); updateTilePreview(tab); updateSensorValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    for (const kind of ['number', 'select', 'datetime']) {
      const select = document.getElementById(prefix + '_' + kind + '_entity');
      bindLive(select, 'change', kind + 'Entity', () => {
        if (select.value) select.dataset.configuredValue = select.value;
        else delete select.dataset.configuredValue;
        maybeFillTitleFromEntity(tab, '_' + kind + '_entity');
        updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab);
      });
      bindLive(document.getElementById(prefix + '_' + kind + '_value_font'), 'change', kind + 'ValueFont', () => {
        updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab);
      });
      bindLive(document.getElementById(prefix + '_' + kind + '_popup_open_mode'), 'change', kind + 'PopupMode', () => {
        updateDraft(tab); scheduleAutoSave(tab);
      });
    }
    bindLive(binarySensorSelect, 'change', 'binarySensorEntity', () => {
      if (binarySensorSelect.value) {
        binarySensorSelect.dataset.configuredValue = binarySensorSelect.value;
      } else {
        delete binarySensorSelect.dataset.configuredValue;
      }
      maybeFillTitleFromEntity(tab, '_binary_sensor_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(binarySensorPopupModeSelect, 'change', 'binarySensorPopupMode', () => {
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(weatherSelect, 'change', 'weatherEntity', () => { maybeFillTitleFromWeather(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(weatherPopupModeSelect, 'change', 'weatherPopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energySelect, 'change', 'energyEntity', () => {
      energySelect.dataset.configuredValue = energySelect.value || '';
      maybeFillTitleFromEnergy(tab);
      updateTilePreview(tab);
      updateEnergyValuePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(energyUnitInput, 'input', 'energyUnit', () => { updateEnergyValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energyDecimalsInput, 'input', 'energyDecimals', () => { updateEnergyValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energyValueFontSelect, 'change', 'energyValueFont', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energyPopupModeSelect, 'change', 'energyPopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energyValueYOffsetInput, 'input', 'energyValueYOffset', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(unitInput, 'input', 'sensorUnit', () => { updateSensorValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(decimalsInput, 'input', 'sensorDecimals', () => { updateSensorValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(valueFontSelect, 'change', 'sensorValueFont', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(sensorPopupModeSelect, 'change', 'sensorPopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(displayModeSelect, 'change', 'sensorDisplayMode', () => { syncGaugeUi(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(gaugeMinInput, 'input', 'sensorGaugeMin', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(gaugeMaxInput, 'input', 'sensorGaugeMax', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(gaugeArcInput, 'input', 'sensorGaugeArc', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(gaugeSizeInput, 'input', 'sensorGaugeSize', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(gaugeYOffsetInput, 'input', 'sensorGaugeYOffset', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(valueYOffsetInput, 'input', 'sensorValueYOffset', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(graphHeightInput, 'input', 'sensorGraphHeight', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(sceneInput, 'input', 'sceneAlias', () => { maybeFillTitleFromScene(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(textInput, 'input', 'textValue', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(textFontInput, 'change', 'textFont', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(navigateSelect, 'change', 'navigateTarget', () => {
      syncFolderPinControls(tab);
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(folderPinToggle, 'change', 'folderPinToggle', () => {
      const tile = tilesData?.[tab]?.[currentTileIndex];
      if (!folderPinToggle.checked && tile?.folder_pin_enabled === true) {
        syncFolderPinControls(tab);
        applyFolderPin(tab);
      } else {
        syncFolderPinControls(tab);
      }
    });
    bindLive(folderPinApply, 'click', 'folderPinApply', () => {
      applyFolderPin(tab);
    });
    bindLive(switchSelect, 'change', 'switchEntity', () => { maybeFillTitleFromSwitch(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(switchStyleSelect, 'change', 'switchStyle', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(switchPopupModeSelect, 'change', 'switchPopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(mediaSelect, 'change', 'mediaEntity', () => { maybeFillTitleFromMedia(tab); updateTilePreview(tab); updateMediaValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(climateSelect, 'change', 'climateEntity', () => {
      if (climateSelect.value) {
        climateSelect.dataset.configuredValue = climateSelect.value;
      } else {
        delete climateSelect.dataset.configuredValue;
      }
      maybeFillTitleFromEntity(tab, '_climate_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(climatePopupModeSelect, 'change', 'climatePopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(coverSelect, 'change', 'coverEntity', () => {
      if (coverSelect.value) {
        coverSelect.dataset.configuredValue = coverSelect.value;
      } else {
        delete coverSelect.dataset.configuredValue;
      }
      maybeFillTitleFromEntity(tab, '_cover_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(coverPopupModeSelect, 'change', 'coverPopupMode', () => {
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(cameraSelect, 'change', 'cameraEntity', () => {
      if (cameraSelect.value) {
        cameraSelect.dataset.configuredValue = cameraSelect.value;
      } else {
        delete cameraSelect.dataset.configuredValue;
      }
      maybeFillTitleFromEntity(tab, '_camera_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    climateSlotSelects.forEach((select, index) => {
      bindLive(select, 'change', 'climateSlot' + index, () => {
        syncClimateSlotFields(tab);
        updateTilePreview(tab);
        updateDraft(tab);
        scheduleAutoSave(tab);
      });
    });
    climateLayoutSelects.forEach((select, index) => {
      bindLive(select, 'change', 'climateLayout' + index, () => {
        updateTilePreview(tab);
        updateDraft(tab);
        scheduleAutoSave(tab);
      });
    });
    bindLive(animationSelect, 'change', 'animationFile', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(animationFpsInput, 'input', 'animationFps', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(animationFitSelect, 'change', 'animationFit', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(animationZoomInput, 'input', 'animationZoom', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(clockTimeCheck, 'change', 'clockShowTime', () => {
      ensureClockSelection(prefix);
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(clockDateCheck, 'change', 'clockShowDate', () => {
      ensureClockSelection(prefix);
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    if (clockTimeFontSelect) {
      const onClockTimeFontChanged = () => { updateClockValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); };
      bindLive(clockTimeFontSelect, 'change', 'clockTimeFont', onClockTimeFontChanged);
      bindLive(clockTimeFontSelect, 'input', 'clockTimeFont', onClockTimeFontChanged);
    }
    if (clockDateFontSelect) {
      const onClockDateFontChanged = () => { updateClockValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); };
      bindLive(clockDateFontSelect, 'change', 'clockDateFont', onClockDateFontChanged);
      bindLive(clockDateFontSelect, 'input', 'clockDateFont', onClockDateFontChanged);
    }
    if (clockTimeFormatSelect) {
      const onClockTimeFormatChanged = () => { updateClockValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); };
      bindLive(clockTimeFormatSelect, 'change', 'clockTimeFormat', onClockTimeFormatChanged);
      bindLive(clockTimeFormatSelect, 'input', 'clockTimeFormat', onClockTimeFormatChanged);
    }
    if (clockDateFormatSelect) {
      const onClockDateFormatChanged = () => { updateClockValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); };
      bindLive(clockDateFormatSelect, 'change', 'clockDateFormat', onClockDateFormatChanged);
      bindLive(clockDateFormatSelect, 'input', 'clockDateFormat', onClockDateFormatChanged);
    }
    if (settingsPanel && settingsPanel.dataset.clockLiveBound !== '1') {
      const delegatedClockRefresh = (e) => {
        const target = e && e.target;
        if (!target || !target.id) return;
        if (
          target.id === (prefix + '_clock_show_time') ||
          target.id === (prefix + '_clock_show_date')
        ) {
          ensureClockSelection(prefix);
          updateTilePreview(tab);
          updateDraft(tab);
          scheduleAutoSave(tab);
          return;
        }
        if (
          target.id === (prefix + '_clock_time_font') ||
          target.id === (prefix + '_clock_date_font') ||
          target.id === (prefix + '_clock_time_format') ||
          target.id === (prefix + '_clock_date_format')
        ) {
          updateClockValuePreview(tab);
          updateDraft(tab);
          scheduleAutoSave(tab);
          return;
        }
      };
      settingsPanel.addEventListener('change', delegatedClockRefresh);
      settingsPanel.addEventListener('input', delegatedClockRefresh);
      settingsPanel.dataset.clockLiveBound = '1';
    }
  }

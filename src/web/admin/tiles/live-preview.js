
  function updateTilePreview(tab) {
    if (currentTileIndex === -1) return;
    if (currentTileIndex === HIDDEN_SETTINGS_TILE_INDEX) {
      const snapshot = buildTileSnapshotFromInputs(tab);
      snapshot.type = '7';
      renderSettingsHiddenSlot(true, snapshot);
      return;
    }
    if (typeof parkClimateMiniEditor === 'function') {
      // A live change rebuilds the preview content. The selection of the mini
      // tile being edited has to survive that render cycle.
      parkClimateMiniEditor(tab, true);
    }
    const prefix = tab;
    const tileId = tab + '-tile-' + currentTileIndex;
    const tileElem = document.getElementById(tileId);
    if (!tileElem) return;

    const wasActive = currentTileTab === tab && currentTileIndex >= 0;
    const typeWas = tileElem.dataset.type || '0';
    const title = document.getElementById(prefix + '_tile_title').value;
    const color = document.getElementById(prefix + '_tile_color').value;
    const type = document.getElementById(prefix + '_tile_type').value;
    const meta = getTileTypeMeta(type);
    const iconInput = document.getElementById(prefix + '_tile_icon');
    const switchStyle = document.getElementById(prefix + '_switch_style')?.value || '0';
    const isEnergyType = type === '14';
    const sensorValueFont = isEnergyType
      ? (document.getElementById(prefix + '_energy_value_font')?.value || '0')
      : (document.getElementById(prefix + '_sensor_value_font')?.value || '0');
    const previewKind = meta.preview || 'none';
    const sensorValueClass = getSensorValueFontClass(isEditablePreview(previewKind)
      ? (document.getElementById(prefix + '_' + previewKind + '_value_font')?.value ?? '2') : sensorValueFont);
    const sensorEntity = document.getElementById(prefix + '_sensor_entity')?.value || '';
    const binarySensorEntity = document.getElementById(
      prefix + '_binary_sensor_entity')?.value || '';
    const energyEntity = document.getElementById(prefix + '_energy_entity')?.value || '';
    const weatherEntity = document.getElementById(prefix + '_weather_entity')?.value || '';
    const switchEntity = document.getElementById(prefix + '_switch_entity')?.value || '';
    const mediaEntity = document.getElementById(prefix + '_media_entity')?.value || '';
    const climateEntity = document.getElementById(prefix + '_climate_entity')?.value || '';
    const coverEntity = document.getElementById(prefix + '_cover_entity')?.value || '';
    const cameraEntity = document.getElementById(prefix + '_camera_entity')?.value || '';
    let iconEntity = (previewKind === 'sensor')
      ? (isEnergyType ? energyEntity : sensorEntity)
      : (previewKind === 'binary_sensor'
        ? binarySensorEntity
      : (previewKind === 'switch'
        ? switchEntity
        : (previewKind === 'weather'
          ? weatherEntity
          : (previewKind === 'media'
            ? mediaEntity
            : (previewKind === 'climate'
              ? climateEntity
              : (previewKind === 'cover'
                ? coverEntity
                : (previewKind === 'camera' ? cameraEntity : '')))))));
    if (isEditablePreview(previewKind)) iconEntity = document.getElementById(prefix + '_' + previewKind + '_entity')?.value || '';
    const rawIcon = iconInput ? iconInput.value : '';
    let iconName = resolveIconName(
      rawIcon,
      iconEntity,
      sensorMetaCache.icons);
    if (previewKind === 'camera' && !iconName &&
        !isExplicitlyDisabledValue(rawIcon)) {
      iconName = 'video';
    }
    let climatePreviewState = null;
    if (previewKind === 'climate') {
      climatePreviewState = parseClimatePreviewPayload(
        climateEntity ? (sensorMetaCache.values[climateEntity] ?? '') : '');
      if (!normalizeMdiIconName(rawIcon) &&
          !isExplicitlyDisabledValue(rawIcon)) {
        iconName = climatePreviewIcon(climatePreviewState, iconName);
      }
    }
    let coverPreviewState = null;
    if (previewKind === 'cover') {
      coverPreviewState = parseCoverPreviewPayload(
        coverEntity ? (sensorMetaCache.values[coverEntity] ?? '') : '');
      if (!normalizeMdiIconName(rawIcon) &&
          !isExplicitlyDisabledValue(rawIcon)) {
        iconName = coverPreviewIcon(coverPreviewState, iconName);
      }
    }
    let binarySensorPreviewState = null;
    if (previewKind === 'binary_sensor') {
      binarySensorPreviewState = parseBinarySensorPreviewPayload(
        binarySensorEntity
          ? (sensorMetaCache.values[binarySensorEntity] ?? '') : '');
      iconName = resolveBinarySensorPreviewIcon(
        rawIcon, binarySensorEntity, binarySensorPreviewState,
        sensorMetaCache.icons);
    }

    tileElem.className = 'tile';
    if (meta.css) tileElem.classList.add(meta.css);
    if (type === '5' && switchStyle === '1') tileElem.classList.add('switch-toggle');
    tileElem.style.background = '';
    tileElem.dataset.type = type;

    if (type === '0') {
      tileElem.classList.add('empty');
      tileElem.style.background = 'transparent';
      tileElem.innerHTML = '';
      applyTileAriaLabel(tileElem, '', type);
      if (wasActive) tileElem.classList.add('active');
      updateLayoutFromInputs(tab);
      return;
    }

    const defaultBg = meta.defaultBg || '#353535';
    if (tileColorInputIsDefault(tab)) {
      const colorInput = document.getElementById(prefix + '_tile_color');
      if (colorInput) colorInput.value = defaultBg;
    }
    const tileBg = tileColorInputIsDefault(tab) ? defaultBg : (color || defaultBg);
    if (isScreensaverTileTab(tab)) {
      const opacity = clampInt(
        document.getElementById('screensaver_tile_opacity')?.value,
        0, 255, 0);
      tileElem.style.background = tileBg + opacity.toString(16).padStart(2, '0');
    } else {
      tileElem.style.background = tileBg;
    }
    tileElem.style.removeProperty('--switch-knob-color');
    tileElem.style.removeProperty('--switch-on-color');
    if (type === '5' && switchStyle === '1') {
      tileElem.style.setProperty('--switch-knob-color', tileBg);
      tileElem.style.setProperty('--switch-on-color', '#3B82F6');
    }

    let html = '';

    if (iconName) {
      const iconStyle = previewKind === 'climate'
        ? ' style="color:' + climatePreviewColor(climatePreviewState) + '"'
        : (previewKind === 'cover'
          ? ' style="color:' + coverPreviewColor(coverPreviewState) + '"'
          : (previewKind === 'binary_sensor'
            ? ' style="color:' + binarySensorPreviewColor(
                binarySensorPreviewState) + '"'
            : ''));
      html += '<i class="mdi mdi-' + escapeHtml(iconName) + ' tile-icon"' + iconStyle + '></i>';
    }

    let displayTitle = title;
    if (previewKind === 'camera' && !displayTitle && cameraEntity) {
      displayTitle = sensorMetaCache.names[cameraEntity] ||
        titleFromEntity(cameraEntity);
    }
    if (displayTitle) {
      html += '<div class="tile-title" id="' + tileId + '-title">' +
        tileTitleHtml(displayTitle) + '</div>';
    }
    applyTileAriaLabel(tileElem, displayTitle, type);

    if (previewKind === 'weather') {
      html += '<div class="tile-ghost-icon"><i class="mdi mdi-weather-partly-cloudy"></i></div>';
    }
    if (previewKind === 'media') {
      html += '<div class="tile-ghost-icon"><i class="mdi mdi-music"></i></div>';
    }
    if (previewKind === 'climate') {
      const climateSpanW = document.getElementById(
        prefix + '_tile_span_w')?.value || 1;
      const climateSpanH = document.getElementById(
        prefix + '_tile_span_h')?.value || 1;
      html += climatePreviewSlots(
        climatePreviewState, climateSpanW, climateSpanH,
        currentClimateSlotConfig(tab),
        currentClimateTargetLayouts(tab),
        currentClimateGeometry(tab));
    }
    if (previewKind === 'cover') {
      const value = coverPreviewState?.position !== null &&
                    coverPreviewState?.position !== undefined
        ? String(coverPreviewState.position) + '%' : '--%';
      html += '<div class="tile-value tile-cover-value">' +
        escapeHtml(coverPreviewStateText(coverPreviewState)) +
        '<br>' + escapeHtml(value) + '</div>';
    }
    if (previewKind === 'binary_sensor') {
      html += '<div class="tile-value tile-binary-sensor-value" id="' +
        tileId + '-value">' +
        escapeHtml(binarySensorPreviewStateText(binarySensorPreviewState)) +
        '</div>';
    }

    if (isEditablePreview(previewKind)) html += '<div class="tile-value tile-editable-value ' + sensorValueClass + '">' + escapeHtml(editablePreviewText(iconEntity, previewKind)) + '</div>';

    if (previewKind === 'sensor') {
      const entitySelect = document.getElementById(prefix + (isEnergyType ? '_energy_entity' : '_sensor_entity'));
      const unitInput = document.getElementById(prefix + (isEnergyType ? '_energy_unit' : '_sensor_unit'));
      const entity = entitySelect ? entitySelect.value : '';
      const unit = resolveUnitValue(unitInput ? unitInput.value : '', entity, sensorMetaCache.units);
      html += '<div class="tile-value ' + sensorValueClass + '" id="' + tileId + '-value">--';
      if (unit) html += '<span class="tile-unit">' + escapeHtml(unit) + '</span>';
      html += '</div>';
      if (entity) {
        tileElem.innerHTML = html;
        if (wasActive) tileElem.classList.add('active');
        if (isEnergyType) updateEnergyValuePreview(tab);
        else updateSensorValuePreview(tab);
      }
    }

    if (previewKind === 'clock') {
      const flags = getClockFlagsFromInputs(prefix);
      const clockTimeFont = document.getElementById(prefix + '_clock_time_font')?.value || '40';
      const clockDateFont = Math.min(72,
        Number(document.getElementById(prefix + '_clock_date_font')?.value || 20));
      const clockTimeFormat = document.getElementById(prefix + '_clock_time_format')?.value || '0';
      const clockDateFormat = document.getElementById(prefix + '_clock_date_format')?.value || '0';
      if (flags & 1) html += '<div class="tile-clock-time" ' + getClockPreviewTextStyle(clockTimeFont, 40, '#fff') + '>' + getClockPreviewTime(clockTimeFormat) + '</div>';
      if (flags & 2) html += '<div class="tile-clock-date" ' + getClockPreviewTextStyle(clockDateFont, 24, '#fff') + '>' + getClockPreviewDate(clockDateFormat) + '</div>';
    }

    if (previewKind === 'text') {
      const textValue = document.getElementById(prefix + '_text_value')?.value || '';
      if (textValue) {
        const textFont = document.getElementById(prefix + '_text_value_font')?.value || '0';
        const textClass = getSensorValueFontClass(textFont);
        html += '<div class="tile-text ' + textClass + '">' +
          escapeHtml(textValue) + '</div>';
      }
    }

    if (previewKind === 'switch' && switchStyle === '1') {
      html += '<div class="tile-switch" id="' + tileId + '-switch"><div class="tile-switch-knob"></div></div>';
    }

    html += getTileResizeHandlesHtml(type);
    tileElem.innerHTML = html;
    if (wasActive) tileElem.classList.add('active');
    if (typeWas !== type && wasActive) {
      tileElem.classList.add('active');
      const settingsId = tab + 'Settings';
      document.getElementById(settingsId)?.classList.remove('hidden');
    }
    if (type === '5') updateSwitchValuePreview(tab);
    updateLayoutFromInputs(tab);
    if (previewKind === 'climate' &&
        typeof mountClimateMiniEditor === 'function') {
      mountClimateMiniEditor(tab);
      syncClimateSlotFields(tab);
    }
  }

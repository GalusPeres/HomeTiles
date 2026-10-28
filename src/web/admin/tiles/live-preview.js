
  // globalThis.hometilesPreviewForcedSurface: the second pass of a preview
  // whose tint changed its surface (tile_surface::change), drawn from the
  // start with that surface.
  function updateTilePreview(tab) {
    // The edited tile draws with its surface's rules (tile_surface), set
    // below once its color is known; restored at the end.
    const surfaceSaved = globalThis.hometilesPreviewSurface;
    let surfaceDrawn = null;
    try {
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
    // A page's tile is drawn as one cell in its field; its stored size
    // stays in the size fields (page-fields.js).
    const pageTile = tileElem.classList.contains('page-field-tile');
    const tileSpan = name => pageTile ? 1 : Number(document.getElementById(prefix + '_tile_' + name)?.value || 1);
    // The edited tile's size, and for a Sensor look its gauge range and
    // color record from the editor (admin-look.js setSensorLookData).
    const sensorLookLayout = () => {
      const layout = {span_w: tileSpan('span_w'), span_h: tileSpan('span_h')};
      if (document.getElementById(prefix + '_tile_type')?.value !== '1') return layout;
      const number = id => {
        const raw = document.getElementById(prefix + id)?.value;
        return raw === undefined || String(raw).trim() === '' ? undefined : Number(raw);
      };
      layout.sensor_gauge_min = number('_sensor_gauge_min');
      layout.sensor_gauge_max = number('_sensor_gauge_max');
      layout.icon_colors = typeof collectIconColorRecord === 'function' ? collectIconColorRecord(tab) : '';
      return layout;
    };

    const wasActive = currentTileTab === tab && currentTileIndex >= 0;
    const typeWas = tileElem.dataset.type || '0';
    const title = document.getElementById(prefix + '_tile_title').value;
    const color = document.getElementById(prefix + '_tile_color').value;
    const type = document.getElementById(prefix + '_tile_type').value;
    const meta = getTileTypeMeta(type);
    const iconInput = document.getElementById(prefix + '_tile_icon');
    const switchStyle = document.getElementById(prefix + '_switch_style')?.value || '0';
    // The bar upright (bar_orientation.h): Switch and Fan.
    const switchUpright = document.getElementById(prefix + '_switch_orientation')?.value === '1';
    const fanUpright = document.getElementById(prefix + '_fan_orientation')?.value === '1';
    const isEnergyType = type === '14';
    // Half-height tiles offer only the value sizes that fit.
    const halfHeight = tileSpan('span_h') === 0.5;
    for (const id of ['_binary_sensor_value_font', '_energy_value_font'])
      syncCompactValueFontOptions(document.getElementById(prefix + id), halfHeight);
    // A Sensor graph tile shows its value in the header row beside the disc
    // like a half-height tile (types/sensor/renderer.cpp).
    const sensorGraph = type === '1' && document.getElementById(prefix + '_sensor_display_mode')?.value === '2';
    syncCompactValueFontOptions(document.getElementById(prefix + '_sensor_value_font'), halfHeight || sensorGraph);
    for (const kind of ['number', 'select', 'datetime'])
      syncEditableValueFontOptions(document.getElementById(prefix + '_' + kind + '_value_font'), halfHeight);
    if (type === '5') {
      // The state beside the disc takes the half-height sizes, the large
      // state of a tall tile the full-size ones.
      const spanH = tileSpan('span_h');
      syncCompactValueFontOptions(document.getElementById(prefix + '_switch_value_font'),
                                  !switchSensorLook(switchStyle, spanH, switchUpright));
      syncSwitchChoices(tab);
    }
    const deviceKind = typeof devicePreviewKind === 'function' ? devicePreviewKind(type) : '';
    if (type === '19' || deviceKind) {
      // Like the Switch: beside the disc the half-height sizes, from 1.5 rows
      // the full-size ones (tile_header.h).
      const spanH = tileSpan('span_h');
      syncCompactValueFontOptions(document.getElementById(prefix + (deviceKind ? '_' + deviceKind : '_cover') +
        '_value_font'), !(spanH > 1 && !(deviceKind === 'fan' && fanUpright)));
      syncSwitchChoices(tab);
    }
    const previewKind = meta.preview || 'none';
    // Number, Select and Date/Time keep their own value size field.
    const sensorValueFont = isEditablePreview(previewKind)
      ? (document.getElementById(prefix + '_' + previewKind + '_value_font')?.value ?? '2')
      : isEnergyType
      ? (document.getElementById(prefix + '_energy_value_font')?.value || '0')
      : (document.getElementById(prefix + (type === '20' ? '_binary_sensor_value_font'
        : (type === '5' ? '_switch_value_font' : (type === '19' ? '_cover_value_font'
          : (deviceKind ? '_' + deviceKind + '_value_font' : '_sensor_value_font')))))?.value || '0');
    const sensorValueClass = getSensorValueFontClass(sensorValueFont);
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
    if (deviceKind) iconEntity = document.getElementById(prefix + '_' + deviceKind + '_entity')?.value || '';
    const calendarTile = previewKind === 'calendar' ? calendarTileFromInputs(tab, tileSpan('span_w'), tileSpan('span_h')) : null;
    if (type === '2') {
      const alias = document.getElementById(prefix + '_scene_alias')?.value || '';
      iconEntity = sceneEntityOf(alias);
    }
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
    let devicePreviewState = null;
    if (deviceKind) {
      devicePreviewState = parseDevicePreviewPayload(deviceDetailPayload(iconEntity));
      if (!normalizeMdiIconName(rawIcon) && !isExplicitlyDisabledValue(rawIcon)) {
        iconName = devicePreviewIcon(deviceKind, devicePreviewState);
      }
    }
    if (calendarTile && !normalizeMdiIconName(rawIcon) && !isExplicitlyDisabledValue(rawIcon)) iconName = 'calendar';
    let binarySensorPreviewState = null;
    if (previewKind === 'binary_sensor') {
      binarySensorPreviewState = parseBinarySensorPreviewPayload(
        binarySensorEntity
          ? (sensorMetaCache.values[binarySensorEntity] ?? '') : '');
      iconName = resolveBinarySensorPreviewIcon(
        rawIcon, binarySensorEntity, binarySensorPreviewState,
        sensorMetaCache.icons);
    }
    if (previewKind === 'media') {
      iconName = mediaPreviewIcon(parseMediaPreviewPayload(
        mediaEntity ? (sensorMetaCache.mediaValues?.[mediaEntity] ?? '') : ''), iconName);
    }

    resetTileClasses(tileElem, meta.css ? ['tile', meta.css] : ['tile']);
    if (type === '5') applySwitchPreviewLayout(tileElem, switchStyle, halfHeight, switchUpright);
    tileElem.style.background = '';
    delete tileElem.dataset.bgOpacity;
    tileElem.dataset.type = type;
    // Every circle shows, in its icon's color (user 2026-10-11).
    tileElem.dataset.iconDisc = '0';
    tileElem.dataset.iconGlow = '1';
    const borderToggle = type === '8' ? '_back_tile_border'
      : (type === '9' ? '_clock_tile_border' : (type === '10' ? '_text_tile_border' : ''));
    tileElem.classList.toggle('tile-border-hidden', !!borderToggle && document.getElementById(prefix + borderToggle)?.checked === false);

    if (type === '0') {
      tileElem.classList.add('empty');
      tileElem.style.background = 'transparent';
      tileElem.innerHTML = '';
      applyTileAriaLabel(tileElem, '', type);
      if (wasActive) tileElem.classList.add('active');
      updateLayoutFromInputs(tab);
    applyCompactSensorPreview(tileElem, type, sensorLookLayout(),
      document.getElementById(prefix + '_sensor_display_mode')?.value || 0, sensorValueFont);
      return;
    }

    const defaultBg = meta.defaultBg || '#353535';
    // Tiles without their own color (or with the stored default grey) show
    // the global default tile color. Only the Tile color buttons change the
    // choice: a Custom color that is still a default grey (Custom was just
    // selected and nothing picked yet) stays Custom instead of switching back
    // to Global and hiding the color field.
    const isDefaultBg = tileColorInputIsDefault(tab) || !tileTypeKeepsOwnColor(type);
    const colorInput = document.getElementById(prefix + '_tile_color');
    if (colorInput?.dataset.bgColorDefault === '1') colorInput.value = defaultBg;
    syncTileColorMode(tab);
    const tileBg = tileBackgroundCss(meta, isDefaultBg,
      isDefaultBg ? defaultBg : (color || defaultBg), null, type);
    tileElem.classList.toggle('tile-transparent', tileBg === 'transparent');
    if (isScreensaverTileTab(tab)) {
      // One opacity for every screensaver tile (screensaver footer).
      const opacity = screensaverTileOpacity();
      tileElem.style.background = tileBackgroundCss(meta, isDefaultBg,
        isDefaultBg ? defaultBg : (color || defaultBg), opacity);
      tileElem.dataset.bgOpacity = String(opacity);
      // A fully transparent card casts no shadow (apply_slot_tile_shadows).
      tileElem.classList.toggle('screensaver-bg-clear', opacity === 0);
    } else {
      tileElem.style.background = tileBg;
    }
    // The tile's surface (tile_surface): own colors and the dark design.
    if (typeof tileSurfaceOf === 'function' && typeof applyTileSurface === 'function') {
      const record = typeof collectIconColorRecord === 'function' ? collectIconColorRecord(prefix) : '';
      const bgValue = isDefaultBg ? 0 : makeTileBgValue(hexToRgb(color || defaultBg));
      // A Camera tile's head over its picture: its fixed surface on that
      // theme's tile color (picture.h head_over_picture).
      const cameraHead = meta.preview === 'camera' ? cameraHeadSurface(cameraPictureOptions(tab), halfHeight) : '';
      if (cameraHead) tileElem.style.background = cameraHeadBackground(cameraHead);
      const surface = globalThis.hometilesPreviewForcedSurface ||
        (cameraHead ? (cameraHead === 'auto' ? '' : cameraHead) : tileSurfaceOf(tab, meta.preview || '', bgValue, record, type));
      if (!cameraHead && tileFollowsTint(tab, meta.preview || '', bgValue, record, type)) tileElem.dataset.followTint = '1';
      else delete tileElem.dataset.followTint;
      // A Media tile and a tile "From icon" show the neutral grey without a
      // tint (kNeutralIdle).
      if (tileElem.dataset.followTint === '1' && tileFillOf(record) > 0) {
        tileElem.dataset.neutralIdle = '1';
      } else {
        delete tileElem.dataset.neutralIdle;
      }
      surfaceDrawn = surface;
      applyTileSurface(tileElem, surface);
      applyFrameLine(tileElem);
      const surfaceMode = typeof surfaceTheme === 'function' ? surfaceTheme(surface) : -1;
      // 0 dark, 1 light, 2 a filled tile (ui_theme::kDarkTinted): its rules everywhere.
      if (surfaceMode >= 0) globalThis.hometilesPreviewSurface = surfaceMode;
    }

    let html = '';
    const locked = typeof previewTileLocked === 'function' && previewTileLocked(type, tileElem);
    const lockIsIcon = locked && !iconName;
    if (lockIsIcon) iconName = 'lock';

    delete tileElem.dataset.ruleFill;
    if (iconName) {
      const iconRecord = typeof collectIconColorRecord === 'function' ? collectIconColorRecord(prefix) : '';
      const ruleFill = {};
      const iconColor = previewIconColor(type, iconRecord, iconEntity, sensorMetaCache,
        binarySensorPreviewState, previewKind === 'climate'
          ? climatePreviewColor(climatePreviewState)
          : (previewKind === 'cover'
            ? coverPreviewColor(coverPreviewState)
            : (deviceKind
              ? devicePreviewColor(deviceKind, devicePreviewState)
              : (previewKind === 'binary_sensor'
                ? binarySensorPreviewColor(binarySensorPreviewState)
                : (calendarTile ? calendarPreviewIconColor(calendarTile) : '')))), ruleFill);
      if (typeof setPreviewRuleFill === 'function') setPreviewRuleFill(tileElem, ruleFill);
      const iconStyle = iconColor ? ' style="color:' + escapeHtml(iconColor) + '"' : '';
      html += '<i class="mdi mdi-' + escapeHtml(iconName) + ' tile-icon"' + iconStyle + '>' +
        (locked && !lockIsIcon ? PREVIEW_LOCK_MARK : '') + '</i>';
    }

    let displayTitle = title;
    if (previewKind === 'camera' && !displayTitle && cameraEntity) {
      displayTitle = sensorMetaCache.names[cameraEntity] ||
        titleFromEntity(cameraEntity);
    }
    if (calendarTile) displayTitle = calendarPreviewTitle(calendarTile, displayTitle);
    if (displayTitle) {
      html += '<div class="tile-title" id="' + tileId + '-title">' +
        tileTitleHtml(displayTitle) + '</div>';
    }
    applyTileAriaLabel(tileElem, displayTitle, type);

    if (previewKind === 'climate') {
      const climateSpanW = tileSpan('span_w');
      const climateSpanH = tileSpan('span_h');
      // Layout "with value" and half height: the value pair beside the
      // disc; half height has no mini fields.
      const climateHalf = Number(climateSpanH) === 0.5;
      const climateValue = document.getElementById(prefix + '_climate_view')?.value === '1';
      tileElem.classList.toggle('climate-header', climateValue && !climateHalf);
      if (climateHalf || climateValue) {
        html += '<div class="tile-value tile-switch-state">' +
          unitTextHtml(climatePreviewHeaderText(climatePreviewState)) + '</div>';
      }
      if (!climateHalf) {
        html += climatePreviewSlots(
          climatePreviewState, climateSpanW, climateSpanH,
          currentClimateSlotConfig(tab),
          currentClimateTargetLayouts(tab),
          currentClimateGeometry(tab),
          climateValue);
      }
    }
    if (previewKind === 'cover') html += coverPreviewExtraHtml(coverPreviewState, halfHeight);
    if (deviceKind) html += devicePreviewExtraHtml(deviceKind, devicePreviewState, halfHeight);
    if (calendarTile) html += calendarPreviewExtraHtml(calendarTile);
    if (previewKind === 'binary_sensor') {
      html += '<div class="tile-value tile-binary-sensor-value ' + (Number(sensorValueFont) ? sensorValueClass : '') + '" id="' +
        tileId + '-value">' +
        escapeHtml(binarySensorPreviewStateText(binarySensorPreviewState)) +
        '</div>';
    }

    if (isEditablePreview(previewKind)) html += '<div class="tile-value tile-editable-value ' + sensorValueClass + '">' + editablePreviewHtml(iconEntity, previewKind) + '</div>';

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
      const clockWeekday = Number(document.getElementById(prefix + '_clock_weekday')?.value || 0);
      applyClockPreviewAlign(tileElem, document.getElementById(prefix + '_clock_align')?.value || 0);
      let clockLines = '';
      if (flags & 1) clockLines += '<div class="tile-clock-time" ' + getClockPreviewTextStyle(clockTimeFont, 40, panelRoleHex('text', '#fff')) + '>' + clockDigitsHtml(getClockPreviewTime(clockTimeFormat)) + '</div>';
      if ((flags & 2) || clockWeekday) clockLines += '<div class="tile-clock-date" ' + getClockPreviewTextStyle(clockDateFont, 20, panelRoleHex('text', '#fff')) + '>' + clockDigitsHtml(getClockPreviewDate(clockDateFormat, clockWeekday, (flags & 2) !== 0)) + '</div>';
      // The analog and the wall clock (grid-preview.js, layoutClockFacePreview).
      const clockStyle = Number(document.getElementById(prefix + '_clock_style')?.value || 0);
      tileElem.dataset.clockOptions = String(packClockDateOptions(clockDateFormat, clockWeekday, false,
        document.getElementById(prefix + '_clock_align')?.value || 0, clockStyle));
      if (clockStyle) {
        const packed = packClockDateOptions(clockDateFormat, clockWeekday, false, 0, clockStyle,
          document.getElementById(prefix + '_clock_dial')?.value || 0, document.getElementById(prefix + '_clock_hands')?.value || 0);
        html += '<svg class="tile-clock-face" data-options="' + packed + '"></svg><div class="tile-clock-face-text">' + clockLines + '</div>';
      } else {
        html += clockLines;
      }
    }

    if (previewKind === 'text') {
      applyTextPreviewAlign(tileElem, packTextTileOptions(document.getElementById(prefix + '_text_align')?.value || 0,
        document.getElementById(prefix + '_text_valign')?.value || 0));
      const textValue = document.getElementById(prefix + '_text_value')?.value || '';
      if (textValue) {
        const textFont = document.getElementById(prefix + '_text_value_font')?.value || '0';
        const textClass = getSensorValueFontClass(textFont);
        html += '<div class="tile-text ' + textClass + '">' +
          escapeHtml(textValue) + '</div>';
      }
    }

    if (previewKind === 'switch') html += switchPreviewExtraHtml(switchStyle, halfHeight);

    html += getTileResizeHandlesHtml(type);
    // A title or icon moves the clock down (clock/renderer.cpp).
    if (type === '9') tileElem.classList.toggle('clock-has-header', !!(displayTitle || iconName));
    tileElem.innerHTML = html;
    if (previewKind === 'weather') {
      const iconRecord = typeof collectIconColorRecord === 'function' ? collectIconColorRecord(prefix) : '';
      applyWeatherPreview(tileElem, applyWeatherSensorOverrides(parseWeatherPreviewPayload(
        weatherEntity ? (sensorMetaCache.weatherValues?.[weatherEntity] ?? '') : ''),
        document.getElementById(prefix + '_weather_temperature_sensor')?.value || '',
        document.getElementById(prefix + '_weather_humidity_sensor')?.value || '',
        sensorMetaCache.values, sensorMetaCache.units), {
        col: Number(tileElem.dataset.col || 0),
        span_w: tileSpan('span_w'),
        span_h: tileSpan('span_h'),
        sensor_display_mode: document.getElementById(prefix + '_weather_colored_icons')?.checked === false ? 1 : 0
      }, iconName, previewIconColor(type, iconRecord, weatherEntity, sensorMetaCache, null, ''));
    }
    if (previewKind === 'media') {
      applyMediaPreview(tileElem, parseMediaPreviewPayload(
        mediaEntity ? (sensorMetaCache.mediaValues?.[mediaEntity] ?? '') : ''), {
        sensor_entity: mediaEntity,
        span_w: tileSpan('span_w'),
        span_h: tileSpan('span_h')
      }, iconName, mediaEntity ? (sensorMetaCache.names?.[mediaEntity] || '') : '');
    }
    if (previewKind === 'energy_flow' && typeof applyEnergyFlowPreview === 'function') {
      applyEnergyFlowPreview(tileElem, {
        flow_entities: energyFlowRecordFromInputs(prefix),
        key_code: energyFlowOptionsFromInputs(prefix),
        span_w: tileSpan('span_w'),
        span_h: tileSpan('span_h'),
        title,
        icon_name: iconInput?.value || ''
      }, sensorMetaCache);
    } else {
      tileElem.classList.remove('energy-flow-tile', 'energy-flow-no-card');
    }
    if (typeof applyTileRulesTint === 'function' && typeof collectIconColorRecord === 'function' &&
        typeof iconColorOwnEntity === 'function') {
      applyTileRulesTint(tileElem, type, collectIconColorRecord(prefix), iconColorOwnEntity(prefix, String(type)), sensorMetaCache);
    }
    if (previewKind === 'media' && typeof collectIconColorRecord === 'function') {
      applyMediaCoverTint(tileElem, collectIconColorRecord(prefix), sensorMetaCache.mediaCoverColors?.[mediaEntity] || '');
      if (tileElem.dataset.neutralIdle === '1' && tileElem.dataset.surface !== 'jewel') {
        setTileTintBackground(tileElem, '#FFFFFF', 0, true);
      }
    }
    applyIconDiscTint(tileElem);
    if (previewKind === 'camera') {
      applyCameraPicturePreview(tileElem, cameraPictureOptions(tab), halfHeight, {
        col: Number(tileElem.dataset.col || 0),
        span_w: tileSpan('span_w')});
    }
    if (previewKind === 'cover') applyCoverPreview(tileElem, coverPreviewState, halfHeight);
    if (deviceKind) applyDevicePreview(tileElem, deviceKind, devicePreviewState, halfHeight, fanUpright);
    if (wasActive) tileElem.classList.add('active');
    if (typeWas !== type && wasActive) {
      tileElem.classList.add('active');
      const settingsId = tab + 'Settings';
      document.getElementById(settingsId)?.classList.remove('hidden');
    }
    if (type === '5') updateSwitchValuePreview(tab);
    updateLayoutFromInputs(tab);
    applyCompactSensorPreview(tileElem, type, sensorLookLayout(),
      document.getElementById(prefix + '_sensor_display_mode')?.value || 0, sensorValueFont);
    // The analog and the wall clock place their face once the tile has its
    // size and classes again (updateLayoutFromInputs, applyCompactSensorPreview):
    // before them the tile measured 1 x 1 in the middle of its rebuild, and the
    // resize observer saw no change at the end of the task (user 2026-10-12:
    // a click on the clock shrank its face into the corner).
    if (previewKind === 'clock' && typeof layoutClockFacePreview === 'function') layoutClockFacePreview(tileElem);
    if (previewKind === 'climate' &&
        tileSpan('span_h') !== 0.5 &&
        typeof mountClimateMiniEditor === 'function') {
      mountClimateMiniEditor(tab);
      syncClimateSlotFields(tab);
    }
    // The automatic icon of the head follows what the preview draws.
    if (typeof renderTileIconButton === 'function') renderTileIconButton(tab);
    // A folder's tab and its page's head follow its tile's title and icon
    // while typing (the panel names the folder after its tile; user
    // 2026-10-14: they waited for the save, a folder not opened yet even for
    // its page from the panel).
    if (type === '4' && typeof updateFolderTabUi === 'function') {
      const folder = Number(document.getElementById(prefix + '_navigate_target')?.value || tileElem.dataset.navigateTarget || 0);
      if (folder > 0) updateFolderTabUi(folder, title.replace(/\s*\n\s*/g, ' '), iconInput?.value || '');
    }
    } finally {
      globalThis.hometilesPreviewSurface = surfaceSaved;
    }
    // Its tint changed the surface: drawn again on it from the start.
    const tileElem = currentTileIndex >= 0 ? document.getElementById(tab + '-tile-' + currentTileIndex) : null;
    if (!globalThis.hometilesPreviewForcedSurface && surfaceDrawn !== null && surfaceDrawn !== 'jewel' &&
        tileElem?.dataset.surface === 'jewel') {
      globalThis.hometilesPreviewForcedSurface = 'jewel';
      try { updateTilePreview(tab); } finally { delete globalThis.hometilesPreviewForcedSurface; }
    }
  }

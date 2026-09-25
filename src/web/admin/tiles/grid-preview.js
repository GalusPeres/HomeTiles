
  function rgbToHex(rgb) {
    const num = Number(rgb);
    const masked = Number.isFinite(num) ? (num & 0xFFFFFF) : 0;
    return '#' + ('000000' + masked.toString(16)).slice(-6);
  }
  function hexToRgb(hex) {
    const parsed = parseInt(String(hex || '').replace('#', ''), 16);
    return isNaN(parsed) ? 0 : (parsed & 0xFFFFFF);
  }
  function makeTileBgValue(rgb) {
    return (Number(rgb) & 0xFFFFFF) | 0x01000000;
  }
  function tileBgValueIsSet(value) {
    const num = Number(value);
    return Number.isFinite(num) && num !== 0;
  }
  function tileBgToHex(value, fallback) {
    const num = Number(value);
    if (!Number.isFinite(num) || num === 0) return fallback || '#353535';
    return rgbToHex(num);
  }
  // Tiles without their own color follow the global default tile color. The
  // preview paints them through one root variable, so a change of that color
  // repaints loaded, cached and lazily inserted grids at once.
  function tileBackgroundCss(meta, isDefault, hex, opacity = null) {
    const shared = !!isDefault && !!meta?.sharedBg;
    const sharedCss = 'var(--tile-default-bg, #2A2A2A)';
    if (opacity === null || opacity === undefined) return shared ? sharedCss : hex;
    if (shared) {
      return 'color-mix(in srgb, ' + sharedCss + ' ' +
        (opacity * 100 / 255).toFixed(2) + '%, transparent)';
    }
    return hex + opacity.toString(16).padStart(2, '0');
  }
  // Mirrors tile_icon_disc::icon_color_tints(): with glow on, a colored icon
  // tints its disc with its own hue; white and grey icons keep the white disc.
  function iconDiscTinted(color) {
    const rgb = String(color || '').match(/(\d+)\D+(\d+)\D+(\d+)/);
    return !!rgb && !(rgb[1] === rgb[2] && rgb[2] === rgb[3]);
  }
  function applyIconDiscTint(tileElem) {
    const icon = tileElem?.querySelector(':scope > .tile-icon');
    if (!icon) return;
    const glow = tileElem.dataset.iconGlow !== '0';
    icon.classList.toggle('tile-icon-tinted', glow && iconDiscTinted(getComputedStyle(icon).color));
    // Mirrors tile_icon_disc::contrast_step_for()/scaled_opa(): discs are
    // subtler on dark tiles (8 % instead of 15 % at luma <= 0.08) in 4 steps.
    const bg = String(getComputedStyle(tileElem).backgroundColor || '').match(/(\d+)\D+(\d+)\D+(\d+)/);
    const luma = bg ? (0.2126 * Number(bg[1]) + 0.7152 * Number(bg[2]) + 0.0722 * Number(bg[3])) / 255 : 1;
    const step = Math.floor(Math.min(1, Math.max(0, (luma - 0.08) / 0.17)) * 3 + 0.5);
    const scaled = full => Math.floor((full * (24 + 7 * step) + 22) / 45);
    tileElem.style.setProperty('--icon-disc-opa', (scaled(38) / 255).toFixed(3));
    tileElem.style.setProperty('--icon-disc-glow', (scaled(51) * 100 / 255).toFixed(1) + '%');
  }
  // Mirrors tileBgColorFollowsDefault(): an unset color and the built-in
  // default grey (stored explicitly by older editors) follow the global
  // default tile color; every other stored color is kept.
  // Built-in default greys: tile_color::kDefault and kLegacyDefault.
  function isDefaultTileGrey(rgb) {
    return rgb === 0x222222 || rgb === 0x2A2A2A;
  }
  function tileBgFollowsDefault(value) {
    const num = Number(value);
    return !Number.isFinite(num) || num === 0 || isDefaultTileGrey(num & 0xFFFFFF);
  }
  function tileColorHexIsDefaultGrey(hex) {
    const text = String(hex || '').trim();
    return /^#[0-9a-f]{6}$/i.test(text) && isDefaultTileGrey(parseInt(text.slice(1), 16));
  }
  // "Use global color" mirrors whether the tile follows the global color.
  function syncTileColorGlobalToggle(tab) {
    const box = document.getElementById(tab + '_tile_color_global');
    if (box) box.checked = tileColorInputIsDefault(tab);
  }
  // Checked: the tile follows the global tile color (stored as the default
  // marker). Unchecked: the tile keeps the color shown in the Color field.
  function toggleTileGlobalColor(tab, useGlobal) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    if (useGlobal) {
      const type = document.getElementById(tab + '_tile_type')?.value || '0';
      input.value = getTileTypeMeta(type).defaultBg || '#222222';
    }
    input.dataset.bgColorDefault = useGlobal ? '1' : '0';
    syncTileColorGlobalToggle(tab);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }
  // State the firmware compares with the per-tile icon color rules, or null
  // while it is missing, unknown or unavailable (the type color applies).
  function iconColorRuleState(typeValue, entity, meta, binaryState) {
    const type = String(typeValue ?? '0');
    if (type === '20') {
      if (!binaryState?.valid || binaryState.available !== true ||
          !['on', 'off'].includes(binaryState.state)) return null;
      return { state: binaryState.state, display: binarySensorPreviewStateText(binaryState) };
    }
    if (['21', '22', '23'].includes(type)) {
      let value = meta?.editableValues?.[entity];
      if (typeof value === 'string') { try { value = JSON.parse(value); } catch (_) { return null; } }
      if (!value || value.state === null || value.state === undefined || !value.available ||
          ['unknown', 'unavailable'].includes(String(value.state))) return null;
      const kind = type === '21' ? 'number' : (type === '22' ? 'select' : 'datetime');
      return { state: String(value.state), display: editablePreviewText(entity, kind, meta) };
    }
    const raw = String(meta?.values?.[entity] ?? '').trim();
    if (!raw || ['unavailable', 'unknown', 'none', 'null', '--'].includes(raw.toLowerCase())) return null;
    return { state: raw, display: null };
  }
  // Icon color of a preview tile: the per-tile rule or fixed icon color
  // (resolveIconColorRecord, same result as the firmware), else `fallback`.
  function previewIconColor(typeValue, record, entity, meta, binaryState, fallback) {
    if (!record || typeof tileTypeHasIconColors !== 'function' ||
        !tileTypeHasIconColors(typeValue)) return fallback;
    const rule = iconColorRuleState(typeValue, entity, meta, binaryState);
    return (rule && resolveIconColorRecord(record, rule.state, rule.display)) || fallback;
  }
  function snapshotBgColorIsDefault(snapshot) {
    return String(snapshot?.bg_color_default || '0') === '1' ||
      tileColorHexIsDefaultGrey(snapshot?.color);
  }
  function tileColorInputIsDefault(tab) {
    const input = document.getElementById(tab + '_tile_color');
    return !!input && (input.dataset.bgColorDefault === '1' || tileColorHexIsDefaultGrey(input.value));
  }
  function setTileColorInputFromStored(tab, value, fallback) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const follows = tileBgFollowsDefault(value);
    input.value = follows ? (fallback || '#2A2A2A') : tileBgToHex(value, fallback || '#2A2A2A');
    input.dataset.bgColorDefault = follows ? '1' : '0';
    syncTileColorGlobalToggle(tab);
  }
  function setTileColorInputFromSnapshot(tab, snapshot) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const meta = getTileTypeMeta(snapshot?.type || '0');
    const isDefault = snapshotBgColorIsDefault(snapshot);
    input.value = isDefault ? (meta.defaultBg || '#2A2A2A') : (snapshot?.color || meta.defaultBg || '#2A2A2A');
    input.dataset.bgColorDefault = isDefault ? '1' : '0';
    syncTileColorGlobalToggle(tab);
  }
  // Picking a color unchecks "Use global color".
  function markTileColorInputExplicit(tab) {
    const input = document.getElementById(tab + '_tile_color');
    if (input) input.dataset.bgColorDefault = '0';
    syncTileColorGlobalToggle(tab);
  }
  function resetTileColor(tab) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const meta = getTileTypeMeta(typeValue);
    input.value = meta.defaultBg || '#2A2A2A';
    input.dataset.bgColorDefault = '1';
    syncTileColorGlobalToggle(tab);
    if (isScreensaverTileTab(tab)) {
      const opacity = document.getElementById('screensaver_tile_opacity');
      if (opacity) opacity.value = String(SCREENSAVER_TILE_DEFAULT_OPACITY);
    }
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }

  function renderTileFromData(tab, index, tile, sensorMeta) {
    const el = document.getElementById(tab + '-tile-' + index);
    if (!el) return;
    const metaValues = sensorMeta?.values || {};
    const metaUnits = sensorMeta?.units || {};
    const metaIcons = sensorMeta?.icons || {};
    const metaNames = sensorMeta?.names || {};
    el.dataset.index = index.toString();
    const typeValue = String(tile?.type ?? '0');
    const meta = getTileTypeMeta(typeValue);
    if (typeValue === '17' &&
        currentTileTab === tab &&
        currentTileIndex === index &&
        el.classList.contains('climate-content-editing')) {
      syncClimateSlotFields(tab);
      return;
    }
    let cls = ['tile'];
    if (meta.css) cls.push(meta.css);
    if (typeValue === '5' && tile.switch_style === 1) cls.push('switch-toggle');
    if (typeValue === '0' && (!meta.css || meta.css !== 'empty')) cls.push('empty');
    el.className = cls.join(' ');
    el.dataset.type = typeValue;
    el.dataset.iconDisc = ['1', '2'].includes(String(tile?.icon_disc)) ? String(tile.icon_disc) : '0';
    el.dataset.iconGlow = ['0', 'false'].includes(String(tile?.icon_glow)) ? '0' : '1';
    el.classList.toggle('tile-border-hidden', ['8','9','10'].includes(typeValue) && Number(tile.sensor_display_mode) === 1);
    applyCompactSensorPreview(el, typeValue, tile, tile.sensor_display_mode);
    if (typeValue === '4') el.dataset.navigateTarget = String(tile.navigate_target || 0);
    else delete el.dataset.navigateTarget;
    if (typeValue === '0') el.style.background = 'transparent';
    else {
      const isDefaultBg = tileBgFollowsDefault(tile.bg_color);
      const bg = tileBackgroundCss(meta, isDefaultBg,
        tileBgToHex(tile.bg_color, meta.defaultBg || '#353535'));
      if (isScreensaverTileTab(tab)) {
        const opacity = clampInt(tile.background_opacity, 0, 255,
                                 SCREENSAVER_TILE_DEFAULT_OPACITY);
        el.style.background = tileBackgroundCss(meta, isDefaultBg,
          tileBgToHex(tile.bg_color, meta.defaultBg || '#353535'), opacity);
      } else {
        el.style.background = bg;
      }
      el.style.removeProperty('--switch-knob-color');
      el.style.removeProperty('--switch-on-color');
      if (typeValue === '5' && tile.switch_style === 1) {
        el.style.setProperty('--switch-knob-color', bg);
        el.style.setProperty('--switch-on-color', '#3B82F6');
      }
    }
    const sensorValueClass = getSensorValueFontClass(tile.sensor_value_font);
    if (typeValue === '0') {
      el.innerHTML = '';
      applyTileAriaLabel(el, '', typeValue);
    }
    else {
      const previewKind = meta.preview || 'none';
      const iconEntity = (isEditablePreview(previewKind) || previewKind === 'sensor' ||
                          previewKind === 'binary_sensor' ||
                          previewKind === 'switch' ||
                          previewKind === 'weather' || previewKind === 'media' ||
                          previewKind === 'climate' || previewKind === 'cover' ||
                          previewKind === 'camera')
        ? (tile.sensor_entity || '')
        : (typeValue === '2' ? (sensorMeta?.sceneEntities?.[tile.scene_alias] || '') : '');
      const rawIcon = tile.icon_name || '';
      let iconName = resolveIconName(
        rawIcon,
        iconEntity,
        metaIcons);
      if (previewKind === 'camera' && !iconName &&
          !isExplicitlyDisabledValue(rawIcon)) {
        iconName = 'video';
      }
      let climatePreviewState = null;
      if (previewKind === 'climate') {
        climatePreviewState = parseClimatePreviewPayload(
          tile.sensor_entity ? (metaValues[tile.sensor_entity] ?? '') : '');
        if (!normalizeMdiIconName(rawIcon) &&
            !isExplicitlyDisabledValue(rawIcon)) {
          iconName = climatePreviewIcon(climatePreviewState, iconName);
        }
      }
      let coverPreviewState = null;
      if (previewKind === 'cover') {
        coverPreviewState = parseCoverPreviewPayload(
          tile.sensor_entity ? (metaValues[tile.sensor_entity] ?? '') : '');
        if (!normalizeMdiIconName(rawIcon) &&
            !isExplicitlyDisabledValue(rawIcon)) {
          iconName = coverPreviewIcon(coverPreviewState, iconName);
        }
      }
      let binarySensorPreviewState = null;
      if (previewKind === 'binary_sensor') {
        binarySensorPreviewState = parseBinarySensorPreviewPayload(
          tile.sensor_entity ? (metaValues[tile.sensor_entity] ?? '') : '');
        iconName = resolveBinarySensorPreviewIcon(
          rawIcon, tile.sensor_entity || '', binarySensorPreviewState,
          metaIcons);
      }

      let html = '';

      if (iconName) {
        const iconColor = previewIconColor(typeValue, tile.icon_colors, tile.sensor_entity || '',
          sensorMeta, binarySensorPreviewState, previewKind === 'climate'
            ? climatePreviewColor(climatePreviewState)
            : (previewKind === 'cover'
              ? coverPreviewColor(coverPreviewState)
              : (previewKind === 'binary_sensor'
                ? binarySensorPreviewColor(binarySensorPreviewState)
                : '')));
        const iconStyle = iconColor ? ' style="color:' + escapeHtml(iconColor) + '"' : '';
        html += '<i class="mdi mdi-' + escapeHtml(iconName) + ' tile-icon"' + iconStyle + '></i>';
      }

      let displayTitle = tile.title || '';
      if (previewKind === 'camera' && !displayTitle && tile.sensor_entity) {
        displayTitle = metaNames[tile.sensor_entity] ||
          titleFromEntity(tile.sensor_entity);
      }
      if (displayTitle.length) {
        html += '<div class="tile-title" id="' + tab + '-tile-' + index + '-title">' +
          tileTitleHtml(displayTitle) + '</div>';
      }
      applyTileAriaLabel(el, displayTitle, typeValue);

      if (previewKind === 'weather') {
        html += '<div class="tile-ghost-icon"><i class="mdi mdi-weather-partly-cloudy"></i></div>';
      }
      if (previewKind === 'media') {
        html += '<div class="tile-ghost-icon"><i class="mdi mdi-music"></i></div>';
      }

      if (previewKind === 'sensor') {
        let value = '--';
        if (tile.sensor_entity) value = formatSensorValue(metaValues[tile.sensor_entity] ?? '--', tile.sensor_decimals);
        const unit = resolveUnitValue(tile.sensor_unit || '', tile.sensor_entity || '', metaUnits);
        html += '<div class="tile-value ' + sensorValueClass + '" id="' + tab + '-tile-' + index + '-value">' +
          escapeHtml(value) +
          (unit ? '<span class="tile-unit">' + escapeHtml(unit) + '</span>' : '') +
          '</div>';
      }
      if (previewKind === 'climate') {
        html += climatePreviewSlots(
          climatePreviewState,
          tile.span_w || 1,
          tile.span_h || 1,
          decodeClimateSlotConfig(tile.sensor_gauge_min || 0),
          decodeClimateTargetLayouts(tile.sensor_gauge_max || 0),
          tile.climate_geometry || tile.scene_alias || '');
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
        html += '<div class="tile-value tile-binary-sensor-value ' + (Number(tile.sensor_value_font) ? sensorValueClass : '') + '" id="' +
          tab + '-tile-' + index + '-value">' +
          escapeHtml(binarySensorPreviewStateText(binarySensorPreviewState)) +
          '</div>';
      }
      if (isEditablePreview(previewKind)) html += '<div class="tile-value tile-editable-value ' + sensorValueClass + '">' + escapeHtml(editablePreviewText(iconEntity, previewKind, sensorMeta)) + '</div>';
      if (previewKind === 'clock') {
        const flags = normalizeClockFlags(tile.sensor_decimals);
        const clockTimeFont = tile.key_code || 40;
        const clockDateFont = Math.min(72, Number(tile.key_modifier || 20));
        const clockTimeFormat = (tile.sensor_gauge_min !== undefined) ? tile.sensor_gauge_min : 0;
        const clockDateFormat = (tile.sensor_gauge_max !== undefined) ? tile.sensor_gauge_max : 0;
        if (flags & 1) html += '<div class="tile-clock-time" ' + getClockPreviewTextStyle(clockTimeFont, 40, '#fff') + '>' + getClockPreviewTime(clockTimeFormat) + '</div>';
        if (flags & 2) html += '<div class="tile-clock-date" ' + getClockPreviewTextStyle(clockDateFont, 24, '#fff') + '>' + getClockPreviewDate(clockDateFormat) + '</div>';
      }
      if (previewKind === 'text') {
        const textValue = tile.text_value || tile.scene_alias || tile.key_macro || '';
        if (textValue) {
          const textClass = getSensorValueFontClass(tile.sensor_value_font);
          html += '<div class="tile-text ' + textClass + '">' +
            escapeHtml(textValue) + '</div>';
        }
      }
      if (previewKind === 'switch' && tile.switch_style === 1) {
        html += '<div class="tile-switch" id="' + tab + '-tile-' + index + '-switch"><div class="tile-switch-knob"></div></div>';
      }
      html += getTileResizeHandlesHtml(typeValue);
      el.innerHTML = html;
      applyIconDiscTint(el);
      if (typeValue === '9') fitCompactClockPreview(el);
    }
    if (currentTileTab === tab && currentTileIndex === index) el.classList.add('active');
    if (typeValue === '5' && tile.sensor_entity) {
      const state = parseSwitchPayload(metaValues[tile.sensor_entity] ?? '');
      applySwitchPreviewState(el, state);
    }
  }

  function fetchTileGridData(tab, force = false) {
    if (!force && tileDataLoadedTabs.has(tab)) {
      return Promise.resolve(getTilesData(tab));
    }
    if (tileDataLoadPromises[tab]) return tileDataLoadPromises[tab];
    const folderId = getFolderIdForTab(tab);
    if (folderId === undefined) return Promise.resolve([]);

    const baseline = getTilesData(tab).map(tile => JSON.stringify(tile));
    tileDataLoadPromises[tab] = fetch(
      '/api/tiles?folder=' + encodeURIComponent(folderId))
      .then(async response => {
        if (!response.ok) throw new Error('Tiles HTTP ' + response.status);
        const tiles = await response.json();
        if (!Array.isArray(tiles)) throw new Error('Invalid tile grid response');
        const current = getTilesData(tab);
        tilesData[tab] = tiles.map((tile, index) => {
          const changed = JSON.stringify(current[index]) !== baseline[index];
          return current[index] && (changed || drafts[tab]?.[index]?._dirty)
            ? current[index] : tile;
        });
        tileDataLoadedTabs.add(tab);
        return tilesData[tab];
      })
      .finally(() => { delete tileDataLoadPromises[tab]; });
    return tileDataLoadPromises[tab];
  }

  function loadSensorValues(
      refreshTiles = false, forceMetaFetch = false, tabsOverride = null) {
    if (dragSource || resizeState) {
      queueDeferredSensorRefresh(refreshTiles);
      return Promise.resolve(false);
    }
    const requestedTabs = Array.isArray(tabsOverride)
      ? tabsOverride
      : (refreshTiles
          ? (currentTileTab ? [currentTileTab] : tileTabs.slice(0, 1))
          : (currentTileTab && tileDataLoadedTabs.has(currentTileTab)
              ? [currentTileTab]
              : []));
    const tabs = Array.from(new Set(requestedTabs)).filter(tab =>
      tileTabs.includes(tab) && getFolderIdForTab(tab) !== undefined);
    const tileRequests = refreshTiles
      ? tabs.map(tab => fetchTileGridData(tab, true))
      : tabs.map(tab => Promise.resolve(getTilesData(tab)));

    return Promise.all([fetchSensorMetaCache(forceMetaFetch), ...tileRequests])
    .then(results => {
      // A refresh may have started shortly before the drag and only arrive
      // during it. In that case it must not overwrite the local preview with the
      // old device state.
      if (dragSource || resizeState) {
        queueDeferredSensorRefresh(refreshTiles);
        return;
      }
      const sensorMeta = normalizeSensorMetaPayload(results[0] || {});
      sensorMetaCache = sensorMeta;
      tabs.forEach((tab, idx) => {
        // Metadata may finish after another edit; render the current cache.
        const tilesForRender = getTilesData(tab);
        if (!Array.isArray(tilesForRender)) return;
        tilesForRender.forEach((tile, i) => renderTileFromData(tab, i, tile, sensorMeta));
        layoutTiles(tab, tilesForRender);
      });
      if (currentTileIndex !== -1 && currentTileTab) {
        restoreCurrentTileSelectionUi();
      } else if (!isScreensaverTileTab(currentTileTab)) {
        restoreSelectedTileState();
      }
      return true;
    })
    .catch(err => {
      console.error('Sensor values load failed:', err);
      return false;
    });
  }


  // The icon block of the common fields only holds Weather's colored icons
  // option: every circle shows, in its icon's color (user 2026-10-11: no
  // circle options per tile). Stored per-tile disc values of older tiles are
  // kept by the server but no longer drawn.
  function syncIconDiscFields(tab) {
    const weather = (document.getElementById(tab + '_tile_type')?.value || '0') === '12';
    document.getElementById(tab + '_tile_icon_disc_fields')?.classList.toggle('hidden', !weather);
    document.getElementById(tab + '_weather_colored_icons_row')?.classList.toggle('hidden', !weather);
  }

  // Per-tile screensaver look (screensaver_tile_look.h): fill opacity,
  // background fill, an own card color, icon visibility and an own text
  // color. Only the screensaver tile editor has these fields.
  function collectScreensaverLookFields(tab, typeValue) {
    const fill = document.getElementById(tab + '_tile_ss_fill');
    if (!fill || String(typeValue ?? '0') === '0') return {};
    const textOn = document.getElementById(tab + '_tile_ss_text_color_on')?.checked === true;
    const card = document.getElementById(tab + '_tile_ss_card_color');
    return {
      ss_fill: fill.checked ? '1' : '0',
      ss_icon: document.getElementById(tab + '_tile_ss_icon')?.checked === false ? '0' : '1',
      ss_opacity: String(clampInt(document.getElementById(tab + '_tile_ss_opacity')?.value, 0, 255, 255)),
      ss_card_color: card?.dataset.set === '1' ? String(card.value || '#3A4A5A').toUpperCase() : '',
      ss_text_color: textOn
        ? String(document.getElementById(tab + '_tile_ss_text_color')?.value || '#FFFFFF').toUpperCase()
        : '',
    };
  }
  function loadScreensaverLookFields(tab, data) {
    const fill = document.getElementById(tab + '_tile_ss_fill');
    if (!fill) return;
    fill.checked = !['0', 'false'].includes(String(data?.ss_fill));
    const icon = document.getElementById(tab + '_tile_ss_icon');
    if (icon) icon.checked = !['0', 'false'].includes(String(data?.ss_icon));
    const opacity = document.getElementById(tab + '_tile_ss_opacity');
    if (opacity) opacity.value = String(clampInt(data?.ss_opacity, 0, 255, 255));
    const cardColor = /^#[0-9a-f]{6}$/i.test(String(data?.ss_card_color || '')) ? String(data.ss_card_color) : '';
    const card = document.getElementById(tab + '_tile_ss_card_color');
    if (card) {
      card.dataset.set = cardColor ? '1' : '0';
      if (cardColor) card.value = cardColor.toLowerCase();
    }
    const text = /^#[0-9a-f]{6}$/i.test(String(data?.ss_text_color || '')) ? String(data.ss_text_color) : '';
    const textOn = document.getElementById(tab + '_tile_ss_text_color_on');
    if (textOn) textOn.checked = !!text;
    const color = document.getElementById(tab + '_tile_ss_text_color');
    if (color && text) color.value = text.toLowerCase();
    syncScreensaverLookFields(tab);
  }
  function resetScreensaverLookFields(tab) {
    loadScreensaverLookFields(tab, {});
    const color = document.getElementById(tab + '_tile_ss_text_color');
    if (color) color.value = '#ffffff';
  }
  function syncScreensaverLookFields(tab) {
    const opacity = document.getElementById(tab + '_tile_ss_opacity');
    const output = document.getElementById(tab + '_tile_ss_opacity_value');
    if (opacity && output) output.textContent = Math.round(Number(opacity.value) * 100 / 255) + ' %';
    // Text color: Automatic (the type's colors) or Custom (the picker).
    const custom = document.getElementById(tab + '_tile_ss_text_color_on')?.checked === true;
    document.getElementById(tab + '_tile_ss_text_color_row')?.classList.toggle('hidden', !custom);
    document.getElementById(tab + '_tile_ss_text_modes')?.querySelectorAll('[data-ss-text-mode]').forEach(button => {
      const active = button.dataset.ssTextMode === (custom ? 'custom' : 'auto');
      button.classList.toggle('active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
    // Background None comes from the fill box (syncTileBgMode).
    if (typeof syncTileColorMode === 'function') syncTileColorMode(tab);
  }
  function setScreensaverTextColorMode(tab, mode) {
    const on = document.getElementById(tab + '_tile_ss_text_color_on');
    if (!on) return;
    const before = on.checked;
    on.checked = mode === 'custom';
    syncScreensaverLookFields(tab);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
    const color = document.getElementById(tab + '_tile_ss_text_color');
    if (on.checked && !before && color) {
      try {
        color.getBoundingClientRect();
        if (typeof color.showPicker === 'function') color.showPicker();
      } catch (_) {}
    }
  }
  // A tile without fill draws its background at opacity 0, like the panel
  // (apply_slot_look); tints keep that through data-bg-opacity.
  function screensaverLookFilled(look) {
    return !['0', 'false'].includes(String(look?.ss_fill));
  }
  // The own card color (Background Custom) as "#RRGGBB", '' for the theme's
  // card; Clock, Text and Pixel animation keep their own color.
  function screensaverLookCardColor(look, typeValue) {
    if (tileTypeKeepsOwnColor(typeValue)) return '';
    return /^#[0-9a-f]{6}$/i.test(String(look?.ss_card_color || '')) ? String(look.ss_card_color) : '';
  }
  // The rest of the look as the screensaver tile previews show it (live and
  // cached grid): no outline without fill, hidden icon, own text color.
  function applyScreensaverLookPreview(tileElem, look) {
    if (!tileElem) return;
    const empty = String(tileElem.dataset.type || '0') === '0';
    const fill = empty || screensaverLookFilled(look);
    const icon = empty || !['0', 'false'].includes(String(look?.ss_icon));
    const text = !empty && /^#[0-9a-f]{6}$/i.test(String(look?.ss_text_color || ''))
      ? String(look.ss_text_color) : '';
    tileElem.classList.toggle('ss-no-fill', !fill);
    if (!fill) tileElem.classList.add('tile-border-hidden');
    tileElem.classList.toggle('ss-icon-hidden', !icon);
    // The tile's Opacity fades only the fill (the background code).
    tileElem.style.opacity = '';
    tileElem.classList.toggle('ss-text-colored', !!text);
    if (text) tileElem.style.setProperty('--ss-text-color', text);
    else tileElem.style.removeProperty('--ss-text-color');
  }

  function collectTypeFieldValues(tab) {
    const prefix = tab;
    const typeValue = document.getElementById(prefix + '_tile_type')?.value || '0';
    const meta = getTileTypeMeta(typeValue);
    const out = collectScreensaverLookFields(prefix, typeValue);
    if (!meta.save) return out;
    const fd = new FormData();
    callTypeHandler(meta, 'save', prefix, fd);
    for (const [key, value] of fd.entries()) {
      out[key] = value;
    }
    return out;
  }

  function normalizeSnapshotLayout(snapshot, index, tab = currentTileTab) {
    // The shown grid, like normalizeTileLayout (an upright layout's lower
    // rows must survive the draft and the save).
    const cols = placeCols(tab);
    const rows = placeRows(tab);
    const fallbackCol = (index >= 0) ? ((index % cols) + 1) : 1;
    const firstRow = firstAllowedGridRow(tab);
    const fallbackRow = (index >= 0)
      ? (Math.max(firstRow, Math.floor(index / cols)) + 1)
      : (firstRow + 1);
    let col = clampHalf(snapshot?.col, 1, cols, fallbackCol);
    let row = clampHalf(snapshot?.row, firstRow + 1, rows + 0.5, fallbackRow);
    let spanW = clampHalf(snapshot?.span_w, 0.5, cols, 1);
    let spanH = clampHalf(snapshot?.span_h, 0.5, rows, 1);
    return constrainLayoutToTab(
      normalizeLayoutForTileType(snapshot?.type, col - 1, row - 1,
                                 spanW, spanH),
      tab);
  }

  function buildTileSnapshotFromInputs(tab) {
    const prefix = tab;
    const colorEl = document.getElementById(prefix + '_tile_color');
    const snapshot = {
      type: document.getElementById(prefix + '_tile_type')?.value || '0',
      title: document.getElementById(prefix + '_tile_title')?.value || '',
      icon: document.getElementById(prefix + '_tile_icon')?.value || '',
      color: colorEl?.value || '#2A2A2A',
      bg_color_default: tileColorInputIsDefault(tab) ? '1' : '0',
      col: document.getElementById(prefix + '_tile_col')?.value || '1',
      row: document.getElementById(prefix + '_tile_row')?.value || '1',
      span_w: document.getElementById(prefix + '_tile_span_w')?.value || '1',
      span_h: document.getElementById(prefix + '_tile_span_h')?.value || '1'
    };
    Object.assign(snapshot, collectTypeFieldValues(tab));
    return snapshot;
  }

  function getTileSnapshotForSave(tab, index) {
    const draft = drafts[tab] && drafts[tab][index];
    if (draft && draft._dirty) return Object.assign({}, draft);
    if (currentTileTab === tab && currentTileIndex === index) return buildTileSnapshotFromInputs(tab);
    return null;
  }

  function applySnapshotToTileData(tab, index, snapshot) {
    const tiles = getTilesData(tab);
    if (!Array.isArray(tiles) || index < 0) return;

    const prev = tiles[index] || {};
    // A deleted (empty) tile starts from nothing: merged over the previous
    // data, its entity and options returned with the next tile in the slot.
    const tile = Number(snapshot?.type) === 0 ? {} : Object.assign({}, prev);
    const layout = normalizeSnapshotLayout(snapshot, index, tab);
    const numericFields = ['type', 'sensor_decimals', 'sensor_value_font', 'sensor_display_mode', 'sensor_gauge_min', 'sensor_gauge_max', 'switch_style', 'navigate_target', 'popup_open_mode', 'key_code', 'key_modifier', 'background_opacity', 'icon_disc', 'icon_glow', 'ss_opacity', 'ss_fill', 'ss_icon'];

    tile.type = clampInt(snapshot?.type, 0, 255, Number(prev.type) || 0);
    tile.title = snapshot?.title || '';
    tile.icon_name = snapshot?.icon || '';
    tile.bg_color = snapshotBgColorIsDefault(snapshot)
                        ? 0
                        : makeTileBgValue(hexToRgb(snapshot?.color || '#2A2A2A'));
    tile.col = layout.col;
    tile.row = layout.row;
    tile.span_w = layout.span_w;
    tile.span_h = layout.span_h;

    for (const [key, value] of Object.entries(snapshot || {})) {
      if (key === '_dirty' || key === '_rev' || key === 'icon' || key === 'color' || key === 'bg_color_default' || key === 'col' || key === 'row' || key === 'span_w' || key === 'span_h' || key === 'type' || key === 'title') continue;
      if (numericFields.includes(key)) {
        const num = Number(value);
        tile[key] = Number.isFinite(num) ? num : value;
      } else {
        tile[key] = value;
      }
    }

    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'switch_entity')) {
      tile.sensor_entity = snapshot.switch_entity || '';
    }
    for (const kind of ['number', 'select', 'datetime']) {
      if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, kind + '_entity')) tile.sensor_entity = snapshot[kind + '_entity'] || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'binary_sensor_entity')) {
      tile.sensor_entity = snapshot.binary_sensor_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'weather_entity')) {
      tile.sensor_entity = snapshot.weather_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'energy_entity')) {
      tile.sensor_entity = snapshot.energy_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'climate_entity')) {
      tile.sensor_entity = snapshot.climate_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'cover_entity')) {
      tile.sensor_entity = snapshot.cover_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'camera_entity')) {
      tile.sensor_entity = snapshot.camera_entity || '';
    }
    // The Media popup's layout in its stored field (types/media/admin.js).
    if (Number(tile.type) === 15 && snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'media_layout')) {
      tile.sensor_display_mode = String(snapshot.media_layout) === '1' ? 1 : 0;
    }
    // The Camera tile's picture options in their stored fields (types/camera/admin.js).
    if (Number(tile.type) === 18 && snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'camera_display')) {
      tile.sensor_display_mode = String(snapshot.camera_display) === '1' ? 1 : 0;
      tile.sensor_decimals = Math.max(0, CAMERA_FITS.indexOf(String(snapshot.camera_fit)));
      tile.image_slideshow_sec = cameraEvery(snapshot.camera_every);
      tile.key_code = cameraTransparency(snapshot.camera_transparency);
      tile.key_modifier = cameraOptionFlags({head: snapshot.camera_head, tap: snapshot.camera_tap});
    }
    for (const kind of ['lock', 'alarm', 'fan']) {
      if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, kind + '_entity')) tile.sensor_entity = snapshot[kind + '_entity'] || '';
    }
    if (snapshot && (Object.prototype.hasOwnProperty.call(snapshot, 'clock_show_time') || Object.prototype.hasOwnProperty.call(snapshot, 'clock_show_date'))) {
      let flags = 0;
      if (String(snapshot.clock_show_time || '0') === '1') flags |= 1;
      if (String(snapshot.clock_show_date || '0') === '1') flags |= 2;
      if (flags === 0) flags = 1;
      tile.sensor_decimals = flags;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'clock_time_format')) {
      const num = Number(snapshot.clock_time_format);
      tile.sensor_gauge_min = Number.isFinite(num) ? num : 0;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'clock_date_format')) {
      const num = Number(snapshot.clock_date_format);
      tile.sensor_gauge_max = Number.isFinite(num) ? num : 0;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'animation_fit')) {
      const num = Number(snapshot.animation_fit);
      tile.sensor_display_mode = Number.isFinite(num) ? num : 0;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'animation_zoom')) {
      const num = Number(snapshot.animation_zoom);
      tile.sensor_gauge_max = Number.isFinite(num) ? num : 100;
    }

    // The Text tile's alignment in its stored field (types/text/text_options.h).
    if (Number(tile.type) === 10 && snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'text_options')) {
      const num = Number(snapshot.text_options);
      tile.sensor_gauge_max = Number.isFinite(num) ? num : 100;
    }
    if ([8,9,10].includes(Number(tile.type)) && snapshot?.tile_border !== undefined) {
      tile.sensor_display_mode = ['0','false'].includes(String(snapshot.tile_border)) ? 1 : 0;
    }
    if (Number(tile.type) === 12 && snapshot?.weather_colored_icons !== undefined) {
      tile.sensor_display_mode = ['0','false'].includes(String(snapshot.weather_colored_icons)) ? 1 : 0;
    }
    tiles[index] = tile;
    tilesData[tab] = tiles;
  }

  function markLatestSaveRequest(tab, index, requestId) {
    if (!latestSaveRequestByTab[tab]) latestSaveRequestByTab[tab] = {};
    latestSaveRequestByTab[tab][index] = requestId;
  }

  function isLatestSaveRequest(tab, index, requestId) {
    return !!(latestSaveRequestByTab[tab] && latestSaveRequestByTab[tab][index] === requestId);
  }

  function getTileSaveKey(tab, index) {
    return tab + ':' + index;
  }

  function queueSaveAfterFlight(tab, index, silent = true) {
    const saveKey = getTileSaveKey(tab, index);
    const existing = queuedSaveByTile[saveKey];
    queuedSaveByTile[saveKey] = {
      silent: existing ? (existing.silent && silent) : silent
    };
  }

  function flushQueuedSave(tab, index) {
    const saveKey = getTileSaveKey(tab, index);
    if (saveInFlightByTile[saveKey]) return;
    const queued = queuedSaveByTile[saveKey];
    if (!queued) return;
    delete queuedSaveByTile[saveKey];
    saveTile(tab, queued.silent, index);
  }


  function collectTypeFieldValues(tab) {
    const prefix = tab;
    const typeValue = document.getElementById(prefix + '_tile_type')?.value || '0';
    const meta = getTileTypeMeta(typeValue);
    if (!meta.save) return {};
    const fd = new FormData();
    callTypeHandler(meta, 'save', prefix, fd);
    const out = {};
    for (const [key, value] of fd.entries()) {
      out[key] = value;
    }
    return out;
  }

  function normalizeSnapshotLayout(snapshot, index, tab = currentTileTab) {
    const fallbackCol = (index >= 0) ? ((index % GRID_COLS) + 1) : 1;
    const firstRow = firstAllowedGridRow(tab);
    const fallbackRow = (index >= 0)
      ? (Math.max(firstRow, Math.floor(index / GRID_COLS)) + 1)
      : (firstRow + 1);
    let col = clampInt(snapshot?.col, 1, GRID_COLS, fallbackCol);
    let row = clampInt(snapshot?.row, firstRow + 1, GRID_ROWS, fallbackRow);
    let spanW = clampInt(snapshot?.span_w, 1, GRID_COLS, 1);
    let spanH = clampInt(snapshot?.span_h, 1, GRID_ROWS, 1);
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
    if (isScreensaverTileTab(tab)) {
      snapshot.background_opacity = document.getElementById('screensaver_tile_opacity')?.value || '0';
    }
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
    const tile = Object.assign({}, prev);
    const layout = normalizeSnapshotLayout(snapshot, index, tab);
    const numericFields = ['type', 'sensor_decimals', 'sensor_value_font', 'sensor_display_mode', 'sensor_gauge_min', 'sensor_gauge_max', 'switch_style', 'navigate_target', 'popup_open_mode', 'key_code', 'key_modifier', 'background_opacity'];

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


  function downloadJsonFile(filename, content) {
    const blob = new Blob([content], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 5000);
  }

  function parseBgColorValue(value) {
    if (value === undefined || value === null) return 0;
    if (typeof value === 'string') {
      let v = value.trim();
      if (!v.length) return 0;
      if (v.startsWith('#')) return parseInt(v.substring(1), 16) || 0;
      if (v.startsWith('0x') || v.startsWith('0X')) return parseInt(v, 16) || 0;
    }
    const num = parseInt(value, 10);
    return isNaN(num) ? 0 : num;
  }

  function buildScreensaverExportConfig(data) {
    return {
      version: Number(data?.version || 1),
      use_wallpapers: !!data?.use_wallpapers,
      shuffle: !!data?.shuffle,
      tile_shadow: !!data?.tile_shadow,
      tile_border: data?.tile_border !== false,
      show_time: !!data?.show_time,
      show_date: !!data?.show_date,
      show_weekday: !!data?.show_weekday,
      clock_shadow: !!data?.clock_shadow,
      time_format: Number(data?.time_format || 0),
      date_format: Number(data?.date_format || 0),
      time_alignment: Math.round(ssClamp(data?.time_alignment ?? 1, 0, 2)),
      date_alignment: Math.round(ssClamp(data?.date_alignment ?? 1, 0, 2)),
      time_font_size: Number(data?.time_font_size || 48),
      date_font_size: Number(data?.date_font_size || 28),
      clock_x: Number(data?.clock_x ?? 500),
      clock_y: Number(data?.clock_y ?? 350),
      duration_seconds: Number(data?.duration_seconds ?? 15),
      wallpapers: Array.isArray(data?.wallpapers) ? data.wallpapers.map(wallpaper => ({
        file_name: String(wallpaper?.file_name || ''),
        enabled: !!wallpaper?.enabled,
        focus_x: Number(wallpaper?.focus_x ?? 500),
        focus_y: Number(wallpaper?.focus_y ?? 500),
        zoom: Number(wallpaper?.zoom ?? 1000)
      })) : []
    };
  }

  async function exportTilesConfig() {
    try {
      const foldersRequest = fetch('/api/folders').then(async res => {
        const data = await res.json();
        if (!res.ok || !Array.isArray(data)) {
          throw new Error('Folder export failed');
        }
        return data.map(folder => ({
          id: Number(folder?.id || 0),
          parent_id: Number(folder?.parent_id || 0),
          name: String(folder?.name || ''),
          icon_name: String(folder?.icon_name || '')
        }));
      });
      const screensaverConfigRequest = fetch('/api/screensaver').then(async res => {
        const data = await res.json();
        if (!res.ok || !data?.success) throw new Error('Screensaver config export failed');
        return data;
      });
      const screensaverGridRequest = fetch(
        '/api/tiles?folder=' + encodeURIComponent(SCREENSAVER_FOLDER_ID)
      ).then(async res => {
        const data = await res.json();
        if (!res.ok || !Array.isArray(data)) throw new Error('Screensaver grid export failed');
        return data.map(tile => {
          const exported = {...tile};
          delete exported.folder_pin_enabled;
          delete exported.folder_pin;
          return exported;
        });
      });
      const [folders, screensaverData, screensaverGrid] = await Promise.all([
        foldersRequest, screensaverConfigRequest, screensaverGridRequest
      ]);
      const tilesLists = await Promise.all(folders.map(async folder => {
        const response = await fetch(
          '/api/tiles?folder=' + encodeURIComponent(folder.id));
        const data = await response.json();
        if (!response.ok || !Array.isArray(data)) {
          throw new Error('Folder grid export failed');
        }
        return data.map(tile => {
          const exported = {...tile};
          delete exported.folder_pin_enabled;
          delete exported.folder_pin;
          return exported;
        });
      }));

      const grids = {};
      folders.forEach((folder, idx) => {
        grids[String(folder.id)] =
          Array.isArray(tilesLists[idx]) ? tilesLists[idx] : [];
      });

      const payload = {
        version: 3,
        exported_at: new Date().toISOString(),
        folders: folders,
        grids: grids,
        screensaver: {
          version: 2,
          config: buildScreensaverExportConfig(screensaverData),
          grid: screensaverGrid,
          source_layout: {
            screen_width: Number(screensaverData.screen_width || 0),
            screen_height: Number(screensaverData.screen_height || 0),
            grid_cols: Number(screensaverData.grid_cols || 0),
            grid_rows: Number(screensaverData.grid_rows || 0)
          }
        }
      };
      const ts = new Date().toISOString().replace(/[:.]/g, '-');
      downloadJsonFile('waveshare_tiles_' + ts + '.json', JSON.stringify(payload, null, 2));
      showNotification(t('exportCreated'));
    } catch (e) {
      showNotification(t('exportFailed'), false);
    }
  }

  function triggerTilesImport(tab) {
    const input = document.getElementById(tab + '_tile_import');
    if (!input) return;
    input.value = '';
    input.click();
  }

  function importTilesConfig(tab, files) {
    if (!files || !files.length) return;
    const file = files[0];
    const reader = new FileReader();
    reader.onload = async () => {
      try {
        const payload = JSON.parse(reader.result);
        await importTilesPayload(payload);
      } catch (e) {
        showNotification(t('importInvalidJson'), false);
      }
    };
    reader.onerror = () => showNotification(t('importFailed'), false);
    reader.readAsText(file);
  }

  function normalizeImportFolderName(value) {
    return String(value || '').trim().toLowerCase();
  }

  function normalizeImportFolderIcon(value) {
    return normalizeIconName(value || '');
  }

  async function fetchFoldersForImport() {
    const res = await fetch('/api/folders');
    if (!res.ok) throw new Error('Folder fetch failed');
    const data = await res.json();
    return Array.isArray(data) ? data : [];
  }

  async function fetchTilesForImport(folderId) {
    const res = await fetch('/api/tiles?folder=' + encodeURIComponent(folderId));
    if (!res.ok) throw new Error('Tile fetch failed');
    const data = await res.json();
    return Array.isArray(data) ? data : [];
  }

  function buildEmptyImportTile(index) {
    return {
      type: 0,
      title: '',
      icon_name: '',
      bg_color: 0,
      col: index % GRID_COLS,
      row: Math.floor(index / GRID_COLS),
      span_w: 1,
      span_h: 1
    };
  }

  function updateFolderImportMap(sourceFolders, targetFolders, sourceToTarget) {
    let changed = false;
    sourceFolders.forEach(sourceFolder => {
      const sourceId = parseInt(sourceFolder && sourceFolder.id, 10);
      const sourceParentId = parseInt(sourceFolder && sourceFolder.parent_id, 10);
      if (isNaN(sourceId) || sourceId === 0 || isNaN(sourceParentId)) return;
      const targetParentId = sourceToTarget[sourceParentId];
      if (targetParentId === undefined) return;
      const sourceName = normalizeImportFolderName(sourceFolder.name);
      const sourceIcon = normalizeImportFolderIcon(sourceFolder.icon_name);
      const match = targetFolders.find(targetFolder =>
        Number(targetFolder.parent_id) === Number(targetParentId) &&
        normalizeImportFolderName(targetFolder.name) === sourceName &&
        normalizeImportFolderIcon(targetFolder.icon_name) === sourceIcon
      );
      if (match && sourceToTarget[sourceId] !== Number(match.id)) {
        sourceToTarget[sourceId] = Number(match.id);
        changed = true;
      }
    });
    return changed;
  }

  async function replaceFolderGridForImport(folderId, sourceTiles, systemType, sourceToTarget = null) {
    const currentTiles = await fetchTilesForImport(folderId);
    const sourceList = Array.isArray(sourceTiles) ? sourceTiles.slice(0, GRID_COLS * GRID_ROWS) : [];
    const currentSystemIndex = currentTiles.findIndex(tile => Number(tile && tile.type) === systemType);
    const sourceSystemTile = sourceList.find(tile => Number(tile && tile.type) === systemType) || null;

    for (let i = 0; i < (GRID_COLS * GRID_ROWS); i++) {
      if (i === currentSystemIndex) continue;
      await postTile(folderId, i, buildEmptyImportTile(i), sourceToTarget);
    }

    if (currentSystemIndex >= 0 && sourceSystemTile) {
      await postTile(folderId, currentSystemIndex, sourceSystemTile, sourceToTarget);
    }

    const availableIndices = [];
    for (let i = 0; i < (GRID_COLS * GRID_ROWS); i++) {
      if (i === currentSystemIndex) continue;
      availableIndices.push(i);
    }

    const nonSystemTiles = sourceList.filter(tile => Number(tile && tile.type) !== systemType);
    for (let i = 0; i < nonSystemTiles.length && i < availableIndices.length; i++) {
      await postTile(folderId, availableIndices[i], nonSystemTiles[i] || {}, sourceToTarget);
    }
  }

  function prepareScreensaverTilesForImport(sourceTiles, sourceLayout) {
    const tileCount = GRID_COLS * GRID_ROWS;
    const sourceCols = Number(sourceLayout?.grid_cols || 0);
    const sourceRows = Number(sourceLayout?.grid_rows || 0);
    const sameLayout = sourceCols === GRID_COLS && sourceRows === GRID_ROWS;
    const sourceEntries = (Array.isArray(sourceTiles) ? sourceTiles : [])
      .map((tile, index) => ({ tile: tile || {}, sourceIndex: index }))
      .filter(entry => Number(entry.tile.type || 0) !== 0);

    if (sameLayout) {
      return sourceEntries.slice(0, tileCount).map(entry => ({
        targetIndex: entry.sourceIndex,
        tile: entry.tile
      }));
    }

    // An import between 7xN and 4xN keeps the relative arrangement of the two
    // bottom rows and packs it into the target grid.
    const firstTargetRow = Math.max(0, GRID_ROWS - 2);
    const firstSourceRow = sourceRows > 1 ? sourceRows - 2 : 0;
    const occupied = Array.from({ length: GRID_ROWS }, () => Array(GRID_COLS).fill(false));
    const prepared = [];
    for (const entry of sourceEntries) {
      if (prepared.length >= tileCount) throw new Error('Screensaver grid does not fit target device');
      const tile = entry.tile;
      const mediaTile = Number(tile.type) === MEDIA_TILE_TYPE;
      let spanW = Math.max(1, Number(tile.span_w || 1));
      let spanH = Math.max(1, Number(tile.span_h || 1));
      if (mediaTile) {
        spanW = Math.max(MEDIA_TILE_MIN_SPAN, spanW);
        spanH = Math.max(MEDIA_TILE_MIN_SPAN, spanH);
      }
      spanW = Math.min(spanW, GRID_COLS, mediaTile ? MEDIA_TILE_MAX_SPAN : GRID_COLS);
      spanH = Math.min(spanH, 2, mediaTile ? MEDIA_TILE_MAX_SPAN : 2);

      const sourceSpanW = Math.max(1, Number(tile.span_w || 1));
      const sourceColRange = Math.max(0, sourceCols - sourceSpanW);
      const targetColRange = Math.max(0, GRID_COLS - spanW);
      const relativeCol = sourceColRange > 0
        ? Math.max(0, Math.min(1, Number(tile.col || 0) / sourceColRange))
        : 0;
      const desiredCol = Math.round(relativeCol * targetColRange);
      const sourceRowOffset = Math.max(0, Math.min(1, Number(tile.row || 0) - firstSourceRow));
      const desiredRow = Math.min(GRID_ROWS - spanH, firstTargetRow + sourceRowOffset);

      let best = null;
      for (let row = firstTargetRow; row <= GRID_ROWS - spanH; row++) {
        for (let col = 0; col <= GRID_COLS - spanW; col++) {
          let free = true;
          for (let y = row; y < row + spanH && free; y++) {
            for (let x = col; x < col + spanW; x++) {
              if (occupied[y][x]) { free = false; break; }
            }
          }
          if (!free) continue;
          const score = Math.abs(row - desiredRow) * (GRID_COLS + 1) + Math.abs(col - desiredCol);
          if (!best || score < best.score) best = { row, col, score };
        }
      }
      if (!best) throw new Error('Screensaver grid does not fit target device');
      for (let y = best.row; y < best.row + spanH; y++) {
        for (let x = best.col; x < best.col + spanW; x++) occupied[y][x] = true;
      }
      prepared.push({
        targetIndex: prepared.length,
        tile: { ...tile, col: best.col, row: best.row, span_w: spanW, span_h: spanH }
      });
    }
    return prepared;
  }

  async function replaceScreensaverGridForImport(sourceTiles, sourceLayout = null) {
    const folderId = SCREENSAVER_FOLDER_ID;
    const currentTiles = await fetchTilesForImport(folderId);
    const tileCount = GRID_COLS * GRID_ROWS;
    const preparedTiles = prepareScreensaverTilesForImport(sourceTiles, sourceLayout);
    const supportedTypes = new Set([1, 2, 5, 14, 20, 21, 22, 23, MEDIA_TILE_TYPE]);
    for (const entry of preparedTiles) {
      if (!supportedTypes.has(Number(entry.tile.type || 0))) {
        throw new Error('Unsupported screensaver tile type');
      }
    }

    // Remove the existing tiles first so the imported positions do not fail on
    // temporary overlaps with the old grid.
    for (let i = 0; i < tileCount; i++) {
      if (Number(currentTiles[i]?.type || 0) !== 0) {
        await postTile(folderId, i, buildEmptyImportTile(i));
      }
    }

    for (const entry of preparedTiles) {
      const tile = entry.tile;
      await postTile(folderId, entry.targetIndex, tile);
    }
  }

  async function importScreensaverConfig(config) {
    const res = await fetch('/api/screensaver', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config)
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok || !data.success) {
      throw new Error(data.error || 'Screensaver config import failed');
    }
  }

  async function importTilesPayload(payload) {
    try {
      if (!payload || typeof payload !== 'object') {
        showNotification(t('importInvalidJson'), false);
        return;
      }
      const grids = (payload.grids && typeof payload.grids === 'object') ? payload.grids : {};
      if (!Object.keys(grids).length) {
        if (Array.isArray(payload.tab0)) grids['0'] = payload.tab0;
        if (Array.isArray(payload.tab1)) grids['1'] = payload.tab1;
        if (Array.isArray(payload.tab2)) grids['2'] = payload.tab2;
      }
      if (!Object.keys(grids).length) {
        showNotification(t('importInvalidJson'), false);
        return;
      }

      showNotification(t('importRunning'));

      const sourceFolders = Array.isArray(payload.folders) ? payload.folders : [{ id: 0, parent_id: 0, name: 'Home', icon_name: '' }];
      const sourceToTarget = { 0: 0 };

      if (Array.isArray(grids['0'])) {
        await replaceFolderGridForImport(0, grids['0'], 7, sourceToTarget);
      }

      let targetFolders = await fetchFoldersForImport();
      updateFolderImportMap(sourceFolders, targetFolders, sourceToTarget);

      const pendingFolderIds = sourceFolders
        .map(folder => parseInt(folder && folder.id, 10))
        .filter(folderId => !isNaN(folderId) && folderId !== 0 && Array.isArray(grids[String(folderId)]));

      let progressed = true;
      while (pendingFolderIds.length && progressed) {
        progressed = false;
        for (let i = 0; i < pendingFolderIds.length; ) {
          const sourceFolderId = pendingFolderIds[i];
          const targetFolderId = sourceToTarget[sourceFolderId];
          if (targetFolderId === undefined) {
            i++;
            continue;
          }
          await replaceFolderGridForImport(targetFolderId, grids[String(sourceFolderId)], 8, sourceToTarget);
          pendingFolderIds.splice(i, 1);
          progressed = true;
          targetFolders = await fetchFoldersForImport();
          updateFolderImportMap(sourceFolders, targetFolders, sourceToTarget);
        }
      }

      if (pendingFolderIds.length) {
        throw new Error('Folder mapping failed');
      }

      // Versions 1 and 2 had no screensaver block and stay importable
      // unchanged. Alternative flat field names are accepted as well, in case an
      // intermediate state of this export function was used.
      const screensaverBlock = payload.screensaver && typeof payload.screensaver === 'object'
        ? payload.screensaver
        : null;
      const screensaverConfig = screensaverBlock?.config || payload.screensaver_config;
      const screensaverGrid = screensaverBlock?.grid || payload.screensaver_grid;
      if (screensaverConfig && typeof screensaverConfig === 'object') {
        await importScreensaverConfig(screensaverConfig);
      }
      if (Array.isArray(screensaverGrid)) {
        await replaceScreensaverGridForImport(
          screensaverGrid, screensaverBlock?.source_layout || null);
      }

      try { localStorage.removeItem('tileDrafts'); } catch (e) {}
      showNotification(t('importComplete'));
      setTimeout(() => location.reload(), 600);
    } catch (e) {
      console.error('Tile import failed:', e);
      showNotification(t('importFailed'), false);
    }
  }

  async function postTile(folderId, index, tile, sourceToTarget = null) {
    const fd = new FormData();
    const type = Number(tile.type);
    let safeType = isNaN(type) ? 0 : type;
    if (safeType === 4 && tile.navigate_kind !== undefined && tile.navigate_kind !== null) {
      const kind = Number(tile.navigate_kind);
      if (kind === 1) safeType = 7;
      else if (kind === 2) safeType = 8;
    }
    fd.append('folder', folderId);
    fd.append('index', index);
    fd.append('type', safeType);
    fd.append('title', tile.title || '');
    fd.append('icon_name', tile.icon_name || '');
    const parsedBgColor = parseBgColorValue(tile.bg_color);
    if (parsedBgColor !== 0 || (typeof tile.bg_color === 'string' && tile.bg_color.trim().startsWith('#'))) {
      fd.append('bg_color', parsedBgColor);
    } else {
      fd.append('bg_color_default', '1');
    }
    const layout = normalizeTileLayout(tile, index, tabByFolder[folderId] || '');
    fd.append('col', layout.col);
    fd.append('row', layout.row);
    fd.append('span_w', layout.span_w);
    fd.append('span_h', layout.span_h);
    if (tile.background_opacity !== undefined && tile.background_opacity !== null) {
      fd.append('background_opacity', tile.background_opacity);
    }

    if ([21, 22, 23].includes(safeType)) fd.append('sensor_value_font', tile.sensor_value_font ?? 2);
    if (safeType === 1) {
      fd.append('sensor_entity', tile.sensor_entity || '');
      fd.append('sensor_unit', tile.sensor_unit || '');
      const dec = tile.sensor_decimals;
      if (dec !== undefined && dec !== null && Number(dec) >= 0) {
        fd.append('sensor_decimals', dec);
      }
      if (tile.sensor_value_font !== undefined && tile.sensor_value_font !== null) {
        fd.append('sensor_value_font', tile.sensor_value_font);
      }
      const isScreensaverTile =
        Number(folderId) === Number(SCREENSAVER_FOLDER_ID);
      const displayMode = isScreensaverTile
        ? 0
        : ((tile.sensor_display_mode !== undefined) ? tile.sensor_display_mode : (tile.sensor_gauge ? 1 : 0));
      fd.append('sensor_display_mode', String(displayMode));
      if (tile.sensor_gauge_min !== undefined && tile.sensor_gauge_min !== null && String(tile.sensor_gauge_min).length > 0) {
        fd.append('sensor_gauge_min', tile.sensor_gauge_min);
      }
      if (tile.sensor_gauge_max !== undefined && tile.sensor_gauge_max !== null && String(tile.sensor_gauge_max).length > 0) {
        fd.append('sensor_gauge_max', tile.sensor_gauge_max);
      }
      if (tile.sensor_gauge_arc !== undefined && tile.sensor_gauge_arc !== null && String(tile.sensor_gauge_arc).length > 0) {
        fd.append('sensor_gauge_arc', tile.sensor_gauge_arc);
      }
      if (tile.sensor_gauge_size !== undefined && tile.sensor_gauge_size !== null && String(tile.sensor_gauge_size).length > 0) {
        fd.append('sensor_gauge_size', tile.sensor_gauge_size);
      }
      if (tile.sensor_gauge_y_offset !== undefined && tile.sensor_gauge_y_offset !== null && String(tile.sensor_gauge_y_offset).length > 0) {
        fd.append('sensor_gauge_y_offset', tile.sensor_gauge_y_offset);
      }
      if (tile.sensor_value_y_offset !== undefined && tile.sensor_value_y_offset !== null && String(tile.sensor_value_y_offset).length > 0) {
        fd.append('sensor_value_y_offset', tile.sensor_value_y_offset);
      }
      if (tile.sensor_graph_height !== undefined && tile.sensor_graph_height !== null && String(tile.sensor_graph_height).length > 0) {
        fd.append('sensor_graph_height', tile.sensor_graph_height);
      }
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType >= 21 && safeType <= 23) {
      const kind = ['number', 'select', 'datetime'][safeType - 21];
      fd.append(kind + '_entity', tile.sensor_entity || tile[kind + '_entity'] || '');
      fd.append('popup_open_mode', tile.popup_open_mode ?? 1);
    } else if (safeType === 20) {
      fd.append(
        'binary_sensor_entity',
        tile.sensor_entity || tile.binary_sensor_entity || '');
      if (tile.popup_open_mode !== undefined &&
          tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 2) {
      fd.append('scene_alias', tile.scene_alias || '');
    } else if (safeType === 4) {
      const rawTarget = Number(tile.navigate_target);
      let target = 0;
      if (!isNaN(rawTarget) && rawTarget > 0) {
        if (sourceToTarget && sourceToTarget[rawTarget] !== undefined) {
          target = sourceToTarget[rawTarget];
        }
      }
      fd.append('navigate_target', target);
    } else if (safeType === 5) {
      fd.append('switch_entity', tile.sensor_entity || '');
      const style = (tile.switch_style !== undefined && tile.switch_style !== null)
        ? tile.switch_style
        : (tile.sensor_decimals === 1 ? 1 : 0);
      fd.append('switch_style', style);
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 10) {
      fd.append('text_value', tile.text_value || tile.scene_alias || tile.key_macro || '');
      fd.append('text_value_font', tile.text_value_font || tile.sensor_value_font || '0');
    } else if (safeType === 9) {
      fd.append('clock_show_time', ((Number(tile.sensor_decimals || 1) & 1) !== 0) ? '1' : '0');
      fd.append('clock_show_date', ((Number(tile.sensor_decimals || 1) & 2) !== 0) ? '1' : '0');
      fd.append('key_code', tile.key_code || 40);
      fd.append('key_modifier', tile.key_modifier || 20);
      fd.append('clock_time_format', (tile.sensor_gauge_min !== undefined && tile.sensor_gauge_min !== null) ? tile.sensor_gauge_min : 0);
      fd.append('clock_date_format', (tile.sensor_gauge_max !== undefined && tile.sensor_gauge_max !== null) ? tile.sensor_gauge_max : 0);
    } else if (safeType === 12) {
      fd.append('weather_entity', tile.sensor_entity || tile.weather_entity || '');
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 14) {
      fd.append('energy_entity', tile.sensor_entity || tile.energy_entity || '');
      fd.append('sensor_unit', tile.sensor_unit || '');
      const dec = tile.sensor_decimals;
      fd.append('sensor_decimals', (dec !== undefined && dec !== null && Number(dec) >= 0) ? dec : '1');
      if (tile.sensor_value_font !== undefined && tile.sensor_value_font !== null) {
        fd.append('sensor_value_font', tile.sensor_value_font);
      }
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
      if (tile.sensor_value_y_offset !== undefined && tile.sensor_value_y_offset !== null && String(tile.sensor_value_y_offset).length > 0) {
        fd.append('sensor_value_y_offset', tile.sensor_value_y_offset);
      }
    } else if (safeType === MEDIA_TILE_TYPE) {
      fd.append('media_entity', tile.sensor_entity || tile.media_entity || '');
    } else if (safeType === 17) {
      fd.append('climate_entity', tile.sensor_entity || tile.climate_entity || '');
      fd.append(
        'climate_slots_packed',
        (tile.sensor_gauge_min !== undefined &&
         tile.sensor_gauge_min !== null)
          ? tile.sensor_gauge_min : 0);
      fd.append(
        'climate_layouts_packed',
        getClimateLayoutPayload(tile.sensor_gauge_max));
      fd.append(
        'climate_geometry',
        tile.climate_geometry || tile.scene_alias || '');
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 19) {
      fd.append('cover_entity', tile.sensor_entity || tile.cover_entity || '');
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 18) {
      fd.append('camera_entity', tile.sensor_entity || tile.camera_entity || '');
    } else if (safeType === 16) {
      fd.append('animation_file', tile.animation_file || tile.scene_alias || '');
      fd.append('animation_fps', tile.animation_fps || tile.image_slideshow_sec || '10');
      fd.append('animation_fit',
        (tile.animation_fit !== undefined && tile.animation_fit !== null)
          ? tile.animation_fit
          : (tile.sensor_display_mode || '0'));
      fd.append('animation_zoom',
        (tile.animation_zoom !== undefined && tile.animation_zoom !== null)
          ? tile.animation_zoom
          : (tile.sensor_gauge_max || '100'));
    }

    const res = await fetch('/api/tiles', { method: 'POST', body: fd });
    const data = await res.json();
    if (!data.success) {
      throw new Error('Tile speichern fehlgeschlagen');
    }
  }

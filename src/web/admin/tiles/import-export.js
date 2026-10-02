
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

  // Whether the Settings tile is hidden, and the parked tile then: the
  // import restores both (issue #70). The page holds them in the Home tab's
  // parking slot, rendered from the device config.
  function exportSettingsTileState() {
    const parked = document.getElementById('settingsHiddenTile');
    if (!parked) return undefined;
    if (parked.dataset.hidden !== '1') return { hidden: false };
    return {
      hidden: true,
      title: String(parked.dataset.title || ''),
      icon_name: String(parked.dataset.icon || ''),
      bg_color: Number(parked.dataset.bgColor || 0),
      col: Number(parked.dataset.col || 0),
      row: Number(parked.dataset.row || 0),
      span_w: Number(parked.dataset.spanW || 1),
      span_h: Number(parked.dataset.spanH || 1)
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
        settings_tile: exportSettingsTileState(),
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
      // A folder its folder tile already mapped keeps that exact target; two
      // folders with the same name and icon would otherwise share one.
      if (sourceToTarget[sourceId] !== undefined) return;
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

  // The type postTile sends: old exports stored folder, Settings and Back
  // tiles as a navigate tile with a kind.
  function importTileType(tile) {
    const type = Number(tile && tile.type);
    if (isNaN(type)) return 0;
    if (type === 4 && tile.navigate_kind !== undefined && tile.navigate_kind !== null) {
      const kind = Number(tile.navigate_kind);
      if (kind === 1) return 7;
      if (kind === 2) return 8;
    }
    return type;
  }

  // A tile as the import messages name it: its title, else its type.
  function importTileName(tile) {
    const title = String(tile?.title || '').replace(/\s+/g, ' ').trim();
    if (title) return title;
    const type = importTileType(tile);
    const option = document.querySelector('select[id$="_tile_type"] option[value="' + type + '"]');
    return option ? option.textContent.trim() : String(type);
  }

  class TileImportError extends Error {
    constructor(message, kind, tile, folderName) {
      super(message);
      this.kind = kind;
      this.tile = tile;
      this.folderName = folderName;
    }
  }

  // One Home or folder grid of an import, planned before anything is
  // written: every tile where postTile will place it, and where the
  // target's system tile (Settings in Home, Back in a folder) ends up. A
  // system tile in the export takes its exported place. Without one (an
  // export with the Settings tile hidden) the target's own stays where it
  // is, or moves to the nearest free place when an imported tile needs its
  // cell (issue #70). A tile that cannot be placed is returned as the
  // conflict, as the server would refuse it (tile_geometry::supported,
  // placementOverlaps).
  function planImportGrid(sourceTiles, systemType, targetSystem, tab) {
    const tileCount = GRID_COLS * GRID_ROWS;
    const occupied = Array.from({ length: GRID_ROWS * 2 }, () => Array(GRID_COLS * 2).fill(false));
    const cells = (layout, mark) => {
      for (let y = layout.row * 2; y < (layout.row + layout.span_h) * 2; y++) {
        for (let x = layout.col * 2; x < (layout.col + layout.span_w) * 2; x++) {
          if (!mark && occupied[y][x]) return false;
          if (mark) occupied[y][x] = true;
        }
      }
      return true;
    };
    const fits = (type, layout) => supportedTileLayout(type, layout) &&
      layout.col + layout.span_w <= GRID_COLS && layout.row + layout.span_h <= GRID_ROWS &&
      cells(layout, false);

    const entries = [];
    let sourceSystem = null;
    (Array.isArray(sourceTiles) ? sourceTiles : []).forEach((tile, index) => {
      const type = importTileType(tile);
      if (!type) return;
      const entry = { tile, type, layout: normalizeTileLayout({ ...tile, type }, index, tab) };
      if (type === systemType && !sourceSystem) sourceSystem = entry;
      else entries.push(entry);
    });
    // Settings belongs to Home, Back to folders; one of each.
    const misplaced = entries.find(entry => entry.type === 7 || entry.type === 8);
    if (misplaced) return { conflict: misplaced.tile };
    if (entries.length + (sourceSystem || targetSystem ? 1 : 0) > tileCount) {
      return { conflict: entries[entries.length - 1].tile };
    }

    // An exported system tile replaces the target's; a target without one
    // (Settings hidden there) keeps it hidden and the place stays free.
    const placeSystem = !!(sourceSystem && targetSystem);
    if (placeSystem) {
      if (!fits(systemType, sourceSystem.layout)) return { conflict: sourceSystem.tile };
      cells(sourceSystem.layout, true);
    }
    for (const entry of entries) {
      if (!fits(entry.type, entry.layout)) return { conflict: entry.tile };
      cells(entry.layout, true);
    }
    const tiles = entries.map(entry => ({ ...entry.tile, ...entry.layout }));
    if (placeSystem) return { tiles, system: { ...sourceSystem.tile, ...sourceSystem.layout } };
    if (!targetSystem) return { tiles, system: null };

    const current = normalizeTileLayout(targetSystem, 0, tab);
    if (fits(systemType, current)) return { tiles, system: null };
    let best = null;
    for (let row = 0; row + current.span_h <= GRID_ROWS; row += 0.5) {
      for (let col = 0; col + current.span_w <= GRID_COLS; col += 0.5) {
        const layout = { ...current, col, row };
        const distance = Math.abs(col - current.col) + Math.abs(row - current.row);
        if ((!best || distance < best.distance) && fits(systemType, layout)) best = { layout, distance };
      }
    }
    if (!best) return { conflict: targetSystem };
    return { tiles, system: { ...targetSystem, ...best.layout } };
  }

  // Writes a planned grid over its current tiles: the old tiles go first
  // so no imported tile meets one of them, then the system tile takes its
  // place, then the imported tiles. Empty cells are not written. A folder
  // tile maps its exported folder to the one the server created for it.
  async function applyImportGrid(folderId, currentTiles, plan, systemType, folderName, sourceToTarget = null) {
    const tileCount = GRID_COLS * GRID_ROWS;
    const systemIndex = currentTiles.findIndex(tile => importTileType(tile) === systemType);
    const post = async (index, tile) => {
      try {
        return await postTile(folderId, index, tile, sourceToTarget);
      } catch (e) {
        throw new TileImportError(e.message, 'stopped', tile, folderName);
      }
    };
    for (let i = 0; i < tileCount; i++) {
      if (i !== systemIndex && importTileType(currentTiles[i]) !== 0) {
        await post(i, buildEmptyImportTile(i));
      }
    }
    if (systemIndex >= 0 && plan.system) await post(systemIndex, plan.system);
    const freeIndices = [];
    for (let i = 0; i < tileCount; i++) {
      if (i !== systemIndex) freeIndices.push(i);
    }
    for (let i = 0; i < plan.tiles.length; i++) {
      const tile = plan.tiles[i];
      const data = await post(freeIndices[i], tile);
      const sourceTarget = Number(tile.navigate_target);
      const target = Number(data && data.navigate_target);
      if (sourceToTarget && importTileType(tile) === 4 && sourceTarget > 0 && target > 0 &&
          sourceToTarget[sourceTarget] === undefined) {
        sourceToTarget[sourceTarget] = target;
      }
    }
  }

  // Hides or shows the Settings tile through the access settings save, which
  // keeps the PIN and the swipe gesture as they are; a shown tile takes the
  // exported place, a hidden one the exported parked tile.
  async function setSettingsTileHiddenForImport(hidden, snapshot, target, folderName) {
    const saved = typeof saveSettingsAccess === 'function' && typeof readSettingsAccessState === 'function'
      ? await saveSettingsAccess(null, target, snapshot, { ...readSettingsAccessState(), tileHidden: hidden }, false)
      : false;
    if (!saved) {
      throw new TileImportError('Settings tile visibility not saved', 'stopped',
                                { type: 7, title: snapshot?.title || '' }, folderName);
    }
  }

  // A folder created by the import gets its Back tile in the first free
  // cell, the top-left one of an empty grid (TileConfig::ensureBackTile).
  const NEW_FOLDER_BACK_TILE = { type: 8, title: '', icon_name: 'arrow-left', col: 0, row: 0, span_w: 1, span_h: 1 };

  async function replaceFolderGridForImport(folderId, sourceTiles, systemType, folderName, sourceToTarget = null) {
    const currentTiles = await fetchTilesForImport(folderId);
    const targetSystem = currentTiles.find(tile => importTileType(tile) === systemType) || null;
    const plan = planImportGrid(sourceTiles, systemType, targetSystem, tabByFolder[folderId] || '');
    if (plan.conflict) throw new TileImportError('Import layout conflict', 'stopped', plan.conflict, folderName);
    await applyImportGrid(folderId, currentTiles, plan, systemType, folderName, sourceToTarget);
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
    const occupied = Array.from({ length: GRID_ROWS * 2 }, () => Array(GRID_COLS * 2).fill(false));
    const prepared = [];
    for (const entry of sourceEntries) {
      if (prepared.length >= tileCount) {
        throw new TileImportError('Screensaver grid does not fit', 'conflict', entry.tile, t('importScreensaver'));
      }
      const tile = entry.tile;
      const mediaTile = Number(tile.type) === MEDIA_TILE_TYPE;
      const half = value => Math.round(Number(value || 1) * 2) / 2;
      let spanW = Math.max(1, half(tile.span_w));
      let spanH = Math.max(supportsHalfSize(tile.type) ? 0.5 : 1, half(tile.span_h));
      if (mediaTile) {
        spanW = Math.max(MEDIA_TILE_MIN_SPAN, spanW);
        spanH = Math.max(MEDIA_TILE_MIN_SPAN, spanH);
      }
      spanW = Math.min(spanW, GRID_COLS, mediaTile ? MEDIA_TILE_MAX_SPAN : GRID_COLS);
      spanH = Math.min(spanH, 2, mediaTile ? MEDIA_TILE_MAX_SPAN : 2);

      const sourceSpanW = Math.max(1, half(tile.span_w));
      const sourceColRange = Math.max(0, sourceCols - sourceSpanW);
      const targetColRange = Math.max(0, GRID_COLS - spanW);
      const relativeCol = sourceColRange > 0
        ? Math.max(0, Math.min(1, Number(tile.col || 0) / sourceColRange))
        : 0;
      const desiredCol = Math.round(relativeCol * targetColRange);
      const sourceRowOffset = Math.max(0, Math.min(1, Number(tile.row || 0) - firstSourceRow));
      const desiredRow = Math.min(GRID_ROWS - spanH, firstTargetRow + sourceRowOffset);

      let best = null;
      for (let row = firstTargetRow; row <= GRID_ROWS - spanH; row += 0.5) {
        for (let col = 0; col <= GRID_COLS - spanW; col += 0.5) {
          let free = true;
          for (let y = row * 2; y < (row + spanH) * 2 && free; y++) {
            for (let x = col * 2; x < (col + spanW) * 2; x++) {
              if (occupied[y][x]) { free = false; break; }
            }
          }
          if (!free) continue;
          const score = Math.abs(row - desiredRow) * (GRID_COLS + 1) + Math.abs(col - desiredCol);
          if (!best || score < best.score) best = { row, col, score };
        }
      }
      if (!best) {
        throw new TileImportError('Screensaver grid does not fit', 'conflict', tile, t('importScreensaver'));
      }
      for (let y = best.row * 2; y < (best.row + spanH) * 2; y++) {
        for (let x = best.col * 2; x < (best.col + spanW) * 2; x++) occupied[y][x] = true;
      }
      prepared.push({
        targetIndex: prepared.length,
        tile: { ...tile, col: best.col, row: best.row, span_w: spanW, span_h: spanH }
      });
    }
    return prepared;
  }

  // The screensaver grid as the import writes it, checked before anything
  // is written like the Home and folder grids.
  function planScreensaverImport(sourceTiles, sourceLayout) {
    const preparedTiles = prepareScreensaverTilesForImport(sourceTiles, sourceLayout);
    const supportedTypes = new Set([1, 2, 5, 14, 20, 21, 22, 23, MEDIA_TILE_TYPE]);
    for (const entry of preparedTiles) {
      if (!supportedTypes.has(Number(entry.tile.type || 0))) {
        throw new TileImportError('Unsupported screensaver tile type', 'conflict', entry.tile, t('importScreensaver'));
      }
    }
    return preparedTiles;
  }

  async function replaceScreensaverGridForImport(preparedTiles) {
    const folderId = SCREENSAVER_FOLDER_ID;
    const currentTiles = await fetchTilesForImport(folderId);
    const tileCount = GRID_COLS * GRID_ROWS;
    const post = async (index, tile) => {
      try {
        await postTile(folderId, index, tile);
      } catch (e) {
        throw new TileImportError(e.message, 'stopped', tile, t('importScreensaver'));
      }
    };

    // Remove the existing tiles first so the imported positions do not fail on
    // temporary overlaps with the old grid.
    for (let i = 0; i < tileCount; i++) {
      if (Number(currentTiles[i]?.type || 0) !== 0) {
        await post(i, buildEmptyImportTile(i));
      }
    }

    for (const entry of preparedTiles) {
      await post(entry.targetIndex, entry.tile);
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

      const sourceFolders = Array.isArray(payload.folders) ? payload.folders : [{ id: 0, parent_id: 0, name: 'Home', icon_name: '' }];
      const folderName = folderId => {
        const folder = sourceFolders.find(entry => Number(entry && entry.id) === Number(folderId));
        return String(folder?.name || '') || String(folderId);
      };
      const sourceFolderIds = sourceFolders
        .map(folder => parseInt(folder && folder.id, 10))
        .filter(folderId => !isNaN(folderId) && folderId !== 0 && Array.isArray(grids[String(folderId)]));
      // Versions 1 and 2 had no screensaver block and stay importable
      // unchanged. Alternative flat field names are accepted as well, in case an
      // intermediate state of this export function was used.
      const screensaverBlock = payload.screensaver && typeof payload.screensaver === 'object'
        ? payload.screensaver
        : null;
      const screensaverConfig = screensaverBlock?.config || payload.screensaver_config;
      const screensaverGrid = screensaverBlock?.grid || payload.screensaver_grid;

      // The Settings tile follows the export (issue #70): hidden there is
      // hidden here, shown there comes back at its exported place. Exports
      // without the flag tell it by their Home grid; a Settings tile in the
      // grid always means shown.
      const homeSource = Array.isArray(grids['0']) ? grids['0'] : null;
      const sourceSettings = homeSource ? homeSource.find(tile => importTileType(tile) === 7) || null : null;
      const exportedSettings = payload.settings_tile && typeof payload.settings_tile === 'object'
        ? payload.settings_tile : null;
      const settingsHidden = !!homeSource && !sourceSettings;
      const parkedSettings = settingsHidden && exportedSettings?.hidden === true ? exportedSettings : null;

      // The whole layout is checked before anything is written: Home with
      // the Settings tile where the export has it, every folder against the
      // Back tile a new folder gets (Home replaces every folder), and the
      // screensaver grid. A tile that does not fit stops the import here.
      let homeTiles = homeSource ? await fetchTilesForImport(0) : null;
      const targetSettings = homeTiles ? homeTiles.find(tile => importTileType(tile) === 7) || null : null;
      const homePlan = homeSource
        ? planImportGrid(homeSource, 7, settingsHidden ? null : (targetSettings || { type: 7 }), tabByFolder[0] || '')
        : null;
      if (homePlan?.conflict) throw new TileImportError('Import layout conflict', 'conflict', homePlan.conflict, folderName(0));
      for (const folderId of sourceFolderIds) {
        const plan = planImportGrid(grids[String(folderId)], 8, NEW_FOLDER_BACK_TILE, '');
        if (plan.conflict) throw new TileImportError('Import layout conflict', 'conflict', plan.conflict, folderName(folderId));
      }
      const screensaverTiles = Array.isArray(screensaverGrid)
        ? planScreensaverImport(screensaverGrid, screensaverBlock?.source_layout || null)
        : null;

      showNotification(t('importRunning'));

      const sourceToTarget = { 0: 0 };
      if (homePlan) {
        // Hiding first frees the Settings tile's cell for the imported tiles;
        // a hidden one takes the exported parked tile.
        if (settingsHidden && (targetSettings || parkedSettings)) {
          await setSettingsTileHiddenForImport(true, parkedSettings, null, folderName(0));
          if (targetSettings) homeTiles = await fetchTilesForImport(0);
        }
        await applyImportGrid(0, homeTiles, homePlan, 7, folderName(0), sourceToTarget);
        if (!settingsHidden && !targetSettings) {
          await setSettingsTileHiddenForImport(
            false, homePlan.system, { col: homePlan.system.col, row: homePlan.system.row }, folderName(0));
        }
      }

      // A folder tile names the folder the server created for it; exports
      // without folder targets match folders by name and icon.
      let targetFolders = await fetchFoldersForImport();
      updateFolderImportMap(sourceFolders, targetFolders, sourceToTarget);

      const pendingFolderIds = sourceFolderIds.slice();
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
          await replaceFolderGridForImport(
            targetFolderId, grids[String(sourceFolderId)], 8, folderName(sourceFolderId), sourceToTarget);
          pendingFolderIds.splice(i, 1);
          progressed = true;
          targetFolders = await fetchFoldersForImport();
          updateFolderImportMap(sourceFolders, targetFolders, sourceToTarget);
        }
      }
      // A folder no folder tile of the export leads to cannot be reached on
      // the device either; it stays out instead of failing the import.
      if (pendingFolderIds.length) {
        console.warn('Import skipped unreachable folders:', pendingFolderIds);
      }

      if (screensaverConfig && typeof screensaverConfig === 'object') {
        await importScreensaverConfig(screensaverConfig);
      }
      if (screensaverTiles) {
        await replaceScreensaverGridForImport(screensaverTiles);
      }

      try { localStorage.removeItem('tileDrafts'); } catch (e) {}
      showNotification(t('importComplete'));
      setTimeout(() => location.reload(), 600);
    } catch (e) {
      console.error('Tile import failed:', e);
      if (e instanceof TileImportError) {
        const message = t(e.kind === 'conflict' ? 'importConflict' : 'importStopped');
        showNotification(message.replace('{tile}', importTileName(e.tile)).replace('{folder}', e.folderName), false);
      } else {
        showNotification(t('importFailed'), false);
      }
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
    if (tile.icon_disc !== undefined && tile.icon_disc !== null) {
      fd.append('icon_disc', tile.icon_disc);
    }
    if (tile.icon_glow !== undefined && tile.icon_glow !== null) {
      fd.append('icon_glow', ['0', 'false'].includes(String(tile.icon_glow)) ? '0' : '1');
    }
    const parsedBgColor = parseBgColorValue(tile.bg_color);
    if (parsedBgColor !== 0 || (typeof tile.bg_color === 'string' && tile.bg_color.trim().startsWith('#'))) {
      fd.append('bg_color', parsedBgColor);
    } else {
      fd.append('bg_color_default', '1');
    }
    const layout = normalizeTileLayout({ ...tile, type: safeType }, index, tabByFolder[folderId] || '');
    fd.append('col', layout.col);
    fd.append('row', layout.row);
    fd.append('span_w', layout.span_w);
    fd.append('span_h', layout.span_h);
    if (tile.background_opacity !== undefined && tile.background_opacity !== null) {
      fd.append('background_opacity', tile.background_opacity);
    }
    // Per-tile icon colors (Sensor family, Binary sensor, Energy); older
    // exports without the field import without icon colors.
    if (typeof tile.icon_colors === 'string') fd.append('icon_colors', tile.icon_colors);

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
      fd.append('sensor_value_font', tile.sensor_value_font ?? 0);
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
      fd.append('sensor_value_font', tile.sensor_value_font ?? 0);
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 8) {
      fd.append('tile_border', Number(tile.sensor_display_mode) === 1 ? '0' : '1');
    } else if (safeType === 10) {
      fd.append('text_value', tile.text_value || tile.scene_alias || tile.key_macro || '');
      fd.append('text_value_font', tile.text_value_font || tile.sensor_value_font || '0');
      fd.append('tile_border', Number(tile.sensor_display_mode) === 1 ? '0' : '1');
    } else if (safeType === 9) {
      fd.append('tile_border', Number(tile.sensor_display_mode) === 1 ? '0' : '1');
      fd.append('clock_show_time', ((Number(tile.sensor_decimals || 1) & 1) !== 0) ? '1' : '0');
      fd.append('clock_show_date', ((Number(tile.sensor_decimals || 1) & 2) !== 0) ? '1' : '0');
      fd.append('key_code', tile.key_code || 40);
      fd.append('key_modifier', tile.key_modifier || 20);
      fd.append('clock_time_format', (tile.sensor_gauge_min !== undefined && tile.sensor_gauge_min !== null) ? tile.sensor_gauge_min : 0);
      fd.append('clock_date_format', (tile.sensor_gauge_max !== undefined && tile.sensor_gauge_max !== null) ? tile.sensor_gauge_max : 0);
    } else if (safeType === 12) {
      fd.append('weather_entity', tile.sensor_entity || tile.weather_entity || '');
      fd.append('weather_colored_icons', Number(tile.sensor_display_mode) === 1 ? '0' : '1');
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
    } else if (safeType === 24 || safeType === 25 || safeType === 26) {
      const kind = devicePreviewKind(safeType);
      fd.append(kind + '_entity', tile.sensor_entity || tile[kind + '_entity'] || '');
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
    const data = await res.json().catch(() => ({}));
    if (!data.success) {
      // The server's English reason stays in the log (importTilesPayload);
      // the page names the tile and folder in the user's language.
      throw new Error('Tile save failed: ' + (data.error || ('HTTP ' + res.status)));
    }
    return data;
  }

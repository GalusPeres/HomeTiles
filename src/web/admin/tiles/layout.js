  function clampInt(value, min, max, fallback) {
    const v = parseInt(value, 10);
    if (isNaN(v)) return fallback !== undefined ? fallback : min;
    if (v < min) return min;
    if (v > max) return max;
    return v;
  }

  function clampHalf(value, min, max, fallback) {
    const number = Number(value);
    return Number.isFinite(number) ? Math.max(min, Math.min(max, Math.round(number * 2) / 2)) : fallback;
  }
  function isCompactSensorType(type) { return [1, 14, 20].includes(Number(type)); }
  function supportedTileLayout(type, layout) {
    if (!layout || ![layout.col, layout.row, layout.span_w, layout.span_h].every(v => Number.isFinite(v) && v >= 0 && Number.isInteger(v * 2))) return false;
    if ([7,8].includes(Number(type)) && (!Number.isInteger(layout.col) || !Number.isInteger(layout.row))) return false;
    const fractionalSize = !Number.isInteger(layout.span_w) || !Number.isInteger(layout.span_h);
    return fractionalSize ? isCompactSensorType(type) && layout.span_w >= 1 && layout.span_h === 0.5 : layout.span_w >= 1 && layout.span_h >= 1;
  }
  function applyCompactSensorPreview(el, type, layout, mode = 0) {
    const compact = isCompactSensorType(type) && layout?.span_w >= 1 &&
      layout.span_h === 0.5;
    el.classList.toggle('sensor-compact', compact);
    el.classList.toggle('sensor-half', compact && layout.span_h === 0.5);
  }

  function normalizeLayoutForTileType(typeValue, col, row, spanW, spanH) {
    let safeCol = clampHalf(col, 0, GRID_COLS - 0.5, 0);
    let safeRow = clampHalf(row, 0, GRID_ROWS - 0.5, 0);
    let safeW = clampHalf(spanW, 0.5, GRID_COLS, 1);
    let safeH = clampHalf(spanH, 0.5, GRID_ROWS, 1);
    if (Number(typeValue) === MEDIA_TILE_TYPE) {
      const minW = Math.min(MEDIA_TILE_MIN_SPAN, GRID_COLS);
      const minH = Math.min(MEDIA_TILE_MIN_SPAN, GRID_ROWS);
      safeW = clampHalf(safeW, minW, Math.min(MEDIA_TILE_MAX_SPAN, GRID_COLS), minW);
      safeH = clampHalf(safeH, minH, Math.min(MEDIA_TILE_MAX_SPAN, GRID_ROWS), minH);
      safeCol = Math.min(safeCol, GRID_COLS - safeW);
      safeRow = Math.min(safeRow, GRID_ROWS - safeH);
    } else {
      safeW = Math.min(safeW, GRID_COLS - safeCol);
      safeH = Math.min(safeH, GRID_ROWS - safeRow);
    }
    return { col: safeCol, row: safeRow, span_w: safeW, span_h: safeH };
  }

  function constrainLayoutToTab(layout, tab) {
    const firstRow = firstAllowedGridRow(tab);
    if (layout.row < firstRow) layout.row = firstRow;
    if (layout.span_h > GRID_ROWS - layout.row) {
      layout.span_h = GRID_ROWS - layout.row;
    }
    return layout;
  }

  function normalizeTileLayout(tile, index, tab = currentTileTab) {
    const fallbackCol = index % GRID_COLS;
    const firstRow = firstAllowedGridRow(tab);
    const fallbackRow = Math.max(firstRow, Math.floor(index / GRID_COLS));
    const col = clampHalf(tile?.col, 0, GRID_COLS - 0.5, fallbackCol);
    const row = clampHalf(tile?.row, firstRow, GRID_ROWS - 0.5, fallbackRow);
    let spanW = clampHalf(tile?.span_w, 0.5, GRID_COLS, 1);
    let spanH = clampHalf(tile?.span_h, 0.5, GRID_ROWS, 1);
    return constrainLayoutToTab(
      normalizeLayoutForTileType(tile?.type, col, row, spanW, spanH), tab);
  }

  function setGridItemPosition(el, col, row, spanW, spanH) {
    if (!el) return;
    el.style.gridColumn = (col + 1) + ' / span ' + spanW;
    el.style.gridRow = (row + 1) + ' / span ' + spanH;
    el.dataset.col = String(col);
    el.dataset.row = String(row);
    el.dataset.spanW = String(spanW);
    el.dataset.spanH = String(spanH);
  }

  function setTileGridPosition(el, col, row, spanW, spanH) {
    setGridItemPosition(el, col, row, spanW, spanH);
    const fractional = [col, row, spanW, spanH].some(v => !Number.isInteger(v));
    el.classList.toggle('fractional-tile', fractional);
    for (const [name, value] of Object.entries({col, row, w: spanW, h: spanH})) el.style.setProperty('--tile-' + name, String(value));
    if (fractional) { el.style.gridColumn = 'auto'; el.style.gridRow = 'auto'; }

  }

  function getTileElementLayout(tab, index) {
    const el = document.getElementById(tab + '-tile-' + index);
    if (!el) return null;
    const col = clampHalf(el.dataset.col, 0, GRID_COLS - 0.5, null);
    const row = clampHalf(el.dataset.row, firstAllowedGridRow(tab), GRID_ROWS - 0.5, null);
    const spanW = clampHalf(el.dataset.spanW, 0.5, GRID_COLS, null);
    const spanH = clampHalf(el.dataset.spanH, 0.5, GRID_ROWS, null);
    if (col === null || row === null || spanW === null || spanH === null) return null;
    return { col, row, span_w: spanW, span_h: spanH };
  }

  function layoutTiles(tab, tiles) {
    if (!Array.isArray(tiles)) return;
    const occupied = Array.from({ length: GRID_ROWS * 2 }, () => Array(GRID_COLS * 2).fill(false));
    const emptyIndices = [];

    tiles.forEach((tile, idx) => {
      const typeNum = Number(tile?.type);
      if (!tile || isNaN(typeNum) || typeNum === 0) {
        emptyIndices.push(idx);
        return;
      }
      const layout = normalizeTileLayout(tile, idx, tab);
      const el = document.getElementById(tab + '-tile-' + idx);
      if (el) {
        setTileGridPosition(el, layout.col, layout.row, layout.span_w, layout.span_h);
        el.style.display = '';
      }
      for (let r = layout.row * 2; r < (layout.row + layout.span_h) * 2; r++) {
        for (let c = layout.col * 2; c < (layout.col + layout.span_w) * 2; c++) {
          if (r < GRID_ROWS * 2 && c < GRID_COLS * 2) occupied[r][c] = true;
        }
      }
    });

    const freeCells = [];
    // New tiles still start at 1x1, but their free slots can start on half cells.
    // Reserve each placeholder so adjacent click targets never overlap.
    for (let r = firstAllowedGridRow(tab) * 2; r + 1 < GRID_ROWS * 2; r++) {
      for (let c = 0; c + 1 < GRID_COLS * 2; c++) {
        if ([occupied[r][c], occupied[r][c+1], occupied[r+1][c], occupied[r+1][c+1]].some(Boolean)) continue;
        freeCells.push({ col: c / 2, row: r / 2 });
        occupied[r][c] = occupied[r][c+1] = occupied[r+1][c] = occupied[r+1][c+1] = true;
      }
    }

    emptyIndices.forEach((idx, i) => {
      const el = document.getElementById(tab + '-tile-' + idx);
      if (!el) return;
      if (i < freeCells.length) {
        const cell = freeCells[i];
        setTileGridPosition(el, cell.col, cell.row, 1, 1);
        el.style.display = '';
      } else {
        el.style.display = 'none';
      }
    });
  }

  function syncTileGridStructure(tab, tiles) {
    if (!Array.isArray(tiles)) return;
    tiles.forEach((tile, index) => {
      const el = document.getElementById(tab + '-tile-' + index);
      if (!el) return;
      el.dataset.index = String(index);
      el.dataset.type = String(tile?.type ?? 0);
    });
    layoutTiles(tab, tiles);
  }

  function normalizeLayoutInputs(tab) {
    const prefix = tab;
    const colEl = document.getElementById(prefix + '_tile_col');
    const rowEl = document.getElementById(prefix + '_tile_row');
    const spanWEl = document.getElementById(prefix + '_tile_span_w');
    const spanHEl = document.getElementById(prefix + '_tile_span_h');

    if (!colEl || !rowEl || !spanWEl || !spanHEl) {
      const fallback = getTileElementLayout(tab, currentTileIndex);
      if (fallback) return fallback;
      return { col: 0, row: 0, span_w: 1, span_h: 1 };
    }

    let col = clampHalf(colEl.value, 1, GRID_COLS, 1);
    const firstRow = firstAllowedGridRow(tab);
    let row = clampHalf(rowEl.value, firstRow + 1, GRID_ROWS + 0.5, firstRow + 1);
    let spanW = clampHalf(spanWEl.value, 0.5, GRID_COLS, 1);
    let spanH = clampHalf(spanHEl.value, 0.5, GRID_ROWS, 1);

    const typeValue = document.getElementById(prefix + '_tile_type')?.value || '0';
    const layout = constrainLayoutToTab(
      normalizeLayoutForTileType(typeValue, col - 1, row - 1, spanW, spanH),
      tab);
    col = layout.col + 1;
    row = layout.row + 1;
    spanW = layout.span_w;
    spanH = layout.span_h;

    colEl.value = String(col);
    rowEl.value = String(row);
    spanWEl.value = String(spanW);
    spanHEl.value = String(spanH);

    return { col: col - 1, row: row - 1, span_w: spanW, span_h: spanH };
  }

  function updateLayoutFromInputs(tab) {
    if (currentTileIndex === -1) return;
    const layout = normalizeLayoutInputs(tab);
    const tiles = getTilesData(tab);
    const tileEl = document.getElementById(tab + '-tile-' + currentTileIndex);
    if (tileEl && (!Array.isArray(tiles) || tiles.length === 0)) {
      setTileGridPosition(tileEl, layout.col, layout.row, layout.span_w, layout.span_h);
      return;
    }
    if (!Array.isArray(tiles) || currentTileIndex >= tiles.length) return;
    const tile = tiles[currentTileIndex] || {};
    const type = document.getElementById(tab + '_tile_type')?.value ?? tile.type;
    if (Number(type) !== 0 && (!supportedTileLayout(type, layout) || !canPlaceTileLayout(tab, currentTileIndex, layout))) {
      applyLayoutInputsFromLayout(tab, normalizeTileLayout(tile, currentTileIndex, tab), false);
      return;
    }
    tile.col = layout.col;
    tile.row = layout.row;
    tile.span_w = layout.span_w;
    tile.span_h = layout.span_h;
    const typeEl = document.getElementById(tab + '_tile_type');
    const typeNum = typeEl ? parseInt(typeEl.value, 10) : 0;
    tile.type = isNaN(typeNum) ? 0 : typeNum;
    tiles[currentTileIndex] = tile;
    layoutTiles(tab, tiles);
    syncTileSizePolicy(tab);
  }

  function applyLayoutInputsFromLayout(tab, layout, persistDraft = true) {
    if (!layout) return;
    const colEl = document.getElementById(tab + '_tile_col');
    const rowEl = document.getElementById(tab + '_tile_row');
    const spanWEl = document.getElementById(tab + '_tile_span_w');
    const spanHEl = document.getElementById(tab + '_tile_span_h');
    const colVal = String(layout.col + 1);
    const rowVal = String(layout.row + 1);
    if (colEl) colEl.value = colVal;
    if (rowEl) rowEl.value = rowVal;
    if (spanWEl && layout.span_w !== undefined) spanWEl.value = String(layout.span_w);
    if (spanHEl && layout.span_h !== undefined) spanHEl.value = String(layout.span_h);
    const tabDrafts = persistDraft ? drafts[tab] : null;
    if (tabDrafts && tabDrafts[currentTileIndex]) {
      tabDrafts[currentTileIndex].col = colVal;
      tabDrafts[currentTileIndex].row = rowVal;
      if (layout.span_w !== undefined) tabDrafts[currentTileIndex].span_w = String(layout.span_w);
      if (layout.span_h !== undefined) tabDrafts[currentTileIndex].span_h = String(layout.span_h);
      persistDrafts();
    }
  }

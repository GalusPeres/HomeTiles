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
  // Types that may use half-cell sizes (mirrors tile_geometry::half_size).
  function supportsHalfSize(type) { return isCompactSensorType(type) || Number(type) === 9; }
  // Every type resizes in half steps from 1x1; only half-size types may be half
  // a row high. Settings/Back stay whole (mirrors tile_geometry::supported).
  function supportedTileLayout(type, layout) {
    const values = layout ? [layout.col, layout.row, layout.span_w, layout.span_h] : [];
    if (!layout || !values.every(v => Number.isFinite(v) && v >= 0 && Number.isInteger(v * 2))) return false;
    if ([7, 8].includes(Number(type)) && values.some(v => !Number.isInteger(v))) return false;
    if (layout.span_w < 1) return false;
    return layout.span_h >= 1 || (supportsHalfSize(type) && layout.span_h === 0.5);
  }
  function applyCompactSensorPreview(el, type, layout, mode = 0) {
    const compact = isCompactSensorType(type) && layout?.span_w >= 1 &&
      layout.span_h === 0.5;
    el.classList.toggle('sensor-compact', compact);
    el.classList.toggle('sensor-half', compact && layout.span_h === 0.5);
    el.classList.toggle('clock-compact', Number(type) === 9 && layout?.span_w >= 1 &&
      layout.span_h === 0.5);
    if (Number(type) === 9) fitCompactClockPreview(el);
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
      // Keep at least a whole cell wide (and a whole row high unless the type
      // allows half a row), so clamping at the grid edge never yields 0.5.
      const type = Number(typeValue);
      const minH = (type === 0 || supportsHalfSize(type)) ? 0.5 : 1;
      safeW = Math.max(1, safeW);
      safeH = Math.max(minH, safeH);
      safeCol = Math.min(safeCol, GRID_COLS - 1);
      safeRow = Math.min(safeRow, GRID_ROWS - minH);
      if (type === 7 || type === 8) {
        safeCol = Math.floor(safeCol);
        safeRow = Math.floor(safeRow);
        safeW = Math.max(1, Math.floor(safeW));
        safeH = Math.max(1, Math.floor(safeH));
      }
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
      markOccupied(occupied, layout);
    });

    // A selected new tile keeps the spot the user picked (newTileSpot), even
    // when a grid re-render recreates its element. One further empty tile is
    // the free slot that follows the pointer (enableFreeSlotHover); it rests
    // on the first free spot so keyboard and touch users reach it.
    const editingNew = idx => currentTileTab === tab && currentTileIndex === idx;
    const empties = emptyIndices
      .map(idx => ({ idx, el: document.getElementById(tab + '-tile-' + idx) }))
      .filter(entry => entry.el)
      .sort((a, b) => editingNew(b.idx) - editingNew(a.idx));
    let freeEl = null;
    empties.forEach(({ idx, el }) => {
      delete el.dataset.freeSlot;
      el.classList.remove('free-slot-hover');
      const kept = editingNew(idx) && newTileSpot?.tab === tab && newTileSpot.index === idx
        ? newTileSpot.layout : null;
      if (kept && slotFits(tab, occupied, kept.col, kept.row, kept.span_w, kept.span_h)) {
        markOccupied(occupied, kept);
        setTileGridPosition(el, kept.col, kept.row, kept.span_w, kept.span_h);
        el.style.display = '';
        return;
      }
      const slot = freeEl ? null : firstFreeSlot(tab, occupied);
      if (slot) {
        freeEl = el;
        el.dataset.freeSlot = '1';
        setTileGridPosition(el, slot.col, slot.row, slot.span_w, slot.span_h);
        el.style.display = '';
      } else {
        el.style.display = 'none';
      }
    });
  }

  function markOccupied(occupied, layout) {
    for (let r = layout.row * 2; r < (layout.row + layout.span_h) * 2; r++) {
      for (let c = layout.col * 2; c < (layout.col + layout.span_w) * 2; c++) {
        if (r >= 0 && c >= 0 && r < GRID_ROWS * 2 && c < GRID_COLS * 2) occupied[r][c] = true;
      }
    }
  }

  function slotFits(tab, occupied, col, row, spanW, spanH) {
    if (col < 0 || row < firstAllowedGridRow(tab) ||
        col + spanW > GRID_COLS || row + spanH > GRID_ROWS) return false;
    for (let r = row * 2; r < (row + spanH) * 2; r++) {
      for (let c = col * 2; c < (col + spanW) * 2; c++) {
        if (occupied[r][c]) return false;
      }
    }
    return true;
  }

  // New tiles start as 1x1. The 1x0.5 slot is offered only where 1x1 does not
  // fit; choosing a type that needs more grows it (grownNewTileLayout).
  const FREE_SLOT_SIZES = [[1, 1], [1, 0.5]];

  // Spot of the new (still empty) tile open in the editor: {tab, index, layout}.
  let newTileSpot = null;

  // Smallest size a type accepts (Media needs 2x2, half-size types 1x0.5).
  function minimumTileSize(type) {
    if (Number(type) === MEDIA_TILE_TYPE) return [Math.min(MEDIA_TILE_MIN_SPAN, GRID_COLS), Math.min(MEDIA_TILE_MIN_SPAN, GRID_ROWS)];
    return [1, supportsHalfSize(type) ? 0.5 : 1];
  }

  // Layout a new half-height tile grows to for a type that needs more room:
  // downwards first, then upwards, then left. Null when it does not fit.
  function grownNewTileLayout(tab, type) {
    if (!newTileSpot || newTileSpot.tab !== tab || newTileSpot.index !== currentTileIndex ||
        !newTileSpot.layout) return null;
    const base = newTileSpot.layout;
    const [minW, minH] = minimumTileSize(type);
    if (base.span_w >= minW && base.span_h >= minH) return base;
    const spanW = Math.max(base.span_w, minW), spanH = Math.max(base.span_h, minH);
    for (const [dx, dy] of [[0, 0], [0, base.span_h - spanH], [base.span_w - spanW, 0], [base.span_w - spanW, base.span_h - spanH]]) {
      const layout = { col: base.col + dx, row: base.row + dy, span_w: spanW, span_h: spanH };
      if (supportedTileLayout(type, layout) && canPlaceTileLayout(tab, currentTileIndex, layout)) return layout;
    }
    return null;
  }

  // Pointer position in (fractional) grid cells.
  function pointerGridPoint(tab, clientX, clientY) {
    const metrics = getTileGridMetrics(tab);
    if (!metrics) return null;
    const x = (clientX - metrics.rect.left - metrics.padLeft + metrics.gapX / 2) / (metrics.cellW + metrics.gapX);
    const y = (clientY - metrics.rect.top - metrics.padTop + metrics.gapY / 2) / (metrics.cellH + metrics.gapY);
    return Number.isFinite(x) && Number.isFinite(y) ? { x, y } : null;
  }

  // The free slot centred under the pointer, snapped to half cells. Nearby
  // half steps that still cover the pointer are tried before a smaller size.
  function freeSlotNear(tab, occupied, point) {
    const snap = value => Math.round(value * 2) / 2;
    const offsets = [0, -0.5, 0.5];
    for (const [spanW, spanH] of FREE_SLOT_SIZES) {
      const baseCol = snap(point.x - spanW / 2);
      const baseRow = snap(point.y - spanH / 2);
      const candidates = [];
      for (const dy of offsets) {
        for (const dx of offsets) {
          const col = baseCol + dx, row = baseRow + dy;
          if (col <= point.x && point.x < col + spanW && row <= point.y && point.y < row + spanH) {
            candidates.push({ col, row, cost: Math.abs(dx) + Math.abs(dy) });
          }
        }
      }
      candidates.sort((a, b) => a.cost - b.cost);
      for (const { col, row } of candidates) {
        if (slotFits(tab, occupied, col, row, spanW, spanH)) {
          return { col, row, span_w: spanW, span_h: spanH };
        }
      }
    }
    return null;
  }

  function firstFreeSlot(tab, occupied) {
    for (const [spanW, spanH, step] of [[1, 1, 1], [1, 1, 0.5], [1, 0.5, 1], [1, 0.5, 0.5]]) {
      for (let r = firstAllowedGridRow(tab); r + spanH <= GRID_ROWS; r += step) {
        for (let c = 0; c + spanW <= GRID_COLS; c += step) {
          if (slotFits(tab, occupied, c, r, spanW, spanH)) {
            return { col: c, row: r, span_w: spanW, span_h: spanH };
          }
        }
      }
    }
    return null;
  }

  // Occupancy as currently shown, including unsaved local edits and a selected
  // new tile, but without the free slot itself.
  function occupiedFromGrid(tab, grid, freeEl) {
    const occupied = Array.from({ length: GRID_ROWS * 2 }, () => Array(GRID_COLS * 2).fill(false));
    grid.querySelectorAll(':scope > .tile[data-index]').forEach(el => {
      if (el === freeEl || el.style.display === 'none') return;
      if (Number(el.dataset.type || 0) === 0 && el.dataset.selected !== '1') return;
      const layout = getTileElementLayout(tab, parseInt(el.dataset.index, 10));
      if (layout) markOccupied(occupied, layout);
    });
    return occupied;
  }

  function freeSlotElement(grid) {
    const free = grid.querySelector(':scope > .tile.empty[data-free-slot="1"]:not([data-selected="1"])');
    if (free) return free;
    const spare = Array.from(grid.querySelectorAll(':scope > .tile.empty'))
      .find(el => el.dataset.selected !== '1' && el.style.display === 'none');
    if (spare) spare.dataset.freeSlot = '1';
    return spare || null;
  }

  // Moves the free slot to the pointer in half-cell steps. A click on it (or a
  // tap on free space) opens the editor for a new tile at exactly that spot.
  function enableFreeSlotHover(tab) {
    const grid = getTileGrid(tab);
    if (!grid || grid.dataset.freeSlotBound === '1') return;
    grid.dataset.freeSlotBound = '1';
    const placeAt = (clientX, clientY) => {
      const el = freeSlotElement(grid);
      if (!el) return null;
      const point = pointerGridPoint(tab, clientX, clientY);
      const slot = point && freeSlotNear(tab, occupiedFromGrid(tab, grid, el), point);
      if (!slot) {
        el.classList.remove('free-slot-hover');
        return null;
      }
      // Only one unselected placeholder may exist, so a stale one left by a
      // previous selection can never catch the click.
      grid.querySelectorAll(':scope > .tile.empty').forEach(other => {
        if (other === el || other.dataset.selected === '1') return;
        other.style.display = 'none';
        delete other.dataset.freeSlot;
        other.classList.remove('free-slot-hover');
      });
      setTileGridPosition(el, slot.col, slot.row, slot.span_w, slot.span_h);
      el.style.display = '';
      el.classList.add('free-slot-hover');
      return el;
    };
    grid.addEventListener('pointermove', event => {
      if (event.pointerType === 'touch' || resizeState || dragSource) return;
      const over = event.target.closest('.tile');
      if (over && over.parentElement === grid && !over.classList.contains('empty')) {
        grid.querySelector(':scope > .tile.free-slot-hover')?.classList.remove('free-slot-hover');
        return;
      }
      placeAt(event.clientX, event.clientY);
    });
    grid.addEventListener('pointerleave', () => {
      grid.querySelector(':scope > .tile.free-slot-hover')?.classList.remove('free-slot-hover');
    });
    grid.addEventListener('click', event => {
      if (event.target !== grid) return;
      const el = placeAt(event.clientX, event.clientY);
      if (el) selectTile(parseInt(el.dataset.index, 10), tab);
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
    if (newTileSpot && newTileSpot.tab === tab && newTileSpot.index === currentTileIndex) {
      newTileSpot.layout = layout;
    }
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

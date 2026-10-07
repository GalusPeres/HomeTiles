  // The shown grid (grid_layout.h): with the head bar the panel shows fewer,
  // larger cells. New places, moves and sizes stay inside it; stored
  // positions keep the full GRID_COLS x GRID_ROWS, so a tile outside the
  // shown grid is marked (tile-outside) and never moved by the editor. The
  // screensaver tab keeps its own grid.
  function headBarLayout() { return typeof HEAD_BAR === 'boolean' && HEAD_BAR; }
  function placeCols(tab = currentTileTab) {
    return headBarLayout() && !isScreensaverTileTab(tab) && typeof GRID_SHOWN_COLS === 'number'
      ? GRID_SHOWN_COLS : GRID_COLS;
  }
  function placeRows(tab = currentTileTab) {
    return headBarLayout() && !isScreensaverTileTab(tab) && typeof GRID_SHOWN_ROWS === 'number'
      ? GRID_SHOWN_ROWS : GRID_ROWS;
  }
  // A stored layout wholly inside the shown grid.
  function insideShownGrid(tab, layout) {
    return !!layout && layout.col + layout.span_w <= placeCols(tab) + 0.001 &&
      layout.row + layout.span_h <= placeRows(tab) + 0.001;
  }
  // Settings and Back give way to the head's gear and X (shownTileLayout).
  function hiddenByHeadBar(tab, type) {
    return headBarLayout() && !isScreensaverTileTab(tab) && [7, 8].includes(Number(type));
  }

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
  // Number, Select and Date/Time render through the Sensor tile
  // (tile_geometry::editable).
  function isEditableValueType(type) { return [21, 22, 23].includes(Number(type)); }
  // Types that may use half-cell sizes (mirrors tile_geometry::half_size).
  // Scene, Folder, Settings, Back and Camera show only an icon and a title;
  // Switch and Cover show their state (tile_geometry::compact_switch,
  // compact_cover).
  function supportsHalfSize(type) {
    return isCompactSensorType(type) || isEditableValueType(type) || [2, 4, 5, 7, 8, 9, 17, 18, 19, 24, 25, 26].includes(Number(type));
  }
  // Every type resizes in half steps from 1x1; only half-size types may be half
  // a row high (mirrors tile_geometry::supported).
  function supportedTileLayout(type, layout) {
    const values = layout ? [layout.col, layout.row, layout.span_w, layout.span_h] : [];
    if (!layout || !values.every(v => Number.isFinite(v) && v >= 0 && Number.isInteger(v * 2))) return false;
    if (layout.span_w < 1) return false;
    return layout.span_h >= 1 || (supportsHalfSize(type) && layout.span_h === 0.5);
  }
  // Half-height value size for a value size choice, like
  // compact_sensor_layout::value_step: the title size by default and for 20,
  // 24, or 28 for 28 and the larger choices (32, 40), which do not fit.
  function compactValueSize(choice) {
    const value = String(choice ?? '0');
    if (value === '2') return 24;
    return ['3', '4', '5'].includes(value) ? 28 : 20;
  }
  // The value size choices a tile shows: Default, 24 and 28 in half-height
  // tiles; Default, 20, 24, 32 and 40 otherwise (28 is the default there).
  // A choice the other size lacks moves to the one that looks the same.
  function syncCompactValueFontOptions(select, halfHeight) {
    if (!select?.options) return;
    const shown = halfHeight ? ['0', '2', '5'] : ['0', '1', '2', '3', '4'];
    for (const option of Array.from(select.options)) {
      const hidden = !shown.includes(option.value);
      option.hidden = hidden;
      option.disabled = hidden;
      if (option.value === '0') {
        option.textContent = option.textContent.replace(/^\d+(?= )/, halfHeight ? '20' : '28');
      }
    }
    const value = select.value;
    if (halfHeight && value === '1') select.value = '0';
    else if (halfHeight && (value === '3' || value === '4')) select.value = '5';
    else if (!halfHeight && value === '5') select.value = '0';
  }
  // Number, Select and Date/Time value sizes (1 = 20, 2 = 24, 0 = 28, 3 = 32,
  // 4 = 40): half-height tiles offer 20, 24 and 28 (editable_display_tile).
  function syncEditableValueFontOptions(select, halfHeight) {
    if (!select?.options) return;
    for (const option of Array.from(select.options)) {
      const hidden = halfHeight && (option.value === '3' || option.value === '4');
      option.hidden = hidden;
      option.disabled = hidden;
    }
    if (halfHeight && (select.value === '3' || select.value === '4')) select.value = '0';
  }
  // The Sensor value size choice that matches a Number, Select or Date/Time
  // size at half height (editable_display_tile).
  function editableCompactValueFont(choice) {
    const value = String(choice ?? '2');
    return value === '1' ? '0' : value === '2' ? '2' : '5';
  }
  function applyCompactSensorPreview(el, type, layout, mode = 0, valueFont = 0) {
    const halfHeight = layout?.span_w >= 1 && layout.span_h === 0.5;
    // A half-height icon-and-title tile (Scene, Folder, Settings, Back, Camera) uses the
    // half-height Sensor header: the icon in the corner disc and the title
    // (if any) centered beside it.
    const compactIconTitle = [2, 4, 7, 8, 18].includes(Number(type)) && halfHeight;
    // Half-height Switch: icon, title and state like a compact Sensor.
    const compactSwitch = [5, 17, 19, 24, 25, 26].includes(Number(type)) && halfHeight;
    // Number, Select and Date/Time like a compact Sensor.
    const compactEditable = isEditableValueType(type) && halfHeight;
    if (compactEditable) valueFont = editableCompactValueFont(valueFont);
    const compact = (isCompactSensorType(type) || compactIconTitle || compactSwitch || compactEditable) && halfHeight;
    el.classList.toggle('sensor-compact', compact);
    el.classList.toggle('sensor-half', compact);
    el.classList.toggle('compact-title-only', compactIconTitle);
    // Switch header layouts show their state beside the disc at the
    // half-height value sizes; from 1.5 rows like a Sensor tile at its value
    // sizes, the bar a third of the extra height higher
    // (switch_layout::sensor_look).
    // The Cover position bar uses the same header (tile_header.h).
    // Lock, Alarm panel and Fan too (types/device).
    const switchHeader = [5, 19, 24, 25, 26].includes(Number(type)) && el.classList.contains('switch-bar');
    const switchTall = switchHeader && Number(layout?.span_h) > 1;
    el.classList.toggle('switch-tall', switchTall);
    if (switchTall) el.style?.setProperty?.('--switch-span-h', String(Number(layout.span_h)));
    else el.style?.removeProperty?.('--switch-span-h');
    const tallSize = switchTall ? ({1: 20, 2: 24, 3: 32, 4: 40}[Number(valueFont)] || 28) : 0;
    for (const size of [20, 24, 32, 40]) el.classList.toggle('switch-value-' + size, tallSize === size);
    const valueSize = (compact && !compactIconTitle) || (switchHeader && !switchTall) ? compactValueSize(valueFont) : 20;
    el.classList.toggle('compact-value-24', valueSize === 24);
    el.classList.toggle('compact-value-28', valueSize === 28);
    el.classList.toggle('clock-compact', Number(type) === 9 && halfHeight);
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
    // Outside the shown grid: placed like a half step (absolute, not a grid
    // track), so it shows beside the screen instead of widening it.
    const headBar = headBarLayout();
    const tab = headBar ? (el.closest?.('[id^="tab-tiles-"]')?.id?.slice('tab-tiles-'.length) || currentTileTab) : '';
    const outside = headBar && !el.classList.contains('empty') &&
      !insideShownGrid(tab, {col, row, span_w: spanW, span_h: spanH});
    el.classList.toggle('tile-outside', outside);
    if (outside) {
      el.title = t('headBarTileOutside');
      el.dataset.outsideTitle = '1';
    } else if (el.dataset.outsideTitle) {
      el.removeAttribute('title');
      delete el.dataset.outsideTitle;
    }
    el.classList.toggle('tile-bar-hidden', headBar && hiddenByHeadBar(tab, el.dataset.type));
    const fractional = outside || [col, row, spanW, spanH].some(v => !Number.isInteger(v));
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
        col + spanW > placeCols(tab) || row + spanH > placeRows(tab)) return false;
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
      for (let r = firstAllowedGridRow(tab); r + spanH <= placeRows(tab); r += step) {
        for (let c = 0; c + spanW <= placeCols(tab); c += step) {
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
    // A selected new tile still of type Empty does not block the free slot:
    // the pointer may pick a spot half a cell next to or over it, and a click
    // moves the new tile there. Once a type is chosen it blocks like a tile.
    const selectedIsEmpty =
      String(document.getElementById(tab + '_tile_type')?.value ?? '0') === '0';
    grid.querySelectorAll(':scope > .tile[data-index]').forEach(el => {
      if (el === freeEl || el.style.display === 'none') return;
      if (Number(el.dataset.type || 0) === 0 &&
          (el.dataset.selected !== '1' || selectedIsEmpty)) return;
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

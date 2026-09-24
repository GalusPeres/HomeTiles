
  function rectsOverlap(a, b) {
    if (!a || !b) return false;
    return !(a.col + a.span_w <= b.col ||
             b.col + b.span_w <= a.col ||
             a.row + a.span_h <= b.row ||
             b.row + b.span_h <= a.row);
  }

  function canPlaceGridLayout(
      layouts, activeIndices, index, candidateLayout,
      columns, rows, firstRow = 0) {
    if (!candidateLayout) return false;
    if (candidateLayout.col < 0 ||
        candidateLayout.row < firstRow ||
        candidateLayout.span_w < 0.5 ||
        candidateLayout.span_h < 0.5 ||
        candidateLayout.col + candidateLayout.span_w > columns ||
        candidateLayout.row + candidateLayout.span_h > rows) {
      return false;
    }
    const active = activeIndices instanceof Set
      ? activeIndices : new Set(activeIndices || []);
    for (const otherIndex of active) {
      if (otherIndex === index) continue;
      const otherLayout = layouts?.[otherIndex];
      if (otherLayout && rectsOverlap(candidateLayout, otherLayout)) {
        return false;
      }
    }
    return true;
  }

  function canPlaceTileLayout(tab, index, candidateLayout) {
    const tiles = getTilesData(tab);
    if (!Array.isArray(tiles)) return false;
    const type = tab === currentTileTab && index === currentTileIndex
      ? document.getElementById(tab + '_tile_type')?.value ?? tiles[index]?.type : tiles[index]?.type;
    if (Number(type) !== 0 && index >= 0 && !supportedTileLayout(type, candidateLayout)) return false;
    const layouts = tiles.map((tile, tileIndex) =>
      getTileElementLayout(tab, tileIndex) ||
      getTileLayoutFromData(tab, tileIndex));
    const active = new Set();
    tiles.forEach((tile, tileIndex) => {
      if (tile && Number(tile.type || 0) !== 0) active.add(tileIndex);
    });
    return canPlaceGridLayout(
      layouts, active, index, candidateLayout,
      GRID_COLS, GRID_ROWS, firstAllowedGridRow(tab));
  }

  function canPlaceHiddenSettingsLayout(tab, candidateLayout) {
    if (tileDataLoadedTabs.has(tab)) {
      return canPlaceTileLayout(tab, -1, candidateLayout);
    }
    const layouts = [];
    const active = new Set();
    document.querySelectorAll('#tab-tiles-' + tab + ' .tile').forEach(tile => {
      const index = Number(tile.dataset.index);
      if (!Number.isInteger(index) || Number(tile.dataset.type || 0) === 0) return;
      const layout = getTileElementLayout(tab, index);
      if (!layout) return;
      layouts[index] = layout;
      active.add(index);
    });
    return canPlaceGridLayout(
      layouts, active, -1, candidateLayout,
      GRID_COLS, GRID_ROWS, firstAllowedGridRow(tab));
  }

  function manhattanDistance(colA, rowA, colB, rowB) {
    return Math.abs(colA - colB) + Math.abs(rowA - rowB);
  }

  function buildGridPlacementCandidates(
      columns, rows, firstRow,
      spanW, spanH, preferredCol, preferredRow, step = 1) {
    const candidates = [];
    for (let row = firstRow; row < rows; row += step) {
      for (let col = 0; col < columns; col += step) {
        if ((col + spanW) > columns || (row + spanH) > rows) continue;
        let distance = (row * columns) + col;
        if (preferredCol >= 0 && preferredRow >= 0) {
          distance = manhattanDistance(col, row, preferredCol, preferredRow);
        }
        candidates.push({ col, row, distance });
      }
    }
    candidates.sort((a, b) => {
      if (a.distance !== b.distance) return a.distance - b.distance;
      if (a.row !== b.row) return a.row - b.row;
      return a.col - b.col;
    });
    return candidates;
  }

  function buildPlacementCandidates(
      tab, spanW, spanH, preferredCol, preferredRow) {
    return buildGridPlacementCandidates(
      GRID_COLS, GRID_ROWS, firstAllowedGridRow(tab),
      spanW, spanH, preferredCol, preferredRow);
  }

  function simulateGridReorderLayouts(
      baseLayouts, activeIndices, fromIdx,
      targetCol, targetRow, columns, rows, firstRow = 0, tileTypes = []) {
    const active = activeIndices instanceof Set
      ? new Set(activeIndices) : new Set(activeIndices || []);
    if (!active.has(fromIdx)) return null;
    const movingBase = baseLayouts?.[fromIdx]
      ? cloneLayout(baseLayouts[fromIdx]) : null;
    if (!movingBase ||
        targetRow < firstRow ||
        targetCol < 0 ||
        targetCol + movingBase.span_w > columns ||
        targetRow + movingBase.span_h > rows) {
      return null;
    }

    const workingLayouts = (baseLayouts || [])
      .map(layout => cloneLayout(layout));
    const targetLayout = {
      col: targetCol,
      row: targetRow,
      span_w: movingBase.span_w,
      span_h: movingBase.span_h
    };
    const displacedIndices = [];
    active.forEach(index => {
      if (index === fromIdx) return;
      const layout = baseLayouts[index];
      if (layout && rectsOverlap(targetLayout, layout)) {
        displacedIndices.push(index);
      }
    });
    const fractional = [targetLayout, ...baseLayouts.filter((_, i) => active.has(i))]
      .some(layout => layout && [layout.col, layout.row, layout.span_w, layout.span_h].some(v => !Number.isInteger(v)));
    displacedIndices.sort((a, b) => {
      const layoutA = baseLayouts[a];
      const layoutB = baseLayouts[b];
      if (layoutA.row !== layoutB.row) return layoutA.row - layoutB.row;
      if (layoutA.col !== layoutB.col) return layoutA.col - layoutB.col;
      return a - b;
    });

    workingLayouts[fromIdx] = targetLayout;
    const floating = new Set(displacedIndices);
    for (let order = 0; order < displacedIndices.length; ++order) {
      const displacedIndex = displacedIndices[order];
      const layout = baseLayouts[displacedIndex];
      if (!layout) return null;
      floating.delete(displacedIndex);
      const preferredCol = order === 0 ? movingBase.col : layout.col;
      const preferredRow = order === 0 ? movingBase.row : layout.row;
      const candidates = buildGridPlacementCandidates(
        columns, rows, firstRow,
        layout.span_w, layout.span_h,
        preferredCol, preferredRow,
        fractional && ![7, 8].includes(Number(tileTypes[displacedIndex])) ? 0.5 : 1);
      let placed = false;
      for (const candidate of candidates) {
        const nextLayout = {
          col: candidate.col,
          row: candidate.row,
          span_w: layout.span_w,
          span_h: layout.span_h
        };
        let blocked = false;
        for (const otherIndex of active) {
          if (otherIndex === displacedIndex ||
              floating.has(otherIndex)) {
            continue;
          }
          const otherLayout = workingLayouts[otherIndex];
          if (otherLayout &&
              rectsOverlap(nextLayout, otherLayout)) {
            blocked = true;
            break;
          }
        }
        if (blocked) continue;
        workingLayouts[displacedIndex] = nextLayout;
        placed = true;
        break;
      }
      if (!placed) return null;
    }
    return {
      targetCol,
      targetRow,
      layouts: workingLayouts
    };
  }

  function simulateSmartReorderLayouts(tab, fromIdx, targetCol, targetRow) {
    const tiles = getTilesData(tab);
    if (!Array.isArray(tiles) || fromIdx < 0 || fromIdx >= tiles.length) return null;

    const baseLayouts = (dragSource && dragSource.tab === tab && Array.isArray(dragSource.baseLayouts))
      ? dragSource.baseLayouts
      : captureLayoutSnapshot(tab);

    const active = new Set();
    tiles.forEach((tile, index) => {
      if (tile && Number(tile.type || 0) !== 0) active.add(index);
    });
    return simulateGridReorderLayouts(
      baseLayouts, active, fromIdx,
      targetCol, targetRow,
      GRID_COLS, GRID_ROWS, firstAllowedGridRow(tab), tiles.map(tile => tile?.type));
  }


  function clearDragPlaceholder() {
    if (dragPlaceholder && dragPlaceholder.parentNode) {
      dragPlaceholder.parentNode.removeChild(dragPlaceholder);
    }
    if (dragPlaceholder) {
      dragPlaceholder.classList.remove('show', 'invalid');
    }
    dragPlaceholder = null;
  }

  function ensureDragPlaceholder(tab) {
    const grid = getTileGrid(tab);
    if (!grid) return null;
    if (!dragPlaceholder) {
      dragPlaceholder = document.createElement('div');
      dragPlaceholder.className = 'tile-drop-placeholder';
    }
    if (dragPlaceholder.parentNode !== grid) grid.appendChild(dragPlaceholder);
    return dragPlaceholder;
  }

  function clearResizePlaceholder() {
    if (resizePlaceholder && resizePlaceholder.parentNode) {
      resizePlaceholder.parentNode.removeChild(resizePlaceholder);
    }
    if (resizePlaceholder) {
      resizePlaceholder.classList.remove('show', 'invalid');
    }
    resizePlaceholder = null;
  }

  function ensureResizePlaceholder(tab) {
    const grid = getTileGrid(tab);
    if (!grid) return null;
    if (!resizePlaceholder) {
      resizePlaceholder = document.createElement('div');
      resizePlaceholder.className = 'tile-resize-placeholder';
    }
    if (resizePlaceholder.parentNode !== grid) grid.appendChild(resizePlaceholder);
    return resizePlaceholder;
  }

  function renderResizePlaceholderPreview(
      tab, placeholder, layout) {
    const source = resizeState
      ? document.getElementById(resizeState.tileId)
      : null;
    if (!source) {
      placeholder.replaceChildren();
      return;
    }
    const preview = source.cloneNode(true);
    preview.removeAttribute('id');
    preview.removeAttribute('onclick');
    preview.removeAttribute('ondblclick');
    preview.removeAttribute('draggable');
    delete preview.dataset.selected;
    preview.classList.remove(
      'active',
      'resizing',
      'resize-invalid',
      'climate-content-editing',
      'climate-mini-selection-active',
      'climate-parent-hover');
    preview.classList.add('tile-resize-preview-card');
    preview.style.removeProperty('grid-column');
    preview.style.removeProperty('grid-row');
    preview.querySelectorAll(
      '.tile-resize-handle, .climate-mini-editor-shell')
      .forEach(element => element.remove());
    preview.querySelectorAll('[id]')
      .forEach(element => element.removeAttribute('id'));

    if (resizeState?.climateState &&
        typeof climateOuterResizePreviewHtml === 'function') {
      const slots = preview.querySelector(
        ':scope > .climate-slots');
      const html = climateOuterResizePreviewHtml(
        tab,
        resizeState.climateState,
        layout.span_w,
        layout.span_h);
      if (slots && html) slots.outerHTML = html;
    }
    const data = getTilesData(tab)?.[resizeState?.index];
    applyCompactSensorPreview(preview, data?.type, layout, data?.sensor_display_mode);
    placeholder.replaceChildren(preview);
  }

  function updateResizePlaceholder(tab, layout, valid) {
    const placeholder = ensureResizePlaceholder(tab);
    if (!placeholder || !layout) return;
    placeholder.classList.add('show');
    placeholder.classList.toggle('invalid', !valid);
    setTileGridPosition(placeholder, layout.col, layout.row, layout.span_w, layout.span_h);
    const previewKey = [
      layout.col,
      layout.row,
      layout.span_w,
      layout.span_h,
      valid ? 1 : 0
    ].join(':');
    if (placeholder.dataset.previewKey !== previewKey) {
      placeholder.dataset.previewKey = previewKey;
      renderResizePlaceholderPreview(
        tab, placeholder, layout);
    }
  }

  function buildResizeCandidate(layout, direction, clientX, clientY, tab) {
    if (!layout) return null;

    let spanW = layout.span_w;
    let spanH = layout.span_h;
    const tiles = getTilesData(tab);
    const tile = Array.isArray(tiles) && currentTileIndex >= 0
      ? tiles[currentTileIndex] : null;
    const typeValue = document.getElementById(tab + '_tile_type')?.value ?? tile?.type ?? 0;
    const isMedia = Number(typeValue) === MEDIA_TILE_TYPE;
    const minW = isMedia ? Math.min(MEDIA_TILE_MIN_SPAN, GRID_COLS) : 1;
    // Every type resizes in half steps except Settings/Back, which stay whole.
    const fixedGrid = [7, 8].includes(Number(typeValue));
    const unit = fixedGrid ? 1 : 0.5;
    const snap = fixedGrid ? clampInt : clampHalf;
    const rawCell = getRawGridCellFromPointer(tab, clientX, clientY, unit);
    if (!rawCell) return null;
    const minH = isMedia ? Math.min(MEDIA_TILE_MIN_SPAN, GRID_ROWS) : (supportsHalfSize(typeValue) ? 0.5 : 1);
    const maxW = isMedia
      ? Math.min(MEDIA_TILE_MAX_SPAN, GRID_COLS - layout.col)
      : GRID_COLS - layout.col;
    const maxH = isMedia
      ? Math.min(MEDIA_TILE_MAX_SPAN, GRID_ROWS - layout.row)
      : GRID_ROWS - layout.row;
    if (String(direction || '').includes('s')) {
      spanH = snap(rawCell.row - layout.row + unit, minH, maxH, layout.span_h);
    }
    if (String(direction || '').includes('e')) {
      spanW = snap(rawCell.col - layout.col + unit, minW, maxW, layout.span_w);
    }

    return {
      col: layout.col,
      row: layout.row,
      span_w: spanW,
      span_h: spanH
    };
  }

  function stopTileResize(commit = true) {
    if (!resizeState) return;
    const state = resizeState;
    resizeState = null;

    window.removeEventListener('pointermove', handleTileResizeMove);
    window.removeEventListener('pointerup', handleTileResizeEnd);
    window.removeEventListener('pointercancel', handleTileResizeCancel);
    document.body.classList.remove('tile-resize-active');

    const tile = document.getElementById(state.tileId);
    if (tile) {
      tile.classList.remove('resizing', 'resize-invalid');
      tile.draggable = true;
    }
    clearResizePlaceholder();

    const finalLayout = commit ? (state.lastValidLayout || state.originalLayout) : state.originalLayout;
    if (state.tab === currentTileTab && state.index === currentTileIndex && finalLayout) {
      if (!commit && state.climateState &&
          typeof restoreClimateOuterResizeState === 'function') {
        restoreClimateOuterResizeState(
          state.tab, state.climateState);
      }
      applyLayoutInputsFromLayout(state.tab, finalLayout, false);
      if (commit && state.climateState &&
          typeof previewClimateOuterResize === 'function') {
        // The new parent and mini geometry is applied on release, within the
        // same JavaScript step. That leaves no frame in which the old mini grid
        // is squeezed into or stretched over the new parent size.
        previewClimateOuterResize(
          state.tab, state.climateState);
      }
      updateLayoutFromInputs(state.tab);
      updateTilePreview(state.tab);
      if (commit && !layoutsEqual(finalLayout, state.originalLayout)) {
        updateDraft(state.tab);
        scheduleAutoSave(state.tab);
      }
    }
    flushDeferredSensorRefresh();
  }

  function handleTileResizeMove(e) {
    if (!resizeState) return;
    e.preventDefault();

    const candidate = buildResizeCandidate(
      resizeState.originalLayout,
      resizeState.direction,
      e.clientX,
      e.clientY,
      resizeState.tab
    );
    if (!candidate) return;

    const valid = canPlaceTileLayout(resizeState.tab, resizeState.index, candidate);
    const tile = document.getElementById(resizeState.tileId);
    if (tile) tile.classList.toggle('resize-invalid', !valid);
    updateResizePlaceholder(resizeState.tab, candidate, valid);
    if (!valid) return;

    // While dragging, the real tile stays unchanged and only the dashed resize
    // placeholder shows the target. That keeps mini tiles from being stretched
    // or squeezed between pointer frames, in every direction and size.
    resizeState.lastValidLayout = cloneLayout(candidate);
  }

  function handleTileResizeEnd() {
    stopTileResize(true);
  }

  function handleTileResizeCancel() {
    stopTileResize(false);
  }

  function beginTileResize(tab, tile, direction, e) {
    if (!tile || dragSource || resizeState) return;
    const tileIndex = parseInt(tile.dataset.index, 10);
    if (isNaN(tileIndex)) return;
    if (currentTileTab !== tab || currentTileIndex !== tileIndex) return;

    const layout = getTileElementLayout(tab, tileIndex) || getTileLayoutFromData(tab, tileIndex);
    if (!layout) return;

    e.preventDefault();
    e.stopPropagation();
    clearDragPlaceholder();

    resizeState = {
      tab,
      index: tileIndex,
      tileId: tile.id,
      direction,
      originalLayout: cloneLayout(layout),
      lastValidLayout: cloneLayout(layout),
      climateState:
        String(tile.dataset.type || '') === '17' &&
        typeof captureClimateOuterResizeState === 'function'
          ? captureClimateOuterResizeState(tab)
          : null
    };

    tile.classList.add('resizing');
    tile.draggable = false;
    document.body.classList.add('tile-resize-active');
    window.addEventListener('pointermove', handleTileResizeMove);
    window.addEventListener('pointerup', handleTileResizeEnd);
    window.addEventListener('pointercancel', handleTileResizeCancel);
  }

  function updateDragPlaceholder(tab, col, row) {
    if (!dragSource || dragSource.tab !== tab) return;
    const sourceLayout = getDragSourceLayout();
    const placeholder = ensureDragPlaceholder(tab);
    if (!sourceLayout || !placeholder) return;

    const targetCol = clampHalf(col, 0, GRID_COLS - 0.5, sourceLayout.col);
    const targetRow = clampHalf(row, firstAllowedGridRow(tab), GRID_ROWS - 0.5, sourceLayout.row);
    const fits = (targetCol + sourceLayout.span_w <= GRID_COLS) &&
                 (targetRow + sourceLayout.span_h <= GRID_ROWS);
    const spanW = Math.max(1, Math.min(sourceLayout.span_w, GRID_COLS - targetCol));
    const spanH = Math.max(0.5, Math.min(sourceLayout.span_h, GRID_ROWS - targetRow));

    placeholder.classList.toggle('invalid', !fits);
    placeholder.classList.add('show');
    setTileGridPosition(placeholder, targetCol, targetRow, spanW, spanH);
  }

  function handleGridDragMove(tab, e) {
    if (!dragSource || dragSource.tab !== tab) return;
    const cell = getGridCellFromPointer(tab, e.clientX, e.clientY);
    if (!cell) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    const sourceLayout = getDragSourceLayout();
    if (!sourceLayout) return;
    const targetCol = clampHalf(cell.col, 0, GRID_COLS - 0.5, sourceLayout.col);
    const targetRow = clampHalf(cell.row, firstAllowedGridRow(tab), GRID_ROWS - 0.5, sourceLayout.row);
    updateDragPlaceholder(tab, targetCol, targetRow);

    if (dragSource.kind === 'hidden-settings') {
      const candidate = {
        col: targetCol,
        row: targetRow,
        span_w: sourceLayout.span_w,
        span_h: sourceLayout.span_h
      };
      const valid = canPlaceHiddenSettingsLayout(tab, candidate);
      ensureDragPlaceholder(tab)?.classList.toggle('invalid', !valid);
      dragSource.hiddenTarget = valid ? candidate : null;
      return;
    }

    if (targetCol === sourceLayout.col && targetRow === sourceLayout.row) {
      dragSource.previewKey = targetCol + ':' + targetRow;
      dragSource.previewResult = null;
      restoreDragPreview(tab);
      return;
    }

    const previewKey = targetCol + ':' + targetRow;
    if (dragSource.previewKey === previewKey && dragSource.previewResult) return;

    const previewResult = simulateSmartReorderLayouts(tab, dragSource.index, targetCol, targetRow);
    dragSource.previewKey = previewKey;
    if (!previewResult) {
      dragSource.previewResult = null;
      const placeholder = ensureDragPlaceholder(tab);
      if (placeholder) placeholder.classList.add('invalid');
      if (!dragSource.appliedPreviewResult) restoreDragPreview(tab);
      return;
    }
    dragSource.previewResult = previewResult;
    applyDragPreviewLayouts(tab, previewResult);
  }

  function handleGridDrop(tab, e) {
    if (!dragSource || dragSource.tab !== tab) return;
    const sourceLayout = getDragSourceLayout();
    const cell = getGridCellFromPointer(tab, e.clientX, e.clientY);
    clearDragPlaceholder();
    if (!sourceLayout || !cell) return;

    e.preventDefault();
    e.stopPropagation();
    const targetCol = clampHalf(cell.col, 0, GRID_COLS - 0.5, sourceLayout.col);
    const targetRow = clampHalf(cell.row, firstAllowedGridRow(tab), GRID_ROWS - 0.5, sourceLayout.row);
    const fits = (targetCol + sourceLayout.span_w <= GRID_COLS) &&
                 (targetRow + sourceLayout.span_h <= GRID_ROWS);

    if (dragSource.kind === 'hidden-settings') {
      const candidate = {
        col: targetCol,
        row: targetRow,
        span_w: sourceLayout.span_w,
        span_h: sourceLayout.span_h
      };
      if (!fits || !canPlaceHiddenSettingsLayout(tab, candidate)) {
        showNotification(t('tileDoesNotFit'), false);
        return;
      }
      dragSource.dropCommitted = true;
      restoreHiddenSettingsTile(candidate.col, candidate.row);
      return;
    }

    if (!fits) {
      showNotification(t('tileDoesNotFit'), false);
      return;
    }
    if (targetCol === sourceLayout.col && targetRow === sourceLayout.row) return;

    let previewResult = dragSource.previewResult;
    if (!previewResult || previewResult.targetCol !== targetCol || previewResult.targetRow !== targetRow) {
      previewResult = simulateSmartReorderLayouts(tab, dragSource.index, targetCol, targetRow);
    }
    if (!previewResult) {
      showNotification(t('noLayoutFound'), false);
      return;
    }

    dragSource.previewResult = previewResult;
    dragSource.dropCommitted = true;
    reorderTiles(dragSource.tab, dragSource.index, dragSource.index, targetCol, targetRow);
  }

  function syncSelectedLayoutInputs(tab, layout) {
    if (!layout) return;
    if (currentTileTab !== tab || currentTileIndex === -1) return;
    applyLayoutInputsFromLayout(tab, layout);
  }

  function captureTilePositionSnapshot(tab) {
    const tiles = getTilesData(tab);
    if (!Array.isArray(tiles)) return [];
    return tiles.map(tile => {
      if (!tile) return null;
      return { col: tile.col, row: tile.row };
    });
  }

  function applyLocalTileReorder(tab, previewResult) {
    const tiles = getTilesData(tab);
    if (!Array.isArray(tiles) || !previewResult || !Array.isArray(previewResult.layouts)) return;

    for (let i = 0; i < tiles.length; i++) {
      const tile = tiles[i];
      const layout = previewResult.layouts[i];
      if (!tile || Number(tile.type || 0) === 0 || !layout) continue;
      const changed = tile.col !== layout.col || tile.row !== layout.row;
      tile.col = layout.col;
      tile.row = layout.row;
      const draft = drafts[tab]?.[i];
      if (changed && draft?._dirty) {
        draft.col = String(layout.col + 1);
        draft.row = String(layout.row + 1);
        draft._rev = Number(draft._rev || 0) + 1;
      }
    }
    persistDrafts();

    tilesData[tab] = tiles;
    layoutTiles(tab, tiles);
    clearReflowPreviewClasses(tab);
    if (previewResult.layouts[currentTileIndex]) {
      syncSelectedLayoutInputs(tab, previewResult.layouts[currentTileIndex]);
    }
  }

  function restoreLocalTileReorder(tab, snapshot) {
    const tiles = getTilesData(tab);
    if (!Array.isArray(tiles) || !Array.isArray(snapshot)) return;
    for (let i = 0; i < tiles.length; i++) {
      const tile = tiles[i];
      const saved = snapshot[i];
      if (!tile || !saved) continue;
      const changed = tile.col !== saved.col || tile.row !== saved.row;
      tile.col = saved.col;
      tile.row = saved.row;
      const draft = drafts[tab]?.[i];
      if (changed && draft?._dirty) {
        draft.col = String(saved.col + 1);
        draft.row = String(saved.row + 1);
        draft._rev = Number(draft._rev || 0) + 1;
      }
    }
    persistDrafts();
    tilesData[tab] = tiles;
    layoutTiles(tab, tiles);
    clearReflowPreviewClasses(tab);
    if (currentTileIndex >= 0 && snapshot[currentTileIndex]) {
      syncSelectedLayoutInputs(tab, {
        col: snapshot[currentTileIndex].col,
        row: snapshot[currentTileIndex].row
      });
    }
  }

  function restoreDragPreviewFromSnapshot(tab, snapshot) {
    restoreLocalTileReorder(tab, snapshot);
    restoreDragPreview(tab);
  }

  // The grid tiles are role="button" with tabindex, so they also have to answer
  // Enter and Space. One delegated listener per grid element survives every tile
  // re-render, and the flag keeps a rebound folder from stacking duplicates.
  function enableTileKeys(tab) {
    const grid = getTileGrid(tab);
    if (!grid || grid.dataset.keysBound === '1') return;
    grid.dataset.keysBound = '1';
    grid.addEventListener('keydown', event => {
      if (event.key !== 'Enter' && event.key !== ' ' &&
          event.key !== 'Spacebar') return;
      const tile = event.target.closest('.tile[data-index]');
      if (!tile || tile.parentElement !== grid) return;
      event.preventDefault();
      const index = parseInt(tile.dataset.index, 10);
      if (Number.isNaN(index)) return;
      selectTile(index, tab);
    });
  }

  function enableTileDrag(tab) {
    const grid = getTileGrid(tab);
    const tiles = grid ? grid.querySelectorAll(':scope > .tile') : [];
    tiles.forEach(tile => {
      tile.addEventListener('dragstart', (e) => {
        if (resizeState) {
          e.preventDefault();
          return;
        }
        const tileIndex = parseInt(tile.dataset.index, 10);
        if (currentTileIndex !== tileIndex || currentTileTab !== tab) {
          selectTile(tileIndex, tab);
        }
        const layout = getTileElementLayout(tab, tileIndex) ||
                       getTileLayoutFromData(tab, tileIndex);
        const anchorCell = getDragAnchorCell(tab, layout, e.clientX, e.clientY);
        const grabOffset = getDragAnchorOffset(tab, layout, anchorCell.col, anchorCell.row, tile.getBoundingClientRect());
        dragSource = {
          kind: 'grid-tile',
          tab,
          index: tileIndex,
          type: Number(tile.dataset.type || 0),
          layout,
          baseLayouts: captureLayoutSnapshot(tab),
          grabCellCol: anchorCell.col,
          grabCellRow: anchorCell.row,
          previewResult: null,
          appliedPreviewResult: null,
          previewKey: '',
          dropCommitted: false
        };
        e.dataTransfer.effectAllowed = 'move';
        tile.classList.add('dragging');
        if (e.dataTransfer.setDragImage) {
          dragPreview = createDragPreview(tile);
          e.dataTransfer.setDragImage(dragPreview, grabOffset.x, grabOffset.y);
        }
      });
      tile.addEventListener('dragend', () => {
        const committedDrop = !!(dragSource && dragSource.tab === tab && dragSource.dropCommitted);
        tile.classList.remove('dragging');
        tiles.forEach(t => t.classList.remove('drop-target'));
        if (dragSource && dragSource.tab === tab && !dragSource.dropCommitted) {
          restoreDragPreview(tab);
        }
        clearReflowPreviewClasses(tab);
        clearDragPlaceholder();
        if (dragPreview && dragPreview.parentNode) dragPreview.parentNode.removeChild(dragPreview);
        dragPreview = null;
        dragSource = null;
        if (committedDrop) clearDeferredSensorRefresh();
        else flushDeferredSensorRefresh();
      });
      tile.addEventListener('dragenter', (e) => {
        handleGridDragMove(tab, e);
      });
      tile.addEventListener('dragover', (e) => {
        handleGridDragMove(tab, e);
      });
      tile.addEventListener('dragleave', () => {});
      tile.addEventListener('drop', (e) => {
        handleGridDrop(tab, e);
      });
    });
    if (!grid) return;
    grid.addEventListener('dragenter', (e) => handleGridDragMove(tab, e));
    grid.addEventListener('dragover', (e) => handleGridDragMove(tab, e));
    grid.addEventListener('dragleave', event => {
      if (!dragSource || dragSource.kind !== 'hidden-settings' ||
          dragSource.tab !== tab) return;
      if (event.relatedTarget instanceof Node &&
          grid.contains(event.relatedTarget)) return;
      dragSource.hiddenTarget = null;
    });
    grid.addEventListener('drop', (e) => handleGridDrop(tab, e));
  }

  async function flushSettingsTileSaveBeforeHide(tab, index) {
    if (index < 0) return true;
    const timerKey = tab + ':' + index;
    if (autoSaveTimers[timerKey]) {
      clearTimeout(autoSaveTimers[timerKey]);
      delete autoSaveTimers[timerKey];
    }
    saveTile(tab, true, index);
    const saveKey = getTileSaveKey(tab, index);
    const deadline = Date.now() + 8000;
    while (Date.now() < deadline) {
      const draft = drafts?.[tab]?.[index];
      if (!saveInFlightByTile[saveKey] && !queuedSaveByTile[saveKey] &&
          !(draft && draft._dirty)) {
        return true;
      }
      await new Promise(resolve => setTimeout(resolve, 40));
    }
    showNotification(t('networkErrorSave'), false);
    return false;
  }

  async function hideSettingsTileFromGrid() {
    const hidden = settingsAccessElement('settings_tile_hidden');
    const swipe = settingsAccessElement('settings_swipe_enabled');
    if (!hidden || settingsTileTransferInFlight) return false;
    settingsTileTransferInFlight = true;
    try {
      const settingsTile = (getTilesData('folder0') || []).findIndex(
        tile => Number(tile?.type || 0) === 7);
      if (settingsTile < 0) {
        return false;
      }
      const snapshot = normalizeHiddenSettingsSnapshot(
        getTileSnapshotForSave('folder0', settingsTile) ||
        currentGridSettingsSnapshot());
      if (!(await flushSettingsTileSaveBeforeHide('folder0', settingsTile))) {
        return false;
      }
      hidden.checked = true;
      if (swipe) swipe.checked = true;
      toggleSettingsAccessFields();
      const saved = await queueSettingsAccessSave(
        null, null, snapshot, false);
      if (!saved) return false;
      return await reconcileSettingsTileUi(true, snapshot);
    } finally {
      settingsTileTransferInFlight = false;
    }
  }

  async function restoreHiddenSettingsTile(col, row) {
    const hidden = settingsAccessElement('settings_tile_hidden');
    if (!hidden || settingsTileTransferInFlight) return false;
    const snapshot = normalizeHiddenSettingsSnapshot();
    settingsTileTransferInFlight = true;
    try {
      hidden.checked = false;
      toggleSettingsAccessFields();
      const saved = await queueSettingsAccessSave(
        null, {col, row}, null, false);
      if (!saved) return false;
      return await reconcileSettingsTileUi(false, snapshot, true);
    } finally {
      settingsTileTransferInFlight = false;
    }
  }

  function enableSettingsHiddenSlot() {
    const slot = document.getElementById('settingsHiddenSlot');
    const hiddenTile = document.getElementById('settingsHiddenTile');
    if (!slot || !hiddenTile || slot.dataset.bound === '1') return;
    slot.dataset.bound = '1';
    hiddenTile.addEventListener('click', () => selectHiddenSettingsTile());

    const acceptsGridSettings = () => {
      if (!dragSource || dragSource.kind !== 'grid-tile' ||
          dragSource.tab !== 'folder0') return false;
      const tile = getTilesData('folder0')?.[dragSource.index];
      return Number(dragSource.type || tile?.type || 0) === 7;
    };
    slot.addEventListener('dragover', event => {
      if (!acceptsGridSettings()) return;
      event.preventDefault();
      event.dataTransfer.dropEffect = 'move';
      slot.classList.add('drop-target');
    });
    slot.addEventListener('dragleave', event => {
      if (event.relatedTarget instanceof Node &&
          slot.contains(event.relatedTarget)) return;
      slot.classList.remove('drop-target');
    });
    slot.addEventListener('drop', event => {
      if (!acceptsGridSettings()) return;
      event.preventDefault();
      event.stopPropagation();
      slot.classList.remove('drop-target');
      restoreDragPreview('folder0');
      clearDragPlaceholder();
      if (dragSource) dragSource.dropCommitted = true;
      hideSettingsTileFromGrid();
    });

    hiddenTile.addEventListener('dragstart', event => {
      if (hiddenTile.dataset.hidden !== '1') {
        event.preventDefault();
        return;
      }
      const spanW = clampInt(hiddenTile.dataset.spanW, 1, GRID_COLS, 1);
      const spanH = clampInt(hiddenTile.dataset.spanH, 1, GRID_ROWS, 1);
      dragSource = {
        kind: 'hidden-settings',
        tab: 'folder0',
        index: -1,
        layout: {col: 0, row: 0, span_w: spanW, span_h: spanH},
        grabCellCol: 0,
        grabCellRow: 0,
        baseLayouts: null,
        dropCommitted: false,
        hiddenTarget: null
      };
      event.dataTransfer.effectAllowed = 'move';
      hiddenTile.classList.add('dragging');
      if (event.dataTransfer.setDragImage) {
        dragPreview = createDragPreview(hiddenTile);
        event.dataTransfer.setDragImage(
          dragPreview, hiddenTile.offsetWidth / 2, hiddenTile.offsetHeight / 2);
      }
    });
    hiddenTile.addEventListener('dragend', () => {
      hiddenTile.classList.remove('dragging');
      slot.classList.remove('drop-target', 'invalid');
      clearDragPlaceholder();
      if (dragPreview && dragPreview.parentNode) dragPreview.parentNode.removeChild(dragPreview);
      dragPreview = null;
      dragSource = null;
      flushDeferredSensorRefresh();
    });
  }

  function enableTileResize(tab) {
    const grid = getTileGrid(tab);
    if (!grid || grid.dataset.resizeBound === '1') return;
    grid.dataset.resizeBound = '1';
    grid.addEventListener('pointerdown', (e) => {
      const handle = e.target.closest('.tile-resize-handle');
      if (!handle) return;
      const tile = handle.closest('.tile');
      if (!tile || tile.classList.contains('empty')) return;
      beginTileResize(tab, tile, handle.dataset.resizeDir || 'se', e);
    });
  }

  function reorderTiles(tab, fromIdx, toIdx, targetCol, targetRow) {
    let col = Number(targetCol);
    let row = Number(targetRow);
    if (isNaN(col)) col = -1;
    if (isNaN(row)) row = -1;
    const folderId = getFolderIdForTab(tab);
    if (folderId === undefined) {
      if (dragSource && dragSource.tab === tab) dragSource.dropCommitted = false;
      restoreDragPreview(tab);
      showNotification(t('folderNotFound'), false);
      return;
    }
    let previewResult = dragSource && dragSource.tab === tab ? dragSource.previewResult : null;
    if (!previewResult || previewResult.targetCol !== col || previewResult.targetRow !== row) {
      previewResult = simulateSmartReorderLayouts(tab, fromIdx, col, row);
    }
    if (!previewResult) {
      if (dragSource && dragSource.tab === tab) dragSource.dropCommitted = false;
      restoreDragPreview(tab);
      showNotification(t('noLayoutFound'), false);
      return;
    }
    const localSnapshot = captureTilePositionSnapshot(tab);
    applyLocalTileReorder(tab, previewResult);
    clearDragPlaceholder();
    fetch('/api/tiles/reorder', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'folder=' + encodeURIComponent(folderId) +
            '&from=' + encodeURIComponent(fromIdx) +
            '&to=' + encodeURIComponent(toIdx) +
            '&target_col=' + encodeURIComponent(col) +
            '&target_row=' + encodeURIComponent(row)
    })
    .then(res => res.json())
    .then(data => {
      if (data.success) {
        showNotification(t('tilesMovedSaved'));
        clearDeferredSensorRefresh();
        // applyLocalTileReorder already stored the confirmed state. No full grid
        // reload: that could visibly jump back to the old state and then forward
        // to the new position again.
      } else {
        if (dragSource && dragSource.tab === tab) dragSource.dropCommitted = false;
        clearDeferredSensorRefresh();
        restoreDragPreviewFromSnapshot(tab, localSnapshot);
        showNotification(t('moveFailed'), false);
      }
    })
    .catch(() => {
      if (dragSource && dragSource.tab === tab) dragSource.dropCommitted = false;
      clearDeferredSensorRefresh();
      restoreDragPreviewFromSnapshot(tab, localSnapshot);
      showNotification(t('networkErrorMove'), false);
    });
  }

  function getTopLeftConfiguredTileIndex(tab) {
    let selectedIndex = -1;
    let selectedRow = Number.MAX_SAFE_INTEGER;
    let selectedCol = Number.MAX_SAFE_INTEGER;
    document.querySelectorAll('#tab-tiles-' + tab + ' .tile').forEach(tile => {
      const index = parseInt(tile.dataset.index, 10);
      if (isNaN(index) || Number(tile.dataset.type || 0) === 0) return;
      const row = Number(tile.dataset.row);
      const col = Number(tile.dataset.col);
      const safeRow = isNaN(row) ? Number.MAX_SAFE_INTEGER : row;
      const safeCol = isNaN(col) ? Number.MAX_SAFE_INTEGER : col;
      if (safeRow < selectedRow || (safeRow === selectedRow && safeCol < selectedCol)) {
        selectedIndex = index;
        selectedRow = safeRow;
        selectedCol = safeCol;
      }
    });
    return selectedIndex >= 0 ? selectedIndex : 0;
  }

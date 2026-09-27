
  function bindClimateMiniGrid(tab) {
    const grid = document.getElementById(
      tab + '_climate_content_grid');
    if (!grid || grid.dataset.climateBound === '1') return;
    grid.dataset.climateBound = '1';
    const setOuterTileDragEnabled = enabled => {
      const outerTile = grid.closest('.tile.climate');
      if (!outerTile) return;
      outerTile.draggable =
        !!enabled &&
        !outerTile.classList.contains(
          'climate-mini-selection-active');
    };
    const releaseOuterTileDrag = () => {
      if (!climateGridDragState) {
        setOuterTileDragEnabled(true);
      }
    };

    let dropPlaceholder = null;
    const ensureDropPlaceholder = () => {
      if (!dropPlaceholder) {
        dropPlaceholder = document.createElement('div');
        dropPlaceholder.className = 'climate-drop-placeholder';
      }
      if (dropPlaceholder.parentElement !== grid) {
        grid.appendChild(dropPlaceholder);
      }
      return dropPlaceholder;
    };
    const clearDropPlaceholder = () => {
      if (!dropPlaceholder) return;
      dropPlaceholder.classList.remove('show', 'invalid');
      dropPlaceholder.remove();
    };
    const showDropPlaceholder = (state, col, row, valid) => {
      const placeholder = ensureDropPlaceholder();
      const source = document.getElementById(
        state.tab + '_climate_slot_row_' + state.index);
      const sourcePreview =
        source?.querySelector('.climate-mini-preview');
      if (sourcePreview) {
        const preview = sourcePreview.cloneNode(true);
        preview.querySelectorAll('[id]').forEach(element => {
          element.removeAttribute('id');
        });
        placeholder.replaceChildren(preview);
      } else {
        placeholder.replaceChildren();
      }
      setGridItemPosition(
        placeholder, col, row,
        state.origin.span_w, state.origin.span_h);
      placeholder.classList.add('show');
      placeholder.classList.toggle('invalid', !valid);
    };
    grid.addEventListener('pointerdown', event => {
      setOuterTileDragEnabled(false);
      event.stopPropagation();
    });
    grid.addEventListener('click', event => {
      event.stopPropagation();
      window.setTimeout(releaseOuterTileDrag, 0);
    });
    window.addEventListener('pointerup', releaseOuterTileDrag, true);
    window.addEventListener('pointercancel', releaseOuterTileDrag, true);

    const selectedContent = document.getElementById(
      tab + '_climate_selected_content');
    selectedContent?.addEventListener('change', () => {
      const index = Number(climateSelectedItemByTab[tab]);
      if (!Number.isFinite(index) || index < 0 || index >= 6) {
        return;
      }
      const source = document.getElementById(
        tab + '_climate_slot_' + index);
      if (!source) return;
      materializeClimateAutomaticItems(tab);
      const pending = climatePendingEmptyByTab[tab];
      if (pending && pending.index === index) {
        const stored = currentClimateGeometry(tab);
        stored[index] = pending.geometry;
        storeClimateGeometry(tab, stored);
        delete climatePendingEmptyByTab[tab];
      }
      source.value = selectedContent.value;
      climateSelectedCellByTab[tab] = -1;
      syncClimateSlotFields(tab);
      notifyClimateGridChanged(tab);
    });

    grid.querySelectorAll('.climate-mini-cell')
      .forEach(cell => {
        const cellIndex = Number(cell.dataset.climateCell);
        cell.addEventListener('click', event => {
        event.preventDefault();
        event.stopPropagation();
        const spanW = document.getElementById(
          tab + '_tile_span_w')?.value || 1;
        const spanH = document.getElementById(
          tab + '_tile_span_h')?.value || 1;
        const { columns, rows } =
          climateGridDimensions(spanW, spanH);
        const configured = currentClimateSlotConfig(tab);
        const resolvedKinds =
          climateResolvedEditorKinds(tab);
        const index = configured.findIndex(
          (value, candidate) =>
            Number(value) === CLIMATE_TILE_CONTENT.EMPTY ||
            (Number(value) === CLIMATE_TILE_CONTENT.AUTO &&
             resolvedKinds[candidate] === null));
        if (index < 0) return;
        const row = Math.floor(cellIndex / columns);
        const col = cellIndex % columns;
        if (row >= rows) return;
        climatePendingEmptyByTab[tab] = {
          index,
          geometry: { col, row, spanW: 1, spanH: 1 }
        };
        selectClimateEditorItem(
          tab, index, cellIndex,
          CLIMATE_TILE_CONTENT.EMPTY);
        document.getElementById(
          tab + '_climate_selected_fields')
          ?.classList.remove('hidden');
      });
      });

    const clearDragClasses = state => {
      if (!state) return;
      state.activeIndices.forEach(activeIndex => {
        document.getElementById(
          state.tab + '_climate_slot_row_' + activeIndex)
          ?.classList.remove(
            'dragging', 'drag-preview-positioned',
            'reflow-preview', 'invalid-drop');
      });
    };

    const restoreDragLayouts = state => {
      if (!state) return;
      applyClimateGridLayouts(
        state.tab, state.baseLayouts,
        state.activeIndices, state.baseLayouts);
      state.activeIndices.forEach(activeIndex => {
        document.getElementById(
          state.tab + '_climate_slot_row_' + activeIndex)
          ?.classList.remove(
            'drag-preview-positioned',
            'reflow-preview', 'invalid-drop');
      });
      document.getElementById(
        state.tab + '_climate_slot_row_' + state.index)
        ?.classList.add('dragging');
    };

    const updateMiniDragPreview = (
        state, clientX, clientY) => {
      if (!state || state.tab !== tab) return;
      const raw = getGridElementCellFromPointer(
        grid, state.columns, state.rows,
        clientX, clientY);
      if (!raw) return;
      const targetCol = Math.max(
        0, Math.min(
          state.columns - state.origin.span_w,
          raw.col - state.anchorCol));
      const targetRow = Math.max(
        0, Math.min(
          state.rows - state.origin.span_h,
          raw.row - state.anchorRow));
      const previewKey = targetCol + ':' + targetRow;
      if (state.previewKey === previewKey) return;
      state.previewKey = previewKey;
      const preview = simulateGridReorderLayouts(
        state.baseLayouts, state.activeIndices,
        state.index, targetCol, targetRow,
        state.columns, state.rows, 0);
      state.preview = preview;
      showDropPlaceholder(
        state, targetCol, targetRow, !!preview);
      if (!preview) {
        restoreDragLayouts(state);
        return;
      }
      applyClimateGridLayouts(
        tab, preview.layouts,
        state.activeIndices, state.baseLayouts);
    };

    grid.addEventListener('dragenter', event => {
      if (!climateGridDragState ||
          climateGridDragState.tab !== tab) return;
      event.preventDefault();
      event.stopPropagation();
    });

    grid.addEventListener('dragover', event => {
      const state = climateGridDragState;
      if (!state || state.tab !== tab) return;
      event.preventDefault();
      event.stopPropagation();
      if (event.dataTransfer) {
        event.dataTransfer.dropEffect = 'move';
      }
      updateMiniDragPreview(
        state, event.clientX, event.clientY);
    });

    grid.addEventListener('drop', event => {
      const state = climateGridDragState;
      if (!state || state.tab !== tab) return;
      event.preventDefault();
      event.stopPropagation();
      if (!state.preview) return;
      state.committed = true;
      clearDropPlaceholder();
      storeClimateGridLayouts(tab, state.preview.layouts);
      syncClimateSlotFields(tab);
      notifyClimateGridChanged(tab);
    });

    let pointerMiniDrag = null;
    let suppressMiniClickUntil = 0;

    const positionPointerDragGhost = (
        pending, clientX, clientY) => {
      if (!climateGridDragPreview || !pending) return;
      climateGridDragPreview.style.left =
        (clientX - pending.offsetX) + 'px';
      climateGridDragPreview.style.top =
        (clientY - pending.offsetY) + 'px';
    };

    const beginPointerMiniDrag = pending => {
      if (!pending) return false;
      const { item, index } = pending;
      selectClimateEditorItem(tab, index);
      delete climatePendingEmptyByTab[tab];
      materializeClimateAutomaticItems(tab);
      const spanW = document.getElementById(
        tab + '_tile_span_w')?.value || 1;
      const spanH = document.getElementById(
        tab + '_tile_span_h')?.value || 1;
      const { columns, rows } =
        climateGridDimensions(spanW, spanH);
      const activeIndices =
        climateActiveGridIndices(tab);
      const baseLayouts =
        climateGridLayouts(tab, columns, rows);
      const origin = cloneLayout(baseLayouts[index]);
      if (!origin || !activeIndices.has(index)) {
        return false;
      }
      const metrics = getGridElementMetrics(
        grid, columns, rows);
      const itemRect = item.getBoundingClientRect();
      const stepX = metrics
        ? metrics.cellW + metrics.gapX : itemRect.width;
      const stepY = metrics
        ? metrics.cellH + metrics.gapY : itemRect.height;
      const localX = Math.max(
        0, pending.startX - itemRect.left);
      const localY = Math.max(
        0, pending.startY - itemRect.top);
      const anchorCol = Math.max(
        0, Math.min(
          origin.span_w - 1,
          Math.floor(localX / Math.max(1, stepX))));
      const anchorRow = Math.max(
        0, Math.min(
          origin.span_h - 1,
          Math.floor(localY / Math.max(1, stepY))));
      climateGridDragState = {
        tab,
        index,
        columns,
        rows,
        activeIndices,
        baseLayouts,
        origin,
        anchorCol,
        anchorRow,
        preview: null,
        previewKey: '',
        committed: false
      };
      pending.started = true;
      pending.offsetX = Math.max(
        0, Math.min(
          itemRect.width,
          pending.startX - itemRect.left));
      pending.offsetY = Math.max(
        0, Math.min(
          itemRect.height,
          pending.startY - itemRect.top));
      item.classList.remove('climate-mini-hover');
      climateGridDragPreview =
        createClimateMiniDragGhost(item, itemRect);
      climateGridDragPreview.style.position = 'fixed';
      climateGridDragPreview.style.zIndex = '99999';
      climateGridDragPreview.style.opacity = '.92';
      item.classList.add('dragging');
      document.body.classList.add(
        'climate-mini-pointer-dragging');
      showDropPlaceholder(
        climateGridDragState,
        origin.col, origin.row, true);
      return true;
    };

    const finishPointerMiniDrag = (
        event, cancelled = false) => {
      const pending = pointerMiniDrag;
      if (!pending ||
          event.pointerId !== pending.pointerId) {
        return;
      }
      pointerMiniDrag = null;
      try {
        pending.item.releasePointerCapture?.(
          pending.pointerId);
      } catch (_) {}
      if (!pending.started) {
        setOuterTileDragEnabled(true);
        return;
      }
      event.preventDefault();
      event.stopPropagation();
      suppressMiniClickUntil = performance.now() + 350;
      const state = climateGridDragState;
      const committedLayouts =
        !cancelled && state?.preview?.layouts
          ? state.preview.layouts
          : null;
      clearDropPlaceholder();
      if (state && !committedLayouts) {
        restoreDragLayouts(state);
      }
      clearDragClasses(state);
      climateGridDragState = null;
      climateGridDragPreview?.remove();
      climateGridDragPreview = null;
      document.body.classList.remove(
        'climate-mini-pointer-dragging');
      setOuterTileDragEnabled(true);
      if (committedLayouts) {
        storeClimateGridLayouts(tab, committedLayouts);
        syncClimateSlotFields(tab);
        notifyClimateGridChanged(tab);
      }
    };

    window.addEventListener('pointermove', event => {
      const pending = pointerMiniDrag;
      if (!pending ||
          event.pointerId !== pending.pointerId) {
        return;
      }
      if (!pending.started) {
        const distance = Math.hypot(
          event.clientX - pending.startX,
          event.clientY - pending.startY);
        if (distance < 5) return;
        if (!beginPointerMiniDrag(pending)) {
          pointerMiniDrag = null;
          setOuterTileDragEnabled(true);
          return;
        }
      }
      event.preventDefault();
      event.stopPropagation();
      positionPointerDragGhost(
        pending, event.clientX, event.clientY);
      updateMiniDragPreview(
        climateGridDragState,
        event.clientX, event.clientY);
    }, true);
    window.addEventListener(
      'pointerup',
      event => finishPointerMiniDrag(event, false),
      true);
    window.addEventListener(
      'pointercancel',
      event => finishPointerMiniDrag(event, true),
      true);

    for (let index = 0; index < 6; ++index) {
      const item = document.getElementById(
        tab + '_climate_slot_row_' + index);
      if (!item) continue;

      item.draggable = false;
      item.addEventListener('pointerdown', event => {
        if (event.target.closest('[data-climate-resize]')) {
          return;
        }
        if (event.pointerType === 'mouse' &&
            event.button !== 0) {
          return;
        }
        if (pointerMiniDrag) return;
        event.stopPropagation();
        setOuterTileDragEnabled(false);
        pointerMiniDrag = {
          pointerId: event.pointerId,
          item,
          index,
          startX: event.clientX,
          startY: event.clientY,
          offsetX: 0,
          offsetY: 0,
          started: false
        };
        try {
          item.setPointerCapture?.(event.pointerId);
        } catch (_) {}
      });
      item.addEventListener('click', event => {
        event.stopPropagation();
        if (performance.now() < suppressMiniClickUntil) {
          event.preventDefault();
          return;
        }
        selectClimateEditorItem(tab, index);
        delete climatePendingEmptyByTab[tab];
      });

      item.querySelectorAll('[data-climate-resize]')
        .forEach(handle => {
          handle.addEventListener('pointerdown', event => {
            event.preventDefault();
            event.stopPropagation();
            selectClimateEditorItem(tab, index);
            materializeClimateAutomaticItems(tab);
            const spanW = document.getElementById(
              tab + '_tile_span_w')?.value || 1;
            const spanH = document.getElementById(
              tab + '_tile_span_h')?.value || 1;
            const { columns, rows } =
              climateGridDimensions(spanW, spanH);
            const configured = currentClimateSlotConfig(tab);
            const stored = currentClimateGeometry(tab);
            const items = stored.map(entry =>
              clampClimateGeometryItem(entry, columns, rows));
            const origin = { ...items[index] };
            const layouts =
              climateGridLayouts(tab, columns, rows);
            const activeIndices =
              climateActiveGridIndices(tab);
            const direction =
              String(handle.dataset.climateResize || 'se');
            item.classList.add('resizing');
            const onMove = moveEvent => {
              const cell = getGridElementCellFromPointer(
                grid, columns, rows,
                moveEvent.clientX, moveEvent.clientY);
              if (!cell) return;
              const candidate = {
                col: origin.col,
                row: origin.row,
                span_w: origin.spanW,
                span_h: origin.spanH
              };
              if (direction.includes('e')) {
                candidate.span_w =
                  cell.col - origin.col + 1;
              }
              if (direction.includes('s')) {
                candidate.span_h =
                  cell.row - origin.row + 1;
              }
              candidate.span_w = Math.max(
                1, Math.min(
                  columns - candidate.col,
                  candidate.span_w));
              candidate.span_h = Math.max(
                1, Math.min(
                  rows - candidate.row,
                  candidate.span_h));
              if (!canPlaceGridLayout(
                    layouts, activeIndices, index,
                    candidate, columns, rows, 0)) {
                item.classList.add('resize-invalid');
                return;
              }
              item.classList.remove('resize-invalid');
              layouts[index] = candidate;
              stored[index] = {
                col: candidate.col,
                row: candidate.row,
                spanW: candidate.span_w,
                spanH: candidate.span_h
              };
              setGridItemPosition(
                item, candidate.col, candidate.row,
                candidate.span_w, candidate.span_h);
              renderClimateEditorItem(
                tab, index, stored[index],
                configured[index]);
            };
            const onEnd = endEvent => {
              window.removeEventListener(
                'pointermove', onMove, true);
              window.removeEventListener(
                'pointerup', onEnd, true);
              window.removeEventListener(
                'pointercancel', onEnd, true);
              item.classList.remove(
                'resizing', 'resize-invalid');
              storeClimateGeometry(tab, stored);
              syncClimateSlotFields(tab);
              notifyClimateGridChanged(tab);
              setOuterTileDragEnabled(true);
            };
            window.addEventListener(
              'pointermove', onMove, true);
            window.addEventListener(
              'pointerup', onEnd, true);
            window.addEventListener(
              'pointercancel', onEnd, true);
          });
        });
    }
  }

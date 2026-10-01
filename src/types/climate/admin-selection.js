
  let climateGridDragState = null;
  let climateGridDragPreview = null;

  function createClimateMiniDragGhost(item, rect) {
    // The slot styles (grid layout of the controls and so on) are scoped under
    // .tile.climate. A bare clone in document.body loses them and collapses into
    // running text, so the wrapper restores the selector context.
    const ghost = document.createElement('div');
    ghost.className =
      'tile climate climate-content-editing climate-mini-drag-ghost';
    ghost.style.position = 'absolute';
    ghost.style.top = '-9999px';
    ghost.style.left = '-9999px';
    ghost.style.width = rect.width + 'px';
    ghost.style.height = rect.height + 'px';
    ghost.style.padding = '0';
    ghost.style.border = '0';
    ghost.style.pointerEvents = 'none';
    const clone = item.cloneNode(true);
    clone.classList.remove(
      'active', 'dragging', 'climate-mini-hover');
    clone.querySelectorAll('.tile-resize-handle')
      .forEach(handle => handle.remove());
    clone.style.position = 'absolute';
    clone.style.inset = '0';
    clone.style.gridArea = 'auto';
    ghost.appendChild(clone);
    document.body.appendChild(ghost);
    return ghost;
  }

  const climateSelectedItemByTab = Object.create(null);
  const climateSelectedCellByTab = Object.create(null);
  const climatePendingEmptyByTab = Object.create(null);
  const climateEditorSnapshotByTab = Object.create(null);
  const climatePendingPreviewSelectionByTab =
    Object.create(null);

  function cloneClimateEditorSnapshot(snapshot) {
    if (!snapshot) return null;
    return {
      tileIndex: snapshot.tileIndex,
      spanW: snapshot.spanW,
      spanH: snapshot.spanH,
      resolvedKinds: Array.isArray(snapshot.resolvedKinds)
        ? snapshot.resolvedKinds.slice()
        : []
    };
  }

  function captureClimateOuterResizeState(tab) {
    return {
      geometry: document.getElementById(
        tab + '_climate_geometry')?.value || '',
      slots: currentClimateSlotConfig(tab),
      layouts: currentClimateTargetLayouts(tab),
      editorSnapshot: cloneClimateEditorSnapshot(
        climateEditorSnapshotByTab[tab]),
      selectedItem: Number(climateSelectedItemByTab[tab]),
      selectedCell: Number(climateSelectedCellByTab[tab]),
      pendingEmpty: climatePendingEmptyByTab[tab]
        ? {
            index: climatePendingEmptyByTab[tab].index,
            geometry: {
              ...climatePendingEmptyByTab[tab].geometry
            }
          }
        : null
    };
  }

  function restoreClimateOuterResizeState(tab, state) {
    if (!state) return;
    const geometry = document.getElementById(
      tab + '_climate_geometry');
    if (geometry) geometry.value = state.geometry || '';
    for (let index = 0; index < 6; ++index) {
      const slot = document.getElementById(
        tab + '_climate_slot_' + index);
      if (slot) {
        slot.value = String(
          state.slots?.[index] ??
          CLIMATE_TILE_CONTENT.AUTO);
      }
      const layout = document.getElementById(
        tab + '_climate_layout_' + index);
      if (layout) {
        layout.value = String(
          state.layouts?.[index] ??
          CLIMATE_TARGET_LAYOUT.AUTO);
      }
    }
    if (state.editorSnapshot) {
      climateEditorSnapshotByTab[tab] =
        cloneClimateEditorSnapshot(state.editorSnapshot);
    } else {
      delete climateEditorSnapshotByTab[tab];
    }
    climateSelectedItemByTab[tab] =
      Number.isFinite(state.selectedItem)
        ? state.selectedItem : -1;
    climateSelectedCellByTab[tab] =
      Number.isFinite(state.selectedCell)
        ? state.selectedCell : -1;
    if (state.pendingEmpty) {
      climatePendingEmptyByTab[tab] = {
        index: state.pendingEmpty.index,
        geometry: { ...state.pendingEmpty.geometry }
      };
    } else {
      delete climatePendingEmptyByTab[tab];
    }
  }

  function previewClimateOuterResize(tab, state) {
    restoreClimateOuterResizeState(tab, state);
    syncClimateSlotFields(tab);
  }

  function climateOuterResizePreviewHtml(
      tab, state, spanW, spanH) {
    if (!state) return '';
    const configured = Array.isArray(state.slots)
      ? state.slots.slice(0, 6)
      : currentClimateSlotConfig(tab);
    while (configured.length < 6) {
      configured.push(CLIMATE_TILE_CONTENT.EMPTY);
    }
    const resolved =
      state.editorSnapshot?.resolvedKinds;
    if (Array.isArray(resolved)) {
      for (let index = 0; index < 6; ++index) {
        if (Number(configured[index]) !==
            CLIMATE_TILE_CONTENT.AUTO) {
          continue;
        }
        const kind = Number(resolved[index]);
        configured[index] =
          Number.isFinite(kind) && kind > 0
            ? kind
            : CLIMATE_TILE_CONTENT.EMPTY;
      }
    }
    return climatePreviewSlots(
      climateEditorState(tab),
      spanW,
      spanH,
      configured,
      state.layouts,
      state.geometry,
      document.getElementById(tab + '_climate_view')?.value === '1');
  }

  function requestClimatePreviewSelection(
      tab, tileIndex, itemIndex = -1, cellIndex = -1) {
    const sameTile =
      currentTileTab === tab &&
      currentTileIndex === tileIndex;
    const pendingSelection = {
      tileIndex,
      itemIndex,
      cellIndex
    };
    climatePendingPreviewSelectionByTab[tab] =
      pendingSelection;
    if (!sameTile && typeof selectTile === 'function') {
      selectTile(tileIndex, tab);
      // selectTile parks the previous editor first. Re-apply the requested
      // mini selection after that cleanup so the async tile load can consume it.
      climatePendingPreviewSelectionByTab[tab] =
        pendingSelection;
    }
    if (sameTile) {
      mountClimateMiniEditor(tab);
      syncClimateSlotFields(tab, true);
    }
  }

  function bindClimatePreviewSelection() {
    if (document.documentElement.dataset
          .climatePreviewSelectionBound === '1') {
      return;
    }
    document.documentElement.dataset
      .climatePreviewSelectionBound = '1';
    const previewTarget = event =>
      event.target?.closest?.(
        '[data-climate-preview-item],' +
        '[data-climate-preview-cell]');
    let hoveredPreview = null;
    let hoveredEditorItem = null;
    let hoveredChildTile = null;
    let hoveredParent = null;
    const setHoverTarget = (previous, next, className) => {
      if (previous === next) return previous;
      previous?.classList.remove(className);
      next?.classList.add(className);
      return next;
    };
    const clearClimateHover = () => {
      hoveredPreview =
        setHoverTarget(
          hoveredPreview, null, 'climate-preview-hover');
      hoveredEditorItem =
        setHoverTarget(
          hoveredEditorItem, null, 'climate-mini-hover');
      hoveredChildTile =
        setHoverTarget(
          hoveredChildTile, null, 'climate-child-hover');
      hoveredParent =
        setHoverTarget(
          hoveredParent, null, 'climate-parent-hover');
    };
    document.addEventListener('pointermove', event => {
      const preview = previewTarget(event);
      const editorItem =
        event.target?.closest?.('.climate-mini-tile') || null;
      const editorCell =
        event.target?.closest?.('.climate-mini-cell') || null;
      const tile =
        event.target?.closest?.('.tile.climate') || null;
      const overMini =
        !!preview || !!editorItem || !!editorCell ||
        !!event.target?.closest?.('.tile-resize-handle');
      const parent =
        tile?.classList.contains(
          'climate-mini-selection-active') &&
        !overMini
          ? tile
          : null;
      const childTile = tile && overMini ? tile : null;
      hoveredPreview =
        setHoverTarget(
          hoveredPreview, preview, 'climate-preview-hover');
      hoveredEditorItem =
        setHoverTarget(
          hoveredEditorItem, editorItem, 'climate-mini-hover');
      hoveredChildTile =
        setHoverTarget(
          hoveredChildTile, childTile, 'climate-child-hover');
      hoveredParent =
        setHoverTarget(
          hoveredParent, parent, 'climate-parent-hover');
    }, true);
    window.addEventListener('blur', clearClimateHover);
    document.documentElement.addEventListener(
      'pointerleave', clearClimateHover);
    document.addEventListener('pointerdown', event => {
      if (previewTarget(event)) event.stopPropagation();
    }, true);
    document.addEventListener('dragstart', event => {
      if (!previewTarget(event)) return;
      event.preventDefault();
      event.stopPropagation();
    }, true);
    document.addEventListener('click', event => {
      const target = previewTarget(event);
      if (!target) return;
      const tile = target.closest('.tile.climate');
      const section = tile?.closest('[id^="tab-tiles-"]');
      const tab = section?.id?.substring(
        'tab-tiles-'.length);
      const tileIndex = Number(tile?.dataset.index);
      if (!tab || !Number.isFinite(tileIndex)) return;
      event.preventDefault();
      event.stopPropagation();
      requestClimatePreviewSelection(
        tab,
        tileIndex,
        Number(target.dataset.climatePreviewItem ?? -1),
        Number(target.dataset.climatePreviewCell ?? -1));
    }, true);
  }

  function parkClimateMiniEditor(tab, preserveSelection = false) {
    const shell = document.getElementById(
      tab + '_climate_editor_shell');
    const stash = document.getElementById(
      tab + '_climate_editor_stash');
    const mountedTile = shell?.parentElement?.matches(
      '.tile.climate') ? shell.parentElement : null;
    if (mountedTile) {
      mountedTile.draggable = true;
    }
    if (shell && stash && shell.parentElement !== stash) {
      stash.appendChild(shell);
    }
    document.querySelectorAll(
      '#tab-tiles-' + tab +
      ' .tile-grid > .tile.climate.climate-content-editing')
      .forEach(tile => {
        tile.classList.remove(
          'climate-content-editing',
          'climate-mini-selection-active');
      });
    if (!preserveSelection) {
      climateSelectedItemByTab[tab] = -1;
      climateSelectedCellByTab[tab] = -1;
      delete climatePendingEmptyByTab[tab];
      delete climatePendingPreviewSelectionByTab[tab];
      delete climateEditorSnapshotByTab[tab];
      selectClimateEditorItem(tab, -1);
    }
  }

  function mountClimateMiniEditor(tab) {
    const shell = document.getElementById(
      tab + '_climate_editor_shell');
    const tile = document.getElementById(
      tab + '-tile-' + currentTileIndex);
    if (!shell ||
        currentTileTab !== tab ||
        !tile ||
        String(tile.dataset.type || '') !== '17') {
      parkClimateMiniEditor(tab);
      return false;
    }

    document.querySelectorAll(
      '#tab-tiles-' + tab +
      ' .tile-grid > .tile.climate.climate-content-editing')
      .forEach(candidate => {
        if (candidate !== tile) {
          candidate.classList.remove(
            'climate-content-editing');
        }
      });

    tile.classList.add('climate-content-editing');
    if (shell.parentElement !== tile) {
      shell.parentElement?.classList?.remove(
        'climate-mini-selection-active');
      tile.appendChild(shell);
    }
    return true;
  }

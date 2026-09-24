
  function syncClimateSlotFields(
      tab, finalizePreviewSelection = false) {
    if (Number(document.getElementById(tab + '_tile_type')?.value) !== 17) {
      parkClimateMiniEditor(tab);
      return;
    }
    mountClimateMiniEditor(tab);
    // Half steps do not change the mini-grid, so only whole cells count here.
    const spanW = Math.max(1, Math.floor(Number(document.getElementById(
      tab + '_tile_span_w')?.value) || 1));
    const spanH = Math.max(1, Math.floor(Number(document.getElementById(
      tab + '_tile_span_h')?.value) || 1));
    const capacity = climateSlotCapacity(spanW, spanH);
    const { columns, rows } =
      climateGridDimensions(spanW, spanH);
    let configured = currentClimateSlotConfig(tab);
    let resolvedKinds = climateResolvedEditorKinds(tab);
    const previousSnapshot = climateEditorSnapshotByTab[tab];
    if (previousSnapshot &&
        previousSnapshot.tileIndex === currentTileIndex &&
        (previousSnapshot.spanW !== spanW ||
         previousSnapshot.spanH !== spanH)) {
      const automaticOnly = configured.every(
        value =>
          Number(value) === CLIMATE_TILE_CONTENT.AUTO);
      if (automaticOnly) {
        // A pristine Climate tile follows the standard layout of its new
        // outer size. Clear the old-size geometry so 2x1 becomes
        // Current + compact target and 1x2 becomes Current + 1x2 target.
        const geometryInput = document.getElementById(
          tab + '_climate_geometry');
        if (geometryInput) geometryInput.value = '';
      } else {
        // Once the user has customized mini-tiles, resizing keeps that
        // explicit content instead of silently adding further items.
        materializeClimateAutomaticItems(
          tab, previousSnapshot.resolvedKinds);
        configured = currentClimateSlotConfig(tab);
        resolvedKinds = climateResolvedEditorKinds(tab);
      }
    }
    const placementConfig = climatePlacementConfig(
      configured, resolvedKinds);
    const stored = currentClimateGeometry(tab);
    const items = stored.map(entry =>
      clampClimateGeometryItem(entry, columns, rows));
    const grid = document.getElementById(
      tab + '_climate_content_grid');
    if (grid) {
      grid.style.setProperty(
        '--climate-editor-columns', String(columns));
      grid.style.setProperty(
        '--climate-editor-rows', String(rows));
    }

    const occupied = Array(columns * rows).fill(false);
    const accepted = [];
    for (let index = 0; index < 6; ++index) {
      const item = document.getElementById(
        tab + '_climate_slot_row_' + index);
      const kind = Number(configured[index]) || 0;
      const active =
        index < capacity &&
        kind !== CLIMATE_TILE_CONTENT.EMPTY &&
        resolvedKinds[index] !== null;
      if (!item) continue;
      item.classList.toggle('hidden', !active);
      if (!active) continue;

      let geometry = items[index];
      if (accepted.some(other =>
            climateGeometryOverlaps(
              geometry, other.geometry))) {
        const free = firstFreeClimatePlacement(
          items, placementConfig, capacity,
          columns, rows, index,
          geometry.spanW, geometry.spanH);
        if (!free) {
          item.classList.add('hidden');
          continue;
        }
        geometry = free;
        items[index] = free;
        stored[index] = free;
      }
      accepted.push({ index, geometry });
      setGridItemPosition(
        item, geometry.col, geometry.row,
        geometry.spanW, geometry.spanH);
      renderClimateEditorItem(
        tab, index, geometry, kind);
      for (let row = geometry.row;
           row < geometry.row + geometry.spanH; ++row) {
        for (let col = geometry.col;
             col < geometry.col + geometry.spanW; ++col) {
          occupied[row * columns + col] = true;
        }
      }

      const layout = document.getElementById(
        tab + '_climate_layout_' + index);
      if (layout) {
        layout.value = String(
          geometry.spanW > 1 && geometry.spanH === 1
            ? CLIMATE_TARGET_LAYOUT.HORIZONTAL
            : (geometry.spanH > 1
                ? CLIMATE_TARGET_LAYOUT.VERTICAL
                : CLIMATE_TARGET_LAYOUT.AUTO));
      }
    }

    grid?.querySelectorAll('.climate-mini-cell')
      .forEach(cell => {
      const cellIndex = Number(cell.dataset.climateCell);
      const row = Math.floor(cellIndex / columns);
      const col = cellIndex % columns;
      const visible =
        Number.isFinite(cellIndex) &&
        cellIndex >= 0 &&
        cellIndex < columns * rows;
      cell.classList.toggle('hidden', !visible);
      cell.classList.toggle(
        'occupied',
        !visible || !!occupied[cellIndex]);
      if (visible) {
        cell.style.gridColumn = String(col + 1);
        cell.style.gridRow = String(row + 1);
      }
      });

    storeClimateGeometry(tab, stored);
    bindClimateMiniGrid(tab);
    const directSelection =
      climatePendingPreviewSelectionByTab[tab];
    if (directSelection &&
        directSelection.tileIndex === currentTileIndex) {
      const directItem = Number(directSelection.itemIndex);
      const directCell = Number(directSelection.cellIndex);
      if (Number.isFinite(directItem) &&
          directItem >= 0 && directItem < 6) {
        const item = document.getElementById(
          tab + '_climate_slot_row_' + directItem);
        if (item && !item.classList.contains('hidden')) {
          climateSelectedItemByTab[tab] = directItem;
          climateSelectedCellByTab[tab] = -1;
          delete climatePendingEmptyByTab[tab];
          if (finalizePreviewSelection) {
            delete climatePendingPreviewSelectionByTab[tab];
          }
        }
      } else if (Number.isFinite(directCell) &&
                 directCell >= 0 &&
                 directCell < columns * rows) {
        const cell = document.getElementById(
          tab + '_climate_cell_' + directCell);
        const index = configured.findIndex(
          (value, candidate) =>
            candidate < capacity &&
            (Number(value) === CLIMATE_TILE_CONTENT.EMPTY ||
             (Number(value) === CLIMATE_TILE_CONTENT.AUTO &&
              resolvedKinds[candidate] === null)));
        if (cell &&
            !cell.classList.contains('hidden') &&
            !cell.classList.contains('occupied') &&
            index >= 0) {
          const row = Math.floor(directCell / columns);
          const col = directCell % columns;
          climatePendingEmptyByTab[tab] = {
            index,
            geometry: { col, row, spanW: 1, spanH: 1 }
          };
          climateSelectedItemByTab[tab] = index;
          climateSelectedCellByTab[tab] = directCell;
          if (finalizePreviewSelection) {
            delete climatePendingPreviewSelectionByTab[tab];
          }
        }
      }
    }
    let selected = Number(climateSelectedItemByTab[tab]);
    let selectedCell = Number(climateSelectedCellByTab[tab]);
    const selectedItem = Number.isFinite(selected)
      ? document.getElementById(
          tab + '_climate_slot_row_' + selected)
      : null;
    const selectedItemVisible =
      !!selectedItem &&
      !selectedItem.classList.contains('hidden');
    const selectedCellElement =
      Number.isFinite(selectedCell) && selectedCell >= 0
        ? document.getElementById(
            tab + '_climate_cell_' + selectedCell)
        : null;
    const selectedEmptyVisible =
      !!selectedCellElement &&
      !selectedCellElement.classList.contains('hidden') &&
      !selectedCellElement.classList.contains('occupied') &&
      climatePendingEmptyByTab[tab]?.index === selected;
    if (selectedItemVisible) {
      selectedCell = -1;
      selectClimateEditorItem(tab, selected);
    } else if (selectedEmptyVisible) {
      selectClimateEditorItem(
        tab, selected, selectedCell,
        CLIMATE_TILE_CONTENT.EMPTY);
    } else {
      selected = -1;
      selectedCell = -1;
      delete climatePendingEmptyByTab[tab];
      selectClimateEditorItem(tab, -1);
    }
    const selectedFields = document.getElementById(
      tab + '_climate_selected_fields');
    selectedFields?.classList.toggle(
      'hidden', selected < 0);
    climateEditorSnapshotByTab[tab] = {
      tileIndex: currentTileIndex,
      spanW,
      spanH,
      resolvedKinds: resolvedKinds.slice()
    };
  }

  function loadClimateFields(tab, data) {
    const entity = document.getElementById(tab + '_climate_entity');
    if (entity) {
      const configuredEntity =
        Object.prototype.hasOwnProperty.call(data, 'sensor_entity')
          ? (data.sensor_entity || '')
          : (data.climate_entity || '');
      entity.value = configuredEntity;
      if (configuredEntity) {
        entity.dataset.configuredValue = configuredEntity;
      } else {
        // The option list is rebuilt asynchronously. Do not let a value from
        // the previously edited climate tile come back when this tile
        // explicitly has no entity configured.
        delete entity.dataset.configuredValue;
      }
    }
    const popup = document.getElementById(tab + '_climate_popup_open_mode');
    if (popup) popup.value = (data.popup_open_mode !== undefined)
      ? String(data.popup_open_mode) : '1';
    const slots = decodeClimateSlotConfig(
      data.climate_slots_packed ?? data.sensor_gauge_min ?? 0);
    slots.forEach((value, index) => {
      const select = document.getElementById(
        tab + '_climate_slot_' + index);
      if (select) select.value = String(value);
    });
    const layouts = decodeClimateTargetLayouts(
      data.climate_layouts_packed ?? data.sensor_gauge_max ?? 0);
    layouts.forEach((value, index) => {
      const select = document.getElementById(
        tab + '_climate_layout_' + index);
      if (select) select.value = String(value);
    });
    const geometry = document.getElementById(
      tab + '_climate_geometry');
    if (geometry) {
      geometry.value =
        data.climate_geometry || data.scene_alias || '';
    }
    syncClimateSlotFields(tab, true);
    maybeFillTitleFromEntity(tab, '_climate_entity');
  }

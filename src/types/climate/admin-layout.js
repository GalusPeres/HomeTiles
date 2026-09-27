
  function selectClimateEditorItem(
      tab, index, cellIndex = -1, valueOverride = null) {
    const source = index >= 0
      ? document.getElementById(
          tab + '_climate_slot_' + index)
      : null;
    const hasSelection = index >= 0 && !!source;
    climateSelectedItemByTab[tab] =
      hasSelection ? index : -1;
    climateSelectedCellByTab[tab] =
      hasSelection ? cellIndex : -1;
    for (let candidate = 0; candidate < 6; ++candidate) {
      const item = document.getElementById(
        tab + '_climate_slot_row_' + candidate);
      const selected = hasSelection &&
        candidate === index &&
        item && !item.classList.contains('hidden');
      item?.classList.toggle('active', !!selected);
      if (item) {
        item.dataset.selected = selected ? '1' : '0';
      }
    }
    document.getElementById(
      tab + '_climate_content_grid')
      ?.querySelectorAll('.climate-mini-cell')
      .forEach(cell => {
        cell.classList.toggle(
          'active',
          Number(cell.dataset.climateCell) === cellIndex);
      });
    const shell = document.getElementById(
      tab + '_climate_editor_shell');
    const outerTile = shell?.parentElement?.matches(
      '.tile.climate') ? shell.parentElement : null;
    outerTile?.classList.toggle(
      'climate-mini-selection-active',
      hasSelection);
    if (outerTile) {
      outerTile.draggable = !hasSelection;
    }
    const editor = document.getElementById(
      tab + '_climate_selected_content');
    if (editor) {
      editor.disabled = !hasSelection;
      if (hasSelection) {
        editor.value = valueOverride !== null
          ? String(valueOverride) : source.value;
      }
    }
    syncClimateContentOptions(tab);
    const selectedFields = document.getElementById(
      tab + '_climate_selected_fields');
    selectedFields?.classList.toggle(
      'hidden', !hasSelection);
    if (selectedFields) {
      selectedFields.hidden = !hasSelection;
      selectedFields.closest('.climate-content-config')
        ?.classList.toggle('hidden', !hasSelection);
    }
  }

  function climateGridLayouts(tab, columns, rows) {
    return currentClimateGeometry(tab).map(entry => {
      const geometry =
        clampClimateGeometryItem(entry, columns, rows);
      return {
        col: geometry.col,
        row: geometry.row,
        span_w: geometry.spanW,
        span_h: geometry.spanH
      };
    });
  }

  function climateActiveGridIndices(tab) {
    const configured = currentClimateSlotConfig(tab);
    const resolved = climateResolvedEditorKinds(tab);
    const active = new Set();
    for (let index = 0; index < 6; ++index) {
      // Count only items that are actually placed: syncClimateSlotFields hides
      // slots without free space, and their stored geometry must not block drag
      // and resize as a phantom occupancy.
      const item = document.getElementById(
        tab + '_climate_slot_row_' + index);
      if (Number(configured[index]) !== CLIMATE_TILE_CONTENT.EMPTY &&
          resolved[index] !== null &&
          item && !item.classList.contains('hidden')) {
        active.add(index);
      }
    }
    return active;
  }

  function applyClimateGridLayouts(
      tab, layouts, activeIndices, baseLayouts = null) {
    activeIndices.forEach(index => {
      const item = document.getElementById(
        tab + '_climate_slot_row_' + index);
      const layout = layouts[index];
      if (!item || !layout) return;
      setGridItemPosition(
        item, layout.col, layout.row,
        layout.span_w, layout.span_h);
      const base = baseLayouts?.[index];
      item.classList.toggle(
        'reflow-preview',
        !!base &&
        (base.col !== layout.col || base.row !== layout.row));
    });
  }

  function storeClimateGridLayouts(tab, layouts) {
    const stored = currentClimateGeometry(tab);
    layouts.forEach((layout, index) => {
      if (!layout) return;
      stored[index] = {
        col: layout.col,
        row: layout.row,
        spanW: layout.span_w,
        spanH: layout.span_h
      };
    });
    storeClimateGeometry(tab, stored);
  }

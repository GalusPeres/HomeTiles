
  function applyTileDataToEditor(index, tab, data) {
        if (!data || typeof data !== 'object') return;
        if (!tilesData[tab]) tilesData[tab] = [];
        tilesData[tab][index] = data;
        const draftForTile = drafts[tab] && drafts[tab][index];
        if (draftForTile && draftForTile._dirty) {
          applySnapshotToTileData(tab, index, draftForTile);
        }
        if (currentTileTab !== tab || currentTileIndex !== index) return;
        const prefix = tab;
        syncTileTypeSelectValue(document.getElementById(prefix + '_tile_type'), data.type || 0);
        applyFolderTypeLock(prefix, Number(data.type) === 4 && data.folder_empty === false);
        resetAllTypeFields(tab);
        updateTileType(tab);
        document.getElementById(prefix + '_tile_title').value = data.title || '';
        document.getElementById(prefix + '_tile_icon').value = data.icon_name || '';
        const colorMeta = getTileTypeMeta(data.type || 0);
        setTileColorInputFromStored(tab, data.bg_color, colorMeta.defaultBg || '#2A2A2A');
        const colEl = document.getElementById(prefix + '_tile_col');
        const rowEl = document.getElementById(prefix + '_tile_row');
        const spanWEl = document.getElementById(prefix + '_tile_span_w');
        const spanHEl = document.getElementById(prefix + '_tile_span_h');
        if (colEl && rowEl && spanWEl && spanHEl) {
          const fallbackLayout = (data.type === 0) ? getTileElementLayout(tab, index) : null;
          const layoutInput = {
            type: data.type,
            col: data.col,
            row: data.row,
            span_w: data.span_w,
            span_h: data.span_h
          };
          if (fallbackLayout) {
            layoutInput.col = fallbackLayout.col;
            layoutInput.row = fallbackLayout.row;
            layoutInput.span_w = fallbackLayout.span_w;
            layoutInput.span_h = fallbackLayout.span_h;
          }
          const layout = normalizeTileLayout(layoutInput, index, tab);
          colEl.value = String(layout.col + 1);
          rowEl.value = String(layout.row + 1);
          spanWEl.value = String(layout.span_w);
          spanHEl.value = String(layout.span_h);
          syncTileSizePolicy(tab);
        }
        const meta = colorMeta;
        callTypeHandler(meta, 'load', prefix, data);
        syncIconDiscFields(prefix);
        refreshEntityOptionLists(prefix);
        syncGaugeUi(tab);
        const tileElem = document.getElementById(tab + '-tile-' + index);
        if (tileElem) {
          tileElem.classList.toggle('active', currentTileTab === tab && currentTileIndex === index);
        }
        const draft = (drafts[tab] || {})[index];
        if (draft && draft._dirty) {
          applyDraft(tab, index);
        } else {
          if (draft && data.type === 0 && draft.type !== data.type) clearDraft(tab, index);
          updateTilePreview(tab);
        }
        setupLivePreview(tab);
        restoreCurrentTileSelectionUi();
  }

  function loadTileData(index, tab) {
    const cached = getTilesData(tab)[index];
    if (cached && tileDataLoadedTabs.has(tab)) {
      applyTileDataToEditor(index, tab, cached);
      return;
    }
    const folderId = getFolderIdForTab(tab);
    if (folderId === undefined) return;
    const baseline = JSON.stringify(cached);
    fetch('/api/tiles?folder=' + encodeURIComponent(folderId) + '&index=' + index)
      .then(res => res.json())
      .then(data => {
        const current = getTilesData(tab)[index];
        const changed = JSON.stringify(current) !== baseline;
        applyTileDataToEditor(index, tab,
          current && (changed || drafts[tab]?.[index]?._dirty) ? current : data);
      })
      .catch(error => console.error('Tile load failed:', error));
  }

  function getCurrentTileType(tab) {
    const tiles = getTilesData(tab);
    if (tiles && currentTileIndex >= 0 && tiles[currentTileIndex]) {
      return String(tiles[currentTileIndex].type ?? '0');
    }
    const typeEl = document.getElementById(tab + '_tile_type');
    return typeEl ? String(typeEl.value) : '0';
  }

  function isLockedTileType(typeValue) {
    const meta = getTileTypeMeta(typeValue);
    return !!meta.locked;
  }

  function applySpecialTileUiState(tab) {
    const prefix = tab;
    const typeEl = document.getElementById(prefix + '_tile_type');
    const navSelect = document.getElementById(prefix + '_navigate_target');
    const noteEl = document.getElementById(prefix + '_navigate_note');
    const typeValue = typeEl ? String(typeEl.value) : '0';
    const meta = getTileTypeMeta(typeValue);
    const locked = !!meta.locked;
    // Fixed types (Settings, Back) and a folder with tiles (applyFolderTypeLock).
    if (typeEl) typeEl.disabled = locked || typeEl.dataset.folderLocked === '1';
    if (navSelect) navSelect.disabled = (!meta.fields || meta.fields !== 'navigate' || locked);
    if (noteEl) {
      if (typeValue === '7') noteEl.textContent = t('settingsTileFixed');
      else if (typeValue === '8') noteEl.textContent = t('backTileFixed');
      else noteEl.textContent = '';
    }
  }

  function updateTileType(tab) {
    const prefix = tab;
    const typeEl = document.getElementById(prefix + '_tile_type');
    let typeValue = typeEl ? typeEl.value : '0';
    const mediaType = Number(typeValue) === MEDIA_TILE_TYPE;
    const spanWEl = document.getElementById(prefix + '_tile_span_w');
    const spanHEl = document.getElementById(prefix + '_tile_span_h');
    if (spanWEl) {
      spanWEl.min = String(mediaType ? Math.min(MEDIA_TILE_MIN_SPAN, placeCols(tab)) : 1);
      spanWEl.max = String(mediaType ? Math.min(MEDIA_TILE_MAX_SPAN, placeCols(tab)) : placeCols(tab));
    }
    if (spanHEl) {
      const availableRows = placeRows(tab) - firstAllowedGridRow(tab);
      spanHEl.min = String(mediaType ? Math.min(MEDIA_TILE_MIN_SPAN, availableRows) : 1);
      spanHEl.max = String(mediaType
        ? Math.min(MEDIA_TILE_MAX_SPAN, availableRows)
        : placeRows(tab));
    }
    document.querySelectorAll('#' + prefix + 'Settings .type-fields').forEach(f => f.classList.remove('show'));
    const meta = getTileTypeMeta(typeValue);
    if (meta.fields) {
      const fieldsEl = document.getElementById(prefix + '_' + meta.fields + '_fields');
      if (fieldsEl) fieldsEl.classList.add('show');
    }
    if (meta.onSelect) {
      callTypeHandler(meta, 'onSelect', tab);
    }
    if (Number(typeValue) === 17) {
      syncClimateSlotFields(tab);
    }
    syncGaugeUi(tab);
    applySpecialTileUiState(tab);
    syncFolderPinControls(tab);
    syncTileSizePolicy(tab);
    syncIconDiscFields(tab);
    placeTileEntityField(tab);
    placeTileTypeParts(tab);
    syncTileAdvancedCaption(tab);
  }

  // Type options marked data-tile-place (Power flow, user 2026-10-26) move
  // to their place in the settings: "head" under Title and Icon, "top" at the
  // start of the settings, "color" into Color. They return to their type
  // fields when another type is shown. The shown type may also name the Auto
  // title and icon of a tile without an entity of its own.
  function placeTileTypeParts(tab) {
    const places = ['head', 'top', 'color'];
    for (const place of places) {
      const slot = document.getElementById(tab + '_tile_' + place + '_slot');
      for (const part of [...(slot?.children || [])]) document.getElementById(part.dataset.placeHome || '')?.append(part);
    }
    const fields = document.querySelector('#' + tab + 'Settings .type-fields.show');
    for (const part of fields?.querySelectorAll(':scope > [data-tile-place]') || []) {
      const slot = places.includes(part.dataset.tilePlace) && document.getElementById(tab + '_tile_' + part.dataset.tilePlace + '_slot');
      if (!slot) continue;
      part.dataset.placeHome = fields.id;
      slot.append(part);
    }
    const typeAuto = (element, key, value) => {
      if (!element) return;
      if (value) {
        element.dataset[key] = value;
        element.dataset.typeAuto = '1';
      } else if (element.dataset.typeAuto) {
        delete element.dataset[key];
        delete element.dataset.typeAuto;
      }
    };
    typeAuto(document.getElementById(tab + '_tile_title_auto'), 'autoTitle', fields?.dataset.autoTitle || '');
    typeAuto(document.getElementById(tab + '_tile_icon'), 'autoIcon', fields?.dataset.autoIcon || '');
    if (typeof refreshTileIconButtons === 'function') refreshTileIconButtons(tab);
  }

  // "Advanced" only above options the type shows (user 2026-10-11): its
  // visible fields, or the half-size note.
  function syncTileAdvancedCaption(tab) {
    const prefix = tab;
    const caption = document.getElementById(prefix + '_tile_group_advanced');
    if (!caption) return;
    const note = document.getElementById(prefix + '_tile_size_note');
    const fields = document.querySelector('#' + prefix + 'Settings .type-fields.show');
    const shown = el => !el.hidden && el.type !== 'hidden' && !el.closest('.hidden, [hidden]') &&
      (!el.isConnected || !el.offsetParent || el.getClientRects().length > 0);
    const any = (note && !note.hidden) ||
      !!fields && [...fields.querySelectorAll('input, select, textarea, button')].some(shown);
    caption.classList.toggle('hidden', !any);
  }

  function syncTileSizePolicy(tab) {
    const typeEl = document.getElementById(tab + '_tile_type');
    if (!typeEl) return;
    // A page's tile is one cell (page-fields.js).
    const h = typeof selectedPageTile === 'function' && selectedPageTile(tab)
      ? 1 : Number(document.getElementById(tab + '_tile_span_h')?.value || 1);
    // Half a row high only suits the half-size types.
    const halfHeight = h < 1;
    // A new half-height tile may still take a larger type when it can grow.
    const isNewTile = Number(getTilesData(tab)?.[currentTileIndex]?.type || 0) === 0;
    for (const option of typeEl.options) {
      if (option.dataset.sizeDisabled === '1') { option.disabled = false; delete option.dataset.sizeDisabled; }
      const type = Number(option.value);
      const grows = isNewTile && type !== 0 && !!grownNewTileLayout(tab, type);
      const blocked = type !== 0 && !grows && halfHeight && !supportsHalfSize(type);
      if (blocked && !option.disabled) {
        option.disabled = true; option.dataset.sizeDisabled = '1';
      }
    }
    const compact = supportsHalfSize(typeEl.value);
    for (const field of ['col', 'row', 'span_w', 'span_h']) {
      const input = document.getElementById(tab + '_tile_' + field);
      if (input) input.step = '0.5';
    }
    const row = document.getElementById(tab + '_tile_row');
    if (row) row.max = String(placeRows(tab) + (compact && h === 0.5 ? 0.5 : 0));
    const height = document.getElementById(tab + '_tile_span_h');
    if (height && compact) height.min = '0.5';
    const note = document.getElementById(tab + '_tile_size_note');
    if (note) note.hidden = !halfHeight;
  }

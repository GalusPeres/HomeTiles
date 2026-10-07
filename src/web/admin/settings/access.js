
  const SETTINGS_ACCESS_PREFIX = 'folder0_';
  const HIDDEN_SETTINGS_TILE_INDEX = -2;
  const settingsAccessElement = suffix =>
    document.getElementById(SETTINGS_ACCESS_PREFIX + suffix);

  function toggleSettingsAccessFields() {
    const pinEnabled =
      settingsAccessElement('settings_pin_enabled')?.checked === true;
    const pin = settingsAccessElement('settings_pin');
    const pinFields = settingsAccessElement('settings_pin_fields');
    const hidden = settingsAccessElement('settings_tile_hidden');
    const swipe = settingsAccessElement('settings_swipe_enabled');
    const edge = settingsAccessElement('settings_reveal_edge');
    const edgeFields = settingsAccessElement('settings_reveal_edge_fields');
    if (pin) pin.disabled = !pinEnabled;
    pinFields?.classList.toggle('is-hidden', !pinEnabled);
    const tileHidden = hidden?.checked === true;
    if (tileHidden && swipe) swipe.checked = true;
    if (swipe) swipe.disabled = tileHidden;
    const swipeEnabled = swipe?.checked === true;
    if (edge) edge.disabled = !swipeEnabled;
    edgeFields?.classList.toggle('is-hidden', !swipeEnabled);
  }

  let settingsAccessSaveQueue = Promise.resolve();
  let settingsAccessCommittedState = null;
  // Settings moves between grid and parking slot still being saved, and the
  // number of the latest one (hideSettingsTileFromGrid).
  let settingsTileTransfersInFlight = 0;
  let settingsTileTransferSeq = 0;

  function readSettingsAccessState() {
    const pinToggle = settingsAccessElement('settings_pin_enabled');
    const tileHidden =
      settingsAccessElement('settings_tile_hidden')?.checked === true;
    return {
      pinEnabled: pinToggle?.checked === true,
      pinConfigured: pinToggle?.dataset.pinConfigured === '1',
      tileHidden,
      swipeEnabled:
        tileHidden ||
        settingsAccessElement('settings_swipe_enabled')?.checked === true,
      revealEdge:
        String(settingsAccessElement('settings_reveal_edge')?.value || '0')
    };
  }

  function settingsAccessStatesEqual(a, b) {
    return !!a && !!b &&
      a.pinEnabled === b.pinEnabled &&
      a.pinConfigured === b.pinConfigured &&
      a.tileHidden === b.tileHidden &&
      a.swipeEnabled === b.swipeEnabled &&
      a.revealEdge === b.revealEdge;
  }

  function setSettingsPinStatus(configured) {
    const pinToggle = settingsAccessElement('settings_pin_enabled');
    const status = settingsAccessElement('settings_pin_status');
    if (pinToggle) pinToggle.dataset.pinConfigured = configured ? '1' : '0';
    if (status) {
      status.textContent = configured
        ? (status.dataset.configuredText || '')
        : (status.dataset.notConfiguredText || '');
    }
  }

  function restoreSettingsAccessState(state) {
    if (!state) return;
    const pinToggle = settingsAccessElement('settings_pin_enabled');
    const hidden = settingsAccessElement('settings_tile_hidden');
    const swipe = settingsAccessElement('settings_swipe_enabled');
    const edge = settingsAccessElement('settings_reveal_edge');
    if (pinToggle) pinToggle.checked = state.pinEnabled;
    if (hidden) hidden.checked = state.tileHidden;
    if (swipe) swipe.checked = state.tileHidden || state.swipeEnabled;
    if (edge) edge.value = state.revealEdge;
    setSettingsPinStatus(state.pinConfigured);
    toggleSettingsAccessFields();
  }

  function normalizeHiddenSettingsSnapshot(source = null) {
    const hiddenTile = document.getElementById('settingsHiddenTile');
    const editorCoordinates = source?._editor_coordinates === true ||
      source?.icon !== undefined || source?.color !== undefined ||
      source?.bg_color_default !== undefined;
    const bgValue = source && source.bg_color !== undefined
      ? Number(source.bg_color)
      : (source && source.bgColor !== undefined
          ? Number(source.bgColor)
          : Number(hiddenTile?.dataset.bgColor || 0));
    const isDefault = source && source.bg_color_default !== undefined
      ? String(source.bg_color_default) === '1'
      : tileBgFollowsDefault(bgValue);
    const color = source?.color ||
      tileBgToHex(bgValue, getTileTypeMeta('7').defaultBg || '#2A2A2A');
    const rawCol = Number(source?.col ?? hiddenTile?.dataset.col ?? 0);
    const rawRow = Number(source?.row ?? hiddenTile?.dataset.row ?? 0);
    return {
      _editor_coordinates: true,
      type: '7',
      title: String(source?.title ?? hiddenTile?.dataset.title ?? ''),
      icon: String(source?.icon ?? source?.icon_name ??
                   hiddenTile?.dataset.icon ?? 'cog'),
      color,
      bg_color_default: isDefault ? '1' : '0',
      bg_color: isDefault ? 0 : makeTileBgValue(hexToRgb(color)),
      col: String(Math.max(1, editorCoordinates ? rawCol : (rawCol + 1))),
      row: String(Math.max(1, editorCoordinates ? rawRow : (rawRow + 1))),
      span_w: String(source?.span_w ?? hiddenTile?.dataset.spanW ?? 1),
      span_h: String(source?.span_h ?? hiddenTile?.dataset.spanH ?? 1)
    };
  }

  function renderSettingsHiddenSlot(hidden, source = null) {
    const slot = document.getElementById('settingsHiddenSlot');
    const tile = document.getElementById('settingsHiddenTile');
    const hint = document.getElementById('settingsHiddenHint');
    if (!slot || !tile) return;
    const snapshot = normalizeHiddenSettingsSnapshot(source);
    slot.classList.toggle('has-tile', !!hidden);
    hint?.classList.toggle('is-hidden', !!hidden);
    tile.className = 'tile settings-hidden-tile ' +
      (hidden ? 'navigate' : 'empty');
    tile.draggable = !!hidden;
    tile.dataset.hidden = hidden ? '1' : '0';
    tile.dataset.type = hidden ? '7' : '0';
    tile.dataset.title = snapshot.title;
    tile.dataset.icon = snapshot.icon;
    tile.dataset.bgColor = String(snapshot.bg_color || 0);
    tile.dataset.col = String(Math.max(0, Number(snapshot.col || 1) - 1));
    tile.dataset.row = String(Math.max(0, Number(snapshot.row || 1) - 1));
    tile.dataset.spanW = String(snapshot.span_w || 1);
    tile.dataset.spanH = String(snapshot.span_h || 1);
    tile.innerHTML = '';
    if (!hidden) {
      tile.style.background = 'transparent';
      const icon = document.createElement('i');
      icon.className = 'mdi mdi-tray-arrow-down tile-icon';
      tile.appendChild(icon);
      return;
    }
    tile.style.background = snapshot.bg_color_default === '1'
      ? tileBackgroundCss(getTileTypeMeta('7'), true,
          getTileTypeMeta('7').defaultBg || '#2A2A2A')
      : snapshot.color;
    // The Settings PIN shows its lock here too (previewTileLocked); without
    // an icon the lock is the icon.
    const locked = typeof previewTileLocked === 'function' && previewTileLocked('7', tile);
    const iconName = normalizeMdiIconName(snapshot.icon) || (locked ? 'lock' : '');
    if (iconName) {
      const icon = document.createElement('i');
      icon.className = 'mdi mdi-' + iconName + ' tile-icon';
      if (locked && iconName !== 'lock') icon.innerHTML = PREVIEW_LOCK_MARK;
      tile.appendChild(icon);
    }
    if (snapshot.title) {
      const title = document.createElement('div');
      title.className = 'tile-title';
      title.innerHTML = tileTitleHtml(snapshot.title);
      tile.appendChild(title);
    }
    if (currentTileIndex === HIDDEN_SETTINGS_TILE_INDEX &&
        currentTileTab === 'folder0') {
      tile.classList.add('active');
    }
  }

  // The Settings tile shows a lock while the Settings PIN is on
  // (previewTileLocked): redraw it, in the grid or parked, once the PIN is
  // set or cleared.
  function refreshSettingsTileLock() {
    const editing = currentTileTab === 'folder0' &&
      (currentTileIndex === HIDDEN_SETTINGS_TILE_INDEX ||
       document.getElementById('folder0-tile-' + currentTileIndex)?.dataset.type === '7');
    if (editing && typeof updateTilePreview === 'function') {
      updateTilePreview('folder0');
    } else if (document.getElementById('settingsHiddenTile')?.dataset.hidden === '1') {
      renderSettingsHiddenSlot(true);
    } else {
      const tiles = getTilesData('folder0');
      const index = tiles.findIndex(item => Number(item?.type || 0) === 7);
      if (index >= 0) renderTileFromData('folder0', index, tiles[index], sensorMetaCache);
    }
  }

  // Shows a Settings move between the grid and the parking slot at once,
  // before the device has saved it (user 2026-10-02: the tile jumped back,
  // its teal selection lagged and it took long to move). The grid data, the
  // selection and the editor follow at once, so the tile can be moved again
  // right away; the reload after the save (reconcileSettingsTileUi) draws the
  // stored state, and a failed save draws it back. Restoring takes the first
  // empty index like TileConfig::ensureSettingsTile.
  // Returns the slot it showed the tile in, -1 when parked or not shown.
  function previewSettingsTileTransfer(hidden, snapshot, target = null) {
    const tiles = getTilesData('folder0');
    const isSettings = tile => Number(tile?.type || 0) === 7;
    const index = hidden
      ? tiles.findIndex(isSettings)
      : (tiles.some(isSettings) || !target ? -1 : tiles.findIndex(tile => !Number(tile?.type || 0)));
    if (index < 0) return -1;
    tiles[index] = hidden ? {type: 0} : {
      type: 7,
      title: snapshot.title,
      icon_name: snapshot.icon,
      bg_color: snapshot.bg_color,
      col: target.col,
      row: target.row,
      span_w: snapshot.span_w,
      span_h: snapshot.span_h
    };
    renderTileFromData('folder0', index, tiles[index], sensorMetaCache);
    layoutTiles('folder0', tiles);
    renderSettingsHiddenSlot(hidden, snapshot);
    if (hidden) selectHiddenSettingsTile();
    else selectTile(index, 'folder0');
    return hidden ? -1 : index;
  }

  function currentGridSettingsSnapshot() {
    const tile = (getTilesData('folder0') || []).find(
      item => Number(item?.type || 0) === 7);
    return tile ? normalizeHiddenSettingsSnapshot(tile) : null;
  }

  async function reconcileSettingsTileUi(
      hiddenWanted, snapshotHint = null, selectRestoredSettings = false) {
    try {
      const tiles = await fetchTileGridData('folder0', true);
      tiles.forEach((tile, index) =>
        renderTileFromData('folder0', index, tile, sensorMetaCache));
      layoutTiles('folder0', tiles);
      const settingsIndex = tiles.findIndex(
        tile => Number(tile?.type || 0) === 7);
      const hidden = !!hiddenWanted && settingsIndex < 0;
      renderSettingsHiddenSlot(hidden, snapshotHint);
      if (hidden) {
        selectHiddenSettingsTile();
      } else if (settingsIndex >= 0 &&
                 (selectRestoredSettings ||
                  currentTileIndex === HIDDEN_SETTINGS_TILE_INDEX)) {
        selectTile(settingsIndex, 'folder0');
      } else {
        restoreCurrentTileSelectionUi();
      }
      return true;
    } catch (error) {
      showNotification(error?.message || t('networkError'), false);
      return false;
    }
  }

  async function saveSettingsAccess(
      pinValue = null, target = null, tileSnapshot = null,
      requestedState = null, reconcileAfterSave = true) {
    const pinToggle = settingsAccessElement('settings_pin_enabled');
    const hidden = settingsAccessElement('settings_tile_hidden');
    const swipe = settingsAccessElement('settings_swipe_enabled');
    const edge = settingsAccessElement('settings_reveal_edge');
    const pinApply = settingsAccessElement('settings_pin_apply');
    if (!pinToggle || !hidden || !swipe || !edge) return false;

    const requested = requestedState || readSettingsAccessState();
    const visibilityChanged = settingsAccessCommittedState &&
      settingsAccessCommittedState.tileHidden !== requested.tileHidden;
    const visibilityMismatch =
      !!currentGridSettingsSnapshot() !== !requested.tileHidden;
    const snapshotHint = tileSnapshot ||
      (requested.tileHidden
        ? (currentGridSettingsSnapshot() ||
           normalizeHiddenSettingsSnapshot())
        : normalizeHiddenSettingsSnapshot());
    const hasNewPin = typeof pinValue === 'string';
    const persistPinEnabled =
      requested.pinEnabled && (requested.pinConfigured || hasNewPin);
    const body = new URLSearchParams();
    body.set('_ajax', '1');
    body.set('_access_only', '1');
    body.set('settings_access_present', '1');
    if (persistPinEnabled) body.set('settings_pin_enabled', '1');
    if (requested.tileHidden) body.set('settings_tile_hidden', '1');
    if (requested.swipeEnabled) body.set('settings_swipe_enabled', '1');
    body.set('settings_reveal_edge', requested.revealEdge);
    if (hasNewPin) body.set('settings_pin', pinValue);
    if (target && [target.col, target.row].every(value =>
        Number.isFinite(value) && value >= 0 && Number.isInteger(value * 2))) {
      body.set('settings_tile_target_col', String(target.col));
      body.set('settings_tile_target_row', String(target.row));
    }
    if (tileSnapshot) {
      const snapshot = normalizeHiddenSettingsSnapshot(tileSnapshot);
      const layout = normalizeSnapshotLayout(
        snapshot, HIDDEN_SETTINGS_TILE_INDEX, 'folder0');
      body.set('settings_tile_snapshot_present', '1');
      body.set('settings_tile_title', snapshot.title);
      body.set('settings_tile_icon', snapshot.icon);
      body.set('settings_tile_col', String(layout.col));
      body.set('settings_tile_row', String(layout.row));
      body.set('settings_tile_span_w', String(layout.span_w));
      body.set('settings_tile_span_h', String(layout.span_h));
      if (snapshot.bg_color_default === '1') {
        body.set('settings_tile_bg_color_default', '1');
      } else {
        body.set('settings_tile_bg_color',
                 String(hexToRgb(snapshot.color)));
      }
    }

    const lockedBefore = pinToggle.dataset.pinConfigured === '1';
    if (pinApply && hasNewPin) pinApply.disabled = true;
    try {
      const response = await fetch('/mqtt', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
        body
      });
      const result = await response.json().catch(() => ({}));
      if (!response.ok || !result.ok) {
        throw new Error(result.error || t('networkErrorSave'));
      }

      const storedPin = persistPinEnabled
        ? String(result.settings_pin || '')
        : '';
      // A legacy hash-only PIN cannot be returned by the device. Keep a newly
      // typed, not-yet-applied PIN when another access option is saved.
      const replacePinInput =
        hasNewPin || !persistPinEnabled || storedPin.length > 0;
      const pinInput = settingsAccessElement('settings_pin');
      if (pinInput && replacePinInput) {
        pinInput.value = storedPin;
        pinInput.type = 'password';
        const showButton = pinInput.closest('.password-field')
          ?.querySelector('.password-toggle');
        if (showButton) {
          showButton.textContent = showButton.dataset.labelShow || '';
        }
      }
      if (hasNewPin) {
        setSettingsPinStatus(true);
      } else if (!requested.pinEnabled) {
        setSettingsPinStatus(false);
      }
      toggleSettingsAccessFields();
      if ((pinToggle.dataset.pinConfigured === '1') !== lockedBefore) refreshSettingsTileLock();
      const savedState = {
        ...requested,
        pinEnabled: persistPinEnabled,
        pinConfigured: persistPinEnabled
      };
      settingsAccessCommittedState = savedState;
      if (reconcileAfterSave &&
          (visibilityChanged || visibilityMismatch)) {
        await reconcileSettingsTileUi(requested.tileHidden, snapshotHint);
      } else if (reconcileAfterSave && tileSnapshot &&
                 requested.tileHidden) {
        renderSettingsHiddenSlot(true, tileSnapshot);
      }
      // The device's answer (truthy): a Settings move reads settings_tile_index.
      return result;
    } catch (error) {
      if (!hasNewPin &&
          settingsAccessStatesEqual(readSettingsAccessState(), requested)) {
        restoreSettingsAccessState(settingsAccessCommittedState);
      }
      showNotification(error?.message || t('networkErrorSave'), false);
      return false;
    } finally {
      if (pinApply && hasNewPin) pinApply.disabled = false;
    }
  }

  function queueSettingsAccessSave(
      pinValue = null, target = null, tileSnapshot = null,
      reconcileAfterSave = true) {
    const requestedState = {...readSettingsAccessState()};
    const requestedTarget = target ? {...target} : null;
    const requestedSnapshot = tileSnapshot
      ? normalizeHiddenSettingsSnapshot(tileSnapshot)
      : null;
    settingsAccessSaveQueue = settingsAccessSaveQueue
      .catch(() => {})
      .then(() => saveSettingsAccess(
        pinValue, requestedTarget, requestedSnapshot, requestedState,
        reconcileAfterSave));
    return settingsAccessSaveQueue;
  }

  function initSettingsAccessControls() {
    const pinToggle = settingsAccessElement('settings_pin_enabled');
    const pinInput = settingsAccessElement('settings_pin');
    const pinApply = settingsAccessElement('settings_pin_apply');
    const hidden = settingsAccessElement('settings_tile_hidden');
    const swipe = settingsAccessElement('settings_swipe_enabled');
    const edge = settingsAccessElement('settings_reveal_edge');
    if (!pinToggle || !hidden || !swipe || !edge) return;

    settingsAccessCommittedState = readSettingsAccessState();
    pinToggle.addEventListener('change', () => {
      toggleSettingsAccessFields();
      if (pinToggle.checked && pinToggle.dataset.pinConfigured !== '1') {
        pinInput?.focus();
        return;
      }
      queueSettingsAccessSave();
    });
    hidden.addEventListener('change', async () => {
      toggleSettingsAccessFields();
      const transferred = hidden.checked
        ? await hideSettingsTileFromGrid()
        : await restoreHiddenSettingsTile();
      if (!transferred) {
        restoreSettingsAccessState(settingsAccessCommittedState);
      }
    });
    swipe.addEventListener('change', () => {
      toggleSettingsAccessFields();
      queueSettingsAccessSave();
    });
    edge.addEventListener('change', () => queueSettingsAccessSave());
    pinApply?.addEventListener('click', () => {
      if (!pinToggle.checked) pinToggle.checked = true;
      toggleSettingsAccessFields();
      queueSettingsAccessSave(pinInput?.value || '');
    });
    pinInput?.addEventListener('keydown', event => {
      if (event.key !== 'Enter') return;
      event.preventDefault();
      pinApply?.click();
    });
  }

  function initAdminSettingsSave() {
    const form = document.getElementById('admin_settings_form');
    if (!form) return;
    form.addEventListener('submit', async event => {
      event.preventDefault();
      const requestedLanguage = String(
        form.elements.namedItem('language')?.value || '').toLowerCase();
      const currentLanguage = String(
        document.documentElement.lang || APP_LOCALE || '').toLowerCase();
      // Several pages carry a Save for this form; the pressed one shows the result.
      const submitButton = event.submitter ||
        document.querySelector('button[form="admin_settings_form"][type="submit"]');
      const originalLabel = submitButton ? submitButton.textContent : '';
      if (submitButton) submitButton.disabled = true;
      try {
        const body = new URLSearchParams(new FormData(form));
        body.set('_ajax', '1');
        const response = await fetch('/mqtt', {
          method: 'POST',
          headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
          body
        });
        const result = await response.json().catch(() => ({}));
        if (!response.ok || !result.ok) {
          throw new Error(result.error || t('networkErrorSave'));
        }
        if (submitButton) submitButton.textContent = '\u2713 ' + originalLabel;
        // The form already contains the saved values. Re-fetching and parsing
        // the complete admin page here blocked LVGL for several seconds.
        // Only a language change requires rebuilding translated server HTML.
        if (result.reload ||
            (requestedLanguage && requestedLanguage !== currentLanguage)) {
          setTimeout(() => location.reload(), 250);
        }
        setTimeout(() => {
          if (submitButton) submitButton.textContent = originalLabel;
        }, 1800);
      } catch (error) {
        showNotification(error?.message || t('networkErrorSave'), false);
      } finally {
        if (submitButton) submitButton.disabled = false;
      }
    });
  }

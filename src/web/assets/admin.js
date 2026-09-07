function t(key) {
    return Object.prototype.hasOwnProperty.call(APP_I18N, key) ? APP_I18N[key] : key;
  }
  function tf(key, replacements) {
    let out = t(key);
    if (!replacements) return out;
    Object.keys(replacements).forEach(name => {
      out = out.replaceAll('{' + name + '}', String(replacements[name]));
    });
    return out;
  }
  let APP_LOCALE = document.documentElement.lang || 'en';
  function formatLocalizedNumber(value, decimals = 0, trimTrailingZeros = false) {
    const numeric = Number(String(value ?? '').trim().replace(',', '.'));
    if (!Number.isFinite(numeric)) return '--';
    const digits = Math.max(0, Math.min(6, Number.parseInt(decimals, 10) || 0));
    return new Intl.NumberFormat(APP_LOCALE, {
      useGrouping: false,
      minimumFractionDigits: trimTrailingZeros ? 0 : digits,
      maximumFractionDigits: digits
    }).format(numeric);
  }
  function localizeNumericText(value) {
    const text = String(value ?? '').trim();
    if (!text.length) return text;
    const normalized = text.replace(',', '.');
    if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(normalized)) return text;
    const fraction = normalized.includes('.') ? normalized.split('.')[1].length : 0;
    return formatLocalizedNumber(Number(normalized), fraction, false);
  }

  let normalTileBordersSaveSequence = 0;
  function applyNormalTileBordersPreview(enabled) {
    document.querySelectorAll('.normal-tile-border-toggle').forEach(input => {
      input.checked = !!enabled;
    });
    document.querySelectorAll('.tile-grid:not(.screensaver-tile-grid)').forEach(grid => {
      grid.classList.toggle('tiles-bordered', !!enabled);
    });
  }
  async function saveNormalTileBorders(enabled) {
    const wanted = !!enabled;
    const previous = !wanted;
    const sequence = ++normalTileBordersSaveSequence;
    applyNormalTileBordersPreview(wanted);
    try {
      const response = await fetch('/api/display/tile-borders', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'enabled=' + (wanted ? '1' : '0')
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
    } catch (error) {
      if (sequence !== normalTileBordersSaveSequence) return;
      applyNormalTileBordersPreview(previous);
      showNotification(t('networkErrorSave'), false);
    }
  }
  let tabSwitchSequence = 0;

  function folderIdFromAdminTabName(tabName) {
    const match = /^tab-tiles-folder(\d+)$/.exec(String(tabName || ''));
    return match ? Number(match[1]) : null;
  }

  // Each tab button names the panel it opens, so the active one is found by that
  // attribute instead of by scanning its inline handler for a quoted name.
  // aria-current tells assistive technology which tab is open; the active class
  // only paints it.
  function setActiveTabButton(tabName) {
    const buttons = Array.from(document.querySelectorAll('.tab-btn'));
    buttons.forEach(button => {
      button.classList.remove('active');
      button.removeAttribute('aria-current');
    });
    const active = buttons.find(button => button.dataset.tabTarget === tabName);
    if (!active) return;
    active.classList.add('active');
    active.setAttribute('aria-current', 'page');
  }

  async function switchTab(tabName) {
    const sequence = ++tabSwitchSequence;
    let target = document.getElementById(tabName);
    if (!target) {
      const folderId = folderIdFromAdminTabName(tabName);
      if (folderId !== null) {
        const loaded = await ensureFolderTabUi(folderId);
        if (!loaded || sequence !== tabSwitchSequence) return;
        target = document.getElementById(tabName);
      }
    }
    if (!target || sequence !== tabSwitchSequence) return;

    const isTileTab = tabName.startsWith('tab-tiles-');
    const tileTab = isTileTab
      ? tabName.substring('tab-tiles-'.length)
      : '';
    let needsTileData = false;
    if (isTileTab) {
      // Keep the previous tab interactive until the requested editor has its
      // complete grid. This prevents an index request or edit racing the
      // initial full-grid response.
      needsTileData = !tileDataLoadedTabs.has(tileTab);
      if (needsTileData) {
        try {
          await fetchTileGridData(tileTab, false);
        } catch (error) {
          console.error('Tile grid load failed:', error);
        }
        if (sequence !== tabSwitchSequence) return;
        if (!tileDataLoadedTabs.has(tileTab) || dragSource || resizeState) {
          showNotification(t('networkError'), false);
          return;
        }
      }
      if (sequence !== tabSwitchSequence) return;
      const freshTiles = getTilesData(tileTab);
      if (sessionRestoredFolderTabs.has(tileTab)) {
        freshTiles.forEach((tile, index) => {
          renderTileFromData(tileTab, index, tile, sensorMetaCache);
        });
        layoutTiles(tileTab, freshTiles);
        sessionRestoredFolderTabs.delete(tileTab);
      } else if (needsTileData) {
        syncTileGridStructure(tileTab, freshTiles);
      }
    }

    const tabs = document.querySelectorAll('.tab-content');
    tabs.forEach(tab => tab.classList.remove('active'));
    target.classList.add('active');
    setActiveTabButton(tabName);
    try { localStorage.setItem('activeAdminTab', tabName); } catch (e) {}
    updateTileSettingsMaxHeight();
    if (isTileTab) {
      const folderId = getFolderIdForTab(tileTab);
      if (folderId > 0 && folderId !== SCREENSAVER_FOLDER_ID) {
        touchFolderTabSessionCache(folderId);
      }
      if (tileTab === 'screensaver') {
        initScreensaverEditor();
      } else {
        const rememberedIndex = getRememberedTileIndex(tileTab);
        selectTile(rememberedIndex === null ? getTopLeftConfiguredTileIndex(tileTab) : rememberedIndex, tileTab);
        window.requestAnimationFrame(restoreCurrentTileSelectionUi);
      }
      // Let the browser paint the selected tab before cached/live values are
      // reconciled. This also keeps a cache hit from extending click latency.
      window.requestAnimationFrame(() => window.setTimeout(() => {
        if (document.getElementById(tabName)?.classList.contains('active')) {
          loadSensorValues(false, false, [tileTab]);
        }
      }, 0));
    }
    if (tabName === 'tab-network') {
      window.setTimeout(() => {
        if (typeof loadFileManager === 'function' && !fileManagerLoaded) loadFileManager();
      }, 0);
    }
    if (tabName === 'tab-hardware') {
      window.setTimeout(initHardwareIo, 0);
    }
  }

  // Caps the tile settings panel at exactly the space below header and tabs so
  // that it scrolls internally instead of stretching the page.
  function updateTileSettingsMaxHeight() {
    document.querySelectorAll('.tile-settings').forEach(panel => {
      panel.style.maxHeight = '';
      if (window.innerWidth <= 1180) return;
      const tab = panel.closest('.tab-content');
      if (!tab || !tab.classList.contains('active')) return;
      const top = panel.getBoundingClientRect().top + window.scrollY;
      // Only card padding and wrapper spacing sit below the panel. Read those
      // from the styles instead of measuring scrollHeight: on large windows
      // scrollHeight is at least the viewport height and would cap the panel
      // far too small.
      let below = 24;
      const card = panel.closest('.card');
      if (card) {
        const ccs = getComputedStyle(card);
        below = (parseFloat(ccs.paddingBottom) || 0) + (parseFloat(ccs.borderBottomWidth) || 0);
        const wrapper = card.parentElement;
        if (wrapper) {
          const wcs = getComputedStyle(wrapper);
          below += (parseFloat(wcs.paddingBottom) || 0) + (parseFloat(wcs.marginBottom) || 0);
        }
      }
      const h = window.innerHeight - top - below;
      if (h > 240) panel.style.maxHeight = h + 'px';
    });
  }
  // Resize fires many times per second while a window is dragged, and both
  // handlers below are expensive: one forces a layout and reads computed styles
  // per panel, the other re-renders the whole screensaver editor. Coalescing to
  // one call per frame keeps that work off every single event.
  function perFrame(callback) {
    let frame = 0;
    return () => {
      if (frame) return;
      frame = requestAnimationFrame(() => {
        frame = 0;
        callback();
      });
    };
  }

  window.addEventListener('resize', perFrame(updateTileSettingsMaxHeight));

  // Fills the server-rendered clock tiles (--:-- placeholders) with the current
  // time and keeps them up to date. Clock tiles re-rendered by this script get
  // their time, including the format, while rendering.
  function fillStaticClockPreviews() {
    if (typeof getClockPreviewTime !== 'function') return;
    document.querySelectorAll('.tile-clock-time').forEach(el => {
      if (el.dataset.autoClock === '1' || el.textContent.trim() === '--:--') {
        el.dataset.autoClock = '1';
        el.textContent = getClockPreviewTime(0);
      }
    });
    document.querySelectorAll('.tile-clock-date').forEach(el => {
      if (el.dataset.autoClock === '1' || el.textContent.trim() === '--.--.----') {
        el.dataset.autoClock = '1';
        el.textContent = getClockPreviewDate(0);
      }
    });
    if (screensaverDraft) {
      const time = document.getElementById('screensaverClockTime');
      const date = document.getElementById('screensaverClockDate');
      if (time) time.textContent = getClockPreviewTime(screensaverDraft.time_format);
      if (date) date.textContent = getScreensaverClockPreviewDate(screensaverDraft);
    }
  }

  function toggleStaticIpFields(checkboxId, fieldsId, noteId) {
    const checkbox = document.getElementById(checkboxId);
    const fields = document.getElementById(fieldsId);
    const note = document.getElementById(noteId);
    if (!checkbox || !fields) return;
    const useStatic = checkbox.checked;
    fields.classList.toggle('is-hidden', !useStatic);
    if (note) {
      note.textContent = useStatic
        ? (note.dataset.staticNote || '')
        : (note.dataset.dhcpNote || '');
    }
  }

  function toggleStaticNetworkFields() {
    toggleStaticIpFields(
      'network_use_static', 'network_static_fields',
      'network_ip_mode_note');
  }

  function toggleNetworkSettings() {
    const mode = document.getElementById('network_mode');
    const wifi = document.getElementById('wifi_network_settings');
    if (!mode || !wifi) return;
    const useEthernet = mode.value === 'ethernet';
    wifi.classList.toggle('is-hidden', useEthernet);
  }

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
  let settingsTileTransferInFlight = false;

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
      : !tileBgValueIsSet(bgValue);
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
      ? (getTileTypeMeta('7').defaultBg || '#2A2A2A')
      : snapshot.color;
    const iconName = normalizeMdiIconName(snapshot.icon);
    if (iconName) {
      const icon = document.createElement('i');
      icon.className = 'mdi mdi-' + iconName + ' tile-icon';
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
    if (target && Number.isInteger(target.col) && Number.isInteger(target.row)) {
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
      return true;
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
      const submitButton =
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

  function togglePasswordVisibility(inputId, buttonEl) {
    const input = document.getElementById(inputId);
    if (!input || !buttonEl) return;
    const showLabel = buttonEl.dataset.labelShow || 'Show';
    const hideLabel = buttonEl.dataset.labelHide || 'Hide';
    const isHidden = input.type === 'password';
    input.type = isHidden ? 'text' : 'password';
    buttonEl.textContent = isHidden ? hideLabel : showLabel;
  }

  function updateOtaFileName(inputEl) {
    const nameEl = document.getElementById('ota_file_name');
    if (!nameEl) return;
    const file = inputEl && inputEl.files && inputEl.files.length ? inputEl.files[0] : null;
    nameEl.textContent = file ? file.name : t('otaNoFileSelected');
  }

  async function createScreenshotAndDownload() {
    showNotification(t('screenshotCreating'));
    try {
      const res = await fetch('/api/screenshot', { method: 'POST' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.success) {
        throw new Error(data.error || t('screenshotFailed'));
      }
      showNotification(t('screenshotSaved'));
      const link = document.createElement('a');
      link.href = '/api/screenshot/download?ts=' + Date.now();
      link.download = 'ui_screenshot.jpg';
      document.body.appendChild(link);
      link.click();
      link.remove();
    } catch (err) {
      showNotification(err?.message || t('screenshotFailed'), false);
    }
  }

  let fileManagerLoaded = false;
  const fileManagerState = { fs: 'sd', path: '/', selected: null, sdAvailable: null };

  function fileManagerText(de, en) {
    return (document.documentElement.lang || '').toLowerCase().startsWith('de') ? de : en;
  }

  async function downloadCrashLog() {
    try {
      const res = await fetch('/api/crashlog?ts=' + Date.now());
      if (res.status === 404) {
        showNotification(fileManagerText('Kein Absturz aufgezeichnet.', 'No crash recorded.'));
        return;
      }
      if (!res.ok) throw new Error();
      const blob = await res.blob();
      const url = URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = url;
      link.download = 'crashlog.txt';
      document.body.appendChild(link);
      link.click();
      link.remove();
      URL.revokeObjectURL(url);
    } catch (err) {
      showNotification(fileManagerText('Download fehlgeschlagen.', 'Download failed.'), false);
    }
  }

  async function eraseCoreDump() {
    if (!confirm(fileManagerText('Gespeicherten Core-Dump wirklich l\u00f6schen?', 'Really delete the stored core dump?'))) return;
    try {
      const res = await fetch('/api/coredump/erase', { method: 'POST' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.success) {
        throw new Error(data.error || fileManagerText('L\u00f6schen fehlgeschlagen.', 'Delete failed.'));
      }
      const actions = document.getElementById('coredump_actions');
      if (actions) actions.style.display = 'none';
      showNotification(fileManagerText('Core-Dump gel\u00f6scht.', 'Core dump deleted.'));
    } catch (err) {
      showNotification(err?.message || fileManagerText('L\u00f6schen fehlgeschlagen.', 'Delete failed.'), false);
    }
  }

  function normalizeFileManagerClientPath(raw) {
    let path = String(raw || '').trim().replaceAll('\\', '/');
    if (!path) path = '/';
    if (!path.startsWith('/')) path = '/' + path;
    while (path.includes('//')) path = path.replaceAll('//', '/');
    while (path.length > 1 && path.endsWith('/')) path = path.slice(0, -1);
    return path;
  }

  function setFileManagerStatus(message, success = null) {
    const el = document.getElementById('file_manager_status');
    if (!el) return;
    el.textContent = message || '';
    el.classList.remove('error', 'success');
    if (success === true) el.classList.add('success');
    if (success === false) el.classList.add('error');
  }

  function setFileManagerStorageState(available, message = null) {
    fileManagerState.sdAvailable = available;
    const badge = document.getElementById('file_manager_sd_state');
    if (badge) {
      badge.classList.remove('ok', 'error', 'checking');
      if (available === true) {
        badge.classList.add('ok');
        badge.textContent = message || fileManagerText('microSD erkannt', 'microSD detected');
      } else if (available === false) {
        badge.classList.add('error');
        badge.textContent = message || fileManagerText('Keine microSD', 'No microSD');
      } else {
        badge.classList.add('checking');
        badge.textContent = message || fileManagerText('Pr\u00fcfe...', 'Checking...');
      }
    }
    document.querySelectorAll('.file-manager-requires-sd').forEach(el => {
      el.disabled = available !== true;
    });
    updateFileManagerSelectionBar();
  }

  function updateFileManagerUploadName(inputEl) {
    const nameEl = document.getElementById('file_manager_upload_name');
    if (!nameEl) return;
    const files = inputEl && inputEl.files ? Array.from(inputEl.files) : [];
    if (!files.length) {
      nameEl.textContent = fileManagerText('Keine Datei ausgew\u00e4hlt', 'No file selected');
    } else if (files.length === 1) {
      nameEl.textContent = files[0].name;
    } else {
      nameEl.textContent = files.length + fileManagerText(
        ' Dateien ausgew\u00e4hlt', ' files selected');
    }
  }

  function formatFileManagerSize(entry) {
    if (!entry || entry.dir) return '-';
    let size = Number(entry.size || 0);
    if (!Number.isFinite(size) || size < 0) size = 0;
    if (size < 1024) return size + ' B';
    if (size < 1024 * 1024) {
      return formatLocalizedNumber(size / 1024, size < 10 * 1024 ? 1 : 0) + ' KB';
    }
    return formatLocalizedNumber(
      size / (1024 * 1024), size < 10 * 1024 * 1024 ? 1 : 0) + ' MB';
  }

  function formatFileManagerModified(entry) {
    const ts = Number(entry && entry.modified ? entry.modified : 0);
    if (!Number.isFinite(ts) || ts <= 0) return '-';
    const date = new Date(ts * 1000);
    if (!Number.isFinite(date.getTime()) || date.getFullYear() < 2020) return '-';
    return date.toLocaleString(undefined, {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit'
    });
  }

  function fileManagerParentOf(path) {
    const current = normalizeFileManagerClientPath(path || '/');
    if (current === '/') return '/';
    const idx = current.lastIndexOf('/');
    return idx <= 0 ? '/' : current.slice(0, idx);
  }

  function fileManagerUrl(endpoint, params = {}) {
    const query = new URLSearchParams(params);
    return endpoint + '?' + query.toString();
  }

  function updateFileManagerRowSelection() {
    const selectedPath = fileManagerState.selected && fileManagerState.selected.path;
    document.querySelectorAll('#file_manager_entries tr[data-file-path]').forEach(row => {
      row.classList.toggle('file-manager-row-selected', !!selectedPath && row.dataset.filePath === selectedPath);
    });
  }

  function updateFileManagerSelectionBar() {
    const selectionEl = document.getElementById('file_manager_selection');
    const primaryBtn = document.getElementById('file_manager_primary_btn');
    const renameBtn = document.getElementById('file_manager_rename_btn');
    const deleteBtn = document.getElementById('file_manager_delete_btn');
    const selected = fileManagerState.selected;
    const hasSelection = !!(selected && selected.path);
    const isParent = !!(selected && selected.parent);
    const hasStorage = fileManagerState.sdAvailable !== false;

    if (selectionEl) {
      if (!hasSelection) {
        selectionEl.textContent = fileManagerText('Keine Auswahl', 'No selection');
      } else {
        const size = selected.dir ? '' : ' - ' + formatFileManagerSize(selected);
        selectionEl.textContent = fileManagerText('Auswahl: ', 'Selection: ') + (selected.name || selected.path) + size;
      }
    }

    if (primaryBtn) {
      primaryBtn.disabled = !hasSelection || !hasStorage;
      primaryBtn.textContent = selected && !selected.dir ? fileManagerText('Download', 'Download') : fileManagerText('\u00d6ffnen', 'Open');
      primaryBtn.title = primaryBtn.textContent;
    }
    if (renameBtn) renameBtn.disabled = !hasSelection || isParent || !hasStorage;
    if (deleteBtn) deleteBtn.disabled = !hasSelection || isParent || !hasStorage;
    updateFileManagerRowSelection();
  }

  function clearFileManagerSelection() {
    fileManagerState.selected = null;
    updateFileManagerSelectionBar();
  }

  function selectFileManagerEntry(entry) {
    if (!entry) {
      clearFileManagerSelection();
      return;
    }
    fileManagerState.selected = {
      path: normalizeFileManagerClientPath(entry.path || '/'),
      name: entry.name || entry.path || '',
      dir: !!entry.dir,
      size: Number(entry.size || 0),
      modified: Number(entry.modified || 0),
      parent: !!entry.parent
    };
    updateFileManagerSelectionBar();
  }

  function appendFileManagerCell(row, className, text) {
    const cell = document.createElement('td');
    if (className) cell.className = className;
    cell.textContent = text;
    row.appendChild(cell);
    return cell;
  }

  function fileManagerIconClass(entry) {
    if (entry && entry.parent) return 'mdi mdi-arrow-up-bold';
    return entry && entry.dir ? 'mdi mdi-folder-outline' : 'mdi mdi-file-outline';
  }

  function appendFileManagerNameCell(row, entry) {
    const cell = document.createElement('td');
    const nameWrap = document.createElement('div');
    nameWrap.className = 'file-manager-name';
    const icon = document.createElement('i');
    icon.className = 'file-manager-name-icon ' + fileManagerIconClass(entry);
    nameWrap.appendChild(icon);
    const label = entry.dir ? document.createElement('button') : document.createElement('span');
    label.className = entry.dir ? 'file-manager-name-link file-manager-folder-name' : '';
    label.textContent = entry.name || entry.path || '';
    if (entry.dir) {
      label.type = 'button';
      label.addEventListener('click', event => {
        event.stopPropagation();
        loadFileManager(entry.path);
      });
    }
    nameWrap.appendChild(label);
    cell.appendChild(nameWrap);
    row.appendChild(cell);
    return cell;
  }

  function renderFileManagerBreadcrumb(path) {
    const el = document.getElementById('file_manager_breadcrumb');
    if (!el) return;
    el.innerHTML = '';

    const current = normalizeFileManagerClientPath(path || '/');
    const rootBtn = document.createElement('button');
    rootBtn.type = 'button';
    rootBtn.className = 'file-manager-breadcrumb-item';
    rootBtn.innerHTML = '<i class="mdi mdi-home-outline"></i><span>root</span>';
    rootBtn.addEventListener('click', () => loadFileManager('/'));
    el.appendChild(rootBtn);

    if (current === '/') return;

    const parts = current.split('/').filter(Boolean);
    let acc = '';
    parts.forEach((part, index) => {
      acc += '/' + part;
      const sep = document.createElement('span');
      sep.className = 'file-manager-breadcrumb-separator';
      sep.textContent = '/';
      el.appendChild(sep);

      const item = document.createElement('button');
      item.type = 'button';
      item.className = 'file-manager-breadcrumb-item';
      item.textContent = part;
      const target = acc;
      item.disabled = index === parts.length - 1;
      item.addEventListener('click', () => loadFileManager(target));
      el.appendChild(item);
    });
  }

  function renderFileManagerEntries(entries, emptyMessage = null) {
    const tbody = document.getElementById('file_manager_entries');
    if (!tbody) return;
    tbody.innerHTML = '';
    renderFileManagerBreadcrumb(fileManagerState.path || '/');

    const addSelectableRow = (row, entry) => {
      row.dataset.filePath = normalizeFileManagerClientPath(entry.path || '/');
      row.tabIndex = 0;
      row.addEventListener('click', () => selectFileManagerEntry(entry));
      row.addEventListener('dblclick', () => {
        selectFileManagerEntry(entry);
        if (entry.dir) {
          loadFileManager(entry.path);
        } else {
          downloadFileManagerFile(entry.path);
        }
      });
      row.addEventListener('keydown', event => {
        if (event.key !== 'Enter') return;
        event.preventDefault();
        selectFileManagerEntry(entry);
        openSelectedFileManagerEntry();
      });
      row.title = entry.dir
        ? fileManagerText('Ausw\u00e4hlen, doppelklicken zum \u00d6ffnen', 'Select, double-click to open')
        : fileManagerText('Ausw\u00e4hlen, doppelklicken zum Download', 'Select, double-click to download');
    };

    const currentPath = normalizeFileManagerClientPath(fileManagerState.path || '/');
    if (currentPath !== '/') {
      const parentEntry = {
        dir: true,
        path: fileManagerParentOf(currentPath),
        name: '.. ' + fileManagerText('Elternordner', 'Parent Directory'),
        parent: true
      };
      const parentRow = document.createElement('tr');
      parentRow.className = 'file-manager-parent-row';
      addSelectableRow(parentRow, parentEntry);
      appendFileManagerNameCell(parentRow, parentEntry);
      appendFileManagerCell(parentRow, 'file-manager-muted', '-');
      appendFileManagerCell(parentRow, 'file-manager-size-cell', '-');
      tbody.appendChild(parentRow);
    }

    if (!Array.isArray(entries) || entries.length === 0) {
      const row = document.createElement('tr');
      const cell = document.createElement('td');
      cell.colSpan = 3;
      cell.textContent = emptyMessage || fileManagerText('Ordner ist leer.', 'Folder is empty.');
      row.appendChild(cell);
      tbody.appendChild(row);
      updateFileManagerRowSelection();
      return;
    }

    entries.forEach(entry => {
      const row = document.createElement('tr');
      addSelectableRow(row, entry);
      appendFileManagerNameCell(row, entry);

      appendFileManagerCell(row, 'file-manager-muted', formatFileManagerModified(entry));
      appendFileManagerCell(row, 'file-manager-size-cell', formatFileManagerSize(entry));

      tbody.appendChild(row);
    });
    updateFileManagerRowSelection();
  }

  async function loadFileManager(path = null) {
    fileManagerState.fs = 'sd';
    if (path !== null) fileManagerState.path = normalizeFileManagerClientPath(path);
    if (!fileManagerState.path) fileManagerState.path = '/';
    clearFileManagerSelection();
    setFileManagerStorageState(null);
    setFileManagerStatus(fileManagerText('Lade Dateien...', 'Loading files...'));

    try {
      const res = await fetch(fileManagerUrl('/api/files/list', {
        fs: fileManagerState.fs,
        path: fileManagerState.path
      }), { cache: 'no-store' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.success) {
        const loadError = new Error(data.error || fileManagerText('Dateiliste konnte nicht geladen werden.', 'Could not load file list.'));
        loadError.status = res.status;
        throw loadError;
      }
      fileManagerState.fs = 'sd';
      fileManagerState.path = data.path || fileManagerState.path;
      setFileManagerStorageState(true);
      renderFileManagerEntries(data.entries || []);
      const count = Array.isArray(data.entries) ? data.entries.length : 0;
      setFileManagerStatus(fileManagerText('Bereit. Eintr\u00e4ge: ', 'Ready. Entries: ') + count, true);
      fileManagerLoaded = true;
    } catch (err) {
      const rawMessage = err?.message || fileManagerText('Dateimanager-Fehler.', 'File manager error.');
      const sdMissing = err?.status === 503 || /microsd/i.test(rawMessage);
      const message = sdMissing
        ? fileManagerText('Keine microSD erkannt.', 'No microSD detected.')
        : rawMessage;
      setFileManagerStorageState(sdMissing ? false : true, sdMissing ? null : fileManagerText('microSD erkannt', 'microSD detected'));
      renderFileManagerEntries([], message);
      setFileManagerStatus(message, false);
      if (!sdMissing) showNotification(message, false);
    }
  }

  function downloadFileManagerFile(path) {
    window.location.href = fileManagerUrl('/api/files/download', {
      fs: fileManagerState.fs,
      path: path
    });
  }

  function openSelectedFileManagerEntry() {
    const entry = fileManagerState.selected;
    if (!entry || !entry.path) return;
    if (entry.dir) {
      loadFileManager(entry.path);
      return;
    }
    downloadFileManagerFile(entry.path);
  }

  function renameSelectedFileManagerEntry() {
    const entry = fileManagerState.selected;
    if (!entry || !entry.path || entry.parent) return;
    renameFileManagerEntry(entry.path, entry.name);
  }

  function deleteSelectedFileManagerEntry() {
    const entry = fileManagerState.selected;
    if (!entry || !entry.path || entry.parent) return;
    deleteFileManagerEntry(entry.path, entry.name, !!entry.dir);
  }

  async function postFileManagerForm(endpoint, fields) {
    const body = new URLSearchParams(fields);
    const res = await fetch(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok || !data.success) {
      throw new Error(data.error || fileManagerText('Aktion fehlgeschlagen.', 'Action failed.'));
    }
    return data;
  }

  async function createFileManagerFolder() {
    const name = prompt(fileManagerText('Name des neuen Ordners:', 'New folder name:'));
    if (name === null) return;
    const trimmed = String(name || '').trim();
    if (!trimmed) return;
    try {
      await postFileManagerForm('/api/files/mkdir', {
        fs: fileManagerState.fs,
        path: fileManagerState.path || '/',
        name: trimmed
      });
      showNotification(fileManagerText('Ordner erstellt.', 'Folder created.'));
      loadFileManager();
    } catch (err) {
      showNotification(err?.message || fileManagerText('Ordner konnte nicht erstellt werden.', 'Could not create folder.'), false);
    }
  }

  async function renameFileManagerEntry(path, currentName) {
    const name = prompt(fileManagerText('Neuer Name:', 'New name:'), currentName || '');
    if (name === null) return;
    const trimmed = String(name || '').trim();
    if (!trimmed || trimmed === currentName) return;
    try {
      await postFileManagerForm('/api/files/rename', {
        fs: fileManagerState.fs,
        path,
        name: trimmed
      });
      invalidateScreensaverEditor();
      showNotification(fileManagerText('Umbenannt.', 'Renamed.'));
      loadFileManager();
    } catch (err) {
      showNotification(err?.message || fileManagerText('Umbenennen fehlgeschlagen.', 'Rename failed.'), false);
    }
  }

  async function deleteFileManagerEntry(path, name, isDir) {
    const message = isDir
      ? fileManagerText('Ordner wirklich l\u00f6schen? Inhalt wird mit gel\u00f6scht: ', 'Delete folder and all contents: ')
      : fileManagerText('Datei wirklich l\u00f6schen: ', 'Delete file: ');
    if (!confirm(message + (name || path))) return;
    try {
      await postFileManagerForm('/api/files/delete', {
        fs: fileManagerState.fs,
        path
      });
      invalidateScreensaverEditor();
      showNotification(fileManagerText('Gel\u00f6scht.', 'Deleted.'));
      loadFileManager();
    } catch (err) {
      showNotification(err?.message || fileManagerText('L\u00f6schen fehlgeschlagen.', 'Delete failed.'), false);
    }
  }

  // Do not start parallel requests such as sensor polling while an upload is
  // running: the server handles one connection at a time, so extra connections
  // only queue up and strain the small internal buffer pool of the device.
  let fileManagerUploadBusy = false;

  // Sequential small chunks instead of one large POST: the device has little
  // internal RAM for WLAN receive buffers. A large upload lets the browser send
  // up to 64KB ahead unacknowledged, which reproducibly crashed the SDIO
  // receive path. With 16KB per request the amount in flight stays bounded.
  const FILE_MANAGER_UPLOAD_PART_SIZE = 16 * 1024;

  async function uploadFileManagerFile() {
    const input = document.getElementById('file_manager_upload');
    if (!input || !input.files || !input.files.length) {
      showNotification(fileManagerText('Bitte zuerst eine Datei ausw\u00e4hlen.', 'Select a file first.'), false);
      return;
    }
    if (fileManagerUploadBusy) return;
    const files = Array.from(input.files);
    fileManagerUploadBusy = true;
    try {
      for (let fileIndex = 0; fileIndex < files.length; fileIndex++) {
        const file = files[fileIndex];
        let offset = 0;
        let firstPart = true;
        while (offset < file.size || firstPart) {
          const end = Math.min(offset + FILE_MANAGER_UPLOAD_PART_SIZE, file.size);
          const formData = new FormData();
          formData.append('file', new File([file.slice(offset, end)], file.name));
          const pct = file.size ? Math.round((end / file.size) * 100) : 100;
          setFileManagerStatus(
            fileManagerText('Upload ', 'Upload ') + (fileIndex + 1) + '/' + files.length +
            ': ' + file.name + ' - ' + pct + '%');
          const res = await fetch(fileManagerUrl('/api/files/upload', {
            fs: fileManagerState.fs,
            path: fileManagerState.path || '/',
            append: firstPart ? '0' : '1'
          }), { method: 'POST', body: formData });
          const data = await res.json().catch(() => ({}));
          if (!res.ok || !data.success) {
            throw new Error(data.error || fileManagerText('Upload fehlgeschlagen.', 'Upload failed.'));
          }
          offset = end;
          firstPart = false;
        }
      }
      input.value = '';
      updateFileManagerUploadName(input);
      const completeMessage = files.length === 1
        ? fileManagerText('Upload abgeschlossen.', 'Upload complete.')
        : files.length + fileManagerText(' Dateien hochgeladen.', ' files uploaded.');
      setFileManagerStatus(completeMessage, true);
      showNotification(completeMessage);
      invalidateScreensaverEditor();
      loadFileManager();
    } catch (err) {
      const message = err?.message || fileManagerText('Upload fehlgeschlagen.', 'Upload failed.');
      setFileManagerStatus(message, false);
      showNotification(message, false);
    } finally {
      fileManagerUploadBusy = false;
    }
  }

  let githubFirmwareUpdateTag = '';

  function formatGithubFirmwareText(key, tag) {
    return String(t(key) || '').replace('%s', String(tag || ''));
  }

  function setGithubOtaUi(message, phase = 'idle', tone = '') {
    const statusEl = document.getElementById('ota_github_status');
    const progressEl = document.getElementById('ota_github_progress');
    const progressBarEl = document.getElementById('ota_github_progress_bar');
    if (statusEl) {
      statusEl.textContent = message || '';
      statusEl.classList.remove('error', 'success');
      if (tone === 'error' || tone === 'success') statusEl.classList.add(tone);
    }
    if (progressEl) {
      progressEl.classList.toggle('is-hidden', phase === 'idle');
      progressEl.classList.toggle('active', phase === 'busy');
    }
    if (progressBarEl) progressBarEl.style.width = phase === 'done' ? '100%' : '0%';
  }

  function setFirmwareOtaControlsDisabled(disabled) {
    const ids = ['ota_github_btn', 'ota_upload_btn', 'ota_choose_btn', 'ota_file'];
    ids.forEach(id => {
      const element = document.getElementById(id);
      if (element) element.disabled = disabled;
    });
  }

  function waitForGithubFirmwareResult(targetTag) {
    const startedAt = Date.now();
    const poll = async () => {
      try {
        const res = await fetch('/api/ota/github/status?ts=' + Date.now(), {
          method: 'GET',
          cache: 'no-store',
          credentials: 'same-origin'
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data.success) throw new Error('offline');
        if (data.install_error) {
          githubFirmwareUpdateTag = '';
          const button = document.getElementById('ota_github_btn');
          if (button) button.textContent = t('otaGithubCheck');
          setGithubOtaUi(data.install_error, 'idle', 'error');
          showNotification(data.install_error, false);
          setFirmwareOtaControlsDisabled(false);
          return;
        }
        if (!data.install_requested && data.current_version === targetTag) {
          window.location.reload();
          return;
        }
      } catch (e) {}

      if (Date.now() - startedAt < 180000) {
        window.setTimeout(poll, 1500);
      } else {
        setGithubOtaUi(t('otaReconnecting'), 'idle', 'error');
        setFirmwareOtaControlsDisabled(false);
      }
    };
    window.setTimeout(poll, 1200);
  }

  async function installGithubFirmware(tag) {
    setFirmwareOtaControlsDisabled(true);
    setGithubOtaUi(t('otaGithubDownloading'), 'busy');
    showNotification(t('otaGithubDownloading'));
    try {
      const res = await fetch('/api/ota/github/install', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'tag=' + encodeURIComponent(tag),
        cache: 'no-store',
        credentials: 'same-origin'
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.success) {
        throw new Error(data.error || t('otaFailed'));
      }
      setGithubOtaUi(t('otaGithubDownloading') + ' ' + t('otaReconnecting'), 'busy');
      waitForGithubFirmwareResult(tag);
    } catch (err) {
      const message = err?.message || t('otaFailed');
      setGithubOtaUi(message, 'idle', 'error');
      showNotification(message, false);
      setFirmwareOtaControlsDisabled(false);
    }
  }

  async function checkOrInstallGithubFirmware() {
    if (githubFirmwareUpdateTag) {
      await installGithubFirmware(githubFirmwareUpdateTag);
      return;
    }

    const button = document.getElementById('ota_github_btn');
    setFirmwareOtaControlsDisabled(true);
    if (button) button.textContent = t('otaGithubChecking');
    setGithubOtaUi(t('otaGithubChecking'));
    try {
      const res = await fetch('/api/ota/github/check', {
        method: 'POST',
        cache: 'no-store',
        credentials: 'same-origin'
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.success) {
        throw new Error(data.error || t('otaGithubCheckFailed'));
      }

      if (data.update_available && data.latest_tag) {
        githubFirmwareUpdateTag = data.latest_tag;
        if (button) button.textContent = formatGithubFirmwareText('otaGithubInstall', data.latest_tag);
        const message = formatGithubFirmwareText('otaGithubAvailable', data.latest_tag);
        setGithubOtaUi(message, 'idle', 'success');
        showNotification(message);
      } else {
        if (button) button.textContent = t('otaGithubCheck');
        setGithubOtaUi(t('otaGithubUpToDate'), 'idle', 'success');
        showNotification(t('otaGithubUpToDate'));
      }
    } catch (err) {
      if (button) button.textContent = t('otaGithubCheck');
      const message = err?.message || t('otaGithubCheckFailed');
      setGithubOtaUi(message, 'idle', 'error');
      showNotification(message, false);
    } finally {
      setFirmwareOtaControlsDisabled(false);
    }
  }

  async function uploadOtaFirmware() {
    const input = document.getElementById('ota_file');
    const button = document.getElementById('ota_upload_btn');
    const chooseBtn = document.getElementById('ota_choose_btn');
    const githubBtn = document.getElementById('ota_github_btn');
    const statusEl = document.getElementById('ota_status');
    const progressEl = document.getElementById('ota_progress');
    const progressBarEl = document.getElementById('ota_progress_bar');
    if (!input || !input.files || !input.files.length) {
      showNotification(t('otaSelectFile'), false);
      return;
    }

    const file = input.files[0];
    if (!file || !String(file.name || '').toLowerCase().endsWith('.bin')) {
      showNotification(t('otaSelectFile'), false);
      return;
    }

    const setOtaUi = (message, phase = 'idle', tone = '', percent = null) => {
      if (statusEl) {
        statusEl.textContent = message || '';
        statusEl.classList.remove('error', 'success');
        if (tone === 'error' || tone === 'success') statusEl.classList.add(tone);
      }
      if (progressEl) {
        progressEl.classList.toggle('is-hidden', phase === 'idle');
        progressEl.classList.toggle('active', phase === 'busy' && percent === null);
      }
      if (progressBarEl) {
        if (percent !== null) {
          const safePercent = Math.max(0, Math.min(100, percent));
          progressBarEl.style.width = safePercent + '%';
        } else if (phase === 'done') {
          progressBarEl.style.width = '100%';
        } else {
          progressBarEl.style.width = '0%';
        }
      }
    };

    const waitForDeviceReload = () => {
      const startedAt = Date.now();
      const tryReload = () => {
        fetch(window.location.pathname + '?ota_ping=' + Date.now(), {
          method: 'GET',
          cache: 'no-store',
          credentials: 'same-origin'
        })
        .then((res) => {
          if (!res.ok) throw new Error('offline');
          window.location.reload();
        })
        .catch(() => {
          if (Date.now() - startedAt < 120000) {
            window.setTimeout(tryReload, 1500);
          }
        });
      };
      window.setTimeout(tryReload, 2500);
    };

    if (button) {
      if (!button.dataset.defaultLabel) button.dataset.defaultLabel = button.textContent;
      button.disabled = true;
      button.textContent = button.dataset.defaultLabel || 'Update';
    }
    if (chooseBtn) chooseBtn.disabled = true;
    if (githubBtn) githubBtn.disabled = true;
    input.disabled = true;
    setOtaUi(t('otaUploading') + ' 0%', 'busy', '', 0);
    showNotification(t('otaUploading'));

    try {
      const otaSize = encodeURIComponent(String(file.size || 0));
      const otaFilename = encodeURIComponent(String(file.name || ''));
      const prepRes = await fetch('/api/ota/prepare?size=' + otaSize + '&filename=' + otaFilename, {
        method: 'POST',
        cache: 'no-store',
        credentials: 'same-origin'
      });
      const prepData = await prepRes.json().catch(() => ({}));
      if (!prepRes.ok || !prepData.success) {
        throw new Error(prepData.error || t('otaFailed'));
      }
      await new Promise(resolve => window.setTimeout(resolve, 250));
    } catch (err) {
      const message = err?.message || t('otaFailed');
      setOtaUi(message, 'idle', 'error');
      showNotification(message, false);
      if (button) {
        button.disabled = false;
        button.textContent = button.dataset.defaultLabel || 'Update';
      }
      if (chooseBtn) chooseBtn.disabled = false;
      if (githubBtn) githubBtn.disabled = false;
      input.disabled = false;
      return;
    }

    const xhr = new XMLHttpRequest();
    const otaSize = encodeURIComponent(String(file.size || 0));
    const otaFilename = encodeURIComponent(String(file.name || ''));
    xhr.open('POST', '/api/ota/upload/raw?size=' + otaSize + '&filename=' + otaFilename, true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.setRequestHeader('X-HomeTiles-OTA-Filename', otaFilename);

    xhr.upload.onprogress = (event) => {
      if (!event.lengthComputable) return;
      const percent = Math.max(1, Math.min(100, Math.round((event.loaded / event.total) * 100)));
      setOtaUi(t('otaUploading') + ' ' + percent + '%', 'busy', '', percent);
    };

    xhr.upload.onload = () => {
      setOtaUi(t('otaInstalling'), 'busy', '', null);
    };

    xhr.onload = () => {
      let data = {};
      try {
        data = JSON.parse(xhr.responseText || '{}');
      } catch (e) {}

      if (xhr.status < 200 || xhr.status >= 300 || !data.success) {
        const message = data.error || t('otaFailed');
        setOtaUi(message, 'idle', 'error');
        showNotification(message, false);
        if (button) {
          button.disabled = false;
          button.textContent = button.dataset.defaultLabel || 'Update';
        }
        if (chooseBtn) chooseBtn.disabled = false;
        if (githubBtn) githubBtn.disabled = false;
        input.disabled = false;
        return;
      }

      const message = t('otaSuccess');
      setOtaUi(message, 'done', 'success', 100);
      showNotification(message);
      if (statusEl) statusEl.textContent = message + ' ' + t('otaReconnecting');
      input.value = '';
      updateOtaFileName(input);
      waitForDeviceReload();
    };

    xhr.onerror = () => {
      const message = t('otaFailed');
      setOtaUi(message, 'idle', 'error');
      showNotification(message, false);
      if (button) {
        button.disabled = false;
        button.textContent = button.dataset.defaultLabel || 'Update';
      }
      if (chooseBtn) chooseBtn.disabled = false;
      if (githubBtn) githubBtn.disabled = false;
      input.disabled = false;
    };

    xhr.send(file);
  }

  // Tile Editor State
  const tileTabs = [];
  const folderByTab = {};
  const tabByFolder = {};
  const folderTabLoadPromises = {};
  const tileDataLoadedTabs = new Set();
  const tileDataLoadPromises = {};
  const sessionRestoredFolderTabs = new Set();
  let currentTileTab = '';
  let currentTileIndex = -1;
  let drafts = {};
  let tilesData = {};
  let autoSaveTimers = {};
  let saveRequestSeq = 0;
  let latestSaveRequestByTab = {};
  let saveInFlightByTile = {};
  let queuedSaveByTile = {};
  let sensorMetaCache = { values: {}, units: {}, icons: {}, names: {}, loaded: false };
  let sensorMetaFetchInFlight = null;
  let lastSensorMetaFetchMs = 0;
  let entityOptionsCache = null;
  let entityOptionsFetchInFlight = null;
  let lastEntityOptionsFetchMs = 0;
  const ENTITY_OPTIONS_CACHE_MS = 60000;
  const SELECTED_TILE_STORAGE_KEY = 'selectedAdminTile';
  let selectedTileByTab = {};

  function normalizeSensorMetaPayload(payload) {
    if (!payload || typeof payload !== 'object') {
      return { values: {}, units: {}, icons: {}, names: {}, loaded: false };
    }
    const hasMeta = Object.prototype.hasOwnProperty.call(payload, 'editable_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'units') ||
                    Object.prototype.hasOwnProperty.call(payload, 'icons') ||
                    Object.prototype.hasOwnProperty.call(payload, 'names') ||
                    Object.prototype.hasOwnProperty.call(payload, 'binary_sensor_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'energy_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'energy_units') ||
                    Object.prototype.hasOwnProperty.call(payload, 'climate_values');
    if (!hasMeta) {
      return { values: payload || {}, units: {}, icons: {}, names: {}, loaded: true };
    }
    return {
      values: Object.assign(
        {},
        payload.values || {},
        payload.binary_sensor_values || {},
        payload.energy_values || {},
        payload.climate_values || {}
      ),
      editableValues: payload.editable_values || payload.editableValues || {},
      units: Object.assign({}, payload.units || {}, payload.energy_units || {}),
      icons: payload.icons || {},
      names: payload.names || {},
      loaded: true
    };
  }

  function isSensorMetaCacheLoaded() {
    return !!(sensorMetaCache && sensorMetaCache.loaded);
  }

  function fetchSensorMetaCache(force = false) {
    const now = Date.now();
    if (sensorMetaFetchInFlight) return sensorMetaFetchInFlight;
    if (!force && sensorMetaCache.loaded && (now - lastSensorMetaFetchMs) < 15000) {
      return Promise.resolve(sensorMetaCache);
    }
    sensorMetaFetchInFlight = fetch('/api/sensor_values')
      .then(res => res.json())
      .then(raw => {
        sensorMetaCache = normalizeSensorMetaPayload(raw || {});
        lastSensorMetaFetchMs = Date.now();
        return sensorMetaCache;
      })
      .catch(() => sensorMetaCache)
      .finally(() => { sensorMetaFetchInFlight = null; });
    return sensorMetaFetchInFlight;
  }

  function fetchEntityOptions(force = false) {
    const now = Date.now();
    if (entityOptionsFetchInFlight) return entityOptionsFetchInFlight;
    if (!force && entityOptionsCache &&
        (now - lastEntityOptionsFetchMs) < ENTITY_OPTIONS_CACHE_MS) {
      return Promise.resolve(entityOptionsCache);
    }
    entityOptionsFetchInFlight = fetch('/api/entity_options')
      .then(res => {
        if (!res.ok) throw new Error('Entity options HTTP ' + res.status);
        return res.json();
      })
      .then(data => {
        if (!data || !data.success) throw new Error('Invalid entity options');
        entityOptionsCache = data;
        lastEntityOptionsFetchMs = Date.now();
        return data;
      })
      .finally(() => { entityOptionsFetchInFlight = null; });
    return entityOptionsFetchInFlight;
  }

  function isExplicitlyDisabledValue(raw) {
    if (raw === undefined || raw === null) return false;
    const text = String(raw);
    if (!text.length) return false;
    const trimmed = text.trim().toLowerCase();
    if (!trimmed.length) return true;
    return trimmed === '-' || trimmed === 'none' || trimmed === 'null' || trimmed === 'no' || trimmed === 'off';
  }

  // Mirrors appendHtmlEscaped() in src/web/server/web_admin_utils.cpp. The tile
  // previews are assembled as markup strings, so every tile title, unit, value
  // and icon name coming from a configuration or from Home Assistant has to be
  // escaped before it is inserted.
  function escapeHtml(value) {
    return String(value ?? '')
      .replaceAll('&', '&amp;')
      .replaceAll('<', '&lt;')
      .replaceAll('>', '&gt;')
      .replaceAll('"', '&quot;')
      .replaceAll("'", '&#39;');
  }

  function normalizeMdiIconName(raw) {
    let iconName = String(raw || '').trim().toLowerCase();
    if (iconName.startsWith('mdi:')) iconName = iconName.substring(4);
    else if (iconName.startsWith('mdi-')) iconName = iconName.substring(4);
    return iconName;
  }

  function resolveIconName(rawIcon, entityId, metaIcons) {
    if (isExplicitlyDisabledValue(rawIcon)) return '';
    const direct = normalizeMdiIconName(rawIcon);
    if (direct) return direct;
    if (entityId && metaIcons && metaIcons[entityId]) {
      return normalizeMdiIconName(metaIcons[entityId]);
    }
    return '';
  }

  function resolveUnitValue(rawUnit, entityId, metaUnits) {
    if (isExplicitlyDisabledValue(rawUnit)) return '';
    const direct = String(rawUnit || '').trim();
    if (direct.length) return rawUnit;
    if (entityId && metaUnits && metaUnits[entityId]) {
      return metaUnits[entityId];
    }
    return '';
  }

  function normalizeTileTitle(value) {
    const lines = String(value ?? '').replace(/\r\n?/g, '\n').replace(/\\n/g, '\n').split('\n');
    const text = lines.length > 2 ? lines[0] + '\n' + lines.slice(1).join(' ') : lines.join('\n');
    let bytes = 0, result = '';
    for (const character of text) {
      const code = character.codePointAt(0);
      bytes += code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
      if (bytes > 255) break;
      result += character;
    }
    return result;
  }

  function tileTitleHtml(value) {
    return '<span class="tile-title-lines">' + normalizeTileTitle(value).split('\n')
      .map(line => '<span class="tile-title-line">' + escapeHtml(line) + '</span>').join('') + '</span>';
  }

  function getTileTypeMeta(typeValue) {
    const key = String(typeValue ?? '0');
    return TILE_TYPE_REGISTRY[key] || TILE_TYPE_REGISTRY['0'] || {};
  }

  // Keeps the accessible name of a grid tile in step with its rendered content.
  // The server emits the same name, so a tile stays announced correctly whether
  // it came from the page load or from a live preview update.
  function applyTileAriaLabel(tileElem, title, typeValue) {
    if (!tileElem) return;
    const label = String(title ?? '').trim() ||
      String(getTileTypeMeta(typeValue).label || '').trim();
    if (label) tileElem.setAttribute('aria-label', label);
    else tileElem.removeAttribute('aria-label');
  }

  // Associates every plain <label> with the control it introduces. The field
  // markup is generated by fourteen independent type modules plus the lazily
  // injected folder fragments, and most of them emit
  // "<label>Text</label><input id=...>" without a for attribute, so clicking
  // the text does nothing and a screen reader announces the field unnamed.
  // Deriving the association from the same adjacency a sighted user reads keeps
  // it correct for every existing field and for every tile type added later,
  // instead of repeating the id in fourteen generators.
  const LABELABLE_CONTROLS = 'input:not([type="hidden"]), select, textarea';

  function associateFieldLabels(root = document) {
    if (!root || typeof root.querySelectorAll !== 'function') return;
    root.querySelectorAll('label:not([for])').forEach(label => {
      // A label that already wraps its control needs no attribute.
      if (label.querySelector(LABELABLE_CONTROLS)) return;
      const control = label.nextElementSibling;
      if (!control || !control.id) return;
      if (!control.matches(LABELABLE_CONTROLS)) return;
      // Do not steal a control that another label already points at.
      const owner = document.querySelector(
        'label[for="' + CSS.escape(control.id) + '"]');
      if (owner && owner !== label) return;
      label.htmlFor = control.id;
    });
  }

  function syncTileTypeSelectValue(selectEl, typeValue) {
    if (!selectEl) return;
    const key = String(typeValue ?? '0');
    Array.from(selectEl.querySelectorAll('option[data-locked-only="1"]')).forEach(opt => {
      if (opt.value !== key) opt.remove();
    });
    let option = Array.from(selectEl.options).find(opt => String(opt.value) === key);
    if (!option) {
      const meta = getTileTypeMeta(key);
      option = document.createElement('option');
      option.value = key;
      option.textContent = meta.label || key;
      option.disabled = !!meta.locked;
      option.dataset.lockedOnly = '1';
      selectEl.appendChild(option);
    }
    selectEl.value = key;
  }

  function callTypeHandler(meta, handlerKey, ...args) {
    if (!meta || !handlerKey) return;
    const fnName = meta[handlerKey];
    if (!fnName) return;
    const fn = window[fnName];
    if (typeof fn === 'function') return fn(...args);
  }

  function rebuildEntitySelect(id, entries) {
    const el = document.getElementById(id);
    if (!el || !Array.isArray(entries)) return;
    const keep = el.value || el.dataset.configuredValue || '';
    const placeholder = el.options.length ? el.options[0].cloneNode(true) : null;
    el.innerHTML = '';
    if (placeholder) el.appendChild(placeholder);
    for (const entry of entries) {
      if (!entry || entry.v === undefined) continue;
      const opt = document.createElement('option');
      opt.value = entry.v;
      opt.textContent = entry.t || entry.v;
      el.appendChild(opt);
    }
    el.value = keep;
    if (keep && el.value !== keep) {
      // The stored entity is missing from the current bridge list, for example
      // while the bridge is offline. Keep the selection instead of clearing it
      // on the next autosave.
      const opt = document.createElement('option');
      opt.value = keep;
      opt.textContent = keep;
      el.appendChild(opt);
      el.value = keep;
    }
    if (keep) el.dataset.configuredValue = keep;
  }

  // One firmware request serves every editor. The short cache avoids repeated
  // large JSON responses while switching quickly between tiles.
  function refreshEntityOptionLists(tab) {
    return fetchEntityOptions()
      .then(data => {
        rebuildEntitySelect(tab + '_sensor_entity', data.sensors);
        rebuildEntitySelect(tab + '_binary_sensor_entity', data.binary_sensors);
        rebuildEntitySelect(tab + '_number_entity', data.numbers);
        rebuildEntitySelect(tab + '_select_entity', data.selects);
        rebuildEntitySelect(tab + '_datetime_entity', data.datetimes);

        rebuildEntitySelect(tab + '_energy_entity', data.energy);
        rebuildEntitySelect(tab + '_weather_entity', data.weathers);
        rebuildEntitySelect(tab + '_switch_entity', data.switches);
        rebuildEntitySelect(tab + '_media_entity', data.media);
        rebuildEntitySelect(tab + '_climate_entity', data.climates);
        rebuildEntitySelect(tab + '_cover_entity', data.covers);
        rebuildEntitySelect(tab + '_camera_entity', data.cameras);
        rebuildEntitySelect(tab + '_scene_alias', data.scenes);
      })
      .catch(() => {});
  }

  function collectTypeFieldValues(tab) {
    const prefix = tab;
    const typeValue = document.getElementById(prefix + '_tile_type')?.value || '0';
    const meta = getTileTypeMeta(typeValue);
    if (!meta.save) return {};
    const fd = new FormData();
    callTypeHandler(meta, 'save', prefix, fd);
    const out = {};
    for (const [key, value] of fd.entries()) {
      out[key] = value;
    }
    return out;
  }

  function normalizeSnapshotLayout(snapshot, index, tab = currentTileTab) {
    const fallbackCol = (index >= 0) ? ((index % GRID_COLS) + 1) : 1;
    const firstRow = firstAllowedGridRow(tab);
    const fallbackRow = (index >= 0)
      ? (Math.max(firstRow, Math.floor(index / GRID_COLS)) + 1)
      : (firstRow + 1);
    let col = clampInt(snapshot?.col, 1, GRID_COLS, fallbackCol);
    let row = clampInt(snapshot?.row, firstRow + 1, GRID_ROWS, fallbackRow);
    let spanW = clampInt(snapshot?.span_w, 1, GRID_COLS, 1);
    let spanH = clampInt(snapshot?.span_h, 1, GRID_ROWS, 1);
    return constrainLayoutToTab(
      normalizeLayoutForTileType(snapshot?.type, col - 1, row - 1,
                                 spanW, spanH),
      tab);
  }

  function buildTileSnapshotFromInputs(tab) {
    const prefix = tab;
    const colorEl = document.getElementById(prefix + '_tile_color');
    const snapshot = {
      type: document.getElementById(prefix + '_tile_type')?.value || '0',
      title: document.getElementById(prefix + '_tile_title')?.value || '',
      icon: document.getElementById(prefix + '_tile_icon')?.value || '',
      color: colorEl?.value || '#2A2A2A',
      bg_color_default: tileColorInputIsDefault(tab) ? '1' : '0',
      col: document.getElementById(prefix + '_tile_col')?.value || '1',
      row: document.getElementById(prefix + '_tile_row')?.value || '1',
      span_w: document.getElementById(prefix + '_tile_span_w')?.value || '1',
      span_h: document.getElementById(prefix + '_tile_span_h')?.value || '1'
    };
    if (isScreensaverTileTab(tab)) {
      snapshot.background_opacity = document.getElementById('screensaver_tile_opacity')?.value || '0';
    }
    Object.assign(snapshot, collectTypeFieldValues(tab));
    return snapshot;
  }

  function getTileSnapshotForSave(tab, index) {
    const draft = drafts[tab] && drafts[tab][index];
    if (draft && draft._dirty) return Object.assign({}, draft);
    if (currentTileTab === tab && currentTileIndex === index) return buildTileSnapshotFromInputs(tab);
    return null;
  }

  function applySnapshotToTileData(tab, index, snapshot) {
    const tiles = getTilesData(tab);
    if (!Array.isArray(tiles) || index < 0) return;

    const prev = tiles[index] || {};
    const tile = Object.assign({}, prev);
    const layout = normalizeSnapshotLayout(snapshot, index, tab);
    const numericFields = ['type', 'sensor_decimals', 'sensor_value_font', 'sensor_display_mode', 'sensor_gauge_min', 'sensor_gauge_max', 'switch_style', 'navigate_target', 'popup_open_mode', 'key_code', 'key_modifier', 'background_opacity'];

    tile.type = clampInt(snapshot?.type, 0, 255, Number(prev.type) || 0);
    tile.title = snapshot?.title || '';
    tile.icon_name = snapshot?.icon || '';
    tile.bg_color = snapshotBgColorIsDefault(snapshot)
                        ? 0
                        : makeTileBgValue(hexToRgb(snapshot?.color || '#2A2A2A'));
    tile.col = layout.col;
    tile.row = layout.row;
    tile.span_w = layout.span_w;
    tile.span_h = layout.span_h;

    for (const [key, value] of Object.entries(snapshot || {})) {
      if (key === '_dirty' || key === '_rev' || key === 'icon' || key === 'color' || key === 'bg_color_default' || key === 'col' || key === 'row' || key === 'span_w' || key === 'span_h' || key === 'type' || key === 'title') continue;
      if (numericFields.includes(key)) {
        const num = Number(value);
        tile[key] = Number.isFinite(num) ? num : value;
      } else {
        tile[key] = value;
      }
    }

    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'switch_entity')) {
      tile.sensor_entity = snapshot.switch_entity || '';
    }
    for (const kind of ['number', 'select', 'datetime']) {
      if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, kind + '_entity')) tile.sensor_entity = snapshot[kind + '_entity'] || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'binary_sensor_entity')) {
      tile.sensor_entity = snapshot.binary_sensor_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'weather_entity')) {
      tile.sensor_entity = snapshot.weather_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'energy_entity')) {
      tile.sensor_entity = snapshot.energy_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'climate_entity')) {
      tile.sensor_entity = snapshot.climate_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'cover_entity')) {
      tile.sensor_entity = snapshot.cover_entity || '';
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'camera_entity')) {
      tile.sensor_entity = snapshot.camera_entity || '';
    }
    if (snapshot && (Object.prototype.hasOwnProperty.call(snapshot, 'clock_show_time') || Object.prototype.hasOwnProperty.call(snapshot, 'clock_show_date'))) {
      let flags = 0;
      if (String(snapshot.clock_show_time || '0') === '1') flags |= 1;
      if (String(snapshot.clock_show_date || '0') === '1') flags |= 2;
      if (flags === 0) flags = 1;
      tile.sensor_decimals = flags;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'clock_time_format')) {
      const num = Number(snapshot.clock_time_format);
      tile.sensor_gauge_min = Number.isFinite(num) ? num : 0;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'clock_date_format')) {
      const num = Number(snapshot.clock_date_format);
      tile.sensor_gauge_max = Number.isFinite(num) ? num : 0;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'animation_fit')) {
      const num = Number(snapshot.animation_fit);
      tile.sensor_display_mode = Number.isFinite(num) ? num : 0;
    }
    if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, 'animation_zoom')) {
      const num = Number(snapshot.animation_zoom);
      tile.sensor_gauge_max = Number.isFinite(num) ? num : 100;
    }

    tiles[index] = tile;
    tilesData[tab] = tiles;
  }

  function markLatestSaveRequest(tab, index, requestId) {
    if (!latestSaveRequestByTab[tab]) latestSaveRequestByTab[tab] = {};
    latestSaveRequestByTab[tab][index] = requestId;
  }

  function isLatestSaveRequest(tab, index, requestId) {
    return !!(latestSaveRequestByTab[tab] && latestSaveRequestByTab[tab][index] === requestId);
  }

  function getTileSaveKey(tab, index) {
    return tab + ':' + index;
  }

  function queueSaveAfterFlight(tab, index, silent = true) {
    const saveKey = getTileSaveKey(tab, index);
    const existing = queuedSaveByTile[saveKey];
    queuedSaveByTile[saveKey] = {
      silent: existing ? (existing.silent && silent) : silent
    };
  }

  function flushQueuedSave(tab, index) {
    const saveKey = getTileSaveKey(tab, index);
    if (saveInFlightByTile[saveKey]) return;
    const queued = queuedSaveByTile[saveKey];
    if (!queued) return;
    delete queuedSaveByTile[saveKey];
    saveTile(tab, queued.silent, index);
  }

  function getTileResizeHandlesHtml(typeValue) {
    if (String(typeValue || '0') === '0') return '';
    return '' +
      '<div class="tile-resize-handle tile-resize-handle-e" data-resize-dir="e"></div>' +
      '<div class="tile-resize-handle tile-resize-handle-s" data-resize-dir="s"></div>' +
      '<div class="tile-resize-handle tile-resize-handle-se" data-resize-dir="se"></div>';
  }

  function initTileTabs() {
    tileTabs.length = 0;
    Object.keys(folderByTab).forEach(k => delete folderByTab[k]);
    Object.keys(tabByFolder).forEach(k => delete tabByFolder[k]);
    // Navigation buttons describe every known folder. Tile bodies are loaded
    // independently, so a folder remains addressable before its editor exists.
    document.querySelectorAll('.folder-tab-btn[data-folder-id]').forEach(btn => {
      const folderId = parseInt(btn.dataset.folderId, 10);
      const tabId = btn.dataset.tabId ||
        (isNaN(folderId) ? '' : 'folder' + folderId);
      if (isNaN(folderId) || !tabId) return;
      folderByTab[tabId] = folderId;
      tabByFolder[folderId] = tabId;
    });
    document.querySelectorAll('.tile-tab').forEach(tabEl => {
      const tabId = tabEl.dataset.tabId || '';
      if (!tabId) return;
      tileTabs.push(tabId);
      const folderId = parseInt(tabEl.dataset.folderId, 10);
      if (!isNaN(folderId)) {
        folderByTab[tabId] = folderId;
        tabByFolder[folderId] = tabId;
      }
      if (!drafts[tabId]) drafts[tabId] = {};
      if (!tilesData[tabId]) tilesData[tabId] = [];
      if (!autoSaveTimers[tabId]) autoSaveTimers[tabId] = null;
    });
    if (!currentTileTab && tileTabs.length) currentTileTab = tileTabs[0];
  }

  function getFolderIdForTab(tab) {
    return folderByTab[tab];
  }

  function getTilesData(tab) {
    return tilesData[tab] || [];
  }
  function isScreensaverTileTab(tab) {
    return tab === 'screensaver';
  }
  function firstAllowedGridRow(tab) {
    return isScreensaverTileTab(tab) ? Math.max(0, GRID_ROWS - 2) : 0;
  }
  function restoreCurrentTileSelectionUi() {
    if (currentTileIndex === -1 || !currentTileTab) return;
    document.querySelectorAll('.tile').forEach(t => t.classList.remove('active'));
    const settingsId = currentTileTab + 'Settings';
    document.getElementById(settingsId)?.classList.remove('hidden');
    if (currentTileTab === 'folder0' &&
        currentTileIndex === HIDDEN_SETTINGS_TILE_INDEX) {
      const hiddenSettingsTile = document.getElementById('settingsHiddenTile');
      if (hiddenSettingsTile?.dataset.hidden === '1') {
        hiddenSettingsTile.classList.add('active');
      }
      return;
    }
    const activeTile = document.getElementById(currentTileTab + '-tile-' + currentTileIndex);
    if (activeTile) {
      activeTile.classList.add('active');
      window.requestAnimationFrame(() => {
        if (currentTileTab && currentTileIndex >= 0) {
          document.getElementById(currentTileTab + '-tile-' + currentTileIndex)?.classList.add('active');
        }
      });
    }
  }
  function ensureNavigateTargetOption(folderId, label) {
    const folderValue = String(folderId);
    document.querySelectorAll('select[id$="_navigate_target"]').forEach(select => {
      let opt = select.querySelector('option[value="' + folderValue + '"]');
      if (!opt) {
        opt = document.createElement('option');
        opt.value = folderValue;
        select.appendChild(opt);
      }
      opt.textContent = label;
    });
  }

  function syncFolderFragmentWithRoot(tabEl) {
    if (!tabEl) return;

    const sourceBorderToggle = Array.from(
      document.querySelectorAll('.normal-tile-border-toggle'))
      .find(toggle => !tabEl.contains(toggle));
    if (sourceBorderToggle) {
      tabEl.querySelectorAll('.normal-tile-border-toggle').forEach(toggle => {
        toggle.checked = sourceBorderToggle.checked;
      });
      tabEl.querySelectorAll('.tile-grid:not(.screensaver-tile-grid)')
        .forEach(grid => {
          grid.classList.toggle('tiles-bordered', sourceBorderToggle.checked);
        });
    }

    const folderOptions = Array.from(
      document.querySelectorAll('.folder-tab-btn[data-folder-id]'))
      .map(button => {
        const folderId = Number(button.dataset.folderId);
        if (!Number.isInteger(folderId) || folderId <= 0) return null;
        const buttonLabel = button.querySelector('span')?.textContent || '';
        return {
          value: String(folderId),
          label: String(button.dataset.folderName || buttonLabel ||
            formatFolderLabel('', folderId)).trim()
        };
      })
      .filter(Boolean);

    tabEl.querySelectorAll('select[id$="_navigate_target"]')
      .forEach(select => {
        const selectedValue = select.value;
        Array.from(select.options).forEach(option => {
          if (Number(option.value) > 0) option.remove();
        });
        folderOptions.forEach(folder => {
          const option = document.createElement('option');
          option.value = folder.value;
          option.textContent = folder.label;
          select.appendChild(option);
        });
        const selectedExists = Array.from(select.options)
          .some(option => option.value === selectedValue);
        if (selectedExists) select.value = selectedValue;
        else if (Array.from(select.options).some(option => option.value === '0')) {
          select.value = '0';
        }
      });
  }

  function installFolderTabFragment(
      folderNum, data, name = null, icon = null,
      bindInteractions = true) {
    const knownTabId = tabByFolder[folderNum] ||
      String(data?.tab_id || ('folder' + folderNum));
    if (document.getElementById('tab-tiles-' + knownTabId)) return true;

    const nav = document.querySelector('.tab-nav');
    const networkTab = document.getElementById('tab-network');
    if (!nav || !networkTab || !data?.tab_id || !data?.tab_html) return false;

    let buttonEl = nav.querySelector(
      '.folder-tab-btn[data-folder-id="' + folderNum + '"]');
    if (!buttonEl) {
      const buttonTpl = document.createElement('template');
      buttonTpl.innerHTML = String(data.button_html || '').trim();
      buttonEl = buttonTpl.content.firstElementChild;
      if (!buttonEl) return false;
      const navButtons = Array.from(nav.querySelectorAll('.tab-btn'));
      const fixedBtn = navButtons.find(
        btn => btn.dataset.tabTarget === 'tab-tiles-screensaver') ||
        navButtons.find(btn => btn.dataset.tabTarget === 'tab-network');
      if (fixedBtn) nav.insertBefore(buttonEl, fixedBtn);
      else nav.appendChild(buttonEl);
    }

    const expectedTabId = String(
      buttonEl.dataset.tabId || knownTabId || ('folder' + folderNum));
    if (String(data.tab_id) !== expectedTabId) return false;

    const tabTpl = document.createElement('template');
    tabTpl.innerHTML = String(data.tab_html || '').trim();
    const tabEl = tabTpl.content.firstElementChild;
    if (!tabEl) return false;
    if (tabEl.id !== 'tab-tiles-' + expectedTabId ||
        String(tabEl.dataset.tabId || '') !== expectedTabId ||
        Number(tabEl.dataset.folderId) !== folderNum) return false;
    if (buttonEl.dataset.folderParent !== undefined) {
      tabEl.dataset.folderParent = buttonEl.dataset.folderParent;
    }
    if (buttonEl.dataset.folderName !== undefined) {
      tabEl.dataset.folderName = buttonEl.dataset.folderName;
    }
    if (buttonEl.dataset.folderIcon !== undefined) {
      tabEl.dataset.folderIcon = buttonEl.dataset.folderIcon;
    }
    const screensaverTab = document.getElementById('tab-tiles-screensaver');
    networkTab.parentNode.insertBefore(tabEl, screensaverTab || networkTab);
    syncFolderFragmentWithRoot(tabEl);
    associateFieldLabels(tabEl);

    initTileTabs();
    if (bindInteractions) {
      enableTileDrag(String(data.tab_id));
      enableTileKeys(String(data.tab_id));
      enableTileResize(String(data.tab_id));
    }
    if (name !== null || icon !== null) {
      ensureNavigateTargetOption(
        folderNum, formatFolderLabel(name, folderNum));
      updateFolderTabUi(folderNum, name || '', icon || '');
    }
    return true;
  }

  async function ensureFolderTabUi(folderId, name = null, icon = null) {
    const folderNum = parseInt(folderId, 10);
    if (isNaN(folderNum) || folderNum <= 0) return false;
    const knownTabId = tabByFolder[folderNum] || ('folder' + folderNum);
    if (document.getElementById('tab-tiles-' + knownTabId)) {
      if (name !== null || icon !== null) {
        updateFolderTabUi(folderNum, name || '', icon || '');
        ensureNavigateTargetOption(
          folderNum, formatFolderLabel(name, folderNum));
      }
      return true;
    }
    if (folderTabLoadPromises[folderNum]) {
      return folderTabLoadPromises[folderNum];
    }

    const cachedFragment = readFolderTabSessionFragment(folderNum);
    if (cachedFragment) {
      const installed = installFolderTabFragment(
        folderNum, cachedFragment, name, icon, true);
      if (installed) {
        sessionRestoredFolderTabs.add(String(cachedFragment.tab_id));
        return true;
      }
      forgetFolderTabSessionFragment(folderNum);
    }

    folderTabLoadPromises[folderNum] = (async () => {
      const res = await fetch(
        '/api/folders/tab?folder_id=' + encodeURIComponent(folderNum));
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.success || !data.tab_id || !data.tab_html) {
        return false;
      }
      const installed = installFolderTabFragment(
        folderNum, data, name, icon, true);
      if (installed) {
        window.setTimeout(() => rememberFolderTabSessionFragment(data), 0);
      }
      return installed;
    })();

    try {
      return await folderTabLoadPromises[folderNum];
    } catch (error) {
      console.error('Folder tab load failed:', error);
      return false;
    } finally {
      delete folderTabLoadPromises[folderNum];
    }
  }

  const FOLDER_TAB_SESSION_CACHE_PREFIX = 'hometilesAdminFolderTabs:';
  const FOLDER_TAB_SESSION_CACHE_VERSION = 2;
  const FOLDER_TAB_SESSION_CACHE_LIMIT = 4;

  function folderTabSessionCacheNamespace() {
    return FOLDER_TAB_SESSION_CACHE_PREFIX + 'v' +
      FOLDER_TAB_SESSION_CACHE_VERSION + ':' +
      String(ADMIN_WEB_SESSION_TOKEN) + ':' + APP_LOCALE + ':' +
      GRID_COLS + 'x' + GRID_ROWS;
  }

  function folderTabSessionIndexKey() {
    return folderTabSessionCacheNamespace() + ':index';
  }

  function folderTabSessionEntryKey(folderId) {
    return folderTabSessionCacheNamespace() + ':folder:' + Number(folderId);
  }

  function readFolderTabSessionIndex() {
    try {
      const raw = sessionStorage.getItem(folderTabSessionIndexKey());
      if (!raw) return [];
      const parsed = JSON.parse(raw);
      if (parsed?.version !== FOLDER_TAB_SESSION_CACHE_VERSION ||
          !Array.isArray(parsed.entries)) return [];
      return parsed.entries.slice(0, FOLDER_TAB_SESSION_CACHE_LIMIT);
    } catch (error) {
      return [];
    }
  }

  function writeFolderTabSessionIndex(entries) {
    try {
      sessionStorage.setItem(folderTabSessionIndexKey(), JSON.stringify({
        version: FOLDER_TAB_SESSION_CACHE_VERSION,
        entries: entries.slice(0, FOLDER_TAB_SESSION_CACHE_LIMIT)
      }));
    } catch (error) {}
  }

  function rememberFolderTabSessionFragment(data) {
    const folderId = Number(data?.folder_id);
    if (!Number.isInteger(folderId) || folderId <= 0 ||
        !data?.tab_id || !data?.tab_html) return;
    const entry = {
      folder_id: folderId,
      tab_id: String(data.tab_id)
    };
    const entries = readFolderTabSessionIndex()
      .filter(item => Number(item?.folder_id) !== folderId);
    let stored = false;
    while (!stored) {
      try {
        sessionStorage.setItem(
          folderTabSessionEntryKey(folderId), String(data.tab_html));
        stored = true;
      } catch (error) {
        const evicted = entries.pop();
        if (!evicted) break;
        try {
          sessionStorage.removeItem(
            folderTabSessionEntryKey(evicted.folder_id));
        } catch (removeError) {}
      }
    }
    if (!stored) return;

    const nextEntries = [entry, ...entries];
    while (nextEntries.length > FOLDER_TAB_SESSION_CACHE_LIMIT) {
      const evicted = nextEntries.pop();
      try {
        sessionStorage.removeItem(
          folderTabSessionEntryKey(evicted.folder_id));
      } catch (error) {}
    }
    writeFolderTabSessionIndex(nextEntries);
  }

  function touchFolderTabSessionCache(folderId) {
    const folderNum = Number(folderId);
    const entries = readFolderTabSessionIndex();
    const index = entries.findIndex(
      item => Number(item?.folder_id) === folderNum);
    if (index <= 0) return;
    const [entry] = entries.splice(index, 1);
    writeFolderTabSessionIndex([entry, ...entries]);
  }

  function forgetFolderTabSessionFragment(folderId) {
    const folderNum = Number(folderId);
    try {
      sessionStorage.removeItem(folderTabSessionEntryKey(folderNum));
    } catch (error) {}
    writeFolderTabSessionIndex(readFolderTabSessionIndex().filter(
      entry => Number(entry?.folder_id) !== folderNum));
  }

  function readFolderTabSessionFragment(folderId) {
    const folderNum = Number(folderId);
    const entry = readFolderTabSessionIndex().find(
      item => Number(item?.folder_id) === folderNum);
    if (!entry) return null;
    const expectedTabId = String(
      tabByFolder[folderNum] || ('folder' + folderNum));
    if (String(entry.tab_id || '') !== expectedTabId) {
      forgetFolderTabSessionFragment(folderNum);
      return null;
    }
    try {
      const tabHtml = sessionStorage.getItem(
        folderTabSessionEntryKey(folderNum));
      if (!tabHtml) {
        forgetFolderTabSessionFragment(folderNum);
        return null;
      }
      return {
        folder_id: folderNum,
        tab_id: expectedTabId,
        tab_html: tabHtml
      };
    } catch (error) {
      return null;
    }
  }

  function prepareFolderTabSessionCache() {
    const namespace = folderTabSessionCacheNamespace();
    const storageKeys = [];
    try {
      for (let index = 0; index < sessionStorage.length; index += 1) {
        const key = sessionStorage.key(index) || '';
        storageKeys.push(key);
      }
      storageKeys.filter(key =>
        key.startsWith(FOLDER_TAB_SESSION_CACHE_PREFIX) &&
        !key.startsWith(namespace + ':'))
        .forEach(key => sessionStorage.removeItem(key));
    } catch (error) {}

    const availableEntryKeys = new Set(storageKeys.filter(key =>
      key.startsWith(namespace + ':folder:')));
    const validFolders = new Map();
    document.querySelectorAll('.folder-tab-btn[data-folder-id]')
      .forEach(button => {
        const folderId = Number(button.dataset.folderId);
        if (!Number.isInteger(folderId) || folderId <= 0) return;
        validFolders.set(folderId, String(
          button.dataset.tabId || ('folder' + folderId)));
      });

    const seenFolderIds = new Set();
    const validEntries = readFolderTabSessionIndex().filter(entry => {
      const folderId = Number(entry?.folder_id);
      const valid = Number.isInteger(folderId) && folderId > 0 &&
        !seenFolderIds.has(folderId) &&
        validFolders.get(folderId) === String(entry?.tab_id || '') &&
        availableEntryKeys.has(folderTabSessionEntryKey(folderId));
      if (valid) seenFolderIds.add(folderId);
      return valid;
    });
    const validEntryKeys = new Set(validEntries.map(entry =>
      folderTabSessionEntryKey(entry.folder_id)));
    availableEntryKeys.forEach(key => {
      if (!validEntryKeys.has(key)) {
        try { sessionStorage.removeItem(key); } catch (error) {}
      }
    });
    writeFolderTabSessionIndex(validEntries);
  }

  function restoreInitialFolderTabSessionFragment(initialTab) {
    const folderId = folderIdFromAdminTabName(initialTab);
    if (folderId === null) return false;
    const entry = readFolderTabSessionFragment(folderId);
    if (!entry) return false;
    if (!installFolderTabFragment(folderId, entry, null, null, false)) {
      forgetFolderTabSessionFragment(folderId);
      return false;
    }
    sessionRestoredFolderTabs.add(String(entry.tab_id));
    return true;
  }
  function persistSelectedTileState() {
    try {
      if (currentTileTab && currentTileIndex >= 0) {
        selectedTileByTab[currentTileTab] = currentTileIndex;
        localStorage.setItem(SELECTED_TILE_STORAGE_KEY, JSON.stringify(selectedTileByTab));
      } else {
        localStorage.removeItem(SELECTED_TILE_STORAGE_KEY);
      }
    } catch (e) {}
  }
  function loadSelectedTileStates() {
    try {
      const raw = localStorage.getItem(SELECTED_TILE_STORAGE_KEY);
      if (!raw) return;
      const saved = JSON.parse(raw);
      // Migration from the previous { tab, index } format to a per-tab selection.
      if (saved && typeof saved.tab === 'string') {
        const index = Number(saved.index);
        if (Number.isInteger(index) && index >= 0 && index < TILES_PER_GRID) {
          selectedTileByTab[saved.tab] = index;
        }
        return;
      }
      if (!saved || typeof saved !== 'object') return;
      Object.entries(saved).forEach(([tab, rawIndex]) => {
        const index = Number(rawIndex);
        if (Number.isInteger(index) && index >= 0 && index < TILES_PER_GRID) {
          selectedTileByTab[tab] = index;
        }
      });
    } catch (e) {
      selectedTileByTab = {};
    }
  }
  function getRememberedTileIndex(tab) {
    const markedTile = document.querySelector('#tab-tiles-' + tab + ' .tile[data-selected="1"]');
    if (markedTile && Number(markedTile.dataset.type || 0) !== 0) {
      const markedIndex = Number(markedTile.dataset.index);
      if (Number.isInteger(markedIndex) && markedIndex >= 0 && markedIndex < TILES_PER_GRID) {
        return markedIndex;
      }
    }
    const index = Number(selectedTileByTab[tab]);
    if (!Number.isInteger(index) || index < 0 || index >= TILES_PER_GRID) return null;
    const tile = document.getElementById(tab + '-tile-' + index);
    if (!tile || Number(tile.dataset.type || 0) === 0) {
      delete selectedTileByTab[tab];
      return null;
    }
    return index;
  }
  function restoreSelectedTileState(tab) {
    const targetTab = tab || currentTileTab;
    const index = targetTab ? getRememberedTileIndex(targetTab) : null;
    if (index === null) return false;
    selectTile(index, targetTab);
    return true;
  }
  function switchToFolderId(folderId) {
    const numericId = Number(folderId);
    if (!Number.isInteger(numericId) || numericId < 0) return;
    const targetTab = tabByFolder[numericId] || ('folder' + numericId);
    switchTab('tab-tiles-' + targetTab);
  }
  function openPreviewNavigation(tileEl, tab) {
    if (!tileEl) return;
    const type = Number(tileEl.dataset.type);
    if (type === 7) {
      switchTab('tab-network');
      return;
    }
    if (type === 8) {
      const folderTab = document.getElementById('tab-tiles-' + tab);
      const parentId = Number(folderTab?.dataset.folderParent);
      switchToFolderId(parentId);
      return;
    }
    if (type !== 4) return;
    const index = Number(tileEl.dataset.index);
    const tile = Number.isInteger(index) ? getTilesData(tab)[index] : null;
    const targetId = Number(tile?.navigate_target ?? tileEl.dataset.navigateTarget);
    switchToFolderId(targetId);
  }
  function clampInt(value, min, max, fallback) {
    const v = parseInt(value, 10);
    if (isNaN(v)) return fallback !== undefined ? fallback : min;
    if (v < min) return min;
    if (v > max) return max;
    return v;
  }

  function normalizeLayoutForTileType(typeValue, col, row, spanW, spanH) {
    let safeCol = clampInt(col, 0, GRID_COLS - 1, 0);
    let safeRow = clampInt(row, 0, GRID_ROWS - 1, 0);
    let safeW = clampInt(spanW, 1, GRID_COLS, 1);
    let safeH = clampInt(spanH, 1, GRID_ROWS, 1);
    if (Number(typeValue) === MEDIA_TILE_TYPE) {
      const minW = Math.min(MEDIA_TILE_MIN_SPAN, GRID_COLS);
      const minH = Math.min(MEDIA_TILE_MIN_SPAN, GRID_ROWS);
      safeW = clampInt(safeW, minW, Math.min(MEDIA_TILE_MAX_SPAN, GRID_COLS), minW);
      safeH = clampInt(safeH, minH, Math.min(MEDIA_TILE_MAX_SPAN, GRID_ROWS), minH);
      safeCol = Math.min(safeCol, GRID_COLS - safeW);
      safeRow = Math.min(safeRow, GRID_ROWS - safeH);
    } else {
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
    const col = clampInt(tile?.col, 0, GRID_COLS - 1, fallbackCol);
    const row = clampInt(tile?.row, firstRow, GRID_ROWS - 1, fallbackRow);
    let spanW = clampInt(tile?.span_w, 1, GRID_COLS, 1);
    let spanH = clampInt(tile?.span_h, 1, GRID_ROWS, 1);
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
  }

  function getTileElementLayout(tab, index) {
    const el = document.getElementById(tab + '-tile-' + index);
    if (!el) return null;
    const col = clampInt(el.dataset.col, 0, GRID_COLS - 1, null);
    const row = clampInt(el.dataset.row, firstAllowedGridRow(tab), GRID_ROWS - 1, null);
    const spanW = clampInt(el.dataset.spanW, 1, GRID_COLS, null);
    const spanH = clampInt(el.dataset.spanH, 1, GRID_ROWS, null);
    if (col === null || row === null || spanW === null || spanH === null) return null;
    return { col, row, span_w: spanW, span_h: spanH };
  }

  function layoutTiles(tab, tiles) {
    if (!Array.isArray(tiles)) return;
    const occupied = Array.from({ length: GRID_ROWS }, () => Array(GRID_COLS).fill(false));
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
      for (let r = layout.row; r < layout.row + layout.span_h; r++) {
        for (let c = layout.col; c < layout.col + layout.span_w; c++) {
          if (r < GRID_ROWS && c < GRID_COLS) occupied[r][c] = true;
        }
      }
    });

    const freeCells = [];
    for (let r = firstAllowedGridRow(tab); r < GRID_ROWS; r++) {
      for (let c = 0; c < GRID_COLS; c++) {
        if (!occupied[r][c]) freeCells.push({ col: c, row: r });
      }
    }

    emptyIndices.forEach((idx, i) => {
      const el = document.getElementById(tab + '-tile-' + idx);
      if (!el) return;
      if (i < freeCells.length) {
        const cell = freeCells[i];
        setTileGridPosition(el, cell.col, cell.row, 1, 1);
        el.style.display = '';
      } else {
        el.style.display = 'none';
      }
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

    let col = clampInt(colEl.value, 1, GRID_COLS, 1);
    const firstRow = firstAllowedGridRow(tab);
    let row = clampInt(rowEl.value, firstRow + 1, GRID_ROWS, firstRow + 1);
    let spanW = clampInt(spanWEl.value, 1, GRID_COLS, 1);
    let spanH = clampInt(spanHEl.value, 1, GRID_ROWS, 1);

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
    const tiles = getTilesData(tab);
    const tileEl = document.getElementById(tab + '-tile-' + currentTileIndex);
    if (tileEl && (!Array.isArray(tiles) || tiles.length === 0)) {
      setTileGridPosition(tileEl, layout.col, layout.row, layout.span_w, layout.span_h);
      return;
    }
    if (!Array.isArray(tiles) || currentTileIndex >= tiles.length) return;
    const tile = tiles[currentTileIndex] || {};
    tile.col = layout.col;
    tile.row = layout.row;
    tile.span_w = layout.span_w;
    tile.span_h = layout.span_h;
    const typeEl = document.getElementById(tab + '_tile_type');
    const typeNum = typeEl ? parseInt(typeEl.value, 10) : 0;
    tile.type = isNaN(typeNum) ? 0 : typeNum;
    tiles[currentTileIndex] = tile;
    layoutTiles(tab, tiles);
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

  function persistDrafts() { try { localStorage.setItem('tileDrafts', JSON.stringify(drafts)); } catch (e) {} }
  function loadDraftsFromStorage() {
    try {
      const raw = localStorage.getItem('tileDrafts');
      if (raw) {
        drafts = JSON.parse(raw);
        // Drafts must not overwrite saved values after a page refresh.
        for (const tab in drafts) {
          const tabDrafts = drafts[tab];
          if (!tabDrafts) continue;
          Object.keys(tabDrafts).forEach(key => {
            if (tabDrafts[key]) tabDrafts[key]._dirty = false;
          });
        }
      }
    } catch (e) {
      drafts = {};
    }
  }
  function clearDraft(tab, index) {
    if (drafts[tab] && drafts[tab][index]) {
      delete drafts[tab][index];
      persistDrafts();
    }
  }

  function updateDraft(tab) {
    if (currentTileIndex === -1) return;
    if (!drafts[tab]) drafts[tab] = {};
    const prefix = tab;
    const prevDraft = drafts[tab][currentTileIndex];
    const colorEl = document.getElementById(prefix + '_tile_color');
    const d = {
      type: document.getElementById(prefix + '_tile_type')?.value || '0',
      title: document.getElementById(prefix + '_tile_title')?.value || '',
      icon: document.getElementById(prefix + '_tile_icon')?.value || '',
      color: colorEl?.value || '#2A2A2A',
      bg_color_default: tileColorInputIsDefault(tab) ? '1' : '0',
      col: document.getElementById(prefix + '_tile_col')?.value || '1',
      row: document.getElementById(prefix + '_tile_row')?.value || '1',
      span_w: document.getElementById(prefix + '_tile_span_w')?.value || '1',
      span_h: document.getElementById(prefix + '_tile_span_h')?.value || '1'
    };
    if (currentTileIndex === HIDDEN_SETTINGS_TILE_INDEX) d.type = '7';
    if (isScreensaverTileTab(tab)) {
      d.background_opacity = document.getElementById('screensaver_tile_opacity')?.value || '0';
    }
    Object.assign(d, collectTypeFieldValues(tab));
    d._dirty = true;
    d._rev = (prevDraft && prevDraft._rev) ? (prevDraft._rev + 1) : 1;
    drafts[tab][currentTileIndex] = d;
    applySnapshotToTileData(tab, currentTileIndex, d);
    persistDrafts();
  }

  function applyDraft(tab, index) {
    const d = drafts[tab] && drafts[tab][index];
    if (!d || !d._dirty) return false;
    const prefix = tab;
    syncTileTypeSelectValue(document.getElementById(prefix + '_tile_type'), d.type || '0');
    resetAllTypeFields(tab);
    updateTileType(tab);
    document.getElementById(prefix + '_tile_title').value = d.title || '';
    document.getElementById(prefix + '_tile_icon').value = d.icon || '';
    setTileColorInputFromSnapshot(tab, d);
    if (isScreensaverTileTab(tab)) {
      const opacity = document.getElementById('screensaver_tile_opacity');
      if (opacity) opacity.value = String(d.background_opacity ?? 0);
    }
    const colEl = document.getElementById(prefix + '_tile_col');
    if (colEl) colEl.value = d.col || '1';
    const rowEl = document.getElementById(prefix + '_tile_row');
    if (rowEl) rowEl.value = d.row || '1';
    const spanWEl = document.getElementById(prefix + '_tile_span_w');
    if (spanWEl) spanWEl.value = d.span_w || '1';
    const spanHEl = document.getElementById(prefix + '_tile_span_h');
    if (spanHEl) spanHEl.value = d.span_h || '1';
    const meta = getTileTypeMeta(d.type || '0');
    callTypeHandler(meta, 'load', prefix, d);
    refreshEntityOptionLists(prefix);
    syncGaugeUi(tab);
    updateTilePreview(tab);
    return true;
  }

  let tileClipboard = null;
  function persistTileClipboard() { try { localStorage.setItem('tileClipboard', JSON.stringify(tileClipboard)); } catch (e) {} }
  function loadTileClipboard() {
    try {
      const raw = localStorage.getItem('tileClipboard');
      if (raw) tileClipboard = JSON.parse(raw);
    } catch (e) {
      tileClipboard = null;
    }
  }

  function collectTileFormData(tab) {
    const prefix = tab;
    const colorEl = document.getElementById(prefix + '_tile_color');
    const data = {
      type: document.getElementById(prefix + '_tile_type')?.value || '0',
      title: document.getElementById(prefix + '_tile_title')?.value || '',
      icon: document.getElementById(prefix + '_tile_icon')?.value || '',
      color: colorEl?.value || '#2A2A2A',
      bg_color_default: tileColorInputIsDefault(tab) ? '1' : '0',
      span_w: document.getElementById(prefix + '_tile_span_w')?.value || '1',
      span_h: document.getElementById(prefix + '_tile_span_h')?.value || '1'
    };
    if (isScreensaverTileTab(tab)) {
      data.background_opacity = document.getElementById('screensaver_tile_opacity')?.value || '0';
    }
    Object.assign(data, collectTypeFieldValues(tab));
    return data;
  }

  function applyTileFormData(tab, data) {
    if (!data) return;
    const prefix = tab;
    const typeValue = data.type || '0';
    const typeEl = document.getElementById(prefix + '_tile_type');
    syncTileTypeSelectValue(typeEl, typeValue);
    resetAllTypeFields(tab);
    updateTileType(tab);

    const titleEl = document.getElementById(prefix + '_tile_title');
    if (titleEl) titleEl.value = data.title || '';
    const iconEl = document.getElementById(prefix + '_tile_icon');
    if (iconEl) iconEl.value = data.icon || '';
    setTileColorInputFromSnapshot(tab, data);
    if (isScreensaverTileTab(tab)) {
      const opacity = document.getElementById('screensaver_tile_opacity');
      if (opacity) opacity.value = String(data.background_opacity ?? 0);
    }
    const spanWEl = document.getElementById(prefix + '_tile_span_w');
    if (spanWEl) spanWEl.value = data.span_w || '1';
    const spanHEl = document.getElementById(prefix + '_tile_span_h');
    if (spanHEl) spanHEl.value = data.span_h || '1';
    const meta = getTileTypeMeta(typeValue);
    callTypeHandler(meta, 'load', prefix, data);
    refreshEntityOptionLists(prefix);
    syncGaugeUi(tab);
  }

  function copyTile(tab) {
    if (currentTileIndex === -1 || currentTileTab !== tab) {
      showNotification(t('selectTileFirst'), false);
      return;
    }
    tileClipboard = collectTileFormData(tab);
    persistTileClipboard();
    showNotification(t('tileCopied'));
  }

  function pasteTile(tab) {
    if (currentTileIndex === -1 || currentTileTab !== tab) {
      showNotification(t('selectTileFirst'), false);
      return;
    }
    if (!tileClipboard) {
      showNotification(t('noCopiedTile'), false);
      return;
    }
    applyTileFormData(tab, tileClipboard);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
    showNotification(t('tilePasted'));
  }

  function selectTile(index, tab) {
    if (currentTileTab &&
        typeof parkClimateMiniEditor === 'function') {
      parkClimateMiniEditor(currentTileTab);
    }
    currentTileIndex = index;
    currentTileTab = tab;
    document.getElementById('settingsHiddenTile')?.classList.remove('active');
    persistSelectedTileState();
    document.querySelectorAll(
      '#tab-tiles-' + tab + ' .tile-grid > .tile')
      .forEach(t => delete t.dataset.selected);
    document.querySelectorAll(
      '.tile-grid > .tile, .screensaver-tile-grid > .tile')
      .forEach(t =>
        t.classList.remove(
          'active', 'drop-target', 'dragging'));
    const tileId = tab + '-tile-' + index;
    const selectedTile = document.getElementById(tileId);
    if (selectedTile) {
      selectedTile.dataset.selected = '1';
      selectedTile.classList.add('active');
    }
    const settingsId = tab + 'Settings';
    const settingsPanel = document.getElementById(settingsId);
    if (settingsPanel) {
      if (isScreensaverTileTab(tab)) {
        screensaverSelected = { kind: 'tile', index };
        document.getElementById('screensaverBackgroundSettings')?.classList.add('hidden');
        document.getElementById('screensaverClockSettings')?.classList.add('hidden');
        document.getElementById('screensaverGrid')?.classList.remove('selected-background');
        document.getElementById('screensaverClock')?.classList.remove('selected-clock');
      }
      const tileSpecific = settingsPanel.querySelector('.tile-specific-settings');
      if (tileSpecific) {
        tileSpecific.classList.remove('hidden');
      }
    }
    // Bind the live handlers right away. This used to happen only after the
    // asynchronous GET of the tile data, and until then size, position and style
    // visibly did not react in the screensaver preview.
    setupLivePreview(tab);
    loadTileData(index, tab);
  }

  function selectHiddenSettingsTile() {
    const hiddenTile = document.getElementById('settingsHiddenTile');
    if (!hiddenTile || hiddenTile.dataset.hidden !== '1') return;
    if (currentTileTab && typeof parkClimateMiniEditor === 'function') {
      parkClimateMiniEditor(currentTileTab);
    }
    currentTileIndex = -2;
    currentTileTab = 'folder0';
    document.querySelectorAll('.tile-grid > .tile').forEach(tile => {
      tile.classList.remove('active');
      delete tile.dataset.selected;
    });
    hiddenTile.classList.add('active');
    const panel = document.getElementById('folder0Settings');
    const specific = panel?.querySelector('.tile-specific-settings');
    if (specific) {
      specific.classList.remove('hidden');
    }
    const snapshot = normalizeHiddenSettingsSnapshot();
    if (!applyDraft('folder0', HIDDEN_SETTINGS_TILE_INDEX)) {
      applyTileFormData('folder0', snapshot);
      const col = document.getElementById('folder0_tile_col');
      const row = document.getElementById('folder0_tile_row');
      if (col) col.value = snapshot.col;
      if (row) row.value = snapshot.row;
      updateTileType('folder0');
      updateTilePreview('folder0');
    }
    setupLivePreview('folder0');
    toggleSettingsAccessFields();
    const settingsBody = specific?.querySelector('.tile-settings-body');
    if (settingsBody) settingsBody.scrollTop = 0;
    updateTileSettingsMaxHeight();
  }

  function titleFromOption(option) {
    if (!option) return '';
    // The first option is the translated "No selection" placeholder.  It is
    // not an entity name and must never become a persisted tile title.
    if (!String(option.value || '').trim().length) return '';
    const label = String(option.textContent || option.innerText || '').trim();
    if (!label.length) return '';
    const sep = label.indexOf(' - ');
    if (sep > 0) return label.substring(0, sep).trim();
    return label;
  }

  function titleFromEntity(entity) {
    let name = String(entity || '').trim();
    if (!name.length) return '';
    const dot = name.indexOf('.');
    if (dot !== -1) name = name.substring(dot + 1);
    name = name.replace(/[_-]+/g, ' ').trim();
    if (!name.length) return '';
    return name.replace(/\b\w/g, (m) => m.toUpperCase());
  }

  function maybeFillTitleFromEntity(tab, selectSuffix) {
    const prefix = tab;
    const titleInput = document.getElementById(prefix + '_tile_title');
    const selectEl = document.getElementById(prefix + selectSuffix);
    if (!titleInput || !selectEl) return;
    if (titleInput.value && titleInput.value.trim().length) return;
    const opt = selectEl.selectedOptions && selectEl.selectedOptions[0];
    let title = titleFromOption(opt);
    if (!title.length) title = titleFromEntity(selectEl.value);
    if (title.length) titleInput.value = title;
  }

  function setupLivePreview(tab) {
    const prefix = tab;
    const bindLive = (el, eventName, key, handler) => {
      if (!el) return;
      // Settings panels can be restored/replaced from the folder-tab HTML
      // cache.  A serialized data-live-bound flag can survive that operation,
      // while the JavaScript listener itself cannot.  Keep the actual handler
      // on the live DOM node and always replace it deterministically.
      const slot = '__homeTilesLive_' + key + '_' + eventName;
      if (typeof el[slot] === 'function') {
        el.removeEventListener(eventName, el[slot]);
      }
      el.addEventListener(eventName, handler);
      el[slot] = handler;
    };

    const titleInput = document.getElementById(prefix + '_tile_title');
    const iconInput = document.getElementById(prefix + '_tile_icon');
    const colorInput = document.getElementById(prefix + '_tile_color');
    const colInput = document.getElementById(prefix + '_tile_col');
    const rowInput = document.getElementById(prefix + '_tile_row');
    const spanWInput = document.getElementById(prefix + '_tile_span_w');
    const spanHInput = document.getElementById(prefix + '_tile_span_h');
    const typeSelect = document.getElementById(prefix + '_tile_type');
    const opacityInput = isScreensaverTileTab(tab)
      ? document.getElementById('screensaver_tile_opacity') : null;
    const entitySelect = document.getElementById(prefix + '_sensor_entity');
    const binarySensorSelect = document.getElementById(
      prefix + '_binary_sensor_entity');
    const binarySensorPopupModeSelect = document.getElementById(
      prefix + '_binary_sensor_popup_open_mode');
      const unitInput = document.getElementById(prefix + '_sensor_unit');
      const decimalsInput = document.getElementById(prefix + '_sensor_decimals');
      const valueFontSelect = document.getElementById(prefix + '_sensor_value_font');
      const sensorPopupModeSelect = document.getElementById(prefix + '_sensor_popup_open_mode');
      const displayModeSelect = document.getElementById(prefix + '_sensor_display_mode');
      const gaugeMinInput = document.getElementById(prefix + '_sensor_gauge_min');
      const gaugeMaxInput = document.getElementById(prefix + '_sensor_gauge_max');
      const gaugeArcInput = document.getElementById(prefix + '_sensor_gauge_arc');
      const gaugeSizeInput = document.getElementById(prefix + '_sensor_gauge_size');
      const gaugeYOffsetInput = document.getElementById(prefix + '_sensor_gauge_y_offset');
      const valueYOffsetInput = document.getElementById(prefix + '_sensor_value_y_offset');
      const graphHeightInput = document.getElementById(prefix + '_sensor_graph_height');
      const weatherSelect = document.getElementById(prefix + '_weather_entity');
      const weatherPopupModeSelect = document.getElementById(prefix + '_weather_popup_open_mode');
      const energySelect = document.getElementById(prefix + '_energy_entity');
      const energyUnitInput = document.getElementById(prefix + '_energy_unit');
      const energyDecimalsInput = document.getElementById(prefix + '_energy_decimals');
      const energyValueFontSelect = document.getElementById(prefix + '_energy_value_font');
      const energyPopupModeSelect = document.getElementById(prefix + '_energy_popup_open_mode');
      const energyValueYOffsetInput = document.getElementById(prefix + '_energy_value_y_offset');
      const sceneInput = document.getElementById(prefix + '_scene_alias');
    const textInput = document.getElementById(prefix + '_text_value');
    const textFontInput = document.getElementById(prefix + '_text_value_font');
    const navigateSelect = document.getElementById(prefix + '_navigate_target');
    const folderPinToggle = document.getElementById(prefix + '_folder_pin_enabled');
    const folderPinApply = document.getElementById(prefix + '_folder_pin_apply');
    const switchSelect = document.getElementById(prefix + '_switch_entity');
    const switchStyleSelect = document.getElementById(prefix + '_switch_style');
    const switchPopupModeSelect = document.getElementById(prefix + '_switch_popup_open_mode');
    const mediaSelect = document.getElementById(prefix + '_media_entity');
    const climateSelect = document.getElementById(prefix + '_climate_entity');
    const coverSelect = document.getElementById(prefix + '_cover_entity');
    const coverPopupModeSelect = document.getElementById(prefix + '_cover_popup_open_mode');
    const cameraSelect = document.getElementById(prefix + '_camera_entity');
    const climatePopupModeSelect = document.getElementById(prefix + '_climate_popup_open_mode');
    const climateSlotSelects = Array.from(
      { length: 6 },
      (_, index) => document.getElementById(
        prefix + '_climate_slot_' + index));
    const climateLayoutSelects = Array.from(
      { length: 6 },
      (_, index) => document.getElementById(
        prefix + '_climate_layout_' + index));
    const animationSelect = document.getElementById(prefix + '_animation_file');
    const animationFpsInput = document.getElementById(prefix + '_animation_fps');
    const animationFitSelect = document.getElementById(prefix + '_animation_fit');
    const animationZoomInput = document.getElementById(prefix + '_animation_zoom');
    const clockTimeCheck = document.getElementById(prefix + '_clock_show_time');
    const clockDateCheck = document.getElementById(prefix + '_clock_show_date');
    const clockTimeFontSelect = document.getElementById(prefix + '_clock_time_font');
    const clockDateFontSelect = document.getElementById(prefix + '_clock_date_font');
    const clockTimeFormatSelect = document.getElementById(prefix + '_clock_time_format');
    const clockDateFormatSelect = document.getElementById(prefix + '_clock_date_format');
    const settingsPanel = document.getElementById(prefix + 'Settings');

    bindLive(titleInput, 'input', 'tileTitle', () => {
      const normalized = normalizeTileTitle(titleInput.value);
      if (normalized !== titleInput.value) titleInput.value = normalized;
      updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab);
    });
    bindLive(iconInput, 'input', 'tileIcon', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(colorInput, 'input', 'tileColor', () => { markTileColorInputExplicit(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(opacityInput, 'input', 'tileOpacity', () => { updateTilePreview(tab); updateDraft(tab); });
    bindLive(opacityInput, 'change', 'tileOpacitySave', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(colInput, 'input', 'tileCol', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(rowInput, 'input', 'tileRow', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(spanWInput, 'input', 'tileSpanW', () => { syncClimateSlotFields(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(spanHInput, 'input', 'tileSpanH', () => { syncClimateSlotFields(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(typeSelect, 'change', 'tileType', () => {
      const tileEl = document.getElementById(tab + '-tile-' + currentTileIndex);
      const previousType = Number(tileEl?.dataset.type ?? 0);
      const nextType = Number(typeSelect.value);
      // A freshly created tile must start with the selected type's real
      // default colour. Do not inherit an explicit colour state from the empty
      // editor placeholder.
      if (previousType === 0 && nextType !== 0) {
        const nextMeta = getTileTypeMeta(typeSelect.value);
        setTileColorInputFromStored(
          tab, 0, nextMeta.defaultBg || '#2A2A2A');
      }
      if (isScreensaverTileTab(tab) && previousType === 0 &&
          nextType !== 0 && opacityInput) {
        opacityInput.value = String(SCREENSAVER_TILE_DEFAULT_OPACITY);
      }
      updateTileType(tab);
      normalizeLayoutInputs(tab);
      updateLayoutFromInputs(tab);
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(entitySelect, 'change', 'sensorEntity', () => { maybeFillTitleFromSensor(tab); updateTilePreview(tab); updateSensorValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    for (const kind of ['number', 'select', 'datetime']) {
      const select = document.getElementById(prefix + '_' + kind + '_entity');
      bindLive(select, 'change', kind + 'Entity', () => {
        if (select.value) select.dataset.configuredValue = select.value;
        else delete select.dataset.configuredValue;
        maybeFillTitleFromEntity(tab, '_' + kind + '_entity');
        updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab);
      });
      bindLive(document.getElementById(prefix + '_' + kind + '_value_font'), 'change', kind + 'ValueFont', () => {
        updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab);
      });
      bindLive(document.getElementById(prefix + '_' + kind + '_popup_open_mode'), 'change', kind + 'PopupMode', () => {
        updateDraft(tab); scheduleAutoSave(tab);
      });
    }
    bindLive(binarySensorSelect, 'change', 'binarySensorEntity', () => {
      if (binarySensorSelect.value) {
        binarySensorSelect.dataset.configuredValue = binarySensorSelect.value;
      } else {
        delete binarySensorSelect.dataset.configuredValue;
      }
      maybeFillTitleFromEntity(tab, '_binary_sensor_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(binarySensorPopupModeSelect, 'change', 'binarySensorPopupMode', () => {
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(weatherSelect, 'change', 'weatherEntity', () => { maybeFillTitleFromWeather(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(weatherPopupModeSelect, 'change', 'weatherPopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energySelect, 'change', 'energyEntity', () => {
      energySelect.dataset.configuredValue = energySelect.value || '';
      maybeFillTitleFromEnergy(tab);
      updateTilePreview(tab);
      updateEnergyValuePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(energyUnitInput, 'input', 'energyUnit', () => { updateEnergyValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energyDecimalsInput, 'input', 'energyDecimals', () => { updateEnergyValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energyValueFontSelect, 'change', 'energyValueFont', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energyPopupModeSelect, 'change', 'energyPopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energyValueYOffsetInput, 'input', 'energyValueYOffset', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(unitInput, 'input', 'sensorUnit', () => { updateSensorValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(decimalsInput, 'input', 'sensorDecimals', () => { updateSensorValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(valueFontSelect, 'change', 'sensorValueFont', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(sensorPopupModeSelect, 'change', 'sensorPopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(displayModeSelect, 'change', 'sensorDisplayMode', () => { syncGaugeUi(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(gaugeMinInput, 'input', 'sensorGaugeMin', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(gaugeMaxInput, 'input', 'sensorGaugeMax', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(gaugeArcInput, 'input', 'sensorGaugeArc', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(gaugeSizeInput, 'input', 'sensorGaugeSize', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(gaugeYOffsetInput, 'input', 'sensorGaugeYOffset', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(valueYOffsetInput, 'input', 'sensorValueYOffset', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(graphHeightInput, 'input', 'sensorGraphHeight', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(sceneInput, 'input', 'sceneAlias', () => { maybeFillTitleFromScene(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(textInput, 'input', 'textValue', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(textFontInput, 'change', 'textFont', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(navigateSelect, 'change', 'navigateTarget', () => {
      syncFolderPinControls(tab);
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(folderPinToggle, 'change', 'folderPinToggle', () => {
      const tile = tilesData?.[tab]?.[currentTileIndex];
      if (!folderPinToggle.checked && tile?.folder_pin_enabled === true) {
        syncFolderPinControls(tab);
        applyFolderPin(tab);
      } else {
        syncFolderPinControls(tab);
      }
    });
    bindLive(folderPinApply, 'click', 'folderPinApply', () => {
      applyFolderPin(tab);
    });
    bindLive(switchSelect, 'change', 'switchEntity', () => { maybeFillTitleFromSwitch(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(switchStyleSelect, 'change', 'switchStyle', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(switchPopupModeSelect, 'change', 'switchPopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(mediaSelect, 'change', 'mediaEntity', () => { maybeFillTitleFromMedia(tab); updateTilePreview(tab); updateMediaValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(climateSelect, 'change', 'climateEntity', () => {
      if (climateSelect.value) {
        climateSelect.dataset.configuredValue = climateSelect.value;
      } else {
        delete climateSelect.dataset.configuredValue;
      }
      maybeFillTitleFromEntity(tab, '_climate_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(climatePopupModeSelect, 'change', 'climatePopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(coverSelect, 'change', 'coverEntity', () => {
      if (coverSelect.value) {
        coverSelect.dataset.configuredValue = coverSelect.value;
      } else {
        delete coverSelect.dataset.configuredValue;
      }
      maybeFillTitleFromEntity(tab, '_cover_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(coverPopupModeSelect, 'change', 'coverPopupMode', () => {
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(cameraSelect, 'change', 'cameraEntity', () => {
      if (cameraSelect.value) {
        cameraSelect.dataset.configuredValue = cameraSelect.value;
      } else {
        delete cameraSelect.dataset.configuredValue;
      }
      maybeFillTitleFromEntity(tab, '_camera_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    climateSlotSelects.forEach((select, index) => {
      bindLive(select, 'change', 'climateSlot' + index, () => {
        syncClimateSlotFields(tab);
        updateTilePreview(tab);
        updateDraft(tab);
        scheduleAutoSave(tab);
      });
    });
    climateLayoutSelects.forEach((select, index) => {
      bindLive(select, 'change', 'climateLayout' + index, () => {
        updateTilePreview(tab);
        updateDraft(tab);
        scheduleAutoSave(tab);
      });
    });
    bindLive(animationSelect, 'change', 'animationFile', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(animationFpsInput, 'input', 'animationFps', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(animationFitSelect, 'change', 'animationFit', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(animationZoomInput, 'input', 'animationZoom', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(clockTimeCheck, 'change', 'clockShowTime', () => {
      ensureClockSelection(prefix);
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(clockDateCheck, 'change', 'clockShowDate', () => {
      ensureClockSelection(prefix);
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    if (clockTimeFontSelect) {
      const onClockTimeFontChanged = () => { updateClockValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); };
      bindLive(clockTimeFontSelect, 'change', 'clockTimeFont', onClockTimeFontChanged);
      bindLive(clockTimeFontSelect, 'input', 'clockTimeFont', onClockTimeFontChanged);
    }
    if (clockDateFontSelect) {
      const onClockDateFontChanged = () => { updateClockValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); };
      bindLive(clockDateFontSelect, 'change', 'clockDateFont', onClockDateFontChanged);
      bindLive(clockDateFontSelect, 'input', 'clockDateFont', onClockDateFontChanged);
    }
    if (clockTimeFormatSelect) {
      const onClockTimeFormatChanged = () => { updateClockValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); };
      bindLive(clockTimeFormatSelect, 'change', 'clockTimeFormat', onClockTimeFormatChanged);
      bindLive(clockTimeFormatSelect, 'input', 'clockTimeFormat', onClockTimeFormatChanged);
    }
    if (clockDateFormatSelect) {
      const onClockDateFormatChanged = () => { updateClockValuePreview(tab); updateDraft(tab); scheduleAutoSave(tab); };
      bindLive(clockDateFormatSelect, 'change', 'clockDateFormat', onClockDateFormatChanged);
      bindLive(clockDateFormatSelect, 'input', 'clockDateFormat', onClockDateFormatChanged);
    }
    if (settingsPanel && settingsPanel.dataset.clockLiveBound !== '1') {
      const delegatedClockRefresh = (e) => {
        const target = e && e.target;
        if (!target || !target.id) return;
        if (
          target.id === (prefix + '_clock_show_time') ||
          target.id === (prefix + '_clock_show_date')
        ) {
          ensureClockSelection(prefix);
          updateTilePreview(tab);
          updateDraft(tab);
          scheduleAutoSave(tab);
          return;
        }
        if (
          target.id === (prefix + '_clock_time_font') ||
          target.id === (prefix + '_clock_date_font') ||
          target.id === (prefix + '_clock_time_format') ||
          target.id === (prefix + '_clock_date_format')
        ) {
          updateClockValuePreview(tab);
          updateDraft(tab);
          scheduleAutoSave(tab);
          return;
        }
      };
      settingsPanel.addEventListener('change', delegatedClockRefresh);
      settingsPanel.addEventListener('input', delegatedClockRefresh);
      settingsPanel.dataset.clockLiveBound = '1';
    }
  }

  function updateTilePreview(tab) {
    if (currentTileIndex === -1) return;
    if (currentTileIndex === HIDDEN_SETTINGS_TILE_INDEX) {
      const snapshot = buildTileSnapshotFromInputs(tab);
      snapshot.type = '7';
      renderSettingsHiddenSlot(true, snapshot);
      return;
    }
    if (typeof parkClimateMiniEditor === 'function') {
      // A live change rebuilds the preview content. The selection of the mini
      // tile being edited has to survive that render cycle.
      parkClimateMiniEditor(tab, true);
    }
    const prefix = tab;
    const tileId = tab + '-tile-' + currentTileIndex;
    const tileElem = document.getElementById(tileId);
    if (!tileElem) return;

    const wasActive = currentTileTab === tab && currentTileIndex >= 0;
    const typeWas = tileElem.dataset.type || '0';
    const title = document.getElementById(prefix + '_tile_title').value;
    const color = document.getElementById(prefix + '_tile_color').value;
    const type = document.getElementById(prefix + '_tile_type').value;
    const meta = getTileTypeMeta(type);
    const iconInput = document.getElementById(prefix + '_tile_icon');
    const switchStyle = document.getElementById(prefix + '_switch_style')?.value || '0';
    const isEnergyType = type === '14';
    const sensorValueFont = isEnergyType
      ? (document.getElementById(prefix + '_energy_value_font')?.value || '0')
      : (document.getElementById(prefix + '_sensor_value_font')?.value || '0');
    const previewKind = meta.preview || 'none';
    const sensorValueClass = getSensorValueFontClass(isEditablePreview(previewKind)
      ? (document.getElementById(prefix + '_' + previewKind + '_value_font')?.value ?? '2') : sensorValueFont);
    const sensorEntity = document.getElementById(prefix + '_sensor_entity')?.value || '';
    const binarySensorEntity = document.getElementById(
      prefix + '_binary_sensor_entity')?.value || '';
    const energyEntity = document.getElementById(prefix + '_energy_entity')?.value || '';
    const weatherEntity = document.getElementById(prefix + '_weather_entity')?.value || '';
    const switchEntity = document.getElementById(prefix + '_switch_entity')?.value || '';
    const mediaEntity = document.getElementById(prefix + '_media_entity')?.value || '';
    const climateEntity = document.getElementById(prefix + '_climate_entity')?.value || '';
    const coverEntity = document.getElementById(prefix + '_cover_entity')?.value || '';
    const cameraEntity = document.getElementById(prefix + '_camera_entity')?.value || '';
    let iconEntity = (previewKind === 'sensor')
      ? (isEnergyType ? energyEntity : sensorEntity)
      : (previewKind === 'binary_sensor'
        ? binarySensorEntity
      : (previewKind === 'switch'
        ? switchEntity
        : (previewKind === 'weather'
          ? weatherEntity
          : (previewKind === 'media'
            ? mediaEntity
            : (previewKind === 'climate'
              ? climateEntity
              : (previewKind === 'cover'
                ? coverEntity
                : (previewKind === 'camera' ? cameraEntity : '')))))));
    if (isEditablePreview(previewKind)) iconEntity = document.getElementById(prefix + '_' + previewKind + '_entity')?.value || '';
    const rawIcon = iconInput ? iconInput.value : '';
    let iconName = resolveIconName(
      rawIcon,
      iconEntity,
      sensorMetaCache.icons);
    if (previewKind === 'camera' && !iconName &&
        !isExplicitlyDisabledValue(rawIcon)) {
      iconName = 'video';
    }
    let climatePreviewState = null;
    if (previewKind === 'climate') {
      climatePreviewState = parseClimatePreviewPayload(
        climateEntity ? (sensorMetaCache.values[climateEntity] ?? '') : '');
      if (!normalizeMdiIconName(rawIcon) &&
          !isExplicitlyDisabledValue(rawIcon)) {
        iconName = climatePreviewIcon(climatePreviewState, iconName);
      }
    }
    let coverPreviewState = null;
    if (previewKind === 'cover') {
      coverPreviewState = parseCoverPreviewPayload(
        coverEntity ? (sensorMetaCache.values[coverEntity] ?? '') : '');
      if (!normalizeMdiIconName(rawIcon) &&
          !isExplicitlyDisabledValue(rawIcon)) {
        iconName = coverPreviewIcon(coverPreviewState, iconName);
      }
    }
    let binarySensorPreviewState = null;
    if (previewKind === 'binary_sensor') {
      binarySensorPreviewState = parseBinarySensorPreviewPayload(
        binarySensorEntity
          ? (sensorMetaCache.values[binarySensorEntity] ?? '') : '');
      iconName = resolveBinarySensorPreviewIcon(
        rawIcon, binarySensorEntity, binarySensorPreviewState,
        sensorMetaCache.icons);
    }

    tileElem.className = 'tile';
    if (meta.css) tileElem.classList.add(meta.css);
    if (type === '5' && switchStyle === '1') tileElem.classList.add('switch-toggle');
    tileElem.style.background = '';
    tileElem.dataset.type = type;

    if (type === '0') {
      tileElem.classList.add('empty');
      tileElem.style.background = 'transparent';
      tileElem.innerHTML = '';
      applyTileAriaLabel(tileElem, '', type);
      if (wasActive) tileElem.classList.add('active');
      updateLayoutFromInputs(tab);
      return;
    }

    const defaultBg = meta.defaultBg || '#353535';
    if (tileColorInputIsDefault(tab)) {
      const colorInput = document.getElementById(prefix + '_tile_color');
      if (colorInput) colorInput.value = defaultBg;
    }
    const tileBg = tileColorInputIsDefault(tab) ? defaultBg : (color || defaultBg);
    if (isScreensaverTileTab(tab)) {
      const opacity = clampInt(
        document.getElementById('screensaver_tile_opacity')?.value,
        0, 255, 0);
      tileElem.style.background = tileBg + opacity.toString(16).padStart(2, '0');
    } else {
      tileElem.style.background = tileBg;
    }
    tileElem.style.removeProperty('--switch-knob-color');
    tileElem.style.removeProperty('--switch-on-color');
    if (type === '5' && switchStyle === '1') {
      tileElem.style.setProperty('--switch-knob-color', tileBg);
      tileElem.style.setProperty('--switch-on-color', '#3B82F6');
    }

    let html = '';

    if (iconName) {
      const iconStyle = previewKind === 'climate'
        ? ' style="color:' + climatePreviewColor(climatePreviewState) + '"'
        : (previewKind === 'cover'
          ? ' style="color:' + coverPreviewColor(coverPreviewState) + '"'
          : (previewKind === 'binary_sensor'
            ? ' style="color:' + binarySensorPreviewColor(
                binarySensorPreviewState) + '"'
            : ''));
      html += '<i class="mdi mdi-' + escapeHtml(iconName) + ' tile-icon"' + iconStyle + '></i>';
    }

    let displayTitle = title;
    if (previewKind === 'camera' && !displayTitle && cameraEntity) {
      displayTitle = sensorMetaCache.names[cameraEntity] ||
        titleFromEntity(cameraEntity);
    }
    if (displayTitle) {
      html += '<div class="tile-title" id="' + tileId + '-title">' +
        tileTitleHtml(displayTitle) + '</div>';
    }
    applyTileAriaLabel(tileElem, displayTitle, type);

    if (previewKind === 'weather') {
      html += '<div class="tile-ghost-icon"><i class="mdi mdi-weather-partly-cloudy"></i></div>';
    }
    if (previewKind === 'media') {
      html += '<div class="tile-ghost-icon"><i class="mdi mdi-music"></i></div>';
    }
    if (previewKind === 'climate') {
      const climateSpanW = document.getElementById(
        prefix + '_tile_span_w')?.value || 1;
      const climateSpanH = document.getElementById(
        prefix + '_tile_span_h')?.value || 1;
      html += climatePreviewSlots(
        climatePreviewState, climateSpanW, climateSpanH,
        currentClimateSlotConfig(tab),
        currentClimateTargetLayouts(tab),
        currentClimateGeometry(tab));
    }
    if (previewKind === 'cover') {
      const value = coverPreviewState?.position !== null &&
                    coverPreviewState?.position !== undefined
        ? String(coverPreviewState.position) + '%' : '--%';
      html += '<div class="tile-value tile-cover-value">' +
        escapeHtml(coverPreviewStateText(coverPreviewState)) +
        '<br>' + escapeHtml(value) + '</div>';
    }
    if (previewKind === 'binary_sensor') {
      html += '<div class="tile-value tile-binary-sensor-value" id="' +
        tileId + '-value">' +
        escapeHtml(binarySensorPreviewStateText(binarySensorPreviewState)) +
        '</div>';
    }

    if (isEditablePreview(previewKind)) html += '<div class="tile-value tile-editable-value ' + sensorValueClass + '">' + escapeHtml(editablePreviewText(iconEntity, previewKind)) + '</div>';

    if (previewKind === 'sensor') {
      const entitySelect = document.getElementById(prefix + (isEnergyType ? '_energy_entity' : '_sensor_entity'));
      const unitInput = document.getElementById(prefix + (isEnergyType ? '_energy_unit' : '_sensor_unit'));
      const entity = entitySelect ? entitySelect.value : '';
      const unit = resolveUnitValue(unitInput ? unitInput.value : '', entity, sensorMetaCache.units);
      html += '<div class="tile-value ' + sensorValueClass + '" id="' + tileId + '-value">--';
      if (unit) html += '<span class="tile-unit">' + escapeHtml(unit) + '</span>';
      html += '</div>';
      if (entity) {
        tileElem.innerHTML = html;
        if (wasActive) tileElem.classList.add('active');
        if (isEnergyType) updateEnergyValuePreview(tab);
        else updateSensorValuePreview(tab);
      }
    }

    if (previewKind === 'clock') {
      const flags = getClockFlagsFromInputs(prefix);
      const clockTimeFont = document.getElementById(prefix + '_clock_time_font')?.value || '40';
      const clockDateFont = Math.min(72,
        Number(document.getElementById(prefix + '_clock_date_font')?.value || 20));
      const clockTimeFormat = document.getElementById(prefix + '_clock_time_format')?.value || '0';
      const clockDateFormat = document.getElementById(prefix + '_clock_date_format')?.value || '0';
      if (flags & 1) html += '<div class="tile-clock-time" ' + getClockPreviewTextStyle(clockTimeFont, 40, '#fff') + '>' + getClockPreviewTime(clockTimeFormat) + '</div>';
      if (flags & 2) html += '<div class="tile-clock-date" ' + getClockPreviewTextStyle(clockDateFont, 24, '#fff') + '>' + getClockPreviewDate(clockDateFormat) + '</div>';
    }

    if (previewKind === 'text') {
      const textValue = document.getElementById(prefix + '_text_value')?.value || '';
      if (textValue) {
        const textFont = document.getElementById(prefix + '_text_value_font')?.value || '0';
        const textClass = getSensorValueFontClass(textFont);
        html += '<div class="tile-text ' + textClass + '">' +
          escapeHtml(textValue) + '</div>';
      }
    }

    if (previewKind === 'switch' && switchStyle === '1') {
      html += '<div class="tile-switch" id="' + tileId + '-switch"><div class="tile-switch-knob"></div></div>';
    }

    html += getTileResizeHandlesHtml(type);
    tileElem.innerHTML = html;
    if (wasActive) tileElem.classList.add('active');
    if (typeWas !== type && wasActive) {
      tileElem.classList.add('active');
      const settingsId = tab + 'Settings';
      document.getElementById(settingsId)?.classList.remove('hidden');
    }
    if (type === '5') updateSwitchValuePreview(tab);
    updateLayoutFromInputs(tab);
    if (previewKind === 'climate' &&
        typeof mountClimateMiniEditor === 'function') {
      mountClimateMiniEditor(tab);
      syncClimateSlotFields(tab);
    }
  }

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
        if (isScreensaverTileTab(tab)) {
          const opacity = document.getElementById('screensaver_tile_opacity');
          if (opacity) opacity.value = String(data.background_opacity ?? 0);
        }
        const colEl = document.getElementById(prefix + '_tile_col');
        const rowEl = document.getElementById(prefix + '_tile_row');
        const spanWEl = document.getElementById(prefix + '_tile_span_w');
        const spanHEl = document.getElementById(prefix + '_tile_span_h');
        if (colEl && rowEl && spanWEl && spanHEl) {
          const fallbackLayout = (data.type === 0) ? getTileElementLayout(tab, index) : null;
          const layoutInput = {
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
        }
        const meta = colorMeta;
        callTypeHandler(meta, 'load', prefix, data);
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
    fetch('/api/tiles?folder=' + encodeURIComponent(folderId) + '&index=' + index)
      .then(res => res.json())
      .then(data => applyTileDataToEditor(index, tab, data))
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
    if (typeEl) typeEl.disabled = locked;
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
      spanWEl.min = String(mediaType ? Math.min(MEDIA_TILE_MIN_SPAN, GRID_COLS) : 1);
      spanWEl.max = String(mediaType ? Math.min(MEDIA_TILE_MAX_SPAN, GRID_COLS) : GRID_COLS);
    }
    if (spanHEl) {
      const availableRows = GRID_ROWS - firstAllowedGridRow(tab);
      spanHEl.min = String(mediaType ? Math.min(MEDIA_TILE_MIN_SPAN, availableRows) : 1);
      spanHEl.max = String(mediaType
        ? Math.min(MEDIA_TILE_MAX_SPAN, availableRows)
        : GRID_ROWS);
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
  }

  let notificationTimer = null;

  function showNotification(message, success = true) {
    const notification = document.getElementById('notification');
    if (!notification) return;
    // A single shared timer. Every call used to schedule its own, so the timeout
    // of an earlier message hid the next one long before its three seconds were
    // up - easy to hit because autosave reports on every field change.
    if (notificationTimer) clearTimeout(notificationTimer);
    notification.textContent = message;
    notification.classList.toggle('is-error', !success);
    notification.classList.add('show');
    notificationTimer = setTimeout(() => {
      notificationTimer = null;
      notification.classList.remove('show');
    }, 3000);
  }

  function scheduleAutoSave(tab, tileIndexOverride = null) {
    const tileIndex = tileIndexOverride !== null ? tileIndexOverride : currentTileIndex;
    if (tileIndex === -1) return;
    const timerKey = tab + ':' + tileIndex;
    if (autoSaveTimers[timerKey]) clearTimeout(autoSaveTimers[timerKey]);
    autoSaveTimers[timerKey] = setTimeout(() => {
      delete autoSaveTimers[timerKey];
      saveTile(tab, true, tileIndex);
    }, 250);
  }

  function resetAllTypeFields(tab) {
    const metas = Object.values(TILE_TYPE_REGISTRY || {});
    metas.forEach(meta => callTypeHandler(meta, 'reset', tab));
  }

  function applyFolderTypeLock(tab, locked) {
    const sel = document.getElementById(tab + '_tile_type');
    if (sel) {
      for (const opt of sel.options) {
        // Keeping the folder and emptying or deleting it stay allowed; every
        // other type is locked while the folder still contains tiles.
        opt.disabled = locked && opt.value !== '4' && opt.value !== '0';
      }
    }
    const hint = document.getElementById(tab + '_tile_type_hint');
    if (hint) hint.classList.toggle('hidden', !locked);
  }

  function resetTile(tab) {
    if (currentTileIndex === -1) return;
    const tileType = getCurrentTileType(tab);
    if (isLockedTileType(tileType)) {
      showNotification(t('tileCannotDelete'), false);
      return;
    }
    const prefix = tab;
    document.getElementById(prefix + '_tile_type').value = '0';
    document.getElementById(prefix + '_tile_title').value = '';
    document.getElementById(prefix + '_tile_icon').value = '';
    setTileColorInputFromStored(tab, 0, '#2A2A2A');
    if (isScreensaverTileTab(tab)) {
      const opacity = document.getElementById('screensaver_tile_opacity');
      if (opacity) opacity.value = '0';
    }
    resetAllTypeFields(tab);
    syncGaugeUi(tab);
    updateTileType(tab);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }

  function deleteFolder(tab) {
    const folderId = getFolderIdForTab(tab);
    if (folderId === undefined || folderId === 0) {
      showNotification(t('folderCannotDelete'), false);
      return;
    }
    const tabEl = document.getElementById('tab-tiles-' + tab);
    const folderName = tabEl ? (tabEl.dataset.folderName || t('folderPrefix').trim()) : t('folderPrefix').trim();
    if (!confirm(tf('deleteFolderConfirm', { name: folderName }))) {
      return;
    }
    const formData = new FormData();
    formData.append('folder_id', folderId);
    fetch('/api/folders/delete', { method: 'POST', body: formData })
      .then(res => res.json())
      .then(data => {
        if (data.success) {
          showNotification(t('folderDeleted'));
          setTimeout(() => location.reload(), 500);
        } else {
          showNotification(data.error || t('deleteFailed'), false);
        }
      })
      .catch(() => showNotification(t('networkError'), false));
  }

  function saveHiddenSettingsTile(tab, silent = false) {
    const tileIndex = HIDDEN_SETTINGS_TILE_INDEX;
    const saveKey = getTileSaveKey(tab, tileIndex);
    if (saveInFlightByTile[saveKey]) {
      queueSaveAfterFlight(tab, tileIndex, silent);
      return;
    }
    const snapshot = getTileSnapshotForSave(tab, tileIndex);
    if (!snapshot) return;
    snapshot.type = '7';
    const requestId = ++saveRequestSeq;
    const draftRev = Number(snapshot._rev || 0);
    markLatestSaveRequest(tab, tileIndex, requestId);
    saveInFlightByTile[saveKey] = true;
    queueSettingsAccessSave(null, null, snapshot)
      .then(success => {
        if (!isLatestSaveRequest(tab, tileIndex, requestId) || !success) return;
        if (!silent) showNotification(t('tileSaved'));
        const currentDraft = drafts[tab] && drafts[tab][tileIndex];
        if (currentDraft && currentDraft._dirty &&
            Number(currentDraft._rev || 0) !== draftRev) {
          queueSaveAfterFlight(tab, tileIndex, true);
          return;
        }
        clearDraft(tab, tileIndex);
      })
      .finally(() => {
        delete saveInFlightByTile[saveKey];
        flushQueuedSave(tab, tileIndex);
      });
  }

  function saveTile(tab, silent = false, tileIndexOverride = null) {
    const tileIndex = tileIndexOverride !== null ? tileIndexOverride : currentTileIndex;
    if (tileIndex === -1) return;
    if (tileIndex === HIDDEN_SETTINGS_TILE_INDEX) {
      saveHiddenSettingsTile(tab, silent);
      return;
    }
    const saveKey = getTileSaveKey(tab, tileIndex);
    if (saveInFlightByTile[saveKey]) {
      queueSaveAfterFlight(tab, tileIndex, silent);
      return;
    }
    const tiles = getTilesData(tab);
    const previousTile = Array.isArray(tiles) ? tiles[tileIndex] : null;
    const previousType = previousTile ? Number(previousTile.type) : NaN;
    const snapshot = getTileSnapshotForSave(tab, tileIndex);
    if (!snapshot) return;
    const formData = new FormData();
    const layout = normalizeSnapshotLayout(snapshot, tileIndex, tab);
    const folderId = getFolderIdForTab(tab);
    if (folderId === undefined) {
      showNotification(t('folderNotFound'), false);
      return;
    }
    formData.append('folder', folderId);
    formData.append('index', tileIndex);
    formData.append('col', layout.col);
    formData.append('row', layout.row);
    formData.append('span_w', layout.span_w);
    formData.append('span_h', layout.span_h);
    formData.append('type', snapshot.type || '0');
    formData.append('title', snapshot.title || '');
    formData.append('icon_name', snapshot.icon || '');
    if (snapshotBgColorIsDefault(snapshot)) {
      formData.append('bg_color_default', '1');
    } else {
      formData.append('bg_color', hexToRgb(snapshot.color || '#2A2A2A'));
    }
    const typeValue = String(snapshot.type || '0');
    for (const [key, value] of Object.entries(snapshot)) {
      if (key === '_dirty' || key === '_rev' || key === 'type' || key === 'title' || key === 'icon' || key === 'color' || key === 'bg_color_default' || key === 'col' || key === 'row' || key === 'span_w' || key === 'span_h') continue;
      formData.append(key, value);
    }
    applySnapshotToTileData(tab, tileIndex, snapshot);
    const requestId = ++saveRequestSeq;
    const draftRev = Number(snapshot._rev || 0);
    markLatestSaveRequest(tab, tileIndex, requestId);
    saveInFlightByTile[saveKey] = true;
    fetch('/api/tiles', { method:'POST', body:formData })
      .then(res => res.json())
      .then(data => {
        if (!isLatestSaveRequest(tab, tileIndex, requestId)) return;
        if (data.success) {
          if (!silent) showNotification(t('tileSaved'));
          const currentDraft = drafts[tab] && drafts[tab][tileIndex];
          if (currentDraft && currentDraft._dirty && Number(currentDraft._rev || 0) !== draftRev) {
            queueSaveAfterFlight(tab, tileIndex, true);
            return;
          }
          clearDraft(tab, tileIndex);
          if (!silent) loadSensorValues(true);
          if (typeValue === '4') {
            const resolvedNavTarget = String((data && data.navigate_target !== undefined && data.navigate_target !== null)
              ? data.navigate_target
              : (snapshot.navigate_target || '0'));
            snapshot.navigate_target = resolvedNavTarget;
            if (tilesData[tab] && tilesData[tab][tileIndex]) {
              tilesData[tab][tileIndex].navigate_target = parseInt(resolvedNavTarget, 10) || 0;
              tilesData[tab][tileIndex].folder_pin_enabled =
                data?.folder_pin_enabled === true;
              tilesData[tab][tileIndex].folder_pin =
                String(data?.folder_pin || '');
            }
            syncFolderPinControls(tab);
            const navTargetNum = parseInt(resolvedNavTarget, 10);
            const titleVal = snapshot.title || '';
            const iconVal = snapshot.icon || '';
            ensureFolderTabUi(navTargetNum, titleVal, iconVal).then(ok => {
              restoreCurrentTileSelectionUi();
              if (!ok) {
                persistSelectedTileState();
                setTimeout(() => location.reload(), 400);
              }
            });
          }
          if (previousType === 4 && typeValue === '0') {
            persistSelectedTileState();
            setTimeout(() => location.reload(), 400);
          }
        } else {
          showNotification(data.error || t('unknownError'), false);
        }
      })
      .catch(() => {
        if (!isLatestSaveRequest(tab, tileIndex, requestId)) return;
        showNotification(t('networkErrorSave'), false);
      })
      .finally(() => {
        delete saveInFlightByTile[saveKey];
        flushQueuedSave(tab, tileIndex);
      });
  }

  function downloadJsonFile(filename, content) {
    const blob = new Blob([content], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 5000);
  }

  function parseBgColorValue(value) {
    if (value === undefined || value === null) return 0;
    if (typeof value === 'string') {
      let v = value.trim();
      if (!v.length) return 0;
      if (v.startsWith('#')) return parseInt(v.substring(1), 16) || 0;
      if (v.startsWith('0x') || v.startsWith('0X')) return parseInt(v, 16) || 0;
    }
    const num = parseInt(value, 10);
    return isNaN(num) ? 0 : num;
  }

  function buildScreensaverExportConfig(data) {
    return {
      version: Number(data?.version || 1),
      use_wallpapers: !!data?.use_wallpapers,
      shuffle: !!data?.shuffle,
      tile_shadow: !!data?.tile_shadow,
      tile_border: data?.tile_border !== false,
      show_time: !!data?.show_time,
      show_date: !!data?.show_date,
      show_weekday: !!data?.show_weekday,
      clock_shadow: !!data?.clock_shadow,
      time_format: Number(data?.time_format || 0),
      date_format: Number(data?.date_format || 0),
      time_alignment: Math.round(ssClamp(data?.time_alignment ?? 1, 0, 2)),
      date_alignment: Math.round(ssClamp(data?.date_alignment ?? 1, 0, 2)),
      time_font_size: Number(data?.time_font_size || 48),
      date_font_size: Number(data?.date_font_size || 28),
      clock_x: Number(data?.clock_x ?? 500),
      clock_y: Number(data?.clock_y ?? 350),
      duration_seconds: Number(data?.duration_seconds ?? 15),
      wallpapers: Array.isArray(data?.wallpapers) ? data.wallpapers.map(wallpaper => ({
        file_name: String(wallpaper?.file_name || ''),
        enabled: !!wallpaper?.enabled,
        focus_x: Number(wallpaper?.focus_x ?? 500),
        focus_y: Number(wallpaper?.focus_y ?? 500),
        zoom: Number(wallpaper?.zoom ?? 1000)
      })) : []
    };
  }

  async function exportTilesConfig() {
    try {
      const foldersRequest = fetch('/api/folders').then(async res => {
        const data = await res.json();
        if (!res.ok || !Array.isArray(data)) {
          throw new Error('Folder export failed');
        }
        return data.map(folder => ({
          id: Number(folder?.id || 0),
          parent_id: Number(folder?.parent_id || 0),
          name: String(folder?.name || ''),
          icon_name: String(folder?.icon_name || '')
        }));
      });
      const screensaverConfigRequest = fetch('/api/screensaver').then(async res => {
        const data = await res.json();
        if (!res.ok || !data?.success) throw new Error('Screensaver config export failed');
        return data;
      });
      const screensaverGridRequest = fetch(
        '/api/tiles?folder=' + encodeURIComponent(SCREENSAVER_FOLDER_ID)
      ).then(async res => {
        const data = await res.json();
        if (!res.ok || !Array.isArray(data)) throw new Error('Screensaver grid export failed');
        return data.map(tile => {
          const exported = {...tile};
          delete exported.folder_pin_enabled;
          delete exported.folder_pin;
          return exported;
        });
      });
      const [folders, screensaverData, screensaverGrid] = await Promise.all([
        foldersRequest, screensaverConfigRequest, screensaverGridRequest
      ]);
      const tilesLists = await Promise.all(folders.map(async folder => {
        const response = await fetch(
          '/api/tiles?folder=' + encodeURIComponent(folder.id));
        const data = await response.json();
        if (!response.ok || !Array.isArray(data)) {
          throw new Error('Folder grid export failed');
        }
        return data.map(tile => {
          const exported = {...tile};
          delete exported.folder_pin_enabled;
          delete exported.folder_pin;
          return exported;
        });
      }));

      const grids = {};
      folders.forEach((folder, idx) => {
        grids[String(folder.id)] =
          Array.isArray(tilesLists[idx]) ? tilesLists[idx] : [];
      });

      const payload = {
        version: 3,
        exported_at: new Date().toISOString(),
        folders: folders,
        grids: grids,
        screensaver: {
          version: 2,
          config: buildScreensaverExportConfig(screensaverData),
          grid: screensaverGrid,
          source_layout: {
            screen_width: Number(screensaverData.screen_width || 0),
            screen_height: Number(screensaverData.screen_height || 0),
            grid_cols: Number(screensaverData.grid_cols || 0),
            grid_rows: Number(screensaverData.grid_rows || 0)
          }
        }
      };
      const ts = new Date().toISOString().replace(/[:.]/g, '-');
      downloadJsonFile('waveshare_tiles_' + ts + '.json', JSON.stringify(payload, null, 2));
      showNotification(t('exportCreated'));
    } catch (e) {
      showNotification(t('exportFailed'), false);
    }
  }

  function triggerTilesImport(tab) {
    const input = document.getElementById(tab + '_tile_import');
    if (!input) return;
    input.value = '';
    input.click();
  }

  function importTilesConfig(tab, files) {
    if (!files || !files.length) return;
    const file = files[0];
    const reader = new FileReader();
    reader.onload = async () => {
      try {
        const payload = JSON.parse(reader.result);
        await importTilesPayload(payload);
      } catch (e) {
        showNotification(t('importInvalidJson'), false);
      }
    };
    reader.onerror = () => showNotification(t('importFailed'), false);
    reader.readAsText(file);
  }

  function normalizeImportFolderName(value) {
    return String(value || '').trim().toLowerCase();
  }

  function normalizeImportFolderIcon(value) {
    return normalizeIconName(value || '');
  }

  async function fetchFoldersForImport() {
    const res = await fetch('/api/folders');
    if (!res.ok) throw new Error('Folder fetch failed');
    const data = await res.json();
    return Array.isArray(data) ? data : [];
  }

  async function fetchTilesForImport(folderId) {
    const res = await fetch('/api/tiles?folder=' + encodeURIComponent(folderId));
    if (!res.ok) throw new Error('Tile fetch failed');
    const data = await res.json();
    return Array.isArray(data) ? data : [];
  }

  function buildEmptyImportTile(index) {
    return {
      type: 0,
      title: '',
      icon_name: '',
      bg_color: 0,
      col: index % GRID_COLS,
      row: Math.floor(index / GRID_COLS),
      span_w: 1,
      span_h: 1
    };
  }

  function updateFolderImportMap(sourceFolders, targetFolders, sourceToTarget) {
    let changed = false;
    sourceFolders.forEach(sourceFolder => {
      const sourceId = parseInt(sourceFolder && sourceFolder.id, 10);
      const sourceParentId = parseInt(sourceFolder && sourceFolder.parent_id, 10);
      if (isNaN(sourceId) || sourceId === 0 || isNaN(sourceParentId)) return;
      const targetParentId = sourceToTarget[sourceParentId];
      if (targetParentId === undefined) return;
      const sourceName = normalizeImportFolderName(sourceFolder.name);
      const sourceIcon = normalizeImportFolderIcon(sourceFolder.icon_name);
      const match = targetFolders.find(targetFolder =>
        Number(targetFolder.parent_id) === Number(targetParentId) &&
        normalizeImportFolderName(targetFolder.name) === sourceName &&
        normalizeImportFolderIcon(targetFolder.icon_name) === sourceIcon
      );
      if (match && sourceToTarget[sourceId] !== Number(match.id)) {
        sourceToTarget[sourceId] = Number(match.id);
        changed = true;
      }
    });
    return changed;
  }

  async function replaceFolderGridForImport(folderId, sourceTiles, systemType, sourceToTarget = null) {
    const currentTiles = await fetchTilesForImport(folderId);
    const sourceList = Array.isArray(sourceTiles) ? sourceTiles.slice(0, GRID_COLS * GRID_ROWS) : [];
    const currentSystemIndex = currentTiles.findIndex(tile => Number(tile && tile.type) === systemType);
    const sourceSystemTile = sourceList.find(tile => Number(tile && tile.type) === systemType) || null;

    for (let i = 0; i < (GRID_COLS * GRID_ROWS); i++) {
      if (i === currentSystemIndex) continue;
      await postTile(folderId, i, buildEmptyImportTile(i), sourceToTarget);
    }

    if (currentSystemIndex >= 0 && sourceSystemTile) {
      await postTile(folderId, currentSystemIndex, sourceSystemTile, sourceToTarget);
    }

    const availableIndices = [];
    for (let i = 0; i < (GRID_COLS * GRID_ROWS); i++) {
      if (i === currentSystemIndex) continue;
      availableIndices.push(i);
    }

    const nonSystemTiles = sourceList.filter(tile => Number(tile && tile.type) !== systemType);
    for (let i = 0; i < nonSystemTiles.length && i < availableIndices.length; i++) {
      await postTile(folderId, availableIndices[i], nonSystemTiles[i] || {}, sourceToTarget);
    }
  }

  function prepareScreensaverTilesForImport(sourceTiles, sourceLayout) {
    const tileCount = GRID_COLS * GRID_ROWS;
    const sourceCols = Number(sourceLayout?.grid_cols || 0);
    const sourceRows = Number(sourceLayout?.grid_rows || 0);
    const sameLayout = sourceCols === GRID_COLS && sourceRows === GRID_ROWS;
    const sourceEntries = (Array.isArray(sourceTiles) ? sourceTiles : [])
      .map((tile, index) => ({ tile: tile || {}, sourceIndex: index }))
      .filter(entry => Number(entry.tile.type || 0) !== 0);

    if (sameLayout) {
      return sourceEntries.slice(0, tileCount).map(entry => ({
        targetIndex: entry.sourceIndex,
        tile: entry.tile
      }));
    }

    // An import between 7xN and 4xN keeps the relative arrangement of the two
    // bottom rows and packs it into the target grid.
    const firstTargetRow = Math.max(0, GRID_ROWS - 2);
    const firstSourceRow = sourceRows > 1 ? sourceRows - 2 : 0;
    const occupied = Array.from({ length: GRID_ROWS }, () => Array(GRID_COLS).fill(false));
    const prepared = [];
    for (const entry of sourceEntries) {
      if (prepared.length >= tileCount) throw new Error('Screensaver grid does not fit target device');
      const tile = entry.tile;
      const mediaTile = Number(tile.type) === MEDIA_TILE_TYPE;
      let spanW = Math.max(1, Number(tile.span_w || 1));
      let spanH = Math.max(1, Number(tile.span_h || 1));
      if (mediaTile) {
        spanW = Math.max(MEDIA_TILE_MIN_SPAN, spanW);
        spanH = Math.max(MEDIA_TILE_MIN_SPAN, spanH);
      }
      spanW = Math.min(spanW, GRID_COLS, mediaTile ? MEDIA_TILE_MAX_SPAN : GRID_COLS);
      spanH = Math.min(spanH, 2, mediaTile ? MEDIA_TILE_MAX_SPAN : 2);

      const sourceSpanW = Math.max(1, Number(tile.span_w || 1));
      const sourceColRange = Math.max(0, sourceCols - sourceSpanW);
      const targetColRange = Math.max(0, GRID_COLS - spanW);
      const relativeCol = sourceColRange > 0
        ? Math.max(0, Math.min(1, Number(tile.col || 0) / sourceColRange))
        : 0;
      const desiredCol = Math.round(relativeCol * targetColRange);
      const sourceRowOffset = Math.max(0, Math.min(1, Number(tile.row || 0) - firstSourceRow));
      const desiredRow = Math.min(GRID_ROWS - spanH, firstTargetRow + sourceRowOffset);

      let best = null;
      for (let row = firstTargetRow; row <= GRID_ROWS - spanH; row++) {
        for (let col = 0; col <= GRID_COLS - spanW; col++) {
          let free = true;
          for (let y = row; y < row + spanH && free; y++) {
            for (let x = col; x < col + spanW; x++) {
              if (occupied[y][x]) { free = false; break; }
            }
          }
          if (!free) continue;
          const score = Math.abs(row - desiredRow) * (GRID_COLS + 1) + Math.abs(col - desiredCol);
          if (!best || score < best.score) best = { row, col, score };
        }
      }
      if (!best) throw new Error('Screensaver grid does not fit target device');
      for (let y = best.row; y < best.row + spanH; y++) {
        for (let x = best.col; x < best.col + spanW; x++) occupied[y][x] = true;
      }
      prepared.push({
        targetIndex: prepared.length,
        tile: { ...tile, col: best.col, row: best.row, span_w: spanW, span_h: spanH }
      });
    }
    return prepared;
  }

  async function replaceScreensaverGridForImport(sourceTiles, sourceLayout = null) {
    const folderId = SCREENSAVER_FOLDER_ID;
    const currentTiles = await fetchTilesForImport(folderId);
    const tileCount = GRID_COLS * GRID_ROWS;
    const preparedTiles = prepareScreensaverTilesForImport(sourceTiles, sourceLayout);
    const supportedTypes = new Set([1, 2, 5, 14, 20, 21, 22, 23, MEDIA_TILE_TYPE]);
    for (const entry of preparedTiles) {
      if (!supportedTypes.has(Number(entry.tile.type || 0))) {
        throw new Error('Unsupported screensaver tile type');
      }
    }

    // Remove the existing tiles first so the imported positions do not fail on
    // temporary overlaps with the old grid.
    for (let i = 0; i < tileCount; i++) {
      if (Number(currentTiles[i]?.type || 0) !== 0) {
        await postTile(folderId, i, buildEmptyImportTile(i));
      }
    }

    for (const entry of preparedTiles) {
      const tile = entry.tile;
      await postTile(folderId, entry.targetIndex, tile);
    }
  }

  async function importScreensaverConfig(config) {
    const res = await fetch('/api/screensaver', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config)
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok || !data.success) {
      throw new Error(data.error || 'Screensaver config import failed');
    }
  }

  async function importTilesPayload(payload) {
    try {
      if (!payload || typeof payload !== 'object') {
        showNotification(t('importInvalidJson'), false);
        return;
      }
      const grids = (payload.grids && typeof payload.grids === 'object') ? payload.grids : {};
      if (!Object.keys(grids).length) {
        if (Array.isArray(payload.tab0)) grids['0'] = payload.tab0;
        if (Array.isArray(payload.tab1)) grids['1'] = payload.tab1;
        if (Array.isArray(payload.tab2)) grids['2'] = payload.tab2;
      }
      if (!Object.keys(grids).length) {
        showNotification(t('importInvalidJson'), false);
        return;
      }

      showNotification(t('importRunning'));

      const sourceFolders = Array.isArray(payload.folders) ? payload.folders : [{ id: 0, parent_id: 0, name: 'Home', icon_name: '' }];
      const sourceToTarget = { 0: 0 };

      if (Array.isArray(grids['0'])) {
        await replaceFolderGridForImport(0, grids['0'], 7, sourceToTarget);
      }

      let targetFolders = await fetchFoldersForImport();
      updateFolderImportMap(sourceFolders, targetFolders, sourceToTarget);

      const pendingFolderIds = sourceFolders
        .map(folder => parseInt(folder && folder.id, 10))
        .filter(folderId => !isNaN(folderId) && folderId !== 0 && Array.isArray(grids[String(folderId)]));

      let progressed = true;
      while (pendingFolderIds.length && progressed) {
        progressed = false;
        for (let i = 0; i < pendingFolderIds.length; ) {
          const sourceFolderId = pendingFolderIds[i];
          const targetFolderId = sourceToTarget[sourceFolderId];
          if (targetFolderId === undefined) {
            i++;
            continue;
          }
          await replaceFolderGridForImport(targetFolderId, grids[String(sourceFolderId)], 8, sourceToTarget);
          pendingFolderIds.splice(i, 1);
          progressed = true;
          targetFolders = await fetchFoldersForImport();
          updateFolderImportMap(sourceFolders, targetFolders, sourceToTarget);
        }
      }

      if (pendingFolderIds.length) {
        throw new Error('Folder mapping failed');
      }

      // Versions 1 and 2 had no screensaver block and stay importable
      // unchanged. Alternative flat field names are accepted as well, in case an
      // intermediate state of this export function was used.
      const screensaverBlock = payload.screensaver && typeof payload.screensaver === 'object'
        ? payload.screensaver
        : null;
      const screensaverConfig = screensaverBlock?.config || payload.screensaver_config;
      const screensaverGrid = screensaverBlock?.grid || payload.screensaver_grid;
      if (screensaverConfig && typeof screensaverConfig === 'object') {
        await importScreensaverConfig(screensaverConfig);
      }
      if (Array.isArray(screensaverGrid)) {
        await replaceScreensaverGridForImport(
          screensaverGrid, screensaverBlock?.source_layout || null);
      }

      try { localStorage.removeItem('tileDrafts'); } catch (e) {}
      showNotification(t('importComplete'));
      setTimeout(() => location.reload(), 600);
    } catch (e) {
      console.error('Tile import failed:', e);
      showNotification(t('importFailed'), false);
    }
  }

  async function postTile(folderId, index, tile, sourceToTarget = null) {
    const fd = new FormData();
    const type = Number(tile.type);
    let safeType = isNaN(type) ? 0 : type;
    if (safeType === 4 && tile.navigate_kind !== undefined && tile.navigate_kind !== null) {
      const kind = Number(tile.navigate_kind);
      if (kind === 1) safeType = 7;
      else if (kind === 2) safeType = 8;
    }
    fd.append('folder', folderId);
    fd.append('index', index);
    fd.append('type', safeType);
    fd.append('title', tile.title || '');
    fd.append('icon_name', tile.icon_name || '');
    const parsedBgColor = parseBgColorValue(tile.bg_color);
    if (parsedBgColor !== 0 || (typeof tile.bg_color === 'string' && tile.bg_color.trim().startsWith('#'))) {
      fd.append('bg_color', parsedBgColor);
    } else {
      fd.append('bg_color_default', '1');
    }
    const layout = normalizeTileLayout(tile, index, tabByFolder[folderId] || '');
    fd.append('col', layout.col);
    fd.append('row', layout.row);
    fd.append('span_w', layout.span_w);
    fd.append('span_h', layout.span_h);
    if (tile.background_opacity !== undefined && tile.background_opacity !== null) {
      fd.append('background_opacity', tile.background_opacity);
    }

    if ([21, 22, 23].includes(safeType)) fd.append('sensor_value_font', tile.sensor_value_font ?? 2);
    if (safeType === 1) {
      fd.append('sensor_entity', tile.sensor_entity || '');
      fd.append('sensor_unit', tile.sensor_unit || '');
      const dec = tile.sensor_decimals;
      if (dec !== undefined && dec !== null && Number(dec) >= 0) {
        fd.append('sensor_decimals', dec);
      }
      if (tile.sensor_value_font !== undefined && tile.sensor_value_font !== null) {
        fd.append('sensor_value_font', tile.sensor_value_font);
      }
      const isScreensaverTile =
        Number(folderId) === Number(SCREENSAVER_FOLDER_ID);
      const displayMode = isScreensaverTile
        ? 0
        : ((tile.sensor_display_mode !== undefined) ? tile.sensor_display_mode : (tile.sensor_gauge ? 1 : 0));
      fd.append('sensor_display_mode', String(displayMode));
      if (tile.sensor_gauge_min !== undefined && tile.sensor_gauge_min !== null && String(tile.sensor_gauge_min).length > 0) {
        fd.append('sensor_gauge_min', tile.sensor_gauge_min);
      }
      if (tile.sensor_gauge_max !== undefined && tile.sensor_gauge_max !== null && String(tile.sensor_gauge_max).length > 0) {
        fd.append('sensor_gauge_max', tile.sensor_gauge_max);
      }
      if (tile.sensor_gauge_arc !== undefined && tile.sensor_gauge_arc !== null && String(tile.sensor_gauge_arc).length > 0) {
        fd.append('sensor_gauge_arc', tile.sensor_gauge_arc);
      }
      if (tile.sensor_gauge_size !== undefined && tile.sensor_gauge_size !== null && String(tile.sensor_gauge_size).length > 0) {
        fd.append('sensor_gauge_size', tile.sensor_gauge_size);
      }
      if (tile.sensor_gauge_y_offset !== undefined && tile.sensor_gauge_y_offset !== null && String(tile.sensor_gauge_y_offset).length > 0) {
        fd.append('sensor_gauge_y_offset', tile.sensor_gauge_y_offset);
      }
      if (tile.sensor_value_y_offset !== undefined && tile.sensor_value_y_offset !== null && String(tile.sensor_value_y_offset).length > 0) {
        fd.append('sensor_value_y_offset', tile.sensor_value_y_offset);
      }
      if (tile.sensor_graph_height !== undefined && tile.sensor_graph_height !== null && String(tile.sensor_graph_height).length > 0) {
        fd.append('sensor_graph_height', tile.sensor_graph_height);
      }
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType >= 21 && safeType <= 23) {
      const kind = ['number', 'select', 'datetime'][safeType - 21];
      fd.append(kind + '_entity', tile.sensor_entity || tile[kind + '_entity'] || '');
      fd.append('popup_open_mode', tile.popup_open_mode ?? 1);
    } else if (safeType === 20) {
      fd.append(
        'binary_sensor_entity',
        tile.sensor_entity || tile.binary_sensor_entity || '');
      if (tile.popup_open_mode !== undefined &&
          tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 2) {
      fd.append('scene_alias', tile.scene_alias || '');
    } else if (safeType === 4) {
      const rawTarget = Number(tile.navigate_target);
      let target = 0;
      if (!isNaN(rawTarget) && rawTarget > 0) {
        if (sourceToTarget && sourceToTarget[rawTarget] !== undefined) {
          target = sourceToTarget[rawTarget];
        }
      }
      fd.append('navigate_target', target);
    } else if (safeType === 5) {
      fd.append('switch_entity', tile.sensor_entity || '');
      const style = (tile.switch_style !== undefined && tile.switch_style !== null)
        ? tile.switch_style
        : (tile.sensor_decimals === 1 ? 1 : 0);
      fd.append('switch_style', style);
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 10) {
      fd.append('text_value', tile.text_value || tile.scene_alias || tile.key_macro || '');
      fd.append('text_value_font', tile.text_value_font || tile.sensor_value_font || '0');
    } else if (safeType === 9) {
      fd.append('clock_show_time', ((Number(tile.sensor_decimals || 1) & 1) !== 0) ? '1' : '0');
      fd.append('clock_show_date', ((Number(tile.sensor_decimals || 1) & 2) !== 0) ? '1' : '0');
      fd.append('key_code', tile.key_code || 40);
      fd.append('key_modifier', tile.key_modifier || 20);
      fd.append('clock_time_format', (tile.sensor_gauge_min !== undefined && tile.sensor_gauge_min !== null) ? tile.sensor_gauge_min : 0);
      fd.append('clock_date_format', (tile.sensor_gauge_max !== undefined && tile.sensor_gauge_max !== null) ? tile.sensor_gauge_max : 0);
    } else if (safeType === 12) {
      fd.append('weather_entity', tile.sensor_entity || tile.weather_entity || '');
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 14) {
      fd.append('energy_entity', tile.sensor_entity || tile.energy_entity || '');
      fd.append('sensor_unit', tile.sensor_unit || '');
      const dec = tile.sensor_decimals;
      fd.append('sensor_decimals', (dec !== undefined && dec !== null && Number(dec) >= 0) ? dec : '1');
      if (tile.sensor_value_font !== undefined && tile.sensor_value_font !== null) {
        fd.append('sensor_value_font', tile.sensor_value_font);
      }
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
      if (tile.sensor_value_y_offset !== undefined && tile.sensor_value_y_offset !== null && String(tile.sensor_value_y_offset).length > 0) {
        fd.append('sensor_value_y_offset', tile.sensor_value_y_offset);
      }
    } else if (safeType === MEDIA_TILE_TYPE) {
      fd.append('media_entity', tile.sensor_entity || tile.media_entity || '');
    } else if (safeType === 17) {
      fd.append('climate_entity', tile.sensor_entity || tile.climate_entity || '');
      fd.append(
        'climate_slots_packed',
        (tile.sensor_gauge_min !== undefined &&
         tile.sensor_gauge_min !== null)
          ? tile.sensor_gauge_min : 0);
      fd.append(
        'climate_layouts_packed',
        getClimateLayoutPayload(tile.sensor_gauge_max));
      fd.append(
        'climate_geometry',
        tile.climate_geometry || tile.scene_alias || '');
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 19) {
      fd.append('cover_entity', tile.sensor_entity || tile.cover_entity || '');
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 18) {
      fd.append('camera_entity', tile.sensor_entity || tile.camera_entity || '');
    } else if (safeType === 16) {
      fd.append('animation_file', tile.animation_file || tile.scene_alias || '');
      fd.append('animation_fps', tile.animation_fps || tile.image_slideshow_sec || '10');
      fd.append('animation_fit',
        (tile.animation_fit !== undefined && tile.animation_fit !== null)
          ? tile.animation_fit
          : (tile.sensor_display_mode || '0'));
      fd.append('animation_zoom',
        (tile.animation_zoom !== undefined && tile.animation_zoom !== null)
          ? tile.animation_zoom
          : (tile.sensor_gauge_max || '100'));
    }

    const res = await fetch('/api/tiles', { method: 'POST', body: fd });
    const data = await res.json();
    if (!data.success) {
      throw new Error('Tile speichern fehlgeschlagen');
    }
  }

  function rgbToHex(rgb) {
    const num = Number(rgb);
    const masked = Number.isFinite(num) ? (num & 0xFFFFFF) : 0;
    return '#' + ('000000' + masked.toString(16)).slice(-6);
  }
  function hexToRgb(hex) {
    const parsed = parseInt(String(hex || '').replace('#', ''), 16);
    return isNaN(parsed) ? 0 : (parsed & 0xFFFFFF);
  }
  function makeTileBgValue(rgb) {
    return (Number(rgb) & 0xFFFFFF) | 0x01000000;
  }
  function tileBgValueIsSet(value) {
    const num = Number(value);
    return Number.isFinite(num) && num !== 0;
  }
  function tileBgToHex(value, fallback) {
    const num = Number(value);
    if (!Number.isFinite(num) || num === 0) return fallback || '#353535';
    return rgbToHex(num);
  }
  function snapshotBgColorIsDefault(snapshot) {
    return String(snapshot?.bg_color_default || '0') === '1';
  }
  function tileColorInputIsDefault(tab) {
    const input = document.getElementById(tab + '_tile_color');
    return !!input && input.dataset.bgColorDefault === '1';
  }
  function setTileColorInputFromStored(tab, value, fallback) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    input.value = tileBgToHex(value, fallback || '#2A2A2A');
    input.dataset.bgColorDefault = tileBgValueIsSet(value) ? '0' : '1';
  }
  function setTileColorInputFromSnapshot(tab, snapshot) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const meta = getTileTypeMeta(snapshot?.type || '0');
    const isDefault = snapshotBgColorIsDefault(snapshot);
    input.value = isDefault ? (meta.defaultBg || '#2A2A2A') : (snapshot?.color || meta.defaultBg || '#2A2A2A');
    input.dataset.bgColorDefault = isDefault ? '1' : '0';
  }
  function markTileColorInputExplicit(tab) {
    const input = document.getElementById(tab + '_tile_color');
    if (input) input.dataset.bgColorDefault = '0';
  }
  function resetTileColor(tab) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const meta = getTileTypeMeta(typeValue);
    input.value = meta.defaultBg || '#2A2A2A';
    input.dataset.bgColorDefault = '1';
    if (isScreensaverTileTab(tab)) {
      const opacity = document.getElementById('screensaver_tile_opacity');
      if (opacity) opacity.value = String(SCREENSAVER_TILE_DEFAULT_OPACITY);
    }
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }

  function renderTileFromData(tab, index, tile, sensorMeta) {
    const el = document.getElementById(tab + '-tile-' + index);
    if (!el) return;
    const metaValues = sensorMeta?.values || {};
    const metaUnits = sensorMeta?.units || {};
    const metaIcons = sensorMeta?.icons || {};
    const metaNames = sensorMeta?.names || {};
    el.dataset.index = index.toString();
    const typeValue = String(tile?.type ?? '0');
    const meta = getTileTypeMeta(typeValue);
    if (typeValue === '17' &&
        currentTileTab === tab &&
        currentTileIndex === index &&
        el.classList.contains('climate-content-editing')) {
      syncClimateSlotFields(tab);
      return;
    }
    let cls = ['tile'];
    if (meta.css) cls.push(meta.css);
    if (typeValue === '5' && tile.switch_style === 1) cls.push('switch-toggle');
    if (typeValue === '0' && (!meta.css || meta.css !== 'empty')) cls.push('empty');
    el.className = cls.join(' ');
    el.dataset.type = typeValue;
    if (typeValue === '4') el.dataset.navigateTarget = String(tile.navigate_target || 0);
    else delete el.dataset.navigateTarget;
    if (typeValue === '0') el.style.background = 'transparent';
    else {
      const bg = tileBgToHex(tile.bg_color, meta.defaultBg || '#353535');
      if (isScreensaverTileTab(tab)) {
        const opacity = clampInt(tile.background_opacity, 0, 255,
                                 SCREENSAVER_TILE_DEFAULT_OPACITY);
        el.style.background = bg + opacity.toString(16).padStart(2, '0');
      } else {
        el.style.background = bg;
      }
      el.style.removeProperty('--switch-knob-color');
      el.style.removeProperty('--switch-on-color');
      if (typeValue === '5' && tile.switch_style === 1) {
        el.style.setProperty('--switch-knob-color', bg);
        el.style.setProperty('--switch-on-color', '#3B82F6');
      }
    }
    const sensorValueClass = getSensorValueFontClass(tile.sensor_value_font);
    if (typeValue === '0') {
      el.innerHTML = '';
      applyTileAriaLabel(el, '', typeValue);
    }
    else {
      const previewKind = meta.preview || 'none';
      const iconEntity = (isEditablePreview(previewKind) || previewKind === 'sensor' ||
                          previewKind === 'binary_sensor' ||
                          previewKind === 'switch' ||
                          previewKind === 'weather' || previewKind === 'media' ||
                          previewKind === 'climate' || previewKind === 'cover' ||
                          previewKind === 'camera')
        ? (tile.sensor_entity || '')
        : '';
      const rawIcon = tile.icon_name || '';
      let iconName = resolveIconName(
        rawIcon,
        iconEntity,
        metaIcons);
      if (previewKind === 'camera' && !iconName &&
          !isExplicitlyDisabledValue(rawIcon)) {
        iconName = 'video';
      }
      let climatePreviewState = null;
      if (previewKind === 'climate') {
        climatePreviewState = parseClimatePreviewPayload(
          tile.sensor_entity ? (metaValues[tile.sensor_entity] ?? '') : '');
        if (!normalizeMdiIconName(rawIcon) &&
            !isExplicitlyDisabledValue(rawIcon)) {
          iconName = climatePreviewIcon(climatePreviewState, iconName);
        }
      }
      let coverPreviewState = null;
      if (previewKind === 'cover') {
        coverPreviewState = parseCoverPreviewPayload(
          tile.sensor_entity ? (metaValues[tile.sensor_entity] ?? '') : '');
        if (!normalizeMdiIconName(rawIcon) &&
            !isExplicitlyDisabledValue(rawIcon)) {
          iconName = coverPreviewIcon(coverPreviewState, iconName);
        }
      }
      let binarySensorPreviewState = null;
      if (previewKind === 'binary_sensor') {
        binarySensorPreviewState = parseBinarySensorPreviewPayload(
          tile.sensor_entity ? (metaValues[tile.sensor_entity] ?? '') : '');
        iconName = resolveBinarySensorPreviewIcon(
          rawIcon, tile.sensor_entity || '', binarySensorPreviewState,
          metaIcons);
      }

      let html = '';

      if (iconName) {
        const iconStyle = previewKind === 'climate'
          ? ' style="color:' + climatePreviewColor(climatePreviewState) + '"'
          : (previewKind === 'cover'
            ? ' style="color:' + coverPreviewColor(coverPreviewState) + '"'
            : (previewKind === 'binary_sensor'
              ? ' style="color:' + binarySensorPreviewColor(
                  binarySensorPreviewState) + '"'
              : ''));
        html += '<i class="mdi mdi-' + escapeHtml(iconName) + ' tile-icon"' + iconStyle + '></i>';
      }

      let displayTitle = tile.title || '';
      if (previewKind === 'camera' && !displayTitle && tile.sensor_entity) {
        displayTitle = metaNames[tile.sensor_entity] ||
          titleFromEntity(tile.sensor_entity);
      }
      if (displayTitle.length) {
        html += '<div class="tile-title" id="' + tab + '-tile-' + index + '-title">' +
          tileTitleHtml(displayTitle) + '</div>';
      }
      applyTileAriaLabel(el, displayTitle, typeValue);

      if (previewKind === 'weather') {
        html += '<div class="tile-ghost-icon"><i class="mdi mdi-weather-partly-cloudy"></i></div>';
      }
      if (previewKind === 'media') {
        html += '<div class="tile-ghost-icon"><i class="mdi mdi-music"></i></div>';
      }

      if (previewKind === 'sensor') {
        let value = '--';
        if (tile.sensor_entity) value = formatSensorValue(metaValues[tile.sensor_entity] ?? '--', tile.sensor_decimals);
        const unit = resolveUnitValue(tile.sensor_unit || '', tile.sensor_entity || '', metaUnits);
        html += '<div class="tile-value ' + sensorValueClass + '" id="' + tab + '-tile-' + index + '-value">' +
          escapeHtml(value) +
          (unit ? '<span class="tile-unit">' + escapeHtml(unit) + '</span>' : '') +
          '</div>';
      }
      if (previewKind === 'climate') {
        html += climatePreviewSlots(
          climatePreviewState,
          tile.span_w || 1,
          tile.span_h || 1,
          decodeClimateSlotConfig(tile.sensor_gauge_min || 0),
          decodeClimateTargetLayouts(tile.sensor_gauge_max || 0),
          tile.climate_geometry || tile.scene_alias || '');
      }
      if (previewKind === 'cover') {
        const value = coverPreviewState?.position !== null &&
                      coverPreviewState?.position !== undefined
          ? String(coverPreviewState.position) + '%' : '--%';
        html += '<div class="tile-value tile-cover-value">' +
          escapeHtml(coverPreviewStateText(coverPreviewState)) +
          '<br>' + escapeHtml(value) + '</div>';
      }
      if (previewKind === 'binary_sensor') {
        html += '<div class="tile-value tile-binary-sensor-value" id="' +
          tab + '-tile-' + index + '-value">' +
          escapeHtml(binarySensorPreviewStateText(binarySensorPreviewState)) +
          '</div>';
      }
      if (isEditablePreview(previewKind)) html += '<div class="tile-value tile-editable-value ' + sensorValueClass + '">' + escapeHtml(editablePreviewText(iconEntity, previewKind, sensorMeta)) + '</div>';
      if (previewKind === 'clock') {
        const flags = normalizeClockFlags(tile.sensor_decimals);
        const clockTimeFont = tile.key_code || 40;
        const clockDateFont = Math.min(72, Number(tile.key_modifier || 20));
        const clockTimeFormat = (tile.sensor_gauge_min !== undefined) ? tile.sensor_gauge_min : 0;
        const clockDateFormat = (tile.sensor_gauge_max !== undefined) ? tile.sensor_gauge_max : 0;
        if (flags & 1) html += '<div class="tile-clock-time" ' + getClockPreviewTextStyle(clockTimeFont, 40, '#fff') + '>' + getClockPreviewTime(clockTimeFormat) + '</div>';
        if (flags & 2) html += '<div class="tile-clock-date" ' + getClockPreviewTextStyle(clockDateFont, 24, '#fff') + '>' + getClockPreviewDate(clockDateFormat) + '</div>';
      }
      if (previewKind === 'text') {
        const textValue = tile.text_value || tile.scene_alias || tile.key_macro || '';
        if (textValue) {
          const textClass = getSensorValueFontClass(tile.sensor_value_font);
          html += '<div class="tile-text ' + textClass + '">' +
            escapeHtml(textValue) + '</div>';
        }
      }
      if (previewKind === 'switch' && tile.switch_style === 1) {
        html += '<div class="tile-switch" id="' + tab + '-tile-' + index + '-switch"><div class="tile-switch-knob"></div></div>';
      }
      html += getTileResizeHandlesHtml(typeValue);
      el.innerHTML = html;
    }
    if (currentTileTab === tab && currentTileIndex === index) el.classList.add('active');
    if (typeValue === '5' && tile.sensor_entity) {
      const state = parseSwitchPayload(metaValues[tile.sensor_entity] ?? '');
      applySwitchPreviewState(el, state);
    }
  }

  function fetchTileGridData(tab, force = false) {
    if (!force && tileDataLoadedTabs.has(tab)) {
      return Promise.resolve(getTilesData(tab));
    }
    if (tileDataLoadPromises[tab]) return tileDataLoadPromises[tab];
    const folderId = getFolderIdForTab(tab);
    if (folderId === undefined) return Promise.resolve([]);

    tileDataLoadPromises[tab] = fetch(
      '/api/tiles?folder=' + encodeURIComponent(folderId))
      .then(async response => {
        if (!response.ok) throw new Error('Tiles HTTP ' + response.status);
        const tiles = await response.json();
        if (!Array.isArray(tiles)) throw new Error('Invalid tile grid response');
        tilesData[tab] = tiles;
        tileDataLoadedTabs.add(tab);
        return tiles;
      })
      .finally(() => { delete tileDataLoadPromises[tab]; });
    return tileDataLoadPromises[tab];
  }

  function loadSensorValues(
      refreshTiles = false, forceMetaFetch = false, tabsOverride = null) {
    if (dragSource || resizeState) {
      queueDeferredSensorRefresh(refreshTiles);
      return Promise.resolve(false);
    }
    const requestedTabs = Array.isArray(tabsOverride)
      ? tabsOverride
      : (refreshTiles
          ? (currentTileTab ? [currentTileTab] : tileTabs.slice(0, 1))
          : (currentTileTab && tileDataLoadedTabs.has(currentTileTab)
              ? [currentTileTab]
              : []));
    const tabs = Array.from(new Set(requestedTabs)).filter(tab =>
      tileTabs.includes(tab) && getFolderIdForTab(tab) !== undefined);
    const tileRequests = refreshTiles
      ? tabs.map(tab => fetchTileGridData(tab, true))
      : tabs.map(tab => Promise.resolve(getTilesData(tab)));

    return Promise.all([fetchSensorMetaCache(forceMetaFetch), ...tileRequests])
    .then(results => {
      // A refresh may have started shortly before the drag and only arrive
      // during it. In that case it must not overwrite the local preview with the
      // old device state.
      if (dragSource || resizeState) {
        queueDeferredSensorRefresh(refreshTiles);
        return;
      }
      const sensorMeta = normalizeSensorMetaPayload(results[0] || {});
      sensorMetaCache = sensorMeta;
      tabs.forEach((tab, idx) => {
        const tiles = Array.isArray(results[idx + 1]) ? results[idx + 1] : [];
        if (refreshTiles) {
          tilesData[tab] = tiles;
        }
        const tilesForRender = refreshTiles ? tiles : getTilesData(tab);
        if (!Array.isArray(tilesForRender)) return;
        tilesForRender.forEach((tile, i) => renderTileFromData(tab, i, tile, sensorMeta));
        layoutTiles(tab, tilesForRender);
      });
      if (currentTileIndex !== -1 && currentTileTab) {
        restoreCurrentTileSelectionUi();
      } else if (!isScreensaverTileTab(currentTileTab)) {
        restoreSelectedTileState();
      }
      return true;
    })
    .catch(err => {
      console.error('Sensor values load failed:', err);
      return false;
    });
  }

  let dragSource = null;
  let dragPreview = null;
  let dragPlaceholder = null;
  let resizeState = null;
  let resizePlaceholder = null;
  let deferredSensorRefresh = false;
  let deferredSensorRefreshTiles = false;

  function queueDeferredSensorRefresh(refreshTiles = false) {
    deferredSensorRefresh = true;
    deferredSensorRefreshTiles = deferredSensorRefreshTiles || refreshTiles;
  }

  function clearDeferredSensorRefresh() {
    deferredSensorRefresh = false;
    deferredSensorRefreshTiles = false;
  }

  function flushDeferredSensorRefresh() {
    if (!deferredSensorRefresh || dragSource || resizeState) return;
    const refreshTiles = deferredSensorRefreshTiles;
    clearDeferredSensorRefresh();
    loadSensorValues(refreshTiles, true);
  }

  function createDragPreview(tile) {
    const clone = tile.cloneNode(true);
    const rect = tile.getBoundingClientRect();
    clone.style.position = 'absolute';
    clone.style.top = '-9999px';
    clone.style.left = '-9999px';
    clone.style.width = rect.width + 'px';
    clone.style.height = rect.height + 'px';
    clone.style.opacity = '0.9';
    clone.style.pointerEvents = 'none';
    clone.style.boxShadow = '0 10px 30px rgba(0,0,0,0.35)';
    clone.style.backgroundClip = 'padding-box';
    clone.style.clipPath = 'inset(0 round 11px)';
    clone.style.display = 'block';
    document.body.appendChild(clone);
    return clone;
  }

  function getTileGrid(tab) {
    return document.querySelector('#tab-tiles-' + tab + ' .tile-grid');
  }

  function parseGridTrackSizes(value) {
    return String(value || '')
      .split(' ')
      .map(part => parseFloat(part))
      .filter(part => !isNaN(part) && part > 0);
  }

  function getGridElementMetrics(grid, columns, rows) {
    if (!grid) return null;
    const style = window.getComputedStyle(grid);
    const rect = grid.getBoundingClientRect();
    const gapX = parseFloat(style.columnGap || style.gap || '0') || 0;
    const gapY = parseFloat(style.rowGap || style.gap || '0') || 0;
    const padLeft = parseFloat(style.paddingLeft || '0') || 0;
    const padTop = parseFloat(style.paddingTop || '0') || 0;
    const padRight = parseFloat(style.paddingRight || '0') || 0;
    const padBottom = parseFloat(style.paddingBottom || '0') || 0;
    const cols = parseGridTrackSizes(style.gridTemplateColumns);
    const gridRows = parseGridTrackSizes(style.gridTemplateRows);
    const columnCount = Math.max(1, Number(columns) || cols.length || 1);
    const rowCount = Math.max(1, Number(rows) || gridRows.length || 1);
    const cellW = cols.length
      ? cols[0]
      : ((rect.width - padLeft - padRight -
          (gapX * (columnCount - 1))) / columnCount);
    const cellH = gridRows.length
      ? gridRows[0]
      : ((rect.height - padTop - padBottom -
          (gapY * (rowCount - 1))) / rowCount);
    // With align-content:space-between (Climate slots) the effective row
    // spacing exceeds the nominal gap. Derive it from the leftover area so the
    // pointer-to-cell mapping is correct there too; identical for 1fr grids.
    let effGapX = gapX;
    let effGapY = gapY;
    if (columnCount > 1 && isFinite(cellW)) {
      effGapX = Math.max(gapX,
        (rect.width - padLeft - padRight - (columnCount * cellW)) /
        (columnCount - 1));
    }
    if (rowCount > 1 && isFinite(cellH)) {
      effGapY = Math.max(gapY,
        (rect.height - padTop - padBottom - (rowCount * cellH)) /
        (rowCount - 1));
    }
    return {
      rect, gapX: effGapX, gapY: effGapY, padLeft, padTop,
      cellW, cellH, columns: columnCount, rows: rowCount
    };
  }

  function getGridElementCellFromPointer(
      grid, columns, rows, clientX, clientY) {
    const metrics = getGridElementMetrics(grid, columns, rows);
    if (!metrics) return null;
    const stepX = metrics.cellW + metrics.gapX;
    const stepY = metrics.cellH + metrics.gapY;
    let relX = clientX - metrics.rect.left - metrics.padLeft;
    let relY = clientY - metrics.rect.top - metrics.padTop;
    if (!isFinite(relX) || !isFinite(relY)) return null;
    relX = Math.max(0, relX);
    relY = Math.max(0, relY);
    const col = Math.max(
      0, Math.min(
        metrics.columns - 1,
        Math.floor((relX + (metrics.gapX / 2)) / stepX)));
    const row = Math.max(
      0, Math.min(
        metrics.rows - 1,
        Math.floor((relY + (metrics.gapY / 2)) / stepY)));
    return { col, row };
  }

  function getTileGridMetrics(tab) {
    const grid = getTileGrid(tab);
    return getGridElementMetrics(grid, GRID_COLS, GRID_ROWS);
  }

  function getRawGridCellFromPointer(tab, clientX, clientY) {
    const metrics = getTileGridMetrics(tab);
    if (!metrics) return null;
    const stepX = metrics.cellW + metrics.gapX;
    const stepY = metrics.cellH + metrics.gapY;
    let relX = clientX - metrics.rect.left - metrics.padLeft;
    let relY = clientY - metrics.rect.top - metrics.padTop;
    if (!isFinite(relX) || !isFinite(relY)) return null;
    relX = Math.max(0, relX);
    relY = Math.max(0, relY);
    let col = Math.floor((relX + (metrics.gapX / 2)) / stepX);
    let row = Math.floor((relY + (metrics.gapY / 2)) / stepY);
    if (!isFinite(col)) col = 0;
    if (!isFinite(row)) row = 0;
    if (col < 0) col = 0;
    const firstRow = firstAllowedGridRow(tab);
    if (row < firstRow) row = firstRow;
    if (col >= GRID_COLS) col = GRID_COLS - 1;
    if (row >= GRID_ROWS) row = GRID_ROWS - 1;
    return { col, row };
  }

  function getGridCellFromPointer(tab, clientX, clientY) {
    const rawCell = getRawGridCellFromPointer(tab, clientX, clientY);
    if (!rawCell) return null;
    if (!dragSource || dragSource.tab !== tab) return rawCell;
    const anchorCol = clampInt(dragSource.grabCellCol, 0, GRID_COLS - 1, 0);
    const anchorRow = clampInt(dragSource.grabCellRow, 0, GRID_ROWS - 1, 0);
    return {
      col: rawCell.col - anchorCol,
      row: rawCell.row - anchorRow
    };
  }

  function getTileLayoutFromData(tab, index) {
    const tiles = getTilesData(tab);
    if (!Array.isArray(tiles) || index < 0 || index >= tiles.length) return null;
    return normalizeTileLayout(tiles[index], index, tab);
  }

  function getDragSourceLayout() {
    if (!dragSource) return null;
    return dragSource.layout ||
      getTileElementLayout(dragSource.tab, dragSource.index) ||
      getTileLayoutFromData(dragSource.tab, dragSource.index);
  }

  function getDragAnchorCell(tab, layout, clientX, clientY) {
    const rawCell = getRawGridCellFromPointer(tab, clientX, clientY);
    if (!layout || !rawCell) return { col: 0, row: 0 };
    const col = clampInt(rawCell.col - layout.col, 0, Math.max(0, layout.span_w - 1), 0);
    const row = clampInt(rawCell.row - layout.row, 0, Math.max(0, layout.span_h - 1), 0);
    return { col, row };
  }

  function getDragAnchorOffset(tab, layout, grabCellCol, grabCellRow, tileRect) {
    const metrics = getTileGridMetrics(tab);
    const rect = tileRect || { width: 0, height: 0 };
    if (!layout || !metrics) {
      return {
        x: Math.max(0, (rect.width / 2) || 0),
        y: Math.max(0, (rect.height / 2) || 0)
      };
    }
    const x = (grabCellCol * (metrics.cellW + metrics.gapX)) + (metrics.cellW / 2);
    const y = (grabCellRow * (metrics.cellH + metrics.gapY)) + (metrics.cellH / 2);
    const maxX = Math.max(0, rect.width - 1);
    const maxY = Math.max(0, rect.height - 1);
    return {
      x: Math.max(0, Math.min(maxX, x)),
      y: Math.max(0, Math.min(maxY, y))
    };
  }

  function cloneLayout(layout) {
    if (!layout) return null;
    return {
      col: layout.col,
      row: layout.row,
      span_w: layout.span_w,
      span_h: layout.span_h
    };
  }

  function captureLayoutSnapshot(tab) {
    const tiles = getTilesData(tab);
    const count = Math.max(Array.isArray(tiles) ? tiles.length : 0, GRID_COLS * GRID_ROWS);
    const snapshot = [];
    for (let i = 0; i < count; i++) {
      const layout = getTileElementLayout(tab, i) || getTileLayoutFromData(tab, i);
      snapshot[i] = cloneLayout(layout);
    }
    return snapshot;
  }

  function clearReflowPreviewClasses(tab) {
    document.querySelectorAll('#tab-tiles-' + tab + ' .tile').forEach(tile => {
      tile.classList.remove('reflow-preview');
    });
  }

  function layoutsEqual(a, b) {
    if (!a || !b) return false;
    return a.col === b.col &&
           a.row === b.row &&
           a.span_w === b.span_w &&
           a.span_h === b.span_h;
  }

  function restoreDragPreview(tab) {
    if (!dragSource || dragSource.tab !== tab || !Array.isArray(dragSource.baseLayouts)) {
      clearReflowPreviewClasses(tab);
      return;
    }
    const tiles = getTilesData(tab);
    dragSource.previewResult = null;
    dragSource.appliedPreviewResult = null;
    dragSource.previewKey = '';
    for (let i = 0; i < dragSource.baseLayouts.length; i++) {
      const tile = Array.isArray(tiles) ? tiles[i] : null;
      if (!tile || Number(tile.type || 0) === 0) continue;
      const el = document.getElementById(tab + '-tile-' + i);
      const layout = dragSource.baseLayouts[i];
      if (!el || !layout) continue;
      setTileGridPosition(el, layout.col, layout.row, layout.span_w, layout.span_h);
    }
    clearReflowPreviewClasses(tab);
  }

  function applyDragPreviewLayouts(tab, previewResult) {
    if (!dragSource || dragSource.tab !== tab || !previewResult || !Array.isArray(previewResult.layouts)) return;
    const tiles = getTilesData(tab);
    for (let i = 0; i < previewResult.layouts.length; i++) {
      const tile = Array.isArray(tiles) ? tiles[i] : null;
      if (!tile || Number(tile.type || 0) === 0) continue;
      const el = document.getElementById(tab + '-tile-' + i);
      if (!el) continue;

      const baseLayout = dragSource.baseLayouts && dragSource.baseLayouts[i] ? dragSource.baseLayouts[i] : null;
      const previewLayout = previewResult.layouts[i] || baseLayout;
      if (i === dragSource.index) {
        if (previewLayout) {
          setTileGridPosition(el, previewLayout.col, previewLayout.row, previewLayout.span_w, previewLayout.span_h);
        } else if (baseLayout) {
          setTileGridPosition(el, baseLayout.col, baseLayout.row, baseLayout.span_w, baseLayout.span_h);
        }
        el.classList.remove('reflow-preview');
        continue;
      }
      if (!previewLayout) continue;
      setTileGridPosition(el, previewLayout.col, previewLayout.row, previewLayout.span_w, previewLayout.span_h);

      const changed = !!(baseLayout &&
        (baseLayout.col !== previewLayout.col || baseLayout.row !== previewLayout.row));
      el.classList.toggle('reflow-preview', changed);
    }
    dragSource.appliedPreviewResult = previewResult;
  }

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
        candidateLayout.span_w < 1 ||
        candidateLayout.span_h < 1 ||
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
      spanW, spanH, preferredCol, preferredRow) {
    const candidates = [];
    for (let row = firstRow; row < rows; row++) {
      for (let col = 0; col < columns; col++) {
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
      targetCol, targetRow, columns, rows, firstRow = 0) {
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
        preferredCol, preferredRow);
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
      GRID_COLS, GRID_ROWS, firstAllowedGridRow(tab));
  }

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
    const rawCell = getRawGridCellFromPointer(tab, clientX, clientY);
    if (!layout || !rawCell) return null;

    let spanW = layout.span_w;
    let spanH = layout.span_h;
    const tiles = getTilesData(tab);
    const tile = Array.isArray(tiles) && currentTileIndex >= 0
      ? tiles[currentTileIndex] : null;
    const typeValue = document.getElementById(tab + '_tile_type')?.value ?? tile?.type ?? 0;
    const isMedia = Number(typeValue) === MEDIA_TILE_TYPE;
    const minW = isMedia ? Math.min(MEDIA_TILE_MIN_SPAN, GRID_COLS) : 1;
    const minH = isMedia ? Math.min(MEDIA_TILE_MIN_SPAN, GRID_ROWS) : 1;
    const maxW = isMedia
      ? Math.min(MEDIA_TILE_MAX_SPAN, GRID_COLS - layout.col)
      : GRID_COLS - layout.col;
    const maxH = isMedia
      ? Math.min(MEDIA_TILE_MAX_SPAN, GRID_ROWS - layout.row)
      : GRID_ROWS - layout.row;
    if (String(direction || '').includes('e')) {
      spanW = clampInt(rawCell.col - layout.col + 1, minW, maxW, layout.span_w);
    }
    if (String(direction || '').includes('s')) {
      spanH = clampInt(rawCell.row - layout.row + 1, minH, maxH, layout.span_h);
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

    const targetCol = clampInt(col, 0, GRID_COLS - 1, sourceLayout.col);
    const targetRow = clampInt(row, firstAllowedGridRow(tab), GRID_ROWS - 1, sourceLayout.row);
    const fits = (targetCol + sourceLayout.span_w <= GRID_COLS) &&
                 (targetRow + sourceLayout.span_h <= GRID_ROWS);
    const spanW = Math.max(1, Math.min(sourceLayout.span_w, GRID_COLS - targetCol));
    const spanH = Math.max(1, Math.min(sourceLayout.span_h, GRID_ROWS - targetRow));

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
    const targetCol = clampInt(cell.col, 0, GRID_COLS - 1, sourceLayout.col);
    const targetRow = clampInt(cell.row, firstAllowedGridRow(tab), GRID_ROWS - 1, sourceLayout.row);
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
    const targetCol = clampInt(cell.col, 0, GRID_COLS - 1, sourceLayout.col);
    const targetRow = clampInt(cell.row, firstAllowedGridRow(tab), GRID_ROWS - 1, sourceLayout.row);
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
      tile.col = layout.col;
      tile.row = layout.row;
    }

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
      tile.col = saved.col;
      tile.row = saved.row;
    }
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
    let col = parseInt(targetCol, 10);
    let row = parseInt(targetRow, 10);
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
      const row = parseInt(tile.style.gridRowStart, 10);
      const col = parseInt(tile.style.gridColumnStart, 10);
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

  let screensaverDraft = null;
  let screensaverLoaded = false;
  let screensaverLoading = false;
  let screensaverSelected = { kind: 'background', index: -1 };
  let screensaverWallpaperIndex = -1;
  let screensaverSaveTimer = null;
  const screensaverTimeFontSizes = [20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96];
  const screensaverDateFontSizes = [20, 24, 28, 32, 40, 48, 56, 64, 72];

  function invalidateScreensaverEditor() {
    screensaverLoaded = false;
    screensaverLoading = false;
    screensaverDraft = null;
    screensaverWallpaperIndex = -1;
  }

  function ssClamp(value, min, max) {
    const n = Number(value);
    return Math.max(min, Math.min(max, Number.isFinite(n) ? n : min));
  }

  function ssNearestClockFont(value, dateLine = false) {
    const wanted = Number(value) || 20;
    const sizes = dateLine ? screensaverDateFontSizes : screensaverTimeFontSizes;
    return sizes.reduce((best, size) =>
      Math.abs(size - wanted) < Math.abs(best - wanted) ? size : best,
      sizes[0]);
  }

  function ssClockAlignment(value) {
    return Math.round(ssClamp(value ?? 1, 0, 2));
  }

  function ssClockAlignmentCss(value) {
    return ['left', 'center', 'right'][ssClockAlignment(value)];
  }

  function getScreensaverClockPreviewDate(d) {
    if (!d) return '';
    const weekdayNames = getClockPreviewLanguage().toLowerCase().startsWith('de')
      ? ['Sonntag', 'Montag', 'Dienstag', 'Mittwoch', 'Donnerstag', 'Freitag', 'Samstag']
      : ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
    let text = d.show_date ? getClockPreviewDate(d.date_format) : '';
    if (d.show_weekday) {
      text = weekdayNames[new Date().getDay()] + (text ? ', ' + text : '');
    }
    return text;
  }

  function ssNormalizeLoaded(data) {
    data.time_font_size = ssNearestClockFont(data.time_font_size || 48, false);
    data.date_font_size = ssNearestClockFont(data.date_font_size || 28, true);
    data.time_alignment = ssClockAlignment(data.time_alignment);
    data.date_alignment = ssClockAlignment(data.date_alignment);
    data.tile_border = data.tile_border !== false;
    data.wallpapers = Array.isArray(data.wallpapers) ? data.wallpapers : [];
    data.duration_seconds = Math.round(ssClamp(
      data.duration_seconds ?? 15, 3, 3600));
    const configured = new Map(data.wallpapers.map(item => [item.file_name, item]));
    const hadConfiguredWallpapers = data.wallpapers.length > 0;
    (data.available_wallpapers || []).forEach(name => {
      if (!configured.has(name)) {
        data.wallpapers.push({
          file_name: name, enabled: !hadConfiguredWallpapers,
          focus_x: 500, focus_y: 500, zoom: 1000
        });
      }
    });
    screensaverWallpaperIndex = data.wallpapers.findIndex(w => w.enabled);
    if (screensaverWallpaperIndex < 0 && data.wallpapers.length) screensaverWallpaperIndex = 0;
    return data;
  }

  function initScreensaverEditor() {
    if (screensaverLoaded || screensaverLoading) {
      if (screensaverLoaded) renderScreensaverEditor();
      return;
    }
    screensaverLoading = true;
    fetch('/api/screensaver').then(r => r.json()).then(config => {
      if (!config || !config.success) throw new Error('screensaver config');
      screensaverDraft = ssNormalizeLoaded(config);
      screensaverLoaded = true;
      bindScreensaverEditor();
      selectScreensaverBackground();
      renderScreensaverEditor();
    }).catch(() => showNotification(t('screensaverLoadFailed'), false))
      .finally(() => { screensaverLoading = false; });
  }

  function ssPayload() {
    const d = screensaverDraft;
    return {
      version: 2,
      use_wallpapers: !!d.use_wallpapers,
      shuffle: !!d.shuffle,
      tile_shadow: !!d.tile_shadow,
      tile_border: d.tile_border !== false,
      show_time: !!d.show_time,
      show_date: !!d.show_date,
      show_weekday: !!d.show_weekday,
      clock_shadow: !!d.clock_shadow,
      time_format: Number(d.time_format || 0),
      date_format: Number(d.date_format || 0),
      time_alignment: ssClockAlignment(d.time_alignment),
      date_alignment: ssClockAlignment(d.date_alignment),
      time_font_size: ssNearestClockFont(d.time_font_size || 48, false),
      date_font_size: ssNearestClockFont(d.date_font_size || 28, true),
      clock_x: Math.round(ssClamp(d.clock_x, 0, 1000)),
      clock_y: Math.round(ssClamp(d.clock_y, 0, 1000)),
      duration_seconds: Math.round(ssClamp(d.duration_seconds, 3, 3600)),
      preview_wallpaper: ssCurrentWallpaper()?.file_name || '',
      wallpapers: d.wallpapers.map(w => ({
        file_name: w.file_name, enabled: !!w.enabled,
        focus_x: Math.round(ssClamp(w.focus_x, 0, 1000)),
        focus_y: Math.round(ssClamp(w.focus_y, 0, 1000)),
        zoom: Math.round(ssClamp(w.zoom, 1000, 3000))
      }))
    };
  }

  function scheduleScreensaverSave() {
    if (!screensaverLoaded) return;
    window.clearTimeout(screensaverSaveTimer);
    screensaverSaveTimer = window.setTimeout(saveScreensaverNow, 650);
  }

  function saveScreensaverNow() {
    if (!screensaverLoaded) return;
    fetch('/api/screensaver', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(ssPayload())
    }).then(async response => {
      const result = await response.json().catch(() => ({}));
      if (!response.ok || !result.success) throw new Error(result.error || 'save');
      showNotification(t('screensaverSaved'));
    }).catch(() => {
      showNotification(t('screensaverSaveFailed'), false);
    });
  }

  function ssCurrentWallpaper() {
    if (!screensaverDraft || screensaverWallpaperIndex < 0) return null;
    return screensaverDraft.wallpapers[screensaverWallpaperIndex] || null;
  }

  function selectScreensaverBackground() {
    screensaverSelected = { kind: 'background', index: -1 };
    const bg = document.getElementById('screensaverBackgroundSettings');
    if (bg) bg.classList.remove('hidden');
    document.getElementById('screensaverClockSettings')?.classList.add('hidden');
    document.getElementById('screensaverClock')?.classList.remove('selected-clock');
    const settings = document.getElementById('screensaverSettings');
    settings?.querySelector('.tile-specific-settings')?.classList.add('hidden');
    document.querySelectorAll('#tab-tiles-screensaver .tile').forEach(tile => {
      tile.classList.remove('active');
      delete tile.dataset.selected;
    });
    document.getElementById('screensaverGrid')?.classList.add('selected-background');
    currentTileIndex = -1;
    currentTileTab = 'screensaver';
    renderScreensaverEditor();
  }

  function selectScreensaverClock() {
    screensaverSelected = { kind: 'clock', index: -1 };
    document.getElementById('screensaverBackgroundSettings')?.classList.add('hidden');
    document.getElementById('screensaverClockSettings')?.classList.remove('hidden');
    document.getElementById('screensaverGrid')?.classList.remove('selected-background');
    const settings = document.getElementById('screensaverSettings');
    settings?.querySelector('.tile-specific-settings')?.classList.add('hidden');
    document.querySelectorAll('#tab-tiles-screensaver .tile').forEach(tile => {
      tile.classList.remove('active');
      delete tile.dataset.selected;
    });
    currentTileIndex = -1;
    currentTileTab = 'screensaver';
    renderScreensaverEditor();
  }

  function renderScreensaverWallpapers() {
    const list = document.getElementById('screensaverWallpaperList');
    if (!list || !screensaverDraft) return;
    list.innerHTML = '';
    screensaverDraft.wallpapers.forEach((wallpaper, index) => {
      const row = document.createElement('div');
      row.className = 'screensaver-wallpaper-row' + (index === screensaverWallpaperIndex ? ' active' : '');
      const enabled = document.createElement('input');
      enabled.type = 'checkbox'; enabled.checked = !!wallpaper.enabled;
      enabled.addEventListener('change', () => {
        wallpaper.enabled = enabled.checked;
        screensaverWallpaperIndex = index;
        renderScreensaverEditor(); scheduleScreensaverSave();
      });
      const name = document.createElement('div');
      name.className = 'screensaver-wallpaper-name'; name.textContent = wallpaper.file_name;
      name.addEventListener('click', () => {
        screensaverWallpaperIndex = index;
        renderScreensaverEditor();
      });
      // Same chevron graphic as the arrow of the select fields.
      const chevronSvg = dir =>
        '<svg width="12" height="8" viewBox="0 0 12 8" aria-hidden="true"><path d="' +
        (dir < 0 ? 'M1 6.5l5-5 5 5' : 'M1 1.5l5 5 5-5') +
        '" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>';
      const up = document.createElement('button');
      up.type = 'button'; up.className = 'screensaver-wallpaper-move'; up.innerHTML = chevronSvg(-1);
      up.setAttribute('aria-label', t('moveUp'));
      up.title = t('moveUp');
      up.disabled = index === 0;
      up.addEventListener('click', () => {
        if (index === 0) return;
        [screensaverDraft.wallpapers[index - 1], screensaverDraft.wallpapers[index]] =
          [screensaverDraft.wallpapers[index], screensaverDraft.wallpapers[index - 1]];
        screensaverWallpaperIndex = index - 1; renderScreensaverEditor(); scheduleScreensaverSave();
      });
      const down = document.createElement('button');
      down.type = 'button'; down.className = 'screensaver-wallpaper-move'; down.innerHTML = chevronSvg(1);
      down.setAttribute('aria-label', t('moveDown'));
      down.title = t('moveDown');
      down.disabled = index === screensaverDraft.wallpapers.length - 1;
      down.addEventListener('click', () => {
        if (down.disabled) return;
        [screensaverDraft.wallpapers[index + 1], screensaverDraft.wallpapers[index]] =
          [screensaverDraft.wallpapers[index], screensaverDraft.wallpapers[index + 1]];
        screensaverWallpaperIndex = index + 1; renderScreensaverEditor(); scheduleScreensaverSave();
      });
      row.append(enabled, name, up, down); list.appendChild(row);
    });
    if (!screensaverDraft.wallpapers.length) {
      const empty = document.createElement('div');
      empty.className = 'screensaver-save-state';
      empty.textContent = t('screensaverNoWallpapers');
      list.appendChild(empty);
    }
  }

  function renderScreensaverEditor() {
    if (!screensaverDraft) return;
    const d = screensaverDraft;
    const preview = document.getElementById('screensaverGrid');
    const image = document.getElementById('screensaverPreviewImage');
    const clock = document.getElementById('screensaverClock');
    if (!preview || !image || !clock) return;
    preview.classList.toggle('selected-background', screensaverSelected.kind === 'background');
    clock.classList.toggle('selected-clock', screensaverSelected.kind === 'clock');
    const width = preview.getBoundingClientRect().width || 800;
    const scale = width / Number(d.screen_width || 1280);
    const rootStyles = getComputedStyle(document.documentElement);
    const devicePx = (name, fallback) => {
      const value = parseFloat(rootStyles.getPropertyValue(name));
      return Number.isFinite(value) && value > 0 ? value : fallback;
    };
    const deviceClockFontPx = (raw, fallback) => {
      const requested = Number(raw || fallback);
      return devicePx('--screensaver-fs' + requested, requested);
    };
    clock.style.setProperty(
      '--screensaver-clock-shadow-2',
      (devicePx('--screensaver-shadow-2', 2) * scale) + 'px');
    clock.style.setProperty(
      '--screensaver-clock-shadow-4',
      (devicePx('--screensaver-shadow-4', 4) * scale) + 'px');
    clock.style.setProperty(
      '--screensaver-clock-shadow-6',
      (devicePx('--screensaver-shadow-6', 6) * scale) + 'px');
    const wallpaper = ssCurrentWallpaper();
    if (d.use_wallpapers && wallpaper && wallpaper.file_name) {
      const wanted = '/api/screensaver/wallpaper?name=' + encodeURIComponent(wallpaper.file_name);
      if (image.dataset.src !== wanted) { image.src = wanted; image.dataset.src = wanted; }
      image.hidden = false;
      image.style.display = 'block';
      image.style.objectPosition = (wallpaper.focus_x / 10) + '% ' + (wallpaper.focus_y / 10) + '%';
      image.style.transformOrigin = (wallpaper.focus_x / 10) + '% ' + (wallpaper.focus_y / 10) + '%';
      image.style.transform = 'scale(' + (wallpaper.zoom / 1000) + ')';
    } else {
      image.hidden = true;
      image.style.display = 'none';
      image.removeAttribute('src');
      delete image.dataset.src;
    }
    clock.style.left = (d.clock_x / 10) + '%';
    clock.style.top = (d.clock_y / 10) + '%';
    const time = document.getElementById('screensaverClockTime');
    const date = document.getElementById('screensaverClockDate');
    time.hidden = !d.show_time;
    date.hidden = !d.show_date && !d.show_weekday;
    time.style.fontSize =
      Math.max(10, deviceClockFontPx(d.time_font_size, 48) * scale) + 'px';
    date.style.fontSize =
      Math.max(8, deviceClockFontPx(d.date_font_size, 28) * scale) + 'px';
    time.textContent = getClockPreviewTime(d.time_format);
    date.textContent = getScreensaverClockPreviewDate(d);
    time.style.width = 'auto';
    date.style.width = 'auto';
    time.style.textAlign = ssClockAlignmentCss(d.time_alignment);
    date.style.textAlign = ssClockAlignmentCss(d.date_alignment);
    // Both lines take the width of the longer line, so the alignment in the
    // browser matches the compact LVGL clock container.
    const clockLineWidth = Math.ceil(Math.max(
      time.hidden ? 0 : time.getBoundingClientRect().width,
      date.hidden ? 0 : date.getBoundingClientRect().width));
    if (!time.hidden && clockLineWidth) time.style.width = clockLineWidth + 'px';
    if (!date.hidden && clockLineWidth) date.style.width = clockLineWidth + 'px';
    clock.hidden = false;
    clock.classList.toggle('clock-disabled', !d.show_time && !d.show_date && !d.show_weekday);
    clock.classList.toggle('clock-shadowed', !!d.clock_shadow);
    preview.classList.toggle('tiles-shadowed', !!d.tile_shadow);
    preview.classList.toggle('tiles-bordered', d.tile_border !== false);
    document.getElementById('screensaverUseWallpapers').checked = !!d.use_wallpapers;
    document.getElementById('screensaverShuffle').checked = !!d.shuffle;
    document.getElementById('screensaverTileShadow').checked = !!d.tile_shadow;
    document.getElementById('screensaverTileBorder').checked = d.tile_border !== false;
    document.getElementById('screensaverShowTime').checked = !!d.show_time;
    document.getElementById('screensaverShowDate').checked = !!d.show_date;
    document.getElementById('screensaverShowWeekday').checked = !!d.show_weekday;
    document.getElementById('screensaverClockShadow').checked = !!d.clock_shadow;
    document.getElementById('screensaverTimeFont').value = String(d.time_font_size || 48);
    document.getElementById('screensaverDateFont').value = String(d.date_font_size || 28);
    document.getElementById('screensaverTimeAlignment').value = String(ssClockAlignment(d.time_alignment));
    document.getElementById('screensaverDateAlignment').value = String(ssClockAlignment(d.date_alignment));
    document.getElementById('screensaverTimeFormat').value = String(d.time_format || 0);
    document.getElementById('screensaverDateFormat').value = String(d.date_format || 0);
    const controls = document.getElementById('screensaverWallpaperControls');
    controls.hidden = !wallpaper;
    if (wallpaper) {
      document.getElementById('screensaverWallpaperDuration').value = d.duration_seconds;
      document.getElementById('screensaverWallpaperZoom').value = wallpaper.zoom;
      document.getElementById('screensaverFocusX').value = wallpaper.focus_x;
      document.getElementById('screensaverFocusY').value = wallpaper.focus_y;
    }
    renderScreensaverWallpapers();
  }

  function bindScreensaverEditor() {
    const preview = document.getElementById('screensaverGrid');
    const clock = document.getElementById('screensaverClock');
    const clockResize = clock?.querySelector('.screensaver-clock-resize-handle');
    if (!preview || preview.dataset.bound === '1') return;
    preview.dataset.bound = '1';
    preview.addEventListener('click', e => {
      if (!e.target.closest('.tile') && !e.target.closest('#screensaverClock')) {
        selectScreensaverBackground();
      }
    });
    let backgroundDrag = null;
    preview.addEventListener('pointerdown', e => {
      if (e.target.closest('.tile') || e.target.closest('#screensaverClock')) return;
      selectScreensaverBackground();
      const wallpaper = ssCurrentWallpaper();
      if (!wallpaper) return;
      backgroundDrag = { id: e.pointerId, x: e.clientX, y: e.clientY,
                         fx: Number(wallpaper.focus_x), fy: Number(wallpaper.focus_y) };
      preview.setPointerCapture(e.pointerId);
    });
    preview.addEventListener('pointermove', e => {
      if (!backgroundDrag || backgroundDrag.id !== e.pointerId) return;
      const wallpaper = ssCurrentWallpaper(); const rect = preview.getBoundingClientRect();
      wallpaper.focus_x = Math.round(ssClamp(backgroundDrag.fx - (e.clientX - backgroundDrag.x) * 1000 / rect.width, 0, 1000));
      wallpaper.focus_y = Math.round(ssClamp(backgroundDrag.fy - (e.clientY - backgroundDrag.y) * 1000 / rect.height, 0, 1000));
      renderScreensaverEditor();
    });
    const finishBackgroundDrag = e => {
      if (!backgroundDrag || backgroundDrag.id !== e.pointerId) return;
      backgroundDrag = null; scheduleScreensaverSave();
    };
    preview.addEventListener('pointerup', finishBackgroundDrag);
    preview.addEventListener('pointercancel', finishBackgroundDrag);
    let clockDrag = null;
    clock.addEventListener('pointerdown', e => {
      if (e.target.closest('.screensaver-clock-resize-handle')) return;
      e.stopPropagation();
      const clockRect = clock.getBoundingClientRect();
      clockDrag = {
        id: e.pointerId,
        offsetX: e.clientX - (clockRect.left + clockRect.width / 2),
        offsetY: e.clientY - (clockRect.top + clockRect.height / 2)
      };
      clock.setPointerCapture(e.pointerId);
      clock.classList.add('dragging'); selectScreensaverClock();
    });
    clock.addEventListener('pointermove', e => {
      if (!clockDrag || clockDrag.id !== e.pointerId) return;
      const rect = preview.getBoundingClientRect();
      const centerX = e.clientX - clockDrag.offsetX;
      const centerY = e.clientY - clockDrag.offsetY;
      screensaverDraft.clock_x = Math.round(ssClamp((centerX - rect.left) * 1000 / rect.width, 0, 1000));
      screensaverDraft.clock_y = Math.round(ssClamp((centerY - rect.top) * 1000 / rect.height, 0, 1000));
      renderScreensaverEditor();
    });
    const finishClockDrag = e => {
      if (!clockDrag || clockDrag.id !== e.pointerId) return;
      clockDrag = null; clock.classList.remove('dragging'); scheduleScreensaverSave();
    };
    clock.addEventListener('pointerup', finishClockDrag);
    clock.addEventListener('pointercancel', finishClockDrag);
    clock.addEventListener('click', e => {
      e.stopPropagation();
      selectScreensaverClock();
    });

    let clockResizeDrag = null;
    if (clockResize) {
      clockResize.addEventListener('pointerdown', e => {
        e.preventDefault();
        e.stopPropagation();
        selectScreensaverClock();
        const rect = clock.getBoundingClientRect();
        clockResizeDrag = {
          id: e.pointerId,
          x: e.clientX,
          y: e.clientY,
          width: Math.max(1, rect.width),
          height: Math.max(1, rect.height),
          timeFont: Number(screensaverDraft.time_font_size || 48),
          dateFont: Number(screensaverDraft.date_font_size || 28)
        };
        clockResize.setPointerCapture(e.pointerId);
      });
      clockResize.addEventListener('pointermove', e => {
        if (!clockResizeDrag || clockResizeDrag.id !== e.pointerId) return;
        const widthFactor = (clockResizeDrag.width + e.clientX - clockResizeDrag.x) /
                            clockResizeDrag.width;
        const heightFactor = (clockResizeDrag.height + e.clientY - clockResizeDrag.y) /
                             clockResizeDrag.height;
        const factor = ssClamp(Math.max(widthFactor, heightFactor), 0.35, 3.0);
        screensaverDraft.time_font_size = ssNearestClockFont(
          clockResizeDrag.timeFont * factor, false);
        screensaverDraft.date_font_size = ssNearestClockFont(
          clockResizeDrag.dateFont * factor, true);
        renderScreensaverEditor();
      });
      const finishClockResize = e => {
        if (!clockResizeDrag || clockResizeDrag.id !== e.pointerId) return;
        clockResizeDrag = null;
        scheduleScreensaverSave();
      };
      clockResize.addEventListener('pointerup', finishClockResize);
      clockResize.addEventListener('pointercancel', finishClockResize);
    }

    const bind = (id, event, fn, save = true) => {
      const element = document.getElementById(id); if (!element) return;
      element.addEventListener(event, () => { fn(element); renderScreensaverEditor(); if (save) scheduleScreensaverSave(); });
    };
    bind('screensaverUseWallpapers', 'change', el => { screensaverDraft.use_wallpapers = el.checked; });
    bind('screensaverShuffle', 'change', el => { screensaverDraft.shuffle = el.checked; });
    bind('screensaverTileShadow', 'change', el => { screensaverDraft.tile_shadow = el.checked; });
    bind('screensaverTileBorder', 'change', el => { screensaverDraft.tile_border = el.checked; });
    bind('screensaverShowTime', 'change', el => { screensaverDraft.show_time = el.checked; });
    bind('screensaverShowDate', 'change', el => { screensaverDraft.show_date = el.checked; });
    bind('screensaverShowWeekday', 'change', el => { screensaverDraft.show_weekday = el.checked; });
    bind('screensaverClockShadow', 'change', el => { screensaverDraft.clock_shadow = el.checked; });
    bind('screensaverTimeFont', 'change', el => {
      screensaverDraft.time_font_size = ssNearestClockFont(el.value, false);
    });
    bind('screensaverDateFont', 'change', el => {
      screensaverDraft.date_font_size = ssNearestClockFont(el.value, true);
    });
    bind('screensaverTimeAlignment', 'change', el => {
      screensaverDraft.time_alignment = ssClockAlignment(el.value);
    });
    bind('screensaverDateAlignment', 'change', el => {
      screensaverDraft.date_alignment = ssClockAlignment(el.value);
    });
    bind('screensaverTimeFormat', 'change', el => { screensaverDraft.time_format = Number(el.value); });
    bind('screensaverDateFormat', 'change', el => { screensaverDraft.date_format = Number(el.value); });
    bind('screensaverWallpaperDuration', 'change', el => {
      screensaverDraft.duration_seconds = Math.round(ssClamp(el.value, 3, 3600));
    });
    bind('screensaverWallpaperZoom', 'input', el => { const w = ssCurrentWallpaper(); if (w) w.zoom = Number(el.value); }, false);
    bind('screensaverWallpaperZoom', 'change', el => { const w = ssCurrentWallpaper(); if (w) w.zoom = Number(el.value); });
    bind('screensaverFocusX', 'input', el => { const w = ssCurrentWallpaper(); if (w) w.focus_x = Number(el.value); }, false);
    bind('screensaverFocusX', 'change', el => { const w = ssCurrentWallpaper(); if (w) w.focus_x = Number(el.value); });
    bind('screensaverFocusY', 'input', el => { const w = ssCurrentWallpaper(); if (w) w.focus_y = Number(el.value); }, false);
    bind('screensaverFocusY', 'change', el => { const w = ssCurrentWallpaper(); if (w) w.focus_y = Number(el.value); });

    window.addEventListener('resize', perFrame(() => {
      if (screensaverLoaded) renderScreensaverEditor();
    }));
  }

  let hardwareIoModel = null;
  let hardwareIoLoaded = false;
  let hardwareIoLoading = false;
  let hardwareIoBound = false;
  let hardwareIoSaving = false;
  let hardwareIoDirty = false;
  let hardwareIoEditVersion = 0;
  let hardwareIoEntityRefreshTimers = [];

  function setHardwareIoSaveState(text, state = '') {
    const el = document.getElementById('hardwareIoSaveState');
    if (!el) return;
    el.textContent = text || '';
    el.className = 'hardware-io-save-state' + (state ? ' ' + state : '');
  }

  function updateHardwareIoSaveActions() {
    const save = document.getElementById('hardwareIoSave');
    const restart = document.getElementById('hardwareIoRestart');
    if (save) save.disabled = hardwareIoSaving || !hardwareIoDirty;
    if (restart) restart.disabled = hardwareIoSaving;
  }

  function markHardwareIoDirty() {
    hardwareIoEditVersion++;
    hardwareIoDirty = true;
    setHardwareIoSaveState(t('ioUnsavedChanges'));
    updateHardwareIoSaveActions();
  }

  function hardwareIoSupportsPin(pin, type) {
    if (!hardwareIoModel || !Array.isArray(hardwareIoModel.pin_options)) return false;
    const option = hardwareIoModel.pin_options.find(item => Number(item.gpio) === Number(pin));
    if (!option || !option[type]) return false;
    return !option.requires_variant ||
      option.requires_variant === hardwareIoModel.board_variant;
  }

  function hardwareIoUnusedPins(type, exceptIndex = -1, includeOtherVariants = false) {
    if (!hardwareIoModel || !Array.isArray(hardwareIoModel.pin_options)) return [];
    const used = new Set((hardwareIoModel.channels || [])
      .map((channel, index) => index === exceptIndex ? null : Number(channel.gpio))
      .filter(pin => Number.isFinite(pin)));
    return hardwareIoModel.pin_options.filter(option => {
      if (!option || !option[type] || used.has(Number(option.gpio))) return false;
      return includeOtherVariants || hardwareIoSupportsPin(option.gpio, type);
    });
  }

  function syncHardwareIoBoardVariant() {
    const selectedVariant = (hardwareIoModel?.channels || []).map(channel =>
      (hardwareIoModel.pin_options || []).find(option =>
        Number(option.gpio) === Number(channel.gpio))?.requires_variant || '')
      .find(Boolean);
    hardwareIoModel.board_variant = selectedVariant || 'standard';
  }

  function nextHardwareIoId(type) {
    const prefix = type === 'temperature' ? 'temperature_' : 'switch_';
    const existing = new Set((hardwareIoModel?.channels || []).map(channel => String(channel.id || '')));
    for (let i = 1; i < 100; i++) {
      const candidate = prefix + i;
      if (!existing.has(candidate)) return candidate;
    }
    return prefix + Date.now().toString(36);
  }

  function hardwareIoAsciiSlug(value) {
    let slug = String(value || '').replace(/[A-Z]/g, character => character.toLowerCase());
    slug = slug.replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
    return slug;
  }

  function hardwareIoDeviceSlug() {
    const slug = hardwareIoAsciiSlug(
      hardwareIoModel?.entity_prefix || hardwareIoModel?.device_name || 'panel');
    return slug || 'panel';
  }

  function hardwareIoNameSlug(channel) {
    const slug = hardwareIoAsciiSlug(channel?.name || '');
    return slug || String(channel?.id || 'channel');
  }

  function hardwareIoLocalEntityId(channel) {
    const domain = channel?.type === 'temperature' ? 'sensor.' : 'switch.';
    return domain + hardwareIoDeviceSlug() + '_' + hardwareIoNameSlug(channel);
  }

  function updateHardwareIoActions() {
    const channels = hardwareIoModel?.channels || [];
    const atLimit = channels.length >= Number(hardwareIoModel?.max_channels || 8);
    const addSwitch = document.getElementById('hardwareIoAddSwitch');
    const addTemperature = document.getElementById('hardwareIoAddTemperature');
    const onboardSwitchAvailable = (hardwareIoModel.pin_options || []).some(option =>
      option.onboard && option.relay &&
      !channels.some(channel => Number(channel.gpio) === Number(option.gpio)));
    if (addSwitch) {
      addSwitch.disabled = atLimit ||
        (!onboardSwitchAvailable && hardwareIoUnusedPins('relay').length === 0);
    }
    if (addTemperature) {
      addTemperature.disabled = atLimit || hardwareIoUnusedPins('temperature').length === 0;
    }
    updateHardwareIoSaveActions();
  }

  function createHardwareIoField(labelText, control, fieldClass = '') {
    const field = document.createElement('div');
    field.className = 'hardware-io-field' + (fieldClass ? ' ' + fieldClass : '');
    const label = document.createElement('label');
    label.textContent = labelText;
    field.appendChild(label);
    field.appendChild(control);
    return field;
  }

  function makeHardwareIoSelect(options, value) {
    const select = document.createElement('select');
    options.forEach(item => {
      const option = document.createElement('option');
      option.value = String(item.value);
      option.textContent = item.label;
      select.appendChild(option);
    });
    select.value = String(value);
    return select;
  }

  function makeHardwareIoToggle(options, value, onChange) {
    const group = document.createElement('div');
    group.className = 'hardware-io-toggle';
    group.setAttribute('role', 'group');
    const buttons = [];
    const activate = nextValue => {
      buttons.forEach(button => {
        const active = button.dataset.value === String(nextValue);
        button.classList.toggle('active', active);
        button.setAttribute('aria-pressed', active ? 'true' : 'false');
      });
    };
    options.forEach(item => {
      const button = document.createElement('button');
      button.type = 'button';
      button.dataset.value = String(item.value);
      button.textContent = item.label;
      button.disabled = !!item.disabled;
      button.addEventListener('click', () => {
        activate(item.value);
        onChange(item.value);
      });
      buttons.push(button);
      group.appendChild(button);
    });
    activate(value);
    return group;
  }

  function renderHardwareIoCard(channel, index) {
    const card = document.createElement('div');
    card.className = 'hardware-io-card hardware-io-card-' +
      (channel.type === 'temperature' ? 'temperature' : 'switch');

    const header = document.createElement('div');
    header.className = 'hardware-io-card-header';
    const typeLabel = document.createElement('div');
    typeLabel.className = 'hardware-io-card-type';
    typeLabel.textContent = channel.type === 'temperature'
      ? t('ioTemperature') : t('ioSwitch');
    const idPreview = document.createElement('div');
    idPreview.className = 'hardware-io-card-id';
    idPreview.textContent = hardwareIoLocalEntityId(channel);
    header.append(typeLabel, idPreview);
    card.appendChild(header);

    const fields = document.createElement('div');
    fields.className = 'hardware-io-fields';
    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.maxLength = 48;
    nameInput.required = true;
    nameInput.value = channel.name || '';
    nameInput.placeholder = channel.type === 'temperature'
      ? t('ioTemperature') : t('ioSwitch');
    nameInput.addEventListener('input', () => {
      channel.name = nameInput.value;
      idPreview.textContent = hardwareIoLocalEntityId(channel);
      markHardwareIoDirty();
    });
    fields.appendChild(createHardwareIoField(
      t('ioName'), nameInput, 'hardware-io-field-name'));

    const availablePins = hardwareIoUnusedPins(channel.type, index);
    if (channel.type === 'relay') {
      hardwareIoUnusedPins(channel.type, index, true).forEach(option => {
        if (!availablePins.some(existing => Number(existing.gpio) === Number(option.gpio))) {
          availablePins.push(option);
        }
      });
    }
    const selectedOption = (hardwareIoModel.pin_options || []).find(option =>
      Number(option.gpio) === Number(channel.gpio) && option[channel.type]);
    if (selectedOption && !availablePins.some(option => Number(option.gpio) === Number(channel.gpio))) {
      availablePins.unshift(selectedOption);
    }
    const gpioSelect = makeHardwareIoSelect(
      availablePins.length
        ? availablePins.map(option => ({value: option.gpio, label: option.label || ('GPIO ' + option.gpio)}))
        : [{value: -1, label: t('ioNoFreeGpio')}],
      channel.gpio);
    gpioSelect.disabled = availablePins.length === 0;
    gpioSelect.addEventListener('change', () => {
      channel.gpio = Number(gpioSelect.value);
      syncHardwareIoBoardVariant();
      renderHardwareIo();
      markHardwareIoDirty();
    });
    fields.appendChild(createHardwareIoField(
      t('ioGpio'), gpioSelect, 'hardware-io-field-gpio'));

    if (channel.type === 'relay') {
      const pinDescriptor = (hardwareIoModel.pin_options || []).find(option =>
        Number(option.gpio) === Number(channel.gpio));
      const fixedOutputLogic = String(pinDescriptor?.fixed_output_logic || '');
      if (fixedOutputLogic === 'high') channel.inverted = false;
      if (fixedOutputLogic === 'low') channel.inverted = true;
      let logicControl;
      if (fixedOutputLogic) {
        logicControl = document.createElement('div');
        logicControl.className = 'hardware-io-fixed-value';
        logicControl.textContent = fixedOutputLogic === 'low'
          ? t('ioActiveLow') : t('ioActiveHigh');
      } else {
        logicControl = makeHardwareIoToggle([
          {value: 'high', label: t('ioHigh')},
          {value: 'low', label: t('ioLow')}
        ], channel.inverted ? 'low' : 'high', value => {
          channel.inverted = value === 'low';
          markHardwareIoDirty();
        });
      }
      fields.appendChild(createHardwareIoField(
        t('ioOutputLogic'), logicControl, 'hardware-io-field-logic'));

      const boot = makeHardwareIoToggle([
        {value: 'off', label: t('ioOff')},
        {value: 'on', label: t('ioOn')}
      ], channel.boot_state || 'off', value => {
        channel.boot_state = value;
        markHardwareIoDirty();
      });
      fields.appendChild(createHardwareIoField(
        t('ioAfterRestart'), boot, 'hardware-io-field-boot'));
    } else {
      const precision = makeHardwareIoSelect([
        {value: 0, label: t('ioDecimalsZero')},
        {value: 1, label: t('ioDecimalOne')},
        {value: 2, label: t('ioDecimalsTwo')},
        {value: 3, label: t('ioDecimalsThree')}
      ], channel.precision ?? 1);
      precision.addEventListener('change', () => {
        channel.precision = Number(precision.value);
        markHardwareIoDirty();
      });
      fields.appendChild(createHardwareIoField(
        t('ioPrecision'), precision, 'hardware-io-field-precision'));
    }

    const remove = document.createElement('button');
    remove.type = 'button';
    remove.className = 'hardware-io-delete mdi mdi-delete-outline';
    remove.title = t('ioRemoveAssignment');
    remove.addEventListener('click', () => {
      if (!window.confirm(tf('ioRemoveConfirm', {
        name: channel.name || channel.id
      }))) return;
      hardwareIoModel.channels.splice(index, 1);
      syncHardwareIoBoardVariant();
      renderHardwareIo();
      markHardwareIoDirty();
    });
    fields.appendChild(remove);
    card.appendChild(fields);
    return card;
  }

  function renderHardwareIo() {
    const list = document.getElementById('hardwareIoList');
    if (!list || !hardwareIoModel) return;
    list.innerHTML = '';
    const channels = hardwareIoModel.channels || [];
    if (!channels.length) {
      const empty = document.createElement('div');
      empty.className = 'hardware-io-empty';
      empty.textContent = (hardwareIoModel.pin_options || []).length
        ? t('ioEmpty') : t('ioNoProfile');
      list.appendChild(empty);
    } else {
      channels.forEach((channel, index) => list.appendChild(renderHardwareIoCard(channel, index)));
    }
    updateHardwareIoActions();
  }

  function addHardwareIoChannel(type) {
    if (!hardwareIoModel) return;
    const channels = hardwareIoModel.channels || (hardwareIoModel.channels = []);
    if (channels.length >= Number(hardwareIoModel.max_channels || 8)) return;
    let pin = null;
    if (type === 'relay') {
      pin = hardwareIoUnusedPins(type)[0] || hardwareIoUnusedPins(type, -1, true)[0] || null;
    }
    if (!pin) pin = hardwareIoUnusedPins(type)[0];
    if (!pin) {
      setHardwareIoSaveState(t('ioNoCompatibleGpio'), 'error');
      return;
    }
    const sequence = channels.filter(channel => channel.type === type).length + 1;
    channels.push({
      id: nextHardwareIoId(type),
      name: (type === 'temperature' ? t('ioTemperature') : t('ioSwitch')) +
        ' ' + sequence,
      type,
      gpio: Number(pin.gpio),
      inverted: pin.fixed_output_logic === 'low',
      boot_state: 'off',
      precision: 1
    });
    syncHardwareIoBoardVariant();
    renderHardwareIo();
    markHardwareIoDirty();
  }

  function bindHardwareIo() {
    if (hardwareIoBound) return;
    hardwareIoBound = true;
    document.getElementById('hardwareIoAddSwitch')?.addEventListener('click', () => {
      addHardwareIoChannel('relay');
    });
    document.getElementById('hardwareIoAddTemperature')?.addEventListener('click', () => {
      addHardwareIoChannel('temperature');
    });
    document.getElementById('hardwareIoSave')?.addEventListener('click', () => {
      saveHardwareIoNow();
    });
    document.getElementById('hardwareIoRestart')?.addEventListener('click', () => {
      const confirmKey = hardwareIoDirty
        ? 'ioRestartUnsavedConfirm' : 'restartConfirm';
      if (!window.confirm(t(confirmKey))) return;
      restartHardwareIoNow();
    });
  }

  function scheduleHardwareIoEntityOptionsRefresh() {
    hardwareIoEntityRefreshTimers.forEach(timer => window.clearTimeout(timer));
    hardwareIoEntityRefreshTimers = [0, 2500, 7500].map(delay => window.setTimeout(() => {
      fetchEntityOptions(true).then(data => {
        tileTabs.forEach(tab => {
          rebuildEntitySelect(tab + '_sensor_entity', data.sensors);
          rebuildEntitySelect(tab + '_switch_entity', data.switches);
        });
        fetchSensorMetaCache(true);
      }).catch(() => {});
    }, delay));
  }

  function restartHardwareIoNow() {
    setHardwareIoSaveState(t('ioRestarting'), 'saving');
    const restartForm = document.getElementById('admin_restart_form');
    if (restartForm) {
      window.setTimeout(() => restartForm.submit(), 100);
      return;
    }
    fetch('/restart', {method: 'POST'}).catch(() => {});
  }

  async function saveHardwareIoNow() {
    if (!hardwareIoModel || hardwareIoSaving) return false;
    if (!hardwareIoDirty) return true;
    if ((hardwareIoModel.channels || []).some(channel =>
      !String(channel.name || '').trim())) {
      setHardwareIoSaveState(t('ioNameRequired'), 'error');
      return false;
    }
    hardwareIoSaving = true;
    updateHardwareIoSaveActions();
    const saveVersion = hardwareIoEditVersion;
    const channels = (hardwareIoModel.channels || []).map(channel => ({
      id: String(channel.id || ''),
      name: String(channel.name || '').trim(),
      type: channel.type === 'temperature' ? 'temperature' : 'relay',
      gpio: Number(channel.gpio),
      inverted: !!channel.inverted,
      boot_state: channel.boot_state === 'on' ? 'on' : 'off',
      precision: Math.max(0, Math.min(3, Number(channel.precision ?? 1)))
    }));
    setHardwareIoSaveState(t('ioSaving'), 'saving');
    let saved = false;
    try {
      const response = await fetch('/api/hardware-io', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          board_variant: hardwareIoModel.board_variant || 'standard',
          channels
        })
      });
      const result = await response.json().catch(() => ({}));
      if (!response.ok || !result.success) {
        console.error('Hardware I/O save rejected:', result.error || response.status);
        throw new Error(t('saveFailed'));
      }
      if (saveVersion === hardwareIoEditVersion) {
        hardwareIoDirty = false;
        setHardwareIoSaveState(t('ioSaved'), 'saved');
        saved = true;
      } else {
        setHardwareIoSaveState(t('ioUnsavedChanges'));
      }
      scheduleHardwareIoEntityOptionsRefresh();
    } catch (error) {
      console.error('Hardware I/O save failed:', error);
      if (saveVersion === hardwareIoEditVersion) {
        setHardwareIoSaveState(t('saveFailed'), 'error');
      }
    } finally {
      hardwareIoSaving = false;
      updateHardwareIoSaveActions();
    }
    return saved;
  }

  async function initHardwareIo() {
    bindHardwareIo();
    if (hardwareIoLoaded || hardwareIoLoading) {
      if (hardwareIoLoaded) renderHardwareIo();
      return;
    }
    hardwareIoLoading = true;
    try {
      const response = await fetch('/api/hardware-io');
      const data = await response.json();
      if (!response.ok || !data?.success) throw new Error(data?.error || ('HTTP ' + response.status));
      data.channels = Array.isArray(data.channels) ? data.channels : [];
      data.pin_options = Array.isArray(data.pin_options) ? data.pin_options : [];
      data.board_variant = data.board_variant || 'standard';
      hardwareIoModel = data;
      hardwareIoLoaded = true;
      hardwareIoDirty = false;
      renderHardwareIo();
      setHardwareIoSaveState(t('ioSaved'), 'saved');
      updateHardwareIoSaveActions();
    } catch (error) {
      const list = document.getElementById('hardwareIoList');
      if (list) {
        list.innerHTML = '';
        const failed = document.createElement('div');
        failed.className = 'hardware-io-empty';
        failed.textContent = t('ioCouldNotLoad');
        list.appendChild(failed);
      }
      setHardwareIoSaveState(t('ioLoadFailed'), 'error');
    } finally {
      hardwareIoLoading = false;
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    toggleStaticNetworkFields();
    toggleNetworkSettings();
    toggleSettingsAccessFields();
    initSettingsAccessControls();
    initAdminSettingsSave();
    initTileTabs();
    let initialTab = '';
    try { initialTab = localStorage.getItem('activeAdminTab') || ''; } catch (e) {}
    prepareFolderTabSessionCache();
    restoreInitialFolderTabSessionFragment(initialTab);
    loadSelectedTileStates();
    loadDraftsFromStorage();
    loadTileClipboard();
    // Stay on the admin tab that was open before a browser reload. Fall back to
    // Home only when that tab no longer exists.
    const homeTab = tabByFolder[0] || tileTabs[0];
    const initialFolderId = folderIdFromAdminTabName(initialTab);
    const initialTabKnown = initialTab && (
      document.getElementById(initialTab) ||
      (initialFolderId !== null && tabByFolder[initialFolderId]));
    if (initialTabKnown) {
      switchTab(initialTab);
    } else if (homeTab) {
      switchTab('tab-tiles-' + homeTab);
    } else {
      switchTab('tab-network');
    }
    setInterval(() => {
      const activeTab = document.querySelector('.tab-content.active');
      if (!document.hidden && !fileManagerUploadBusy &&
          activeTab && activeTab.classList.contains('tile-tab')) {
        loadSensorValues(false, false);
      }
    }, 15000);
    tileTabs.forEach(tab => {
      enableTileDrag(tab);
      enableTileKeys(tab);
      enableTileResize(tab);
    });
    enableSettingsHiddenSlot();
    associateFieldLabels();
    fillStaticClockPreviews();
    setInterval(fillStaticClockPreviews, 30000);
    updateTileSettingsMaxHeight();
  });

function maybeFillTitleFromSensor(tab) {
    maybeFillTitleFromEntity(tab, '_sensor_entity');
  }

  function formatSensorValue(value, decimals) {
    if (value === undefined || value === null) return '--';
    let text = String(value).trim();
    if (!text.length) return '--';
    const lower = text.toLowerCase();
    if (lower === 'unavailable' || lower === 'unknown' || lower === 'none') return '--';
    if (decimals === undefined || decimals === null || decimals === '' || Number(decimals) === -1) {
      return localizeNumericText(text);
    }
    const num = parseFloat(text.replace(',', '.'));
    if (isNaN(num) || !isFinite(num)) return text;
    const d = Math.max(0, Math.min(6, parseInt(decimals, 10) || 0));
    return formatLocalizedNumber(num, d, false);
  }

  const BINARY_SENSOR_ICON_PAIRS = Object.freeze({
    '': Object.freeze(['radiobox-blank', 'checkbox-marked-circle']),
    battery: Object.freeze(['battery', 'battery-outline']),
    battery_charging: Object.freeze(['battery', 'battery-charging']),
    carbon_monoxide: Object.freeze(['smoke-detector', 'smoke-detector-alert']),
    cold: Object.freeze(['thermometer', 'snowflake']),
    connectivity: Object.freeze(['close-network-outline', 'check-network-outline']),
    door: Object.freeze(['door-closed', 'door-open']),
    garage_door: Object.freeze(['garage', 'garage-open']),
    gas: Object.freeze(['check-circle', 'alert-circle']),
    heat: Object.freeze(['thermometer', 'fire']),
    light: Object.freeze(['brightness-5', 'brightness-7']),
    lock: Object.freeze(['lock', 'lock-open']),
    moisture: Object.freeze(['water-off', 'water']),
    motion: Object.freeze(['motion-sensor-off', 'motion-sensor']),
    moving: Object.freeze(['octagon', 'arrow-right']),
    occupancy: Object.freeze(['home-outline', 'home']),
    opening: Object.freeze(['square', 'square-outline']),
    plug: Object.freeze(['power-plug-off', 'power-plug']),
    power: Object.freeze(['power-plug-off', 'power-plug']),
    presence: Object.freeze(['home-outline', 'home']),
    problem: Object.freeze(['check-circle', 'alert-circle']),
    running: Object.freeze(['stop', 'play']),
    safety: Object.freeze(['check-circle', 'alert-circle']),
    smoke: Object.freeze(['smoke-detector-variant', 'smoke-detector-variant-alert']),
    sound: Object.freeze(['music-note-off', 'music-note']),
    tamper: Object.freeze(['check-circle', 'alert-circle']),
    update: Object.freeze(['package', 'package-up']),
    vibration: Object.freeze(['crop-portrait', 'vibrate']),
    window: Object.freeze(['window-closed', 'window-open'])
  });
  function parseBinarySensorPreviewPayload(value) {
    const out = {
      valid: false,
      state: '',
      available: false,
      deviceClass: '',
      icon: ''
    };
    if (value === undefined || value === null) return out;

    let source = value;
    if (typeof source === 'string') {
      const text = source.trim();
      if (!text.length) return out;
      if (text.startsWith('{')) {
        try {
          source = JSON.parse(text);
        } catch (error) {
          return out;
        }
      } else {
        source = { state: text };
      }
    }
    if (!source || typeof source !== 'object') return out;

    const attrs = source.attributes && typeof source.attributes === 'object'
      ? source.attributes : source;
    const state = String(source.state ?? attrs.state ?? '').trim().toLowerCase();
    if (!['on', 'off', 'unknown', 'unavailable'].includes(state)) return out;

    out.valid = true;
    out.state = state;
    out.available = typeof source.available === 'boolean'
      ? source.available : state !== 'unavailable';
    if (state === 'unavailable') out.available = false;
    out.deviceClass = String(
      source.device_class ?? attrs.device_class ?? '').trim().toLowerCase();
    out.icon = normalizeMdiIconName(source.icon ?? attrs.icon ?? '');
    return out;
  }

  function binarySensorPreviewIconPair(deviceClass) {
    return BINARY_SENSOR_ICON_PAIRS[String(deviceClass || '').toLowerCase()] ||
      BINARY_SENSOR_ICON_PAIRS[''];
  }

  function resolveBinarySensorPreviewIcon(
      rawIcon, entityId, state, metaIcons) {
    if (isExplicitlyDisabledValue(rawIcon)) return '';
    const configured = normalizeMdiIconName(rawIcon);
    if (configured) return configured;

    const stateIcon = normalizeMdiIconName(state?.icon || '');
    if (stateIcon) return stateIcon;
    const entityIcon = entityId && metaIcons
      ? normalizeMdiIconName(metaIcons[entityId]) : '';
    if (entityIcon) return entityIcon;

    const pair = binarySensorPreviewIconPair(state?.deviceClass);
    const active = state?.valid === true && state?.available === true &&
      state?.state === 'on';
    return pair[active ? 1 : 0];
  }

  function binarySensorPreviewColor(state) {
    return state?.valid === true && state?.available === true &&
      state?.state === 'on' ? '#ffc107' : '#9e9e9e';
  }

  function binarySensorPreviewStateText(state) {
    if (!state?.valid) return '--';
    const translations = typeof BINARY_SENSOR_I18N === 'object'
      ? BINARY_SENSOR_I18N : {};
    if (state.available === false || state.state === 'unavailable') {
      return translations.unavailable || '--';
    }
    if (state.state === 'unknown') return translations.unknown || '--';
    const states = translations.states || {};
    const pair = states[state.deviceClass] || states[''] || {};
    return pair[state.state] || translations.unknown || '--';
  }

  function updateSensorValuePreview(tab) {
    if (currentTileIndex === -1) return;
    const prefix = tab;
    const entitySelect = document.getElementById(prefix + '_sensor_entity');
    const unitInput = document.getElementById(prefix + '_sensor_unit');
    const decimalsInput = document.getElementById(prefix + '_sensor_decimals');
    const valueFontSelect = document.getElementById(prefix + '_sensor_value_font');
    if (!entitySelect) return;
    const entity = entitySelect.value;
    if (!entity) {
      const valueElem = document.getElementById(tab + '-tile-' + currentTileIndex + '-value');
      if (valueElem) {
        const unit = resolveUnitValue(unitInput ? unitInput.value : '', '', sensorMetaCache.units);
        valueElem.innerHTML = '--' +
          (unit ? '<span class="tile-unit">' + escapeHtml(unit) + '</span>' : '');
        applySensorValueFontClass(valueElem, valueFontSelect ? valueFontSelect.value : '0');
      }
      return;
    }
    const applyMeta = (meta) => {
      const values = (meta && meta.values) || {};
      const valueElem = document.getElementById(tab + '-tile-' + currentTileIndex + '-value');
      if (valueElem) {
        const decimals = decimalsInput ? decimalsInput.value : '';
        const value = formatSensorValue(values[entity] ?? '--', decimals);
        const unit = resolveUnitValue(unitInput ? unitInput.value : '', entity, (meta && meta.units) || {});
        valueElem.innerHTML = escapeHtml(value) +
          (unit ? '<span class="tile-unit">' + escapeHtml(unit) + '</span>' : '');
        applySensorValueFontClass(valueElem, valueFontSelect ? valueFontSelect.value : '0');
      }
    };
    const metaPromise = isSensorMetaCacheLoaded() ? Promise.resolve(sensorMetaCache) : fetchSensorMetaCache();
    metaPromise
      .then(meta => applyMeta(meta))
      .catch(err => console.error('Sensor value load failed:', err));
  }

  function normalizeSensorValueFont(value) {
    const v = String(value || '0');
    return (['1','2','3','4'].includes(v)) ? v : '0';
  }

  function getSensorValueFontClass(value) {
    const v = normalizeSensorValueFont(value);
    if (v === '1') return 'sensor-value-size-20';
    if (v === '2') return 'sensor-value-size-24';
    if (v === '3') return 'sensor-value-size-32';
    if (v === '4') return 'sensor-value-size-40';
    return 'sensor-value-size-default';
  }

  function applySensorValueFontClass(el, value) {
    if (!el) return;
    el.classList.remove('sensor-value-size-20', 'sensor-value-size-24', 'sensor-value-size-32', 'sensor-value-size-40', 'sensor-value-size-default');
    el.classList.add(getSensorValueFontClass(value));
  }

  function syncGaugeUi(tab) {
    const prefix = tab;
    const typeValue = document.getElementById(prefix + '_tile_type')?.value || '0';
    const gaugeWrap = document.getElementById(prefix + '_sensor_gauge_fields');
    const graphWrap = document.getElementById(prefix + '_sensor_graph_fields');
    if (typeValue !== '1') {
      if (gaugeWrap) gaugeWrap.classList.add('hidden');
      if (graphWrap) graphWrap.classList.add('hidden');
      return;
    }
    const displayMode = document.getElementById(prefix + '_sensor_display_mode')?.value || '0';
    if (gaugeWrap) {
      if (displayMode === '1') gaugeWrap.classList.remove('hidden');
      else gaugeWrap.classList.add('hidden');
    }
    if (graphWrap) {
      if (displayMode === '2') graphWrap.classList.remove('hidden');
      else graphWrap.classList.add('hidden');
    }
  }

  function loadSensorFields(tab, data) {
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_sensor_entity');
    if (entityEl) entityEl.value = data.sensor_entity || '';
    const unitEl = document.getElementById(prefix + '_sensor_unit');
    if (unitEl) unitEl.value = data.sensor_unit || '';
    const decEl = document.getElementById(prefix + '_sensor_decimals');
    if (decEl) decEl.value = (data.sensor_decimals !== undefined && data.sensor_decimals >= 0) ? data.sensor_decimals : '';
    const fontEl = document.getElementById(prefix + '_sensor_value_font');
    if (fontEl) fontEl.value = (data.sensor_value_font !== undefined) ? String(data.sensor_value_font) : '0';
    const popupModeEl = document.getElementById(prefix + '_sensor_popup_open_mode');
    if (popupModeEl) popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    const displayModeEl = document.getElementById(prefix + '_sensor_display_mode');
    if (displayModeEl) displayModeEl.value = (data.sensor_display_mode !== undefined) ? String(data.sensor_display_mode) : '0';
    const gaugeMinEl = document.getElementById(prefix + '_sensor_gauge_min');
    if (gaugeMinEl) gaugeMinEl.value = (data.sensor_gauge_min !== undefined && data.sensor_gauge_min !== null) ? String(data.sensor_gauge_min) : '';
    const gaugeMaxEl = document.getElementById(prefix + '_sensor_gauge_max');
    if (gaugeMaxEl) gaugeMaxEl.value = (data.sensor_gauge_max !== undefined && data.sensor_gauge_max !== null) ? String(data.sensor_gauge_max) : '';
    const gaugeArcEl = document.getElementById(prefix + '_sensor_gauge_arc');
    if (gaugeArcEl) gaugeArcEl.value = (data.sensor_gauge_arc !== undefined && data.sensor_gauge_arc !== null) ? String(data.sensor_gauge_arc) : '';
    const gaugeSizeEl = document.getElementById(prefix + '_sensor_gauge_size');
    if (gaugeSizeEl) gaugeSizeEl.value = (data.sensor_gauge_size !== undefined && data.sensor_gauge_size !== null) ? String(data.sensor_gauge_size) : '';
    const gaugeYOffsetEl = document.getElementById(prefix + '_sensor_gauge_y_offset');
    if (gaugeYOffsetEl) gaugeYOffsetEl.value = (data.sensor_gauge_y_offset !== undefined && data.sensor_gauge_y_offset !== null) ? String(data.sensor_gauge_y_offset) : '';
    const valueYOffsetEl = document.getElementById(prefix + '_sensor_value_y_offset');
    if (valueYOffsetEl) valueYOffsetEl.value = (data.sensor_value_y_offset !== undefined && data.sensor_value_y_offset !== null) ? String(data.sensor_value_y_offset) : '';
    const graphHeightEl = document.getElementById(prefix + '_sensor_graph_height');
    if (graphHeightEl) graphHeightEl.value = (data.sensor_graph_height !== undefined && data.sensor_graph_height !== null) ? String(data.sensor_graph_height) : '';
    syncGaugeUi(tab);
  }

  function saveSensorFields(tab, formData) {
    const prefix = tab;
    formData.append('sensor_entity', document.getElementById(prefix + '_sensor_entity')?.value || '');
    formData.append('sensor_unit', document.getElementById(prefix + '_sensor_unit')?.value || '');
    formData.append('sensor_decimals', document.getElementById(prefix + '_sensor_decimals')?.value || '');
    formData.append('sensor_value_font', document.getElementById(prefix + '_sensor_value_font')?.value || '0');
    formData.append('popup_open_mode', document.getElementById(prefix + '_sensor_popup_open_mode')?.value || '1');
    formData.append('sensor_display_mode', document.getElementById(prefix + '_sensor_display_mode')?.value || '0');
    formData.append('sensor_gauge_min', document.getElementById(prefix + '_sensor_gauge_min')?.value || '');
    formData.append('sensor_gauge_max', document.getElementById(prefix + '_sensor_gauge_max')?.value || '');
    formData.append('sensor_gauge_arc', document.getElementById(prefix + '_sensor_gauge_arc')?.value || '');
    formData.append('sensor_gauge_size', document.getElementById(prefix + '_sensor_gauge_size')?.value || '');
    formData.append('sensor_gauge_y_offset', document.getElementById(prefix + '_sensor_gauge_y_offset')?.value || '');
    formData.append('sensor_value_y_offset', document.getElementById(prefix + '_sensor_value_y_offset')?.value || '');
    formData.append('sensor_graph_height', document.getElementById(prefix + '_sensor_graph_height')?.value || '');
  }

  function resetSensorFields(tab) {
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_sensor_entity');
    if (entityEl) entityEl.value = '';
    const unitEl = document.getElementById(prefix + '_sensor_unit');
    if (unitEl) unitEl.value = '';
    const decEl = document.getElementById(prefix + '_sensor_decimals');
    if (decEl) decEl.value = '';
    const fontEl = document.getElementById(prefix + '_sensor_value_font');
    if (fontEl) fontEl.value = '0';
    const popupModeEl = document.getElementById(prefix + '_sensor_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
    const displayModeEl = document.getElementById(prefix + '_sensor_display_mode');
    if (displayModeEl) displayModeEl.value = '0';
    const gaugeMinEl = document.getElementById(prefix + '_sensor_gauge_min');
    if (gaugeMinEl) gaugeMinEl.value = '';
    const gaugeMaxEl = document.getElementById(prefix + '_sensor_gauge_max');
    if (gaugeMaxEl) gaugeMaxEl.value = '';
    const gaugeArcEl = document.getElementById(prefix + '_sensor_gauge_arc');
    if (gaugeArcEl) gaugeArcEl.value = '';
    const gaugeSizeEl = document.getElementById(prefix + '_sensor_gauge_size');
    if (gaugeSizeEl) gaugeSizeEl.value = '';
    const gaugeYOffsetEl = document.getElementById(prefix + '_sensor_gauge_y_offset');
    if (gaugeYOffsetEl) gaugeYOffsetEl.value = '';
    const valueYOffsetEl = document.getElementById(prefix + '_sensor_value_y_offset');
    if (valueYOffsetEl) valueYOffsetEl.value = '';
    const graphHeightEl = document.getElementById(prefix + '_sensor_graph_height');
    if (graphHeightEl) graphHeightEl.value = '';
  }

  function loadBinarySensorFields(tab, data) {
    const entity = document.getElementById(tab + '_binary_sensor_entity');
    const configured = data.sensor_entity || data.binary_sensor_entity || '';
    if (entity) {
      if (configured) {
        entity.dataset.configuredValue = configured;
        if (!Array.from(entity.options).some(option => option.value === configured)) {
          const option = document.createElement('option');
          option.value = configured;
          option.textContent = configured;
          entity.appendChild(option);
        }
      } else {
        delete entity.dataset.configuredValue;
      }
      entity.value = configured;
    }
    const popup = document.getElementById(
      tab + '_binary_sensor_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
  }

  function saveBinarySensorFields(tab, formData) {
    const entityEl = document.getElementById(tab + '_binary_sensor_entity');
    const entity = entityEl
      ? (entityEl.value || entityEl.dataset.configuredValue || '') : '';
    formData.append('binary_sensor_entity', entity);
    formData.append('sensor_entity', entity);
    const popup = document.getElementById(
      tab + '_binary_sensor_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetBinarySensorFields(tab) {
    const entity = document.getElementById(tab + '_binary_sensor_entity');
    if (entity) {
      entity.value = '';
      delete entity.dataset.configuredValue;
    }
    const popup = document.getElementById(
      tab + '_binary_sensor_popup_open_mode');
    if (popup) popup.value = '1';
  }

function maybeFillTitleFromEnergy(tab) {
    maybeFillTitleFromEntity(tab, '_energy_entity');
  }

  function updateEnergyValuePreview(tab) {
    if (currentTileIndex === -1) return;
    const prefix = tab;
    const entitySelect = document.getElementById(prefix + '_energy_entity');
    const unitInput = document.getElementById(prefix + '_energy_unit');
    const decimalsInput = document.getElementById(prefix + '_energy_decimals');
    const valueFontSelect = document.getElementById(prefix + '_energy_value_font');
    if (!entitySelect) return;
    const entity = entitySelect.value;
    const valueElem = document.getElementById(tab + '-tile-' + currentTileIndex + '-value');
    if (!valueElem) return;
    const applyMeta = (meta) => {
      const values = (meta && meta.values) || {};
      const decimals = decimalsInput ? decimalsInput.value : '1';
      const value = entity ? formatSensorValue(values[entity] ?? '--', decimals) : '--';
      const unit = resolveUnitValue(unitInput ? unitInput.value : '', entity, (meta && meta.units) || {});
      valueElem.innerHTML = escapeHtml(value) +
        (unit && value !== '--'
          ? '<span class="tile-unit">' + escapeHtml(unit) + '</span>' : '');
      applySensorValueFontClass(valueElem, valueFontSelect ? valueFontSelect.value : '0');
    };
    const metaPromise = isSensorMetaCacheLoaded() ? Promise.resolve(sensorMetaCache) : fetchSensorMetaCache();
    metaPromise
      .then(meta => applyMeta(meta))
      .catch(err => console.error('Energy value load failed:', err));
  }

  function loadEnergyFields(tab, data) {
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_energy_entity');
    if (entityEl) {
      const configuredEntity = data.sensor_entity || data.energy_entity || '';
      entityEl.dataset.configuredValue = configuredEntity;
      entityEl.value = configuredEntity;
      if (configuredEntity && entityEl.value !== configuredEntity) {
        const opt = document.createElement('option');
        opt.value = configuredEntity;
        opt.textContent = configuredEntity;
        entityEl.appendChild(opt);
        entityEl.value = configuredEntity;
      }
    }
    const unitEl = document.getElementById(prefix + '_energy_unit');
    if (unitEl) unitEl.value = data.sensor_unit || '';
    const decEl = document.getElementById(prefix + '_energy_decimals');
    if (decEl) decEl.value = (data.sensor_decimals !== undefined && data.sensor_decimals >= 0) ? data.sensor_decimals : '1';
    const fontEl = document.getElementById(prefix + '_energy_value_font');
    if (fontEl) fontEl.value = (data.sensor_value_font !== undefined) ? String(data.sensor_value_font) : '0';
    const popupModeEl = document.getElementById(prefix + '_energy_popup_open_mode');
    if (popupModeEl) popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    const valueYOffsetEl = document.getElementById(prefix + '_energy_value_y_offset');
    if (valueYOffsetEl) valueYOffsetEl.value = (data.sensor_value_y_offset !== undefined && data.sensor_value_y_offset !== null) ? String(data.sensor_value_y_offset) : '';
    maybeFillTitleFromEnergy(tab);
  }

  function saveEnergyFields(tab, formData) {
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_energy_entity');
    const entity = entityEl ? (entityEl.value || entityEl.dataset.configuredValue || '') : '';
    formData.append('energy_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('sensor_unit', document.getElementById(prefix + '_energy_unit')?.value || '');
    formData.append('sensor_decimals', document.getElementById(prefix + '_energy_decimals')?.value || '1');
    formData.append('sensor_value_font', document.getElementById(prefix + '_energy_value_font')?.value || '0');
    formData.append('popup_open_mode', document.getElementById(prefix + '_energy_popup_open_mode')?.value || '1');
    formData.append('sensor_value_y_offset', document.getElementById(prefix + '_energy_value_y_offset')?.value || '');
  }

  function resetEnergyFields(tab) {
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_energy_entity');
    if (entityEl) entityEl.value = '';
    const unitEl = document.getElementById(prefix + '_energy_unit');
    if (unitEl) unitEl.value = '';
    const decEl = document.getElementById(prefix + '_energy_decimals');
    if (decEl) decEl.value = '1';
    const fontEl = document.getElementById(prefix + '_energy_value_font');
    if (fontEl) fontEl.value = '0';
    const popupModeEl = document.getElementById(prefix + '_energy_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
    const valueYOffsetEl = document.getElementById(prefix + '_energy_value_y_offset');
    if (valueYOffsetEl) valueYOffsetEl.value = '';
  }

function maybeFillTitleFromWeather(tab) {
    maybeFillTitleFromEntity(tab, '_weather_entity');
  }

  function loadWeatherFields(tab, data) {
    const prefix = tab;
    const el = document.getElementById(prefix + '_weather_entity');
    if (el) el.value = data.sensor_entity || data.weather_entity || '';
    const popupModeEl = document.getElementById(prefix + '_weather_popup_open_mode');
    if (popupModeEl) popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    maybeFillTitleFromWeather(tab);
  }

  function saveWeatherFields(tab, formData) {
    const prefix = tab;
    formData.append('weather_entity', document.getElementById(prefix + '_weather_entity')?.value || '');
    formData.append('popup_open_mode', document.getElementById(prefix + '_weather_popup_open_mode')?.value || '1');
  }

  function resetWeatherFields(tab) {
    const prefix = tab;
    const el = document.getElementById(prefix + '_weather_entity');
    if (el) el.value = '';
    const popupModeEl = document.getElementById(prefix + '_weather_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
  }

function maybeFillTitleFromScene(tab) {
    const prefix = tab;
    const typeSel = document.getElementById(prefix + '_tile_type');
    const titleInput = document.getElementById(prefix + '_tile_title');
    const sceneSel = document.getElementById(prefix + '_scene_alias');
    if (!typeSel || !titleInput || !sceneSel) return;
    if (typeSel.value !== '2') return;
    if (titleInput.value && titleInput.value.trim().length) return;
    const opt = sceneSel.selectedOptions && sceneSel.selectedOptions[0];
    if (!opt) return;
    const label = opt.textContent || opt.innerText || '';
    const title = label.split(' - ')[0] || opt.value || '';
    if (title.trim().length) titleInput.value = title.trim();
  }

  function loadSceneFields(tab, data) {
    const prefix = tab;
    const sceneEl = document.getElementById(prefix + '_scene_alias');
    if (sceneEl) sceneEl.value = data.scene_alias || '';
    maybeFillTitleFromScene(tab);
  }

  function saveSceneFields(tab, formData) {
    const prefix = tab;
    formData.append('scene_alias', document.getElementById(prefix + '_scene_alias')?.value || '');
  }

  function resetSceneFields(tab) {
    const prefix = tab;
    const sceneEl = document.getElementById(prefix + '_scene_alias');
    if (sceneEl) sceneEl.value = '';
  }

function normalizeIconName(value) {
    let icon = String(value || '').trim().toLowerCase();
    if (icon.startsWith('mdi:')) icon = icon.substring(4);
    else if (icon.startsWith('mdi-')) icon = icon.substring(4);
    return icon;
  }
  function formatFolderLabel(name, folderId) {
    let label = String(name || '').trim();
    if (!label.length) label = t('folderPrefix') + folderId;
    return label;
  }
  function updateFolderTabUi(folderId, name, icon) {
    if (folderId === undefined || folderId === null) return;
    const folderNum = parseInt(folderId, 10);
    if (isNaN(folderNum)) return;
    const label = formatFolderLabel(name, folderNum);
    const iconName = normalizeIconName(icon);
    const tabId = tabByFolder[folderNum];
    if (tabId) {
      const tabEl = document.getElementById('tab-tiles-' + tabId);
      if (tabEl) {
        tabEl.dataset.folderName = label;
        tabEl.dataset.folderIcon = iconName;
      }
      const btn = document.querySelector(
        '.folder-tab-btn[data-folder-id="' + folderNum + '"]') ||
        Array.from(document.querySelectorAll('.tab-btn')).find(
          b => b.dataset.tabTarget === 'tab-tiles-' + tabId);
      if (btn) {
        btn.dataset.folderName = label;
        btn.dataset.folderIcon = iconName;
        const labelEl = btn.querySelector('span');
        if (labelEl) labelEl.textContent = label;
        let iconEl = btn.querySelector('i.mdi');
        if (iconName) {
          if (!iconEl) {
            iconEl = document.createElement('i');
            iconEl.className = 'mdi';
            iconEl.style.fontSize = '24px';
            if (labelEl) btn.insertBefore(iconEl, labelEl);
            else btn.appendChild(iconEl);
          }
          iconEl.className = 'mdi mdi-' + iconName;
          iconEl.style.fontSize = '24px';
        } else if (iconEl) {
          iconEl.remove();
        }
      }
    }
    document.querySelectorAll('select[id$="_navigate_target"]').forEach(select => {
      const opt = select.querySelector('option[value="' + folderNum + '"]');
      if (opt) opt.textContent = label;
    });
  }

  function loadNavigateFields(tab, data) {
    const prefix = tab;
    const toggle = document.getElementById(prefix + '_folder_pin_enabled');
    const input = document.getElementById(prefix + '_folder_pin');
    const status = document.getElementById(prefix + '_folder_pin_status');
    if (toggle) toggle.checked = data?.folder_pin_enabled === true;
    if (input) {
      input.value = String(data?.folder_pin || '');
      input.type = 'password';
      const showButton = input.closest('.password-field')
        ?.querySelector('.password-toggle');
      if (showButton) {
        showButton.textContent = showButton.dataset.labelShow || '';
      }
    }
    if (status) {
      status.textContent = data?.folder_pin_enabled === true
        ? navigateText('folderPinSaved')
        : '';
    }
    syncFolderPinControls(tab);
  }

  function saveNavigateFields(tab, formData) {
    const prefix = tab;
    const navEl = document.getElementById(prefix + '_navigate_target');
    if (navEl) {
      formData.append('navigate_target', navEl.value || '0');
    }
  }

  function resetNavigateFields(tab) {
    const prefix = tab;
    const toggle = document.getElementById(prefix + '_folder_pin_enabled');
    const input = document.getElementById(prefix + '_folder_pin');
    const status = document.getElementById(prefix + '_folder_pin_status');
    if (toggle) toggle.checked = false;
    if (input) {
      input.value = '';
      input.type = 'password';
    }
    if (status) status.textContent = '';
    syncFolderPinControls(tab);
  }

  function syncFolderPinControls(tab) {
    const fields = document.getElementById(tab + '_navigate_fields');
    const toggle = document.getElementById(tab + '_folder_pin_enabled');
    const input = document.getElementById(tab + '_folder_pin');
    const label = fields?.querySelector('.folder-pin-label');
    const control = fields?.querySelector('.folder-pin-control');
    const status = document.getElementById(tab + '_folder_pin_status');
    const type = Number(document.getElementById(tab + '_tile_type')?.value || 0);
    const tile = tilesData?.[tab]?.[currentTileIndex];
    const tileEl = document.getElementById(tab + '-tile-' + currentTileIndex);
    const targetSelect = document.getElementById(tab + '_navigate_target');
    const folderId = Number(
      targetSelect?.value ?? tile?.navigate_target ??
      tileEl?.dataset.navigateTarget ?? 0);
    const isFolderTile = type === 4 && Number.isInteger(folderId) && folderId > 0;
    if (fields) fields.classList.toggle('is-hidden', !isFolderTile);

    const showPinEditor = isFolderTile && toggle?.checked === true;
    label?.classList.toggle('is-hidden', !showPinEditor);
    control?.classList.toggle('is-hidden', !showPinEditor);
    status?.classList.toggle('is-hidden', !showPinEditor);
    if (!input) return;
    input.disabled = !showPinEditor;
    if (!showPinEditor) input.value = '';
  }

  function navigateText(key) {
    return typeof NAVIGATE_I18N === 'object' && NAVIGATE_I18N?.[key]
      ? NAVIGATE_I18N[key]
      : t('unknownError');
  }

  async function applyFolderPin(tab) {
    const prefix = tab;
    const toggle = document.getElementById(prefix + '_folder_pin_enabled');
    const input = document.getElementById(prefix + '_folder_pin');
    const button = document.getElementById(prefix + '_folder_pin_apply');
    const status = document.getElementById(prefix + '_folder_pin_status');
    const tile = tilesData?.[tab]?.[currentTileIndex];
    const tileEl = document.getElementById(tab + '-tile-' + currentTileIndex);
    const folderId = Number(
      tile?.navigate_target ?? tileEl?.dataset.navigateTarget ?? 0);
    if (!Number.isInteger(folderId) || folderId <= 0) {
      showNotification(navigateText('folderPinCreateFirst'), false);
      return;
    }

    if (button) button.disabled = true;
    if (status) status.textContent = '';
    try {
      const body = new URLSearchParams();
      body.set('folder_id', String(folderId));
      body.set('enabled', toggle?.checked ? '1' : '0');
      body.set('pin', input?.value || '');
      const response = await fetch('/api/folders/access', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
        body
      });
      const result = await response.json().catch(() => ({}));
      if (!response.ok || !result.success) {
        throw new Error(result.error || navigateText('folderPinSaveFailed'));
      }
      const enabled = result.pin_enabled === true;
      const storedPin = enabled ? String(result.folder_pin || '') : '';
      if (toggle) toggle.checked = enabled;
      if (input) {
        input.value = storedPin;
        input.type = 'password';
        const showButton = input.closest('.password-field')
          ?.querySelector('.password-toggle');
        if (showButton) {
          showButton.textContent = showButton.dataset.labelShow || '';
        }
      }
      if (tile) {
        tile.folder_pin_enabled = enabled;
        tile.folder_pin = storedPin;
      }
      if (tileEl) tileEl.dataset.folderPinEnabled = enabled ? '1' : '0';
      syncFolderPinControls(tab);
      if (status) status.textContent = navigateText('folderPinSaved');
      showNotification(navigateText('folderPinSaved'));
    } catch (error) {
      const message = error?.message || navigateText('folderPinSaveFailed');
      if (toggle && tile) toggle.checked = tile.folder_pin_enabled === true;
      syncFolderPinControls(tab);
      if (status) status.textContent = message;
      showNotification(message, false);
    } finally {
      if (button) button.disabled = false;
    }
  }

function maybeFillTitleFromSwitch(tab) {
    maybeFillTitleFromEntity(tab, '_switch_entity');
  }

  const SWITCH_TOGGLE_ON = '#3B82F6';
  const SWITCH_ICON_ON = '#FFD54F';
  const SWITCH_ICON_OFF = '#B0B0B0';
  const SWITCH_ICON_NEUTRAL = '#FFFFFF';

  function syncSwitchPreviewPalette(tileElem) {
    if (!tileElem) return;
    const switchEl = tileElem.querySelector('.tile-switch');
    if (!switchEl) return;
    const bg = tileElem.style.background || window.getComputedStyle(tileElem).backgroundColor || '#353535';
    switchEl.style.setProperty('--switch-knob-color', bg);
    if (tileElem.classList.contains('switch-toggle')) {
      switchEl.style.setProperty('--switch-on-color', SWITCH_TOGGLE_ON);
    }
  }

  function parseOnOff(text) {
    const lower = String(text || '').trim().toLowerCase();
    if (['on', 'true', '1', 'yes'].includes(lower)) return true;
    if (['off', 'false', '0', 'no'].includes(lower)) return false;
    return null;
  }

  function parseHexColor(text) {
    let t = String(text || '').trim();
    if (t.startsWith('#')) t = t.substring(1);
    if (t.startsWith('0x') || t.startsWith('0X')) t = t.substring(2);
    if (t.length !== 6) return null;
    if (!/^[0-9a-fA-F]{6}$/.test(t)) return null;
    return '#' + t.toLowerCase();
  }

  function rgbToHexColor(r, g, b) {
    const clamp = (v) => Math.max(0, Math.min(255, v));
    const rr = clamp(r).toString(16).padStart(2, '0');
    const gg = clamp(g).toString(16).padStart(2, '0');
    const bb = clamp(b).toString(16).padStart(2, '0');
    return '#' + rr + gg + bb;
  }

  function parseRgbList(list) {
    if (Array.isArray(list) && list.length >= 3) {
      return rgbToHexColor(Number(list[0]), Number(list[1]), Number(list[2]));
    }
    const parts = String(list || '').split(',');
    if (parts.length < 3) return null;
    return rgbToHexColor(parseInt(parts[0], 10), parseInt(parts[1], 10), parseInt(parts[2], 10));
  }

  function hsToRgb(h, s) {
    const hh = ((h % 360) + 360) % 360;
    const sat = Math.max(0, Math.min(100, s)) / 100;
    const c = sat;
    const x = c * (1 - Math.abs((hh / 60) % 2 - 1));
    const m = 1 - c;
    let r1 = 0, g1 = 0, b1 = 0;
    if (hh < 60) { r1 = c; g1 = x; b1 = 0; }
    else if (hh < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (hh < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (hh < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (hh < 300) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }
    return rgbToHexColor(Math.round((r1 + m) * 255), Math.round((g1 + m) * 255), Math.round((b1 + m) * 255));
  }

  function parseSwitchPayload(value) {
    const out = {
      available: true,
      hasState: false,
      isOn: false,
      hasColor: false,
      color: null
    };
    if (value === undefined || value === null) return out;
    const text = String(value).trim();
    if (!text.length) return out;

    if (text.startsWith('{')) {
      try {
        const obj = JSON.parse(text);
        if (obj && typeof obj === 'object') {
          if (obj.available !== undefined) {
            out.available = obj.available !== false;
          }
          if (obj.state !== undefined) {
            const normalizedState = String(obj.state).trim().toLowerCase();
            if (normalizedState === 'unavailable') {
              out.available = false;
            } else {
              const on = parseOnOff(obj.state);
              if (on !== null) {
                out.hasState = true;
                out.isOn = on;
              }
            }
          }
          if (obj.color) {
            const hex = parseHexColor(obj.color);
            if (hex) {
              out.hasColor = true;
              out.color = hex;
            }
          }
          if (!out.hasColor && obj.rgb_color) {
            const hex = parseRgbList(obj.rgb_color);
            if (hex) {
              out.hasColor = true;
              out.color = hex;
            }
          }
          if (!out.hasColor && obj.hs_color && Array.isArray(obj.hs_color) && obj.hs_color.length >= 2) {
            out.hasColor = true;
            out.color = hsToRgb(Number(obj.hs_color[0]), Number(obj.hs_color[1]));
          }
        }
      } catch (e) {}
    }

    if (text.toLowerCase() === 'unavailable') {
      out.available = false;
    }

    if (!out.hasState) {
      const on = parseOnOff(text);
      if (on !== null) {
        out.hasState = true;
        out.isOn = on;
      }
    }

    if (!out.hasColor) {
      const hex = parseHexColor(text);
      if (hex) {
        out.hasColor = true;
        out.color = hex;
      } else if (text.startsWith('rgb(') && text.endsWith(')')) {
        const hexRgb = parseRgbList(text.substring(4, text.length - 1));
        if (hexRgb) {
          out.hasColor = true;
          out.color = hexRgb;
        }
      }
    }

    if (!out.hasState && out.hasColor) {
      out.hasState = true;
      out.isOn = true;
    }
    return out;
  }

  function applySwitchPreviewState(tileElem, state) {
    if (!tileElem) return;
    const iconEl = tileElem.querySelector('.tile-icon');
    const switchEl = tileElem.querySelector('.tile-switch');
    const isToggleStyle = tileElem.classList.contains('switch-toggle');
    syncSwitchPreviewPalette(tileElem);
    if (state.available === false) {
      if (iconEl) iconEl.style.color = SWITCH_ICON_OFF;
      if (switchEl) {
        switchEl.classList.remove('is-on');
        if (isToggleStyle) {
          switchEl.style.setProperty('--switch-on-color', SWITCH_TOGGLE_ON);
        } else {
          switchEl.style.removeProperty('--switch-on-color');
        }
      }
      return;
    }
    if (!state.hasState && !state.hasColor) {
      if (iconEl && isToggleStyle) iconEl.style.color = SWITCH_ICON_NEUTRAL;
      return;
    }
    let isOn = state.hasState ? state.isOn : state.hasColor;
    let color = SWITCH_ICON_OFF;
    if (isOn) color = state.hasColor ? state.color : SWITCH_ICON_ON;
    if (iconEl) iconEl.style.color = isToggleStyle ? SWITCH_ICON_NEUTRAL : color;
    if (switchEl) {
      if (isOn) switchEl.classList.add('is-on');
      else switchEl.classList.remove('is-on');
      if (isToggleStyle) {
        switchEl.style.setProperty('--switch-on-color', SWITCH_TOGGLE_ON);
      } else if (isOn && state.hasColor) {
        switchEl.style.setProperty('--switch-on-color', state.color);
      } else {
        switchEl.style.removeProperty('--switch-on-color');
      }
    }
  }

  function updateSwitchValuePreview(tab) {
    if (currentTileIndex === -1) return;
    const prefix = tab;
    const entitySelect = document.getElementById(prefix + '_switch_entity');
    if (!entitySelect) return;
    const entity = entitySelect.value;
    const tileElem = document.getElementById(tab + '-tile-' + currentTileIndex);
    if (!entity || !tileElem) return;
    syncSwitchPreviewPalette(tileElem);
    const applyMeta = (meta) => {
      const values = (meta && meta.values) || {};
      const state = parseSwitchPayload(values[entity] ?? '');
      applySwitchPreviewState(tileElem, state);
    };
    const metaPromise = isSensorMetaCacheLoaded() ? Promise.resolve(sensorMetaCache) : fetchSensorMetaCache();
    metaPromise
      .then(meta => applyMeta(meta))
      .catch(err => console.error('Switch state load failed:', err));
  }

  function loadSwitchFields(tab, data) {
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_switch_entity');
    if (entityEl) entityEl.value = data.sensor_entity || data.switch_entity || '';
    const styleEl = document.getElementById(prefix + '_switch_style');
    if (styleEl) {
      styleEl.value = (data.switch_style !== undefined && data.switch_style !== null) ? String(data.switch_style) : '0';
    }
    const popupModeEl = document.getElementById(prefix + '_switch_popup_open_mode');
    if (popupModeEl) {
      popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    }
    maybeFillTitleFromSwitch(tab);
  }

  function saveSwitchFields(tab, formData) {
    const prefix = tab;
    formData.append('switch_entity', document.getElementById(prefix + '_switch_entity')?.value || '');
    const styleEl = document.getElementById(prefix + '_switch_style');
    formData.append('switch_style', styleEl ? styleEl.value : '0');
    formData.append('popup_open_mode', document.getElementById(prefix + '_switch_popup_open_mode')?.value || '1');
  }

  function resetSwitchFields(tab) {
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_switch_entity');
    if (entityEl) entityEl.value = '';
    const styleEl = document.getElementById(prefix + '_switch_style');
    if (styleEl) styleEl.value = '0';
    const popupModeEl = document.getElementById(prefix + '_switch_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
  }

  function parseCoverPreviewPayload(value) {
    const out = {
      state: 'unknown',
      position: null,
      tiltPosition: null,
      deviceClass: '',
      available: true
    };
    if (value === undefined || value === null) return out;
    const text = String(value).trim();
    if (!text.length) return out;
    if (!text.startsWith('{')) {
      out.state = text.toLowerCase();
      out.available = out.state !== 'unavailable';
      return out;
    }
    try {
      const obj = JSON.parse(text);
      if (!obj || typeof obj !== 'object') return out;
      const attrs = obj.attributes && typeof obj.attributes === 'object'
        ? obj.attributes : obj;
      out.state = String(obj.state ?? attrs.state ?? 'unknown').toLowerCase();
      out.available = obj.available !== undefined
        ? !!obj.available : out.state !== 'unavailable';
      const position = obj.current_position ?? attrs.current_position;
      if (position !== undefined && position !== null &&
          Number.isFinite(Number(position))) {
        out.position = Math.max(0, Math.min(100, Math.round(Number(position))));
      }
      const tilt = obj.current_tilt_position ?? attrs.current_tilt_position;
      if (tilt !== undefined && tilt !== null && Number.isFinite(Number(tilt))) {
        out.tiltPosition = Math.max(0, Math.min(100, Math.round(Number(tilt))));
      }
      out.deviceClass = String(obj.device_class ?? attrs.device_class ?? '').toLowerCase();
    } catch (e) {}
    return out;
  }

  function coverPreviewIcon(state, fallback = '') {
    if (fallback) return fallback;
    const deviceClass = state?.deviceClass || '';
    const value = String(state?.state || '').toLowerCase();
    const resolve = (defaultIcon, closedIcon, closingIcon, openingIcon) => {
      if (value === 'closed' && closedIcon) return closedIcon;
      if (value === 'closing' && closingIcon) return closingIcon;
      if (value === 'opening' && openingIcon) return openingIcon;
      return defaultIcon;
    };
    if (deviceClass === 'blind') {
      return resolve('blinds-horizontal', 'blinds-horizontal-closed',
        'arrow-down-box', 'arrow-up-box');
    }
    if (deviceClass === 'curtain') {
      return resolve('curtains', 'curtains-closed',
        'arrow-collapse-horizontal', 'arrow-split-vertical');
    }
    if (deviceClass === 'damper') {
      return resolve('circle', 'circle-slice-8');
    }
    if (deviceClass === 'door') return resolve('door-open', 'door-closed');
    if (deviceClass === 'garage') {
      return resolve('garage-open', 'garage', 'arrow-down-box', 'arrow-up-box');
    }
    if (deviceClass === 'gate') {
      return resolve('gate-open', 'gate', 'arrow-right', 'arrow-right');
    }
    if (deviceClass === 'shade') {
      return resolve('roller-shade', 'roller-shade-closed',
        'arrow-down-box', 'arrow-up-box');
    }
    if (deviceClass === 'shutter') {
      return resolve('window-shutter-open', 'window-shutter',
        'arrow-down-box', 'arrow-up-box');
    }
    if (deviceClass === 'window') {
      return resolve('window-open', 'window-closed',
        'arrow-down-box', 'arrow-up-box');
    }
    return resolve('window-open', 'window-closed',
      'arrow-down-box', 'arrow-up-box');
  }

  function coverPreviewColor(state) {
    const value = String(state?.state || 'unknown').toLowerCase();
    if (state?.available === false ||
        value === 'closed' || value === 'unknown' || value === 'unavailable') {
      return '#9e9e9e';
    }
    return '#926bc7';
  }

  function coverPreviewStateText(state) {
    if (state?.available === false) return COVER_I18N.unavailable;
    const labels = {
      open: COVER_I18N.open,
      opening: COVER_I18N.opening,
      closed: COVER_I18N.closed,
      closing: COVER_I18N.closing,
      unavailable: COVER_I18N.unavailable,
      unknown: COVER_I18N.unknown
    };
    return labels[String(state?.state || 'unknown').toLowerCase()] ||
      COVER_I18N.unknown;
  }

  function loadCoverFields(tab, data) {
    const entity = document.getElementById(tab + '_cover_entity');
    const configured = data.sensor_entity || data.cover_entity || '';
    if (entity) {
      if (configured) {
        entity.dataset.configuredValue = configured;
        if (!Array.from(entity.options).some(option => option.value === configured)) {
          const option = document.createElement('option');
          option.value = configured;
          option.textContent = configured;
          entity.appendChild(option);
        }
      } else {
        delete entity.dataset.configuredValue;
      }
      entity.value = configured;
    }
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
    maybeFillTitleFromEntity(tab, '_cover_entity');
  }

  function saveCoverFields(tab, formData) {
    const entity = document.getElementById(tab + '_cover_entity')?.value || '';
    formData.append('cover_entity', entity);
    formData.append('sensor_entity', entity);
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetCoverFields(tab) {
    const entity = document.getElementById(tab + '_cover_entity');
    if (entity) {
      entity.value = '';
      delete entity.dataset.configuredValue;
    }
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) popup.value = '1';
  }

function maybeFillTitleFromMedia(tab) {
    maybeFillTitleFromEntity(tab, '_media_entity');
  }

  function updateMediaValuePreview(tab) {
    // Media tiles stay intentionally simple in the WebUI preview:
    // only icon and configured tile title are shown.
  }

  function loadMediaFields(tab, data) {
    const prefix = tab;
    const el = document.getElementById(prefix + '_media_entity');
    if (el) el.value = data.sensor_entity || data.media_entity || '';
    maybeFillTitleFromMedia(tab);
    updateMediaValuePreview(tab);
  }

  function saveMediaFields(tab, formData) {
    const prefix = tab;
    const entity = document.getElementById(prefix + '_media_entity')?.value || '';
    formData.append('media_entity', entity);
    formData.append('sensor_entity', entity);
  }

  function resetMediaFields(tab) {
    const prefix = tab;
    const el = document.getElementById(prefix + '_media_entity');
    if (el) el.value = '';
  }

  const CLIMATE_TILE_CONTENT = Object.freeze({
    AUTO: 0,
    EMPTY: 1,
    CURRENT_TEMPERATURE: 2,
    CURRENT_HUMIDITY: 3,
    TARGET_TEMPERATURE: 4,
    TARGET_TEMPERATURE_LOW: 5,
    TARGET_TEMPERATURE_HIGH: 6,
    TARGET_HUMIDITY: 7,
    HVAC_MODE: 8
  });
  const CLIMATE_TARGET_LAYOUT = Object.freeze({
    AUTO: 0,
    HORIZONTAL: 1,
    VERTICAL: 2
  });
  const CLIMATE_SUPPORTED_FEATURE = Object.freeze({
    TARGET_TEMPERATURE: 1,
    TARGET_TEMPERATURE_RANGE: 2,
    TARGET_HUMIDITY: 4,
    FAN_MODE: 8,
    PRESET_MODE: 16,
    SWING_MODE: 32,
    SWING_HORIZONTAL_MODE: 512
  });
  const CLIMATE_LAYOUT_MAGIC = 0x434c0000;
  const CLIMATE_LAYOUT_MAGIC_MASK = 0xffff0000;
  const CLIMATE_LAYOUT_VALUE_MASK = 0x00000fff;
  const CLIMATE_GEOMETRY_PREFIX = 'CLG2:';
  const CLIMATE_GEOMETRY_LEGACY_PREFIX = 'CLG1:';

  function climateMaxGridColumns() {
    return Math.max(
      1, Number(
        typeof GRID_COLS === 'number' ? GRID_COLS : 7) || 7);
  }

  function climateMaxOuterRows() {
    return Math.max(
      1, Number(
        typeof GRID_ROWS === 'number' ? GRID_ROWS : 5) || 5);
  }

  function climateSlotCapacity(spanW, spanH) {
    const { columns, rows } =
      climateGridDimensions(spanW, spanH);
    return Math.min(6, columns * rows);
  }

  function climateGridDimensions(spanW, spanH) {
    const columns = Math.max(
      1, Math.min(
        climateMaxGridColumns(), Number(spanW) || 1));
    const outerRows = Math.max(
      1, Math.min(
        climateMaxOuterRows(), Number(spanH) || 1));
    return {
      columns,
      rows: outerRows * 2 - 1
    };
  }

  function clampClimateGeometryItem(item, columns, rows) {
    const col = Math.max(
      0, Math.min(columns - 1, Number(item?.col) || 0));
    const row = Math.max(
      0, Math.min(rows - 1, Number(item?.row) || 0));
    const spanW = Math.max(
      1, Math.min(columns - col, Number(item?.spanW) || 1));
    const spanH = Math.max(
      1, Math.min(rows - row, Number(item?.spanH) || 1));
    return { col, row, spanW, spanH };
  }

  function sanitizeClimateGeometryItem(item) {
    const maxColumns = climateMaxGridColumns();
    const maxRows = climateMaxOuterRows() * 2 - 1;
    return {
      col: Math.max(
        0, Math.min(maxColumns - 1, Number(item?.col) || 0)),
      row: Math.max(
        0, Math.min(maxRows - 1, Number(item?.row) || 0)),
      spanW: Math.max(
        1, Math.min(maxColumns, Number(item?.spanW) || 1)),
      spanH: Math.max(
        1, Math.min(maxRows, Number(item?.spanH) || 1))
    };
  }

  function defaultClimateGeometry(
      spanW, spanH, slotConfig = null,
      targetLayoutConfig = null) {
    const { columns, rows } =
      climateGridDimensions(spanW, spanH);
    const configured = Array.isArray(slotConfig)
      ? slotConfig : Array(6).fill(CLIMATE_TILE_CONTENT.AUTO);
    const layouts = Array.isArray(targetLayoutConfig)
      ? targetLayoutConfig : Array(6).fill(CLIMATE_TARGET_LAYOUT.AUTO);
    return Array.from({ length: 6 }, (_, index) => {
      const item = {
        col: index % columns,
        row: Math.min(rows - 1, Math.floor(index / columns)),
        spanW: 1,
        spanH: 1
      };
      const content = Number(configured[index]) || 0;
      const adjustable =
        content >= CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE &&
        content <= CLIMATE_TILE_CONTENT.TARGET_HUMIDITY;
      if (!adjustable) return item;
      const layout = Number(layouts[index]) || 0;
      const canHorizontal = item.col + 1 < columns;
      const canVertical = item.row + 1 < rows;
      if (layout === CLIMATE_TARGET_LAYOUT.HORIZONTAL &&
          canHorizontal) {
        item.spanW = 2;
      } else if (layout === CLIMATE_TARGET_LAYOUT.VERTICAL &&
                 canVertical) {
        item.spanH = 2;
      } else if (columns === 1 && canVertical) {
        item.spanH = 2;
      } else if (rows === 1 && canHorizontal) {
        item.spanW = 2;
      } else if (canVertical) {
        item.spanH = 2;
      } else if (canHorizontal) {
        item.spanW = 2;
      }
      return item;
    });
  }

  function decodeClimateGeometry(
      value, spanW, spanH, slotConfig = null,
      targetLayoutConfig = null) {
    const text = String(value || '').trim();
    const currentMatch =
      text.match(/^CLG2:([0-9a-fA-F]{24})$/);
    if (currentMatch) {
      return Array.from({ length: 6 }, (_, index) => {
        const offset = index * 4;
        const raw = Number.parseInt(
          currentMatch[1].slice(offset, offset + 4), 16);
        return sanitizeClimateGeometryItem({
          col: raw & 0x07,
          row: (raw >> 3) & 0x0f,
          spanW: ((raw >> 7) & 0x07) + 1,
          spanH: ((raw >> 10) & 0x0f) + 1
        });
      });
    }
    const legacyMatch =
      text.match(/^CLG1:([0-9a-fA-F]{9})$/);
    if (!legacyMatch) {
      return defaultClimateGeometry(
        spanW, spanH, slotConfig, targetLayoutConfig);
    }
    let packed = BigInt('0x' + legacyMatch[1]);
    return Array.from({ length: 6 }, (_, index) => {
      const raw = Number((packed >> BigInt(index * 6)) & 0x3fn);
      return sanitizeClimateGeometryItem({
        col: raw & 0x01,
        row: (raw >> 1) & 0x03,
        spanW: ((raw >> 3) & 0x01) + 1,
        spanH: ((raw >> 4) & 0x03) + 1
      });
    });
  }

  function encodeClimateGeometry(items) {
    const encoded = Array.from({ length: 6 }, (_, index) => {
      const item = sanitizeClimateGeometryItem(
        items[index] || {});
      const raw =
        item.col |
        (item.row << 3) |
        ((item.spanW - 1) << 7) |
        ((item.spanH - 1) << 10);
      return (raw & 0x3fff)
        .toString(16).toUpperCase().padStart(4, '0');
    }).join('');
    return CLIMATE_GEOMETRY_PREFIX + encoded;
  }

  function currentClimateGeometry(tab) {
    const spanW = document.getElementById(
      tab + '_tile_span_w')?.value || 1;
    const spanH = document.getElementById(
      tab + '_tile_span_h')?.value || 1;
    const input = document.getElementById(
      tab + '_climate_geometry');
    const configured = currentClimateSlotConfig(tab);
    const resolved = climateResolvedEditorKinds(tab);
    const effectiveConfig = configured.map((value, index) =>
      Number(value) === CLIMATE_TILE_CONTENT.AUTO &&
      resolved[index] !== null
        ? resolved[index]
        : value);
    return decodeClimateGeometry(
      input?.value || '', spanW, spanH,
      effectiveConfig,
      currentClimateTargetLayouts(tab));
  }

  function storeClimateGeometry(tab, items) {
    const input = document.getElementById(
      tab + '_climate_geometry');
    if (input) input.value = encodeClimateGeometry(items);
  }

  function decodeClimateSlotConfig(packedValue) {
    const packed = Math.max(0, Number(packedValue) || 0) >>> 0;
    return Array.from({ length: 6 }, (_, index) => {
      const value = (packed >>> (index * 4)) & 0x0f;
      return value <= CLIMATE_TILE_CONTENT.HVAC_MODE
        ? value : CLIMATE_TILE_CONTENT.AUTO;
    });
  }

  function packClimateSlotConfig(tab) {
    let packed = 0;
    for (let index = 0; index < 6; ++index) {
      const select = document.getElementById(
        tab + '_climate_slot_' + index);
      const value = Math.max(
        0, Math.min(
          CLIMATE_TILE_CONTENT.HVAC_MODE,
          Number(select?.value) || 0));
      packed |= (value & 0x0f) << (index * 4);
    }
    return packed >>> 0;
  }

  function currentClimateSlotConfig(tab) {
    return Array.from({ length: 6 }, (_, index) => {
      const select = document.getElementById(
        tab + '_climate_slot_' + index);
      return Number(select?.value) || 0;
    });
  }

  function getClimateLayoutPayload(storedValue) {
    const stored = Math.max(0, Number(storedValue) || 0) >>> 0;
    if (((stored & CLIMATE_LAYOUT_MAGIC_MASK) >>> 0) ===
        CLIMATE_LAYOUT_MAGIC) {
      return stored & CLIMATE_LAYOUT_VALUE_MASK;
    }
    return stored <= CLIMATE_LAYOUT_VALUE_MASK ? stored : 0;
  }

  function decodeClimateTargetLayouts(storedValue) {
    const packed = getClimateLayoutPayload(storedValue);
    return Array.from({ length: 6 }, (_, index) => {
      const value = (packed >>> (index * 2)) & 0x03;
      return value <= CLIMATE_TARGET_LAYOUT.VERTICAL
        ? value : CLIMATE_TARGET_LAYOUT.AUTO;
    });
  }

  function packClimateTargetLayouts(tab) {
    let packed = 0;
    for (let index = 0; index < 6; ++index) {
      const select = document.getElementById(
        tab + '_climate_layout_' + index);
      const value = Math.max(
        0, Math.min(
          CLIMATE_TARGET_LAYOUT.VERTICAL,
          Number(select?.value) || 0));
      packed |= (value & 0x03) << (index * 2);
    }
    return packed >>> 0;
  }

  function currentClimateTargetLayouts(tab) {
    return Array.from({ length: 6 }, (_, index) => {
      const select = document.getElementById(
        tab + '_climate_layout_' + index);
      return Number(select?.value) || CLIMATE_TARGET_LAYOUT.AUTO;
    });
  }

  function climateGeometryOverlaps(a, b) {
    return a.col < b.col + b.spanW &&
      a.col + a.spanW > b.col &&
      a.row < b.row + b.spanH &&
      a.row + a.spanH > b.row;
  }

  function canPlaceClimateItem(
      items, configured, index, candidate, capacity) {
    for (let other = 0; other < capacity; ++other) {
      if (other === index) continue;
      if (Number(configured[other]) === CLIMATE_TILE_CONTENT.EMPTY) {
        continue;
      }
      if (climateGeometryOverlaps(candidate, items[other])) {
        return false;
      }
    }
    return true;
  }

  function firstFreeClimatePlacement(
      items, configured, capacity, columns, rows,
      ignoreIndex = -1, spanW = 1, spanH = 1) {
    const safeSpanW = Math.max(
      1, Math.min(columns, Number(spanW) || 1));
    const safeSpanH = Math.max(
      1, Math.min(rows, Number(spanH) || 1));
    for (let row = 0; row + safeSpanH <= rows; ++row) {
      for (let col = 0; col + safeSpanW <= columns; ++col) {
        const candidate = {
          col, row,
          spanW: safeSpanW,
          spanH: safeSpanH
        };
        if (canPlaceClimateItem(
              items, configured, ignoreIndex,
              candidate, capacity)) {
          return candidate;
        }
      }
    }
    return null;
  }

  function notifyClimateGridChanged(tab) {
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }

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
      state.geometry);
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

  function climateEditorState(tab) {
    const entity = document.getElementById(
      tab + '_climate_entity')?.value || '';
    return parseClimatePreviewPayload(
      sensorMetaCache?.values?.[entity] ?? '');
  }

  function climateAvailableContentKinds(tab) {
    const entity = document.getElementById(
      tab + '_climate_entity')?.value || '';
    const state = climateEditorState(tab);
    if (!entity || !state.valid) {
      // Without a selected entity or a loaded state the capabilities are not
      // known yet. Keep the editor usable until real metadata arrives.
      return null;
    }

    const available = new Set([
      CLIMATE_TILE_CONTENT.AUTO,
      CLIMATE_TILE_CONTENT.EMPTY
    ]);
    if (state.current !== '--') {
      available.add(
        CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
    }
    if (state.currentHumidity !== null) {
      available.add(
        CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY);
    }
    if (state.target !== null) {
      available.add(
        CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
    }
    if (state.targetLow !== null) {
      available.add(
        CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW);
    }
    if (state.targetHigh !== null) {
      available.add(
        CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH);
    }
    if (state.targetHumidity !== null) {
      available.add(
        CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
    }
    if (state.mode) {
      available.add(CLIMATE_TILE_CONTENT.HVAC_MODE);
    }
    return available;
  }

  function syncClimateContentOptions(tab) {
    const editor = document.getElementById(
      tab + '_climate_selected_content');
    if (!editor) return;
    const available = climateAvailableContentKinds(tab);
    const selectedKind = Number(editor.value);
    Array.from(editor.options).forEach(option => {
      const kind = Number(option.value);
      // Never silently discard an explicitly stored legacy/custom choice.
      // Keep that one visible so the user can change or remove it.
      const selectedExplicit =
        kind === selectedKind &&
        kind !== CLIMATE_TILE_CONTENT.AUTO &&
        kind !== CLIMATE_TILE_CONTENT.EMPTY;
      const visible =
        !available ||
        available.has(kind) ||
        selectedExplicit;
      option.hidden = !visible;
      option.disabled = !visible;
      option.style.display = visible ? '' : 'none';
    });
  }

  function climateAutomaticEditorKinds(tab) {
    const state = climateEditorState(tab);
    const spanW = Math.max(1, Number(
      document.getElementById(
        tab + '_tile_span_w')?.value) || 1);
    const spanH = Math.max(1, Number(
      document.getElementById(
        tab + '_tile_span_h')?.value) || 1);
    const capacity = climateSlotCapacity(spanW, spanH);
    const kinds = [];
    const add = kind => {
      if (kinds.length < capacity) kinds.push(kind);
    };

    const addPrimaryTarget = () => {
      if (state.targetLow !== null &&
          state.targetHigh !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW);
      } else if (state.target !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      } else if (state.targetHumidity !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      } else if (!state.valid) {
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      }
    };

    if (spanW === 1 && spanH === 1) {
      if (!state.valid || state.current !== '--') {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      } else {
        addPrimaryTarget();
      }
    } else if (spanW >= 2 && spanH === 1) {
      if (!state.valid || state.current !== '--') {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
    } else if (spanW === 1) {
      if (!state.valid || state.current !== '--') {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
      if (spanH === 2) return kinds;
      if (state.targetHumidity !== null &&
          (state.targetLow !== null ||
           state.targetHigh !== null ||
           state.target !== null)) {
        add(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      }
    } else {
      if (!state.valid || state.current !== '--') {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      if (state.currentHumidity !== null) {
        add(CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY);
      }
      if (state.targetLow !== null &&
          state.targetHigh !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW);
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH);
      } else if (state.target !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      }
      if (state.targetHumidity !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      }
      if (state.mode) add(CLIMATE_TILE_CONTENT.HVAC_MODE);
    }
    return kinds;
  }

  function climateResolvedEditorKinds(tab) {
    const configured = currentClimateSlotConfig(tab);
    const automatic = climateAutomaticEditorKinds(tab);
    const capacity = climateSlotCapacity(
      document.getElementById(
        tab + '_tile_span_w')?.value || 1,
      document.getElementById(
        tab + '_tile_span_h')?.value || 1);
    const explicit = new Set();
    configured.slice(0, capacity).forEach(selection => {
      const kind = Number(selection) || 0;
      if (kind !== CLIMATE_TILE_CONTENT.AUTO &&
          kind !== CLIMATE_TILE_CONTENT.EMPTY) {
        explicit.add(kind);
      }
    });
    let cursor = 0;
    return configured.map((selection, index) => {
      const kind = Number(selection) || 0;
      if (index >= capacity ||
          kind === CLIMATE_TILE_CONTENT.EMPTY) {
        return null;
      }
      if (kind !== CLIMATE_TILE_CONTENT.AUTO) return kind;
      while (cursor < automatic.length) {
        const candidate = automatic[cursor++];
        if (!explicit.has(candidate)) return candidate;
      }
      return null;
    });
  }

  function materializeClimateAutomaticItems(
      tab, resolvedOverride = null) {
    const configured = currentClimateSlotConfig(tab);
    const resolved = Array.isArray(resolvedOverride)
      ? resolvedOverride : climateResolvedEditorKinds(tab);
    for (let index = 0; index < 6; ++index) {
      if (Number(configured[index]) !== CLIMATE_TILE_CONTENT.AUTO) {
        continue;
      }
      const source = document.getElementById(
        tab + '_climate_slot_' + index);
      if (!source) continue;
      const kind = Number(resolved[index]);
      source.value = Number.isFinite(kind) && kind > 0
        ? String(kind)
        : String(CLIMATE_TILE_CONTENT.EMPTY);
    }
  }

  function climatePlacementConfig(
      configured, resolvedKinds) {
    return configured.map((selection, index) =>
      resolvedKinds[index] === null
        ? CLIMATE_TILE_CONTENT.EMPTY
        : selection);
  }

  function climateTargetCaption(state, kind) {
    if (state?.available === false) return CLIMATE_I18N.unavailable;
    const entityState = String(state?.mode || '').toLowerCase();
    if (entityState === 'unknown') return CLIMATE_I18N.unknown;
    if (kind === CLIMATE_TILE_CONTENT.TARGET_HUMIDITY) {
      return CLIMATE_I18N.targetHumidity;
    }
    if (kind === CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW) {
      return CLIMATE_I18N.heat;
    }
    if (kind === CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH) {
      return CLIMATE_I18N.cool;
    }

    const action = String(state?.action || '').toLowerCase();
    const mode = String(state?.mode || '').toLowerCase();
    const actionLabels = {
      heating: CLIMATE_I18N.heat,
      preheating: CLIMATE_I18N.heat,
      cooling: CLIMATE_I18N.cool
    };
    if (actionLabels[action]) return actionLabels[action];

    const modeLabels = {
      off: CLIMATE_I18N.off,
      heat: CLIMATE_I18N.heat,
      cool: CLIMATE_I18N.cool,
      heat_cool: CLIMATE_I18N.heatCool,
      auto: CLIMATE_I18N.autoMode,
      dry: CLIMATE_I18N.dry,
      fan_only: CLIMATE_I18N.fanOnly
    };
    if (modeLabels[mode]) return modeLabels[mode];
    if (mode) return CLIMATE_I18N.genericClimate;
    return CLIMATE_I18N.targetTemperature;
  }

  function climateModeText(state) {
    const raw = String(state?.mode || '').toLowerCase();
    if (state?.available === false || raw === 'unavailable') {
      return CLIMATE_I18N.unavailable;
    }
    if (raw === 'unknown') return CLIMATE_I18N.unknown;
    if (!raw) return '--';
    const labels = {
      off: CLIMATE_I18N.off,
      heat: CLIMATE_I18N.heat,
      cool: CLIMATE_I18N.cool,
      auto: CLIMATE_I18N.autoMode,
      dry: CLIMATE_I18N.dry,
      fan_only: CLIMATE_I18N.fanOnly,
      heat_cool: CLIMATE_I18N.heatCool
    };
    if (labels[raw]) return labels[raw];
    const fallback = raw.replaceAll('_', ' ');
    return fallback.charAt(0).toUpperCase() + fallback.slice(1);
  }

  function climateFeatureSupported(state, feature, legacySupported = true) {
    if (!state?.hasSupportedFeatures) return legacySupported;
    return (Number(state.supportedFeatures) & feature) !== 0;
  }

  function climateTargetInteractive(state, kind) {
    if (state?.available === false) return false;
    switch (kind) {
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE:
        return state?.target !== null &&
          climateFeatureSupported(
            state, CLIMATE_SUPPORTED_FEATURE.TARGET_TEMPERATURE);
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW:
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH:
        return state?.targetLow !== null && state?.targetHigh !== null &&
          climateFeatureSupported(
            state, CLIMATE_SUPPORTED_FEATURE.TARGET_TEMPERATURE_RANGE);
      case CLIMATE_TILE_CONTENT.TARGET_HUMIDITY:
        return state?.targetHumidity !== null &&
          climateFeatureSupported(
            state, CLIMATE_SUPPORTED_FEATURE.TARGET_HUMIDITY);
      default:
        return false;
    }
  }

  function climateEditorContentInfo(tab, kind) {
    const state = climateEditorState(tab);
    const unit = state.unit || '\u00B0C';
    const temperature = value =>
      String(value ?? '--') + ' ' + unit;
    const selected = Number(kind) || 0;
    switch (selected) {
      case CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE:
        return {
          label: CLIMATE_I18N.currentTemperature,
          value: temperature(state.current),
          adjustable: false
        };
      case CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY:
        return {
          label: CLIMATE_I18N.currentHumidity,
          value: state.currentHumidity !== null
            ? state.currentHumidity + '%' : '--%',
          adjustable: false
        };
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE:
        return {
          label: climateTargetCaption(state, selected),
          value: temperature(state.target),
          adjustable: true
        };
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW:
        return {
          label: climateTargetCaption(state, selected),
          value: temperature(state.targetLow),
          adjustable: true
        };
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH:
        return {
          label: climateTargetCaption(state, selected),
          value: temperature(state.targetHigh),
          adjustable: true
        };
      case CLIMATE_TILE_CONTENT.TARGET_HUMIDITY:
        return {
          label: climateTargetCaption(state, selected),
          value: state.targetHumidity !== null
            ? state.targetHumidity + '%' : '--%',
          adjustable: true
        };
      case CLIMATE_TILE_CONTENT.HVAC_MODE:
        return {
          label: CLIMATE_I18N.mode,
          value: climateModeText(state),
          adjustable: false
        };
      case CLIMATE_TILE_CONTENT.AUTO:
      default:
        return {
          label: CLIMATE_I18N.automatic,
          value: '--',
          adjustable: false
        };
    }
  }

  function renderClimateEditorItem(tab, index, geometry, kind) {
    const preview = document.getElementById(
      tab + '_climate_preview_' + index);
    if (!preview) return;
    const resolvedKind =
      climateResolvedEditorKinds(tab)[index];
    const info = climateEditorContentInfo(
      tab, resolvedKind ?? kind);
    const expanded =
      geometry.spanW > 1 || geometry.spanH > 1;
    const item = document.getElementById(
      tab + '_climate_slot_row_' + index);
    item?.classList.toggle(
      'climate-mini-control-item',
      info.adjustable && expanded);
    if (info.adjustable && !expanded) {
      preview.innerHTML =
        '<div class="climate-slot climate-slot-control ' +
        'climate-slot-control-compact">' +
        '<span class="climate-minus" aria-hidden="true">-</span>' +
        '<strong>' + escapeHtml(info.value) + '</strong>' +
        '<span class="climate-plus" aria-hidden="true">+</span></div>';
      return;
    }
    if (info.adjustable) {
      const orientation =
        geometry.spanW > 1 && geometry.spanH === 1
          ? 'climate-slot-control-horizontal'
          : (geometry.spanW > 1 && geometry.spanH > 1
              ? 'climate-slot-control-large'
              : 'climate-slot-control-vertical');
      preview.innerHTML =
        '<div class="climate-slot climate-slot-control ' +
        orientation + '">' +
        '<small>' + escapeHtml(info.label) + '</small>' +
        '<span class="climate-minus">-</span>' +
        '<strong>' + escapeHtml(info.value) + '</strong>' +
        '<span class="climate-plus">+</span></div>';
      return;
    }
    preview.innerHTML =
      '<div class="climate-slot climate-slot-value' +
      (resolvedKind === CLIMATE_TILE_CONTENT.HVAC_MODE
        ? ' climate-slot-mode' : '') + '">' +
      '<strong>' + escapeHtml(info.value) + '</strong></div>';
  }

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

  function climateActiveGridIndices(tab, capacity) {
    const configured = currentClimateSlotConfig(tab);
    const resolved = climateResolvedEditorKinds(tab);
    const active = new Set();
    for (let index = 0; index < capacity; ++index) {
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
        const capacity = climateSlotCapacity(spanW, spanH);
        const configured = currentClimateSlotConfig(tab);
        const resolvedKinds =
          climateResolvedEditorKinds(tab);
        const index = configured.findIndex(
          (value, candidate) =>
            candidate < capacity &&
            (Number(value) === CLIMATE_TILE_CONTENT.EMPTY ||
             (Number(value) === CLIMATE_TILE_CONTENT.AUTO &&
              resolvedKinds[candidate] === null)));
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
      const capacity = climateSlotCapacity(spanW, spanH);
      const activeIndices =
        climateActiveGridIndices(tab, capacity);
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
            const capacity =
              climateSlotCapacity(spanW, spanH);
            const configured = currentClimateSlotConfig(tab);
            const stored = currentClimateGeometry(tab);
            const items = stored.map(entry =>
              clampClimateGeometryItem(entry, columns, rows));
            const origin = { ...items[index] };
            const layouts =
              climateGridLayouts(tab, columns, rows);
            const activeIndices =
              climateActiveGridIndices(tab, capacity);
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

  function syncClimateSlotFields(
      tab, finalizePreviewSelection = false) {
    mountClimateMiniEditor(tab);
    const spanW = Math.max(1, Number(document.getElementById(
      tab + '_tile_span_w')?.value) || 1);
    const spanH = Math.max(1, Number(document.getElementById(
      tab + '_tile_span_h')?.value) || 1);
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

  function parseClimatePreviewPayload(value) {
    const out = {
      valid: false,
      current: '--',
      currentHumidity: null,
      target: null,
      targetHumidity: null,
      targetLow: null,
      targetHigh: null,
      unit: '\u00B0C',
      mode: '',
      action: '',
      preset: '',
      available: true,
      hasSupportedFeatures: false,
      supportedFeatures: 0
    };
    if (value === undefined || value === null) return out;
    const text = String(value).trim();
    if (!text.length) return out;
    if (!text.startsWith('{')) {
      const state = text.toLowerCase();
      if (state === 'unavailable' || state === 'unknown') {
        out.mode = state;
        out.available = state !== 'unavailable';
        out.valid = true;
        return out;
      }
      const numeric = Number(text.replace(',', '.'));
      if (Number.isFinite(numeric)) {
        out.valid = true;
        out.current = formatLocalizedNumber(numeric, 1, false);
      }
      return out;
    }
    try {
      const obj = JSON.parse(text);
      if (!obj || typeof obj !== 'object') return out;
      const attrs = obj.attributes && typeof obj.attributes === 'object'
        ? obj.attributes : obj;
      const current = attrs.current_temperature;
      if (current !== undefined && current !== null && Number.isFinite(Number(current))) {
        out.current = formatLocalizedNumber(Number(current), 1, false);
      }
      const currentHumidity = attrs.current_humidity;
      if (currentHumidity !== undefined && currentHumidity !== null &&
          Number.isFinite(Number(currentHumidity))) {
        out.currentHumidity = formatLocalizedNumber(
          Number(currentHumidity), 0, true);
      }
      const target = attrs.temperature;
      if (target !== undefined && target !== null &&
          Number.isFinite(Number(target))) {
        out.target = formatLocalizedNumber(Number(target), 1, false);
      }
      const targetHumidity = attrs.humidity;
      if (targetHumidity !== undefined && targetHumidity !== null &&
          Number.isFinite(Number(targetHumidity))) {
        out.targetHumidity = formatLocalizedNumber(
          Number(targetHumidity), 0, true);
      }
      const targetLow = attrs.target_temp_low;
      if (targetLow !== undefined && targetLow !== null &&
          Number.isFinite(Number(targetLow))) {
        out.targetLow = formatLocalizedNumber(Number(targetLow), 1, false);
      }
      const targetHigh = attrs.target_temp_high;
      if (targetHigh !== undefined && targetHigh !== null &&
          Number.isFinite(Number(targetHigh))) {
        out.targetHigh = formatLocalizedNumber(Number(targetHigh), 1, false);
      }
      out.unit = attrs.temperature_unit || attrs.unit_of_measurement || '\u00B0C';
      out.mode = String(obj.hvac_mode || obj.state || attrs.hvac_mode || '').toLowerCase();
      out.action = String(obj.hvac_action || attrs.hvac_action || '').toLowerCase();
      out.preset = String(obj.preset_mode || attrs.preset_mode || '').toLowerCase();
      const available = obj.available ?? attrs.available;
      out.available = available !== undefined
        ? !!available : out.mode !== 'unavailable';
      const supportedFeatures =
        obj.supported_features ?? attrs.supported_features;
      if (supportedFeatures !== undefined && supportedFeatures !== null &&
          Number.isFinite(Number(supportedFeatures)) &&
          Number(supportedFeatures) >= 0) {
        out.hasSupportedFeatures = true;
        out.supportedFeatures = Math.min(
          65535, Math.round(Number(supportedFeatures)));
      }
      out.valid =
        !out.available ||
        out.current !== '--' ||
        out.currentHumidity !== null ||
        out.target !== null ||
        out.targetHumidity !== null ||
        out.targetLow !== null ||
        out.targetHigh !== null ||
        !!out.mode ||
        !!out.action;
    } catch (e) {}
    return out;
  }

  function climatePreviewIcon(state, baseIcon) {
    const action = String(state?.action || '').toLowerCase();
    const mode = String(state?.mode || '').toLowerCase();
    const fallback = normalizeMdiIconName(baseIcon) || 'thermostat';
    if (state?.available === false) return fallback;
    if (action === 'heating' || action === 'preheating') return 'fire';
    if (action === 'cooling') return 'snowflake';
    if (action === 'drying') return 'water-percent';
    if (action === 'fan') return 'fan';
    if (action === 'defrosting') return 'snowflake-melt';
    if (mode === 'off' || action === 'idle' || action === 'off' || action) {
      return fallback;
    }
    if (mode === 'heat') return 'fire';
    if (mode === 'cool') return 'snowflake';
    if (mode === 'dry') return 'water-percent';
    if (mode === 'fan_only') return 'fan';
    if (mode === 'heat_cool') return 'sun-snowflake-variant';
    if (mode === 'auto') return 'thermostat-auto';
    return fallback;
  }

  function climatePreviewColor(state) {
    const action = String(state?.action || '').toLowerCase();
    const mode = String(state?.mode || '').toLowerCase();
    if (state?.available === false || mode === 'unavailable') {
      return '#9e9e9e';
    }
    if (action === 'heating' || action === 'preheating') return '#ff8a3d';
    if (action === 'cooling') return '#4fc3f7';
    if (action === 'drying') return '#ffd54f';
    if (action === 'fan') return '#4db6ac';
    if (action === 'defrosting') return '#81d4fa';
    if (mode === 'off' || action === 'idle' || action === 'off') return '#9e9e9e';
    if (!action && mode === 'heat') return '#ff8a3d';
    if (!action && mode === 'cool') return '#4fc3f7';
    if (!action && mode === 'dry') return '#ffd54f';
    if (!action && mode === 'fan_only') return '#4db6ac';
    return '#ffffff';
  }

  function climatePreviewSlots(
      state, spanW, spanH, slotConfig = null,
      targetLayoutConfig = null, geometryConfig = null) {
    const w = Math.max(1, Number(spanW) || 1);
    const h = Math.max(1, Number(spanH) || 1);
    const capacity = climateSlotCapacity(w, h);
    const { columns, rows } =
      climateGridDimensions(w, h);
    const configured = Array.isArray(slotConfig)
      ? slotConfig.slice(0, 6)
      : decodeClimateSlotConfig(slotConfig || 0);
    while (configured.length < 6) {
      configured.push(CLIMATE_TILE_CONTENT.AUTO);
    }
    const targetLayouts = Array.isArray(targetLayoutConfig)
      ? targetLayoutConfig.slice(0, 6)
      : decodeClimateTargetLayouts(targetLayoutConfig || 0);
    while (targetLayouts.length < 6) {
      targetLayouts.push(CLIMATE_TARGET_LAYOUT.AUTO);
    }
    const geometry = Array.isArray(geometryConfig)
      ? geometryConfig.slice(0, 6)
      : decodeClimateGeometry(
          geometryConfig || '', w, h,
          configured, targetLayouts);
    while (geometry.length < 6) {
      geometry.push({ col: 0, row: 0, spanW: 1, spanH: 1 });
    }

    const automatic = [];
    const temp = (value) => String(value ?? '--') + ' ' + state.unit;
    const addAutomatic = (kind) => {
      if (automatic.length < capacity) {
        automatic.push(kind);
      }
    };

    const addPrimaryTarget = () => {
      if (state.targetLow !== null && state.targetHigh !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW);
      } else if (state.target !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      } else if (state.targetHumidity !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      } else if (!state.valid) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      }
    };

    const entityState = String(state?.mode || '').toLowerCase();
    if (state?.available === false || entityState === 'unavailable' ||
        entityState === 'unknown') {
      addAutomatic(CLIMATE_TILE_CONTENT.HVAC_MODE);
    } else if (w === 1 && h === 1) {
      if (!state.valid || state.current !== '--') {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      } else {
        addPrimaryTarget();
      }
    } else if (w >= 2 && h === 1) {
      if (!state.valid || state.current !== '--') {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
    } else if (w === 1) {
      if (!state.valid || state.current !== '--') {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
      if (h > 2 &&
          state.targetHumidity !== null &&
          (state.targetLow !== null ||
           state.targetHigh !== null ||
           state.target !== null)) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      }
    } else {
      if (!state.valid || state.current !== '--') {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      if (state.currentHumidity !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY);
      }
      if (state.targetLow !== null && state.targetHigh !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW);
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH);
      } else if (state.target !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      }
      if (state.targetHumidity !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      }
      if (state.mode) {
        addAutomatic(CLIMATE_TILE_CONTENT.HVAC_MODE);
      }
    }

    const slotForKind = (kind) => {
      switch (kind) {
        case CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE:
          return { kind, value: temp(state.current), adjustable: false };
        case CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY:
          return {
            kind,
            value: state.currentHumidity !== null
              ? state.currentHumidity + '%' : '--%',
            adjustable: false
          };
        case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE:
          return {
            kind,
            value: temp(state.target),
            adjustable: true,
            interactive: climateTargetInteractive(state, kind),
            caption: climateTargetCaption(state, kind)
          };
        case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW:
          return {
            kind,
            value: temp(state.targetLow),
            adjustable: true,
            interactive: climateTargetInteractive(state, kind),
            caption: climateTargetCaption(state, kind)
          };
        case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH:
          return {
            kind,
            value: temp(state.targetHigh),
            adjustable: true,
            interactive: climateTargetInteractive(state, kind),
            caption: climateTargetCaption(state, kind)
          };
        case CLIMATE_TILE_CONTENT.TARGET_HUMIDITY:
          return {
            kind,
            value: state.targetHumidity !== null
              ? state.targetHumidity + '%' : '--%',
            adjustable: true,
            interactive: climateTargetInteractive(state, kind),
            caption: climateTargetCaption(state, kind)
          };
        case CLIMATE_TILE_CONTENT.HVAC_MODE:
          return {
            kind,
            value: climateModeText(state),
            adjustable: false
          };
        default:
          return null;
      }
    };

    const explicitlyConfigured = new Set();
    configured.slice(0, capacity).forEach(selection => {
      const kind = Number(selection) || 0;
      if (kind !== CLIMATE_TILE_CONTENT.AUTO &&
          kind !== CLIMATE_TILE_CONTENT.EMPTY) {
        explicitlyConfigured.add(kind);
      }
    });

    const slots = [];
    let automaticCursor = 0;
    for (let index = 0; index < capacity; ++index) {
      const selection = Number(configured[index]) || 0;
      if (selection === CLIMATE_TILE_CONTENT.EMPTY) continue;
      let kind = selection;
      let targetLayout = Number(targetLayouts[index]) || 0;
      if (selection === CLIMATE_TILE_CONTENT.AUTO) {
        kind = undefined;
        targetLayout = CLIMATE_TARGET_LAYOUT.AUTO;
        while (automaticCursor < automatic.length) {
          const candidate = automatic[automaticCursor++];
          if (explicitlyConfigured.has(candidate)) continue;
          kind = candidate;
          break;
        }
      }
      const slot = slotForKind(kind);
      if (!slot) continue;
      slots.push({
        ...slot,
        targetLayout,
        itemIndex: index,
        ...clampClimateGeometryItem(
          geometry[index], columns, rows)
      });
    }

    const logicalRows = rows;
    const hasStoredGeometry =
      Array.isArray(geometryConfig) ||
      /^CLG[12]:/i.test(String(geometryConfig || '').trim());
    const placedSlots = [];
    slots.forEach(slot => {
      let candidate = {
        col: slot.col,
        row: slot.row,
        spanW: slot.spanW,
        spanH: slot.spanH
      };
      if (!hasStoredGeometry && slot.adjustable &&
          candidate.spanW === 1 && candidate.spanH === 1) {
        const canHorizontal =
          candidate.col + 1 < columns;
        const canVertical =
          candidate.row + 1 < logicalRows;
        if (slot.targetLayout ===
              CLIMATE_TARGET_LAYOUT.HORIZONTAL &&
            canHorizontal) {
          candidate.spanW = 2;
        } else if (slot.targetLayout ===
                     CLIMATE_TARGET_LAYOUT.VERTICAL &&
                   canVertical) {
          candidate.spanH = 2;
        } else if (columns === 1 && canVertical) {
          candidate.spanH = 2;
        } else if (logicalRows === 1 && canHorizontal) {
          candidate.spanW = 2;
        } else if (canVertical) {
          candidate.spanH = 2;
        } else if (canHorizontal) {
          candidate.spanW = 2;
        }
      }
      const overlaps = value =>
        placedSlots.some(other =>
            climateGeometryOverlaps(value, {
              col: other.col,
              row: other.row,
              spanW: other.spanW,
              spanH: other.spanH
            }));
      if (overlaps(candidate)) {
        let free = null;
        for (let row = 0;
             row + candidate.spanH <= logicalRows && !free;
             ++row) {
          for (let col = 0;
               col + candidate.spanW <= columns;
               ++col) {
            const next = {
              col, row,
              spanW: candidate.spanW,
              spanH: candidate.spanH
            };
            if (!placedSlots.some(other =>
                  climateGeometryOverlaps(next, {
                    col: other.col,
                    row: other.row,
                    spanW: other.spanW,
                    spanH: other.spanH
                  }))) {
              free = next;
              break;
            }
          }
        }
        if (!free) return;
        candidate = free;
      }
      placedSlots.push({ ...slot, ...candidate });
    });

    const occupiedCells =
      Array(columns * logicalRows).fill(false);
    placedSlots.forEach(slot => {
      for (let row = slot.row;
           row < slot.row + slot.spanH; ++row) {
        for (let col = slot.col;
             col < slot.col + slot.spanW; ++col) {
          occupiedCells[row * columns + col] = true;
        }
      }
    });
    const emptyCellLabel = CLIMATE_I18N.emptyField;
    const previewCells = occupiedCells.map(
      (occupied, cellIndex) => {
        if (occupied) return '';
        const row = Math.floor(cellIndex / columns) + 1;
        const column = cellIndex % columns + 1;
        return '<button type="button" ' +
          'class="climate-preview-cell" ' +
          'data-climate-preview-cell="' + cellIndex + '" ' +
          'aria-label="' + emptyCellLabel + '" ' +
          'style="grid-column:' + column +
          ';grid-row:' + row + '"></button>';
      }).join('');

    return '<div class="climate-slots" style="--climate-columns:' +
      columns +
      ';--climate-rows:' + logicalRows + '">' +
      previewCells +
      placedSlots.map(slot => {
        const row = slot.row + 1;
        const column = slot.col + 1;
        const horizontal =
          slot.adjustable && slot.spanW > 1 &&
          slot.spanH === 1;
        const vertical =
          slot.adjustable && slot.spanW === 1 &&
          slot.spanH > 1;
        const large =
          slot.adjustable && slot.spanW > 1 &&
          slot.spanH > 1;
        const compact =
          slot.adjustable && slot.spanW === 1 &&
          slot.spanH === 1;
        const gridStyle =
          'grid-column:' + column + ' / span ' +
          slot.spanW +
          ';grid-row:' + row + ' / span ' +
          slot.spanH;
        if (!slot.adjustable || slot.interactive === false) {
          return '<div class="climate-slot climate-slot-value' +
            (slot.kind === CLIMATE_TILE_CONTENT.HVAC_MODE
              ? ' climate-slot-mode' : '') +
            '" data-climate-preview-item="' +
            slot.itemIndex + '" style="' +
            gridStyle + '"><strong>' + escapeHtml(slot.value) +
            '</strong></div>';
        }
        if (compact) {
          return '<div class="climate-slot climate-slot-control ' +
            'climate-slot-control-compact" ' +
            'data-climate-preview-item="' +
            slot.itemIndex + '" style="' + gridStyle + '">' +
            '<span class="climate-minus" aria-hidden="true">-</span>' +
            '<strong>' + escapeHtml(slot.value) + '</strong>' +
            '<span class="climate-plus" aria-hidden="true">+</span></div>';
        }
        const controlClass = horizontal
          ? 'climate-slot-control-horizontal'
          : (large
              ? 'climate-slot-control-large'
              : 'climate-slot-control-vertical ' +
                (columns > 1
                  ? (slot.col === 0
                      ? 'climate-slot-column-left'
                      : 'climate-slot-column-right')
                  : ''));
        return '<div class="climate-slot climate-slot-control ' +
          controlClass +
          '" data-climate-preview-item="' +
          slot.itemIndex + '" style="' + gridStyle + '">' +
          '<small>' + escapeHtml(slot.caption) + '</small>' +
          '<span class="climate-minus">-</span><strong>' +
          escapeHtml(slot.value) +
          '</strong><span class="climate-plus">+</span></div>';
      }).join('') +
      '</div>';
  }

  function saveClimateFields(tab, formData) {
    const packed = packClimateSlotConfig(tab);
    const packedLayouts = packClimateTargetLayouts(tab);
    const geometry = document.getElementById(
      tab + '_climate_geometry')?.value || '';
    formData.append('climate_entity',
      document.getElementById(tab + '_climate_entity')?.value || '');
    formData.append('popup_open_mode',
      document.getElementById(tab + '_climate_popup_open_mode')?.value || '1');
    formData.append('climate_slots_packed', String(packed));
    formData.append('climate_layouts_packed', String(packedLayouts));
    formData.append('climate_geometry', geometry);
    formData.append('scene_alias', geometry);
    // Keep the local tile/draft representation in sync with the V7 storage
    // field used by the firmware.
    formData.append('sensor_gauge_min', String(packed));
    formData.append(
      'sensor_gauge_max',
      String((CLIMATE_LAYOUT_MAGIC | packedLayouts) >>> 0));
  }

  function resetClimateFields(tab) {
    const entity = document.getElementById(tab + '_climate_entity');
    if (entity) {
      entity.value = '';
      delete entity.dataset.configuredValue;
    }
    const popup = document.getElementById(tab + '_climate_popup_open_mode');
    if (popup) popup.value = '1';
    const geometry = document.getElementById(
      tab + '_climate_geometry');
    if (geometry) geometry.value = '';
    for (let index = 0; index < 6; ++index) {
      const select = document.getElementById(
        tab + '_climate_slot_' + index);
      if (select) select.value = String(CLIMATE_TILE_CONTENT.AUTO);
      const layout = document.getElementById(
        tab + '_climate_layout_' + index);
      if (layout) {
        layout.value = String(CLIMATE_TARGET_LAYOUT.AUTO);
      }
    }
    syncClimateSlotFields(tab);
  }
  bindClimatePreviewSelection();

function loadCameraFields(tab, data) {
    const el = document.getElementById(tab + '_camera_entity');
    const configured = data.sensor_entity || data.camera_entity || '';
    if (el) {
      if (configured) {
        el.dataset.configuredValue = configured;
        if (!Array.from(el.options).some(opt => opt.value === configured)) {
          const opt = document.createElement('option');
          opt.value = configured;
          opt.textContent = configured;
          el.appendChild(opt);
        }
      } else {
        delete el.dataset.configuredValue;
      }
      el.value = configured;
    }
    maybeFillTitleFromEntity(tab, '_camera_entity');
  }
  function saveCameraFields(tab, formData) {
    const entity =
      document.getElementById(tab + '_camera_entity')?.value || '';
    formData.append('camera_entity', entity);
    formData.append('sensor_entity', entity);
  }
  function resetCameraFields(tab) {
    const el = document.getElementById(tab + '_camera_entity');
    if (el) {
      el.value = '';
      delete el.dataset.configuredValue;
    }
  }

function loadAnimationFields(tab, data) {
    const el = document.getElementById(tab + '_animation_file');
    if (el) {
      // The chosen file name is stored in scene_alias (see firmware web_handler).
      const val = (data && (data.animation_file !== undefined ? data.animation_file
                           : data.scene_alias)) || '';
      el.value = val;
      if (el.value !== val) el.value = '';  // saved file gone from SD -> none
    }
    // Speed (fps) is stored in image_slideshow_sec for this tile type.
    const fps = document.getElementById(tab + '_animation_fps');
    if (fps) {
      let v = data && data.image_slideshow_sec ? parseInt(data.image_slideshow_sec, 10) : 10;
      if (!(v >= 1 && v <= 30)) v = 10;
      fps.value = v;
      const lbl = document.getElementById(tab + '_animation_fps_val');
      if (lbl) lbl.textContent = v;
    }
    const fit = document.getElementById(tab + '_animation_fit');
    if (fit) {
      let v = data && data.animation_fit !== undefined
        ? parseInt(data.animation_fit, 10)
        : (data && data.sensor_display_mode !== undefined ? parseInt(data.sensor_display_mode, 10) : 0);
      if (!(v >= 0 && v <= 2)) v = 0;
      fit.value = String(v);
    }
    const zoom = document.getElementById(tab + '_animation_zoom');
    if (zoom) {
      let v = data && data.animation_zoom !== undefined
        ? parseInt(data.animation_zoom, 10)
        : (data && data.sensor_gauge_max !== undefined ? parseInt(data.sensor_gauge_max, 10) : 100);
      if (!(v >= 25 && v <= 300)) v = 100;
      zoom.value = v;
      const lbl = document.getElementById(tab + '_animation_zoom_val');
      if (lbl) lbl.textContent = v;
    }
  }

  function saveAnimationFields(tab, formData) {
    const el = document.getElementById(tab + '_animation_file');
    formData.append('animation_file', el ? (el.value || '') : '');
    const fps = document.getElementById(tab + '_animation_fps');
    formData.append('animation_fps', fps ? (fps.value || '10') : '10');
    const fit = document.getElementById(tab + '_animation_fit');
    formData.append('animation_fit', fit ? (fit.value || '0') : '0');
    const zoom = document.getElementById(tab + '_animation_zoom');
    formData.append('animation_zoom', zoom ? (zoom.value || '100') : '100');
  }

  function resetAnimationFields(tab) {
    const el = document.getElementById(tab + '_animation_file');
    if (el) el.value = '';
    const fps = document.getElementById(tab + '_animation_fps');
    if (fps) fps.value = 10;
    const lbl = document.getElementById(tab + '_animation_fps_val');
    if (lbl) lbl.textContent = '10';
    const fit = document.getElementById(tab + '_animation_fit');
    if (fit) fit.value = '0';
    const zoom = document.getElementById(tab + '_animation_zoom');
    if (zoom) zoom.value = 100;
    const zoomLbl = document.getElementById(tab + '_animation_zoom_val');
    if (zoomLbl) zoomLbl.textContent = '100';
  }

function getClockPreviewLanguage() {
    return document.getElementById('language')?.value || document.documentElement.lang || 'en';
  }

  function normalizeClockTimeFormat(raw) {
    const num = Number(raw);
    return (num === 1 || num === 2) ? num : 0;
  }

  function normalizeClockDateFormat(raw) {
    const num = Number(raw);
    return (num === 1 || num === 2 || num === 3) ? num : 0;
  }

  function resolveClockTimeFormat(raw) {
    const safe = normalizeClockTimeFormat(raw);
    if (safe !== 0) return safe;
    return getClockPreviewLanguage().toLowerCase().startsWith('de') ? 1 : 2;
  }

  function resolveClockDateFormat(raw) {
    const safe = normalizeClockDateFormat(raw);
    if (safe !== 0) return safe;
    return getClockPreviewLanguage().toLowerCase().startsWith('de') ? 1 : 2;
  }

  function getClockPreviewTime(rawFormat) {
    const now = new Date();
    const format = resolveClockTimeFormat(rawFormat);
    if (format === 2) {
      let hh = now.getHours() % 12;
      if (hh === 0) hh = 12;
      const mm = String(now.getMinutes()).padStart(2, '0');
      return String(hh) + ':' + mm + (now.getHours() < 12 ? ' AM' : ' PM');
    }
    const hh = String(now.getHours()).padStart(2, '0');
    const mm = String(now.getMinutes()).padStart(2, '0');
    return hh + ':' + mm;
  }

  function getClockPreviewDate(rawFormat) {
    const now = new Date();
    const format = resolveClockDateFormat(rawFormat);
    const dd = String(now.getDate()).padStart(2, '0');
    const mm = String(now.getMonth() + 1).padStart(2, '0');
    const yyyy = String(now.getFullYear());
    if (format === 2) return mm + '/' + dd + '/' + yyyy;
    if (format === 3) return yyyy + '/' + mm + '/' + dd;
    return dd + '.' + mm + '.' + yyyy;
  }

  function normalizeClockPreviewFont(raw, fallback) {
    const num = Number(raw);
    switch (num) {
      case 20:
      case 24:
      case 28:
      case 32:
      case 40:
      case 48:
      case 56:
      case 64:
      case 72:
      case 80:
      case 96:
        return num;
      default:
        return fallback;
    }
  }

  function getClockPreviewCssPx(raw, fallback) {
    const n = normalizeClockPreviewFont(raw, fallback);
    // Same scaling as the CSS variables (LVGL pixels * preview factor).
    const v = parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--fs' + n));
    return (v > 0) ? v : Math.round(n / 2);
  }

  function getClockPreviewTextStyle(raw, fallback, color) {
    const size = getClockPreviewCssPx(raw, fallback);
    const safeColor = color || '#fff';
    return 'style="font-size:' + size + 'px; line-height:1; color:' + safeColor + ';"';
  }

  function applyClockPreviewTextStyle(el, raw, fallback, color, lineHeight) {
    if (!el) return;
    const size = getClockPreviewCssPx(raw, fallback);
    el.style.fontSize = size + 'px';
    el.style.color = color || '#fff';
    el.style.lineHeight = lineHeight || '1';
  }

  function normalizeClockFlags(raw) {
    const num = Number(raw);
    if (!Number.isFinite(num) || num < 0) return 1;
    const flags = num & 3;
    return flags === 0 ? 1 : flags;
  }

  function getClockFlagsFromInputs(prefix) {
    const timeEl = document.getElementById(prefix + '_clock_show_time');
    const dateEl = document.getElementById(prefix + '_clock_show_date');
    let flags = 0;
    if (timeEl && timeEl.checked) flags |= 1;
    if (dateEl && dateEl.checked) flags |= 2;
    if (flags === 0) flags = 1;
    return flags;
  }

  function applyClockFlagsToInputs(prefix, flags) {
    const safe = normalizeClockFlags(flags);
    const timeEl = document.getElementById(prefix + '_clock_show_time');
    const dateEl = document.getElementById(prefix + '_clock_show_date');
    if (timeEl) timeEl.checked = (safe & 1) !== 0;
    if (dateEl) dateEl.checked = (safe & 2) !== 0;
  }

  function ensureClockSelection(prefix) {
    const timeEl = document.getElementById(prefix + '_clock_show_time');
    const dateEl = document.getElementById(prefix + '_clock_show_date');
    if (!timeEl || !dateEl) return;
    if (!timeEl.checked && !dateEl.checked) timeEl.checked = true;
  }

  function loadClockFields(tab, data) {
    const timeFontEl = document.getElementById(tab + '_clock_time_font');
    if (timeFontEl) {
      const timeFont = (data && data.key_code !== undefined) ? Number(data.key_code) : 40;
      timeFontEl.value = String(timeFont);
    }
    const dateFontEl = document.getElementById(tab + '_clock_date_font');
    if (dateFontEl) {
      const storedDateFont = (data && data.key_modifier !== undefined)
        ? Number(data.key_modifier) : 20;
      const dateFont = Math.min(72, storedDateFont || 20);
      dateFontEl.value = String(dateFont);
    }
    const timeFormatEl = document.getElementById(tab + '_clock_time_format');
    if (timeFormatEl) {
      const timeFormat = (data && data.sensor_gauge_min !== undefined) ? data.sensor_gauge_min : (data ? data.clock_time_format : 0);
      timeFormatEl.value = String(timeFormat !== undefined ? timeFormat : 0);
    }
    const dateFormatEl = document.getElementById(tab + '_clock_date_format');
    if (dateFormatEl) {
      const dateFormat = (data && data.sensor_gauge_max !== undefined) ? data.sensor_gauge_max : (data ? data.clock_date_format : 0);
      dateFormatEl.value = String(dateFormat !== undefined ? dateFormat : 0);
    }
    if (data && (data.clock_show_time !== undefined || data.clock_show_date !== undefined)) {
      const showTime = String(data.clock_show_time || '0') === '1';
      const showDate = String(data.clock_show_date || '0') === '1';
      let flags = 0;
      if (showTime) flags |= 1;
      if (showDate) flags |= 2;
      if (flags === 0) flags = 1;
      applyClockFlagsToInputs(tab, flags);
      return;
    }
    const flags = (data && data.clock_flags !== undefined && data.clock_flags !== null)
      ? data.clock_flags
      : (data ? data.sensor_decimals : 1);
    applyClockFlagsToInputs(tab, flags);
  }

  function updateClockValuePreview(tab) {
    if (currentTileIndex === -1) return;
    const prefix = tab;
    const tileId = tab + '-tile-' + currentTileIndex;
    const tileElem = document.getElementById(tileId);
    if (!tileElem) return;

    const flags = getClockFlagsFromInputs(prefix);
    const timeFont = document.getElementById(prefix + '_clock_time_font')?.value || '40';
    const dateFont = Math.min(72,
      Number(document.getElementById(prefix + '_clock_date_font')?.value || 20));
    const timeFormat = document.getElementById(prefix + '_clock_time_format')?.value || '0';
    const dateFormat = document.getElementById(prefix + '_clock_date_format')?.value || '0';
    const timeEl = tileElem.querySelector('.tile-clock-time');
    const dateEl = tileElem.querySelector('.tile-clock-date');

    const needsTime = (flags & 1) !== 0;
    const needsDate = (flags & 2) !== 0;
    if ((needsTime && !timeEl) || (needsDate && !dateEl) || (!needsTime && timeEl) || (!needsDate && dateEl)) {
      updateTilePreview(tab);
      return;
    }

    if (timeEl) {
      timeEl.textContent = getClockPreviewTime(timeFormat);
      applyClockPreviewTextStyle(timeEl, timeFont, 40, '#fff', '1');
    }
    if (dateEl) {
      dateEl.textContent = getClockPreviewDate(dateFormat);
      applyClockPreviewTextStyle(dateEl, dateFont, 24, '#fff', '1.1');
    }
  }

  function saveClockFields(tab, formData) {
    ensureClockSelection(tab);
    const flags = getClockFlagsFromInputs(tab);
    formData.append('clock_show_time', (flags & 1) ? '1' : '0');
    formData.append('clock_show_date', (flags & 2) ? '1' : '0');
    formData.append('key_code', document.getElementById(tab + '_clock_time_font')?.value || '40');
    formData.append('key_modifier', document.getElementById(tab + '_clock_date_font')?.value || '20');
    formData.append('clock_time_format', document.getElementById(tab + '_clock_time_format')?.value || '0');
    formData.append('clock_date_format', document.getElementById(tab + '_clock_date_format')?.value || '0');
  }

  function resetClockFields(tab) {
    applyClockFlagsToInputs(tab, 1);
    const timeFontEl = document.getElementById(tab + '_clock_time_font');
    if (timeFontEl) timeFontEl.value = '40';
    const dateFontEl = document.getElementById(tab + '_clock_date_font');
    if (dateFontEl) dateFontEl.value = '24';
    const timeFormatEl = document.getElementById(tab + '_clock_time_format');
    if (timeFormatEl) timeFormatEl.value = '0';
    const dateFormatEl = document.getElementById(tab + '_clock_date_format');
    if (dateFormatEl) dateFormatEl.value = '0';
  }

function normalizeTextValueFont(value) {
    const v = String(value || '0');
    return (['1','2','3','4'].includes(v)) ? v : '0';
  }

  function loadTextFields(tab, data) {
    const prefix = tab;
    const textEl = document.getElementById(prefix + '_text_value');
    const fontEl = document.getElementById(prefix + '_text_value_font');
    if (!textEl) return;
    if (data && data.text_value !== undefined) {
      textEl.value = data.text_value || '';
    } else if (data && data.scene_alias !== undefined) {
      textEl.value = data.scene_alias || '';
    } else if (data && data.key_macro !== undefined) {
      textEl.value = data.key_macro || '';
    } else {
      textEl.value = '';
    }
    if (fontEl) {
      if (data && data.text_value_font !== undefined) {
        fontEl.value = normalizeTextValueFont(data.text_value_font);
      } else if (data && data.sensor_value_font !== undefined) {
        fontEl.value = normalizeTextValueFont(data.sensor_value_font);
      } else {
        fontEl.value = '0';
      }
    }
  }

  function saveTextFields(tab, formData) {
    const prefix = tab;
    formData.append('text_value', document.getElementById(prefix + '_text_value')?.value || '');
    formData.append('text_value_font', document.getElementById(prefix + '_text_value_font')?.value || '0');
  }

  function resetTextFields(tab) {
    const prefix = tab;
    const textEl = document.getElementById(prefix + '_text_value');
    if (textEl) textEl.value = '';
    const fontEl = document.getElementById(prefix + '_text_value_font');
    if (fontEl) fontEl.value = '0';
  }
  function isEditablePreview(kind) { return ['number', 'select', 'datetime'].includes(kind); }
  function editablePreviewText(entity, kind, meta = sensorMetaCache) {
    let value = meta?.editableValues?.[entity];
    if (typeof value === 'string') { try { value = JSON.parse(value); } catch (_) { return '--'; } }
    if (!value || value.version !== 1 || value.state === null || value.state === undefined) return '--';
    const tr = kind === 'number' ? NUMBER_I18N : kind === 'select' ? SELECT_I18N : DATETIME_I18N;
    if (!value.available || value.state === 'unavailable') return tr.unavailable;
    if (value.state === 'unknown') return tr.unknown;
    if (value.kind === 'number') {
      if (!String(value.state).trim() || !Number.isFinite(Number(value.state))) return tr.unknown;
      return formatSensorValue(String(value.state), undefined) + (value.unit ? ' ' + value.unit : '');
    }
    return String(value.state);
  }

  function loadNumberFields(tab, data) {
    const font = document.getElementById(tab + '_number_value_font');
    if (font) font.value = String(data.sensor_value_font ?? 2);
    const entity = document.getElementById(tab + '_number_entity');
    const configured = data.sensor_entity || data.number_entity || '';
    if (entity) {
      if (configured) {
        entity.dataset.configuredValue = configured;
        if (!Array.from(entity.options).some(option => option.value === configured)) {
          const option = document.createElement('option');
          option.value = configured;
          option.textContent = configured;
          entity.appendChild(option);
        }
      } else {
        delete entity.dataset.configuredValue;
      }
      entity.value = configured;
    }
    const popup = document.getElementById(
      tab + '_number_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
  }

  function saveNumberFields(tab, formData) {
    formData.append('sensor_value_font', document.getElementById(tab + '_number_value_font')?.value ?? '2');
    const entityEl = document.getElementById(tab + '_number_entity');
    const entity = entityEl
      ? (entityEl.value || entityEl.dataset.configuredValue || '') : '';
    formData.append('number_entity', entity);
    formData.append('sensor_entity', entity);
    const popup = document.getElementById(
      tab + '_number_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetNumberFields(tab) {
    const font = document.getElementById(tab + '_number_value_font');
    if (font) font.value = '2';
    const entity = document.getElementById(tab + '_number_entity');
    if (entity) {
      entity.value = '';
      delete entity.dataset.configuredValue;
    }
    const popup = document.getElementById(
      tab + '_number_popup_open_mode');
    if (popup) popup.value = '1';
  }

  function loadSelectFields(tab, data) {
    const font = document.getElementById(tab + '_select_value_font');
    if (font) font.value = String(data.sensor_value_font ?? 2);
    const entity = document.getElementById(tab + '_select_entity');
    const configured = data.sensor_entity || data.select_entity || '';
    if (entity) {
      if (configured) {
        entity.dataset.configuredValue = configured;
        if (!Array.from(entity.options).some(option => option.value === configured)) {
          const option = document.createElement('option');
          option.value = configured;
          option.textContent = configured;
          entity.appendChild(option);
        }
      } else {
        delete entity.dataset.configuredValue;
      }
      entity.value = configured;
    }
    const popup = document.getElementById(
      tab + '_select_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
  }

  function saveSelectFields(tab, formData) {
    formData.append('sensor_value_font', document.getElementById(tab + '_select_value_font')?.value ?? '2');
    const entityEl = document.getElementById(tab + '_select_entity');
    const entity = entityEl
      ? (entityEl.value || entityEl.dataset.configuredValue || '') : '';
    formData.append('select_entity', entity);
    formData.append('sensor_entity', entity);
    const popup = document.getElementById(
      tab + '_select_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetSelectFields(tab) {
    const font = document.getElementById(tab + '_select_value_font');
    if (font) font.value = '2';
    const entity = document.getElementById(tab + '_select_entity');
    if (entity) {
      entity.value = '';
      delete entity.dataset.configuredValue;
    }
    const popup = document.getElementById(
      tab + '_select_popup_open_mode');
    if (popup) popup.value = '1';
  }

  function loadDateTimeFields(tab, data) {
    const font = document.getElementById(tab + '_datetime_value_font');
    if (font) font.value = String(data.sensor_value_font ?? 2);
    const entity = document.getElementById(tab + '_datetime_entity');
    const configured = data.sensor_entity || data.datetime_entity || '';
    if (entity) {
      if (configured) {
        entity.dataset.configuredValue = configured;
        if (!Array.from(entity.options).some(option => option.value === configured)) {
          const option = document.createElement('option');
          option.value = configured;
          option.textContent = configured;
          entity.appendChild(option);
        }
      } else {
        delete entity.dataset.configuredValue;
      }
      entity.value = configured;
    }
    const popup = document.getElementById(
      tab + '_datetime_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
  }

  function saveDateTimeFields(tab, formData) {
    formData.append('sensor_value_font', document.getElementById(tab + '_datetime_value_font')?.value ?? '2');
    const entityEl = document.getElementById(tab + '_datetime_entity');
    const entity = entityEl
      ? (entityEl.value || entityEl.dataset.configuredValue || '') : '';
    formData.append('datetime_entity', entity);
    formData.append('sensor_entity', entity);
    const popup = document.getElementById(
      tab + '_datetime_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetDateTimeFields(tab) {
    const font = document.getElementById(tab + '_datetime_value_font');
    if (font) font.value = '2';
    const entity = document.getElementById(tab + '_datetime_entity');
    if (entity) {
      entity.value = '';
      delete entity.dataset.configuredValue;
    }
    const popup = document.getElementById(
      tab + '_datetime_popup_open_mode');
    if (popup) popup.value = '1';
  }

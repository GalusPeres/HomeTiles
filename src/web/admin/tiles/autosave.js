
  let notificationTimer = null;

  function showNotification(message, success = true) {
    const notification = document.getElementById('notification');
    if (!notification) return;
    // The layout window keeps the editor's saves to itself (layout-window.js).
    if (success && typeof layoutWindowOpen === 'function' && layoutWindowOpen()) return;
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
    resetScreensaverLookFields(tab);
  }

  // A folder that still contains tiles keeps its type: the whole type list
  // is greyed out (user 2026-10-12: choosing Empty deleted a whole folder
  // with its tiles at once); it goes only by the Delete button after a
  // question. applySpecialTileUiState keeps the lock.
  function applyFolderTypeLock(tab, locked) {
    const sel = document.getElementById(tab + '_tile_type');
    if (!sel) return;
    sel.dataset.folderLocked = locked ? '1' : '';
    sel.disabled = locked || !!getTileTypeMeta(sel.value).locked;
    // Why it is grey, on hover (user 2026-10-12): on the field and its box,
    // since a disabled field gets no pointer of its own in every browser.
    const tip = locked ? t('folderTypeLocked') : '';
    for (const el of [sel, sel.parentElement]) {
      if (!el) continue;
      if (tip) el.title = tip;
      else el.removeAttribute('title');
    }
  }

  // Folders whose deletion was confirmed (tab:index): their save says so.
  const confirmedFolderDeletes = new Set();

  function resetTile(tab) {
    if (currentTileIndex === -1) return;
    const tileType = getCurrentTileType(tab);
    if (isLockedTileType(tileType)) {
      showNotification(t('tileCannotDelete'), false);
      return;
    }
    // A folder with tiles in it: the question names it, its tiles go too.
    const stored = getTilesData(tab)?.[currentTileIndex];
    if (Number(stored?.type) === 4 && stored.folder_empty === false) {
      const name = stored.title || t('folderPrefix').trim();
      if (!confirm(tf('deleteFolderConfirm', { name }))) return;
      confirmedFolderDeletes.add(tab + ':' + currentTileIndex);
    }
    // A page's tile: its field (page-fields.js).
    const pageSlot = tab === 'folder0' && Number(stored?.type) === 4
      ? pageSlotIds().indexOf(Number(stored.navigate_target)) : -1;
    const prefix = tab;
    document.getElementById(prefix + '_tile_type').value = '0';
    document.getElementById(prefix + '_tile_title').value = '';
    document.getElementById(prefix + '_tile_icon').value = '';
    setTileColorInputFromStored(tab, 0, '#2A2A2A');
    resetAllTypeFields(tab);
    syncGaugeUi(tab);
    updateTileType(tab);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
    // A deleted page leaves its field free and picked (user 2026-10-22: an
    // empty selected tile stayed in it). The save keeps its own tile index.
    if (pageSlot >= 0) {
      layoutTiles(tab, getTilesData(tab));
      selectPageField(pageSlot);
    }
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
          forgetFolderTabSessionFragment(folderId);
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

  // A new folder's title and icon as the panel saved them ("Folder 17" and
  // the folder icon, user 2026-10-22: its tab showed them, the tile had
  // none): into the tile, a newer draft and the editor wherever still empty.
  function fillFolderName(tab, index, data) {
    const title = String(data.title || '');
    const icon = String(data.icon_name || '');
    const stored = getTilesData(tab)?.[index];
    if (stored) {
      if (!stored.title) stored.title = title;
      if (!stored.icon_name) stored.icon_name = icon;
    }
    const draft = drafts[tab]?.[index];
    if (draft) {
      if (!draft.title) draft.title = title;
      if (!draft.icon) draft.icon = icon;
    }
    if (currentTileTab === tab && currentTileIndex === index) {
      const titleInput = document.getElementById(tab + '_tile_title');
      if (titleInput && !titleInput.value) titleInput.value = title;
      const iconInput = document.getElementById(tab + '_tile_icon');
      if (iconInput && !iconInput.value) iconInput.value = icon;
      if (typeof refreshTileIconButtons === 'function') refreshTileIconButtons(tab);
      updateTilePreview(tab);
    } else if (stored) {
      renderTileFromData(tab, index, stored, sensorMetaCache);
    }
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
    if ((snapshot.type || '0') === '0' && confirmedFolderDeletes.delete(tab + ':' + tileIndex)) {
      formData.append('delete_folder', '1');
    }
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
    // The layout window keeps the editor's places to itself (layout-window.js).
    if (typeof layoutWindowOpen === 'function' && layoutWindowOpen()) {
      clearDraft(tab, tileIndex);
      return;
    }
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
          // A new tile's stable ID from the panel: its places are kept by it
          // (the layout window looked a new folder's up without it).
          if (data.view_id !== undefined && tilesData[tab] && tilesData[tab][tileIndex] &&
              Number(tilesData[tab][tileIndex].type || 0) !== 0) {
            tilesData[tab][tileIndex].view_id = Number(data.view_id) || 0;
          }
          if (typeValue === '4' && data.title !== undefined) fillFolderName(tab, tileIndex, data);
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
            // The tile knows its folder at once (user 2026-10-12: a new folder
            // went into a page slot only after a few clicks; an autosave
            // draws nothing again).
            const tileEl = document.getElementById(tab + '-tile-' + tileIndex);
            if (tileEl) tileEl.dataset.navigateTarget = resolvedNavTarget;
            if (tilesData[tab] && tilesData[tab][tileIndex]) {
              tilesData[tab][tileIndex].navigate_target = parseInt(resolvedNavTarget, 10) || 0;
              tilesData[tab][tileIndex].folder_pin_enabled =
                data?.folder_pin_enabled === true;
              tilesData[tab][tileIndex].folder_pin =
                String(data?.folder_pin || '');
            }
            syncFolderPinControls(tab);
            const navTargetNum = parseInt(resolvedNavTarget, 10);
            const titleVal = tilesData[tab]?.[tileIndex]?.title || snapshot.title || '';
            const iconVal = tilesData[tab]?.[tileIndex]?.icon_name || snapshot.icon || '';
            ensureFolderTabUi(navTargetNum, titleVal, iconVal).then(ok => {
              restoreCurrentTileSelectionUi();
              if (!ok) {
                persistSelectedTileState();
                setTimeout(() => location.reload(), 400);
              }
            });
          }
          // A deleted folder's tab goes (user 2026-10-22: it stayed). The
          // editor showed the tile empty before this save, so the panel says
          // which folder went; its kept tab markup goes too (IDs come back).
          const deletedFolder = Number(data.folder_deleted) || 0;
          if (deletedFolder > 0) {
            forgetFolderTabSessionFragment(deletedFolder);
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

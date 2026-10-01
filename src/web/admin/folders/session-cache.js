
  // Folder tab markup is kept in localStorage, so a second browser tab or a
  // new window opens folders without asking the device again. The namespace
  // carries the per-boot token, so nothing survives a reboot or OTA; the tile
  // data itself is always fetched fresh and re-rendered over the markup.
  const FOLDER_TAB_SESSION_CACHE_PREFIX = 'hometilesAdminFolderTabs:';
  const FOLDER_TAB_SESSION_CACHE_VERSION = 2;
  const FOLDER_TAB_SESSION_CACHE_LIMIT = 8;

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
      const raw = localStorage.getItem(folderTabSessionIndexKey());
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
      localStorage.setItem(folderTabSessionIndexKey(), JSON.stringify({
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
        localStorage.setItem(
          folderTabSessionEntryKey(folderId), String(data.tab_html));
        stored = true;
      } catch (error) {
        const evicted = entries.pop();
        if (!evicted) break;
        try {
          localStorage.removeItem(
            folderTabSessionEntryKey(evicted.folder_id));
        } catch (removeError) {}
      }
    }
    if (!stored) return;

    const nextEntries = [entry, ...entries];
    while (nextEntries.length > FOLDER_TAB_SESSION_CACHE_LIMIT) {
      const evicted = nextEntries.pop();
      try {
        localStorage.removeItem(
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
      localStorage.removeItem(folderTabSessionEntryKey(folderNum));
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
      const tabHtml = localStorage.getItem(
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
      for (let index = 0; index < localStorage.length; index += 1) {
        const key = localStorage.key(index) || '';
        storageKeys.push(key);
      }
      storageKeys.filter(key =>
        key.startsWith(FOLDER_TAB_SESSION_CACHE_PREFIX) &&
        !key.startsWith(namespace + ':'))
        .forEach(key => localStorage.removeItem(key));
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
        try { localStorage.removeItem(key); } catch (error) {}
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

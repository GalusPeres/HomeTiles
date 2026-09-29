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
    if (active) {
      active.classList.add('active');
      active.setAttribute('aria-current', 'page');
    }
    syncFolderMenuButton(active || null);
  }

  // Every folder besides Home sits in the Folders menu. Its button stands for
  // the open folder (icon and name, marked active), else it shows its own
  // label; picking a folder closes the menu.
  function syncFolderMenuButton(active) {
    const menuButton = document.getElementById('folderMenuButton');
    if (!menuButton) return;
    const inMenu = !!active && !!active.closest('.folder-menu-list');
    menuButton.classList.toggle('active', inMenu);
    const label = menuButton.querySelector('.folder-menu-label');
    if (label) {
      label.textContent = inMenu
        ? (active.dataset.folderName || active.textContent.trim())
        : (menuButton.dataset.defaultLabel || '');
    }
    const icon = menuButton.querySelector('.folder-menu-icon');
    if (icon) {
      const name = inMenu && active.dataset.folderIcon ? active.dataset.folderIcon : 'folder-multiple';
      icon.className = 'mdi folder-menu-icon mdi-' + name;
    }
    toggleFolderMenu(false);
  }

  function toggleFolderMenu(open) {
    const list = document.getElementById('folderMenuList');
    const button = document.getElementById('folderMenuButton');
    if (!list || !button) return;
    const show = open === undefined ? list.hidden : !!open;
    list.hidden = !show;
    button.setAttribute('aria-expanded', show ? 'true' : 'false');
    button.classList.toggle('open', show);
  }

  document.addEventListener('click', event => {
    if (!event.target.closest || !event.target.closest('.folder-menu')) toggleFolderMenu(false);
  });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') toggleFolderMenu(false);
  });

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

  // The Tile settings panel takes the height of the tile editor row, which
  // fills the card on wide windows (admin.css); narrow windows stack it below
  // the grid. Only an inline cap from an earlier layout is cleared here.
  function updateTileSettingsMaxHeight() {
    document.querySelectorAll('.tile-settings').forEach(panel => { panel.style.maxHeight = ''; });
  }
  // Resize fires many times per second while a window is dragged, and the
  // screensaver handler below re-renders the whole screensaver editor.
  // Coalescing to one call per frame keeps that work off every single event.
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
    if (typeof fitCompactClockPreview === 'function') {
      document.querySelectorAll('.tile.clock-compact').forEach(fitCompactClockPreview);
    }
    if (screensaverDraft) {
      const time = document.getElementById('screensaverClockTime');
      const date = document.getElementById('screensaverClockDate');
      if (time) time.textContent = getClockPreviewTime(screensaverDraft.time_format);
      if (date) date.textContent = getScreensaverClockPreviewDate(screensaverDraft);
    }
  }

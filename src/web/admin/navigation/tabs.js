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


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
    syncTileRadiusControls(tabEl);

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

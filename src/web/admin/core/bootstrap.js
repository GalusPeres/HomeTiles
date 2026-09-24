
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
      enableFreeSlotHover(tab);
    });
    enableSettingsHiddenSlot();
    associateFieldLabels();
    fillStaticClockPreviews();
    setInterval(fillStaticClockPreviews, 30000);
    updateTileSettingsMaxHeight();
  });

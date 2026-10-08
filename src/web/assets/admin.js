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

  // Issue #73: the panel builds this page in its own language, so a language
  // change made on the display shows only after a reload. A small poll
  // notices it and reloads once nothing is being edited here.
  const DEVICE_LANGUAGE_POLL_MS = 15000;

  function adminEditsPending() {
    if (typeof layoutWindowOpen === 'function' && layoutWindowOpen()) return true;
    const active = document.activeElement;
    if (active?.matches?.('input, textarea, select')) return true;
    if (dragSource || resizeState || fileManagerUploadBusy || hardwareIoDirty) return true;
    const busy = map => Object.values(map || {}).some(Boolean);
    return busy(autoSaveTimers) || busy(saveInFlightByTile) || busy(queuedSaveByTile);
  }

  async function checkDeviceLanguage() {
    if (document.hidden) return;
    try {
      const response = await fetch('/api/language', {cache: 'no-store'});
      if (!response.ok) return;
      const data = await response.json();
      const device = String(data?.language || '').toLowerCase();
      const page = String(document.documentElement.lang || APP_LOCALE).toLowerCase();
      if (device && device !== page && !adminEditsPending()) location.reload();
    } catch (error) {
      // Offline or signed out: the next poll tries again.
    }
  }

  function watchDeviceLanguage() {
    window.setInterval(checkDeviceLanguage, DEVICE_LANGUAGE_POLL_MS);
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden) checkDeviceLanguage();
    });
  }
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

// The gear in Home's head with a bar layout (a Settings tile option): saved
// at once, the preview's head follows.
async function saveHeadGear(enabled) {
  const show = value => {
    document.querySelectorAll('.settings-head-gear').forEach(box => { box.checked = value; });
    document.querySelectorAll('.head-bar-preview .head-gear').forEach(gear => gear.classList.toggle('is-off', !value));
  };
  show(!!enabled);
  try {
    const response = await fetch('/api/display/head-gear', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'enabled=' + (enabled ? '1' : '0')
    });
    if (!response.ok) throw new Error('HTTP ' + response.status);
  } catch (error) {
    show(!enabled);
    showNotification(t('networkErrorSave'), false);
  }
}

// A layout (grid_layout.h) is read at boot: the layout window restarts the
// panel after choosing one.
function restartPanelForLayout() {
  const restartForm = document.getElementById('admin_restart_form');
  // form.submit() cannot carry the CSRF header a password-protected panel
  // requires (hardware/editor.js restartHardwareIoNow).
  if (restartForm && !window.HomeTilesAuth?.csrfToken()) {
    window.setTimeout(() => restartForm.submit(), 100);
    return;
  }
  fetch('/restart', {method: 'POST'}).catch(() => {}).finally(() => {
    window.setTimeout(() => window.location.assign('/'), 300);
  });
}
// The head bar preview's time (web_admin_html.cpp), from the browser clock
// in the panel's 12/24-hour format.
function refreshHeadBarTime() {
  const now = new Date();
  document.querySelectorAll('.head-bar-preview .head-time').forEach(el => {
    let hour = now.getHours();
    if (el.dataset.hour12 === '1') hour = hour % 12 || 12;
    el.textContent = String(hour).padStart(2, '0') + ':' + String(now.getMinutes()).padStart(2, '0');
  });
}
// The layout window shows the head with any layout.
if (typeof window !== 'undefined' && window.setInterval) {
  window.setInterval(refreshHeadBarTime, 30000);
}

// Icon discs are a root class: every preview grid, including cached and lazily
// inserted folders, follows it without re-rendering a tile.
let iconDiscsSaveSequence = 0;
function iconDiscsEnabled() {
  return !document.documentElement.classList.contains('icon-discs-off');
}
function applyIconDiscsPreview(enabled) {
  document.documentElement.classList.toggle('icon-discs-off', !enabled);
  document.querySelectorAll('.global-icon-disc-toggle').forEach(input => {
    input.checked = !!enabled;
  });
  // A dark icon is lifted against its circle or, without one, the tile.
  document.querySelectorAll('.tile').forEach(tile => applyIconDiscTint(tile));
}
async function saveIconDiscs(enabled) {
  const wanted = !!enabled;
  const sequence = ++iconDiscsSaveSequence;
  applyIconDiscsPreview(wanted);
  try {
    const response = await fetch('/api/display/icon-discs', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'enabled=' + (wanted ? '1' : '0')
    });
    if (!response.ok) throw new Error('HTTP ' + response.status);
  } catch (error) {
    if (sequence !== iconDiscsSaveSequence) return;
    applyIconDiscsPreview(!wanted);
    showNotification(t('networkErrorSave'), false);
  }
}

// Glow strength of colored icon discs: previews read the root variable
// --icon-glow-pct (applyIconDiscTint); the device rebuilds its tiles after
// the save. Range and step mirror icon_glow.h.
let iconGlowConfirmed = null;
let iconGlowSaveSequence = 0;
function currentIconGlow() {
  const raw = String(getComputedStyle(document.documentElement).getPropertyValue('--icon-glow-pct')).trim();
  const value = Number(raw);
  return raw !== '' && Number.isFinite(value) ? value : 25;
}
function previewIconGlowLive(value) {
  const number = Math.round(Number(value) / 5) * 5;
  const percent = Math.min(100, Math.max(0, Number.isFinite(number) ? number : 25));
  if (iconGlowConfirmed === null) iconGlowConfirmed = currentIconGlow();
  document.documentElement.style.setProperty('--icon-glow-pct', String(percent));
  document.querySelectorAll('.global-icon-glow').forEach(input => { input.value = String(percent); });
  document.querySelectorAll('.global-icon-glow-value').forEach(output => { output.textContent = percent + ' %'; });
  document.querySelectorAll('.tile').forEach(tile => applyIconDiscTint(tile));
  return percent;
}
async function saveIconGlow(value) {
  const percent = previewIconGlowLive(value);
  const sequence = ++iconGlowSaveSequence;
  try {
    const response = await fetch('/api/display/icon-glow', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'percent=' + percent
    });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    if (sequence === iconGlowSaveSequence) iconGlowConfirmed = percent;
  } catch (error) {
    if (sequence !== iconGlowSaveSequence) return;
    const confirmed = iconGlowConfirmed;
    iconGlowConfirmed = null;
    if (confirmed !== null) previewIconGlowLive(confirmed);
    showNotification(t('networkErrorSave'), false);
  }
}

// The global default tile color paints every tile without its own color
// through --tile-default-bg; reset and new tiles take it as their default.
let defaultTileColorConfirmed = null;
let defaultTileColorSaveSequence = 0;
function currentDefaultTileColor() {
  const value = getComputedStyle(document.documentElement)
    .getPropertyValue('--tile-default-bg').trim();
  return /^#[0-9a-f]{6}$/i.test(value) ? value.toUpperCase() : '#2A2A2A';
}
function previewDefaultTileColor(value) {
  const color = String(value || '').trim().toUpperCase();
  if (!/^#[0-9A-F]{6}$/.test(color)) return;
  if (defaultTileColorConfirmed === null) {
    defaultTileColorConfirmed = currentDefaultTileColor();
  }
  document.documentElement.style.setProperty('--tile-default-bg', color);
  Object.values(typeof TILE_TYPE_REGISTRY === 'object' ? TILE_TYPE_REGISTRY : {})
    .forEach(meta => { if (meta && meta.sharedBg) meta.defaultBg = color; });
  // Circles are opaque steps above their tile (tone_color.h): recompute them
  // for the new background.
  if (typeof applyIconDiscTint === 'function') document.querySelectorAll('.tile').forEach(tile => applyIconDiscTint(tile));
  document.querySelectorAll('.global-tile-color').forEach(input => { input.value = color; });
  // Open editors of tiles without their own color show the new default.
  document.querySelectorAll('input[type="color"][id$="_tile_color"]').forEach(input => {
    if (input.dataset.bgColorDefault !== '1') return;
    const tab = input.id.slice(0, -'_tile_color'.length);
    const type = document.getElementById(tab + '_tile_type')?.value || '0';
    if (getTileTypeMeta(type).sharedBg) input.value = color;
  });
  return color;
}
async function saveDefaultTileColor(value) {
  const color = previewDefaultTileColor(value);
  if (!color) return;
  const sequence = ++defaultTileColorSaveSequence;
  try {
    const response = await fetch('/api/display/tile-color', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: new URLSearchParams({color}).toString()
    });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const result = await response.json();
    if (!result.success || String(result.color).toUpperCase() !== color) {
      throw new Error('invalid response');
    }
    if (sequence === defaultTileColorSaveSequence) defaultTileColorConfirmed = color;
  } catch (error) {
    if (sequence !== defaultTileColorSaveSequence) return;
    previewDefaultTileColor(defaultTileColorConfirmed || color);
    showNotification(t('networkErrorSave'), false);
  }
}
function syncGlobalDisplayControls(tabEl) {
  tabEl.querySelectorAll('.global-icon-disc-toggle').forEach(input => {
    input.checked = iconDiscsEnabled();
  });
  const color = currentDefaultTileColor();
  tabEl.querySelectorAll('.global-tile-color').forEach(input => { input.value = color; });
  const glow = currentIconGlow();
  tabEl.querySelectorAll('.global-icon-glow').forEach(input => { input.value = String(glow); });
  tabEl.querySelectorAll('.global-icon-glow-value').forEach(output => { output.textContent = glow + ' %'; });
}

// The shared root variables also reach cached and lazily inserted folder grids.
let tileRadiusConfirmed = null;
let tileRadiusWanted = null;
let tileRadiusSaving = false;
let tileRadiusRevision = 0;
let tileRadiusPreviewTimer = null;
let tileRadiusLiveWanted = null;
function previewTileRadiusLive(value) {
  const radius = previewTileRadius(value);
  tileRadiusLiveWanted = radius;
  if (tileRadiusPreviewTimer === null) tileRadiusPreviewTimer = setTimeout(() => {
    tileRadiusPreviewTimer = null;
    queueTileRadius(tileRadiusLiveWanted, false);
  }, 80);
}
function previewTileRadius(value) {
  const input = document.querySelector('.global-tile-radius');
  if (!input) return;
  const radius = Math.max(Number(input.min), Math.min(Number(input.max), Math.round(Number(value))));
  if (!Number.isFinite(radius)) return;
  const root = document.documentElement;
  if (tileRadiusConfirmed === null) {
    tileRadiusConfirmed = Number(getComputedStyle(root).getPropertyValue('--tile-radius-device'));
  }
  const scale = Number(getComputedStyle(root).getPropertyValue('--radius-preview-scale'));
  // Unrounded like the server's --tile-radius: the corner disc is concentric
  // with the card corner only at the exact device radius.
  root.style.setProperty('--tile-radius', (radius * scale).toFixed(2) + 'px');
  root.style.setProperty('--tile-radius-device', String(radius));
  document.querySelectorAll('.global-tile-radius').forEach(control => { control.value = radius; });
  document.querySelectorAll('.global-tile-radius-value').forEach(output => { output.textContent = radius; });
  tileRadiusRevision++;
  return radius;
}
function saveTileRadius(value) {
  clearTimeout(tileRadiusPreviewTimer);
  tileRadiusPreviewTimer = null;
  return queueTileRadius(value, true);
}
async function queueTileRadius(value, persist) {
  const radius = previewTileRadius(value);
  if (radius === undefined) return;
  tileRadiusWanted = { radius, persist };
  if (tileRadiusSaving) return;
  tileRadiusSaving = true;
  try {
    while (tileRadiusWanted !== null) {
      const wanted = tileRadiusWanted;
      const revision = tileRadiusRevision;
      tileRadiusWanted = null;
      try {
        const response = await fetch('/api/display/tile-radius', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({ radius: String(wanted.radius), preview: wanted.persist ? '0' : '1' }).toString()
        });
        if (!response.ok) throw new Error('save failed');
        const result = await response.json();
        if (!result.success || result.radius !== wanted.radius) throw new Error('invalid response');
        if (wanted.persist) tileRadiusConfirmed = wanted.radius;
      } catch (error) {
        if (tileRadiusWanted === null && revision === tileRadiusRevision) {
          previewTileRadius(tileRadiusConfirmed);
          showNotification(t('networkErrorSave'), false);
        }
      }
    }
  } finally { tileRadiusSaving = false; }
}
function syncTileRadiusControls(tabEl) {
  const value = getComputedStyle(document.documentElement).getPropertyValue('--tile-radius-device').trim();
  tabEl.querySelectorAll('.global-tile-radius').forEach(control => { control.value = value; });
  tabEl.querySelectorAll('.global-tile-radius-value').forEach(output => { output.textContent = value; });
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
    if (active) {
      active.classList.add('active');
      active.setAttribute('aria-current', 'page');
    }
  }


  async function switchTab(tabName) {
    // I/O became a Settings page; a remembered or linked I/O tab opens it.
    if (tabName === 'tab-hardware') {
      tabName = 'tab-network';
      showSettingsPage('io');
    }
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
    if (tabName === 'tab-network') runSettingsPageWork();
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
  // Settings tab: a list of pages like the panel's Settings (WLAN,
  // Lokalisierung, System, then I/O, camera, files and diagnostics). One page
  // shows at a time; the footer under the list and the page carries that
  // page's buttons. Each page scrolls by itself, with its scrollbar in the
  // card's free right margin, so nothing moves when a row folds out.
  let activeSettingsPage = '';

  function settingsTabActive() {
    return !!document.getElementById('tab-network')?.classList.contains('active');
  }

  // The pages always reserve their scrollbar's width and give it back with a
  // negative margin (admin.css --settings-scrollbar), so the cards end where
  // the tab bar ends. Browsers draw the scrollbar in different widths.
  function updateSettingsScrollbarWidth() {
    const tab = document.getElementById('tab-network');
    const page = document.querySelector('.settings-page:not([hidden])');
    if (!tab || !page || !page.offsetWidth) return;
    tab.style.setProperty('--settings-scrollbar', (page.offsetWidth - page.clientWidth) + 'px');
  }

  // Work a page needs only while it is on screen: the file list reads the
  // microSD card, the I/O editor its assignments.
  function runSettingsPageWork() {
    if (!settingsTabActive()) return;
    updateSettingsScrollbarWidth();
    if (activeSettingsPage === 'files' && typeof loadFileManager === 'function' && !fileManagerLoaded) {
      window.setTimeout(loadFileManager, 0);
    }
    if (activeSettingsPage === 'io') window.setTimeout(initHardwareIo, 0);
  }

  function showSettingsPage(page) {
    const pages = Array.from(document.querySelectorAll('.settings-page'));
    if (!pages.length) return;
    if (!pages.some(section => section.dataset.settingsPage === page)) {
      page = pages[0].dataset.settingsPage;
    }
    activeSettingsPage = page;
    document.querySelectorAll('.settings-nav-item').forEach(item => {
      const on = item.dataset.settingsPage === page;
      item.classList.toggle('active', on);
      if (on) item.setAttribute('aria-current', 'page');
      else item.removeAttribute('aria-current');
    });
    pages.forEach(section => { section.hidden = section.dataset.settingsPage !== page; });
    let footer = false;
    document.querySelectorAll('.settings-foot-set').forEach(set => {
      set.hidden = set.dataset.settingsFoot !== page;
      footer = footer || !set.hidden;
    });
    const foot = document.getElementById('settingsFoot');
    if (foot) foot.hidden = !footer;
    try { localStorage.setItem('activeSettingsPage', page); } catch (e) {}
    runSettingsPageWork();
  }

  // Opens a Settings page, optionally with one of its rows folded out (the
  // password badge and the entity picker's password hint).
  function openSettingsPage(page, foldId) {
    switchTab('tab-network');
    showSettingsPage(page);
    const fold = foldId ? document.getElementById(foldId) : null;
    if (!fold) return;
    fold.open = true;
    // switchTab shows Settings right away (no tile data to wait for).
    fold.scrollIntoView({block: 'nearest', behavior: 'smooth'});
  }

  function initSettingsPages() {
    document.querySelectorAll('.settings-nav-item').forEach(item => {
      item.addEventListener('click', () => showSettingsPage(item.dataset.settingsPage));
    });
    let page = '';
    try { page = localStorage.getItem('activeSettingsPage') || ''; } catch (e) {}
    showSettingsPage(page);
    window.addEventListener('resize', perFrame(updateSettingsScrollbarWidth));
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
  // Optional Web Admin password section (Settings tab). The password never
  // leaves the browser: auth.js derives a PBKDF2-HMAC-SHA256 key and the
  // panel stores only salt, iteration count and key.
  function initWebAdminPasswordSettings() {
    const section = document.getElementById('web_auth_section');
    const auth = window.HomeTilesAuth;
    if (!section || !auth) return;
    const input = document.getElementById('web_auth_password');
    const repeat = document.getElementById('web_auth_password_repeat');
    const setButton = document.getElementById('web_auth_set');
    const removeButton = document.getElementById('web_auth_remove');
    const logoutButton = document.getElementById('web_auth_logout');
    const busy = value => {
      [setButton, removeButton, logoutButton].forEach(button => {
        if (button) button.disabled = value;
      });
    };

    setButton?.addEventListener('click', async () => {
      const password = String(input?.value || '');
      if (password.length < 8) {
        showNotification(t('webAuthTooShort'), false);
        input?.focus();
        return;
      }
      if (password !== String(repeat?.value || '')) {
        showNotification(t('webAuthMismatch'), false);
        repeat?.focus();
        return;
      }
      busy(true);
      try {
        if (!await auth.setPassword(password)) throw new Error('set');
        // Setting a password ends every session, including this one. Sign in
        // again right away so the page stays usable.
        const login = await auth.login(password);
        if (input) input.value = '';
        if (repeat) repeat.value = '';
        showNotification(t('webAuthSaved'), true);
        window.setTimeout(() => window.location.reload(), login.ok ? 400 : 1200);
      } catch (error) {
        // The first password needs a tap on the display first.
        showNotification(t(auth.passwordError?.() === 'panel_tap_required' ? 'webAuthPanelTap' : 'webAuthChangeFailed'), false);
      } finally {
        busy(false);
      }
    });

    removeButton?.addEventListener('click', async () => {
      if (!window.confirm(t('webAuthRemoveConfirm'))) return;
      busy(true);
      try {
        if (!await auth.removePassword()) throw new Error('remove');
        showNotification(t('webAuthRemoved'), true);
        window.setTimeout(() => window.location.reload(), 400);
      } catch (error) {
        showNotification(t('webAuthChangeFailed'), false);
      } finally {
        busy(false);
      }
    });

    logoutButton?.addEventListener('click', async () => {
      busy(true);
      await auth.logout();
      window.location.replace('/');
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
  // Built-in camera opt-in (only rendered on the exact camera profile). The
  // server provides every visible text as data attributes on the status line;
  // this code only selects between them. A ready sensor adds its model name
  // and chip ID; internal detail codes stay diagnostic-only in the JSON.
  // The live-stream mode and rotation selects are server-rendered too and
  // save on change.
  let localCameraSaveSequence = 0;
  let localCameraModeSequence = 0;
  let localCameraMirrorSequence = 0;
  let localCameraRotationSequence = 0;
  let localCameraRbSwapSequence = 0;
  let localCameraIndicatorSequence = 0;
  let localCameraPollTimer = null;
  const LOCAL_CAMERA_STATE_KEYS = {
    disabled: 'stateDisabled',
    probing: 'stateProbing',
    ready: 'stateReady',
    not_found: 'stateNotFound',
    error: 'stateError'
  };

  function localCameraStatusText(note, status) {
    const state = status && typeof status.state === 'string' ? status.state : 'error';
    const key = LOCAL_CAMERA_STATE_KEYS[state] || 'stateError';
    let text = (note.dataset.label || '') + ': ' + (note.dataset[key] || state);
    if (state === 'ready') {
      const parts = [];
      if (status.sensor) parts.push(String(status.sensor).toUpperCase());
      if (status.chip_id) parts.push(String(status.chip_id));
      if (parts.length) text += ' (' + parts.join(', ') + ')';
    }
    return text;
  }

  function applyLocalCameraStatus(status) {
    const note = document.getElementById('local_camera_status');
    const toggle = document.getElementById('local_camera_enabled');
    if (!note || !status || typeof status !== 'object') return;
    if (toggle && typeof status.enabled === 'boolean') toggle.checked = status.enabled;
    const mirrorToggle = document.getElementById('local_camera_mirror');
    if (mirrorToggle && typeof status.mirror === 'boolean') mirrorToggle.checked = status.mirror;
    const rotationSelect = document.getElementById('local_camera_rotation');
    if (rotationSelect && Number.isInteger(status.rotation)) {
      rotationSelect.value = String(status.rotation);
      rotationSelect.dataset.saved = String(status.rotation);
    }
    const rbSwapToggle = document.getElementById('local_camera_rb_swap');
    if (rbSwapToggle && typeof status.rb_swap === 'boolean') rbSwapToggle.checked = status.rb_swap;
    if (Number.isInteger(status.indicator)) applyLocalCameraIndicator(status.indicator);
    const modeSelect = document.getElementById('local_camera_stream_mode');
    if (modeSelect && Number.isInteger(status.stream_mode)) {
      modeSelect.value = String(status.stream_mode);
      modeSelect.dataset.saved = String(status.stream_mode);
    }
    if (Number.isInteger(status.stream_mode)) showLocalCameraCustom(status.stream_mode);
    applyLocalCameraCustom(status.custom);
    applyLocalCameraImage(status.image);
    note.dataset.state = String(status.state || '');
    note.textContent = localCameraStatusText(note, status);
    clearTimeout(localCameraPollTimer);
    localCameraPollTimer = null;
    // The sensor probe runs on the camera worker; follow it briefly.
    if (status.state === 'probing') {
      localCameraPollTimer = setTimeout(refreshLocalCameraStatus, 1000);
    }
  }

  async function refreshLocalCameraStatus() {
    if (!document.getElementById('local_camera_status')) return;
    try {
      const response = await fetch('/api/local-camera', {cache: 'no-store'});
      if (!response.ok) return;
      applyLocalCameraStatus(await response.json());
    } catch (error) {
      // A status refresh is optional; the next toggle or reload retries.
    }
  }

  async function saveLocalCameraEnabled(enabled) {
    const wanted = !!enabled;
    const sequence = ++localCameraSaveSequence;
    const toggle = document.getElementById('local_camera_enabled');
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'enabled=' + (wanted ? '1' : '0')
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraSaveSequence) return;
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraSaveSequence) return;
      if (toggle) toggle.checked = !wanted;
      showNotification(t('networkErrorSave'), false);
    }
  }

  async function saveLocalCameraMirror(enabled) {
    const wanted = !!enabled;
    const sequence = ++localCameraMirrorSequence;
    const toggle = document.getElementById('local_camera_mirror');
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'mirror=' + (wanted ? '1' : '0')
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraMirrorSequence) return;
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraMirrorSequence) return;
      if (toggle) toggle.checked = !wanted;
      showNotification(t('networkErrorSave'), false);
    }
  }

  // Rotation: clockwise quarter turns 0..3 (the select shows the degrees).
  // A failed save restores the last saved value.
  async function saveLocalCameraRotation(value) {
    const rotation = String(value);
    const sequence = ++localCameraRotationSequence;
    const select = document.getElementById('local_camera_rotation');
    const previous = select && select.dataset.saved !== undefined ? select.dataset.saved : null;
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'rotation=' + encodeURIComponent(rotation)
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraRotationSequence) return;
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraRotationSequence) return;
      if (select && previous !== null) select.value = previous;
      showNotification(t('networkErrorSave'), false);
    }
  }

  async function saveLocalCameraRbSwap(enabled) {
    const wanted = !!enabled;
    const sequence = ++localCameraRbSwapSequence;
    const toggle = document.getElementById('local_camera_rb_swap');
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'rb_swap=' + (wanted ? '1' : '0')
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraRbSwapSequence) return;
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraRbSwapSequence) return;
      if (toggle) toggle.checked = !wanted;
      showNotification(t('networkErrorSave'), false);
    }
  }

  // Indicator style (experimental): 0 none, 1 line only, 2 line with the pill.
  // The pill checkbox only applies while the line is shown and keeps its own
  // state while the line is off.
  function applyLocalCameraIndicator(style) {
    const line = document.getElementById('local_camera_indicator_line');
    const pill = document.getElementById('local_camera_indicator_pill');
    if (line) line.checked = style !== 0;
    if (pill) {
      if (style !== 0) pill.checked = style === 2;
      pill.disabled = style === 0;
    }
  }

  function localCameraIndicatorStyle() {
    const line = document.getElementById('local_camera_indicator_line');
    const pill = document.getElementById('local_camera_indicator_pill');
    if (!line || !line.checked) return 0;
    return pill && pill.checked ? 2 : 1;
  }

  async function saveLocalCameraIndicator() {
    const style = localCameraIndicatorStyle();
    const sequence = ++localCameraIndicatorSequence;
    const line = document.getElementById('local_camera_indicator_line');
    const pill = document.getElementById('local_camera_indicator_pill');
    if (pill) pill.disabled = style === 0;
    const saved = line && line.dataset.saved !== undefined ? parseInt(line.dataset.saved, 10) : null;
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'indicator=' + style
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraIndicatorSequence) return;
      if (line && Number.isInteger(status.indicator)) line.dataset.saved = String(status.indicator);
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraIndicatorSequence) return;
      if (Number.isInteger(saved)) applyLocalCameraIndicator(saved);
      showNotification(t('networkErrorSave'), false);
    }
  }

  // Custom stream mode: frames per second and JPEG quality sliders, shown only
  // while the Custom mode is selected (its id comes from data-mode). A slider
  // saves on release; a failed save restores the last saved value. Numbers
  // are untranslated.
  const LOCAL_CAMERA_CUSTOM_KEYS = ['fps', 'quality'];
  let localCameraCustomSequence = 0;

  function showLocalCameraCustom(mode) {
    const block = document.getElementById('local_camera_custom');
    if (!block) return;
    block.hidden = String(mode) !== String(block.dataset.mode);
  }

  function setLocalCameraCustomSlider(key, value) {
    const slider = document.getElementById('local_camera_custom_' + key);
    if (!slider) return;
    slider.value = String(value);
    const output = document.getElementById('local_camera_custom_' + key + '_value');
    if (output) output.textContent = String(value);
  }

  function applyLocalCameraCustom(custom) {
    if (!custom || typeof custom !== 'object') return;
    for (const key of LOCAL_CAMERA_CUSTOM_KEYS) {
      const slider = document.getElementById('local_camera_custom_' + key);
      if (!slider || !Number.isInteger(custom[key])) continue;
      slider.dataset.saved = String(custom[key]);
      setLocalCameraCustomSlider(key, custom[key]);
    }
  }

  function localCameraCustomInput(slider) {
    const key = slider && slider.dataset ? slider.dataset.customKey : '';
    if (!LOCAL_CAMERA_CUSTOM_KEYS.includes(key)) return;
    const output = document.getElementById('local_camera_custom_' + key + '_value');
    if (output) output.textContent = String(slider.value);
  }

  async function localCameraCustomChange(slider) {
    const key = slider && slider.dataset ? slider.dataset.customKey : '';
    if (!LOCAL_CAMERA_CUSTOM_KEYS.includes(key)) return;
    const value = parseInt(slider.value, 10);
    if (!Number.isInteger(value)) return;
    localCameraCustomInput(slider);
    const sequence = ++localCameraCustomSequence;
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'custom_' + key + '=' + value
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraCustomSequence) return;
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraCustomSequence) return;
      if (slider.dataset.saved !== undefined) setLocalCameraCustomSlider(key, slider.dataset.saved);
      showNotification(t('networkErrorSave'), false);
    }
  }

  async function saveLocalCameraStreamMode(value) {
    const mode = String(value);
    const sequence = ++localCameraModeSequence;
    const select = document.getElementById('local_camera_stream_mode');
    const previous = select && select.dataset.saved !== undefined ? select.dataset.saved : null;
    showLocalCameraCustom(mode);
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'mode=' + encodeURIComponent(mode)
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraModeSequence) return;
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraModeSequence) return;
      if (select && previous !== null) select.value = previous;
      if (previous !== null) showLocalCameraCustom(previous);
      showNotification(t('networkErrorSave'), false);
    }
  }

  // Image controls (brightness, contrast, saturation, red, blue, Max. gain). Sliders save
  // while dragging (debounced) and immediately on release. One POST is in
  // flight at a time so the device applies values in order; a value that is
  // still pending is never overwritten by a status update, and a failed save
  // restores the last saved value. Numbers are untranslated.
  const LOCAL_CAMERA_IMAGE_KEYS = ['brightness', 'contrast', 'saturation', 'red', 'blue', 'gain'];
  const LOCAL_CAMERA_IMAGE_DEBOUNCE_MS = 300;
  let localCameraImagePending = {};
  let localCameraImageTimer = null;
  let localCameraImageInFlight = null;

  function localCameraImageSlider(key) {
    return document.getElementById('local_camera_' + key);
  }

  function showLocalCameraImageValue(slider, value) {
    const output = document.getElementById('local_camera_' + slider.dataset.imageKey + '_value');
    if (output) output.textContent = String(value) + (slider.dataset.unit || '');
  }

  function setLocalCameraImageSlider(key, value) {
    const slider = localCameraImageSlider(key);
    if (!slider) return;
    slider.value = String(value);
    showLocalCameraImageValue(slider, value);
  }

  function applyLocalCameraImage(image) {
    if (!image || typeof image !== 'object') return;
    for (const key of LOCAL_CAMERA_IMAGE_KEYS) {
      const slider = localCameraImageSlider(key);
      if (!slider || !Number.isInteger(image[key])) continue;
      slider.dataset.saved = String(image[key]);
      // Keep what the user is dragging or has not sent yet.
      const busy = key in localCameraImagePending ||
        (localCameraImageInFlight && key in localCameraImageInFlight &&
         String(localCameraImageInFlight[key]) !== String(image[key]));
      if (!busy) setLocalCameraImageSlider(key, image[key]);
    }
  }

  function queueLocalCameraImage(slider) {
    const key = slider && slider.dataset ? slider.dataset.imageKey : '';
    if (!LOCAL_CAMERA_IMAGE_KEYS.includes(key)) return false;
    const value = parseInt(slider.value, 10);
    if (!Number.isInteger(value)) return false;
    showLocalCameraImageValue(slider, value);
    localCameraImagePending[key] = value;
    return true;
  }

  function localCameraImageInput(slider) {
    if (!queueLocalCameraImage(slider)) return;
    clearTimeout(localCameraImageTimer);
    localCameraImageTimer = setTimeout(flushLocalCameraImage, LOCAL_CAMERA_IMAGE_DEBOUNCE_MS);
  }

  function localCameraImageChange(slider) {
    if (!queueLocalCameraImage(slider)) return;
    return flushLocalCameraImage();
  }

  function resetLocalCameraImage() {
    localCameraImagePending = {reset: 1};
    return flushLocalCameraImage();
  }

  async function flushLocalCameraImage() {
    clearTimeout(localCameraImageTimer);
    localCameraImageTimer = null;
    // The running save flushes the rest when it finishes.
    if (localCameraImageInFlight) return;
    const values = localCameraImagePending;
    const keys = Object.keys(values);
    if (!keys.length) return;
    localCameraImagePending = {};
    localCameraImageInFlight = values;
    const body = keys.map(key => key + '=' + encodeURIComponent(String(values[key]))).join('&');
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      localCameraImageInFlight = null;
      applyLocalCameraStatus(status);
    } catch (error) {
      localCameraImageInFlight = null;
      const sent = 'reset' in values ? LOCAL_CAMERA_IMAGE_KEYS : keys;
      for (const key of sent) {
        const slider = localCameraImageSlider(key);
        if (!slider || key in localCameraImagePending) continue;
        if (slider.dataset.saved !== undefined) setLocalCameraImageSlider(key, slider.dataset.saved);
      }
      showNotification(t('networkErrorSave'), false);
    }
    if (Object.keys(localCameraImagePending).length) await flushLocalCameraImage();
  }

  document.addEventListener('DOMContentLoaded', () => {
    const modeSelect = document.getElementById('local_camera_stream_mode');
    if (modeSelect) modeSelect.dataset.saved = modeSelect.value;
    const rotationSelect = document.getElementById('local_camera_rotation');
    if (rotationSelect) rotationSelect.dataset.saved = rotationSelect.value;
    const indicatorLine = document.getElementById('local_camera_indicator_line');
    if (indicatorLine) indicatorLine.dataset.saved = String(localCameraIndicatorStyle());
    const note = document.getElementById('local_camera_status');
    if (note && note.dataset.state === 'probing') refreshLocalCameraStatus();
  });

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
  let sensorMetaCache = { values: {}, units: {}, icons: {}, names: {}, sceneEntities: {}, loaded: false };
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
      return { values: {}, units: {}, icons: {}, names: {}, sceneEntities: {}, loaded: false };
    }
    const hasMeta = Object.prototype.hasOwnProperty.call(payload, 'editable_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'device_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'units') ||
                    Object.prototype.hasOwnProperty.call(payload, 'icons') ||
                    Object.prototype.hasOwnProperty.call(payload, 'names') ||
                    Object.prototype.hasOwnProperty.call(payload, 'binary_sensor_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'energy_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'energy_units') ||
                    Object.prototype.hasOwnProperty.call(payload, 'climate_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'weather_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'media_values');
    if (!hasMeta) {
      return { values: payload || {}, units: {}, icons: {}, names: {}, sceneEntities: {}, loaded: true };
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
      // Lock, Alarm panel and Fan detail states (types/device).
      deviceValues: payload.device_values || payload.deviceValues || {},
      // Weather and media tile states, the payloads their tiles draw.
      weatherValues: payload.weather_values || payload.weatherValues || {},
      mediaValues: payload.media_values || payload.mediaValues || {},
      // "From cover": the color each shown media card sampled from its cover.
      mediaCoverColors: payload.media_cover_colors || payload.mediaCoverColors || {},
      units: Object.assign({}, payload.units || {}, payload.energy_units || {}),
      icons: payload.icons || {},
      names: payload.names || {},
      // Scene alias -> entity, so scene tiles resolve the entity icon like the
      // device. A grid refresh normalizes the already normalized cache again,
      // so the normalized name must survive too (it lost every scene icon).
      sceneEntities: payload.scene_entities || payload.sceneEntities || {},
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
  // Preview text sits on its LVGL baseline. LVGL draws the baseline base_line
  // above the bottom of a label's line box; the browser puts it where the
  // font's rounded ascent and descent and the half-leading put it, which
  // depends on the size and the display scale (Chrome snaps it to whole
  // device pixels, so one shift from the server was up to a pixel off; user
  // 2026-10-02). The page measures the browser baseline per size and zoom and
  // moves each text by the difference to the LVGL baseline (--lb*, --*-base).
  const previewBaselineCache = new Map();
  let previewBaselineProbe = null;

  function previewCssBaseline(fontPx, linePx) {
    const key = fontPx.toFixed(3) + '/' + linePx.toFixed(3) + '@' + (window.devicePixelRatio || 1);
    if (previewBaselineCache.has(key)) return previewBaselineCache.get(key);
    if (!previewBaselineProbe) {
      previewBaselineProbe = document.createElement('div');
      previewBaselineProbe.setAttribute('aria-hidden', 'true');
      previewBaselineProbe.style.cssText =
        'position:absolute;left:-9999px;top:0;visibility:hidden;white-space:nowrap;font-weight:400;';
      const mark = document.createElement('span');
      mark.style.cssText = 'display:inline-block;width:0;height:0;vertical-align:baseline;';
      previewBaselineProbe.append('0', mark);
    }
    if (!previewBaselineProbe.isConnected) document.body.appendChild(previewBaselineProbe);
    previewBaselineProbe.style.fontSize = fontPx + 'px';
    previewBaselineProbe.style.lineHeight = linePx + 'px';
    const value = previewBaselineProbe.lastChild.getBoundingClientRect().top -
      previewBaselineProbe.getBoundingClientRect().top;
    previewBaselineCache.set(key, value);
    return value;
  }

  // The shift that moves the browser baseline of a line box onto the LVGL
  // baseline lvglBasePx below its top.
  function previewBaselineShift(fontPx, linePx, lvglBasePx) {
    return lvglBasePx - previewCssBaseline(fontPx, linePx);
  }

  // Sets every emitted shift (--ldyNN for Clock and Text lines, the
  // half-height title and value) from the measured browser baselines.
  function calibratePreviewBaselines() {
    const root = document.documentElement;
    const style = getComputedStyle(root);
    const px = name => parseFloat(style.getPropertyValue(name));
    const set = (shiftName, fontPx, linePx, basePx) => {
      if (!(fontPx > 0) || !(linePx > 0) || !Number.isFinite(basePx)) return;
      root.style.setProperty(shiftName, previewBaselineShift(fontPx, linePx, basePx).toFixed(3) + 'px');
    };
    for (const size of [16, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96]) {
      set('--ldy' + size, px('--fs' + size), px('--lh' + size), px('--lb' + size));
    }
    for (const [shift, font, line, base] of [
      ['--compact-title-dy', '--compact-title-font', '--compact-title-line', '--compact-title-base'],
      ['--compact-value-dy', '--compact-value-font', '--compact-value-line', '--compact-value-base'],
      ['--compact-value-dy-24', '--compact-value-font-24', '--compact-value-line-step-24', '--compact-value-base-24'],
      ['--compact-value-dy-28', '--compact-value-font-28', '--compact-value-line-step-28', '--compact-value-base-28'],
    ]) set(shift, px(font), px(line), px(base));
  }

  function bindPreviewBaselines() {
    const calibrate = () => {
      calibratePreviewBaselines();
      // The screensaver clock places its lines in script.
      if (typeof screensaverDraft !== 'undefined' && screensaverDraft &&
          typeof renderScreensaverEditor === 'function') {
        renderScreensaverEditor();
      }
    };
    calibrate();
    if (document.fonts?.ready) document.fonts.ready.then(calibrate);
    // Browser zoom changes the device pixel ratio and with it the rounding.
    window.addEventListener('resize', perFrame(calibrate));
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

  // One firmware request serves every editor. The short cache avoids repeated
  // large JSON responses while switching quickly between tiles.
  function refreshEntityOptionLists(tab) {
    return fetchEntityOptions()
      .then(() => refreshEntityPickers(tab))
      .catch(() => {});
  }
  // Shared entity picker of every tile field that names a Home Assistant
  // entity, like Home Assistant's own picker: icon, name, area and device, and
  // type, with a search. The server renders `<input type="hidden"
  // data-entity-picker="<list>">` (appendEntityPickerField); the value stays
  // in that input, so loading, saving, drafts, copy and import keep using the
  // field id as before. The choices are the Bridge's released entities from
  // /api/entity_options (fetchEntityOptions); a panel paired with a Bridge
  // that searches asks it as well (/api/entity_search, entity_search.h): with
  // a Web Admin password every Home Assistant entity of the list, otherwise
  // the released ones with their area and device.

  // Home Assistant domains in the order of ENTITY_KIND_LABELS
  // (LocaleProfile::entity_kind_labels).
  const ENTITY_KIND_DOMAINS = [
    ['sensor'], ['binary_sensor'], ['light'], ['switch', 'input_boolean'], ['fan'], ['cover'],
    ['climate'], ['media_player'], ['weather'], ['camera'], ['lock'], ['alarm_control_panel'],
    ['number', 'input_number'], ['select', 'input_select'],
    ['datetime', 'date', 'time', 'input_datetime'], ['scene'], ['script'], ['button', 'input_button'],
    ['automation'], ['humidifier'], ['siren'], ['remote']
  ];
  // Lists the Bridge searches (entity_search.py LIST_DOMAINS).
  const ENTITY_REMOTE_LISTS = new Set(['sensors', 'binary_sensors', 'numbers', 'selects', 'datetimes',
    'weathers', 'switches', 'media', 'climates', 'covers', 'cameras', 'locks', 'alarm_panels', 'fans']);
  const ENTITY_REMOTE_DELAY_MS = 250;
  const ENTITY_REMOTE_POLL_MS = 120;
  const ENTITY_REMOTE_TIMEOUT_MS = 5000;
  const ENTITY_PICKER_FALLBACK_ICON = 'shape-outline';
  const entityPickerNativeValue = typeof HTMLInputElement === 'function'
    ? Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value') : null;
  // What the Bridge told about entities (name, icon, area ▸ device), by id.
  const entityRemoteInfo = new Map();
  const entityRemoteAsked = new Set();
  // The Bridge's first page of each list, so a list opens with it at once.
  const entityRemoteFirstPages = new Map();
  const entityRemoteFetching = new Set();
  let entityRemoteQueue = Promise.resolve();
  let entityRemoteSupported = true;  // false once the panel has no Bridge search
  let entityRemoteGeneration = 0;
  let entityRemoteTimer = 0;
  let entityPickerPopover = null;
  // {input, local, entries, query, active, state, remote, full, more}
  let entityPickerOpen = null;

  function entityKindLabel(entityId) {
    const domain = String(entityId || '').split('.')[0];
    const index = ENTITY_KIND_DOMAINS.findIndex(domains => domains.includes(domain));
    return index >= 0 && typeof ENTITY_KIND_LABELS !== 'undefined' ? ENTITY_KIND_LABELS[index] || '' : '';
  }

  function entityPickerContext(area, device) {
    return area && device ? area + ' ▸ ' + device : String(area || device || '');
  }

  // The entries of one list key, as {value, name, icon, entity}.
  function entityPickerEntries(data, list) {
    const raw = list === 'icon_sources' && typeof iconColorSourceEntries === 'function'
      ? iconColorSourceEntries(data) : (data && data[list]);
    const seen = new Set();
    const entries = [];
    for (const entry of Array.isArray(raw) ? raw : []) {
      const value = String(entry?.v ?? '');
      if (!value || seen.has(value)) continue;
      seen.add(value);
      entries.push({
        value,
        name: String(entry.t || '') || titleFromEntity(value) || value,
        icon: normalizeMdiIconName(entry.i || ''),
        entity: String(entry.e || value)
      });
    }
    return entries.sort((a, b) =>
      a.name.localeCompare(b.name, APP_LOCALE, { sensitivity: 'base' }) || a.value.localeCompare(b.value));
  }

  // Like Home Assistant, the list leaves out the device name in front of an
  // entity's name; the device stands in the line below. The tile title keeps
  // the whole name.
  function entityPickerShortName(name, device) {
    const prefix = String(device || '').trim() + ' ';
    if (prefix.length < 2 || name.length <= prefix.length ||
        name.slice(0, prefix.length).toLowerCase() !== prefix.toLowerCase()) {
      return name;
    }
    return name.slice(prefix.length).trim() || name;
  }

  function entityPickerFromRemote(item) {
    const value = String(item?.v || '');
    const name = String(item.t || '') || titleFromEntity(value) || value;
    const entry = {
      value,
      name,
      label: entityPickerShortName(name, item.d),
      icon: normalizeMdiIconName(item.i || ''),
      entity: value,
      context: entityPickerContext(item.a, item.d),
      remote: true
    };
    if (value) entityRemoteInfo.set(value, entry);
    return entry;
  }

  // What the field shows for its value, also when the value is no longer in
  // the released list (it is kept, never cleared).
  function entityPickerEntryFor(input, value) {
    if (!value) return null;
    const list = input?.dataset?.entityPicker || '';
    if (ENTITY_REMOTE_LISTS.has(list) && entityRemoteInfo.has(value)) return entityRemoteInfo.get(value);
    const known = entityOptionsCache ? entityPickerEntries(entityOptionsCache, list).find(entry => entry.value === value) : null;
    if (known) return known;
    const entity = list === 'scenes' ? (sensorMetaCache.sceneEntities?.[value] || value) : value;
    return {
      value,
      name: sensorMetaCache.names?.[entity] || titleFromEntity(value) || value,
      icon: normalizeMdiIconName(sensorMetaCache.icons?.[entity] || ''),
      entity
    };
  }

  // The chosen entity's name for an automatic tile title ('' without one).
  function entityPickerName(input) {
    const entry = entityPickerEntryFor(input, input?.value || '');
    return entry ? entry.name : '';
  }

  function entityPickerRowHtml(entry, query) {
    const mark = text => {
      const plain = String(text || '');
      const words = String(query || '').toLowerCase().split(/\s+/).filter(Boolean);
      const lower = plain.toLowerCase();
      const word = words.find(candidate => lower.includes(candidate));
      if (!word) return escapeHtml(plain);
      const at = lower.indexOf(word);
      return escapeHtml(plain.slice(0, at)) + '<mark>' + escapeHtml(plain.slice(at, at + word.length)) + '</mark>' +
        escapeHtml(plain.slice(at + word.length));
    };
    return '<span class="entity-picker-icon"><i class="mdi mdi-' +
      escapeHtml(entry.icon || ENTITY_PICKER_FALLBACK_ICON) + '"></i></span>' +
      '<span class="entity-picker-text"><span class="entity-picker-name">' + mark(entry.label || entry.name) + '</span>' +
      '<span class="entity-picker-context">' + mark(entry.context || entry.entity) + '</span></span>';
  }

  function entityPickerControl(input) {
    const next = input?.nextElementSibling;
    return next && next.classList.contains('entity-picker') ? next : null;
  }

  // Draws the closed field of one input from its current value.
  function renderEntityPicker(input) {
    const control = entityPickerControl(input);
    if (!control) return;
    const field = control.querySelector('.entity-picker-field');
    const clear = control.querySelector('.entity-picker-clear');
    const entry = entityPickerEntryFor(input, input.value);
    if (field) {
      field.innerHTML = (entry ? entityPickerRowHtml(entry, '')
        : '<span class="entity-picker-placeholder">' + escapeHtml(t('entityPickerChoose')) + '</span>') +
        '<i class="mdi mdi-chevron-down entity-picker-chevron"></i>';
    }
    if (clear) clear.hidden = !entry;
    if (entry && !entry.remote && input.closest?.('.tile-entity-slot')) entityPickerLookup(input);
  }

  // Adds the visible field after a hidden entity input (once per element) and
  // keeps it in step with every later `input.value = ...`.
  function attachEntityPicker(input) {
    if (!input || !input.id) return;
    let control = entityPickerControl(input);
    if (!control) {
      control = document.createElement('div');
      control.className = 'entity-picker';
      input.insertAdjacentElement('afterend', control);
    }
    // Clicks are delegated, so markup restored from the session cache or
    // moved under Type keeps working and is only redrawn.
    if (!control.querySelector('.entity-picker-field')) {
      control.innerHTML = '<button type="button" class="entity-picker-field" id="' + escapeHtml(input.id) +
        '_picker" aria-haspopup="listbox" aria-expanded="false"></button>' +
        '<button type="button" class="entity-picker-clear" aria-label="' + escapeHtml(t('entityPickerClear')) +
        '" title="' + escapeHtml(t('entityPickerClear')) + '" hidden><i class="mdi mdi-close"></i></button>';
    }
    if (entityPickerNativeValue && !Object.prototype.hasOwnProperty.call(input, 'value')) {
      Object.defineProperty(input, 'value', {
        configurable: true,
        get() { return entityPickerNativeValue.get.call(this); },
        set(value) {
          entityPickerNativeValue.set.call(this, value);
          renderEntityPicker(this);
        }
      });
    }
    renderEntityPicker(input);
  }

  function setupEntityPickers(root) {
    (root || document).querySelectorAll?.('input[data-entity-picker]').forEach(attachEntityPicker);
  }

  // Redraws the fields of one tile tab (or all) once fresh names arrived.
  function refreshEntityPickers(tab) {
    const selector = tab ? 'input[data-entity-picker][id^="' + tab + '_"]' : 'input[data-entity-picker]';
    document.querySelectorAll(selector).forEach(renderEntityPicker);
    if (typeof refreshTileIconButtons === 'function') refreshTileIconButtons(tab);
    if (entityPickerOpen && entityPickerOpen.state !== 'ready') loadEntityPickerEntries();
  }

  // ---- Bridge search ----

  // One search through the panel, from match `offset` on; null without a
  // Bridge that searches or when a newer search replaced it (the panel keeps
  // the newest only).
  async function entityRemoteSearch(list, query, generation, offset) {
    const params = new URLSearchParams({ q: query, list });
    if (offset) params.set('o', String(offset));
    const begin = await fetch('/api/entity_search', { method: 'POST', body: params });
    if (!begin.ok) throw new Error('Entity search HTTP ' + begin.status);
    const started = await begin.json();
    if (!started.bridge) {
      entityRemoteSupported = false;
      return null;
    }
    const deadline = Date.now() + ENTITY_REMOTE_TIMEOUT_MS;
    while (Date.now() < deadline) {
      await new Promise(resolve => setTimeout(resolve, ENTITY_REMOTE_POLL_MS));
      if (generation !== entityRemoteGeneration) return null;
      const poll = await fetch('/api/entity_search?id=' + encodeURIComponent(started.id));
      if (!poll.ok) throw new Error('Entity search HTTP ' + poll.status);
      const answer = await poll.json();
      if (answer.ready) return answer;
    }
    throw new Error('Entity search timeout');
  }

  // Merges one page of the Bridge's answer into the open list.
  function entityPickerApplyRemote(open, answer, append) {
    const entries = append ? open.entries : [];
    const seen = new Set(entries.map(entry => entry.value));
    const page = (Array.isArray(answer.r) ? answer.r : []).map(entityPickerFromRemote);
    const fresh = page.filter(entry => entry.value && !seen.has(entry.value) && seen.add(entry.value));
    open.offset = (append ? open.offset : 0) + page.length;
    open.full = !!answer.full;
    // A Bridge without pages repeats its first one, which ends the list.
    open.more = !!answer.more && fresh.length > 0;
    // Panel entities the Bridge does not search (display, local relays)
    // follow the last page.
    const own = open.more ? [] :
      open.local.filter(entry => !seen.has(entry.value) && entityPickerMatchesQuery(entry, open.query));
    open.entries = entries.concat(fresh, own);
    open.remote = 'ready';
  }

  function entityPickerRemoteSearch(open) {
    const list = open.input.dataset.entityPicker;
    if (!entityRemoteSupported || !ENTITY_REMOTE_LISTS.has(list)) return;
    const generation = ++entityRemoteGeneration;
    // A list opened from the remembered first page keeps its place when the
    // fresh one arrives.
    const shown = open.remote === 'ready';
    open.remote = 'loading';
    open.paging = false;
    entityRemoteSearch(list, open.query, generation, 0)
      .then(answer => {
        if (entityPickerOpen !== open || generation !== entityRemoteGeneration) return;
        if (!answer) {
          open.remote = null;
        } else {
          if (!open.query) entityRemoteFirstPages.set(list, answer);
          entityPickerApplyRemote(open, answer, false);
          if (!open.query && !shown) {
            open.active = Math.max(0, open.entries.findIndex(entry => entry.value === open.input.value));
          }
          renderEntityPicker(open.input);
        }
        renderEntityPickerList(shown);
      })
      .catch(() => {
        if (entityPickerOpen !== open || generation !== entityRemoteGeneration) return;
        // A remembered page stays; without one the panel's own list shows.
        open.remote = open.entries.length ? 'ready' : null;
        renderEntityPickerList(shown);
      });
  }

  // The next page of the Bridge's matches once the list nears its end.
  function entityPickerRemoteMore(open) {
    if (!open || open.remote !== 'ready' || !open.more || open.paging) return;
    open.paging = true;
    const generation = entityRemoteGeneration;
    entityRemoteSearch(open.input.dataset.entityPicker, open.query, generation, open.offset)
      .then(answer => {
        if (entityPickerOpen !== open || generation !== entityRemoteGeneration) return;
        open.paging = false;
        if (answer) entityPickerApplyRemote(open, answer, true);
        else open.more = false;
        renderEntityPickerList(true);
      })
      .catch(() => {
        if (entityPickerOpen !== open || generation !== entityRemoteGeneration) return;
        open.paging = false;
        open.more = false;
        renderEntityPickerList(true);
      });
  }

  function entityPickerNearEnd(list) {
    if (list.scrollHeight - list.scrollTop - list.clientHeight < 120) entityPickerRemoteMore(entityPickerOpen);
  }

  // Requests ahead of an opening run one after another, since the panel
  // keeps only its newest search; an open list's own search cancels them.
  function entityRemoteInBackground(job) {
    entityRemoteQueue = entityRemoteQueue.then(job).catch(() => {});
  }

  // Closed fields of these Bridge entities show their area and device.
  function entityPickerShowRemote(items) {
    const values = new Set(items.map(item => entityPickerFromRemote(item).value));
    document.querySelectorAll('input[data-entity-picker]').forEach(input => {
      if (values.has(input.value)) renderEntityPicker(input);
    });
    if (typeof refreshTileIconButtons === 'function') refreshTileIconButtons();
  }

  // The shown tile's entity field: the first page of its list arrives ahead
  // of the opening, then area and device of its own entity.
  function entityPickerWarm(input) {
    const list = input?.dataset?.entityPicker || '';
    if (!entityRemoteSupported || !ENTITY_REMOTE_LISTS.has(list)) return;
    if (!entityRemoteFirstPages.has(list) && !entityRemoteFetching.has(list)) {
      entityRemoteFetching.add(list);
      entityRemoteInBackground(async () => {
        try {
          if (entityPickerOpen) return;
          const answer = await entityRemoteSearch(list, '', ++entityRemoteGeneration, 0);
          if (!answer) return;
          entityRemoteFirstPages.set(list, answer);
          entityPickerShowRemote(Array.isArray(answer.r) ? answer.r : []);
        } finally {
          entityRemoteFetching.delete(list);
        }
      });
    }
    entityPickerLookup(input);
  }

  // A closed field learns area and device of its entity from the Bridge,
  // once per entity; the first page often has it already.
  function entityPickerLookup(input) {
    const list = input?.dataset?.entityPicker || '';
    const value = input?.value || '';
    if (!value || !entityRemoteSupported || !ENTITY_REMOTE_LISTS.has(list) || entityRemoteAsked.has(value)) {
      return;
    }
    entityRemoteAsked.add(value);
    entityRemoteInBackground(async () => {
      if (entityRemoteInfo.has(value)) return;
      if (entityPickerOpen) {
        entityRemoteAsked.delete(value);
        return;
      }
      const answer = await entityRemoteSearch(list, value, ++entityRemoteGeneration, 0);
      const item = answer && (answer.r || []).find(entry => entry.v === value);
      if (item) entityPickerShowRemote([item]);
    });
  }

  // ---- Tile adoption and placement ----

  // The tile tab of a tile's own entity field (it sits in the entity slot
  // under Type); '' for other entity fields such as the icon color source.
  function entityPickerTileTab(input) {
    const slot = input?.closest?.('.tile-entity-slot');
    return slot ? slot.id.replace(/_tile_entity_slot$/, '') : '';
  }

  // A tile takes the name, icon and icon color of every entity chosen for it
  // from the list, also the one it already has; they stay editable until an
  // entity is chosen again.
  function adoptEntityPickerEntry(tab, entry) {
    const title = document.getElementById(tab + '_tile_title');
    if (title) title.value = normalizeTileTitle(entry.name);
    // An empty icon is the entity's own icon, also its state icons.
    const icon = document.getElementById(tab + '_tile_icon');
    if (icon) icon.value = '';
    if (typeof setIconColorInput === 'function') setIconColorInput(tab, '');
  }

  function chooseEntityPickerValue(input, value, entry) {
    if (!input) return;
    const tab = entry ? entityPickerTileTab(input) : '';
    if (input.value === value && !tab) return;
    input.value = value;
    if (tab) adoptEntityPickerEntry(tab, entry);
    input.dispatchEvent(new Event('change', { bubbles: true }));
  }

  // The entity of the shown tile type sits right under Type, outside the
  // scrolling settings body, so it never needs scrolling to; it returns to
  // the start of its own type fields when another type is shown.
  function placeTileEntityField(tab) {
    const slot = document.getElementById(tab + '_tile_entity_slot');
    if (!slot) return;
    const parts = input => [
      document.querySelector('label[for="' + input.id + '_picker"]'), input, entityPickerControl(input)
    ].filter(Boolean);
    for (const input of slot.querySelectorAll('input[data-entity-picker]')) {
      document.getElementById(input.dataset.entityHome || '')?.prepend(...parts(input));
    }
    const fields = document.querySelector('#' + tab + 'Settings .type-fields.show');
    const input = fields?.querySelector(':scope > input[data-entity-picker]');
    if (!input) return;
    input.dataset.entityHome = fields.id;
    slot.append(...parts(input));
    entityPickerWarm(input);
  }

  // ---- Open list ----

  function entityPickerMatchesQuery(entry, query) {
    const words = String(query || '').toLowerCase().split(/\s+/).filter(Boolean);
    const text = (entry.name + ' ' + entry.value + ' ' + entry.entity + ' ' + (entry.context || '')).toLowerCase();
    return words.every(word => text.includes(word));
  }

  // The Bridge's answer is already filtered; while a new one is on its way
  // the previous one filters here, and the panel's own list without a Bridge.
  function entityPickerMatches() {
    const open = entityPickerOpen;
    if (open.remote === 'ready') return open.entries;
    if (open.remote === 'loading' && open.entries.length) {
      return open.entries.filter(entry => entityPickerMatchesQuery(entry, open.query));
    }
    return open.local.filter(entry => entityPickerMatchesQuery(entry, open.query));
  }

  function renderEntityPickerList(keepScroll) {
    const open = entityPickerOpen;
    const list = entityPickerPopover?.querySelector('.entity-picker-list');
    const foot = entityPickerPopover?.querySelector('.entity-picker-foot');
    if (!open || !list) return;
    // Without a Web Admin password the Bridge finds only released entities.
    if (foot) foot.hidden = !(open.remote === 'ready' && !open.full);
    // Nothing of the Bridge's list is there yet: the list waits at full size
    // instead of showing the panel's short list first and then growing.
    const waiting = open.remote === 'loading' && !open.entries.length;
    entityPickerPopover.classList.toggle('waiting', waiting);
    if (waiting || (open.state === 'loading' && open.remote !== 'ready')) {
      list.innerHTML = '<div class="entity-picker-loading">' + '<div></div>'.repeat(6) + '</div>';
      return;
    }
    if (open.state === 'failed' && open.remote !== 'ready') {
      list.innerHTML = '<div class="entity-picker-state"><i class="mdi mdi-lan-disconnect"></i>' +
        escapeHtml(t('entityPickerLoadFailed')) +
        '<button type="button" class="btn entity-picker-retry">' + escapeHtml(t('entityPickerRetry')) + '</button></div>';
      return;
    }
    const matches = entityPickerMatches();
    open.active = Math.max(0, Math.min(open.active, matches.length - 1));
    if (!matches.length) {
      list.innerHTML = '<div class="entity-picker-state">' + escapeHtml(open.query
        ? tf('entityPickerNoMatch', { query: open.query }) : t('entityPickerNone')) + '</div>';
      return;
    }
    const current = open.input.value;
    list.innerHTML = matches.map((entry, index) => {
      const kind = entityKindLabel(entry.entity);
      return '<div class="entity-picker-item' + (index === open.active ? ' active' : '') +
        (entry.value === current ? ' selected' : '') + '" role="option" aria-selected="' +
        (entry.value === current ? 'true' : 'false') + '" data-index="' + index + '">' +
        entityPickerRowHtml(entry, open.query) +
        (kind ? '<span class="entity-picker-kind">' + escapeHtml(kind) + '</span>' : '') + '</div>';
    }).join('') + (open.remote === 'ready' && open.more
      ? '<div class="entity-picker-loading entity-picker-more"><div></div></div>' : '');
    if (!keepScroll) list.querySelector('.entity-picker-item.active')?.scrollIntoView({ block: 'nearest' });
    // A short first page loads the next one right away.
    if (open.remote === 'ready' && open.more) entityPickerNearEnd(list);
  }

  function loadEntityPickerEntries(force) {
    const open = entityPickerOpen;
    if (!open) return;
    const apply = data => {
      open.local = entityPickerEntries(data, open.input.dataset.entityPicker);
      open.state = 'ready';
      // Without a search the list starts at the chosen entity.
      if (!open.query && open.remote !== 'ready') {
        open.active = Math.max(0, open.local.findIndex(entry => entry.value === open.input.value));
      }
      renderEntityPickerList();
    };
    if (entityOptionsCache && !force) {
      apply(entityOptionsCache);
    } else {
      open.state = 'loading';
      renderEntityPickerList();
    }
    fetchEntityOptions(!!force)
      .then(data => {
        if (entityPickerOpen !== open) return;
        apply(data);
        renderEntityPicker(open.input);
      })
      .catch(() => {
        if (entityPickerOpen !== open || open.state === 'ready') return;
        open.state = 'failed';
        renderEntityPickerList();
      });
  }

  function placeEntityPickerPopover() {
    const open = entityPickerOpen;
    const field = open && entityPickerControl(open.input)?.querySelector('.entity-picker-field');
    if (!field || !entityPickerPopover) return;
    const pop = entityPickerPopover;
    const viewportW = window.innerWidth;
    const viewportH = window.innerHeight;
    if (viewportW <= 560) {
      // Phones: a sheet at the bottom edge.
      pop.classList.add('sheet');
      pop.style.left = pop.style.top = pop.style.width = pop.style.maxHeight = '';
      return;
    }
    pop.classList.remove('sheet');
    const rect = field.getBoundingClientRect();
    const width = Math.min(Math.max(rect.width, 340), viewportW - 16);
    const below = viewportH - rect.bottom - 12;
    const above = rect.top - 12;
    const up = below < 280 && above > below;
    const height = Math.min(440, Math.max(160, up ? above : below));
    pop.style.width = width + 'px';
    pop.style.left = Math.max(8, Math.min(rect.left, viewportW - width - 8)) + 'px';
    pop.style.maxHeight = height + 'px';
    pop.style.top = (up ? Math.max(8, rect.top - 6 - Math.min(height, pop.offsetHeight || height))
      : rect.bottom + 6) + 'px';
  }

  function openEntityPicker(input) {
    if (!input) return;
    closeEntityPicker(false);
    if (typeof closeIconPicker === 'function') closeIconPicker(false);
    if (!entityPickerPopover) {
      entityPickerPopover = document.createElement('div');
      entityPickerPopover.className = 'entity-picker-popover';
      entityPickerPopover.innerHTML = '<div class="entity-picker-search"><i class="mdi mdi-magnify"></i>' +
        '<input type="search" autocomplete="off" spellcheck="false"></div>' +
        '<div class="entity-picker-list" role="listbox"></div>' +
        '<div class="entity-picker-foot" hidden><i class="mdi mdi-lock-outline"></i><span></span>' +
        '<button type="button" class="entity-picker-password"></button></div>';
      document.body.appendChild(entityPickerPopover);
    }
    const search = entityPickerPopover.querySelector('input');
    search.value = '';
    search.placeholder = t('entityPickerSearch');
    entityPickerPopover.querySelector('.entity-picker-foot span').textContent = t('entityPickerReleased');
    entityPickerPopover.querySelector('.entity-picker-password').textContent = t('webAuthSet');
    entityPickerOpen = { input, local: [], entries: [], query: '', active: 0, state: 'loading',
      remote: null, full: false, more: false, offset: 0, paging: false };
    const field = entityPickerControl(input)?.querySelector('.entity-picker-field');
    field?.classList.add('open');
    field?.setAttribute('aria-expanded', 'true');
    entityPickerPopover.classList.add('open');
    loadEntityPickerEntries(false);
    // The remembered first page shows at once; the Bridge's fresh one follows.
    const remembered = entityRemoteSupported && entityRemoteFirstPages.get(input.dataset.entityPicker);
    if (remembered) {
      entityPickerApplyRemote(entityPickerOpen, remembered, false);
      entityPickerOpen.active = Math.max(0, entityPickerOpen.entries.findIndex(entry => entry.value === input.value));
    }
    entityPickerRemoteSearch(entityPickerOpen);
    // Drawn once the Bridge search is under way, so the panel's short list
    // never shows first.
    renderEntityPickerList();
    placeEntityPickerPopover();
    search.focus();
  }

  function closeEntityPicker(focusField) {
    const open = entityPickerOpen;
    if (!open) return;
    entityPickerOpen = null;
    window.clearTimeout(entityRemoteTimer);
    entityPickerPopover?.classList.remove('open');
    const field = entityPickerControl(open.input)?.querySelector('.entity-picker-field');
    field?.classList.remove('open');
    field?.setAttribute('aria-expanded', 'false');
    if (focusField) field?.focus();
  }

  function pickEntityPickerIndex(index) {
    const open = entityPickerOpen;
    const entry = open ? entityPickerMatches()[index] : null;
    if (!entry) return;
    closeEntityPicker(true);
    chooseEntityPickerValue(open.input, entry.value, entry);
  }

  document.addEventListener('click', event => {
    const target = event.target;
    const control = target?.closest?.('.entity-picker');
    if (control) {
      const input = control.previousElementSibling;
      if (target.closest('.entity-picker-clear')) {
        closeEntityPicker(false);
        chooseEntityPickerValue(input, '');
        control.querySelector('.entity-picker-field')?.focus();
      } else if (target.closest('.entity-picker-field')) {
        if (entityPickerOpen?.input === input) closeEntityPicker(true);
        else openEntityPicker(input);
      }
      return;
    }
    if (!entityPickerOpen) return;
    if (!target?.closest?.('.entity-picker-popover')) {
      closeEntityPicker(false);
      return;
    }
    const item = target.closest('.entity-picker-item');
    if (item) {
      pickEntityPickerIndex(Number(item.dataset.index));
    } else if (target.closest('.entity-picker-retry')) {
      loadEntityPickerEntries(true);
    } else if (target.closest('.entity-picker-password')) {
      // The Web Admin password row (Settings > System).
      closeEntityPicker(false);
      if (typeof openSettingsPage === 'function') openSettingsPage('system', 'web_auth_section');
    }
  });

  document.addEventListener('input', event => {
    const open = entityPickerOpen;
    if (!open || !event.target?.closest?.('.entity-picker-search')) return;
    open.query = event.target.value;
    open.active = 0;
    if (open.remote) {
      // The own list right away, the Bridge's answer after a short pause; a
      // page of the previous search in flight is dropped.
      open.remote = 'loading';
      entityRemoteGeneration++;
      window.clearTimeout(entityRemoteTimer);
      entityRemoteTimer = window.setTimeout(() => entityPickerRemoteSearch(open), ENTITY_REMOTE_DELAY_MS);
    }
    renderEntityPickerList();
  });

  document.addEventListener('keydown', event => {
    if (!entityPickerOpen || !event.target?.closest?.('.entity-picker-search')) return;
    const count = entityPickerMatches().length;
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      event.preventDefault();
      if (!count) return;
      const step = event.key === 'ArrowDown' ? 1 : -1;
      entityPickerOpen.active = (entityPickerOpen.active + step + count) % count;
      renderEntityPickerList();
    } else if (event.key === 'Enter') {
      event.preventDefault();
      pickEntityPickerIndex(entityPickerOpen.active);
    } else if (event.key === 'Escape') {
      event.preventDefault();
      closeEntityPicker(true);
    } else if (event.key === 'Tab') {
      closeEntityPicker(false);
    }
  });

  // The open list follows its field once per frame while the page scrolls or
  // the window is resized; scrolling the list itself moves nothing.
  const placeEntityPickerSoon = perFrame(placeEntityPickerPopover);
  window.addEventListener('resize', perFrame(placeEntityPickerPopover));
  document.addEventListener('scroll', event => {
    if (!entityPickerOpen) return;
    if (event.target?.classList?.contains('entity-picker-list')) entityPickerNearEnd(event.target);
    else if (!event.target?.closest?.('.entity-picker-popover')) placeEntityPickerSoon();
  }, true);

  // Tile tabs arrive later (folder fragments, session cache): attach the
  // fields of every inserted entity input.
  document.addEventListener('DOMContentLoaded', () => {
    setupEntityPickers(document);
    if (typeof MutationObserver !== 'function') return;
    new MutationObserver(records => {
      for (const record of records) {
        for (const node of record.addedNodes) {
          if (node.nodeType !== 1) continue;
          if (node.matches?.('input[data-entity-picker]')) attachEntityPicker(node);
          else if (node.querySelector?.('input[data-entity-picker]')) setupEntityPickers(node);
        }
      }
    }).observe(document.body, { childList: true, subtree: true });
  });
  // The fixed head of the tile settings: Title and Icon side by side under
  // the heading with Type (web_admin_html.cpp). The title has at most two
  // lines like every tile title; the icon picker keeps the icon name in the
  // hidden `<tab>_tile_icon` input: empty = automatic (the entity's own
  // icon, also its state icons), "none" = no icon, else an MDI name from the
  // panel's own list (/api/mdi_icons).

  const ICON_PICKER_STEP = 240;
  const tileHeadNativeTitle = typeof HTMLTextAreaElement === 'function'
    ? Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value') : null;
  const tileHeadNativeInput = typeof HTMLInputElement === 'function'
    ? Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value') : null;
  let mdiIconNames = null;
  let mdiIconFetch = null;
  let iconPickerPopover = null;
  let iconPickerOpen = null;  // {tab, query, matches, shown, active, state}

  function fetchMdiIconNames() {
    if (mdiIconNames) return Promise.resolve(mdiIconNames);
    if (!mdiIconFetch) {
      mdiIconFetch = fetch('/api/mdi_icons')
        .then(res => {
          if (!res.ok) throw new Error('MDI icons HTTP ' + res.status);
          return res.text();
        })
        .then(text => {
          mdiIconNames = text.split('\n').map(name => name.trim()).filter(Boolean);
          return mdiIconNames;
        })
        .finally(() => { mdiIconFetch = null; });
    }
    return mdiIconFetch;
  }

  // ---- Title: one line, two at most ----

  function syncTileTitleRows(textarea) {
    textarea?.classList.toggle('two', String(textarea.value || '').includes('\n'));
  }

  function tileHeadTab(element) {
    return String(element?.id || '').replace(/_tile_(icon|icon_picker|title|title_auto)$/, '');
  }

  // ---- Title: Auto = the entity's name from Home Assistant ----

  function tileEntityName(tab) {
    const entity = document.querySelector('#' + tab + '_tile_entity_slot input[data-entity-picker]');
    return entity && entity.value && typeof entityPickerName === 'function' ? entityPickerName(entity) : '';
  }

  // Auto lights while the title is the entity's name: choosing an entity or
  // the switch puts the name in, typing turns it off. The title stays a plain
  // stored title, so nothing new is saved.
  function syncTileTitleAuto(tab) {
    const button = document.getElementById(tab + '_tile_title_auto');
    const title = document.getElementById(tab + '_tile_title');
    if (!button || !title) return;
    const name = tileEntityName(tab);
    const on = !!name && title.value === normalizeTileTitle(name);
    button.classList.toggle('on', on);
    button.setAttribute('aria-pressed', on ? 'true' : 'false');
    button.disabled = !name;
  }

  // ---- Icon button ----

  // The icon the tile shows without a choice of its own: its entity's icon,
  // else the icon its preview draws (type defaults such as a folder).
  function tileAutomaticIcon(tab) {
    const entity = document.querySelector('#' + tab + '_tile_entity_slot input[data-entity-picker]');
    const entry = entity && entity.value && typeof entityPickerEntryFor === 'function'
      ? entityPickerEntryFor(entity, entity.value) : null;
    if (entry && entry.icon) return entry.icon;
    const preview = document.getElementById(tab + '-tile-' + currentTileIndex)?.querySelector('.tile-icon');
    const drawn = Array.from(preview?.classList || []).find(name => name.startsWith('mdi-'));
    return drawn ? drawn.substring(4) : 'shape-outline';
  }

  function renderTileIconButton(tab) {
    const input = document.getElementById(tab + '_tile_icon');
    const button = document.getElementById(tab + '_tile_icon_picker');
    if (!input || !button) return;
    const raw = String(input.value || '').trim();
    const none = isExplicitlyDisabledValue(raw);
    const icon = none ? 'cancel' : (normalizeMdiIconName(raw) || tileAutomaticIcon(tab));
    button.innerHTML = '<i class="mdi mdi-' + escapeHtml(icon) + '"></i>';
    button.classList.toggle('none', none);
    button.title = none ? t('iconPickerNone') : raw ? normalizeMdiIconName(raw) : t('iconPickerAuto');
  }

  // Also after names and icons of entities arrived from the Bridge.
  function refreshTileIconButtons(tab) {
    const selector = tab ? '#' + tab + '_tile_icon' : 'input[data-tile-icon]';
    document.querySelectorAll(selector).forEach(input => {
      renderTileIconButton(tileHeadTab(input));
      syncTileTitleAuto(tileHeadTab(input));
    });
  }

  // Programmatic loads (tile, draft, paste, import) redraw the head.
  function attachTileHead(element) {
    if (!element || Object.prototype.hasOwnProperty.call(element, 'value')) return;
    const title = element.tagName === 'TEXTAREA';
    const native = title ? tileHeadNativeTitle : tileHeadNativeInput;
    if (!native) return;
    Object.defineProperty(element, 'value', {
      configurable: true,
      get() { return native.get.call(this); },
      set(value) {
        native.set.call(this, value);
        if (title) {
          syncTileTitleRows(this);
          syncTileTitleAuto(tileHeadTab(this));
        } else {
          renderTileIconButton(tileHeadTab(this));
        }
      }
    });
    if (title) {
      syncTileTitleRows(element);
      syncTileTitleAuto(tileHeadTab(element));
    } else {
      renderTileIconButton(tileHeadTab(element));
    }
  }

  function setupTileHeads(root) {
    (root || document).querySelectorAll?.('input[data-tile-icon], textarea.tile-title-input').forEach(attachTileHead);
  }

  // ---- Icon list ----

  function iconPickerChoose(value) {
    const open = iconPickerOpen;
    if (!open) return;
    const input = document.getElementById(open.tab + '_tile_icon');
    closeIconPicker(true);
    if (!input || input.value === value) return;
    input.value = value;
    input.dispatchEvent(new Event('input', { bubbles: true }));
  }

  function iconPickerCurrent(tab) {
    const raw = String(document.getElementById(tab + '_tile_icon')?.value || '').trim();
    return isExplicitlyDisabledValue(raw) ? 'none' : normalizeMdiIconName(raw);
  }

  function iconPickerCellsHtml(from, to) {
    const open = iconPickerOpen;
    const current = iconPickerCurrent(open.tab);
    let html = '';
    for (let index = from; index < to; index++) {
      const name = open.matches[index];
      html += '<button type="button" class="icon-picker-cell' + (name === current ? ' selected' : '') +
        (index === open.active ? ' active' : '') + '" data-icon="' + escapeHtml(name) + '" data-index="' + index +
        '" title="' + escapeHtml(name) + '"><i class="mdi mdi-' + escapeHtml(name) + '"></i></button>';
    }
    return html;
  }

  function renderIconPickerList() {
    const open = iconPickerOpen;
    const list = iconPickerPopover?.querySelector('.icon-picker-list');
    if (!open || !list) return;
    const current = iconPickerCurrent(open.tab);
    const choice = (value, icon, name, hint) =>
      '<button type="button" class="icon-picker-choice' + (current === value ? ' selected' : '') + '" data-icon="' +
      value + '"><span class="entity-picker-icon"><i class="mdi mdi-' + escapeHtml(icon) + '"></i></span>' +
      '<span class="entity-picker-text"><span class="entity-picker-name">' + escapeHtml(name) + '</span>' +
      '<span class="entity-picker-context">' + escapeHtml(hint) + '</span></span></button>';
    let html = open.query ? '' : '<div class="icon-picker-choices">' +
      choice('', tileAutomaticIcon(open.tab), t('iconPickerAuto'), t('iconPickerAutoHint')) +
      choice('none', 'cancel', t('iconPickerNone'), t('iconPickerNoneHint')) + '</div>';
    if (open.state === 'loading') {
      html += '<div class="entity-picker-loading"><div></div><div></div><div></div></div>';
    } else if (open.state === 'failed') {
      html += '<div class="entity-picker-state"><i class="mdi mdi-lan-disconnect"></i>' + escapeHtml(t('networkError')) +
        '<button type="button" class="btn icon-picker-retry">' + escapeHtml(t('entityPickerRetry')) + '</button></div>';
    } else if (!open.matches.length) {
      html += '<div class="entity-picker-state">' + escapeHtml(tf('iconPickerNoMatch', { query: open.query })) + '</div>';
    } else {
      html += '<div class="icon-picker-grid">' + iconPickerCellsHtml(0, open.shown) + '</div>';
    }
    list.innerHTML = html;
    iconPickerShowName();
  }

  // More icons as the list scrolls near its end.
  function growIconPickerList() {
    const open = iconPickerOpen;
    const grid = iconPickerPopover?.querySelector('.icon-picker-grid');
    if (!open || !grid || open.shown >= open.matches.length) return;
    const next = Math.min(open.matches.length, open.shown + ICON_PICKER_STEP);
    grid.insertAdjacentHTML('beforeend', iconPickerCellsHtml(open.shown, next));
    open.shown = next;
  }

  function iconPickerShowName(name) {
    const footer = iconPickerPopover?.querySelector('.icon-picker-name');
    const open = iconPickerOpen;
    if (!footer || !open) return;
    footer.textContent = name || open.matches[open.active] || ' ';
  }

  function filterIconPicker() {
    const open = iconPickerOpen;
    const words = open.query.toLowerCase().split(/\s+/).filter(Boolean);
    open.matches = (mdiIconNames || []).filter(name => words.every(word => name.includes(word)));
    open.shown = Math.min(open.matches.length, ICON_PICKER_STEP);
    // A search marks its first match for Enter; without one only the chosen
    // icon is marked.
    open.active = open.query ? 0 : -1;
    if (!open.query) {
      const current = open.matches.indexOf(iconPickerCurrent(open.tab));
      if (current >= 0) {
        open.active = current;
        open.shown = Math.min(open.matches.length, Math.max(open.shown, current + ICON_PICKER_STEP));
      }
    }
  }

  function loadIconPicker() {
    const open = iconPickerOpen;
    if (!open) return;
    open.state = mdiIconNames ? 'ready' : 'loading';
    if (mdiIconNames) filterIconPicker();
    renderIconPickerList();
    if (mdiIconNames) return;
    fetchMdiIconNames()
      .then(() => {
        if (iconPickerOpen !== open) return;
        open.state = 'ready';
        filterIconPicker();
        renderIconPickerList();
      })
      .catch(() => {
        if (iconPickerOpen !== open) return;
        open.state = 'failed';
        renderIconPickerList();
      });
  }

  function placeIconPicker() {
    const open = iconPickerOpen;
    const row = open && document.getElementById(open.tab + '_tile_title')?.closest('.tile-head-row');
    if (!row || !iconPickerPopover) return;
    const pop = iconPickerPopover;
    if (window.innerWidth <= 560) {
      pop.classList.add('sheet');
      pop.style.left = pop.style.top = pop.style.width = pop.style.maxHeight = '';
      return;
    }
    pop.classList.remove('sheet');
    const rect = row.getBoundingClientRect();
    const below = window.innerHeight - rect.bottom - 12;
    const above = rect.top - 12;
    const up = below < 300 && above > below;
    const height = Math.min(460, Math.max(180, up ? above : below));
    pop.style.left = rect.left + 'px';
    pop.style.width = rect.width + 'px';
    pop.style.maxHeight = height + 'px';
    pop.style.top = (up ? Math.max(8, rect.top - 6 - Math.min(height, pop.offsetHeight || height)) : rect.bottom + 6) + 'px';
  }

  function openIconPicker(tab) {
    closeIconPicker(false);
    if (typeof closeEntityPicker === 'function') closeEntityPicker(false);
    if (!iconPickerPopover) {
      iconPickerPopover = document.createElement('div');
      iconPickerPopover.className = 'icon-picker-popover';
      iconPickerPopover.innerHTML = '<div class="icon-picker-search"><i class="mdi mdi-magnify"></i>' +
        '<input type="search" autocomplete="off" spellcheck="false"></div>' +
        '<div class="icon-picker-list" role="listbox"></div><div class="icon-picker-name"> </div>';
      document.body.appendChild(iconPickerPopover);
    }
    const search = iconPickerPopover.querySelector('input');
    search.value = '';
    search.placeholder = t('iconPickerSearch');
    iconPickerOpen = { tab, query: '', matches: [], shown: 0, active: 0, state: 'loading' };
    const button = document.getElementById(tab + '_tile_icon_picker');
    button?.classList.add('open');
    button?.setAttribute('aria-expanded', 'true');
    iconPickerPopover.classList.add('open');
    iconPickerPopover.querySelector('.icon-picker-list').scrollTop = 0;
    loadIconPicker();
    placeIconPicker();
    iconPickerPopover.querySelector('.icon-picker-cell.active')?.scrollIntoView({ block: 'nearest' });
    search.focus();
  }

  function closeIconPicker(focusButton) {
    const open = iconPickerOpen;
    if (!open) return;
    iconPickerOpen = null;
    iconPickerPopover?.classList.remove('open');
    const button = document.getElementById(open.tab + '_tile_icon_picker');
    button?.classList.remove('open');
    button?.setAttribute('aria-expanded', 'false');
    if (focusButton) button?.focus();
  }

  function moveIconPickerActive(step) {
    const open = iconPickerOpen;
    const grid = iconPickerPopover?.querySelector('.icon-picker-grid');
    if (!open || !grid || !open.matches.length) return;
    const next = Math.max(0, Math.min(open.matches.length - 1, open.active + step));
    while (next >= open.shown) growIconPickerList();
    grid.querySelector('.icon-picker-cell.active')?.classList.remove('active');
    open.active = next;
    const cell = grid.querySelector('[data-index="' + next + '"]');
    cell?.classList.add('active');
    cell?.scrollIntoView({ block: 'nearest' });
    iconPickerShowName();
  }

  document.addEventListener('click', event => {
    const target = event.target;
    const auto = target?.closest?.('.tile-title-auto');
    if (auto) {
      // Off: the entity's name comes back like typed (preview, draft, save).
      const tab = tileHeadTab(auto);
      const title = document.getElementById(tab + '_tile_title');
      const name = tileEntityName(tab);
      if (title && name && !auto.classList.contains('on')) {
        title.value = normalizeTileTitle(name);
        title.dispatchEvent(new Event('input', { bubbles: true }));
      }
      return;
    }
    const button = target?.closest?.('.tile-icon-picker');
    if (button) {
      const tab = tileHeadTab(button);
      if (iconPickerOpen?.tab === tab) closeIconPicker(true);
      else openIconPicker(tab);
      return;
    }
    if (!iconPickerOpen) return;
    if (!target?.closest?.('.icon-picker-popover')) {
      closeIconPicker(false);
      return;
    }
    const choice = target.closest('[data-icon]');
    if (choice) iconPickerChoose(choice.dataset.icon);
    else if (target.closest('.icon-picker-retry')) loadIconPicker();
  });

  document.addEventListener('mouseover', event => {
    const cell = iconPickerOpen && event.target?.closest?.('.icon-picker-cell');
    if (cell) iconPickerShowName(cell.dataset.icon);
  });

  document.addEventListener('input', event => {
    const target = event.target;
    if (target?.classList?.contains('tile-title-input')) {
      syncTileTitleRows(target);
      syncTileTitleAuto(tileHeadTab(target));
    }
    if (!iconPickerOpen || !target?.closest?.('.icon-picker-search')) return;
    iconPickerOpen.query = target.value;
    if (iconPickerOpen.state === 'ready') filterIconPicker();
    renderIconPickerList();
  });

  document.addEventListener('keydown', event => {
    const target = event.target;
    // A third line is blocked; pasted lines join the second line
    // (normalizeTileTitle in the live editor).
    if (event.key === 'Enter' && target?.classList?.contains('tile-title-input') &&
        String(target.value || '').includes('\n')) {
      event.preventDefault();
      return;
    }
    if (!iconPickerOpen || !target?.closest?.('.icon-picker-search')) return;
    const grid = iconPickerPopover.querySelector('.icon-picker-grid');
    const first = grid?.querySelector('.icon-picker-cell');
    let columns = 1;
    if (first) {
      const top = first.offsetTop;
      columns = Array.from(grid.children).findIndex(cell => cell.offsetTop !== top);
      if (columns <= 0) columns = grid.children.length || 1;
    }
    const steps = { ArrowRight: 1, ArrowLeft: -1, ArrowDown: columns, ArrowUp: -columns };
    if (steps[event.key]) {
      event.preventDefault();
      moveIconPickerActive(steps[event.key]);
    } else if (event.key === 'Enter') {
      event.preventDefault();
      const name = iconPickerOpen.matches[iconPickerOpen.active];
      if (name) iconPickerChoose(name);
    } else if (event.key === 'Escape') {
      event.preventDefault();
      closeIconPicker(true);
    } else if (event.key === 'Tab') {
      closeIconPicker(false);
    }
  });

  // The open list follows its row once per frame while the page scrolls or
  // the window is resized; scrolling the list itself loads more icons.
  const placeIconPickerSoon = perFrame(placeIconPicker);
  window.addEventListener('resize', perFrame(placeIconPicker));
  document.addEventListener('scroll', event => {
    if (!iconPickerOpen) return;
    const list = event.target?.closest?.('.icon-picker-list');
    if (list) {
      if (list.scrollTop + list.clientHeight > list.scrollHeight - 200) growIconPickerList();
    } else if (!event.target?.closest?.('.icon-picker-popover')) {
      placeIconPickerSoon();
    }
  }, true);

  // Rows of choices (Icon color, Tile color) span the panel in equal parts;
  // every label stays on one line, the font steps down from 12 px to 10 px
  // at most until nothing overflows.
  const choiceRowObserver = typeof ResizeObserver === 'function'
    ? new ResizeObserver(entries => entries.forEach(entry => fitChoiceRow(entry.target))) : null;

  function fitChoiceRow(row) {
    if (!row || !row.offsetWidth) return;
    const buttons = Array.from(row.querySelectorAll('button'));
    buttons.forEach(button => { button.style.fontSize = ''; });
    const shown = buttons.filter(button => button.offsetWidth);
    const overflows = () => shown.some(button => button.scrollWidth > button.clientWidth + 0.5);
    for (let size = 11.5; size >= 10 && overflows(); size -= 0.5) {
      shown.forEach(button => { button.style.fontSize = size + 'px'; });
    }
  }

  function fitChoiceRows(tab) {
    for (const suffix of ['_tile_icon_color_modes', '_tile_color_modes']) {
      fitChoiceRow(document.getElementById(tab + suffix));
    }
  }

  function observeChoiceRows(root) {
    if (!choiceRowObserver) return;
    (root || document).querySelectorAll?.('.choice-row').forEach(row => choiceRowObserver.observe(row));
  }

  // The Type list opens down from the head; keep it inside the panel.
  function fitTileTypePicker(event) {
    const select = event.target;
    if (!select?.matches?.('.tile-head-top select')) return;
    const panel = select.closest('.tile-settings')?.getBoundingClientRect();
    if (!panel) return;
    const room = Math.round(panel.bottom - select.getBoundingClientRect().bottom - 12);
    select.style.setProperty('--picker-room', Math.max(160, room) + 'px');
  }
  document.addEventListener('pointerdown', fitTileTypePicker, true);
  document.addEventListener('focusin', fitTileTypePicker);

  // A newly chosen entity brings its icon to the button and its name (Auto).
  document.addEventListener('change', event => {
    const slot = event.target?.closest?.('.tile-entity-slot');
    if (!slot) return;
    const tab = slot.id.replace(/_tile_entity_slot$/, '');
    renderTileIconButton(tab);
    syncTileTitleAuto(tab);
  });

  document.addEventListener('DOMContentLoaded', () => {
    setupTileHeads(document);
    observeChoiceRows(document);
    if (typeof MutationObserver !== 'function') return;
    new MutationObserver(records => {
      for (const record of records) {
        for (const node of record.addedNodes) {
          if (node.nodeType !== 1) continue;
          if (node.matches?.('input[data-tile-icon], textarea.tile-title-input')) attachTileHead(node);
          else if (node.querySelector?.('input[data-tile-icon], textarea.tile-title-input')) setupTileHeads(node);
          if (node.matches?.('.choice-row')) observeChoiceRows(node.parentNode);
          else observeChoiceRows(node);
        }
      }
    }).observe(document.body, { childList: true, subtree: true });
  });

  // Per-tile icon disc options are common to every type with an icon, so
  // they travel with the type fields through drafts, copy/paste and saves.
  function tileTypeHasIcon(typeValue) {
    return !['0', '16'].includes(String(typeValue ?? '0'));
  }
  // Glow only matters where the icon can take a color: from its entity
  // (switch/light, climate, cover, binary sensor), from the color bar or
  // state colors (sensor family, energy) or from a fixed icon color (scene,
  // folder, back, camera). Other icons are always white.
  function tileTypeHasColoredIcon(typeValue) {
    return ['1', '2', '4', '5', '8', '12', '14', '15', '17', '18', '19', '20', '21', '22', '23']
      .includes(String(typeValue ?? '0'));
  }
  // Stored disc mode: 0 follows the global option, 2 hides the disc on this
  // tile. A legacy stored 1 ("on") loads as checked.
  function iconDiscModeFromCheckbox(box) {
    return box?.checked === false ? '2' : '0';
  }
  // Like the per-tile Tile borders option, only Back, Clock and Text can hide
  // their own icon disc; every other tile follows the global option.
  function tileTypeHasDiscToggle(typeValue) {
    return ['8', '9', '10'].includes(String(typeValue ?? '0'));
  }
  function collectIconDiscFields(tab, typeValue) {
    const box = document.getElementById(tab + '_tile_icon_disc');
    if (!box || !tileTypeHasIcon(typeValue)) return {};
    const glow = document.getElementById(tab + '_tile_icon_glow');
    return {
      icon_disc: tileTypeHasDiscToggle(typeValue) ? iconDiscModeFromCheckbox(box) : '0',
      icon_glow: tileTypeHasColoredIcon(typeValue) && glow?.checked === false ? '0' : '1',
    };
  }
  function loadIconDiscFields(tab, data) {
    const box = document.getElementById(tab + '_tile_icon_disc');
    if (box) box.checked = String(data?.icon_disc) !== '2';
    const glow = document.getElementById(tab + '_tile_icon_glow');
    if (glow) glow.checked = !['0', 'false'].includes(String(data?.icon_glow));
    syncIconDiscFields(tab);
  }
  function resetIconDiscFields(tab) {
    const box = document.getElementById(tab + '_tile_icon_disc');
    if (box) box.checked = true;
    const glow = document.getElementById(tab + '_tile_icon_glow');
    if (glow) glow.checked = true;
  }
  function syncIconDiscFields(tab) {
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const discToggle = tileTypeHasDiscToggle(typeValue);
    const colored = tileTypeHasColoredIcon(typeValue);
    const weather = typeValue === '12';
    document.getElementById(tab + '_tile_icon_disc_fields')
      ?.classList.toggle('hidden', !tileTypeHasIcon(typeValue) || (!discToggle && !colored && !weather));
    document.getElementById(tab + '_tile_icon_disc_row')?.classList.toggle('hidden', !discToggle);
    document.getElementById(tab + '_tile_icon_glow_row')?.classList.toggle('hidden', !colored);
    document.getElementById(tab + '_weather_colored_icons_row')?.classList.toggle('hidden', !weather);
  }

  function collectTypeFieldValues(tab) {
    const prefix = tab;
    const typeValue = document.getElementById(prefix + '_tile_type')?.value || '0';
    const meta = getTileTypeMeta(typeValue);
    const out = collectIconDiscFields(prefix, typeValue);
    if (!meta.save) return out;
    const fd = new FormData();
    callTypeHandler(meta, 'save', prefix, fd);
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
    let col = clampHalf(snapshot?.col, 1, GRID_COLS, fallbackCol);
    let row = clampHalf(snapshot?.row, firstRow + 1, GRID_ROWS + 0.5, fallbackRow);
    let spanW = clampHalf(snapshot?.span_w, 0.5, GRID_COLS, 1);
    let spanH = clampHalf(snapshot?.span_h, 0.5, GRID_ROWS, 1);
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
    // A deleted (empty) tile starts from nothing: merged over the previous
    // data, its entity and options returned with the next tile in the slot.
    const tile = Number(snapshot?.type) === 0 ? {} : Object.assign({}, prev);
    const layout = normalizeSnapshotLayout(snapshot, index, tab);
    const numericFields = ['type', 'sensor_decimals', 'sensor_value_font', 'sensor_display_mode', 'sensor_gauge_min', 'sensor_gauge_max', 'switch_style', 'navigate_target', 'popup_open_mode', 'key_code', 'key_modifier', 'background_opacity', 'icon_disc', 'icon_glow'];

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
    for (const kind of ['lock', 'alarm', 'fan']) {
      if (snapshot && Object.prototype.hasOwnProperty.call(snapshot, kind + '_entity')) tile.sensor_entity = snapshot[kind + '_entity'] || '';
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

    if ([8,9,10].includes(Number(tile.type)) && snapshot?.tile_border !== undefined) {
      tile.sensor_display_mode = ['0','false'].includes(String(snapshot.tile_border)) ? 1 : 0;
    }
    if (Number(tile.type) === 12 && snapshot?.weather_colored_icons !== undefined) {
      tile.sensor_display_mode = ['0','false'].includes(String(snapshot.weather_colored_icons)) ? 1 : 0;
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
  // Per-tile icon colors for the Sensor family (Sensor, Number, Select,
  // Date/Time), Binary sensor and Energy: a fixed icon color, a color bar for
  // numeric states ("Icon color by value") and up to six state colors for
  // text states ("Icon color by state"); Binary sensors show one On and one
  // Off color, stored as the state lines "is RRGGBB on" and "is RRGGBB off".
  // The editor keeps the canonical v2 record of
  // src/tiles/config/tile_icon_colors.h in the "icon_colors" field. Parsing,
  // normalization and the color formulas below mirror that header step by
  // step, so the Web Admin previews show the device colors. The six type
  // modules call the load/save/reset helpers from their own field handlers,
  // so drafts, copy/paste, autosave and import/export carry the record like
  // any other type field.
  // Rules (every tile type, "src ..." in the record): the tile's own entity
  // or another one, entity color or own rules, coloring the icon and/or
  // tinting the tile (tile_icon_source.cpp). Scene, Folder, Back, Camera,
  // Clock and Text have no entity of their own and use another entity only
  // (tileTypeHasFixedIconColorOnly / tileTypeRulesUseOwnEntity in
  // tile_type_policy.h).
  const ICON_COLOR_FIXED_TYPES = ['2', '4', '8', '9', '10', '18'];
  const ICON_COLOR_OWN_TYPES = ['1', '5', '12', '14', '15', '17', '19', '20', '21', '22', '23', '24', '25', '26'];
  // The own entity field of each type (pairs, not an object with numeric keys).
  const ICON_COLOR_ENTITY_FIELDS = [['1', '_sensor_entity'], ['5', '_switch_entity'], ['12', '_weather_entity'],
    ['14', '_energy_entity'], ['15', '_media_entity'], ['17', '_climate_entity'], ['19', '_cover_entity'],
    ['20', '_binary_sensor_entity'], ['21', '_number_entity'], ['22', '_select_entity'], ['23', '_datetime_entity'],
    ['24', '_lock_entity'], ['25', '_alarm_entity'], ['26', '_fan_entity']];
  // Domains shown by the Switch tile (tile_icon_source.cpp switch_domain).
  const ICON_COLOR_SWITCH_DOMAINS = ['light', 'switch', 'input_boolean', 'automation', 'fan',
    'humidifier', 'remote', 'siren'];
  const ICON_COLOR_TYPES = ICON_COLOR_OWN_TYPES.concat(ICON_COLOR_FIXED_TYPES);
  const ICON_COLOR_BAR_TYPES = ['1', '14', '21'];
  const ICON_COLOR_ROW_TYPES = ['1', '20', '22', '23'];
  // Media: icon color and tile color "From cover" (the "cover" line).
  const ICON_COLOR_MEDIA_TYPE = '15';
  const ICON_COLOR_MAX_STOPS = 6;
  const ICON_COLOR_MAX_ROWS = 6;
  const ICON_COLOR_MAX_VALUE_BYTES = 32;
  const ICON_COLOR_MAX_NUMBER_BYTES = 12;
  const ICON_COLOR_LEGACY_MAX_RULES = 3;
  const ICON_COLOR_ROW_DEFAULT = '#22C55E';
  // The Binary sensor state colors without per-tile colors.
  const ICON_COLOR_BINARY_DEFAULTS = { on: '#FFC107', off: '#9E9E9E' };
  // Stop positions are thousandths of the bar.
  const ICON_COLOR_PRESETS = {
    cold_warm: [[0, 0x3B82F6], [450, 0x22C55E], [700, 0xF59E0B], [1000, 0xEF4444]],
    traffic: [[0, 0x22C55E], [500, 0xEAB308], [1000, 0xEF4444]],
    battery: [[0, 0xEF4444], [250, 0xF59E0B], [600, 0x22C55E], [1000, 0x22C55E]],
    humidity: [[0, 0xF59E0B], [400, 0x22C55E], [700, 0x22C55E], [1000, 0x3B82F6]],
    single: [[0, 0xFFFFFF], [1000, 0xFFFFFF]]
  };

  function tileTypeHasIconColors(typeValue) {
    return ICON_COLOR_TYPES.includes(String(typeValue ?? '0'));
  }

  function tileTypeHasFixedIconColorOnly(typeValue) {
    return ICON_COLOR_FIXED_TYPES.includes(String(typeValue ?? '0'));
  }

  // ---- Record model (mirrors tile_icon_colors.h) ----

  function iconColorTrim(text) {
    return String(text).replace(/^[ \t\r]+|[ \t\r]+$/g, '');
  }

  // "RRGGBB" or "#RRGGBB", exactly.
  function iconColorParseHex(text) {
    const match = /^#?([0-9a-fA-F]{6})$/.exec(String(text));
    return match ? parseInt(match[1], 16) : null;
  }

  function iconColorHex(rgb) {
    return (rgb >>> 0).toString(16).toUpperCase().padStart(6, '0');
  }

  function normalizeIconColorHex(value) {
    const rgb = iconColorParseHex(String(value ?? '').trim());
    return rgb === null ? '' : '#' + iconColorHex(rgb);
  }

  function iconColorIsV2(text) {
    return text.startsWith('v2') && (text.length === 2 || text[2] === '\n' || text[2] === '\r');
  }

  // parse_decimal(): "[-]digits[.digits]" with comma or dot, at most twelve
  // characters; returns the value and the canonical text.
  function iconColorParseDecimal(text) {
    const trimmed = iconColorTrim(text);
    if (!trimmed || trimmed.length > ICON_COLOR_MAX_NUMBER_BYTES ||
        !/^-?[0-9]*[.,]?[0-9]*$/.test(trimmed) || !/[0-9]/.test(trimmed)) return null;
    const canonical = trimmed.replace(',', '.');
    const value = Number(canonical);
    return Number.isFinite(value) ? { value, text: canonical } : null;
  }

  // leading_number(): the leading decimal number of a state, without exponent.
  function iconColorLeadingNumber(state) {
    const match = /^[ \t\r]*(-?[0-9]*(?:[.,][0-9]*)?)/.exec(String(state));
    const text = match ? match[1] : '';
    if (!/[0-9]/.test(text) || text.length > 31) return null;
    const value = Number(text.replace(',', '.'));
    return Number.isFinite(value) ? value : null;
  }

  // fold_at(): ASCII and the Latin-1 letters U+00C0-U+00DE except U+00D7.
  function iconColorFold(text) {
    let out = '';
    for (const ch of String(text)) {
      const code = ch.codePointAt(0);
      if (code >= 0x41 && code <= 0x5A) out += String.fromCharCode(code + 32);
      else if (code >= 0xC0 && code <= 0xDE && code !== 0xD7) out += String.fromCharCode(code + 0x20);
      else out += ch;
    }
    return out;
  }

  function iconColorTextMatches(op, value, state) {
    if (state === null || state === undefined) return false;
    const haystack = iconColorFold(iconColorTrim(String(state)));
    const needle = iconColorFold(value);
    if (op === 'is') return haystack === needle;
    return needle.length > 0 && haystack.includes(needle);
  }

  // parse_rule(): "<op> RRGGBB <value>".
  function iconColorParseRule(line) {
    const text = iconColorTrim(line);
    const space = text.indexOf(' ');
    const op = space < 0 ? '' : text.slice(0, space);
    if (!['ge', 'le', 'eq', 'is', 'has'].includes(op)) return null;
    const rest = text.slice(space + 1);
    const colorEnd = rest.indexOf(' ');
    const color = iconColorParseHex(colorEnd < 0 ? rest : rest.slice(0, colorEnd));
    if (color === null) return null;
    const value = colorEnd < 0 ? '' : iconColorTrim(rest.slice(colorEnd));
    return value ? { op, color, value } : null;
  }

  function iconColorSortStops(stops) {
    return stops.sort((a, b) => a.position - b.position);
  }

  // parse_stop(): "P:RRGGBB", P with 1-4 digits, clamped to 0..1000.
  function iconColorParseStop(token) {
    const colon = token.indexOf(':');
    if (colon < 1 || colon > 4 || !/^[0-9]+$/.test(token.slice(0, colon))) return null;
    const color = iconColorParseHex(token.slice(colon + 1));
    if (color === null) return null;
    return { position: Math.min(Number(token.slice(0, colon)), 1000), color };
  }

  // parse_bar(): "bar <smooth|steps> <min> <max> <P>:<RRGGBB> ...".
  function iconColorParseBar(line) {
    const tokens = String(line).split(/[ \t\r]+/).filter(Boolean);
    if (tokens[0] !== 'bar' || !['smooth', 'steps'].includes(tokens[1])) return null;
    const min = iconColorParseDecimal(tokens[2] ?? '');
    const max = iconColorParseDecimal(tokens[3] ?? '');
    if (!min || !max || !(min.value < max.value)) return null;
    const stops = [];
    for (const token of tokens.slice(4)) {
      if (stops.length >= ICON_COLOR_MAX_STOPS) break;
      const stop = iconColorParseStop(token);
      if (stop) stops.push(stop);
    }
    if (stops.length < 2) return null;
    return { mode: tokens[1], min: min.value, max: max.value, minText: min.text, maxText: max.text,
      stops: iconColorSortStops(stops) };
  }

  // valid_entity(): "<domain>.<object>" in lowercase letters, digits and
  // underscores, at most 128 characters.
  function iconColorValidEntity(entity) {
    const text = String(entity ?? '');
    return text.length <= 128 && /^[a-z0-9_]+\.[a-z0-9_]+$/.test(text);
  }

  // parse_source_line(): "src <auto|rules> <self|entity_id> [tile=NN]
  // [noicon] [off]", options in any order.
  function iconColorParseSource(line) {
    const tokens = String(line).split(/[ \t\r]+/).filter(Boolean);
    if (tokens.length < 3 || tokens[0] !== 'src' || !['auto', 'rules'].includes(tokens[1])) return null;
    const layer = { mode: tokens[1], self: tokens[2] === 'self', entity: '', tile: 0, icon: true, enabled: true };
    if (!layer.self) {
      if (!iconColorValidEntity(tokens[2])) return null;
      layer.entity = tokens[2];
    }
    for (const token of tokens.slice(3)) {
      if (token === 'noicon') layer.icon = false;
      else if (token === 'off') layer.enabled = false;
      else if (/^tile=[0-9]{1,2}$/.test(token)) layer.tile = Math.min(50, Math.max(10, Number(token.slice(5))));
      else return null;
    }
    return layer;
  }

  // own_state_colors_icon(): no rule layer (b40 records) or an enabled
  // "src rules self" that colors the icon.
  function iconColorOwnStateColorsIcon(record) {
    const layer = iconColorRecordSource(record);
    return !layer || (layer.mode === 'rules' && layer.self && layer.enabled && layer.icon);
  }

  // source_of(): the first "src" line of a v2 record, or null.
  function iconColorRecordSource(record) {
    const text = String(record ?? '');
    if (!iconColorIsV2(text)) return null;
    const line = text.split('\n').slice(2).find(candidate => candidate.startsWith('src '));
    return line === undefined ? null : iconColorParseSource(line);
  }

  // mix_colors(): per 8-bit channel with a weight of 0..1024, rounded.
  function iconColorMix(a, b, weight) {
    let out = 0;
    for (let shift = 16; shift >= 0; shift -= 8) {
      const from = (a >> shift) & 0xFF;
      const to = (b >> shift) & 0xFF;
      out |= ((from * (1024 - weight) + to * weight + 512) >> 10) << shift;
    }
    return out >>> 0;
  }

  // bar_color_at(): smooth bars interpolate between neighbouring stops;
  // steps bars take the last stop at or below t; t is clamped to 0..1.
  function iconColorBarColorAt(bar, t) {
    if (!(t > 0)) t = 0;
    if (t > 1) t = 1;
    let index = -1;
    bar.stops.forEach((stop, i) => { if (stop.position / 1000 <= t) index = i; });
    if (index < 0) return bar.stops[0].color;
    if (bar.mode === 'steps' || index + 1 === bar.stops.length) return bar.stops[index].color;
    const from = bar.stops[index].position / 1000;
    const to = bar.stops[index + 1].position / 1000;
    const fraction = (t - from) / (to - from);
    return iconColorMix(bar.stops[index].color, bar.stops[index + 1].color, Math.floor(fraction * 1024 + 0.5));
  }

  function iconColorBarColor(bar, value) {
    return iconColorBarColorAt(bar, (value - bar.min) / (bar.max - bar.min));
  }

  // resolve(): the icon color for a known state as "#RRGGBB", or '' for the
  // type default. Only v2 records are evaluated.
  function resolveIconColorRecord(record, state, display, fixedFallback = true) {
    const text = String(record ?? '');
    if (!iconColorIsV2(text) || state === undefined || state === null) return '';
    const lines = text.split('\n');
    const number = iconColorLeadingNumber(state);
    let barSeen = false;
    for (const line of lines.slice(2)) {
      if (line.startsWith('bar ')) {
        if (barSeen) continue;
        barSeen = true;
        const bar = number !== null ? iconColorParseBar(line) : null;
        if (bar) return '#' + iconColorHex(iconColorBarColor(bar, number));
        continue;
      }
      const rule = iconColorParseRule(line);
      if (!rule || (rule.op !== 'is' && rule.op !== 'has')) continue;
      if (iconColorTextMatches(rule.op, rule.value, state) ||
          (display !== undefined && display !== null && iconColorTextMatches(rule.op, rule.value, display))) {
        return '#' + iconColorHex(rule.color);
      }
    }
    if (!fixedFallback) return '';
    const fixed = iconColorParseHex(iconColorTrim(lines[1] ?? ''));
    return fixed === null ? '' : '#' + iconColorHex(fixed);
  }

  // copy_value() plus trim: no control characters, at most 32 UTF-8 bytes
  // without a split character.
  function iconColorClipValue(value) {
    let bytes = new TextEncoder().encode(String(value).replace(/[\u0000-\u001f\u007f]/g, ''));
    if (bytes.length > ICON_COLOR_MAX_VALUE_BYTES) {
      let n = ICON_COLOR_MAX_VALUE_BYTES;
      let lead = n;
      while (lead > 0 && (bytes[lead - 1] & 0xC0) === 0x80) lead--;
      if (lead > 0) {
        const c = bytes[lead - 1];
        const need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (n - (lead - 1) < need) n = lead - 1;
      }
      bytes = bytes.slice(0, n);
    }
    return iconColorTrim(new TextDecoder().decode(bytes));
  }

  // A b39 numeric rule counts only with a complete number.
  function iconColorLegacyNumericValid(value) {
    return new TextEncoder().encode(value).length <= ICON_COLOR_MAX_VALUE_BYTES &&
      /^[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?$/.test(value.replace(/,/g, '.'));
  }

  // migrate_legacy_bar(): b39 ">=" thresholds on whole numbers in descending
  // order become a steps bar when their positions are exact thousandths.
  function iconColorMigrateLegacyBar(body, fixed) {
    const thresholds = [];
    const colors = [];
    let rules = 0;
    for (const line of body) {
      if (rules >= ICON_COLOR_LEGACY_MAX_RULES) break;
      const rule = iconColorParseRule(line);
      if (!rule) continue;
      if (rule.op === 'is' || rule.op === 'has') { rules++; continue; }
      if (!iconColorLegacyNumericValid(rule.value)) continue;
      rules++;
      if (rule.op !== 'ge' || !/^-?[0-9]{1,7}$/.test(rule.value)) return null;
      const value = Number(rule.value);
      if (thresholds.length && value >= thresholds[thresholds.length - 1]) return null;
      thresholds.push(value);
      colors.push(rule.color);
    }
    if (!thresholds.length) return null;
    const lowest = thresholds[thresholds.length - 1];
    const highest = thresholds[0];
    const min = thresholds.length === 1 ? lowest - 1 : lowest - (highest - lowest);
    const range = highest - min;
    const stops = [{ position: 0, color: fixed === null ? 0xFFFFFF : fixed }];
    for (let i = thresholds.length - 1; i >= 0; i--) {
      const scaled = (thresholds[i] - min) * 1000;
      if (scaled % range) return null;
      stops.push({ position: scaled / range, color: colors[i] });
    }
    return { mode: 'steps', min, max: highest, minText: String(min), maxText: String(highest), stops };
  }

  // normalize(): any record (editor, import, b39) in the canonical v2 form;
  // numeric types keep only the bar, text types only the state lines.
  function normalizeIconColorRecord(record, allowBar, allowRows, allowSource = false, allowSelf = false,
    allowCover = false) {
    const text = String(record ?? '');
    const v2 = iconColorIsV2(text);
    let source = allowSource ? iconColorRecordSource(text) : null;
    if (source?.self && !allowSelf) source = null;
    if (source?.mode === 'rules') {
      if (!source.self || (!allowBar && !allowRows)) {
        allowBar = true;
        allowRows = true;
      }
    } else if (source?.mode === 'auto') {
      allowBar = false;
      allowRows = false;
    }
    // "src rules self" coloring the icon only is implicit (b40 records).
    const emitLayer = !!source &&
      !(source.mode === 'rules' && source.self && source.icon && !source.tile && source.enabled);
    const lines = text.split('\n');
    const fixedIndex = v2 ? 1 : 0;
    const fixed = iconColorParseHex(iconColorTrim(lines[fixedIndex] ?? ''));
    const body = lines.slice(fixedIndex + 1);
    let bar = null;
    if (allowBar && v2) {
      const line = body.find(candidate => candidate.startsWith('bar '));
      if (line !== undefined) bar = iconColorParseBar(line);
    } else if (allowBar) {
      bar = iconColorMigrateLegacyBar(body, fixed);
    }
    let out = 'v2\n' + (fixed === null ? '' : iconColorHex(fixed));
    const fill = v2 ? iconColorFillOf(body) : 0;
    if (fill) out += '\nfill ' + fill;
    const cover = allowCover && v2 ? iconColorCoverOf(body) : { icon: false, tile: 0 };
    const hasCover = cover.icon || cover.tile > 0;
    if (hasCover) out += '\ncover' + (cover.icon ? ' icon' : '') + (cover.tile ? ' tile=' + cover.tile : '');
    if (emitLayer) {
      out += '\nsrc ' + source.mode + ' ' + (source.self ? 'self' : source.entity) +
        (source.tile ? ' tile=' + source.tile : '') + (source.icon ? '' : ' noicon') + (source.enabled ? '' : ' off');
    }
    if (bar) {
      out += '\nbar ' + bar.mode + ' ' + bar.minText + ' ' + bar.maxText +
        bar.stops.map(stop => ' ' + stop.position + ':' + iconColorHex(stop.color)).join('');
    }
    let rows = 0;
    if (allowRows) {
      let legacyRules = 0;
      for (const line of body) {
        if (rows >= ICON_COLOR_MAX_ROWS) break;
        if (v2 && (line.startsWith('bar ') || line.startsWith('src ') || line.startsWith('cover'))) continue;
        const rule = iconColorParseRule(line);
        if (!rule) continue;
        const textRule = rule.op === 'is' || rule.op === 'has';
        if (!v2) {
          if (legacyRules >= ICON_COLOR_LEGACY_MAX_RULES) break;
          if (!textRule) {
            if (iconColorLegacyNumericValid(rule.value)) legacyRules++;
            continue;
          }
          legacyRules++;
        } else if (!textRule) {
          continue;
        }
        const value = iconColorClipValue(rule.value);
        if (!value) continue;
        out += '\n' + rule.op + ' ' + iconColorHex(rule.color) + ' ' + value;
        rows++;
      }
    }
    return fixed === null && !fill && !hasCover && !emitLayer && !bar && rows === 0 ? '' : out;
  }

  // tile_icon_colors::fill_of(): the "fill NN" tint of the fixed color in
  // percent (clamped like the rule tint), 0 without one.
  function iconColorFillOf(lines) {
    const line = lines.find(candidate => candidate.startsWith('fill '));
    if (line === undefined) return 0;
    const text = iconColorTrim(line.slice(5));
    if (!/^[0-9]+$/.test(text) || text.length > 4) return 0;
    return Math.min(50, Math.max(10, Number(text)));
  }

  // tile_icon_colors::cover_of(): "From cover" of a Media tile, the first
  // "cover [icon] [tile=NN]" line; an unknown token makes it invalid.
  function iconColorCoverOf(lines) {
    const none = { icon: false, tile: 0 };
    const line = lines.find(candidate => candidate.startsWith('cover'));
    if (line === undefined) return none;
    const tokens = line.split(/[ \t\r]+/).filter(Boolean);
    if (tokens[0] !== 'cover') return none;
    const out = { icon: false, tile: 0 };
    for (const token of tokens.slice(1)) {
      if (token === 'icon') out.icon = true;
      else if (/^tile=[0-9]{1,2}$/.test(token)) out.tile = Math.min(50, Math.max(10, Number(token.slice(5))));
      else return none;
    }
    return out;
  }

  // Editor view of a record: fixed color, bar and state rows.
  function parseIconColorRecord(record) {
    const lines = normalizeIconColorRecord(record, true, true, true, true, true).split('\n');
    const fixed = iconColorParseHex(lines[1] ?? '');
    let bar = null;
    const rows = [];
    for (const line of lines.slice(2)) {
      if (line.startsWith('bar ')) { bar = iconColorParseBar(line); continue; }
      const rule = iconColorParseRule(line);
      if (rule) rows.push({ has: rule.op === 'has', color: '#' + iconColorHex(rule.color), value: rule.value });
    }
    return { color: fixed === null ? '' : '#' + iconColorHex(fixed), bar, rows,
      fill: iconColorFillOf(lines.slice(2)), cover: iconColorCoverOf(lines.slice(2)),
      source: iconColorRecordSource(lines.join('\n')) };
  }

  // ---- Source entity (icon-and-title tiles), mirrors tile_icon_source.cpp ----

  // The raw state of a payload: the JSON "state" field, else the plain text.
  function iconColorPayloadState(payload) {
    const text = String(payload ?? '').trim();
    if (!text.startsWith('{')) return text;
    try {
      const value = JSON.parse(text);
      return value && value.state !== undefined && value.state !== null ? String(value.state).trim() : '';
    } catch (_) {
      return '';
    }
  }

  function iconColorStateKnown(state) {
    const text = String(state ?? '').trim().toLowerCase();
    return !!text && !['unavailable', 'unknown', 'none', 'null'].includes(text);
  }

  // "auto": the entity's own icon color as its tile preview shows it.
  function iconColorSourceAutoColor(entity, payload) {
    const domain = String(entity).split('.')[0];
    const text = String(payload ?? '').trim();
    if (!text) return '';
    if (ICON_COLOR_SWITCH_DOMAINS.includes(domain) && typeof parseSwitchPayload === 'function') {
      const state = parseSwitchPayload(text);
      if (!state || state.available === false || !state.hasState) return '';
      return state.isOn ? (state.hasColor ? state.color : '#FFD54F') : '#B0B0B0';
    }
    if (domain === 'binary_sensor' && typeof parseBinarySensorPreviewPayload === 'function') {
      const state = parseBinarySensorPreviewPayload(text);
      if (!state?.valid || state.available !== true || !['on', 'off'].includes(state.state)) return '';
      return binarySensorPreviewColor(state);
    }
    if (domain === 'climate' && typeof parseClimatePreviewPayload === 'function') {
      const state = parseClimatePreviewPayload(text);
      return state && state.available !== false ? climatePreviewColor(state) : '';
    }
    if (domain === 'cover' && typeof parseCoverPreviewPayload === 'function') {
      const state = parseCoverPreviewPayload(text);
      return state && state.available !== false ? coverPreviewColor(state) : '';
    }
    return '';
  }

  // The `active` flag of tile_icon_source.cpp's auto_color(): false while the
  // entity is off, closed or not running (climate_visuals::state_active()).
  function iconColorSourceAutoActive(entity, payload) {
    const domain = String(entity).split('.')[0];
    const text = String(payload ?? '').trim();
    if (!text) return false;
    if (ICON_COLOR_SWITCH_DOMAINS.includes(domain) && typeof parseSwitchPayload === 'function') {
      const state = parseSwitchPayload(text);
      return !!state && state.available !== false && !!state.hasState && !!state.isOn;
    }
    if (domain === 'binary_sensor' && typeof parseBinarySensorPreviewPayload === 'function') {
      const state = parseBinarySensorPreviewPayload(text);
      return !!state?.valid && state.available === true && state.state === 'on';
    }
    if (domain === 'climate' && typeof parseClimatePreviewPayload === 'function') {
      const state = parseClimatePreviewPayload(text);
      if (!state || state.available === false) return false;
      const action = String(state.action || '');
      const mode = String(state.mode || '');
      if (['heating', 'preheating', 'cooling', 'drying', 'fan', 'defrosting'].includes(action)) return true;
      return !!mode && mode !== 'off' && mode !== 'unknown' && action !== 'off';
    }
    if (domain === 'cover' && typeof parseCoverPreviewPayload === 'function') {
      const state = parseCoverPreviewPayload(text);
      const value = String(state?.state || 'unknown').toLowerCase();
      return !!state && state.available !== false && !['closed', 'unknown', 'unavailable'].includes(value);
    }
    return false;
  }

  // tile_icon_source::rule_color(): the rule color of a layer from the preview
  // states of its entity (own or other); '' without a known state or result.
  // Rules never fall back to the fixed color here.
  function iconColorLayerColor(record, layer, ownEntity, meta, typeValue) {
    if (!layer || !layer.enabled) return '';
    const entity = layer.self ? String(ownEntity || '') : layer.entity;
    if (!entity) return '';
    if (layer.self && ['21', '22', '23'].includes(String(typeValue))) {
      if (layer.mode === 'auto' || typeof iconColorRuleState !== 'function') return '';
      const rule = iconColorRuleState(typeValue, entity, meta, null);
      return rule ? resolveIconColorRecord(record, rule.state, rule.display, false) : '';
    }
    const payload = meta?.values?.[entity];
    if (payload === undefined || payload === null || !String(payload).trim()) return '';
    if (layer.mode === 'auto') return normalizeIconColorHex(iconColorSourceAutoColor(entity, payload));
    const state = iconColorPayloadState(payload);
    if (!iconColorStateKnown(state)) return '';
    let display = null;
    if (entity.startsWith('binary_sensor.') && typeof parseBinarySensorPreviewPayload === 'function' &&
        typeof binarySensorPreviewStateText === 'function') {
      const parsed = parseBinarySensorPreviewPayload(String(payload));
      if (parsed?.valid && ['on', 'off'].includes(parsed.state)) display = binarySensorPreviewStateText(parsed);
    }
    return resolveIconColorRecord(record, state, display, false);
  }

  // The icon color of a tile with rules on another entity, else the fixed
  // color ('' = white).
  function iconColorSourcePreview(record, meta) {
    const layer = iconColorRecordSource(record);
    const color = layer && !layer.self && layer.icon ? iconColorLayerColor(record, layer, '', meta, '') : '';
    return color || resolveIconColorRecord(record, '', null);
  }

  // The rules' tile tint for the preview: { color, percent } or null. Entity
  // color tints only while the entity is active, like refresh_card(). With
  // Tile color "From icon" the tile follows the icon instead (no rule tint).
  function iconColorTilePreviewTint(typeValue, record, ownEntity, meta) {
    const layer = iconColorRecordSource(record);
    if (parseIconColorRecord(record).fill) return null;
    const ruleTint = (() => {
      if (!layer || !layer.enabled || !layer.tile) return null;
      const color = iconColorLayerColor(record, layer, ownEntity, meta, typeValue);
      if (!color) return null;
      if (layer.mode === 'auto') {
        const entity = layer.self ? String(ownEntity || '') : layer.entity;
        if (!iconColorSourceAutoActive(entity, meta?.values?.[entity])) return null;
      }
      return tileTintChoice(true, color, layer.tile, 0, '');
    })();
    return ruleTint;
  }

  // tile_tint::has_hue(): white, grey and black never tint a tile.
  function tileTintHasHue(color) {
    const hex = normalizeIconColorHex(color);
    return !!hex && !(hex.slice(1, 3) === hex.slice(3, 5) && hex.slice(3, 5) === hex.slice(5, 7));
  }

  // tile_tint::choose(), the one tint rule of device and preview: with Tile
  // color "From icon" the tile follows the real color the icon shows (rules
  // included), else an applying rule "Tint tile" with a real color tints it;
  // null without a tint.
  function tileTintChoice(ruleActive, ruleColor, rulePercent, fillPercent, iconColor) {
    if (fillPercent) {
      return tileTintHasHue(iconColor) ? { color: normalizeIconColorHex(iconColor), percent: fillPercent } : null;
    }
    if (ruleActive && rulePercent && tileTintHasHue(ruleColor)) {
      return { color: normalizeIconColorHex(ruleColor), percent: rulePercent };
    }
    return null;
  }

  // tile_tint::background(): the base mixed with the color, darkened in 5 %
  // steps until white text keeps a contrast of at least 4.5:1.
  function tileTintBackground(base, color, percent) {
    const parse = value => {
      const hex = normalizeIconColorHex(value);
      return hex ? [1, 3, 5].map(i => parseInt(hex.slice(i, i + 2), 16)) : [42, 42, 42];
    };
    const channel = v => { const c = v / 255; return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
    const contrast = rgb => 1.05 / (0.2126 * channel(rgb[0]) + 0.7152 * channel(rgb[1]) + 0.0722 * channel(rgb[2]) + 0.05);
    const from = parse(base);
    const to = parse(color);
    let out = from.map((v, i) => Math.floor((v * (100 - percent) + to[i] * percent + 50) / 100));
    for (let i = 0; i < 40 && contrast(out) < 4.5; i++) out = out.map(v => Math.floor((v * 95 + 50) / 100));
    return '#' + out.map(v => v.toString(16).toUpperCase().padStart(2, '0')).join('');
  }
  // Tints a preview card from the global tile color. A screensaver card keeps
  // its own opacity (data-bg-opacity): the panel sets bg_opa after the tint
  // (image_screensaver build_slot_tile); the opaque tint hid the wallpaper
  // in the preview (user 2026-10-02).
  function setTileTintBackground(el, color, percent) {
    const base = String(getComputedStyle(document.documentElement).getPropertyValue('--tile-default-bg') || '').trim();
    const hex = tileTintBackground(base || '#1A1A1A', color, percent);
    const opacity = el.dataset.bgOpacity;
    el.style.background = opacity === undefined ? hex : hex + Number(opacity).toString(16).padStart(2, '0');
  }

  // Entities offered as a source: the states the Bridge publishes to tiles.
  function iconColorSourceEntries(data) {
    const seen = new Set();
    const entries = [];
    for (const list of [data?.sensors, data?.binary_sensors, data?.switches, data?.climates, data?.covers]) {
      for (const entry of Array.isArray(list) ? list : []) {
        const value = String(entry?.v ?? '');
        if (!iconColorValidEntity(value) || seen.has(value)) continue;
        seen.add(value);
        entries.push(entry);
      }
    }
    return entries;
  }

  // ---- Editor ----

  const iconColorEl = (tab, suffix) => document.getElementById(tab + suffix);

  function iconColorTypeOf(tab) {
    return String(iconColorEl(tab, '_tile_type')?.value || '0');
  }

  // Bar state lives in a hidden input: "" (off) or "<mode> P:RRGGBB ...".
  function readIconColorBar(tab) {
    const tokens = String(iconColorEl(tab, '_tile_icon_bar')?.value || '').split(' ').filter(Boolean);
    const mode = ['smooth', 'steps'].includes(tokens[0]) ? tokens[0] : 'off';
    const stops = mode === 'off' ? [] : tokens.slice(1).map(iconColorParseStop).filter(Boolean);
    return { mode, stops };
  }

  function writeIconColorBar(tab, mode, stops) {
    const input = iconColorEl(tab, '_tile_icon_bar');
    if (!input) return;
    input.value = mode === 'off' ? ''
      : [mode].concat(stops.map(stop => stop.position + ':' + iconColorHex(stop.color))).join(' ');
  }

  function iconColorSelectedStop(tab) {
    const value = Number(iconColorEl(tab, '_tile_icon_bar_handles')?.dataset.selected ?? -1);
    return Number.isInteger(value) ? value : -1;
  }

  function setIconColorSelectedStop(tab, index) {
    const handles = iconColorEl(tab, '_tile_icon_bar_handles');
    if (handles) handles.dataset.selected = String(index);
  }

  function iconColorBarRange(tab) {
    const min = iconColorParseDecimal(iconColorEl(tab, '_tile_icon_bar_min')?.value ?? '');
    const max = iconColorParseDecimal(iconColorEl(tab, '_tile_icon_bar_max')?.value ?? '');
    return min && max && min.value < max.value ? { min: min.value, max: max.value } : null;
  }

  function ensureIconColorRange(tab) {
    if (iconColorBarRange(tab)) return;
    const min = iconColorEl(tab, '_tile_icon_bar_min');
    const max = iconColorEl(tab, '_tile_icon_bar_max');
    if (min) min.value = '0';
    if (max) max.value = '100';
  }

  function iconColorFormatValue(value, span) {
    const decimals = span >= 20 ? 0 : (span >= 2 ? 1 : 2);
    return String(Number(value.toFixed(decimals)));
  }

  function iconColorGradient(mode, stops) {
    const css = rgb => '#' + iconColorHex(rgb);
    if (mode === 'steps') {
      const parts = [];
      stops.forEach((stop, index) => {
        const from = index === 0 ? 0 : stop.position / 10;
        const to = index + 1 < stops.length ? stops[index + 1].position / 10 : 100;
        parts.push(css(stop.color) + ' ' + from + '%', css(stop.color) + ' ' + to + '%');
      });
      return 'linear-gradient(90deg, ' + parts.join(', ') + ')';
    }
    return 'linear-gradient(90deg, ' +
      stops.map(stop => css(stop.color) + ' ' + stop.position / 10 + '%').join(', ') + ')';
  }

  // Draws the bar, the stop handles and their value labels. Existing handles
  // are updated in place so a drag keeps its pointer capture.
  function renderIconColorBar(tab) {
    const section = iconColorEl(tab, '_tile_icon_bar_section');
    if (!section) return;
    const bar = readIconColorBar(tab);
    section.querySelectorAll('[data-icon-color="mode"]').forEach(button => {
      const active = button.dataset.mode === bar.mode;
      button.classList.toggle('active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
    iconColorEl(tab, '_tile_icon_bar_editor')?.classList.toggle('hidden', bar.mode === 'off');
    const strip = iconColorEl(tab, '_tile_icon_bar_strip');
    const handles = iconColorEl(tab, '_tile_icon_bar_handles');
    if (!strip || !handles || bar.mode === 'off') return;
    strip.style.background = iconColorGradient(bar.mode, iconColorSortStops(bar.stops.map(stop => ({ ...stop }))));
    let knobs = Array.from(handles.querySelectorAll('.icon-color-stop'));
    let labels = Array.from(handles.querySelectorAll('.icon-color-stop-label'));
    if (knobs.length !== bar.stops.length) {
      knobs.concat(labels).forEach(el => el.remove());
      knobs = bar.stops.map((stop, index) => {
        const knob = document.createElement('button');
        knob.type = 'button';
        knob.className = 'icon-color-stop';
        knob.dataset.iconColor = 'stop';
        knob.dataset.stop = String(index);
        handles.appendChild(knob);
        return knob;
      });
      labels = bar.stops.map(() => {
        const label = document.createElement('span');
        label.className = 'icon-color-stop-label';
        handles.appendChild(label);
        return label;
      });
    }
    const selected = iconColorSelectedStop(tab);
    const range = iconColorBarRange(tab);
    bar.stops.forEach((stop, index) => {
      const left = stop.position / 10 + '%';
      knobs[index].style.left = left;
      knobs[index].style.background = '#' + iconColorHex(stop.color);
      knobs[index].classList.toggle('selected', index === selected);
      labels[index].style.left = left;
      labels[index].textContent = range
        ? iconColorFormatValue(range.min + stop.position / 1000 * (range.max - range.min), range.max - range.min)
        : '';
    });
    const remove = handles.querySelector('[data-icon-color="stop-remove"]');
    const removable = selected >= 0 && selected < bar.stops.length && bar.stops.length > 2;
    if (remove) {
      remove.classList.toggle('hidden', !removable);
      if (removable) remove.style.left = bar.stops[selected].position / 10 + '%';
    }
  }

  function iconColorRuleRow(tab, index) {
    return iconColorEl(tab, '_tile_icon_rule_' + index);
  }

  function readIconColorRows(tab) {
    const rows = [];
    for (let index = 0; index < ICON_COLOR_MAX_ROWS; index++) {
      const row = iconColorRuleRow(tab, index);
      if (!row || row.classList.contains('hidden')) continue;
      rows.push({
        value: document.getElementById(row.id + '_value')?.value || '',
        has: !!document.getElementById(row.id + '_has')?.checked,
        color: document.getElementById(row.id + '_color')?.value || ICON_COLOR_ROW_DEFAULT
      });
    }
    return rows;
  }

  function writeIconColorRows(tab, rows) {
    for (let index = 0; index < ICON_COLOR_MAX_ROWS; index++) {
      const row = iconColorRuleRow(tab, index);
      if (!row) continue;
      const entry = rows[index];
      row.classList.toggle('hidden', !entry);
      const value = document.getElementById(row.id + '_value');
      const has = document.getElementById(row.id + '_has');
      const color = document.getElementById(row.id + '_color');
      if (value) value.value = entry ? entry.value : '';
      if (has) has.checked = !!entry?.has;
      if (color) color.value = entry ? (normalizeIconColorHex(entry.color) || ICON_COLOR_ROW_DEFAULT) : ICON_COLOR_ROW_DEFAULT;
    }
    iconColorEl(tab, '_tile_icon_rule_add')?.classList.toggle('hidden', rows.length >= ICON_COLOR_MAX_ROWS);
  }

  // Binary sensor On/Off pickers; unset pickers keep the type color.
  function setIconColorBinaryInput(tab, state, color) {
    const input = iconColorEl(tab, '_tile_icon_' + state);
    if (!input) return;
    const hex = normalizeIconColorHex(color);
    input.value = hex || input.dataset.default || ICON_COLOR_BINARY_DEFAULTS[state];
    input.dataset.unset = hex ? '0' : '1';
  }

  function readIconColorBinaryRows(tab) {
    const rows = [];
    for (const state of ['on', 'off']) {
      const input = iconColorEl(tab, '_tile_icon_' + state);
      if (input && input.dataset.unset === '0') rows.push({ value: state, has: false, color: input.value });
    }
    return rows;
  }

  // An unset icon color keeps the type's default (white or state color).
  function setIconColorInput(tab, color) {
    const input = iconColorEl(tab, '_tile_icon_color');
    if (!input) return;
    const hex = normalizeIconColorHex(color);
    input.value = hex || '#FFFFFF';
    input.dataset.unset = hex ? '0' : '1';
  }

  function iconColorOwnEntity(tab, type) {
    const field = ICON_COLOR_ENTITY_FIELDS.find(pair => pair[0] === type);
    return field ? String(iconColorEl(tab, field[1])?.value || '') : '';
  }

  // The rule layer in the editor, or null when there is nothing to keep
  // (another entity without a choice, or switched off at the defaults).
  function readIconColorSource(tab) {
    const type = iconColorTypeOf(tab);
    const own = ICON_COLOR_OWN_TYPES.includes(type) && iconColorEl(tab, '_tile_icon_source_kind')?.value !== 'other';
    const entity = String(iconColorEl(tab, '_tile_icon_source')?.value || '').trim();
    if (!own && !iconColorValidEntity(entity)) return null;
    const strength = Number(iconColorEl(tab, '_tile_icon_rule_strength')?.value || 20);
    return {
      mode: iconColorEl(tab, '_tile_icon_source_mode')?.value === 'auto' ? 'auto' : 'rules',
      self: own,
      entity: own ? '' : entity,
      tile: iconColorEl(tab, '_tile_icon_rule_tile')?.checked
        ? Math.min(50, Math.max(10, Math.round(strength / 5) * 5)) : 0,
      icon: iconColorEl(tab, '_tile_icon_rule_icon')?.checked !== false,
      enabled: iconColorEl(tab, '_tile_icon_rules_on')?.value === '1'
    };
  }

  function writeIconColorSource(tab, layer) {
    const select = iconColorEl(tab, '_tile_icon_source');
    const entity = layer && !layer.self ? layer.entity : '';
    if (select) select.value = entity;
    const set = (suffix, value) => { const el = iconColorEl(tab, suffix); if (el) el.value = value; };
    // Without a stored layer the rules start at Own entity and Own rules, also
    // when the cell is still Empty and only gets its type afterwards; types
    // without an entity of their own show Other entity regardless.
    set('_tile_icon_source_kind', layer && !layer.self ? 'other' : 'self');
    set('_tile_icon_source_mode', layer?.mode === 'auto' ? 'auto' : 'rules');
    const icon = iconColorEl(tab, '_tile_icon_rule_icon');
    if (icon) icon.checked = layer ? layer.icon : true;
    const tile = iconColorEl(tab, '_tile_icon_rule_tile');
    if (tile) tile.checked = !!layer?.tile;
    set('_tile_icon_rule_strength', String(layer?.tile || 20));
  }

  function collectIconColorRecord(tab) {
    const type = iconColorTypeOf(tab);
    const input = iconColorEl(tab, '_tile_icon_color');
    // Media "From cover": the icon takes the cover color instead of its own.
    const media = type === ICON_COLOR_MEDIA_TYPE;
    const coverIcon = media && !!iconColorEl(tab, '_tile_icon_cover')?.checked;
    const coverTile = media && !!iconColorEl(tab, '_tile_cover_fill')?.checked;
    const fixed = input && input.dataset.unset !== '1' && !coverIcon ? normalizeIconColorHex(input.value).slice(1) : '';
    const lines = ['v2', fixed];
    if (iconColorEl(tab, '_tile_icon_fill')?.checked) {
      lines.push('fill ' + (iconColorEl(tab, '_tile_icon_fill_strength')?.value || '20'));
    }
    if (coverIcon || coverTile) {
      lines.push('cover' + (coverIcon ? ' icon' : '') +
        (coverTile ? ' tile=' + (iconColorEl(tab, '_tile_icon_fill_strength')?.value || '20') : ''));
    }
    const layer = ICON_COLOR_TYPES.includes(type) ? readIconColorSource(tab) : null;
    const bar = readIconColorBar(tab);
    if (bar.mode !== 'off') {
      // A number with inner spaces is invalid rather than a second token.
      const number = suffix => {
        const text = iconColorTrim(iconColorEl(tab, suffix)?.value ?? '');
        return /^[^ \t\r]+$/.test(text) ? text : '-';
      };
      lines.push(['bar', bar.mode, number('_tile_icon_bar_min'), number('_tile_icon_bar_max')]
        .concat(bar.stops.map(stop => stop.position + ':' + iconColorHex(stop.color))).join(' '));
    }
    const binaryOwn = type === '20' && (!layer || layer.self);
    const rows = binaryOwn ? readIconColorBinaryRows(tab) : readIconColorRows(tab);
    for (const row of rows) {
      const color = normalizeIconColorHex(row.color) || ICON_COLOR_ROW_DEFAULT;
      const value = row.value.replace(/[\u0000-\u001f\u007f]/g, '');
      lines.push((row.has ? 'has ' : 'is ') + color.slice(1) + ' ' + value);
    }
    // A switched-off own layer at its defaults without rules is no layer.
    const idle = layer && layer.self && !layer.enabled && layer.mode === 'rules' && layer.icon && !layer.tile &&
      bar.mode === 'off' && rows.length === 0;
    if (layer && !idle) {
      lines.splice(2, 0, 'src ' + layer.mode + ' ' + (layer.self ? 'self' : layer.entity) +
        (layer.tile ? ' tile=' + layer.tile : '') + (layer.icon ? '' : ' noicon') + (layer.enabled ? '' : ' off'));
    }
    return normalizeIconColorRecord(lines.join('\n'), ICON_COLOR_BAR_TYPES.includes(type),
      ICON_COLOR_ROW_TYPES.includes(type), true, ICON_COLOR_OWN_TYPES.includes(type), media);
  }

  // States can be numbers or text: the current state picks the bar or the
  // state list while neither holds colors.
  function iconColorSensorIsText(tab, sourceEntity = null) {
    const entity = sourceEntity ?? (iconColorEl(tab, '_sensor_entity')?.value || '');
    const meta = typeof sensorMetaCache === 'object' ? sensorMetaCache : null;
    const raw = iconColorPayloadState(meta?.values?.[entity] ?? '');
    if (!raw || ['unavailable', 'unknown', 'none', 'null', '--'].includes(raw.toLowerCase())) return false;
    return iconColorLeadingNumber(raw) === null;
  }

  function iconColorMarkActive(tab, role, value) {
    iconColorEl(tab, '_tile_icon_source_section')?.querySelectorAll('[data-icon-color="' + role + '"]').forEach(button => {
      const active = button.dataset.mode === value;
      button.classList.toggle('active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
  }

  function syncIconColorFields(tab) {
    const block = iconColorEl(tab, '_tile_icon_color_fields');
    if (!block) return;
    const type = iconColorTypeOf(tab);
    const visible = tileTypeHasIconColors(type);
    // Tile color "From icon color" lives with the tile color (grid-preview.js).
    if (typeof syncTileColorMode === 'function') syncTileColorMode(tab);
    block.classList.toggle('hidden', !visible);
    iconColorEl(tab, '_tile_icon_color_fixed')?.classList.toggle('hidden', !visible);
    // Icon color: Automatic (unset), Custom (the color field) or, on Media
    // only, From cover.
    const media = type === ICON_COLOR_MEDIA_TYPE;
    const coverIconBox = iconColorEl(tab, '_tile_icon_cover');
    if (coverIconBox && !media) coverIconBox.checked = false;
    const coverIcon = media && !!coverIconBox?.checked;
    const unset = iconColorEl(tab, '_tile_icon_color')?.dataset.unset !== '0';
    const iconMode = coverIcon ? 'cover' : unset ? 'auto' : 'own';
    iconColorEl(tab, '_tile_icon_color_row')?.classList.toggle('hidden', iconMode !== 'own');
    iconColorEl(tab, '_tile_icon_color_modes')?.querySelectorAll('[data-icon-color="icon-color-mode"]').forEach(button => {
      const active = button.dataset.mode === iconMode;
      button.classList.toggle('active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
      if (button.dataset.mode === 'cover') button.classList.toggle('hidden', !media);
    });
    if (typeof fitChoiceRows === 'function') fitChoiceRows(tab);
    if (!visible) return;
    const own = ICON_COLOR_OWN_TYPES.includes(type);
    const kindInput = iconColorEl(tab, '_tile_icon_source_kind');
    const kind = own && kindInput?.value !== 'other' ? 'self' : 'other';
    const on = iconColorEl(tab, '_tile_icon_rules_on')?.value === '1';
    const mode = iconColorEl(tab, '_tile_icon_source_mode')?.value === 'auto' ? 'auto' : 'rules';
    const tileTint = !!iconColorEl(tab, '_tile_icon_rule_tile')?.checked;
    // With Tile color "From icon" the tile follows the icon: "Tint tile"
    // stays visible but greyed out (its setting is kept) and a note says why.
    const followsIcon = !!iconColorEl(tab, '_tile_icon_fill')?.checked;
    iconColorEl(tab, '_tile_icon_source_section')?.classList.remove('hidden');
    iconColorEl(tab, '_tile_icon_rules_body')?.classList.toggle('hidden', !on);
    iconColorEl(tab, '_tile_icon_source_kinds')?.classList.toggle('hidden', !own);
    iconColorEl(tab, '_tile_icon_source')?.classList.toggle('hidden', kind !== 'other');
    iconColorEl(tab, '_tile_icon_rule_strength_row')?.classList.toggle('hidden', !tileTint);
    iconColorEl(tab, '_tile_icon_rule_strength_row')?.classList.toggle('is-disabled', followsIcon);
    const tintBox = iconColorEl(tab, '_tile_icon_rule_tile');
    if (tintBox) tintBox.disabled = followsIcon;
    tintBox?.closest('label')?.classList.toggle('is-disabled', followsIcon);
    const tintStrength = iconColorEl(tab, '_tile_icon_rule_strength');
    if (tintStrength) tintStrength.disabled = followsIcon;
    iconColorEl(tab, '_tile_icon_rule_follows_icon')?.classList.toggle('hidden', !followsIcon);
    const strength = iconColorEl(tab, '_tile_icon_rule_strength');
    const output = iconColorEl(tab, '_tile_icon_rule_strength_value');
    if (strength && output) output.textContent = strength.value + ' %';
    iconColorMarkActive(tab, 'rules-on', on ? '1' : '0');
    iconColorMarkActive(tab, 'source-kind', kind);
    iconColorMarkActive(tab, 'source-mode', mode);
    const layer = readIconColorSource(tab);
    let showBinary = false;
    let showBar = false;
    let showRows = false;
    if (on && mode === 'rules' && layer) {
      const sensorBar = ICON_COLOR_BAR_TYPES.includes(type);
      const sensorRows = ICON_COLOR_ROW_TYPES.includes(type);
      if (kind === 'self' && type === '20') {
        showBinary = true;
      } else if (kind === 'self' && (sensorBar || sensorRows) && !(sensorBar && sensorRows)) {
        showBar = sensorBar;
        showRows = sensorRows;
      } else {
        showBar = true;
        showRows = true;
      }
    }
    if (showBar && showRows) {
      const hasBar = readIconColorBar(tab).mode !== 'off';
      const hasRows = readIconColorRows(tab).length > 0;
      if (hasBar || hasRows) {
        showBar = hasBar;
        showRows = hasRows;
      } else {
        const text = iconColorSensorIsText(tab, kind === 'self' ? iconColorOwnEntity(tab, type) : layer.entity);
        showBar = !text;
        showRows = text;
        if (typeof isSensorMetaCacheLoaded === 'function' && !isSensorMetaCacheLoaded() &&
            typeof fetchSensorMetaCache === 'function' && block.dataset.metaRequested !== '1') {
          block.dataset.metaRequested = '1';
          fetchSensorMetaCache().then(() => syncIconColorFields(tab)).catch(() => {});
        }
      }
    }
    iconColorEl(tab, '_tile_icon_bar_section')?.classList.toggle('hidden', !showBar);
    iconColorEl(tab, '_tile_icon_state_section')?.classList.toggle('hidden', !showRows);
    iconColorEl(tab, '_tile_icon_binary_section')?.classList.toggle('hidden', !showBinary);
    if (showBar) renderIconColorBar(tab);
  }

  function loadIconColorFields(tab, data) {
    const type = iconColorTypeOf(tab);
    const parsed = parseIconColorRecord(data?.icon_colors);
    setIconColorInput(tab, parsed.color);
    const fill = iconColorEl(tab, '_tile_icon_fill');
    if (fill) fill.checked = parsed.fill > 0;
    const media = type === ICON_COLOR_MEDIA_TYPE;
    const coverIcon = iconColorEl(tab, '_tile_icon_cover');
    if (coverIcon) coverIcon.checked = media && parsed.cover.icon;
    const coverFill = iconColorEl(tab, '_tile_cover_fill');
    if (coverFill) coverFill.checked = media && parsed.cover.tile > 0 && !parsed.fill;
    const fillStrength = iconColorEl(tab, '_tile_icon_fill_strength');
    if (fillStrength) fillStrength.value = String(parsed.fill || (media && parsed.cover.tile) || 20);
    const barInput = iconColorEl(tab, '_tile_icon_bar');
    if (barInput) barInput.dataset.last = '';
    writeIconColorBar(tab, parsed.bar ? parsed.bar.mode : 'off', parsed.bar ? parsed.bar.stops : []);
    const min = iconColorEl(tab, '_tile_icon_bar_min');
    const max = iconColorEl(tab, '_tile_icon_bar_max');
    if (min) min.value = parsed.bar ? parsed.bar.minText : '';
    if (max) max.value = parsed.bar ? parsed.bar.maxText : '';
    setIconColorSelectedStop(tab, -1);
    writeIconColorRows(tab, parsed.rows);
    writeIconColorSource(tab, parsed.source);
    // Records without a layer (b40) keep their own rules switched on.
    const own = ICON_COLOR_OWN_TYPES.includes(type);
    const on = parsed.source ? parsed.source.enabled : own && (!!parsed.bar || parsed.rows.length > 0);
    const onInput = iconColorEl(tab, '_tile_icon_rules_on');
    if (onInput) onInput.value = on ? '1' : '0';
    for (const state of ['on', 'off']) {
      const row = parsed.rows.find(entry => !entry.has && iconColorFold(entry.value) === state);
      setIconColorBinaryInput(tab, state, row ? row.color : '');
    }
    syncIconColorFields(tab);
  }

  function saveIconColorFields(tab, formData) {
    formData.append('icon_colors', collectIconColorRecord(tab));
  }

  function resetIconColorFields(tab) {
    loadIconColorFields(tab, {});
  }

  // Delegated listeners survive folder-tab HTML replacement without stale or
  // duplicate handlers. Every change takes the shared live-editor path:
  // preview, draft snapshot and autosave.
  function commitIconColorChange(tab) {
    syncIconColorFields(tab);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }

  function iconColorEventTab(element) {
    return element?.closest?.('.tile-icon-color-fields')?.dataset.tab || '';
  }

  function iconColorBarPosition(element, clientX) {
    const rect = element.getBoundingClientRect();
    if (!rect.width) return null;
    return Math.max(0, Math.min(1000, Math.round((clientX - rect.left) / rect.width * 1000)));
  }

  function addIconColorStop(tab, position) {
    const bar = readIconColorBar(tab);
    if (bar.mode === 'off' || bar.stops.length >= ICON_COLOR_MAX_STOPS || position === null) return false;
    const stops = iconColorSortStops(bar.stops);
    const stop = { position, color: iconColorBarColorAt({ mode: bar.mode, stops }, position / 1000) };
    stops.push(stop);
    iconColorSortStops(stops);
    writeIconColorBar(tab, bar.mode, stops);
    setIconColorSelectedStop(tab, stops.indexOf(stop));
    return true;
  }

  function openIconColorStopPicker(tab, index) {
    const stop = readIconColorBar(tab).stops[index];
    const picker = iconColorEl(tab, '_tile_icon_stop_color');
    if (!stop || !picker) return;
    picker.value = '#' + iconColorHex(stop.color);
    try {
      if (typeof picker.showPicker === 'function') picker.showPicker();
      else picker.click();
    } catch (_) {
      picker.click();
    }
  }

  document.addEventListener('input', event => {
    const target = event.target;
    const role = target?.dataset?.iconColor;
    if (!['color', 'binary', 'value', 'rule-color', 'min', 'max', 'stop-color', 'rule-strength', 'fill-strength'].includes(role)) return;
    const tab = iconColorEventTab(target);
    if (!tab) return;
    if (role === 'color' || role === 'binary') target.dataset.unset = '0';
    if (role === 'stop-color') {
      const bar = readIconColorBar(tab);
      const stop = bar.stops[iconColorSelectedStop(tab)];
      const rgb = iconColorParseHex(target.value);
      if (!stop || rgb === null) return;
      stop.color = rgb;
      writeIconColorBar(tab, bar.mode, bar.stops);
    }
    commitIconColorChange(tab);
  });

  document.addEventListener('change', event => {
    const target = event.target;
    const id = target?.id || '';
    if (['has', 'source', 'rule-target', 'fill-target'].includes(target?.dataset?.iconColor)) {
      const tab = iconColorEventTab(target);
      if (tab) commitIconColorChange(tab);
      return;
    }
    for (const suffix of ['_tile_type', '_sensor_entity', '_select_entity', '_datetime_entity']) {
      if (id.endsWith(suffix)) syncIconColorFields(id.slice(0, -suffix.length));
    }
  });

  document.addEventListener('click', event => {
    const button = event.target?.closest?.('button[data-icon-color]');
    const tab = iconColorEventTab(button);
    if (!button || !tab) return;
    const role = button.dataset.iconColor;
    if (role === 'stop') {
      // A finished drag is not a click on the stop.
      if (button.dataset.dragged === '1') {
        delete button.dataset.dragged;
        return;
      }
      const index = Number(button.dataset.stop);
      setIconColorSelectedStop(tab, index);
      renderIconColorBar(tab);
      openIconColorStopPicker(tab, index);
      return;
    }
    if (role === 'icon-color-mode') {
      const mode = button.dataset.mode;
      const cover = iconColorEl(tab, '_tile_icon_cover');
      if (cover) cover.checked = mode === 'cover';
      // Custom starts from the color the field shows; Automatic forgets it.
      const input = iconColorEl(tab, '_tile_icon_color');
      if (mode === 'auto') setIconColorInput(tab, '');
      else if (mode === 'own' && input) input.dataset.unset = '0';
    } else if (role === 'source-mode') {
      const mode = iconColorEl(tab, '_tile_icon_source_mode');
      if (mode) mode.value = button.dataset.mode === 'rules' ? 'rules' : 'auto';
    } else if (role === 'strength-reset') {
      const strength = iconColorEl(tab, '_tile_icon_rule_strength');
      if (strength) strength.value = '20';
    } else if (role === 'fill-strength-reset') {
      const strength = iconColorEl(tab, '_tile_icon_fill_strength');
      if (strength) strength.value = '20';
    } else if (role === 'rules-on') {
      const on = iconColorEl(tab, '_tile_icon_rules_on');
      if (on) on.value = button.dataset.mode === '1' ? '1' : '0';
    } else if (role === 'source-kind') {
      const kind = iconColorEl(tab, '_tile_icon_source_kind');
      if (kind) kind.value = button.dataset.mode === 'other' ? 'other' : 'self';
    } else if (role === 'binary-clear') {
      setIconColorBinaryInput(tab, button.dataset.state, '');
    } else if (role === 'remove') {
      const rows = readIconColorRows(tab);
      rows.splice(Number(button.dataset.rule), 1);
      writeIconColorRows(tab, rows);
    } else if (role === 'add') {
      const rows = readIconColorRows(tab);
      if (rows.length >= ICON_COLOR_MAX_ROWS) return;
      rows.push({ value: '', has: false, color: ICON_COLOR_ROW_DEFAULT });
      writeIconColorRows(tab, rows);
      document.getElementById(tab + '_tile_icon_rule_' + (rows.length - 1) + '_value')?.focus();
    } else if (role === 'mode') {
      const mode = button.dataset.mode;
      const input = iconColorEl(tab, '_tile_icon_bar');
      const bar = readIconColorBar(tab);
      if (mode === 'off') {
        if (bar.mode !== 'off' && input) input.dataset.last = input.value;
        writeIconColorBar(tab, 'off', []);
      } else if (bar.mode === 'off') {
        // Turning the bar on restores the last stops, else Cold -> Warm.
        const last = String(input?.dataset.last || '').split(' ').slice(1).map(iconColorParseStop).filter(Boolean);
        const stops = last.length >= 2 ? last
          : ICON_COLOR_PRESETS.cold_warm.map(([position, color]) => ({ position, color }));
        writeIconColorBar(tab, mode, stops);
        ensureIconColorRange(tab);
      } else {
        writeIconColorBar(tab, mode, bar.stops);
      }
      setIconColorSelectedStop(tab, -1);
    } else if (role === 'preset') {
      const preset = ICON_COLOR_PRESETS[button.dataset.preset];
      if (!preset) return;
      const bar = readIconColorBar(tab);
      writeIconColorBar(tab, bar.mode === 'off' ? 'smooth' : bar.mode,
        preset.map(([position, color]) => ({ position, color })));
      ensureIconColorRange(tab);
      setIconColorSelectedStop(tab, -1);
    } else if (role === 'stop-remove') {
      const bar = readIconColorBar(tab);
      const selected = iconColorSelectedStop(tab);
      if (bar.stops.length <= 2 || !bar.stops[selected]) return;
      bar.stops.splice(selected, 1);
      writeIconColorBar(tab, bar.mode, bar.stops);
      setIconColorSelectedStop(tab, -1);
    } else {
      return;
    }
    commitIconColorChange(tab);
  });

  // Double-click on the bar adds a stop with the color already shown there.
  document.addEventListener('dblclick', event => {
    const strip = event.target?.closest?.('[data-icon-color="bar"]');
    const tab = iconColorEventTab(strip);
    if (!strip || !tab) return;
    if (addIconColorStop(tab, iconColorBarPosition(strip, event.clientX))) commitIconColorChange(tab);
  });

  // Dragging a stop handle (mouse, pen or touch) moves it along the bar; the
  // preview follows live, the draft and autosave follow on release.
  let iconColorDrag = null;

  document.addEventListener('pointerdown', event => {
    const knob = event.target?.closest?.('[data-icon-color="stop"]');
    const tab = iconColorEventTab(knob);
    if (!knob || !tab || (event.button !== undefined && event.button !== 0)) return;
    delete knob.dataset.dragged;
    const index = Number(knob.dataset.stop);
    setIconColorSelectedStop(tab, index);
    renderIconColorBar(tab);
    const start = readIconColorBar(tab).stops[index];
    iconColorDrag = { tab, index, knob, pointerId: event.pointerId, startX: event.clientX,
      startPosition: start ? start.position : 0, moved: false };
    try { knob.setPointerCapture?.(event.pointerId); } catch (_) {}
    event.preventDefault();
  });

  document.addEventListener('pointermove', event => {
    const drag = iconColorDrag;
    if (!drag || event.pointerId !== drag.pointerId) return;
    if (!drag.moved && Math.abs(event.clientX - drag.startX) < 3) return;
    drag.moved = true;
    const handles = iconColorEl(drag.tab, '_tile_icon_bar_handles');
    const position = handles ? iconColorBarPosition(handles, event.clientX) : null;
    const bar = readIconColorBar(drag.tab);
    if (position === null || !bar.stops[drag.index]) return;
    bar.stops[drag.index].position = position;
    writeIconColorBar(drag.tab, bar.mode, bar.stops);
    renderIconColorBar(drag.tab);
    updateTilePreview(drag.tab);
  });

  function endIconColorDrag(event) {
    const drag = iconColorDrag;
    if (!drag || event.pointerId !== drag.pointerId) return;
    iconColorDrag = null;
    if (!drag.moved) return;
    drag.knob.dataset.dragged = '1';
    const bar = readIconColorBar(drag.tab);
    const moved = bar.stops[drag.index];
    if (!moved) return;
    // Re-sort on release; a stop dragged onto a neighbour's position lands on
    // the side it came from, so dragging past a neighbour puts it behind it.
    const stops = iconColorSortStops(bar.stops.filter(stop => stop !== moved));
    const right = moved.position >= drag.startPosition;
    let at = stops.findIndex(stop => right ? stop.position > moved.position : stop.position >= moved.position);
    if (at < 0) at = stops.length;
    stops.splice(at, 0, moved);
    writeIconColorBar(drag.tab, bar.mode, stops);
    setIconColorSelectedStop(drag.tab, at);
    commitIconColorChange(drag.tab);
  }

  document.addEventListener('pointerup', endIconColorDrag);
  document.addEventListener('pointercancel', endIconColorDrag);

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
    syncGlobalDisplayControls(tabEl);

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
        const buttonLabel = button.querySelector('.tab-label')?.textContent || '';
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
      // Folders go after the others in the folder row.
      (document.getElementById('folderTabs') || nav).appendChild(buttonEl);
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
      enableFreeSlotHover(String(data.tab_id));
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

  // Folder tabs not opened yet are prefetched one at a time while the Web
  // Admin is idle, so a later click opens them at once. The device answers
  // from its UI loop, so the prefetch waits for a quiet editor, leaves a short
  // gap between requests and stops at the first failure.
  const FOLDER_TAB_PREFETCH_IDLE_MS = 2500;
  const FOLDER_TAB_PREFETCH_STEP_MS = 150;
  let folderTabPrefetchTimer = null;
  let folderTabPrefetchStopped = false;
  let lastAdminInteractionMs = Date.now();

  function noteAdminInteraction() {
    lastAdminInteractionMs = Date.now();
  }

  function scheduleFolderTabPrefetch(delayMs = FOLDER_TAB_PREFETCH_IDLE_MS) {
    if (folderTabPrefetchStopped || folderTabPrefetchTimer) return;
    folderTabPrefetchTimer = window.setTimeout(runFolderTabPrefetch, delayMs);
  }

  function nextFolderTabToPrefetch() {
    for (const key of Object.keys(tabByFolder)) {
      const folderId = Number(key);
      if (!Number.isInteger(folderId) || folderId <= 0) continue;
      const tab = tabByFolder[folderId];
      if (!document.getElementById('tab-tiles-' + tab)) {
        return { folderId, tab, step: 'tab' };
      }
      if (!tileDataLoadedTabs.has(tab)) return { folderId, tab, step: 'tiles' };
    }
    return null;
  }

  async function runFolderTabPrefetch() {
    folderTabPrefetchTimer = null;
    if (folderTabPrefetchStopped) return;
    const idleMs = Date.now() - lastAdminInteractionMs;
    if (document.hidden || dragSource || resizeState || fileManagerUploadBusy ||
        idleMs < FOLDER_TAB_PREFETCH_IDLE_MS) {
      scheduleFolderTabPrefetch(Math.max(
        FOLDER_TAB_PREFETCH_STEP_MS, FOLDER_TAB_PREFETCH_IDLE_MS - idleMs));
      return;
    }
    const next = nextFolderTabToPrefetch();
    if (!next) return;
    let ok = false;
    try {
      ok = next.step === 'tab'
        ? await ensureFolderTabUi(next.folderId)
        : Array.isArray(await fetchTileGridData(next.tab, false));
    } catch (error) {
      ok = false;
    }
    if (!ok) {
      folderTabPrefetchStopped = true;
      return;
    }
    scheduleFolderTabPrefetch(FOLDER_TAB_PREFETCH_STEP_MS);
  }

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
  // The shown grid (grid_layout.h): with a bar layout the panel shows fewer,
  // larger cells. New places, moves and sizes stay inside it. A tile without
  // a place in the active layout (layout_hidden, tile_layouts.h) is not shown
  // and takes no room; the layout window places it. The screensaver tab
  // keeps its own grid.
  function headBarLayout() { return typeof HEAD_BAR === 'boolean' && HEAD_BAR; }
  function placeCols(tab = currentTileTab) {
    return headBarLayout() && !isScreensaverTileTab(tab) && typeof GRID_SHOWN_COLS === 'number'
      ? GRID_SHOWN_COLS : GRID_COLS;
  }
  function placeRows(tab = currentTileTab) {
    return headBarLayout() && !isScreensaverTileTab(tab) && typeof GRID_SHOWN_ROWS === 'number'
      ? GRID_SHOWN_ROWS : GRID_ROWS;
  }
  // A tile that is shown and takes room in the editor's grid.
  function tileTakesRoom(tile) {
    return !!tile && Number(tile.type || 0) !== 0 && !tile.layout_hidden;
  }

  function clampInt(value, min, max, fallback) {
    const v = parseInt(value, 10);
    if (isNaN(v)) return fallback !== undefined ? fallback : min;
    if (v < min) return min;
    if (v > max) return max;
    return v;
  }

  function clampHalf(value, min, max, fallback) {
    const number = Number(value);
    return Number.isFinite(number) ? Math.max(min, Math.min(max, Math.round(number * 2) / 2)) : fallback;
  }
  function isCompactSensorType(type) { return [1, 14, 20].includes(Number(type)); }
  // Number, Select and Date/Time render through the Sensor tile
  // (tile_geometry::editable).
  function isEditableValueType(type) { return [21, 22, 23].includes(Number(type)); }
  // Types that may use half-cell sizes (mirrors tile_geometry::half_size).
  // Scene, Folder, Settings, Back and Camera show only an icon and a title;
  // Switch and Cover show their state (tile_geometry::compact_switch,
  // compact_cover).
  function supportsHalfSize(type) {
    return isCompactSensorType(type) || isEditableValueType(type) || [2, 4, 5, 7, 8, 9, 17, 18, 19, 24, 25, 26].includes(Number(type));
  }
  // Every type resizes in half steps from 1x1; only half-size types may be half
  // a row high (mirrors tile_geometry::supported).
  function supportedTileLayout(type, layout) {
    const values = layout ? [layout.col, layout.row, layout.span_w, layout.span_h] : [];
    if (!layout || !values.every(v => Number.isFinite(v) && v >= 0 && Number.isInteger(v * 2))) return false;
    if (layout.span_w < 1) return false;
    return layout.span_h >= 1 || (supportsHalfSize(type) && layout.span_h === 0.5);
  }
  // Half-height value size for a value size choice, like
  // compact_sensor_layout::value_step: the title size by default and for 20,
  // 24, or 28 for 28 and the larger choices (32, 40), which do not fit.
  function compactValueSize(choice) {
    const value = String(choice ?? '0');
    if (value === '2') return 24;
    return ['3', '4', '5'].includes(value) ? 28 : 20;
  }
  // The value size choices a tile shows: Default, 24 and 28 in half-height
  // tiles; Default, 20, 24, 32 and 40 otherwise (28 is the default there).
  // A choice the other size lacks moves to the one that looks the same.
  function syncCompactValueFontOptions(select, halfHeight) {
    if (!select?.options) return;
    const shown = halfHeight ? ['0', '2', '5'] : ['0', '1', '2', '3', '4'];
    for (const option of Array.from(select.options)) {
      const hidden = !shown.includes(option.value);
      option.hidden = hidden;
      option.disabled = hidden;
      if (option.value === '0') {
        option.textContent = option.textContent.replace(/^\d+(?= )/, halfHeight ? '20' : '28');
      }
    }
    const value = select.value;
    if (halfHeight && value === '1') select.value = '0';
    else if (halfHeight && (value === '3' || value === '4')) select.value = '5';
    else if (!halfHeight && value === '5') select.value = '0';
  }
  // Number, Select and Date/Time value sizes (1 = 20, 2 = 24, 0 = 28, 3 = 32,
  // 4 = 40): half-height tiles offer 20, 24 and 28 (editable_display_tile).
  function syncEditableValueFontOptions(select, halfHeight) {
    if (!select?.options) return;
    for (const option of Array.from(select.options)) {
      const hidden = halfHeight && (option.value === '3' || option.value === '4');
      option.hidden = hidden;
      option.disabled = hidden;
    }
    if (halfHeight && (select.value === '3' || select.value === '4')) select.value = '0';
  }
  // The Sensor value size choice that matches a Number, Select or Date/Time
  // size at half height (editable_display_tile).
  function editableCompactValueFont(choice) {
    const value = String(choice ?? '2');
    return value === '1' ? '0' : value === '2' ? '2' : '5';
  }
  function applyCompactSensorPreview(el, type, layout, mode = 0, valueFont = 0) {
    const halfHeight = layout?.span_w >= 1 && layout.span_h === 0.5;
    // A half-height icon-and-title tile (Scene, Folder, Settings, Back, Camera) uses the
    // half-height Sensor header: the icon in the corner disc and the title
    // (if any) centered beside it.
    const compactIconTitle = [2, 4, 7, 8, 18].includes(Number(type)) && halfHeight;
    // Half-height Switch: icon, title and state like a compact Sensor.
    const compactSwitch = [5, 17, 19, 24, 25, 26].includes(Number(type)) && halfHeight;
    // Number, Select and Date/Time like a compact Sensor.
    const compactEditable = isEditableValueType(type) && halfHeight;
    if (compactEditable) valueFont = editableCompactValueFont(valueFont);
    const compact = (isCompactSensorType(type) || compactIconTitle || compactSwitch || compactEditable) && halfHeight;
    el.classList.toggle('sensor-compact', compact);
    el.classList.toggle('sensor-half', compact);
    el.classList.toggle('compact-title-only', compactIconTitle);
    // Switch header layouts show their state beside the disc at the
    // half-height value sizes; from 1.5 rows like a Sensor tile at its value
    // sizes, the bar a third of the extra height higher
    // (switch_layout::sensor_look).
    // The Cover position bar uses the same header (tile_header.h).
    // Lock, Alarm panel and Fan too (types/device).
    const switchHeader = [5, 19, 24, 25, 26].includes(Number(type)) && el.classList.contains('switch-bar');
    const switchTall = switchHeader && Number(layout?.span_h) > 1;
    el.classList.toggle('switch-tall', switchTall);
    if (switchTall) el.style?.setProperty?.('--switch-span-h', String(Number(layout.span_h)));
    else el.style?.removeProperty?.('--switch-span-h');
    const tallSize = switchTall ? ({1: 20, 2: 24, 3: 32, 4: 40}[Number(valueFont)] || 28) : 0;
    for (const size of [20, 24, 32, 40]) el.classList.toggle('switch-value-' + size, tallSize === size);
    const valueSize = (compact && !compactIconTitle) || (switchHeader && !switchTall) ? compactValueSize(valueFont) : 20;
    el.classList.toggle('compact-value-24', valueSize === 24);
    el.classList.toggle('compact-value-28', valueSize === 28);
    el.classList.toggle('clock-compact', Number(type) === 9 && halfHeight);
    if (Number(type) === 9) fitCompactClockPreview(el);
  }

  function normalizeLayoutForTileType(typeValue, col, row, spanW, spanH) {
    let safeCol = clampHalf(col, 0, GRID_COLS - 0.5, 0);
    let safeRow = clampHalf(row, 0, GRID_ROWS - 0.5, 0);
    let safeW = clampHalf(spanW, 0.5, GRID_COLS, 1);
    let safeH = clampHalf(spanH, 0.5, GRID_ROWS, 1);
    if (Number(typeValue) === MEDIA_TILE_TYPE) {
      const minW = Math.min(MEDIA_TILE_MIN_SPAN, GRID_COLS);
      const minH = Math.min(MEDIA_TILE_MIN_SPAN, GRID_ROWS);
      safeW = clampHalf(safeW, minW, Math.min(MEDIA_TILE_MAX_SPAN, GRID_COLS), minW);
      safeH = clampHalf(safeH, minH, Math.min(MEDIA_TILE_MAX_SPAN, GRID_ROWS), minH);
      safeCol = Math.min(safeCol, GRID_COLS - safeW);
      safeRow = Math.min(safeRow, GRID_ROWS - safeH);
    } else {
      // Keep at least a whole cell wide (and a whole row high unless the type
      // allows half a row), so clamping at the grid edge never yields 0.5.
      const type = Number(typeValue);
      const minH = (type === 0 || supportsHalfSize(type)) ? 0.5 : 1;
      safeW = Math.max(1, safeW);
      safeH = Math.max(minH, safeH);
      safeCol = Math.min(safeCol, GRID_COLS - 1);
      safeRow = Math.min(safeRow, GRID_ROWS - minH);
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
    const col = clampHalf(tile?.col, 0, GRID_COLS - 0.5, fallbackCol);
    const row = clampHalf(tile?.row, firstRow, GRID_ROWS - 0.5, fallbackRow);
    let spanW = clampHalf(tile?.span_w, 0.5, GRID_COLS, 1);
    let spanH = clampHalf(tile?.span_h, 0.5, GRID_ROWS, 1);
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
    const fractional = [col, row, spanW, spanH].some(v => !Number.isInteger(v));
    el.classList.toggle('fractional-tile', fractional);
    for (const [name, value] of Object.entries({col, row, w: spanW, h: spanH})) el.style.setProperty('--tile-' + name, String(value));
    if (fractional) { el.style.gridColumn = 'auto'; el.style.gridRow = 'auto'; }

  }

  function getTileElementLayout(tab, index) {
    const el = document.getElementById(tab + '-tile-' + index);
    if (!el) return null;
    const col = clampHalf(el.dataset.col, 0, GRID_COLS - 0.5, null);
    const row = clampHalf(el.dataset.row, firstAllowedGridRow(tab), GRID_ROWS - 0.5, null);
    const spanW = clampHalf(el.dataset.spanW, 0.5, GRID_COLS, null);
    const spanH = clampHalf(el.dataset.spanH, 0.5, GRID_ROWS, null);
    if (col === null || row === null || spanW === null || spanH === null) return null;
    return { col, row, span_w: spanW, span_h: spanH };
  }

  function layoutTiles(tab, tiles) {
    if (!Array.isArray(tiles)) return;
    const occupied = Array.from({ length: GRID_ROWS * 2 }, () => Array(GRID_COLS * 2).fill(false));
    const emptyIndices = [];

    tiles.forEach((tile, idx) => {
      const typeNum = Number(tile?.type);
      if (!tile || isNaN(typeNum) || typeNum === 0) {
        emptyIndices.push(idx);
        return;
      }
      const el = document.getElementById(tab + '-tile-' + idx);
      if (tile.layout_hidden) {
        el?.classList.add('tile-unplaced');
        if (el) el.style.display = 'none';
        return;
      }
      el?.classList.remove('tile-unplaced');
      const layout = normalizeTileLayout(tile, idx, tab);
      if (el) {
        setTileGridPosition(el, layout.col, layout.row, layout.span_w, layout.span_h);
        el.style.display = '';
      }
      markOccupied(occupied, layout);
    });

    // A selected new tile keeps the spot the user picked (newTileSpot), even
    // when a grid re-render recreates its element. One further empty tile is
    // the free slot that follows the pointer (enableFreeSlotHover); it rests
    // on the first free spot so keyboard and touch users reach it.
    const editingNew = idx => currentTileTab === tab && currentTileIndex === idx;
    const empties = emptyIndices
      .map(idx => ({ idx, el: document.getElementById(tab + '-tile-' + idx) }))
      .filter(entry => entry.el)
      .sort((a, b) => editingNew(b.idx) - editingNew(a.idx));
    let freeEl = null;
    empties.forEach(({ idx, el }) => {
      delete el.dataset.freeSlot;
      el.classList.remove('free-slot-hover');
      const kept = editingNew(idx) && newTileSpot?.tab === tab && newTileSpot.index === idx
        ? newTileSpot.layout : null;
      if (kept && slotFits(tab, occupied, kept.col, kept.row, kept.span_w, kept.span_h)) {
        markOccupied(occupied, kept);
        setTileGridPosition(el, kept.col, kept.row, kept.span_w, kept.span_h);
        el.style.display = '';
        return;
      }
      const slot = freeEl ? null : firstFreeSlot(tab, occupied);
      if (slot) {
        freeEl = el;
        el.dataset.freeSlot = '1';
        setTileGridPosition(el, slot.col, slot.row, slot.span_w, slot.span_h);
        el.style.display = '';
      } else {
        el.style.display = 'none';
      }
    });
    // The layout window marks what does not fit its layout.
    if (typeof layoutWindowOpen === 'function' && layoutWindowOpen()) markLayoutRed(tab);
  }

  function markOccupied(occupied, layout) {
    for (let r = layout.row * 2; r < (layout.row + layout.span_h) * 2; r++) {
      for (let c = layout.col * 2; c < (layout.col + layout.span_w) * 2; c++) {
        if (r >= 0 && c >= 0 && r < GRID_ROWS * 2 && c < GRID_COLS * 2) occupied[r][c] = true;
      }
    }
  }

  function slotFits(tab, occupied, col, row, spanW, spanH) {
    if (col < 0 || row < firstAllowedGridRow(tab) ||
        col + spanW > placeCols(tab) || row + spanH > placeRows(tab)) return false;
    for (let r = row * 2; r < (row + spanH) * 2; r++) {
      for (let c = col * 2; c < (col + spanW) * 2; c++) {
        if (occupied[r][c]) return false;
      }
    }
    return true;
  }

  // New tiles start as 1x1. The 1x0.5 slot is offered only where 1x1 does not
  // fit; choosing a type that needs more grows it (grownNewTileLayout).
  const FREE_SLOT_SIZES = [[1, 1], [1, 0.5]];

  // Spot of the new (still empty) tile open in the editor: {tab, index, layout}.
  let newTileSpot = null;

  // Smallest size a type accepts (Media needs 2x2, half-size types 1x0.5).
  function minimumTileSize(type) {
    if (Number(type) === MEDIA_TILE_TYPE) return [Math.min(MEDIA_TILE_MIN_SPAN, GRID_COLS), Math.min(MEDIA_TILE_MIN_SPAN, GRID_ROWS)];
    return [1, supportsHalfSize(type) ? 0.5 : 1];
  }

  // Layout a new half-height tile grows to for a type that needs more room:
  // downwards first, then upwards, then left. Null when it does not fit.
  function grownNewTileLayout(tab, type) {
    if (!newTileSpot || newTileSpot.tab !== tab || newTileSpot.index !== currentTileIndex ||
        !newTileSpot.layout) return null;
    const base = newTileSpot.layout;
    const [minW, minH] = minimumTileSize(type);
    if (base.span_w >= minW && base.span_h >= minH) return base;
    const spanW = Math.max(base.span_w, minW), spanH = Math.max(base.span_h, minH);
    for (const [dx, dy] of [[0, 0], [0, base.span_h - spanH], [base.span_w - spanW, 0], [base.span_w - spanW, base.span_h - spanH]]) {
      const layout = { col: base.col + dx, row: base.row + dy, span_w: spanW, span_h: spanH };
      if (supportedTileLayout(type, layout) && canPlaceTileLayout(tab, currentTileIndex, layout)) return layout;
    }
    return null;
  }

  // Pointer position in (fractional) grid cells.
  function pointerGridPoint(tab, clientX, clientY) {
    const metrics = getTileGridMetrics(tab);
    if (!metrics) return null;
    const x = (clientX - metrics.rect.left - metrics.padLeft + metrics.gapX / 2) / (metrics.cellW + metrics.gapX);
    const y = (clientY - metrics.rect.top - metrics.padTop + metrics.gapY / 2) / (metrics.cellH + metrics.gapY);
    return Number.isFinite(x) && Number.isFinite(y) ? { x, y } : null;
  }

  // The free slot centred under the pointer, snapped to half cells. Nearby
  // half steps that still cover the pointer are tried before a smaller size.
  function freeSlotNear(tab, occupied, point) {
    const snap = value => Math.round(value * 2) / 2;
    const offsets = [0, -0.5, 0.5];
    for (const [spanW, spanH] of FREE_SLOT_SIZES) {
      const baseCol = snap(point.x - spanW / 2);
      const baseRow = snap(point.y - spanH / 2);
      const candidates = [];
      for (const dy of offsets) {
        for (const dx of offsets) {
          const col = baseCol + dx, row = baseRow + dy;
          if (col <= point.x && point.x < col + spanW && row <= point.y && point.y < row + spanH) {
            candidates.push({ col, row, cost: Math.abs(dx) + Math.abs(dy) });
          }
        }
      }
      candidates.sort((a, b) => a.cost - b.cost);
      for (const { col, row } of candidates) {
        if (slotFits(tab, occupied, col, row, spanW, spanH)) {
          return { col, row, span_w: spanW, span_h: spanH };
        }
      }
    }
    return null;
  }

  function firstFreeSlot(tab, occupied) {
    for (const [spanW, spanH, step] of [[1, 1, 1], [1, 1, 0.5], [1, 0.5, 1], [1, 0.5, 0.5]]) {
      for (let r = firstAllowedGridRow(tab); r + spanH <= placeRows(tab); r += step) {
        for (let c = 0; c + spanW <= placeCols(tab); c += step) {
          if (slotFits(tab, occupied, c, r, spanW, spanH)) {
            return { col: c, row: r, span_w: spanW, span_h: spanH };
          }
        }
      }
    }
    return null;
  }

  // Occupancy as currently shown, including unsaved local edits and a selected
  // new tile, but without the free slot itself.
  function occupiedFromGrid(tab, grid, freeEl) {
    const occupied = Array.from({ length: GRID_ROWS * 2 }, () => Array(GRID_COLS * 2).fill(false));
    // A selected new tile still of type Empty does not block the free slot:
    // the pointer may pick a spot half a cell next to or over it, and a click
    // moves the new tile there. Once a type is chosen it blocks like a tile.
    const selectedIsEmpty =
      String(document.getElementById(tab + '_tile_type')?.value ?? '0') === '0';
    grid.querySelectorAll(':scope > .tile[data-index]').forEach(el => {
      if (el === freeEl || el.style.display === 'none') return;
      if (Number(el.dataset.type || 0) === 0 &&
          (el.dataset.selected !== '1' || selectedIsEmpty)) return;
      const layout = getTileElementLayout(tab, parseInt(el.dataset.index, 10));
      if (layout) markOccupied(occupied, layout);
    });
    return occupied;
  }

  function freeSlotElement(grid) {
    const free = grid.querySelector(':scope > .tile.empty[data-free-slot="1"]:not([data-selected="1"])');
    if (free) return free;
    const spare = Array.from(grid.querySelectorAll(':scope > .tile.empty'))
      .find(el => el.dataset.selected !== '1' && el.style.display === 'none');
    if (spare) spare.dataset.freeSlot = '1';
    return spare || null;
  }

  // Moves the free slot to the pointer in half-cell steps. A click on it (or a
  // tap on free space) opens the editor for a new tile at exactly that spot.
  function enableFreeSlotHover(tab) {
    const grid = getTileGrid(tab);
    if (!grid || grid.dataset.freeSlotBound === '1') return;
    grid.dataset.freeSlotBound = '1';
    const placeAt = (clientX, clientY) => {
      const el = freeSlotElement(grid);
      if (!el) return null;
      const point = pointerGridPoint(tab, clientX, clientY);
      const slot = point && freeSlotNear(tab, occupiedFromGrid(tab, grid, el), point);
      if (!slot) {
        el.classList.remove('free-slot-hover');
        return null;
      }
      // Only one unselected placeholder may exist, so a stale one left by a
      // previous selection can never catch the click.
      grid.querySelectorAll(':scope > .tile.empty').forEach(other => {
        if (other === el || other.dataset.selected === '1') return;
        other.style.display = 'none';
        delete other.dataset.freeSlot;
        other.classList.remove('free-slot-hover');
      });
      setTileGridPosition(el, slot.col, slot.row, slot.span_w, slot.span_h);
      el.style.display = '';
      el.classList.add('free-slot-hover');
      return el;
    };
    grid.addEventListener('pointermove', event => {
      if (event.pointerType === 'touch' || resizeState || dragSource) return;
      const over = event.target.closest('.tile');
      if (over && over.parentElement === grid && !over.classList.contains('empty')) {
        grid.querySelector(':scope > .tile.free-slot-hover')?.classList.remove('free-slot-hover');
        return;
      }
      placeAt(event.clientX, event.clientY);
    });
    grid.addEventListener('pointerleave', () => {
      grid.querySelector(':scope > .tile.free-slot-hover')?.classList.remove('free-slot-hover');
    });
    grid.addEventListener('click', event => {
      if (event.target !== grid) return;
      const el = placeAt(event.clientX, event.clientY);
      if (el) selectTile(parseInt(el.dataset.index, 10), tab);
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

    let col = clampHalf(colEl.value, 1, GRID_COLS, 1);
    const firstRow = firstAllowedGridRow(tab);
    let row = clampHalf(rowEl.value, firstRow + 1, GRID_ROWS + 0.5, firstRow + 1);
    let spanW = clampHalf(spanWEl.value, 0.5, GRID_COLS, 1);
    let spanH = clampHalf(spanHEl.value, 0.5, GRID_ROWS, 1);

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
    if (newTileSpot && newTileSpot.tab === tab && newTileSpot.index === currentTileIndex) {
      newTileSpot.layout = layout;
    }
    const tiles = getTilesData(tab);
    const tileEl = document.getElementById(tab + '-tile-' + currentTileIndex);
    if (tileEl && (!Array.isArray(tiles) || tiles.length === 0)) {
      setTileGridPosition(tileEl, layout.col, layout.row, layout.span_w, layout.span_h);
      return;
    }
    if (!Array.isArray(tiles) || currentTileIndex >= tiles.length) return;
    const tile = tiles[currentTileIndex] || {};
    const type = document.getElementById(tab + '_tile_type')?.value ?? tile.type;
    if (Number(type) !== 0 && (!supportedTileLayout(type, layout) || !canPlaceTileLayout(tab, currentTileIndex, layout))) {
      // The stored place stays; a preview redraw may have dropped the tile's
      // placement classes (a tile outside the shown grid then fell into the
      // first free grid cell), so they are set again.
      const stored = normalizeTileLayout(tile, currentTileIndex, tab);
      applyLayoutInputsFromLayout(tab, stored, false);
      if (tileEl) setTileGridPosition(tileEl, stored.col, stored.row, stored.span_w, stored.span_h);
      return;
    }
    tile.col = layout.col;
    tile.row = layout.row;
    tile.span_w = layout.span_w;
    tile.span_h = layout.span_h;
    const typeEl = document.getElementById(tab + '_tile_type');
    const typeNum = typeEl ? parseInt(typeEl.value, 10) : 0;
    tile.type = isNaN(typeNum) ? 0 : typeNum;
    tiles[currentTileIndex] = tile;
    layoutTiles(tab, tiles);
    syncTileSizePolicy(tab);
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
    const colEl = document.getElementById(prefix + '_tile_col');
    if (colEl) colEl.value = d.col || '1';
    const rowEl = document.getElementById(prefix + '_tile_row');
    if (rowEl) rowEl.value = d.row || '1';
    const spanWEl = document.getElementById(prefix + '_tile_span_w');
    if (spanWEl) spanWEl.value = d.span_w || '1';
    const spanHEl = document.getElementById(prefix + '_tile_span_h');
    if (spanHEl) spanHEl.value = d.span_h || '1';
    syncTileSizePolicy(tab);
    const meta = getTileTypeMeta(d.type || '0');
    callTypeHandler(meta, 'load', prefix, d);
    loadIconDiscFields(prefix, d);
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
    const spanWEl = document.getElementById(prefix + '_tile_span_w');
    if (spanWEl) spanWEl.value = data.span_w || '1';
    const spanHEl = document.getElementById(prefix + '_tile_span_h');
    if (spanHEl) spanHEl.value = data.span_h || '1';
    syncTileSizePolicy(tab);
    const meta = getTileTypeMeta(typeValue);
    callTypeHandler(meta, 'load', prefix, data);
    loadIconDiscFields(prefix, data);
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
    // Paste fills an empty tile only, and only where the copied size fits
    // without covering other tiles. Pasting over a tile or into too small a
    // gap used to replace the Back, Settings or a folder tile, or let the
    // overlap fix move the pasted tile to column 1 / row 1.
    if (Number(getCurrentTileType(tab) || 0) !== 0) {
      showNotification(t('pasteEmptyOnly'), false);
      return;
    }
    const target = getTileElementLayout(tab, currentTileIndex) ||
      getTileLayoutFromData(tab, currentTileIndex);
    const candidate = target && {
      col: target.col,
      row: target.row,
      span_w: Number(tileClipboard.span_w) || 1,
      span_h: Number(tileClipboard.span_h) || 1
    };
    if (!candidate ||
        !supportedTileLayout(tileClipboard.type, candidate) ||
        !canPlaceTileLayout(tab, currentTileIndex, candidate)) {
      showNotification(t('pasteNoSpace'), false);
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
    // A new tile keeps the spot it was picked at (see layoutTiles).
    newTileSpot = Number(getTilesData(tab)?.[index]?.type || 0) === 0
      ? { tab, index, layout: getTileElementLayout(tab, index) }
      : null;
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
    applyFolderTypeLock('folder0', false);
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
    if (!String(selectEl.value || '').trim()) return;
    let title = entityPickerName(selectEl);
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
      const weatherColoredIconsCheck = document.getElementById(prefix + '_weather_colored_icons');
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
    bindLive(document.getElementById(prefix + '_tile_icon_disc'), 'change', 'tileIconDisc', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(document.getElementById(prefix + '_tile_icon_glow'), 'change', 'tileIconGlow', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(colorInput, 'input', 'tileColor', () => { markTileColorInputExplicit(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(colInput, 'input', 'tileCol', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(rowInput, 'input', 'tileRow', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(spanWInput, 'input', 'tileSpanW', () => { syncClimateSlotFields(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(spanHInput, 'input', 'tileSpanH', () => { syncClimateSlotFields(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(typeSelect, 'change', 'tileType', () => {
      const tileEl = document.getElementById(tab + '-tile-' + currentTileIndex);
      const previousType = Number(tileEl?.dataset.type ?? 0);
      const nextType = Number(typeSelect.value);
      let currentLayout = getTileElementLayout(tab, currentTileIndex);
      // A new tile grows from 1x0.5 to the smallest size the chosen type needs.
      const grown = previousType === 0 && nextType !== 0 ? grownNewTileLayout(tab, nextType) : null;
      if (grown && currentLayout && (grown.span_w !== currentLayout.span_w || grown.span_h !== currentLayout.span_h)) {
        applyLayoutInputsFromLayout(tab, grown, false);
        newTileSpot.layout = grown;
        if (tileEl) setTileGridPosition(tileEl, grown.col, grown.row, grown.span_w, grown.span_h);
        currentLayout = grown;
      }
      if (nextType !== 0 && currentLayout && !supportedTileLayout(nextType, currentLayout)) {
        typeSelect.value = String(previousType);
        return;
      }
      // A freshly created tile must start with the selected type's real
      // default colour. Do not inherit an explicit colour state from the empty
      // editor placeholder.
      if (previousType === 0 && nextType !== 0) {
        const nextMeta = getTileTypeMeta(typeSelect.value);
        setTileColorInputFromStored(
          tab, 0, nextMeta.defaultBg || '#2A2A2A');
      }
      updateTileType(tab);
      // New tiles start in the HomeTiles look: a type with icon colors tints
      // the tile with the color its icon shows at 20 % (Tile color "From
      // icon"). Existing tiles and the screensaver keep their own style.
      if (previousType === 0 && nextType !== 0 && !isScreensaverTileTab(tab) &&
          typeof tileTypeHasIconColors === 'function' &&
          tileTypeHasIconColors(String(nextType))) {
        const strength = document.getElementById(tab + '_tile_icon_fill_strength');
        if (strength) strength.value = '20';
        const fill = document.getElementById(tab + '_tile_icon_fill');
        if (fill) fill.checked = true;
        syncTileColorMode(tab);
        if (typeof syncIconColorFields === 'function') syncIconColorFields(tab);
      }
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
      maybeFillTitleFromEntity(tab, '_binary_sensor_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(document.getElementById(prefix + '_binary_sensor_value_font'), 'change', 'binarySensorValueFont', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(document.getElementById(prefix + '_switch_value_font'), 'change', 'switchValueFont', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(binarySensorPopupModeSelect, 'change', 'binarySensorPopupMode', () => {
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(weatherSelect, 'change', 'weatherEntity', () => { maybeFillTitleFromWeather(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(weatherPopupModeSelect, 'change', 'weatherPopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(weatherColoredIconsCheck, 'change', 'weatherColoredIcons', () => { updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(energySelect, 'change', 'energyEntity', () => {
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
    bindLive(sceneInput, 'change', 'sceneAlias', () => { maybeFillTitleFromScene(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
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
      maybeFillTitleFromEntity(tab, '_climate_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(climatePopupModeSelect, 'change', 'climatePopupMode', () => { updateDraft(tab); scheduleAutoSave(tab); });
    // Layout "with value" drops the current temperature from the automatic
    // mini fields (climateAutomaticEditorKinds).
    bindLive(document.getElementById(prefix + '_climate_view'), 'change', 'climateView', () => {
      syncClimateSlotFields(tab); updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab);
    });
    bindLive(document.getElementById(prefix + '_cover_value_font'), 'change', 'coverValueFont', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    bindLive(coverSelect, 'change', 'coverEntity', () => {
      maybeFillTitleFromEntity(tab, '_cover_entity');
      updateTilePreview(tab);
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    bindLive(coverPopupModeSelect, 'change', 'coverPopupMode', () => {
      updateDraft(tab);
      scheduleAutoSave(tab);
    });
    // Lock, Alarm panel and Fan: entity, state size and popup gesture.
    for (const kind of ['lock', 'alarm', 'fan']) {
      const select = document.getElementById(prefix + '_' + kind + '_entity');
      bindLive(document.getElementById(prefix + '_' + kind + '_value_font'), 'change', kind + 'ValueFont', () => {
        updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab);
      });
      bindLive(select, 'change', kind + 'Entity', () => {
        maybeFillTitleFromEntity(tab, '_' + kind + '_entity');
        updateTilePreview(tab);
        updateDraft(tab);
        scheduleAutoSave(tab);
      });
      bindLive(document.getElementById(prefix + '_' + kind + '_popup_open_mode'), 'change', kind + 'PopupMode', () => {
        updateDraft(tab);
        scheduleAutoSave(tab);
      });
    }
    bindLive(cameraSelect, 'change', 'cameraEntity', () => {
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
    for (const kind of ['clock','text','back']) {
      bindLive(document.getElementById(prefix + '_' + kind + '_tile_border'), 'change', kind + 'TileBorder', () => { updateTilePreview(tab); updateDraft(tab); scheduleAutoSave(tab); });
    }
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

  // Tile Settings keep a clicked control where it is on screen. A choice that
  // hides fields below it (Tile color, Rules, Own/Other entity, bar mode, ...)
  // shortens the scrolling settings body; scrolled near its end, the browser
  // then clamps the scroll position and everything above, the clicked control
  // included, slides down. After the handlers ran, the body scrolls the
  // control back; a spacer at the end of the body makes room when the content
  // got too short and shrinks away again while the user scrolls up.
  const SETTINGS_SCROLL_SPACER = 'tile-settings-scroll-spacer';

  function settingsScrollSpacer(body) {
    let spacer = body.querySelector(':scope > .' + SETTINGS_SCROLL_SPACER);
    if (!spacer) {
      spacer = document.createElement('div');
      spacer.className = SETTINGS_SCROLL_SPACER;
      spacer.setAttribute('aria-hidden', 'true');
      body.appendChild(spacer);
    }
    return spacer;
  }

  function keepSettingsControlInPlace(body, control, top) {
    if (!body.isConnected || !control.isConnected || !control.getClientRects().length) return;
    const shift = control.getBoundingClientRect().top - top;
    if (Math.abs(shift) < 1) return;
    const target = body.scrollTop + shift;
    const room = body.scrollHeight - body.clientHeight;
    if (target > room) {
      const spacer = settingsScrollSpacer(body);
      spacer.style.height = ((parseFloat(spacer.style.height) || 0) + target - room) + 'px';
    }
    body.scrollTop = target;
  }

  // Only the part of the spacer below the visible area goes, so the view
  // never moves while it shrinks.
  function trimSettingsScrollSpacer(body) {
    const spacer = body.querySelector(':scope > .' + SETTINGS_SCROLL_SPACER);
    const height = spacer ? parseFloat(spacer.style.height) || 0 : 0;
    if (!height) return;
    const below = body.scrollHeight - body.scrollTop - body.clientHeight;
    const next = Math.max(0, height - Math.max(0, below));
    if (next !== height) spacer.style.height = next ? next + 'px' : '';
  }

  // One gesture fires several events (a label click, the click it forwards to
  // its checkbox, the change); the first one records the position.
  let pendingSettingsControl = null;

  function rememberSettingsControl(event) {
    const body = event.target?.closest?.('.tile-settings-body');
    if (!body || pendingSettingsControl) return;
    const control = event.target.closest('button, label, input, select, textarea') || event.target;
    pendingSettingsControl = {body, control, top: control.getBoundingClientRect().top};
    requestAnimationFrame(() => {
      const pending = pendingSettingsControl;
      pendingSettingsControl = null;
      if (pending) keepSettingsControlInPlace(pending.body, pending.control, pending.top);
    });
  }

  document.addEventListener('click', rememberSettingsControl, true);
  document.addEventListener('change', rememberSettingsControl, true);
  document.addEventListener('scroll', event => {
    if (event.target?.classList?.contains('tile-settings-body')) trimSettingsScrollSpacer(event.target);
  }, true);

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
    // Half-height tiles offer only the value sizes that fit.
    const halfHeight = Number(document.getElementById(prefix + '_tile_span_h')?.value || 1) === 0.5;
    for (const id of ['_sensor_value_font', '_binary_sensor_value_font', '_energy_value_font'])
      syncCompactValueFontOptions(document.getElementById(prefix + id), halfHeight);
    for (const kind of ['number', 'select', 'datetime'])
      syncEditableValueFontOptions(document.getElementById(prefix + '_' + kind + '_value_font'), halfHeight);
    if (type === '5') {
      // The state beside the disc takes the half-height sizes, the large
      // state of a tall tile the full-size ones.
      const spanH = Number(document.getElementById(prefix + '_tile_span_h')?.value || 1);
      syncCompactValueFontOptions(document.getElementById(prefix + '_switch_value_font'),
                                  !switchSensorLook(switchStyle, spanH));
      syncSwitchChoices(tab);
    }
    const deviceKind = typeof devicePreviewKind === 'function' ? devicePreviewKind(type) : '';
    if (type === '19' || deviceKind) {
      // Like the Switch: beside the disc the half-height sizes, from 1.5 rows
      // the full-size ones (tile_header.h).
      const spanH = Number(document.getElementById(prefix + '_tile_span_h')?.value || 1);
      syncCompactValueFontOptions(document.getElementById(prefix + (deviceKind ? '_' + deviceKind : '_cover') +
        '_value_font'), !(spanH > 1));
      syncSwitchChoices(tab);
    }
    const previewKind = meta.preview || 'none';
    // Number, Select and Date/Time keep their own value size field.
    const sensorValueFont = isEditablePreview(previewKind)
      ? (document.getElementById(prefix + '_' + previewKind + '_value_font')?.value ?? '2')
      : isEnergyType
      ? (document.getElementById(prefix + '_energy_value_font')?.value || '0')
      : (document.getElementById(prefix + (type === '20' ? '_binary_sensor_value_font'
        : (type === '5' ? '_switch_value_font' : (type === '19' ? '_cover_value_font'
          : (deviceKind ? '_' + deviceKind + '_value_font' : '_sensor_value_font')))))?.value || '0');
    const sensorValueClass = getSensorValueFontClass(sensorValueFont);
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
    if (deviceKind) iconEntity = document.getElementById(prefix + '_' + deviceKind + '_entity')?.value || '';
    if (type === '2') {
      const alias = document.getElementById(prefix + '_scene_alias')?.value || '';
      iconEntity = sensorMetaCache.sceneEntities?.[alias] || '';
    }
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
    let devicePreviewState = null;
    if (deviceKind) {
      devicePreviewState = parseDevicePreviewPayload(deviceDetailPayload(iconEntity));
      if (!normalizeMdiIconName(rawIcon) && !isExplicitlyDisabledValue(rawIcon)) {
        iconName = devicePreviewIcon(deviceKind, devicePreviewState);
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
    if (type === '5') applySwitchPreviewLayout(tileElem, switchStyle, halfHeight);
    tileElem.style.background = '';
    delete tileElem.dataset.bgOpacity;
    tileElem.dataset.type = type;
    tileElem.dataset.iconDisc = tileTypeHasDiscToggle(type)
      && document.getElementById(prefix + '_tile_icon_disc')?.checked === false ? '2' : '0';
    tileElem.dataset.iconGlow = tileTypeHasColoredIcon(type)
      && document.getElementById(prefix + '_tile_icon_glow')?.checked === false ? '0' : '1';
    const borderToggle = type === '8' ? '_back_tile_border'
      : (type === '9' ? '_clock_tile_border' : (type === '10' ? '_text_tile_border' : ''));
    tileElem.classList.toggle('tile-border-hidden', !!borderToggle && document.getElementById(prefix + borderToggle)?.checked === false);

    if (type === '0') {
      tileElem.classList.add('empty');
      tileElem.style.background = 'transparent';
      tileElem.innerHTML = '';
      applyTileAriaLabel(tileElem, '', type);
      if (wasActive) tileElem.classList.add('active');
      updateLayoutFromInputs(tab);
    applyCompactSensorPreview(tileElem, type, {span_w:Number(document.getElementById(prefix + '_tile_span_w')?.value || 1),
      span_h:Number(document.getElementById(prefix + '_tile_span_h')?.value || 1)},
      document.getElementById(prefix + '_sensor_display_mode')?.value || 0, sensorValueFont);
      return;
    }

    const defaultBg = meta.defaultBg || '#353535';
    // Tiles without their own color (or with the stored default grey) show
    // the global default tile color. Only the Tile color buttons change the
    // choice: a Custom color that is still a default grey (Custom was just
    // selected and nothing picked yet) stays Custom instead of switching back
    // to Global and hiding the color field.
    const isDefaultBg = tileColorInputIsDefault(tab);
    const colorInput = document.getElementById(prefix + '_tile_color');
    if (colorInput?.dataset.bgColorDefault === '1') colorInput.value = defaultBg;
    syncTileColorMode(tab);
    const tileBg = tileBackgroundCss(meta, isDefaultBg,
      isDefaultBg ? defaultBg : (color || defaultBg));
    if (isScreensaverTileTab(tab)) {
      // One opacity for every screensaver tile (screensaver footer).
      const opacity = screensaverTileOpacity();
      tileElem.style.background = tileBackgroundCss(meta, isDefaultBg,
        isDefaultBg ? defaultBg : (color || defaultBg), opacity);
      tileElem.dataset.bgOpacity = String(opacity);
      // A fully transparent card casts no shadow (apply_slot_tile_shadows).
      tileElem.classList.toggle('screensaver-bg-clear', opacity === 0);
    } else {
      tileElem.style.background = tileBg;
    }

    let html = '';
    const locked = typeof previewTileLocked === 'function' && previewTileLocked(type, tileElem);
    const lockIsIcon = locked && !iconName;
    if (lockIsIcon) iconName = 'lock';

    if (iconName) {
      const iconRecord = typeof collectIconColorRecord === 'function' ? collectIconColorRecord(prefix) : '';
      const iconColor = previewIconColor(type, iconRecord, iconEntity, sensorMetaCache,
        binarySensorPreviewState, previewKind === 'climate'
          ? climatePreviewColor(climatePreviewState)
          : (previewKind === 'cover'
            ? coverPreviewColor(coverPreviewState)
            : (deviceKind
              ? devicePreviewColor(deviceKind, devicePreviewState)
              : (previewKind === 'binary_sensor'
                ? binarySensorPreviewColor(binarySensorPreviewState)
                : ''))));
      const iconStyle = iconColor ? ' style="color:' + escapeHtml(iconColor) + '"' : '';
      html += '<i class="mdi mdi-' + escapeHtml(iconName) + ' tile-icon"' + iconStyle + '>' +
        (locked && !lockIsIcon ? PREVIEW_LOCK_MARK : '') + '</i>';
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

    if (previewKind === 'climate') {
      const climateSpanW = document.getElementById(
        prefix + '_tile_span_w')?.value || 1;
      const climateSpanH = document.getElementById(
        prefix + '_tile_span_h')?.value || 1;
      // Layout "with value" and half height: the value pair beside the
      // disc; half height has no mini fields.
      const climateHalf = Number(climateSpanH) === 0.5;
      const climateValue = document.getElementById(prefix + '_climate_view')?.value === '1';
      tileElem.classList.toggle('climate-header', climateValue && !climateHalf);
      if (climateHalf || climateValue) {
        html += '<div class="tile-value tile-switch-state">' +
          escapeHtml(climatePreviewHeaderText(climatePreviewState)) + '</div>';
      }
      if (!climateHalf) {
        html += climatePreviewSlots(
          climatePreviewState, climateSpanW, climateSpanH,
          currentClimateSlotConfig(tab),
          currentClimateTargetLayouts(tab),
          currentClimateGeometry(tab),
          climateValue);
      }
    }
    if (previewKind === 'cover') html += coverPreviewExtraHtml(coverPreviewState, halfHeight);
    if (deviceKind) html += devicePreviewExtraHtml(deviceKind, devicePreviewState, halfHeight);
    if (previewKind === 'binary_sensor') {
      html += '<div class="tile-value tile-binary-sensor-value ' + (Number(sensorValueFont) ? sensorValueClass : '') + '" id="' +
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
      if (flags & 2) html += '<div class="tile-clock-date" ' + getClockPreviewTextStyle(clockDateFont, 20, '#fff') + '>' + getClockPreviewDate(clockDateFormat) + '</div>';
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

    if (previewKind === 'switch') html += switchPreviewExtraHtml(switchStyle, halfHeight);

    html += getTileResizeHandlesHtml(type);
    // A title or icon moves the clock down (clock/renderer.cpp).
    if (type === '9') tileElem.classList.toggle('clock-has-header', !!(displayTitle || iconName));
    tileElem.innerHTML = html;
    if (previewKind === 'weather') {
      const iconRecord = typeof collectIconColorRecord === 'function' ? collectIconColorRecord(prefix) : '';
      applyWeatherPreview(tileElem, parseWeatherPreviewPayload(
        weatherEntity ? (sensorMetaCache.weatherValues?.[weatherEntity] ?? '') : ''), {
        col: Number(tileElem.dataset.col || 0),
        span_w: Number(document.getElementById(prefix + '_tile_span_w')?.value || 1),
        span_h: Number(document.getElementById(prefix + '_tile_span_h')?.value || 1),
        sensor_display_mode: document.getElementById(prefix + '_weather_colored_icons')?.checked === false ? 1 : 0
      }, iconName, previewIconColor(type, iconRecord, weatherEntity, sensorMetaCache, null, ''));
    }
    if (previewKind === 'media') {
      applyMediaPreview(tileElem, parseMediaPreviewPayload(
        mediaEntity ? (sensorMetaCache.mediaValues?.[mediaEntity] ?? '') : ''), {
        sensor_entity: mediaEntity,
        span_w: Number(document.getElementById(prefix + '_tile_span_w')?.value || 1),
        span_h: Number(document.getElementById(prefix + '_tile_span_h')?.value || 1)
      }, iconName, mediaEntity ? (sensorMetaCache.names?.[mediaEntity] || '') : '');
    }
    if (typeof applyTileRulesTint === 'function' && typeof collectIconColorRecord === 'function' &&
        typeof iconColorOwnEntity === 'function') {
      applyTileRulesTint(tileElem, type, collectIconColorRecord(prefix), iconColorOwnEntity(prefix, String(type)), sensorMetaCache);
    }
    if (previewKind === 'media' && typeof collectIconColorRecord === 'function') {
      applyMediaCoverTint(tileElem, collectIconColorRecord(prefix), sensorMetaCache.mediaCoverColors?.[mediaEntity] || '');
    }
    applyIconDiscTint(tileElem);
    if (previewKind === 'cover') applyCoverPreview(tileElem, coverPreviewState, halfHeight);
    if (deviceKind) applyDevicePreview(tileElem, deviceKind, devicePreviewState, halfHeight);
    if (wasActive) tileElem.classList.add('active');
    if (typeWas !== type && wasActive) {
      tileElem.classList.add('active');
      const settingsId = tab + 'Settings';
      document.getElementById(settingsId)?.classList.remove('hidden');
    }
    if (type === '5') updateSwitchValuePreview(tab);
    updateLayoutFromInputs(tab);
    applyCompactSensorPreview(tileElem, type, {span_w:Number(document.getElementById(prefix + '_tile_span_w')?.value || 1),
      span_h:Number(document.getElementById(prefix + '_tile_span_h')?.value || 1)},
      document.getElementById(prefix + '_sensor_display_mode')?.value || 0, sensorValueFont);
    if (previewKind === 'climate' &&
        Number(document.getElementById(prefix + '_tile_span_h')?.value || 1) !== 0.5 &&
        typeof mountClimateMiniEditor === 'function') {
      mountClimateMiniEditor(tab);
      syncClimateSlotFields(tab);
    }
    // The automatic icon of the head follows what the preview draws.
    if (typeof renderTileIconButton === 'function') renderTileIconButton(tab);
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
        loadIconDiscFields(prefix, data);
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
    syncTileSizePolicy(tab);
    syncIconDiscFields(tab);
    placeTileEntityField(tab);
  }

  function syncTileSizePolicy(tab) {
    const typeEl = document.getElementById(tab + '_tile_type');
    if (!typeEl) return;
    const h = Number(document.getElementById(tab + '_tile_span_h')?.value || 1);
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
    if (row) row.max = String(GRID_ROWS + (compact && h === 0.5 ? 0.5 : 0));
    const height = document.getElementById(tab + '_tile_span_h');
    if (height && compact) height.min = '0.5';
    const note = document.getElementById(tab + '_tile_size_note');
    if (note) note.hidden = !halfHeight;
  }

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
    resetIconDiscFields(tab);
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

  // Whether the Settings tile is hidden, and the parked tile then: the
  // import restores both (issue #70). The page holds them in the Home tab's
  // parking slot, rendered from the device config.
  function exportSettingsTileState() {
    const parked = document.getElementById('settingsHiddenTile');
    if (!parked) return undefined;
    if (parked.dataset.hidden !== '1') return { hidden: false };
    return {
      hidden: true,
      title: String(parked.dataset.title || ''),
      icon_name: String(parked.dataset.icon || ''),
      bg_color: Number(parked.dataset.bgColor || 0),
      col: Number(parked.dataset.col || 0),
      row: Number(parked.dataset.row || 0),
      span_w: Number(parked.dataset.spanW || 1),
      span_h: Number(parked.dataset.spanH || 1)
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
      // The tiles with their classic places whatever layout is shown; the
      // other layouts' places come with the layout file (tile_layouts.h).
      const layoutsRequest = fetch('/api/layouts', {cache: 'no-store'})
        .then(res => res.ok ? res.json() : null).catch(() => null);
      const tilesLists = await Promise.all(folders.map(async folder => {
        const response = await fetch(
          '/api/tiles?folder=' + encodeURIComponent(folder.id) + '&layout=classic');
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
      const layoutData = await layoutsRequest;

      const payload = {
        version: 3,
        exported_at: new Date().toISOString(),
        folders: folders,
        grids: grids,
        settings_tile: exportSettingsTileState(),
        // Only a panel with the same screen takes these places back.
        layouts: layoutData?.success ? {
          screen_width: LAYOUTS.classic.screenW,
          screen_height: LAYOUTS.classic.screenH,
          active: layoutData.stored,
          places: layoutData.places
        } : undefined,
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
    const res = await fetch('/api/tiles?folder=' + encodeURIComponent(folderId) + '&layout=classic');
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
      // A folder its folder tile already mapped keeps that exact target; two
      // folders with the same name and icon would otherwise share one.
      if (sourceToTarget[sourceId] !== undefined) return;
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

  // The type postTile sends: old exports stored folder, Settings and Back
  // tiles as a navigate tile with a kind.
  function importTileType(tile) {
    const type = Number(tile && tile.type);
    if (isNaN(type)) return 0;
    if (type === 4 && tile.navigate_kind !== undefined && tile.navigate_kind !== null) {
      const kind = Number(tile.navigate_kind);
      if (kind === 1) return 7;
      if (kind === 2) return 8;
    }
    return type;
  }

  // A tile as the import messages name it: its title, else its type.
  function importTileName(tile) {
    const title = String(tile?.title || '').replace(/\s+/g, ' ').trim();
    if (title) return title;
    const type = importTileType(tile);
    const option = document.querySelector('select[id$="_tile_type"] option[value="' + type + '"]');
    return option ? option.textContent.trim() : String(type);
  }

  class TileImportError extends Error {
    constructor(message, kind, tile, folderName) {
      super(message);
      this.kind = kind;
      this.tile = tile;
      this.folderName = folderName;
    }
  }

  // One Home or folder grid of an import, planned before anything is
  // written: every tile where postTile will place it, and where the
  // target's system tile (Settings in Home, Back in a folder) ends up. A
  // system tile in the export takes its exported place. Without one (an
  // export with the Settings tile hidden) the target's own stays where it
  // is, or moves to the nearest free place when an imported tile needs its
  // cell (issue #70). A tile that cannot be placed is returned as the
  // conflict, as the server would refuse it (tile_geometry::supported,
  // placementOverlaps).
  function planImportGrid(sourceTiles, systemType, targetSystem, tab) {
    const tileCount = GRID_COLS * GRID_ROWS;
    const occupied = Array.from({ length: GRID_ROWS * 2 }, () => Array(GRID_COLS * 2).fill(false));
    const cells = (layout, mark) => {
      for (let y = layout.row * 2; y < (layout.row + layout.span_h) * 2; y++) {
        for (let x = layout.col * 2; x < (layout.col + layout.span_w) * 2; x++) {
          if (!mark && occupied[y][x]) return false;
          if (mark) occupied[y][x] = true;
        }
      }
      return true;
    };
    const fits = (type, layout) => supportedTileLayout(type, layout) &&
      layout.col + layout.span_w <= GRID_COLS && layout.row + layout.span_h <= GRID_ROWS &&
      cells(layout, false);

    const entries = [];
    let sourceSystem = null;
    (Array.isArray(sourceTiles) ? sourceTiles : []).forEach((tile, index) => {
      const type = importTileType(tile);
      if (!type) return;
      const entry = { tile, type, layout: normalizeTileLayout({ ...tile, type }, index, tab) };
      if (type === systemType && !sourceSystem) sourceSystem = entry;
      else entries.push(entry);
    });
    // Settings belongs to Home, Back to folders; one of each.
    const misplaced = entries.find(entry => entry.type === 7 || entry.type === 8);
    if (misplaced) return { conflict: misplaced.tile };
    if (entries.length + (sourceSystem || targetSystem ? 1 : 0) > tileCount) {
      return { conflict: entries[entries.length - 1].tile };
    }

    // An exported system tile replaces the target's; a target without one
    // (Settings hidden there) keeps it hidden and the place stays free.
    const placeSystem = !!(sourceSystem && targetSystem);
    if (placeSystem) {
      if (!fits(systemType, sourceSystem.layout)) return { conflict: sourceSystem.tile };
      cells(sourceSystem.layout, true);
    }
    for (const entry of entries) {
      if (!fits(entry.type, entry.layout)) return { conflict: entry.tile };
      cells(entry.layout, true);
    }
    const tiles = entries.map(entry => ({ ...entry.tile, ...entry.layout }));
    if (placeSystem) return { tiles, system: { ...sourceSystem.tile, ...sourceSystem.layout } };
    if (!targetSystem) return { tiles, system: null };

    const current = normalizeTileLayout(targetSystem, 0, tab);
    if (fits(systemType, current)) return { tiles, system: null };
    let best = null;
    for (let row = 0; row + current.span_h <= GRID_ROWS; row += 0.5) {
      for (let col = 0; col + current.span_w <= GRID_COLS; col += 0.5) {
        const layout = { ...current, col, row };
        const distance = Math.abs(col - current.col) + Math.abs(row - current.row);
        if ((!best || distance < best.distance) && fits(systemType, layout)) best = { layout, distance };
      }
    }
    if (!best) return { conflict: targetSystem };
    return { tiles, system: { ...targetSystem, ...best.layout } };
  }

  // Writes a planned grid over its current tiles: the old tiles go first
  // so no imported tile meets one of them, then the system tile takes its
  // place, then the imported tiles. Empty cells are not written. A folder
  // tile maps its exported folder to the one the server created for it.
  // Where each exported tile landed ({source, view, target, index}), so the
  // other layouts' places follow their tiles (restoreImportLayouts).
  let importedSlots = [];

  async function applyImportGrid(folderId, currentTiles, plan, systemType, folderName, sourceToTarget = null,
                                 sourceFolderId = null) {
    const tileCount = GRID_COLS * GRID_ROWS;
    const systemIndex = currentTiles.findIndex(tile => importTileType(tile) === systemType);
    const post = async (index, tile) => {
      try {
        return await postTile(folderId, index, tile, sourceToTarget);
      } catch (e) {
        throw new TileImportError(e.message, 'stopped', tile, folderName);
      }
    };
    for (let i = 0; i < tileCount; i++) {
      if (i !== systemIndex && importTileType(currentTiles[i]) !== 0) {
        await post(i, buildEmptyImportTile(i));
      }
    }
    if (systemIndex >= 0 && plan.system) await post(systemIndex, plan.system);
    const note = (tile, index) => {
      if (sourceFolderId !== null && Number(tile.view_id) > 0) {
        importedSlots.push({source: sourceFolderId, view: Number(tile.view_id), target: folderId, index});
      }
    };
    const freeIndices = [];
    for (let i = 0; i < tileCount; i++) {
      if (i !== systemIndex) freeIndices.push(i);
    }
    for (let i = 0; i < plan.tiles.length; i++) {
      const tile = plan.tiles[i];
      const data = await post(freeIndices[i], tile);
      note(tile, freeIndices[i]);
      const sourceTarget = Number(tile.navigate_target);
      const target = Number(data && data.navigate_target);
      if (sourceToTarget && importTileType(tile) === 4 && sourceTarget > 0 && target > 0 &&
          sourceToTarget[sourceTarget] === undefined) {
        sourceToTarget[sourceTarget] = target;
      }
    }
  }

  // Hides or shows the Settings tile through the access settings save, which
  // keeps the PIN and the swipe gesture as they are; a shown tile takes the
  // exported place, a hidden one the exported parked tile.
  async function setSettingsTileHiddenForImport(hidden, snapshot, target, folderName) {
    const saved = typeof saveSettingsAccess === 'function' && typeof readSettingsAccessState === 'function'
      ? await saveSettingsAccess(null, target, snapshot, { ...readSettingsAccessState(), tileHidden: hidden }, false)
      : false;
    if (!saved) {
      throw new TileImportError('Settings tile visibility not saved', 'stopped',
                                { type: 7, title: snapshot?.title || '' }, folderName);
    }
  }

  // A folder created by the import gets its Back tile in the first free
  // cell, the top-left one of an empty grid (TileConfig::ensureBackTile).
  const NEW_FOLDER_BACK_TILE = { type: 8, title: '', icon_name: 'arrow-left', col: 0, row: 0, span_w: 1, span_h: 1 };

  async function replaceFolderGridForImport(folderId, sourceTiles, systemType, folderName, sourceToTarget = null,
                                           sourceFolderId = null) {
    const currentTiles = await fetchTilesForImport(folderId);
    const targetSystem = currentTiles.find(tile => importTileType(tile) === systemType) || null;
    const plan = planImportGrid(sourceTiles, systemType, targetSystem, tabByFolder[folderId] || '');
    if (plan.conflict) throw new TileImportError('Import layout conflict', 'stopped', plan.conflict, folderName);
    await applyImportGrid(folderId, currentTiles, plan, systemType, folderName, sourceToTarget, sourceFolderId);
  }

  // The bar layouts' places of an export from a panel with the same screen
  // go to the imported tiles (their new IDs); a layout whose folders would
  // lose their place stays as it was.
  async function restoreImportLayouts(layouts) {
    if (!layouts || typeof layouts !== 'object' || !importedSlots.length) return;
    if (Number(layouts.screen_width) !== LAYOUTS.classic.screenW ||
        Number(layouts.screen_height) !== LAYOUTS.classic.screenH) return;
    const tiles = {};
    for (const target of new Set(importedSlots.map(slot => slot.target))) {
      tiles[target] = await fetchTilesForImport(target);
    }
    for (const key of ['bar', 'portrait']) {
      const places = layouts.places?.[key];
      if (!places || !LAYOUTS[key]?.available) continue;
      const folders = {};
      for (const slot of importedSlots) {
        const place = places[String(slot.source)]?.[String(slot.view)];
        const view = tiles[slot.target]?.[slot.index]?.view_id;
        if (!Array.isArray(place) || !view) continue;
        (folders[slot.target] = folders[slot.target] || {})[view] = place;
      }
      if (!Object.keys(folders).length) continue;
      await fetch('/api/layouts', {method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({layout: key, folders})}).catch(() => {});
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
    const occupied = Array.from({ length: GRID_ROWS * 2 }, () => Array(GRID_COLS * 2).fill(false));
    const prepared = [];
    for (const entry of sourceEntries) {
      if (prepared.length >= tileCount) {
        throw new TileImportError('Screensaver grid does not fit', 'conflict', entry.tile, t('importScreensaver'));
      }
      const tile = entry.tile;
      const mediaTile = Number(tile.type) === MEDIA_TILE_TYPE;
      const half = value => Math.round(Number(value || 1) * 2) / 2;
      let spanW = Math.max(1, half(tile.span_w));
      let spanH = Math.max(supportsHalfSize(tile.type) ? 0.5 : 1, half(tile.span_h));
      if (mediaTile) {
        spanW = Math.max(MEDIA_TILE_MIN_SPAN, spanW);
        spanH = Math.max(MEDIA_TILE_MIN_SPAN, spanH);
      }
      spanW = Math.min(spanW, GRID_COLS, mediaTile ? MEDIA_TILE_MAX_SPAN : GRID_COLS);
      spanH = Math.min(spanH, 2, mediaTile ? MEDIA_TILE_MAX_SPAN : 2);

      const sourceSpanW = Math.max(1, half(tile.span_w));
      const sourceColRange = Math.max(0, sourceCols - sourceSpanW);
      const targetColRange = Math.max(0, GRID_COLS - spanW);
      const relativeCol = sourceColRange > 0
        ? Math.max(0, Math.min(1, Number(tile.col || 0) / sourceColRange))
        : 0;
      const desiredCol = Math.round(relativeCol * targetColRange);
      const sourceRowOffset = Math.max(0, Math.min(1, Number(tile.row || 0) - firstSourceRow));
      const desiredRow = Math.min(GRID_ROWS - spanH, firstTargetRow + sourceRowOffset);

      let best = null;
      for (let row = firstTargetRow; row <= GRID_ROWS - spanH; row += 0.5) {
        for (let col = 0; col <= GRID_COLS - spanW; col += 0.5) {
          let free = true;
          for (let y = row * 2; y < (row + spanH) * 2 && free; y++) {
            for (let x = col * 2; x < (col + spanW) * 2; x++) {
              if (occupied[y][x]) { free = false; break; }
            }
          }
          if (!free) continue;
          const score = Math.abs(row - desiredRow) * (GRID_COLS + 1) + Math.abs(col - desiredCol);
          if (!best || score < best.score) best = { row, col, score };
        }
      }
      if (!best) {
        throw new TileImportError('Screensaver grid does not fit', 'conflict', tile, t('importScreensaver'));
      }
      for (let y = best.row * 2; y < (best.row + spanH) * 2; y++) {
        for (let x = best.col * 2; x < (best.col + spanW) * 2; x++) occupied[y][x] = true;
      }
      prepared.push({
        targetIndex: prepared.length,
        tile: { ...tile, col: best.col, row: best.row, span_w: spanW, span_h: spanH }
      });
    }
    return prepared;
  }

  // The screensaver grid as the import writes it, checked before anything
  // is written like the Home and folder grids.
  function planScreensaverImport(sourceTiles, sourceLayout) {
    const preparedTiles = prepareScreensaverTilesForImport(sourceTiles, sourceLayout);
    const supportedTypes = new Set([1, 2, 5, 14, 20, 21, 22, 23, MEDIA_TILE_TYPE]);
    for (const entry of preparedTiles) {
      if (!supportedTypes.has(Number(entry.tile.type || 0))) {
        throw new TileImportError('Unsupported screensaver tile type', 'conflict', entry.tile, t('importScreensaver'));
      }
    }
    return preparedTiles;
  }

  async function replaceScreensaverGridForImport(preparedTiles) {
    const folderId = SCREENSAVER_FOLDER_ID;
    const currentTiles = await fetchTilesForImport(folderId);
    const tileCount = GRID_COLS * GRID_ROWS;
    const post = async (index, tile) => {
      try {
        await postTile(folderId, index, tile);
      } catch (e) {
        throw new TileImportError(e.message, 'stopped', tile, t('importScreensaver'));
      }
    };

    // Remove the existing tiles first so the imported positions do not fail on
    // temporary overlaps with the old grid.
    for (let i = 0; i < tileCount; i++) {
      if (Number(currentTiles[i]?.type || 0) !== 0) {
        await post(i, buildEmptyImportTile(i));
      }
    }

    for (const entry of preparedTiles) {
      await post(entry.targetIndex, entry.tile);
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

      const sourceFolders = Array.isArray(payload.folders) ? payload.folders : [{ id: 0, parent_id: 0, name: 'Home', icon_name: '' }];
      const folderName = folderId => {
        const folder = sourceFolders.find(entry => Number(entry && entry.id) === Number(folderId));
        return String(folder?.name || '') || String(folderId);
      };
      const sourceFolderIds = sourceFolders
        .map(folder => parseInt(folder && folder.id, 10))
        .filter(folderId => !isNaN(folderId) && folderId !== 0 && Array.isArray(grids[String(folderId)]));
      // Versions 1 and 2 had no screensaver block and stay importable
      // unchanged. Alternative flat field names are accepted as well, in case an
      // intermediate state of this export function was used.
      const screensaverBlock = payload.screensaver && typeof payload.screensaver === 'object'
        ? payload.screensaver
        : null;
      const screensaverConfig = screensaverBlock?.config || payload.screensaver_config;
      const screensaverGrid = screensaverBlock?.grid || payload.screensaver_grid;

      // The Settings tile follows the export (issue #70): hidden there is
      // hidden here, shown there comes back at its exported place. Exports
      // without the flag tell it by their Home grid; a Settings tile in the
      // grid always means shown.
      const homeSource = Array.isArray(grids['0']) ? grids['0'] : null;
      const sourceSettings = homeSource ? homeSource.find(tile => importTileType(tile) === 7) || null : null;
      const exportedSettings = payload.settings_tile && typeof payload.settings_tile === 'object'
        ? payload.settings_tile : null;
      const settingsHidden = !!homeSource && !sourceSettings;
      const parkedSettings = settingsHidden && exportedSettings?.hidden === true ? exportedSettings : null;

      // The whole layout is checked before anything is written: Home with
      // the Settings tile where the export has it, every folder against the
      // Back tile a new folder gets (Home replaces every folder), and the
      // screensaver grid. A tile that does not fit stops the import here.
      let homeTiles = homeSource ? await fetchTilesForImport(0) : null;
      const targetSettings = homeTiles ? homeTiles.find(tile => importTileType(tile) === 7) || null : null;
      const homePlan = homeSource
        ? planImportGrid(homeSource, 7, settingsHidden ? null : (targetSettings || { type: 7 }), tabByFolder[0] || '')
        : null;
      if (homePlan?.conflict) throw new TileImportError('Import layout conflict', 'conflict', homePlan.conflict, folderName(0));
      for (const folderId of sourceFolderIds) {
        const plan = planImportGrid(grids[String(folderId)], 8, NEW_FOLDER_BACK_TILE, '');
        if (plan.conflict) throw new TileImportError('Import layout conflict', 'conflict', plan.conflict, folderName(folderId));
      }
      const screensaverTiles = Array.isArray(screensaverGrid)
        ? planScreensaverImport(screensaverGrid, screensaverBlock?.source_layout || null)
        : null;

      showNotification(t('importRunning'));
      importedSlots = [];

      const sourceToTarget = { 0: 0 };
      if (homePlan) {
        // Hiding first frees the Settings tile's cell for the imported tiles;
        // a hidden one takes the exported parked tile.
        if (settingsHidden && (targetSettings || parkedSettings)) {
          await setSettingsTileHiddenForImport(true, parkedSettings, null, folderName(0));
          if (targetSettings) homeTiles = await fetchTilesForImport(0);
        }
        await applyImportGrid(0, homeTiles, homePlan, 7, folderName(0), sourceToTarget, 0);
        if (!settingsHidden && !targetSettings) {
          await setSettingsTileHiddenForImport(
            false, homePlan.system, { col: homePlan.system.col, row: homePlan.system.row }, folderName(0));
        }
      }

      // A folder tile names the folder the server created for it; exports
      // without folder targets match folders by name and icon.
      let targetFolders = await fetchFoldersForImport();
      updateFolderImportMap(sourceFolders, targetFolders, sourceToTarget);

      const pendingFolderIds = sourceFolderIds.slice();
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
          await replaceFolderGridForImport(
            targetFolderId, grids[String(sourceFolderId)], 8, folderName(sourceFolderId), sourceToTarget,
            sourceFolderId);
          pendingFolderIds.splice(i, 1);
          progressed = true;
          targetFolders = await fetchFoldersForImport();
          updateFolderImportMap(sourceFolders, targetFolders, sourceToTarget);
        }
      }
      // A folder no folder tile of the export leads to cannot be reached on
      // the device either; it stays out instead of failing the import.
      if (pendingFolderIds.length) {
        console.warn('Import skipped unreachable folders:', pendingFolderIds);
      }

      await restoreImportLayouts(payload.layouts);

      if (screensaverConfig && typeof screensaverConfig === 'object') {
        await importScreensaverConfig(screensaverConfig);
      }
      if (screensaverTiles) {
        await replaceScreensaverGridForImport(screensaverTiles);
      }

      try { localStorage.removeItem('tileDrafts'); } catch (e) {}
      showNotification(t('importComplete'));
      setTimeout(() => location.reload(), 600);
    } catch (e) {
      console.error('Tile import failed:', e);
      if (e instanceof TileImportError) {
        const message = t(e.kind === 'conflict' ? 'importConflict' : 'importStopped');
        showNotification(message.replace('{tile}', importTileName(e.tile)).replace('{folder}', e.folderName), false);
      } else {
        showNotification(t('importFailed'), false);
      }
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
    // The export's places are the classic layout's (tile_layouts.h).
    fd.append('layout', 'classic');
    fd.append('title', tile.title || '');
    fd.append('icon_name', tile.icon_name || '');
    if (tile.icon_disc !== undefined && tile.icon_disc !== null) {
      fd.append('icon_disc', tile.icon_disc);
    }
    if (tile.icon_glow !== undefined && tile.icon_glow !== null) {
      fd.append('icon_glow', ['0', 'false'].includes(String(tile.icon_glow)) ? '0' : '1');
    }
    const parsedBgColor = parseBgColorValue(tile.bg_color);
    if (parsedBgColor !== 0 || (typeof tile.bg_color === 'string' && tile.bg_color.trim().startsWith('#'))) {
      fd.append('bg_color', parsedBgColor);
    } else {
      fd.append('bg_color_default', '1');
    }
    const layout = normalizeTileLayout({ ...tile, type: safeType }, index, tabByFolder[folderId] || '');
    fd.append('col', layout.col);
    fd.append('row', layout.row);
    fd.append('span_w', layout.span_w);
    fd.append('span_h', layout.span_h);
    if (tile.background_opacity !== undefined && tile.background_opacity !== null) {
      fd.append('background_opacity', tile.background_opacity);
    }
    // Per-tile icon colors (Sensor family, Binary sensor, Energy); older
    // exports without the field import without icon colors.
    if (typeof tile.icon_colors === 'string') fd.append('icon_colors', tile.icon_colors);

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
      fd.append('sensor_value_font', tile.sensor_value_font ?? 0);
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
      fd.append('sensor_value_font', tile.sensor_value_font ?? 0);
      if (tile.popup_open_mode !== undefined && tile.popup_open_mode !== null) {
        fd.append('popup_open_mode', tile.popup_open_mode);
      }
    } else if (safeType === 8) {
      fd.append('tile_border', Number(tile.sensor_display_mode) === 1 ? '0' : '1');
    } else if (safeType === 10) {
      fd.append('text_value', tile.text_value || tile.scene_alias || tile.key_macro || '');
      fd.append('text_value_font', tile.text_value_font || tile.sensor_value_font || '0');
      fd.append('tile_border', Number(tile.sensor_display_mode) === 1 ? '0' : '1');
    } else if (safeType === 9) {
      fd.append('tile_border', Number(tile.sensor_display_mode) === 1 ? '0' : '1');
      fd.append('clock_show_time', ((Number(tile.sensor_decimals || 1) & 1) !== 0) ? '1' : '0');
      fd.append('clock_show_date', ((Number(tile.sensor_decimals || 1) & 2) !== 0) ? '1' : '0');
      fd.append('key_code', tile.key_code || 40);
      fd.append('key_modifier', tile.key_modifier || 20);
      fd.append('clock_time_format', (tile.sensor_gauge_min !== undefined && tile.sensor_gauge_min !== null) ? tile.sensor_gauge_min : 0);
      fd.append('clock_date_format', (tile.sensor_gauge_max !== undefined && tile.sensor_gauge_max !== null) ? tile.sensor_gauge_max : 0);
    } else if (safeType === 12) {
      fd.append('weather_entity', tile.sensor_entity || tile.weather_entity || '');
      fd.append('weather_colored_icons', Number(tile.sensor_display_mode) === 1 ? '0' : '1');
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
    } else if (safeType === 24 || safeType === 25 || safeType === 26) {
      const kind = devicePreviewKind(safeType);
      fd.append(kind + '_entity', tile.sensor_entity || tile[kind + '_entity'] || '');
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
    const data = await res.json().catch(() => ({}));
    if (!data.success) {
      // The server's English reason stays in the log (importTilesPayload);
      // the page names the tile and folder in the user's language.
      throw new Error('Tile save failed: ' + (data.error || ('HTTP ' + res.status)));
    }
    return data;
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
  // Tiles without their own color follow the global default tile color. The
  // preview paints them through one root variable, so a change of that color
  // repaints loaded, cached and lazily inserted grids at once.
  function tileBackgroundCss(meta, isDefault, hex, opacity = null) {
    const shared = !!isDefault && !!meta?.sharedBg;
    const sharedCss = 'var(--tile-default-bg, #2A2A2A)';
    if (opacity === null || opacity === undefined) return shared ? sharedCss : hex;
    if (shared) {
      return 'color-mix(in srgb, ' + sharedCss + ' ' +
        (opacity * 100 / 255).toFixed(2) + '%, transparent)';
    }
    return hex + opacity.toString(16).padStart(2, '0');
  }
  // Mirrors tile_icon_disc::icon_color_tints(): with glow on, a colored icon
  // tints its disc with its own hue; white and grey icons keep the white disc.
  function iconDiscTinted(color) {
    const rgb = String(color || '').match(/(\d+)\D+(\d+)\D+(\d+)/);
    return !!rgb && !(rgb[1] === rgb[2] && rgb[2] === rgb[3]);
  }
  // Channels (0..255) of a computed CSS color, or null when it is fully
  // transparent or unknown. Chrome reports color-mix() backgrounds (screensaver
  // tiles with an opacity) as color(srgb r g b / a) with 0..1 channels.
  function cssColorMatch(value) {
    const text = String(value || '').trim();
    const srgb = text.match(/^color\(srgb\s+([\d.eE+-]+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)(?:\s*\/\s*([\d.]+%?))?/);
    const rgb = text.match(/^rgba?\(\s*([\d.]+)[\s,]+([\d.]+)[\s,]+([\d.]+)(?:[\s,/]+([\d.]+%?))?/);
    const match = srgb || rgb;
    if (!match) return null;
    const alpha = match[4] === undefined ? 1
      : match[4].endsWith('%') ? Number(match[4].slice(0, -1)) / 100 : Number(match[4]);
    return { match, srgb: !!srgb, alpha };
  }
  function cssColorChannels(value) {
    const parsed = cssColorMatch(value);
    if (!parsed || !(parsed.alpha > 0)) return null;
    return [1, 2, 3].map(i => {
      const v = Number(parsed.match[i]) * (parsed.srgb ? 255 : 1);
      return Math.max(0, Math.min(255, Math.round(v)));
    });
  }
  // The alpha (0..1) of a computed CSS color; 0 when unknown.
  function cssColorAlpha(value) {
    const parsed = cssColorMatch(value);
    return parsed && parsed.alpha > 0 ? Math.min(1, parsed.alpha) : 0;
  }
  // Mirrors src/ui/shared/tone_color.h: the circle and the controls sit a
  // fixed step of perceived lightness (OKLCH L) above the card in the icon's
  // hue (white icons: the tile's own color, lighter); a dark icon is shown
  // lighter in its own hue.
  function toneToLinear(v) {
    v /= 255;
    return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
  }
  function toneToSrgb(v) {
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    return Math.floor((v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055) * 255 + 0.5);
  }
  function toneOklch(rgb) {
    const [r, g, b] = rgb.map(toneToLinear);
    const l = Math.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const m = Math.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const s = Math.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    return { L: 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, C: Math.hypot(A, B), h: Math.atan2(B, A) };
  }
  function toneLinear(L, C, h) {
    const A = C * Math.cos(h), B = C * Math.sin(h);
    const l = (L + 0.3963377774 * A + 0.2158037573 * B) ** 3;
    const m = (L - 0.1055613458 * A - 0.0638541728 * B) ** 3;
    const s = (L - 0.0894841775 * A - 1.2914855480 * B) ** 3;
    return [4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
      -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
      -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s];
  }
  function toneRgb(L, C, h) {
    L = Math.min(1, Math.max(0, L));
    const fits = c => toneLinear(L, c, h).every(v => v >= -0.0005 && v <= 1.0005);
    if (!fits(C)) {
      let lo = 0, hi = C;
      for (let i = 0; i < 16; i++) { const mid = (lo + hi) / 2; if (fits(mid)) lo = mid; else hi = mid; }
      C = lo;
    }
    return toneLinear(L, C, h).map(toneToSrgb);
  }
  function toneBlend(under, over, opa) {
    return under.map((v, i) => Math.floor((v * (255 - opa) + over[i] * opa + 127) / 255));
  }
  // tone_color::fill(): what the circle and the controls draw. Opaque in
  // exactly their steps: 0.06 L at the default 25 %, controls at least 0.03;
  // the circle keeps 55 % of the icon's chroma. On a see-through card
  // (screensaver tiles below full Tile opacity) a veil calibrated to land on
  // the step, the controls at least opacity 32.
  function toneFill(card, icon, tinted, percent, seeThrough) {
    percent = Math.min(100, Math.max(0, percent));
    const base = toneOklch(card), seed = toneOklch(icon);
    const lift = step => tinted ? toneRgb(base.L + step, seed.C * 0.55, seed.h) : toneRgb(base.L + step, base.C, base.h);
    const discStep = percent * 0.0024;
    if (!seeThrough) {
      const control = lift(Math.max(discStep, 0.03));
      const disc = !percent ? card.slice() : discStep >= 0.03 ? control : lift(discStep);
      return { discColor: disc, controlColor: control, discOpa: percent ? 255 : 0, controlOpa: 255, disc, control, tinted };
    }
    const discOpa = Math.floor((percent * 255 + 50) / 100);
    const controlOpa = Math.max(discOpa, 32);
    const target = lift(discOpa > 32 ? discStep : 0.03);
    const color = card.map((under, i) => {
      const delta = (target[i] - under) * 255;
      return Math.min(255, Math.max(0, under + Math.trunc((delta + (delta >= 0 ? 1 : -1) * Math.floor(controlOpa / 2)) / controlOpa)));
    });
    return {
      discColor: color, controlColor: color, discOpa, controlOpa,
      disc: toneBlend(card, color, discOpa), control: toneBlend(card, color, controlOpa), tinted,
    };
  }
  // tone_color::readable_icon(): unchanged while at least 0.22 L above the
  // circle the icon gets with the default settings (tile color #1A1A1A, tile
  // color From icon 20 % like tile_tint::background, Circle strength 25 %),
  // else lighter in its own hue. The global tile color and the Circle
  // strength never change an icon's color.
  function toneReadableIcon(icon) {
    const tinted = icon[0] !== icon[1] || icon[1] !== icon[2];
    const linear = v => { const c = v / 255; return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
    const contrast = c => 1.05 / (0.2126 * linear(c[0]) + 0.7152 * linear(c[1]) + 0.0722 * linear(c[2]) + 0.05);
    let card = [26, 26, 26];
    if (tinted) {
      card = card.map((v, i) => Math.floor((v * 80 + icon[i] * 20 + 50) / 100));
      for (let i = 0; i < 40 && contrast(card) < 4.5; i++) card = card.map(v => Math.floor((v * 95 + 50) / 100));
    }
    const base = toneOklch(card), seed = toneOklch(icon);
    const circle = tinted ? toneRgb(base.L + 0.06, seed.C * 0.55, seed.h) : toneRgb(base.L + 0.06, base.C, base.h);
    const minimum = toneOklch(circle).L + 0.22;
    return seed.L >= minimum ? icon : toneRgb(minimum, seed.C, seed.h);
  }
  function toneHex(rgb) {
    return '#' + rgb.map(v => v.toString(16).padStart(2, '0')).join('');
  }
  // Default tile color "From icon" strength (tile_icon_colors::kTintDefault).
  const ICON_FILL_DEFAULT = 20;
  // A PIN-protected Folder or Settings tile shows a lock in its icon
  // (navigate renderer, icon_lock_mark.h); without an own icon the lock is
  // the icon. Folders follow their target's stored PIN (data-folder-pin-enabled),
  // Settings the stored Settings PIN.
  function previewTileLocked(typeValue, tileElem) {
    if (String(typeValue) === '7') {
      return document.getElementById('folder0_settings_pin_enabled')?.dataset.pinConfigured === '1';
    }
    return String(typeValue) === '4' && tileElem?.dataset.folderPinEnabled === '1';
  }
  const PREVIEW_LOCK_MARK = '<span class="tile-icon-lock mdi mdi-lock" aria-hidden="true"></span>';

  function applyIconDiscTint(tileElem) {
    const icon = tileElem?.querySelector(':scope > .tile-icon');
    if (!icon) return;
    const glow = tileElem.dataset.iconGlow !== '0';
    // The color the icon was given (tile_icon_disc::icon_color): a shown
    // readability lift is remembered, a new color from the editor replaces it.
    const current = cssColorChannels(getComputedStyle(icon).color);
    const currentHex = current ? toneHex(current) : '';
    const given = icon.dataset.toneShown && icon.dataset.toneShown === currentHex && icon.dataset.toneGiven
      ? icon.dataset.toneGiven.slice(1).match(/../g).map(v => parseInt(v, 16))
      : current;
    const givenHex = given ? toneHex(given) : '';
    // Mirrors tile_icon_source.cpp on_icon_color(): with Tile color "From
    // icon color" the tile takes the color the icon was given; grey and white
    // icons (off, default) keep the untinted background (tile_tint::choose).
    const fill = Number(tileElem.dataset.iconFill || 0);
    if (fill && tileElem.dataset.ruleTint !== '1' && typeof tileTintBackground === 'function' &&
        typeof tileTintChoice === 'function') {
      const choice = given ? tileTintChoice(false, '', 0, fill, givenHex) : null;
      if (choice) {
        setTileTintBackground(tileElem, choice.color, choice.percent);
      } else if (tileElem.dataset.baseBg !== undefined) {
        tileElem.style.background = tileElem.dataset.baseBg;
      }
    }
    // An unset Icon color field of the edited tile shows the color the icon
    // has now (the entity's own color, e.g. a light or a detected Binary
    // sensor); only picking a color stores a fixed one.
    if (typeof currentTileTab === 'string' && tileElem.id === currentTileTab + '-tile-' + currentTileIndex) {
      const input = document.getElementById(currentTileTab + '_tile_icon_color');
      if (input && input.dataset.unset === '1' && given) input.value = givenHex;
    }
    const tinted = glow && !!given && iconDiscTinted('rgb(' + given.join(',') + ')');
    icon.classList.toggle('tile-icon-tinted', tinted);
    // Mirrors ui_surface_style::border_hint(): a glowing icon gives the tile
    // outline its hue halfway to white (lv_color_mix(white, icon, 128)) at the
    // hairline's 20 %, mostly the tile with a hint of the icon.
    // Only a card in the tile color From icon (tile_icon_disc
    // apply_tile_options); Global and Custom keep the neutral hairline.
    if (tinted && fill > 0) {
      const hint = given.map(v => Math.floor(((255 * 128 + v * 127) * 0x8081) / 0x800000));
      tileElem.style.setProperty('--tile-border-tint', 'rgba(' + hint.join(',') + ',0.20)');
    } else {
      tileElem.style.removeProperty('--tile-border-tint');
    }
    // Global Circle strength (icon_glow.h, 0..100 %).
    const glowRaw = String(getComputedStyle(document.documentElement).getPropertyValue('--icon-glow-pct')).trim();
    const glowValue = glowRaw === '' ? 25 : Number(glowRaw);
    const glowPct = Math.min(100, Math.max(0, Number.isFinite(glowValue) ? glowValue : 25));
    const background = getComputedStyle(tileElem).backgroundColor;
    const card = cssColorChannels(background) || [0, 0, 0];
    // Screensaver tiles below full Tile opacity let the wallpaper through.
    const seeThrough = cssColorAlpha(background) < 1;
    // "Circle in icon color" shows the circle of the tile color "From icon"
    // on a Global or Custom tile too (tile_icon_disc::circle_card): computed
    // for the card From icon would give at its default strength.
    let circleCard = card;
    if (tinted && !(fill > 0) && !seeThrough && typeof tileTintBackground === 'function') {
      const base = String(getComputedStyle(document.documentElement).getPropertyValue('--tile-default-bg') || '').trim();
      const familyHex = tileTintBackground(base || '#1A1A1A', givenHex, ICON_FILL_DEFAULT);
      circleCard = [1, 3, 5].map(i => parseInt(familyHex.slice(i, i + 2), 16));
    }
    const tone = toneFill(circleCard, given || [255, 255, 255], tinted, glowPct, seeThrough);
    const rgba = (color, opa) => 'rgba(' + color.join(',') + ',' + (opa / 255).toFixed(3) + ')';
    tileElem.style.setProperty('--icon-disc-bg', rgba(tone.discColor, tone.discOpa));
    // Mirrors icon_lock_mark::behind(): the lock's rim takes the circle over
    // the card, or the card when the circle is off.
    const discShown = tileElem.dataset.iconDisc === '1' ||
      (tileElem.dataset.iconDisc !== '2' && !tileElem.closest('.icon-discs-off'));
    const discAlpha = discShown ? tone.discOpa / 255 : 0;
    const lockRim = card.map((c, i) => Math.round(c * (1 - discAlpha) + tone.discColor[i] * discAlpha));
    tileElem.style.setProperty('--icon-lock-rim', 'rgb(' + lockRim.join(',') + ')');
    // Mirrors tile_icon_source::refresh_controls(): tile controls (the
    // Climate target pill, Media buttons, the Switch bar) take the circle's
    // color whenever it is tinted, in every tile color; else the neutral step.
    const controls = tinted ? tone : toneFill(card, given || [255, 255, 255], false, glowPct, seeThrough);
    tileElem.style.setProperty('--control-fill', rgba(controls.controlColor, controls.controlOpa));
    // The icon, readable like on the device (the same with every tile color,
    // Circle strength and circle option).
    if (!given) return;
    const readable = toneReadableIcon(given);
    const readableHex = toneHex(readable);
    if (readableHex === givenHex) {
      if (icon.dataset.toneShown) {
        delete icon.dataset.toneShown;
        delete icon.dataset.toneGiven;
        icon.style.color = givenHex;
      }
    } else {
      icon.dataset.toneGiven = givenHex;
      icon.dataset.toneShown = readableHex;
      icon.style.color = readableHex;
    }
  }
  // Mirrors tileBgColorFollowsDefault(): an unset color and the built-in
  // default grey (stored explicitly by older editors) follow the global
  // default tile color; every other stored color is kept.
  // Built-in default greys: tile_color::kDefault, kLegacyDefault and
  // kPreviousDefault.
  function isDefaultTileGrey(rgb) {
    return rgb === 0x1A1A1A || rgb === 0x2A2A2A || rgb === 0x222222;
  }
  function tileBgFollowsDefault(value) {
    const num = Number(value);
    return !Number.isFinite(num) || num === 0 || isDefaultTileGrey(num & 0xFFFFFF);
  }
  function tileColorHexIsDefaultGrey(hex) {
    const text = String(hex || '').trim();
    return /^#[0-9a-f]{6}$/i.test(text) && isDefaultTileGrey(parseInt(text.slice(1), 16));
  }
  // Tile color is one choice, like the device (tile_tint::choose): Global
  // follows the global tile color (stored as the default marker), Custom keeps
  // the picked color, From icon color tints the tile with the color the icon
  // shows (the icon colors' hidden "fill" checkbox). Only tiles with icon
  // colors offer From icon color. Nothing else switches the choice.
  function tileColorMode(tab) {
    if (document.getElementById(tab + '_tile_icon_fill')?.checked) return 'icon';
    if (document.getElementById(tab + '_tile_cover_fill')?.checked) return 'cover';
    return document.getElementById(tab + '_tile_color')?.dataset.bgColorDefault === '0' ? 'custom' : 'global';
  }
  function syncTileColorMode(tab) {
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const iconOffered = typeof tileTypeHasIconColors === 'function' && tileTypeHasIconColors(typeValue);
    const fill = document.getElementById(tab + '_tile_icon_fill');
    if (fill?.checked && !iconOffered) fill.checked = false;
    // Media tiles also offer "From cover" (the album cover's color).
    const coverOffered = String(typeValue) === '15';
    const coverFill = document.getElementById(tab + '_tile_cover_fill');
    if (coverFill?.checked && !coverOffered) coverFill.checked = false;
    const mode = tileColorMode(tab);
    document.getElementById(tab + '_tile_color_modes')?.querySelectorAll('[data-tile-color-mode]').forEach(button => {
      const active = button.dataset.tileColorMode === mode;
      button.classList.toggle('active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
      if (button.dataset.tileColorMode === 'icon') button.classList.toggle('hidden', !iconOffered);
      if (button.dataset.tileColorMode === 'cover') button.classList.toggle('hidden', !coverOffered);
    });
    document.getElementById(tab + '_tile_color_row')?.classList.toggle('color-hidden', mode !== 'custom');
    document.getElementById(tab + '_tile_icon_fill_row')?.classList.toggle('hidden', mode !== 'icon' && mode !== 'cover');
    const strength = document.getElementById(tab + '_tile_icon_fill_strength');
    const output = document.getElementById(tab + '_tile_icon_fill_strength_value');
    if (strength && output) output.textContent = strength.value + ' %';
    if (typeof fitChoiceRows === 'function') fitChoiceRows(tab);
  }
  function setTileColorMode(tab, mode) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const before = tileColorMode(tab);
    // Leaving Custom remembers its color for a later return.
    if (before === 'custom' && mode !== 'custom') input.dataset.customColor = input.value;
    const fill = document.getElementById(tab + '_tile_icon_fill');
    if (fill) fill.checked = mode === 'icon';
    const coverFill = document.getElementById(tab + '_tile_cover_fill');
    if (coverFill) coverFill.checked = mode === 'cover';
    const remembered = input.dataset.customColor || '';
    if (mode === 'custom') {
      if (before !== 'custom' && remembered) input.value = remembered;
      input.dataset.bgColorDefault = '0';
    } else {
      const type = document.getElementById(tab + '_tile_type')?.value || '0';
      input.value = getTileTypeMeta(type).defaultBg || '#1A1A1A';
      input.dataset.bgColorDefault = '1';
    }
    syncTileColorMode(tab);
    // The rules hide "Tint tile" while the tile follows the icon.
    if (typeof syncIconColorFields === 'function') syncIconColorFields(tab);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
    // A first Custom opens the color picker right away. The color row was
    // display:none until syncTileColorMode above, and Chrome anchors the
    // picker to the input's box from the last layout without running one:
    // with no box yet it opened in the top left corner of the window. Reading
    // the input's rect lays the row out first.
    if (mode === 'custom' && before !== 'custom' && !remembered) {
      try {
        input.getBoundingClientRect();
        if (typeof input.showPicker === 'function') input.showPicker();
      } catch (_) {}
    }
  }
  // State the firmware compares with the per-tile icon color rules, or null
  // while it is missing, unknown or unavailable (the type color applies).
  function iconColorRuleState(typeValue, entity, meta, binaryState) {
    const type = String(typeValue ?? '0');
    // Icon-and-title tiles have no state; their fixed icon color applies.
    if (typeof tileTypeHasFixedIconColorOnly === 'function' &&
        tileTypeHasFixedIconColorOnly(type)) return { state: '', display: null };
    if (type === '20') {
      if (!binaryState?.valid || binaryState.available !== true ||
          !['on', 'off'].includes(binaryState.state)) return null;
      return { state: binaryState.state, display: binarySensorPreviewStateText(binaryState) };
    }
    if (['21', '22', '23'].includes(type)) {
      let value = meta?.editableValues?.[entity];
      if (typeof value === 'string') { try { value = JSON.parse(value); } catch (_) { return null; } }
      if (!value || value.state === null || value.state === undefined || !value.available ||
          ['unknown', 'unavailable'].includes(String(value.state))) return null;
      const kind = type === '21' ? 'number' : (type === '22' ? 'select' : 'datetime');
      return { state: String(value.state), display: editablePreviewText(entity, kind, meta) };
    }
    const raw = String(meta?.values?.[entity] ?? '').trim();
    if (!raw || ['unavailable', 'unknown', 'none', 'null', '--'].includes(raw.toLowerCase())) return null;
    return { state: raw, display: null };
  }
  // Icon color of a preview tile: the per-tile rule or fixed icon color
  // (resolveIconColorRecord, same result as the firmware), else `fallback`.
  function previewIconColor(typeValue, record, entity, meta, binaryState, fallback) {
    if (!record || typeof tileTypeHasIconColors !== 'function' ||
        !tileTypeHasIconColors(typeValue)) return fallback;
    const type = String(typeValue ?? '0');
    // Rules that color the icon win (tile_icon_source::refresh_card).
    const layer = typeof iconColorRecordSource === 'function' ? iconColorRecordSource(record) : null;
    if (layer && layer.enabled && layer.icon && typeof iconColorLayerColor === 'function') {
      const color = iconColorLayerColor(record, layer, entity, meta, type);
      if (color) return color;
    }
    // Only the Sensor family colors its icon from its own state; every other
    // case shows the fixed icon color, else the type's color.
    const ownRules = ['1', '14', '20', '21', '22', '23'].includes(type) &&
      typeof iconColorOwnStateColorsIcon === 'function' && iconColorOwnStateColorsIcon(record);
    if (!ownRules) return resolveIconColorRecord(record, '', null) || fallback;
    const rule = iconColorRuleState(typeValue, entity, meta, binaryState);
    return (rule && resolveIconColorRecord(record, rule.state, rule.display)) || fallback;
  }
  // Tints a preview tile like tile_icon_source.cpp ("Tint tile" rules): the
  // tint replaces the tile color and starts from the global default tile
  // color, never from an own tile color.
  function applyTileRulesTint(el, typeValue, record, ownEntity, meta) {
    if (!el) return;
    // The icon color's "Tint tile" option follows the icon (applyIconDiscTint)
    // from this untinted background, unless a rule tint wins.
    el.dataset.baseBg = el.style.background || '';
    const fill = record && typeof parseIconColorRecord === 'function' ? parseIconColorRecord(record).fill : 0;
    if (fill) el.dataset.iconFill = String(fill);
    else delete el.dataset.iconFill;
    const tint = record && typeof iconColorTilePreviewTint === 'function'
      ? iconColorTilePreviewTint(String(typeValue ?? '0'), record, ownEntity, meta) : null;
    el.dataset.ruleTint = tint ? '1' : '0';
    if (!tint) return;
    setTileTintBackground(el, tint.color, tint.percent);
  }
  function snapshotBgColorIsDefault(snapshot) {
    return String(snapshot?.bg_color_default || '0') === '1' ||
      tileColorHexIsDefaultGrey(snapshot?.color);
  }
  function tileColorInputIsDefault(tab) {
    const input = document.getElementById(tab + '_tile_color');
    return !!input && (input.dataset.bgColorDefault === '1' || tileColorHexIsDefaultGrey(input.value));
  }
  function setTileColorInputFromStored(tab, value, fallback) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const follows = tileBgFollowsDefault(value);
    input.value = follows ? (fallback || '#2A2A2A') : tileBgToHex(value, fallback || '#2A2A2A');
    input.dataset.bgColorDefault = follows ? '1' : '0';
    delete input.dataset.customColor;
    syncTileColorMode(tab);
  }
  function setTileColorInputFromSnapshot(tab, snapshot) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const meta = getTileTypeMeta(snapshot?.type || '0');
    const isDefault = snapshotBgColorIsDefault(snapshot);
    input.value = isDefault ? (meta.defaultBg || '#2A2A2A') : (snapshot?.color || meta.defaultBg || '#2A2A2A');
    input.dataset.bgColorDefault = isDefault ? '1' : '0';
    delete input.dataset.customColor;
    syncTileColorMode(tab);
  }
  // Picking a color selects Tile color Custom. Rules are never switched off:
  // while a rule "Tint tile" applies, it wins (tile_tint::choose).
  function markTileColorInputExplicit(tab) {
    const input = document.getElementById(tab + '_tile_color');
    if (input) input.dataset.bgColorDefault = '0';
    const fill = document.getElementById(tab + '_tile_icon_fill');
    const followed = !!fill?.checked;
    if (fill) fill.checked = false;
    syncTileColorMode(tab);
    if (followed && typeof syncIconColorFields === 'function') syncIconColorFields(tab);
  }
  function resetTileColor(tab) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const meta = getTileTypeMeta(typeValue);
    input.value = meta.defaultBg || '#2A2A2A';
    input.dataset.bgColorDefault = '1';
    const fill = document.getElementById(tab + '_tile_icon_fill');
    if (fill) fill.checked = false;
    syncTileColorMode(tab);
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
    if (typeValue === '0' && (!meta.css || meta.css !== 'empty')) cls.push('empty');
    el.className = cls.join(' ');
    if (typeValue === '5') applySwitchPreviewLayout(el, tile.switch_style, Number(tile.span_h) === 0.5);
    el.dataset.type = typeValue;
    el.dataset.iconDisc = ['1', '2'].includes(String(tile?.icon_disc)) ? String(tile.icon_disc) : '0';
    el.dataset.iconGlow = ['0', 'false'].includes(String(tile?.icon_glow)) ? '0' : '1';
    el.classList.toggle('tile-border-hidden', ['8','9','10'].includes(typeValue) && Number(tile.sensor_display_mode) === 1);
    applyCompactSensorPreview(el, typeValue, tile, tile.sensor_display_mode, tile.sensor_value_font);
    if (typeValue === '4') {
      el.dataset.navigateTarget = String(tile.navigate_target || 0);
      el.dataset.folderPinEnabled = tile.folder_pin_enabled === true ? '1' : '0';
    } else {
      delete el.dataset.navigateTarget;
      delete el.dataset.folderPinEnabled;
    }
    delete el.dataset.bgOpacity;
    if (typeValue === '0') el.style.background = 'transparent';
    else {
      const isDefaultBg = tileBgFollowsDefault(tile.bg_color);
      const bg = tileBackgroundCss(meta, isDefaultBg,
        tileBgToHex(tile.bg_color, meta.defaultBg || '#353535'));
      if (isScreensaverTileTab(tab)) {
        // One opacity for every screensaver tile (screensaver footer).
        const opacity = screensaverTileOpacity();
        el.style.background = tileBackgroundCss(meta, isDefaultBg,
          tileBgToHex(tile.bg_color, meta.defaultBg || '#353535'), opacity);
        el.dataset.bgOpacity = String(opacity);
        // A fully transparent card casts no shadow (apply_slot_tile_shadows).
        el.classList.toggle('screensaver-bg-clear', opacity === 0);
      } else {
        el.style.background = bg;
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
                          previewKind === 'device' || previewKind === 'camera')
        ? (tile.sensor_entity || '')
        : (typeValue === '2' ? (sensorMeta?.sceneEntities?.[tile.scene_alias] || '') : '');
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
      const deviceKind = previewKind === 'device' ? devicePreviewKind(typeValue) : '';
      let devicePreviewState = null;
      if (deviceKind) {
        devicePreviewState = parseDevicePreviewPayload(deviceDetailPayload(tile.sensor_entity || '', sensorMeta));
        if (!normalizeMdiIconName(rawIcon) && !isExplicitlyDisabledValue(rawIcon)) {
          iconName = devicePreviewIcon(deviceKind, devicePreviewState);
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
      const locked = typeof previewTileLocked === 'function' && previewTileLocked(typeValue, el);
      const lockIsIcon = locked && !iconName;
      if (lockIsIcon) iconName = 'lock';

      if (iconName) {
        const iconColor = previewIconColor(typeValue, tile.icon_colors, tile.sensor_entity || '',
          sensorMeta, binarySensorPreviewState, previewKind === 'climate'
            ? climatePreviewColor(climatePreviewState)
            : (previewKind === 'cover'
              ? coverPreviewColor(coverPreviewState)
              : (deviceKind
                ? devicePreviewColor(deviceKind, devicePreviewState)
                : (previewKind === 'binary_sensor'
                  ? binarySensorPreviewColor(binarySensorPreviewState)
                  : ''))));
        const iconStyle = iconColor ? ' style="color:' + escapeHtml(iconColor) + '"' : '';
        html += '<i class="mdi mdi-' + escapeHtml(iconName) + ' tile-icon"' + iconStyle + '>' +
          (locked && !lockIsIcon ? PREVIEW_LOCK_MARK : '') + '</i>';
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
        // Layout "with value" and half height: the value pair beside the
        // disc; half height has no mini fields.
        const climateHalf = Number(tile.span_h) === 0.5;
        const climateValue = Number(tile.sensor_display_mode) === 1;
        el.classList.toggle('climate-header', climateValue && !climateHalf);
        if (climateHalf || climateValue) {
          html += '<div class="tile-value tile-switch-state">' +
            escapeHtml(climatePreviewHeaderText(climatePreviewState)) + '</div>';
        }
        if (!climateHalf) {
          html += climatePreviewSlots(
            climatePreviewState,
            tile.span_w || 1,
            tile.span_h || 1,
            decodeClimateSlotConfig(tile.sensor_gauge_min || 0),
            decodeClimateTargetLayouts(tile.sensor_gauge_max || 0),
            tile.climate_geometry || tile.scene_alias || '',
            climateValue);
        }
      }
      if (previewKind === 'cover') html += coverPreviewExtraHtml(coverPreviewState, Number(tile.span_h) === 0.5);
      if (deviceKind) html += devicePreviewExtraHtml(deviceKind, devicePreviewState, Number(tile.span_h) === 0.5);
      if (previewKind === 'binary_sensor') {
        html += '<div class="tile-value tile-binary-sensor-value ' + (Number(tile.sensor_value_font) ? sensorValueClass : '') + '" id="' +
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
        if (flags & 2) html += '<div class="tile-clock-date" ' + getClockPreviewTextStyle(clockDateFont, 20, '#fff') + '>' + getClockPreviewDate(clockDateFormat) + '</div>';
      }
      if (previewKind === 'text') {
        const textValue = tile.text_value || tile.scene_alias || tile.key_macro || '';
        if (textValue) {
          const textClass = getSensorValueFontClass(tile.sensor_value_font);
          html += '<div class="tile-text ' + textClass + '">' +
            escapeHtml(textValue) + '</div>';
        }
      }
      if (previewKind === 'switch') html += switchPreviewExtraHtml(tile.switch_style, Number(tile.span_h) === 0.5);
      html += getTileResizeHandlesHtml(typeValue);
      // A media tile's shown artwork survives the re-render; applyMediaPreview
      // keeps it when it is still the same picture.
      const keptCover = previewKind === 'media'
        ? el.querySelector(':scope > .media-preview-cover:not([hidden])') : null;
      el.innerHTML = html;
      if (keptCover) el.appendChild(keptCover);
      if (previewKind === 'weather') {
        applyWeatherPreview(el, parseWeatherPreviewPayload(
          tile.sensor_entity ? (sensorMeta?.weatherValues?.[tile.sensor_entity] ?? '') : ''),
          tile, iconName, previewIconColor(typeValue, tile.icon_colors, tile.sensor_entity || '', sensorMeta, null, ''));
      }
      if (previewKind === 'media') {
        applyMediaPreview(el, parseMediaPreviewPayload(
          tile.sensor_entity ? (sensorMeta?.mediaValues?.[tile.sensor_entity] ?? '') : ''),
          tile, iconName, tile.sensor_entity ? (metaNames[tile.sensor_entity] || '') : '');
      }
      if (typeof applyTileRulesTint === 'function') {
        applyTileRulesTint(el, typeValue, tile.icon_colors, tile.sensor_entity || '', sensorMeta);
      }
      if (previewKind === 'media') {
        applyMediaCoverTint(el, tile.icon_colors, sensorMeta?.mediaCoverColors?.[tile.sensor_entity || ''] || '');
      }
      applyIconDiscTint(el);
      if (previewKind === 'cover') {
        applyCoverPreview(el, coverPreviewState, Number(tile.span_h) === 0.5);
        // The header classes need the bar class set above (switch-tall).
        applyCompactSensorPreview(el, typeValue, tile, tile.sensor_display_mode, tile.sensor_value_font);
      }
      if (deviceKind) {
        applyDevicePreview(el, deviceKind, devicePreviewState, Number(tile.span_h) === 0.5);
        applyCompactSensorPreview(el, typeValue, tile, tile.sensor_display_mode, tile.sensor_value_font);
      }
      if (typeValue === '9') {
        // A title or icon moves the clock down (clock/renderer.cpp).
        el.classList.toggle('clock-has-header', !!(displayTitle || iconName));
        fitCompactClockPreview(el);
      }
    }
    if (currentTileTab === tab && currentTileIndex === index) el.classList.add('active');
    if (typeValue === '5' && tile.sensor_entity) {
      const state = parseSwitchPayload(metaValues[tile.sensor_entity] ?? '');
      applySwitchPreviewState(el, state, tile.sensor_entity);
    }
  }

  function fetchTileGridData(tab, force = false) {
    if (!force && tileDataLoadedTabs.has(tab)) {
      return Promise.resolve(getTilesData(tab));
    }
    if (tileDataLoadPromises[tab]) return tileDataLoadPromises[tab];
    const folderId = getFolderIdForTab(tab);
    if (folderId === undefined) return Promise.resolve([]);

    const baseline = getTilesData(tab).map(tile => JSON.stringify(tile));
    tileDataLoadPromises[tab] = fetch(
      '/api/tiles?folder=' + encodeURIComponent(folderId))
      .then(async response => {
        if (!response.ok) throw new Error('Tiles HTTP ' + response.status);
        const tiles = await response.json();
        if (!Array.isArray(tiles)) throw new Error('Invalid tile grid response');
        const current = getTilesData(tab);
        tilesData[tab] = tiles.map((tile, index) => {
          const changed = JSON.stringify(current[index]) !== baseline[index];
          return current[index] && (changed || drafts[tab]?.[index]?._dirty)
            ? current[index] : tile;
        });
        tileDataLoadedTabs.add(tab);
        return tilesData[tab];
      })
      .finally(() => { delete tileDataLoadPromises[tab]; });
    return tileDataLoadPromises[tab];
  }

  function loadSensorValues(
      refreshTiles = false, forceMetaFetch = false, tabsOverride = null) {
    // The layout window's tiles are not the panel's: values only.
    if (typeof layoutWindowOpen === 'function' && layoutWindowOpen()) refreshTiles = false;
    // A Settings move to or from the parking slot shows ahead of the device
    // (previewSettingsTileTransfer); stored tile data would draw it back.
    if (dragSource || resizeState || settingsTileTransfersInFlight) {
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
      if (dragSource || resizeState || settingsTileTransfersInFlight) {
        queueDeferredSensorRefresh(refreshTiles);
        return;
      }
      const sensorMeta = normalizeSensorMetaPayload(results[0] || {});
      sensorMetaCache = sensorMeta;
      tabs.forEach((tab, idx) => {
        // Metadata may finish after another edit; render the current cache.
        const tilesForRender = getTilesData(tab);
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
    // No second element with the tile's id (getElementById, selection).
    clone.removeAttribute('id');
    const rect = tile.getBoundingClientRect();
    // The tile's own display: a forced block dropped the flex centering of
    // Folder, Settings and Switch tiles, so the drag image showed icon and
    // disc at the top left (user 2026-10-02).
    const display = getComputedStyle(tile).display;
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
    clone.style.display = display === 'none' ? 'block' : display;
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
    return getGridElementMetrics(grid, placeCols(tab), placeRows(tab));
  }

  function getRawGridCellFromPointer(tab, clientX, clientY, sizeStep = null) {
    const metrics = getTileGridMetrics(tab);
    if (!metrics) return null;
    const stepX = metrics.cellW + metrics.gapX;
    const stepY = metrics.cellH + metrics.gapY;
    let relX = clientX - metrics.rect.left - metrics.padLeft;
    let relY = clientY - metrics.rect.top - metrics.padTop;
    if (!isFinite(relX) || !isFinite(relY)) return null;
    relX = Math.max(0, relX);
    relY = Math.max(0, relY);
    const unit = sizeStep ?? 0.5;
    let col = Math.floor((relX + (metrics.gapX / 2)) / (stepX * unit)) * unit;
    let row = Math.floor((relY + (metrics.gapY / 2)) / (stepY * unit)) * unit;
    if (!isFinite(col)) col = 0;
    if (!isFinite(row)) row = 0;
    if (col < 0) col = 0;
    const firstRow = firstAllowedGridRow(tab);
    if (row < firstRow) row = firstRow;
    if (col >= placeCols(tab)) col = placeCols(tab) - unit;
    if (row >= placeRows(tab)) row = placeRows(tab) - unit;
    return { col, row };
  }

  function getGridCellFromPointer(tab, clientX, clientY) {
    if (dragSource && dragSource.tab === tab && dragSource.dropOffset) {
      // The drop spot is the half cell nearest to the drag image's top-left
      // corner, so it always lies under the dragged tile; the half cell under
      // the pointer put it up to a half cell off (user 2026-10-02).
      const metrics = getTileGridMetrics(tab);
      if (!metrics) return null;
      const halfX = (metrics.cellW + metrics.gapX) / 2;
      const halfY = (metrics.cellH + metrics.gapY) / 2;
      const left = clientX - dragSource.dropOffset.x - metrics.rect.left - metrics.padLeft;
      const top = clientY - dragSource.dropOffset.y - metrics.rect.top - metrics.padTop;
      if (!isFinite(left) || !isFinite(top) || !(halfX > 0) || !(halfY > 0)) return null;
      const layout = getDragSourceLayout();
      return {
        col: Math.max(0, Math.min(placeCols(tab) - (layout?.span_w || 0.5), Math.round(left / halfX) / 2)),
        row: Math.max(firstAllowedGridRow(tab),
                      Math.min(placeRows(tab) - (layout?.span_h || 0.5), Math.round(top / halfY) / 2))
      };
    }
    const rawCell = getRawGridCellFromPointer(tab, clientX, clientY);
    if (!rawCell) return null;
    if (!dragSource || dragSource.tab !== tab) return rawCell;
    const anchorCol = clampHalf(dragSource.grabCellCol, 0, GRID_COLS - 1, 0);
    const anchorRow = clampHalf(dragSource.grabCellRow, 0, GRID_ROWS - 1, 0);
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

  // The pointer's offset from the tile's grid corner, measured in the grid
  // frame (getTileGridMetrics) that the drop spot is snapped in.
  function getDragLayoutOffset(tab, layout, clientX, clientY) {
    const metrics = getTileGridMetrics(tab);
    if (!metrics || !layout) return null;
    return {
      x: clientX - metrics.rect.left - metrics.padLeft - layout.col * (metrics.cellW + metrics.gapX),
      y: clientY - metrics.rect.top - metrics.padTop - layout.row * (metrics.cellH + metrics.gapY)
    };
  }

  function getDragAnchorCell(tab, layout, clientX, clientY) {
    const rawCell = getRawGridCellFromPointer(tab, clientX, clientY);
    if (!layout || !rawCell) return { col: 0, row: 0 };
    const unit = 0.5;
    const col = clampHalf(rawCell.col - layout.col, 0, Math.max(0, layout.span_w - unit), 0);
    const row = clampHalf(rawCell.row - layout.row, 0, Math.max(0, layout.span_h - unit), 0);
    return { col, row };
  }

  // Where the pointer took the tile: the drag image stays exactly under the
  // pointer. Centering it on the grabbed half cell moved it by up to a quarter
  // tile (user 2026-10-02: a dragged tile sat slightly off).
  function getDragGrabOffset(tileRect, clientX, clientY) {
    const rect = tileRect || { left: 0, top: 0, width: 0, height: 0 };
    return {
      x: Math.max(0, Math.min(Math.max(0, rect.width - 1), clientX - rect.left)),
      y: Math.max(0, Math.min(Math.max(0, rect.height - 1), clientY - rect.top))
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
      if (!tileTakesRoom(tile)) continue;
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
      if (!tileTakesRoom(tile)) continue;
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
        candidateLayout.span_w < 0.5 ||
        candidateLayout.span_h < 0.5 ||
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
    const type = tab === currentTileTab && index === currentTileIndex
      ? document.getElementById(tab + '_tile_type')?.value ?? tiles[index]?.type : tiles[index]?.type;
    if (Number(type) !== 0 && index >= 0 && !supportedTileLayout(type, candidateLayout)) return false;
    const layouts = tiles.map((tile, tileIndex) =>
      getTileElementLayout(tab, tileIndex) ||
      getTileLayoutFromData(tab, tileIndex));
    const active = new Set();
    tiles.forEach((tile, tileIndex) => {
      if (tileTakesRoom(tile)) active.add(tileIndex);
    });
    return canPlaceGridLayout(
      layouts, active, index, candidateLayout,
      placeCols(tab), placeRows(tab), firstAllowedGridRow(tab));
  }

  function canPlaceHiddenSettingsLayout(tab, candidateLayout) {
    if (!supportedTileLayout(7, candidateLayout)) return false;
    if (tileDataLoadedTabs.has(tab)) {
      return canPlaceTileLayout(tab, -1, candidateLayout);
    }
    const layouts = [];
    const active = new Set();
    document.querySelectorAll('#tab-tiles-' + tab + ' .tile').forEach(tile => {
      const index = Number(tile.dataset.index);
      if (!Number.isInteger(index) || Number(tile.dataset.type || 0) === 0 ||
          tile.classList.contains('tile-unplaced')) return;
      const layout = getTileElementLayout(tab, index);
      if (!layout) return;
      layouts[index] = layout;
      active.add(index);
    });
    return canPlaceGridLayout(
      layouts, active, -1, candidateLayout,
      placeCols(tab), placeRows(tab), firstAllowedGridRow(tab));
  }

  function manhattanDistance(colA, rowA, colB, rowB) {
    return Math.abs(colA - colB) + Math.abs(rowA - rowB);
  }

  function buildGridPlacementCandidates(
      columns, rows, firstRow,
      spanW, spanH, preferredCol, preferredRow, step = 1) {
    const candidates = [];
    for (let row = firstRow; row < rows; row += step) {
      for (let col = 0; col < columns; col += step) {
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
      placeCols(tab), placeRows(tab), firstAllowedGridRow(tab),
      spanW, spanH, preferredCol, preferredRow);
  }

  function simulateGridReorderLayouts(
      baseLayouts, activeIndices, fromIdx,
      targetCol, targetRow, columns, rows, firstRow = 0, tileTypes = []) {
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
    const fractional = [targetLayout, ...baseLayouts.filter((_, i) => active.has(i))]
      .some(layout => layout && [layout.col, layout.row, layout.span_w, layout.span_h].some(v => !Number.isInteger(v)));
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
        preferredCol, preferredRow,
        fractional ? 0.5 : 1);
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
      if (tileTakesRoom(tile)) active.add(index);
    });
    return simulateGridReorderLayouts(
      baseLayouts, active, fromIdx,
      targetCol, targetRow,
      placeCols(tab), placeRows(tab), firstAllowedGridRow(tab), tiles.map(tile => tile?.type));
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
    const data = getTilesData(tab)?.[resizeState?.index];
    // A Switch tile gains or loses its bar between half and full height.
    const isSwitch = Number(data?.type) === 5;
    if (isSwitch) prepareSwitchResizePreview(preview, data, layout);
    applyCompactSensorPreview(preview, data?.type, layout, data?.sensor_display_mode, data?.sensor_value_font);
    placeholder.replaceChildren(preview);
    if (isSwitch) finishSwitchResizePreview(preview, data);
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
    if (!layout) return null;

    let spanW = layout.span_w;
    let spanH = layout.span_h;
    const tiles = getTilesData(tab);
    const tile = Array.isArray(tiles) && currentTileIndex >= 0
      ? tiles[currentTileIndex] : null;
    const typeValue = document.getElementById(tab + '_tile_type')?.value ?? tile?.type ?? 0;
    const isMedia = Number(typeValue) === MEDIA_TILE_TYPE;
    const minW = isMedia ? Math.min(MEDIA_TILE_MIN_SPAN, placeCols(tab)) : 1;
    const unit = 0.5;
    const snap = clampHalf;
    const rawCell = getRawGridCellFromPointer(tab, clientX, clientY, unit);
    if (!rawCell) return null;
    const minH = isMedia ? Math.min(MEDIA_TILE_MIN_SPAN, placeRows(tab)) : (supportsHalfSize(typeValue) ? 0.5 : 1);
    const maxW = isMedia
      ? Math.min(MEDIA_TILE_MAX_SPAN, placeCols(tab) - layout.col)
      : placeCols(tab) - layout.col;
    const maxH = isMedia
      ? Math.min(MEDIA_TILE_MAX_SPAN, placeRows(tab) - layout.row)
      : placeRows(tab) - layout.row;
    if (String(direction || '').includes('s')) {
      spanH = snap(rawCell.row - layout.row + unit, minH, maxH, layout.span_h);
    }
    if (String(direction || '').includes('e')) {
      spanW = snap(rawCell.col - layout.col + unit, minW, maxW, layout.span_w);
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
    // The hidden card's target shows at once: before the first pointer move
    // the tile was simply gone (user 2026-10-02).
    updateResizePlaceholder(tab, layout, true);
    window.addEventListener('pointermove', handleTileResizeMove);
    window.addEventListener('pointerup', handleTileResizeEnd);
    window.addEventListener('pointercancel', handleTileResizeCancel);
  }

  function updateDragPlaceholder(tab, col, row) {
    if (!dragSource || dragSource.tab !== tab) return;
    const sourceLayout = getDragSourceLayout();
    const placeholder = ensureDragPlaceholder(tab);
    if (!sourceLayout || !placeholder) return;

    const targetCol = clampHalf(col, 0, placeCols(tab) - 0.5, sourceLayout.col);
    const targetRow = clampHalf(row, firstAllowedGridRow(tab), placeRows(tab) - 0.5, sourceLayout.row);
    const fits = (targetCol + sourceLayout.span_w <= placeCols(tab)) &&
                 (targetRow + sourceLayout.span_h <= placeRows(tab));
    const spanW = Math.max(1, Math.min(sourceLayout.span_w, placeCols(tab) - targetCol));
    const spanH = Math.max(0.5, Math.min(sourceLayout.span_h, placeRows(tab) - targetRow));

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
    const targetCol = clampHalf(cell.col, 0, placeCols(tab) - 0.5, sourceLayout.col);
    const targetRow = clampHalf(cell.row, firstAllowedGridRow(tab), placeRows(tab) - 0.5, sourceLayout.row);
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
    const targetCol = clampHalf(cell.col, 0, placeCols(tab) - 0.5, sourceLayout.col);
    const targetRow = clampHalf(cell.row, firstAllowedGridRow(tab), placeRows(tab) - 0.5, sourceLayout.row);
    const fits = (targetCol + sourceLayout.span_w <= placeCols(tab)) &&
                 (targetRow + sourceLayout.span_h <= placeRows(tab));

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
      if (!tileTakesRoom(tile) || !layout) continue;
      const changed = tile.col !== layout.col || tile.row !== layout.row;
      tile.col = layout.col;
      tile.row = layout.row;
      const draft = drafts[tab]?.[i];
      if (changed && draft?._dirty) {
        draft.col = String(layout.col + 1);
        draft.row = String(layout.row + 1);
        draft._rev = Number(draft._rev || 0) + 1;
      }
    }
    persistDrafts();

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
      const changed = tile.col !== saved.col || tile.row !== saved.row;
      tile.col = saved.col;
      tile.row = saved.row;
      const draft = drafts[tab]?.[i];
      if (changed && draft?._dirty) {
        draft.col = String(saved.col + 1);
        draft.row = String(saved.row + 1);
        draft._rev = Number(draft._rev || 0) + 1;
      }
    }
    persistDrafts();
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
        const grabOffset = getDragGrabOffset(tile.getBoundingClientRect(), e.clientX, e.clientY);
        dragSource = {
          kind: 'grid-tile',
          tab,
          index: tileIndex,
          type: Number(tile.dataset.type || 0),
          layout,
          baseLayouts: captureLayoutSnapshot(tab),
          grabCellCol: anchorCell.col,
          grabCellRow: anchorCell.row,
          dropOffset: getDragLayoutOffset(tab, layout, e.clientX, e.clientY) || grabOffset,
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
    // A pending edit needs no save of its own: the parking save carries the
    // tile's snapshot with it, and an extra save made the device write and
    // rebuild its grid twice. Only a save already on its way must land first.
    if (autoSaveTimers[timerKey]) {
      clearTimeout(autoSaveTimers[timerKey]);
      delete autoSaveTimers[timerKey];
    }
    clearDraft(tab, index);
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

  // Moves of the Settings tile between the grid and the parking slot show at
  // once and queue their saves (queueSettingsAccessSave keeps the order), so
  // a move made while the device still saves the previous one is not lost
  // (user 2026-10-02). Only the latest move reconciles with the device, and
  // only when the device put the tile elsewhere than the preview: reloading
  // the Home grid made the panel read every folder it links to.
  function settingsTransferMatches(saved, index) {
    return saved && Number(saved.settings_tile_index) === index;
  }

  async function hideSettingsTileFromGrid() {
    const hidden = settingsAccessElement('settings_tile_hidden');
    const swipe = settingsAccessElement('settings_swipe_enabled');
    if (!hidden) return false;
    const settingsTile = (getTilesData('folder0') || []).findIndex(
      tile => Number(tile?.type || 0) === 7);
    if (settingsTile < 0) {
      return false;
    }
    const snapshot = normalizeHiddenSettingsSnapshot(
      getTileSnapshotForSave('folder0', settingsTile) ||
      currentGridSettingsSnapshot());
    const transfer = ++settingsTileTransferSeq;
    settingsTileTransfersInFlight++;
    try {
      previewSettingsTileTransfer(true, snapshot);
      if (!(await flushSettingsTileSaveBeforeHide('folder0', settingsTile))) {
        if (transfer === settingsTileTransferSeq) await reconcileSettingsTileUi(false);
        return false;
      }
      hidden.checked = true;
      if (swipe) swipe.checked = true;
      toggleSettingsAccessFields();
      const saved = await queueSettingsAccessSave(
        null, null, snapshot, false);
      if (transfer !== settingsTileTransferSeq) return saved;
      if (!saved) {
        await reconcileSettingsTileUi(false);
        return false;
      }
      if (settingsTransferMatches(saved, -1)) return true;
      return await reconcileSettingsTileUi(true, snapshot);
    } finally {
      settingsTileTransfersInFlight--;
      flushDeferredSensorRefresh();
    }
  }

  async function restoreHiddenSettingsTile(col, row) {
    const hidden = settingsAccessElement('settings_tile_hidden');
    if (!hidden) return false;
    const snapshot = normalizeHiddenSettingsSnapshot();
    const transfer = ++settingsTileTransferSeq;
    settingsTileTransfersInFlight++;
    try {
      // The Settings checkbox restores without a drop spot; the device then
      // picks the spot and the reload shows it.
      const shownAt = Number.isFinite(col) && Number.isFinite(row)
        ? previewSettingsTileTransfer(false, snapshot, {col, row})
        : -1;
      hidden.checked = false;
      toggleSettingsAccessFields();
      const saved = await queueSettingsAccessSave(
        null, {col, row}, null, false);
      if (transfer !== settingsTileTransferSeq) return saved;
      if (!saved) {
        await reconcileSettingsTileUi(true, snapshot);
        return false;
      }
      if (shownAt >= 0 && settingsTransferMatches(saved, shownAt)) return true;
      return await reconcileSettingsTileUi(false, snapshot, true);
    } finally {
      settingsTileTransfersInFlight--;
      flushDeferredSensorRefresh();
    }
  }

  function enableSettingsHiddenSlot() {
    const slot = document.getElementById('settingsHiddenSlot');
    const hiddenTile = document.getElementById('settingsHiddenTile');
    const hint = document.getElementById('settingsHiddenHint');
    if (!slot || !hiddenTile || slot.dataset.bound === '1') return;
    slot.dataset.bound = '1';
    hiddenTile.addEventListener('click', () => selectHiddenSettingsTile());

    const acceptsGridSettings = () => {
      if (!dragSource || dragSource.kind !== 'grid-tile' ||
          dragSource.tab !== 'folder0') return false;
      const tile = getTilesData('folder0')?.[dragSource.index];
      return Number(dragSource.type || tile?.type || 0) === 7;
    };
    // The slot is one more drop cell of the Settings tile and works like a
    // grid cell (user 2026-10-02): with the pointer over it the teal
    // placeholder shows in the slot and the grid's placeholder and reflow
    // preview go back; a parked tile dropped on it stays parked.
    const draggingSettings = () => acceptsGridSettings() ||
      (dragSource?.kind === 'hidden-settings' && dragSource.tab === 'folder0');
    slot.addEventListener('dragover', event => {
      if (!draggingSettings()) return;
      event.preventDefault();
      event.dataTransfer.dropEffect = 'move';
      if (slot.classList.contains('drop-target')) return;
      restoreDragPreview('folder0');
      clearDragPlaceholder();
      if (dragSource.kind === 'hidden-settings') dragSource.hiddenTarget = null;
      slot.classList.add('drop-target');
    });
    slot.addEventListener('dragleave', event => {
      if (event.relatedTarget instanceof Node &&
          slot.contains(event.relatedTarget)) return;
      slot.classList.remove('drop-target');
    });
    slot.addEventListener('drop', event => {
      if (!draggingSettings()) return;
      event.preventDefault();
      event.stopPropagation();
      slot.classList.remove('drop-target');
      restoreDragPreview('folder0');
      clearDragPlaceholder();
      if (dragSource.kind === 'hidden-settings') return;
      dragSource.dropCommitted = true;
      hideSettingsTileFromGrid();
    });

    hiddenTile.addEventListener('dragstart', event => {
      if (hiddenTile.dataset.hidden !== '1') {
        event.preventDefault();
        return;
      }
      // Taking the parked tile selects it like a grid tile, so the drag image
      // carries the teal selection.
      if (currentTileTab !== 'folder0' || currentTileIndex !== HIDDEN_SETTINGS_TILE_INDEX) {
        selectHiddenSettingsTile();
      }
      const spanW = clampHalf(hiddenTile.dataset.spanW, 1, GRID_COLS, 1);
      const spanH = clampHalf(hiddenTile.dataset.spanH, 0.5, GRID_ROWS, 1);
      // The slot shows one cell: the grabbed half of it anchors the drop like
      // a grid tile, and the drag image stays where the pointer took it.
      const rect = hiddenTile.getBoundingClientRect();
      const grabOffset = getDragGrabOffset(rect, event.clientX, event.clientY);
      const grabCellCol = spanW > 0.5 && grabOffset.x >= rect.width / 2 ? 0.5 : 0;
      const grabCellRow = spanH > 0.5 && grabOffset.y >= rect.height / 2 ? 0.5 : 0;
      dragSource = {
        kind: 'hidden-settings',
        tab: 'folder0',
        index: -1,
        layout: {col: 0, row: 0, span_w: spanW, span_h: spanH},
        grabCellCol,
        grabCellRow,
        dropOffset: grabOffset,
        baseLayouts: null,
        dropCommitted: false,
        hiddenTarget: null
      };
      event.dataTransfer.effectAllowed = 'move';
      hiddenTile.classList.add('dragging');
      if (event.dataTransfer.setDragImage) {
        dragPreview = createDragPreview(hiddenTile);
        event.dataTransfer.setDragImage(dragPreview, grabOffset.x, grabOffset.y);
      }
      // The slot it left is the empty slot at once: tray icon and hint.
      slot.classList.add('lifting');
      hint?.classList.remove('is-hidden');
    });
    hiddenTile.addEventListener('dragend', () => {
      hiddenTile.classList.remove('dragging');
      slot.classList.remove('drop-target', 'invalid', 'lifting');
      hint?.classList.toggle('is-hidden', hiddenTile.dataset.hidden === '1');
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
    let col = Number(targetCol);
    let row = Number(targetRow);
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
    // The layout window keeps the editor's places to itself (layout-window.js).
    if (typeof layoutWindowOpen === 'function' && layoutWindowOpen()) {
      clearDeferredSensorRefresh();
      return;
    }
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
      if (isNaN(index) || Number(tile.dataset.type || 0) === 0 ||
          tile.classList.contains('tile-unplaced')) return;
      const row = Number(tile.dataset.row);
      const col = Number(tile.dataset.col);
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
  // The layout window ("Layout ändern", grid_layout.h / tile_layouts.h). It
  // holds the page's real tile grid and editor, laid out for the chosen
  // layout in one area for all three: the layout's screen and, beside it, its
  // storage ("Ablage"): tiles there are no part of the layout but keep their
  // spot. A tile half over the screen's edge is red. Each layout keeps its
  // own arrangement: "Speichern" stores it, "Umstellen" stores it and
  // restarts the panel with it; switching between layouts keeps the unsaved
  // ones. Nothing else reaches the panel while the window is open: the
  // editor's own saves stay in the window.
  const LAYOUT_KEYS = ['classic', 'bar', 'portrait'];
  let layoutWindow = null;
  const layoutWindowFetch = window.fetch.bind(window);
  let layoutBaseSizes = null;  // every px size of the page's preview (:root)
  let layoutHead = null;       // the head's places on the page's screen

  function layoutWindowOpen() { return !!layoutWindow; }
  function layoutPx(value) { return value.toFixed(2) + 'px'; }
  function layoutClone(value) { return JSON.parse(JSON.stringify(value)); }
  function layoutEmptyTile() { return {type: 0, title: '', icon_name: '', col: 0, row: 0, span_w: 1, span_h: 1}; }
  function layoutUsed(tile) { return !!tile && Number(tile.type || 0) !== 0; }
  function layoutNavType(type) { return [7, 8].includes(Number(type)); }
  // The bar layouts' head has the X: no Back tile there. The Settings tile
  // is a tile like any other (user 2026-10-08).
  function layoutBarHidden(type) { return Number(type) === 8; }
  function layoutRootPx(name) {
    return parseFloat(getComputedStyle(document.documentElement).getPropertyValue(name)) || 0;
  }
  // Preview px per screen px (web_admin_styles.cpp), the same for every layout.
  function layoutScale() { return layoutRootPx('--radius-preview-scale') || 1; }
  function layoutTabs() {
    return tileTabs.filter(tab => !isScreensaverTileTab(tab) && document.getElementById('tab-tiles-' + tab));
  }
  function layoutSize(key) { return LAYOUTS[key].cols + ' × ' + String(LAYOUTS[key].rows).replace('.', ','); }

  // The window's editor area: the same for every layout. Every layout's
  // screen starts right under the head, in the middle of the area (user
  // 2026-10-08), with the storage on both sides.
  function layoutCanvas() {
    const keys = LAYOUT_KEYS.filter(key => LAYOUTS[key].available);
    return {cols: Math.max(...keys.map(key => LAYOUTS[key].cols)),
            rows: Math.ceil(Math.max(...keys.map(key => LAYOUTS[key].rows)))};
  }
  // Where a layout's screen starts in the area's columns (half steps): its
  // places are stored from its own first column.
  function layoutOffsetX(key) { return Math.floor(layoutCanvas().cols - LAYOUTS[key].cols) / 2; }
  function layoutInside(tile, key) {
    const L = LAYOUTS[key], x = layoutOffsetX(key);
    return tile.col >= x - 1e-6 && tile.col + tile.span_w <= x + L.cols + 1e-6 &&
      tile.row >= -1e-6 && tile.row + tile.span_h <= L.rows + 1e-6;
  }
  // The head on the layout's screen: the X and the time keep their distance
  // to its right edge.
  function layoutHeadVars(L) {
    const w = L.screenW * layoutScale();
    const shift = w - layoutHead.w;
    return {'--head-screen-w': layoutPx(w), '--head-close-x': layoutPx(layoutHead.close + shift),
      '--head-time-x': layoutPx(layoutHead.time + shift), '--head-title-w': layoutPx(layoutHead.title + shift)};
  }
  function layoutSetVars(el, vars) { for (const [name, value] of Object.entries(vars)) el.style.setProperty(name, value); }

  // Every layout's own preview sizes (cells, circles, text places): the
  // page's for the active layout, .layout-vars-<key> for the others
  // (web_admin_styles.cpp). Read at every opening: the page's sizes change
  // with the global options (the tile radius slider).
  function readLayoutBaseSizes() {
    layoutBaseSizes = {};
    const rootStyle = getComputedStyle(document.documentElement);
    const px = value => /^\s*-?[\d.]+px\s*$/.test(value) ? parseFloat(value) : null;
    for (const key of LAYOUT_KEYS) layoutBaseSizes[key] = {};
    for (const sheet of document.styleSheets) {
      let rules;
      try { rules = sheet.cssRules; } catch (e) { continue; }
      for (const rule of rules) {
        const key = rule.selectorText === ':root' ? ACTIVE_LAYOUT
          : LAYOUT_KEYS.find(k => rule.selectorText === '.layout-vars-' + k);
        if (!key || !rule.style) continue;
        for (const name of rule.style) {
          if (!name.startsWith('--')) continue;
          // The page's values as they are now (the radius slider sets some).
          const raw = key === ACTIVE_LAYOUT ? rootStyle.getPropertyValue(name) : rule.style.getPropertyValue(name);
          const value = name === '--radius-preview-scale' ? parseFloat(raw) : px(raw);
          if (value !== null && !Number.isNaN(value)) layoutBaseSizes[key][name] = value;
        }
      }
    }
    // The global options live on the page only (the radius slider's last
    // value). A page without a layout's own sizes takes the page's with that
    // layout's cells and margins.
    const page = layoutBaseSizes[ACTIVE_LAYOUT];
    for (const key of LAYOUT_KEYS) {
      if (key === ACTIVE_LAYOUT) continue;
      if (!Object.keys(layoutBaseSizes[key]).length) {
        const L = LAYOUTS[key], s = layoutScale();
        layoutBaseSizes[key] = {...page, '--preview-cell-w': L.cellW * s, '--preview-cell-h': L.cellH * s,
          '--preview-pad-left': L.padLeft * s, '--preview-pad-right': L.padRight * s,
          '--preview-pad-top': L.padTop * s, '--preview-pad-bottom': L.padBottom * s};
      }
      if (page['--tile-radius'] !== undefined) layoutBaseSizes[key]['--tile-radius'] = page['--tile-radius'];
    }
    layoutHead = {w: layoutRootPx('--head-screen-w'), close: layoutRootPx('--head-close-x'),
      time: layoutRootPx('--head-time-x'), title: layoutRootPx('--head-title-w')};
  }

  // --- Places ------------------------------------------------------------

  function layoutFolderId(tab) { return String(getFolderIdForTab(tab)); }
  // A tile without a classic place, and its spot in the classic storage
  // (none yet: [0, 0, 0, 0]).
  function classicHidden(tab, view) {
    return !!layoutWindow.data.places?.classic_parked?.[layoutFolderId(tab)]?.[view];
  }
  function classicParked(tab, view) {
    const p = layoutWindow.data.places?.classic_parked?.[layoutFolderId(tab)]?.[view];
    return p && p[2] > 0 && p[3] > 0
      ? {col: p[0] + layoutOffsetX('classic'), row: p[1], span_w: p[2], span_h: p[3]} : null;
  }
  // Wholly beside the layout's screen: in its storage.
  function layoutParked(tile, key) {
    const L = LAYOUTS[key], x = layoutOffsetX(key);
    return tile.col >= x + L.cols - 1e-6 || tile.row >= L.rows - 1e-6 ||
      tile.col + tile.span_w <= x + 1e-6 || tile.row + tile.span_h <= 1e-6;
  }
  // The page's tiles with their classic places.
  function layoutBase(tab) {
    const classic = {};
    (layoutWindow.data.classic?.[layoutFolderId(tab)] || []).forEach(([slot, view, col, row, w, h]) => {
      classic[slot] = {view, col, row, span_w: w, span_h: h};
    });
    return layoutClone(layoutWindow.pages[tab] || []).map((tile, index) => {
      if (!layoutUsed(tile)) return tile;
      delete tile.layout_hidden;
      const p = classic[index];
      if (p && Number(p.view) === Number(tile.view_id)) {
        Object.assign(tile, {col: p.col + layoutOffsetX('classic'), row: p.row, span_w: p.span_w, span_h: p.span_h});
      } else {
        tile._unplaced = true;
      }
      return tile;
    });
  }

  // Every tile on its own spot of the window's area: tiles that need a place
  // (none in this layout, or one taken) go to a free spot in the storage
  // beside the layout's screen first.
  function layoutSettle(key, tiles) {
    const canvas = layoutCanvas();
    const cols2 = canvas.cols * 2, rows2 = canvas.rows * 2;
    const taken = Array.from({length: rows2}, () => Array(cols2).fill(false));
    const fits = (col, row, w, h) => {
      if (col < 0 || row < 0 || col + w > canvas.cols || row + h > canvas.rows) return false;
      for (let r = row * 2; r < (row + h) * 2; r++) for (let c = col * 2; c < (col + w) * 2; c++) if (taken[r][c]) return false;
      return true;
    };
    const take = tile => {
      for (let r = tile.row * 2; r < (tile.row + tile.span_h) * 2; r++) for (let c = tile.col * 2; c < (tile.col + tile.span_w) * 2; c++) taken[r][c] = true;
    };
    const order = tiles.map((tile, index) => index).filter(index => layoutUsed(tiles[index]))
      .sort((a, b) => (tiles[a]._unplaced ? 1 : 0) - (tiles[b]._unplaced ? 1 : 0));
    const later = [];
    for (const index of order) {
      const tile = tiles[index];
      if (!tile._unplaced && fits(tile.col, tile.row, tile.span_w, tile.span_h)) take(tile);
      else later.push(tile);
    }
    for (const tile of later) {
      const sizes = [[tile.span_w, tile.span_h], [1, 1], [1, 0.5]];
      let spot = null;
      for (const outside of [true, false]) {
        for (const [w, h] of sizes) {
          for (let row = 0; !spot && row + h <= canvas.rows; row += 0.5) {
            for (let col = 0; !spot && col + w <= canvas.cols; col += 0.5) {
              if (fits(col, row, w, h) && (!outside || layoutParked({col, row, span_w: w, span_h: h}, key)) &&
                  supportedTileLayout(tile.type, {col, row, span_w: w, span_h: h})) spot = {col, row, span_w: w, span_h: h};
            }
          }
          if (spot) break;
        }
        if (spot) break;
      }
      if (spot) { Object.assign(tile, spot); take(tile); }
    }
    tiles.forEach(tile => { if (tile) delete tile._unplaced; });
    return tiles;
  }

  // Taking a layout over from another one: every tile as it is there, the
  // same place on the screen and the same size (user 2026-10-08: "wie es
  // aktuell im Layout ist"); what does not fit the new screen is red.
  // Settings and Back stay in the classic layout only.
  function takeoverTiles(tab, key, from) {
    const L = LAYOUTS[key];
    const tiles = layoutBase(tab);
    if (from === 'classic') {
      tiles.forEach(tile => {
        if (!layoutUsed(tile)) return;
        if (classicHidden(tab, tile.view_id)) tile._unplaced = true;
        tile.col += layoutOffsetX(key) - layoutOffsetX('classic');
      });
    } else {
      // The other layout as its window shows it: saved, or its first take-over.
      const source = setupTiles(tab, from);
      tiles.forEach((tile, index) => {
        const p = source[index];
        if (!layoutUsed(tile)) return;
        if (layoutUsed(p) && layoutInside(p, from)) {
          Object.assign(tile, {col: p.col - layoutOffsetX(from) + layoutOffsetX(key), row: p.row,
            span_w: p.span_w, span_h: p.span_h});
          delete tile._unplaced;
        } else if (!layoutBarHidden(tile.type)) {
          // Back keeps its classic place, here in this layout's columns.
          // Not on that screen: into this layout's storage too. Back keeps
          // its classic place.
          tile._unplaced = true;
        }
      });
    }
    return layoutSettle(key, L.bar ? tiles.map(tile => (layoutUsed(tile) && layoutBarHidden(tile.type)
      ? layoutEmptyTile() : tile)) : tiles);
  }

  // The window's start: the layout's saved places; a layout never set up is
  // taken over from the classic layout.
  function setupTiles(tab, key) {
    if (key === 'classic') {
      const tiles = layoutBase(tab);
      tiles.forEach(tile => {
        if (!layoutUsed(tile) || !classicHidden(tab, tile.view_id)) return;
        const spot = classicParked(tab, tile.view_id);
        if (spot) {
          Object.assign(tile, spot);
          delete tile._unplaced;
        } else {
          tile._unplaced = true;
        }
      });
      return layoutSettle(key, tiles);
    }
    const places = layoutWindow.data.places?.[key]?.[layoutFolderId(tab)];
    if (!places) return takeoverTiles(tab, key, 'classic');
    return layoutSettle(key, layoutBase(tab).map(tile => {
      if (!layoutUsed(tile)) return tile;
      if (layoutBarHidden(tile.type)) return layoutEmptyTile();
      const p = places[tile.view_id];
      if (p) {
        Object.assign(tile, {col: p[0] + layoutOffsetX(key), row: p[1], span_w: p[2], span_h: p[3]});
        delete tile._unplaced;
      } else {
        tile._unplaced = true;
      }
      return tile;
    }));
  }

  // Every tile's place on every page, to tell unsaved changes.
  function layoutSignature(list) {
    return JSON.stringify(layoutTabs().map(tab => (list(tab) || []).map(tile => layoutUsed(tile)
      ? [tile.col, tile.row, tile.span_w, tile.span_h] : 0)));
  }
  // A layout's places as stored (its window start) and its unsaved ones.
  function layoutSavedSig(key) {
    if (!(key in layoutWindow.saved)) layoutWindow.saved[key] = layoutSignature(tab => setupTiles(tab, key));
    return layoutWindow.saved[key];
  }
  function layoutDirty(key = layoutWindow?.key) {
    if (!layoutWindow) return false;
    const work = key === layoutWindow.key ? getTilesData : (layoutWindow.work[key] ? tab => layoutWindow.work[key][tab] : null);
    return !!work && layoutSignature(work) !== layoutSavedSig(key);
  }

  // --- The window ----------------------------------------------------------

  function showLayoutTiles(tab, tiles) {
    tilesData[tab] = tiles;
    tiles.forEach((tile, index) => renderTileFromData(tab, index, tile || layoutEmptyTile(), sensorMetaCache));
    layoutTiles(tab, tiles);
  }

  function useLayoutGrid() {
    const canvas = layoutCanvas();
    GRID_COLS = canvas.cols;
    GRID_ROWS = canvas.rows;
    GRID_SHOWN_COLS = canvas.cols;
    GRID_SHOWN_ROWS = canvas.rows;
    HEAD_BAR = false;
  }

  // Red: half over the screen's edge, or a folder without a place (it could
  // not be reached); a tile wholly in the storage is only dimmed.
  function layoutRed(tile, key) {
    return !layoutInside(tile, key) && (Number(tile.type) === 4 || !layoutParked(tile, key));
  }
  function redCount(tab) {
    return getTilesData(tab).filter(tile => layoutUsed(tile) && layoutRed(tile, layoutWindow.key)).length;
  }
  function parkedCount(tab) {
    return getTilesData(tab).filter(tile => layoutUsed(tile) && !layoutRed(tile, layoutWindow.key) &&
      !layoutInside(tile, layoutWindow.key)).length;
  }

  function markLayoutRed(tab) {
    if (!layoutWindow || layoutWindow.tab !== tab) return;
    document.querySelectorAll(`#tab-tiles-${tab} .setup-grid > .tile`).forEach(el => {
      const place = !el.classList.contains('empty') && el.dataset.col !== undefined
        ? {type: el.dataset.type, col: Number(el.dataset.col), row: Number(el.dataset.row),
          span_w: Number(el.dataset.spanW), span_h: Number(el.dataset.spanH)} : null;
      const red = !!place && layoutRed(place, layoutWindow.key);
      el.classList.toggle('setup-red', red);
      el.classList.toggle('setup-parked', !!place && !red && !layoutInside(place, layoutWindow.key));
    });
    // Folders carry a badge on their top right corner like the page tabs: a
    // green tick when they have a place (still reachable), a red "!" when not.
    const grid = document.querySelector(`#tab-tiles-${tab} .setup-grid`);
    if (grid) {
      grid.querySelectorAll('.setup-flag').forEach(flag => flag.remove());
      grid.querySelectorAll(':scope > .tile[data-type="4"]:not(.empty)').forEach(el => {
        const flag = document.createElement('span');
        const missing = el.classList.contains('setup-red');
        flag.className = 'setup-flag ' + (missing ? 'missing' : 'ok');
        flag.dataset.for = el.id;
        flag.innerHTML = missing ? '!' : '<i class="mdi mdi-check"></i>';
        grid.appendChild(flag);
      });
      placeLayoutBadges(grid);
    }
    refreshLayoutStatus();
  }

  // "**...**" in a translation is shown bold.
  function layoutRichText(text) {
    return escapeHtml(text).replace(/\*\*(.+?)\*\*/g, '<b>$1</b>');
  }

  function refreshLayoutStatus() {
    const dialog = document.querySelector('.setup-dialog');
    if (!dialog || !layoutWindow) return;
    let total = 0, parked = 0;
    for (const tab of layoutTabs()) {
      const count = redCount(tab);
      total += count;
      parked += parkedCount(tab);
      const badge = dialog.querySelector(`.setup-tab[data-tab="${tab}"] .setup-badge`);
      if (badge) {
        badge.textContent = count || '';
        badge.hidden = !count;
      }
    }
    dialog.querySelectorAll('.setup-tab').forEach(b => b.classList.toggle('active', b.dataset.tab === layoutWindow.tab));
    dialog.querySelectorAll('.setup-layout').forEach(b => {
      b.classList.toggle('selected', b.dataset.key === layoutWindow.key);
      const arrow = dialog.querySelector(`.setup-arrow[data-key="${b.dataset.key}"]`);
      arrow?.classList.toggle('selected', b.dataset.key === layoutWindow.key);
      if (arrow && b.dataset.key === layoutWindow.key) arrow.parentNode.appendChild(arrow);
      b.classList.toggle('is-dirty', layoutDirty(b.dataset.key));
    });
    // A folder without a place could not be reached in that layout, so every
    // folder must have one (green) before the layout can be stored or shown.
    let missing = 0;
    for (const tab of layoutTabs()) {
      getTilesData(tab).forEach(tile => {
        if (layoutUsed(tile) && Number(tile.type) === 4 && !layoutInside(tile, layoutWindow.key)) missing++;
      });
    }
    layoutWindow.missingFolders = missing;
    // Red (a folder without a place, a tile half over the edge) is to be
    // solved first: nothing is stored or switched before (user 2026-10-08).
    layoutWindow.blocked = total > 0;
    const L = LAYOUTS[layoutWindow.key];
    const apply = dialog.querySelector('.setup-apply');
    apply.disabled = layoutWindow.blocked || !L.switchable;
    // Every note side by side (user 2026-10-08): folders without a place,
    // tiles half over the edge, tiles in the storage, a layout that cannot
    // be switched to yet; or that everything fits.
    const tiles = total - missing;
    const notes = [];
    if (missing) notes.push(['missing', 'folder-alert-outline',
      missing === 1 ? t('layoutFolderMissing') : tf('layoutFoldersMissing', {n: missing})]);
    if (tiles) notes.push(['missing', 'alert-circle-outline',
      tiles === 1 ? t('layoutTileMissing') : tf('layoutTilesMissing', {n: tiles})]);
    if (parked) notes.push(['parked', 'tray-arrow-down',
      parked === 1 ? t('layoutParkedTile') : tf('layoutParkedTiles', {n: parked})]);
    if (!notes.length) notes.push(['ok', 'check-circle-outline', t('layoutAllFit')]);
    dialog.querySelector('.setup-status').innerHTML = notes.map(([kind, icon, text]) =>
      `<span class="setup-status-item is-${kind}"><i class="mdi mdi-${icon}"></i><span>${layoutRichText(text)}</span></span>`).join('');
    refreshLayoutSave();
    refreshLayoutDelete();
  }

  function unmountLayoutGrid() {
    const home = layoutWindow?.home;
    if (!home) return;
    home.grid.querySelectorAll(':scope > .setup-storage-label, :scope > .setup-flag, :scope > .setup-trash')
      .forEach(node => node.remove());
    home.grid.classList.remove('setup-grid', 'setup-head', 'setup-head-ghost');
    home.grid.removeAttribute('style');
    home.placeholder.replaceWith(home.grid);
    layoutWindow.home = null;
  }

  async function mountLayoutTab(tab) {
    unmountLayoutGrid();
    layoutWindow.tab = tab;
    await switchTab('tab-tiles-' + tab);
    if (!layoutWindow) return;
    const host = document.getElementById('tab-tiles-' + tab);
    const backdrop = document.querySelector('.setup-backdrop');
    const grid = host?.querySelector('.tile-grid');
    if (!host || !backdrop || !grid) return;
    // The page keeps the grid's room, so nothing behind the window reflows.
    const placeholder = document.createElement('div');
    placeholder.style.cssText = `width:${grid.offsetWidth}px;height:${grid.offsetHeight}px;flex:none;`;
    grid.replaceWith(placeholder);
    layoutWindow.home = {grid, placeholder};
    // First in the tab, so the editor's "#tab-tiles-x .tile-grid" still finds this grid.
    host.insertBefore(backdrop, host.firstChild);
    backdrop.querySelector('.setup-stage').appendChild(grid);
    const L = LAYOUTS[layoutWindow.key];
    grid.classList.add('setup-grid', 'setup-head');
    // The classic layout has no head on the panel: here it is only the place
    // for folders still to be pulled in, so it is greyed out.
    grid.classList.toggle('setup-head-ghost', !L.bar);
    // One scale for all layouts, taken from the largest one: the head and the
    // tiles keep their size when the layout changes, and so does the window.
    // The screen always starts in the same corner. All preview sizes grow by
    // that one factor, so the editor's own measuring stays right.
    const dialog = backdrop.querySelector('.setup-dialog');
    const chrome = [...dialog.children].filter(el => !el.classList.contains('setup-stage'))
      .reduce((sum, el) => sum + el.offsetHeight, 0) + 16 * (dialog.children.length - 1) + 48 + 2;
    // Every layout with its own sizes; each gets the head's room on top, the
    // classic one too.
    const sizesOf = key => ({...layoutBaseSizes[key], ...Object.fromEntries(Object.entries(layoutHeadVars(LAYOUTS[key]))
      .map(([n, val]) => [n, parseFloat(val)])),
      ...(LAYOUTS[key].bar ? {} : {'--preview-pad-top': layoutBaseSizes.bar['--preview-pad-top']})});
    const areaOf = (sizes, cols, rows) => {
      const frame = sizes['--preview-frame'] || 0;
      const gap = sizes['--preview-gap'] || 0;
      return [2 * frame + sizes['--preview-pad-left'] + sizes['--preview-pad-right'] + cols * sizes['--preview-cell-w'] + (cols - 1) * gap,
        2 * frame + sizes['--preview-pad-top'] + sizes['--preview-pad-bottom'] + rows * sizes['--preview-cell-h'] + (rows - 1) * gap];
    };
    const canvas = layoutCanvas();
    const areas = LAYOUT_KEYS.filter(key => LAYOUTS[key].available).map(key => areaOf(sizesOf(key), canvas.cols, canvas.rows));
    // The page's own preview scale; smaller only when the window lacks room.
    const roomW = Math.min(innerWidth - 32, layoutRootPx('--admin-wrapper-width') || innerWidth) - 58;
    // Taken once: the window keeps its size whatever layout it shows.
    const f = layoutWindow.scale ||
      (layoutWindow.scale = Math.min(0.9, ...areas.map(([w, h]) => Math.min(roomW / w, (innerHeight - 32 - chrome) / h))));
    const stage = backdrop.querySelector('.setup-stage');
    stage.style.width = Math.max(...areas.map(([w]) => w)) * f + 2 + 'px';
    stage.style.height = Math.max(...areas.map(([, h]) => h)) * f + 2 + 'px';
    const base = sizesOf(layoutWindow.key);
    const scale = base['--radius-preview-scale'] || layoutScale();
    layoutSetVars(grid, Object.fromEntries(Object.entries(base).filter(([n]) => n !== '--radius-preview-scale')
      .map(([n, val]) => [n, (val * f).toFixed(2) + 'px'])));
    layoutSetVars(grid, {'--grid-cols': String(GRID_COLS), '--grid-rows': String(GRID_ROWS),
      '--radius-preview-scale': String(scale * f)});
    // The layout's screen, black with its rounded corners, behind the tiles.
    // The classic screen has no head: its cells start where the bar
    // layouts' do, right under the window's head.
    let w, h, y = 0;
    if (L.bar) {
      [w, h] = areaOf(base, L.cols, L.rows).map(value => value * f);
    } else {
      const own = layoutBaseSizes.classic;
      const px = name => own[name];
      const frame = base['--preview-frame'] || 0;
      const gap = base['--preview-gap'] || 0;
      w = (2 * frame + px('--preview-pad-left') + px('--preview-pad-right') + L.cols * px('--preview-cell-w') + (L.cols - 1) * gap) * f;
      h = (2 * frame + px('--preview-pad-top') + px('--preview-pad-bottom') + L.rows * px('--preview-cell-h') + (L.rows - 1) * gap) * f;
      y = (base['--preview-pad-top'] - px('--preview-pad-top')) * f;
    }
    const r = parseFloat(getComputedStyle(grid).borderTopLeftRadius) || 20;
    const svg = `<svg xmlns='http://www.w3.org/2000/svg' width='${w}' height='${h}'><rect width='${w}' height='${h}' rx='${r}' fill='black'/></svg>`;
    grid.style.backgroundImage = `url("data:image/svg+xml,${encodeURIComponent(svg)}")`;
    // In the middle of the area: the head and the screen move along.
    const x = layoutOffsetX(layoutWindow.key) * (base['--preview-cell-w'] + (base['--preview-gap'] || 0)) * f;
    grid.style.backgroundPosition = `${x}px ${y}px`;
    grid.style.setProperty('--setup-screen-x', x.toFixed(2) + 'px');
    // The storage's name in its bottom right corner (beside every screen).
    const label = document.createElement('div');
    label.className = 'setup-storage-label';
    label.innerHTML = `<b>${escapeHtml(t('layoutStorage'))}</b><span>${escapeHtml(t('layoutStorageHint'))}</span>`;
    grid.appendChild(label);
    // Tiles such as Media measure their card when they are drawn: draw them
    // again at the window's scale.
    showLayoutTiles(tab, getTilesData(tab));
  }

  async function loadLayoutWindow(key, from) {
    // "Kopieren von" can be undone: the places before it, until the next
    // layout or save.
    layoutWindow.undo = from && layoutWindow.shown && key === layoutWindow.key
      ? Object.fromEntries(layoutTabs().map(tab => [tab, layoutClone(getTilesData(tab))])) : null;
    // The layout left keeps its unsaved places for later.
    if (layoutWindow.shown) {
      layoutWindow.work[layoutWindow.key] = Object.fromEntries(layoutTabs().map(tab => [tab, layoutClone(getTilesData(tab))]));
    }
    layoutWindow.key = key;
    layoutWindow.shown = true;
    useLayoutGrid();
    const work = !from && layoutWindow.work[key];
    for (const tab of layoutTabs()) {
      showLayoutTiles(tab, from ? takeoverTiles(tab, key, from) : work?.[tab] ? layoutClone(work[tab]) : setupTiles(tab, key));
    }
    delete layoutWindow.work[key];
    layoutSavedSig(key);
    await mountLayoutTab(layoutWindow.tab);
    refreshLayoutButtons();
  }

  function refreshLayoutButtons() {
    const dialog = document.querySelector('.setup-dialog');
    if (!dialog || !layoutWindow) return;
    // Take over from any other layout.
    dialog.querySelector('.setup-undo').hidden = !layoutWindow.undo;
    dialog.querySelector('.setup-from').innerHTML = `<option value="">${escapeHtml(t('layoutCopyFrom'))}</option>` +
      LAYOUT_KEYS.filter(key => key !== layoutWindow.key && LAYOUTS[key].available)
        .map(key => `<option value="${key}">${escapeHtml(LAYOUTS[key].name)}</option>`).join('');
    // The active layout needs no switch; the button keeps its room, so the
    // window keeps its size.
    dialog.querySelector('.setup-apply').style.visibility = layoutWindow.key === ACTIVE_LAYOUT ? 'hidden' : '';
    refreshLayoutStatus();
  }

  function refreshLayoutSave() {
    const button = document.querySelector('.setup-save');
    if (!button || !layoutWindow) return;
    const changed = layoutDirty();
    // A layout with a folder out of reach is never stored, like it can never
    // be switched to: the folder would be gone on the panel.
    button.disabled = !changed || layoutWindow.blocked || layoutWindow.busy;
    button.innerHTML = changed ? escapeHtml(t('save')) : `<i class="mdi mdi-check"></i> ${escapeHtml(t('layoutSaved'))}`;
  }

  // The selected tile with the cross on its corner: on the screen it goes
  // into the storage, in the storage (or half over the edge) it is deleted
  // (not folders, not Settings and Back).
  function layoutSelectedTile() {
    if (!layoutWindow || currentTileTab !== layoutWindow.tab || currentTileIndex < 0) return null;
    const tile = getTilesData(layoutWindow.tab)[currentTileIndex];
    return layoutUsed(tile) && Number(tile.type) !== 4 && !layoutNavType(tile.type) ? tile : null;
  }

  function refreshLayoutDelete() {
    const grid = layoutWindow && document.querySelector(`#tab-tiles-${layoutWindow.tab} .setup-grid`);
    if (!grid) return;
    grid.querySelectorAll('.setup-trash').forEach(node => node.remove());
    const el = layoutSelectedTile() && document.getElementById(`${layoutWindow.tab}-tile-${currentTileIndex}`);
    if (!el) return;
    const cross = document.createElement('span');
    cross.className = 'setup-trash';
    cross.title = t(layoutInside(layoutSelectedTile(), layoutWindow.key) ? 'layoutPark' : 'layoutDeleteTile');
    cross.dataset.for = el.id;
    cross.innerHTML = '<i class="mdi mdi-close"></i>';
    grid.appendChild(cross);
    placeLayoutBadges(grid);
  }

  // Badges sit on their tile's top right corner, half outside. While a tile
  // is dragged or resized they ride on the editor's placeholder, so they go
  // along with the tile's new place.
  let layoutMoving = '';
  function placeLayoutBadges(grid) {
    const resizing = resizeState ? resizeState.tileId : '';
    const target = layoutMoving || resizing;
    const holder = target && grid.querySelector(resizing ? '.tile-resize-placeholder.show' : '.tile-drop-placeholder.show');
    grid.querySelectorAll('.setup-flag, .setup-trash').forEach(badge => {
      const tile = document.getElementById(badge.dataset.for);
      const box = badge.dataset.for === target && holder ? holder : tile;
      if (!box) return;
      const left = (box.offsetLeft + box.offsetWidth - 11) + 'px';
      const top = (box.offsetTop - 7) + 'px';
      if (badge.style.left !== left) badge.style.left = left;
      if (badge.style.top !== top) badge.style.top = top;
    });
  }

  function deselectLayoutTile(tab) {
    currentTileIndex = -1;
    document.querySelectorAll(`#tab-tiles-${tab} .tile-grid > .tile`).forEach(el => {
      el.classList.remove('active');
      delete el.dataset.selected;
    });
  }

  // The cross on a tile on the screen puts it into the storage: no part of
  // this layout, its places in the other layouts stay (user 2026-10-08).
  function parkLayoutTile() {
    const tile = layoutSelectedTile();
    if (!tile) return;
    const tab = layoutWindow.tab, index = currentTileIndex;
    // The window's own drafts end with it; the tile's place is its data.
    if (drafts[tab]) delete drafts[tab][index];
    deselectLayoutTile(tab);
    const tiles = getTilesData(tab);
    tile._unplaced = true;
    layoutSettle(layoutWindow.key, tiles);
    showLayoutTiles(tab, tiles);
  }

  // The cross on a tile in the storage deletes it, from every layout.
  async function deleteLayoutTile() {
    const tile = layoutSelectedTile();
    if (!tile) return;
    const tab = layoutWindow.tab, index = currentTileIndex;
    const name = tile.title || getTileTypeMeta(tile.type)?.label || '';
    if (!window.confirm(tf('layoutDeleteConfirm', {tile: name}))) return;
    const form = new FormData();
    form.append('folder', layoutFolderId(tab));
    form.append('index', String(index));
    form.append('type', '0');
    try {
      const response = await layoutWindowFetch('/api/tiles', {method: 'POST', body: form});
      if (!response.ok) throw new Error('HTTP ' + response.status);
    } catch (error) {
      showNotification(t('deleteFailed'), false);
      return;
    }
    if (!layoutWindow) return;
    layoutWindow.changed = true;
    // Gone from every layout: from the page, the stored places and every
    // layout's unsaved ones; only the other edits remain unsaved.
    const tabIndex = layoutTabs().indexOf(tab);
    if (layoutWindow.pages[tab]) layoutWindow.pages[tab][index] = layoutEmptyTile();
    for (const work of Object.values(layoutWindow.work)) if (work[tab]) work[tab][index] = layoutEmptyTile();
    for (const key of Object.keys(layoutWindow.saved)) {
      const saved = JSON.parse(layoutWindow.saved[key]);
      if (saved[tabIndex]) saved[tabIndex][index] = 0;
      layoutWindow.saved[key] = JSON.stringify(saved);
    }
    if (drafts[tab]) delete drafts[tab][index];
    deselectLayoutTile(tab);
    getTilesData(tab)[index] = layoutEmptyTile();
    renderTileFromData(tab, index, layoutEmptyTile(), sensorMetaCache);
    layoutTiles(tab, getTilesData(tab));
  }

  // Back to the places before "Kopieren von".
  async function undoLayoutCopy() {
    const before = layoutWindow?.undo;
    if (!before) return;
    layoutWindow.undo = null;
    for (const tab of layoutTabs()) if (before[tab]) showLayoutTiles(tab, before[tab]);
    await mountLayoutTab(layoutWindow.tab);
    refreshLayoutButtons();
  }

  // Closing the window with layouts not saved asks first.
  function mayLeaveLayout() {
    const open = LAYOUT_KEYS.filter(key => layoutDirty(key)).map(key => LAYOUTS[key].name);
    return !open.length || window.confirm(tf('layoutUnsavedConfirm', {layout: open.join(', ')}));
  }

  // "Speichern": every place of every page; the server takes what lies on
  // the layout's screen (and keeps the bar layouts' red places).
  async function saveLayoutWindow() {
    if (!layoutWindow || layoutWindow.blocked || layoutWindow.busy) return false;
    const key = layoutWindow.key;
    const folders = {};
    for (const tab of layoutTabs()) {
      const places = folders[layoutFolderId(tab)] = {};
      getTilesData(tab).forEach(tile => {
        if (!layoutUsed(tile) || !tile.view_id || (key !== 'classic' && layoutBarHidden(tile.type))) return;
        places[tile.view_id] = [tile.col - layoutOffsetX(key), tile.row, tile.span_w, tile.span_h];
      });
    }
    layoutWindow.busy = true;
    refreshLayoutSave();
    try {
      const response = await layoutWindowFetch('/api/layouts', {
        method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({layout: key, folders})});
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const data = await (await layoutWindowFetch('/api/layouts', {cache: 'no-store'})).json();
      if (data?.success) layoutWindow.data = data;
    } catch (error) {
      showNotification(t('saveFailed'), false);
      return false;
    } finally {
      if (layoutWindow) layoutWindow.busy = false;
    }
    if (!layoutWindow) return false;
    if (key === ACTIVE_LAYOUT) layoutWindow.changed = true;
    for (const other of Object.keys(layoutWindow.saved)) if (!layoutWindow.work[other]) delete layoutWindow.saved[other];
    layoutWindow.undo = null;
    const undo = document.querySelector('.setup-undo');
    if (undo) undo.hidden = true;
    layoutWindow.saved[key] = layoutSignature(getTilesData);
    refreshLayoutStatus();
    return true;
  }

  // "Umstellen": stored, chosen, and the panel restarts with it.
  async function applyLayoutWindow() {
    if (!layoutWindow) return;
    const L = LAYOUTS[layoutWindow.key];
    if (!L.switchable || layoutWindow.blocked) return;
    if (!window.confirm(tf('layoutSwitchConfirm', {layout: L.name}))) return;
    if (!(await saveLayoutWindow())) return;
    try {
      const response = await layoutWindowFetch('/api/layouts/active', {
        method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'layout=' + encodeURIComponent(layoutWindow.key)});
      if (!response.ok) throw new Error('HTTP ' + response.status);
    } catch (error) {
      showNotification(t('saveFailed'), false);
      return;
    }
    closeLayoutWindow();
    restartPanelForLayout();
  }

  async function openLayoutWindow() {
    if (layoutWindow) return;
    // Every folder's page with its tiles, and the stored places.
    for (const folderId of Object.keys(tabByFolder).map(Number).filter(id => id > 0)) {
      try { await ensureFolderTabUi(folderId); } catch (error) {}
    }
    initTileTabs();
    let data = null;
    try {
      for (const tab of tileTabs.filter(tab => !isScreensaverTileTab(tab))) await fetchTileGridData(tab);
      const response = await layoutWindowFetch('/api/layouts', {cache: 'no-store'});
      data = await response.json();
      if (!response.ok || !data?.success) throw new Error('layouts');
    } catch (error) {
      showNotification(t('loadFailed'), false);
      return;
    }
    if (layoutWindow) return;
    readLayoutBaseSizes();
    const tabs = layoutTabs();
    layoutWindow = {
      key: ACTIVE_LAYOUT === 'classic' ? 'bar' : ACTIVE_LAYOUT,
      tab: tabs.includes(currentTileTab) ? currentTileTab : tabs[0],
      data,
      pages: Object.fromEntries(tabs.map(tab => [tab, layoutClone(getTilesData(tab))])),
      drafts: layoutClone(drafts),
      grid: {GRID_COLS, GRID_ROWS, GRID_SHOWN_COLS, GRID_SHOWN_ROWS, HEAD_BAR},
      changed: false,
      busy: false,
      shown: false,
      undo: null,
      work: {},
      saved: {},
      missingFolders: 0,
      blocked: false,
      home: null
    };
    const backdrop = document.createElement('div');
    backdrop.className = 'setup-backdrop';
    const layoutButton = key => {
      const L = LAYOUTS[key];
      const shape = 'setup-shape' + (L.portrait ? ' portrait' : '') + (L.bar ? ' bar' : '');
      const active = key === ACTIVE_LAYOUT ? `<span class="setup-active">${escapeHtml(t('layoutActive'))}</span>` : '';
      return `<button type="button" class="setup-layout" data-key="${key}"><span class="${shape}"></span>` +
        `<span class="setup-layout-text"><b>${escapeHtml(L.name)}</b><small>${layoutSize(key)}</small></span>${active}</button>`;
    };
    // The layout the panel shows on the left, arrows to the others on the
    // right, one above the other (user 2026-10-08); the arrow to the layout
    // being set up lights up, none while the active one itself is edited.
    const targets = LAYOUT_KEYS.filter(key => key !== ACTIVE_LAYOUT && LAYOUTS[key].available);
    const arrowHeight = targets.length > 1 ? 76 : 34;
    const arrows = targets.map((key, index) => {
      const y = targets.length > 1 ? (index ? 59 : 17) : 17;
      const middle = arrowHeight / 2;
      return `<g class="setup-arrow" data-key="${key}"><path d="M2 ${middle} H12 C22 ${middle} 22 ${y} 32 ${y} H40"/>` +
        `<path d="M35 ${y - 5} L40.5 ${y} L35 ${y + 5}"/></g>`;
    }).join('');
    const layoutButtons = layoutButton(ACTIVE_LAYOUT) +
      `<svg class="setup-arrows" viewBox="0 0 44 ${arrowHeight}" width="44" height="${arrowHeight}" aria-hidden="true">` +
      `${arrows}</svg><div class="setup-targets">${targets.map(layoutButton).join('')}</div>`;
    const tabButtons = tabs.map(tab => {
      const host = document.getElementById('tab-tiles-' + tab);
      const home = layoutFolderId(tab) === '0';
      const name = home ? t('home') : host.dataset.folderName || tab;
      const icon = home ? 'home' : host.dataset.folderIcon || 'folder';
      return `<button type="button" class="tab-btn setup-tab" data-tab="${tab}"><i class="mdi mdi-${escapeHtml(icon)}"></i> ` +
        `${escapeHtml(name)}<span class="setup-badge" hidden></span></button>`;
    }).join('');
    backdrop.innerHTML = [
      '<div class="setup-dialog" role="dialog" aria-modal="true">',
      // One head: the title, the layouts, the X with the notes below it
      // (user 2026-10-08).
      `<div class="setup-head-row"><div class="setup-title">${escapeHtml(t('layoutChange'))}</div>`,
      `<div class="setup-layouts">${layoutButtons}</div>`,
      `<div class="setup-head-right"><button type="button" class="setup-close" aria-label="${escapeHtml(t('close'))}">` +
        '<i class="mdi mdi-close"></i></button><div class="setup-status"></div></div></div>',

      `<div class="setup-tabs">${tabButtons}</div>`,
      '<div class="setup-stage"></div>',
      // The buttons always on the right.
      '<div class="setup-foot"><div class="setup-actions">',
      `<button type="button" class="btn setup-undo" hidden><i class="mdi mdi-undo"></i> ${escapeHtml(t('layoutUndo'))}</button>`,
      `<select class="setup-from" aria-label="${escapeHtml(t('layoutCopyFrom'))}"></select>`,
      `<button type="button" class="btn setup-save">${escapeHtml(t('save'))}</button>`,
      `<button type="button" class="btn btn-go setup-apply">${escapeHtml(t('layoutSwitch'))}</button></div></div></div>`
    ].join('');
    document.body.appendChild(backdrop);
    document.body.classList.add('setup-window');
    const buttons = [...backdrop.querySelectorAll('.setup-layout')];
    const widest = Math.max(...buttons.map(button => button.offsetWidth));
    buttons.forEach(button => { button.style.width = widest + 'px'; });
    // A click on the window's free area does not start a new tile.
    backdrop.querySelector('.setup-stage').addEventListener('click', event => {
      if (event.target.classList?.contains('setup-grid')) event.stopPropagation();
    }, true);
    await loadLayoutWindow(layoutWindow.key, '');
  }

  async function closeLayoutWindow() {
    if (!layoutWindow) return;
    const state = layoutWindow;
    unmountLayoutGrid();
    document.querySelector('.setup-backdrop')?.remove();
    document.body.classList.remove('setup-window');
    // The window's places never reach the editor: pending saves are dropped,
    // its drafts give way to the page's.
    for (const key of Object.keys(autoSaveTimers)) {
      if (autoSaveTimers[key]) clearTimeout(autoSaveTimers[key]);
      if (key.includes(':')) delete autoSaveTimers[key];
      else autoSaveTimers[key] = null;
    }
    for (const key of Object.keys(drafts)) delete drafts[key];
    Object.assign(drafts, state.drafts);
    persistDrafts();
    GRID_COLS = state.grid.GRID_COLS;
    GRID_ROWS = state.grid.GRID_ROWS;
    GRID_SHOWN_COLS = state.grid.GRID_SHOWN_COLS;
    GRID_SHOWN_ROWS = state.grid.GRID_SHOWN_ROWS;
    HEAD_BAR = state.grid.HEAD_BAR;
    layoutWindow = null;
    currentTileIndex = -1;
    document.querySelectorAll('.tile-grid > .tile').forEach(el => {
      el.classList.remove('active', 'setup-red');
      delete el.dataset.selected;
    });
    document.querySelectorAll('.tile-specific-settings').forEach(el => el.classList.add('hidden'));
    // The page again, with what the panel now has.
    for (const tab of Object.keys(state.pages)) {
      tilesData[tab] = state.pages[tab];
      if (state.changed) {
        try { await fetchTileGridData(tab, true); } catch (error) {}
      }
      showLayoutTiles(tab, getTilesData(tab));
    }
  }

  // The window's editor saves only in the window (see above).
  window.fetch = (url, options) => (layoutWindow && options && /post/i.test(options.method || '') &&
      /^\/api\/tiles(\/reorder)?(\?|$)/.test(String(url)))
    ? Promise.resolve(new Response('{"success":true}', {headers: {'Content-Type': 'application/json'}}))
    : layoutWindowFetch(url, options);

  document.addEventListener('dragstart', event => {
    const tile = layoutWindow && event.target.closest?.('.setup-grid > .tile');
    layoutMoving = tile ? tile.id : '';
  }, true);
  document.addEventListener('dragend', () => {
    layoutMoving = '';
    const grid = layoutWindow && document.querySelector(`#tab-tiles-${layoutWindow.tab} .setup-grid`);
    if (grid) requestAnimationFrame(() => placeLayoutBadges(grid));
  }, true);
  // The placeholders move by style changes; the badges follow frame by frame.
  let layoutBadgeFrame = 0;
  if (typeof MutationObserver === 'function') {
    new MutationObserver(records => {
      if (!layoutWindow || layoutBadgeFrame) return;
      if (records.every(record => record.target.classList?.contains('setup-flag') ||
          record.target.classList?.contains('setup-trash'))) return;
      layoutBadgeFrame = requestAnimationFrame(() => {
        layoutBadgeFrame = 0;
        const grid = layoutWindow && document.querySelector(`#tab-tiles-${layoutWindow.tab} .setup-grid`);
        if (grid) placeLayoutBadges(grid);
      });
    }).observe(document.body, {attributes: true, subtree: true, attributeFilter: ['style', 'class']});
  }

  document.addEventListener('click', event => {
    if (!layoutWindow || !event.target.closest?.('.setup-dialog')) return;
    const layout = event.target.closest('.setup-layout');
    if (layout && layout.dataset.key !== layoutWindow.key) loadLayoutWindow(layout.dataset.key, '');
    const tab = event.target.closest('.setup-tab');
    if (tab && tab.dataset.tab !== layoutWindow.tab) mountLayoutTab(tab.dataset.tab);
    if (event.target.closest('.setup-close') && mayLeaveLayout()) { closeLayoutWindow(); return; }
    if (event.target.closest('.setup-save')) saveLayoutWindow();
    if (event.target.closest('.setup-undo')) undoLayoutCopy();
    if (event.target.closest('.setup-trash')) {
      const tile = layoutSelectedTile();
      if (tile && layoutInside(tile, layoutWindow.key)) parkLayoutTile();
      else deleteLayoutTile();
    }
    if (event.target.closest('.setup-apply')) applyLayoutWindow();
    // Selecting a tile decides the delete cross.
    setTimeout(refreshLayoutDelete, 0);
  });
  document.addEventListener('change', event => {
    const from = event.target.closest?.('.setup-from');
    if (!from || !layoutWindow || !from.value) return;
    const source = from.value;
    from.value = '';
    loadLayoutWindow(layoutWindow.key, source);
  });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape' && layoutWindow && mayLeaveLayout()) closeLayoutWindow();
  });

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
    syncScreensaverImages();
  }

  function ssClamp(value, min, max) {
    const n = Number(value);
    return Math.max(min, Math.min(max, Number.isFinite(n) ? n : min));
  }

  // The image frame stands for the panel's screen: its box in the grid's
  // padding box (where the clock is positioned) and on the page.
  function ssPreviewScreenRect(preview) {
    const frame = preview.querySelector('.screensaver-grid-image-frame');
    return (frame || preview).getBoundingClientRect();
  }

  // One background opacity for every screensaver tile (tile_opacity, set in
  // the screensaver footer): the draft once loaded, else the server-rendered
  // slider.
  function screensaverTileOpacity() {
    const value = screensaverDraft?.tile_opacity ??
      document.getElementById('screensaverTileOpacity')?.value;
    return Math.round(ssClamp(value ?? SCREENSAVER_TILE_DEFAULT_OPACITY, 0, 255));
  }

  // The new opacity on every screensaver tile preview and in the footer.
  function refreshScreensaverTileOpacity() {
    const value = screensaverTileOpacity();
    const output = document.getElementById('screensaverTileOpacityValue');
    if (output) output.textContent = Math.round(value * 100 / 255) + ' %';
    const tiles = tilesData.screensaver || [];
    tiles.forEach((tile, index) => {
      if (tile && Number(tile.type)) renderTileFromData('screensaver', index, tile, sensorMetaCache);
    });
    layoutTiles('screensaver', tiles);
    if (currentTileTab === 'screensaver' && currentTileIndex >= 0) updateTilePreview('screensaver');
  }

  function ssPreviewScreen(preview) {
    const frame = preview.querySelector('.screensaver-grid-image-frame');
    const rect = ssPreviewScreenRect(preview);
    return {
      left: frame ? frame.offsetLeft : 0,
      top: frame ? frame.offsetTop : 0,
      width: rect.width || 800,
      height: rect.height || 500
    };
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
    return data;
  }

  // Keep the stored image list in step with the card: entries of deleted
  // files are dropped and new images join checked, so an upload appears in
  // the slideshow without extra clicks. Returns true when the list changed.
  function ssSyncCardImages(data) {
    const available = Array.isArray(data.available_wallpapers) ? data.available_wallpapers : [];
    const key = name => String(name || '').toLowerCase();
    let changed = false;
    // Without a card the list stays untouched; its images may come back.
    if (data.sd_ready === true) {
      const onCard = new Set(available.map(key));
      const kept = data.wallpapers.filter(item => onCard.has(key(item.file_name)));
      changed = kept.length !== data.wallpapers.length;
      data.wallpapers = kept;
    }
    const listed = new Set(data.wallpapers.map(item => key(item.file_name)));
    available.forEach(name => {
      // The display stores at most 32 images.
      if (listed.has(key(name)) || data.wallpapers.length >= 32) return;
      listed.add(key(name));
      data.wallpapers.push({
        file_name: name, enabled: true,
        focus_x: 500, focus_y: 500, zoom: 1000
      });
      changed = true;
    });
    return changed;
  }

  // File manager uploads, renames and deletions store the updated list right
  // away, so the display follows the card even while the Screensaver tab is
  // closed. An open editor syncs and saves through its own load instead.
  function syncScreensaverImages() {
    fetch('/api/screensaver').then(r => r.json()).then(config => {
      if (!config || !config.success || screensaverLoaded || screensaverLoading) return;
      const data = ssNormalizeLoaded(config);
      if (!ssSyncCardImages(data)) return;
      return fetch('/api/screensaver', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(ssPayload(data, ''))
      });
    }).catch(() => {});
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
      const imagesChanged = ssSyncCardImages(screensaverDraft);
      screensaverWallpaperIndex = screensaverDraft.wallpapers.findIndex(w => w.enabled);
      if (screensaverWallpaperIndex < 0 && screensaverDraft.wallpapers.length) screensaverWallpaperIndex = 0;
      screensaverLoaded = true;
      bindScreensaverEditor();
      selectScreensaverBackground();
      renderScreensaverEditor();
      if (imagesChanged) scheduleScreensaverSave();
    }).catch(() => showNotification(t('screensaverLoadFailed'), false))
      .finally(() => { screensaverLoading = false; });
  }

  function ssPayload(d = screensaverDraft, previewName = null) {
    return {
      version: 2,
      use_wallpapers: !!d.use_wallpapers,
      shuffle: !!d.shuffle,
      tile_shadow: !!d.tile_shadow,
      tile_border: d.tile_border !== false,
      tile_opacity: Math.round(ssClamp(d.tile_opacity ?? SCREENSAVER_TILE_DEFAULT_OPACITY, 0, 255)),
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
      preview_wallpaper: previewName ?? (ssCurrentWallpaper()?.file_name || ''),
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
    // The image frame is the panel's screen. The grid around it is wider
    // (editor padding and gaps), so the clock is placed and scaled on the
    // frame; on the grid it sat about 10 px up and right, against the edge.
    const screen = ssPreviewScreen(preview);
    const scale = screen.width / Number(d.screen_width || 1280);
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
    clock.style.left = (screen.left + d.clock_x * screen.width / 1000) + 'px';
    clock.style.top = (screen.top + d.clock_y * screen.height / 1000) + 'px';
    const time = document.getElementById('screensaverClockTime');
    const date = document.getElementById('screensaverClockDate');
    time.hidden = !d.show_time;
    date.hidden = !d.show_date && !d.show_weekday;
    time.style.fontSize =
      Math.max(10, deviceClockFontPx(d.time_font_size, 48) * scale) + 'px';
    date.style.fontSize =
      Math.max(8, deviceClockFontPx(d.date_font_size, 28) * scale) + 'px';
    // Each line is as tall as its LVGL font's line height, the glyphs on the
    // LVGL baseline, with the device gap between the lines (clock/renderer.cpp).
    const applyClockLine = (el, raw, fallback, minPx) => {
      const size = Number(raw || fallback);
      const fontPx = deviceClockFontPx(raw, fallback) * scale;
      // Tiny previews keep a readable font; the line box grows with it.
      const lineScale = fontPx < minPx ? minPx / fontPx : 1;
      const linePx = devicePx('--screensaver-lh' + size, size * 1.21) * scale * lineScale;
      // The glyphs on the LVGL baseline, measured in this browser and zoom
      // (text-baseline.js).
      const base = parseFloat(rootStyles.getPropertyValue('--screensaver-lb' + size));
      el.style.lineHeight = linePx + 'px';
      el.style.position = 'relative';
      el.style.top = (Number.isFinite(base)
        ? previewBaselineShift(Math.max(minPx, fontPx), linePx, base * scale * lineScale) : 0) + 'px';
    };
    applyClockLine(time, d.time_font_size, 48, 10);
    applyClockLine(date, d.date_font_size, 28, 8);
    date.style.marginTop = !time.hidden && !date.hidden
      ? devicePx('--screensaver-clock-gap', 6) * scale + 'px' : '0px';
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
    const opacityInput = document.getElementById('screensaverTileOpacity');
    if (opacityInput) opacityInput.value = String(screensaverTileOpacity());
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
    // Tile selection replaces the clicked child before the event reaches the grid.
    // The original event path retains its tile even when that child is detached.
    const fromTileOrClock = event => event.composedPath().some(
      node => node?.matches?.('.tile, #screensaverClock'));
    preview.addEventListener('click', e => {
      if (!fromTileOrClock(e)) {
        selectScreensaverBackground();
      }
    });
    let backgroundDrag = null;
    preview.addEventListener('pointerdown', e => {
      if (fromTileOrClock(e)) return;
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
      const rect = ssPreviewScreenRect(preview);
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
    bind('screensaverTileOpacity', 'input', el => {
      screensaverDraft.tile_opacity = Number(el.value);
      refreshScreensaverTileOpacity();
    }, false);
    bind('screensaverTileOpacity', 'change', el => { screensaverDraft.tile_opacity = Number(el.value); });
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
      fetchEntityOptions(true).then(() => {
        refreshEntityPickers();
        fetchSensorMetaCache(true);
      }).catch(() => {});
    }, delay));
  }

  function restartHardwareIoNow() {
    setHardwareIoSaveState(t('ioRestarting'), 'saving');
    const restartForm = document.getElementById('admin_restart_form');
    // form.submit() cannot carry the CSRF header a password-protected panel
    // requires; send the same request with fetch and reload as before.
    if (restartForm && !window.HomeTilesAuth?.csrfToken()) {
      window.setTimeout(() => restartForm.submit(), 100);
      return;
    }
    fetch('/restart', {method: 'POST'}).catch(() => {}).finally(() => {
      if (restartForm) window.setTimeout(() => window.location.assign('/'), 300);
    });
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
    bindPreviewBaselines();
    toggleStaticNetworkFields();
    toggleNetworkSettings();
    toggleSettingsAccessFields();
    initSettingsAccessControls();
    initAdminSettingsSave();
    initWebAdminPasswordSettings();
    initSettingsPages();
    initTileTabs();
    let initialTab = '';
    try { initialTab = localStorage.getItem('activeAdminTab') || ''; } catch (e) {}
    // I/O was a tab of its own before it became a Settings page.
    if (initialTab === 'tab-hardware') {
      initialTab = 'tab-network';
      showSettingsPage('io');
    }
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
    watchDeviceLanguage();
    updateTileSettingsMaxHeight();
    // Any editing postpones the background folder tab prefetch.
    ['pointerdown', 'keydown', 'input'].forEach(type =>
      document.addEventListener(type, noteAdminInteraction, true));
    scheduleFolderTabPrefetch();
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
    return (['1','2','3','4','5'].includes(v)) ? v : '0';
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
    loadIconColorFields(tab, data);
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
    // The entity is known now: numeric states show the color bar, text
    // states the state list.
    syncIconColorFields(tab);
  }

  function saveSensorFields(tab, formData) {
    saveIconColorFields(tab, formData);
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
    resetIconColorFields(tab);
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
    loadIconColorFields(tab, data);
    const entity = document.getElementById(tab + '_binary_sensor_entity');
    const configured = data.sensor_entity || data.binary_sensor_entity || '';
    if (entity) {
      entity.value = configured;
    }
    const font = document.getElementById(tab + '_binary_sensor_value_font');
    if (font) font.value = normalizeSensorValueFont(data.sensor_value_font);
    const popup = document.getElementById(
      tab + '_binary_sensor_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
  }

  function saveBinarySensorFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const entityEl = document.getElementById(tab + '_binary_sensor_entity');
    const entity = entityEl
      ? entityEl.value : '';
    formData.append('binary_sensor_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('sensor_value_font', document.getElementById(tab + '_binary_sensor_value_font')?.value || '0');
    const popup = document.getElementById(
      tab + '_binary_sensor_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetBinarySensorFields(tab) {
    resetIconColorFields(tab);
    const font = document.getElementById(tab + '_binary_sensor_value_font');
    if (font) font.value = '0';
    const entity = document.getElementById(tab + '_binary_sensor_entity');
    if (entity) entity.value = '';
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
    loadIconColorFields(tab, data);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_energy_entity');
    if (entityEl) {
      entityEl.value = data.sensor_entity || data.energy_entity || '';
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
    saveIconColorFields(tab, formData);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_energy_entity');
    const entity = entityEl ? entityEl.value : '';
    formData.append('energy_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('sensor_unit', document.getElementById(prefix + '_energy_unit')?.value || '');
    formData.append('sensor_decimals', document.getElementById(prefix + '_energy_decimals')?.value || '1');
    formData.append('sensor_value_font', document.getElementById(prefix + '_energy_value_font')?.value || '0');
    formData.append('popup_open_mode', document.getElementById(prefix + '_energy_popup_open_mode')?.value || '1');
    formData.append('sensor_value_y_offset', document.getElementById(prefix + '_energy_value_y_offset')?.value || '');
  }

  function resetEnergyFields(tab) {
    resetIconColorFields(tab);
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
// Generated by tools/generate-weather-icon-fonts.mjs from
// tools/weather-icons/parts.mjs. Do not edit by hand.
// The device's filled, multi-color weather icons (weather_icon_table.h) as SVG
// for the Web Admin preview: shared layer paths in the 24-unit MDI grid, and
// per icon its weather color and its layers bottom to top.
  const WEATHER_ICON_LAYER_PATHS = Object.freeze([
    '<path d="M12,7A5,5 0 0,1 17,12A5,5 0 0,1 12,17A5,5 0 0,1 7,12A5,5 0 0,1 12,7Z"/><path d="M12,2L14.39,5.42C13.65,5.15 12.84,5 12,5C11.16,5 10.35,5.15 9.61,5.42L12,2Z"/><path d="M3.34,7L7.5,6.65C6.9,7.16 6.36,7.78 5.94,8.5C5.5,9.24 5.25,10 5.11,10.79L3.34,7Z"/><path d="M3.36,17L5.12,13.23C5.26,14 5.53,14.78 5.95,15.5C6.37,16.24 6.91,16.86 7.5,17.37L3.36,17Z"/><path d="M20.65,7L18.88,10.79C18.74,10 18.47,9.23 18.05,8.5C17.63,7.78 17.1,7.15 16.5,6.64L20.65,7Z"/><path d="M20.64,17L16.5,17.36C17.09,16.85 17.62,16.22 18.04,15.5C18.46,14.77 18.73,14 18.87,13.21L20.64,17Z"/><path d="M12,22L9.59,18.56C10.33,18.83 11.14,19 12,19C12.82,19 13.63,18.83 14.37,18.56L12,22Z"/>',
    '<path d="M18.97,15.95C19.8,15.87 20.69,17.05 20.16,17.8C19.84,18.25 19.5,18.67 19.08,19.07C15.17,23 8.84,23 4.94,19.07C1.03,15.17 1.03,8.83 4.94,4.93C5.34,4.53 5.76,4.17 6.21,3.85C6.96,3.32 8.14,4.21 8.06,5.04C7.79,7.9 8.75,10.87 10.95,13.06C13.14,15.26 16.1,16.22 18.97,15.95Z"/>',
    '<path d="M17.75,4.09L15.22,6.03L16.13,9.09L13.5,7.28L10.87,9.09L11.78,6.03L9.25,4.09L12.44,4L13.5,1L14.56,4L17.75,4.09Z"/><path d="M21.25,11L19.61,12.25L20.2,14.23L18.5,13.06L16.8,14.23L17.39,12.25L15.75,11L17.81,10.95L18.5,9L19.19,10.95L21.25,11Z"/>',
    '<path d="M5,10.5A5.5,5.5 0 1,1 16,10.5A5.5,5.5 0 1,1 5,10.5Z"/><path d="M13.55,3.64C13,3.4 12.45,3.23 11.88,3.12L14.37,1.82L15.27,4.71C14.76,4.29 14.19,3.93 13.55,3.64Z"/><path d="M6.09,4.44C5.6,4.79 5.17,5.19 4.8,5.63L4.91,2.82L7.87,3.5C7.25,3.71 6.65,4.03 6.09,4.44Z"/><path d="M18,9.71C17.91,9.12 17.78,8.55 17.59,8L19.97,9.5L17.92,11.73C18.03,11.08 18.05,10.4 18,9.71Z"/><path d="M3.04,11.3C3.11,11.9 3.24,12.47 3.43,13L1.06,11.5L3.1,9.28C3,9.93 2.97,10.61 3.04,11.3Z"/>',
    '<path d="M6,16A6,6 0 1,1 18,16A6,6 0 1,1 6,16Z"/><path d="M2,18A4,4 0 1,1 10,18A4,4 0 1,1 2,18Z"/><path d="M16,19A3,3 0 1,1 22,19A3,3 0 1,1 16,19Z"/><path d="M6,18H12V22H6Z"/><path d="M12,16H19V22H12Z"/>',
    '<path d="M18.97,15.95C19.8,15.87 20.69,17.05 20.16,17.8C19.84,18.25 19.5,18.67 19.08,19.07C15.17,23 8.84,23 4.94,19.07C1.03,15.17 1.03,8.83 4.94,4.93C5.34,4.53 5.76,4.17 6.21,3.85C6.96,3.32 8.14,4.21 8.06,5.04C7.79,7.9 8.75,10.87 10.95,13.06C13.14,15.26 16.1,16.22 18.97,15.95Z" transform="translate(8.26 -1.05) scale(0.62)"/>',
    '<path d="M6,19A5,5 0 0,1 1,14A5,5 0 0,1 6,9C7,6.65 9.3,5 12,5C15.43,5 18.24,7.66 18.5,11.03L19,11A4,4 0 0,1 23,15A4,4 0 0,1 19,19H6Z" transform="translate(8.88 -0.1) scale(0.62)"/>',
    '<path d="M6,19A5,5 0 0,1 1,14A5,5 0 0,1 6,9C7,6.65 9.3,5 12,5C15.43,5 18.24,7.66 18.5,11.03L19,11A4,4 0 0,1 23,15A4,4 0 0,1 19,19H6Z" transform="translate(0 1.5) scale(1)"/>',
    '<path d="M1,12A5,5 0 0,1 6,7C7,4.65 9.3,3 12,3C15.43,3 18.24,5.66 18.5,9.03L19,9C21.19,9 22.97,10.76 23,13H1.1L1,12Z"/>',
    '<path d="M3,15H13A1,1 0 0,1 14,16A1,1 0 0,1 13,17H3A1,1 0 0,1 2,16A1,1 0 0,1 3,15Z"/><path d="M16,15H21A1,1 0 0,1 22,16A1,1 0 0,1 21,17H16A1,1 0 0,1 15,16A1,1 0 0,1 16,15Z"/><path d="M3,19H5A1,1 0 0,1 6,20A1,1 0 0,1 5,21H3A1,1 0 0,1 2,20A1,1 0 0,1 3,19Z"/><path d="M8,19H21A1,1 0 0,1 22,20A1,1 0 0,1 21,21H8A1,1 0 0,1 7,20A1,1 0 0,1 8,19Z"/>',
    '<path d="M6,16A5,5 0 0,1 1,11A5,5 0 0,1 6,6C7,3.65 9.3,2 12,2C15.43,2 18.24,4.66 18.5,8.03L19,8A4,4 0 0,1 23,12A4,4 0 0,1 19,16H6Z" transform="translate(1.68 0.28) scale(0.86)"/>',
    '<path d="M18.5,18.67C18.5,19.96 17.5,21 16.25,21C15,21 14,19.96 14,18.67C14,17.12 16.25,14.5 16.25,14.5C16.25,14.5 18.5,17.12 18.5,18.67Z" transform="translate(-8.25 1.2) scale(1)"/><path d="M18.5,18.67C18.5,19.96 17.5,21 16.25,21C15,21 14,19.96 14,18.67C14,17.12 16.25,14.5 16.25,14.5C16.25,14.5 18.5,17.12 18.5,18.67Z" transform="translate(-2.25 1.2) scale(1)"/>',
    '<path d="M7.683,15.95L6.483,20.35A0.95,0.95 0 0,0 8.317,20.85L9.517,16.45A0.95,0.95 0 0,0 7.683,15.95Z"/><path d="M11.682,15.955L10.082,21.955A0.95,0.95 0 0,0 11.918,22.445L13.518,16.445A0.95,0.95 0 0,0 11.682,15.955Z"/><path d="M15.683,15.95L14.483,20.35A0.95,0.95 0 0,0 16.317,20.85L17.517,16.45A0.95,0.95 0 0,0 15.683,15.95Z"/>',
    '<path d="M7.88,18.07L10.07,17.5L8.46,15.88C8.07,15.5 8.07,14.86 8.46,14.46C8.85,14.07 9.5,14.07 9.88,14.46L11.5,16.07L12.07,13.88C12.21,13.34 12.76,13.03 13.29,13.17C13.83,13.31 14.14,13.86 14,14.4L13.41,16.59L15.6,16C16.14,15.86 16.69,16.17 16.83,16.71C16.97,17.24 16.66,17.79 16.12,17.93L13.93,18.5L15.54,20.12C15.93,20.5 15.93,21.15 15.54,21.54C15.15,21.93 14.5,21.93 14.12,21.54L12.5,19.93L11.93,22.12C11.79,22.66 11.24,22.97 10.71,22.83C10.17,22.69 9.86,22.14 10,21.6L10.59,19.41L8.4,20C7.86,20.14 7.31,19.83 7.17,19.29C7.03,18.76 7.34,18.21 7.88,18.07Z" transform="translate(3.36 6.04) scale(0.72)"/>',
    '<path d="M7.88,18.07L10.07,17.5L8.46,15.88C8.07,15.5 8.07,14.86 8.46,14.46C8.85,14.07 9.5,14.07 9.88,14.46L11.5,16.07L12.07,13.88C12.21,13.34 12.76,13.03 13.29,13.17C13.83,13.31 14.14,13.86 14,14.4L13.41,16.59L15.6,16C16.14,15.86 16.69,16.17 16.83,16.71C16.97,17.24 16.66,17.79 16.12,17.93L13.93,18.5L15.54,20.12C15.93,20.5 15.93,21.15 15.54,21.54C15.15,21.93 14.5,21.93 14.12,21.54L12.5,19.93L11.93,22.12C11.79,22.66 11.24,22.97 10.71,22.83C10.17,22.69 9.86,22.14 10,21.6L10.59,19.41L8.4,20C7.86,20.14 7.31,19.83 7.17,19.29C7.03,18.76 7.34,18.21 7.88,18.07Z" transform="translate(1.36 8.04) scale(0.62)"/>',
    '<path d="M18.5,18.67C18.5,19.96 17.5,21 16.25,21C15,21 14,19.96 14,18.67C14,17.12 16.25,14.5 16.25,14.5C16.25,14.5 18.5,17.12 18.5,18.67Z" transform="translate(-1 1.2) scale(1)"/>',
    '<path d="M6.8,19.8A1.7,1.7 0 1,1 10.2,19.8A1.7,1.7 0 1,1 6.8,19.8Z"/><path d="M10.5,17.4A1.5,1.5 0 1,1 13.5,17.4A1.5,1.5 0 1,1 10.5,17.4Z"/><path d="M13.8,19.8A1.7,1.7 0 1,1 17.2,19.8A1.7,1.7 0 1,1 13.8,19.8Z"/>',
    '<path d="M12,11H15L13,15H15L11.25,22L12,17H9.5L12,11Z" transform="translate(0 1) scale(1)"/>',
    '<path d="M12,11H15L13,15H15L11.25,22L12,17H9.5L12,11Z" transform="translate(-2.5 1) scale(1)"/>',
    '<path d="M18.5,18.67C18.5,19.96 17.5,21 16.25,21C15,21 14,19.96 14,18.67C14,17.12 16.25,14.5 16.25,14.5C16.25,14.5 18.5,17.12 18.5,18.67Z" transform="translate(-0.5 1.2) scale(1)"/>',
    '<path d="M4,10A1,1 0 0,1 3,9A1,1 0 0,1 4,8H12A2,2 0 0,0 14,6A2,2 0 0,0 12,4C11.45,4 10.95,4.22 10.59,4.59C10.2,5 9.56,5 9.17,4.59C8.78,4.2 8.78,3.56 9.17,3.17C9.9,2.45 10.9,2 12,2A4,4 0 0,1 16,6A4,4 0 0,1 12,10H4Z"/><path d="M19,12A1,1 0 0,0 20,11A1,1 0 0,0 19,10C18.72,10 18.47,10.11 18.29,10.29C17.9,10.68 17.27,10.68 16.88,10.29C16.5,9.9 16.5,9.27 16.88,8.88C17.42,8.34 18.17,8 19,8A3,3 0 0,1 22,11A3,3 0 0,1 19,14H5A1,1 0 0,1 4,13A1,1 0 0,1 5,12H19Z"/><path d="M18,18H4A1,1 0 0,1 3,17A1,1 0 0,1 4,16H18A3,3 0 0,1 21,19A3,3 0 0,1 18,22C17.17,22 16.42,21.66 15.88,21.12C15.5,20.73 15.5,20.1 15.88,19.71C16.27,19.32 16.9,19.32 17.29,19.71C17.47,19.89 17.72,20 18,20A1,1 0 0,0 19,19A1,1 0 0,0 18,18Z"/>',
    '<path d="M6,6L6.69,6.06C7.32,3.72 9.46,2 12,2A5.5,5.5 0 0,1 17.5,7.5L17.42,8.45C17.88,8.16 18.42,8 19,8A3,3 0 0,1 22,11A3,3 0 0,1 19,14H6A4,4 0 0,1 2,10A4,4 0 0,1 6,6Z"/>',
    '<path d="M18,18H4A1,1 0 0,1 3,17A1,1 0 0,1 4,16H18A3,3 0 0,1 21,19A3,3 0 0,1 18,22C17.17,22 16.42,21.66 15.88,21.12C15.5,20.73 15.5,20.1 15.88,19.71C16.27,19.32 16.9,19.32 17.29,19.71C17.47,19.89 17.72,20 18,20A1,1 0 0,0 19,19A1,1 0 0,0 18,18Z"/>',
    '<path d="M13,13H11V7H13M13,17H11V15H13M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2Z"/>',
  ]);
  const WEATHER_ICONS = Object.freeze({
    'weather-sunny': {tint: '#FFB224', layers: [['#FFC53D', 0]]},
    'weather-night': {tint: '#5E7092', layers: [['#FFE08A', 1], ['#FFFFFF', 2]]},
    'weather-partly-cloudy': {tint: '#CCAB6B', layers: [['#FFC53D', 3], ['#EEF2F7', 4]]},
    'weather-night-partly-cloudy': {tint: '#7C8AA2', layers: [['#FFE08A', 5], ['#C3CCD8', 4]]},
    'weather-cloudy': {tint: '#9AA4B2', layers: [['#8E9BB0', 6], ['#C3CCD8', 7]]},
    'weather-fog': {tint: '#B8BEC6', layers: [['#C3CCD8', 8], ['#8E9BB0', 9]]},
    'weather-rainy': {tint: '#4A7FC0', layers: [['#EEF2F7', 10], ['#4DA3FF', 11]]},
    'weather-pouring': {tint: '#2F5FB0', layers: [['#C3CCD8', 10], ['#4DA3FF', 12]]},
    'weather-snowy': {tint: '#7CC4F0', layers: [['#EEF2F7', 10], ['#CFE8FF', 13]]},
    'weather-snowy-rainy': {tint: '#63A2D8', layers: [['#EEF2F7', 10], ['#CFE8FF', 14], ['#4DA3FF', 15]]},
    'weather-hail': {tint: '#5FB4D8', layers: [['#C3CCD8', 10], ['#CFE8FF', 16]]},
    'weather-lightning': {tint: '#8B5CF6', layers: [['#9EAABA', 10], ['#FFC53D', 17]]},
    'weather-lightning-rainy': {tint: '#6B6EDB', layers: [['#9EAABA', 10], ['#FFC53D', 18], ['#4DA3FF', 19]]},
    'weather-windy': {tint: '#4FB8B0', layers: [['#A9C7E8', 20]]},
    'weather-windy-variant': {tint: '#75AEB1', layers: [['#C3CCD8', 21], ['#A9C7E8', 22]]},
    'alert-circle-outline': {tint: '#E5533D', layers: [['#FFB020', 23]]},
  });

function maybeFillTitleFromWeather(tab) {
    maybeFillTitleFromEntity(tab, '_weather_entity');
  }

  function loadWeatherFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const el = document.getElementById(prefix + '_weather_entity');
    if (el) el.value = data.sensor_entity || data.weather_entity || '';
    const popupModeEl = document.getElementById(prefix + '_weather_popup_open_mode');
    if (popupModeEl) popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    const colored = document.getElementById(prefix + '_weather_colored_icons');
    if (colored) colored.checked = data?.weather_colored_icons !== undefined ? !['0', 'false'].includes(String(data.weather_colored_icons)) : Number(data?.sensor_display_mode) !== 1;
    maybeFillTitleFromWeather(tab);
  }

  function saveWeatherFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    formData.append('weather_entity', document.getElementById(prefix + '_weather_entity')?.value || '');
    formData.append('popup_open_mode', document.getElementById(prefix + '_weather_popup_open_mode')?.value || '1');
    const colored = document.getElementById(prefix + '_weather_colored_icons');
    if (colored) formData.append('weather_colored_icons', colored.checked ? '1' : '0');
  }

  function resetWeatherFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const el = document.getElementById(prefix + '_weather_entity');
    if (el) el.value = '';
    const popupModeEl = document.getElementById(prefix + '_weather_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
    const colored = document.getElementById(prefix + '_weather_colored_icons');
    if (colored) colored.checked = true;
  }

  // --- Tile preview -------------------------------------------------------
  // What types/weather/renderer.cpp builds and update_weather_tile_state()
  // (tiles/runtime/tile_renderer.cpp) fills from the cached payload, at the
  // device positions (WEATHER_TILE_LAYOUT, display pixels).
  const WEATHER_CONDITION_ICONS = Object.freeze({
    'clear-night': 'weather-night', cloudy: 'weather-cloudy', exceptional: 'alert-circle-outline',
    fog: 'weather-fog', hail: 'weather-hail', lightning: 'weather-lightning',
    'lightning-rainy': 'weather-lightning-rainy', partlycloudy: 'weather-partly-cloudy',
    pouring: 'weather-pouring', rainy: 'weather-rainy', snowy: 'weather-snowy',
    'snowy-rainy': 'weather-snowy-rainy', sunny: 'weather-sunny', windy: 'weather-windy',
    'windy-variant': 'weather-windy-variant'
  });

  // A number or a numeric string (extract_json_number_or_string_field).
  function weatherPreviewNumber(value) {
    if (typeof value === 'number') return Number.isFinite(value) ? value : null;
    const text = String(value ?? '').trim().replace(',', '.');
    const number = text ? parseFloat(text) : NaN;
    return Number.isFinite(number) ? number : null;
  }

  function weatherPreviewString(value) {
    return typeof value === 'string' ? value : '';
  }

  function weatherIsoParts(iso) {
    const match = /^(\d{4})-(\d{2})-(\d{2})/.exec(String(iso || ''));
    if (!match) return null;
    const [y, m, d] = match.slice(1).map(Number);
    return y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31 ? Date.UTC(y, m - 1, d) : null;
  }

  function weatherIsoDate(utcMs) {
    const date = new Date(utcMs);
    return date.getUTCFullYear() + '-' + String(date.getUTCMonth() + 1).padStart(2, '0') + '-' +
      String(date.getUTCDate()).padStart(2, '0');
  }

  function weatherLocalToday(now) {
    return now.getFullYear() + '-' + String(now.getMonth() + 1).padStart(2, '0') + '-' +
      String(now.getDate()).padStart(2, '0');
  }

  // i18n::weather_weekday_short: the first ten characters as a date.
  function weatherWeekdayShort(iso) {
    const day = weatherIsoParts(iso);
    return day === null ? '' : (WEATHER_I18N.weekdaysShort[new Date(day).getUTCDay()] || '');
  }

  // i18n::weather_condition_label.
  function weatherConditionLabel(condition) {
    const key = String(condition || '').trim().toLowerCase();
    if (!key) return '--';
    if (WEATHER_I18N.conditions[key]) return WEATHER_I18N.conditions[key];
    const text = String(condition).replaceAll('-', ' ').replaceAll('_', ' ').trim();
    return text || '--';
  }

  // weather_icons::for_now: partly cloudy and sunny turn into their night
  // icons between the bridge's sunset and sunrise of today.
  function weatherIconForNow(name, sun, now) {
    if (!Array.isArray(sun)) return name;
    const today = weatherLocalToday(now);
    const minute = now.getHours() * 60 + now.getMinutes();
    const days = sun.filter(day => day && typeof day.d === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(day.d) &&
      (day.up !== undefined || (Number.isInteger(day.r) && Number.isInteger(day.s) &&
                                day.r >= 0 && day.r < day.s && day.s <= 1440))).slice(0, 8);
    const day = days.find(entry => entry.d === today);
    if (!day) return name;
    const night = day.up !== undefined ? day.up !== true : (minute < day.r || minute >= day.s);
    if (!night) return name;
    if (name === 'weather-partly-cloudy') return 'weather-night-partly-cloudy';
    if (name === 'weather-sunny') return 'weather-night';
    return name;
  }

  function parseWeatherPreviewPayload(raw) {
    let data = raw;
    if (typeof raw === 'string') {
      if (!raw.trim()) return null;
      try { data = JSON.parse(raw); } catch (_) { return null; }
    }
    if (!data || typeof data !== 'object' || Array.isArray(data)) return null;
    // An empty state falls back to the condition (extract_json_string_field).
    const condition = weatherPreviewString(data.state).trim() || weatherPreviewString(data.condition).trim();
    let icon = normalizeMdiIconName(weatherPreviewString(data.icon));
    if (!icon) icon = WEATHER_CONDITION_ICONS[condition.trim().toLowerCase()] || '';
    const units = data.units && typeof data.units === 'object' ? data.units : null;
    return {
      condition,
      icon: icon ? weatherIconForNow(icon, data.sun, new Date()) : '',
      temperature: weatherPreviewNumber(data.temperature),
      unit: units ? weatherPreviewString(units.temperature) : weatherPreviewString(data.temperature_unit),
      forecast: Array.isArray(data.forecast) ? data.forecast.filter(entry => entry && typeof entry === 'object') : []
    };
  }

  function weatherPreviewTemp(value) {
    return formatLocalizedNumber(value, 1, true);
  }

  // The device's filled weather icon as SVG layers (admin-icons.js); single
  // draws every layer in the label color (an icon color the user chose).
  function weatherIconSvg(name, single) {
    const entry = WEATHER_ICONS[name];
    if (!entry) return '';
    return '<svg class="weather-icon-svg" viewBox="0 0 24 24" aria-hidden="true">' +
      entry.layers.map(([color, index]) => '<g fill="' + (single ? 'currentColor' : color) + '">' +
        WEATHER_ICON_LAYER_PATHS[index] + '</g>').join('') + '</svg>';
  }

  // weather_forecast_gap()/weather_forecast_count() (widgets.h): as many days
  // as fit the card's real width with even room between and around their
  // widest text (textW, measured by the device in its language).
  function weatherForecastGap(textW) {
    return Math.trunc(textW * 85 / 100);
  }
  function weatherForecastCount(cardW, textW) {
    const gap = weatherForecastGap(textW);
    const days = textW + gap > 0 ? Math.trunc((cardW - gap) / (textW + gap)) : 1;
    return Math.max(1, Math.min(8, days));
  }

  // tile_geometry::extent in display pixels.
  function weatherExtent(position, span, cell, gap) {
    const edge = value => Math.round(value * (cell + gap));
    return edge(position + span) - edge(position) - gap;
  }

  // Fills a rendered weather preview tile: the header icon, condition |
  // temperature and the forecast columns. iconName is the tile's initial icon
  // (before a state arrives), forcedColor an icon color of the user or a rule.
  // A text's width in display pixels as LVGL lays it out: whole-pixel glyph
  // advances plus kerning (WEATHER_TILE_LAYOUT adv/kern, lv_text_get_size);
  // null when the table lacks a glyph.
  function weatherDeviceTextWidth(text, font) {
    if (!font?.adv) return null;
    const chars = [...String(text)];
    let width = 0;
    for (let i = 0; i < chars.length; ++i) {
      const advance = font.adv[chars[i]];
      if (advance === undefined) return null;
      width += advance + ((i + 1 < chars.length && font.kern?.[chars[i] + chars[i + 1]]) || 0);
    }
    return width;
  }

  function applyWeatherPreview(el, state, tile, iconName, forcedColor) {
    if (!el || typeof WEATHER_TILE_LAYOUT === 'undefined') return;
    const L = WEATHER_TILE_LAYOUT;
    // The tile's own grid sizes (inherited variables), not the page's.
    const rootStyle = getComputedStyle(el.isConnected ? el : document.documentElement);
    const scale = parseFloat(rootStyle.getPropertyValue('--radius-preview-scale')) || 0.5;
    const colored = Number(tile?.sensor_display_mode) !== 1;
    const now = new Date();

    // Header icon: the current weather icon, colored in its layers with the
    // weather color as the label color (disc and Tile color From icon).
    let icon = el.querySelector(':scope > .tile-icon');
    const name = state ? state.icon : normalizeMdiIconName(iconName);
    // A state shows its icon even when the tile's own icon is off.
    if (!icon && name) {
      el.insertAdjacentHTML('afterbegin', '<i class="mdi tile-icon"></i>');
      icon = el.querySelector(':scope > .tile-icon');
    }
    if (icon && !name) icon.remove();
    else if (icon) {
      const entry = WEATHER_ICONS[name];
      if (colored && entry) {
        icon.className = 'mdi tile-icon tile-weather-icon';
        icon.innerHTML = weatherIconSvg(name, !!forcedColor);
        icon.style.color = forcedColor || entry.tint;
      } else {
        icon.className = 'mdi mdi-' + name + ' tile-icon';
        icon.innerHTML = '';
        icon.style.color = forcedColor || '';
      }
    }

    // Display pixels of the card and the preview's (border box) size.
    const col = Number(tile?.col) || 0;
    const row = Number(tile?.row) || 0;
    const spanW = Math.max(1, Number(tile?.span_w) || 1);
    const spanH = Math.max(1, Number(tile?.span_h) || 1);
    const cellW = parseFloat(rootStyle.getPropertyValue('--preview-cell-w'));
    const cellH = parseFloat(rootStyle.getPropertyValue('--preview-cell-h'));
    const previewGap = parseFloat(rootStyle.getPropertyValue('--preview-gap')) || 0;
    // The tile's own grid in display pixels: the layout window shows other
    // layouts' cells than this boot's (WEATHER_TILE_LAYOUT).
    const gridCellW = cellW > 0 ? Math.round(cellW / scale) : L.cellW;
    const gridCellH = cellH > 0 ? Math.round(cellH / scale) : L.cellH;
    const gridGap = previewGap > 0 ? Math.round(previewGap / scale) : L.gap;
    const cardW = weatherExtent(col, spanW, gridCellW, gridGap);
    const cardH = weatherExtent(row, spanH, gridCellH, gridGap);
    // The tile's size like the grid gives it (.fractional-tile), never
    // measured: a re-rendered tile at a half position is placed only after
    // it is filled.
    const width = cellW > 0 ? spanW * (cellW + previewGap) - previewGap : el.offsetWidth;
    const height = cellH > 0 ? Math.max(1, Number(tile?.span_h) || 1) * (cellH + previewGap) - previewGap
      : el.offsetHeight;
    const px = value => (value * scale).toFixed(2) + 'px';
    // Unrounded sizes, the glyphs on the LVGL baseline as this browser draws
    // them (text-baseline.js).
    const font = f => {
      const size = Math.max(6, f.px * scale);
      const line = f.line * scale;
      return {size, line, shift: typeof previewBaselineShift === 'function'
        ? previewBaselineShift(size, line, (f.line - f.base) * scale)
        : (f.line / 2 - f.base - 0.364 * f.px) * scale};
    };
    const fontCss = f => 'font-size:' + f.size.toFixed(2) + 'px;line-height:' + f.line.toFixed(2) + 'px;';
    const family = getComputedStyle(el).fontFamily || 'sans-serif';
    const measure = (text, f) => {
      const context = (weatherPreviewMeasure.context ||= document.createElement('canvas').getContext('2d'));
      if (!context) return 0;
      context.font = '400 ' + f.size + 'px ' + family;
      return context.measureText(text).width;
    };
    // Positions below are in the border box; absolute children start inside
    // the 3 px editor border.
    const at = (left, top) => 'left:' + (left - 3).toFixed(2) + 'px;top:' + (top - 3).toFixed(2) + 'px;';

    // Condition | temperature.
    const valueFont = font(L.value);
    const hasTemp = !!state && state.temperature !== null;
    const tempText = hasTemp ? weatherPreviewTemp(state.temperature) + (state.unit ? ' ' + state.unit : '') : '--';
    const conditionText = state ? weatherConditionLabel(state.condition) : '--';
    // The card's real size decides (widgets.h): the condition whenever it
    // fits, below the narrow width only completely; the forecast row from
    // the device's two-row height.
    let showCondition = conditionText !== '--';
    let room = 0;
    if (showCondition) {
      const gap = L.valueGap * scale;
      room = width - 2 * L.padH * scale - measure(tempText, valueFont) - measure('|', valueFont) - 2 * gap;
      const conditionWidth = measure(conditionText, valueFont);
      if (cardW < L.conditionNarrowW ? conditionWidth > room : room < L.minConditionRoom * scale) showCondition = false;
    }
    const showForecast = cardH >= L.forecastMinH;
    const rowCenter = showForecast ? (gridCellH / 2 + L.valueDy) * scale : height / 2 + L.valueDy * scale;
    const textSpan = (cls, text, f, extra = '') => '<span class="' + cls + '" style="' + fontCss(f) +
      'top:' + f.shift.toFixed(2) + 'px;' + extra + '">' + escapeHtml(text) + '</span>';
    let html = '<div class="weather-preview-row" style="top:' + (rowCenter - valueFont.line / 2 - 3).toFixed(2) +
      'px;height:' + valueFont.line.toFixed(2) + 'px;gap:' + px(L.valueGap) + '">';
    if (showCondition) {
      html += textSpan('weather-preview-condition', conditionText, valueFont, 'max-width:' + Math.max(0, room).toFixed(2) + 'px;');
      if (hasTemp) html += textSpan('weather-preview-separator', '|', valueFont);
    }
    html += textSpan('weather-preview-temp', tempText, valueFont) + '</div>';

    // Forecast columns, spread evenly over the card, the row anchored to the
    // card's bottom like on the device.
    const count = showForecast ? weatherForecastCount(cardW, L.forecastTextW) : 0;
    if (count > 0) {
      const today = weatherLocalToday(now);
      const slots = Array.from({length: count}, () => null);
      let base = today;
      let fallback = 0;
      for (const entry of state ? state.forecast : []) {
        const datetime = weatherPreviewString(entry.datetime);
        let dateLocal = weatherPreviewString(entry.date_local);
        if (!dateLocal && datetime.length >= 10) dateLocal = datetime.slice(0, 10);
        const low = ['templow', 'temperature_low', 'temp_low', 'low']
          .map(key => weatherPreviewNumber(entry[key])).find(value => value !== null) ?? null;
        const conditionIcon = WEATHER_CONDITION_ICONS[weatherPreviewString(entry.condition).trim().toLowerCase()] || '';
        const slot = {
          dateLocal,
          day: datetime ? weatherWeekdayShort(datetime) : '',
          icon: normalizeMdiIconName(weatherPreviewString(entry.icon)) || conditionIcon,
          high: weatherPreviewNumber(entry.temperature),
          low
        };
        let index = -1;
        const baseDay = weatherIsoParts(base);
        const day = weatherIsoParts(dateLocal);
        if (baseDay !== null && day !== null) {
          const offset = Math.round((day - baseDay) / 86400000);
          if (offset >= 0 && offset < count) index = offset;
        }
        if (index < 0) {
          while (fallback < count && slots[fallback]) ++fallback;
          if (fallback < count) index = fallback++;
        }
        if (index >= 0) slots[index] = slot;
      }

      // Even room around the days' widest text, each column centred on its
      // day (renderer.cpp), in display pixels with the device's integer
      // division so the columns do not drift apart from the device's.
      const textW = L.forecastTextW;
      const spacing = Math.trunc((cardW - count * textW) / (count + 1));
      const columnLeft = i => (spacing + Math.trunc(textW / 2) + i * (textW + spacing) - Math.trunc(L.colW / 2)) * scale;
      const rowTop = height - (gridCellH + L.headroom - L.yOffset) * scale;
      const contentTop = rowTop + L.padV * scale;
      const contentW = (L.colW - 2 * L.padH) * scale;
      const dayFont = font(L.day);
      const tempFont = font(L.temp);
      const unitFont = font(L.unit);
      const unitText = L.unitGap + (state?.unit || '°C');
      const baseDay = weatherIsoParts(base);
      for (let i = 0; i < count; ++i) {
        const slot = slots[i];
        const left = columnLeft(i) + L.padH * scale;
        const displayDate = slot?.dateLocal || (baseDay !== null ? weatherIsoDate(baseDay + i * 86400000) : '');
        let dayText = slot?.day || '';
        if ((i === 0 && (slot || displayDate)) || displayDate === today) dayText = WEATHER_I18N.today;
        else if (!dayText && displayDate) dayText = weatherWeekdayShort(displayDate);
        // Until a first state arrives the device shows white placeholders.
        if (!state) dayText = '';
        html += '<div class="weather-preview-day" style="' + at(left, contentTop + L.dayTop * scale + dayFont.shift) +
          'width:' + contentW.toFixed(2) + 'px;' + fontCss(dayFont) +
          'color:' + (slot || !state ? '#FFFFFF' : '#7F8BAA') + '">' + escapeHtml(dayText || '--') + '</div>';
        if (!slot) continue;
        if (slot.icon) {
          const svg = colored ? weatherIconSvg(slot.icon, false) : '';
          html += '<div class="weather-preview-icon" style="' +
            at(left, contentTop + (L.iconTop + L.iconEmDy) * scale) + 'width:' + contentW.toFixed(2) + 'px;">' +
            (svg || '<i class="mdi mdi-' + escapeHtml(slot.icon) + '"></i>') + '</div>';
        }
        for (const [value, top] of [[slot.high, L.tempTop], [slot.low, L.lowTop]]) {
          if (value === null) continue;
          const text = weatherPreviewTemp(value);
          let valueWidth = measure(text, tempFont);
          let x;
          const valueDevice = weatherDeviceTextWidth(text, L.temp);
          const unitDevice = weatherDeviceTextWidth(unitText, L.unit);
          if (valueDevice !== null && unitDevice !== null) {
            // The device's whole-pixel layout (position_tile_value_unit_centered).
            const contentDevice = L.colW - 2 * L.padH;
            const total = valueDevice + unitDevice;
            let xDevice = Math.trunc(contentDevice / 2) - Math.trunc(total / 2);
            if (xDevice < 0) xDevice = 0;
            if (xDevice + total > contentDevice) xDevice = contentDevice - total;
            x = xDevice * scale;
            valueWidth = valueDevice * scale;
          } else {
            const total = valueWidth + measure(unitText, unitFont);
            x = contentW / 2 - total / 2;
            if (x < 0) x = 0;
            if (x + total > contentW) x = contentW - total;
          }
          const y = contentTop + top * scale;
          html += '<div class="weather-preview-temp-value" style="' + at(left + x, y + tempFont.shift) + fontCss(tempFont) + '">' +
            escapeHtml(text) + '</div>';
          html += '<div class="weather-preview-temp-unit" style="' +
            at(left + x + valueWidth, y + L.unitDy * scale + unitFont.shift) + fontCss(unitFont) + '">' +
            escapeHtml(unitText) + '</div>';
        }
      }
    }
    el.querySelectorAll(':scope > .weather-preview-row, :scope > .weather-preview-day, ' +
      ':scope > .weather-preview-icon, :scope > .weather-preview-temp-value, ' +
      ':scope > .weather-preview-temp-unit').forEach(node => node.remove());
    const handles = el.querySelector(':scope > .tile-resize-handle');
    if (handles) handles.insertAdjacentHTML('beforebegin', html);
    else el.insertAdjacentHTML('beforeend', html);
  }
  const weatherPreviewMeasure = {context: null};

function maybeFillTitleFromScene(tab) {
    const prefix = tab;
    const typeSel = document.getElementById(prefix + '_tile_type');
    const titleInput = document.getElementById(prefix + '_tile_title');
    const sceneSel = document.getElementById(prefix + '_scene_alias');
    if (!typeSel || !titleInput || !sceneSel) return;
    if (typeSel.value !== '2') return;
    if (titleInput.value && titleInput.value.trim().length) return;
    if (!sceneSel.value) return;
    const title = entityPickerName(sceneSel) || sceneSel.value;
    if (title.trim().length) titleInput.value = title.trim();
  }

  function loadSceneFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const sceneEl = document.getElementById(prefix + '_scene_alias');
    if (sceneEl) sceneEl.value = data.scene_alias || '';
    maybeFillTitleFromScene(tab);
  }

  function saveSceneFields(tab, formData) {
    const prefix = tab;
    formData.append('scene_alias', document.getElementById(prefix + '_scene_alias')?.value || '');
    saveIconColorFields(tab, formData);
  }

  function resetSceneFields(tab) {
    const prefix = tab;
    const sceneEl = document.getElementById(prefix + '_scene_alias');
    if (sceneEl) sceneEl.value = '';
    resetIconColorFields(tab);
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
        const labelEl = btn.querySelector('.tab-label');
        if (labelEl) labelEl.textContent = label;
        // The circle always holds an icon; without one the folder icon.
        const iconEl = btn.querySelector('.tab-disc i.mdi');
        if (iconEl) iconEl.className = 'mdi mdi-' + (iconName || (folderNum === 0 ? 'home' : 'folder'));
      }
    }
    document.querySelectorAll('select[id$="_navigate_target"]').forEach(select => {
      const opt = select.querySelector('option[value="' + folderNum + '"]');
      if (opt) opt.textContent = label;
    });
  }

  function loadNavigateFields(tab, data) {
    loadIconColorFields(tab, data);
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
    saveIconColorFields(tab, formData);
  }

  function resetNavigateFields(tab) {
    resetIconColorFields(tab);
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
      // The tile shows the lock of a protected Folder (previewTileLocked).
      if (typeof updateTilePreview === 'function') updateTilePreview(tab);
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

  // Back tile: the same per-tile border flag as Clock and Text.
  function loadBackFields(tab, data) {
    loadIconColorFields(tab, data);
    const border = document.getElementById(tab + '_back_tile_border');
    if (border) border.checked = data?.tile_border !== undefined ? !['0','false'].includes(String(data.tile_border)) : Number(data?.sensor_display_mode) !== 1;
  }

  function saveBackFields(tab, formData) {
    formData.append('tile_border', document.getElementById(tab + '_back_tile_border')?.checked === false ? '0' : '1');
    saveIconColorFields(tab, formData);
  }

  function resetBackFields(tab) {
    const border = document.getElementById(tab + '_back_tile_border');
    if (border) border.checked = true;
    resetIconColorFields(tab);
  }

function maybeFillTitleFromSwitch(tab) {
    maybeFillTitleFromEntity(tab, '_switch_entity');
  }

  const SWITCH_ICON_ON = '#FFD54F';
  const SWITCH_ICON_OFF = '#B0B0B0';
  // Layouts (src/types/switch/layout.h): 0 icon button, 1 switch, 2 dimmer,
  // 3 automatic. New tiles start with Automatic.
  const SWITCH_LAYOUT_NEW_TILE = '3';
  // Home Assistant color modes that dim (every mode except onoff).
  // Mirrors parse_switch_payload() (tile_renderer.cpp).
  const SWITCH_DIMMING_MODES = ['brightness', 'color_temp', 'hs', 'rgb', 'xy', 'rgbw', 'rgbww'];

  // The Sensor value size choices (Tile::sensor_value_font 0..5). One row
  // high the state shows the half-height sizes (compact_sensor_layout::
  // value_step), from 1.5 rows the full-size ones; the editor offers the
  // fitting ones (syncCompactValueFontOptions).
  function switchValueFont(value) {
    const v = String(value ?? '0');
    return ['1', '2', '3', '4', '5'].includes(v) ? v : '0';
  }

  // switch_layout::sensor_look: from 1.5 rows a header layout looks like a
  // Sensor tile with the bar below.
  function switchSensorLook(style, spanH) {
    return switchLayoutValue(style) !== 0 && Number(spanH) > 1;
  }

  // The segmented choices of the Switch fields (like Tile color). The hidden
  // select of each keeps the value; a button sets it and fires its change
  // event, so the existing preview, draft and autosave bindings run.
  // The Climate tile's Layout uses the same segmented choice.
  const SWITCH_CHOICE_FIELDS = ['switch_style', 'switch_value_font', 'switch_popup_open_mode', 'climate_view',
    'cover_value_font', 'lock_value_font', 'alarm_value_font', 'fan_value_font'];

  function syncSwitchChoices(tab) {
    for (const field of SWITCH_CHOICE_FIELDS) {
      const select = document.getElementById(tab + '_' + field);
      const group = document.getElementById(tab + '_' + field + '_choices');
      if (!select || !group?.querySelectorAll) continue;
      const options = select.options ? Array.from(select.options) : [];
      for (const button of group.querySelectorAll('button[data-value]')) {
        button.classList.toggle('active', button.dataset.value === String(select.value));
        // Value sizes this tile size does not offer stay hidden.
        const option = options.find(o => o.value === button.dataset.value);
        button.classList.toggle('hidden', !!option?.hidden);
      }
    }
  }

  function setSwitchChoice(tab, field, value) {
    const select = document.getElementById(tab + '_' + field);
    if (!select) return;
    if (String(select.value) !== String(value)) {
      select.value = String(value);
      if (typeof select.dispatchEvent === 'function') select.dispatchEvent(new Event('change', {bubbles: true}));
    }
    syncSwitchChoices(tab);
  }

  function switchLayoutValue(value) {
    const layout = Number(value);
    return [0, 1, 2, 3].includes(layout) ? layout : 0;
  }

  // Header layouts (Switch, Dimmer, Automatic) and every half-height Switch
  // tile use the Sensor header; only a full-size icon button does not.
  function switchUsesHeader(style, halfHeight) {
    return !!halfHeight || switchLayoutValue(style) !== 0;
  }

  // switch_layout::bar_for(): no bar at half height or for the icon button.
  function switchBarKind(style, halfHeight, dimmable) {
    const layout = switchLayoutValue(style);
    if (halfHeight || layout === 0) return 'none';
    if (layout === 1) return 'toggle';
    return dimmable ? 'dimmer' : 'toggle';
  }

  // Markup after the icon and title of a header layout: the state line and,
  // at full height, the control bar.
  function switchPreviewExtraHtml(style, halfHeight) {
    if (!switchUsesHeader(style, halfHeight)) return '';
    let html = '<div class="tile-value tile-switch-state">--</div>';
    if (!halfHeight) {
      html += '<div class="tile-switch" data-bar="none">' +
        '<div class="tile-switch-fill"><span class="tile-switch-handle"></span></div>' +
        '<div class="tile-switch-knob"><i class="mdi mdi-power tile-switch-symbol"></i></div>' +
        '</div>';
    }
    return html;
  }

  // The tile classes and layout of a Switch preview (live and cached grid).
  function applySwitchPreviewLayout(tileElem, style, halfHeight) {
    if (!tileElem) return;
    tileElem.dataset.switchLayout = String(switchLayoutValue(style));
    tileElem.classList.toggle('switch-bar', switchUsesHeader(style, halfHeight) && !halfHeight);
  }

  // switch_layout::Dimmer: the fill stays inside the bar's shape; its end
  // has a quarter of the bar height as rounding, growing into the bar's
  // radius only near the full end. The smallest piece is the bar radius plus
  // that rounding wide (tangential, no edge) with the handle in its middle.
  // `baseHeight`: the one-row bar height; a taller bar keeps its end
  // rounding and handle width, only the handle grows in height.
  function switchDimmerGeometry(width, height, radius, baseHeight = 0) {
    const reference = baseHeight > 0 ? baseHeight : height;
    const endRadius = Math.floor(reference / 4);
    const minFill = Math.min(radius + endRadius, width);
    const margin = Math.floor(minFill / 2);
    const low = minFill - margin;
    const high = width - margin;
    return {
      margin,
      minFill,
      endRadius,
      handleWidth: Math.max(3, Math.floor(reference * 7 / 100)),
      handleHeight: Math.floor(height * 42 / 100),
      handleX(value) {
        if (value <= 1 || high <= low) return low;
        if (value >= 100) return high;
        return low + Math.floor(((value - 1) * (high - low) + 49) / 99);
      },
      fillWidth(value) {
        if (!value) return 0;
        return Math.min(width, this.handleX(value) + margin);
      },
      endRadiusFor(fill) {
        if (fill <= width - radius || endRadius >= radius) return endRadius;
        const corner = width - radius;
        for (let end = endRadius; end < radius; end++) {
          let inside = true;
          for (let x = fill - end; x <= fill && inside; x++) {
            const dxEnd = x - (fill - end);
            const endTop = end - Math.sqrt(Math.max(0, end * end - dxEnd * dxEnd));
            const dxBar = x - corner;
            const barTop = dxBar <= 0 ? 0 : radius - Math.sqrt(Math.max(0, radius * radius - dxBar * dxBar));
            if (endTop + 0.01 < barTop) inside = false;
          }
          if (inside) return end;
        }
        return radius;
      }
    };
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
      unknown: false,
      hasColor: false,
      color: null,
      hasBrightness: false,
      brightness: 0,
      supportsBrightness: false
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
            } else if (normalizedState === 'unknown') {
              out.unknown = true;
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
          // Like parse_switch_payload(): brightness_pct, else brightness 0..255.
          const pct = Number(obj.brightness_pct);
          const raw = Number(obj.brightness);
          if (obj.brightness_pct !== undefined && obj.brightness_pct !== null && Number.isFinite(pct)) {
            out.hasBrightness = true;
            out.brightness = Math.max(0, Math.min(100, Math.round(pct)));
          } else if (obj.brightness !== undefined && obj.brightness !== null && Number.isFinite(raw)) {
            out.hasBrightness = true;
            out.brightness = Math.max(0, Math.min(100, Math.round(raw / 255 * 100)));
          }
          // supported_color_modes decides; color_mode only when it is absent.
          if (Array.isArray(obj.supported_color_modes)) {
            out.supportsBrightness = obj.supported_color_modes.some(mode =>
              SWITCH_DIMMING_MODES.includes(String(mode).toLowerCase()));
          } else if (obj.color_mode) {
            out.supportsBrightness = SWITCH_DIMMING_MODES.includes(String(obj.color_mode).toLowerCase());
          }
        }
      } catch (e) {}
    }

    if (text.toLowerCase() === 'unavailable') {
      out.available = false;
    }
    if (text.toLowerCase() === 'unknown') {
      out.unknown = true;
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

  // Resize preview (drag-resize.js): the copy of a Switch tile at its old
  // size takes the parts of the new size like the device: no bar at half
  // height, the header and bar of the layout from one row. Before the
  // compact classes, so the tall look follows too.
  function prepareSwitchResizePreview(preview, data, layout) {
    if (!preview || !data) return;
    const half = Number(layout?.span_h) === 0.5;
    const oldState = Array.from(preview.children).find(el => el.classList.contains('tile-switch-state'));
    preview.dataset.switchStateText = oldState ? oldState.textContent : '';
    for (const el of Array.from(preview.children)) {
      if (el.classList.contains('tile-switch-state') || el.classList.contains('tile-switch')) el.remove();
    }
    preview.insertAdjacentHTML('beforeend', switchPreviewExtraHtml(data.switch_style, half));
    applySwitchPreviewLayout(preview, data.switch_style, half);
  }

  // Once the copy is in the grid (the bar measures itself): the state from
  // the entity cache, else the text the tile showed.
  function finishSwitchResizePreview(preview, data) {
    if (!preview || !data) return;
    const entity = data.sensor_entity || '';
    const values = (typeof sensorMetaCache === 'object' && sensorMetaCache?.values) || {};
    if (entity && values[entity] !== undefined) {
      applySwitchPreviewState(preview, parseSwitchPayload(values[entity]), entity);
      return;
    }
    const label = Array.from(preview.children).find(el => el.classList.contains('tile-switch-state'));
    if (label && preview.dataset.switchStateText) label.textContent = preview.dataset.switchStateText;
  }

  function applySwitchPreviewState(tileElem, state, entity) {
    if (!tileElem) return;
    applySwitchPreviewColors(tileElem, state);
    // The disc and the control fill follow the state color like the device.
    applyIconDiscTint(tileElem);
    applySwitchPreviewBar(tileElem, state, entity);
  }

  function applySwitchPreviewColors(tileElem, state) {
    const iconEl = tileElem.querySelector('.tile-icon');
    if (!iconEl) return;
    if (state.available === false) {
      iconEl.style.color = SWITCH_ICON_OFF;
      return;
    }
    if (!state.hasState && !state.hasColor) return;
    const isOn = state.hasState ? state.isOn : state.hasColor;
    iconEl.style.color = isOn ? (state.hasColor ? state.color : SWITCH_ICON_ON) : SWITCH_ICON_OFF;
  }

  // show_state_text(): the same words as the device, from the central
  // translations (SWITCH_I18N, types/switch/web_scripts.cpp).
  function switchPreviewStateText(state, dimmable, level, on) {
    const i18n = typeof SWITCH_I18N === 'object' ? SWITCH_I18N : {};
    if (state.available === false) return i18n.unavailable || 'Unavailable';
    if (state.unknown) return i18n.unknown || 'Unknown';
    if (!state.hasState && !state.hasBrightness) return '--';
    if (!on) return i18n.off || 'Off';
    if (dimmable) return level + ' %';
    return i18n.on || 'On';
  }

  function applySwitchPreviewBar(tileElem, state, entity) {
    // Each appears once per tile, directly in it.
    const label = tileElem.querySelector('.tile-switch-state');
    const bar = tileElem.querySelector('.tile-switch');
    if (!label && !bar) return;
    const halfHeight = tileElem.classList.contains('sensor-half');
    const style = tileElem.dataset.switchLayout || '0';
    const dimmable = String(entity || '').startsWith('light.') && !!state.supportsBrightness;
    const available = state.available !== false;
    let on = state.hasState ? !!state.isOn : (!!state.hasBrightness && state.brightness > 0);
    if (!available) on = false;
    // A dimmable light on without a reported brightness: On without a level.
    const level = on ? (state.hasBrightness ? Math.max(1, state.brightness) : (dimmable ? 0 : 100)) : 0;
    if (label) label.textContent = switchPreviewStateText(state, dimmable && level > 0, level, on);
    if (!bar) return;
    const kind = switchBarKind(style, halfHeight, dimmable);
    bar.dataset.bar = kind;
    bar.classList.toggle('is-on', on);
    bar.classList.toggle('is-unavailable', !available);
    // Accent = the color the icon shows, card = the tile, thumb off = one
    // OKLCH step above the control fill (tone_color::lifted).
    const iconEl = tileElem.querySelector('.tile-icon');
    const accent = iconEl ? getComputedStyle(iconEl).color : (state.hasColor ? state.color : SWITCH_ICON_ON);
    bar.style.setProperty('--switch-accent', accent);
    bar.style.setProperty('--switch-card', getComputedStyle(tileElem).backgroundColor);
    const control = cssColorChannels(getComputedStyle(bar).backgroundColor);
    if (control) {
      const base = toneOklch(control);
      bar.style.setProperty('--switch-thumb-off', toneHex(toneRgb(base.L + 0.06, base.C, base.h)));
    }
    const symbol = bar.querySelector('.tile-switch-symbol');
    if (symbol) symbol.className = 'mdi ' + (on ? 'mdi-power' : 'mdi-circle-outline') + ' tile-switch-symbol';
    bar.__switchFill = {kind, level};
    drawSwitchPreviewFill(bar);
    // The fill is measured in pixels, but the grid gives the tile its size
    // only after the render (layoutTiles), and a hidden tab measures 0: the
    // fill used the one-cell bar. It is drawn again whenever the bar's size
    // changes.
    if (switchBarObserver && !bar.__switchObserved) {
      bar.__switchObserved = true;
      switchBarObserver.observe(bar);
    }
  }

  const switchBarObserver = typeof ResizeObserver === 'function'
    ? new ResizeObserver(entries => {
      for (const entry of entries) {
        const bar = entry.target;
        if (!bar.isConnected) {
          switchBarObserver.unobserve(bar);
          bar.__switchObserved = false;
          continue;
        }
        // A bar whose content depends on its width (the Alarm panel's
        // mode slots) rebuilds itself first.
        if (typeof bar.__onResize === 'function') bar.__onResize();
        drawSwitchPreviewFill(bar);
      }
    })
    : null;

  function drawSwitchPreviewFill(bar) {
    const fill = bar?.querySelector('.tile-switch-fill');
    const drawn = bar?.__switchFill;
    if (!fill || !drawn) return;
    const {kind, level} = drawn;
    const width = bar.clientWidth;
    const height = bar.clientHeight;
    const radius = Math.min(parseFloat(getComputedStyle(bar).borderTopLeftRadius) || 0, height / 2);
    const base = parseFloat(getComputedStyle(bar).getPropertyValue('--switch-bar-height')) || height;
    const geometry = switchDimmerGeometry(width, height, radius, Math.min(base, height));
    const fillWidth = kind === 'dimmer' ? geometry.fillWidth(level) : 0;
    const endRadius = geometry.endRadiusFor(fillWidth);
    // The fill element starts one end radius left of the bar, so only the
    // bar's round start shows (overflow hidden).
    fill.style.width = (fillWidth ? fillWidth + endRadius : 0) + 'px';
    bar.style.setProperty('--switch-end-radius', endRadius + 'px');
    bar.style.setProperty('--switch-handle-margin', geometry.margin + 'px');
    bar.style.setProperty('--switch-handle-w', geometry.handleWidth + 'px');
    bar.style.setProperty('--switch-handle-h', geometry.handleHeight + 'px');
  }

  function updateSwitchValuePreview(tab) {
    if (currentTileIndex === -1) return;
    const prefix = tab;
    const entitySelect = document.getElementById(prefix + '_switch_entity');
    if (!entitySelect) return;
    const entity = entitySelect.value;
    const tileElem = document.getElementById(tab + '-tile-' + currentTileIndex);
    if (!entity || !tileElem) return;
    const applyMeta = (meta) => {
      const values = (meta && meta.values) || {};
      const state = parseSwitchPayload(values[entity] ?? '');
      applySwitchPreviewState(tileElem, state, entity);
    };
    const metaPromise = isSensorMetaCacheLoaded() ? Promise.resolve(sensorMetaCache) : fetchSensorMetaCache();
    metaPromise
      .then(meta => applyMeta(meta))
      .catch(err => console.error('Switch state load failed:', err));
  }

  function loadSwitchFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_switch_entity');
    if (entityEl) entityEl.value = data.sensor_entity || data.switch_entity || '';
    const styleEl = document.getElementById(prefix + '_switch_style');
    if (styleEl) {
      styleEl.value = (data.switch_style !== undefined && data.switch_style !== null)
        ? String(switchLayoutValue(data.switch_style)) : '0';
    }
    const fontEl = document.getElementById(prefix + '_switch_value_font');
    if (fontEl) fontEl.value = switchValueFont(data.sensor_value_font);
    const popupModeEl = document.getElementById(prefix + '_switch_popup_open_mode');
    if (popupModeEl) {
      popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    }
    syncSwitchChoices(tab);
    maybeFillTitleFromSwitch(tab);
  }

  function saveSwitchFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    formData.append('switch_entity', document.getElementById(prefix + '_switch_entity')?.value || '');
    const styleEl = document.getElementById(prefix + '_switch_style');
    formData.append('switch_style', styleEl ? String(switchLayoutValue(styleEl.value)) : '0');
    formData.append('sensor_value_font', switchValueFont(document.getElementById(prefix + '_switch_value_font')?.value));
    formData.append('popup_open_mode', document.getElementById(prefix + '_switch_popup_open_mode')?.value || '1');
  }

  function resetSwitchFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_switch_entity');
    if (entityEl) entityEl.value = '';
    const styleEl = document.getElementById(prefix + '_switch_style');
    if (styleEl) styleEl.value = SWITCH_LAYOUT_NEW_TILE;
    const fontEl = document.getElementById(prefix + '_switch_value_font');
    if (fontEl) fontEl.value = '0';
    const popupModeEl = document.getElementById(prefix + '_switch_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
    syncSwitchChoices(tab);
  }

  function parseCoverPreviewPayload(value) {
    const out = {
      state: 'unknown',
      position: null,
      tiltPosition: null,
      deviceClass: '',
      available: true,
      // A state was reported (the device's CoverState::valid).
      reported: false,
      supportedFeatures: null
    };
    if (value === undefined || value === null) return out;
    const text = String(value).trim();
    if (!text.length) return out;
    out.reported = true;
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
      const features = obj.supported_features ?? attrs.supported_features;
      if (features !== undefined && features !== null && Number.isFinite(Number(features))) {
        out.supportedFeatures = Math.max(0, Math.min(255, Math.round(Number(features))));
      }
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

  // The state color of icon and position fill (cover renderer
  // cover_icon_color): closed keeps the active color, unknown and
  // unavailable take the inactive grey.
  function coverPreviewColor(state) {
    const value = String(state?.state || 'unknown').toLowerCase();
    if (state?.available === false || value === 'unknown' || value === 'unavailable') {
      return '#9e9e9e';
    }
    return '#926bc7';
  }

  // A full tile of a Cover with a position (or not reported yet) shows the
  // header and the position bar (cover renderer show_view); without
  // supported_features the device assumes open, close and stop, plus the
  // position when one is reported. Bit 4 is SET_POSITION.
  function coverPreviewPositionable(state) {
    if (!state || !state.reported) return true;
    const features = state.supportedFeatures ?? (11 | (state.position !== null ? 4 : 0));
    return (features & 4) !== 0;
  }

  // cover_state_line(): "Open · 58 %".
  function coverPreviewStateLine(state) {
    if (!state || !state.reported) return '--';
    const text = coverPreviewStateText(state);
    return state.available !== false && state.position !== null ? text + ' \u00B7 ' + state.position + ' %' : text;
  }

  // Markup after the icon and title: half height and full tiles of a
  // positionable Cover show the state line beside the disc, full tiles also
  // the position bar (the Switch preview's bar); other Covers keep the
  // centered state and position.
  function coverPreviewExtraHtml(state, halfHeight) {
    if (!halfHeight && !coverPreviewPositionable(state)) {
      const value = state?.position !== null && state?.position !== undefined
        ? String(state.position) + '%' : '--%';
      return '<div class="tile-value tile-cover-value">' + escapeHtml(coverPreviewStateText(state)) +
        '<br>' + escapeHtml(value) + '</div>';
    }
    let html = '<div class="tile-value tile-switch-state">' + escapeHtml(coverPreviewStateLine(state)) + '</div>';
    if (!halfHeight) {
      html += '<div class="tile-switch" data-bar="dimmer">' +
        '<div class="tile-switch-fill"><span class="tile-switch-handle"></span></div></div>';
    }
    return html;
  }

  // The header layout class and the position fill, drawn like the Switch
  // dimmer (drawSwitchPreviewFill) in the state color with the handle in the
  // tile color.
  function applyCoverPreview(tileElem, state, halfHeight) {
    if (!tileElem) return;
    tileElem.classList.toggle('switch-bar', !halfHeight && coverPreviewPositionable(state));
    const bar = tileElem.querySelector('.tile-switch');
    if (!bar) return;
    const available = state?.available !== false && !!state?.reported;
    // The closed part like Home Assistant's cover position feature (and the
    // popup's shutter): 75 % open fills a quarter; fully open keeps the
    // smallest piece with the handle (cover renderer cover_fill_level).
    const level = available && state.position !== null ? Math.max(1, 100 - state.position) : 0;
    bar.dataset.bar = 'dimmer';
    bar.classList.toggle('is-unavailable', !available);
    bar.style.setProperty('--switch-accent', coverPreviewColor(state));
    bar.style.setProperty('--switch-card', getComputedStyle(tileElem).backgroundColor);
    bar.__switchFill = {kind: 'dimmer', level};
    drawSwitchPreviewFill(bar);
    if (switchBarObserver && !bar.__switchObserved) {
      bar.__switchObserved = true;
      switchBarObserver.observe(bar);
    }
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
    loadIconColorFields(tab, data);
    const entity = document.getElementById(tab + '_cover_entity');
    const configured = data.sensor_entity || data.cover_entity || '';
    if (entity) {
      entity.value = configured;
    }
    // State size like the Switch tile (Tile::sensor_value_font).
    const font = document.getElementById(tab + '_cover_value_font');
    if (font) font.value = switchValueFont(data.sensor_value_font);
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
    maybeFillTitleFromEntity(tab, '_cover_entity');
  }

  function saveCoverFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const entity = document.getElementById(tab + '_cover_entity')?.value || '';
    formData.append('cover_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('sensor_value_font', switchValueFont(document.getElementById(tab + '_cover_value_font')?.value));
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetCoverFields(tab) {
    resetIconColorFields(tab);
    const entity = document.getElementById(tab + '_cover_entity');
    if (entity) entity.value = '';
    const font = document.getElementById(tab + '_cover_value_font');
    if (font) font.value = '0';
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) popup.value = '1';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
  }
  // Lock (24), Alarm panel (25) and Fan (26): the Switch tile's header with
  // a control bar (types/device/device_tile.cpp). The preview mirrors the
  // device: the state colors and icons of device_visual.h, the bar of
  // bar_model() and the words of DEVICE_I18N (types/device/web.cpp).
  const DEVICE_COLORS = {green: '#4CAF50', red: '#F44336', orange: '#FF9800', grey: '#9E9E9E', cyan: '#00BCD4'};
  // Home Assistant's alarm modes: state, feature bit, icon.
  const DEVICE_ALARM_MODES = [
    ['armed_home', 1, 'home'], ['armed_away', 2, 'lock'], ['armed_night', 4, 'moon-waning-crescent'],
    ['armed_vacation', 32, 'airplane'], ['armed_custom_bypass', 16, 'shield']
  ];

  function devicePreviewKind(type) {
    const value = String(type);
    return value === '24' ? 'lock' : (value === '25' ? 'alarm' : (value === '26' ? 'fan' : ''));
  }

  // The retained detail state (/api/sensor_values device_values).
  function deviceDetailPayload(entity, meta = sensorMetaCache) {
    return entity ? (meta?.deviceValues?.[entity] ?? '') : '';
  }

  // device_detail::parse(): a missing state stays apart from a known one.
  function parseDevicePreviewPayload(value) {
    const out = {valid: false, available: false, state: '', features: 0, percentage: null,
      step: null, preset: ''};
    const text = value === undefined || value === null ? '' : String(value).trim();
    if (!text.startsWith('{')) return out;
    try {
      const obj = JSON.parse(text);
      if (!obj || typeof obj !== 'object') return out;
      out.valid = true;
      out.state = typeof obj.state === 'string' ? obj.state.toLowerCase() : '';
      out.available = !!out.state && out.state !== 'unavailable';
      out.features = Number.isFinite(Number(obj.supported_features)) ? Number(obj.supported_features) : 0;
      if (typeof obj.percentage === 'number' && Number.isFinite(obj.percentage)) {
        out.percentage = Math.max(0, Math.min(100, Math.round(obj.percentage)));
      }
      if (typeof obj.percentage_step === 'number' && obj.percentage_step > 0 && obj.percentage_step <= 100) {
        out.step = obj.percentage_step;
      }
      out.preset = typeof obj.preset_mode === 'string' ? obj.preset_mode : '';
    } catch (e) {}
    return out;
  }

  function deviceSpeedCount(d) {
    if (!d.step) return 100;
    return Math.max(1, Math.min(100, Math.round(100 / d.step)));
  }

  function deviceLevelText(count, percentage) {
    const i18n = typeof DEVICE_I18N === 'object' ? DEVICE_I18N.fan : {};
    if (count < 100) {
      const speed = Math.max(1, Math.min(count, Math.round(percentage * count / 100)));
      return String(i18n.speed || 'Speed %u').replace('%u', String(speed));
    }
    return percentage + ' %';
  }

  // device_visual::visual(): label, icon and state color.
  function devicePreviewVisual(kind, d) {
    const words = typeof DEVICE_I18N === 'object' ? (DEVICE_I18N[kind] || {}) : {};
    const out = {label: '--', icon: kind === 'lock' ? 'lock' : (kind === 'alarm' ? 'shield' : 'fan-off'),
      color: DEVICE_COLORS.grey, waiting: false};
    if (!d.valid) return out;
    const state = d.available ? d.state : 'unavailable';
    if (kind === 'lock') {
      out.label = words[state] || words.unknown || state;
      if (!d.available) return out;
      if (state === 'locked') out.color = DEVICE_COLORS.green;
      else if (state === 'unlocked' || state === 'open') {
        out.icon = 'lock-open-variant';
        out.color = DEVICE_COLORS.red;
      } else if (['locking', 'unlocking', 'opening'].includes(state)) {
        out.icon = 'lock-clock';
        out.color = DEVICE_COLORS.orange;
        out.waiting = true;
      } else if (state === 'jammed') {
        out.icon = 'lock-alert';
        out.color = DEVICE_COLORS.red;
        out.waiting = true;
      }
      return out;
    }
    if (kind === 'alarm') {
      out.label = words[state] || words.unknown || state;
      if (!d.available) return out;
      const looks = {
        disarmed: ['shield-off', 'grey'], armed_home: ['shield-home', 'green'], armed_away: ['shield-lock', 'green'],
        armed_night: ['shield-moon', 'green'], armed_vacation: ['shield-airplane', 'green'],
        armed_custom_bypass: ['security', 'green'], arming: ['shield', 'orange'], pending: ['shield-outline', 'orange'],
        disarming: ['shield', 'orange'], triggered: ['bell-ring', 'red']
      };
      const look = looks[state];
      if (look) {
        out.icon = look[0];
        out.color = DEVICE_COLORS[look[1]];
        out.waiting = look[1] === 'orange' || state === 'triggered';
      }
      return out;
    }
    if (!d.available) {
      out.label = words.unavailable || 'Unavailable';
      return out;
    }
    if (state === 'unknown') {
      out.label = words.unknown || 'Unknown';
      return out;
    }
    if (state !== 'on') {
      out.label = words.off || 'Off';
      return out;
    }
    out.icon = 'fan';
    out.color = DEVICE_COLORS.cyan;
    if (d.preset) {
      const preset = d.preset.replace(/_/g, ' ');
      out.label = preset.charAt(0).toUpperCase() + preset.slice(1);
    } else if ((d.features & 1) && d.percentage !== null) {
      out.label = deviceLevelText(deviceSpeedCount(d), d.percentage);
    } else {
      out.label = words.on || 'On';
    }
    return out;
  }

  function devicePreviewIcon(kind, d, fallback = '') {
    return fallback || devicePreviewVisual(kind, d).icon;
  }

  function devicePreviewColor(kind, d) {
    return devicePreviewVisual(kind, d).color;
  }

  // The state line beside the disc and, for full tiles, the bar.
  function devicePreviewExtraHtml(kind, d, halfHeight) {
    let html = '<div class="tile-value tile-switch-state">' + escapeHtml(devicePreviewVisual(kind, d).label) + '</div>';
    if (!halfHeight) {
      html += '<div class="tile-switch" data-bar="none">' +
        '<div class="tile-switch-fill"><span class="tile-switch-handle"></span></div>' +
        '<div class="tile-switch-knob"><i class="mdi tile-switch-symbol"></i></div>' +
        '<div class="tile-device-parts"></div></div>';
    }
    return html;
  }

  function devicePart(icon, cls) {
    return '<span class="tile-device-part' + (cls ? ' ' + cls : '') + '">' +
      (icon ? '<i class="mdi mdi-' + escapeHtml(icon) + '"></i>' : '') + '</span>';
  }

  // bar_model(): the bar the device draws for this state.
  function applyDevicePreview(tileElem, kind, d, halfHeight) {
    if (!tileElem) return;
    tileElem.classList.toggle('switch-bar', !halfHeight);
    const bar = tileElem.querySelector('.tile-switch');
    if (!bar) return;
    const visual = devicePreviewVisual(kind, d);
    const iconEl = tileElem.querySelector('.tile-icon');
    const accent = iconEl ? getComputedStyle(iconEl).color : visual.color;
    const card = getComputedStyle(tileElem).backgroundColor;
    bar.style.setProperty('--switch-accent', accent);
    bar.style.setProperty('--switch-card', card);
    const control = cssColorChannels(getComputedStyle(bar).backgroundColor);
    if (control) {
      const base = toneOklch(control);
      bar.style.setProperty('--switch-thumb-off', toneHex(toneRgb(base.L + 0.06, base.C, base.h)));
      bar.style.setProperty('--device-button', toneHex(toneRgb(base.L + 0.04, base.C, base.h)));
    }
    bar.classList.toggle('is-unavailable', !d.valid || !d.available);
    bar.classList.remove('is-on', 'knob-accent');
    const parts = bar.querySelector('.tile-device-parts');
    const symbol = bar.querySelector('.tile-switch-symbol');
    let barKind = 'none';
    let onResize = null;
    let partsHtml = '';
    let gapped = false;
    let level = 0;
    if (kind === 'lock') {
      const s = d.state;
      if (!['locked', 'locking', 'unlocked', 'unlocking', 'open', 'opening'].includes(s)) {
        barKind = 'parts';
        gapped = true;
        partsHtml = devicePart('lock-open-variant', 'button') + devicePart('lock', 'button');
      } else {
        barKind = 'toggle';
        bar.classList.toggle('is-on', s === 'locked' || s === 'locking');
        bar.classList.add('knob-accent');
        if (symbol) symbol.className = 'mdi mdi-' + visual.icon + ' tile-switch-symbol';
      }
    } else if (kind === 'alarm') {
      if (['arming', 'pending', 'triggered'].includes(d.state)) {
        barKind = 'parts';
        partsHtml = devicePart('shield-off', 'button');
      } else {
        barKind = 'parts';
        // alarm_slots(): Disarm first, then the supported modes in reverse.
        const iconWidth = parseFloat(getComputedStyle(tileElem).getPropertyValue('--icon-size')) || 24;
        const fitNow = () => Math.max(2, Math.min(6, Math.floor((bar.clientWidth || 60) / iconWidth)));
        const fit = fitNow();
        // The device counts the slots for the tile's size; here they follow
        // the bar's width, also when the tile is resized or laid out later
        // (a 2x1 tile kept the three slots of 1x1 and lit none).
        onResize = () => {
          if (fitNow() !== fit) applyDevicePreview(tileElem, kind, d, halfHeight);
        };
        const modes = DEVICE_ALARM_MODES.filter(mode => d.features & mode[1]).slice(0, fit - 1);
        const slots = [['disarmed', 0, 'shield-off'], ...modes.reverse()];
        partsHtml = slots.map(slot => devicePart(slot[2], slot[0] === d.state ? 'lit' : '')).join('');
      }
    } else {
      const on = d.state === 'on';
      const percentage = on ? (d.percentage ?? 100) : 0;
      const count = deviceSpeedCount(d);
      if (!(d.features & 1)) {
        barKind = 'toggle';
        bar.classList.toggle('is-on', on);
        if (symbol) symbol.className = 'mdi mdi-' + (on ? 'fan' : 'fan-off') + ' tile-switch-symbol';
      } else if (count >= 2 && count <= 4) {
        barKind = 'parts';
        gapped = true;
        const speed = percentage > 0 ? Math.max(1, Math.min(count, Math.round(percentage * count / 100))) : 0;
        for (let i = 0; i < count; ++i) partsHtml += devicePart('', i < speed ? 'lit' : 'button');
      } else {
        barKind = 'dimmer';
        level = on ? Math.max(1, percentage) : 0;
      }
    }
    bar.dataset.bar = barKind;
    if (parts) {
      parts.classList.toggle('gapped', gapped);
      parts.innerHTML = partsHtml;
    }
    bar.__switchFill = {kind: barKind === 'dimmer' ? 'dimmer' : 'none', level};
    bar.__onResize = onResize;
    drawSwitchPreviewFill(bar);
    if (switchBarObserver && !bar.__switchObserved) {
      bar.__switchObserved = true;
      switchBarObserver.observe(bar);
    }
  }

  function loadDeviceFields(tab, data, prefix) {
    loadIconColorFields(tab, data);
    const entity = document.getElementById(tab + '_' + prefix + '_entity');
    const configured = data.sensor_entity || data[prefix + '_entity'] || '';
    if (entity) {
      entity.value = configured;
    }
    // State size like the Switch tile (Tile::sensor_value_font).
    const font = document.getElementById(tab + '_' + prefix + '_value_font');
    if (font) font.value = switchValueFont(data.sensor_value_font);
    const popup = document.getElementById(tab + '_' + prefix + '_popup_open_mode');
    if (popup) popup.value = data.popup_open_mode !== undefined ? String(data.popup_open_mode) : '1';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
    maybeFillTitleFromEntity(tab, '_' + prefix + '_entity');
  }

  function saveDeviceFields(tab, formData, prefix) {
    saveIconColorFields(tab, formData);
    const entity = document.getElementById(tab + '_' + prefix + '_entity')?.value || '';
    formData.append(prefix + '_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('sensor_value_font',
      switchValueFont(document.getElementById(tab + '_' + prefix + '_value_font')?.value));
    const popup = document.getElementById(tab + '_' + prefix + '_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetDeviceFields(tab, prefix) {
    resetIconColorFields(tab);
    const entity = document.getElementById(tab + '_' + prefix + '_entity');
    if (entity) entity.value = '';
    const font = document.getElementById(tab + '_' + prefix + '_value_font');
    if (font) font.value = '0';
    const popup = document.getElementById(tab + '_' + prefix + '_popup_open_mode');
    if (popup) popup.value = '1';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
  }

  // The registry calls these by name (TileTypeDescriptor js_load/js_save/js_reset).
  function loadLockFields(tab, data) { loadDeviceFields(tab, data, 'lock'); }
  function saveLockFields(tab, formData) { saveDeviceFields(tab, formData, 'lock'); }
  function resetLockFields(tab) { resetDeviceFields(tab, 'lock'); }
  function loadAlarmFields(tab, data) { loadDeviceFields(tab, data, 'alarm'); }
  function saveAlarmFields(tab, formData) { saveDeviceFields(tab, formData, 'alarm'); }
  function resetAlarmFields(tab) { resetDeviceFields(tab, 'alarm'); }
  function loadFanFields(tab, data) { loadDeviceFields(tab, data, 'fan'); }
  function saveFanFields(tab, formData) { saveDeviceFields(tab, formData, 'fan'); }
  function resetFanFields(tab) { resetDeviceFields(tab, 'fan'); }

function maybeFillTitleFromMedia(tab) {
    maybeFillTitleFromEntity(tab, '_media_entity');
  }

  function updateMediaValuePreview(tab) {
    // The tile preview renders the media state with the tile
    // (applyMediaPreview in updateTilePreview); nothing to refresh here.
  }

  function loadMediaFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const el = document.getElementById(prefix + '_media_entity');
    if (el) el.value = data.sensor_entity || data.media_entity || '';
    maybeFillTitleFromMedia(tab);
    updateMediaValuePreview(tab);
  }

  function saveMediaFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    const entity = document.getElementById(prefix + '_media_entity')?.value || '';
    formData.append('media_entity', entity);
    formData.append('sensor_entity', entity);
  }

  function resetMediaFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const el = document.getElementById(prefix + '_media_entity');
    if (el) el.value = '';
  }

  // --- Tile preview -------------------------------------------------------
  // What types/media/renderer.cpp builds and update_media_tile_state()
  // (tiles/runtime/tile_renderer.cpp) fills from the cached payload, placed
  // by set_media_cover_text_layout() (content_layout.cpp) in display pixels
  // (MEDIA_TILE_LAYOUT).

  // sanitize_media_display_text().
  function mediaPreviewText(value) {
    return typeof value === 'string'
      ? value.replaceAll('`', "'").replace(/[\r\n]/g, ' ').trim() : '';
  }

  function parseMediaPreviewPayload(raw) {
    const text = String(raw ?? '').trim();
    if (!text) return null;
    if (!text.startsWith('{')) return {state: text};
    let data;
    try { data = JSON.parse(text); } catch (_) { return null; }
    if (!data || typeof data !== 'object') return null;
    const field = key => mediaPreviewText(data[key]);
    return {
      state: field('state'), title: field('media_title'), artist: field('media_artist'),
      album: field('media_album_name'), app: field('app_name'), source: field('source'),
      channel: field('media_channel'),
      cover: field('entity_picture') || field('media_image_url')
    };
  }

  // media_empty_title_label(): like Home Assistant, "Unavailable" and
  // "Unknown", else the player state or "No playback".
  function mediaPreviewEmptyTitle(state) {
    const key = String(state || '').trim().toLowerCase();
    const labels = {unavailable: 'unavailable', unknown: 'unknown', playing: 'playing', paused: 'paused',
                    idle: 'idle', standby: 'standby', off: 'off'};
    return MEDIA_I18N[labels[key]] || MEDIA_I18N.noPlayback;
  }

  // Positions the title and subtitle like set_media_cover_text_layout().
  function mediaPreviewTextLayout(L, width, height, large, coverVisible, hasSubtitle) {
    const top = large ? L.coverTop : L.coverTopSmall;
    const footer = L.button - L.buttonBottom + L.footerGap;
    const side = Math.max(1, Math.min(L.maxCover, Math.trunc(width * 42 / 100), height - top - footer));
    const textX = coverVisible ? side + L.textAfterCover : L.textLeft;
    const titleHeight = (large ? L.title : L.titleSmall).line;
    const gap = hasSubtitle ? L.subtitleGap : 0;
    const block = titleHeight + gap + (hasSubtitle ? L.subtitle.line : 0);
    const center = coverVisible ? top + Math.trunc(side / 2) : Math.trunc((top + height - footer) / 2);
    const titleY = Math.max(top, Math.min(center - Math.trunc(block / 2), height - footer - block));
    return {side, top, textX, textWidth: Math.max(1, width - textX - L.textRight), titleY,
            subtitleY: titleY + titleHeight + gap};
  }

  // "From cover" (tile_icon_source.cpp apply_cover): the icon color and the
  // tile tint take the color the panel sampled from the cover it shows. A
  // rule's tile tint and "From icon" win over the cover tint, a rule's icon
  // color over the cover icon color.
  function applyMediaCoverTint(el, record, coverColor) {
    if (!el || !coverColor || typeof parseIconColorRecord !== 'function') return;
    const parsed = parseIconColorRecord(record || '');
    const cover = parsed.cover || {icon: false, tile: 0};
    const layer = typeof iconColorRecordSource === 'function' ? iconColorRecordSource(record || '') : null;
    if (cover.icon && !(layer && layer.enabled && layer.icon)) {
      const icon = el.querySelector(':scope > .tile-icon');
      if (icon) icon.style.color = coverColor;
    }
    if (cover.tile && !parsed.fill && el.dataset.ruleTint !== '1' && typeof setTileTintBackground === 'function') {
      setTileTintBackground(el, coverColor, cover.tile);
    }
  }

  // An artwork loaded: the texts move beside it like on the device.
  function mediaPreviewCoverLoaded(image) {
    const tile = image?.closest('.tile');
    const cover = image?.parentElement;
    if (!tile || !cover) return;
    cover.hidden = false;
    tile.querySelectorAll(':scope > [data-cover-style]').forEach(node => {
      node.setAttribute('style', node.dataset.coverStyle);
    });
  }

  // Fills a rendered media preview tile: header fallbacks, title, subtitle,
  // artwork and the three controls. iconName is the tile's resolved icon,
  // displayName the entity's name for a tile without its own title.
  function applyMediaPreview(el, state, tile, iconName, displayName) {
    if (!el || typeof MEDIA_TILE_LAYOUT === 'undefined') return;
    const L = MEDIA_TILE_LAYOUT;
    // The tile's own grid sizes (inherited variables), not the page's: a grid
    // such as the screensaver's has its own cells.
    const rootStyle = getComputedStyle(el.isConnected ? el : document.documentElement);
    const scale = parseFloat(rootStyle.getPropertyValue('--radius-preview-scale')) || 0.5;
    const entity = String(tile?.sensor_entity || '');

    // Header: the television icon and the entity's name stand in like on
    // the device.
    if (!iconName && !el.querySelector(':scope > .tile-icon')) {
      el.insertAdjacentHTML('afterbegin', '<i class="mdi mdi-television tile-icon"></i>');
    }
    if (!el.querySelector(':scope > .tile-title')) {
      const name = displayName || (entity && typeof titleFromEntity === 'function' ? titleFromEntity(entity) : '') ||
        'Media';
      const icon = el.querySelector(':scope > .tile-icon');
      const title = '<div class="tile-title">' + tileTitleHtml(name) + '</div>';
      if (icon) icon.insertAdjacentHTML('afterend', title);
      else el.insertAdjacentHTML('afterbegin', title);
    }

    const spanW = Math.max(1, Number(tile?.span_w) || 1);
    const spanH = Math.max(1, Number(tile?.span_h) || 1);
    const large = spanW > 1 || spanH > 1;
    const cellW = parseFloat(rootStyle.getPropertyValue('--preview-cell-w'));
    const cellH = parseFloat(rootStyle.getPropertyValue('--preview-cell-h'));
    const previewGap = parseFloat(rootStyle.getPropertyValue('--preview-gap')) || 0;
    // The card in display pixels, from the preview tile's size like the grid
    // gives it (.fractional-tile), never measured: a re-rendered tile at a
    // half position is placed only after it is filled.
    const cardW = Math.round((cellW > 0 ? spanW * (cellW + previewGap) - previewGap : el.offsetWidth) / scale);
    const cardH = Math.round((cellH > 0 ? spanH * (cellH + previewGap) - previewGap : el.offsetHeight) / scale);
    const width = cardW - 2 * L.padH;
    const height = cardH - 2 * L.padV;
    // Unrounded sizes, the glyphs on the LVGL baseline as this browser draws
    // them (text-baseline.js).
    const font = f => {
      const size = Math.max(6, f.px * scale);
      const line = f.line * scale;
      return {size, line, shift: typeof previewBaselineShift === 'function'
        ? previewBaselineShift(size, line, (f.line - f.base) * scale)
        : (f.line / 2 - f.base - 0.364 * f.px) * scale};
    };
    const fontCss = f => 'font-size:' + f.size.toFixed(2) + 'px;line-height:' + f.line.toFixed(2) + 'px;';
    // Display pixels in the content area to the preview's absolute position
    // inside the 3 px editor border.
    const at = (x, y) => 'left:' + ((L.padH + x) * scale - 3).toFixed(2) + 'px;top:' +
      ((L.padV + y) * scale - 3).toFixed(2) + 'px;';

    const titleText = state ? (state.title || state.channel) : '';
    const mainText = titleText || mediaPreviewEmptyTitle(state?.state);
    let subtitle = state ? (state.artist || state.album || state.app || state.source) : '';
    if (subtitle && subtitle.toLowerCase() === mainText.trim().toLowerCase()) subtitle = '';
    const titleFont = font(large ? L.title : L.titleSmall);
    const subtitleFont = font(L.subtitle);
    const plain = mediaPreviewTextLayout(L, width, height, large, false, !!subtitle);
    const covered = mediaPreviewTextLayout(L, width, height, large, true, !!subtitle);
    const textStyle = (layout, y, f) => at(layout.textX, y) + 'width:' + (layout.textWidth * scale).toFixed(2) + 'px;' +
      fontCss(f) + 'margin-top:' + f.shift.toFixed(2) + 'px;';

    let html = '';
    const hasCover = !!state?.cover && /^(https?:|data:image\/)/i.test(state.cover);
    // The artwork already shown stays: a re-render (sensor refresh, layout)
    // only moves it, so it never blinks away until a new copy has loaded
    // (user 2026-10-08). A new artwork shows once it loaded.
    const shownCover = hasCover ? [...el.querySelectorAll(':scope > .media-preview-cover:not([hidden])')]
      .find(node => node.querySelector('img')?.getAttribute('src') === state.cover &&
        node.querySelector('img')?.complete) : null;
    const coverStyle = hasCover ? at(L.coverLeft, plain.top) + 'width:' + (plain.side * scale).toFixed(2) + 'px;height:' +
      (plain.side * scale).toFixed(2) + 'px;border-radius:' + (L.coverRadius * scale).toFixed(2) + 'px' : '';
    if (hasCover && !shownCover) {
      html += '<div class="media-preview-cover" hidden style="' + coverStyle + '">' +
        '<img src="' + escapeHtml(state.cover) + '" alt="" referrerpolicy="no-referrer" ' +
        'onload="mediaPreviewCoverLoaded(this)" onerror="this.parentElement.remove()"></div>';
    }
    const titleLayout = shownCover ? covered : plain;
    html += '<div class="media-preview-title' + (titleText ? '' : ' media-preview-state') + '" style="' +
      textStyle(titleLayout, titleLayout.titleY, titleFont) + '" data-cover-style="' +
      escapeHtml(textStyle(covered, covered.titleY, titleFont)) + '">' + escapeHtml(mainText) + '</div>';
    if (subtitle) {
      html += '<div class="media-preview-subtitle" style="' +
        textStyle(titleLayout, titleLayout.subtitleY, subtitleFont) +
        '" data-cover-style="' + escapeHtml(textStyle(covered, covered.subtitleY, subtitleFont)) + '">' +
        escapeHtml(subtitle) + '</div>';
    }

    // Previous, play/pause and next at the bottom middle; an unavailable
    // player dims them. Play is a white circle with the icon in the tile color.
    if (entity) {
      const unavailable = String(state?.state || '').trim().toLowerCase() === 'unavailable';
      const playing = String(state?.state || '').trim().toLowerCase() === 'playing';
      const cardColor = el.style.background || 'var(--tile-default-bg, #1A1A1A)';
      const buttonY = height - L.button + L.buttonBottom;
      const iconTop = (Math.trunc((L.button - L.iconLine) / 2) + L.iconEmDy) * scale;
      for (const [offset, icon, primary] of [[-L.buttonSide, 'skip-previous', false],
        [0, playing ? 'pause' : 'play', true], [L.buttonSide, 'skip-next', false]]) {
        const x = Math.trunc((width - L.button) / 2) + offset;
        html += '<div class="media-preview-control' + (primary ? ' media-preview-play' : '') +
          (unavailable ? ' media-preview-disabled' : '') + '" style="' + at(x, buttonY) +
          'width:' + (L.button * scale).toFixed(2) + 'px;height:' + (L.button * scale).toFixed(2) + 'px;' +
          (primary ? 'color:' + escapeHtml(cardColor) + ';' : '') + '">' +
          '<i class="mdi mdi-' + icon + '" style="top:' + iconTop.toFixed(2) + 'px"></i></div>';
      }
    }
    el.querySelectorAll(':scope > :is(.media-preview-cover, .media-preview-title, .media-preview-subtitle, ' +
      '.media-preview-control)').forEach(node => { if (node !== shownCover) node.remove(); });
    if (shownCover) shownCover.setAttribute('style', coverStyle);
    const handles = el.querySelector(':scope > .tile-resize-handle');
    if (handles) handles.insertAdjacentHTML('beforebegin', html);
    else el.insertAdjacentHTML('beforeend', html);
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

  // Width counts whole cells. The mini-grid has one row per half cell below
  // the header row, so half steps add a row: 1 -> 1, 1.5 -> 2, 2 -> 3
  // (climateTileGridRows on the device).
  function climateGridDimensions(spanW, spanH) {
    const columns = Math.max(
      1, Math.min(
        climateMaxGridColumns(), Math.floor(Number(spanW) || 1)));
    const halfRows = Math.max(
      2, Math.min(
        climateMaxOuterRows() * 2,
        Math.round((Number(spanH) || 1) * 2)));
    return {
      columns,
      rows: halfRows - 1
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

  // Items are placed top-left first (row, column, then item number) when the
  // tile has a stored mini-grid, so a smaller tile keeps what it can still
  // show and drops only the rest. Without stored geometry the item number is
  // the position (build_slot_kinds on the device).
  function climatePlacementOrderFor(geometry, hasStoredGeometry) {
    const order = [0, 1, 2, 3, 4, 5];
    if (!hasStoredGeometry) return order;
    const at = (index, key) => Number(geometry[index]?.[key]) || 0;
    return order.sort((a, b) =>
      at(a, 'row') - at(b, 'row') ||
      at(a, 'col') - at(b, 'col') ||
      a - b);
  }

  function climatePlacementOrder(tab, geometry) {
    const stored = document.getElementById(
      tab + '_climate_geometry')?.value || '';
    return climatePlacementOrderFor(
      geometry, /^CLG[12]:/i.test(String(stored).trim()));
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
      state.geometry,
      document.getElementById(tab + '_climate_view')?.value === '1');
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
    const header = document.getElementById(tab + '_climate_view')?.value === '1';
    const spanW = Math.max(1, Math.floor(Number(
      document.getElementById(
        tab + '_tile_span_w')?.value) || 1));
    // Height follows half steps through the mini-grid rows.
    const spanH = Math.max(1, Number(
      document.getElementById(
        tab + '_tile_span_h')?.value) || 1);
    const { rows } = climateGridDimensions(spanW, spanH);
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

    if (spanW === 1 && rows === 1) {
      if (!header && (!state.valid || state.current !== '--')) {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      } else {
        addPrimaryTarget();
      }
    } else if (spanW >= 2 && rows === 1) {
      if (!header && (!state.valid || state.current !== '--')) {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
    } else if (spanW === 1) {
      if (!header && (!state.valid || state.current !== '--')) {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
      if (rows <= 3) return kinds;
      if (state.targetHumidity !== null &&
          (state.targetLow !== null ||
           state.targetHigh !== null ||
           state.target !== null)) {
        add(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      }
    } else {
      if (!header && (!state.valid || state.current !== '--')) {
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

  // Every configured item takes part, not only the first cells-many item
  // numbers: after a resize the items that still fit stay, whatever their
  // number (build_slot_kinds on the device).
  function climateResolvedEditorKinds(tab) {
    const configured = currentClimateSlotConfig(tab);
    const automatic = climateAutomaticEditorKinds(tab);
    const explicit = new Set();
    configured.forEach(selection => {
      const kind = Number(selection) || 0;
      if (kind !== CLIMATE_TILE_CONTENT.AUTO &&
          kind !== CLIMATE_TILE_CONTENT.EMPTY) {
        explicit.add(kind);
      }
    });
    let cursor = 0;
    return configured.map(selection => {
      const kind = Number(selection) || 0;
      if (kind === CLIMATE_TILE_CONTENT.EMPTY) return null;
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

  function climateTargetCaption(state, kind) {
    if (state?.available === false) return CLIMATE_I18N.unavailable;
    const entityState = String(state?.mode || '').toLowerCase();
    if (entityState === 'unknown') return CLIMATE_I18N.unknown;
    if (kind === CLIMATE_TILE_CONTENT.TARGET_HUMIDITY) {
      return CLIMATE_I18N.humidityCaption;
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
        const configured = currentClimateSlotConfig(tab);
        const resolvedKinds =
          climateResolvedEditorKinds(tab);
        const index = configured.findIndex(
          (value, candidate) =>
            Number(value) === CLIMATE_TILE_CONTENT.EMPTY ||
            (Number(value) === CLIMATE_TILE_CONTENT.AUTO &&
             resolvedKinds[candidate] === null));
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
      const activeIndices =
        climateActiveGridIndices(tab);
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
            const configured = currentClimateSlotConfig(tab);
            const stored = currentClimateGeometry(tab);
            const items = stored.map(entry =>
              clampClimateGeometryItem(entry, columns, rows));
            const origin = { ...items[index] };
            const layouts =
              climateGridLayouts(tab, columns, rows);
            const activeIndices =
              climateActiveGridIndices(tab);
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
    if (Number(document.getElementById(tab + '_tile_type')?.value) !== 17) {
      parkClimateMiniEditor(tab);
      return;
    }
    mountClimateMiniEditor(tab);
    // Width counts whole cells; half heights add a mini-grid row.
    const spanW = Math.max(1, Math.floor(Number(document.getElementById(
      tab + '_tile_span_w')?.value) || 1));
    const spanH = Math.max(1, Math.round(Number(document.getElementById(
      tab + '_tile_span_h')?.value) * 2 || 2) / 2);
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
    const fits = candidate => !accepted.some(other =>
      climateGeometryOverlaps(candidate, other.geometry));
    for (const index of climatePlacementOrder(tab, stored)) {
      const item = document.getElementById(
        tab + '_climate_slot_row_' + index);
      const kind = Number(configured[index]) || 0;
      const active =
        kind !== CLIMATE_TILE_CONTENT.EMPTY &&
        resolvedKinds[index] !== null;
      if (!item) continue;
      item.classList.toggle('hidden', !active);
      if (!active) continue;

      let geometry = items[index];
      if (!fits(geometry)) {
        let free = null;
        for (let row = 0;
             row + geometry.spanH <= rows && !free; ++row) {
          for (let col = 0;
               col + geometry.spanW <= columns; ++col) {
            const candidate = {
              col, row,
              spanW: geometry.spanW,
              spanH: geometry.spanH
            };
            if (fits(candidate)) {
              free = candidate;
              break;
            }
          }
        }
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
            Number(value) === CLIMATE_TILE_CONTENT.EMPTY ||
            (Number(value) === CLIMATE_TILE_CONTENT.AUTO &&
             resolvedKinds[candidate] === null));
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
    loadIconColorFields(tab, data);
    const entity = document.getElementById(tab + '_climate_entity');
    if (entity) {
      const configuredEntity =
        Object.prototype.hasOwnProperty.call(data, 'sensor_entity')
          ? (data.sensor_entity || '')
          : (data.climate_entity || '');
      entity.value = configuredEntity;
    }
    const popup = document.getElementById(tab + '_climate_popup_open_mode');
    if (popup) popup.value = (data.popup_open_mode !== undefined)
      ? String(data.popup_open_mode) : '1';
    const view = document.getElementById(tab + '_climate_view');
    if (view) view.value = Number(data.sensor_display_mode) === 1 ? '1' : '0';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
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
    // Unknown is inactive like unavailable (climate_visuals: 0x9E9E9E).
    if (state?.available === false || mode === 'unavailable' || mode === 'unknown') {
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

  // The header value pair of the Layout "with value" and half height
  // (climate_header_text): the action or mode and the current temperature.
  function climatePreviewHeaderText(state) {
    if (!state?.valid) return '--';
    const actions = {
      heating: CLIMATE_I18N.heating, preheating: CLIMATE_I18N.preheating, cooling: CLIMATE_I18N.cooling,
      drying: CLIMATE_I18N.drying, fan: CLIMATE_I18N.fan, defrosting: CLIMATE_I18N.defrosting,
      idle: CLIMATE_I18N.idle
    };
    const action = String(state.action || '').toLowerCase();
    const label = state.available !== false && actions[action] ? actions[action] : climateModeText(state);
    if (state.available === false || state.current === '--') return label;
    return label + ' \u00B7 ' + state.current + ' ' + state.unit;
  }

  // `header`: the Layout "with value" shows the current temperature in the
  // header, so the automatic fields start with the target
  // (build_automatic_slot_kinds).
  function climatePreviewSlots(
      state, spanW, spanH, slotConfig = null,
      targetLayoutConfig = null, geometryConfig = null, header = false) {
    // Layout variants follow whole cells in width and mini-grid rows in
    // height (half steps add a row), like build_automatic_slot_kinds.
    const w = Math.max(1, Math.floor(Number(spanW) || 1));
    const h = Math.max(1, Math.round(Number(spanH) * 2 || 2) / 2);
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
    } else if (w === 1 && rows === 1) {
      if (!header && (!state.valid || state.current !== '--')) {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      } else {
        addPrimaryTarget();
      }
    } else if (w >= 2 && rows === 1) {
      if (!header && (!state.valid || state.current !== '--')) {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
    } else if (w === 1) {
      if (!header && (!state.valid || state.current !== '--')) {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
      if (rows > 3 &&
          state.targetHumidity !== null &&
          (state.targetLow !== null ||
           state.targetHigh !== null ||
           state.target !== null)) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      }
    } else {
      if (!header && (!state.valid || state.current !== '--')) {
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

    // Every configured item takes part, not only the first cells-many item
    // numbers; what does not fit is dropped during placement below.
    const explicitlyConfigured = new Set();
    configured.forEach(selection => {
      const kind = Number(selection) || 0;
      if (kind !== CLIMATE_TILE_CONTENT.AUTO &&
          kind !== CLIMATE_TILE_CONTENT.EMPTY) {
        explicitlyConfigured.add(kind);
      }
    });

    const slots = [];
    let automaticCursor = 0;
    for (let index = 0; index < 6; ++index) {
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
    const order = climatePlacementOrderFor(geometry, hasStoredGeometry);
    slots.sort((a, b) =>
      order.indexOf(a.itemIndex) - order.indexOf(b.itemIndex));
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
    saveIconColorFields(tab, formData);
    const packed = packClimateSlotConfig(tab);
    const packedLayouts = packClimateTargetLayouts(tab);
    const geometry = document.getElementById(
      tab + '_climate_geometry')?.value || '';
    formData.append('climate_entity',
      document.getElementById(tab + '_climate_entity')?.value || '');
    formData.append('popup_open_mode',
      document.getElementById(tab + '_climate_popup_open_mode')?.value || '1');
    const view = document.getElementById(tab + '_climate_view')?.value === '1' ? '1' : '0';
    formData.append('climate_view', view);
    formData.append('sensor_display_mode', view);
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
    resetIconColorFields(tab);
    const entity = document.getElementById(tab + '_climate_entity');
    if (entity) entity.value = '';
    const popup = document.getElementById(tab + '_climate_popup_open_mode');
    if (popup) popup.value = '1';
    const view = document.getElementById(tab + '_climate_view');
    if (view) view.value = '0';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
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
    loadIconColorFields(tab, data);
    const el = document.getElementById(tab + '_camera_entity');
    const configured = data.sensor_entity || data.camera_entity || '';
    if (el) {
      el.value = configured;
    }
    maybeFillTitleFromEntity(tab, '_camera_entity');
  }
  function saveCameraFields(tab, formData) {
    const entity =
      document.getElementById(tab + '_camera_entity')?.value || '';
    formData.append('camera_entity', entity);
    formData.append('sensor_entity', entity);
    saveIconColorFields(tab, formData);
  }
  function resetCameraFields(tab) {
    resetIconColorFields(tab);
    const el = document.getElementById(tab + '_camera_entity');
    if (el) el.value = '';
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

  function getClockPreviewCssPx(raw, fallback, el) {
    const n = normalizeClockPreviewFont(raw, fallback);
    // Same scaling as the CSS variables (LVGL pixels * preview factor), as the
    // tile's own grid sets them (a grid may scale its preview).
    const v = parseFloat(getComputedStyle(el?.isConnected ? el : document.documentElement).getPropertyValue('--fs' + n));
    return (v > 0) ? v : Math.round(n / 2);
  }

  // Each clock line is as tall as its LVGL font's line height, with the
  // glyphs on the LVGL baseline (--lh/--ldy, web_admin_styles.cpp).
  function clockPreviewLineCss(size) {
    return 'line-height:var(--lh' + size + '); top:var(--ldy' + size + ', 0px);';
  }

  function applyClockPreviewLine(el, size, px) {
    el.style.fontSize = px + 'px';
    el.style.lineHeight = 'var(--lh' + size + ')';
    el.style.top = 'var(--ldy' + size + ', 0px)';
  }

  function getClockPreviewTextStyle(raw, fallback, color) {
    const font = normalizeClockPreviewFont(raw, fallback);
    const size = getClockPreviewCssPx(raw, fallback);
    const safeColor = color || '#fff';
    return 'data-clock-font="' + font + '" style="font-size:' + size + 'px; ' +
      clockPreviewLineCss(font) + ' color:' + safeColor + ';"';
  }

  function applyClockPreviewTextStyle(el, raw, fallback, color) {
    if (!el) return;
    const font = normalizeClockPreviewFont(raw, fallback);
    el.dataset.clockFont = String(font);
    applyClockPreviewLine(el, font, getClockPreviewCssPx(raw, fallback, el));
    el.style.color = color || '#fff';
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
    loadIconColorFields(tab, data);
    const border = document.getElementById(tab + '_clock_tile_border');
    if (border) border.checked = data?.tile_border !== undefined ? !['0','false'].includes(String(data.tile_border)) : Number(data?.sensor_display_mode) !== 1;
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
      applyClockPreviewTextStyle(timeEl, timeFont, 40, '#fff');
    }
    if (dateEl) {
      dateEl.textContent = getClockPreviewDate(dateFormat);
      applyClockPreviewTextStyle(dateEl, dateFont, 20, '#fff');
    }
    fitCompactClockPreview(tileElem);
  }

  const CLOCK_PREVIEW_FONT_SIZES = [20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96];
  let clockPreviewMeasureContext = null;

  function measureClockPreviewText(el, text, px) {
    clockPreviewMeasureContext = clockPreviewMeasureContext ||
      document.createElement('canvas').getContext('2d');
    if (!clockPreviewMeasureContext) return 0;
    const style = getComputedStyle(el);
    clockPreviewMeasureContext.font = style.fontWeight + ' ' + px + 'px ' + style.fontFamily;
    return clockPreviewMeasureContext.measureText(text).width;
  }

  // Worst-case samples keep the chosen size stable while the time changes.
  function clockPreviewSample(el, isTime) {
    return isTime ? (/[AP]M/.test(el.textContent) ? '88:88 PM' : '88:88')
      : el.textContent.replace(/[0-9]/g, '8');
  }

  // Half-height clocks use one row: the largest configured-or-smaller size whose
  // rendered size fits 80% of the tile height and whose text fits the width.
  // The date follows only from width 2 and only when it still fits.
  // The firmware applies the same rule (fit_compact_clock in clock/renderer.cpp).
  function fitCompactClockPreview(tileElem) {
    if (!tileElem) return;
    const lines = [tileElem.querySelector('.tile-clock-time'), tileElem.querySelector('.tile-clock-date')];
    lines.forEach(el => {
      if (!el) return;
      el.hidden = false;
      const font = normalizeClockPreviewFont(el.dataset.clockFont, 40);
      applyClockPreviewLine(el, font, getClockPreviewCssPx(font, 40, tileElem));
    });
    if (!tileElem.classList.contains('clock-compact')) return;
    const style = getComputedStyle(tileElem);
    // The tile's own grid sizes (inherited variables), not the page's.
    const root = tileElem.isConnected ? style : getComputedStyle(document.documentElement);
    const cellW = parseFloat(root.getPropertyValue('--preview-cell-w'));
    const cellH = parseFloat(root.getPropertyValue('--preview-cell-h'));
    const gridGap = parseFloat(root.getPropertyValue('--preview-gap')) || 0;
    const span = (value, cell) => (Number(value) || 1) * (cell + gridGap) - gridGap;
    // Hidden folder tabs have no layout yet; the grid variables still hold the size.
    const tileW = cellW > 0 ? span(tileElem.dataset.spanW, cellW) : tileElem.clientWidth;
    const tileH = cellH > 0 ? span(tileElem.dataset.spanH, cellH) : tileElem.clientHeight;
    const availW = tileW - parseFloat(style.paddingLeft || 0) - parseFloat(style.paddingRight || 0);
    const maxPx = tileH * 0.8;
    const gap = parseFloat(style.columnGap || 0) || 0;
    const [time, date] = lines;
    const primary = time || date;
    const secondary = time && date && Number(tileElem.dataset.spanW) >= 2 ? date : null;
    if (date && date !== primary && date !== secondary) date.hidden = true;
    if (!primary) return;
    const fit = (el, capPx, usedW) => {
      const sample = clockPreviewSample(el, el === time);
      for (const size of [...CLOCK_PREVIEW_FONT_SIZES].reverse()) {
        const px = getClockPreviewCssPx(size, size, tileElem);
        if (size > Number(el.dataset.clockFont || 40) || px > capPx) continue;
        const width = measureClockPreviewText(el, sample, px);
        if (usedW + width <= availW) return { size, px, width };
      }
      return null;
    };
    const first = fit(primary, maxPx, 0) || { size: 20, px: getClockPreviewCssPx(20, 20, tileElem), width: 0 };
    applyClockPreviewLine(primary, first.size, first.px);
    if (!secondary) return;
    const second = fit(secondary, first.px, first.width + gap);
    if (second) applyClockPreviewLine(secondary, second.size, second.px);
    else secondary.hidden = true;
  }

  function saveClockFields(tab, formData) {
    saveIconColorFields(tab, formData);
    formData.append('tile_border', document.getElementById(tab + '_clock_tile_border')?.checked === false ? '0' : '1');
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
    resetIconColorFields(tab);
    const border = document.getElementById(tab + '_clock_tile_border');
    if (border) border.checked = true;
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
    loadIconColorFields(tab, data);
    const border = document.getElementById(tab + '_text_tile_border');
    if (border) border.checked = data?.tile_border !== undefined ? !['0','false'].includes(String(data.tile_border)) : Number(data?.sensor_display_mode) !== 1;
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
    saveIconColorFields(tab, formData);
    formData.append('tile_border', document.getElementById(tab + '_text_tile_border')?.checked === false ? '0' : '1');
    const prefix = tab;
    formData.append('text_value', document.getElementById(prefix + '_text_value')?.value || '');
    formData.append('text_value_font', document.getElementById(prefix + '_text_value_font')?.value || '0');
  }

  function resetTextFields(tab) {
    resetIconColorFields(tab);
    const border = document.getElementById(tab + '_text_tile_border');
    if (border) border.checked = true;
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
    loadIconColorFields(tab, data);
    const font = document.getElementById(tab + '_number_value_font');
    if (font) font.value = String(data.sensor_value_font ?? 2);
    const entity = document.getElementById(tab + '_number_entity');
    const configured = data.sensor_entity || data.number_entity || '';
    if (entity) {
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
    saveIconColorFields(tab, formData);
    formData.append('sensor_value_font', document.getElementById(tab + '_number_value_font')?.value ?? '2');
    const entityEl = document.getElementById(tab + '_number_entity');
    const entity = entityEl
      ? entityEl.value : '';
    formData.append('number_entity', entity);
    formData.append('sensor_entity', entity);
    const popup = document.getElementById(
      tab + '_number_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetNumberFields(tab) {
    resetIconColorFields(tab);
    const font = document.getElementById(tab + '_number_value_font');
    if (font) font.value = '2';
    const entity = document.getElementById(tab + '_number_entity');
    if (entity) entity.value = '';
    const popup = document.getElementById(
      tab + '_number_popup_open_mode');
    if (popup) popup.value = '1';
  }

  function loadSelectFields(tab, data) {
    loadIconColorFields(tab, data);
    const font = document.getElementById(tab + '_select_value_font');
    if (font) font.value = String(data.sensor_value_font ?? 2);
    const entity = document.getElementById(tab + '_select_entity');
    const configured = data.sensor_entity || data.select_entity || '';
    if (entity) {
      entity.value = configured;
    }
    const popup = document.getElementById(
      tab + '_select_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
    // The entity is known now: the state color section follows it.
    syncIconColorFields(tab);
  }

  function saveSelectFields(tab, formData) {
    saveIconColorFields(tab, formData);
    formData.append('sensor_value_font', document.getElementById(tab + '_select_value_font')?.value ?? '2');
    const entityEl = document.getElementById(tab + '_select_entity');
    const entity = entityEl
      ? entityEl.value : '';
    formData.append('select_entity', entity);
    formData.append('sensor_entity', entity);
    const popup = document.getElementById(
      tab + '_select_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetSelectFields(tab) {
    resetIconColorFields(tab);
    const font = document.getElementById(tab + '_select_value_font');
    if (font) font.value = '2';
    const entity = document.getElementById(tab + '_select_entity');
    if (entity) entity.value = '';
    const popup = document.getElementById(
      tab + '_select_popup_open_mode');
    if (popup) popup.value = '1';
  }

  function loadDateTimeFields(tab, data) {
    loadIconColorFields(tab, data);
    const font = document.getElementById(tab + '_datetime_value_font');
    if (font) font.value = String(data.sensor_value_font ?? 2);
    const entity = document.getElementById(tab + '_datetime_entity');
    const configured = data.sensor_entity || data.datetime_entity || '';
    if (entity) {
      entity.value = configured;
    }
    const popup = document.getElementById(
      tab + '_datetime_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
    // The entity is known now: the state color section follows it.
    syncIconColorFields(tab);
  }

  function saveDateTimeFields(tab, formData) {
    saveIconColorFields(tab, formData);
    formData.append('sensor_value_font', document.getElementById(tab + '_datetime_value_font')?.value ?? '2');
    const entityEl = document.getElementById(tab + '_datetime_entity');
    const entity = entityEl
      ? entityEl.value : '';
    formData.append('datetime_entity', entity);
    formData.append('sensor_entity', entity);
    const popup = document.getElementById(
      tab + '_datetime_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetDateTimeFields(tab) {
    resetIconColorFields(tab);
    const font = document.getElementById(tab + '_datetime_value_font');
    if (font) font.value = '2';
    const entity = document.getElementById(tab + '_datetime_entity');
    if (entity) entity.value = '';
    const popup = document.getElementById(
      tab + '_datetime_popup_open_mode');
    if (popup) popup.value = '1';
  }

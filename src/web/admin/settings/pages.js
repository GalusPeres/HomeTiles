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

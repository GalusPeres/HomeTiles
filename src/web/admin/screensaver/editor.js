
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
    // In the layout window: that layout's screen (layout-window.js).
    const shown = layoutWindowScreen(preview);
    if (shown) return shown.rect;
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
    const shown = layoutWindowScreen(preview);
    if (shown) return shown;
    const frame = preview.querySelector('.screensaver-grid-image-frame');
    const rect = ssPreviewScreenRect(preview);
    return {
      left: frame ? frame.offsetLeft : 0,
      top: frame ? frame.offsetTop : 0,
      width: rect.width || 800,
      height: rect.height || 500
    };
  }

  // The clock's place and size: the draft's (the active layout's), in the
  // layout window the edited layout's (user 2026-10-08: the clock per
  // layout); the window stores it with its places.
  function ssClockPlace(preview) {
    return (layoutWindowScreen(preview) && layoutWindowClock()) || screensaverDraft;
  }
  function ssClockEdited(preview) {
    if (layoutWindowScreen(preview) && layoutWindowClock()) layoutWindowClockChanged();
    else scheduleScreensaverSave();
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
    // The picture's source: the SD slideshow or the Bridge's picture of an
    // image or camera entity (the slideshow stays stored).
    data.picture_source = data.picture_source === 'ha' ? 'ha' : 'sd';
    data.picture_entity = typeof data.picture_entity === 'string' ? data.picture_entity : '';
    // How the Bridge places it and a camera's interval (3..60 s).
    data.picture_fit = ['fit', 'original'].includes(data.picture_fit) ? data.picture_fit : 'fill';
    data.picture_every = Math.round(ssClamp(data.picture_every ?? 10, 3, 60));
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
      picture_source: d.picture_source === 'ha' ? 'ha' : 'sd',
      picture_entity: String(d.picture_entity || ''),
      picture_fit: ['fit', 'original'].includes(d.picture_fit) ? d.picture_fit : 'fill',
      picture_every: Math.round(ssClamp(d.picture_every ?? 10, 3, 60)),
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
    const scale = screen.width / Number(screen.screenW || d.screen_width || 1280);
    const place = ssClockPlace(preview);
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
    // The type picks the controls: the slideshow's or the picture entity.
    const fromHa = d.picture_source === 'ha';
    const source = document.getElementById('screensaverPictureSource');
    if (source && source.value !== d.picture_source) source.value = d.picture_source;
    document.getElementById('screensaverHaPicture')?.classList.toggle('hidden', !fromHa);
    document.getElementById('screensaverSdPicture')?.classList.toggle('hidden', fromHa);
    const heading = document.getElementById('screensaverBackgroundHeading');
    if (heading) heading.textContent = fromHa ? heading.dataset.ha : heading.dataset.sd;
    const pictureEntity = document.getElementById('screensaver_picture_entity');
    if (pictureEntity && pictureEntity.value !== d.picture_entity) pictureEntity.value = d.picture_entity;
    const pictureFit = document.getElementById('screensaverPictureFit');
    if (pictureFit && pictureFit.value !== d.picture_fit) pictureFit.value = d.picture_fit;
    const pictureEvery = document.getElementById('screensaverPictureEvery');
    if (pictureEvery && document.activeElement !== pictureEvery &&
        Number(pictureEvery.value) !== d.picture_every) pictureEvery.value = String(d.picture_every);
    // Only a camera sends stills on an interval.
    document.getElementById('screensaverPictureEveryRow')?.classList.toggle('hidden',
      !String(d.picture_entity || '').startsWith('camera.'));
    const wallpaper = fromHa ? null : ssCurrentWallpaper();
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
    clock.style.left = (screen.left + place.clock_x * screen.width / 1000) + 'px';
    clock.style.top = (screen.top + place.clock_y * screen.height / 1000) + 'px';
    const time = document.getElementById('screensaverClockTime');
    const date = document.getElementById('screensaverClockDate');
    time.hidden = !d.show_time;
    date.hidden = !d.show_date && !d.show_weekday;
    time.style.fontSize =
      Math.max(10, deviceClockFontPx(place.time_font_size, 48) * scale) + 'px';
    date.style.fontSize =
      Math.max(8, deviceClockFontPx(place.date_font_size, 28) * scale) + 'px';
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
    applyClockLine(time, place.time_font_size, 48, 10);
    applyClockLine(date, place.date_font_size, 28, 8);
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
      if (fromTileOrClock(e) || layoutWindowScreen(preview)) return;
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
      const place = ssClockPlace(preview);
      place.clock_x = Math.round(ssClamp((centerX - rect.left) * 1000 / rect.width, 0, 1000));
      place.clock_y = Math.round(ssClamp((centerY - rect.top) * 1000 / rect.height, 0, 1000));
      renderScreensaverEditor();
    });
    const finishClockDrag = e => {
      if (!clockDrag || clockDrag.id !== e.pointerId) return;
      clockDrag = null; clock.classList.remove('dragging'); ssClockEdited(preview);
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
          timeFont: Number(ssClockPlace(preview).time_font_size || 48),
          dateFont: Number(ssClockPlace(preview).date_font_size || 28)
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
        const place = ssClockPlace(preview);
        place.time_font_size = ssNearestClockFont(
          clockResizeDrag.timeFont * factor, false);
        place.date_font_size = ssNearestClockFont(
          clockResizeDrag.dateFont * factor, true);
        renderScreensaverEditor();
      });
      const finishClockResize = e => {
        if (!clockResizeDrag || clockResizeDrag.id !== e.pointerId) return;
        clockResizeDrag = null;
        ssClockEdited(preview);
      };
      clockResize.addEventListener('pointerup', finishClockResize);
      clockResize.addEventListener('pointercancel', finishClockResize);
    }

    const bind = (id, event, fn, save = true) => {
      const element = document.getElementById(id); if (!element) return;
      element.addEventListener(event, () => { fn(element); renderScreensaverEditor(); if (save) scheduleScreensaverSave(); });
    };
    bind('screensaverUseWallpapers', 'change', el => { screensaverDraft.use_wallpapers = el.checked; });
    bind('screensaverPictureSource', 'change', el => {
      screensaverDraft.picture_source = el.value === 'ha' ? 'ha' : 'sd';
    });
    bind('screensaver_picture_entity', 'change', el => { screensaverDraft.picture_entity = el.value || ''; });
    bind('screensaverPictureFit', 'change', el => {
      screensaverDraft.picture_fit = ['fit', 'original'].includes(el.value) ? el.value : 'fill';
    });
    bind('screensaverPictureEvery', 'change', el => {
      screensaverDraft.picture_every = Math.round(ssClamp(Number(el.value) || 10, 3, 60));
    });
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


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

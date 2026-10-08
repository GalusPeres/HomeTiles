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
  // screen starts in its top left corner, right under the head; the storage
  // lies to the right and below (user 2026-10-08, every panel). The area
  // keeps at least two columns and two rows beside the smallest screen (the
  // square panels' layouts are all 3 x 3).
  function layoutCanvas() {
    const keys = LAYOUT_KEYS.filter(key => LAYOUTS[key].available);
    const cols = keys.map(key => LAYOUTS[key].cols);
    const rows = keys.map(key => Math.ceil(LAYOUTS[key].rows));
    return {cols: Math.max(...cols, Math.min(...cols) + 2), rows: Math.max(...rows, Math.min(...rows) + 2)};
  }
  function layoutInside(tile, key) {
    const L = LAYOUTS[key];
    return tile.col >= -1e-6 && tile.col + tile.span_w <= L.cols + 1e-6 &&
      tile.row >= -1e-6 && tile.row + tile.span_h <= L.rows + 1e-6;
  }
  // The head on the layout's screen: the X and the time keep their distance
  // to its right edge.
  function layoutHeadVars(L) {
    const w = L.screenW * layoutScale();
    const shift = w - layoutHead.w;
    return {'--head-screen-w': layoutPx(w), '--head-close-x': layoutPx(layoutHead.close + shift),
      '--head-time-x': layoutPx(layoutHead.time + shift), '--head-title-w': layoutPx(layoutHead.title + shift),
      '--head-time-x-alone': layoutPx(layoutHead.timeAlone + shift)};
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
      time: layoutRootPx('--head-time-x'), title: layoutRootPx('--head-title-w'),
      timeAlone: layoutRootPx('--head-time-x-alone') || layoutRootPx('--head-time-x')};
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
      ? {col: p[0], row: p[1], span_w: p[2], span_h: p[3]} : null;
  }
  // Wholly beside the layout's screen, to the right or below: in its storage.
  function layoutParked(tile, key) {
    const L = LAYOUTS[key];
    return tile.col >= -1e-6 && tile.row >= -1e-6 && (tile.col >= L.cols - 1e-6 || tile.row >= L.rows - 1e-6);
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
        Object.assign(tile, {col: p.col, row: p.row, span_w: p.span_w, span_h: p.span_h});
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
    // Every tile in the size it is drawn in (a Media tile at least 2 x 2).
    const drawn = (type, col, row, w, h) => {
      const shown = normalizeLayoutForTileType(type, col, row, w, h);
      return shown.col === col && shown.row === row && shown.span_w === w && shown.span_h === h;
    };
    const later = [];
    for (const index of order) {
      const tile = tiles[index];
      const shown = normalizeLayoutForTileType(tile.type, tile.col, tile.row, tile.span_w, tile.span_h);
      if (!tile._unplaced && shown.col === tile.col && shown.row === tile.row &&
          fits(tile.col, tile.row, shown.span_w, shown.span_h)) {
        Object.assign(tile, shown);
        take(tile);
      } else {
        later.push(tile);
      }
    }
    // The others keep their size (user 2026-10-08: they are moved by hand):
    // the first free spot beside the screen, else on it.
    for (const tile of later) {
      const shown = normalizeLayoutForTileType(tile.type, 0, 0, tile.span_w, tile.span_h);
      const w = shown.span_w, h = shown.span_h;
      let spot = null;
      for (const outside of [true, false]) {
        for (let row = 0; !spot && row + h <= canvas.rows; row += 0.5) {
          for (let col = 0; !spot && col + w <= canvas.cols; col += 0.5) {
            if (fits(col, row, w, h) && (!outside || layoutParked({col, row, span_w: w, span_h: h}, key)) &&
                supportedTileLayout(tile.type, {col, row, span_w: w, span_h: h}) && drawn(tile.type, col, row, w, h)) {
              spot = {col, row, span_w: w, span_h: h};
            }
          }
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
      });
    } else {
      // The other layout as its window shows it: saved, or its first take-over.
      const source = setupTiles(tab, from);
      tiles.forEach((tile, index) => {
        const p = source[index];
        if (!layoutUsed(tile)) return;
        if (layoutUsed(p) && layoutInside(p, from)) {
          Object.assign(tile, {col: p.col, row: p.row, span_w: p.span_w, span_h: p.span_h});
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
        Object.assign(tile, {col: p[0], row: p[1], span_w: p[2], span_h: p[3]});
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
    grid.style.backgroundPosition = `0px ${y}px`;
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
    cross.title = t('layoutDeleteTile');
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
  // The cross on a tile deletes it after the question, from every layout
  // (user 2026-10-08); into the storage a tile is dragged.
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
        places[tile.view_id] = [tile.col, tile.row, tile.span_w, tile.span_h];
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
      // The page's selected tile: selected again when the window closes.
      selection: {tab: currentTileTab, index: currentTileIndex, byTab: layoutClone(selectedTileByTab)},
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
      // One head: the title, the layouts with the notes right beside them,
      // the X (user 2026-10-08).
      `<div class="setup-head-row"><div class="setup-title">${escapeHtml(t('layoutChange'))}</div>`,
      `<div class="setup-layouts">${layoutButtons}</div><div class="setup-status"></div>`,
      `<div class="setup-head-right"><button type="button" class="setup-close" aria-label="${escapeHtml(t('close'))}">` +
        '<i class="mdi mdi-close"></i></button></div></div>',

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
    // The tile the page had selected before, with its settings beside the
    // preview (the window's own selections are forgotten).
    const selection = state.selection;
    for (const key of Object.keys(selectedTileByTab)) delete selectedTileByTab[key];
    Object.assign(selectedTileByTab, selection.byTab);
    currentTileTab = selection.tab;
    // Deleted meanwhile or now in the storage: the first tile top left.
    const visible = (tile) => !!tile && Number(tile.type || 0) !== 0 && !tile.layout_hidden;
    const firstVisible = tab => {
      const tiles = getTilesData(tab) || [];
      let best = -1;
      tiles.forEach((tile, index) => {
        if (!visible(tile)) return;
        const other = tiles[best];
        if (best < 0 || Number(tile.row) < Number(other.row) ||
            (Number(tile.row) === Number(other.row) && Number(tile.col) < Number(other.col))) best = index;
      });
      return best;
    };
    let restored = false;
    if (selection.index === -2 && typeof selectHiddenSettingsTile === 'function') {
      selectHiddenSettingsTile();
      restored = currentTileIndex === -2;
    } else if (selection.tab && selection.index >= 0) {
      const index = visible(getTilesData(selection.tab)?.[selection.index]) ? selection.index : firstVisible(selection.tab);
      if (index >= 0) {
        selectTile(index, selection.tab);
        restored = true;
      }
    }
    if (!restored) {
      try { localStorage.setItem(SELECTED_TILE_STORAGE_KEY, JSON.stringify(selectedTileByTab)); } catch (e) {}
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
    if (event.target.closest('.setup-trash')) deleteLayoutTile();
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

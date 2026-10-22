  // Home's page fields and Settings field (head bar layouts, user 2026-10-13,
  // build/design-mockups/pages/felder-*.png): cells of Home's own grid,
  // exactly the tiles' size, in two columns beside a square or upright screen
  // or in rows under a landscape one. A page is its folder's own tile shown
  // in its field (one cell), dragged like any tile: a Home folder onto a free
  // field becomes a page, a page onto the screen a folder tile again, a page
  // onto another field moves there (onto a page they swap). The parked
  // Settings tile stands in the Settings field (settings/access.js parks it).
  // The dots take the pages in field order and skip the free fields. The
  // layout window shows the same fields with its own pages (layout-window.js).
  const PAGE_SLOT_COUNT = 5;

  // The markup's holder: the page places (data-pages), and at first the
  // fields' elements, which layoutPageFields moves into the grid.
  function pageSlotsElement() {
    return document.getElementById('pageSlots');
  }

  // The places as stored, without looking for their tiles.
  function pageSlotIds() {
    const stored = String(pageSlotsElement()?.dataset.pages || '').split(',').map(Number);
    return Array.from({length: PAGE_SLOT_COUNT}, (_, i) => stored[i] > 0 ? stored[i] : 0);
  }

  // The places as put: a folder or 0 for a free one; a folder deleted
  // meanwhile has left its place free.
  function pageSlotList() {
    const stored = String(pageSlotsElement()?.dataset.pages || '').split(',').map(Number);
    return Array.from({length: PAGE_SLOT_COUNT}, (_, i) => stored[i] > 0 && homeFolderTile(stored[i]) ? stored[i] : 0);
  }

  // The pages in dot order.
  function pageFolders() {
    return pageSlotList().filter(id => id > 0);
  }

  // The stored form: up to the last page, free places as 0.
  function encodePageSlots(slots) {
    const last = slots.reduce((end, id, i) => id ? i + 1 : end, 0);
    return slots.slice(0, last).join(',');
  }

  // Home's folder tile of a folder, from the page itself: the server and
  // the browser both mark it (data-navigate-target), also before the tile
  // data has loaded.
  function homeFolderTile(folderId) {
    return getTileGrid('folder0')?.querySelector(
      ':scope > .tile[data-type="4"][data-navigate-target="' + Number(folderId) + '"]') || null;
  }

  function homeFolderTileIndex(folderId) {
    const tile = homeFolderTile(folderId);
    return tile ? Number(tile.dataset.index) : -1;
  }

  // The layout window shows the fields too (user 2026-10-13), on Home: in its
  // bar layouts its own pages, in every layout the Settings field, as
  // Settings is parked per layout (stored with the places, layout-window.js).
  function pageFieldsInWindow() {
    return typeof layoutWindowOpen === 'function' && layoutWindowOpen();
  }

  // The fields show in Home's grid of a head bar layout; in the layout window
  // only while it holds Home's grid (behind it the page keeps its own).
  function pageFieldsShown() {
    if (!pageSlotsElement()) return false;
    if (!pageFieldsInWindow()) return headBarLayout();
    return layoutWindowShowsFields() && !!getTileGrid('folder0')?.classList.contains('setup-grid');
  }

  // The page fields themselves: the window's classic layout has the
  // Settings field only (pages need the head bar).
  function pageSlotsShown() {
    return !pageFieldsInWindow() || layoutWindowShowsPages();
  }

  // A panel whose active layout is the classic one has no fields in its
  // page: the layout window brings them for its bar layouts.
  function ensurePageFields() {
    if (pageSlotsElement()) return;
    const holder = document.createElement('div');
    holder.id = 'pageSlots';
    holder.className = 'page-fields';
    holder.dataset.pages = '';
    const word = document.createElement('div');
    word.className = 'page-fields-label';
    word.textContent = t('pagesTitle');
    holder.appendChild(word);
    for (let i = 0; i < PAGE_SLOT_COUNT; ++i) {
      const field = document.createElement('div');
      field.className = 'page-field';
      field.dataset.slot = String(i);
      holder.appendChild(field);
    }
    document.body.appendChild(holder);
    enablePageFields();
  }

  // The fields: the pages' and the Settings field.
  function pageFieldCount() {
    return PAGE_SLOT_COUNT + 1;
  }

  // The room the fields take beside a square or upright screen (two
  // columns, 14 px apart) or under a landscape one (rows, one word over
  // them, a little off the screen: user 2026-10-14): a part that grows with
  // the cells and a fixed one. The layout window keeps it beside its area
  // (layout-window.js mountLayoutTab).
  const PAGE_FIELDS_WORD_ROOM = 26;
  const PAGE_FIELDS_WORD_Y = 6;
  function pageFieldRoom(cellW, cellH, gap, cols, rows) {
    if (cols > rows) {
      const fieldRows = Math.ceil(pageFieldCount() / cols);
      return {beside: false, w: 0, h: gap + fieldRows * (cellH + gap), fixedW: 0, fixedH: PAGE_FIELDS_WORD_ROOM};
    }
    return {beside: true, w: 2 * (cellW + gap) + gap, h: 0, fixedW: 14, fixedH: 0};
  }

  // Where the fields lie, in the grid's cell units from its first cell
  // (fractional: they start one grid gap off the screen's edge), and the
  // room they take beside or under the screen.
  function pageFieldPlaces(grid) {
    const style = getComputedStyle(grid);
    const px = name => parseFloat(style.getPropertyValue(name)) || 0;
    const cellW = px('--preview-cell-w'), cellH = px('--preview-cell-h'), gap = px('--preview-gap');
    const padRight = px('--preview-pad-right') || px('--preview-pad');
    const padBottom = px('--preview-pad-bottom') || px('--preview-pad');
    // In the layout window its whole area (screen and storage).
    const cols = placeCols('folder0'), rows = placeRows('folder0');
    const stepX = cellW + gap, stepY = cellH + gap;
    const screenW = cols * stepX - gap, screenH = rows * stepY - gap;
    // The fields lie on a black box of their own like the screen (user
    // 2026-10-13), one word over it. Its black margin is one grid gap on
    // every side (user 2026-10-14: the top one was a pixel short of it, its
    // 1 px line inside), under a landscape screen its sides line up with the
    // screen's. The boxes are outer edges in the grid's padding box units
    // from its first cell's corner.
    const room = pageFieldRoom(cellW, cellH, gap, cols, rows);
    const fieldRows = Math.ceil(pageFieldCount() / (room.beside ? 2 : cols));
    const settingsField = pageFieldCount() > PAGE_SLOT_COUNT;
    if (!room.beside) {
      // Under a landscape screen in its columns; the Settings field last,
      // in the sixth column or under the last one.
      const word = room.fixedH + gap;
      const top = (screenH + padBottom + word) / stepY;
      const at = (col, row) => ({col, row: top + row});
      const wordY = screenH + padBottom + PAGE_FIELDS_WORD_Y;
      // A word over the Settings field where it starts a row of its own
      // or stands in the first one (user 2026-10-14).
      const settingsWord = place => ({x: place.col * stepX, y: wordY + (place.row - top) * stepY});
      // The window's classic layout: the Settings field alone on its box.
      if (!pageSlotsShown()) {
        const place = at(cols - 1, Math.floor(PAGE_SLOT_COUNT / cols));
        return {
          pages: Array.from({length: PAGE_SLOT_COUNT}, (_, i) => at(i % cols, Math.floor(i / cols))),
          settings: settingsField ? place : null,
          right: 0,
          bottom: room.h + room.fixedH,
          box: {x: place.col * stepX - gap - 1, y: place.row * stepY - gap - 1, w: cellW + 2 * gap + 2, h: cellH + 2 * gap + 2},
          word: null,
          settingsWord: settingsField ? settingsWord(place) : null
        };
      }
      const settingsPlace = at(cols - 1, Math.floor(PAGE_SLOT_COUNT / cols));
      return {
        pages: Array.from({length: PAGE_SLOT_COUNT}, (_, i) => at(i % cols, Math.floor(i / cols))),
        settings: settingsField ? settingsPlace : null,
        right: 0,
        bottom: room.h + room.fixedH,
        // The box from the screen's left edge to its right one and down to
        // the grid's end (its bottom margin is the grid's: one gap).
        box: {x: null, y: screenH + padBottom + word - gap - 1, w: null, h: null},
        word: {x: 0, y: wordY},
        // Over the pages' row: no room for it under it.
        settingsWord: settingsField && settingsPlace.row === top ? settingsWord(settingsPlace) : null
      };
    }
    // Beside a square or upright screen in two columns along its rows, the
    // word in its head's height; the Settings field bottom right.
    const apart = room.fixedW;
    const left = (screenW + padRight + apart + gap) / stepX;
    // The window's classic layout: the Settings field alone on its box.
    if (!pageSlotsShown()) {
      const at = {col: left + 1, row: Math.floor(PAGE_SLOT_COUNT / 2)};
      return {
        pages: Array.from({length: PAGE_SLOT_COUNT}, (_, i) => ({col: left + i % 2, row: Math.floor(i / 2)})),
        settings: settingsField ? at : null,
        right: room.w + room.fixedW,
        bottom: 0,
        box: {x: at.col * stepX - gap, y: at.row * stepY - gap, w: stepX + gap, h: stepY + gap},
        word: null
      };
    }
    return {
      pages: Array.from({length: PAGE_SLOT_COUNT}, (_, i) => ({col: left + i % 2, row: Math.floor(i / 2)})),
      settings: settingsField ? {col: left + 1, row: Math.floor(PAGE_SLOT_COUNT / 2)} : null,
      right: room.w + room.fixedW,
      bottom: 0,
      box: {x: screenW + padRight + apart - 1, y: -gap - 1, w: 2 * stepX + gap + 2, h: fieldRows * stepY + gap + 2},
      word: {x: screenW + padRight + apart, y: -gap - 19},
      // Between the fields' rows there is no room for a word.
      settingsWord: null
    };
  }

  function setFieldPlace(el, place) {
    el.style.setProperty('--tile-col', String(place.col));
    el.style.setProperty('--tile-row', String(place.row));
  }

  // A page's folder tile stands in its field as one cell (layout.js
  // layoutTiles, grid-preview.js renderTileFromData); in the layout window
  // the Settings tile parked in the shown layout stands in its field.
  function pageFieldOfTile(tab, tile) {
    if (tab !== 'folder0' || !tile?.layout_hidden || !pageFieldsShown()) return null;
    if (Number(tile.type) === 7) {
      const grid = getTileGrid('folder0');
      return pageFieldsInWindow() && grid ? pageFieldPlaces(grid).settings : null;
    }
    if (Number(tile.type) !== 4) return null;
    const slot = pageSlotIds().indexOf(Number(tile.navigate_target));
    const grid = getTileGrid('folder0');
    return slot >= 0 && grid ? pageFieldPlaces(grid).pages[slot] : null;
  }

  // The tile picked in the editor is a page's: one cell in its field, the
  // editor neither moves nor sizes it (layout.js updateLayoutFromInputs,
  // live-preview.js, type-selection.js) and its stored size stays.
  function selectedPageTile(tab) {
    return currentTileIndex >= 0 &&
      !!document.getElementById(tab + '-tile-' + currentTileIndex)?.classList.contains('page-field-tile');
  }

  // Shown there only: its stored place (the tile's data-col/-row, which a
  // tile save sends) stays as it is.
  function placeOnPageField(el, place) {
    el.classList.remove('tile-unplaced');
    el.classList.add('fractional-tile', 'page-field-tile');
    el.style.gridColumn = 'auto';
    el.style.gridRow = 'auto';
    for (const [name, value] of Object.entries({col: place.col, row: place.row, w: 1, h: 1})) {
      el.style.setProperty('--tile-' + name, String(value));
    }
    el.style.display = '';
  }

  // The grid's room for the fields, the screen drawn behind its own part
  // only (admin.css .has-page-fields), the free fields' frames, the word
  // and the Settings field. Runs after every layout of Home's grid.
  function layoutPageFields(tab) {
    const holder = pageSlotsElement();
    const grid = getTileGrid('folder0');
    if (tab !== 'folder0' || !holder || !grid) return;
    if (holder.children.length) grid.append(...holder.children);
    // While the layout window is open Home's grid behind it stays as the
    // page had it (user 2026-10-13: the window's sizes widened it there and
    // squeezed the page); the window lays it out once it holds it.
    if (pageFieldsInWindow() && !grid.classList.contains('setup-grid')) return;
    const shown = pageFieldsShown();
    grid.classList.toggle('has-page-fields', shown);
    if (!shown) {
      grid.style.removeProperty('--fields-right');
      grid.style.removeProperty('--fields-bottom');
      grid.style.removeProperty('--grid-border');
      return;
    }
    const places = pageFieldPlaces(grid);
    grid.style.setProperty('--fields-right', places.right + 'px');
    grid.style.setProperty('--fields-bottom', places.bottom + 'px');
    // The fields' box (admin.css .has-page-fields::after), from the grid's
    // padding box corner; under the screen as wide as it, never past the
    // grid's border edges. Both boxes take the border as drawn (0.8 px at 125 %
    // Windows scaling) and the grid's fractional size (clientWidth/-Height
    // are rounded): a box a fraction of a pixel past the grid gave the
    // preview scroll bars (Tab5, user 2026-10-15).
    const style = getComputedStyle(grid);
    const padLeft = parseFloat(style.paddingLeft) || 0, padTop = parseFloat(style.paddingTop) || 0;
    const border = parseFloat(style.borderLeftWidth) || 0;
    grid.style.setProperty('--grid-border', border + 'px');
    const box = places.box;
    const boxLeft = box.x === null ? -border : padLeft + box.x;
    const boxTop = padTop + box.y;
    const toEdgeW = 'calc(100% + ' + (border - boxLeft) + 'px)';
    const toEdgeH = 'calc(100% + ' + (border - boxTop) + 'px)';
    grid.style.setProperty('--fields-box-l', boxLeft + 'px');
    grid.style.setProperty('--fields-box-t', boxTop + 'px');
    grid.style.setProperty('--fields-box-w', box.w === null ? toEdgeW : 'min(' + box.w + 'px, ' + toEdgeW + ')');
    grid.style.setProperty('--fields-box-h', box.h === null ? toEdgeH : 'min(' + box.h + 'px, ' + toEdgeH + ')');
    const slots = pageSlotList();
    // A picked field taken meanwhile (a folder dropped in) or out of sight.
    if (markedPageSlot >= 0 && (slots[markedPageSlot] || pageFieldsInWindow() || !pageSlotsShown())) {
      clearPageFieldMark(true);
    }
    grid.querySelectorAll(':scope > .page-field:not(.settings-field)').forEach(field => {
      const slot = Number(field.dataset.slot);
      setFieldPlace(field, places.pages[slot]);
      field.hidden = !pageSlotsShown() || !!slots[slot];
      // A free field is picked like a free cell (#100, user 2026-10-22: a
      // click made a folder at once, mistakes guaranteed); only the picked
      // one shows its "+", which makes the page. The layout window keeps its
      // pages until "Speichern": none there.
      field.classList.toggle('can-add', !pageFieldsInWindow());
      field.classList.toggle('marked', markedPageSlot === slot);
      if (!field.querySelector(':scope > .page-field-add')) {
        const plus = document.createElement('button');
        plus.type = 'button';
        plus.className = 'page-field-add';
        plus.setAttribute('aria-label', getTileTypeMeta('4')?.label || '');
        plus.innerHTML = '<i class="mdi mdi-plus"></i>';
        field.appendChild(plus);
        field.addEventListener('click', () => selectPageField(Number(field.dataset.slot)));
        plus.addEventListener('click', event => {
          event.stopPropagation();
          // The second click of a double click on the field picks nothing.
          if (event.detail > 1 || markedPageSlot !== Number(field.dataset.slot)) return;
          addPageFolder(markedPageSlot);
        });
      }
    });
    const word = grid.querySelector(':scope > .page-fields-label:not(.settings-label)');
    const placeWord = (el, place) => {
      el.hidden = !place;
      if (!place) return;
      el.style.setProperty('--word-x', place.x + 'px');
      el.style.setProperty('--word-y', place.y + 'px');
    };
    if (word) placeWord(word, places.word);
    // The Settings field's own word, the tile type's name (user 2026-10-14).
    let settingsWord = grid.querySelector(':scope > .page-fields-label.settings-label');
    if (!settingsWord && places.settingsWord) {
      settingsWord = document.createElement('div');
      settingsWord.className = 'page-fields-label settings-label';
      settingsWord.textContent = getTileTypeMeta('7')?.label || '';
      grid.appendChild(settingsWord);
    }
    if (settingsWord) placeWord(settingsWord, places.settingsWord);
    const settings = grid.querySelector(':scope > .settings-hidden-slot');
    if (settings && places.settings) setFieldPlace(settings, places.settings);
    // The window's own free Settings field (its parked tile stands there
    // itself; the editor's parking slot stays the page's).
    let free = grid.querySelector(':scope > .page-field.settings-field');
    if (pageFieldsInWindow() && !free) {
      free = document.createElement('div');
      free.className = 'page-field settings-field';
      grid.appendChild(free);
    }
    if (free) {
      if (places.settings) setFieldPlace(free, places.settings);
      // Parked, its tile stands there (a free frame where there is none).
      free.hidden = !pageFieldsInWindow() || (layoutWindowSettingsParked() && layoutSettingsIndex() >= 0);
    }
  }

  // The field under the pointer: a page field or the Settings field.
  function pageFieldAt(clientX, clientY) {
    const grid = getTileGrid('folder0');
    if (!grid || !pageFieldsShown()) return null;
    const style = getComputedStyle(grid);
    const px = name => parseFloat(style.getPropertyValue(name)) || 0;
    const rect = grid.getBoundingClientRect();
    const cellW = px('--preview-cell-w'), cellH = px('--preview-cell-h'), gap = px('--preview-gap');
    const left = rect.left + grid.clientLeft + parseFloat(style.paddingLeft);
    const top = rect.top + grid.clientTop + parseFloat(style.paddingTop);
    const inside = place => place &&
      clientX >= left + place.col * (cellW + gap) && clientX < left + place.col * (cellW + gap) + cellW &&
      clientY >= top + place.row * (cellH + gap) && clientY < top + place.row * (cellH + gap) + cellH;
    const places = pageFieldPlaces(grid);
    const slot = pageSlotsShown() ? places.pages.findIndex(inside) : -1;
    if (slot >= 0) return {kind: 'page', slot, place: places.pages[slot]};
    return inside(places.settings) ? {kind: 'settings', place: places.settings} : null;
  }

  // True beside or under the screen, where only the fields are: no free
  // slot follows the pointer there (layout.js enableFreeSlotHover).
  function pointerBesideScreen(grid, clientX, clientY) {
    if (!grid?.classList.contains('has-page-fields')) return false;
    const rect = grid.getBoundingClientRect();
    const style = getComputedStyle(grid);
    return clientX > rect.right - (parseFloat(style.getPropertyValue('--fields-right')) || 0) ||
      clientY > rect.bottom - (parseFloat(style.getPropertyValue('--fields-bottom')) || 0);
  }

  function draggedHomeFolder() {
    if (!dragSource || dragSource.kind !== 'grid-tile' || dragSource.tab !== 'folder0') return 0;
    const tile = document.getElementById('folder0-tile-' + dragSource.index);
    return tile?.dataset.type === '4' ? Number(tile.dataset.navigateTarget || 0) : 0;
  }

  // A drag over a field (drag-resize.js asks first): the grid's placeholder
  // there, and the drop decides there. False when no field is under the
  // pointer, so the grid handles it.
  function pageFieldDrag(tab, event, drop) {
    if (tab !== 'folder0' || !dragSource) return false;
    const field = pageFieldAt(event.clientX, event.clientY);
    if (!field) {
      // Beside the screen only the fields take a tile (between them, or the
      // layout window's free spot where the editor has its Settings field).
      if (!pointerBesideScreen(getTileGrid('folder0'), event.clientX, event.clientY)) return false;
      event.preventDefault();
      restoreDragPreview(tab);
      dragSource.previewKey = '';
      if (drop) {
        event.stopPropagation();
        clearDragPlaceholder();
      } else {
        event.dataTransfer.dropEffect = 'none';
        ensureDragPlaceholder(tab)?.classList.remove('show');
      }
      return true;
    }
    event.preventDefault();
    if (drop) event.stopPropagation();
    const places = pageSlotList();
    const folder = dragSource.kind === 'page-slot' ? dragSource.folder : draggedHomeFolder();
    const settings = dragSource.kind === 'hidden-settings' ||
      (dragSource.kind === 'grid-tile' && Number(dragSource.type) === 7);
    // A folder onto a free field, a page onto any; Settings onto its own.
    const fits = field.kind === 'page'
      ? pageSlotsShown() && folder > 0 && !dragSource.settings && (dragSource.kind === 'page-slot' || !places[field.slot])
      : settings || !!dragSource.settings;
    restoreDragPreview(tab);
    dragSource.previewKey = '';
    if (!drop) {
      event.dataTransfer.dropEffect = fits ? 'move' : 'none';
      const placeholder = ensureDragPlaceholder(tab);
      if (placeholder) {
        placeholder.classList.add('show');
        placeholder.classList.toggle('invalid', !fits);
        setTileGridPosition(placeholder, field.place.col, field.place.row, 1, 1);
      }
      return true;
    }
    clearDragPlaceholder();
    if (!fits) return true;
    if (field.kind === 'settings') {
      dragSource.dropCommitted = true;
      // In the layout window parked in its layout until "Speichern".
      if (pageFieldsInWindow()) layoutSettingsIn();
      else if (dragSource.kind !== 'hidden-settings') hideSettingsTileFromGrid();
      return true;
    }
    const from = places.indexOf(folder);
    if (from === field.slot) return true;
    // Pages have no PIN: a locked folder asks before it becomes one.
    const unlock = from < 0 && homeFolderTile(folder)?.dataset.folderPinEnabled === '1';
    if (unlock && !window.confirm(t('pageUnlockConfirm'))) return true;
    dragSource.dropCommitted = true;
    // Exactly where it is dropped; the page there takes its old place.
    if (from >= 0) places[from] = places[field.slot];
    places[field.slot] = folder;
    // In the layout window it stays there until "Speichern" (layout-window.js).
    if (pageFieldsInWindow()) layoutWindowPages(places, unlock);
    else savePages(places, {}, unlock);
    return true;
  }

  // Home and the pages show the dots in their head and the gear; a page
  // left the fields shows its X again (home_bar.cpp). Only what differs is
  // touched: rebuilding the dots for every folder tab the page loaded made
  // them blink (user 2026-10-12). The layout window passes its own pages.
  function syncPageHeads(pages = pageFolders()) {
    document.querySelectorAll('.tile-tab[data-folder-id]').forEach(tab => {
      const head = tab.querySelector('.head-bar-preview');
      if (!head) return;
      const folder = Number(tab.dataset.folderId);
      const page = !pages.length ? -1 : folder === 0 ? 0 : pages.indexOf(folder) + (pages.includes(folder) ? 1 : -1);
      const want = page < 0 ? '' : Array.from({length: pages.length + 1}, (_, i) => i === page ? 'O' : 'o').join('');
      const shown = head.querySelector('.head-dots');
      const have = shown ? Array.from(shown.children, dot => dot.classList.contains('on') ? 'O' : 'o').join('') : '';
      if (have !== want) {
        shown?.remove();
        head.classList.toggle('has-dots', page >= 0);
        if (page >= 0) {
          head.style.setProperty('--head-dots-n', String(pages.length + 1));
          const dots = document.createElement('span');
          dots.className = 'head-dots';
          for (let i = 0; i <= pages.length; ++i) {
            const dot = document.createElement('i');
            if (i === page) dot.className = 'on';
            dots.appendChild(dot);
          }
          head.insertBefore(dots, head.querySelector('.head-time'));
        }
      }
      if (folder === 0) return;
      const button = head.querySelector('.head-button');
      const icon = button?.querySelector('i');
      if (!button || !icon) return;
      const homeGear = document.querySelector('#tab-tiles-folder0 .head-bar-preview .head-gear');
      const iconClass = 'mdi ' + (page > 0 ? 'mdi-cog' : 'mdi-window-close');
      if (button.classList.contains('head-gear') !== page > 0) button.classList.toggle('head-gear', page > 0);
      const off = page > 0 && !!homeGear?.classList.contains('is-off');
      if (button.classList.contains('is-off') !== off) button.classList.toggle('is-off', off);
      if (icon.className !== iconClass) icon.className = iconClass;
    });
  }

  // Shows a change of the pages at once, like any move: Home's folder tiles
  // in their fields or on the screen, and the heads. `slots` and `previous`
  // are the places after and before; a folder leaving them comes back on the
  // screen as one cell (`out`: at its dropped spot).
  function showPagesLocally(slots, previous, out = null) {
    pageSlotsElement().dataset.pages = encodePageSlots(slots);
    const tiles = getTilesData('folder0') || [];
    const pages = slots.filter(id => id > 0);
    new Set([...pages, ...previous.filter(id => id > 0)]).forEach(folder => {
      const el = homeFolderTile(folder);
      const tile = el ? tiles[Number(el.dataset.index)] : null;
      if (!tile) return;
      const page = pages.includes(folder);
      // A folder in the fields is one cell, and comes back as one (user
      // 2026-10-12: a half-high one changed its size in and out).
      tile.layout_hidden = page;
      Object.assign(tile, {span_w: 1, span_h: 1});
      if (!page && out && out.folder === folder) Object.assign(tile, {col: out.col, row: out.row});
      renderTileFromData('folder0', Number(el.dataset.index), tile, sensorMetaCache);
    });
    layoutTiles('folder0', tiles);
    syncPageHeads();
    // A page has no PIN: its folder's PIN option goes.
    if (currentTileTab === 'folder0' && typeof syncFolderPinControls === 'function') syncFolderPinControls('folder0');
  }

  // The saves go out one after the other; each shows before it is sent.
  let pageSlotsQueue = Promise.resolve();

  function savePages(slots, extra = {}, unlock = false) {
    const previous = pageSlotList();
    const out = extra.out ? {folder: Number(extra.out), col: Number(extra.col), row: Number(extra.row)} : null;
    showPagesLocally(slots, previous, out);
    pageSlotsQueue = pageSlotsQueue.then(() => sendPages(slots, previous, extra, unlock));
    return pageSlotsQueue;
  }

  async function sendPages(slots, previous, extra, unlock) {
    const undo = message => {
      showPagesLocally(previous, slots);
      showNotification(message, false);
      return false;
    };
    try {
      const body = new URLSearchParams({pages: encodePageSlots(slots)});
      if (unlock) body.set('unlock', '1');
      ['out', 'col', 'row'].forEach(key => {
        if (extra[key] !== undefined) body.set(key, String(extra[key]));
      });
      const response = await fetch('/api/pages', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: body.toString()
      });
      let data = {};
      try { data = await response.json(); } catch (_) {}
      if (!response.ok || !data.success) {
        return undo(data.error === 'No free cell' ? t('tileDoesNotFit') : t('networkErrorSave'));
      }
      const tiles = getTilesData('folder0') || [];
      // A page has no PIN: its lock went with the save.
      if (unlock) {
        slots.filter(id => id > 0).forEach(folder => {
          const index = homeFolderTileIndex(folder);
          if (index < 0 || !tiles[index]) return;
          tiles[index].folder_pin_enabled = false;
          renderTileFromData('folder0', index, tiles[index], sensorMetaCache);
        });
        layoutTiles('folder0', tiles);
      }
      // A folder back on the screen: the place the device gave it.
      if (extra.out !== undefined) {
        const index = homeFolderTileIndex(Number(extra.out));
        const tile = index >= 0 ? tiles[index] : null;
        const place = tile ? data.places?.[ACTIVE_LAYOUT]?.['0']?.[String(tile.view_id)] : null;
        if (place) {
          Object.assign(tile, {col: place[0], row: place[1], span_w: place[2], span_h: place[3]});
          renderTileFromData('folder0', index, tile, sensorMetaCache);
          layoutTiles('folder0', tiles);
        } else {
          const fresh = await fetchTileGridData('folder0', true);
          fresh.forEach((data, i) => renderTileFromData('folder0', i, data, sensorMetaCache));
          layoutTiles('folder0', fresh);
        }
      }
      showNotification(t('pagesSaved'));
      return true;
    } catch (error) {
      return undo(t('networkErrorSave'));
    }
  }

  // The free page field picked in the editor (-1: none).
  let markedPageSlot = -1;

  // A free field picked (#100, user 2026-10-22): its teal ring and "+", and
  // the editor with Type offering Empty and Folder only, and the title and
  // icon the new folder gets. Nothing is made yet.
  function selectPageField(slot) {
    if (pageFieldsInWindow() || pageSlotList()[slot] || markedPageSlot === slot) return;
    if (currentTileTab && typeof parkClimateMiniEditor === 'function') parkClimateMiniEditor(currentTileTab);
    clearPageFieldMark();
    currentTileIndex = -1;
    currentTileTab = 'folder0';
    newTileSpot = null;
    persistSelectedTileState();
    document.querySelectorAll('.tile-grid > .tile').forEach(tile => {
      tile.classList.remove('active');
      delete tile.dataset.selected;
    });
    document.getElementById('settingsHiddenTile')?.classList.remove('active');
    markedPageSlot = slot;
    const specific = document.querySelector('#folder0Settings .tile-specific-settings');
    specific?.classList.remove('hidden');
    specific?.classList.add('page-field-marked');
    setSettingsPane('folder0', 'tile');
    applyFolderTypeLock('folder0', false);
    const type = document.getElementById('folder0_tile_type');
    if (type) {
      for (const option of type.options) {
        option.dataset.pageHidden = option.hidden ? '1' : '0';
        option.hidden = option.value !== '0' && option.value !== '4';
      }
      type.value = '0';
    }
    const title = document.getElementById('folder0_tile_title');
    if (title) title.value = '';
    const icon = document.getElementById('folder0_tile_icon');
    if (icon) icon.value = '';
    updateTileType('folder0');
    if (typeof refreshTileIconButtons === 'function') refreshTileIconButtons('folder0');
    setupLivePreview('folder0');
    layoutPageFields('folder0');
  }

  // The pick ends with any other selection; `hideEditor` when nothing else
  // is picked (its field was taken).
  function clearPageFieldMark(hideEditor = false) {
    if (markedPageSlot < 0) return;
    markedPageSlot = -1;
    document.querySelectorAll('.tile-grid > .page-field.marked').forEach(field => field.classList.remove('marked'));
    const specific = document.querySelector('#folder0Settings .tile-specific-settings');
    specific?.classList.remove('page-field-marked');
    if (hideEditor && currentTileIndex === -1) specific?.classList.add('hidden');
    for (const option of document.getElementById('folder0_tile_type')?.options || []) {
      if (option.dataset.pageHidden === undefined) continue;
      option.hidden = option.dataset.pageHidden === '1';
      delete option.dataset.pageHidden;
    }
  }

  // Type changed while a free field is picked (editor.js): Folder makes its
  // page, the menu stays at Empty until the panel made it.
  function pageFieldTypeChosen(select) {
    if (markedPageSlot < 0) return false;
    const folder = select.value === '4';
    select.value = '0';
    updateTileType('folder0');
    if (folder) addPageFolder(markedPageSlot);
    return true;
  }

  // The panel makes a new Home folder that is the page there at once
  // (/api/pages new=<slot>), also when Home's screen has no free cell; the
  // picked field's title and icon name it, the editor then shows its tile.
  function addPageFolder(slot) {
    if (pageFieldsInWindow()) return pageSlotsQueue;
    const named = markedPageSlot === slot;
    const title = named ? String(document.getElementById('folder0_tile_title')?.value || '').trim() : '';
    const icon = named ? String(document.getElementById('folder0_tile_icon')?.value || '') : '';
    pageSlotsQueue = pageSlotsQueue.then(() => sendNewPage(slot, title, icon));
    return pageSlotsQueue;
  }

  async function sendNewPage(slot, title, icon) {
    // A second request before the first was done finds the field taken.
    const slots = pageSlotList();
    if (slots[slot]) return false;
    try {
      const body = new URLSearchParams({pages: encodePageSlots(slots), new: String(slot)});
      if (title) body.set('title', title);
      if (icon) body.set('icon_name', icon);
      const response = await fetch('/api/pages', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: body.toString()
      });
      let data = {};
      try { data = await response.json(); } catch (_) {}
      if (!response.ok || !data.success) {
        const used = (getTilesData('folder0') || []).filter(tile => Number(tile?.type || 0) !== 0).length;
        showNotification(data.error === 'Folder full' ? tf('folderFull', {n: used, max: TILES_PER_GRID})
          : t('networkErrorSave'), false);
        return false;
      }
      const folder = Number(data.folder) || 0;
      const index = Number(data.index);
      slots[slot] = folder;
      pageSlotsElement().dataset.pages = encodePageSlots(slots);
      const fresh = await fetchTileGridData('folder0', true);
      fresh.forEach((tile, i) => renderTileFromData('folder0', i, tile, sensorMetaCache));
      layoutTiles('folder0', fresh);
      syncPageHeads();
      const ok = await ensureFolderTabUi(folder, String(data.title || ''), '');
      if (!ok) {
        persistSelectedTileState();
        setTimeout(() => location.reload(), 400);
        return true;
      }
      if (index >= 0) selectTile(index, 'folder0');
      showNotification(t('pagesSaved'));
      return true;
    } catch (error) {
      showNotification(t('networkErrorSave'), false);
      return false;
    }
  }

  // A page leaving the fields onto Home's screen (drag-resize.js
  // handleGridDrop).
  function canPlacePageTile(candidate) {
    if (!dragSource || dragSource.kind !== 'page-slot') return false;
    return supportedTileLayout(dragSource.settings ? 7 : 4, candidate) &&
      canPlaceTileLayout('folder0', dragSource.index, candidate);
  }

  function dropPageOnGrid(candidate) {
    const folder = dragSource.folder;
    dragSource.dropCommitted = true;
    // The layout window's parked Settings tile back on its area, its own size.
    if (dragSource.settings) {
      layoutSettingsOut({col: candidate.col, row: candidate.row, span_w: candidate.span_w, span_h: candidate.span_h});
      return;
    }
    // One cell, like the device (/api/pages).
    const place = {col: candidate.col, row: candidate.row, span_w: 1, span_h: 1};
    // In the layout window it stays there until "Speichern" (layout-window.js).
    if (pageFieldsInWindow()) {
      layoutPageOut(folder, place);
      return;
    }
    // Its field stays free.
    savePages(pageSlotList().map(id => id === folder ? 0 : id), {out: folder, ...place});
  }

  function enablePageFields() {
    const grid = getTileGrid('folder0');
    if (!pageSlotsElement() || !grid || grid.dataset.pageFieldsBound === '1') return;
    grid.dataset.pageFieldsBound = '1';
    layoutPageFields('folder0');
    syncPageHeads();
    // A page's tile is taken like any tile; its drag is a page's: one cell,
    // onto the screen it becomes a folder tile again (after the tile's own
    // dragstart, drag-resize.js enableTileDrag).
    grid.addEventListener('dragstart', event => {
      const tile = event.target.closest?.('.tile.page-field-tile');
      if (!tile || dragSource?.kind !== 'grid-tile' || dragSource.index !== Number(tile.dataset.index)) return;
      // The layout window's parked Settings tile: out of its field in its
      // own size (layoutSettingsOut).
      const settings = tile.dataset.type === '7';
      const own = settings ? getTilesData('folder0')?.[Number(tile.dataset.index)] : null;
      // The grab point from where it is drawn: its stored place lies elsewhere.
      dragSource = {...dragSource, kind: 'page-slot', folder: Number(tile.dataset.navigateTarget || 0), settings,
        layout: {col: 0, row: 0, span_w: Number(own?.span_w) || 1, span_h: Number(own?.span_h) || 1},
        grabCellCol: 0, grabCellRow: 0,
        dropOffset: getDragGrabOffset(tile.getBoundingClientRect(), event.clientX, event.clientY),
        baseLayouts: null, pageTarget: null};
    });
  }

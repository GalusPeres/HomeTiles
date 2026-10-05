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

  // ---- Icon button ----

  function tileHeadTab(element) {
    return String(element?.id || '').replace(/_tile_(icon|icon_picker|title)$/, '');
  }

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

  function refreshTileIconButtons(tab) {
    const selector = tab ? '#' + tab + '_tile_icon' : 'input[data-tile-icon]';
    document.querySelectorAll(selector).forEach(input => renderTileIconButton(tileHeadTab(input)));
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
        if (title) syncTileTitleRows(this);
        else renderTileIconButton(tileHeadTab(this));
      }
    });
    if (title) syncTileTitleRows(element);
    else renderTileIconButton(tileHeadTab(element));
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
    if (target?.classList?.contains('tile-title-input')) syncTileTitleRows(target);
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

  // A newly chosen entity brings its icon to the button.
  document.addEventListener('change', event => {
    const slot = event.target?.closest?.('.tile-entity-slot');
    if (slot) renderTileIconButton(slot.id.replace(/_tile_entity_slot$/, ''));
  });

  document.addEventListener('DOMContentLoaded', () => {
    setupTileHeads(document);
    if (typeof MutationObserver !== 'function') return;
    new MutationObserver(records => {
      for (const record of records) {
        for (const node of record.addedNodes) {
          if (node.nodeType !== 1) continue;
          if (node.matches?.('input[data-tile-icon], textarea.tile-title-input')) attachTileHead(node);
          else if (node.querySelector?.('input[data-tile-icon], textarea.tile-title-input')) setupTileHeads(node);
        }
      }
    }).observe(document.body, { childList: true, subtree: true });
  });

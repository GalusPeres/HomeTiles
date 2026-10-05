  // Shared entity picker of every tile field that names a Home Assistant
  // entity, like Home Assistant's own picker: icon, name, entity id and type,
  // with a search. The server renders `<input type="hidden"
  // data-entity-picker="<list>">` (appendEntityPickerField); the value stays
  // in that input, so loading, saving, drafts, copy and import keep using the
  // field id as before. The choices are the Bridge's released entities from
  // /api/entity_options (fetchEntityOptions), filtered to the field's list.

  // Home Assistant domains in the order of ENTITY_KIND_LABELS
  // (LocaleProfile::entity_kind_labels).
  const ENTITY_KIND_DOMAINS = [
    ['sensor'], ['binary_sensor'], ['light'], ['switch', 'input_boolean'], ['fan'], ['cover'],
    ['climate'], ['media_player'], ['weather'], ['camera'], ['lock'], ['alarm_control_panel'],
    ['number', 'input_number'], ['select', 'input_select'],
    ['datetime', 'date', 'time', 'input_datetime'], ['scene'], ['script'], ['button', 'input_button'],
    ['automation'], ['humidifier'], ['siren'], ['remote']
  ];
  const ENTITY_PICKER_FALLBACK_ICON = 'shape-outline';
  const entityPickerNativeValue = typeof HTMLInputElement === 'function'
    ? Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value') : null;
  let entityPickerPopover = null;
  let entityPickerOpen = null;  // {input, entries, query, active, state}

  function entityKindLabel(entityId) {
    const domain = String(entityId || '').split('.')[0];
    const index = ENTITY_KIND_DOMAINS.findIndex(domains => domains.includes(domain));
    return index >= 0 && typeof ENTITY_KIND_LABELS !== 'undefined' ? ENTITY_KIND_LABELS[index] || '' : '';
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

  // What the field shows for its value, also when the value is no longer in
  // the released list (it is kept, never cleared).
  function entityPickerEntryFor(input, value) {
    if (!value) return null;
    const list = input?.dataset?.entityPicker || '';
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
      '<span class="entity-picker-text"><span class="entity-picker-name">' + mark(entry.name) + '</span>' +
      '<span class="entity-picker-context">' + mark(entry.entity) + '</span></span>';
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
    if (entityPickerOpen && entityPickerOpen.state !== 'ready') loadEntityPickerEntries();
  }

  // The tile tab of a tile's own entity field (it sits in the entity slot
  // under Type); '' for other entity fields such as the icon color source.
  function entityPickerTileTab(input) {
    const slot = input?.closest?.('.tile-entity-slot');
    return slot ? slot.id.replace(/_tile_entity_slot$/, '') : '';
  }

  // A tile takes the name, icon and icon color of every entity chosen for it;
  // they stay editable until another entity is chosen.
  function adoptEntityPickerEntry(tab, entry) {
    const title = document.getElementById(tab + '_tile_title');
    if (title) title.value = normalizeTileTitle(entry.name);
    // An empty icon is the entity's own icon, also its state icons.
    const icon = document.getElementById(tab + '_tile_icon');
    if (icon) icon.value = '';
    if (typeof setIconColorInput === 'function') setIconColorInput(tab, '');
  }

  function chooseEntityPickerValue(input, value, entry) {
    if (!input || input.value === value) return;
    input.value = value;
    const tab = entry ? entityPickerTileTab(input) : '';
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
  }

  // ---- Open list ----

  function entityPickerMatches() {
    const open = entityPickerOpen;
    const words = open.query.toLowerCase().split(/\s+/).filter(Boolean);
    return open.entries.filter(entry => {
      const text = (entry.name + ' ' + entry.value + ' ' + entry.entity).toLowerCase();
      return words.every(word => text.includes(word));
    });
  }

  function renderEntityPickerList() {
    const open = entityPickerOpen;
    const list = entityPickerPopover?.querySelector('.entity-picker-list');
    if (!open || !list) return;
    if (open.state === 'loading') {
      list.innerHTML = '<div class="entity-picker-loading"><div></div><div></div><div></div></div>';
      return;
    }
    if (open.state === 'failed') {
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
    }).join('');
    list.querySelector('.entity-picker-item.active')?.scrollIntoView({ block: 'nearest' });
  }

  function loadEntityPickerEntries(force) {
    const open = entityPickerOpen;
    if (!open) return;
    const apply = data => {
      open.entries = entityPickerEntries(data, open.input.dataset.entityPicker);
      open.state = 'ready';
      // Without a search the list starts at the chosen entity.
      if (!open.query) open.active = Math.max(0, open.entries.findIndex(entry => entry.value === open.input.value));
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
    if (!entityPickerPopover) {
      entityPickerPopover = document.createElement('div');
      entityPickerPopover.className = 'entity-picker-popover';
      entityPickerPopover.innerHTML = '<div class="entity-picker-search"><i class="mdi mdi-magnify"></i>' +
        '<input type="search" autocomplete="off" spellcheck="false"></div>' +
        '<div class="entity-picker-list" role="listbox"></div>';
      document.body.appendChild(entityPickerPopover);
    }
    const search = entityPickerPopover.querySelector('input');
    search.value = '';
    search.placeholder = t('entityPickerSearch');
    entityPickerOpen = { input, entries: [], query: '', active: 0, state: 'loading' };
    const field = entityPickerControl(input)?.querySelector('.entity-picker-field');
    field?.classList.add('open');
    field?.setAttribute('aria-expanded', 'true');
    entityPickerPopover.classList.add('open');
    loadEntityPickerEntries(false);
    placeEntityPickerPopover();
    search.focus();
  }

  function closeEntityPicker(focusField) {
    const open = entityPickerOpen;
    if (!open) return;
    entityPickerOpen = null;
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
    if (item) pickEntityPickerIndex(Number(item.dataset.index));
    else if (target.closest('.entity-picker-retry')) loadEntityPickerEntries(true);
  });

  document.addEventListener('input', event => {
    if (!entityPickerOpen || !event.target?.closest?.('.entity-picker-search')) return;
    entityPickerOpen.query = event.target.value;
    entityPickerOpen.active = 0;
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
    if (entityPickerOpen && !event.target?.closest?.('.entity-picker-popover')) placeEntityPickerSoon();
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

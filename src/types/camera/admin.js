  // The Camera tile's picture (src/types/camera/picture.h), kept in fields
  // the tile does not use otherwise: sensor_display_mode 1 = the picture,
  // sensor_decimals its placement (0 fill, 1 fit, 2 original), image_slideshow_sec
  // a camera's interval, key_code the transparency of the circle and the
  // title pill, key_modifier bit 0 a tap opens the full screen, bit 1 the
  // light and bit 2 the dark head (neither: the design's). The form sends camera_display, camera_fit,
  // camera_every, camera_transparency, camera_head and camera_tap
  // (web_handler.cpp).
  const CAMERA_FITS = ['fill', 'fit', 'original'];
  const CAMERA_TAP_FULL = 1;
  const CAMERA_HEAD_LIGHT = 2;
  const CAMERA_HEAD_DARK = 4;
  // With several entries the tile shows the one last chosen in the popup.
  const CAMERA_LAST_CHOSEN = 8;

  function cameraEvery(value) {
    const seconds = Math.round(Number(value));
    return seconds >= 3 && seconds <= 60 ? seconds : 10;
  }

  function cameraTransparency(value) {
    const percent = Math.round(Number(value));
    return Number.isFinite(percent) ? Math.min(100, Math.max(0, percent)) : 0;
  }

  // The picture options of stored tile data (GET /api/tiles, drafts, imports).
  function cameraPictureOptionsOf(tile) {
    // A copied tile and a draft carry the editor's field names
    // (saveCameraFields); a stored tile its packed fields.
    if (tile && tile.camera_display !== undefined) {
      return {
        picture: String(tile.camera_display) === '1',
        fit: CAMERA_FITS.includes(tile.camera_fit) ? tile.camera_fit : 'fill',
        every: cameraEvery(tile.camera_every),
        transparency: cameraTransparency(tile.camera_transparency),
        head: ['dark', 'light'].includes(tile.camera_head) ? tile.camera_head : 'auto',
        tap: String(tile.camera_tap) === '1' ? '1' : '0',
        shows: tile.camera_shows === 'last' ? 'last' : ''
      };
    }
    const flags = Number(tile?.key_modifier) || 0;
    return {
      picture: Number(tile?.sensor_display_mode) === 1,
      fit: CAMERA_FITS[Number(tile?.sensor_decimals)] || 'fill',
      every: cameraEvery(tile?.image_slideshow_sec),
      transparency: cameraTransparency(tile?.key_code),
      head: (flags & CAMERA_HEAD_LIGHT) ? 'light' : (flags & CAMERA_HEAD_DARK) ? 'dark' : 'auto',
      tap: (flags & CAMERA_TAP_FULL) ? '1' : '0',
      shows: (flags & CAMERA_LAST_CHOSEN) ? 'last' : ''
    };
  }

  // key_modifier of the options (picture.h kTapFullScreen, kHeadLight, kHeadDark).
  function cameraOptionFlags(options) {
    return (options?.tap === '1' ? CAMERA_TAP_FULL : 0) | (options?.shows === 'last' ? CAMERA_LAST_CHOSEN : 0) |
      (options?.head === 'light' ? CAMERA_HEAD_LIGHT : options?.head === 'dark' ? CAMERA_HEAD_DARK : 0);
  }

  // The picture options in the editor's fields.
  function cameraPictureOptions(tab) {
    const value = field => document.getElementById(tab + '_' + field)?.value;
    const fit = value('camera_fit');
    return {
      picture: value('camera_display') === '1',
      fit: CAMERA_FITS.includes(fit) ? fit : 'fill',
      every: cameraEvery(value('camera_every')),
      transparency: cameraTransparency(value('camera_transparency')),
      head: ['dark', 'light'].includes(value('camera_head')) ? value('camera_head') : 'auto',
      tap: value('camera_tap') === '1' ? '1' : '0',
      shows: value('camera_shows') === 'last' ? 'last' : ''
    };
  }

  // The rows of a picture show only with "Picture"; the interval only with a Home
  // Assistant camera in the list (an image entity sends each new picture at once,
  // the Bridge's own cameras too).
  function syncCameraFields(tab) {
    const options = cameraPictureOptions(tab);
    const editor = cameraEditor(tab);
    const camera = editor.source === 'h' && editor.rows.h.some(row => String(row.value).startsWith('camera.'));
    document.getElementById(tab + '_camera_picture_fields')?.classList.toggle('hidden', !options.picture);
    document.getElementById(tab + '_camera_every_row')?.classList.toggle('hidden', !camera);
    const output = document.getElementById(tab + '_camera_transparency_value');
    if (output) output.textContent = options.transparency + ' %';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
  }

  function loadCameraFields(tab, data) {
    loadIconColorFields(tab, data);
    const configured = data.sensor_entity || data.camera_entity || '';
    const options = cameraPictureOptionsOf(data);
    // The record, or the one entity of a tile from before the entries.
    const editor = cameraEditor(tab);
    let rows = cameraParseRecord(data.camera_entries);
    if (!rows.length && cameraEntryEntity('h', configured)) {
      const title = document.getElementById(tab + '_tile_title')?.value || '';
      const name = sensorMetaCache.names?.[configured] || titleFromEntity(configured) || '';
      rows = [{ kind: 'h', value: configured, title: title === normalizeTileTitle(name) ? '' : title,
        icon: document.getElementById(tab + '_tile_icon')?.value || '', name, line: configured }];
    }
    const source = rows.length ? cameraSourceOfKind(rows[0].kind)
      : ['r', 'w'].includes(data.camera_source) ? data.camera_source : 'h';
    editor.source = source;
    editor.rows = { h: [], r: [], w: [] };
    editor.rows[source] = rows;
    editor.open = -1;
    const shownAt = cameraValidRows(rows).findIndex(row => cameraEntryEntity(row.kind, row.value) === configured);
    editor.shows = { h: 0, r: 0, w: 0 };
    editor.shows[source] = options.shows === 'last' ? 'last' : Math.max(0, shownAt);
    const set = (field, value) => {
      const input = document.getElementById(tab + '_' + field);
      if (input) input.value = String(value);
    };
    set('camera_display', options.picture ? '1' : '0');
    set('camera_fit', options.fit);
    set('camera_every', options.every);
    set('camera_transparency', options.transparency);
    set('camera_head', options.head);
    set('camera_tap', options.tap);
    set('camera_source', source);
    renderCameraEntries(tab);
    cameraSyncTile(tab);
  }
  function saveCameraFields(tab, formData) {
    const entity =
      document.getElementById(tab + '_camera_entity')?.value || '';
    formData.append('camera_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('camera_entries', document.getElementById(tab + '_camera_entries')?.value || '');
    formData.append('camera_source', cameraEditor(tab).source);
    formData.append('camera_shows', cameraEditor(tab).shows[cameraEditor(tab).source] === 'last' ? 'last' : '');
    const options = cameraPictureOptions(tab);
    formData.append('camera_display', options.picture ? '1' : '0');
    formData.append('camera_fit', options.fit);
    formData.append('camera_every', String(options.every));
    formData.append('camera_transparency', String(options.transparency));
    formData.append('camera_head', options.head);
    formData.append('camera_tap', options.tap);
    saveIconColorFields(tab, formData);
  }
  function resetCameraFields(tab) {
    resetIconColorFields(tab);
    const el = document.getElementById(tab + '_camera_entity');
    if (el) el.value = '';
    const entriesField = document.getElementById(tab + '_camera_entries');
    if (entriesField) entriesField.value = '';
    cameraEditors.delete(tab);
    const set = (field, value) => {
      const input = document.getElementById(tab + '_' + field);
      if (input) input.value = value;
    };
    set('camera_display', '0');
    set('camera_fit', 'fill');
    set('camera_every', '10');
    set('camera_transparency', '0');
    set('camera_head', 'auto');
    set('camera_tap', '0');
    set('camera_source', 'h');
    renderCameraEntries(tab);
    syncCameraFields(tab);
  }

  // The head over the picture (camera_picture_layout::head): the circle in
  // the corner, the title on a pill mirrored into the other corner, or one
  // shared pill when less than a grid gap would stay free between them or the
  // title does not fit its pill.
  function cameraPictureHead(cardW, inset, disc, shrink, pad, gap, text) {
    text = Math.max(0, text);
    const boxW = text + 2 * (pad + shrink);
    const pillX = cardW - inset - boxW;
    const circleRight = inset + disc - shrink;
    if (pillX + shrink - circleRight >= gap) {
      return {shared: false, pillX, pillW: boxW, textX: pillX + shrink + pad, textW: text};
    }
    const textX = inset + disc;
    return {shared: true, pillX: inset, pillW: cardW - 2 * inset, textX,
            textW: Math.max(0, cardW - inset - shrink - pad - textX)};
  }

  let cameraMeasureContext = null;
  function cameraTextWidth(element, text) {
    if (!cameraMeasureContext && typeof document.createElement === 'function') {
      cameraMeasureContext = document.createElement('canvas').getContext?.('2d') || null;
    }
    if (!cameraMeasureContext || typeof getComputedStyle !== 'function') return 0;
    cameraMeasureContext.font = getComputedStyle(element).font;
    return cameraMeasureContext.measureText(text).width;
  }

  // The head rule's title width in display pixels (camera_picture_layout::
  // title_metric): the panel's whole-pixel advances of its title font
  // (CAMERA_TILE_LAYOUT.adv), so panel and preview choose the same head. A
  // character outside that table is measured in the browser and scaled.
  function cameraTitleMetric(layout, text, element, scale) {
    let width = 0;
    for (const char of String(text)) {
      const advance = layout?.adv?.[char.codePointAt(0) - layout.first];
      width += advance ? advance : (element ? cameraTextWidth(element, char) / (scale || 1) : 0);
    }
    return width;
  }

  // tile_geometry::extent in display pixels.
  function cameraExtent(position, span, cell, gap) {
    const edge = value => Math.round(value * (cell + gap));
    return edge(position + span) - edge(position) - gap;
  }

  // picture.h head_over_picture: the head over the picture follows the
  // design ('auto') or draws dark or light in either design ('' without a
  // picture); its card takes that design's tile color, never an own color
  // (cameraHeadBackground).
  function cameraHeadSurface(options, halfHeight) {
    if (!options?.picture || halfHeight) return '';
    return ['dark', 'light'].includes(options.head) ? options.head : 'auto';
  }
  function cameraHeadBackground(surface) {
    return surface === 'light' ? 'var(--pvl-card)' : surface === 'dark' ? 'var(--tile-default-bg)' : 'var(--tile-card)';
  }

  // The preview of a Camera tile showing its picture (live and cached grid):
  // the browser has no picture, so a neutral stand-in fills the card; the
  // head sits like on the panel, chosen in display pixels from the tile's
  // column and span (`place`). Half-height tiles keep the icon look.
  function applyCameraPicturePreview(tileElem, options, halfHeight, place) {
    if (!tileElem) return;
    const on = !!options?.picture && !halfHeight;
    tileElem.classList.toggle('camera-picture', on);
    tileElem.classList.remove('camera-head-shared');
    if (!on) return;
    tileElem.style.setProperty('--camera-head-opa', String((100 - cameraTransparency(options.transparency)) / 100));
    const title = tileElem.querySelector(':scope > .tile-title');
    if (!title || typeof getComputedStyle !== 'function') return;
    // One line, like the panel (hometiles_title single_line).
    const lines = title.querySelectorAll ? [...title.querySelectorAll('.tile-title-line')] : [];
    const text = lines.length ? lines.map(line => line.textContent).join(' ') : String(title.textContent || '');
    title.textContent = text;
    const layout = typeof CAMERA_TILE_LAYOUT === 'object' ? CAMERA_TILE_LAYOUT : null;
    if (!layout) return;
    const scale = parseFloat(getComputedStyle(tileElem).getPropertyValue('--radius-preview-scale')) || 0;
    const cardW = cameraExtent(Number(place?.col) || 0, Math.max(1, Number(place?.span_w) || 1),
                               layout.cellW, layout.gap);
    const head = cameraPictureHead(cardW, layout.inset, layout.disc, layout.shrink, layout.pad, layout.gap,
                                   cameraTitleMetric(layout, text, title, scale));
    tileElem.classList.toggle('camera-head-shared', head.shared);
  }

  // ---- The entries of a Camera tile (types/camera/camera_entries.h) ----
  // One source for the whole tile, under it its entries, all built alike: a row
  // that folds out in its own frame, one open at a time. The source's own field,
  // then title and icon as in a tile's head; removing only in there. Adding only
  // adds a row. The first or chosen entry fills the tile's own fields (the hidden
  // <tab>_camera_entity, <tab>_tile_title, <tab>_tile_icon), so previews, drafts
  // and saving work as for every tile.

  const CAMERA_MAX_ENTRIES = 8;
  // A radar's and a webcam's default color (mockup webcam-radar/radar-tall.html),
  // a plain icon color the user can change.
  const CAMERA_SOURCE_COLOR = '#29B6F6';
  const CAMERA_OWN_COORDINATES = 'geo:own';
  // The picker's kinds per source; camera_entries.h kHomeAssistant and so on.
  const CAMERA_PLACE_KINDS = ['r', 'c'];
  const cameraEditors = new Map();

  function cameraText(key) {
    return typeof CAMERA_TEXT === 'object' ? String(CAMERA_TEXT[key] ?? '') : '';
  }

  function cameraEditor(tab) {
    if (!cameraEditors.has(tab)) {
      cameraEditors.set(tab, { source: 'h', rows: { h: [], r: [], w: [] }, shows: { h: 0, r: 0, w: 0 }, open: -1 });
    }
    return cameraEditors.get(tab);
  }

  function cameraSourceOfKind(kind) {
    return CAMERA_PLACE_KINDS.includes(kind) ? 'r' : kind === 'w' ? 'w' : 'h';
  }

  function cameraFnv1a(text) {
    let hash = 2166136261;
    for (const byte of new TextEncoder().encode(String(text))) {
      hash = Math.imul(hash ^ byte, 16777619) >>> 0;
    }
    return hash >>> 0;
  }

  function cameraHex(hash) {
    return hash.toString(16).padStart(8, '0');
  }

  function cameraCoordinates(value) {
    const found = /^geo:(-?[0-9.]+),(-?[0-9.]+)$/.exec(String(value || ''));
    if (!found) return null;
    const lat = Number(found[1]);
    const lon = Number(found[2]);
    return Number.isFinite(lat) && Number.isFinite(lon) && Math.abs(lat) <= 90 && Math.abs(lon) <= 180 ? { lat, lon } : null;
  }

  function cameraHundredths(value) {
    const number = Math.round(Math.abs(value) * 100);
    return value < 0 && number ? 'm' + number : String(number);
  }

  function cameraHttpsAddress(value) {
    const text = String(value || '');
    return text.length > 8 && text.length <= 400 && text.startsWith('https://') && !/[\s"\\]/.test(text);
  }

  // camera_entries::entity_of: the entity id of an entry, '' when it names nothing.
  function cameraEntryEntity(kind, value) {
    const text = String(value || '');
    if (kind === 'h') return /^(camera|image)\.[a-z0-9_]+$/.test(text) ? text : '';
    if (kind === 'w') return cameraHttpsAddress(text) ? 'camera.ht_cam_' + cameraHex(cameraFnv1a(text)) : '';
    if (kind === 'r' && text === 'zone.home') return 'camera.ht_radar_home';
    if (kind === 'r' && text.startsWith('zone.')) {
      const zone = text.slice(5);
      if (!/^[a-z0-9_]+$/.test(zone)) return '';
      return 'camera.ht_radar_zone_' + (zone.length <= 43 ? zone : cameraHex(cameraFnv1a(zone)));
    }
    if (kind !== 'r' && kind !== 'c') return '';
    const place = cameraCoordinates(text);
    return place ? 'camera.ht_radar_' + cameraHundredths(place.lat) + '_' + cameraHundredths(place.lon) : '';
  }

  function cameraEntryField(text, limit) {
    return Array.from(String(text || '').replace(/[\u0000-\u001f]/g, '')).reduce((out, char) =>
      new TextEncoder().encode(out + char).length <= limit ? out + char : out, '');
  }

  function cameraParseRecord(record) {
    const rows = [];
    for (const line of String(record || '').split('\n')) {
      const [kind, value, title, icon, name, context] = line.split('\t');
      if (!cameraEntryEntity(kind, value)) continue;
      const row = { kind, value, title: title || '', icon: icon || '', name: name || '', line: context || '' };
      if (kind === 'c') {
        const place = cameraCoordinates(value);
        row.lat = String(place.lat);
        row.lon = String(place.lon);
      }
      rows.push(row);
    }
    return rows;
  }

  // The source's rows that name something, one per entity.
  function cameraValidRows(rows) {
    const seen = new Set();
    return rows.filter(row => {
      const entity = cameraEntryEntity(row.kind, row.value);
      if (!entity || seen.has(entity)) return false;
      seen.add(entity);
      return true;
    }).slice(0, CAMERA_MAX_ENTRIES);
  }

  function cameraRecordOf(rows) {
    return cameraValidRows(rows).map(row => [row.kind, row.value, cameraEntryField(row.title, 64), cameraEntryField(row.icon, 48),
      cameraEntryField(row.name, 64), cameraEntryField(row.line, 64)].join('\t')).join('\n');
  }

  function cameraRowTitle(row) {
    return row.title || row.name || '';
  }

  // The icon a row stands for without an own one, in the list and in the icon
  // picker's Automatic.
  function cameraRowAutoIcon(row) {
    if (CAMERA_PLACE_KINDS.includes(row.kind)) return 'map-marker';
    if (row.kind === 'w') return 'webcam';
    const known = entityRemoteInfo.get(row.value)?.icon || normalizeMdiIconName(sensorMetaCache.icons?.[row.value] || '');
    return known || (String(row.value).startsWith('image.') ? 'image' : 'video');
  }

  function cameraRowIcon(row) {
    if (isExplicitlyDisabledValue(row.icon)) return 'cancel';
    return normalizeMdiIconName(row.icon) || cameraRowAutoIcon(row);
  }

  // What the tile stores as its icon for this entry: on the panel a radar shows
  // its own icon, a webcam its own, a Home Assistant entity its entity's
  // (types/camera/renderer.cpp camera_entry_icon).
  function cameraTileIcon(row) {
    if (row.icon) return row.icon;
    return CAMERA_PLACE_KINDS.includes(row.kind) ? 'weather-pouring' : row.kind === 'w' ? 'webcam' : '';
  }

  function cameraOwnCoordinatesEntry() {
    return { value: CAMERA_OWN_COORDINATES, name: cameraText('ownCoordinates'), icon: 'target', entity: '',
      context: cameraText('ownCoordinatesHint') };
  }

  // The kind at the right of a picker row.
  function cameraPickerKind(entry) {
    const value = String(entry?.value || '');
    if (value === 'zone.home') return cameraText('placeHome');
    if (value.startsWith('zone.')) return cameraText('kindZone');
    if (value === CAMERA_OWN_COORDINATES) return '';
    if (value.startsWith('geo:')) return cameraText('place');
    if (value.startsWith('image.')) return cameraText('kindImage');
    return '';
  }

  function cameraEntryBodyHtml(tab, editor, row, index) {
    const id = tab + '_camera_entry';
    const removeTitle = escapeHtml(cameraText('remove'));
    const fieldLabel = (text, target) => '<div class="camera-entry-label"><label for="' + target + '">' + escapeHtml(text) +
      '</label><button type="button" class="camera-entry-remove" data-camera-remove="' + index + '" aria-label="' + removeTitle +
      '" title="' + removeTitle + '"><i class="mdi mdi-delete-outline"></i></button></div>';
    let field = '';
    if (editor.source === 'w') {
      field = '<div>' + fieldLabel(cameraText('address'), id + '_url') + '<input type="url" id="' + id +
        '_url" data-camera-url placeholder="https://" spellcheck="false" autocomplete="off" value="' + escapeHtml(row.value) + '">' +
        '<div class="camera-entry-fail" id="' + id + '_fail" hidden>' + escapeHtml(cameraText('noPictureFound')) + '</div></div>';
    } else {
      const label = editor.source === 'r' ? cameraText('place') : cameraText('entity');
      const value = row.kind === 'c' ? CAMERA_OWN_COORDINATES : row.value;
      field = '<div>' + fieldLabel(label, id + '_value_picker') + '<input type="hidden" id="' + id +
        '_value" data-entity-picker="' + (editor.source === 'r' ? 'places' : 'images') + '" value="' + escapeHtml(value) + '"></div>';
      if (row.kind === 'c') {
        field += '<div class="camera-entry-coords"><div><label for="' + id + '_lat">' + escapeHtml(cameraText('latitudeLabel')) +
          '</label><input type="text" inputmode="decimal" id="' + id + '_lat" data-camera-coordinate value="' + escapeHtml(row.lat || '') +
          '"></div><div><label for="' + id + '_lon">' + escapeHtml(cameraText('longitudeLabel')) +
          '</label><input type="text" inputmode="decimal" id="' + id + '_lon" data-camera-coordinate value="' + escapeHtml(row.lon || '') +
          '"></div></div><div class="camera-entry-fail" id="' + id + '_coord_fail" hidden></div>';
      }
    }
    const none = isExplicitlyDisabledValue(row.icon);
    return '<div class="camera-entry-body">' + field +
      '<div class="tile-head-row"><div><div class="tile-title-label"><label for="' + id + '_title">' + escapeHtml(cameraText('title')) +
      '</label><button type="button" class="tile-title-auto' + (row.title ? '' : ' on') + '" data-camera-auto aria-pressed="' +
      (row.title ? 'false' : 'true') + '">' + escapeHtml(cameraText('auto')) + '</button></div><input type="text" id="' + id +
      '_title" data-camera-title maxlength="64" value="' + escapeHtml(cameraRowTitle(row)) + '"></div><div><label for="' + id +
      '_icon_button">' + escapeHtml(cameraText('icon')) + '</label><input type="hidden" id="' + id + '_icon" value="' + escapeHtml(row.icon) +
      '"><button type="button" class="tile-icon-picker' + (none ? ' none' : '') + '" id="' + id + '_icon_button" data-icon-input="' + id +
      '_icon" data-icon-entry="1" data-icon-auto="' + escapeHtml(cameraRowAutoIcon(row)) + '" aria-haspopup="listbox"><i class="mdi mdi-' +
      escapeHtml(cameraRowIcon(row)) + '"></i></button></div></div></div>';
  }

  function cameraEntryPlaceholder(editor) {
    return editor.source === 'w' ? cameraText('newWebcam') : t('entityPickerChoose');
  }

  function cameraEntryHtml(tab, editor, row, index) {
    const open = editor.open === index;
    const name = cameraRowTitle(row);
    return '<div class="camera-entry" data-camera-entry="' + index + '"><div class="camera-entry-head" data-camera-toggle="' + index +
      '"><span class="entity-picker-icon"><i class="mdi mdi-' + escapeHtml(cameraRowIcon(row)) + '"></i></span>' +
      '<span class="entity-picker-text"><span class="entity-picker-name' + (name ? '' : ' camera-entry-empty') + '">' +
      escapeHtml(name || cameraEntryPlaceholder(editor)) + '</span><span class="entity-picker-context">' + escapeHtml(row.line || '') +
      '</span></span><i class="mdi mdi-pencil camera-entry-pen"></i></div>' +
      (open ? cameraEntryBodyHtml(tab, editor, row, index) : '') + '</div>';
  }

  // The picker shows a chosen place by what the row knows of it.
  function cameraTeachPicker(row) {
    const value = row.kind === 'c' ? CAMERA_OWN_COORDINATES : row.value;
    if (!value || row.kind === 'h') return;
    const name = row.kind === 'c' ? cameraText('ownCoordinates') : row.name || value;
    entityRemoteInfo.set(value, { value, name, label: name, icon: cameraRowAutoIcon(row), entity: value,
      context: row.line || '', remote: true });
  }

  function renderCameraEntries(tab) {
    const list = document.getElementById(tab + '_camera_list');
    if (!list) return;
    const editor = cameraEditor(tab);
    const rows = editor.rows[editor.source];
    const label = document.getElementById(tab + '_camera_list_label');
    if (label) label.textContent = cameraText(editor.source === 'r' ? 'listPlaces' : editor.source === 'w' ? 'listWebcams' : 'entity');
    const add = document.getElementById(tab + '_camera_add');
    if (add) {
      add.textContent = cameraText(editor.source === 'r' ? 'addPlace' : editor.source === 'w' ? 'addWebcam' : 'addEntity');
      add.classList.toggle('hidden', rows.length >= CAMERA_MAX_ENTRIES);
    }
    if (editor.open >= rows.length) editor.open = -1;
    const open = rows[editor.open];
    if (open) cameraTeachPicker(open);
    list.innerHTML = rows.map((row, index) => cameraEntryHtml(tab, editor, row, index)).join('');
    if (typeof setupEntityPickers === 'function') setupEntityPickers(list);
    cameraRenderShows(tab);
    cameraWatchSources(tab);
  }

  // "The tile shows": only with more than one entry, the first by default.
  function cameraRenderShows(tab) {
    const editor = cameraEditor(tab);
    const select = document.getElementById(tab + '_camera_shows');
    const row = document.getElementById(tab + '_camera_shows_row');
    if (!select) return;
    const rows = editor.rows[editor.source];
    const valid = cameraValidRows(rows);
    row?.classList.toggle('hidden', valid.length < 2);
    let shown = editor.shows[editor.source];
    if (shown !== 'last' && !(shown < valid.length)) shown = 0;
    editor.shows[editor.source] = shown;
    select.innerHTML = valid.map((entry, index) => '<option value="' + index + '">' +
      escapeHtml(cameraRowTitle(entry) || entry.value) + '</option>').join('') +
      '<option value="last">' + escapeHtml(cameraText('showsLast')) + '</option>';
    select.value = String(shown);
  }

  // The chosen entry into the tile's own fields, and every other derived field.
  function cameraSyncTile(tab) {
    const editor = cameraEditor(tab);
    const valid = cameraValidRows(editor.rows[editor.source]);
    const shown = editor.shows[editor.source];
    const row = valid[shown === 'last' ? 0 : shown] || valid[0] || null;
    const entity = row ? cameraEntryEntity(row.kind, row.value) : '';
    const set = (suffix, value) => {
      const input = document.getElementById(tab + suffix);
      if (input && input.value !== value) input.value = value;
    };
    set('_camera_entity', entity);
    set('_camera_entries', cameraRecordOf(editor.rows[editor.source]));
    set('_tile_title', row ? normalizeTileTitle(cameraRowTitle(row)) : '');
    set('_tile_icon', row ? cameraTileIcon(row) : '');
    syncCameraFields(tab);
  }

  function cameraEntriesChanged(tab) {
    cameraSyncTile(tab);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }

  // Quelle: each source keeps its own rows while the panel is open; the default
  // color follows a radar or a webcam unless another color was chosen.
  function cameraSourceChanged(tab) {
    const editor = cameraEditor(tab);
    const source = ['r', 'w'].includes(document.getElementById(tab + '_camera_source')?.value)
      ? document.getElementById(tab + '_camera_source').value : 'h';
    if (source === editor.source) return;
    editor.source = source;
    editor.open = -1;
    // Just picking the radar is enough: Home Assistant's home.
    if (source === 'r' && !editor.rows.r.length) {
      editor.rows.r.push({ kind: 'r', value: 'zone.home', title: '', icon: '', name: cameraText('placeHome'), line: 'Home Assistant' });
    }
    const color = document.getElementById(tab + '_tile_icon_color');
    if (color && typeof setIconColorInput === 'function') {
      const unset = color.dataset.unset !== '0';
      if (source !== 'h' && unset) setIconColorInput(tab, CAMERA_SOURCE_COLOR);
      else if (source === 'h' && !unset && String(color.value).toUpperCase() === CAMERA_SOURCE_COLOR) setIconColorInput(tab, '');
      if (typeof syncIconColorFields === 'function') syncIconColorFields(tab);
    }
    renderCameraEntries(tab);
    cameraEntriesChanged(tab);
  }

  function cameraShowsChanged(tab) {
    const editor = cameraEditor(tab);
    const value = document.getElementById(tab + '_camera_shows')?.value;
    editor.shows[editor.source] = value === 'last' ? 'last' : Number(value) || 0;
    cameraEntriesChanged(tab);
  }

  // A page without a picture, told by the Bridge (entity_search.h): asked while a
  // webcam's details are open.
  let cameraSourcesTimer = 0;
  function cameraWatchSources(tab) {
    window.clearTimeout(cameraSourcesTimer);
    const editor = cameraEditor(tab);
    if (editor.source !== 'w' || !editor.rows.w[editor.open]) return;
    const ask = () => {
      const fail = document.getElementById(tab + '_camera_entry_fail');
      const row = editor.rows.w[editor.open];
      if (!fail || !row || !row.value) return;
      fetch('/api/camera_sources')
        .then(res => (res.ok ? res.json() : null))
        .then(status => {
          const id = cameraEntryEntity('w', row.value).slice(7);
          const current = document.getElementById(tab + '_camera_entry_fail');
          if (current && id) current.hidden = status?.src?.[id] !== 'none';
        })
        .catch(() => {});
      cameraSourcesTimer = window.setTimeout(ask, 5000);
    };
    cameraSourcesTimer = window.setTimeout(ask, 300);
  }

  function cameraTabOf(element) {
    return element?.closest?.('.camera-entries-wrap')?.dataset.tab || '';
  }

  // The row's head without drawing the open field again (typing keeps its focus).
  function cameraShowRowHead(tab, index) {
    const editor = cameraEditor(tab);
    const row = editor.rows[editor.source][index];
    const head = document.querySelector('#' + tab + '_camera_list [data-camera-toggle="' + index + '"]');
    if (!row || !head) return;
    const name = cameraRowTitle(row);
    const label = head.querySelector('.entity-picker-name');
    label.textContent = name || cameraEntryPlaceholder(editor);
    label.classList.toggle('camera-entry-empty', !name);
    head.querySelector('.entity-picker-context').textContent = row.line || '';
    head.querySelector('.entity-picker-icon i').className = 'mdi mdi-' + cameraRowIcon(row);
    const auto = document.querySelector('#' + tab + '_camera_list [data-camera-auto]');
    auto?.classList.toggle('on', !row.title);
    auto?.setAttribute('aria-pressed', row.title ? 'false' : 'true');
    cameraRenderShows(tab);
  }

  function cameraCoordinate(text, limit) {
    const trimmed = String(text || '').trim();
    if (!trimmed) return null;
    const value = Number(trimmed.replace(',', '.'));
    return Number.isFinite(value) && Math.abs(value) <= limit ? value : null;
  }

  function cameraReadCoordinates(tab, row) {
    row.lat = document.getElementById(tab + '_camera_entry_lat')?.value || '';
    row.lon = document.getElementById(tab + '_camera_entry_lon')?.value || '';
    const lat = cameraCoordinate(row.lat, 90);
    const lon = cameraCoordinate(row.lon, 180);
    const fail = document.getElementById(tab + '_camera_entry_coord_fail');
    const wrong = String(row.lat).trim() && lat === null ? cameraText('latitudeRange')
      : String(row.lon).trim() && lon === null ? cameraText('longitudeRange') : '';
    if (fail) {
      fail.textContent = wrong;
      fail.hidden = !wrong;
    }
    if (lat === null || lon === null) {
      row.value = '';
      row.line = cameraText('noCoordinates');
    } else {
      row.value = 'geo:' + lat + ',' + lon;
      row.line = lat + ', ' + lon;
    }
    row.name = cameraText('ownCoordinates');
    cameraTeachPicker(row);
  }

  function cameraChoosePlace(tab, row, value) {
    const input = document.getElementById(tab + '_camera_entry_value');
    const entry = value && typeof entityPickerEntryFor === 'function' ? entityPickerEntryFor(input, value) : null;
    row.title = '';
    row.icon = '';
    if (value === CAMERA_OWN_COORDINATES) {
      const coordinates = cameraCoordinates(row.value);
      row.kind = 'c';
      row.value = coordinates ? row.value : '';
      row.lat = coordinates ? String(coordinates.lat) : '';
      row.lon = coordinates ? String(coordinates.lon) : '';
      row.name = cameraText('ownCoordinates');
      row.line = coordinates ? coordinates.lat + ', ' + coordinates.lon : cameraText('noCoordinates');
      return;
    }
    row.kind = 'r';
    row.value = value;
    row.name = entry?.name || '';
    row.line = entry?.context || (value.startsWith('zone.') ? 'Home Assistant' : '');
  }

  document.addEventListener('click', event => {
    const target = event.target;
    const tab = cameraTabOf(target);
    if (!tab) return;
    const editor = cameraEditor(tab);
    const rows = editor.rows[editor.source];
    const row = rows[editor.open];
    if (target.closest('[data-camera-add]')) {
      if (rows.length >= CAMERA_MAX_ENTRIES) return;
      const kind = editor.source === 'r' ? 'r' : editor.source;
      rows.push({ kind, value: '', title: '', icon: '', name: '', line: '' });
      renderCameraEntries(tab);
      return;
    }
    const remove = target.closest('[data-camera-remove]');
    if (remove) {
      const gone = Number(remove.dataset.cameraRemove);
      const valid = cameraValidRows(rows);
      const removedShown = valid.indexOf(rows[gone]);
      rows.splice(gone, 1);
      editor.open = -1;
      // What the tile shows follows the list; a removed entry falls back to the first.
      const shown = editor.shows[editor.source];
      if (typeof shown === 'number' && removedShown >= 0) {
        editor.shows[editor.source] = shown === removedShown ? 0 : shown > removedShown ? shown - 1 : shown;
      }
      renderCameraEntries(tab);
      cameraEntriesChanged(tab);
      return;
    }
    if (target.closest('[data-camera-auto]') && row) {
      row.title = row.title ? '' : cameraRowTitle(row);
      const title = document.getElementById(tab + '_camera_entry_title');
      if (title) title.value = cameraRowTitle(row);
      cameraShowRowHead(tab, editor.open);
      cameraEntriesChanged(tab);
      return;
    }
    const toggle = target.closest('[data-camera-toggle]');
    if (toggle) {
      const index = Number(toggle.dataset.cameraToggle);
      editor.open = editor.open === index ? -1 : index;
      renderCameraEntries(tab);
      if (editor.open >= 0) document.querySelector('#' + tab + '_camera_list [data-camera-entry="' + index + '"]')?.scrollIntoView({ block: 'nearest' });
    }
  });

  document.addEventListener('input', event => {
    const target = event.target;
    const tab = cameraTabOf(target);
    if (!tab) return;
    const editor = cameraEditor(tab);
    const row = editor.rows[editor.source][editor.open];
    if (!row) return;
    if (target.matches('[data-camera-title]')) {
      row.title = target.value === row.name ? '' : target.value;
    } else if (target.matches('[data-camera-coordinate]')) {
      cameraReadCoordinates(tab, row);
    } else if (target.matches('[data-camera-url]')) {
      row.value = target.value.trim();
      let host = '';
      try {
        host = new URL(row.value).hostname.replace(/^www\./, '');
      } catch (error) {
        host = '';
      }
      row.name = host;
      row.line = host;
      const fail = document.getElementById(tab + '_camera_entry_fail');
      if (fail) fail.hidden = true;
      if (!row.title) {
        const title = document.getElementById(tab + '_camera_entry_title');
        if (title) title.value = host;
      }
      cameraWatchSources(tab);
    } else if (target.id === tab + '_camera_entry_icon') {
      row.icon = target.value;
      const button = document.getElementById(tab + '_camera_entry_icon_button');
      if (button) {
        button.innerHTML = '<i class="mdi mdi-' + escapeHtml(cameraRowIcon(row)) + '"></i>';
        button.classList.toggle('none', isExplicitlyDisabledValue(row.icon));
      }
    } else {
      return;
    }
    cameraShowRowHead(tab, editor.open);
    cameraEntriesChanged(tab);
  });

  // The entity picker of the open entry.
  document.addEventListener('change', event => {
    const target = event.target;
    const tab = cameraTabOf(target);
    if (!tab || target.id !== tab + '_camera_entry_value') return;
    const editor = cameraEditor(tab);
    const row = editor.rows[editor.source][editor.open];
    if (!row) return;
    if (editor.source === 'r') {
      cameraChoosePlace(tab, row, target.value);
    } else {
      const entry = target.value ? entityPickerEntryFor(target, target.value) : null;
      row.value = target.value;
      row.title = '';
      row.icon = '';
      row.name = entry?.name || '';
      row.line = target.value;
    }
    renderCameraEntries(tab);
    cameraEntriesChanged(tab);
  });

  // The rain radar's emergency address (System page): saved as it changes.
  document.addEventListener('change', event => {
    const input = event.target;
    if (input?.id !== 'radar_source') return;
    const value = String(input.value || '').trim();
    if (value && !cameraHttpsAddress(value)) {
      input.setAttribute('aria-invalid', 'true');
      return;
    }
    input.removeAttribute('aria-invalid');
    fetch('/api/radar_source', { method: 'POST', body: new URLSearchParams({ url: value }) })
      .then(res => {
        if (!res.ok) input.setAttribute('aria-invalid', 'true');
      })
      .catch(() => input.setAttribute('aria-invalid', 'true'));
  });


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
    const rootStyle = getComputedStyle(document.documentElement);
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
    const font = f => ({
      size: Math.max(6, Math.round(f.px * scale)),
      line: f.line * scale,
      shift: (f.line / 2 - f.base - 0.364 * f.px) * scale
    });
    const fontCss = f => 'font-size:' + f.size + 'px;line-height:' + f.line.toFixed(2) + 'px;';
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
    if (state?.cover && /^(https?:|data:image\/)/i.test(state.cover)) {
      const side = (plain.side * scale).toFixed(2) + 'px';
      html += '<div class="media-preview-cover" hidden style="' + at(L.coverLeft, plain.top) + 'width:' + side +
        ';height:' + side + ';border-radius:' + (L.coverRadius * scale).toFixed(2) + 'px">' +
        '<img src="' + escapeHtml(state.cover) + '" alt="" referrerpolicy="no-referrer" ' +
        'onload="mediaPreviewCoverLoaded(this)" onerror="this.parentElement.remove()"></div>';
    }
    html += '<div class="media-preview-title' + (titleText ? '' : ' media-preview-state') + '" style="' +
      textStyle(plain, plain.titleY, titleFont) + '" data-cover-style="' +
      escapeHtml(textStyle(covered, covered.titleY, titleFont)) + '">' + escapeHtml(mainText) + '</div>';
    if (subtitle) {
      html += '<div class="media-preview-subtitle" style="' + textStyle(plain, plain.subtitleY, subtitleFont) +
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
      '.media-preview-control)').forEach(node => node.remove());
    const handles = el.querySelector(':scope > .tile-resize-handle');
    if (handles) handles.insertAdjacentHTML('beforebegin', html);
    else el.insertAdjacentHTML('beforeend', html);
  }

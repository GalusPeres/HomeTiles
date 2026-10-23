
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
    const layout = document.getElementById(prefix + '_media_layout');
    if (layout) layout.value = Number(data.sensor_display_mode) === 1 ? '1' : '0';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
    maybeFillTitleFromMedia(tab);
    updateMediaValuePreview(tab);
  }

  function saveMediaFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    const entity = document.getElementById(prefix + '_media_entity')?.value || '';
    formData.append('media_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('media_layout', document.getElementById(prefix + '_media_layout')?.value === '1' ? '1' : '0');
  }

  function resetMediaFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const el = document.getElementById(prefix + '_media_entity');
    if (el) el.value = '';
    const layout = document.getElementById(prefix + '_media_layout');
    if (layout) layout.value = '0';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
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

  // media/state_icon.h: the player's icon in its state form (speaker-play,
  // speaker-pause, speaker-off ...) where MDI has one, like the device's
  // tile; the page's MDI font tells which forms exist (found ones are kept;
  // a function property, so a preview drawn before this file ran works).
  function mediaPreviewIcon(state, iconName) {
    const suffix = {playing: '-play', paused: '-pause', off: '-off', standby: '-off'}[
      String(state?.state || '').trim().toLowerCase()];
    if (!iconName || !suffix) return iconName;
    const mediaKnownIcons = mediaPreviewIcon.known || (mediaPreviewIcon.known = new Set());
    const variant = iconName + suffix;
    if (!mediaKnownIcons.has(variant)) {
      const probe = document.createElement('i');
      probe.className = 'mdi mdi-' + variant;
      probe.style.cssText = 'position:absolute;visibility:hidden';
      document.body.appendChild(probe);
      const glyph = getComputedStyle(probe, '::before').content;
      probe.remove();
      if (glyph && glyph !== 'none' && glyph !== 'normal' && glyph !== '""') mediaKnownIcons.add(variant);
    }
    return mediaKnownIcons.has(variant) ? variant : iconName;
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
  // The playing bars stand over the title in the same centred block.
  function mediaPreviewTextLayout(L, width, height, large, coverVisible, hasSubtitle, hasBars = false) {
    const top = large ? L.coverTop : L.coverTopSmall;
    const footer = L.button - L.buttonBottom + L.footerGap;
    const side = Math.max(1, Math.min(L.maxCover, Math.trunc(width * 42 / 100), height - top - footer));
    const textX = coverVisible ? side + L.textAfterCover : L.textLeft;
    const titleHeight = (large ? L.title : L.titleSmall).line;
    const gap = hasSubtitle ? L.subtitleGap : 0;
    const barsHeight = hasBars ? L.barsHeight + L.barsGap : 0;
    const block = barsHeight + titleHeight + gap + (hasSubtitle ? L.subtitle.line : 0);
    const center = coverVisible ? top + Math.trunc(side / 2) : Math.trunc((top + height - footer) / 2);
    const barsY = Math.max(top, Math.min(center - Math.trunc(block / 2), height - footer - block));
    const titleY = barsY + barsHeight;
    return {side, top, textX, textWidth: Math.max(1, width - textX - L.textRight), barsY, titleY,
            subtitleY: titleY + titleHeight + gap};
  }

  // "From cover" (tile_icon_source.cpp apply_cover): the icon takes the color
  // the panel sampled from the cover it shows (a former cover tile tint too,
  // tile_icon_colors::cover_colors_icon); a rule's icon color wins. "Fill"
  // then follows the icon (applyIconDiscTint), the cover never tints the
  // tile itself (user 2026-10-11).
  function applyMediaCoverTint(el, record, coverColor) {
    mediaPreviewBarsColor(el);
    if (!el || !coverColor || typeof parseIconColorRecord !== 'function') return;
    const parsed = parseIconColorRecord(record || '');
    const cover = parsed.cover || {icon: false, tile: 0};
    const layer = typeof iconColorRecordSource === 'function' ? iconColorRecordSource(record || '') : null;
    const ruleIcon = layer && layer.enabled && typeof iconColorRuleColorsIcon === 'function' && iconColorRuleColorsIcon(layer);
    if ((cover.icon || cover.tile > 0) && !ruleIcon) {
      const icon = el.querySelector(':scope > .tile-icon');
      if (icon) icon.style.color = coverColor;
    }
    mediaPreviewBarsColor(el);
  }

  // The playing bars take the tile icon's color like on the panel (its
  // label's color): set after the preview is filled and after every tint.
  function mediaPreviewBarsColor(el) {
    const icon = el?.querySelector(':scope > .tile-icon');
    if (icon) el.style.setProperty('--media-bars', getComputedStyle(icon).color);
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
      el.insertAdjacentHTML('afterbegin',
        '<i class="mdi mdi-' + mediaPreviewIcon(state, 'television') + ' tile-icon"></i>');
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
    // The playing bars while a song plays or pauses (update_media_tile_state).
    const playerState = String(state?.state || '').trim().toLowerCase();
    const hasBars = playerState === 'playing' || playerState === 'paused';
    const plain = mediaPreviewTextLayout(L, width, height, large, false, !!subtitle, hasBars);
    const covered = mediaPreviewTextLayout(L, width, height, large, true, !!subtitle, hasBars);
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
    // The bars stand like a paused player's (playing_bars.h kStill); the
    // panel moves them while it plays.
    if (hasBars) {
      const barsStyle = layout => at(layout.textX, layout.barsY) + 'width:' +
        ((4 * L.barWidth + 3 * L.barGap) * scale).toFixed(2) + 'px;height:' + (L.barsHeight * scale).toFixed(2) +
        'px;gap:' + (L.barGap * scale).toFixed(2) + 'px;';
      html += '<div class="media-preview-bars" style="' + barsStyle(titleLayout) + '" data-cover-style="' +
        escapeHtml(barsStyle(covered)) + '">' + [35, 70, 50, 85].map(level => '<i style="width:' +
          (L.barWidth * scale).toFixed(2) + 'px;height:' +
          (Math.max(L.barWidth, Math.trunc(L.barsHeight * level / 100 + 0.5)) * scale).toFixed(2) +
          'px;border-radius:' + (L.barWidth / 2 * scale).toFixed(2) + 'px"></i>').join('') + '</div>';
    }

    // Previous, play/pause and next at the bottom middle; an unavailable
    // player dims them. Play is a white circle with the icon in the tile color.
    if (entity) {
      const unavailable = String(state?.state || '').trim().toLowerCase() === 'unavailable';
      const playing = String(state?.state || '').trim().toLowerCase() === 'playing';
      const buttonY = height - L.button + L.buttonBottom;
      const iconTop = (Math.trunc((L.button - L.iconLine) / 2) + L.iconEmDy) * scale;
      for (const [offset, icon, primary] of [[-L.buttonSide, 'skip-previous', false],
        [0, playing ? 'pause' : 'play', true], [L.buttonSide, 'skip-next', false]]) {
        const x = Math.trunc((width - L.button) / 2) + offset;
        html += '<div class="media-preview-control' + (primary ? ' media-preview-play' : '') +
          (unavailable ? ' media-preview-disabled' : '') + '" style="' + at(x, buttonY) +
          'width:' + (L.button * scale).toFixed(2) + 'px;height:' + (L.button * scale).toFixed(2) + 'px;' +
          '">' +
          '<i class="mdi mdi-' + icon + '" style="top:' + iconTop.toFixed(2) + 'px"></i></div>';
      }
    }
    el.querySelectorAll(':scope > :is(.media-preview-cover, .media-preview-title, .media-preview-subtitle, ' +
      '.media-preview-bars, .media-preview-control)').forEach(node => { if (node !== shownCover) node.remove(); });
    if (shownCover) shownCover.setAttribute('style', coverStyle);
    const handles = el.querySelector(':scope > .tile-resize-handle');
    if (handles) handles.insertAdjacentHTML('beforebegin', html);
    else el.insertAdjacentHTML('beforeend', html);
    mediaPreviewBarsColor(el);
  }

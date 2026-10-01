
  function parseCoverPreviewPayload(value) {
    const out = {
      state: 'unknown',
      position: null,
      tiltPosition: null,
      deviceClass: '',
      available: true,
      // A state was reported (the device's CoverState::valid).
      reported: false,
      supportedFeatures: null
    };
    if (value === undefined || value === null) return out;
    const text = String(value).trim();
    if (!text.length) return out;
    out.reported = true;
    if (!text.startsWith('{')) {
      out.state = text.toLowerCase();
      out.available = out.state !== 'unavailable';
      return out;
    }
    try {
      const obj = JSON.parse(text);
      if (!obj || typeof obj !== 'object') return out;
      const attrs = obj.attributes && typeof obj.attributes === 'object'
        ? obj.attributes : obj;
      out.state = String(obj.state ?? attrs.state ?? 'unknown').toLowerCase();
      out.available = obj.available !== undefined
        ? !!obj.available : out.state !== 'unavailable';
      const position = obj.current_position ?? attrs.current_position;
      if (position !== undefined && position !== null &&
          Number.isFinite(Number(position))) {
        out.position = Math.max(0, Math.min(100, Math.round(Number(position))));
      }
      const tilt = obj.current_tilt_position ?? attrs.current_tilt_position;
      if (tilt !== undefined && tilt !== null && Number.isFinite(Number(tilt))) {
        out.tiltPosition = Math.max(0, Math.min(100, Math.round(Number(tilt))));
      }
      out.deviceClass = String(obj.device_class ?? attrs.device_class ?? '').toLowerCase();
      const features = obj.supported_features ?? attrs.supported_features;
      if (features !== undefined && features !== null && Number.isFinite(Number(features))) {
        out.supportedFeatures = Math.max(0, Math.min(255, Math.round(Number(features))));
      }
    } catch (e) {}
    return out;
  }

  function coverPreviewIcon(state, fallback = '') {
    if (fallback) return fallback;
    const deviceClass = state?.deviceClass || '';
    const value = String(state?.state || '').toLowerCase();
    const resolve = (defaultIcon, closedIcon, closingIcon, openingIcon) => {
      if (value === 'closed' && closedIcon) return closedIcon;
      if (value === 'closing' && closingIcon) return closingIcon;
      if (value === 'opening' && openingIcon) return openingIcon;
      return defaultIcon;
    };
    if (deviceClass === 'blind') {
      return resolve('blinds-horizontal', 'blinds-horizontal-closed',
        'arrow-down-box', 'arrow-up-box');
    }
    if (deviceClass === 'curtain') {
      return resolve('curtains', 'curtains-closed',
        'arrow-collapse-horizontal', 'arrow-split-vertical');
    }
    if (deviceClass === 'damper') {
      return resolve('circle', 'circle-slice-8');
    }
    if (deviceClass === 'door') return resolve('door-open', 'door-closed');
    if (deviceClass === 'garage') {
      return resolve('garage-open', 'garage', 'arrow-down-box', 'arrow-up-box');
    }
    if (deviceClass === 'gate') {
      return resolve('gate-open', 'gate', 'arrow-right', 'arrow-right');
    }
    if (deviceClass === 'shade') {
      return resolve('roller-shade', 'roller-shade-closed',
        'arrow-down-box', 'arrow-up-box');
    }
    if (deviceClass === 'shutter') {
      return resolve('window-shutter-open', 'window-shutter',
        'arrow-down-box', 'arrow-up-box');
    }
    if (deviceClass === 'window') {
      return resolve('window-open', 'window-closed',
        'arrow-down-box', 'arrow-up-box');
    }
    return resolve('window-open', 'window-closed',
      'arrow-down-box', 'arrow-up-box');
  }

  // Closed keeps the active color like the tile (cover_icon_active).
  function coverPreviewColor(state) {
    const value = String(state?.state || 'unknown').toLowerCase();
    if (state?.available === false || value === 'unknown' || value === 'unavailable') {
      return '#9e9e9e';
    }
    return '#926bc7';
  }

  // Home Assistant's --state-cover-active-color: the position fill
  // (cover renderer kCoverActive).
  const COVER_PREVIEW_ACTIVE = '#926BC7';

  // A full tile of a Cover with a position (or not reported yet) shows the
  // header and the position bar (cover renderer show_view); without
  // supported_features the device assumes open, close and stop, plus the
  // position when one is reported. Bit 4 is SET_POSITION.
  function coverPreviewPositionable(state) {
    if (!state || !state.reported) return true;
    const features = state.supportedFeatures ?? (11 | (state.position !== null ? 4 : 0));
    return (features & 4) !== 0;
  }

  // cover_state_line(): "Open · 58 %".
  function coverPreviewStateLine(state) {
    if (!state || !state.reported) return '--';
    const text = coverPreviewStateText(state);
    return state.available !== false && state.position !== null ? text + ' \u00B7 ' + state.position + ' %' : text;
  }

  // Markup after the icon and title: half height and full tiles of a
  // positionable Cover show the state line beside the disc, full tiles also
  // the position bar (the Switch preview's bar); other Covers keep the
  // centered state and position.
  function coverPreviewExtraHtml(state, halfHeight) {
    if (!halfHeight && !coverPreviewPositionable(state)) {
      const value = state?.position !== null && state?.position !== undefined
        ? String(state.position) + '%' : '--%';
      return '<div class="tile-value tile-cover-value">' + escapeHtml(coverPreviewStateText(state)) +
        '<br>' + escapeHtml(value) + '</div>';
    }
    let html = '<div class="tile-value tile-switch-state">' + escapeHtml(coverPreviewStateLine(state)) + '</div>';
    if (!halfHeight) {
      html += '<div class="tile-switch" data-bar="dimmer">' +
        '<div class="tile-switch-fill"><span class="tile-switch-handle"></span></div></div>';
    }
    return html;
  }

  // The header layout class and the position fill, drawn like the Switch
  // dimmer (drawSwitchPreviewFill) in the cover color with the handle in the
  // tile color.
  function applyCoverPreview(tileElem, state, halfHeight) {
    if (!tileElem) return;
    tileElem.classList.toggle('switch-bar', !halfHeight && coverPreviewPositionable(state));
    const bar = tileElem.querySelector('.tile-switch');
    if (!bar) return;
    const available = state?.available !== false && !!state?.reported;
    // The closed part like Home Assistant's cover position feature (and the
    // popup's shutter): 75 % open fills a quarter; fully open keeps the
    // smallest piece with the handle (cover renderer cover_fill_level).
    const level = available && state.position !== null ? Math.max(1, 100 - state.position) : 0;
    bar.dataset.bar = 'dimmer';
    bar.classList.toggle('is-unavailable', !available);
    bar.style.setProperty('--switch-accent', COVER_PREVIEW_ACTIVE);
    bar.style.setProperty('--switch-card', getComputedStyle(tileElem).backgroundColor);
    bar.__switchFill = {kind: 'dimmer', level};
    drawSwitchPreviewFill(bar);
    if (switchBarObserver && !bar.__switchObserved) {
      bar.__switchObserved = true;
      switchBarObserver.observe(bar);
    }
  }

  function coverPreviewStateText(state) {
    if (state?.available === false) return COVER_I18N.unavailable;
    const labels = {
      open: COVER_I18N.open,
      opening: COVER_I18N.opening,
      closed: COVER_I18N.closed,
      closing: COVER_I18N.closing,
      unavailable: COVER_I18N.unavailable,
      unknown: COVER_I18N.unknown
    };
    return labels[String(state?.state || 'unknown').toLowerCase()] ||
      COVER_I18N.unknown;
  }

  function loadCoverFields(tab, data) {
    loadIconColorFields(tab, data);
    const entity = document.getElementById(tab + '_cover_entity');
    const configured = data.sensor_entity || data.cover_entity || '';
    if (entity) {
      if (configured) {
        entity.dataset.configuredValue = configured;
        if (!Array.from(entity.options).some(option => option.value === configured)) {
          const option = document.createElement('option');
          option.value = configured;
          option.textContent = configured;
          entity.appendChild(option);
        }
      } else {
        delete entity.dataset.configuredValue;
      }
      entity.value = configured;
    }
    // State size like the Switch tile (Tile::sensor_value_font).
    const font = document.getElementById(tab + '_cover_value_font');
    if (font) font.value = switchValueFont(data.sensor_value_font);
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
    maybeFillTitleFromEntity(tab, '_cover_entity');
  }

  function saveCoverFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const entity = document.getElementById(tab + '_cover_entity')?.value || '';
    formData.append('cover_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('sensor_value_font', switchValueFont(document.getElementById(tab + '_cover_value_font')?.value));
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetCoverFields(tab) {
    resetIconColorFields(tab);
    const entity = document.getElementById(tab + '_cover_entity');
    if (entity) {
      entity.value = '';
      delete entity.dataset.configuredValue;
    }
    const font = document.getElementById(tab + '_cover_value_font');
    if (font) font.value = '0';
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) popup.value = '1';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
  }


  function parseCoverPreviewPayload(value) {
    const out = {
      state: 'unknown',
      position: null,
      tiltPosition: null,
      deviceClass: '',
      available: true
    };
    if (value === undefined || value === null) return out;
    const text = String(value).trim();
    if (!text.length) return out;
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

  function coverPreviewColor(state) {
    const value = String(state?.state || 'unknown').toLowerCase();
    if (state?.available === false ||
        value === 'closed' || value === 'unknown' || value === 'unavailable') {
      return '#9e9e9e';
    }
    return '#926bc7';
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
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) {
      popup.value = data.popup_open_mode !== undefined
        ? String(data.popup_open_mode) : '1';
    }
    maybeFillTitleFromEntity(tab, '_cover_entity');
  }

  function saveCoverFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const entity = document.getElementById(tab + '_cover_entity')?.value || '';
    formData.append('cover_entity', entity);
    formData.append('sensor_entity', entity);
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
    const popup = document.getElementById(tab + '_cover_popup_open_mode');
    if (popup) popup.value = '1';
  }

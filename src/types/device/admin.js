  // Lock (24), Alarm panel (25) and Fan (26): the Switch tile's header with
  // a control bar (types/device/device_tile.cpp). The preview mirrors the
  // device: the state colors and icons of device_visual.h, the bar of
  // bar_model() and the words of DEVICE_I18N (types/device/web.cpp).
  const DEVICE_COLORS = {green: '#4CAF50', red: '#F44336', orange: '#FF9800', grey: '#9E9E9E', cyan: '#00BCD4'};
  // Home Assistant's alarm modes: state, feature bit, icon.
  const DEVICE_ALARM_MODES = [
    ['armed_home', 1, 'home'], ['armed_away', 2, 'lock'], ['armed_night', 4, 'moon-waning-crescent'],
    ['armed_vacation', 32, 'airplane'], ['armed_custom_bypass', 16, 'shield']
  ];

  function devicePreviewKind(type) {
    const value = String(type);
    return value === '24' ? 'lock' : (value === '25' ? 'alarm' : (value === '26' ? 'fan' : ''));
  }

  // The retained detail state (/api/sensor_values device_values).
  function deviceDetailPayload(entity, meta = sensorMetaCache) {
    return entity ? (meta?.deviceValues?.[entity] ?? '') : '';
  }

  // device_detail::parse(): a missing state stays apart from a known one.
  function parseDevicePreviewPayload(value) {
    const out = {valid: false, available: false, state: '', features: 0, percentage: null,
      step: null, preset: ''};
    const text = value === undefined || value === null ? '' : String(value).trim();
    if (!text.startsWith('{')) return out;
    try {
      const obj = JSON.parse(text);
      if (!obj || typeof obj !== 'object') return out;
      out.valid = true;
      out.state = typeof obj.state === 'string' ? obj.state.toLowerCase() : '';
      out.available = !!out.state && out.state !== 'unavailable';
      out.features = Number.isFinite(Number(obj.supported_features)) ? Number(obj.supported_features) : 0;
      if (typeof obj.percentage === 'number' && Number.isFinite(obj.percentage)) {
        out.percentage = Math.max(0, Math.min(100, Math.round(obj.percentage)));
      }
      if (typeof obj.percentage_step === 'number' && obj.percentage_step > 0 && obj.percentage_step <= 100) {
        out.step = obj.percentage_step;
      }
      out.preset = typeof obj.preset_mode === 'string' ? obj.preset_mode : '';
    } catch (e) {}
    return out;
  }

  function deviceSpeedCount(d) {
    if (!d.step) return 100;
    return Math.max(1, Math.min(100, Math.round(100 / d.step)));
  }

  function deviceLevelText(count, percentage) {
    const i18n = typeof DEVICE_I18N === 'object' ? DEVICE_I18N.fan : {};
    if (count < 100) {
      const speed = Math.max(1, Math.min(count, Math.round(percentage * count / 100)));
      return String(i18n.speed || 'Speed %u').replace('%u', String(speed));
    }
    return percentage + ' %';
  }

  // device_visual::visual(): label, icon and state color.
  function devicePreviewVisual(kind, d) {
    const words = typeof DEVICE_I18N === 'object' ? (DEVICE_I18N[kind] || {}) : {};
    const out = {label: '--', icon: kind === 'lock' ? 'lock' : (kind === 'alarm' ? 'shield' : 'fan-off'),
      color: DEVICE_COLORS.grey, waiting: false};
    if (!d.valid) return out;
    const state = d.available ? d.state : 'unavailable';
    if (kind === 'lock') {
      out.label = words[state] || words.unknown || state;
      if (!d.available) return out;
      if (state === 'locked') out.color = DEVICE_COLORS.green;
      else if (state === 'unlocked' || state === 'open') {
        out.icon = 'lock-open-variant';
        out.color = DEVICE_COLORS.red;
      } else if (['locking', 'unlocking', 'opening'].includes(state)) {
        out.icon = 'lock-clock';
        out.color = DEVICE_COLORS.orange;
        out.waiting = true;
      } else if (state === 'jammed') {
        out.icon = 'lock-alert';
        out.color = DEVICE_COLORS.red;
        out.waiting = true;
      }
      return out;
    }
    if (kind === 'alarm') {
      out.label = words[state] || words.unknown || state;
      if (!d.available) return out;
      const looks = {
        disarmed: ['shield-off', 'grey'], armed_home: ['shield-home', 'green'], armed_away: ['shield-lock', 'green'],
        armed_night: ['shield-moon', 'green'], armed_vacation: ['shield-airplane', 'green'],
        armed_custom_bypass: ['security', 'green'], arming: ['shield', 'orange'], pending: ['shield-outline', 'orange'],
        disarming: ['shield', 'orange'], triggered: ['bell-ring', 'red']
      };
      const look = looks[state];
      if (look) {
        out.icon = look[0];
        out.color = DEVICE_COLORS[look[1]];
        out.waiting = look[1] === 'orange' || state === 'triggered';
      }
      return out;
    }
    if (!d.available) {
      out.label = words.unavailable || 'Unavailable';
      return out;
    }
    if (state === 'unknown') {
      out.label = words.unknown || 'Unknown';
      return out;
    }
    if (state !== 'on') {
      out.label = words.off || 'Off';
      return out;
    }
    out.icon = 'fan';
    out.color = DEVICE_COLORS.cyan;
    if (d.preset) {
      const preset = d.preset.replace(/_/g, ' ');
      out.label = preset.charAt(0).toUpperCase() + preset.slice(1);
    } else if ((d.features & 1) && d.percentage !== null) {
      out.label = deviceLevelText(deviceSpeedCount(d), d.percentage);
    } else {
      out.label = words.on || 'On';
    }
    return out;
  }

  function devicePreviewIcon(kind, d, fallback = '') {
    return fallback || devicePreviewVisual(kind, d).icon;
  }

  function devicePreviewColor(kind, d) {
    return devicePreviewVisual(kind, d).color;
  }

  // The state line beside the disc and, for full tiles, the bar.
  function devicePreviewExtraHtml(kind, d, halfHeight) {
    let html = '<div class="tile-value tile-switch-state">' + escapeHtml(devicePreviewVisual(kind, d).label) + '</div>';
    if (!halfHeight) {
      html += '<div class="tile-switch" data-bar="none">' +
        '<div class="tile-switch-fill"><span class="tile-switch-handle"></span></div>' +
        '<div class="tile-switch-knob"><i class="mdi tile-switch-symbol"></i></div>' +
        '<div class="tile-device-parts"></div></div>';
    }
    return html;
  }

  function devicePart(icon, cls) {
    return '<span class="tile-device-part' + (cls ? ' ' + cls : '') + '">' +
      (icon ? '<i class="mdi mdi-' + escapeHtml(icon) + '"></i>' : '') + '</span>';
  }

  // bar_model(): the bar the device draws for this state.
  function applyDevicePreview(tileElem, kind, d, halfHeight) {
    if (!tileElem) return;
    tileElem.classList.toggle('switch-bar', !halfHeight);
    const bar = tileElem.querySelector('.tile-switch');
    if (!bar) return;
    const visual = devicePreviewVisual(kind, d);
    const iconEl = tileElem.querySelector('.tile-icon');
    const accent = iconEl ? getComputedStyle(iconEl).color : visual.color;
    const card = getComputedStyle(tileElem).backgroundColor;
    bar.style.setProperty('--switch-accent', accent);
    bar.style.setProperty('--switch-card', card);
    const control = cssColorChannels(getComputedStyle(bar).backgroundColor);
    if (control) {
      const base = toneOklch(control);
      bar.style.setProperty('--switch-thumb-off', toneHex(toneRgb(base.L + 0.06, base.C, base.h)));
      bar.style.setProperty('--device-button', toneHex(toneRgb(base.L + 0.04, base.C, base.h)));
    }
    bar.classList.toggle('is-unavailable', !d.valid || !d.available);
    bar.classList.remove('is-on', 'knob-accent');
    const parts = bar.querySelector('.tile-device-parts');
    const symbol = bar.querySelector('.tile-switch-symbol');
    let barKind = 'none';
    let onResize = null;
    let partsHtml = '';
    let gapped = false;
    let level = 0;
    if (kind === 'lock') {
      const s = d.state;
      if (!['locked', 'locking', 'unlocked', 'unlocking', 'open', 'opening'].includes(s)) {
        barKind = 'parts';
        gapped = true;
        partsHtml = devicePart('lock-open-variant', 'button') + devicePart('lock', 'button');
      } else {
        barKind = 'toggle';
        bar.classList.toggle('is-on', s === 'locked' || s === 'locking');
        bar.classList.add('knob-accent');
        if (symbol) symbol.className = 'mdi mdi-' + visual.icon + ' tile-switch-symbol';
      }
    } else if (kind === 'alarm') {
      if (['arming', 'pending', 'triggered'].includes(d.state)) {
        barKind = 'parts';
        partsHtml = devicePart('shield-off', 'button');
      } else {
        barKind = 'parts';
        // alarm_slots(): Disarm first, then the supported modes in reverse.
        const iconWidth = parseFloat(getComputedStyle(tileElem).getPropertyValue('--icon-size')) || 24;
        const fitNow = () => Math.max(2, Math.min(6, Math.floor((bar.clientWidth || 60) / iconWidth)));
        const fit = fitNow();
        // The device counts the slots for the tile's size; here they follow
        // the bar's width, also when the tile is resized or laid out later
        // (a 2x1 tile kept the three slots of 1x1 and lit none).
        onResize = () => {
          if (fitNow() !== fit) applyDevicePreview(tileElem, kind, d, halfHeight);
        };
        const modes = DEVICE_ALARM_MODES.filter(mode => d.features & mode[1]).slice(0, fit - 1);
        const slots = [['disarmed', 0, 'shield-off'], ...modes.reverse()];
        partsHtml = slots.map(slot => devicePart(slot[2], slot[0] === d.state ? 'lit' : '')).join('');
      }
    } else {
      const on = d.state === 'on';
      const percentage = on ? (d.percentage ?? 100) : 0;
      const count = deviceSpeedCount(d);
      if (!(d.features & 1)) {
        barKind = 'toggle';
        bar.classList.toggle('is-on', on);
        if (symbol) symbol.className = 'mdi mdi-' + (on ? 'fan' : 'fan-off') + ' tile-switch-symbol';
      } else if (count >= 2 && count <= 4) {
        barKind = 'parts';
        gapped = true;
        const speed = percentage > 0 ? Math.max(1, Math.min(count, Math.round(percentage * count / 100))) : 0;
        for (let i = 0; i < count; ++i) partsHtml += devicePart('', i < speed ? 'lit' : 'button');
      } else {
        barKind = 'dimmer';
        level = on ? Math.max(1, percentage) : 0;
      }
    }
    bar.dataset.bar = barKind;
    if (parts) {
      parts.classList.toggle('gapped', gapped);
      parts.innerHTML = partsHtml;
    }
    bar.__switchFill = {kind: barKind === 'dimmer' ? 'dimmer' : 'none', level};
    bar.__onResize = onResize;
    drawSwitchPreviewFill(bar);
    if (switchBarObserver && !bar.__switchObserved) {
      bar.__switchObserved = true;
      switchBarObserver.observe(bar);
    }
  }

  function loadDeviceFields(tab, data, prefix) {
    loadIconColorFields(tab, data);
    const entity = document.getElementById(tab + '_' + prefix + '_entity');
    const configured = data.sensor_entity || data[prefix + '_entity'] || '';
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
    const font = document.getElementById(tab + '_' + prefix + '_value_font');
    if (font) font.value = switchValueFont(data.sensor_value_font);
    const popup = document.getElementById(tab + '_' + prefix + '_popup_open_mode');
    if (popup) popup.value = data.popup_open_mode !== undefined ? String(data.popup_open_mode) : '1';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
    maybeFillTitleFromEntity(tab, '_' + prefix + '_entity');
  }

  function saveDeviceFields(tab, formData, prefix) {
    saveIconColorFields(tab, formData);
    const entity = document.getElementById(tab + '_' + prefix + '_entity')?.value || '';
    formData.append(prefix + '_entity', entity);
    formData.append('sensor_entity', entity);
    formData.append('sensor_value_font',
      switchValueFont(document.getElementById(tab + '_' + prefix + '_value_font')?.value));
    const popup = document.getElementById(tab + '_' + prefix + '_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetDeviceFields(tab, prefix) {
    resetIconColorFields(tab);
    const entity = document.getElementById(tab + '_' + prefix + '_entity');
    if (entity) {
      entity.value = '';
      delete entity.dataset.configuredValue;
    }
    const font = document.getElementById(tab + '_' + prefix + '_value_font');
    if (font) font.value = '0';
    const popup = document.getElementById(tab + '_' + prefix + '_popup_open_mode');
    if (popup) popup.value = '1';
    if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
  }

  // The registry calls these by name (TileTypeDescriptor js_load/js_save/js_reset).
  function loadLockFields(tab, data) { loadDeviceFields(tab, data, 'lock'); }
  function saveLockFields(tab, formData) { saveDeviceFields(tab, formData, 'lock'); }
  function resetLockFields(tab) { resetDeviceFields(tab, 'lock'); }
  function loadAlarmFields(tab, data) { loadDeviceFields(tab, data, 'alarm'); }
  function saveAlarmFields(tab, formData) { saveDeviceFields(tab, formData, 'alarm'); }
  function resetAlarmFields(tab) { resetDeviceFields(tab, 'alarm'); }
  function loadFanFields(tab, data) { loadDeviceFields(tab, data, 'fan'); }
  function saveFanFields(tab, formData) { saveDeviceFields(tab, formData, 'fan'); }
  function resetFanFields(tab) { resetDeviceFields(tab, 'fan'); }

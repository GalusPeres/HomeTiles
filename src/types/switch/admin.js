
function maybeFillTitleFromSwitch(tab) {
    maybeFillTitleFromEntity(tab, '_switch_entity');
  }

  const SWITCH_ICON_ON = '#FFD54F';
  const SWITCH_ICON_OFF = '#B0B0B0';
  // Layouts (src/types/switch/layout.h): 0 icon button, 1 switch, 2 dimmer,
  // 3 automatic. New tiles start with Automatic.
  const SWITCH_LAYOUT_NEW_TILE = '3';
  // Home Assistant color modes that dim (every mode except onoff).
  // Mirrors parse_switch_payload() (tile_renderer.cpp).
  const SWITCH_DIMMING_MODES = ['brightness', 'color_temp', 'hs', 'rgb', 'xy', 'rgbw', 'rgbww'];

  // The Sensor value size choices (Tile::sensor_value_font 0..5). One row
  // high the state shows the half-height sizes (compact_sensor_layout::
  // value_step), from 1.5 rows the full-size ones; the editor offers the
  // fitting ones (syncCompactValueFontOptions).
  function switchValueFont(value) {
    const v = String(value ?? '0');
    return ['1', '2', '3', '4', '5'].includes(v) ? v : '0';
  }

  // switch_layout::sensor_look: from 1.5 rows a header layout looks like a
  // Sensor tile with the bar below.
  function switchSensorLook(style, spanH) {
    return switchLayoutValue(style) !== 0 && Number(spanH) > 1;
  }

  // The segmented choices of the Switch fields (like Tile color). The hidden
  // select of each keeps the value; a button sets it and fires its change
  // event, so the existing preview, draft and autosave bindings run.
  const SWITCH_CHOICE_FIELDS = ['switch_style', 'switch_value_font', 'switch_popup_open_mode'];

  function syncSwitchChoices(tab) {
    for (const field of SWITCH_CHOICE_FIELDS) {
      const select = document.getElementById(tab + '_' + field);
      const group = document.getElementById(tab + '_' + field + '_choices');
      if (!select || !group?.querySelectorAll) continue;
      const options = select.options ? Array.from(select.options) : [];
      for (const button of group.querySelectorAll('button[data-value]')) {
        button.classList.toggle('active', button.dataset.value === String(select.value));
        // Value sizes this tile size does not offer stay hidden.
        const option = options.find(o => o.value === button.dataset.value);
        button.classList.toggle('hidden', !!option?.hidden);
      }
    }
  }

  function setSwitchChoice(tab, field, value) {
    const select = document.getElementById(tab + '_' + field);
    if (!select) return;
    if (String(select.value) !== String(value)) {
      select.value = String(value);
      if (typeof select.dispatchEvent === 'function') select.dispatchEvent(new Event('change', {bubbles: true}));
    }
    syncSwitchChoices(tab);
  }

  function switchLayoutValue(value) {
    const layout = Number(value);
    return [0, 1, 2, 3].includes(layout) ? layout : 0;
  }

  // Header layouts (Switch, Dimmer, Automatic) and every half-height Switch
  // tile use the Sensor header; only a full-size icon button does not.
  function switchUsesHeader(style, halfHeight) {
    return !!halfHeight || switchLayoutValue(style) !== 0;
  }

  // switch_layout::bar_for(): no bar at half height or for the icon button.
  function switchBarKind(style, halfHeight, dimmable) {
    const layout = switchLayoutValue(style);
    if (halfHeight || layout === 0) return 'none';
    if (layout === 1) return 'toggle';
    return dimmable ? 'dimmer' : 'toggle';
  }

  // Markup after the icon and title of a header layout: the state line and,
  // at full height, the control bar.
  function switchPreviewExtraHtml(style, halfHeight) {
    if (!switchUsesHeader(style, halfHeight)) return '';
    let html = '<div class="tile-value tile-switch-state">--</div>';
    if (!halfHeight) {
      html += '<div class="tile-switch" data-bar="none">' +
        '<div class="tile-switch-fill"><span class="tile-switch-handle"></span></div>' +
        '<div class="tile-switch-knob"><i class="mdi mdi-power tile-switch-symbol"></i></div>' +
        '</div>';
    }
    return html;
  }

  // The tile classes and layout of a Switch preview (live and cached grid).
  function applySwitchPreviewLayout(tileElem, style, halfHeight) {
    if (!tileElem) return;
    tileElem.dataset.switchLayout = String(switchLayoutValue(style));
    tileElem.classList.toggle('switch-bar', switchUsesHeader(style, halfHeight) && !halfHeight);
  }

  // switch_layout::Dimmer: the fill stays inside the bar's shape; its end
  // has a quarter of the bar height as rounding, growing into the bar's
  // radius only near the full end. The smallest piece is the bar radius plus
  // that rounding wide (tangential, no edge) with the handle in its middle.
  function switchDimmerGeometry(width, height, radius) {
    const endRadius = Math.floor(height / 4);
    const minFill = Math.min(radius + endRadius, width);
    const margin = Math.floor(minFill / 2);
    const low = minFill - margin;
    const high = width - margin;
    return {
      margin,
      minFill,
      endRadius,
      handleWidth: Math.max(3, Math.floor(height * 7 / 100)),
      handleHeight: Math.floor(height * 42 / 100),
      handleX(value) {
        if (value <= 1 || high <= low) return low;
        if (value >= 100) return high;
        return low + Math.floor(((value - 1) * (high - low) + 49) / 99);
      },
      fillWidth(value) {
        if (!value) return 0;
        return Math.min(width, this.handleX(value) + margin);
      },
      endRadiusFor(fill) {
        if (fill <= width - radius || endRadius >= radius) return endRadius;
        const corner = width - radius;
        for (let end = endRadius; end < radius; end++) {
          let inside = true;
          for (let x = fill - end; x <= fill && inside; x++) {
            const dxEnd = x - (fill - end);
            const endTop = end - Math.sqrt(Math.max(0, end * end - dxEnd * dxEnd));
            const dxBar = x - corner;
            const barTop = dxBar <= 0 ? 0 : radius - Math.sqrt(Math.max(0, radius * radius - dxBar * dxBar));
            if (endTop + 0.01 < barTop) inside = false;
          }
          if (inside) return end;
        }
        return radius;
      }
    };
  }

  function parseOnOff(text) {
    const lower = String(text || '').trim().toLowerCase();
    if (['on', 'true', '1', 'yes'].includes(lower)) return true;
    if (['off', 'false', '0', 'no'].includes(lower)) return false;
    return null;
  }

  function parseHexColor(text) {
    let t = String(text || '').trim();
    if (t.startsWith('#')) t = t.substring(1);
    if (t.startsWith('0x') || t.startsWith('0X')) t = t.substring(2);
    if (t.length !== 6) return null;
    if (!/^[0-9a-fA-F]{6}$/.test(t)) return null;
    return '#' + t.toLowerCase();
  }

  function rgbToHexColor(r, g, b) {
    const clamp = (v) => Math.max(0, Math.min(255, v));
    const rr = clamp(r).toString(16).padStart(2, '0');
    const gg = clamp(g).toString(16).padStart(2, '0');
    const bb = clamp(b).toString(16).padStart(2, '0');
    return '#' + rr + gg + bb;
  }

  function parseRgbList(list) {
    if (Array.isArray(list) && list.length >= 3) {
      return rgbToHexColor(Number(list[0]), Number(list[1]), Number(list[2]));
    }
    const parts = String(list || '').split(',');
    if (parts.length < 3) return null;
    return rgbToHexColor(parseInt(parts[0], 10), parseInt(parts[1], 10), parseInt(parts[2], 10));
  }

  function hsToRgb(h, s) {
    const hh = ((h % 360) + 360) % 360;
    const sat = Math.max(0, Math.min(100, s)) / 100;
    const c = sat;
    const x = c * (1 - Math.abs((hh / 60) % 2 - 1));
    const m = 1 - c;
    let r1 = 0, g1 = 0, b1 = 0;
    if (hh < 60) { r1 = c; g1 = x; b1 = 0; }
    else if (hh < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (hh < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (hh < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (hh < 300) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }
    return rgbToHexColor(Math.round((r1 + m) * 255), Math.round((g1 + m) * 255), Math.round((b1 + m) * 255));
  }

  function parseSwitchPayload(value) {
    const out = {
      available: true,
      hasState: false,
      isOn: false,
      unknown: false,
      hasColor: false,
      color: null,
      hasBrightness: false,
      brightness: 0,
      supportsBrightness: false
    };
    if (value === undefined || value === null) return out;
    const text = String(value).trim();
    if (!text.length) return out;

    if (text.startsWith('{')) {
      try {
        const obj = JSON.parse(text);
        if (obj && typeof obj === 'object') {
          if (obj.available !== undefined) {
            out.available = obj.available !== false;
          }
          if (obj.state !== undefined) {
            const normalizedState = String(obj.state).trim().toLowerCase();
            if (normalizedState === 'unavailable') {
              out.available = false;
            } else if (normalizedState === 'unknown') {
              out.unknown = true;
            } else {
              const on = parseOnOff(obj.state);
              if (on !== null) {
                out.hasState = true;
                out.isOn = on;
              }
            }
          }
          if (obj.color) {
            const hex = parseHexColor(obj.color);
            if (hex) {
              out.hasColor = true;
              out.color = hex;
            }
          }
          if (!out.hasColor && obj.rgb_color) {
            const hex = parseRgbList(obj.rgb_color);
            if (hex) {
              out.hasColor = true;
              out.color = hex;
            }
          }
          if (!out.hasColor && obj.hs_color && Array.isArray(obj.hs_color) && obj.hs_color.length >= 2) {
            out.hasColor = true;
            out.color = hsToRgb(Number(obj.hs_color[0]), Number(obj.hs_color[1]));
          }
          // Like parse_switch_payload(): brightness_pct, else brightness 0..255.
          const pct = Number(obj.brightness_pct);
          const raw = Number(obj.brightness);
          if (obj.brightness_pct !== undefined && obj.brightness_pct !== null && Number.isFinite(pct)) {
            out.hasBrightness = true;
            out.brightness = Math.max(0, Math.min(100, Math.round(pct)));
          } else if (obj.brightness !== undefined && obj.brightness !== null && Number.isFinite(raw)) {
            out.hasBrightness = true;
            out.brightness = Math.max(0, Math.min(100, Math.round(raw / 255 * 100)));
          }
          // supported_color_modes decides; color_mode only when it is absent.
          if (Array.isArray(obj.supported_color_modes)) {
            out.supportsBrightness = obj.supported_color_modes.some(mode =>
              SWITCH_DIMMING_MODES.includes(String(mode).toLowerCase()));
          } else if (obj.color_mode) {
            out.supportsBrightness = SWITCH_DIMMING_MODES.includes(String(obj.color_mode).toLowerCase());
          }
        }
      } catch (e) {}
    }

    if (text.toLowerCase() === 'unavailable') {
      out.available = false;
    }
    if (text.toLowerCase() === 'unknown') {
      out.unknown = true;
    }

    if (!out.hasState) {
      const on = parseOnOff(text);
      if (on !== null) {
        out.hasState = true;
        out.isOn = on;
      }
    }

    if (!out.hasColor) {
      const hex = parseHexColor(text);
      if (hex) {
        out.hasColor = true;
        out.color = hex;
      } else if (text.startsWith('rgb(') && text.endsWith(')')) {
        const hexRgb = parseRgbList(text.substring(4, text.length - 1));
        if (hexRgb) {
          out.hasColor = true;
          out.color = hexRgb;
        }
      }
    }

    if (!out.hasState && out.hasColor) {
      out.hasState = true;
      out.isOn = true;
    }
    return out;
  }

  function applySwitchPreviewState(tileElem, state, entity) {
    if (!tileElem) return;
    applySwitchPreviewColors(tileElem, state);
    // The disc and the control fill follow the state color like the device.
    applyIconDiscTint(tileElem);
    applySwitchPreviewBar(tileElem, state, entity);
  }

  function applySwitchPreviewColors(tileElem, state) {
    const iconEl = tileElem.querySelector('.tile-icon');
    if (!iconEl) return;
    if (state.available === false) {
      iconEl.style.color = SWITCH_ICON_OFF;
      return;
    }
    if (!state.hasState && !state.hasColor) return;
    const isOn = state.hasState ? state.isOn : state.hasColor;
    iconEl.style.color = isOn ? (state.hasColor ? state.color : SWITCH_ICON_ON) : SWITCH_ICON_OFF;
  }

  // show_state_text(): the same words as the device, from the central
  // translations (SWITCH_I18N, types/switch/web_scripts.cpp).
  function switchPreviewStateText(state, dimmable, level, on) {
    const i18n = typeof SWITCH_I18N === 'object' ? SWITCH_I18N : {};
    if (state.available === false) return i18n.unavailable || 'Unavailable';
    if (state.unknown) return i18n.unknown || 'Unknown';
    if (!state.hasState && !state.hasBrightness) return '--';
    if (!on) return i18n.off || 'Off';
    if (dimmable) return level + ' %';
    return i18n.on || 'On';
  }

  function applySwitchPreviewBar(tileElem, state, entity) {
    // Each appears once per tile, directly in it.
    const label = tileElem.querySelector('.tile-switch-state');
    const bar = tileElem.querySelector('.tile-switch');
    if (!label && !bar) return;
    const halfHeight = tileElem.classList.contains('sensor-half');
    const style = tileElem.dataset.switchLayout || '0';
    const dimmable = String(entity || '').startsWith('light.') && !!state.supportsBrightness;
    const available = state.available !== false;
    let on = state.hasState ? !!state.isOn : (!!state.hasBrightness && state.brightness > 0);
    if (!available) on = false;
    const level = on ? (state.hasBrightness ? Math.max(1, state.brightness) : 100) : 0;
    if (label) label.textContent = switchPreviewStateText(state, dimmable, level, on);
    if (!bar) return;
    const kind = switchBarKind(style, halfHeight, dimmable);
    bar.dataset.bar = kind;
    bar.classList.toggle('is-on', on);
    bar.classList.toggle('is-unavailable', !available);
    // Accent = the color the icon shows, card = the tile, thumb off = one
    // OKLCH step above the control fill (tone_color::lifted).
    const iconEl = tileElem.querySelector('.tile-icon');
    const accent = iconEl ? getComputedStyle(iconEl).color : (state.hasColor ? state.color : SWITCH_ICON_ON);
    bar.style.setProperty('--switch-accent', accent);
    bar.style.setProperty('--switch-card', getComputedStyle(tileElem).backgroundColor);
    const control = cssColorChannels(getComputedStyle(bar).backgroundColor);
    if (control) {
      const base = toneOklch(control);
      bar.style.setProperty('--switch-thumb-off', toneHex(toneRgb(base.L + 0.06, base.C, base.h)));
    }
    const symbol = bar.querySelector('.tile-switch-symbol');
    if (symbol) symbol.className = 'mdi ' + (on ? 'mdi-power' : 'mdi-circle-outline') + ' tile-switch-symbol';
    const fill = bar.querySelector('.tile-switch-fill');
    if (!fill) return;
    const width = bar.clientWidth;
    const height = bar.clientHeight;
    const radius = Math.min(parseFloat(getComputedStyle(bar).borderTopLeftRadius) || 0, height / 2);
    const geometry = switchDimmerGeometry(width, height, radius);
    const fillWidth = kind === 'dimmer' ? geometry.fillWidth(level) : 0;
    const endRadius = geometry.endRadiusFor(fillWidth);
    // The fill element starts one end radius left of the bar, so only the
    // bar's round start shows (overflow hidden).
    fill.style.width = (fillWidth ? fillWidth + endRadius : 0) + 'px';
    bar.style.setProperty('--switch-end-radius', endRadius + 'px');
    bar.style.setProperty('--switch-handle-margin', geometry.margin + 'px');
    bar.style.setProperty('--switch-handle-w', geometry.handleWidth + 'px');
    bar.style.setProperty('--switch-handle-h', geometry.handleHeight + 'px');
  }

  function updateSwitchValuePreview(tab) {
    if (currentTileIndex === -1) return;
    const prefix = tab;
    const entitySelect = document.getElementById(prefix + '_switch_entity');
    if (!entitySelect) return;
    const entity = entitySelect.value;
    const tileElem = document.getElementById(tab + '-tile-' + currentTileIndex);
    if (!entity || !tileElem) return;
    const applyMeta = (meta) => {
      const values = (meta && meta.values) || {};
      const state = parseSwitchPayload(values[entity] ?? '');
      applySwitchPreviewState(tileElem, state, entity);
    };
    const metaPromise = isSensorMetaCacheLoaded() ? Promise.resolve(sensorMetaCache) : fetchSensorMetaCache();
    metaPromise
      .then(meta => applyMeta(meta))
      .catch(err => console.error('Switch state load failed:', err));
  }

  function loadSwitchFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_switch_entity');
    if (entityEl) entityEl.value = data.sensor_entity || data.switch_entity || '';
    const styleEl = document.getElementById(prefix + '_switch_style');
    if (styleEl) {
      styleEl.value = (data.switch_style !== undefined && data.switch_style !== null)
        ? String(switchLayoutValue(data.switch_style)) : '0';
    }
    const fontEl = document.getElementById(prefix + '_switch_value_font');
    if (fontEl) fontEl.value = switchValueFont(data.sensor_value_font);
    const popupModeEl = document.getElementById(prefix + '_switch_popup_open_mode');
    if (popupModeEl) {
      popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    }
    syncSwitchChoices(tab);
    maybeFillTitleFromSwitch(tab);
  }

  function saveSwitchFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    formData.append('switch_entity', document.getElementById(prefix + '_switch_entity')?.value || '');
    const styleEl = document.getElementById(prefix + '_switch_style');
    formData.append('switch_style', styleEl ? String(switchLayoutValue(styleEl.value)) : '0');
    formData.append('sensor_value_font', switchValueFont(document.getElementById(prefix + '_switch_value_font')?.value));
    formData.append('popup_open_mode', document.getElementById(prefix + '_switch_popup_open_mode')?.value || '1');
  }

  function resetSwitchFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_switch_entity');
    if (entityEl) entityEl.value = '';
    const styleEl = document.getElementById(prefix + '_switch_style');
    if (styleEl) styleEl.value = SWITCH_LAYOUT_NEW_TILE;
    const fontEl = document.getElementById(prefix + '_switch_value_font');
    if (fontEl) fontEl.value = '0';
    const popupModeEl = document.getElementById(prefix + '_switch_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
    syncSwitchChoices(tab);
  }

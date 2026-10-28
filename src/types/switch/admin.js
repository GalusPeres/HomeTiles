
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
  // An upright bar keeps the header beside the disc (bar_orientation.h).
  function switchSensorLook(style, spanH, upright = false) {
    return switchLayoutValue(style) !== 0 && Number(spanH) > 1 && !upright;
  }

  // The segmented choices of the Switch fields (like Tile color). The hidden
  // select of each keeps the value; a button sets it and fires its change
  // event, so the existing preview, draft and autosave bindings run.
  // The Climate tile's Layout and the Camera tile's choices use the same
  // segmented choice.
  const SWITCH_CHOICE_FIELDS = ['switch_style', 'switch_value_font', 'switch_popup_open_mode', 'climate_view', 'media_layout',
    'cover_value_font', 'lock_value_font', 'alarm_value_font', 'fan_value_font', 'camera_display', 'camera_fit',
    'camera_head', 'camera_tap', 'switch_orientation', 'fan_orientation', 'sensor_layout', 'sensor_form',
    'sensor_orientation', 'sensor_ring', 'sensor_place', 'sensor_value_font', 'sensor_popup_open_mode'];

  function syncSwitchChoices(tab) {
    for (const field of SWITCH_CHOICE_FIELDS) {
      const select = document.getElementById(tab + '_' + field);
      const group = document.getElementById(tab + '_' + field + '_choices');
      if (!select || !group?.querySelectorAll) continue;
      const options = select.options ? Array.from(select.options) : [];
      for (const button of group.querySelectorAll('button[data-value]')) {
        button.classList.toggle('active', button.dataset.value === String(select.value));
        // Value sizes this tile size does not offer stay hidden; the others
        // carry the size this panel draws (syncCompactValueFontOptions), the
        // Default button only its word so the row keeps its width.
        const option = options.find(o => o.value === button.dataset.value);
        button.classList.toggle('hidden', !!option?.hidden);
        const name = option?.value === '0' && select.dataset?.defaultLabel ? select.dataset.defaultLabel : option?.textContent;
        if (name && button.textContent !== name) button.textContent = name;
      }
    }
    // The Switch tile's Orientation row only with a bar (not the icon button).
    const style = document.getElementById(tab + '_switch_style');
    if (style) {
      const hide = switchLayoutValue(style.value) === 0;
      const label = typeof document.querySelector === 'function'
        ? document.querySelector('label[for="' + tab + '_switch_orientation"]') : null;
      label?.classList?.toggle('hidden', hide);
      document.getElementById(tab + '_switch_orientation_choices')?.classList?.toggle('hidden', hide);
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
  // `upright`: the bar stands below the header (bar_orientation.h).
  function applySwitchPreviewLayout(tileElem, style, halfHeight, upright = false) {
    if (!tileElem) return;
    tileElem.dataset.switchLayout = String(switchLayoutValue(style));
    const bar = switchUsesHeader(style, halfHeight) && !halfHeight;
    tileElem.classList.toggle('switch-bar', bar);
    tileElem.classList.toggle('switch-upright', bar && !!upright);
  }

  // switch_layout::Dimmer: the fill stays inside the bar's shape; its end
  // has a quarter of the bar height as rounding, growing into the bar's
  // radius only near the full end. The smallest piece is the bar radius plus
  // that rounding wide (tangential, no edge) with the handle in its middle.
  // `baseHeight`: the one-row bar height; a taller bar keeps its end
  // rounding and handle width, only the handle grows in height.
  function switchDimmerGeometry(width, height, radius, baseHeight = 0) {
    const reference = baseHeight > 0 ? baseHeight : height;
    const endRadius = Math.floor(reference / 4);
    const minFill = Math.min(radius + endRadius, width);
    const margin = Math.floor(minFill / 2);
    const low = minFill - margin;
    const high = width - margin;
    return {
      margin,
      minFill,
      endRadius,
      handleWidth: Math.max(3, Math.floor(reference * 7 / 100)),
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

  // Resize preview (drag-resize.js): the copy of a Switch tile at its old
  // size takes the parts of the new size like the device: no bar at half
  // height, the header and bar of the layout from one row. Before the
  // compact classes, so the tall look follows too.
  function prepareSwitchResizePreview(preview, data, layout) {
    if (!preview || !data) return;
    const half = Number(layout?.span_h) === 0.5;
    const oldState = Array.from(preview.children).find(el => el.classList.contains('tile-switch-state'));
    preview.dataset.switchStateText = oldState ? oldState.textContent : '';
    for (const el of Array.from(preview.children)) {
      if (el.classList.contains('tile-switch-state') || el.classList.contains('tile-switch')) el.remove();
    }
    preview.insertAdjacentHTML('beforeend', switchPreviewExtraHtml(data.switch_style, half));
    applySwitchPreviewLayout(preview, data.switch_style, half, Number(data.bar_orientation) === 1);
  }

  // Once the copy is in the grid (the bar measures itself): the state from
  // the entity cache, else the text the tile showed.
  function finishSwitchResizePreview(preview, data) {
    if (!preview || !data) return;
    const entity = data.sensor_entity || '';
    const values = (typeof sensorMetaCache === 'object' && sensorMetaCache?.values) || {};
    if (entity && values[entity] !== undefined) {
      applySwitchPreviewState(preview, parseSwitchPayload(values[entity]), entity);
      return;
    }
    const label = Array.from(preview.children).find(el => el.classList.contains('tile-switch-state'));
    if (label && preview.dataset.switchStateText) label.textContent = preview.dataset.switchStateText;
  }

  function applySwitchPreviewState(tileElem, state, entity) {
    if (!tileElem) return;
    // On the tile's own surface: the editor applies the state after the meta
    // fetch, when its surface scope has ended (user 2026-10-11: a filled
    // Relay showed the light theme's icon and knob after a click).
    withSurface(surfaceTheme(tileElem.dataset.surface || ''), () => {
      applySwitchPreviewColors(tileElem, state);
      // The disc and the control fill follow the state color like the device.
      applyIconDiscTint(tileElem);
      applySwitchPreviewBar(tileElem, state, entity);
    });
  }

  function applySwitchPreviewColors(tileElem, state) {
    const iconEl = tileElem.querySelector('.tile-icon');
    if (!iconEl) return;
    if (state.available === false) {
      // Unavailable: fainter than off in the light theme (icon_unavailable).
      iconEl.style.color = panelRoleHex('icon-unavailable', SWITCH_ICON_OFF);
      return;
    }
    if (!state.hasState && !state.hasColor) return;
    const isOn = state.hasState ? state.isOn : state.hasColor;
    const onColor = state.hasColor ? state.color : SWITCH_ICON_ON;
    iconEl.style.color = isOn ? (typeof toneLightShown === 'function' ? toneLightShown(onColor) : onColor)
      : panelRoleHex('icon-off', SWITCH_ICON_OFF);
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
    // A dimmable light on without a reported brightness: On without a level.
    const level = on ? (state.hasBrightness ? Math.max(1, state.brightness) : (dimmable ? 0 : 100)) : 0;
    if (label) {
      // A level's percent a step smaller after it (unitTextHtml); words as text.
      const text = switchPreviewStateText(state, dimmable && level > 0, level, on);
      const html = unitTextHtml(text);
      if (html === escapeHtml(text)) label.textContent = text;
      else label.innerHTML = html;
    }
    if (!bar) return;
    const kind = switchBarKind(style, halfHeight, dimmable);
    bar.dataset.bar = kind;
    bar.classList.toggle('is-on', on);
    bar.classList.toggle('is-unavailable', !available);
    // Accent = the color the icon shows, card = the tile, thumb off = one
    // OKLCH step above the control fill (tone_color::lifted).
    const iconEl = tileElem.querySelector('.tile-icon');
    // The light theme: the light's own color, a white light in light grey
    // (accent_color, tone_color::light_accent); marks on it by its lightness.
    withSurface(surfaceTheme(tileElem.dataset.surface || ''), () => {
      const givenHex = iconEl?.dataset.toneGiven || '';
      const shown = iconEl ? cssColorChannels(getComputedStyle(iconEl).color) : null;
      const given = /^#[0-9a-f]{6}$/i.test(givenHex) ? [1, 3, 5].map(i => parseInt(givenHex.slice(i, i + 2), 16)) : shown;
      const accentRgb = panelLight() && given ? toneLightAccent(given) : shown;
      const accent = accentRgb ? toneHex(accentRgb) : (state.hasColor ? state.color : SWITCH_ICON_ON);
      const card = cssColorChannels(getComputedStyle(tileElem).backgroundColor) || [26, 26, 26];
      bar.style.setProperty('--switch-accent', accent);
      bar.style.setProperty('--switch-card', toneHex(card));
      bar.style.setProperty('--switch-mark', toneHex(accentRgb ? toneMarkOn(accentRgb, card) : card));
      bar.style.setProperty('--switch-notch', toneHex(accentRgb ? toneNotchOn(accentRgb, card) : card));
    });
    const control = cssColorChannels(getComputedStyle(bar).backgroundColor);
    if (control) {
      const base = toneOklch(control);
      bar.style.setProperty('--switch-thumb-off', toneHex(toneRgb(base.L + toneStep(0.06), base.C, base.h)));
    }
    const symbol = bar.querySelector('.tile-switch-symbol');
    if (symbol) symbol.className = 'mdi ' + (on ? 'mdi-power' : 'mdi-circle-outline') + ' tile-switch-symbol';
    bar.__switchFill = {kind, level};
    drawSwitchPreviewFill(bar);
    // The fill is measured in pixels, but the grid gives the tile its size
    // only after the render (layoutTiles), and a hidden tab measures 0: the
    // fill used the one-cell bar. It is drawn again whenever the bar's size
    // changes.
    if (switchBarObserver && !bar.__switchObserved) {
      bar.__switchObserved = true;
      switchBarObserver.observe(bar);
    }
  }

  const switchBarObserver = typeof ResizeObserver === 'function'
    ? new ResizeObserver(entries => {
      for (const entry of entries) {
        const bar = entry.target;
        if (!bar.isConnected) {
          switchBarObserver.unobserve(bar);
          bar.__switchObserved = false;
          continue;
        }
        // A bar whose content depends on its width (the Alarm panel's
        // mode slots) rebuilds itself first.
        if (typeof bar.__onResize === 'function') bar.__onResize();
        drawSwitchPreviewFill(bar);
      }
    })
    : null;

  function drawSwitchPreviewFill(bar) {
    const fill = bar?.querySelector('.tile-switch-fill');
    const drawn = bar?.__switchFill;
    if (!fill || !drawn) return;
    const {kind, level} = drawn;
    const width = bar.clientWidth;
    const height = bar.clientHeight;
    const base = parseFloat(getComputedStyle(bar).getPropertyValue('--switch-bar-height')) || height;
    // An upright bar (level_bar::upright_geometry): along its height, with
    // the one-row bar as its thickness; the fill rises from the bottom.
    const upright = !!bar.parentElement?.classList?.contains('switch-upright');
    const thickness = upright ? Math.min(base, width) : Math.min(base, height);
    const radius = Math.min(parseFloat(getComputedStyle(bar).borderTopLeftRadius) || 0, (upright ? width : height) / 2);
    const geometry = upright ? switchDimmerGeometry(height, thickness, radius, thickness)
      : switchDimmerGeometry(width, height, radius, thickness);
    const fillWidth = kind === 'dimmer' ? geometry.fillWidth(level) : 0;
    const endRadius = geometry.endRadiusFor(fillWidth);
    // The fill element starts one end radius past the bar's start, so only
    // the bar's round start shows (overflow hidden).
    const size = (fillWidth ? fillWidth + endRadius : 0) + 'px';
    fill.style.width = upright ? '' : size;
    fill.style.height = upright ? size : '';
    bar.style.setProperty('--switch-end-radius', endRadius + 'px');
    bar.style.setProperty('--switch-handle-margin', geometry.margin + 'px');
    bar.style.setProperty('--switch-handle-w', geometry.handleWidth + 'px');
    // Upright: the Light popup's handle for its slider width
    // (switch_layout::upright_handle_length).
    bar.style.setProperty('--switch-handle-h', (upright ? Math.floor(width * 46 / 160) : geometry.handleHeight) + 'px');
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
    const orientationEl = document.getElementById(prefix + '_switch_orientation');
    if (orientationEl) orientationEl.value = Number(data.bar_orientation) === 1 ? '1' : '0';
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
    formData.append('bar_orientation', document.getElementById(prefix + '_switch_orientation')?.value === '1' ? '1' : '0');
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
    const orientationEl = document.getElementById(prefix + '_switch_orientation');
    if (orientationEl) orientationEl.value = '0';
    const fontEl = document.getElementById(prefix + '_switch_value_font');
    if (fontEl) fontEl.value = '0';
    const popupModeEl = document.getElementById(prefix + '_switch_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
    syncSwitchChoices(tab);
  }


function maybeFillTitleFromSwitch(tab) {
    maybeFillTitleFromEntity(tab, '_switch_entity');
  }

  const SWITCH_TOGGLE_ON = '#3B82F6';
  const SWITCH_ICON_ON = '#FFD54F';
  const SWITCH_ICON_OFF = '#B0B0B0';
  const SWITCH_ICON_NEUTRAL = '#FFFFFF';

  function syncSwitchPreviewPalette(tileElem) {
    if (!tileElem) return;
    const switchEl = tileElem.querySelector('.tile-switch');
    if (!switchEl) return;
    const bg = tileElem.style.background || window.getComputedStyle(tileElem).backgroundColor || '#353535';
    switchEl.style.setProperty('--switch-knob-color', bg);
    if (tileElem.classList.contains('switch-toggle')) {
      switchEl.style.setProperty('--switch-on-color', SWITCH_TOGGLE_ON);
    }
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
      hasColor: false,
      color: null
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
        }
      } catch (e) {}
    }

    if (text.toLowerCase() === 'unavailable') {
      out.available = false;
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

  function applySwitchPreviewState(tileElem, state) {
    if (!tileElem) return;
    applySwitchPreviewColors(tileElem, state);
    // The icon disc follows the state color like on the device.
    applyIconDiscTint(tileElem);
  }

  function applySwitchPreviewColors(tileElem, state) {
    const iconEl = tileElem.querySelector('.tile-icon');
    const switchEl = tileElem.querySelector('.tile-switch');
    const isToggleStyle = tileElem.classList.contains('switch-toggle');
    syncSwitchPreviewPalette(tileElem);
    if (state.available === false) {
      if (iconEl) iconEl.style.color = SWITCH_ICON_OFF;
      if (switchEl) {
        switchEl.classList.remove('is-on');
        if (isToggleStyle) {
          switchEl.style.setProperty('--switch-on-color', SWITCH_TOGGLE_ON);
        } else {
          switchEl.style.removeProperty('--switch-on-color');
        }
      }
      return;
    }
    if (!state.hasState && !state.hasColor) {
      if (iconEl && isToggleStyle) iconEl.style.color = SWITCH_ICON_NEUTRAL;
      return;
    }
    let isOn = state.hasState ? state.isOn : state.hasColor;
    let color = SWITCH_ICON_OFF;
    if (isOn) color = state.hasColor ? state.color : SWITCH_ICON_ON;
    if (iconEl) iconEl.style.color = isToggleStyle ? SWITCH_ICON_NEUTRAL : color;
    if (switchEl) {
      if (isOn) switchEl.classList.add('is-on');
      else switchEl.classList.remove('is-on');
      if (isToggleStyle) {
        switchEl.style.setProperty('--switch-on-color', SWITCH_TOGGLE_ON);
      } else if (isOn && state.hasColor) {
        switchEl.style.setProperty('--switch-on-color', state.color);
      } else {
        switchEl.style.removeProperty('--switch-on-color');
      }
    }
  }

  function updateSwitchValuePreview(tab) {
    if (currentTileIndex === -1) return;
    const prefix = tab;
    const entitySelect = document.getElementById(prefix + '_switch_entity');
    if (!entitySelect) return;
    const entity = entitySelect.value;
    const tileElem = document.getElementById(tab + '-tile-' + currentTileIndex);
    if (!entity || !tileElem) return;
    syncSwitchPreviewPalette(tileElem);
    const applyMeta = (meta) => {
      const values = (meta && meta.values) || {};
      const state = parseSwitchPayload(values[entity] ?? '');
      applySwitchPreviewState(tileElem, state);
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
      styleEl.value = (data.switch_style !== undefined && data.switch_style !== null) ? String(data.switch_style) : '0';
    }
    const popupModeEl = document.getElementById(prefix + '_switch_popup_open_mode');
    if (popupModeEl) {
      popupModeEl.value = (data.popup_open_mode !== undefined) ? String(data.popup_open_mode) : '1';
    }
    maybeFillTitleFromSwitch(tab);
  }

  function saveSwitchFields(tab, formData) {
    saveIconColorFields(tab, formData);
    const prefix = tab;
    formData.append('switch_entity', document.getElementById(prefix + '_switch_entity')?.value || '');
    const styleEl = document.getElementById(prefix + '_switch_style');
    formData.append('switch_style', styleEl ? styleEl.value : '0');
    formData.append('popup_open_mode', document.getElementById(prefix + '_switch_popup_open_mode')?.value || '1');
  }

  function resetSwitchFields(tab) {
    resetIconColorFields(tab);
    const prefix = tab;
    const entityEl = document.getElementById(prefix + '_switch_entity');
    if (entityEl) entityEl.value = '';
    const styleEl = document.getElementById(prefix + '_switch_style');
    if (styleEl) styleEl.value = '0';
    const popupModeEl = document.getElementById(prefix + '_switch_popup_open_mode');
    if (popupModeEl) popupModeEl.value = '1';
  }

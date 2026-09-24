
function normalizeTextValueFont(value) {
    const v = String(value || '0');
    return (['1','2','3','4'].includes(v)) ? v : '0';
  }

  function loadTextFields(tab, data) {
    const border = document.getElementById(tab + '_text_tile_border');
    if (border) border.checked = data?.tile_border !== undefined ? !['0','false'].includes(String(data.tile_border)) : Number(data?.sensor_display_mode) !== 1;
    const prefix = tab;
    const textEl = document.getElementById(prefix + '_text_value');
    const fontEl = document.getElementById(prefix + '_text_value_font');
    if (!textEl) return;
    if (data && data.text_value !== undefined) {
      textEl.value = data.text_value || '';
    } else if (data && data.scene_alias !== undefined) {
      textEl.value = data.scene_alias || '';
    } else if (data && data.key_macro !== undefined) {
      textEl.value = data.key_macro || '';
    } else {
      textEl.value = '';
    }
    if (fontEl) {
      if (data && data.text_value_font !== undefined) {
        fontEl.value = normalizeTextValueFont(data.text_value_font);
      } else if (data && data.sensor_value_font !== undefined) {
        fontEl.value = normalizeTextValueFont(data.sensor_value_font);
      } else {
        fontEl.value = '0';
      }
    }
  }

  function saveTextFields(tab, formData) {
    formData.append('tile_border', document.getElementById(tab + '_text_tile_border')?.checked === false ? '0' : '1');
    const prefix = tab;
    formData.append('text_value', document.getElementById(prefix + '_text_value')?.value || '');
    formData.append('text_value_font', document.getElementById(prefix + '_text_value_font')?.value || '0');
  }

  function resetTextFields(tab) {
    const border = document.getElementById(tab + '_text_tile_border');
    if (border) border.checked = true;
    const prefix = tab;
    const textEl = document.getElementById(prefix + '_text_value');
    if (textEl) textEl.value = '';
    const fontEl = document.getElementById(prefix + '_text_value_font');
    if (fontEl) fontEl.value = '0';
  }

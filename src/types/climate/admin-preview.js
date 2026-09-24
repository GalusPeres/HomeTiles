
  function parseClimatePreviewPayload(value) {
    const out = {
      valid: false,
      current: '--',
      currentHumidity: null,
      target: null,
      targetHumidity: null,
      targetLow: null,
      targetHigh: null,
      unit: '\u00B0C',
      mode: '',
      action: '',
      preset: '',
      available: true,
      hasSupportedFeatures: false,
      supportedFeatures: 0
    };
    if (value === undefined || value === null) return out;
    const text = String(value).trim();
    if (!text.length) return out;
    if (!text.startsWith('{')) {
      const state = text.toLowerCase();
      if (state === 'unavailable' || state === 'unknown') {
        out.mode = state;
        out.available = state !== 'unavailable';
        out.valid = true;
        return out;
      }
      const numeric = Number(text.replace(',', '.'));
      if (Number.isFinite(numeric)) {
        out.valid = true;
        out.current = formatLocalizedNumber(numeric, 1, false);
      }
      return out;
    }
    try {
      const obj = JSON.parse(text);
      if (!obj || typeof obj !== 'object') return out;
      const attrs = obj.attributes && typeof obj.attributes === 'object'
        ? obj.attributes : obj;
      const current = attrs.current_temperature;
      if (current !== undefined && current !== null && Number.isFinite(Number(current))) {
        out.current = formatLocalizedNumber(Number(current), 1, false);
      }
      const currentHumidity = attrs.current_humidity;
      if (currentHumidity !== undefined && currentHumidity !== null &&
          Number.isFinite(Number(currentHumidity))) {
        out.currentHumidity = formatLocalizedNumber(
          Number(currentHumidity), 0, true);
      }
      const target = attrs.temperature;
      if (target !== undefined && target !== null &&
          Number.isFinite(Number(target))) {
        out.target = formatLocalizedNumber(Number(target), 1, false);
      }
      const targetHumidity = attrs.humidity;
      if (targetHumidity !== undefined && targetHumidity !== null &&
          Number.isFinite(Number(targetHumidity))) {
        out.targetHumidity = formatLocalizedNumber(
          Number(targetHumidity), 0, true);
      }
      const targetLow = attrs.target_temp_low;
      if (targetLow !== undefined && targetLow !== null &&
          Number.isFinite(Number(targetLow))) {
        out.targetLow = formatLocalizedNumber(Number(targetLow), 1, false);
      }
      const targetHigh = attrs.target_temp_high;
      if (targetHigh !== undefined && targetHigh !== null &&
          Number.isFinite(Number(targetHigh))) {
        out.targetHigh = formatLocalizedNumber(Number(targetHigh), 1, false);
      }
      out.unit = attrs.temperature_unit || attrs.unit_of_measurement || '\u00B0C';
      out.mode = String(obj.hvac_mode || obj.state || attrs.hvac_mode || '').toLowerCase();
      out.action = String(obj.hvac_action || attrs.hvac_action || '').toLowerCase();
      out.preset = String(obj.preset_mode || attrs.preset_mode || '').toLowerCase();
      const available = obj.available ?? attrs.available;
      out.available = available !== undefined
        ? !!available : out.mode !== 'unavailable';
      const supportedFeatures =
        obj.supported_features ?? attrs.supported_features;
      if (supportedFeatures !== undefined && supportedFeatures !== null &&
          Number.isFinite(Number(supportedFeatures)) &&
          Number(supportedFeatures) >= 0) {
        out.hasSupportedFeatures = true;
        out.supportedFeatures = Math.min(
          65535, Math.round(Number(supportedFeatures)));
      }
      out.valid =
        !out.available ||
        out.current !== '--' ||
        out.currentHumidity !== null ||
        out.target !== null ||
        out.targetHumidity !== null ||
        out.targetLow !== null ||
        out.targetHigh !== null ||
        !!out.mode ||
        !!out.action;
    } catch (e) {}
    return out;
  }

  function climatePreviewIcon(state, baseIcon) {
    const action = String(state?.action || '').toLowerCase();
    const mode = String(state?.mode || '').toLowerCase();
    const fallback = normalizeMdiIconName(baseIcon) || 'thermostat';
    if (state?.available === false) return fallback;
    if (action === 'heating' || action === 'preheating') return 'fire';
    if (action === 'cooling') return 'snowflake';
    if (action === 'drying') return 'water-percent';
    if (action === 'fan') return 'fan';
    if (action === 'defrosting') return 'snowflake-melt';
    if (mode === 'off' || action === 'idle' || action === 'off' || action) {
      return fallback;
    }
    if (mode === 'heat') return 'fire';
    if (mode === 'cool') return 'snowflake';
    if (mode === 'dry') return 'water-percent';
    if (mode === 'fan_only') return 'fan';
    if (mode === 'heat_cool') return 'sun-snowflake-variant';
    if (mode === 'auto') return 'thermostat-auto';
    return fallback;
  }

  function climatePreviewColor(state) {
    const action = String(state?.action || '').toLowerCase();
    const mode = String(state?.mode || '').toLowerCase();
    if (state?.available === false || mode === 'unavailable') {
      return '#9e9e9e';
    }
    if (action === 'heating' || action === 'preheating') return '#ff8a3d';
    if (action === 'cooling') return '#4fc3f7';
    if (action === 'drying') return '#ffd54f';
    if (action === 'fan') return '#4db6ac';
    if (action === 'defrosting') return '#81d4fa';
    if (mode === 'off' || action === 'idle' || action === 'off') return '#9e9e9e';
    if (!action && mode === 'heat') return '#ff8a3d';
    if (!action && mode === 'cool') return '#4fc3f7';
    if (!action && mode === 'dry') return '#ffd54f';
    if (!action && mode === 'fan_only') return '#4db6ac';
    return '#ffffff';
  }

  function climatePreviewSlots(
      state, spanW, spanH, slotConfig = null,
      targetLayoutConfig = null, geometryConfig = null) {
    // Layout variants follow whole cells, like build_automatic_slot_kinds.
    const w = Math.max(1, Math.floor(Number(spanW) || 1));
    const h = Math.max(1, Math.floor(Number(spanH) || 1));
    const capacity = climateSlotCapacity(w, h);
    const { columns, rows } =
      climateGridDimensions(w, h);
    const configured = Array.isArray(slotConfig)
      ? slotConfig.slice(0, 6)
      : decodeClimateSlotConfig(slotConfig || 0);
    while (configured.length < 6) {
      configured.push(CLIMATE_TILE_CONTENT.AUTO);
    }
    const targetLayouts = Array.isArray(targetLayoutConfig)
      ? targetLayoutConfig.slice(0, 6)
      : decodeClimateTargetLayouts(targetLayoutConfig || 0);
    while (targetLayouts.length < 6) {
      targetLayouts.push(CLIMATE_TARGET_LAYOUT.AUTO);
    }
    const geometry = Array.isArray(geometryConfig)
      ? geometryConfig.slice(0, 6)
      : decodeClimateGeometry(
          geometryConfig || '', w, h,
          configured, targetLayouts);
    while (geometry.length < 6) {
      geometry.push({ col: 0, row: 0, spanW: 1, spanH: 1 });
    }

    const automatic = [];
    const temp = (value) => String(value ?? '--') + ' ' + state.unit;
    const addAutomatic = (kind) => {
      if (automatic.length < capacity) {
        automatic.push(kind);
      }
    };

    const addPrimaryTarget = () => {
      if (state.targetLow !== null && state.targetHigh !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW);
      } else if (state.target !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      } else if (state.targetHumidity !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      } else if (!state.valid) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      }
    };

    const entityState = String(state?.mode || '').toLowerCase();
    if (state?.available === false || entityState === 'unavailable' ||
        entityState === 'unknown') {
      addAutomatic(CLIMATE_TILE_CONTENT.HVAC_MODE);
    } else if (w === 1 && h === 1) {
      if (!state.valid || state.current !== '--') {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      } else {
        addPrimaryTarget();
      }
    } else if (w >= 2 && h === 1) {
      if (!state.valid || state.current !== '--') {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
    } else if (w === 1) {
      if (!state.valid || state.current !== '--') {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
      if (h > 2 &&
          state.targetHumidity !== null &&
          (state.targetLow !== null ||
           state.targetHigh !== null ||
           state.target !== null)) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      }
    } else {
      if (!state.valid || state.current !== '--') {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      if (state.currentHumidity !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY);
      }
      if (state.targetLow !== null && state.targetHigh !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW);
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH);
      } else if (state.target !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      }
      if (state.targetHumidity !== null) {
        addAutomatic(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      }
      if (state.mode) {
        addAutomatic(CLIMATE_TILE_CONTENT.HVAC_MODE);
      }
    }

    const slotForKind = (kind) => {
      switch (kind) {
        case CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE:
          return { kind, value: temp(state.current), adjustable: false };
        case CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY:
          return {
            kind,
            value: state.currentHumidity !== null
              ? state.currentHumidity + '%' : '--%',
            adjustable: false
          };
        case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE:
          return {
            kind,
            value: temp(state.target),
            adjustable: true,
            interactive: climateTargetInteractive(state, kind),
            caption: climateTargetCaption(state, kind)
          };
        case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW:
          return {
            kind,
            value: temp(state.targetLow),
            adjustable: true,
            interactive: climateTargetInteractive(state, kind),
            caption: climateTargetCaption(state, kind)
          };
        case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH:
          return {
            kind,
            value: temp(state.targetHigh),
            adjustable: true,
            interactive: climateTargetInteractive(state, kind),
            caption: climateTargetCaption(state, kind)
          };
        case CLIMATE_TILE_CONTENT.TARGET_HUMIDITY:
          return {
            kind,
            value: state.targetHumidity !== null
              ? state.targetHumidity + '%' : '--%',
            adjustable: true,
            interactive: climateTargetInteractive(state, kind),
            caption: climateTargetCaption(state, kind)
          };
        case CLIMATE_TILE_CONTENT.HVAC_MODE:
          return {
            kind,
            value: climateModeText(state),
            adjustable: false
          };
        default:
          return null;
      }
    };

    const explicitlyConfigured = new Set();
    configured.slice(0, capacity).forEach(selection => {
      const kind = Number(selection) || 0;
      if (kind !== CLIMATE_TILE_CONTENT.AUTO &&
          kind !== CLIMATE_TILE_CONTENT.EMPTY) {
        explicitlyConfigured.add(kind);
      }
    });

    const slots = [];
    let automaticCursor = 0;
    for (let index = 0; index < capacity; ++index) {
      const selection = Number(configured[index]) || 0;
      if (selection === CLIMATE_TILE_CONTENT.EMPTY) continue;
      let kind = selection;
      let targetLayout = Number(targetLayouts[index]) || 0;
      if (selection === CLIMATE_TILE_CONTENT.AUTO) {
        kind = undefined;
        targetLayout = CLIMATE_TARGET_LAYOUT.AUTO;
        while (automaticCursor < automatic.length) {
          const candidate = automatic[automaticCursor++];
          if (explicitlyConfigured.has(candidate)) continue;
          kind = candidate;
          break;
        }
      }
      const slot = slotForKind(kind);
      if (!slot) continue;
      slots.push({
        ...slot,
        targetLayout,
        itemIndex: index,
        ...clampClimateGeometryItem(
          geometry[index], columns, rows)
      });
    }

    const logicalRows = rows;
    const hasStoredGeometry =
      Array.isArray(geometryConfig) ||
      /^CLG[12]:/i.test(String(geometryConfig || '').trim());
    const placedSlots = [];
    slots.forEach(slot => {
      let candidate = {
        col: slot.col,
        row: slot.row,
        spanW: slot.spanW,
        spanH: slot.spanH
      };
      if (!hasStoredGeometry && slot.adjustable &&
          candidate.spanW === 1 && candidate.spanH === 1) {
        const canHorizontal =
          candidate.col + 1 < columns;
        const canVertical =
          candidate.row + 1 < logicalRows;
        if (slot.targetLayout ===
              CLIMATE_TARGET_LAYOUT.HORIZONTAL &&
            canHorizontal) {
          candidate.spanW = 2;
        } else if (slot.targetLayout ===
                     CLIMATE_TARGET_LAYOUT.VERTICAL &&
                   canVertical) {
          candidate.spanH = 2;
        } else if (columns === 1 && canVertical) {
          candidate.spanH = 2;
        } else if (logicalRows === 1 && canHorizontal) {
          candidate.spanW = 2;
        } else if (canVertical) {
          candidate.spanH = 2;
        } else if (canHorizontal) {
          candidate.spanW = 2;
        }
      }
      const overlaps = value =>
        placedSlots.some(other =>
            climateGeometryOverlaps(value, {
              col: other.col,
              row: other.row,
              spanW: other.spanW,
              spanH: other.spanH
            }));
      if (overlaps(candidate)) {
        let free = null;
        for (let row = 0;
             row + candidate.spanH <= logicalRows && !free;
             ++row) {
          for (let col = 0;
               col + candidate.spanW <= columns;
               ++col) {
            const next = {
              col, row,
              spanW: candidate.spanW,
              spanH: candidate.spanH
            };
            if (!placedSlots.some(other =>
                  climateGeometryOverlaps(next, {
                    col: other.col,
                    row: other.row,
                    spanW: other.spanW,
                    spanH: other.spanH
                  }))) {
              free = next;
              break;
            }
          }
        }
        if (!free) return;
        candidate = free;
      }
      placedSlots.push({ ...slot, ...candidate });
    });

    const occupiedCells =
      Array(columns * logicalRows).fill(false);
    placedSlots.forEach(slot => {
      for (let row = slot.row;
           row < slot.row + slot.spanH; ++row) {
        for (let col = slot.col;
             col < slot.col + slot.spanW; ++col) {
          occupiedCells[row * columns + col] = true;
        }
      }
    });
    const emptyCellLabel = CLIMATE_I18N.emptyField;
    const previewCells = occupiedCells.map(
      (occupied, cellIndex) => {
        if (occupied) return '';
        const row = Math.floor(cellIndex / columns) + 1;
        const column = cellIndex % columns + 1;
        return '<button type="button" ' +
          'class="climate-preview-cell" ' +
          'data-climate-preview-cell="' + cellIndex + '" ' +
          'aria-label="' + emptyCellLabel + '" ' +
          'style="grid-column:' + column +
          ';grid-row:' + row + '"></button>';
      }).join('');

    return '<div class="climate-slots" style="--climate-columns:' +
      columns +
      ';--climate-rows:' + logicalRows + '">' +
      previewCells +
      placedSlots.map(slot => {
        const row = slot.row + 1;
        const column = slot.col + 1;
        const horizontal =
          slot.adjustable && slot.spanW > 1 &&
          slot.spanH === 1;
        const vertical =
          slot.adjustable && slot.spanW === 1 &&
          slot.spanH > 1;
        const large =
          slot.adjustable && slot.spanW > 1 &&
          slot.spanH > 1;
        const compact =
          slot.adjustable && slot.spanW === 1 &&
          slot.spanH === 1;
        const gridStyle =
          'grid-column:' + column + ' / span ' +
          slot.spanW +
          ';grid-row:' + row + ' / span ' +
          slot.spanH;
        if (!slot.adjustable || slot.interactive === false) {
          return '<div class="climate-slot climate-slot-value' +
            (slot.kind === CLIMATE_TILE_CONTENT.HVAC_MODE
              ? ' climate-slot-mode' : '') +
            '" data-climate-preview-item="' +
            slot.itemIndex + '" style="' +
            gridStyle + '"><strong>' + escapeHtml(slot.value) +
            '</strong></div>';
        }
        if (compact) {
          return '<div class="climate-slot climate-slot-control ' +
            'climate-slot-control-compact" ' +
            'data-climate-preview-item="' +
            slot.itemIndex + '" style="' + gridStyle + '">' +
            '<span class="climate-minus" aria-hidden="true">-</span>' +
            '<strong>' + escapeHtml(slot.value) + '</strong>' +
            '<span class="climate-plus" aria-hidden="true">+</span></div>';
        }
        const controlClass = horizontal
          ? 'climate-slot-control-horizontal'
          : (large
              ? 'climate-slot-control-large'
              : 'climate-slot-control-vertical ' +
                (columns > 1
                  ? (slot.col === 0
                      ? 'climate-slot-column-left'
                      : 'climate-slot-column-right')
                  : ''));
        return '<div class="climate-slot climate-slot-control ' +
          controlClass +
          '" data-climate-preview-item="' +
          slot.itemIndex + '" style="' + gridStyle + '">' +
          '<small>' + escapeHtml(slot.caption) + '</small>' +
          '<span class="climate-minus">-</span><strong>' +
          escapeHtml(slot.value) +
          '</strong><span class="climate-plus">+</span></div>';
      }).join('') +
      '</div>';
  }

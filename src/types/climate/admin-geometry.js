
  const CLIMATE_TILE_CONTENT = Object.freeze({
    AUTO: 0,
    EMPTY: 1,
    CURRENT_TEMPERATURE: 2,
    CURRENT_HUMIDITY: 3,
    TARGET_TEMPERATURE: 4,
    TARGET_TEMPERATURE_LOW: 5,
    TARGET_TEMPERATURE_HIGH: 6,
    TARGET_HUMIDITY: 7,
    HVAC_MODE: 8
  });
  const CLIMATE_TARGET_LAYOUT = Object.freeze({
    AUTO: 0,
    HORIZONTAL: 1,
    VERTICAL: 2
  });
  const CLIMATE_SUPPORTED_FEATURE = Object.freeze({
    TARGET_TEMPERATURE: 1,
    TARGET_TEMPERATURE_RANGE: 2,
    TARGET_HUMIDITY: 4,
    FAN_MODE: 8,
    PRESET_MODE: 16,
    SWING_MODE: 32,
    SWING_HORIZONTAL_MODE: 512
  });
  const CLIMATE_LAYOUT_MAGIC = 0x434c0000;
  const CLIMATE_LAYOUT_MAGIC_MASK = 0xffff0000;
  const CLIMATE_LAYOUT_VALUE_MASK = 0x00000fff;
  const CLIMATE_GEOMETRY_PREFIX = 'CLG2:';
  const CLIMATE_GEOMETRY_LEGACY_PREFIX = 'CLG1:';

  function climateMaxGridColumns() {
    return Math.max(
      1, Number(
        typeof GRID_COLS === 'number' ? GRID_COLS : 7) || 7);
  }

  function climateMaxOuterRows() {
    return Math.max(
      1, Number(
        typeof GRID_ROWS === 'number' ? GRID_ROWS : 5) || 5);
  }

  function climateSlotCapacity(spanW, spanH) {
    const { columns, rows } =
      climateGridDimensions(spanW, spanH);
    return Math.min(6, columns * rows);
  }

  function climateGridDimensions(spanW, spanH) {
    const columns = Math.max(
      1, Math.min(
        climateMaxGridColumns(), Math.floor(Number(spanW) || 1)));
    const outerRows = Math.max(
      1, Math.min(
        climateMaxOuterRows(), Math.floor(Number(spanH) || 1)));
    return {
      columns,
      rows: outerRows * 2 - 1
    };
  }

  function clampClimateGeometryItem(item, columns, rows) {
    const col = Math.max(
      0, Math.min(columns - 1, Number(item?.col) || 0));
    const row = Math.max(
      0, Math.min(rows - 1, Number(item?.row) || 0));
    const spanW = Math.max(
      1, Math.min(columns - col, Number(item?.spanW) || 1));
    const spanH = Math.max(
      1, Math.min(rows - row, Number(item?.spanH) || 1));
    return { col, row, spanW, spanH };
  }

  function sanitizeClimateGeometryItem(item) {
    const maxColumns = climateMaxGridColumns();
    const maxRows = climateMaxOuterRows() * 2 - 1;
    return {
      col: Math.max(
        0, Math.min(maxColumns - 1, Number(item?.col) || 0)),
      row: Math.max(
        0, Math.min(maxRows - 1, Number(item?.row) || 0)),
      spanW: Math.max(
        1, Math.min(maxColumns, Number(item?.spanW) || 1)),
      spanH: Math.max(
        1, Math.min(maxRows, Number(item?.spanH) || 1))
    };
  }

  function defaultClimateGeometry(
      spanW, spanH, slotConfig = null,
      targetLayoutConfig = null) {
    const { columns, rows } =
      climateGridDimensions(spanW, spanH);
    const configured = Array.isArray(slotConfig)
      ? slotConfig : Array(6).fill(CLIMATE_TILE_CONTENT.AUTO);
    const layouts = Array.isArray(targetLayoutConfig)
      ? targetLayoutConfig : Array(6).fill(CLIMATE_TARGET_LAYOUT.AUTO);
    return Array.from({ length: 6 }, (_, index) => {
      const item = {
        col: index % columns,
        row: Math.min(rows - 1, Math.floor(index / columns)),
        spanW: 1,
        spanH: 1
      };
      const content = Number(configured[index]) || 0;
      const adjustable =
        content >= CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE &&
        content <= CLIMATE_TILE_CONTENT.TARGET_HUMIDITY;
      if (!adjustable) return item;
      const layout = Number(layouts[index]) || 0;
      const canHorizontal = item.col + 1 < columns;
      const canVertical = item.row + 1 < rows;
      if (layout === CLIMATE_TARGET_LAYOUT.HORIZONTAL &&
          canHorizontal) {
        item.spanW = 2;
      } else if (layout === CLIMATE_TARGET_LAYOUT.VERTICAL &&
                 canVertical) {
        item.spanH = 2;
      } else if (columns === 1 && canVertical) {
        item.spanH = 2;
      } else if (rows === 1 && canHorizontal) {
        item.spanW = 2;
      } else if (canVertical) {
        item.spanH = 2;
      } else if (canHorizontal) {
        item.spanW = 2;
      }
      return item;
    });
  }

  function decodeClimateGeometry(
      value, spanW, spanH, slotConfig = null,
      targetLayoutConfig = null) {
    const text = String(value || '').trim();
    const currentMatch =
      text.match(/^CLG2:([0-9a-fA-F]{24})$/);
    if (currentMatch) {
      return Array.from({ length: 6 }, (_, index) => {
        const offset = index * 4;
        const raw = Number.parseInt(
          currentMatch[1].slice(offset, offset + 4), 16);
        return sanitizeClimateGeometryItem({
          col: raw & 0x07,
          row: (raw >> 3) & 0x0f,
          spanW: ((raw >> 7) & 0x07) + 1,
          spanH: ((raw >> 10) & 0x0f) + 1
        });
      });
    }
    const legacyMatch =
      text.match(/^CLG1:([0-9a-fA-F]{9})$/);
    if (!legacyMatch) {
      return defaultClimateGeometry(
        spanW, spanH, slotConfig, targetLayoutConfig);
    }
    let packed = BigInt('0x' + legacyMatch[1]);
    return Array.from({ length: 6 }, (_, index) => {
      const raw = Number((packed >> BigInt(index * 6)) & 0x3fn);
      return sanitizeClimateGeometryItem({
        col: raw & 0x01,
        row: (raw >> 1) & 0x03,
        spanW: ((raw >> 3) & 0x01) + 1,
        spanH: ((raw >> 4) & 0x03) + 1
      });
    });
  }

  function encodeClimateGeometry(items) {
    const encoded = Array.from({ length: 6 }, (_, index) => {
      const item = sanitizeClimateGeometryItem(
        items[index] || {});
      const raw =
        item.col |
        (item.row << 3) |
        ((item.spanW - 1) << 7) |
        ((item.spanH - 1) << 10);
      return (raw & 0x3fff)
        .toString(16).toUpperCase().padStart(4, '0');
    }).join('');
    return CLIMATE_GEOMETRY_PREFIX + encoded;
  }

  function currentClimateGeometry(tab) {
    const spanW = document.getElementById(
      tab + '_tile_span_w')?.value || 1;
    const spanH = document.getElementById(
      tab + '_tile_span_h')?.value || 1;
    const input = document.getElementById(
      tab + '_climate_geometry');
    const configured = currentClimateSlotConfig(tab);
    const resolved = climateResolvedEditorKinds(tab);
    const effectiveConfig = configured.map((value, index) =>
      Number(value) === CLIMATE_TILE_CONTENT.AUTO &&
      resolved[index] !== null
        ? resolved[index]
        : value);
    return decodeClimateGeometry(
      input?.value || '', spanW, spanH,
      effectiveConfig,
      currentClimateTargetLayouts(tab));
  }

  function storeClimateGeometry(tab, items) {
    const input = document.getElementById(
      tab + '_climate_geometry');
    if (input) input.value = encodeClimateGeometry(items);
  }

  function decodeClimateSlotConfig(packedValue) {
    const packed = Math.max(0, Number(packedValue) || 0) >>> 0;
    return Array.from({ length: 6 }, (_, index) => {
      const value = (packed >>> (index * 4)) & 0x0f;
      return value <= CLIMATE_TILE_CONTENT.HVAC_MODE
        ? value : CLIMATE_TILE_CONTENT.AUTO;
    });
  }

  function packClimateSlotConfig(tab) {
    let packed = 0;
    for (let index = 0; index < 6; ++index) {
      const select = document.getElementById(
        tab + '_climate_slot_' + index);
      const value = Math.max(
        0, Math.min(
          CLIMATE_TILE_CONTENT.HVAC_MODE,
          Number(select?.value) || 0));
      packed |= (value & 0x0f) << (index * 4);
    }
    return packed >>> 0;
  }

  function currentClimateSlotConfig(tab) {
    return Array.from({ length: 6 }, (_, index) => {
      const select = document.getElementById(
        tab + '_climate_slot_' + index);
      return Number(select?.value) || 0;
    });
  }

  function getClimateLayoutPayload(storedValue) {
    const stored = Math.max(0, Number(storedValue) || 0) >>> 0;
    if (((stored & CLIMATE_LAYOUT_MAGIC_MASK) >>> 0) ===
        CLIMATE_LAYOUT_MAGIC) {
      return stored & CLIMATE_LAYOUT_VALUE_MASK;
    }
    return stored <= CLIMATE_LAYOUT_VALUE_MASK ? stored : 0;
  }

  function decodeClimateTargetLayouts(storedValue) {
    const packed = getClimateLayoutPayload(storedValue);
    return Array.from({ length: 6 }, (_, index) => {
      const value = (packed >>> (index * 2)) & 0x03;
      return value <= CLIMATE_TARGET_LAYOUT.VERTICAL
        ? value : CLIMATE_TARGET_LAYOUT.AUTO;
    });
  }

  function packClimateTargetLayouts(tab) {
    let packed = 0;
    for (let index = 0; index < 6; ++index) {
      const select = document.getElementById(
        tab + '_climate_layout_' + index);
      const value = Math.max(
        0, Math.min(
          CLIMATE_TARGET_LAYOUT.VERTICAL,
          Number(select?.value) || 0));
      packed |= (value & 0x03) << (index * 2);
    }
    return packed >>> 0;
  }

  function currentClimateTargetLayouts(tab) {
    return Array.from({ length: 6 }, (_, index) => {
      const select = document.getElementById(
        tab + '_climate_layout_' + index);
      return Number(select?.value) || CLIMATE_TARGET_LAYOUT.AUTO;
    });
  }

  function climateGeometryOverlaps(a, b) {
    return a.col < b.col + b.spanW &&
      a.col + a.spanW > b.col &&
      a.row < b.row + b.spanH &&
      a.row + a.spanH > b.row;
  }

  function canPlaceClimateItem(
      items, configured, index, candidate, capacity) {
    for (let other = 0; other < capacity; ++other) {
      if (other === index) continue;
      if (Number(configured[other]) === CLIMATE_TILE_CONTENT.EMPTY) {
        continue;
      }
      if (climateGeometryOverlaps(candidate, items[other])) {
        return false;
      }
    }
    return true;
  }

  function firstFreeClimatePlacement(
      items, configured, capacity, columns, rows,
      ignoreIndex = -1, spanW = 1, spanH = 1) {
    const safeSpanW = Math.max(
      1, Math.min(columns, Number(spanW) || 1));
    const safeSpanH = Math.max(
      1, Math.min(rows, Number(spanH) || 1));
    for (let row = 0; row + safeSpanH <= rows; ++row) {
      for (let col = 0; col + safeSpanW <= columns; ++col) {
        const candidate = {
          col, row,
          spanW: safeSpanW,
          spanH: safeSpanH
        };
        if (canPlaceClimateItem(
              items, configured, ignoreIndex,
              candidate, capacity)) {
          return candidate;
        }
      }
    }
    return null;
  }

  function notifyClimateGridChanged(tab) {
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }

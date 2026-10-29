  // Power flow tile (types/power_flow): the node sensors, the options and
  // the previews. The record (Tile::flow_entities) holds one entity per line
  // in Home Assistant's order; lines after the five pickers are kept.
  const POWER_FLOW_TILE_TYPE = 28;
  const POWER_FLOW_FIELDS = Object.freeze(['flow_pv_entity', 'flow_grid_entity', 'flow_battery_entity',
    'flow_battery_charge_entity', 'flow_home_entity']);
  // key_code bits (tile_config.h kFlow*Bit); the card checkbox is the
  // inverse of the stored "no card" bit.
  const POWER_FLOW_OPTIONS = Object.freeze([['flow_header', 1], ['flow_no_card', 2], ['flow_grid_invert', 4],
    ['flow_battery_invert', 8]]);

  function powerFlowLines(record) {
    return String(record || '').split('\n');
  }

  function powerFlowRecordFromInputs(tab) {
    const container = document.getElementById(tab + '_power_flow_fields');
    const lines = powerFlowLines(container?.dataset.flowRecord || '');
    POWER_FLOW_FIELDS.forEach((field, i) => {
      lines[i] = String(document.getElementById(tab + '_' + field)?.value || '').trim();
    });
    while (lines.length && !lines[lines.length - 1]) lines.pop();
    return lines.join('\n');
  }

  function powerFlowOptionsFromInputs(tab) {
    let bits = 0;
    if (document.getElementById(tab + '_flow_header')?.checked) bits |= 1;
    if (document.getElementById(tab + '_flow_card')?.checked === false) bits |= 2;
    if (document.getElementById(tab + '_flow_grid_invert')?.checked) bits |= 4;
    if (document.getElementById(tab + '_flow_battery_invert')?.checked) bits |= 8;
    return bits;
  }

  function powerFlowSetOptions(tab, bits) {
    const set = (field, on) => { const box = document.getElementById(tab + '_' + field); if (box) box.checked = on; };
    set('flow_header', (bits & 1) !== 0);
    set('flow_card', (bits & 2) === 0);
    set('flow_grid_invert', (bits & 4) !== 0);
    set('flow_battery_invert', (bits & 8) !== 0);
  }

  function loadPowerFlowFields(tab, data) {
    const record = String(data?.flow_entities || '');
    const container = document.getElementById(tab + '_power_flow_fields');
    if (container) container.dataset.flowRecord = record;
    const lines = powerFlowLines(record);
    POWER_FLOW_FIELDS.forEach((field, i) => {
      const input = document.getElementById(tab + '_' + field);
      if (input) input.value = lines[i] || '';
    });
    // Stored tiles carry key_code, copied form data (clipboard.js) the
    // option fields.
    powerFlowSetOptions(tab, data && Object.prototype.hasOwnProperty.call(data, 'flow_header')
      ? powerFlowOptionBits(data) : (Number(data?.key_code) || 0));
  }

  function savePowerFlowFields(tab, formData) {
    formData.append('flow_entities', powerFlowRecordFromInputs(tab));
    const bits = powerFlowOptionsFromInputs(tab);
    for (const [name, bit] of POWER_FLOW_OPTIONS) formData.append(name, (bits & bit) ? '1' : '0');
  }

  function resetPowerFlowFields(tab) {
    const container = document.getElementById(tab + '_power_flow_fields');
    if (container) delete container.dataset.flowRecord;
    for (const field of POWER_FLOW_FIELDS) {
      const input = document.getElementById(tab + '_' + field);
      if (input) input.value = '';
    }
    powerFlowSetOptions(tab, 0);
  }

  // "Show title and icon" with an empty title puts the type's name in, like
  // Auto (type-selection.js placeTileTypeParts names it).
  function powerFlowHeaderTitle(tab) {
    if (!document.getElementById(tab + '_flow_header')?.checked) return;
    const title = document.getElementById(tab + '_tile_title');
    const name = document.getElementById(tab + '_tile_title_auto')?.dataset.autoTitle || '';
    if (!title || String(title.value || '').trim() || !name) return;
    title.value = normalizeTileTitle(name);
    title.dispatchEvent(new Event('input', { bubbles: true }));
  }

  // The stored key_code of a snapshot's options (snapshots.js, import).
  function powerFlowOptionBits(source) {
    let bits = 0;
    for (const [name, bit] of POWER_FLOW_OPTIONS) {
      if (['1', 'true', 'on'].includes(String(source?.[name] ?? '0'))) bits |= bit;
    }
    return bits;
  }

  // ----- Geometry: flow_geometry.h line for line --------------------------
  const POWER_FLOW_BEND_SEGMENTS = 16;

  function powerFlowPolyline() {
    return { p: [], along: [], bend: null, bendFirst: 0, bendLast: 0 };
  }

  function powerFlowAdd(line, x, y) {
    const n = line.p.length;
    line.along.push(n ? line.along[n - 1] + Math.hypot(x - line.p[n - 1][0], y - line.p[n - 1][1]) : 0);
    line.p.push([x, y]);
  }

  function powerFlowCircleOffset(radius, d) {
    const q = radius * radius - d * d;
    return q > 0 ? Math.sqrt(q) : 0;
  }

  // C's lroundf: halves away from zero.
  function powerFlowRound(value) {
    return value < 0 ? -Math.round(-value) : Math.round(value);
  }

  // Down (or up) from the source's disc, a quarter circle on whole pixels,
  // along the lane to the target's disc.
  function powerFlowLeg(l, from, dir, to, laneY, spacing) {
    const line = powerFlowPolyline();
    const down = from === 'pv';
    const x = powerFlowRound(l.node.pv[0] + dir * spacing);
    const lane = powerFlowRound(laneY);
    const y0 = l.node[from][1] + (down ? 1 : -1) * powerFlowCircleOffset(l.disc, x - l.node[from][0]);
    const endX = l.node[to][0] - dir * powerFlowCircleOffset(l.disc, lane - l.node[to][1]);
    const sign = lane >= y0 ? 1 : -1;
    const radius = powerFlowRound(Math.min(Math.abs(lane - y0), Math.abs(endX - x)));
    const fromAngle = dir > 0 ? 180 : 0;
    const toAngle = sign > 0 ? 90 : (dir > 0 ? 270 : -90);
    const bend = { cx: x + dir * radius, cy: lane - sign * radius, radius, fromAngle, toAngle };
    powerFlowAdd(line, x, y0);
    if (Math.abs(bend.cy - y0) > 0.5) powerFlowAdd(line, x, bend.cy);
    line.bendFirst = line.p.length - 1;
    if (radius > 0) {
      line.bend = bend;
      for (let i = 1; i <= POWER_FLOW_BEND_SEGMENTS; ++i) {
        const angle = (fromAngle + (toAngle - fromAngle) * i / POWER_FLOW_BEND_SEGMENTS) * Math.PI / 180;
        powerFlowAdd(line, bend.cx + radius * Math.cos(angle), bend.cy + radius * Math.sin(angle));
      }
    }
    line.bendLast = line.p.length - 1;
    if (Math.abs(endX - bend.cx) > 0.5) powerFlowAdd(line, endX, lane);
    return line;
  }

  // `top`: the header's room (flow_geometry.h header_top), the flow below it.
  function powerFlowLayout(width, height, m, top = 0) {
    const ox = width / 2, oy = (top + height) / 2;
    const l = { width, height, ring: m.ring, disc: m.disc, node: {
      pv: [ox, top + m.insetY + m.ring], grid: [m.insetX + m.ring, oy],
      home: [width - m.insetX - m.ring, oy], battery: [ox, height - m.insetY - m.ring] } };
    const spacing = Math.min(m.spacing, m.disc * 0.8);
    const gridHome = powerFlowPolyline();
    powerFlowAdd(gridHome, l.node.grid[0] + m.disc, oy);
    powerFlowAdd(gridHome, l.node.home[0] - m.disc, oy);
    const pvBattery = powerFlowPolyline();
    powerFlowAdd(pvBattery, ox, l.node.pv[1] + m.disc);
    powerFlowAdd(pvBattery, ox, l.node.battery[1] - m.disc);
    l.lines = {
      gridHome, pvBattery,
      pvHome: powerFlowLeg(l, 'pv', 1, 'home', oy - spacing, spacing),
      pvGrid: powerFlowLeg(l, 'pv', -1, 'grid', oy - spacing, spacing),
      batHome: powerFlowLeg(l, 'battery', 1, 'home', oy + spacing, spacing),
      batGrid: powerFlowLeg(l, 'battery', -1, 'grid', oy + spacing, spacing),
    };
    return l;
  }

  function powerFlowPointAt(line, distance) {
    const n = line.p.length;
    if (!n) return [0, 0];
    if (distance <= 0) return line.p[0];
    for (let i = 1; i < n; ++i) {
      if (distance > line.along[i]) continue;
      const piece = line.along[i] - line.along[i - 1];
      const t = piece > 0 ? (distance - line.along[i - 1]) / piece : 0;
      return [line.p[i - 1][0] + (line.p[i][0] - line.p[i - 1][0]) * t,
        line.p[i - 1][1] + (line.p[i][1] - line.p[i - 1][1]) * t];
    }
    return line.p[n - 1];
  }

  // ----- Flows: flow_model.h ------------------------------------------------
  const POWER_FLOW_IDLE_WATTS = 10;

  function powerFlowCompute(input) {
    const pos = v => v > 0 ? v : 0;
    const solar = input.hasPv ? pos(input.pv) : 0;
    const gridIn = input.hasGrid ? pos(input.grid) : 0;
    const gridOut = input.hasGrid ? pos(-input.grid) : 0;
    const batOut = input.hasBattery ? pos(input.battery) : 0;
    const batIn = input.hasBattery ? pos(-input.battery) : 0;
    const pvGrid = Math.min(solar, gridOut);
    const batGrid = Math.min(batOut, gridOut - pvGrid);
    const solarLeft = solar - pvGrid;
    const pvBattery = Math.min(solarLeft, batIn);
    const gridBattery = Math.min(gridIn, pos(batIn - pvBattery));
    const pvHome = solarLeft - pvBattery;
    const gridHome = gridIn - gridBattery;
    const batHome = batOut - batGrid;
    const idle = v => v >= POWER_FLOW_IDLE_WATTS ? v : 0;
    const gridToBattery = gridBattery > batGrid;
    const into = [['pv', pvHome], ['grid', gridHome], ['battery', batHome]];
    let best = POWER_FLOW_IDLE_WATTS, homeSource = '';
    for (const [node, watts] of into) if (watts >= best) { best = watts; homeSource = node; }
    return {
      watts: { gridHome: idle(gridHome), pvBattery: idle(pvBattery), pvHome: idle(pvHome), pvGrid: idle(pvGrid),
        batHome: idle(batHome), batGrid: idle(gridToBattery ? gridBattery : batGrid) },
      gridToBattery,
      home: input.hasHome ? pos(input.home) : pvHome + gridHome + batHome,
      homeKnown: input.hasHome || input.hasPv || input.hasGrid || input.hasBattery,
      homeSource,
    };
  }

  function powerFlowUnitFactor(unit) {
    const u = String(unit || '').trim();
    if (/^[kK]W$/.test(u)) return 1000;
    if (u === 'MW') return 1e6;
    if (u === 'GW') return 1e9;
    if (u === 'mW') return 0.001;
    return 1;
  }

  function powerFlowNumber(value) {
    const text = String(value ?? '').trim().replace(',', '.');
    if (!text) return null;
    const number = Number.parseFloat(text);
    return Number.isFinite(number) ? number : null;
  }

  // A power's amount and unit, as the panel shows them (renderer.cpp
  // show_power): the unit a step smaller after it (.tile-unit).
  function powerFlowPower(watts) {
    const a = Math.abs(watts);
    if (a >= 999950) return [formatLocalizedNumber(a / 1e6, 1), 'MW'];
    if (a >= 999.5) return [formatLocalizedNumber(a / 1000, 1), 'kW'];
    return [formatLocalizedNumber(a, 0), 'W'];
  }

  // While the battery charges: Home Assistant's charging icon for its level,
  // as on the panel (renderer.cpp charging_icon).
  function powerFlowChargingIcon(level) {
    if (level === null) return 'battery-charging';
    if (level < 10) return 'battery-charging-outline';
    return 'battery-charging-' + Math.min(10, Math.round(level / 10)) * 10;
  }

  // ----- Preview --------------------------------------------------------------
  const POWER_FLOW_COLORS = Object.freeze({ pv: '#FF9800', grid: '#488FC2', gridReturn: '#8353D1', battery: '#F06292',
    batteryOut: '#4DB6AC', header: '#F29C06' });

  function powerFlowHexRgb(hex) {
    return [1, 3, 5].map(i => parseInt(String(hex).slice(i, i + 2), 16));
  }

  // The plain tile disc of an icon color on the card (tile_icon_disc::tile_fill).
  function powerFlowDisc(card, hex) {
    const rgb = powerFlowHexRgb(hex);
    const glowRaw = String(getComputedStyle(document.documentElement).getPropertyValue('--icon-glow-pct')).trim();
    const glowValue = glowRaw === '' ? 25 : Number(glowRaw);
    const glow = Math.min(100, Math.max(0, Number.isFinite(glowValue) ? glowValue : 25));
    if (panelLight()) {
      const share = Number(panelVar('--pv-light-circle', '0.8'));
      const amount = glow * (Number.isFinite(share) ? share : 0.8) / 100;
      return toneHex(toneBlend(card, toneReadableIcon(rgb), Math.round(Math.min(1, amount) * 255)));
    }
    return toneHex(toneFill(card, rgb, true, glow, false).disc);
  }

  // Draws the tile like render_power_flow_tile() in display pixels, scaled
  // to the preview tile. `tile`: flow_entities, key_code, span_w, span_h,
  // title, icon_name.
  function applyPowerFlowPreview(el, tile, meta) {
    if (!el || typeof POWER_FLOW_LAYOUT === 'undefined') return;
    const L = POWER_FLOW_LAYOUT;
    el.querySelectorAll(':scope > .tile-icon, :scope > .tile-title, :scope > .tile-value, :scope > .power-flow-preview')
      .forEach(node => node.remove());
    const rootStyle = getComputedStyle(el.isConnected ? el : document.documentElement);
    const scale = parseFloat(rootStyle.getPropertyValue('--radius-preview-scale')) || 0.5;
    const spanW = Math.max(1, Number(tile?.span_w) || 1);
    const spanH = Math.max(1, Number(tile?.span_h) || 1);
    const cellW = parseFloat(rootStyle.getPropertyValue('--preview-cell-w'));
    const cellH = parseFloat(rootStyle.getPropertyValue('--preview-cell-h'));
    const previewGap = parseFloat(rootStyle.getPropertyValue('--preview-gap')) || 0;
    const width = (cellW > 0 ? spanW * (cellW + previewGap) - previewGap : el.offsetWidth) / scale;
    const height = (cellH > 0 ? spanH * (cellH + previewGap) - previewGap : el.offsetHeight) / scale;
    const bits = Number(tile?.key_code) || 0;
    const onCard = (bits & 2) === 0;
    el.classList.add('power-flow-tile');
    el.classList.toggle('power-flow-no-card', !onCard);
    if (!onCard) el.style.setProperty('--tile-border-tint', 'transparent');

    const lines = powerFlowLines(tile?.flow_entities);
    const entity = { pv: lines[0] || '', grid: lines[1] || '', bat: lines[2] || '', charge: lines[3] || '', home: lines[4] || '' };
    const values = meta?.values || {}, units = meta?.units || {}, icons = meta?.icons || {};
    const watts = id => {
      if (!id) return null;
      const n = powerFlowNumber(values[id]);
      return n === null ? null : n * powerFlowUnitFactor(units[id]);
    };
    const pv = watts(entity.pv), grid = watts(entity.grid), bat = watts(entity.bat), home = watts(entity.home);
    const input = { hasPv: pv !== null, hasGrid: grid !== null, hasBattery: bat !== null, hasHome: home !== null,
      pv: pv || 0, grid: ((bits & 4) ? -1 : 1) * (grid || 0), battery: ((bits & 8) ? -1 : 1) * (bat || 0), home: home || 0 };
    const flows = powerFlowCompute(input);

    const l = powerFlowLayout(width, height, L, (bits & 1) ? 2 * L.ring - L.insetY : 0);
    const light = panelLight();
    const cardHex = String(rootStyle.getPropertyValue('--tile-card') || '').trim() || (light ? '#FFFFFF' : '#1A1A1A');
    const cardRgb = /^#[0-9a-f]{6}$/i.test(cardHex) ? powerFlowHexRgb(cardHex) : (light ? [255, 255, 255] : [26, 26, 26]);
    const idle = !light ? '#3C3C3C' : onCard ? '#DCDEE2' : '#CDD0D5';
    const text = panelRoleHex('text', light ? '#1A1A1A' : '#FFFFFF');
    // The home joins with the first sensor: a tile without any shows no node
    // (user 2026-10-26).
    const shown = { pv: !!entity.pv, grid: !!entity.grid, battery: !!(entity.bat || entity.charge),
      home: Object.values(entity).some(Boolean) };
    const lineShown = { gridHome: shown.grid, pvBattery: shown.pv && shown.battery, pvHome: shown.pv,
      pvGrid: shown.pv && shown.grid, batHome: shown.battery, batGrid: shown.battery && shown.grid };
    const source = { gridHome: 'grid', pvBattery: 'pv', pvHome: 'pv', pvGrid: 'pv', batHome: 'battery',
      batGrid: flows.gridToBattery ? 'grid' : 'battery' };
    // A node's color now, like the panel (renderer.cpp node_color): the grid
    // purple while it takes the feed-in, the battery magenta only while it
    // charges.
    const colorOf = { pv: POWER_FLOW_COLORS.pv,
      grid: input.hasGrid && -input.grid >= POWER_FLOW_IDLE_WATTS ? POWER_FLOW_COLORS.gridReturn : POWER_FLOW_COLORS.grid,
      battery: input.hasBattery && -input.battery >= POWER_FLOW_IDLE_WATTS ? POWER_FLOW_COLORS.battery : POWER_FLOW_COLORS.batteryOut };
    const r = n => Math.round(n);
    let svg = '';
    // Straight pieces and the bend as a true circle arc, like the panel.
    const path = line => {
      const points = line.bend ? [...line.p.slice(0, line.bendFirst + 1), ...line.p.slice(line.bendLast)] : line.p;
      let d = '';
      points.forEach((p, i) => {
        if (line.bend && i === line.bendFirst + 1) {
          const b = line.bend;
          d += ' A' + b.radius + ' ' + b.radius + ' 0 0 ' + (b.toAngle > b.fromAngle ? 1 : 0) + ' ' + r(p[0]) + ' ' + r(p[1]);
          return;
        }
        d += (i ? ' L' : 'M') + r(p[0]) + ' ' + r(p[1]);
      });
      return d;
    };
    const live = name => lineShown[name] && flows.watts[name] > 0;
    for (const pass of [false, true]) {
      for (const name of Object.keys(l.lines)) {
        if (!lineShown[name] || live(name) !== pass) continue;
        const color = pass ? colorOf[source[name]] : idle;
        svg += '<path d="' + path(l.lines[name]) + '" fill="none" stroke="' + color + '" stroke-width="' + L.lineWidth +
          '" stroke-linecap="round" stroke-linejoin="round"/>';
      }
    }
    // One dot halfway along every flow (the device moves it).
    for (const name of Object.keys(l.lines)) {
      if (!live(name)) continue;
      const line = l.lines[name];
      const p = powerFlowPointAt(line, line.along[line.along.length - 1] / 2);
      svg += '<circle cx="' + r(p[0]) + '" cy="' + r(p[1]) + '" r="' + L.dotRadius + '" fill="' + colorOf[source[name]] + '"/>';
    }
    // The global frame line (display-borders.js marks the grid) on the rings
    // only without a card.
    const frame = !onCard && !!el.closest?.('.tile-grid')?.classList.contains('tiles-bordered');
    const frameColor = light ? '#DDDDDD' : '#292929';
    let html = '';
    // A node is the corner of a 1x1 tile (renderer.cpp create_node): the
    // tile's corner disc in a ring shaped like the card corner.
    const square = (cx, cy, half, corner, attributes) => {
      const side = 2 * half;
      return '<rect x="' + (r(cx) - half) + '" y="' + (r(cy) - half) + '" width="' + side + '" height="' + side +
        '" rx="' + Math.min(corner, half) + '" ' + attributes + '/>';
    };
    const node = (cx, cy, hex, icon, mirrored = false) => {
      svg += square(cx, cy, L.ring, L.ringCorner, 'fill="' + toneHex(cardRgb) + '"');
      if (frame) {
        svg += square(cx, cy, L.ring - 0.5, L.ringCorner - 0.5, 'fill="none" stroke="' + frameColor + '" stroke-width="1"');
      }
      svg += square(cx, cy, L.disc, L.discCorner, 'fill="' + powerFlowDisc(cardRgb, hex) + '"');
      html += '<i class="mdi mdi-' + escapeHtml(icon) + '" style="position:absolute;left:' + r(cx - L.iconPx / 2) +
        'px;top:' + r(cy - L.iconPx / 2) + 'px;width:' + L.iconPx + 'px;height:' + L.iconPx + 'px;font-size:' + L.iconPx +
        'px;line-height:' + L.iconPx + 'px;text-align:center;color:' + hex + (mirrored ? ';transform:scaleX(-1)' : '') +
        '"></i>';
    };
    // `parts`: the amount and its unit ('' for none).
    const label = (x, w, cy, parts, align) => {
      html += '<div style="position:absolute;left:' + r(x) + 'px;top:' + r(cy - L.valueLine / 2) + 'px;width:' + r(w) +
        'px;height:' + L.valueLine + 'px;line-height:' + L.valueLine + 'px;font-size:' + L.valuePx + 'px;text-align:' +
        align + ';color:' + text + ';white-space:nowrap;overflow:hidden">' + escapeHtml(parts[0]) +
        (parts[1] ? '<span class="tile-unit">' + escapeHtml(parts[1]) + '</span>' : '') + '</div>';
    };
    const power = (known, watts) => known ? powerFlowPower(watts) : ['--', ''];
    const iconOf = id => String(icons[id] || '').replace(/^mdi:/, '');
    const homeColor = colorOf[flows.homeSource || 'grid'];
    if (shown.pv) node(l.node.pv[0], l.node.pv[1], POWER_FLOW_COLORS.pv, 'solar-power');
    // The grid's direction past the idle threshold, as on the panel: while
    // it supplies "export" (the arrow leaves the mast towards the lines),
    // while feeding in "import" mirrored (the arrow comes from the lines).
    const supplying = input.hasGrid && input.grid >= POWER_FLOW_IDLE_WATTS;
    const feeding = input.hasGrid && -input.grid >= POWER_FLOW_IDLE_WATTS;
    const gridIcon = supplying ? 'transmission-tower-export' : feeding ? 'transmission-tower-import' : 'transmission-tower';
    if (shown.grid) node(l.node.grid[0], l.node.grid[1], colorOf.grid, gridIcon, feeding);
    if (shown.home) node(l.node.home[0], l.node.home[1], homeColor, 'home-lightning-bolt');
    const level = entity.charge ? powerFlowNumber(values[entity.charge]) : null;
    const charging = input.hasBattery && -input.battery >= POWER_FLOW_IDLE_WATTS;
    if (shown.battery) node(l.node.battery[0], l.node.battery[1], colorOf.battery,
      charging ? powerFlowChargingIcon(level) : iconOf(entity.charge || entity.bat) || 'battery');

    const gap = r(l.ring) + L.valueGap;
    const below = gap + L.valueBelow - (L.valueLine - L.valueBase) + L.valueLine / 2;
    if (shown.pv) label(r(l.node.pv[0]) + gap, width - L.edge - r(l.node.pv[0]) - gap, l.node.pv[1],
      power(input.hasPv, pv), 'left');
    if (shown.grid) label(L.edge, 2 * (r(l.node.grid[0]) - L.edge), l.node.grid[1] + below,
      power(input.hasGrid, grid), 'center');
    const homeHalf = r(width - l.node.home[0]) - L.edge;
    const homeKnown = entity.home ? input.hasHome : flows.homeKnown;
    if (shown.home) label(r(l.node.home[0]) - homeHalf, 2 * homeHalf, l.node.home[1] + below,
      power(homeKnown, flows.home), 'center');
    if (shown.battery) {
      label(r(l.node.battery[0]) + gap, width - L.edge - r(l.node.battery[0]) - gap, l.node.battery[1],
        power(input.hasBattery, bat), 'left');
      const charge = powerFlowNumber(values[entity.charge]);
      label(L.edge, r(l.node.battery[0]) - gap - L.edge, l.node.battery[1],
        entity.charge && charge !== null ? [formatLocalizedNumber(charge, 0), '%'] : ['--', ''], 'right');
    }
    if (bits & 1) {
      node(l.ring, l.ring, POWER_FLOW_COLORS.header,
        normalizeMdiIconName(tile?.icon_name || '') || 'lightning-bolt');
      if (tile?.title) {
        const right = width - L.titleRight;
        const left = r(l.ring * 2) + L.valueGap;
        html += '<div style="position:absolute;left:' + r(left) + 'px;top:' + r(L.titleTop) + 'px;width:' +
          r(right - left) + 'px;line-height:' + L.valueLine + 'px;font-size:' + L.valuePx + 'px;text-align:right;color:' +
          text + ';white-space:nowrap;overflow:hidden;text-overflow:ellipsis">' + escapeHtml(tile.title) + '</div>';
      }
    }
    el.insertAdjacentHTML('afterbegin', '<div class="power-flow-preview" style="position:absolute;left:0;top:0;width:' +
      width.toFixed(2) + 'px;height:' + height.toFixed(2) + 'px;transform:scale(' + scale + ');transform-origin:0 0;' +
      'pointer-events:none"><svg width="' + width.toFixed(2) + '" height="' + height.toFixed(2) +
      '" style="position:absolute;left:0;top:0">' + svg + '</svg>' + html + '</div>');
  }

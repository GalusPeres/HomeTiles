// Weather graph tile: two linked halves, today and tomorrow, each a whole
// 00-23 day. The panel draws a temperature line and a condition band; the Web
// Admin preview mirrors it with a small SVG. Numeric precipitation is not
// available from the Yandex integration, so only the condition band is drawn.
// Passed hours of today come from history entries with "src":"hist" and merge
// with the forecast the same way as the panel (graph_renderer.cpp).

function weatherGraphDay(value) {
  return String(value) === '1' ? '1' : '0';
}

function weatherGraphNewPairId() {
  let id = 100;
  // 100 is the Tile default of sensor_gauge_max, so it never names a pair.
  while (id === 100) id = 1 + Math.floor(Math.random() * 65534);
  return String(id);
}

function weatherGraphPairValue(value) {
  const pair = Number(value);
  return Number.isFinite(pair) && pair > 0 && pair <= 0xFFFF ? pair : 0;
}

function weatherGraphEntityInput(tab) {
  return document.getElementById(tab + '_weather_graph_entity');
}

function weatherGraphDayInput(tab) {
  return document.getElementById(tab + '_weather_graph_day');
}

function weatherGraphPairInput(tab) {
  return document.getElementById(tab + '_weather_graph_pair_id');
}

function weatherGraphPairFor(tab, entity, pairId, exceptIndex) {
  const tiles = (typeof tilesData === 'object' && tilesData && tilesData[tab]) || [];
  for (let i = 0; i < tiles.length; i++) {
    if (i === exceptIndex) continue;
    const tile = tiles[i];
    if (!tile || Number(tile.type) !== 30) continue;
    const otherPair = Number(tile.sensor_gauge_max ?? tile.weather_graph_pair_id);
    if (pairId && otherPair === Number(pairId)) return i;
    if (!pairId && entity && String(tile.sensor_entity || '') === entity) return i;
  }
  return -1;
}

function loadWeatherGraphFields(tab, data) {
  const prefix = tab;
  const entity = String((data && (data.sensor_entity || data.weather_entity)) || '');
  const entityInput = weatherGraphEntityInput(prefix);
  if (entityInput) entityInput.value = entity;
  let pair = weatherGraphPairValue(data && (data.sensor_gauge_max ?? data.weather_graph_pair_id));
  // sensor_gauge_max starts at 100 for a tile that was never saved: it is not
  // a pair unless a partner still carries it.
  if (pair === 100 && weatherGraphPairFor(tab, entity, 100, currentTileIndex) < 0) pair = 0;
  if (!pair) {
    const other = weatherGraphPairFor(tab, entity, 0, currentTileIndex);
    if (other >= 0) {
      const tiles = tilesData?.[tab] || [];
      const otherTile = tiles[other];
      if (otherTile && Number(otherTile.sensor_gauge_max ?? otherTile.weather_graph_pair_id) > 0) {
        pair = Number(otherTile.sensor_gauge_max ?? otherTile.weather_graph_pair_id);
      }
    }
  }
  const pairInput = weatherGraphPairInput(prefix);
  if (pairInput) pairInput.value = String(pair || 0);
  const dayInput = weatherGraphDayInput(prefix);
  if (dayInput) dayInput.value = weatherGraphDay(data && data.sensor_display_mode);
  if (dayInput && !dayInput.dataset.weatherGraphBound) {
    dayInput.dataset.weatherGraphBound = '1';
    dayInput.addEventListener('change', () => {
      if (typeof updateTilePreview === 'function') updateTilePreview(currentTileTab);
      if (typeof updateDraft === 'function') updateDraft(currentTileTab);
      if (typeof scheduleAutoSave === 'function') scheduleAutoSave(currentTileTab);
      if (typeof syncWeatherGraphPairLocal === 'function') {
        syncWeatherGraphPairLocal(currentTileTab, currentTileIndex);
      }
    });
  }
  if (typeof syncSwitchChoices === 'function') syncSwitchChoices(prefix);
}

function saveWeatherGraphFields(tab, formData) {
  const entityInput = weatherGraphEntityInput(tab);
  const entity = entityInput ? entityInput.value : '';
  const dayInput = weatherGraphDayInput(tab);
  const day = weatherGraphDay(dayInput ? dayInput.value : 0);
  let pairInput = weatherGraphPairInput(tab);
  let pair = weatherGraphPairValue(pairInput ? pairInput.value : 0);
  if (pair === 100 && weatherGraphPairFor(tab, entity, 100, currentTileIndex) < 0) pair = 0;
  if (!pair && entity) {
    const other = weatherGraphPairFor(tab, entity, 0, currentTileIndex);
    if (other >= 0) {
      const otherTile = (tilesData?.[tab] || [])[other];
      if (otherTile && Number(otherTile.sensor_gauge_max ?? otherTile.weather_graph_pair_id) > 0) {
        pair = Number(otherTile.sensor_gauge_max ?? otherTile.weather_graph_pair_id);
      }
    }
  }
  if (!pair) pair = Number(weatherGraphNewPairId());
  if (pairInput) pairInput.value = String(pair);
  formData.append('weather_graph_entity', entity);
  formData.append('weather_graph_day', day);
  formData.append('weather_graph_pair_id', String(pair));
  // Keep the visible half of the pair in step without waiting for a reload.
  if (typeof syncWeatherGraphPairLocal === 'function') syncWeatherGraphPairLocal(tab, currentTileIndex);
}

function resetWeatherGraphFields(tab) {
  const entityInput = weatherGraphEntityInput(tab);
  if (entityInput) entityInput.value = '';
  const dayInput = weatherGraphDayInput(tab);
  if (dayInput) dayInput.value = '0';
  const pairInput = weatherGraphPairInput(tab);
  if (pairInput) pairInput.value = '0';
  if (typeof syncSwitchChoices === 'function') syncSwitchChoices(tab);
  const host = document.getElementById(tab + '_tile_graph');
  if (host) host.innerHTML = '';
}

// Keeps the partner tile's shared fields in the browser. The server mirrors
// the saved tile too, so a reload and another device see the same values.
function syncWeatherGraphPairLocal(tab, index) {
  if (index < 0 || typeof tilesData !== 'object' || !tilesData) return;
  const entityInput = weatherGraphEntityInput(tab);
  const pairInput = weatherGraphPairInput(tab);
  const dayInput = weatherGraphDayInput(tab);
  const iconInput = document.getElementById(tab + '_tile_icon');
  if (!entityInput || !pairInput) return;
  const entity = entityInput.value;
  const pair = Number(pairInput.value);
  if (!entity || !pair) return;
  const other = weatherGraphPairFor(tab, entity, pair, index);
  if (other < 0) return;
  const tiles = tilesData[tab] || [];
  const tile = Object.assign({}, tiles[other] || {});
  tile.type = 30;
  tile.sensor_entity = entity;
  tile.sensor_gauge_max = pair;
  tile.sensor_display_mode = Number(dayInput.value) === 1 ? 0 : 1;
  // The name stays per half (graph_renderer.cpp graph_title): the partner
  // keeps its own title.
  if (iconInput) tile.icon_name = iconInput.value;
  tiles[other] = tile;
  if (typeof renderTileFromData === 'function') {
    renderTileFromData(tab, other, tile, typeof sensorMetaCache === 'object' ? sensorMetaCache : null);
  }
}

function weatherGraphLocalDate(date) {
  const y = date.getFullYear();
  const m = String(date.getMonth() + 1).padStart(2, '0');
  const d = String(date.getDate()).padStart(2, '0');
  return y + '-' + m + '-' + d;
}

function weatherGraphHour(entry) {
  const raw = entry.hour_local ?? entry.h;
  if (raw !== undefined && raw !== null) {
    const value = Number(raw);
    if (Number.isFinite(value) && value >= 0 && value <= 23) return Math.round(value);
  }
  const datetime = String(entry.datetime || '');
  if (datetime.length >= 13) {
    const value = Number(datetime.slice(11, 13));
    if (Number.isFinite(value)) return value;
  }
  return -1;
}

function weatherGraphNumber(entry, keys) {
  for (const key of keys) {
    const value = Number(entry?.[key]);
    if (Number.isFinite(value)) return value;
  }
  return null;
}

function weatherGraphColor(condition, icon, precip) {
  const text = (String(condition || '') + ' ' + String(icon || '')).toLowerCase();
  if (text.includes('thunder') || text.includes('lightning')) return '#9b5cff';
  if (text.includes('hail')) return '#d85cff';
  if (text.includes('snow') || text.includes('sleet')) return '#8fd3ff';
  if (text.includes('rain') || text.includes('showers') || text.includes('pouring') ||
      text.includes('drizzle')) return '#3d8bfd';
  // No rain wording, but the payload counts millimetres: rain by number, so
  // the cell colour and the episode label always agree.
  if (Number(precip) > 0) return '#3d8bfd';
  if (text.includes('fog')) return '#7a7a7a';
  if (text.includes('cloudy') || text.includes('overcast')) return '#4a4a4a';
  return '#2e2e2e';
}

function applyWeatherGraphPreview(el, tile, sensorMeta) {
  if (!el) return;
  let host = el.querySelector('.tile-weather-graph');
  if (!host) {
    // The cached-grid preview builds its own DOM; the live editor receives the
    // server's placeholder. Both end up with the same host.
    host = document.createElement('div');
    host.className = 'tile-weather-graph';
    el.appendChild(host);
  }
  const raw = tile.sensor_entity && sensorMeta
    ? ((sensorMeta.weatherGraphValues && sensorMeta.weatherGraphValues[tile.sensor_entity]) ||
       (sensorMeta.weatherValues && sensorMeta.weatherValues[tile.sensor_entity]) || '')
    : '';
  let data = null;
  try { data = raw ? (typeof raw === 'string' ? JSON.parse(raw) : raw) : null; } catch (_) { data = null; }
  const hourly = data && Array.isArray(data.forecast_hourly) ? data.forecast_hourly : [];
  const now = new Date();
  const today = weatherGraphLocalDate(now);
  const tomorrowDate = new Date(now.getTime() + 86400000);
  const tomorrow = weatherGraphLocalDate(tomorrowDate);
  const day = Number(tile.sensor_display_mode) === 1 ? 1 : 0;
  // The day's 24 hourly slots, 00 to 23, exactly like the panel
  // (graph_renderer.cpp): an hour the payload does not carry stays empty and
  // the line starts where the data starts, on the same axis as the other half.
  // Merge duplicates of the same date+hour like the panel: hist fills the
  // past, forecast covers the present and future.
  const isPastSlot = (date, hour) => date < today || (date === today && hour < now.getHours());
  const bySlot = new Map();
  for (const entry of hourly) {
    if (!entry || typeof entry !== 'object') continue;
    let date = String(entry.date_local || entry.d || '');
    if (!date) date = String(entry.datetime || '').slice(0, 10);
    const hour = weatherGraphHour(entry);
    if (!date || hour < 0 || hour > 23) continue;
    const key = date + ' ' + String(hour).padStart(2, '0');
    const item = {
      hour,
      temp: weatherGraphNumber(entry, ['temperature', 't', 'native_temperature']),
      precip: weatherGraphNumber(entry, ['precipitation', 'p']),
      // The same key order as the panel: the bridge may flatten the entries.
      condition: entry.condition || entry.c || entry.state || '',
      icon: entry.icon || entry.i || '',
      isHist: String(entry.src || '').toLowerCase() === 'hist'
    };
    const old = bySlot.get(key);
    if (!old) {
      bySlot.set(key, item);
      continue;
    }
    if (item.isHist !== old.isHist) {
      const past = isPastSlot(date, hour);
      if (item.isHist && !old.isHist) {
        if (past) bySlot.set(key, item);
      } else if (!item.isHist && old.isHist) {
        if (!past) bySlot.set(key, item);
      }
      continue;
    }
    // Same source twice: fill what the first entry missed, keep the first
    // otherwise so the result stays deterministic.
    if ((old.temp === null && item.temp !== null) ||
        (!old.condition && item.condition)) {
      bySlot.set(key, item);
    }
  }
  const slots = new Array(24).fill(null);
  for (const [key, item] of bySlot) {
    const date = key.slice(0, 10);
    if (day === 0 ? date !== today : date !== tomorrow) continue;
    slots[item.hour] = item;
  }
  const rows = slots.filter(Boolean);
  // The shared Y range of the pair: today's and tomorrow's temperatures, over
  // the merged slots like the panel's compute_scale (not the raw entries, so
  // duplicates never widen the range).
  const scaleRows = [];
  for (const [key, item] of bySlot) {
    const date = key.slice(0, 10);
    if (item.temp === null) continue;
    if (date === today || date === tomorrow) scaleRows.push(item.temp);
  }
  const hasTemp = scaleRows.length > 0;
  let low = hasTemp ? Math.min(...scaleRows) : 0;
  let high = hasTemp ? Math.max(...scaleRows) : 30;
  if (high - low < 2) high = low + 2;
  const pad = Math.max(1, (high - low) * 0.15);
  low -= pad;
  high += pad;
  if (!rows.length && !hasTemp) {
    host.innerHTML = '<span class="weather-graph-empty">--</span>';
    return;
  }
  const width = host.clientWidth || 240;
  const height = host.clientHeight || 120;
  const top = Math.max(10, height * 0.08);
  const graphBottom = height - Math.max(18, height * 0.16);
  // The band carries the precipitation names, like the panel (graph_box).
  const bandTop = height - Math.max(12, height * 0.09);
  const font = Math.max(7, Math.round(height * 0.075));
  // The readouts are the biggest numbers of the panel (FONT_VALUE).
  const valueFont = Math.max(font + 4, Math.round(height * 0.13));
  // The x of an hour on the fixed 00-23 axis.
  const xOfHour = hour => (hour / 23) * width;
  const cellW = width / 24;
  const yOfTemp = temp => top + ((high - temp) / (high - low)) * (graphBottom - top);
  // A smooth line, sampled four times per hour with cosine easing between the
  // hourly points, exactly like the panel's chart (graph_renderer.cpp).
  // Cosine stays inside [p1, p2]: no overshoot on steps, no dips on flats.
  const CURVE_SUB = 4;
  const curvePoints = 24 + 23 * (CURVE_SUB - 1);
  const present = slots.map(row => !!row && row.temp !== null);
  const samplePts = new Array(curvePoints).fill(null);
  for (let hour = 0; hour < 24; hour++) {
    if (!present[hour]) continue;
    const at = hour * CURVE_SUB;
    samplePts[at] = {x: xOfHour(hour), y: yOfTemp(slots[hour].temp)};
    if (hour + 1 >= 24 || !present[hour + 1]) continue;
    const p1 = slots[hour].temp;
    const p2 = slots[hour + 1].temp;
    for (let s = 1; s < CURVE_SUB; s++) {
      const t = s / CURVE_SUB;
      const blend = 0.5 - 0.5 * Math.cos(Math.PI * t);
      samplePts[at + s] = {x: xOfHour(hour + t), y: yOfTemp(p1 + (p2 - p1) * blend)};
    }
  }
  // A gap the payload did not carry breaks the line, like LV_CHART_POINT_NONE.
  const runs = [];
  let run = [];
  for (const point of samplePts) {
    if (point) run.push(point);
    else if (run.length) { runs.push(run); run = []; }
  }
  if (run.length) runs.push(run);
  const lineSvg = runs.filter(points => points.length > 1).map(points =>
    '<polyline points="' + points.map(point =>
      point.x.toFixed(2) + ',' + point.y.toFixed(2)).join(' ') +
    '" fill="none" stroke="currentColor" stroke-width="1.8" ' +
    'stroke-linecap="round" stroke-linejoin="round"/>').join('');
  // A slot the payload did not carry stays a dry cell, so the band is whole.
  const band = slots.map((row, hour) => {
    const fill = row ? weatherGraphColor(row.condition, row.icon, row.precip) : '#2e2e2e';
    return '<rect x="' + (hour * cellW).toFixed(2) + '" y="' + bandTop.toFixed(2) +
      '" width="' + (cellW + 0.2).toFixed(2) + '" height="' + Math.max(6, height - bandTop).toFixed(2) +
      '" fill="' + fill + '"/>';
  }).join('');
  // Hour marks under the scale: 00, 03, ... 21, every three hours.
  const hourSvg = [];
  for (let hour = 0; hour < 24; hour += 3) {
    const half = font;
    let x = xOfHour(hour);
    if (x < half) x = half;
    if (x > width - half) x = width - half;
    hourSvg.push('<text x="' + x.toFixed(2) + '" y="' + (bandTop - 2).toFixed(2) +
      '" fill="currentColor" font-size="' + font + '" text-anchor="middle">' +
      String(hour).padStart(2, '0') + '</text>');
  }
  // The temperature above the line, every three hours.
  // The curve's place, so a readout never sits on the line: above its point,
  // and below it when the curve crosses its box (graph_renderer.cpp).
  const gap = Math.max(3, Math.round(valueFont * 0.25));
  const lineCrosses = (x0, x1, y0, y1) => {
    for (let i = 0; i + 1 < curvePoints; i++) {
      const a = samplePts[i];
      const b = samplePts[i + 1];
      if (!a || !b) continue;
      const ov0 = Math.max(Math.min(a.x, b.x), x0);
      const ov1 = Math.min(Math.max(a.x, b.x), x1);
      if (ov0 > ov1) continue;
      const ya = a.x === b.x ? a.y : a.y + (b.y - a.y) * (ov0 - a.x) / (b.x - a.x);
      const yb = a.x === b.x ? a.y : a.y + (b.y - a.y) * (ov1 - a.x) / (b.x - a.x);
      const lo = Math.min(ya, yb);
      const hi = Math.max(ya, yb);
      if (hi < y0 || lo > y1) continue;
      return true;
    }
    return false;
  };
  const tempSvg = [];
  for (let hour = 0; hour < 24; hour += 3) {
    const row = slots[hour];
    if (!row || row.temp === null) continue;
    const text = Math.round(row.temp) + '\u00b0';
    const half = text.length * valueFont * 0.62 / 2;
    let x = xOfHour(hour);
    if (x < half) x = half;
    if (x > width - half) x = width - half;
    const pointY = top + ((high - row.temp) / (high - low)) * (graphBottom - top);
    let y = pointY - gap;
    if (y - valueFont < top) y = top + valueFont;
    if (lineCrosses(x - half, x + half, y - valueFont, y)) {
      const below = pointY + gap + valueFont;
      if (below <= graphBottom) y = below;
    }
    tempSvg.push('<text x="' + x.toFixed(2) + '" y="' + y.toFixed(2) +
      '" fill="currentColor" font-size="' + valueFont + '" text-anchor="middle">' +
      text + '</text>');
  }
  const group = row => {
    const text = (String(row.condition || '') + ' ' + String(row.icon || '')).toLowerCase();
    if (text.includes('thunder') || text.includes('lightning')) return 'lightning';
    if (text.includes('hail')) return 'hail';
    if (text.includes('snow') || text.includes('sleet')) return 'snowy';
    if (text.includes('rain') || text.includes('showers') ||
        text.includes('drizzle') || text.includes('pouring')) return 'rainy';
    // No wording, but the payload counts millimetres: rain by number.
    if (Number(row.precip) > 0) return 'rainy';
    return null;
  };
  const episodes = [];
  let episodeStart = 0;
  let episodeGroup = slots[0] ? group(slots[0]) : null;
  for (let i = 1; i <= 24; i++) {
    const next = i < 24 && slots[i] ? group(slots[i]) : null;
    if (next !== episodeGroup) {
      if (episodeGroup) episodes.push({start: episodeStart, end: i, key: episodeGroup});
      episodeStart = i;
      episodeGroup = next;
    }
  }
  const conditions = (typeof WEATHER_I18N === 'object' && WEATHER_I18N && WEATHER_I18N.conditions) || {};
  const svgEscape = value => String(value).replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
  const episodeSvg = episodes.map(episode => {
    const name = String(conditions[episode.key] || episode.key.replace(/-/g, ' '));
    const w = (episode.end - episode.start) * cellW;
    const textW = name.length * font * 0.62;
    // A one-hour episode in a wide word stays a colored cell alone.
    if (textW > w + cellW * 0.5) return '';
    const half = textW / 2;
    let cx = episode.start * cellW + w / 2;
    if (cx < half) cx = half;
    if (cx > width - half) cx = width - half;
    return '<text x="' + cx.toFixed(2) + '" y="' + (bandTop + font - 1).toFixed(2) +
      '" fill="#141414" font-size="' + font + '" text-anchor="middle">' +
      svgEscape(name) + '</text>';
  }).join('');
  // The panel title is the tile's own name when it has one, otherwise the
  // day alone (graph_renderer.cpp graph_title).
  const named = String(tile.title || '').trim();
  const titleText = named || (day === 1
    ? ((typeof WEATHER_I18N === 'object' && WEATHER_I18N && WEATHER_I18N.tomorrow) || 'Tomorrow')
    : ((typeof WEATHER_I18N === 'object' && WEATHER_I18N && WEATHER_I18N.today) || 'Today'));
  let titleEl = el.querySelector('.tile-title');
  if (!titleEl) {
    titleEl = document.createElement('div');
    titleEl.className = 'tile-title';
    el.insertBefore(titleEl, host);
  }
  titleEl.textContent = titleText;
  // The current hour on the today half: a dashed vertical line with its dot on
  // the curve, like the panel's chart cursor (graph_renderer.cpp). The
  // tomorrow half has no "now".
  let nowSvg = '';
  if (day === 0) {
    const nowPos = now.getHours() + now.getMinutes() / 60;
    const nowX = Math.min(width, Math.max(0, xOfHour(nowPos)));
    const nowSlot = slots[Math.min(23, Math.max(0, Math.floor(nowPos)))];
    nowSvg = '<line x1="' + nowX.toFixed(2) + '" y1="' + top.toFixed(2) +
      '" x2="' + nowX.toFixed(2) + '" y2="' + bandTop.toFixed(2) +
      '" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 4" opacity=".65"/>';
    if (nowSlot && nowSlot.temp !== null) {
      nowSvg += '<circle cx="' + nowX.toFixed(2) + '" cy="' +
        yOfTemp(nowSlot.temp).toFixed(2) + '" r="3.5" fill="currentColor"/>';
    }
  }
  host.innerHTML =
    '<svg class="weather-graph-svg" viewBox="0 0 ' + width + ' ' + height + '" aria-hidden="true">' +
    lineSvg + band + episodeSvg + tempSvg.join('') + hourSvg.join('') + nowSvg + '</svg>';
}

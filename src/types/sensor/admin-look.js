  // The Sensor tile's looks below its header (types/sensor/sensor_look.h,
  // gauge lab build/design-mockups/gauge-media/lab.html, 2026-10-28): the
  // panel's box, arc fit, shapes and value place, drawn as SVG in panel
  // pixels over the card and scaled like the rest of the preview
  // (--radius-preview-scale). Lit places take the icon's shown color
  // (--icon-shown), the track and the unlit places the controls' fill
  // (--control-fill); with a color bar every place takes its own colors,
  // which applyIconDiscTint hands over (tile.sensorLookTones).
  const SENSOR_LOOK_VALUE_INSIDE = [8, 10, 11, 12];
  let sensorLookClipSeq = 0;

  function isSensorLookMode(mode) {
    const m = Number(mode);
    return m >= 3 && m <= 12;
  }

  function sensorLookValueInside(mode) {
    return SENSOR_LOOK_VALUE_INSIDE.includes(Number(mode));
  }

  function sensorLookRootValue(name, fallback) {
    if (typeof getComputedStyle !== 'function' || typeof document === 'undefined') return fallback;
    const value = parseFloat(getComputedStyle(document.documentElement).getPropertyValue(name));
    return Number.isFinite(value) ? value : fallback;
  }

  // The look's sizes in panel pixels (tile_layout::scale of the lab's 12,
  // 16, 6, 5 and 11), from web_admin_styles.cpp.
  function sensorLookMetrics() {
    const raw = typeof getComputedStyle === 'function' && typeof document !== 'undefined'
      ? String(getComputedStyle(document.documentElement).getPropertyValue('--sensor-look-metrics')).trim() : '';
    const parts = raw.split(/\s+/).map(Number);
    const [stroke, band, stripe, gap, pitch] = parts.length === 5 && parts.every(Number.isFinite)
      ? parts : [12, 16, 6, 5, 11];
    return {stroke, band, stripe, gap, pitch};
  }

  // ---- The arc fit (sensor_look::fit_arc / auto_arc) ----

  function sensorLookColumn(box, m, x) {
    if (x < m - 1e-4 || x > box.w - m + 1e-4) return null;
    let top = m, bottom = box.h - m;
    const dx = Math.max(box.r - x, x - (box.w - box.r), 0);
    if (dx > 0) {
      const inner = box.r - m;
      if (inner <= 0 || dx > inner + 1e-4) return null;
      const s = Math.sqrt(Math.max(0, inner * inner - dx * dx));
      top = Math.max(top, box.r - s);
      bottom = Math.min(bottom, box.h - box.r + s);
    }
    return top <= bottom + 1e-4 ? [top, bottom] : null;
  }

  function sensorLookFitArc(box, angle, sw) {
    const half = angle / 2 * Math.PI / 180, cx = box.w / 2, samples = 72;
    const sines = [], cosines = [];
    for (let k = 0; k <= samples; ++k) {
      const a = -half + 2 * half * k / samples;
      sines.push(Math.sin(a));
      cosines.push(Math.cos(a));
    }
    const heights = r => {
      let lo = -1e9, hi = 1e9;
      for (let k = 0; k <= samples; ++k) {
        const column = sensorLookColumn(box, sw / 2, cx + r * sines[k]);
        if (!column) return null;
        lo = Math.max(lo, column[0] + r * cosines[k]);
        hi = Math.min(hi, column[1] + r * cosines[k]);
        if (lo > hi) return null;
      }
      return [lo, hi];
    };
    let lo = 5, hi = 2 * Math.max(box.w, box.h);
    if (!heights(lo)) return null;
    for (let i = 0; i < 24; ++i) {
      const mid = (lo + hi) / 2;
      if (heights(mid)) lo = mid;
      else hi = mid;
    }
    const range = heights(lo);
    return {cx, cy: (range[0] + range[1]) / 2, r: lo, half, sw};
  }

  function sensorLookAutoArc(box, sw) {
    let best = null, bestScore = -1;
    for (let angle = 40; angle <= 300; angle += 10) {
      const arc = sensorLookFitArc(box, angle, sw);
      if (!arc) continue;
      const w = 2 * arc.r * (arc.half >= Math.PI / 2 ? 1 : Math.sin(arc.half)) + arc.sw;
      const h = arc.r * (1 - Math.cos(Math.min(arc.half, Math.PI))) + arc.sw;
      const score = (w / box.w) * (h / box.h);
      if (score > bestScore) { bestScore = score; best = arc; }
    }
    return best;
  }

  function sensorLookArc(mode, box, m) {
    mode = Number(mode);
    if (mode === 7 || mode === 8) return sensorLookAutoArc(box, m.stroke);
    if (mode === 9 || mode === 10) return sensorLookAutoArc(box, m.band);
    if (mode === 11 || mode === 12) {
      const sw = mode === 11 ? m.stroke : m.band;
      const r = (Math.min(box.w, box.h) - sw) / 2;
      return r > sw / 2 ? {cx: box.w / 2, cy: box.h / 2, r, half: Math.PI, sw} : null;
    }
    return null;
  }

  // sensor_look::value_top: rings and nearly closed arcs of stripes centre
  // the value, other arcs put its top at the middle of their inside.
  function sensorLookValueTop(mode, arc, size) {
    mode = Number(mode);
    if (mode === 11 || mode === 12 || (mode === 10 && 2 * arc.half >= 1.5 * Math.PI)) return arc.cy - size * 0.6;
    return (arc.cy - arc.r + arc.sw / 2 + arc.cy - arc.r * Math.cos(arc.half)) / 2;
  }

  // ---- Shapes as SVG (the lab's drawing, box-local coordinates) ----

  const sensorLookNum = v => (Math.round(v * 100) / 100).toString();

  // Stripes (sensor_look::emit_stripes): round ends on the box, in the
  // corners following the corner curve; each stripe shrunk by r and stroked
  // 2r wide with round joins.
  function sensorLookStripes(box, m, fraction, up, colorOf) {
    const sw = m.stripe, rc = sw / 2, U = up ? box.h : box.w, V = up ? box.w : box.h, R = box.r;
    const n = Math.floor((U + m.gap) / (sw + m.gap));
    if (n < 1) return '';
    const gap = n > 1 ? (U - n * sw) / (n - 1) : 0;
    const lit = Math.round(n * fraction);
    const edge = (u, r, dir) => {
      const off = Math.max(0, R - u, up ? 0 : u - (U - R)), ri = R - r;
      if (off <= 0) return dir > 0 ? V - r : r;
      const d = Math.sqrt(Math.max(0, ri * ri - off * off));
      return dir > 0 ? V - R + d : R - d;
    };
    const map = (u, v) => up ? `${sensorLookNum(v)} ${sensorLookNum(U - u)}` : `${sensorLookNum(u)} ${sensorLookNum(v)}`;
    let body = '';
    for (let i = 0; i < n; ++i) {
      const su = i * (sw + gap), drop = Math.abs(edge(su, 0, 1) - edge(su + sw, 0, 1));
      const r = drop > sw ? sw / 3 : rc, a = su + r, b = su + sw - r;
      let d = '';
      for (let k = 0; k <= 8; ++k) { const u = a + (b - a) * k / 8; d += `${k ? 'L' : 'M'}${map(u, edge(u, r, -1))}`; }
      for (let k = 0; k <= 8; ++k) { const u = b - (b - a) * k / 8; d += `L${map(u, edge(u, r, 1))}`; }
      const color = colorOf(i < lit, (i + 0.5) / n);
      body += `<path d="${d}Z" style="fill:${color};stroke:${color}" stroke-width="${sensorLookNum(2 * r)}" stroke-linejoin="round"/>`;
    }
    return body;
  }

  // A level (sensor_look::emit_level): the track is the whole box, the fill
  // as long as the value with its moving end rounded, inside the box.
  function sensorLookLevel(box, end, fraction, up, colorOf) {
    const r = box.r, id = 'sensorLookClip' + (++sensorLookClipSeq);
    let body = `<rect width="${sensorLookNum(box.w)}" height="${sensorLookNum(box.h)}" rx="${sensorLookNum(r)}" style="fill:${colorOf(false, 0.5)}"/>`;
    if (!(fraction > 0)) return body;
    body += `<clipPath id="${id}"><rect width="${sensorLookNum(box.w)}" height="${sensorLookNum(box.h)}" rx="${sensorLookNum(r)}"/></clipPath>`;
    let d;
    if (up) {
      const length = box.h * fraction, e = Math.min(end, length, box.w / 2), y = box.h - length;
      d = `M0 ${sensorLookNum(box.h)}V${sensorLookNum(y + e)}A${e} ${e} 0 0 1 ${sensorLookNum(e)} ${sensorLookNum(y)}` +
        `H${sensorLookNum(box.w - e)}A${e} ${e} 0 0 1 ${sensorLookNum(box.w)} ${sensorLookNum(y + e)}V${sensorLookNum(box.h)}Z`;
    } else {
      const length = box.w * fraction, e = Math.min(end, length, box.h / 2);
      d = `M0 0H${sensorLookNum(length - e)}A${e} ${e} 0 0 1 ${sensorLookNum(length)} ${sensorLookNum(e)}` +
        `V${sensorLookNum(box.h - e)}A${e} ${e} 0 0 1 ${sensorLookNum(length - e)} ${sensorLookNum(box.h)}H0Z`;
    }
    return body + `<path d="${d}" clip-path="url(#${id})" style="fill:${colorOf(true, fraction)}"/>`;
  }

  // An arc or ring (sensor_look::emit_arc): track, fill to the value with
  // round ends; with a color bar small pieces in their colors and round ends.
  function sensorLookArcBody(arc, fraction, closed, places, colorOf) {
    const {cx, cy, r, sw} = arc;
    const a0 = closed ? 0 : -arc.half, a1 = closed ? 2 * Math.PI : arc.half;
    const at = a => `${sensorLookNum(cx + r * Math.sin(a))} ${sensorLookNum(cy - r * Math.cos(a))}`;
    const path = (p, q) => q - p > Math.PI
      ? `M${at(p)}A${r} ${r} 0 0 1 ${at((p + q) / 2)}A${r} ${r} 0 0 1 ${at(q)}`
      : `M${at(p)}A${r} ${r} 0 0 1 ${at(q)}`;
    const aOf = p => a0 + (a1 - a0) * p;
    const cap = (p, color) => {
      const a = aOf(p);
      return `<circle cx="${sensorLookNum(cx + r * Math.sin(a))}" cy="${sensorLookNum(cy - r * Math.cos(a))}" r="${sw / 2}" style="fill:${color}"/>`;
    };
    let body = '';
    if (!places) {
      body += `<path d="${path(a0, a1)}" style="stroke:${colorOf(false, 0.5)}" stroke-width="${sw}" fill="none" stroke-linecap="${closed ? 'butt' : 'round'}"/>`;
      if (fraction > 0) {
        body += `<path d="${path(a0, Math.max(aOf(Math.min(1, fraction)), a0 + 1e-4))}" style="stroke:${colorOf(true, fraction)}" stroke-width="${sw}" fill="none" stroke-linecap="round"/>`;
      }
      return body;
    }
    const pieces = 48, colorAt = p => colorOf(p < fraction, p);
    if (!closed) body += cap(0, colorOf(fraction > 0, 0)) + cap(1, colorOf(fraction >= 1, 1));
    for (let k = 0; k < pieces; ++k) {
      const p0 = k / pieces, p1 = Math.min(1, (k + 1.2) / pieces);
      body += `<path d="${path(aOf(p0), aOf(p1))}" style="stroke:${colorAt((k + 0.5) / pieces)}" stroke-width="${sw}" fill="none"/>`;
    }
    if (fraction > 0) body += cap(0, colorOf(true, 0));
    if (fraction > 0 && fraction < 1) body += cap(fraction, colorOf(true, fraction));
    return body;
  }

  // Round stripes across an arc or ring (sensor_look::emit_ticks).
  function sensorLookTicks(arc, m, fraction, closed, colorOf) {
    const halfW = m.stripe / 2, r0 = arc.r - m.band / 2 + halfW, r1 = arc.r + m.band / 2 - halfW;
    const n = closed ? Math.floor(2 * Math.PI * arc.r / m.pitch)
      : Math.max(2, Math.floor(arc.r * 2 * arc.half / m.pitch) + 1);
    if (n < 2) return '';
    const lit = Math.round(n * fraction);
    let body = '';
    for (let i = 0; i < n; ++i) {
      const a = closed ? 2 * Math.PI * i / n : -arc.half + 2 * arc.half * i / (n - 1);
      const sn = Math.sin(a), cs = Math.cos(a);
      body += `<line x1="${sensorLookNum(arc.cx + r0 * sn)}" y1="${sensorLookNum(arc.cy - r0 * cs)}" ` +
        `x2="${sensorLookNum(arc.cx + r1 * sn)}" y2="${sensorLookNum(arc.cy - r1 * cs)}" ` +
        `style="stroke:${colorOf(i < lit, (i + 0.5) / n)}" stroke-width="${sensorLookNum(m.stripe)}" stroke-linecap="round"/>`;
    }
    return body;
  }

  // ---- The tile ----

  // The look's data on the tile element (applyCompactSensorPreview): the
  // mode, the gauge range and the tile's color record.
  function setSensorLookData(el, mode, data) {
    if (!el?.dataset) return;
    if (!isSensorLookMode(mode)) {
      delete el.dataset.lookMode;
      return;
    }
    el.dataset.lookMode = String(Number(mode));
    let min = Number(data?.sensor_gauge_min), max = Number(data?.sensor_gauge_max);
    if (!Number.isFinite(min)) min = 0;
    if (!Number.isFinite(max) || max <= min) { min = 0; max = 100; }
    el.dataset.lookMin = String(min);
    el.dataset.lookMax = String(max);
    el.dataset.lookRecord = String(data?.icon_colors || '');
  }

  // The color bar that colors the icon from the tile's own state
  // (tile_icon_colors::own_bar), or null.
  function sensorLookBar(record) {
    if (!record || typeof parseIconColorRecord !== 'function') return null;
    if (typeof iconColorOwnStateColorsIcon === 'function' && !iconColorOwnStateColorsIcon(record)) return null;
    const parsed = parseIconColorRecord(record);
    return parsed?.bar || null;
  }

  function sensorLookShownValue(el) {
    const value = el.querySelector(':scope > .tile-value');
    const text = value?.firstChild?.nodeType === 3 ? value.firstChild.nodeValue : (value?.textContent || '');
    const number = parseFloat(String(text).replace(',', '.'));
    return Number.isFinite(number) ? number : NaN;
  }

  function renderSensorLookPreview(el) {
    if (!el?.querySelector) return;
    let svg = el.querySelector(':scope > .sensor-look-svg');
    const mode = Number(el.dataset.lookMode);
    if (!isSensorLookMode(mode)) {
      svg?.remove();
      return;
    }
    const scale = sensorLookRootValue('--radius-preview-scale', 1) || 1;
    const W = el.offsetWidth / scale, H = el.offsetHeight / scale;
    if (!(W > 8 && H > 8)) return;
    const inset = sensorLookRootValue('--climate-margin-x', 6 * scale) / scale;
    const top = sensorLookRootValue('--climate-slots-top', 78 * scale) / scale;
    const radius = sensorLookRootValue('--tile-radius-device', 31) - inset;
    const box = {x: inset, y: top, w: W - 2 * inset, h: H - inset - top};
    box.r = Math.max(0, Math.min(radius, box.w / 2, box.h / 2));
    if (box.w < 8 || box.h < 8) { svg?.remove(); return; }
    const m = sensorLookMetrics();
    const end = Math.max(1, Math.round(sensorLookRootValue('--switch-bar-height', 68 * scale) / scale / 4));
    const min = Number(el.dataset.lookMin) || 0, max = Number(el.dataset.lookMax) || 100;
    const value = sensorLookShownValue(el);
    const fraction = Number.isFinite(value) ? Math.min(1, Math.max(0, (value - min) / (max - min))) : 0;
    // A color bar colors every place of stripes, arcs and rings.
    const bar = mode >= 5 ? sensorLookBar(el.dataset.lookRecord) : null;
    const tones = typeof el.sensorLookTones === 'function' ? el.sensorLookTones : null;
    const colorOf = (lit, place) => {
      if (bar && tones && typeof iconColorBarColor === 'function') {
        const rgb = iconColorBarColor(bar, min + (max - min) * place);
        const tone = tones([(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255]);
        return lit ? tone.lit : tone.off;
      }
      return lit ? 'var(--icon-shown, currentColor)' : 'var(--control-fill, rgba(255,255,255,0.12))';
    };
    let body = '';
    if (mode === 3 || mode === 4) body = sensorLookLevel(box, end, fraction, mode === 4, colorOf);
    else if (mode === 5 || mode === 6) body = sensorLookStripes(box, m, fraction, mode === 6, colorOf);
    const arc = sensorLookArc(mode, box, m);
    if (arc && (mode === 7 || mode === 8 || mode === 11)) body = sensorLookArcBody(arc, fraction, mode === 11, !!(bar && tones), colorOf);
    if (arc && (mode === 9 || mode === 10 || mode === 12)) body = sensorLookTicks(arc, m, fraction, mode === 12, colorOf);
    if (!svg) {
      el.insertAdjacentHTML('afterbegin', '<svg class="sensor-look-svg" aria-hidden="true"></svg>');
      svg = el.querySelector(':scope > .sensor-look-svg');
    }
    svg.setAttribute('viewBox', `0 0 ${sensorLookNum(W)} ${sensorLookNum(H)}`);
    svg.innerHTML = `<g transform="translate(${sensorLookNum(box.x)} ${sensorLookNum(box.y)})">${body}</g>`;
    // The value inside the arc or ring: 20 on one-row tiles, 24 on taller
    // ones, its em box's top where the panel puts it.
    const valueEl = el.querySelector(':scope > .tile-value');
    if (valueEl && sensorLookValueInside(mode) && arc) {
      const taller = Number(el.dataset.lookTall) === 1;
      const fontPx = sensorLookRootValue(taller ? '--fs24' : '--fs20', (taller ? 24 : 20) * scale);
      const size = fontPx / scale;
      valueEl.style.top = sensorLookNum((box.y + sensorLookValueTop(mode, arc, size)) * scale - 3) + 'px';
      valueEl.style.fontSize = sensorLookNum(fontPx) + 'px';
    } else if (valueEl) {
      valueEl.style.removeProperty('top');
      valueEl.style.removeProperty('font-size');
    }
  }

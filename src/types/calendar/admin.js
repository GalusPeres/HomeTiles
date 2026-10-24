  // Calendar (27): up to four Home Assistant calendars in their own colors
  // (types/calendar/calendar_tile.cpp). The preview mirrors the device: the
  // events payload of /api/sensor_values calendar_values (calendar_events.h),
  // the layouts of layout_of() and the words of CALENDAR_I18N (types/calendar/web.cpp).
  const CALENDAR_FIELDS = ['calendar_entity', 'calendar_entity_2', 'calendar_entity_3', 'calendar_entity_4'];
  // calendar_model::calendar_color(): dark and light theme.
  const CALENDAR_COLORS = [['#5B9BF0', '#2F6FD0'], ['#4DB6AC', '#00897B'], ['#B388FF', '#7E57C2'], ['#FFA65C', '#E07020']];
  // i18n::CalendarLabel order.
  const CALENDAR_LABEL = {
    type: 0, entity: 1, calendarN: 2, view: 3, agenda: 4, week: 5, month: 6, weekStart: 7, automatic: 8,
    allDay: 9, noEvents: 10, nothingPlanned: 11, inDays: 12, inHours: 13, inHoursMinutes: 14, inMinutes: 15,
    now: 16, until: 17, upNext: 18, eventCount: 19, oneEvent: 20, list: 21, noData: 22
  };
  const CALENDAR_AGENDA_DAYS = 31;

  function calendarI18n() { return typeof CALENDAR_I18N === 'object' ? CALENDAR_I18N : {}; }
  function calendarLabel(key, ...args) {
    let text = String(calendarI18n().labels?.[CALENDAR_LABEL[key]] ?? '');
    for (const arg of args) text = text.replace(/%[us]/, String(arg));
    return text;
  }
  function calendarColor(index) {
    const light = typeof panelLight === 'function' && panelLight();
    return CALENDAR_COLORS[index % CALENDAR_COLORS.length][light ? 1 : 0];
  }

  // calendar_store::split_entities(): at most four calendar ids, no duplicates.
  function calendarEntitiesOf(list) {
    const out = [];
    for (const raw of String(list || '').split(',')) {
      const id = raw.trim();
      if (/^calendar\.[a-z0-9_]+$/.test(id) && !out.includes(id) && out.length < 4) out.push(id);
    }
    return out;
  }

  // Days since 1970-01-01 (calendar_events::days_from_civil).
  function calendarDay(y, m, d) { return Math.floor(Date.UTC(y, m - 1, d) / 86400000); }
  function calendarCivil(day) {
    const date = new Date(day * 86400000);
    return {y: date.getUTCFullYear(), m: date.getUTCMonth() + 1, d: date.getUTCDate()};
  }
  function calendarWeekday(day) { return new Date(day * 86400000).getUTCDay(); }
  // "2026-10-24" or "2026-10-24T10:05" -> {day, minute} (minute -1: all day).
  function calendarStamp(text) {
    const match = /^(\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2}))?/.exec(String(text || ''));
    if (!match) return null;
    const [y, m, d] = [Number(match[1]), Number(match[2]), Number(match[3])];
    if (m < 1 || m > 12 || d < 1 || d > new Date(Date.UTC(y, m, 0)).getUTCDate()) return null;
    if (match[4] === undefined) return {day: calendarDay(y, m, d), minute: -1};
    const hh = Number(match[4]), mm = Number(match[5]);
    if (hh > 23 || mm > 59) return null;
    return {day: calendarDay(y, m, d), minute: hh * 60 + mm};
  }

  // calendar_events::parse(): a missing payload stays apart from an empty one.
  function parseCalendarPreviewPayload(value) {
    const out = {valid: false, available: false, from: 0, to: -1, events: []};
    const text = value === undefined || value === null ? '' : String(value).trim();
    if (!text.startsWith('{')) return out;
    let obj;
    try { obj = JSON.parse(text); } catch (e) { return out; }
    if (!obj || obj.v !== 1) return out;
    const from = calendarStamp(obj.from), to = calendarStamp(obj.to);
    if (!from || !to || to.day < from.day) return out;
    out.valid = true;
    out.available = typeof obj.state === 'string' && obj.state !== 'unavailable';
    out.from = from.day;
    out.to = to.day;
    for (const item of Array.isArray(obj.e) ? obj.e.slice(0, 96) : []) {
      if (!item || typeof item !== 'object') continue;
      const s = calendarStamp(item.s), e = calendarStamp(item.e);
      if (!s || !e || (s.minute < 0) !== (e.minute < 0)) continue;
      const event = {startDay: s.day, startMin: s.minute, endDay: e.day, endMin: e.minute,
        title: String(item.t || ''), location: String(item.l || '')};
      if (s.minute < 0) {
        event.endDay = e.day > s.day ? e.day - 1 : s.day;
      } else {
        if (e.day < s.day || (e.day === s.day && e.minute < s.minute)) { event.endDay = s.day; event.endMin = s.minute; }
        if (event.endMin === 0 && event.endDay > s.day) { event.endDay -= 1; event.endMin = 1440; }
      }
      out.events.push(event);
    }
    out.events.sort((a, b) => a.startDay - b.startDay || a.startMin - b.startMin || a.title.localeCompare(b.title));
    return out;
  }

  function calendarPreviewSources(entities, meta = sensorMetaCache) {
    return entities.map(id => parseCalendarPreviewPayload(meta?.calendarValues?.[id] ?? ''));
  }
  function calendarItemsOn(sources, day) {
    const items = [];
    sources.forEach((source, calendar) => {
      if (!source.valid) return;
      for (const event of source.events) {
        if (event.startDay > day) break;
        if (event.endDay >= day) items.push({event, calendar});
      }
    });
    const start = item => item.event.startMin < 0 ? -1 : (item.event.startDay === day ? item.event.startMin : 0);
    return items.sort((a, b) => start(a) - start(b) || a.calendar - b.calendar || a.event.title.localeCompare(b.event.title));
  }
  function calendarNow() {
    const now = new Date();
    return {today: calendarDay(now.getFullYear(), now.getMonth() + 1, now.getDate()), minute: now.getHours() * 60 + now.getMinutes()};
  }
  function calendarClock(minute) {
    const value = ((minute % 1440) + 1440) % 1440;
    const pad = n => String(n).padStart(2, '0');
    return pad(Math.floor(value / 60)) + ':' + pad(value % 60);
  }
  function calendarDayLabel(day, now) {
    const t = calendarI18n();
    if (day === now.today) return t.today || 'Today';
    if (day === now.today + 1) return t.tomorrow || 'Tomorrow';
    const c = calendarCivil(day);
    const date = String(t.shortDate || '{d} {m}').replace('{d}', String(c.d)).replace('{m}', t.monthsShort?.[c.m - 1] || '');
    return (t.weekdaysShort?.[calendarWeekday(day)] || '') + ' ' + date;
  }
  // calendar_model::next_event() within the agenda's days.
  function calendarNextEvent(sources, now) {
    let best = null;
    const rank = n => n.ongoing && n.event.startMin >= 0 ? 0 : (n.ongoing ? 1 : 2);
    sources.forEach((source, calendar) => {
      if (!source.valid) return;
      for (const event of source.events) {
        if (event.startDay > now.today + CALENDAR_AGENDA_DAYS) break;
        if (event.endDay < now.today) continue;
        if (event.startMin >= 0 && event.endDay === now.today && event.endMin <= now.minute) continue;
        const ongoing = event.startDay < now.today ||
          (event.startDay === now.today && (event.startMin < 0 || event.startMin <= now.minute));
        const candidate = {event, calendar, ongoing, day: ongoing ? now.today : event.startDay};
        if (!best || rank(candidate) < rank(best) ||
            (rank(candidate) === rank(best) && rank(candidate) === 2 &&
             (candidate.day < best.day || (candidate.day === best.day && event.startMin < best.event.startMin)))) {
          best = candidate;
        }
      }
    });
    return best;
  }
  function calendarCountdown(minutes) {
    if (minutes < 60) return calendarLabel('inMinutes', Math.max(1, minutes));
    const h = Math.floor(minutes / 60), m = minutes % 60;
    return m ? calendarLabel('inHoursMinutes', h, m) : calendarLabel('inHours', h);
  }
  // calendar_text::when_line().
  function calendarWhenLine(next, now, brief) {
    if (!next) return '';
    const e = next.event;
    const dot = ' · ';
    if (next.ongoing) {
      if (e.startMin < 0) return e.endDay > now.today ? calendarLabel('until', calendarDayLabel(e.endDay, now)) : calendarLabel('allDay');
      return calendarLabel('until', e.endDay === now.today ? calendarClock(e.endMin) : calendarDayLabel(e.endDay, now));
    }
    const days = next.day - now.today;
    const time = e.startMin < 0 ? '' : calendarClock(e.startMin);
    if (days === 0) {
      if (brief || e.startMin < 0) return e.startMin < 0 ? calendarLabel('allDay') : time;
      return time + dot + calendarCountdown(e.startMin - now.minute);
    }
    if (days === 1) return calendarDayLabel(next.day, now) + (time ? dot + time : '');
    if (days < 7) return (calendarI18n().weekdaysShort?.[calendarWeekday(next.day)] || '') + (time ? dot + time : '');
    return brief ? calendarDayLabel(next.day, now) : calendarDayLabel(next.day, now) + dot + calendarLabel('inDays', days);
  }
  function calendarWeekStart(stored) {
    const value = Number(stored);
    if ([0, 1, 6].includes(value)) return value;
    const fallback = Number(calendarI18n().firstWeekday);
    return fallback >= 0 && fallback <= 6 ? fallback : 1;
  }

  // calendar_tile.cpp layout_of().
  function calendarPreviewLayout(tile) {
    const w = Number(tile.span_w) || 1, h = Number(tile.span_h) || 1;
    if (h === 0.5) return 'compact';
    const view = Number(tile.sensor_display_mode) || 0;
    if (view === 2 && calendarI18n().monthView && w >= 2 && h >= 2) return 'month';
    if (view === 1 && w >= 1.5) return 'week';
    return h >= 2 ? 'agenda' : 'next';
  }
  // The title the tile shows: the month's on the month view, none at half height.
  function calendarPreviewTitle(tile, title) {
    const layout = calendarPreviewLayout(tile);
    if (layout === 'compact') return '';
    if (layout === 'month') {
      const c = calendarCivil(calendarNow().today);
      return (calendarI18n().months?.[c.m - 1] || '') + ' ' + c.y;
    }
    return title;
  }
  function calendarPreviewIconColor(tile) {
    return calendarEntitiesOf(tile.sensor_entity).length ? calendarColor(0) : '';
  }

  function calendarGridHtml(sources, first, rows, now, month, weekStart) {
    const t = calendarI18n();
    let html = '<div class="calendar-preview-grid' + (month ? ' month' : ' week') + '" style="--calendar-rows:' + rows + '">';
    if (month) {
      for (let i = 0; i < 7; ++i) html += '<span class="calendar-preview-dow">' + escapeHtml(t.weekdaysShort?.[(weekStart + i) % 7] || '') + '</span>';
    }
    const shownMonth = calendarCivil(now.today).m;
    for (let i = 0; i < rows * 7; ++i) {
      const day = first + i;
      const c = calendarCivil(day);
      const cls = ['calendar-preview-day'];
      if (day === now.today) cls.push('today');
      else if (month && c.m !== shownMonth) cls.push('outside');
      else if (day < now.today) cls.push('past');
      const dots = month && c.m !== shownMonth ? [] : [...new Set(calendarItemsOn(sources, day).map(item => item.calendar))]
        .sort().map(index => '<i style="background:' + calendarColor(index) + '"></i>').join('');
      html += '<span class="' + cls.join(' ') + '">' +
        (month ? '' : '<small>' + escapeHtml(t.weekdaysShort?.[calendarWeekday(day)] || '') + '</small>') +
        '<b>' + c.d + '</b><span class="calendar-preview-dots">' + dots + '</span></span>';
    }
    return html + '</div>';
  }

  // The content below the header (or the two lines of a half-height tile).
  function calendarPreviewExtraHtml(tile, meta = sensorMetaCache) {
    const entities = calendarEntitiesOf(tile.sensor_entity);
    const sources = calendarPreviewSources(entities, meta);
    const layout = calendarPreviewLayout(tile);
    const now = calendarNow();
    const hasData = sources.some(source => source.valid);
    const message = !entities.length ? '--' : (!hasData ? calendarLabel('noData') : '');
    if (layout === 'compact' || layout === 'next') {
      const next = message ? null : calendarNextEvent(sources, now);
      const what = message || (next ? next.event.title : calendarLabel('nothingPlanned'));
      const when = next ? calendarWhenLine(next, now, layout === 'compact') : '';
      if (layout === 'compact') {
        return '<div class="tile-title calendar-preview-when">' + escapeHtml(when) + '</div>' +
          '<div class="tile-value calendar-preview-what">' + escapeHtml(what) + '</div>';
      }
      return '<div class="calendar-preview-body next' + (Number(tile.span_w) >= 2 ? ' wide' : '') + '">' +
        (next ? '<span class="calendar-preview-bar" style="background:' + calendarColor(next.calendar) + '"></span>' : '') +
        '<div class="calendar-preview-what">' + escapeHtml(what) + '</div>' +
        '<div class="calendar-preview-when">' + escapeHtml(when) + '</div></div>';
    }
    if (layout === 'week' || layout === 'month') {
      const weekStart = calendarWeekStart(tile.sensor_decimals);
      let first, rows = 1;
      if (layout === 'month') {
        const c = calendarCivil(now.today);
        const day1 = calendarDay(c.y, c.m, 1);
        first = day1 - (calendarWeekday(day1) - weekStart + 7) % 7;
        const last = calendarDay(c.y, c.m, new Date(Date.UTC(c.y, c.m, 0)).getUTCDate());
        rows = Math.floor((last - first) / 7) + 1;
      } else {
        first = now.today - (calendarWeekday(now.today) - weekStart + 7) % 7;
      }
      return '<div class="calendar-preview-body">' + calendarGridHtml(sources, first, rows, now, layout === 'month', weekStart) + '</div>';
    }
    // Agenda: the coming days with events (cut by the tile's height in CSS).
    let html = '<div class="calendar-preview-body agenda">';
    if (message) return html + '<div class="calendar-preview-message">' + escapeHtml(message) + '</div></div>';
    let rows = 0;
    const limit = Math.max(2, Math.round((Number(tile.span_h) || 2) * 3));
    for (let day = now.today; day <= now.today + CALENDAR_AGENDA_DAYS && rows < limit; ++day) {
      const items = calendarItemsOn(sources, day).filter(item => !(day === now.today && item.event.startMin >= 0 &&
        item.event.endDay === day && item.event.endMin <= now.minute));
      if (!items.length) continue;
      html += '<div class="calendar-preview-daylabel">' + escapeHtml(calendarDayLabel(day, now)) + '</div>';
      for (const item of items) {
        if (rows >= limit) break;
        const start = item.event.startMin < 0 ? calendarLabel('allDay')
          : calendarClock(item.event.startDay === day ? item.event.startMin : 0);
        html += '<div class="calendar-preview-row"><span class="calendar-preview-time">' + escapeHtml(start) + '</span>' +
          '<span class="calendar-preview-bar" style="background:' + calendarColor(item.calendar) + '"></span>' +
          '<span class="calendar-preview-title">' + escapeHtml(item.event.title) + '</span></div>';
        ++rows;
      }
    }
    if (!rows) html += '<div class="calendar-preview-message">' + escapeHtml(calendarLabel('nothingPlanned')) + '</div>';
    return html + '</div>';
  }

  // The tile the live preview draws from the open fields.
  function calendarTileFromInputs(tab, spanW, spanH) {
    return {
      sensor_entity: calendarEntityList(tab),
      sensor_display_mode: Number(document.getElementById(tab + '_calendar_view')?.value || 0),
      sensor_decimals: Number(document.getElementById(tab + '_calendar_week_start')?.value ?? 255),
      span_w: spanW,
      span_h: spanH
    };
  }

  function calendarEntityList(tab) {
    return calendarEntitiesOf(CALENDAR_FIELDS.map(field => document.getElementById(tab + '_' + field)?.value || '').join(','))
      .join(',');
  }

  function loadCalendarFields(tab, data) {
    const entities = calendarEntitiesOf(data.sensor_entity || data.calendar_entities || '');
    CALENDAR_FIELDS.forEach((field, i) => {
      const input = document.getElementById(tab + '_' + field);
      if (input) input.value = entities[i] || '';
    });
    const view = document.getElementById(tab + '_calendar_view');
    if (view) {
      const value = String(Number(data.sensor_display_mode) || 0);
      view.value = Array.from(view.options || []).some(option => option.value === value) ? value : '0';
    }
    const weekStart = document.getElementById(tab + '_calendar_week_start');
    if (weekStart) {
      const value = Number(data.sensor_decimals);
      weekStart.value = [0, 1, 6].includes(value) ? String(value) : '255';
    }
    const popup = document.getElementById(tab + '_calendar_popup_open_mode');
    if (popup) popup.value = data.popup_open_mode !== undefined ? String(data.popup_open_mode) : '1';
    maybeFillTitleFromEntity(tab, '_calendar_entity');
  }

  function saveCalendarFields(tab, formData) {
    const entities = calendarEntityList(tab).split(',').filter(Boolean);
    CALENDAR_FIELDS.forEach((field, i) => formData.append(field, entities[i] || ''));
    formData.append('sensor_entity', entities.join(','));
    formData.append('calendar_view', document.getElementById(tab + '_calendar_view')?.value || '0');
    formData.append('calendar_week_start', document.getElementById(tab + '_calendar_week_start')?.value || '255');
    const popup = document.getElementById(tab + '_calendar_popup_open_mode');
    if (popup) formData.append('popup_open_mode', popup.value || '1');
  }

  function resetCalendarFields(tab) {
    for (const field of CALENDAR_FIELDS) {
      const input = document.getElementById(tab + '_' + field);
      if (input) input.value = '';
    }
    const view = document.getElementById(tab + '_calendar_view');
    if (view) view.value = '0';
    const weekStart = document.getElementById(tab + '_calendar_week_start');
    if (weekStart) weekStart.value = '255';
    const popup = document.getElementById(tab + '_calendar_popup_open_mode');
    if (popup) popup.value = '1';
  }


  // Timer tile (types/timer, type 30): only the shared fields (title, icon,
  // icon colors); the panel shows its Home Assistant voice timers.
  function loadTimerFields(tab, data) {
    loadIconColorFields(tab, data);
  }

  function saveTimerFields(tab, formData) {
    saveIconColorFields(tab, formData);
  }

  function resetTimerFields(tab) {
    resetIconColorFields(tab);
  }

  // The preview shows the panel's tile without a timer (renderer.cpp): the
  // timer icon unless the tile has its own, "--:--" over the name at the
  // bottom ("Tap to start" below on taller tiles), from two columns the name
  // and "No timers running" beside the circle and the presets below.
  const TIMER_PRESET_MINUTES = [1, 5, 10, 15];

  function timerPreviewText(key, fallback) {
    return (typeof TIMER_I18N === 'object' && TIMER_I18N && TIMER_I18N[key]) || fallback;
  }

  function timerPreviewIcon(rawIcon, iconName) {
    if (iconName || isExplicitlyDisabledValue(rawIcon)) return iconName;
    return 'timer-outline';
  }

  function timerPreviewHtml(title, spanW, spanH) {
    const name = String(title || '').replace(/\s*\n\s*/g, ' ').trim() || timerPreviewText('defaultName', 'Timer');
    if (Number(spanW) >= 2 && Number(spanH) >= 1) {
      const presets = TIMER_PRESET_MINUTES.map(minutes => '<span class="tile-timer-preset">' +
        escapeHtml(timerPreviewText('minutes', '%d min').replace('%d', String(minutes))) + '</span>').join('');
      return '<div class="tile-timer-head"><div class="tile-timer-title">' + escapeHtml(name) +
        '</div><div class="tile-timer-hint">' + escapeHtml(timerPreviewText('none', 'No timers running')) +
        '</div></div><div class="tile-timer-presets">' + presets + '</div>';
    }
    let html = '<div class="tile-timer-foot"><div class="tile-timer-value">--:--</div><div class="tile-timer-title">' +
      escapeHtml(name) + '</div>';
    const hintSpan = Number(panelVar('--timer-hint-span', '1.5'));
    if (Number(spanH) >= (Number.isFinite(hintSpan) ? hintSpan : 1.5)) html += '<div class="tile-timer-hint">' + escapeHtml(timerPreviewText('tapStart', 'Tap to start')) + '</div>';
    return html + '</div>';
  }

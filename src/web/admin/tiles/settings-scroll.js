
  // Tile Settings keep a clicked control where it is on screen. A choice that
  // hides fields below it (Tile color, Rules, Own/Other entity, bar mode, ...)
  // shortens the scrolling settings body; scrolled near its end, the browser
  // then clamps the scroll position and everything above, the clicked control
  // included, slides down. After the handlers ran, the body scrolls the
  // control back; a spacer at the end of the body makes room when the content
  // got too short and shrinks away again while the user scrolls up.
  const SETTINGS_SCROLL_SPACER = 'tile-settings-scroll-spacer';

  function settingsScrollSpacer(body) {
    let spacer = body.querySelector(':scope > .' + SETTINGS_SCROLL_SPACER);
    if (!spacer) {
      spacer = document.createElement('div');
      spacer.className = SETTINGS_SCROLL_SPACER;
      spacer.setAttribute('aria-hidden', 'true');
      body.appendChild(spacer);
    }
    return spacer;
  }

  function keepSettingsControlInPlace(body, control, top) {
    if (!body.isConnected || !control.isConnected || !control.getClientRects().length) return;
    const shift = control.getBoundingClientRect().top - top;
    if (Math.abs(shift) < 1) return;
    const target = body.scrollTop + shift;
    const room = body.scrollHeight - body.clientHeight;
    if (target > room) {
      const spacer = settingsScrollSpacer(body);
      spacer.style.height = ((parseFloat(spacer.style.height) || 0) + target - room) + 'px';
    }
    body.scrollTop = target;
  }

  // Only the part of the spacer below the visible area goes, so the view
  // never moves while it shrinks.
  function trimSettingsScrollSpacer(body) {
    const spacer = body.querySelector(':scope > .' + SETTINGS_SCROLL_SPACER);
    const height = spacer ? parseFloat(spacer.style.height) || 0 : 0;
    if (!height) return;
    const below = body.scrollHeight - body.scrollTop - body.clientHeight;
    const next = Math.max(0, height - Math.max(0, below));
    if (next !== height) spacer.style.height = next ? next + 'px' : '';
  }

  // One gesture fires several events (a label click, the click it forwards to
  // its checkbox, the change); the first one records the position.
  let pendingSettingsControl = null;

  function rememberSettingsControl(event) {
    const body = event.target?.closest?.('.tile-settings-body');
    if (!body || pendingSettingsControl) return;
    const control = event.target.closest('button, label, input, select, textarea') || event.target;
    pendingSettingsControl = {body, control, top: control.getBoundingClientRect().top};
    requestAnimationFrame(() => {
      const pending = pendingSettingsControl;
      pendingSettingsControl = null;
      if (pending) keepSettingsControlInPlace(pending.body, pending.control, pending.top);
    });
  }

  document.addEventListener('click', rememberSettingsControl, true);
  document.addEventListener('change', rememberSettingsControl, true);
  document.addEventListener('scroll', event => {
    if (event.target?.classList?.contains('tile-settings-body')) trimSettingsScrollSpacer(event.target);
  }, true);

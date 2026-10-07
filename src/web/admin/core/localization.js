function t(key) {
    return Object.prototype.hasOwnProperty.call(APP_I18N, key) ? APP_I18N[key] : key;
  }
  function tf(key, replacements) {
    let out = t(key);
    if (!replacements) return out;
    Object.keys(replacements).forEach(name => {
      out = out.replaceAll('{' + name + '}', String(replacements[name]));
    });
    return out;
  }
  let APP_LOCALE = document.documentElement.lang || 'en';

  // Issue #73: the panel builds this page in its own language, so a language
  // change made on the display shows only after a reload. A small poll
  // notices it and reloads once nothing is being edited here.
  const DEVICE_LANGUAGE_POLL_MS = 15000;

  function adminEditsPending() {
    const active = document.activeElement;
    if (active?.matches?.('input, textarea, select')) return true;
    if (dragSource || resizeState || fileManagerUploadBusy || hardwareIoDirty) return true;
    const busy = map => Object.values(map || {}).some(Boolean);
    return busy(autoSaveTimers) || busy(saveInFlightByTile) || busy(queuedSaveByTile);
  }

  async function checkDeviceLanguage() {
    if (document.hidden) return;
    try {
      const response = await fetch('/api/language', {cache: 'no-store'});
      if (!response.ok) return;
      const data = await response.json();
      const device = String(data?.language || '').toLowerCase();
      const page = String(document.documentElement.lang || APP_LOCALE).toLowerCase();
      if (device && device !== page && !adminEditsPending()) location.reload();
    } catch (error) {
      // Offline or signed out: the next poll tries again.
    }
  }

  function watchDeviceLanguage() {
    window.setInterval(checkDeviceLanguage, DEVICE_LANGUAGE_POLL_MS);
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden) checkDeviceLanguage();
    });
  }
  function formatLocalizedNumber(value, decimals = 0, trimTrailingZeros = false) {
    const numeric = Number(String(value ?? '').trim().replace(',', '.'));
    if (!Number.isFinite(numeric)) return '--';
    const digits = Math.max(0, Math.min(6, Number.parseInt(decimals, 10) || 0));
    return new Intl.NumberFormat(APP_LOCALE, {
      useGrouping: false,
      minimumFractionDigits: trimTrailingZeros ? 0 : digits,
      maximumFractionDigits: digits
    }).format(numeric);
  }
  function localizeNumericText(value) {
    const text = String(value ?? '').trim();
    if (!text.length) return text;
    const normalized = text.replace(',', '.');
    if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(normalized)) return text;
    const fraction = normalized.includes('.') ? normalized.split('.')[1].length : 0;
    return formatLocalizedNumber(Number(normalized), fraction, false);
  }

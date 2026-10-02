  // Preview text sits on its LVGL baseline. LVGL draws the baseline base_line
  // above the bottom of a label's line box; the browser puts it where the
  // font's rounded ascent and descent and the half-leading put it, which
  // depends on the size and the display scale (Chrome snaps it to whole
  // device pixels, so one shift from the server was up to a pixel off; user
  // 2026-10-02). The page measures the browser baseline per size and zoom and
  // moves each text by the difference to the LVGL baseline (--lb*, --*-base).
  const previewBaselineCache = new Map();
  let previewBaselineProbe = null;

  function previewCssBaseline(fontPx, linePx) {
    const key = fontPx.toFixed(3) + '/' + linePx.toFixed(3) + '@' + (window.devicePixelRatio || 1);
    if (previewBaselineCache.has(key)) return previewBaselineCache.get(key);
    if (!previewBaselineProbe) {
      previewBaselineProbe = document.createElement('div');
      previewBaselineProbe.setAttribute('aria-hidden', 'true');
      previewBaselineProbe.style.cssText =
        'position:absolute;left:-9999px;top:0;visibility:hidden;white-space:nowrap;font-weight:400;';
      const mark = document.createElement('span');
      mark.style.cssText = 'display:inline-block;width:0;height:0;vertical-align:baseline;';
      previewBaselineProbe.append('0', mark);
    }
    if (!previewBaselineProbe.isConnected) document.body.appendChild(previewBaselineProbe);
    previewBaselineProbe.style.fontSize = fontPx + 'px';
    previewBaselineProbe.style.lineHeight = linePx + 'px';
    const value = previewBaselineProbe.lastChild.getBoundingClientRect().top -
      previewBaselineProbe.getBoundingClientRect().top;
    previewBaselineCache.set(key, value);
    return value;
  }

  // The shift that moves the browser baseline of a line box onto the LVGL
  // baseline lvglBasePx below its top.
  function previewBaselineShift(fontPx, linePx, lvglBasePx) {
    return lvglBasePx - previewCssBaseline(fontPx, linePx);
  }

  // Sets every emitted shift (--ldyNN for Clock and Text lines, the
  // half-height title and value) from the measured browser baselines.
  function calibratePreviewBaselines() {
    const root = document.documentElement;
    const style = getComputedStyle(root);
    const px = name => parseFloat(style.getPropertyValue(name));
    const set = (shiftName, fontPx, linePx, basePx) => {
      if (!(fontPx > 0) || !(linePx > 0) || !Number.isFinite(basePx)) return;
      root.style.setProperty(shiftName, previewBaselineShift(fontPx, linePx, basePx).toFixed(3) + 'px');
    };
    for (const size of [16, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96]) {
      set('--ldy' + size, px('--fs' + size), px('--lh' + size), px('--lb' + size));
    }
    for (const [shift, font, line, base] of [
      ['--compact-title-dy', '--compact-title-font', '--compact-title-line', '--compact-title-base'],
      ['--compact-value-dy', '--compact-value-font', '--compact-value-line', '--compact-value-base'],
      ['--compact-value-dy-24', '--compact-value-font-24', '--compact-value-line-step-24', '--compact-value-base-24'],
      ['--compact-value-dy-28', '--compact-value-font-28', '--compact-value-line-step-28', '--compact-value-base-28'],
    ]) set(shift, px(font), px(line), px(base));
  }

  function bindPreviewBaselines() {
    const calibrate = () => {
      calibratePreviewBaselines();
      // The screensaver clock places its lines in script.
      if (typeof screensaverDraft !== 'undefined' && screensaverDraft &&
          typeof renderScreensaverEditor === 'function') {
        renderScreensaverEditor();
      }
    };
    calibrate();
    if (document.fonts?.ready) document.fonts.ready.then(calibrate);
    // Browser zoom changes the device pixel ratio and with it the rounding.
    window.addEventListener('resize', perFrame(calibrate));
  }

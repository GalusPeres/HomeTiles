
  function rgbToHex(rgb) {
    const num = Number(rgb);
    const masked = Number.isFinite(num) ? (num & 0xFFFFFF) : 0;
    return '#' + ('000000' + masked.toString(16)).slice(-6);
  }
  function hexToRgb(hex) {
    const parsed = parseInt(String(hex || '').replace('#', ''), 16);
    return isNaN(parsed) ? 0 : (parsed & 0xFFFFFF);
  }
  function makeTileBgValue(rgb) {
    return (Number(rgb) & 0xFFFFFF) | 0x01000000;
  }
  function tileBgValueIsSet(value) {
    const num = Number(value);
    return Number.isFinite(num) && num !== 0;
  }
  function tileBgToHex(value, fallback) {
    const num = Number(value);
    if (!Number.isFinite(num) || num === 0) return fallback || '#353535';
    return rgbToHex(num);
  }
  // Tiles without their own color follow the global default tile color. The
  // preview paints them through one root variable, so a change of that color
  // repaints loaded, cached and lazily inserted grids at once.
  function tileBackgroundCss(meta, isDefault, hex, opacity = null) {
    const shared = !!isDefault && !!meta?.sharedBg;
    const sharedCss = 'var(--tile-default-bg, #2A2A2A)';
    if (opacity === null || opacity === undefined) return shared ? sharedCss : hex;
    if (shared) {
      return 'color-mix(in srgb, ' + sharedCss + ' ' +
        (opacity * 100 / 255).toFixed(2) + '%, transparent)';
    }
    return hex + opacity.toString(16).padStart(2, '0');
  }
  // Mirrors tile_icon_disc::icon_color_tints(): with glow on, a colored icon
  // tints its disc with its own hue; white and grey icons keep the white disc.
  function iconDiscTinted(color) {
    const rgb = String(color || '').match(/(\d+)\D+(\d+)\D+(\d+)/);
    return !!rgb && !(rgb[1] === rgb[2] && rgb[2] === rgb[3]);
  }
  // Channels (0..255) of a computed CSS color, or null when it is fully
  // transparent or unknown. Chrome reports color-mix() backgrounds (screensaver
  // tiles with an opacity) as color(srgb r g b / a) with 0..1 channels.
  function cssColorMatch(value) {
    const text = String(value || '').trim();
    const srgb = text.match(/^color\(srgb\s+([\d.eE+-]+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)(?:\s*\/\s*([\d.]+%?))?/);
    const rgb = text.match(/^rgba?\(\s*([\d.]+)[\s,]+([\d.]+)[\s,]+([\d.]+)(?:[\s,/]+([\d.]+%?))?/);
    const match = srgb || rgb;
    if (!match) return null;
    const alpha = match[4] === undefined ? 1
      : match[4].endsWith('%') ? Number(match[4].slice(0, -1)) / 100 : Number(match[4]);
    return { match, srgb: !!srgb, alpha };
  }
  function cssColorChannels(value) {
    const parsed = cssColorMatch(value);
    if (!parsed || !(parsed.alpha > 0)) return null;
    return [1, 2, 3].map(i => {
      const v = Number(parsed.match[i]) * (parsed.srgb ? 255 : 1);
      return Math.max(0, Math.min(255, Math.round(v)));
    });
  }
  // The alpha (0..1) of a computed CSS color; 0 when unknown.
  function cssColorAlpha(value) {
    const parsed = cssColorMatch(value);
    return parsed && parsed.alpha > 0 ? Math.min(1, parsed.alpha) : 0;
  }
  // Mirrors src/ui/shared/tone_color.h: the circle and the controls sit a
  // fixed step of perceived lightness (OKLCH L) above the card in the icon's
  // hue (white icons: the tile's own color, lighter); a dark icon is shown
  // lighter in its own hue.
  function toneToLinear(v) {
    v /= 255;
    return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
  }
  function toneToSrgb(v) {
    if (v <= 0) return 0;
    if (v >= 1) return 255;
    return Math.floor((v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055) * 255 + 0.5);
  }
  function toneOklch(rgb) {
    const [r, g, b] = rgb.map(toneToLinear);
    const l = Math.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const m = Math.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const s = Math.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    return { L: 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, C: Math.hypot(A, B), h: Math.atan2(B, A) };
  }
  function toneLinear(L, C, h) {
    const A = C * Math.cos(h), B = C * Math.sin(h);
    const l = (L + 0.3963377774 * A + 0.2158037573 * B) ** 3;
    const m = (L - 0.1055613458 * A - 0.0638541728 * B) ** 3;
    const s = (L - 0.0894841775 * A - 1.2914855480 * B) ** 3;
    return [4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
      -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
      -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s];
  }
  function toneRgb(L, C, h) {
    L = Math.min(1, Math.max(0, L));
    const fits = c => toneLinear(L, c, h).every(v => v >= -0.0005 && v <= 1.0005);
    if (!fits(C)) {
      let lo = 0, hi = C;
      for (let i = 0; i < 16; i++) { const mid = (lo + hi) / 2; if (fits(mid)) lo = mid; else hi = mid; }
      C = lo;
    }
    return toneLinear(L, C, h).map(toneToSrgb);
  }
  function toneBlend(under, over, opa) {
    return under.map((v, i) => Math.floor((v * (255 - opa) + over[i] * opa + 127) / 255));
  }
  // tone_color::fill(): what the circle and the controls draw. Opaque in
  // exactly their steps: 0.06 L at the default 25 %, controls at least 0.03;
  // the circle keeps 55 % of the icon's chroma. On a see-through card
  // (screensaver tiles below full Tile opacity) a veil calibrated to land on
  // the step, the controls at least opacity 32.
  function toneFill(card, icon, tinted, percent, seeThrough) {
    percent = Math.min(100, Math.max(0, percent));
    const base = toneOklch(card), seed = toneOklch(icon);
    const lift = step => tinted ? toneRgb(base.L + step, seed.C * 0.55, seed.h) : toneRgb(base.L + step, base.C, base.h);
    const discStep = percent * 0.0024;
    if (!seeThrough) {
      const control = lift(Math.max(discStep, 0.03));
      const disc = !percent ? card.slice() : discStep >= 0.03 ? control : lift(discStep);
      return { discColor: disc, controlColor: control, discOpa: percent ? 255 : 0, controlOpa: 255, disc, control, tinted };
    }
    const discOpa = Math.floor((percent * 255 + 50) / 100);
    const controlOpa = Math.max(discOpa, 32);
    const target = lift(discOpa > 32 ? discStep : 0.03);
    const color = card.map((under, i) => {
      const delta = (target[i] - under) * 255;
      return Math.min(255, Math.max(0, under + Math.trunc((delta + (delta >= 0 ? 1 : -1) * Math.floor(controlOpa / 2)) / controlOpa)));
    });
    return {
      discColor: color, controlColor: color, discOpa, controlOpa,
      disc: toneBlend(card, color, discOpa), control: toneBlend(card, color, controlOpa), tinted,
    };
  }
  // tone_color::readable_icon(): unchanged while at least 0.22 L above the
  // circle the icon gets with the default settings (tile color #1A1A1A, tile
  // color From icon 20 % like tile_tint::background, Circle strength 25 %),
  // else lighter in its own hue. The global tile color and the Circle
  // strength never change an icon's color.
  function toneReadableIcon(icon) {
    const tinted = icon[0] !== icon[1] || icon[1] !== icon[2];
    const linear = v => { const c = v / 255; return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
    const contrast = c => 1.05 / (0.2126 * linear(c[0]) + 0.7152 * linear(c[1]) + 0.0722 * linear(c[2]) + 0.05);
    let card = [26, 26, 26];
    if (tinted) {
      card = card.map((v, i) => Math.floor((v * 80 + icon[i] * 20 + 50) / 100));
      for (let i = 0; i < 40 && contrast(card) < 4.5; i++) card = card.map(v => Math.floor((v * 95 + 50) / 100));
    }
    const base = toneOklch(card), seed = toneOklch(icon);
    const circle = tinted ? toneRgb(base.L + 0.06, seed.C * 0.55, seed.h) : toneRgb(base.L + 0.06, base.C, base.h);
    const minimum = toneOklch(circle).L + 0.22;
    return seed.L >= minimum ? icon : toneRgb(minimum, seed.C, seed.h);
  }
  function toneHex(rgb) {
    return '#' + rgb.map(v => v.toString(16).padStart(2, '0')).join('');
  }
  // Default tile color "From icon" strength (tile_icon_colors::kTintDefault).
  const ICON_FILL_DEFAULT = 20;
  // A PIN-protected Folder or Settings tile shows a lock in its icon
  // (navigate renderer, icon_lock_mark.h); without an own icon the lock is
  // the icon. Folders follow their target's stored PIN (data-folder-pin-enabled),
  // Settings the stored Settings PIN.
  function previewTileLocked(typeValue, tileElem) {
    if (String(typeValue) === '7') {
      return document.getElementById('folder0_settings_pin_enabled')?.dataset.pinConfigured === '1';
    }
    return String(typeValue) === '4' && tileElem?.dataset.folderPinEnabled === '1';
  }
  const PREVIEW_LOCK_MARK = '<span class="tile-icon-lock mdi mdi-lock" aria-hidden="true"></span>';

  function applyIconDiscTint(tileElem) {
    const icon = tileElem?.querySelector(':scope > .tile-icon');
    if (!icon) return;
    const glow = tileElem.dataset.iconGlow !== '0';
    // The color the icon was given (tile_icon_disc::icon_color): a shown
    // readability lift is remembered, a new color from the editor replaces it.
    const current = cssColorChannels(getComputedStyle(icon).color);
    const currentHex = current ? toneHex(current) : '';
    const given = icon.dataset.toneShown && icon.dataset.toneShown === currentHex && icon.dataset.toneGiven
      ? icon.dataset.toneGiven.slice(1).match(/../g).map(v => parseInt(v, 16))
      : current;
    const givenHex = given ? toneHex(given) : '';
    // Mirrors tile_icon_source.cpp on_icon_color(): with Tile color "From
    // icon color" the tile takes the color the icon was given; grey and white
    // icons (off, default) keep the untinted background (tile_tint::choose).
    const fill = Number(tileElem.dataset.iconFill || 0);
    if (fill && tileElem.dataset.ruleTint !== '1' && typeof tileTintBackground === 'function' &&
        typeof tileTintChoice === 'function') {
      const choice = given ? tileTintChoice(false, '', 0, fill, givenHex) : null;
      if (choice) {
        const base = String(getComputedStyle(document.documentElement).getPropertyValue('--tile-default-bg') || '').trim();
        tileElem.style.background = tileTintBackground(base || '#1A1A1A', choice.color, choice.percent);
      } else if (tileElem.dataset.baseBg !== undefined) {
        tileElem.style.background = tileElem.dataset.baseBg;
      }
    }
    // An unset Icon color field of the edited tile shows the color the icon
    // has now (the entity's own color, e.g. a light or a detected Binary
    // sensor); only picking a color stores a fixed one.
    if (typeof currentTileTab === 'string' && tileElem.id === currentTileTab + '-tile-' + currentTileIndex) {
      const input = document.getElementById(currentTileTab + '_tile_icon_color');
      if (input && input.dataset.unset === '1' && given) input.value = givenHex;
    }
    const tinted = glow && !!given && iconDiscTinted('rgb(' + given.join(',') + ')');
    icon.classList.toggle('tile-icon-tinted', tinted);
    // Mirrors ui_surface_style::border_hint(): a glowing icon gives the tile
    // outline its hue halfway to white (lv_color_mix(white, icon, 128)) at the
    // hairline's 20 %, mostly the tile with a hint of the icon.
    // Only a card in the tile color From icon (tile_icon_disc
    // apply_tile_options); Global and Custom keep the neutral hairline.
    if (tinted && fill > 0) {
      const hint = given.map(v => Math.floor(((255 * 128 + v * 127) * 0x8081) / 0x800000));
      tileElem.style.setProperty('--tile-border-tint', 'rgba(' + hint.join(',') + ',0.20)');
    } else {
      tileElem.style.removeProperty('--tile-border-tint');
    }
    // Global Circle strength (icon_glow.h, 0..100 %).
    const glowRaw = String(getComputedStyle(document.documentElement).getPropertyValue('--icon-glow-pct')).trim();
    const glowValue = glowRaw === '' ? 25 : Number(glowRaw);
    const glowPct = Math.min(100, Math.max(0, Number.isFinite(glowValue) ? glowValue : 25));
    const background = getComputedStyle(tileElem).backgroundColor;
    const card = cssColorChannels(background) || [0, 0, 0];
    // Screensaver tiles below full Tile opacity let the wallpaper through.
    const seeThrough = cssColorAlpha(background) < 1;
    // "Circle in icon color" shows the circle of the tile color "From icon"
    // on a Global or Custom tile too (tile_icon_disc::circle_card): computed
    // for the card From icon would give at its default strength.
    let circleCard = card;
    if (tinted && !(fill > 0) && !seeThrough && typeof tileTintBackground === 'function') {
      const base = String(getComputedStyle(document.documentElement).getPropertyValue('--tile-default-bg') || '').trim();
      const familyHex = tileTintBackground(base || '#1A1A1A', givenHex, ICON_FILL_DEFAULT);
      circleCard = [1, 3, 5].map(i => parseInt(familyHex.slice(i, i + 2), 16));
    }
    const tone = toneFill(circleCard, given || [255, 255, 255], tinted, glowPct, seeThrough);
    const rgba = (color, opa) => 'rgba(' + color.join(',') + ',' + (opa / 255).toFixed(3) + ')';
    tileElem.style.setProperty('--icon-disc-bg', rgba(tone.discColor, tone.discOpa));
    // Mirrors icon_lock_mark::behind(): the lock's rim takes the circle over
    // the card, or the card when the circle is off.
    const discShown = tileElem.dataset.iconDisc === '1' ||
      (tileElem.dataset.iconDisc !== '2' && !tileElem.closest('.icon-discs-off'));
    const discAlpha = discShown ? tone.discOpa / 255 : 0;
    const lockRim = card.map((c, i) => Math.round(c * (1 - discAlpha) + tone.discColor[i] * discAlpha));
    tileElem.style.setProperty('--icon-lock-rim', 'rgb(' + lockRim.join(',') + ')');
    // Mirrors tile_icon_source::refresh_controls(): tile controls (the
    // Climate target pill, Media buttons, the Switch bar) take the circle's
    // color whenever it is tinted, in every tile color; else the neutral step.
    const controls = tinted ? tone : toneFill(card, given || [255, 255, 255], false, glowPct, seeThrough);
    tileElem.style.setProperty('--control-fill', rgba(controls.controlColor, controls.controlOpa));
    // The icon, readable like on the device (the same with every tile color,
    // Circle strength and circle option).
    if (!given) return;
    const readable = toneReadableIcon(given);
    const readableHex = toneHex(readable);
    if (readableHex === givenHex) {
      if (icon.dataset.toneShown) {
        delete icon.dataset.toneShown;
        delete icon.dataset.toneGiven;
        icon.style.color = givenHex;
      }
    } else {
      icon.dataset.toneGiven = givenHex;
      icon.dataset.toneShown = readableHex;
      icon.style.color = readableHex;
    }
  }
  // Mirrors tileBgColorFollowsDefault(): an unset color and the built-in
  // default grey (stored explicitly by older editors) follow the global
  // default tile color; every other stored color is kept.
  // Built-in default greys: tile_color::kDefault, kLegacyDefault and
  // kPreviousDefault.
  function isDefaultTileGrey(rgb) {
    return rgb === 0x1A1A1A || rgb === 0x2A2A2A || rgb === 0x222222;
  }
  function tileBgFollowsDefault(value) {
    const num = Number(value);
    return !Number.isFinite(num) || num === 0 || isDefaultTileGrey(num & 0xFFFFFF);
  }
  function tileColorHexIsDefaultGrey(hex) {
    const text = String(hex || '').trim();
    return /^#[0-9a-f]{6}$/i.test(text) && isDefaultTileGrey(parseInt(text.slice(1), 16));
  }
  // Tile color is one choice, like the device (tile_tint::choose): Global
  // follows the global tile color (stored as the default marker), Custom keeps
  // the picked color, From icon color tints the tile with the color the icon
  // shows (the icon colors' hidden "fill" checkbox). Only tiles with icon
  // colors offer From icon color. Nothing else switches the choice.
  function tileColorMode(tab) {
    if (document.getElementById(tab + '_tile_icon_fill')?.checked) return 'icon';
    if (document.getElementById(tab + '_tile_cover_fill')?.checked) return 'cover';
    return document.getElementById(tab + '_tile_color')?.dataset.bgColorDefault === '0' ? 'custom' : 'global';
  }
  function syncTileColorMode(tab) {
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const iconOffered = typeof tileTypeHasIconColors === 'function' && tileTypeHasIconColors(typeValue);
    const fill = document.getElementById(tab + '_tile_icon_fill');
    if (fill?.checked && !iconOffered) fill.checked = false;
    // Media tiles also offer "From cover" (the album cover's color).
    const coverOffered = String(typeValue) === '15';
    const coverFill = document.getElementById(tab + '_tile_cover_fill');
    if (coverFill?.checked && !coverOffered) coverFill.checked = false;
    const mode = tileColorMode(tab);
    document.getElementById(tab + '_tile_color_modes')?.querySelectorAll('[data-tile-color-mode]').forEach(button => {
      const active = button.dataset.tileColorMode === mode;
      button.classList.toggle('active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
      if (button.dataset.tileColorMode === 'icon') button.classList.toggle('hidden', !iconOffered);
      if (button.dataset.tileColorMode === 'cover') button.classList.toggle('hidden', !coverOffered);
    });
    document.getElementById(tab + '_tile_color_row')?.classList.toggle('color-hidden', mode !== 'custom');
    document.getElementById(tab + '_tile_icon_fill_row')?.classList.toggle('hidden', mode !== 'icon' && mode !== 'cover');
    const strength = document.getElementById(tab + '_tile_icon_fill_strength');
    const output = document.getElementById(tab + '_tile_icon_fill_strength_value');
    if (strength && output) output.textContent = strength.value + ' %';
  }
  function setTileColorMode(tab, mode) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const before = tileColorMode(tab);
    // Leaving Custom remembers its color for a later return.
    if (before === 'custom' && mode !== 'custom') input.dataset.customColor = input.value;
    const fill = document.getElementById(tab + '_tile_icon_fill');
    if (fill) fill.checked = mode === 'icon';
    const coverFill = document.getElementById(tab + '_tile_cover_fill');
    if (coverFill) coverFill.checked = mode === 'cover';
    const remembered = input.dataset.customColor || '';
    if (mode === 'custom') {
      if (before !== 'custom' && remembered) input.value = remembered;
      input.dataset.bgColorDefault = '0';
    } else {
      const type = document.getElementById(tab + '_tile_type')?.value || '0';
      input.value = getTileTypeMeta(type).defaultBg || '#1A1A1A';
      input.dataset.bgColorDefault = '1';
    }
    syncTileColorMode(tab);
    // The rules hide "Tint tile" while the tile follows the icon.
    if (typeof syncIconColorFields === 'function') syncIconColorFields(tab);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
    // A first Custom opens the color picker right away. The color row was
    // display:none until syncTileColorMode above, and Chrome anchors the
    // picker to the input's box from the last layout without running one:
    // with no box yet it opened in the top left corner of the window. Reading
    // the input's rect lays the row out first.
    if (mode === 'custom' && before !== 'custom' && !remembered) {
      try {
        input.getBoundingClientRect();
        if (typeof input.showPicker === 'function') input.showPicker();
      } catch (_) {}
    }
  }
  // State the firmware compares with the per-tile icon color rules, or null
  // while it is missing, unknown or unavailable (the type color applies).
  function iconColorRuleState(typeValue, entity, meta, binaryState) {
    const type = String(typeValue ?? '0');
    // Icon-and-title tiles have no state; their fixed icon color applies.
    if (typeof tileTypeHasFixedIconColorOnly === 'function' &&
        tileTypeHasFixedIconColorOnly(type)) return { state: '', display: null };
    if (type === '20') {
      if (!binaryState?.valid || binaryState.available !== true ||
          !['on', 'off'].includes(binaryState.state)) return null;
      return { state: binaryState.state, display: binarySensorPreviewStateText(binaryState) };
    }
    if (['21', '22', '23'].includes(type)) {
      let value = meta?.editableValues?.[entity];
      if (typeof value === 'string') { try { value = JSON.parse(value); } catch (_) { return null; } }
      if (!value || value.state === null || value.state === undefined || !value.available ||
          ['unknown', 'unavailable'].includes(String(value.state))) return null;
      const kind = type === '21' ? 'number' : (type === '22' ? 'select' : 'datetime');
      return { state: String(value.state), display: editablePreviewText(entity, kind, meta) };
    }
    const raw = String(meta?.values?.[entity] ?? '').trim();
    if (!raw || ['unavailable', 'unknown', 'none', 'null', '--'].includes(raw.toLowerCase())) return null;
    return { state: raw, display: null };
  }
  // Icon color of a preview tile: the per-tile rule or fixed icon color
  // (resolveIconColorRecord, same result as the firmware), else `fallback`.
  function previewIconColor(typeValue, record, entity, meta, binaryState, fallback) {
    if (!record || typeof tileTypeHasIconColors !== 'function' ||
        !tileTypeHasIconColors(typeValue)) return fallback;
    const type = String(typeValue ?? '0');
    // Rules that color the icon win (tile_icon_source::refresh_card).
    const layer = typeof iconColorRecordSource === 'function' ? iconColorRecordSource(record) : null;
    if (layer && layer.enabled && layer.icon && typeof iconColorLayerColor === 'function') {
      const color = iconColorLayerColor(record, layer, entity, meta, type);
      if (color) return color;
    }
    // Only the Sensor family colors its icon from its own state; every other
    // case shows the fixed icon color, else the type's color.
    const ownRules = ['1', '14', '20', '21', '22', '23'].includes(type) &&
      typeof iconColorOwnStateColorsIcon === 'function' && iconColorOwnStateColorsIcon(record);
    if (!ownRules) return resolveIconColorRecord(record, '', null) || fallback;
    const rule = iconColorRuleState(typeValue, entity, meta, binaryState);
    return (rule && resolveIconColorRecord(record, rule.state, rule.display)) || fallback;
  }
  // Tints a preview tile like tile_icon_source.cpp ("Tint tile" rules): the
  // tint replaces the tile color and starts from the global default tile
  // color, never from an own tile color.
  function applyTileRulesTint(el, typeValue, record, ownEntity, meta) {
    if (!el) return;
    // The icon color's "Tint tile" option follows the icon (applyIconDiscTint)
    // from this untinted background, unless a rule tint wins.
    el.dataset.baseBg = el.style.background || '';
    const fill = record && typeof parseIconColorRecord === 'function' ? parseIconColorRecord(record).fill : 0;
    if (fill) el.dataset.iconFill = String(fill);
    else delete el.dataset.iconFill;
    const tint = record && typeof iconColorTilePreviewTint === 'function'
      ? iconColorTilePreviewTint(String(typeValue ?? '0'), record, ownEntity, meta) : null;
    el.dataset.ruleTint = tint ? '1' : '0';
    if (!tint) return;
    const base = String(getComputedStyle(document.documentElement).getPropertyValue('--tile-default-bg') || '').trim();
    el.style.background = tileTintBackground(base || '#1A1A1A', tint.color, tint.percent);
  }
  function snapshotBgColorIsDefault(snapshot) {
    return String(snapshot?.bg_color_default || '0') === '1' ||
      tileColorHexIsDefaultGrey(snapshot?.color);
  }
  function tileColorInputIsDefault(tab) {
    const input = document.getElementById(tab + '_tile_color');
    return !!input && (input.dataset.bgColorDefault === '1' || tileColorHexIsDefaultGrey(input.value));
  }
  function setTileColorInputFromStored(tab, value, fallback) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const follows = tileBgFollowsDefault(value);
    input.value = follows ? (fallback || '#2A2A2A') : tileBgToHex(value, fallback || '#2A2A2A');
    input.dataset.bgColorDefault = follows ? '1' : '0';
    delete input.dataset.customColor;
    syncTileColorMode(tab);
  }
  function setTileColorInputFromSnapshot(tab, snapshot) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const meta = getTileTypeMeta(snapshot?.type || '0');
    const isDefault = snapshotBgColorIsDefault(snapshot);
    input.value = isDefault ? (meta.defaultBg || '#2A2A2A') : (snapshot?.color || meta.defaultBg || '#2A2A2A');
    input.dataset.bgColorDefault = isDefault ? '1' : '0';
    delete input.dataset.customColor;
    syncTileColorMode(tab);
  }
  // Picking a color selects Tile color Custom. Rules are never switched off:
  // while a rule "Tint tile" applies, it wins (tile_tint::choose).
  function markTileColorInputExplicit(tab) {
    const input = document.getElementById(tab + '_tile_color');
    if (input) input.dataset.bgColorDefault = '0';
    const fill = document.getElementById(tab + '_tile_icon_fill');
    const followed = !!fill?.checked;
    if (fill) fill.checked = false;
    syncTileColorMode(tab);
    if (followed && typeof syncIconColorFields === 'function') syncIconColorFields(tab);
  }
  function resetTileColor(tab) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const meta = getTileTypeMeta(typeValue);
    input.value = meta.defaultBg || '#2A2A2A';
    input.dataset.bgColorDefault = '1';
    const fill = document.getElementById(tab + '_tile_icon_fill');
    if (fill) fill.checked = false;
    syncTileColorMode(tab);
    if (isScreensaverTileTab(tab)) {
      const opacity = document.getElementById('screensaver_tile_opacity');
      if (opacity) opacity.value = String(SCREENSAVER_TILE_DEFAULT_OPACITY);
    }
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }

  function renderTileFromData(tab, index, tile, sensorMeta) {
    const el = document.getElementById(tab + '-tile-' + index);
    if (!el) return;
    const metaValues = sensorMeta?.values || {};
    const metaUnits = sensorMeta?.units || {};
    const metaIcons = sensorMeta?.icons || {};
    const metaNames = sensorMeta?.names || {};
    el.dataset.index = index.toString();
    const typeValue = String(tile?.type ?? '0');
    const meta = getTileTypeMeta(typeValue);
    if (typeValue === '17' &&
        currentTileTab === tab &&
        currentTileIndex === index &&
        el.classList.contains('climate-content-editing')) {
      syncClimateSlotFields(tab);
      return;
    }
    let cls = ['tile'];
    if (meta.css) cls.push(meta.css);
    if (typeValue === '0' && (!meta.css || meta.css !== 'empty')) cls.push('empty');
    el.className = cls.join(' ');
    if (typeValue === '5') applySwitchPreviewLayout(el, tile.switch_style, Number(tile.span_h) === 0.5);
    el.dataset.type = typeValue;
    el.dataset.iconDisc = ['1', '2'].includes(String(tile?.icon_disc)) ? String(tile.icon_disc) : '0';
    el.dataset.iconGlow = ['0', 'false'].includes(String(tile?.icon_glow)) ? '0' : '1';
    el.classList.toggle('tile-border-hidden', ['8','9','10'].includes(typeValue) && Number(tile.sensor_display_mode) === 1);
    applyCompactSensorPreview(el, typeValue, tile, tile.sensor_display_mode, tile.sensor_value_font);
    if (typeValue === '4') {
      el.dataset.navigateTarget = String(tile.navigate_target || 0);
      el.dataset.folderPinEnabled = tile.folder_pin_enabled === true ? '1' : '0';
    } else {
      delete el.dataset.navigateTarget;
      delete el.dataset.folderPinEnabled;
    }
    if (typeValue === '0') el.style.background = 'transparent';
    else {
      const isDefaultBg = tileBgFollowsDefault(tile.bg_color);
      const bg = tileBackgroundCss(meta, isDefaultBg,
        tileBgToHex(tile.bg_color, meta.defaultBg || '#353535'));
      if (isScreensaverTileTab(tab)) {
        const opacity = clampInt(tile.background_opacity, 0, 255,
                                 SCREENSAVER_TILE_DEFAULT_OPACITY);
        el.style.background = tileBackgroundCss(meta, isDefaultBg,
          tileBgToHex(tile.bg_color, meta.defaultBg || '#353535'), opacity);
      } else {
        el.style.background = bg;
      }
    }
    const sensorValueClass = getSensorValueFontClass(tile.sensor_value_font);
    if (typeValue === '0') {
      el.innerHTML = '';
      applyTileAriaLabel(el, '', typeValue);
    }
    else {
      const previewKind = meta.preview || 'none';
      const iconEntity = (isEditablePreview(previewKind) || previewKind === 'sensor' ||
                          previewKind === 'binary_sensor' ||
                          previewKind === 'switch' ||
                          previewKind === 'weather' || previewKind === 'media' ||
                          previewKind === 'climate' || previewKind === 'cover' ||
                          previewKind === 'device' || previewKind === 'camera')
        ? (tile.sensor_entity || '')
        : (typeValue === '2' ? (sensorMeta?.sceneEntities?.[tile.scene_alias] || '') : '');
      const rawIcon = tile.icon_name || '';
      let iconName = resolveIconName(
        rawIcon,
        iconEntity,
        metaIcons);
      if (previewKind === 'camera' && !iconName &&
          !isExplicitlyDisabledValue(rawIcon)) {
        iconName = 'video';
      }
      let climatePreviewState = null;
      if (previewKind === 'climate') {
        climatePreviewState = parseClimatePreviewPayload(
          tile.sensor_entity ? (metaValues[tile.sensor_entity] ?? '') : '');
        if (!normalizeMdiIconName(rawIcon) &&
            !isExplicitlyDisabledValue(rawIcon)) {
          iconName = climatePreviewIcon(climatePreviewState, iconName);
        }
      }
      let coverPreviewState = null;
      if (previewKind === 'cover') {
        coverPreviewState = parseCoverPreviewPayload(
          tile.sensor_entity ? (metaValues[tile.sensor_entity] ?? '') : '');
        if (!normalizeMdiIconName(rawIcon) &&
            !isExplicitlyDisabledValue(rawIcon)) {
          iconName = coverPreviewIcon(coverPreviewState, iconName);
        }
      }
      const deviceKind = previewKind === 'device' ? devicePreviewKind(typeValue) : '';
      let devicePreviewState = null;
      if (deviceKind) {
        devicePreviewState = parseDevicePreviewPayload(deviceDetailPayload(tile.sensor_entity || '', sensorMeta));
        if (!normalizeMdiIconName(rawIcon) && !isExplicitlyDisabledValue(rawIcon)) {
          iconName = devicePreviewIcon(deviceKind, devicePreviewState);
        }
      }
      let binarySensorPreviewState = null;
      if (previewKind === 'binary_sensor') {
        binarySensorPreviewState = parseBinarySensorPreviewPayload(
          tile.sensor_entity ? (metaValues[tile.sensor_entity] ?? '') : '');
        iconName = resolveBinarySensorPreviewIcon(
          rawIcon, tile.sensor_entity || '', binarySensorPreviewState,
          metaIcons);
      }

      let html = '';
      const locked = typeof previewTileLocked === 'function' && previewTileLocked(typeValue, el);
      const lockIsIcon = locked && !iconName;
      if (lockIsIcon) iconName = 'lock';

      if (iconName) {
        const iconColor = previewIconColor(typeValue, tile.icon_colors, tile.sensor_entity || '',
          sensorMeta, binarySensorPreviewState, previewKind === 'climate'
            ? climatePreviewColor(climatePreviewState)
            : (previewKind === 'cover'
              ? coverPreviewColor(coverPreviewState)
              : (deviceKind
                ? devicePreviewColor(deviceKind, devicePreviewState)
                : (previewKind === 'binary_sensor'
                  ? binarySensorPreviewColor(binarySensorPreviewState)
                  : ''))));
        const iconStyle = iconColor ? ' style="color:' + escapeHtml(iconColor) + '"' : '';
        html += '<i class="mdi mdi-' + escapeHtml(iconName) + ' tile-icon"' + iconStyle + '>' +
          (locked && !lockIsIcon ? PREVIEW_LOCK_MARK : '') + '</i>';
      }

      let displayTitle = tile.title || '';
      if (previewKind === 'camera' && !displayTitle && tile.sensor_entity) {
        displayTitle = metaNames[tile.sensor_entity] ||
          titleFromEntity(tile.sensor_entity);
      }
      if (displayTitle.length) {
        html += '<div class="tile-title" id="' + tab + '-tile-' + index + '-title">' +
          tileTitleHtml(displayTitle) + '</div>';
      }
      applyTileAriaLabel(el, displayTitle, typeValue);

      if (previewKind === 'weather') {
        html += '<div class="tile-ghost-icon"><i class="mdi mdi-weather-partly-cloudy"></i></div>';
      }
      if (previewKind === 'media') {
        html += '<div class="tile-ghost-icon"><i class="mdi mdi-music"></i></div>';
      }

      if (previewKind === 'sensor') {
        let value = '--';
        if (tile.sensor_entity) value = formatSensorValue(metaValues[tile.sensor_entity] ?? '--', tile.sensor_decimals);
        const unit = resolveUnitValue(tile.sensor_unit || '', tile.sensor_entity || '', metaUnits);
        html += '<div class="tile-value ' + sensorValueClass + '" id="' + tab + '-tile-' + index + '-value">' +
          escapeHtml(value) +
          (unit ? '<span class="tile-unit">' + escapeHtml(unit) + '</span>' : '') +
          '</div>';
      }
      if (previewKind === 'climate') {
        // Layout "with value" and half height: the value pair beside the
        // disc; half height has no mini fields.
        const climateHalf = Number(tile.span_h) === 0.5;
        const climateValue = Number(tile.sensor_display_mode) === 1;
        el.classList.toggle('climate-header', climateValue && !climateHalf);
        if (climateHalf || climateValue) {
          html += '<div class="tile-value tile-switch-state">' +
            escapeHtml(climatePreviewHeaderText(climatePreviewState)) + '</div>';
        }
        if (!climateHalf) {
          html += climatePreviewSlots(
            climatePreviewState,
            tile.span_w || 1,
            tile.span_h || 1,
            decodeClimateSlotConfig(tile.sensor_gauge_min || 0),
            decodeClimateTargetLayouts(tile.sensor_gauge_max || 0),
            tile.climate_geometry || tile.scene_alias || '',
            climateValue);
        }
      }
      if (previewKind === 'cover') html += coverPreviewExtraHtml(coverPreviewState, Number(tile.span_h) === 0.5);
      if (deviceKind) html += devicePreviewExtraHtml(deviceKind, devicePreviewState, Number(tile.span_h) === 0.5);
      if (previewKind === 'binary_sensor') {
        html += '<div class="tile-value tile-binary-sensor-value ' + (Number(tile.sensor_value_font) ? sensorValueClass : '') + '" id="' +
          tab + '-tile-' + index + '-value">' +
          escapeHtml(binarySensorPreviewStateText(binarySensorPreviewState)) +
          '</div>';
      }
      if (isEditablePreview(previewKind)) html += '<div class="tile-value tile-editable-value ' + sensorValueClass + '">' + escapeHtml(editablePreviewText(iconEntity, previewKind, sensorMeta)) + '</div>';
      if (previewKind === 'clock') {
        const flags = normalizeClockFlags(tile.sensor_decimals);
        const clockTimeFont = tile.key_code || 40;
        const clockDateFont = Math.min(72, Number(tile.key_modifier || 20));
        const clockTimeFormat = (tile.sensor_gauge_min !== undefined) ? tile.sensor_gauge_min : 0;
        const clockDateFormat = (tile.sensor_gauge_max !== undefined) ? tile.sensor_gauge_max : 0;
        if (flags & 1) html += '<div class="tile-clock-time" ' + getClockPreviewTextStyle(clockTimeFont, 40, '#fff') + '>' + getClockPreviewTime(clockTimeFormat) + '</div>';
        if (flags & 2) html += '<div class="tile-clock-date" ' + getClockPreviewTextStyle(clockDateFont, 24, '#fff') + '>' + getClockPreviewDate(clockDateFormat) + '</div>';
      }
      if (previewKind === 'text') {
        const textValue = tile.text_value || tile.scene_alias || tile.key_macro || '';
        if (textValue) {
          const textClass = getSensorValueFontClass(tile.sensor_value_font);
          html += '<div class="tile-text ' + textClass + '">' +
            escapeHtml(textValue) + '</div>';
        }
      }
      if (previewKind === 'switch') html += switchPreviewExtraHtml(tile.switch_style, Number(tile.span_h) === 0.5);
      html += getTileResizeHandlesHtml(typeValue);
      el.innerHTML = html;
      if (typeof applyTileRulesTint === 'function') {
        applyTileRulesTint(el, typeValue, tile.icon_colors, tile.sensor_entity || '', sensorMeta);
      }
      applyIconDiscTint(el);
      if (previewKind === 'cover') {
        applyCoverPreview(el, coverPreviewState, Number(tile.span_h) === 0.5);
        // The header classes need the bar class set above (switch-tall).
        applyCompactSensorPreview(el, typeValue, tile, tile.sensor_display_mode, tile.sensor_value_font);
      }
      if (deviceKind) {
        applyDevicePreview(el, deviceKind, devicePreviewState, Number(tile.span_h) === 0.5);
        applyCompactSensorPreview(el, typeValue, tile, tile.sensor_display_mode, tile.sensor_value_font);
      }
      if (typeValue === '9') fitCompactClockPreview(el);
    }
    if (currentTileTab === tab && currentTileIndex === index) el.classList.add('active');
    if (typeValue === '5' && tile.sensor_entity) {
      const state = parseSwitchPayload(metaValues[tile.sensor_entity] ?? '');
      applySwitchPreviewState(el, state, tile.sensor_entity);
    }
  }

  function fetchTileGridData(tab, force = false) {
    if (!force && tileDataLoadedTabs.has(tab)) {
      return Promise.resolve(getTilesData(tab));
    }
    if (tileDataLoadPromises[tab]) return tileDataLoadPromises[tab];
    const folderId = getFolderIdForTab(tab);
    if (folderId === undefined) return Promise.resolve([]);

    const baseline = getTilesData(tab).map(tile => JSON.stringify(tile));
    tileDataLoadPromises[tab] = fetch(
      '/api/tiles?folder=' + encodeURIComponent(folderId))
      .then(async response => {
        if (!response.ok) throw new Error('Tiles HTTP ' + response.status);
        const tiles = await response.json();
        if (!Array.isArray(tiles)) throw new Error('Invalid tile grid response');
        const current = getTilesData(tab);
        tilesData[tab] = tiles.map((tile, index) => {
          const changed = JSON.stringify(current[index]) !== baseline[index];
          return current[index] && (changed || drafts[tab]?.[index]?._dirty)
            ? current[index] : tile;
        });
        tileDataLoadedTabs.add(tab);
        return tilesData[tab];
      })
      .finally(() => { delete tileDataLoadPromises[tab]; });
    return tileDataLoadPromises[tab];
  }

  function loadSensorValues(
      refreshTiles = false, forceMetaFetch = false, tabsOverride = null) {
    // A Settings move to or from the parking slot shows ahead of the device
    // (previewSettingsTileTransfer); stored tile data would draw it back.
    if (dragSource || resizeState || settingsTileTransferInFlight) {
      queueDeferredSensorRefresh(refreshTiles);
      return Promise.resolve(false);
    }
    const requestedTabs = Array.isArray(tabsOverride)
      ? tabsOverride
      : (refreshTiles
          ? (currentTileTab ? [currentTileTab] : tileTabs.slice(0, 1))
          : (currentTileTab && tileDataLoadedTabs.has(currentTileTab)
              ? [currentTileTab]
              : []));
    const tabs = Array.from(new Set(requestedTabs)).filter(tab =>
      tileTabs.includes(tab) && getFolderIdForTab(tab) !== undefined);
    const tileRequests = refreshTiles
      ? tabs.map(tab => fetchTileGridData(tab, true))
      : tabs.map(tab => Promise.resolve(getTilesData(tab)));

    return Promise.all([fetchSensorMetaCache(forceMetaFetch), ...tileRequests])
    .then(results => {
      // A refresh may have started shortly before the drag and only arrive
      // during it. In that case it must not overwrite the local preview with the
      // old device state.
      if (dragSource || resizeState || settingsTileTransferInFlight) {
        queueDeferredSensorRefresh(refreshTiles);
        return;
      }
      const sensorMeta = normalizeSensorMetaPayload(results[0] || {});
      sensorMetaCache = sensorMeta;
      tabs.forEach((tab, idx) => {
        // Metadata may finish after another edit; render the current cache.
        const tilesForRender = getTilesData(tab);
        if (!Array.isArray(tilesForRender)) return;
        tilesForRender.forEach((tile, i) => renderTileFromData(tab, i, tile, sensorMeta));
        layoutTiles(tab, tilesForRender);
      });
      if (currentTileIndex !== -1 && currentTileTab) {
        restoreCurrentTileSelectionUi();
      } else if (!isScreensaverTileTab(currentTileTab)) {
        restoreSelectedTileState();
      }
      return true;
    })
    .catch(err => {
      console.error('Sensor values load failed:', err);
      return false;
    });
  }

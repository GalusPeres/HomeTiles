
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
  // The card is --tile-card: the global tile color in the dark theme, the
  // light card in the light one (tileDefaultBgColor); a black tile shows the
  // light theme's screen color (tileBgColorOrDefault).
  function tileBackgroundCss(meta, isDefault, hex, opacity = null, typeValue = null) {
    const shared = !!isDefault && !!meta?.sharedBg;
    const black = !shared && /^#0{6}$/.test(String(hex));
    if (black && !isDefault && opacity === null && tileTypeBlackIsTransparent(typeValue)) return 'transparent';
    const cardCss = shared ? 'var(--tile-card, #2A2A2A)' : black ? 'var(--pv-black-tile, #000000)' : hex;
    if (opacity === null || opacity === undefined) return cardCss;
    if (shared || black) {
      return 'color-mix(in srgb, ' + cardCss + ' ' +
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
    const lift = step => tinted ? toneRgb(base.L + toneStep(step), seed.C * 0.55, seed.h)
      : toneRgb(base.L + toneStep(step), base.C, base.h);
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
    // The light theme shows every colored icon in its own color, on every
    // surface (tone_color::readable_icon).
    if (panelThemeLight() && tinted) return icon;
    if (panelLight()) {
      // The icon's own color, like Home Assistant; only an icon without a
      // color (white, almost white, the text color) takes the icon grey.
      const seed = toneOklch(icon);
      const text = toneHex(icon) === panelRoleHex('text', '#1a1a1a').toLowerCase();
      return text || (seed.L > 0.95 && (!tinted || seed.C < 0.08)) ? (panelRole('icon') || icon) : icon;
    }
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
  // The panel's theme (html[data-panel-theme], ui_theme.h) and its roles
  // (--pv-*, written from ui_theme.h by web_admin_styles.cpp; --pvd-* and
  // --pvl-* keep the dark and the light table).
  // tile_surface (tile_icon_disc.h): a tile in its own color follows that
  // color's lightness; in the light theme a tile's Design shows the dark
  // design (Weather, Media while it plays, Design Dark, tinted tiles with the
  // global option). While withSurface() runs, panelLight() and panelRole()
  // answer for that surface (globalThis.hometilesPreviewSurface: 0 dark,
  // 1 light, 2 dark with the light greys (ui_theme::kDarkTinted), unset the
  // page's theme; each function reads it by itself).
  function panelThemeLight() {
    return typeof document !== 'undefined' && document.documentElement?.dataset?.panelTheme === 'light';
  }
  function panelLight() {
    const surface = globalThis.hometilesPreviewSurface;
    if (surface === 0 || surface === 1 || surface === 2) return surface === 1;
    return typeof document !== 'undefined' && document.documentElement?.dataset?.panelTheme === 'light';
  }
  function withSurface(surface, run) {
    const saved = globalThis.hometilesPreviewSurface;
    if (surface === 0 || surface === 1 || surface === 2) globalThis.hometilesPreviewSurface = surface;
    try { return run(); } finally { globalThis.hometilesPreviewSurface = saved; }
  }
  // The role table a surface reads a role from: 'd' dark, 'l' light, '' the
  // page's. Surface 2 (ui_theme::kDarkTinted) has its own greys ('j').
  function surfaceRoleTable(theme, name, pageLight) {
    // Surface 2 (ui_theme::kDarkTinted): the dark roles, its own lighter greys.
    if (theme === 2) {
      const grey = ['icon-off', 'icon-inactive', 'icon-rest', 'icon-unavailable'].includes(name);
      return grey ? 'j' : (pageLight ? 'd' : '');
    }
    if ((theme === 0 || theme === 1) && (theme === 1) !== pageLight) return theme === 1 ? 'l' : 'd';
    return '';
  }
  function panelVar(name, fallback) {
    if (typeof document === 'undefined' || typeof getComputedStyle !== 'function') return fallback;
    const value = String(getComputedStyle(document.documentElement).getPropertyValue(name) || '').trim();
    return value || fallback;
  }
  function panelRole(name) {
    const surface = globalThis.hometilesPreviewSurface;
    const pageLight = typeof document !== 'undefined' && document.documentElement?.dataset?.panelTheme === 'light';
    const table = surfaceRoleTable(surface, name, pageLight);
    const prefix = table ? '--pv' + table + '-' : '--pv-';
    const hex = panelVar(prefix + name, '');
    return /^#[0-9a-f]{6}$/i.test(hex) ? [1, 3, 5].map(i => parseInt(hex.slice(i, i + 2), 16)) : null;
  }
  // The roles a surface overrides on its tile (the preview's CSS reads them).
  const SURFACE_ROLES = ['screen', 'card', 'text', 'icon', 'icon-off', 'icon-inactive', 'icon-rest',
    'text-secondary', 'text-soft', 'icon-unavailable', 'text-rgb'];
  function surfaceTheme(surface) {
    if (surface === 'jewel') return 2;
    return surface === 'light' ? 1 : surface === 'dark' ? 0 : -1;
  }
  // tone_color::light_surface(): text, icons and circles on a color above
  // --pv-light-surface (OKLCH L) follow the light theme's rules.
  function toneLightSurface(rgb) {
    const limit = Number(panelVar('--pv-light-surface', '0.72'));
    return toneOklch(rgb).L > (Number.isFinite(limit) ? limit : 0.72);
  }
  // The filled tile of the light theme (ui_theme kJewel*, --pv-jewel-*).
  function jewelVar(name, fallback) {
    const value = Number(panelVar('--pv-jewel-' + name, String(fallback)));
    return Number.isFinite(value) ? value : fallback;
  }
  // tone_color::jewel_card(): the card below its icon's own lightness, in its
  // hue, with chroma up to the strength's cap.
  // tone_color::jewel_idle_card(): a filled tile without a color.
  function toneJewelIdleCard() {
    return toneRgb(jewelVar('idle', 0.59), 0, 0);
  }
  function toneJewelCard(icon, strength) {
    if (toneNoColor(icon)) return toneRgb(jewelVar('neutral', 0.63), 0, 0);
    const seed = toneOklch(icon);
    const L = Math.max(jewelVar('min', 0.42), Math.min(jewelVar('max', 0.68), seed.L - jewelVar('below', 0.18)));
    const cap = jewelVar('chroma', 0.1) * (strength || 20) / 20;
    return toneRgb(L, Math.min(seed.C * jewelVar('card-share', 0.85), cap), seed.h);
  }
  // tone_color::jewel_fill(): circle and controls one step above the card.
  function toneJewelFill(card, icon, glowPct, strength) {
    const base = toneOklch(card), seed = toneOklch(icon);
    const cap = jewelVar('chroma', 0.1) * jewelVar('circle-cap', 1.3) * (strength || 20) / 20;
    const C = toneNoColor(icon) ? 0 : Math.min(seed.C * jewelVar('circle-share', 0.75), cap);
    const step = jewelVar('step', 0.05) * glowPct / 25;
    const control = Math.max(step, 0.03);
    const disc = glowPct ? toneRgb(base.L + step, C, seed.h) : card.slice();
    return {discColor: disc, controlColor: toneRgb(base.L + control, C, seed.h), discOpa: glowPct ? 255 : 0,
      controlOpa: 255, disc, control: toneRgb(base.L + control, C, seed.h), tinted: true};
  }
  // tile_icon_colors::fills(): "Ausfuellen" (user 2026-10-11, the one tile
  // color option), also a former "From cover" or rule "Tint tile"; the fixed
  // strength (tile_icon_colors::kTintDefault) when filled, 0 without.
  function tileFillOf(iconColors) {
    if (!iconColors || typeof parseIconColorRecord !== 'function') return 0;
    const parsed = parseIconColorRecord(iconColors);
    return Number(parsed.fill || 0) > 0 || Number(parsed.cover?.tile || 0) > 0 || Number(parsed.source?.tile || 0) > 0
      ? ICON_FILL_DEFAULT : 0;
  }
  // tileTypeKeepsOwnColor() (tile_config.h): only Clock, Text and Pixel
  // animation keep an own color; every other tile takes its color from its
  // icon.
  const KEEPS_OWN_COLOR_TYPES = ['9', '10', '16'];
  function tileTypeKeepsOwnColor(typeValue) { return KEEPS_OWN_COLOR_TYPES.includes(String(typeValue ?? '0')); }
  // tileTransparent() (tile_config.h): a Clock or Text tile in black has no
  // card and no line ("Transparent", user 2026-10-12).
  function tileTypeBlackIsTransparent(typeValue) { return ['9', '10'].includes(String(typeValue ?? '0')); }
  // tileFills() (tile_config.h): "Fill" of a tile, never on the tiles that
  // keep an own color.
  function tileFills(typeValue, iconColors) { return tileTypeKeepsOwnColor(typeValue) ? 0 : tileFillOf(iconColors); }
  // tileHasOwnColor() (tile_config.h).
  function tileHasOwnColor(bgValue, iconColors, typeValue) {
    return tileTypeKeepsOwnColor(typeValue) && tileBgValueIsSet(bgValue) && !tileBgFollowsDefault(bgValue);
  }
  // tile_surface_of() (tile_renderer.cpp): a tile in its own color follows
  // its lightness; every other tile, and the screensaver, keeps the theme.
  function tileSurfaceOf(tab, previewKind, bgValue, iconColors, typeValue) {
    if (typeof isScreensaverTileTab === 'function' && isScreensaverTileTab(tab)) {
      // The screensaver's own design (screensaver_config.h theme): Dark or
      // Light for all its tiles, Automatic follows the panel.
      const theme = document.getElementById('screensaverGrid')?.dataset.ssTheme;
      return theme === '0' ? 'dark' : theme === '1' ? 'light' : '';
    }
    if (!tileHasOwnColor(bgValue, iconColors, typeValue)) return '';
    const own = Number(bgValue) & 0xFFFFFF;
    if (!own) return '';
    return toneLightSurface([(own >> 16) & 0xFF, (own >> 8) & 0xFF, own & 0xFF]) ? 'light' : 'dark';
  }
  // tile_surface_rule_of() (tile_renderer.cpp): in the light theme a tile with
  // "Ausfuellen" is filled from the start; a rule row with its own "Fuellen"
  // changes a tile between the white and the filled look.
  function tileFollowsTint(tab, previewKind, bgValue, iconColors, typeValue) {
    if (typeof isScreensaverTileTab === 'function' && isScreensaverTileTab(tab)) return false;
    const rows = typeof iconColorRowsFill === 'function' && iconColorRowsFill(iconColors);
    return panelThemeLight() && (tileFills(typeValue, iconColors) > 0 || rows) &&
      !tileHasOwnColor(bgValue, iconColors, typeValue);
  }
  // ui_surface_style frame_line(): a solid line, on the theme's plain card a
  // fixed neutral (light #DDDDDD, dark #292929), on any other card that card
  // a little darker on the light theme's screen (36 of black) and a little
  // lighter on the dark one (lv_color_mix 23 of white). user 2026-10-11.
  function applyFrameLine(el) {
    if (!el?.style?.setProperty || typeof getComputedStyle !== 'function') return;
    const card = cssColorChannels(getComputedStyle(el).backgroundColor);
    if (!card) return;
    // The plain card of either theme keeps that theme's fixed line.
    let line;
    if (card.every(v => v === 255)) line = [221, 221, 221];
    else if (card.every(v => v === 26)) line = [41, 41, 41];
    else if (!panelThemeLight()) line = card.map(v => Math.floor((255 * 23 + v * 232) * 0x8081 / 0x800000));
    // frame_line_shown(): a filled card of the light theme (white words)
    // draws no line (user 2026-10-11, b391).
    else if (!toneLightSurface(card)) {
      el.style.setProperty('--tile-border-tint', 'transparent');
      return;
    }
    else line = card.map(v => Math.floor((v * 219) * 0x8081 / 0x800000));
    el.style.setProperty('--tile-border-tint', 'rgb(' + line.join(',') + ')');
  }
  // Marks a tile with its surface: the other theme's roles on it (CSS) and,
  // for the dark design, its card.
  function applyTileSurface(el, surface) {
    if (!el) return;
    if (surface) el.dataset.surface = surface;
    else delete el.dataset.surface;
    const theme = surfaceTheme(surface);
    const pageLight = panelThemeLight();
    for (const name of SURFACE_ROLES) {
      const table = surfaceRoleTable(theme, name, pageLight);
      if (table) el.style.setProperty('--pv-' + name, 'var(--pv' + table + '-' + name + ')');
      else el.style.removeProperty('--pv-' + name);
    }
    // A screensaver tile in the other design paints that theme's card
    // (tileBackgroundCss uses --tile-card).
    const saverCard = el.closest?.('.screensaver-tile-grid') && (theme === 0 || theme === 1)
      ? (theme === 0 ? 'var(--tile-default-bg)' : 'var(--pvl-card)') : '';
    if (saverCard) el.style.setProperty('--tile-card', saverCard);
    else el.style.removeProperty('--tile-card');

  }
  // ui_theme::unavailable_icon(): the light theme's one unavailable grey for
  // every domain, the dark theme each type's own `darkGrey`.
  function unavailableIconHex(darkGrey) {
    return panelThemeLight() ? panelRoleHex('icon-unavailable', '#BDBDBD') : darkGrey;
  }
  // tone_color::no_color(): white, almost white or grey-white.
  function toneNoColor(rgb) {
    const seed = toneOklch(rgb);
    const hue = rgb[0] !== rgb[1] || rgb[1] !== rgb[2];
    return seed.L > 0.9 && (!hue || seed.C < 0.08);
  }
  // tone_color::light_accent(): the light theme's light color.
  function toneLightAccent(rgb) {
    return panelLight() ? toneHaLightColor(rgb) : rgb;
  }
  // switch_state_icon_color() (tile_renderer.cpp): the light theme shows a
  // light's color as Home Assistant does, white light in its light grey.
  function toneLightShown(hex) {
    if (!panelThemeLight() || !/^#[0-9a-f]{6}$/i.test(String(hex))) return hex;
    const rgb = [1, 3, 5].map(i => parseInt(String(hex).slice(i, i + 2), 16));
    return toneHex(toneHaLightColor(rgb)).toUpperCase();
  }
  // tone_color::ha_light_color(): a light's color as Home Assistant shows
  // it: an HSV saturation below 0.4 raised to 0.4, white (below 0.1) a grey
  // of value 225.
  function toneHaLightColor(rgb) {
    const [r, g, b] = rgb.map(v => v / 255);
    const max = Math.max(r, g, b), min = Math.min(r, g, b), d = max - min;
    let sat = max > 0 ? d / max : 0, value = max;
    if (sat >= 0.4 || max <= 0) return rgb;
    let hue = 0;
    if (d > 0) {
      if (max === r) hue = ((g - b) / d) % 6;
      else if (max === g) hue = (b - r) / d + 2;
      else hue = (r - g) / d + 4;
      hue *= 60;
      if (hue < 0) hue += 360;
    }
    if (sat < 0.1) value = 225 / 255;
    else sat = 0.4;
    const channel = n => {
      const k = (n + hue / 60) % 6;
      const m = Math.max(0, Math.min(1, k, 4 - k));
      return Math.floor((value - value * sat * m) * 255 + 0.5);
    };
    return [channel(5), channel(3), channel(1)];
  }
  // tone_color::notch_on(): a handle line, the card's color on a light fill.
  function toneNotchOn(fill, card) {
    return toneLightSurface(fill) ? card : [255, 255, 255];
  }
  // tone_color::mark_on(): dark on a light surface (the dark theme keeps its
  // card cut-out there), white on a dark one.
  function toneMarkOn(surface, cutout) {
    if (toneLightSurface(surface)) return panelLight() ? (panelRole('text') || [26, 26, 26]) : cutout;
    return [255, 255, 255];
  }
  function panelRoleHex(name, fallback) {
    const rgb = panelRole(name);
    return rgb ? toneHex(rgb) : fallback;
  }
  // tone_color::lifted(): the light theme steps down by --pv-light-step of
  // the dark step.
  function toneStep(step) {
    if (!panelLight()) return step;
    const scale = Number(panelVar('--pv-light-step', '0.833333'));
    return -step * (Number.isFinite(scale) ? scale : 5 / 6);
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
    if (!tileElem) return;
    // The tile's surface rules (tile_surface) while its colors are computed.
    const surfaceSaved = globalThis.hometilesPreviewSurface;
    const surfaceMode = typeof surfaceTheme === 'function' ? surfaceTheme(tileElem.dataset?.surface || '') : -1;
    // 0 dark, 1 light, 2 a filled tile (ui_theme::kDarkTinted): its rules everywhere.
    if (surfaceMode >= 0) globalThis.hometilesPreviewSurface = surfaceMode;
    try {
    // Cut-outs in the card's color (the Media play glyph,
    // tile_icon_disc::refresh_cutouts) follow the card after every tint.
    const cutCard = typeof getComputedStyle === 'function'
      ? cssColorChannels(getComputedStyle(tileElem).backgroundColor) : null;
    if (cutCard && tileElem.style?.setProperty) tileElem.style.setProperty('--tile-cut', toneHex(cutCard));
    const icon = tileElem.querySelector(':scope > .tile-icon');
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
    if (fill && typeof tileTintBackground === 'function' && typeof tileTintChoice === 'function') {
      let choice = given ? tileTintChoice(false, '', 0, fill, givenHex) : null;
      // An icon without a color (white, almost white light) never tints the
      // card (tile_icon_source without_white).
      if (choice && toneNoColor(given)) choice = null;
      // White light fills in the neutral grey (tile_icon_source with_white).
      if (!choice && given && tileElem.dataset.followTint === '1' && toneNoColor(given) && toneOklch(given).L <= 0.95) {
        choice = { color: givenHex.toUpperCase(), percent: fill };
      }
      if (choice) {
        setTileTintBackground(tileElem, choice.color, choice.percent);
      } else if (tileElem.dataset.neutralIdle === '1') {
        setTileTintBackground(tileElem, '#FFFFFF', 0, true);
      } else if (tileElem.dataset.baseBg !== undefined) {
        tileElem.style.background = tileElem.dataset.baseBg;
      }
      // The surface may have changed with the tint (tile_surface::change).
      const nowMode = surfaceTheme(tileElem.dataset?.surface || '');
      globalThis.hometilesPreviewSurface = nowMode >= 0 ? nowMode : undefined;
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
    // The frame line follows the card (ui_surface_style frame_line).
    applyFrameLine(tileElem);
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
      // The tile's card (a screensaver tile in the other design has its own).
      const base = String(getComputedStyle(tileElem).getPropertyValue('--tile-card') || '').trim();
      const familyHex = tileTintBackground(base || '#1A1A1A', givenHex, ICON_FILL_DEFAULT);
      circleCard = [1, 3, 5].map(i => parseInt(familyHex.slice(i, i + 2), 16));
    }
    let tone = toneFill(circleCard, given || [255, 255, 255], tinted, glowPct, seeThrough);
    // The light theme's tile circle (tone_color::tile_fill, lab 2026-10-10):
    // the shown icon color at --pv-light-circle of the Circle strength over
    // the card itself; controls at least 10 %.
    const lightCircle = panelLight() && !seeThrough;
    if (lightCircle) {
      const shown = toneReadableIcon(given || [255, 255, 255]);
      const picked = tinted || !iconDiscTinted('rgb(' + shown.join(',') + ')') ? shown : (panelRole('icon') || shown);
      // tone_color::circle_source(): a white light's grey takes the neutral #BDBDBD.
      const over = toneNoColor(picked) ? [189, 189, 189] : picked;
      const share = Number(panelVar('--pv-light-circle', '0.8'));
      const amount = glowPct * (Number.isFinite(share) ? share : 0.8) / 100;
      const blendOver = value => toneBlend(card, over, Math.round(Math.min(1, value) * 255));
      const disc = glowPct ? blendOver(amount) : card.slice();
      const control = blendOver(Math.max(amount, 0.1));
      tone = { discColor: disc, controlColor: control, discOpa: glowPct ? 255 : 0, controlOpa: 255, disc, control, tinted };
    }
    // A filled tile of the light theme (tone_color::jewel_fill): one step
    // above its card in the icon's hue.
    if (tinted && !seeThrough && tileElem.dataset.surface === 'jewel' && typeof toneJewelFill === 'function') {
      tone = toneJewelFill(card, given, glowPct, Number(tileElem.dataset.jewelStrength) || 20);
    }
    const rgba = (color, opa) => 'rgba(' + color.join(',') + ',' + (opa / 255).toFixed(3) + ')';
    tileElem.style.setProperty('--icon-disc-bg', rgba(tone.discColor, tone.discOpa));
    // Mirrors icon_lock_mark::behind(): the lock's rim takes the circle over
    // the card.
    const discAlpha = tone.discOpa / 255;
    const lockRim = card.map((c, i) => Math.round(c * (1 - discAlpha) + tone.discColor[i] * discAlpha));
    tileElem.style.setProperty('--icon-lock-rim', 'rgb(' + lockRim.join(',') + ')');
    // Mirrors tile_icon_source::refresh_controls(): tile controls (the
    // Climate target pill, Media buttons, the Switch bar) take the circle's
    // color whenever it is tinted, in every tile color; else the neutral step.
    const controls = tinted || lightCircle ? tone : toneFill(card, given || [255, 255, 255], false, glowPct, seeThrough);
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
    } finally {
      globalThis.hometilesPreviewSurface = surfaceSaved;
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
  // The color of a tile without an icon (Clock, Text, Pixel animation):
  // Global is the theme's card (stored as the default marker), Custom keeps
  // the picked color; on Clock and Text black is Transparent. A clicked
  // choice holds while editing (data-color-mode): dragging the Custom picker
  // over black must not jump to Transparent (user 2026-10-12); a loaded
  // tile's choice follows its stored color.
  function tileColorMode(tab) {
    const input = document.getElementById(tab + '_tile_color');
    if (input?.dataset.bgColorDefault !== '0') return 'global';
    if (input.dataset.colorMode === 'custom' || input.dataset.colorMode === 'transparent') return input.dataset.colorMode;
    const type = document.getElementById(tab + '_tile_type')?.value || '0';
    return tileTypeBlackIsTransparent(type) && /^#0{6}$/.test(String(input.value)) ? 'transparent' : 'custom';
  }
  // user 2026-10-11: a tile with an icon offers only "Fill" (its icon's
  // color); Clock, Text and Pixel animation keep Global or a Custom color.
  function syncTileColorMode(tab) {
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const keepsOwn = tileTypeKeepsOwnColor(typeValue);
    const iconOffered = !keepsOwn && typeof tileTypeHasIconColors === 'function' && tileTypeHasIconColors(typeValue);
    const fill = document.getElementById(tab + '_tile_icon_fill');
    if (fill?.checked && !iconOffered) fill.checked = false;
    const input = document.getElementById(tab + '_tile_color');
    if (input && !keepsOwn) input.dataset.bgColorDefault = '1';
    const mode = tileColorMode(tab);
    const modes = document.getElementById(tab + '_tile_color_modes');
    modes?.querySelectorAll('[data-tile-color-mode]').forEach(button => {
      const active = button.dataset.tileColorMode === mode;
      button.classList.toggle('active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
    modes?.classList.toggle('hidden', !keepsOwn);
    modes?.previousElementSibling?.classList?.toggle('hidden', !keepsOwn);
    modes?.querySelector('[data-tile-color-mode="transparent"]')?.classList.toggle('hidden', !tileTypeBlackIsTransparent(typeValue));
    document.getElementById(tab + '_tile_color_row')?.classList.toggle('color-hidden', !keepsOwn || mode !== 'custom');
    document.getElementById(tab + '_tile_icon_fill_row')?.classList.toggle('hidden', !iconOffered);
    if (typeof fitChoiceRows === 'function') fitChoiceRows(tab);
  }
  function setTileColorMode(tab, mode) {
    const input = document.getElementById(tab + '_tile_color');
    if (!input) return;
    // Only Clock, Text and Pixel animation have a tile color choice; a tile
    // with an icon takes its color from the icon (no hidden picker opens).
    if (!tileTypeKeepsOwnColor(document.getElementById(tab + '_tile_type')?.value || '0')) return;
    const before = tileColorMode(tab);
    // Leaving Custom remembers its color for a later return.
    if (before === 'custom' && mode !== 'custom') input.dataset.customColor = input.value;
    const remembered = input.dataset.customColor || '';
    if (mode === 'custom' || mode === 'transparent') input.dataset.colorMode = mode;
    if (mode === 'custom') {
      if (before !== 'custom' && remembered) input.value = remembered;
      // From Transparent a color to start from (black would stay Transparent).
      else if (before === 'transparent') input.value = '#333333';
      input.dataset.bgColorDefault = '0';
    } else if (mode === 'transparent') {
      input.value = '#000000';
      input.dataset.bgColorDefault = '0';
    } else {
      delete input.dataset.colorMode;
      const type = document.getElementById(tab + '_tile_type')?.value || '0';
      input.value = getTileTypeMeta(type).defaultBg || '#1A1A1A';
      input.dataset.bgColorDefault = '1';
    }
    syncTileColorMode(tab);
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
  // `out`, when given, receives the matching state line's own "Fuellen"
  // (`fill`: 1, 0, or -1 when "Ausfuellen" decides).
  function previewIconColor(typeValue, record, entity, meta, binaryState, fallback, out = null) {
    if (out) out.fill = -1;
    if (!record || typeof tileTypeHasIconColors !== 'function' ||
        !tileTypeHasIconColors(typeValue)) return fallback;
    const type = String(typeValue ?? '0');
    // Rules that color the icon win (tile_icon_source::refresh_card).
    const layer = typeof iconColorRecordSource === 'function' ? iconColorRecordSource(record) : null;
    if (layer && layer.enabled && iconColorRuleColorsIcon(layer) && typeof iconColorLayerColor === 'function') {
      const color = iconColorLayerColor(record, layer, entity, meta, type, out);
      if (color) return color;
    }
    // Only the Sensor family colors its icon from its own state; every other
    // case shows the fixed icon color, else the type's color.
    const ownRules = ['1', '14', '20', '21', '22', '23'].includes(type) &&
      typeof iconColorOwnStateColorsIcon === 'function' && iconColorOwnStateColorsIcon(record);
    if (!ownRules) return resolveIconColorRecord(record, '', null) || fallback;
    const rule = iconColorRuleState(typeValue, entity, meta, binaryState);
    return (rule && resolveIconColorRecord(record, rule.state, rule.display, true, out)) || fallback;
  }
  // The row fill a preview tile shows (tile_icon_disc::rule_fill): 'on',
  // 'off' or '' when "Ausfuellen" decides.
  function setPreviewRuleFill(el, out) {
    if (!el) return;
    if (out?.fill === 1) el.dataset.ruleFill = 'on';
    else if (out?.fill === 0) el.dataset.ruleFill = 'off';
    else delete el.dataset.ruleFill;
  }
  // Tints a preview tile like tile_icon_source.cpp ("Tint tile" rules): the
  // tint replaces the tile color and starts from the global default tile
  // color, never from an own tile color.
  function applyTileRulesTint(el, typeValue, record, ownEntity, meta) {
    if (!el) return;
    // A tile with "Fill" follows its icon (applyIconDiscTint) from this
    // untinted background; rules color only the icon (user 2026-10-11).
    el.dataset.baseBg = el.style.background || '';
    // The matching rule row's own "Fuellen" decides (tile_icon_disc::shown_fill):
    // on fills at "Fill" or the default strength, off keeps the plain card,
    // also on a tile filled from the start (kNeutralIdle).
    const ownFill = tileFills(typeValue, record);
    const rule = el.dataset.ruleFill || '';
    const fill = rule === 'off' ? 0 : rule === 'on' ? (ownFill || ICON_FILL_DEFAULT) : ownFill;
    if (fill) el.dataset.iconFill = String(fill);
    else delete el.dataset.iconFill;
    if (rule === 'off') delete el.dataset.neutralIdle;
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
    delete input.dataset.colorMode;
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
    delete input.dataset.colorMode;
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
    delete input.dataset.colorMode;
    const typeValue = document.getElementById(tab + '_tile_type')?.value || '0';
    const meta = getTileTypeMeta(typeValue);
    input.value = meta.defaultBg || '#2A2A2A';
    input.dataset.bgColorDefault = '1';
    const fill = document.getElementById(tab + '_tile_icon_fill');
    if (fill) fill.checked = false;
    syncTileColorMode(tab);
    updateTilePreview(tab);
    updateDraft(tab);
    scheduleAutoSave(tab);
  }

  // `forcedSurface`: the second pass of a tile whose tint changed its surface
  // (tile_surface::change), drawn from the start with that surface.
  function renderTileFromData(tab, index, tile, sensorMeta, forcedSurface) {
    const el = document.getElementById(tab + '-tile-' + index);
    if (!el) return;
    // A page's folder tile is one cell in its field (page-fields.js).
    if (typeof pageFieldOfTile === 'function' && pageFieldOfTile(tab, tile)) tile = {...tile, span_w: 1, span_h: 1};
    // The whole tile draws with its surface's rules (tile_surface).
    const surfaceType = String(tile?.type ?? '0');
    const surfaceKind = getTileTypeMeta(surfaceType).preview || '';
    // A Camera tile's head over its picture (picture.h head_over_picture).
    const cameraHead = surfaceKind === 'camera'
      ? cameraHeadSurface(cameraPictureOptionsOf(tile), Number(tile?.span_h) === 0.5) : '';
    const surface = forcedSurface || (cameraHead ? (cameraHead === 'auto' ? '' : cameraHead)
      : surfaceType === '0' || typeof tileSurfaceOf !== 'function' ? ''
      : tileSurfaceOf(tab, surfaceKind, tile?.bg_color, tile?.icon_colors, surfaceType));
    if (surfaceType !== '0' && !cameraHead && typeof tileFollowsTint === 'function' &&
        tileFollowsTint(tab, surfaceKind, tile?.bg_color, tile?.icon_colors, surfaceType)) {
      el.dataset.followTint = '1';
    } else {
      delete el.dataset.followTint;
    }
    // A tile with "Fill" shows the neutral grey without a tint (kNeutralIdle).
    if (el.dataset.followTint === '1' && tileFillOf(tile?.icon_colors) > 0) {
      el.dataset.neutralIdle = '1';
    } else {
      delete el.dataset.neutralIdle;
    }
    const surfaceSaved = globalThis.hometilesPreviewSurface;
    const surfaceMode = typeof surfaceTheme === 'function' ? surfaceTheme(surface) : -1;
    // 0 dark, 1 light, 2 a filled tile (ui_theme::kDarkTinted): its rules everywhere.
    if (surfaceMode >= 0) globalThis.hometilesPreviewSurface = surfaceMode;
    try {
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
    resetTileClasses(el, cls);
    if (typeValue === '5') applySwitchPreviewLayout(el, tile.switch_style, Number(tile.span_h) === 0.5);
    el.dataset.type = typeValue;
    // Every circle shows, in its icon's color (user 2026-10-11: no circle options).
    el.dataset.iconDisc = '0';
    el.dataset.iconGlow = '1';
    el.classList.toggle('tile-border-hidden', ['8','9','10'].includes(typeValue) && Number(tile.sensor_display_mode) === 1);
    applyCompactSensorPreview(el, typeValue, tile, tile.sensor_display_mode, tile.sensor_value_font);
    if (typeValue === '4') {
      el.dataset.navigateTarget = String(tile.navigate_target || 0);
      el.dataset.folderPinEnabled = tile.folder_pin_enabled === true ? '1' : '0';
    } else {
      delete el.dataset.navigateTarget;
      delete el.dataset.folderPinEnabled;
    }
    delete el.dataset.bgOpacity;
    if (typeValue === '0') el.style.background = 'transparent';
    else {
      // A tile with an icon takes its color from the icon: no own color.
      const isDefaultBg = tileBgFollowsDefault(tile.bg_color) || !tileTypeKeepsOwnColor(typeValue);
      const bg = tileBackgroundCss(meta, isDefaultBg,
        tileBgToHex(tile.bg_color, meta.defaultBg || '#353535'), null, typeValue);
      el.classList.toggle('tile-transparent', bg === 'transparent');
      if (isScreensaverTileTab(tab)) {
        // One opacity for every screensaver tile (screensaver footer).
        const opacity = screensaverTileOpacity();
        el.style.background = tileBackgroundCss(meta, isDefaultBg,
          tileBgToHex(tile.bg_color, meta.defaultBg || '#353535'), opacity);
        el.dataset.bgOpacity = String(opacity);
        // A fully transparent card casts no shadow (apply_slot_tile_shadows).
        el.classList.toggle('screensaver-bg-clear', opacity === 0);
      } else {
        el.style.background = cameraHead ? cameraHeadBackground(cameraHead) : bg;
      }
    }
    if (typeof applyTileSurface === 'function') applyTileSurface(el, surface);
    if (typeValue !== '0') applyFrameLine(el);
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
        : (typeValue === '2' ? sceneEntityOf(tile.scene_alias, sensorMeta?.sceneEntities) : '');
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
      if (previewKind === 'calendar' && !normalizeMdiIconName(rawIcon) && !isExplicitlyDisabledValue(rawIcon)) {
        iconName = 'calendar';
      }
      let binarySensorPreviewState = null;
      if (previewKind === 'binary_sensor') {
        binarySensorPreviewState = parseBinarySensorPreviewPayload(
          tile.sensor_entity ? (metaValues[tile.sensor_entity] ?? '') : '');
        iconName = resolveBinarySensorPreviewIcon(
          rawIcon, tile.sensor_entity || '', binarySensorPreviewState,
          metaIcons);
      }
      if (previewKind === 'media') {
        iconName = mediaPreviewIcon(parseMediaPreviewPayload(
          tile.sensor_entity ? (sensorMeta?.mediaValues?.[tile.sensor_entity] ?? '') : ''), iconName);
      }

      let html = '';
      const locked = typeof previewTileLocked === 'function' && previewTileLocked(typeValue, el);
      const lockIsIcon = locked && !iconName;
      if (lockIsIcon) iconName = 'lock';

      delete el.dataset.ruleFill;
      if (iconName) {
        const ruleFill = {};
        const iconColor = previewIconColor(typeValue, tile.icon_colors, tile.sensor_entity || '',
          sensorMeta, binarySensorPreviewState, previewKind === 'climate'
            ? climatePreviewColor(climatePreviewState)
            : (previewKind === 'cover'
              ? coverPreviewColor(coverPreviewState)
              : (deviceKind
                ? devicePreviewColor(deviceKind, devicePreviewState)
                : (previewKind === 'binary_sensor'
                  ? binarySensorPreviewColor(binarySensorPreviewState)
                  : (previewKind === 'calendar' ? calendarPreviewIconColor(tile) : '')))), ruleFill);
        if (typeof setPreviewRuleFill === 'function') setPreviewRuleFill(el, ruleFill);
        const iconStyle = iconColor ? ' style="color:' + escapeHtml(iconColor) + '"' : '';
        html += '<i class="mdi mdi-' + escapeHtml(iconName) + ' tile-icon"' + iconStyle + '>' +
          (locked && !lockIsIcon ? PREVIEW_LOCK_MARK : '') + '</i>';
      }

      let displayTitle = tile.title || '';
      if (previewKind === 'camera' && !displayTitle && tile.sensor_entity) {
        displayTitle = metaNames[tile.sensor_entity] ||
          titleFromEntity(tile.sensor_entity);
      }
      if (previewKind === 'calendar') displayTitle = calendarPreviewTitle(tile, displayTitle, sensorMeta);
      if (displayTitle.length) {
        html += '<div class="tile-title" id="' + tab + '-tile-' + index + '-title">' +
          tileTitleHtml(displayTitle) + '</div>';
      }
      applyTileAriaLabel(el, displayTitle, typeValue);


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
            unitTextHtml(climatePreviewHeaderText(climatePreviewState)) + '</div>';
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
      if (previewKind === 'calendar') html += calendarPreviewExtraHtml(tile, sensorMeta);
      if (previewKind === 'binary_sensor') {
        html += '<div class="tile-value tile-binary-sensor-value ' + (Number(tile.sensor_value_font) ? sensorValueClass : '') + '" id="' +
          tab + '-tile-' + index + '-value">' +
          escapeHtml(binarySensorPreviewStateText(binarySensorPreviewState)) +
          '</div>';
      }
      if (isEditablePreview(previewKind)) html += '<div class="tile-value tile-editable-value ' + sensorValueClass + '">' + editablePreviewHtml(iconEntity, previewKind, sensorMeta) + '</div>';
      if (previewKind === 'clock') {
        const flags = normalizeClockFlags(tile.sensor_decimals);
        const clockTimeFont = tile.key_code || 40;
        const clockDateFont = Math.min(72, Number(tile.key_modifier || 20));
        const clockTimeFormat = (tile.sensor_gauge_min !== undefined) ? tile.sensor_gauge_min : 0;
        const clockDate = clockDateOptions(tile.sensor_gauge_max !== undefined ? tile.sensor_gauge_max : 0);
        applyClockPreviewAlign(el, clockDate.align);
        let clockLines = '';
        if (flags & 1) clockLines += '<div class="tile-clock-time" ' + getClockPreviewTextStyle(clockTimeFont, 40, panelRoleHex('text', '#fff')) + '>' + clockDigitsHtml(getClockPreviewTime(clockTimeFormat)) + '</div>';
        if ((flags & 2) || clockDate.weekday) clockLines += '<div class="tile-clock-date" ' + getClockPreviewTextStyle(clockDateFont, 20, panelRoleHex('text', '#fff')) + '>' + clockDigitsHtml(getClockPreviewDate(clockDate.format, clockDate.weekday, (flags & 2) !== 0)) + '</div>';
        // The analog and the wall clock: a face and the lines beside or below
        // it (layoutClockFacePreview, after the tile has its size).
        html += clockDate.style ? '<svg class="tile-clock-face" data-options="' + Number(tile.sensor_gauge_max || 0) + '"></svg>'
          + '<div class="tile-clock-face-text">' + clockLines + '</div>' : clockLines;
      }
      if (previewKind === 'text') {
        applyTextPreviewAlign(el, tile.sensor_gauge_max);
        const textValue = tile.text_value || tile.scene_alias || tile.key_macro || '';
        if (textValue) {
          const textClass = getSensorValueFontClass(tile.sensor_value_font);
          html += '<div class="tile-text ' + textClass + '">' +
            escapeHtml(textValue) + '</div>';
        }
      }
      if (previewKind === 'switch') html += switchPreviewExtraHtml(tile.switch_style, Number(tile.span_h) === 0.5);
      html += getTileResizeHandlesHtml(typeValue);
      // A media tile's shown artwork survives the re-render; applyMediaPreview
      // keeps it when it is still the same picture.
      const keptCover = previewKind === 'media'
        ? el.querySelector(':scope > .media-preview-cover:not([hidden])') : null;
      el.innerHTML = html;
      if (keptCover) el.appendChild(keptCover);
      if (previewKind === 'weather') {
        applyWeatherPreview(el, applyWeatherSensorOverrides(parseWeatherPreviewPayload(
          tile.sensor_entity ? (sensorMeta?.weatherValues?.[tile.sensor_entity] ?? '') : ''),
          tile.weather_temperature_sensor || '', tile.weather_humidity_sensor || '', metaValues, metaUnits),
          tile, iconName, previewIconColor(typeValue, tile.icon_colors, tile.sensor_entity || '', sensorMeta, null, ''));
      }
      if (previewKind === 'media') {
        applyMediaPreview(el, parseMediaPreviewPayload(
          tile.sensor_entity ? (sensorMeta?.mediaValues?.[tile.sensor_entity] ?? '') : ''),
          tile, iconName, tile.sensor_entity ? (metaNames[tile.sensor_entity] || '') : '');
      }
      if (previewKind === 'energy_flow' && typeof applyEnergyFlowPreview === 'function') {
        applyEnergyFlowPreview(el, tile, sensorMeta);
      } else {
        el.classList.remove('energy-flow-tile', 'energy-flow-no-card');
      }
      if (typeof applyTileRulesTint === 'function') {
        applyTileRulesTint(el, typeValue, tile.icon_colors, tile.sensor_entity || '', sensorMeta);
      }
      if (previewKind === 'media') {
        applyMediaCoverTint(el, tile.icon_colors, sensorMeta?.mediaCoverColors?.[tile.sensor_entity || ''] || '');
        if (el.dataset.neutralIdle === '1' && el.dataset.surface !== 'jewel') setTileTintBackground(el, '#FFFFFF', 0, true);
      }
      applyIconDiscTint(el);
      if (previewKind === 'camera') {
        applyCameraPicturePreview(el, cameraPictureOptionsOf(tile), Number(tile.span_h) === 0.5, tile);
      }
      if (previewKind === 'cover') {
        applyCoverPreview(el, coverPreviewState, Number(tile.span_h) === 0.5);
        // The header classes need the bar class set above (switch-tall).
        applyCompactSensorPreview(el, typeValue, tile, tile.sensor_display_mode, tile.sensor_value_font);
      }
      if (deviceKind) {
        applyDevicePreview(el, deviceKind, devicePreviewState, Number(tile.span_h) === 0.5);
        applyCompactSensorPreview(el, typeValue, tile, tile.sensor_display_mode, tile.sensor_value_font);
      }
      if (typeValue === '9') {
        // A title or icon moves the clock down (clock/renderer.cpp).
        el.classList.toggle('clock-has-header', !!(displayTitle || iconName));
        fitCompactClockPreview(el);
        el.dataset.clockOptions = String(Number(tile.sensor_gauge_max || 0));
        layoutClockFacePreview(el);
      }
    }
    if (currentTileTab === tab && currentTileIndex === index) el.classList.add('active');
    if (typeValue === '5' && tile.sensor_entity) {
      const state = parseSwitchPayload(metaValues[tile.sensor_entity] ?? '');
      applySwitchPreviewState(el, state, tile.sensor_entity);
    }
    } finally {
      globalThis.hometilesPreviewSurface = surfaceSaved;
    }
    // Its tint changed the surface: drawn again on it from the start.
    if (!forcedSurface && el.dataset.surface === 'jewel' && surface !== 'jewel') {
      renderTileFromData(tab, index, tile, sensorMeta, 'jewel');
      return;
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
    // The layout window's tiles are not the panel's: values only.
    if (typeof layoutWindowOpen === 'function' && layoutWindowOpen()) refreshTiles = false;
    // A Settings move to or from the parking slot shows ahead of the device
    // (previewSettingsTileTransfer); stored tile data would draw it back.
    if (dragSource || resizeState || settingsTileTransfersInFlight) {
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
      if (dragSource || resizeState || settingsTileTransfersInFlight) {
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
      } else if (!isScreensaverTileTab(currentTileTab) && !pageFieldPicked()) {
        // A picked free page field is a selection too (user 2026-10-22: every
        // refresh jumped back to the last tile).
        restoreSelectedTileState();
      }
      return true;
    })
    .catch(err => {
      console.error('Sensor values load failed:', err);
      return false;
    });
  }

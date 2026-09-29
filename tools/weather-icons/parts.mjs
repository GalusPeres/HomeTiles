// Filled, multi-color weather icons derived from the Material Design Icons
// (Apache-2.0, see src/fonts/MaterialDesignIcons-LICENSE.txt) weather-*
// outlines: the outer contours are kept and the inner holes dropped, and each
// icon is split into parts that are drawn in their own color. Coordinates use
// the 24x24 MDI grid. tools/generate-weather-icon-fonts.mjs turns every layer
// into one glyph; layers are drawn bottom to top.

export const COLORS = {
  sun: '#FFC53D',
  cloud: '#EEF2F7',
  cloudGray: '#C3CCD8',
  cloudBack: '#8E9BB0',
  cloudStorm: '#9EAABA',
  rain: '#4DA3FF',
  ice: '#CFE8FF',
  bolt: '#FFC53D',
  moon: '#FFE08A',
  star: '#FFFFFF',
  fog: '#8E9BB0',
  wind: '#A9C7E8',
  alert: '#FFB020',
};

const circle = (cx, cy, r) =>
  `M${cx - r},${cy}A${r},${r} 0 1,1 ${cx + r},${cy}A${r},${r} 0 1,1 ${cx - r},${cy}Z`;
const rect = (x1, y1, x2, y2) => `M${x1},${y1}H${x2}V${y2}H${x1}Z`;
// Rounded line (capsule) from (x1,y1) to (x2,y2) with width w.
function capsule(x1, y1, x2, y2, w) {
  const r = w / 2;
  const dx = x2 - x1, dy = y2 - y1;
  const len = Math.hypot(dx, dy);
  const nx = (-dy / len) * r, ny = (dx / len) * r;
  const f = (v) => +v.toFixed(3);
  return `M${f(x1 + nx)},${f(y1 + ny)}L${f(x2 + nx)},${f(y2 + ny)}` +
         `A${r},${r} 0 0,0 ${f(x2 - nx)},${f(y2 - ny)}` +
         `L${f(x1 - nx)},${f(y1 - ny)}A${r},${r} 0 0,0 ${f(x1 + nx)},${f(y1 + ny)}Z`;
}

// weather-cloudy outer contour (y 5..19) and the raised variant of the
// precipitation icons (y 2..16).
const CLOUD_FULL = 'M6,19A5,5 0 0,1 1,14A5,5 0 0,1 6,9C7,6.65 9.3,5 12,5C15.43,5 18.24,7.66 18.5,11.03L19,11A4,4 0 0,1 23,15A4,4 0 0,1 19,19H6Z';
const CLOUD_HIGH = 'M6,16A5,5 0 0,1 1,11A5,5 0 0,1 6,6C7,3.65 9.3,2 12,2C15.43,2 18.24,4.66 18.5,8.03L19,8A4,4 0 0,1 23,12A4,4 0 0,1 19,16H6Z';
// Precipitation cloud: CLOUD_HIGH scaled to 86 % around its top center, so
// drops and flakes fit below it (y 2..14).
const PRECIP = { s: 0.86, dx: 12 * 0.14, dy: 2 * 0.14 };
const precipCloud = (color) => ({ color, paths: [{ d: CLOUD_HIGH, t: PRECIP }] });

// Small MDI drop (weather-snowy-rainy), 14..18.5 x 14.5..21.
const DROP = 'M18.5,18.67C18.5,19.96 17.5,21 16.25,21C15,21 14,19.96 14,18.67C14,17.12 16.25,14.5 16.25,14.5C16.25,14.5 18.5,17.12 18.5,18.67Z';
const drop = (dx, dy) => ({ d: DROP, t: { s: 1, dx, dy } });
// MDI snowflake (weather-snowy), centered near (12,18).
const FLAKE = 'M7.88,18.07L10.07,17.5L8.46,15.88C8.07,15.5 8.07,14.86 8.46,14.46C8.85,14.07 9.5,14.07 9.88,14.46L11.5,16.07L12.07,13.88C12.21,13.34 12.76,13.03 13.29,13.17C13.83,13.31 14.14,13.86 14,14.4L13.41,16.59L15.6,16C16.14,15.86 16.69,16.17 16.83,16.71C16.97,17.24 16.66,17.79 16.12,17.93L13.93,18.5L15.54,20.12C15.93,20.5 15.93,21.15 15.54,21.54C15.15,21.93 14.5,21.93 14.12,21.54L12.5,19.93L11.93,22.12C11.79,22.66 11.24,22.97 10.71,22.83C10.17,22.69 9.86,22.14 10,21.6L10.59,19.41L8.4,20C7.86,20.14 7.31,19.83 7.17,19.29C7.03,18.76 7.34,18.21 7.88,18.07Z';
const flake = (s, cx, cy) => ({ d: FLAKE, t: { s, dx: cx - 12 * s, dy: cy - 18 * s } });
// MDI bolt (weather-lightning).
const BOLT = 'M12,11H15L13,15H15L11.25,22L12,17H9.5L12,11Z';

// weather-sunny without the inner hole: disc plus six rays.
const SUNNY = [
  'M12,7A5,5 0 0,1 17,12A5,5 0 0,1 12,17A5,5 0 0,1 7,12A5,5 0 0,1 12,7Z',
  'M12,2L14.39,5.42C13.65,5.15 12.84,5 12,5C11.16,5 10.35,5.15 9.61,5.42L12,2Z',
  'M3.34,7L7.5,6.65C6.9,7.16 6.36,7.78 5.94,8.5C5.5,9.24 5.25,10 5.11,10.79L3.34,7Z',
  'M3.36,17L5.12,13.23C5.26,14 5.53,14.78 5.95,15.5C6.37,16.24 6.91,16.86 7.5,17.37L3.36,17Z',
  'M20.65,7L18.88,10.79C18.74,10 18.47,9.23 18.05,8.5C17.63,7.78 17.1,7.15 16.5,6.64L20.65,7Z',
  'M20.64,17L16.5,17.36C17.09,16.85 17.62,16.22 18.04,15.5C18.46,14.77 18.73,14 18.87,13.21L20.64,17Z',
  'M12,22L9.59,18.56C10.33,18.83 11.14,19 12,19C12.82,19 13.63,18.83 14.37,18.56L12,22Z',
];

// weather-partly-cloudy: sun disc (center 10.5,10.5, r 5.5) with its four
// rays, and the cloud filled from its three bumps.
const PARTLY_SUN = [
  circle(10.5, 10.5, 5.5),
  'M13.55,3.64C13,3.4 12.45,3.23 11.88,3.12L14.37,1.82L15.27,4.71C14.76,4.29 14.19,3.93 13.55,3.64Z',
  'M6.09,4.44C5.6,4.79 5.17,5.19 4.8,5.63L4.91,2.82L7.87,3.5C7.25,3.71 6.65,4.03 6.09,4.44Z',
  'M18,9.71C17.91,9.12 17.78,8.55 17.59,8L19.97,9.5L17.92,11.73C18.03,11.08 18.05,10.4 18,9.71Z',
  'M3.04,11.3C3.11,11.9 3.24,12.47 3.43,13L1.06,11.5L3.1,9.28C3,9.93 2.97,10.61 3.04,11.3Z',
];
const PARTLY_CLOUD = [
  circle(12, 16, 6), circle(6, 18, 4), circle(19, 19, 3),
  rect(6, 18, 12, 22), rect(12, 16, 19, 22),
];

// weather-night without the inner hole: filled crescent and two stars.
const MOON = 'M18.97,15.95C19.8,15.87 20.69,17.05 20.16,17.8C19.84,18.25 19.5,18.67 19.08,19.07C15.17,23 8.84,23 4.94,19.07C1.03,15.17 1.03,8.83 4.94,4.93C5.34,4.53 5.76,4.17 6.21,3.85C6.96,3.32 8.14,4.21 8.06,5.04C7.79,7.9 8.75,10.87 10.95,13.06C13.14,15.26 16.1,16.22 18.97,15.95Z';
const STARS = [
  'M17.75,4.09L15.22,6.03L16.13,9.09L13.5,7.28L10.87,9.09L11.78,6.03L9.25,4.09L12.44,4L13.5,1L14.56,4L17.75,4.09Z',
  'M21.25,11L19.61,12.25L20.2,14.23L18.5,13.06L16.8,14.23L17.39,12.25L15.75,11L17.81,10.95L18.5,9L19.19,10.95L21.25,11Z',
];

// weather-fog: flat-bottom cloud filled, plus its four bars.
const FOG_CLOUD = 'M1,12A5,5 0 0,1 6,7C7,4.65 9.3,3 12,3C15.43,3 18.24,5.66 18.5,9.03L19,9C21.19,9 22.97,10.76 23,13H1.1L1,12Z';
const FOG_BARS = [
  'M3,15H13A1,1 0 0,1 14,16A1,1 0 0,1 13,17H3A1,1 0 0,1 2,16A1,1 0 0,1 3,15Z',
  'M16,15H21A1,1 0 0,1 22,16A1,1 0 0,1 21,17H16A1,1 0 0,1 15,16A1,1 0 0,1 16,15Z',
  'M3,19H5A1,1 0 0,1 6,20A1,1 0 0,1 5,21H3A1,1 0 0,1 2,20A1,1 0 0,1 3,19Z',
  'M8,19H21A1,1 0 0,1 22,20A1,1 0 0,1 21,21H8A1,1 0 0,1 7,20A1,1 0 0,1 8,19Z',
];

// weather-windy lines (already filled strokes).
const WIND_LINES = [
  'M4,10A1,1 0 0,1 3,9A1,1 0 0,1 4,8H12A2,2 0 0,0 14,6A2,2 0 0,0 12,4C11.45,4 10.95,4.22 10.59,4.59C10.2,5 9.56,5 9.17,4.59C8.78,4.2 8.78,3.56 9.17,3.17C9.9,2.45 10.9,2 12,2A4,4 0 0,1 16,6A4,4 0 0,1 12,10H4Z',
  'M19,12A1,1 0 0,0 20,11A1,1 0 0,0 19,10C18.72,10 18.47,10.11 18.29,10.29C17.9,10.68 17.27,10.68 16.88,10.29C16.5,9.9 16.5,9.27 16.88,8.88C17.42,8.34 18.17,8 19,8A3,3 0 0,1 22,11A3,3 0 0,1 19,14H5A1,1 0 0,1 4,13A1,1 0 0,1 5,12H19Z',
  'M18,18H4A1,1 0 0,1 3,17A1,1 0 0,1 4,16H18A3,3 0 0,1 21,19A3,3 0 0,1 18,22C17.17,22 16.42,21.66 15.88,21.12C15.5,20.73 15.5,20.1 15.88,19.71C16.27,19.32 16.9,19.32 17.29,19.71C17.47,19.89 17.72,20 18,20A1,1 0 0,0 19,19A1,1 0 0,0 18,18Z',
];
// weather-windy-variant: cloud filled, plus the lower wind line.
const WINDY_CLOUD = 'M6,6L6.69,6.06C7.32,3.72 9.46,2 12,2A5.5,5.5 0 0,1 17.5,7.5L17.42,8.45C17.88,8.16 18.42,8 19,8A3,3 0 0,1 22,11A3,3 0 0,1 19,14H6A4,4 0 0,1 2,10A4,4 0 0,1 6,6Z';

// alert-circle (filled) for Home Assistant's "exceptional" condition.
const ALERT_CIRCLE = 'M13,13H11V7H13M13,17H11V15H13M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2Z';

const P = (d, t) => ({ d, t });

// Keyed by the MDI icon name the weather tile and popup resolve, either from
// the bridge icon field or from the Home Assistant condition.
export const ICONS = {
  'weather-sunny': [{ color: COLORS.sun, paths: SUNNY.map((d) => P(d)) }],
  'weather-night': [
    { color: COLORS.moon, paths: [P(MOON)] },
    { color: COLORS.star, paths: STARS.map((d) => P(d)) },
  ],
  'weather-partly-cloudy': [
    { color: COLORS.sun, paths: PARTLY_SUN.map((d) => P(d)) },
    { color: COLORS.cloud, paths: PARTLY_CLOUD.map((d) => P(d)) },
  ],
  // Night variant of partly cloudy (weather-night-partly-cloudy): the filled
  // weather-night crescent, scaled into the upper right, behind the cloud.
  'weather-night-partly-cloudy': [
    { color: COLORS.moon, paths: [P(MOON, { s: 0.62, dx: 8.26, dy: -1.05 })] },
    { color: COLORS.cloudGray, paths: PARTLY_CLOUD.map((d) => P(d)) },
  ],
  'weather-cloudy': [
    { color: COLORS.cloudBack, paths: [P(CLOUD_FULL, { s: 0.62, dx: 8.88, dy: -0.1 })] },
    { color: COLORS.cloudGray, paths: [P(CLOUD_FULL, { s: 1, dx: 0, dy: 1.5 })] },
  ],
  'weather-fog': [
    { color: COLORS.cloudGray, paths: [P(FOG_CLOUD)] },
    { color: COLORS.fog, paths: FOG_BARS.map((d) => P(d)) },
  ],
  'weather-rainy': [
    precipCloud(COLORS.cloud),
    { color: COLORS.rain, paths: [drop(-8.25, 1.2), drop(-2.25, 1.2)] },
  ],
  'weather-pouring': [
    precipCloud(COLORS.cloudGray),
    { color: COLORS.rain, paths: [
      P(capsule(8.6, 16.2, 7.4, 20.6, 1.9)),
      P(capsule(12.6, 16.2, 11.0, 22.2, 1.9)),
      P(capsule(16.6, 16.2, 15.4, 20.6, 1.9)),
    ] },
  ],
  'weather-snowy': [
    precipCloud(COLORS.cloud),
    { color: COLORS.ice, paths: [flake(0.72, 12, 19)] },
  ],
  'weather-snowy-rainy': [
    precipCloud(COLORS.cloud),
    { color: COLORS.ice, paths: [flake(0.62, 8.8, 19.2)] },
    { color: COLORS.rain, paths: [drop(-1.0, 1.2)] },
  ],
  'weather-hail': [
    precipCloud(COLORS.cloudGray),
    { color: COLORS.ice, paths: [
      P(circle(8.5, 19.8, 1.7)), P(circle(12, 17.4, 1.5)), P(circle(15.5, 19.8, 1.7)),
    ] },
  ],
  'weather-lightning': [
    precipCloud(COLORS.cloudStorm),
    { color: COLORS.bolt, paths: [P(BOLT, { s: 1, dx: 0, dy: 1 })] },
  ],
  'weather-lightning-rainy': [
    precipCloud(COLORS.cloudStorm),
    { color: COLORS.bolt, paths: [P(BOLT, { s: 1, dx: -2.5, dy: 1 })] },
    { color: COLORS.rain, paths: [drop(-0.5, 1.2)] },
  ],
  'weather-windy': [{ color: COLORS.wind, paths: WIND_LINES.map((d) => P(d)) }],
  'weather-windy-variant': [
    { color: COLORS.cloudGray, paths: [P(WINDY_CLOUD)] },
    { color: COLORS.wind, paths: [P(WIND_LINES[2])] },
  ],
  'alert-circle-outline': [{ color: COLORS.alert, paths: [P(ALERT_CIRCLE)] }],
};

// Weather color of each icon: the label color of a colored weather icon, so
// Tile color "From icon" and the icon disc follow the weather. Five base
// tones like the sky (sun gold, cloud grey, rain blue, cold ice blue, storm
// purple, plus wind teal and night grey blue); a mixed condition takes the
// midpoint of its parts, and heavier weather a deeper tone.
export const TINTS = {
  'weather-sunny': '#FFB224',
  'weather-night': '#5E7092',
  'weather-partly-cloudy': '#CCAB6B',       // sun + cloud
  'weather-night-partly-cloudy': '#7C8AA2', // night + cloud
  'weather-cloudy': '#9AA4B2',
  'weather-fog': '#B8BEC6',
  'weather-rainy': '#4A7FC0',
  'weather-pouring': '#2F5FB0',
  'weather-snowy': '#7CC4F0',
  'weather-snowy-rainy': '#63A2D8',         // rain + snow
  'weather-hail': '#5FB4D8',
  'weather-lightning': '#8B5CF6',
  'weather-lightning-rainy': '#6B6EDB',     // storm + rain
  'weather-windy': '#4FB8B0',
  'weather-windy-variant': '#75AEB1',       // wind + cloud
  'alert-circle-outline': '#E5533D',
};

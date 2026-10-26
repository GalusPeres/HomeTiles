// The head bar's page dots in the Web Admin preview follow the preview's
// theme (user 2026-10-26: the light preview showed the dark theme's white
// pill, the panel a black one). The dot colors are emitted for both themes;
// html[data-panel-theme="light"] switches them like the --pv-* roles.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');

const styles = read('src/web/server/render/web_admin_styles.cpp');
const head = styles.slice(styles.indexOf('static void appendHeadBarVars(String& html) {\n  auto emit_scaled'));
assert.ok(head.length > 0, 'appendHeadBarVars found');
assert.ok(!/--head-dot-on:#%06X;--head-dot-off:#%06X;",\s*static_cast<unsigned>\(settings_style/.test(head),
  'the dots are not emitted once in the page load theme');
assert.match(head, /auto emit_dots = \[&html\]\(int8_t surface, const char\* selector\) \{\n    const ui_theme::SurfaceScope scope\(surface\);/,
  'each theme\'s dots are computed on that theme');
assert.ok(head.includes('emit_dots(0, ":root");') && head.includes('emit_dots(1, "html[data-panel-theme=\\"light\\"]");'),
  'dark dots on :root, light dots under html[data-panel-theme="light"]');
// The device's colors: the text color for the pill, a grey for the others.
const bar = read('src/ui/tabs/tiles/home_bar.cpp');
assert.match(bar, /uint32_t dot_color\(bool active\) \{\n  if \(active\) return ui_theme::text\(\);\n  return ui_theme::light\(\) \? ui_theme::kLight\.icon_unavailable : 0x5C5C5C;\n\}/,
  'home_bar::dot_color depends on the theme it is asked in');
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('background:var(--head-dot-off);') &&
  css.includes('.head-bar-preview .head-dots > i.on { width:var(--head-dot-active); background:var(--head-dot-on); }'),
  'the preview dots use the emitted colors');
console.log('head dots: both themes emitted, the preview switches them with its theme');

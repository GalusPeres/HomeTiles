// Settings pages scroll by themselves with the scrollbar in the card's free
// right margin. Two regressions guard it:
// - The Settings tab clipped its content (overflow:hidden), which cut off a
//   scrollbar pushed out by a negative margin. The tab must not clip.
// - The cards and the footer line moved inward whenever a row folded out and
//   a scrollbar appeared. Each page reserves its gutter for good and gives it
//   back with the width pages.js measures, and the footer sits outside the
//   scrolling page, under the list and the page, so the scrollbar ends above
//   its line.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const css = readRepoFile('src/web/assets/admin.css').replace(/\r\n?/g, '\n');
const pages = readRepoFile('src/web/admin/settings/pages.js').replace(/\r\n?/g, '\n');
const html = readRepoFile('src/web/server/render/web_admin_html.cpp').replace(/\r\n?/g, '\n');
const rule = selector => {
  const start = css.indexOf(`    ${selector} {`);
  assert.ok(start >= 0, `${selector} rule exists`);
  return css.slice(start, css.indexOf('}', start));
};

const tab = rule('#tab-network.active');
assert.doesNotMatch(tab, /overflow/, 'the Settings tab does not clip its pages');
const page = rule('.settings-page');
assert.match(page, /overflow-y:auto;/, 'a page scrolls by itself');
assert.match(page, /scrollbar-gutter:stable;/, 'the gutter is always reserved, so nothing moves');
assert.match(page, /margin-right:calc\(-8px - var\(--settings-scrollbar, 10px\)\);/,
  'the measured gutter is given back: the cards end where the tab bar ends');
assert.match(page, /padding-right:8px;/, 'a gap between the cards and the scrollbar');
assert.match(pages, /tab\.style\.setProperty\('--settings-scrollbar', \(page\.offsetWidth - page\.clientWidth\) \+ 'px'\);/,
  'pages.js measures the browser\'s scrollbar width');
assert.match(css, /\* \{ scrollbar-width:thin; scrollbar-color:#555555 transparent; \}/, 'a visible thumb');

// The footer is outside every page: after the list and the pages, inside the tab.
const pagesEnd = html.indexOf('</section>\n</div>\n</div>\n');
const footer = html.indexOf('<div class="admin-footer-actions settings-foot" id="settingsFoot">');
assert.ok(pagesEnd > 0 && footer > pagesEnd, 'the footer follows the closed list and pages');
assert.match(rule('.admin-footer-actions.settings-foot'), /padding-right:0;/,
  'its buttons end flush with the cards');

console.log('Settings pages keep their width and their scrollbar beside the cards.');

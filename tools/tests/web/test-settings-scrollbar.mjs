// The Settings tab kept a gutter but showed no scrollbar: the form reached
// 12 px past #tab-network with a negative margin, and #tab-network clips its
// content (overflow:hidden), so the form's scrollbar was cut off. The form now
// stays inside the tab like the I/O list, whose scrollbar shows.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const css = readRepoFile('src/web/assets/admin.css').replace(/\r\n?/g, '\n');
const rule = selector => {
  const start = css.indexOf(`    ${selector} {`);
  assert.ok(start >= 0, `${selector} rule exists`);
  return css.slice(start, css.indexOf('}', start));
};

assert.match(css, /#tab-network\.active,\s*#tab-hardware\.active \{[^}]*overflow:hidden;/,
  'the Settings tab clips its content');
const form = rule('#admin_settings_form');
assert.match(form, /overflow-y:auto;/, 'the form scrolls itself');
assert.match(form, /margin:0;/, 'no negative margin pushes the scrollbar out of the tab');
assert.doesNotMatch(form, /margin:[^;]*-\d/, 'no negative margin');
assert.match(form, /padding-right:4px;/, 'the same gap to the scrollbar as the I/O list');
assert.match(rule('.hardware-io-list'), /overflow-y:auto;[\s\S]*padding-right:4px;/, 'I/O list reference');
assert.match(css, /\* \{ scrollbar-width:thin; scrollbar-color:#555555 transparent; \}/, 'a visible thumb');

console.log('Settings scrollbar stays inside the tab.');

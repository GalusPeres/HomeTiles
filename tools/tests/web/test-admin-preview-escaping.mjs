// The Web Admin tile previews are built as markup strings, so every value that
// comes from a tile configuration or from Home Assistant must be escaped. The
// server-rendered grid already does this through appendHtmlEscaped(); this
// regression test keeps the browser previews consistent with it.
import assert from 'node:assert/strict';

import {
  adminSource, extractDeliveredFunction, inlineScriptSafe, readRepoFile
} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const utils = readRepoFile("src/web/server/web_admin_utils.cpp");
for (const entity of ['&amp;', '&lt;', '&gt;', '&quot;']) {
  assert.ok(utils.includes(entity),
    `appendHtmlEscaped must keep escaping ${entity}`);
}

// Every untrusted value reaching preview markup, in both the live editor
// preview and the cached-grid preview.
for (const marker of [
  "'<i class=\"mdi mdi-' + escapeHtml(iconName) + ' tile-icon\"'",
  'tileTitleHtml(displayTitle)',
  'escapeHtml(coverPreviewStateText(coverPreviewState))',
  "'<span class=\"tile-unit\">' + escapeHtml(unit) + '</span>'",
  'escapeHtml(textValue)',
  "'<strong>' + escapeHtml(info.value) + '</strong>'",
  "'<small>' + escapeHtml(info.label) + '</small>'",
  "'<strong>' + escapeHtml(slot.value) + '</strong>'",
  "'<small>' + escapeHtml(slot.caption) + '</small>'"
]) {
  assert.ok(adminSource.includes(marker),
    `Preview markup must escape untrusted values: ${marker}`);
}

// Guards against a reintroduced raw interpolation of the same values.
for (const pattern of [
  /'<i class="mdi mdi-' \+ iconName/,
  /-title">' \+ displayTitle/,
  /'<span class="tile-unit">' \+ unit\b/,
  /textClass \+ '">' \+ textValue/,
  /'<strong>' \+ (?:info|slot)\.value/,
  /'<small>' \+ (?:info\.label|slot\.caption)/
]) {
  assert.doesNotMatch(adminSource, pattern,
    `Preview markup interpolates an unescaped value: ${pattern}`);
}

const harness = `<!doctype html><html><body>
  <div class="tile" id="tile"></div>
  <pre id="result">running</pre>
  <script>
  (() => {
    ${inlineScriptSafe(extractDeliveredFunction('escapeHtml'))}
    ${inlineScriptSafe(extractDeliveredFunction('normalizeTileTitle'))}
    ${inlineScriptSafe(extractDeliveredFunction('tileTitleHtml'))}
    try {
      const hostile = '<img src=x onerror="document.body.dataset.xss=1">';
      const tile = document.getElementById('tile');
      tile.innerHTML = '<div class="tile-title">' + tileTitleHtml(hostile) +
        '</div><span class="tile-unit">' + escapeHtml('" onmouseover="1') +
        '</span>';
      if (tile.querySelector('img')) {
        throw new Error('Hostile markup was rendered as an element');
      }
      if (document.body.dataset.xss) {
        throw new Error('Hostile markup executed');
      }
      if (tile.querySelector('.tile-title').textContent !== hostile) {
        throw new Error('Escaping changed the displayed text');
      }
      if (tile.querySelectorAll('.tile-unit').length !== 1 ||
          tile.querySelector('.tile-unit').getAttribute('onmouseover') !== null) {
        throw new Error('Quote in a unit broke out of its attribute context');
      }
      if (escapeHtml('a & b') !== 'a &amp; b' ||
          escapeHtml(null) !== '' || escapeHtml(0) !== '0') {
        throw new Error('escapeHtml lost a plain value');
      }
      document.body.dataset.result = 'pass';
      document.getElementById('result').textContent = 'pass';
    } catch (error) {
      document.body.dataset.result = 'fail';
      document.getElementById('result').textContent = error.stack || String(error);
    }
  })();
  </script>
</body></html>`;

if (!runDomHarness({
  label: 'Admin preview escaping DOM test',
  html: harness,
  tmpPrefix: 'hometiles-preview-escaping-'
})) {
  console.log('Admin preview escaping source contract passed.');
}

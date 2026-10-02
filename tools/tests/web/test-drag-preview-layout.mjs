// The drag image of a tile looks like the tile (user 2026-10-02: the dragged
// Settings tile showed its icon and disc at the top left). The real
// createDragPreview clones the tile with the real admin.css in Chrome; icon
// and title must sit where they sit on the tile, for a centered Folder /
// Settings / Switch tile and a header tile alike.
import {extractDeliveredFunction, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const html = `<!doctype html><html><head><style>
${readRepoFile('src/web/assets/admin.css')}
:root { --grid-cols:4; --grid-rows:2; --preview-cell-w:120px; --preview-cell-h:110px; --preview-gap:10px; --preview-pad:6px; }
</style></head><body>
<div id="tab-tiles-folder0" class="tile-tab tab-content active"><div class="tile-grid">
<div class="tile navigate active" id="t-settings" data-type="7" style="grid-column:1 / span 1;grid-row:1 / span 1">
<i class="mdi mdi-cog tile-icon"><span class="tile-icon-lock mdi mdi-lock"></span></i><div class="tile-title">Settings</div></div>
<div class="tile switch" id="t-switch" data-type="5" style="grid-column:2 / span 1;grid-row:1 / span 1">
<i class="mdi mdi-lightbulb tile-icon"></i><div class="tile-title">Lamp</div></div>
<div class="tile sensor" id="t-sensor" data-type="1" style="grid-column:3 / span 2;grid-row:1 / span 1">
<i class="mdi mdi-thermometer tile-icon"></i><div class="tile-title">Kitchen</div></div>
</div></div><pre id="result">running</pre><script>
${inlineScriptSafe(extractDeliveredFunction('createDragPreview'))}
try {
  const offsets = (tile, part) => {
    const box = tile.getBoundingClientRect();
    const item = tile.querySelector(part).getBoundingClientRect();
    return {x: item.left - box.left, y: item.top - box.top, w: item.width, h: item.height};
  };
  const failures = [];
  for (const id of ['t-settings', 't-switch', 't-sensor']) {
    const tile = document.getElementById(id);
    const preview = createDragPreview(tile);
    for (const part of ['.tile-icon', '.tile-title']) {
      const a = offsets(tile, part), b = offsets(preview, part);
      if (['x', 'y', 'w', 'h'].some(key => Math.abs(a[key] - b[key]) > 0.5)) {
        failures.push(id + ' ' + part + ' tile ' + JSON.stringify(a) + ' preview ' + JSON.stringify(b));
      }
    }
    preview.remove();
  }
  if (failures.length) throw new Error(failures.join('\\n'));
  document.body.dataset.result = 'pass';
} catch (error) {
  document.body.dataset.result = 'fail';
  document.getElementById('result').textContent = error.message;
}
</script></body></html>`;

try {
  runDomHarness({label: 'Drag preview layout', html, tmpPrefix: 'hometiles-drag-preview-'});
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 400));
}
console.log('Drag preview: icon and title sit where they sit on the tile');

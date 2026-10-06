// On wide windows the Tile settings panel reaches the end of its column for
// every tile type. With the short Empty type it used to end right below its
// few fields, and the Type list (sized to the room left in the panel) only got
// a small scrolling window (S3 b240, Web Admin).
import {readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const css = readRepoFile('src/web/assets/admin.css');
const html = `<!doctype html><html><head><style>${css}
html, body { margin:0; height:100%; }
#frame { height:900px; display:flex; flex-direction:column; }
</style></head><body>
<div id="frame">
  <div class="tab-content tile-tab active">
    <div class="tile-editor" id="editor">
      <div class="tile-editor-main">
        <div class="tile-grid-scroll"><div id="grid" style="width:540px;height:540px"></div></div>
        <div class="folder-footer" style="height:220px">Global settings</div>
      </div>
      <div class="tile-settings" id="settings">
        <div class="tile-settings-head"><div class="tile-head-top"><h3>Tile Settings</h3><select><option>Empty</option></select></div></div>
        <div class="tile-settings-body"><p>Only a few fields</p></div>
      </div>
    </div>
  </div>
</div>
<pre id="result"></pre>
<script>
const height = id => document.getElementById(id).getBoundingClientRect().height;
const settings = height('settings'), grid = height('grid'), editor = height('editor');
const ok = settings >= grid && Math.abs(settings - editor) <= 1;
document.body.dataset.result = ok ? 'pass' : 'fail';
document.getElementById('result').textContent =
  'settings=' + settings + ' grid=' + grid + ' editor=' + editor;
</script></body></html>`;

let checked = false;
try {
  checked = runDomHarness({label: 'Tile settings full height', html, tmpPrefix: 'hometiles-settings-height-',
    extraArgs: ['--window-size=1400,1000']}) !== false;
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 400));
}
if (checked) console.log('Tile settings full height: a short type still reaches the end of the column');

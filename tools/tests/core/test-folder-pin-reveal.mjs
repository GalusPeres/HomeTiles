import {extractDeliveredFunction, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const productionFunctions = [
  extractDeliveredFunction('togglePasswordVisibility'),
  extractDeliveredFunction('loadNavigateFields')
].join('\n\n');

const harness = `<!doctype html><html><body>
  <input type="checkbox" id="folder0_folder_pin_enabled">
  <div class="password-field">
    <input type="password" id="folder0_folder_pin">
    <button class="password-toggle" data-label-show="Show"
            data-label-hide="Hide">Show</button>
  </div>
  <span id="folder0_folder_pin_status"></span>
  <pre id="result">running</pre>
  <script>
  (() => {
    function syncFolderPinControls() {}
    function loadIconColorFields() {}
    function navigateText(key) { return key === 'folderPinSaved' ? 'PIN saved' : key; }
    ${inlineScriptSafe(productionFunctions)}
    try {
      const input = document.getElementById('folder0_folder_pin');
      const button = document.querySelector('.password-toggle');
      const status = document.getElementById('folder0_folder_pin_status');
      loadNavigateFields('folder0', {
        folder_pin_enabled: true,
        folder_pin: '1234'
      });
      if (input.value !== '1234' || input.type !== 'password' ||
          button.textContent !== 'Show' || status.textContent !== 'PIN saved') {
        throw new Error('Stored PIN was not restored in masked form');
      }
      togglePasswordVisibility('folder0_folder_pin', button);
      if (input.type !== 'text' || input.value !== '1234' ||
          button.textContent !== 'Hide') {
        throw new Error('Show did not reveal the restored PIN');
      }
      loadNavigateFields('folder0', {
        folder_pin_enabled: true,
        folder_pin: '1234'
      });
      if (input.type !== 'password' || input.value !== '1234' ||
          button.textContent !== 'Show') {
        throw new Error('Returning to the folder did not restore masked state');
      }
      loadNavigateFields('folder0', {
        folder_pin_enabled: true,
        folder_pin: ''
      });
      if (input.value !== '' || input.type !== 'password') {
        throw new Error('Legacy hash-only PIN must remain protected and blank');
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

runDomHarness({
  label: 'Folder PIN reveal DOM test',
  html: harness,
  tmpPrefix: 'hometiles-folder-pin-'
});

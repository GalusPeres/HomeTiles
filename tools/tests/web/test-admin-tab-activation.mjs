// switchTab used to find the active tab button by scanning its inline onclick
// attribute for a quoted panel name, and nothing told a screen reader which tab
// was open. Each button now names its panel with data-tab-target and the active
// one carries aria-current.
import assert from 'node:assert/strict';

import {
  adminSource, extractDeliveredFunction, inlineScriptSafe, readRepoFile
} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const html = readRepoFile("src/web/server/render/web_admin_html.cpp");

// Every tab button, static or per folder, must name the panel it opens.
for (const target of ['tab-tiles-screensaver', 'tab-hardware', 'tab-network']) {
  assert.ok(html.includes(`data-tab-target="${target}"`),
    `The ${target} button must name its panel`);
}
assert.ok(html.includes('html += R"html(" data-tab-target="tab-tiles-)html";'),
  'Folder tab buttons must name their panel too');

assert.doesNotMatch(adminSource, /getAttribute\('onclick'\)/,
  'switchTab must not identify the active tab from its inline handler');
assert.ok(adminSource.includes('button.dataset.tabTarget === tabName'),
  'The active tab must be matched by its named panel');
assert.ok(adminSource.includes('setActiveTabButton(tabName);'),
  'switchTab must delegate the button state to setActiveTabButton');

const harness = `<!doctype html><html><body>
  <div class="tab-nav">
    <button class="tab-btn folder-tab-btn" type="button"
            data-tab-target="tab-tiles-folder0">Home</button>
    <div class="folder-menu" id="folderMenu">
      <button class="tab-btn folder-menu-btn" id="folderMenuButton" type="button" aria-expanded="false"
              data-default-label="Folders"><i class="mdi folder-menu-icon mdi-folder-multiple"></i><span
              class="folder-menu-label">Folders</span></button>
      <div class="folder-menu-list" id="folderMenuList">
        <button class="tab-btn folder-tab-btn" type="button" data-folder-name="Folder 1" data-folder-icon="lightbulb"
                data-tab-target="tab-tiles-folder1"
                onclick="switchTab('tab-tiles-folder1')">Folder 1</button>
        <button class="tab-btn folder-tab-btn" type="button" data-folder-name="Folder 10"
                data-tab-target="tab-tiles-folder10"
                onclick="switchTab('tab-tiles-folder10')">Folder 10</button>
      </div>
    </div>
    <button class="tab-btn" type="button" data-tab-target="tab-network"
            onclick="switchTab('tab-network')">Settings</button>
  </div>
  <div id="tab-tiles-folder0" class="tab-content"></div>
  <div id="tab-tiles-folder1" class="tab-content active"></div>
  <div id="tab-tiles-folder10" class="tab-content"></div>
  <div id="tab-network" class="tab-content"></div>
  <pre id="result">running</pre>
  <script>
  (() => {
    ${inlineScriptSafe(['setActiveTabButton', 'syncFolderMenuButton', 'toggleFolderMenu']
      .map(extractDeliveredFunction).join('\n'))}
    function activate(tabName) {
      document.querySelectorAll('.tab-content')
        .forEach(tab => tab.classList.remove('active'));
      document.getElementById(tabName).classList.add('active');
      setActiveTabButton(tabName);
    }
    const currents = () => Array.from(
      document.querySelectorAll('.tab-btn[aria-current]'))
      .map(btn => btn.dataset.tabTarget);
    try {
      activate('tab-network');
      if (currents().join(',') !== 'tab-network') {
        throw new Error('Exactly the open tab must be current: ' + currents());
      }
      if (!document.getElementById('tab-network').classList.contains('active')) {
        throw new Error('The opened panel must be active');
      }
      // Outside the Folders menu its button keeps its own label, closed.
      const menuButton = document.getElementById('folderMenuButton');
      const menuLabel = () => menuButton.querySelector('.folder-menu-label').textContent;
      if (menuButton.classList.contains('active') || menuLabel() !== 'Folders' ||
          !document.getElementById('folderMenuList').hidden) {
        throw new Error('The Folders button must rest outside a folder: ' + menuLabel());
      }

      activate('tab-tiles-folder10');
      if (currents().join(',') !== 'tab-tiles-folder10') {
        throw new Error('Switching must move aria-current: ' + currents());
      }
      const painted = Array.from(document.querySelectorAll('.tab-btn.active:not(.folder-menu-btn)'))
        .map(btn => btn.dataset.tabTarget);
      if (painted.join(',') !== 'tab-tiles-folder10') {
        throw new Error('Only one button may look active: ' + painted);
      }
      // A folder from the menu: the Folders button shows it and looks active.
      if (!menuButton.classList.contains('active') || menuLabel() !== 'Folder 10') {
        throw new Error('The Folders button must show the open folder: ' + menuLabel());
      }

      // folder1 is a prefix of folder10. A substring match would pick whichever
      // button comes first in the document; the lookup has to be exact.
      activate('tab-tiles-folder1');
      if (currents().join(',') !== 'tab-tiles-folder1') {
        throw new Error('A prefix folder id must resolve exactly: ' + currents());
      }
      if (menuLabel() !== 'Folder 1' ||
          !menuButton.querySelector('.folder-menu-icon').classList.contains('mdi-lightbulb')) {
        throw new Error('The Folders button takes the folder icon: ' + menuLabel());
      }
      activate('tab-tiles-folder0');
      if (menuButton.classList.contains('active') || menuLabel() !== 'Folders') {
        throw new Error('Home resets the Folders button');
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
  label: 'Admin tab activation DOM test',
  html: harness,
  tmpPrefix: 'hometiles-tab-activation-'
})) {
  console.log('Admin tab activation source contract passed.');
}

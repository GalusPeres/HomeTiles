import {extractDeliveredFunction, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const productionFunctions = [
  extractDeliveredFunction('clampHalf'),
  extractDeliveredFunction('restoreCurrentTileSelectionUi'),
  extractDeliveredFunction('enableTileDrag'),
  extractDeliveredFunction('enableSettingsHiddenSlot')
].join('\n\n');

const harness = `<!doctype html>
<html><body>
  <div id="tab-tiles-folder0">
    <div id="mainGrid" class="tile-grid">
      <div id="settingsGridTile" class="tile navigate active" draggable="true"
           data-index="0" data-type="7"><span id="gridSettingsChild">Settings</span></div>
      <div id="gridTargetTile" class="tile empty" draggable="true"
           data-index="1" data-type="0"><span id="gridTargetChild">Target</span></div>
    </div>
    <div id="settingsHiddenSlot" class="tile-grid settings-hidden-slot">
      <div id="settingsHiddenTile" class="tile settings-hidden-tile empty"
           draggable="false" data-hidden="0" data-span-w="1" data-span-h="1">
        <i id="parkingChild">Parking</i>
      </div>
      <span id="settingsHiddenHint"></span>
    </div>
  </div>
  <pre id="result">running</pre>
  <script>
  (() => {
    const GRID_COLS = 6;
    const GRID_ROWS = 5;
    const HIDDEN_SETTINGS_TILE_INDEX = -2;
    let resizeState = null;
    let dragSource = null;
    let dragPreview = null;
    let currentTileIndex = 0;
    let currentTileTab = 'folder0';
    let gridVisible = true;
    let hideCalls = 0;
    let restoreCalls = 0;
    let wrongGridDropCalls = 0;
    const gridTiles = [{type: 7}, {type: 0}];

    function getTileGrid() { return document.getElementById('mainGrid'); }
    function getTilesData() { return gridTiles; }
    function selectTile(index, tab) {
      currentTileIndex = index;
      currentTileTab = tab;
    }
    function getTileElementLayout(tab, index) {
      return {col: index, row: 0, span_w: 1, span_h: 1};
    }
    function getTileLayoutFromData(tab, index) {
      return getTileElementLayout(tab, index);
    }
    function getDragAnchorCell() { return {col: 0, row: 0}; }
    function getDragGrabOffset() { return {x: 4, y: 4}; }
    function getDragLayoutOffset() { return {x: 4, y: 4}; }
    function captureLayoutSnapshot() { return []; }
    function createDragPreview(tile) {
      const preview = tile.cloneNode(true);
      document.body.appendChild(preview);
      return preview;
    }
    function restoreDragPreview() {}
    function clearReflowPreviewClasses() {}
    function clearDragPlaceholder() {}
    function clearDeferredSensorRefresh() {}
    function flushDeferredSensorRefresh() {}
    function handleGridDragMove(tab, event) {
      if (!dragSource || dragSource.tab !== tab) return;
      event.preventDefault();
      if (dragSource.kind === 'hidden-settings') {
        dragSource.hiddenTarget = {col: 1, row: 0, span_w: 1, span_h: 1};
      }
    }
    function handleGridDrop(tab, event) {
      if (!dragSource || dragSource.tab !== tab) return;
      event.preventDefault();
      event.stopPropagation();
      if (dragSource.kind === 'hidden-settings') {
        dragSource.dropCommitted = true;
        restoreHiddenSettingsTile(1, 0);
      } else {
        wrongGridDropCalls++;
        dragSource.dropCommitted = true;
      }
    }
    function settingsAccessElement() { return null; }
    function selectHiddenSettingsTile() {}
    function t(key) { return key; }
    function showNotification() {}

    function renderVisibility(hidden) {
      gridVisible = !hidden;
      currentTileIndex = hidden ? HIDDEN_SETTINGS_TILE_INDEX : 0;
      currentTileTab = 'folder0';
      gridTiles[0].type = hidden ? 0 : 7;
      const source = document.getElementById('settingsGridTile');
      source.dataset.type = hidden ? '0' : '7';
      source.draggable = !hidden;
      source.classList.toggle('active', !hidden);
      const parked = document.getElementById('settingsHiddenTile');
      parked.dataset.hidden = hidden ? '1' : '0';
      parked.dataset.type = hidden ? '7' : '0';
      parked.draggable = hidden;
      parked.classList.toggle('active', hidden);
      parked.style.background = hidden ? '#8b3434' : 'transparent';
    }
    function hideSettingsTileFromGrid() {
      hideCalls++;
      renderVisibility(true);
      return Promise.resolve(true);
    }
    function restoreHiddenSettingsTile() {
      restoreCalls++;
      renderVisibility(false);
      return Promise.resolve(true);
    }

    ${inlineScriptSafe(productionFunctions)}

    function transfer(source, target) {
      const dataTransfer = new DataTransfer();
      source.dispatchEvent(new DragEvent('dragstart', {
        bubbles: true, cancelable: true, dataTransfer, clientX: 20, clientY: 20
      }));
      target.dispatchEvent(new DragEvent('dragenter', {
        bubbles: true, cancelable: true, dataTransfer, clientX: 120, clientY: 20
      }));
      target.dispatchEvent(new DragEvent('dragover', {
        bubbles: true, cancelable: true, dataTransfer, clientX: 120, clientY: 20
      }));
      target.dispatchEvent(new DragEvent('drop', {
        bubbles: true, cancelable: true, dataTransfer, clientX: 120, clientY: 20
      }));
      source.dispatchEvent(new DragEvent('dragend', {
        bubbles: true, cancelable: true, dataTransfer, clientX: 120, clientY: 20
      }));
    }

    try {
      enableTileDrag('folder0');
      enableSettingsHiddenSlot();
      const gridSource = document.getElementById('settingsGridTile');
      const parkingChild = document.getElementById('parkingChild');
      const parkedSource = document.getElementById('settingsHiddenTile');
      const gridTargetChild = document.getElementById('gridTargetChild');

      for (let round = 0; round < 4; round++) {
        transfer(gridSource, parkingChild);
        if (hideCalls !== round + 1 || gridVisible || wrongGridDropCalls !== 0) {
          throw new Error('Grid to parking failed in round ' + (round + 1) +
            ': hide=' + hideCalls + ', visible=' + gridVisible +
            ', wrong-grid-drop=' + wrongGridDropCalls);
        }
        if (parkedSource.style.background !== 'rgb(139, 52, 52)' ||
            !parkedSource.classList.contains('active')) {
          throw new Error('Parking tile lost its color or active outline');
        }
        restoreCurrentTileSelectionUi();
        if (!parkedSource.classList.contains('active')) {
          throw new Error('Periodic preview refresh removed the parking selection');
        }

        transfer(parkedSource, gridTargetChild);
        if (restoreCalls !== round + 1 || !gridVisible) {
          throw new Error('Parking to grid failed in round ' + (round + 1));
        }
      }

      if (dragSource !== null || dragPreview !== null) {
        throw new Error('Drag lifecycle was not cleaned up');
      }
      document.body.dataset.result = 'pass';
      document.getElementById('result').textContent =
        'pass: four real DOM drag roundtrips through child targets';
    } catch (error) {
      document.body.dataset.result = 'fail';
      document.getElementById('result').textContent = error.stack || String(error);
    }
  })();
  </script>
</body></html>`;

runDomHarness({
  label: 'Settings tile DOM drag roundtrip test',
  html: harness,
  tmpPrefix: 'hometiles-settings-drag-'
});

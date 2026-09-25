
function maybeFillTitleFromScene(tab) {
    const prefix = tab;
    const typeSel = document.getElementById(prefix + '_tile_type');
    const titleInput = document.getElementById(prefix + '_tile_title');
    const sceneSel = document.getElementById(prefix + '_scene_alias');
    if (!typeSel || !titleInput || !sceneSel) return;
    if (typeSel.value !== '2') return;
    if (titleInput.value && titleInput.value.trim().length) return;
    const opt = sceneSel.selectedOptions && sceneSel.selectedOptions[0];
    if (!opt) return;
    const label = opt.textContent || opt.innerText || '';
    const title = label.split(' - ')[0] || opt.value || '';
    if (title.trim().length) titleInput.value = title.trim();
  }

  function loadSceneFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const sceneEl = document.getElementById(prefix + '_scene_alias');
    if (sceneEl) sceneEl.value = data.scene_alias || '';
    maybeFillTitleFromScene(tab);
  }

  function saveSceneFields(tab, formData) {
    const prefix = tab;
    formData.append('scene_alias', document.getElementById(prefix + '_scene_alias')?.value || '');
    saveIconColorFields(tab, formData);
  }

  function resetSceneFields(tab) {
    const prefix = tab;
    const sceneEl = document.getElementById(prefix + '_scene_alias');
    if (sceneEl) sceneEl.value = '';
    resetIconColorFields(tab);
  }

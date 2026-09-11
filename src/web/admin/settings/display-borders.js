
  let normalTileBordersSaveSequence = 0;
  function applyNormalTileBordersPreview(enabled) {
    document.querySelectorAll('.normal-tile-border-toggle').forEach(input => {
      input.checked = !!enabled;
    });
    document.querySelectorAll('.tile-grid:not(.screensaver-tile-grid)').forEach(grid => {
      grid.classList.toggle('tiles-bordered', !!enabled);
    });
  }
  async function saveNormalTileBorders(enabled) {
    const wanted = !!enabled;
    const previous = !wanted;
    const sequence = ++normalTileBordersSaveSequence;
    applyNormalTileBordersPreview(wanted);
    try {
      const response = await fetch('/api/display/tile-borders', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'enabled=' + (wanted ? '1' : '0')
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
    } catch (error) {
      if (sequence !== normalTileBordersSaveSequence) return;
      applyNormalTileBordersPreview(previous);
      showNotification(t('networkErrorSave'), false);
    }
  }

// The shared root variables also reach cached and lazily inserted folder grids.
let tileRadiusConfirmed = null;
let tileRadiusWanted = null;
let tileRadiusSaving = false;
let tileRadiusRevision = 0;
let tileRadiusPreviewTimer = null;
let tileRadiusLiveWanted = null;
function previewTileRadiusLive(value) {
  const radius = previewTileRadius(value);
  tileRadiusLiveWanted = radius;
  if (tileRadiusPreviewTimer === null) tileRadiusPreviewTimer = setTimeout(() => {
    tileRadiusPreviewTimer = null;
    queueTileRadius(tileRadiusLiveWanted, false);
  }, 80);
}
function previewTileRadius(value) {
  const input = document.querySelector('.global-tile-radius');
  if (!input) return;
  const radius = Math.max(Number(input.min), Math.min(Number(input.max), Math.round(Number(value))));
  if (!Number.isFinite(radius)) return;
  const root = document.documentElement;
  if (tileRadiusConfirmed === null) {
    tileRadiusConfirmed = Number(getComputedStyle(root).getPropertyValue('--tile-radius-device'));
  }
  const scale = Number(getComputedStyle(root).getPropertyValue('--radius-preview-scale'));
  root.style.setProperty('--tile-radius', Math.max(1, Math.round(radius * scale)) + 'px');
  root.style.setProperty('--tile-radius-device', String(radius));
  document.querySelectorAll('.global-tile-radius').forEach(control => { control.value = radius; });
  document.querySelectorAll('.global-tile-radius-value').forEach(output => { output.textContent = radius; });
  tileRadiusRevision++;
  return radius;
}
function saveTileRadius(value) {
  clearTimeout(tileRadiusPreviewTimer);
  tileRadiusPreviewTimer = null;
  return queueTileRadius(value, true);
}
async function queueTileRadius(value, persist) {
  const radius = previewTileRadius(value);
  if (radius === undefined) return;
  tileRadiusWanted = { radius, persist };
  if (tileRadiusSaving) return;
  tileRadiusSaving = true;
  try {
    while (tileRadiusWanted !== null) {
      const wanted = tileRadiusWanted;
      const revision = tileRadiusRevision;
      tileRadiusWanted = null;
      try {
        const response = await fetch('/api/display/tile-radius', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({ radius: String(wanted.radius), preview: wanted.persist ? '0' : '1' }).toString()
        });
        if (!response.ok) throw new Error('save failed');
        const result = await response.json();
        if (!result.success || result.radius !== wanted.radius) throw new Error('invalid response');
        if (wanted.persist) tileRadiusConfirmed = wanted.radius;
      } catch (error) {
        if (tileRadiusWanted === null && revision === tileRadiusRevision) {
          previewTileRadius(tileRadiusConfirmed);
          showNotification(t('networkErrorSave'), false);
        }
      }
    }
  } finally { tileRadiusSaving = false; }
}
function syncTileRadiusControls(tabEl) {
  const value = getComputedStyle(document.documentElement).getPropertyValue('--tile-radius-device').trim();
  tabEl.querySelectorAll('.global-tile-radius').forEach(control => { control.value = value; });
  tabEl.querySelectorAll('.global-tile-radius-value').forEach(output => { output.textContent = value; });
}

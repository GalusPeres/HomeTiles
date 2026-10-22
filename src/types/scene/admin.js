function maybeFillTitleFromScene(tab) {
    const prefix = tab;
    const typeSel = document.getElementById(prefix + '_tile_type');
    const titleInput = document.getElementById(prefix + '_tile_title');
    const sceneSel = document.getElementById(prefix + '_scene_alias');
    if (!typeSel || !titleInput || !sceneSel) return;
    if (typeSel.value !== '2') return;
    if (titleInput.value && titleInput.value.trim().length) return;
    if (!sceneSel.value) return;
    // A picture without a title fills the tile: keep the title empty.
    if (document.getElementById(prefix + '_scene_image')?.value) return;
    const title = entityPickerName(sceneSel) || sceneSel.value;
    if (title.trim().length) titleInput.value = title.trim();
  }

  // Button tile pictures (types/scene/picture.h): JPEG files in the panel's
  // /pictures, listed and uploaded by /api/tile_pictures and shown in the
  // previews through /api/tile_picture. Uploads are re-encoded here as a
  // baseline JPEG (the panel's TJpgDec reads no progressive JPEG) of at most
  // this many pixels on the longest side; the panel reduces it to the tile.
  const SCENE_PICTURE_MAX_SIDE = 512;
  const SCENE_PICTURE_QUALITY = 0.88;
  let scenePictureList = null;
  let scenePictureListLoad = null;

  function scenePictureText(key, fallback) {
    return (typeof SCENE_I18N === 'object' && SCENE_I18N && SCENE_I18N[key]) || fallback;
  }

  function scenePictureUrl(path) {
    return '/api/tile_picture?path=' + encodeURIComponent(path);
  }

  function isScenePicturePath(path) {
    return /^\/pictures\/[^/\\]+\.(jpe?g)$/i.test(String(path || '')) && !String(path).includes('..');
  }

  function scenePictureName(path) {
    return String(path || '').replace(/^\/pictures\//, '');
  }

  // The panel's pictures, loaded once per page (an upload adds its own).
  function loadScenePictureList(force) {
    if (scenePictureList && !force) return Promise.resolve(scenePictureList);
    if (scenePictureListLoad && !force) return scenePictureListLoad;
    scenePictureListLoad = fetch('/api/tile_pictures', {cache: 'no-store'})
      .then(response => (response.ok ? response.json() : []))
      .then(list => {
        scenePictureList = (Array.isArray(list) ? list : [])
          .map(item => String(item?.path || ''))
          .filter(isScenePicturePath)
          .sort((a, b) => a.localeCompare(b));
        return scenePictureList;
      })
      .catch(() => (scenePictureList = scenePictureList || []))
      .finally(() => { scenePictureListLoad = null; });
    return scenePictureListLoad;
  }

  // Options: "No picture", every listed picture, and the configured one even
  // when the list could not be read or lacks it.
  function fillScenePictureOptions(tab, selected) {
    const select = document.getElementById(tab + '_scene_image');
    if (!select) return;
    const value = selected !== undefined ? selected : select.value;
    const paths = new Set(scenePictureList || []);
    if (value && isScenePicturePath(value)) paths.add(value);
    select.textContent = '';
    const none = document.createElement('option');
    none.value = '';
    none.textContent = scenePictureText('noPicture', 'No picture');
    select.appendChild(none);
    for (const path of [...paths].sort((a, b) => a.localeCompare(b))) {
      const option = document.createElement('option');
      option.value = path;
      option.textContent = scenePictureName(path);
      select.appendChild(option);
    }
    select.value = value && isScenePicturePath(value) ? value : '';
  }

  function setScenePictureStatus(tab, text) {
    const status = document.getElementById(tab + '_scene_image_status');
    if (status) status.textContent = text || '';
  }

  // Draws the picture into a preview card like the panel does: without a
  // title it fills the card (cover), with a title it takes the icon's place.
  // Both previews rebuild the card's HTML right before this call, so only
  // the class of an earlier picture needs clearing.
  function applyScenePicturePreview(el, path, hasTitle) {
    if (!el) return;
    el.classList.remove('scene-picture-full');
    if (!path || !isScenePicturePath(path)) return;
    const img = document.createElement('img');
    img.alt = '';
    img.draggable = false;
    img.decoding = 'async';
    img.src = scenePictureUrl(path);
    if (!hasTitle) {
      img.className = 'scene-picture';
      el.classList.add('scene-picture-full');
      el.prepend(img);
      return;
    }
    // The picture replaces the MDI icon (picture_icon_box): same box and place.
    el.querySelectorAll(':scope > .tile-icon').forEach(node => node.remove());
    const box = document.createElement('i');
    box.className = 'tile-icon scene-picture-icon';
    img.className = 'scene-picture-icon-img';
    box.appendChild(img);
    const title = el.querySelector(':scope > .tile-title');
    el.insertBefore(box, title || el.firstChild);
  }

  // Upload: any picture the browser can read, scaled down to
  // SCENE_PICTURE_MAX_SIDE and stored as /pictures/scene-<time>.jpg.
  async function uploadScenePicture(tab, file) {
    if (!file) return '';
    setScenePictureStatus(tab, scenePictureText('uploadingStatus', 'Uploading…'));
    try {
      // An <img> reads every format the browser knows and keeps the photo's
      // own orientation when drawn.
      const url = URL.createObjectURL(file);
      const image = new Image();
      try {
        await new Promise((resolve, reject) => {
          image.onload = resolve;
          image.onerror = reject;
          image.src = url;
        });
      } finally {
        URL.revokeObjectURL(url);
      }
      const width = image.naturalWidth;
      const height = image.naturalHeight;
      if (!width || !height) throw Error('Picture decode failed');
      const scale = Math.min(1, SCENE_PICTURE_MAX_SIDE / Math.max(width, height));
      const canvas = document.createElement('canvas');
      canvas.width = Math.max(1, Math.round(width * scale));
      canvas.height = Math.max(1, Math.round(height * scale));
      const context = canvas.getContext('2d');
      context.fillStyle = '#000';
      context.fillRect(0, 0, canvas.width, canvas.height);
      context.drawImage(image, 0, 0, canvas.width, canvas.height);
      // toDataURL encodes synchronously (toBlob's callback can stall in
      // background tabs); a JPEG data URL is a baseline JPEG in every browser.
      const dataUrl = canvas.toDataURL('image/jpeg', SCENE_PICTURE_QUALITY);
      if (!dataUrl.startsWith('data:image/jpeg;base64,')) throw Error('Picture encode failed');
      const binary = atob(dataUrl.slice(dataUrl.indexOf(',') + 1));
      const bytes = new Uint8Array(binary.length);
      for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
      const blob = new Blob([bytes], {type: 'image/jpeg'});
      const name = 'scene-' + Date.now().toString(36) + '.jpg';
      const body = new FormData();
      body.append('file', blob, name);
      const response = await fetch('/api/tile_pictures', {method: 'POST', body});
      const data = await response.json().catch(() => ({}));
      if (!response.ok || !data.ok || !isScenePicturePath(data.path)) throw Error(data.error || 'Picture upload failed');
      scenePictureList = [...new Set([...(scenePictureList || []), data.path])];
      setScenePictureStatus(tab, '');
      return data.path;
    } catch (error) {
      setScenePictureStatus(tab, scenePictureText('uploadFailed', 'The picture could not be uploaded.'));
      return '';
    }
  }

  function loadSceneFields(tab, data) {
    loadIconColorFields(tab, data);
    const prefix = tab;
    const sceneEl = document.getElementById(prefix + '_scene_alias');
    if (sceneEl) sceneEl.value = data.scene_alias || '';
    const picture = data.scene_image || '';
    fillScenePictureOptions(tab, picture);
    setScenePictureStatus(tab, '');
    loadScenePictureList().then(() => {
      // Only while the same tile is still loaded with that picture.
      const select = document.getElementById(prefix + '_scene_image');
      if (select && select.value === (isScenePicturePath(picture) ? picture : '')) fillScenePictureOptions(tab);
    });
    maybeFillTitleFromScene(tab);
  }

  function saveSceneFields(tab, formData) {
    const prefix = tab;
    formData.append('scene_alias', document.getElementById(prefix + '_scene_alias')?.value || '');
    formData.append('scene_image', document.getElementById(prefix + '_scene_image')?.value || '');
    saveIconColorFields(tab, formData);
  }

  function resetSceneFields(tab) {
    const prefix = tab;
    const sceneEl = document.getElementById(prefix + '_scene_alias');
    if (sceneEl) sceneEl.value = '';
    fillScenePictureOptions(tab, '');
    setScenePictureStatus(tab, '');
    resetIconColorFields(tab);
  }

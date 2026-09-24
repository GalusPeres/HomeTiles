  // Built-in camera opt-in (only rendered on the exact camera profile). The
  // server provides every visible text as data attributes on the status line;
  // this code only selects between them. A ready sensor adds its model name
  // and chip ID; internal detail codes stay diagnostic-only in the JSON.
  // The live-stream mode select is server-rendered too and saves on change.
  let localCameraSaveSequence = 0;
  let localCameraModeSequence = 0;
  let localCameraMirrorSequence = 0;
  let localCameraIndicatorSequence = 0;
  let localCameraPollTimer = null;
  const LOCAL_CAMERA_STATE_KEYS = {
    disabled: 'stateDisabled',
    probing: 'stateProbing',
    ready: 'stateReady',
    not_found: 'stateNotFound',
    error: 'stateError'
  };

  function localCameraStatusText(note, status) {
    const state = status && typeof status.state === 'string' ? status.state : 'error';
    const key = LOCAL_CAMERA_STATE_KEYS[state] || 'stateError';
    let text = (note.dataset.label || '') + ': ' + (note.dataset[key] || state);
    if (state === 'ready') {
      const parts = [];
      if (status.sensor) parts.push(String(status.sensor).toUpperCase());
      if (status.chip_id) parts.push(String(status.chip_id));
      if (parts.length) text += ' (' + parts.join(', ') + ')';
    }
    return text;
  }

  function applyLocalCameraStatus(status) {
    const note = document.getElementById('local_camera_status');
    const toggle = document.getElementById('local_camera_enabled');
    if (!note || !status || typeof status !== 'object') return;
    if (toggle && typeof status.enabled === 'boolean') toggle.checked = status.enabled;
    const mirrorToggle = document.getElementById('local_camera_mirror');
    if (mirrorToggle && typeof status.mirror === 'boolean') mirrorToggle.checked = status.mirror;
    if (Number.isInteger(status.indicator)) applyLocalCameraIndicator(status.indicator);
    const modeSelect = document.getElementById('local_camera_stream_mode');
    if (modeSelect && Number.isInteger(status.stream_mode)) {
      modeSelect.value = String(status.stream_mode);
      modeSelect.dataset.saved = String(status.stream_mode);
    }
    if (Number.isInteger(status.stream_mode)) showLocalCameraCustom(status.stream_mode);
    applyLocalCameraCustom(status.custom);
    applyLocalCameraImage(status.image);
    note.dataset.state = String(status.state || '');
    note.textContent = localCameraStatusText(note, status);
    clearTimeout(localCameraPollTimer);
    localCameraPollTimer = null;
    // The sensor probe runs on the camera worker; follow it briefly.
    if (status.state === 'probing') {
      localCameraPollTimer = setTimeout(refreshLocalCameraStatus, 1000);
    }
  }

  async function refreshLocalCameraStatus() {
    if (!document.getElementById('local_camera_status')) return;
    try {
      const response = await fetch('/api/local-camera', {cache: 'no-store'});
      if (!response.ok) return;
      applyLocalCameraStatus(await response.json());
    } catch (error) {
      // A status refresh is optional; the next toggle or reload retries.
    }
  }

  async function saveLocalCameraEnabled(enabled) {
    const wanted = !!enabled;
    const sequence = ++localCameraSaveSequence;
    const toggle = document.getElementById('local_camera_enabled');
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'enabled=' + (wanted ? '1' : '0')
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraSaveSequence) return;
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraSaveSequence) return;
      if (toggle) toggle.checked = !wanted;
      showNotification(t('networkErrorSave'), false);
    }
  }

  async function saveLocalCameraMirror(enabled) {
    const wanted = !!enabled;
    const sequence = ++localCameraMirrorSequence;
    const toggle = document.getElementById('local_camera_mirror');
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'mirror=' + (wanted ? '1' : '0')
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraMirrorSequence) return;
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraMirrorSequence) return;
      if (toggle) toggle.checked = !wanted;
      showNotification(t('networkErrorSave'), false);
    }
  }

  // Indicator style (experimental): 0 none, 1 line only, 2 line with the pill.
  // The pill checkbox only applies while the line is shown and keeps its own
  // state while the line is off.
  function applyLocalCameraIndicator(style) {
    const line = document.getElementById('local_camera_indicator_line');
    const pill = document.getElementById('local_camera_indicator_pill');
    if (line) line.checked = style !== 0;
    if (pill) {
      if (style !== 0) pill.checked = style === 2;
      pill.disabled = style === 0;
    }
  }

  function localCameraIndicatorStyle() {
    const line = document.getElementById('local_camera_indicator_line');
    const pill = document.getElementById('local_camera_indicator_pill');
    if (!line || !line.checked) return 0;
    return pill && pill.checked ? 2 : 1;
  }

  async function saveLocalCameraIndicator() {
    const style = localCameraIndicatorStyle();
    const sequence = ++localCameraIndicatorSequence;
    const line = document.getElementById('local_camera_indicator_line');
    const pill = document.getElementById('local_camera_indicator_pill');
    if (pill) pill.disabled = style === 0;
    const saved = line && line.dataset.saved !== undefined ? parseInt(line.dataset.saved, 10) : null;
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'indicator=' + style
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraIndicatorSequence) return;
      if (line && Number.isInteger(status.indicator)) line.dataset.saved = String(status.indicator);
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraIndicatorSequence) return;
      if (Number.isInteger(saved)) applyLocalCameraIndicator(saved);
      showNotification(t('networkErrorSave'), false);
    }
  }

  // Custom stream mode: frames per second and JPEG quality sliders, shown only
  // while the Custom mode is selected (its id comes from data-mode). A slider
  // saves on release; a failed save restores the last saved value. Numbers
  // are untranslated.
  const LOCAL_CAMERA_CUSTOM_KEYS = ['fps', 'quality'];
  let localCameraCustomSequence = 0;

  function showLocalCameraCustom(mode) {
    const block = document.getElementById('local_camera_custom');
    if (!block) return;
    block.hidden = String(mode) !== String(block.dataset.mode);
  }

  function setLocalCameraCustomSlider(key, value) {
    const slider = document.getElementById('local_camera_custom_' + key);
    if (!slider) return;
    slider.value = String(value);
    const output = document.getElementById('local_camera_custom_' + key + '_value');
    if (output) output.textContent = String(value);
  }

  function applyLocalCameraCustom(custom) {
    if (!custom || typeof custom !== 'object') return;
    for (const key of LOCAL_CAMERA_CUSTOM_KEYS) {
      const slider = document.getElementById('local_camera_custom_' + key);
      if (!slider || !Number.isInteger(custom[key])) continue;
      slider.dataset.saved = String(custom[key]);
      setLocalCameraCustomSlider(key, custom[key]);
    }
  }

  function localCameraCustomInput(slider) {
    const key = slider && slider.dataset ? slider.dataset.customKey : '';
    if (!LOCAL_CAMERA_CUSTOM_KEYS.includes(key)) return;
    const output = document.getElementById('local_camera_custom_' + key + '_value');
    if (output) output.textContent = String(slider.value);
  }

  async function localCameraCustomChange(slider) {
    const key = slider && slider.dataset ? slider.dataset.customKey : '';
    if (!LOCAL_CAMERA_CUSTOM_KEYS.includes(key)) return;
    const value = parseInt(slider.value, 10);
    if (!Number.isInteger(value)) return;
    localCameraCustomInput(slider);
    const sequence = ++localCameraCustomSequence;
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'custom_' + key + '=' + value
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraCustomSequence) return;
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraCustomSequence) return;
      if (slider.dataset.saved !== undefined) setLocalCameraCustomSlider(key, slider.dataset.saved);
      showNotification(t('networkErrorSave'), false);
    }
  }

  async function saveLocalCameraStreamMode(value) {
    const mode = String(value);
    const sequence = ++localCameraModeSequence;
    const select = document.getElementById('local_camera_stream_mode');
    const previous = select && select.dataset.saved !== undefined ? select.dataset.saved : null;
    showLocalCameraCustom(mode);
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'mode=' + encodeURIComponent(mode)
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      if (sequence !== localCameraModeSequence) return;
      applyLocalCameraStatus(status);
    } catch (error) {
      if (sequence !== localCameraModeSequence) return;
      if (select && previous !== null) select.value = previous;
      if (previous !== null) showLocalCameraCustom(previous);
      showNotification(t('networkErrorSave'), false);
    }
  }

  // Image controls (brightness, contrast, saturation, red, blue, Max. gain). Sliders save
  // while dragging (debounced) and immediately on release. One POST is in
  // flight at a time so the device applies values in order; a value that is
  // still pending is never overwritten by a status update, and a failed save
  // restores the last saved value. Numbers are untranslated.
  const LOCAL_CAMERA_IMAGE_KEYS = ['brightness', 'contrast', 'saturation', 'red', 'blue', 'gain'];
  const LOCAL_CAMERA_IMAGE_DEBOUNCE_MS = 300;
  let localCameraImagePending = {};
  let localCameraImageTimer = null;
  let localCameraImageInFlight = null;

  function localCameraImageSlider(key) {
    return document.getElementById('local_camera_' + key);
  }

  function showLocalCameraImageValue(slider, value) {
    const output = document.getElementById('local_camera_' + slider.dataset.imageKey + '_value');
    if (output) output.textContent = String(value) + (slider.dataset.unit || '');
  }

  function setLocalCameraImageSlider(key, value) {
    const slider = localCameraImageSlider(key);
    if (!slider) return;
    slider.value = String(value);
    showLocalCameraImageValue(slider, value);
  }

  function applyLocalCameraImage(image) {
    if (!image || typeof image !== 'object') return;
    for (const key of LOCAL_CAMERA_IMAGE_KEYS) {
      const slider = localCameraImageSlider(key);
      if (!slider || !Number.isInteger(image[key])) continue;
      slider.dataset.saved = String(image[key]);
      // Keep what the user is dragging or has not sent yet.
      const busy = key in localCameraImagePending ||
        (localCameraImageInFlight && key in localCameraImageInFlight &&
         String(localCameraImageInFlight[key]) !== String(image[key]));
      if (!busy) setLocalCameraImageSlider(key, image[key]);
    }
  }

  function queueLocalCameraImage(slider) {
    const key = slider && slider.dataset ? slider.dataset.imageKey : '';
    if (!LOCAL_CAMERA_IMAGE_KEYS.includes(key)) return false;
    const value = parseInt(slider.value, 10);
    if (!Number.isInteger(value)) return false;
    showLocalCameraImageValue(slider, value);
    localCameraImagePending[key] = value;
    return true;
  }

  function localCameraImageInput(slider) {
    if (!queueLocalCameraImage(slider)) return;
    clearTimeout(localCameraImageTimer);
    localCameraImageTimer = setTimeout(flushLocalCameraImage, LOCAL_CAMERA_IMAGE_DEBOUNCE_MS);
  }

  function localCameraImageChange(slider) {
    if (!queueLocalCameraImage(slider)) return;
    return flushLocalCameraImage();
  }

  function resetLocalCameraImage() {
    localCameraImagePending = {reset: 1};
    return flushLocalCameraImage();
  }

  async function flushLocalCameraImage() {
    clearTimeout(localCameraImageTimer);
    localCameraImageTimer = null;
    // The running save flushes the rest when it finishes.
    if (localCameraImageInFlight) return;
    const values = localCameraImagePending;
    const keys = Object.keys(values);
    if (!keys.length) return;
    localCameraImagePending = {};
    localCameraImageInFlight = values;
    const body = keys.map(key => key + '=' + encodeURIComponent(String(values[key]))).join('&');
    try {
      const response = await fetch('/api/local-camera', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const status = await response.json();
      localCameraImageInFlight = null;
      applyLocalCameraStatus(status);
    } catch (error) {
      localCameraImageInFlight = null;
      const sent = 'reset' in values ? LOCAL_CAMERA_IMAGE_KEYS : keys;
      for (const key of sent) {
        const slider = localCameraImageSlider(key);
        if (!slider || key in localCameraImagePending) continue;
        if (slider.dataset.saved !== undefined) setLocalCameraImageSlider(key, slider.dataset.saved);
      }
      showNotification(t('networkErrorSave'), false);
    }
    if (Object.keys(localCameraImagePending).length) await flushLocalCameraImage();
  }

  document.addEventListener('DOMContentLoaded', () => {
    const modeSelect = document.getElementById('local_camera_stream_mode');
    if (modeSelect) modeSelect.dataset.saved = modeSelect.value;
    const indicatorLine = document.getElementById('local_camera_indicator_line');
    if (indicatorLine) indicatorLine.dataset.saved = String(localCameraIndicatorStyle());
    const note = document.getElementById('local_camera_status');
    if (note && note.dataset.state === 'probing') refreshLocalCameraStatus();
  });

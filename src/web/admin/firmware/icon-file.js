
  // The System page's Icons card: "Update" lets the panel download its icon
  // file now; the file row uploads one chosen by hand. Both restart the panel,
  // the page reloads once it answers again.

  function updateIconFileName(inputEl) {
    const nameEl = document.getElementById('icon_file_name');
    if (!nameEl) return;
    const file = inputEl && inputEl.files && inputEl.files.length ? inputEl.files[0] : null;
    nameEl.textContent = file ? file.name : t('otaNoFileSelected');
  }

  function setIconFileUi(message, phase = 'idle', tone = '', percent = null) {
    const statusEl = document.getElementById('icon_status');
    const progressEl = document.getElementById('icon_progress');
    const progressBarEl = document.getElementById('icon_progress_bar');
    if (statusEl) {
      statusEl.textContent = message || '';
      statusEl.classList.remove('error', 'success');
      if (tone === 'error' || tone === 'success') statusEl.classList.add(tone);
    }
    if (progressEl) {
      progressEl.classList.toggle('is-hidden', phase === 'idle');
      progressEl.classList.toggle('active', phase === 'busy' && percent === null);
    }
    if (progressBarEl) {
      progressBarEl.style.width = percent !== null ? Math.max(0, Math.min(100, percent)) + '%'
        : (phase === 'done' ? '100%' : '0%');
    }
  }

  function setIconFileControlsDisabled(disabled) {
    ['icon_download_btn', 'icon_choose_btn', 'icon_upload_btn', 'icon_file'].forEach(id => {
      const element = document.getElementById(id);
      if (element) element.disabled = disabled;
    });
  }

  // The panel restarts after either way; reload once it answers again.
  function waitForIconFileRestart() {
    const startedAt = Date.now();
    const tryReload = () => {
      fetch(window.location.pathname + '?icon_ping=' + Date.now(), { cache: 'no-store', credentials: 'same-origin' })
        .then(res => {
          if (!res.ok) throw new Error('offline');
          window.location.reload();
        })
        .catch(() => {
          if (Date.now() - startedAt < 180000) window.setTimeout(tryReload, 1500);
        });
    };
    window.setTimeout(tryReload, 4000);
  }

  async function downloadIconFile() {
    setIconFileControlsDisabled(true);
    setIconFileUi(t('otaGithubDownloading'), 'busy');
    try {
      const res = await fetch('/api/icons/download', { method: 'POST', cache: 'no-store', credentials: 'same-origin' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.success) throw new Error(data.error || t('otaFailed'));
      setIconFileUi(t('otaGithubDownloading') + ' ' + t('otaReconnecting'), 'busy');
      waitForIconFileRestart();
    } catch (err) {
      setIconFileUi(err?.message || t('otaFailed'), 'idle', 'error');
      setIconFileControlsDisabled(false);
    }
  }

  function uploadIconFile() {
    const input = document.getElementById('icon_file');
    const file = input && input.files && input.files.length ? input.files[0] : null;
    if (!file) {
      setIconFileUi(t('otaNoFileSelected'), 'idle', 'error');
      return;
    }
    setIconFileControlsDisabled(true);
    setIconFileUi(t('otaUploading') + ' 0%', 'busy', '', 0);
    const fail = message => {
      setIconFileUi(message || t('otaFailed'), 'idle', 'error');
      setIconFileControlsDisabled(false);
    };
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/icons/upload', true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = event => {
      if (!event.lengthComputable) return;
      const percent = Math.max(1, Math.min(100, Math.round((event.loaded / event.total) * 100)));
      setIconFileUi(t('otaUploading') + ' ' + percent + '%', 'busy', '', percent);
    };
    xhr.upload.onload = () => setIconFileUi(t('otaInstalling'), 'busy');
    xhr.onload = () => {
      let data = {};
      try { data = JSON.parse(xhr.responseText || '{}'); } catch (e) {}
      if (xhr.status < 200 || xhr.status >= 300 || !data.success) {
        fail(data.error);
        return;
      }
      setIconFileUi(t('otaSuccess') + ' ' + t('otaReconnecting'), 'done', 'success', 100);
      waitForIconFileRestart();
    };
    xhr.onerror = () => fail();
    xhr.send(file);
  }


  let fileManagerLoaded = false;
  const fileManagerState = { fs: 'sd', path: '/', selected: null, sdAvailable: null };

  function fileManagerText(de, en) {
    return (document.documentElement.lang || '').toLowerCase().startsWith('de') ? de : en;
  }

  async function downloadCrashLog() {
    try {
      const res = await fetch('/api/crashlog?ts=' + Date.now());
      if (res.status === 404) {
        showNotification(fileManagerText('Kein Absturz aufgezeichnet.', 'No crash recorded.'));
        return;
      }
      if (!res.ok) throw new Error();
      const blob = await res.blob();
      const url = URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = url;
      link.download = 'crashlog.txt';
      document.body.appendChild(link);
      link.click();
      link.remove();
      URL.revokeObjectURL(url);
    } catch (err) {
      showNotification(fileManagerText('Download fehlgeschlagen.', 'Download failed.'), false);
    }
  }

  async function eraseCoreDump() {
    if (!confirm(fileManagerText('Gespeicherten Core-Dump wirklich l\u00f6schen?', 'Really delete the stored core dump?'))) return;
    try {
      const res = await fetch('/api/coredump/erase', { method: 'POST' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.success) {
        throw new Error(data.error || fileManagerText('L\u00f6schen fehlgeschlagen.', 'Delete failed.'));
      }
      const actions = document.getElementById('coredump_actions');
      if (actions) actions.style.display = 'none';
      showNotification(fileManagerText('Core-Dump gel\u00f6scht.', 'Core dump deleted.'));
    } catch (err) {
      showNotification(err?.message || fileManagerText('L\u00f6schen fehlgeschlagen.', 'Delete failed.'), false);
    }
  }

  function normalizeFileManagerClientPath(raw) {
    let path = String(raw || '').trim().replaceAll('\\', '/');
    if (!path) path = '/';
    if (!path.startsWith('/')) path = '/' + path;
    while (path.includes('//')) path = path.replaceAll('//', '/');
    while (path.length > 1 && path.endsWith('/')) path = path.slice(0, -1);
    return path;
  }

  function setFileManagerStatus(message, success = null) {
    const el = document.getElementById('file_manager_status');
    if (!el) return;
    el.textContent = message || '';
    el.classList.remove('error', 'success');
    if (success === true) el.classList.add('success');
    if (success === false) el.classList.add('error');
  }

  function setFileManagerStorageState(available, message = null) {
    fileManagerState.sdAvailable = available;
    const badge = document.getElementById('file_manager_sd_state');
    if (badge) {
      badge.classList.remove('ok', 'error', 'checking');
      if (available === true) {
        badge.classList.add('ok');
        badge.textContent = message || fileManagerText('microSD erkannt', 'microSD detected');
      } else if (available === false) {
        badge.classList.add('error');
        badge.textContent = message || fileManagerText('Keine microSD', 'No microSD');
      } else {
        badge.classList.add('checking');
        badge.textContent = message || fileManagerText('Pr\u00fcfe...', 'Checking...');
      }
    }
    // The internal storage of a test build only lists, downloads and deletes.
    document.querySelectorAll('.file-manager-requires-sd').forEach(el => {
      el.disabled = available !== true || fileManagerState.fs === 'internal';
    });
    updateFileManagerSelectionBar();
  }

  function updateFileManagerUploadName(inputEl) {
    const nameEl = document.getElementById('file_manager_upload_name');
    if (!nameEl) return;
    const files = inputEl && inputEl.files ? Array.from(inputEl.files) : [];
    if (!files.length) {
      nameEl.textContent = fileManagerText('Keine Datei ausgew\u00e4hlt', 'No file selected');
    } else if (files.length === 1) {
      nameEl.textContent = files[0].name;
    } else {
      nameEl.textContent = files.length + fileManagerText(
        ' Dateien ausgew\u00e4hlt', ' files selected');
    }
  }

  function formatFileManagerSize(entry) {
    if (!entry || entry.dir) return '-';
    let size = Number(entry.size || 0);
    if (!Number.isFinite(size) || size < 0) size = 0;
    if (size < 1024) return size + ' B';
    if (size < 1024 * 1024) {
      return formatLocalizedNumber(size / 1024, size < 10 * 1024 ? 1 : 0) + ' KB';
    }
    return formatLocalizedNumber(
      size / (1024 * 1024), size < 10 * 1024 * 1024 ? 1 : 0) + ' MB';
  }

  function formatFileManagerModified(entry) {
    const ts = Number(entry && entry.modified ? entry.modified : 0);
    if (!Number.isFinite(ts) || ts <= 0) return '-';
    const date = new Date(ts * 1000);
    if (!Number.isFinite(date.getTime()) || date.getFullYear() < 2020) return '-';
    return date.toLocaleString(undefined, {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit'
    });
  }

  function fileManagerParentOf(path) {
    const current = normalizeFileManagerClientPath(path || '/');
    if (current === '/') return '/';
    const idx = current.lastIndexOf('/');
    return idx <= 0 ? '/' : current.slice(0, idx);
  }

  function fileManagerUrl(endpoint, params = {}) {
    const query = new URLSearchParams(params);
    return endpoint + '?' + query.toString();
  }

  function updateFileManagerRowSelection() {
    const selectedPath = fileManagerState.selected && fileManagerState.selected.path;
    document.querySelectorAll('#file_manager_entries tr[data-file-path]').forEach(row => {
      row.classList.toggle('file-manager-row-selected', !!selectedPath && row.dataset.filePath === selectedPath);
    });
  }

  function updateFileManagerSelectionBar() {
    const selectionEl = document.getElementById('file_manager_selection');
    const primaryBtn = document.getElementById('file_manager_primary_btn');
    const renameBtn = document.getElementById('file_manager_rename_btn');
    const deleteBtn = document.getElementById('file_manager_delete_btn');
    const selected = fileManagerState.selected;
    const hasSelection = !!(selected && selected.path);
    const isParent = !!(selected && selected.parent);
    const hasStorage = fileManagerState.sdAvailable !== false;

    if (selectionEl) {
      if (!hasSelection) {
        selectionEl.textContent = fileManagerText('Keine Auswahl', 'No selection');
      } else {
        const size = selected.dir ? '' : ' - ' + formatFileManagerSize(selected);
        selectionEl.textContent = fileManagerText('Auswahl: ', 'Selection: ') + (selected.name || selected.path) + size;
      }
    }

    if (primaryBtn) {
      primaryBtn.disabled = !hasSelection || !hasStorage;
      primaryBtn.textContent = selected && !selected.dir ? fileManagerText('Download', 'Download') : fileManagerText('\u00d6ffnen', 'Open');
      primaryBtn.title = primaryBtn.textContent;
    }
    if (renameBtn) {
      renameBtn.disabled = !hasSelection || isParent || !hasStorage || fileManagerState.fs === 'internal';
    }
    if (deleteBtn) deleteBtn.disabled = !hasSelection || isParent || !hasStorage;
    updateFileManagerRowSelection();
  }

  function clearFileManagerSelection() {
    fileManagerState.selected = null;
    updateFileManagerSelectionBar();
  }

  function selectFileManagerEntry(entry) {
    if (!entry) {
      clearFileManagerSelection();
      return;
    }
    fileManagerState.selected = {
      path: normalizeFileManagerClientPath(entry.path || '/'),
      name: entry.name || entry.path || '',
      dir: !!entry.dir,
      size: Number(entry.size || 0),
      modified: Number(entry.modified || 0),
      parent: !!entry.parent
    };
    updateFileManagerSelectionBar();
  }

  function appendFileManagerCell(row, className, text) {
    const cell = document.createElement('td');
    if (className) cell.className = className;
    cell.textContent = text;
    row.appendChild(cell);
    return cell;
  }

  function fileManagerIconClass(entry) {
    if (entry && entry.parent) return 'mdi mdi-arrow-up-bold';
    return entry && entry.dir ? 'mdi mdi-folder-outline' : 'mdi mdi-file-outline';
  }

  function appendFileManagerNameCell(row, entry) {
    const cell = document.createElement('td');
    const nameWrap = document.createElement('div');
    nameWrap.className = 'file-manager-name';
    const icon = document.createElement('i');
    icon.className = 'file-manager-name-icon ' + fileManagerIconClass(entry);
    nameWrap.appendChild(icon);
    const label = entry.dir ? document.createElement('button') : document.createElement('span');
    label.className = entry.dir ? 'file-manager-name-link file-manager-folder-name' : '';
    label.textContent = entry.name || entry.path || '';
    if (entry.dir) {
      label.type = 'button';
      label.addEventListener('click', event => {
        event.stopPropagation();
        loadFileManager(entry.path);
      });
    }
    nameWrap.appendChild(label);
    cell.appendChild(nameWrap);
    row.appendChild(cell);
    return cell;
  }

  function renderFileManagerBreadcrumb(path) {
    const el = document.getElementById('file_manager_breadcrumb');
    if (!el) return;
    el.innerHTML = '';

    const current = normalizeFileManagerClientPath(path || '/');
    const rootBtn = document.createElement('button');
    rootBtn.type = 'button';
    rootBtn.className = 'file-manager-breadcrumb-item';
    rootBtn.innerHTML = '<i class="mdi mdi-home-outline"></i><span>root</span>';
    rootBtn.addEventListener('click', () => loadFileManager('/'));
    el.appendChild(rootBtn);

    if (current === '/') return;

    const parts = current.split('/').filter(Boolean);
    let acc = '';
    parts.forEach((part, index) => {
      acc += '/' + part;
      const sep = document.createElement('span');
      sep.className = 'file-manager-breadcrumb-separator';
      sep.textContent = '/';
      el.appendChild(sep);

      const item = document.createElement('button');
      item.type = 'button';
      item.className = 'file-manager-breadcrumb-item';
      item.textContent = part;
      const target = acc;
      item.disabled = index === parts.length - 1;
      item.addEventListener('click', () => loadFileManager(target));
      el.appendChild(item);
    });
  }

  function renderFileManagerEntries(entries, emptyMessage = null) {
    const tbody = document.getElementById('file_manager_entries');
    if (!tbody) return;
    tbody.innerHTML = '';
    renderFileManagerBreadcrumb(fileManagerState.path || '/');

    const addSelectableRow = (row, entry) => {
      row.dataset.filePath = normalizeFileManagerClientPath(entry.path || '/');
      row.tabIndex = 0;
      row.addEventListener('click', () => selectFileManagerEntry(entry));
      row.addEventListener('dblclick', () => {
        selectFileManagerEntry(entry);
        if (entry.dir) {
          loadFileManager(entry.path);
        } else {
          downloadFileManagerFile(entry.path);
        }
      });
      row.addEventListener('keydown', event => {
        if (event.key !== 'Enter') return;
        event.preventDefault();
        selectFileManagerEntry(entry);
        openSelectedFileManagerEntry();
      });
      row.title = entry.dir
        ? fileManagerText('Ausw\u00e4hlen, doppelklicken zum \u00d6ffnen', 'Select, double-click to open')
        : fileManagerText('Ausw\u00e4hlen, doppelklicken zum Download', 'Select, double-click to download');
    };

    const currentPath = normalizeFileManagerClientPath(fileManagerState.path || '/');
    if (currentPath !== '/') {
      const parentEntry = {
        dir: true,
        path: fileManagerParentOf(currentPath),
        name: '.. ' + fileManagerText('Elternordner', 'Parent Directory'),
        parent: true
      };
      const parentRow = document.createElement('tr');
      parentRow.className = 'file-manager-parent-row';
      addSelectableRow(parentRow, parentEntry);
      appendFileManagerNameCell(parentRow, parentEntry);
      appendFileManagerCell(parentRow, 'file-manager-muted', '-');
      appendFileManagerCell(parentRow, 'file-manager-size-cell', '-');
      tbody.appendChild(parentRow);
    }

    if (!Array.isArray(entries) || entries.length === 0) {
      const row = document.createElement('tr');
      const cell = document.createElement('td');
      cell.colSpan = 3;
      cell.textContent = emptyMessage || fileManagerText('Ordner ist leer.', 'Folder is empty.');
      row.appendChild(cell);
      tbody.appendChild(row);
      updateFileManagerRowSelection();
      return;
    }

    entries.forEach(entry => {
      const row = document.createElement('tr');
      addSelectableRow(row, entry);
      appendFileManagerNameCell(row, entry);

      appendFileManagerCell(row, 'file-manager-muted', formatFileManagerModified(entry));
      appendFileManagerCell(row, 'file-manager-size-cell', formatFileManagerSize(entry));

      tbody.appendChild(row);
    });
    updateFileManagerRowSelection();
  }

  // Test builds: the storage switch, microSD or the panel's own storage.
  function selectFileManagerStorage(fs) {
    fileManagerState.fs = fs === 'internal' ? 'internal' : 'sd';
    document.querySelectorAll('.file-manager-storage-switch button').forEach(btn => {
      btn.classList.toggle('active', btn.dataset.fs === fileManagerState.fs);
    });
    loadFileManager('/');
  }

  async function loadFileManager(path = null) {
    if (path !== null) fileManagerState.path = normalizeFileManagerClientPath(path);
    if (!fileManagerState.path) fileManagerState.path = '/';
    clearFileManagerSelection();
    setFileManagerStorageState(null);
    setFileManagerStatus(fileManagerText('Lade Dateien...', 'Loading files...'));

    try {
      const res = await fetch(fileManagerUrl('/api/files/list', {
        fs: fileManagerState.fs,
        path: fileManagerState.path
      }), { cache: 'no-store' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.success) {
        const loadError = new Error(data.error || fileManagerText('Dateiliste konnte nicht geladen werden.', 'Could not load file list.'));
        loadError.status = res.status;
        throw loadError;
      }
      fileManagerState.path = data.path || fileManagerState.path;
      if (fileManagerState.fs === 'internal') {
        setFileManagerStorageState(true,
          formatFileManagerSize({ size: data.used }) + ' / ' + formatFileManagerSize({ size: data.total }));
      } else {
        setFileManagerStorageState(true);
      }
      renderFileManagerEntries(data.entries || []);
      const count = Array.isArray(data.entries) ? data.entries.length : 0;
      setFileManagerStatus(fileManagerText('Bereit. Eintr\u00e4ge: ', 'Ready. Entries: ') + count, true);
      fileManagerLoaded = true;
    } catch (err) {
      const rawMessage = err?.message || fileManagerText('Dateimanager-Fehler.', 'File manager error.');
      const sdMissing = fileManagerState.fs === 'sd' &&
        (err?.status === 503 || /microsd/i.test(rawMessage));
      const message = sdMissing
        ? fileManagerText('Keine microSD erkannt.', 'No microSD detected.')
        : rawMessage;
      if (fileManagerState.fs === 'internal') {
        // The badge names the storage (the switch's own label) in red.
        const label = document.querySelector('.file-manager-storage-switch [data-fs="internal"]');
        setFileManagerStorageState(false, label ? label.textContent : null);
      } else {
        setFileManagerStorageState(sdMissing ? false : true, sdMissing ? null : fileManagerText('microSD erkannt', 'microSD detected'));
      }
      renderFileManagerEntries([], message);
      setFileManagerStatus(message, false);
      if (!sdMissing) showNotification(message, false);
    }
  }

  function downloadFileManagerFile(path) {
    window.location.href = fileManagerUrl('/api/files/download', {
      fs: fileManagerState.fs,
      path: path
    });
  }

  function openSelectedFileManagerEntry() {
    const entry = fileManagerState.selected;
    if (!entry || !entry.path) return;
    if (entry.dir) {
      loadFileManager(entry.path);
      return;
    }
    downloadFileManagerFile(entry.path);
  }

  function renameSelectedFileManagerEntry() {
    const entry = fileManagerState.selected;
    if (!entry || !entry.path || entry.parent) return;
    renameFileManagerEntry(entry.path, entry.name);
  }

  function deleteSelectedFileManagerEntry() {
    const entry = fileManagerState.selected;
    if (!entry || !entry.path || entry.parent) return;
    deleteFileManagerEntry(entry.path, entry.name, !!entry.dir);
  }

  async function postFileManagerForm(endpoint, fields) {
    const body = new URLSearchParams(fields);
    const res = await fetch(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok || !data.success) {
      throw new Error(data.error || fileManagerText('Aktion fehlgeschlagen.', 'Action failed.'));
    }
    return data;
  }

  async function createFileManagerFolder() {
    const name = prompt(fileManagerText('Name des neuen Ordners:', 'New folder name:'));
    if (name === null) return;
    const trimmed = String(name || '').trim();
    if (!trimmed) return;
    try {
      await postFileManagerForm('/api/files/mkdir', {
        fs: fileManagerState.fs,
        path: fileManagerState.path || '/',
        name: trimmed
      });
      showNotification(fileManagerText('Ordner erstellt.', 'Folder created.'));
      loadFileManager();
    } catch (err) {
      showNotification(err?.message || fileManagerText('Ordner konnte nicht erstellt werden.', 'Could not create folder.'), false);
    }
  }

  async function renameFileManagerEntry(path, currentName) {
    const name = prompt(fileManagerText('Neuer Name:', 'New name:'), currentName || '');
    if (name === null) return;
    const trimmed = String(name || '').trim();
    if (!trimmed || trimmed === currentName) return;
    try {
      await postFileManagerForm('/api/files/rename', {
        fs: fileManagerState.fs,
        path,
        name: trimmed
      });
      invalidateScreensaverEditor();
      showNotification(fileManagerText('Umbenannt.', 'Renamed.'));
      loadFileManager();
    } catch (err) {
      showNotification(err?.message || fileManagerText('Umbenennen fehlgeschlagen.', 'Rename failed.'), false);
    }
  }

  async function deleteFileManagerEntry(path, name, isDir) {
    const message = isDir
      ? fileManagerText('Ordner wirklich l\u00f6schen? Inhalt wird mit gel\u00f6scht: ', 'Delete folder and all contents: ')
      : fileManagerText('Datei wirklich l\u00f6schen: ', 'Delete file: ');
    if (!confirm(message + (name || path))) return;
    try {
      await postFileManagerForm('/api/files/delete', {
        fs: fileManagerState.fs,
        path
      });
      invalidateScreensaverEditor();
      showNotification(fileManagerText('Gel\u00f6scht.', 'Deleted.'));
      loadFileManager();
    } catch (err) {
      showNotification(err?.message || fileManagerText('L\u00f6schen fehlgeschlagen.', 'Delete failed.'), false);
    }
  }

  // Do not start parallel requests such as sensor polling while an upload is
  // running: the server handles one connection at a time, so extra connections
  // only queue up and strain the small internal buffer pool of the device.
  let fileManagerUploadBusy = false;

  // Sequential small chunks instead of one large POST: the device has little
  // internal RAM for WLAN receive buffers. A large upload lets the browser send
  // up to 64KB ahead unacknowledged, which reproducibly crashed the SDIO
  // receive path. With 16KB per request the amount in flight stays bounded.
  const FILE_MANAGER_UPLOAD_PART_SIZE = 16 * 1024;

  async function uploadFileManagerFile() {
    const input = document.getElementById('file_manager_upload');
    if (!input || !input.files || !input.files.length) {
      showNotification(fileManagerText('Bitte zuerst eine Datei ausw\u00e4hlen.', 'Select a file first.'), false);
      return;
    }
    if (fileManagerUploadBusy) return;
    const files = Array.from(input.files);
    fileManagerUploadBusy = true;
    try {
      for (let fileIndex = 0; fileIndex < files.length; fileIndex++) {
        const file = files[fileIndex];
        let offset = 0;
        let firstPart = true;
        while (offset < file.size || firstPart) {
          const end = Math.min(offset + FILE_MANAGER_UPLOAD_PART_SIZE, file.size);
          const formData = new FormData();
          formData.append('file', new File([file.slice(offset, end)], file.name));
          const pct = file.size ? Math.round((end / file.size) * 100) : 100;
          setFileManagerStatus(
            fileManagerText('Upload ', 'Upload ') + (fileIndex + 1) + '/' + files.length +
            ': ' + file.name + ' - ' + pct + '%');
          const res = await fetch(fileManagerUrl('/api/files/upload', {
            fs: fileManagerState.fs,
            path: fileManagerState.path || '/',
            append: firstPart ? '0' : '1'
          }), { method: 'POST', body: formData });
          const data = await res.json().catch(() => ({}));
          if (!res.ok || !data.success) {
            throw new Error(data.error || fileManagerText('Upload fehlgeschlagen.', 'Upload failed.'));
          }
          offset = end;
          firstPart = false;
        }
      }
      input.value = '';
      updateFileManagerUploadName(input);
      const completeMessage = files.length === 1
        ? fileManagerText('Upload abgeschlossen.', 'Upload complete.')
        : files.length + fileManagerText(' Dateien hochgeladen.', ' files uploaded.');
      setFileManagerStatus(completeMessage, true);
      showNotification(completeMessage);
      invalidateScreensaverEditor();
      loadFileManager();
    } catch (err) {
      const message = err?.message || fileManagerText('Upload fehlgeschlagen.', 'Upload failed.');
      setFileManagerStatus(message, false);
      showNotification(message, false);
    } finally {
      fileManagerUploadBusy = false;
    }
  }

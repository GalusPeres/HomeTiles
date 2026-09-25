
function normalizeIconName(value) {
    let icon = String(value || '').trim().toLowerCase();
    if (icon.startsWith('mdi:')) icon = icon.substring(4);
    else if (icon.startsWith('mdi-')) icon = icon.substring(4);
    return icon;
  }
  function formatFolderLabel(name, folderId) {
    let label = String(name || '').trim();
    if (!label.length) label = t('folderPrefix') + folderId;
    return label;
  }
  function updateFolderTabUi(folderId, name, icon) {
    if (folderId === undefined || folderId === null) return;
    const folderNum = parseInt(folderId, 10);
    if (isNaN(folderNum)) return;
    const label = formatFolderLabel(name, folderNum);
    const iconName = normalizeIconName(icon);
    const tabId = tabByFolder[folderNum];
    if (tabId) {
      const tabEl = document.getElementById('tab-tiles-' + tabId);
      if (tabEl) {
        tabEl.dataset.folderName = label;
        tabEl.dataset.folderIcon = iconName;
      }
      const btn = document.querySelector(
        '.folder-tab-btn[data-folder-id="' + folderNum + '"]') ||
        Array.from(document.querySelectorAll('.tab-btn')).find(
          b => b.dataset.tabTarget === 'tab-tiles-' + tabId);
      if (btn) {
        btn.dataset.folderName = label;
        btn.dataset.folderIcon = iconName;
        const labelEl = btn.querySelector('span');
        if (labelEl) labelEl.textContent = label;
        let iconEl = btn.querySelector('i.mdi');
        if (iconName) {
          if (!iconEl) {
            iconEl = document.createElement('i');
            iconEl.className = 'mdi';
            iconEl.style.fontSize = '24px';
            if (labelEl) btn.insertBefore(iconEl, labelEl);
            else btn.appendChild(iconEl);
          }
          iconEl.className = 'mdi mdi-' + iconName;
          iconEl.style.fontSize = '24px';
        } else if (iconEl) {
          iconEl.remove();
        }
      }
    }
    document.querySelectorAll('select[id$="_navigate_target"]').forEach(select => {
      const opt = select.querySelector('option[value="' + folderNum + '"]');
      if (opt) opt.textContent = label;
    });
  }

  function loadNavigateFields(tab, data) {
    const prefix = tab;
    const toggle = document.getElementById(prefix + '_folder_pin_enabled');
    const input = document.getElementById(prefix + '_folder_pin');
    const status = document.getElementById(prefix + '_folder_pin_status');
    if (toggle) toggle.checked = data?.folder_pin_enabled === true;
    if (input) {
      input.value = String(data?.folder_pin || '');
      input.type = 'password';
      const showButton = input.closest('.password-field')
        ?.querySelector('.password-toggle');
      if (showButton) {
        showButton.textContent = showButton.dataset.labelShow || '';
      }
    }
    if (status) {
      status.textContent = data?.folder_pin_enabled === true
        ? navigateText('folderPinSaved')
        : '';
    }
    syncFolderPinControls(tab);
  }

  function saveNavigateFields(tab, formData) {
    const prefix = tab;
    const navEl = document.getElementById(prefix + '_navigate_target');
    if (navEl) {
      formData.append('navigate_target', navEl.value || '0');
    }
  }

  function resetNavigateFields(tab) {
    const prefix = tab;
    const toggle = document.getElementById(prefix + '_folder_pin_enabled');
    const input = document.getElementById(prefix + '_folder_pin');
    const status = document.getElementById(prefix + '_folder_pin_status');
    if (toggle) toggle.checked = false;
    if (input) {
      input.value = '';
      input.type = 'password';
    }
    if (status) status.textContent = '';
    syncFolderPinControls(tab);
  }

  function syncFolderPinControls(tab) {
    const fields = document.getElementById(tab + '_navigate_fields');
    const toggle = document.getElementById(tab + '_folder_pin_enabled');
    const input = document.getElementById(tab + '_folder_pin');
    const label = fields?.querySelector('.folder-pin-label');
    const control = fields?.querySelector('.folder-pin-control');
    const status = document.getElementById(tab + '_folder_pin_status');
    const type = Number(document.getElementById(tab + '_tile_type')?.value || 0);
    const tile = tilesData?.[tab]?.[currentTileIndex];
    const tileEl = document.getElementById(tab + '-tile-' + currentTileIndex);
    const targetSelect = document.getElementById(tab + '_navigate_target');
    const folderId = Number(
      targetSelect?.value ?? tile?.navigate_target ??
      tileEl?.dataset.navigateTarget ?? 0);
    const isFolderTile = type === 4 && Number.isInteger(folderId) && folderId > 0;
    if (fields) fields.classList.toggle('is-hidden', !isFolderTile);

    const showPinEditor = isFolderTile && toggle?.checked === true;
    label?.classList.toggle('is-hidden', !showPinEditor);
    control?.classList.toggle('is-hidden', !showPinEditor);
    status?.classList.toggle('is-hidden', !showPinEditor);
    if (!input) return;
    input.disabled = !showPinEditor;
    if (!showPinEditor) input.value = '';
  }

  function navigateText(key) {
    return typeof NAVIGATE_I18N === 'object' && NAVIGATE_I18N?.[key]
      ? NAVIGATE_I18N[key]
      : t('unknownError');
  }

  async function applyFolderPin(tab) {
    const prefix = tab;
    const toggle = document.getElementById(prefix + '_folder_pin_enabled');
    const input = document.getElementById(prefix + '_folder_pin');
    const button = document.getElementById(prefix + '_folder_pin_apply');
    const status = document.getElementById(prefix + '_folder_pin_status');
    const tile = tilesData?.[tab]?.[currentTileIndex];
    const tileEl = document.getElementById(tab + '-tile-' + currentTileIndex);
    const folderId = Number(
      tile?.navigate_target ?? tileEl?.dataset.navigateTarget ?? 0);
    if (!Number.isInteger(folderId) || folderId <= 0) {
      showNotification(navigateText('folderPinCreateFirst'), false);
      return;
    }

    if (button) button.disabled = true;
    if (status) status.textContent = '';
    try {
      const body = new URLSearchParams();
      body.set('folder_id', String(folderId));
      body.set('enabled', toggle?.checked ? '1' : '0');
      body.set('pin', input?.value || '');
      const response = await fetch('/api/folders/access', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
        body
      });
      const result = await response.json().catch(() => ({}));
      if (!response.ok || !result.success) {
        throw new Error(result.error || navigateText('folderPinSaveFailed'));
      }
      const enabled = result.pin_enabled === true;
      const storedPin = enabled ? String(result.folder_pin || '') : '';
      if (toggle) toggle.checked = enabled;
      if (input) {
        input.value = storedPin;
        input.type = 'password';
        const showButton = input.closest('.password-field')
          ?.querySelector('.password-toggle');
        if (showButton) {
          showButton.textContent = showButton.dataset.labelShow || '';
        }
      }
      if (tile) {
        tile.folder_pin_enabled = enabled;
        tile.folder_pin = storedPin;
      }
      if (tileEl) tileEl.dataset.folderPinEnabled = enabled ? '1' : '0';
      syncFolderPinControls(tab);
      if (status) status.textContent = navigateText('folderPinSaved');
      showNotification(navigateText('folderPinSaved'));
    } catch (error) {
      const message = error?.message || navigateText('folderPinSaveFailed');
      if (toggle && tile) toggle.checked = tile.folder_pin_enabled === true;
      syncFolderPinControls(tab);
      if (status) status.textContent = message;
      showNotification(message, false);
    } finally {
      if (button) button.disabled = false;
    }
  }

  // Back tile: the same per-tile border flag as Clock and Text.
  function loadBackFields(tab, data) {
    const border = document.getElementById(tab + '_back_tile_border');
    if (border) border.checked = data?.tile_border !== undefined ? !['0','false'].includes(String(data.tile_border)) : Number(data?.sensor_display_mode) !== 1;
  }

  function saveBackFields(tab, formData) {
    formData.append('tile_border', document.getElementById(tab + '_back_tile_border')?.checked === false ? '0' : '1');
  }

  function resetBackFields(tab) {
    const border = document.getElementById(tab + '_back_tile_border');
    if (border) border.checked = true;
  }

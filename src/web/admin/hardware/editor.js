
  let hardwareIoModel = null;
  let hardwareIoLoaded = false;
  let hardwareIoLoading = false;
  let hardwareIoBound = false;
  let hardwareIoSaving = false;
  let hardwareIoDirty = false;
  let hardwareIoEditVersion = 0;
  let hardwareIoEntityRefreshTimers = [];

  function setHardwareIoSaveState(text, state = '') {
    const el = document.getElementById('hardwareIoSaveState');
    if (!el) return;
    el.textContent = text || '';
    el.className = 'hardware-io-save-state' + (state ? ' ' + state : '');
  }

  function updateHardwareIoSaveActions() {
    const save = document.getElementById('hardwareIoSave');
    const restart = document.getElementById('hardwareIoRestart');
    if (save) save.disabled = hardwareIoSaving || !hardwareIoDirty;
    if (restart) restart.disabled = hardwareIoSaving;
  }

  function markHardwareIoDirty() {
    hardwareIoEditVersion++;
    hardwareIoDirty = true;
    setHardwareIoSaveState(t('ioUnsavedChanges'));
    updateHardwareIoSaveActions();
  }

  function hardwareIoSupportsPin(pin, type) {
    if (!hardwareIoModel || !Array.isArray(hardwareIoModel.pin_options)) return false;
    const option = hardwareIoModel.pin_options.find(item => Number(item.gpio) === Number(pin));
    if (!option || !option[type]) return false;
    return !option.requires_variant ||
      option.requires_variant === hardwareIoModel.board_variant;
  }

  function hardwareIoUnusedPins(type, exceptIndex = -1, includeOtherVariants = false) {
    if (!hardwareIoModel || !Array.isArray(hardwareIoModel.pin_options)) return [];
    const used = new Set((hardwareIoModel.channels || [])
      .map((channel, index) => index === exceptIndex ? null : Number(channel.gpio))
      .filter(pin => Number.isFinite(pin)));
    return hardwareIoModel.pin_options.filter(option => {
      if (!option || !option[type] || used.has(Number(option.gpio))) return false;
      return includeOtherVariants || hardwareIoSupportsPin(option.gpio, type);
    });
  }

  function syncHardwareIoBoardVariant() {
    const selectedVariant = (hardwareIoModel?.channels || []).map(channel =>
      (hardwareIoModel.pin_options || []).find(option =>
        Number(option.gpio) === Number(channel.gpio))?.requires_variant || '')
      .find(Boolean);
    hardwareIoModel.board_variant = selectedVariant || 'standard';
  }

  function nextHardwareIoId(type) {
    const prefix = type === 'temperature' ? 'temperature_' : 'switch_';
    const existing = new Set((hardwareIoModel?.channels || []).map(channel => String(channel.id || '')));
    for (let i = 1; i < 100; i++) {
      const candidate = prefix + i;
      if (!existing.has(candidate)) return candidate;
    }
    return prefix + Date.now().toString(36);
  }

  function hardwareIoAsciiSlug(value) {
    let slug = String(value || '').replace(/[A-Z]/g, character => character.toLowerCase());
    slug = slug.replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
    return slug;
  }

  function hardwareIoDeviceSlug() {
    const slug = hardwareIoAsciiSlug(
      hardwareIoModel?.entity_prefix || hardwareIoModel?.device_name || 'panel');
    return slug || 'panel';
  }

  function hardwareIoNameSlug(channel) {
    const slug = hardwareIoAsciiSlug(channel?.name || '');
    return slug || String(channel?.id || 'channel');
  }

  function hardwareIoLocalEntityId(channel) {
    const domain = channel?.type === 'temperature' ? 'sensor.' : 'switch.';
    return domain + hardwareIoDeviceSlug() + '_' + hardwareIoNameSlug(channel);
  }

  function updateHardwareIoActions() {
    const channels = hardwareIoModel?.channels || [];
    const atLimit = channels.length >= Number(hardwareIoModel?.max_channels || 8);
    const addSwitch = document.getElementById('hardwareIoAddSwitch');
    const addTemperature = document.getElementById('hardwareIoAddTemperature');
    const onboardSwitchAvailable = (hardwareIoModel.pin_options || []).some(option =>
      option.onboard && option.relay &&
      !channels.some(channel => Number(channel.gpio) === Number(option.gpio)));
    if (addSwitch) {
      addSwitch.disabled = atLimit ||
        (!onboardSwitchAvailable && hardwareIoUnusedPins('relay').length === 0);
    }
    if (addTemperature) {
      addTemperature.disabled = atLimit || hardwareIoUnusedPins('temperature').length === 0;
    }
    updateHardwareIoSaveActions();
  }

  function createHardwareIoField(labelText, control, fieldClass = '') {
    const field = document.createElement('div');
    field.className = 'hardware-io-field' + (fieldClass ? ' ' + fieldClass : '');
    const label = document.createElement('label');
    label.textContent = labelText;
    field.appendChild(label);
    field.appendChild(control);
    return field;
  }

  function makeHardwareIoSelect(options, value) {
    const select = document.createElement('select');
    options.forEach(item => {
      const option = document.createElement('option');
      option.value = String(item.value);
      option.textContent = item.label;
      select.appendChild(option);
    });
    select.value = String(value);
    return select;
  }

  function makeHardwareIoToggle(options, value, onChange) {
    const group = document.createElement('div');
    group.className = 'hardware-io-toggle';
    group.setAttribute('role', 'group');
    const buttons = [];
    const activate = nextValue => {
      buttons.forEach(button => {
        const active = button.dataset.value === String(nextValue);
        button.classList.toggle('active', active);
        button.setAttribute('aria-pressed', active ? 'true' : 'false');
      });
    };
    options.forEach(item => {
      const button = document.createElement('button');
      button.type = 'button';
      button.dataset.value = String(item.value);
      button.textContent = item.label;
      button.disabled = !!item.disabled;
      button.addEventListener('click', () => {
        activate(item.value);
        onChange(item.value);
      });
      buttons.push(button);
      group.appendChild(button);
    });
    activate(value);
    return group;
  }

  function renderHardwareIoCard(channel, index) {
    const card = document.createElement('div');
    card.className = 'hardware-io-card hardware-io-card-' +
      (channel.type === 'temperature' ? 'temperature' : 'switch');

    const header = document.createElement('div');
    header.className = 'hardware-io-card-header';
    const typeLabel = document.createElement('div');
    typeLabel.className = 'hardware-io-card-type';
    typeLabel.textContent = channel.type === 'temperature'
      ? t('ioTemperature') : t('ioSwitch');
    const idPreview = document.createElement('div');
    idPreview.className = 'hardware-io-card-id';
    idPreview.textContent = hardwareIoLocalEntityId(channel);
    header.append(typeLabel, idPreview);
    card.appendChild(header);

    const fields = document.createElement('div');
    fields.className = 'hardware-io-fields';
    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.maxLength = 48;
    nameInput.required = true;
    nameInput.value = channel.name || '';
    nameInput.placeholder = channel.type === 'temperature'
      ? t('ioTemperature') : t('ioSwitch');
    nameInput.addEventListener('input', () => {
      channel.name = nameInput.value;
      idPreview.textContent = hardwareIoLocalEntityId(channel);
      markHardwareIoDirty();
    });
    fields.appendChild(createHardwareIoField(
      t('ioName'), nameInput, 'hardware-io-field-name'));

    const availablePins = hardwareIoUnusedPins(channel.type, index);
    if (channel.type === 'relay') {
      hardwareIoUnusedPins(channel.type, index, true).forEach(option => {
        if (!availablePins.some(existing => Number(existing.gpio) === Number(option.gpio))) {
          availablePins.push(option);
        }
      });
    }
    const selectedOption = (hardwareIoModel.pin_options || []).find(option =>
      Number(option.gpio) === Number(channel.gpio) && option[channel.type]);
    if (selectedOption && !availablePins.some(option => Number(option.gpio) === Number(channel.gpio))) {
      availablePins.unshift(selectedOption);
    }
    const gpioSelect = makeHardwareIoSelect(
      availablePins.length
        ? availablePins.map(option => ({value: option.gpio, label: option.label || ('GPIO ' + option.gpio)}))
        : [{value: -1, label: t('ioNoFreeGpio')}],
      channel.gpio);
    gpioSelect.disabled = availablePins.length === 0;
    gpioSelect.addEventListener('change', () => {
      channel.gpio = Number(gpioSelect.value);
      syncHardwareIoBoardVariant();
      renderHardwareIo();
      markHardwareIoDirty();
    });
    fields.appendChild(createHardwareIoField(
      t('ioGpio'), gpioSelect, 'hardware-io-field-gpio'));

    if (channel.type === 'relay') {
      const pinDescriptor = (hardwareIoModel.pin_options || []).find(option =>
        Number(option.gpio) === Number(channel.gpio));
      const fixedOutputLogic = String(pinDescriptor?.fixed_output_logic || '');
      if (fixedOutputLogic === 'high') channel.inverted = false;
      if (fixedOutputLogic === 'low') channel.inverted = true;
      let logicControl;
      if (fixedOutputLogic) {
        logicControl = document.createElement('div');
        logicControl.className = 'hardware-io-fixed-value';
        logicControl.textContent = fixedOutputLogic === 'low'
          ? t('ioActiveLow') : t('ioActiveHigh');
      } else {
        logicControl = makeHardwareIoToggle([
          {value: 'high', label: t('ioHigh')},
          {value: 'low', label: t('ioLow')}
        ], channel.inverted ? 'low' : 'high', value => {
          channel.inverted = value === 'low';
          markHardwareIoDirty();
        });
      }
      fields.appendChild(createHardwareIoField(
        t('ioOutputLogic'), logicControl, 'hardware-io-field-logic'));

      const boot = makeHardwareIoToggle([
        {value: 'off', label: t('ioOff')},
        {value: 'on', label: t('ioOn')}
      ], channel.boot_state || 'off', value => {
        channel.boot_state = value;
        markHardwareIoDirty();
      });
      fields.appendChild(createHardwareIoField(
        t('ioAfterRestart'), boot, 'hardware-io-field-boot'));
    } else {
      const precision = makeHardwareIoSelect([
        {value: 0, label: t('ioDecimalsZero')},
        {value: 1, label: t('ioDecimalOne')},
        {value: 2, label: t('ioDecimalsTwo')},
        {value: 3, label: t('ioDecimalsThree')}
      ], channel.precision ?? 1);
      precision.addEventListener('change', () => {
        channel.precision = Number(precision.value);
        markHardwareIoDirty();
      });
      fields.appendChild(createHardwareIoField(
        t('ioPrecision'), precision, 'hardware-io-field-precision'));
    }

    const remove = document.createElement('button');
    remove.type = 'button';
    remove.className = 'hardware-io-delete mdi mdi-delete-outline';
    remove.title = t('ioRemoveAssignment');
    remove.addEventListener('click', () => {
      if (!window.confirm(tf('ioRemoveConfirm', {
        name: channel.name || channel.id
      }))) return;
      hardwareIoModel.channels.splice(index, 1);
      syncHardwareIoBoardVariant();
      renderHardwareIo();
      markHardwareIoDirty();
    });
    fields.appendChild(remove);
    card.appendChild(fields);
    return card;
  }

  function renderHardwareIo() {
    const list = document.getElementById('hardwareIoList');
    if (!list || !hardwareIoModel) return;
    list.innerHTML = '';
    const channels = hardwareIoModel.channels || [];
    if (!channels.length) {
      const empty = document.createElement('div');
      empty.className = 'hardware-io-empty';
      empty.textContent = (hardwareIoModel.pin_options || []).length
        ? t('ioEmpty') : t('ioNoProfile');
      list.appendChild(empty);
    } else {
      channels.forEach((channel, index) => list.appendChild(renderHardwareIoCard(channel, index)));
    }
    updateHardwareIoActions();
  }

  function addHardwareIoChannel(type) {
    if (!hardwareIoModel) return;
    const channels = hardwareIoModel.channels || (hardwareIoModel.channels = []);
    if (channels.length >= Number(hardwareIoModel.max_channels || 8)) return;
    let pin = null;
    if (type === 'relay') {
      pin = hardwareIoUnusedPins(type)[0] || hardwareIoUnusedPins(type, -1, true)[0] || null;
    }
    if (!pin) pin = hardwareIoUnusedPins(type)[0];
    if (!pin) {
      setHardwareIoSaveState(t('ioNoCompatibleGpio'), 'error');
      return;
    }
    const sequence = channels.filter(channel => channel.type === type).length + 1;
    channels.push({
      id: nextHardwareIoId(type),
      name: (type === 'temperature' ? t('ioTemperature') : t('ioSwitch')) +
        ' ' + sequence,
      type,
      gpio: Number(pin.gpio),
      inverted: pin.fixed_output_logic === 'low',
      boot_state: 'off',
      precision: 1
    });
    syncHardwareIoBoardVariant();
    renderHardwareIo();
    markHardwareIoDirty();
  }

  function bindHardwareIo() {
    if (hardwareIoBound) return;
    hardwareIoBound = true;
    document.getElementById('hardwareIoAddSwitch')?.addEventListener('click', () => {
      addHardwareIoChannel('relay');
    });
    document.getElementById('hardwareIoAddTemperature')?.addEventListener('click', () => {
      addHardwareIoChannel('temperature');
    });
    document.getElementById('hardwareIoSave')?.addEventListener('click', () => {
      saveHardwareIoNow();
    });
    document.getElementById('hardwareIoRestart')?.addEventListener('click', () => {
      const confirmKey = hardwareIoDirty
        ? 'ioRestartUnsavedConfirm' : 'restartConfirm';
      if (!window.confirm(t(confirmKey))) return;
      restartHardwareIoNow();
    });
  }

  function scheduleHardwareIoEntityOptionsRefresh() {
    hardwareIoEntityRefreshTimers.forEach(timer => window.clearTimeout(timer));
    hardwareIoEntityRefreshTimers = [0, 2500, 7500].map(delay => window.setTimeout(() => {
      fetchEntityOptions(true).then(() => {
        refreshEntityPickers();
        fetchSensorMetaCache(true);
      }).catch(() => {});
    }, delay));
  }

  function restartHardwareIoNow() {
    setHardwareIoSaveState(t('ioRestarting'), 'saving');
    const restartForm = document.getElementById('admin_restart_form');
    // form.submit() cannot carry the CSRF header a password-protected panel
    // requires; send the same request with fetch and reload as before.
    if (restartForm && !window.HomeTilesAuth?.csrfToken()) {
      window.setTimeout(() => restartForm.submit(), 100);
      return;
    }
    fetch('/restart', {method: 'POST'}).catch(() => {}).finally(() => {
      if (restartForm) window.setTimeout(() => window.location.assign('/'), 300);
    });
  }

  async function saveHardwareIoNow() {
    if (!hardwareIoModel || hardwareIoSaving) return false;
    if (!hardwareIoDirty) return true;
    if ((hardwareIoModel.channels || []).some(channel =>
      !String(channel.name || '').trim())) {
      setHardwareIoSaveState(t('ioNameRequired'), 'error');
      return false;
    }
    hardwareIoSaving = true;
    updateHardwareIoSaveActions();
    const saveVersion = hardwareIoEditVersion;
    const channels = (hardwareIoModel.channels || []).map(channel => ({
      id: String(channel.id || ''),
      name: String(channel.name || '').trim(),
      type: channel.type === 'temperature' ? 'temperature' : 'relay',
      gpio: Number(channel.gpio),
      inverted: !!channel.inverted,
      boot_state: channel.boot_state === 'on' ? 'on' : 'off',
      precision: Math.max(0, Math.min(3, Number(channel.precision ?? 1)))
    }));
    setHardwareIoSaveState(t('ioSaving'), 'saving');
    let saved = false;
    try {
      const response = await fetch('/api/hardware-io', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          board_variant: hardwareIoModel.board_variant || 'standard',
          channels
        })
      });
      const result = await response.json().catch(() => ({}));
      if (!response.ok || !result.success) {
        console.error('Hardware I/O save rejected:', result.error || response.status);
        throw new Error(t('saveFailed'));
      }
      if (saveVersion === hardwareIoEditVersion) {
        hardwareIoDirty = false;
        setHardwareIoSaveState(t('ioSaved'), 'saved');
        saved = true;
      } else {
        setHardwareIoSaveState(t('ioUnsavedChanges'));
      }
      scheduleHardwareIoEntityOptionsRefresh();
    } catch (error) {
      console.error('Hardware I/O save failed:', error);
      if (saveVersion === hardwareIoEditVersion) {
        setHardwareIoSaveState(t('saveFailed'), 'error');
      }
    } finally {
      hardwareIoSaving = false;
      updateHardwareIoSaveActions();
    }
    return saved;
  }

  async function initHardwareIo() {
    bindHardwareIo();
    if (hardwareIoLoaded || hardwareIoLoading) {
      if (hardwareIoLoaded) renderHardwareIo();
      return;
    }
    hardwareIoLoading = true;
    try {
      const response = await fetch('/api/hardware-io');
      const data = await response.json();
      if (!response.ok || !data?.success) throw new Error(data?.error || ('HTTP ' + response.status));
      data.channels = Array.isArray(data.channels) ? data.channels : [];
      data.pin_options = Array.isArray(data.pin_options) ? data.pin_options : [];
      data.board_variant = data.board_variant || 'standard';
      hardwareIoModel = data;
      hardwareIoLoaded = true;
      hardwareIoDirty = false;
      renderHardwareIo();
      setHardwareIoSaveState(t('ioSaved'), 'saved');
      updateHardwareIoSaveActions();
    } catch (error) {
      const list = document.getElementById('hardwareIoList');
      if (list) {
        list.innerHTML = '';
        const failed = document.createElement('div');
        failed.className = 'hardware-io-empty';
        failed.textContent = t('ioCouldNotLoad');
        list.appendChild(failed);
      }
      setHardwareIoSaveState(t('ioLoadFailed'), 'error');
    } finally {
      hardwareIoLoading = false;
    }
  }

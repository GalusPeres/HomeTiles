  const tileTabs = [];
  const folderByTab = {};
  const tabByFolder = {};
  const folderTabLoadPromises = {};
  const tileDataLoadedTabs = new Set();
  const tileDataLoadPromises = {};
  const sessionRestoredFolderTabs = new Set();
  let currentTileTab = '';
  let currentTileIndex = -1;
  let drafts = {};
  let tilesData = {};
  let autoSaveTimers = {};
  let saveRequestSeq = 0;
  let latestSaveRequestByTab = {};
  let saveInFlightByTile = {};
  let queuedSaveByTile = {};
  let sensorMetaCache = { values: {}, units: {}, icons: {}, names: {}, loaded: false };
  let sensorMetaFetchInFlight = null;
  let lastSensorMetaFetchMs = 0;
  let entityOptionsCache = null;
  let entityOptionsFetchInFlight = null;
  let lastEntityOptionsFetchMs = 0;
  const ENTITY_OPTIONS_CACHE_MS = 60000;
  const SELECTED_TILE_STORAGE_KEY = 'selectedAdminTile';
  let selectedTileByTab = {};

  function normalizeSensorMetaPayload(payload) {
    if (!payload || typeof payload !== 'object') {
      return { values: {}, units: {}, icons: {}, names: {}, loaded: false };
    }
    const hasMeta = Object.prototype.hasOwnProperty.call(payload, 'editable_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'units') ||
                    Object.prototype.hasOwnProperty.call(payload, 'icons') ||
                    Object.prototype.hasOwnProperty.call(payload, 'names') ||
                    Object.prototype.hasOwnProperty.call(payload, 'binary_sensor_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'energy_values') ||
                    Object.prototype.hasOwnProperty.call(payload, 'energy_units') ||
                    Object.prototype.hasOwnProperty.call(payload, 'climate_values');
    if (!hasMeta) {
      return { values: payload || {}, units: {}, icons: {}, names: {}, loaded: true };
    }
    return {
      values: Object.assign(
        {},
        payload.values || {},
        payload.binary_sensor_values || {},
        payload.energy_values || {},
        payload.climate_values || {}
      ),
      editableValues: payload.editable_values || payload.editableValues || {},
      units: Object.assign({}, payload.units || {}, payload.energy_units || {}),
      icons: payload.icons || {},
      names: payload.names || {},
      loaded: true
    };
  }

  function isSensorMetaCacheLoaded() {
    return !!(sensorMetaCache && sensorMetaCache.loaded);
  }

  function fetchSensorMetaCache(force = false) {
    const now = Date.now();
    if (sensorMetaFetchInFlight) return sensorMetaFetchInFlight;
    if (!force && sensorMetaCache.loaded && (now - lastSensorMetaFetchMs) < 15000) {
      return Promise.resolve(sensorMetaCache);
    }
    sensorMetaFetchInFlight = fetch('/api/sensor_values')
      .then(res => res.json())
      .then(raw => {
        sensorMetaCache = normalizeSensorMetaPayload(raw || {});
        lastSensorMetaFetchMs = Date.now();
        return sensorMetaCache;
      })
      .catch(() => sensorMetaCache)
      .finally(() => { sensorMetaFetchInFlight = null; });
    return sensorMetaFetchInFlight;
  }

  function fetchEntityOptions(force = false) {
    const now = Date.now();
    if (entityOptionsFetchInFlight) return entityOptionsFetchInFlight;
    if (!force && entityOptionsCache &&
        (now - lastEntityOptionsFetchMs) < ENTITY_OPTIONS_CACHE_MS) {
      return Promise.resolve(entityOptionsCache);
    }
    entityOptionsFetchInFlight = fetch('/api/entity_options')
      .then(res => {
        if (!res.ok) throw new Error('Entity options HTTP ' + res.status);
        return res.json();
      })
      .then(data => {
        if (!data || !data.success) throw new Error('Invalid entity options');
        entityOptionsCache = data;
        lastEntityOptionsFetchMs = Date.now();
        return data;
      })
      .finally(() => { entityOptionsFetchInFlight = null; });
    return entityOptionsFetchInFlight;
  }

  function isExplicitlyDisabledValue(raw) {
    if (raw === undefined || raw === null) return false;
    const text = String(raw);
    if (!text.length) return false;
    const trimmed = text.trim().toLowerCase();
    if (!trimmed.length) return true;
    return trimmed === '-' || trimmed === 'none' || trimmed === 'null' || trimmed === 'no' || trimmed === 'off';
  }

  // Mirrors appendHtmlEscaped() in src/web/server/web_admin_utils.cpp. The tile
  // previews are assembled as markup strings, so every tile title, unit, value
  // and icon name coming from a configuration or from Home Assistant has to be
  // escaped before it is inserted.
  function escapeHtml(value) {
    return String(value ?? '')
      .replaceAll('&', '&amp;')
      .replaceAll('<', '&lt;')
      .replaceAll('>', '&gt;')
      .replaceAll('"', '&quot;')
      .replaceAll("'", '&#39;');
  }

  function normalizeMdiIconName(raw) {
    let iconName = String(raw || '').trim().toLowerCase();
    if (iconName.startsWith('mdi:')) iconName = iconName.substring(4);
    else if (iconName.startsWith('mdi-')) iconName = iconName.substring(4);
    return iconName;
  }

  function resolveIconName(rawIcon, entityId, metaIcons) {
    if (isExplicitlyDisabledValue(rawIcon)) return '';
    const direct = normalizeMdiIconName(rawIcon);
    if (direct) return direct;
    if (entityId && metaIcons && metaIcons[entityId]) {
      return normalizeMdiIconName(metaIcons[entityId]);
    }
    return '';
  }

  function resolveUnitValue(rawUnit, entityId, metaUnits) {
    if (isExplicitlyDisabledValue(rawUnit)) return '';
    const direct = String(rawUnit || '').trim();
    if (direct.length) return rawUnit;
    if (entityId && metaUnits && metaUnits[entityId]) {
      return metaUnits[entityId];
    }
    return '';
  }

  function normalizeTileTitle(value) {
    const lines = String(value ?? '').replace(/\r\n?/g, '\n').replace(/\\n/g, '\n').split('\n');
    const text = lines.length > 2 ? lines[0] + '\n' + lines.slice(1).join(' ') : lines.join('\n');
    let bytes = 0, result = '';
    for (const character of text) {
      const code = character.codePointAt(0);
      bytes += code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
      if (bytes > 255) break;
      result += character;
    }
    return result;
  }

  function tileTitleHtml(value) {
    return '<span class="tile-title-lines">' + normalizeTileTitle(value).split('\n')
      .map(line => '<span class="tile-title-line">' + escapeHtml(line) + '</span>').join('') + '</span>';
  }

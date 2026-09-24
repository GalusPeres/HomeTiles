
  function climateEditorState(tab) {
    const entity = document.getElementById(
      tab + '_climate_entity')?.value || '';
    return parseClimatePreviewPayload(
      sensorMetaCache?.values?.[entity] ?? '');
  }

  function climateAvailableContentKinds(tab) {
    const entity = document.getElementById(
      tab + '_climate_entity')?.value || '';
    const state = climateEditorState(tab);
    if (!entity || !state.valid) {
      // Without a selected entity or a loaded state the capabilities are not
      // known yet. Keep the editor usable until real metadata arrives.
      return null;
    }

    const available = new Set([
      CLIMATE_TILE_CONTENT.AUTO,
      CLIMATE_TILE_CONTENT.EMPTY
    ]);
    if (state.current !== '--') {
      available.add(
        CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
    }
    if (state.currentHumidity !== null) {
      available.add(
        CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY);
    }
    if (state.target !== null) {
      available.add(
        CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
    }
    if (state.targetLow !== null) {
      available.add(
        CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW);
    }
    if (state.targetHigh !== null) {
      available.add(
        CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH);
    }
    if (state.targetHumidity !== null) {
      available.add(
        CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
    }
    if (state.mode) {
      available.add(CLIMATE_TILE_CONTENT.HVAC_MODE);
    }
    return available;
  }

  function syncClimateContentOptions(tab) {
    const editor = document.getElementById(
      tab + '_climate_selected_content');
    if (!editor) return;
    const available = climateAvailableContentKinds(tab);
    const selectedKind = Number(editor.value);
    Array.from(editor.options).forEach(option => {
      const kind = Number(option.value);
      // Never silently discard an explicitly stored legacy/custom choice.
      // Keep that one visible so the user can change or remove it.
      const selectedExplicit =
        kind === selectedKind &&
        kind !== CLIMATE_TILE_CONTENT.AUTO &&
        kind !== CLIMATE_TILE_CONTENT.EMPTY;
      const visible =
        !available ||
        available.has(kind) ||
        selectedExplicit;
      option.hidden = !visible;
      option.disabled = !visible;
      option.style.display = visible ? '' : 'none';
    });
  }

  function climateAutomaticEditorKinds(tab) {
    const state = climateEditorState(tab);
    const spanW = Math.max(1, Math.floor(Number(
      document.getElementById(
        tab + '_tile_span_w')?.value) || 1));
    const spanH = Math.max(1, Math.floor(Number(
      document.getElementById(
        tab + '_tile_span_h')?.value) || 1));
    const capacity = climateSlotCapacity(spanW, spanH);
    const kinds = [];
    const add = kind => {
      if (kinds.length < capacity) kinds.push(kind);
    };

    const addPrimaryTarget = () => {
      if (state.targetLow !== null &&
          state.targetHigh !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW);
      } else if (state.target !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      } else if (state.targetHumidity !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      } else if (!state.valid) {
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      }
    };

    if (spanW === 1 && spanH === 1) {
      if (!state.valid || state.current !== '--') {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      } else {
        addPrimaryTarget();
      }
    } else if (spanW >= 2 && spanH === 1) {
      if (!state.valid || state.current !== '--') {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
    } else if (spanW === 1) {
      if (!state.valid || state.current !== '--') {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      addPrimaryTarget();
      if (spanH === 2) return kinds;
      if (state.targetHumidity !== null &&
          (state.targetLow !== null ||
           state.targetHigh !== null ||
           state.target !== null)) {
        add(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      }
    } else {
      if (!state.valid || state.current !== '--') {
        add(CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE);
      }
      if (state.currentHumidity !== null) {
        add(CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY);
      }
      if (state.targetLow !== null &&
          state.targetHigh !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW);
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH);
      } else if (state.target !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE);
      }
      if (state.targetHumidity !== null) {
        add(CLIMATE_TILE_CONTENT.TARGET_HUMIDITY);
      }
      if (state.mode) add(CLIMATE_TILE_CONTENT.HVAC_MODE);
    }
    return kinds;
  }

  function climateResolvedEditorKinds(tab) {
    const configured = currentClimateSlotConfig(tab);
    const automatic = climateAutomaticEditorKinds(tab);
    const capacity = climateSlotCapacity(
      document.getElementById(
        tab + '_tile_span_w')?.value || 1,
      document.getElementById(
        tab + '_tile_span_h')?.value || 1);
    const explicit = new Set();
    configured.slice(0, capacity).forEach(selection => {
      const kind = Number(selection) || 0;
      if (kind !== CLIMATE_TILE_CONTENT.AUTO &&
          kind !== CLIMATE_TILE_CONTENT.EMPTY) {
        explicit.add(kind);
      }
    });
    let cursor = 0;
    return configured.map((selection, index) => {
      const kind = Number(selection) || 0;
      if (index >= capacity ||
          kind === CLIMATE_TILE_CONTENT.EMPTY) {
        return null;
      }
      if (kind !== CLIMATE_TILE_CONTENT.AUTO) return kind;
      while (cursor < automatic.length) {
        const candidate = automatic[cursor++];
        if (!explicit.has(candidate)) return candidate;
      }
      return null;
    });
  }

  function materializeClimateAutomaticItems(
      tab, resolvedOverride = null) {
    const configured = currentClimateSlotConfig(tab);
    const resolved = Array.isArray(resolvedOverride)
      ? resolvedOverride : climateResolvedEditorKinds(tab);
    for (let index = 0; index < 6; ++index) {
      if (Number(configured[index]) !== CLIMATE_TILE_CONTENT.AUTO) {
        continue;
      }
      const source = document.getElementById(
        tab + '_climate_slot_' + index);
      if (!source) continue;
      const kind = Number(resolved[index]);
      source.value = Number.isFinite(kind) && kind > 0
        ? String(kind)
        : String(CLIMATE_TILE_CONTENT.EMPTY);
    }
  }

  function climatePlacementConfig(
      configured, resolvedKinds) {
    return configured.map((selection, index) =>
      resolvedKinds[index] === null
        ? CLIMATE_TILE_CONTENT.EMPTY
        : selection);
  }

  function climateTargetCaption(state, kind) {
    if (state?.available === false) return CLIMATE_I18N.unavailable;
    const entityState = String(state?.mode || '').toLowerCase();
    if (entityState === 'unknown') return CLIMATE_I18N.unknown;
    if (kind === CLIMATE_TILE_CONTENT.TARGET_HUMIDITY) {
      return CLIMATE_I18N.targetHumidity;
    }
    if (kind === CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW) {
      return CLIMATE_I18N.heat;
    }
    if (kind === CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH) {
      return CLIMATE_I18N.cool;
    }

    const action = String(state?.action || '').toLowerCase();
    const mode = String(state?.mode || '').toLowerCase();
    const actionLabels = {
      heating: CLIMATE_I18N.heat,
      preheating: CLIMATE_I18N.heat,
      cooling: CLIMATE_I18N.cool
    };
    if (actionLabels[action]) return actionLabels[action];

    const modeLabels = {
      off: CLIMATE_I18N.off,
      heat: CLIMATE_I18N.heat,
      cool: CLIMATE_I18N.cool,
      heat_cool: CLIMATE_I18N.heatCool,
      auto: CLIMATE_I18N.autoMode,
      dry: CLIMATE_I18N.dry,
      fan_only: CLIMATE_I18N.fanOnly
    };
    if (modeLabels[mode]) return modeLabels[mode];
    if (mode) return CLIMATE_I18N.genericClimate;
    return CLIMATE_I18N.targetTemperature;
  }

  function climateModeText(state) {
    const raw = String(state?.mode || '').toLowerCase();
    if (state?.available === false || raw === 'unavailable') {
      return CLIMATE_I18N.unavailable;
    }
    if (raw === 'unknown') return CLIMATE_I18N.unknown;
    if (!raw) return '--';
    const labels = {
      off: CLIMATE_I18N.off,
      heat: CLIMATE_I18N.heat,
      cool: CLIMATE_I18N.cool,
      auto: CLIMATE_I18N.autoMode,
      dry: CLIMATE_I18N.dry,
      fan_only: CLIMATE_I18N.fanOnly,
      heat_cool: CLIMATE_I18N.heatCool
    };
    if (labels[raw]) return labels[raw];
    const fallback = raw.replaceAll('_', ' ');
    return fallback.charAt(0).toUpperCase() + fallback.slice(1);
  }

  function climateFeatureSupported(state, feature, legacySupported = true) {
    if (!state?.hasSupportedFeatures) return legacySupported;
    return (Number(state.supportedFeatures) & feature) !== 0;
  }

  function climateTargetInteractive(state, kind) {
    if (state?.available === false) return false;
    switch (kind) {
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE:
        return state?.target !== null &&
          climateFeatureSupported(
            state, CLIMATE_SUPPORTED_FEATURE.TARGET_TEMPERATURE);
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW:
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH:
        return state?.targetLow !== null && state?.targetHigh !== null &&
          climateFeatureSupported(
            state, CLIMATE_SUPPORTED_FEATURE.TARGET_TEMPERATURE_RANGE);
      case CLIMATE_TILE_CONTENT.TARGET_HUMIDITY:
        return state?.targetHumidity !== null &&
          climateFeatureSupported(
            state, CLIMATE_SUPPORTED_FEATURE.TARGET_HUMIDITY);
      default:
        return false;
    }
  }

  function climateEditorContentInfo(tab, kind) {
    const state = climateEditorState(tab);
    const unit = state.unit || '\u00B0C';
    const temperature = value =>
      String(value ?? '--') + ' ' + unit;
    const selected = Number(kind) || 0;
    switch (selected) {
      case CLIMATE_TILE_CONTENT.CURRENT_TEMPERATURE:
        return {
          label: CLIMATE_I18N.currentTemperature,
          value: temperature(state.current),
          adjustable: false
        };
      case CLIMATE_TILE_CONTENT.CURRENT_HUMIDITY:
        return {
          label: CLIMATE_I18N.currentHumidity,
          value: state.currentHumidity !== null
            ? state.currentHumidity + '%' : '--%',
          adjustable: false
        };
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE:
        return {
          label: climateTargetCaption(state, selected),
          value: temperature(state.target),
          adjustable: true
        };
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_LOW:
        return {
          label: climateTargetCaption(state, selected),
          value: temperature(state.targetLow),
          adjustable: true
        };
      case CLIMATE_TILE_CONTENT.TARGET_TEMPERATURE_HIGH:
        return {
          label: climateTargetCaption(state, selected),
          value: temperature(state.targetHigh),
          adjustable: true
        };
      case CLIMATE_TILE_CONTENT.TARGET_HUMIDITY:
        return {
          label: climateTargetCaption(state, selected),
          value: state.targetHumidity !== null
            ? state.targetHumidity + '%' : '--%',
          adjustable: true
        };
      case CLIMATE_TILE_CONTENT.HVAC_MODE:
        return {
          label: CLIMATE_I18N.mode,
          value: climateModeText(state),
          adjustable: false
        };
      case CLIMATE_TILE_CONTENT.AUTO:
      default:
        return {
          label: CLIMATE_I18N.automatic,
          value: '--',
          adjustable: false
        };
    }
  }

  function renderClimateEditorItem(tab, index, geometry, kind) {
    const preview = document.getElementById(
      tab + '_climate_preview_' + index);
    if (!preview) return;
    const resolvedKind =
      climateResolvedEditorKinds(tab)[index];
    const info = climateEditorContentInfo(
      tab, resolvedKind ?? kind);
    const expanded =
      geometry.spanW > 1 || geometry.spanH > 1;
    const item = document.getElementById(
      tab + '_climate_slot_row_' + index);
    item?.classList.toggle(
      'climate-mini-control-item',
      info.adjustable && expanded);
    if (info.adjustable && !expanded) {
      preview.innerHTML =
        '<div class="climate-slot climate-slot-control ' +
        'climate-slot-control-compact">' +
        '<span class="climate-minus" aria-hidden="true">-</span>' +
        '<strong>' + escapeHtml(info.value) + '</strong>' +
        '<span class="climate-plus" aria-hidden="true">+</span></div>';
      return;
    }
    if (info.adjustable) {
      const orientation =
        geometry.spanW > 1 && geometry.spanH === 1
          ? 'climate-slot-control-horizontal'
          : (geometry.spanW > 1 && geometry.spanH > 1
              ? 'climate-slot-control-large'
              : 'climate-slot-control-vertical');
      preview.innerHTML =
        '<div class="climate-slot climate-slot-control ' +
        orientation + '">' +
        '<small>' + escapeHtml(info.label) + '</small>' +
        '<span class="climate-minus">-</span>' +
        '<strong>' + escapeHtml(info.value) + '</strong>' +
        '<span class="climate-plus">+</span></div>';
      return;
    }
    preview.innerHTML =
      '<div class="climate-slot climate-slot-value' +
      (resolvedKind === CLIMATE_TILE_CONTENT.HVAC_MODE
        ? ' climate-slot-mode' : '') + '">' +
      '<strong>' + escapeHtml(info.value) + '</strong></div>';
  }

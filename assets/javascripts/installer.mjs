import { ESPLoader, Transport } from "https://unpkg.com/esptool-js@0.6.1/bundle.js";
import { ESP32P4ROM } from "https://unpkg.com/esptool-js@0.6.1/lib/targets/esp32p4.js";
import { ESP32S3ROM } from "https://unpkg.com/esptool-js@0.6.1/lib/targets/esp32s3.js";
import { retainPageComponent } from "./retained-page-component.mjs?v=serial-navigation-2";
import { serialAccess } from "./serial-access.mjs?v=serial-navigation-2";
import { serialActivity } from "./serial-activity.mjs?v=serial-navigation-3";

import {
  APP_SLOTS,
  DEVICE_PROFILES,
  INSTALLER_BAUD_RATE,
  INSTALLER_SCHEMA_VERSION,
  OTA_DATA_LAYOUT,
  PARTITION_TABLE,
  assertFirmwareRevisionCompatible,
  assertHomeTilesPartitionLayout,
  assertOtaBootSelectionApplied,
  buildFlashPlan,
  buildSafeOtaUpdatePlan,
  parseEspIdfPartitionTable,
  parseEspRomChipIdentity,
  releaseAssetNames,
  resolveSameOriginAsset,
  validateFirmwareDescriptor,
} from "./installer-contract.mjs?v=installer-ui-15";

const LAST_RUN_STORAGE_KEY = "hometiles.webInstaller.lastRun.v1";
const LOG_MAX_LINES = 300;
const LOG_MAX_CHARACTERS = 64 * 1024;
const LOG_MAX_LINE_CHARACTERS = 4096;
const GUITION_S3_DEVICE_KEY = "guition_esp32_4848s040";
const GUITION_S3_NORMAL_BOOT_RESET_SEQUENCE = "D0|R1|W100|R0|W100|D0";
const ESP_GET_SECURITY_INFO = 0x14;

class HomeTilesESPLoader extends ESPLoader {
  async detectChip(mode = "default_reset") {
    await this.connect(mode, 7, false);
    this.info("Detecting chip type... ", false);

    const securityInfo = await this.checkCommand(
      "read ESP ROM chip identity",
      ESP_GET_SECURITY_INFO,
      new Uint8Array(0),
      0,
      20,
    );
    const identity = parseEspRomChipIdentity(securityInfo);
    this.chip = identity.chipFamily === "ESP32-P4" ? new ESP32P4ROM() : new ESP32S3ROM();
    this.info(this.chip.CHIP_NAME);
  }
}

export function mountInstaller(root) {
  const elements = {
    copyLog: root.querySelector("#installer-copy-log"),
    device: root.querySelector("#installer-device"),
    deviceDetails: root.querySelector("#installer-device-details"),
    exactHardware: root.querySelector("#installer-exact-hardware"),
    exactHardwareText: root.querySelector("#installer-exact-hardware-text"),
    factoryConfirmation: root.querySelector("#installer-factory-confirmation"),
    factoryConfirmationRow: root.querySelector("#installer-factory-confirmation-row"),
    firmwareFile: root.querySelector("#installer-firmware-file"),
    firmwareSource: root.querySelector("#installer-firmware-source"),
    flash: root.querySelector("#installer-flash"),
    logActionStatus: root.querySelector("#installer-log-action-status"),
    logOutput: root.querySelector("#installer-log-output"),
    logPanel: root.querySelector("#installer-log-panel"),
    phase: root.querySelector("#installer-phase"),
    progress: root.querySelector("#installer-progress"),
    progressPanel: root.querySelector("#installer-progress-panel"),
    progressText: root.querySelector("#installer-progress-text"),
    status: root.querySelector("#installer-status"),
    serialBusy: root.querySelector("#installer-serial-busy"),
  };
  function readLastRun() {
    try {
      const saved = JSON.parse(window.localStorage.getItem(LAST_RUN_STORAGE_KEY) || "null");
      if (
        saved?.version !== 1 ||
        !DEVICE_PROFILES.some((device) => device.key === saved.deviceKey) ||
        !["update", "factory"].includes(saved.mode)
      ) {
        return null;
      }
      // Older versions persisted successful results indefinitely.
      if (!saved.busy && !saved.recoveryRequired && saved.kind === "success") {
        window.localStorage.removeItem(LAST_RUN_STORAGE_KEY);
        return null;
      }
      return {
        version: 1,
        deviceKey: saved.deviceKey,
        mode: saved.mode,
        busy: saved.busy === true,
        mutationStarted: saved.mutationStarted === true,
        recoveryRequired: saved.recoveryRequired === true,
        progress: Math.max(0, Math.min(100, Number(saved.progress) || 0)),
        phase: String(saved.phase || "Previous attempt").slice(0, 80),
        message: String(saved.message || "Previous installer state restored.").slice(0, 1000),
        kind: ["info", "success", "warning", "error"].includes(saved.kind)
          ? saved.kind
          : "info",
        updatedAt: String(saved.updatedAt || ""),
      };
    } catch (error) {
      console.debug("[WebInstaller] Saved installer state could not be read", error);
      return null;
    }
  }

  const state = {
    busy: false,
    flashMutationStarted: false,
    lastPersistedProgress: -5,
    lastRun: readLastRun(),
    progress: 0,
    releaseIndex: null,
    visible: root.isConnected,
  };

  const logState = {
    characterCount: 0,
    followTail: true,
    lines: [],
    renderPending: false,
    terminalPartial: "",
    terminalPartialTimestamp: "",
  };

  function logTimestamp() {
    return new Date().toISOString();
  }

  function sanitizeLogText(value) {
    return String(value ?? "")
      .replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "")
      .replace(/\r\n/g, "\n")
      .replace(/\r/g, "\n");
  }

  function trimLogBuffer() {
    while (
      logState.lines.length + (logState.terminalPartial ? 1 : 0) > LOG_MAX_LINES ||
      logState.characterCount + logState.terminalPartial.length > LOG_MAX_CHARACTERS
    ) {
      const removed = logState.lines.shift();
      if (removed === undefined) {
        logState.terminalPartial = logState.terminalPartial.slice(-LOG_MAX_CHARACTERS);
        break;
      }
      logState.characterCount -= removed.length + 1;
    }
  }

  function pushLogLine(source, message, timestamp = logTimestamp()) {
    const text = sanitizeLogText(message).trimEnd();
    if (!text.trim()) return;
    const line = `${timestamp} [${source}] ${text.slice(0, LOG_MAX_LINE_CHARACTERS)}`;
    logState.lines.push(line);
    logState.characterCount += line.length + 1;
    trimLogBuffer();
  }

  function completeLogText() {
    const lines = [...logState.lines];
    if (logState.terminalPartial) {
      lines.push(
        `${logState.terminalPartialTimestamp || logTimestamp()} [ESPTOOL] ${logState.terminalPartial}`,
      );
    }
    return lines.join("\n");
  }

  function renderLog() {
    logState.renderPending = false;
    if (!root.isConnected) return;
    elements.logOutput.textContent = completeLogText();
    if (logState.followTail) elements.logOutput.scrollTop = elements.logOutput.scrollHeight;
  }

  function resetLog() {
    logState.characterCount = 0;
    logState.followTail = true;
    logState.lines = [];
    logState.renderPending = false;
    logState.terminalPartial = "";
    logState.terminalPartialTimestamp = "";
    elements.logOutput.textContent = "";
  }

  function scheduleLogRender() {
    if (logState.renderPending || !root.isConnected) return;
    logState.renderPending = true;
    window.requestAnimationFrame(renderLog);
  }

  function appendLogLine(source, message) {
    for (const line of sanitizeLogText(message).split("\n")) pushLogLine(source, line);
    scheduleLogRender();
  }

  function appendTerminalText(value, terminateLine = false) {
    const incoming = sanitizeLogText(value);
    if (!logState.terminalPartial && incoming) logState.terminalPartialTimestamp = logTimestamp();
    const parts = `${logState.terminalPartial}${incoming}`.split("\n");
    logState.terminalPartial = parts.pop() || "";
    parts.forEach((line, index) => {
      pushLogLine(
        "ESPTOOL",
        line,
        index === 0 && logState.terminalPartialTimestamp
          ? logState.terminalPartialTimestamp
          : logTimestamp(),
      );
    });
    if (terminateLine || logState.terminalPartial.length > LOG_MAX_LINE_CHARACTERS) {
      pushLogLine(
        "ESPTOOL",
        logState.terminalPartial,
        logState.terminalPartialTimestamp || logTimestamp(),
      );
      logState.terminalPartial = "";
    }
    if (!logState.terminalPartial) logState.terminalPartialTimestamp = "";
    trimLogBuffer();
    scheduleLogRender();
  }

  const terminal = {
    clean() {
      appendTerminalText("", true);
      appendLogLine("ESPTOOL", "Terminal output cleared.");
    },
    write(message) {
      appendTerminalText(message);
    },
    writeLine(message) {
      appendTerminalText(message, true);
    },
  };

  function setLogActionStatus(message) {
    elements.logActionStatus.textContent = message;
  }

  async function copyFlashLog() {
    const text = completeLogText();
    if (!text) {
      setLogActionStatus("The log is empty.");
      return;
    }
    try {
      if (!navigator.clipboard?.writeText) throw new Error("Clipboard API unavailable");
      await navigator.clipboard.writeText(text);
      setLogActionStatus("Log copied.");
    } catch (error) {
      console.debug("[WebInstaller] Flash log copy failed", error);
      setLogActionStatus("Copy failed.");
    }
  }

  function selectedMode() {
    return root.querySelector('input[name="installer-mode"]:checked')?.value || "update";
  }

  function selectedDevice() {
    return state.releaseIndex?.devices.find((device) => device.key === elements.device.value);
  }

  function selectedFirmwareSource() {
    return elements.firmwareSource.value === "local" ? "local" : "published";
  }

  function setPhase(message) {
    elements.phase.textContent = message;
  }

  function setStatus(message, kind = "info") {
    elements.status.textContent = message;
    elements.status.dataset.kind = kind;
    elements.status.setAttribute("role", kind === "error" ? "alert" : "status");
    elements.status.setAttribute("aria-live", kind === "error" ? "assertive" : "polite");
  }

  function setProgress(value) {
    state.progress = Math.max(0, Math.min(100, Number(value) || 0));
    elements.progress.value = state.progress;
    elements.progressText.textContent = `${Math.round(state.progress)}%`;
    if (state.busy) serialActivity.set("installer", state.flashMutationStarted
      ? `Flashing · ${Math.round(state.progress)}%` : elements.phase.textContent, "busy", true);
  }

  function persistLastRun(overrides = {}) {
    const record = {
      version: 1,
      deviceKey: elements.device.value || state.lastRun?.deviceKey || "",
      mode: selectedMode(),
      busy: state.busy,
      mutationStarted: state.flashMutationStarted,
      recoveryRequired: false,
      progress: state.progress,
      phase: elements.phase.textContent,
      message: elements.status.textContent,
      kind: elements.status.dataset.kind || "info",
      updatedAt: new Date().toISOString(),
      ...overrides,
    };
    try {
      if (!record.busy && !record.recoveryRequired && record.kind === "success") {
        window.localStorage.removeItem(LAST_RUN_STORAGE_KEY);
      } else {
        window.localStorage.setItem(LAST_RUN_STORAGE_KEY, JSON.stringify(record));
      }
      state.lastRun = record;
    } catch (error) {
      console.debug("[WebInstaller] Installer state could not be saved", error);
    }
  }

  function showActivity(
    phase,
    message,
    { kind = "info", progress = state.progress, persist = state.busy, announce = true } = {},
  ) {
    setPhase(phase);
    setProgress(progress);
    setStatus(message, kind);
    if (announce && !state.busy && (kind === "error" || kind === "warning" || kind === "success")) {
      serialActivity.set("installer", kind === "success" ? "Flash complete" : phase, kind === "success" ? "success" : "error");
    }
    elements.progressPanel.hidden = !(state.busy || progress > 0);
    if (state.busy || progress > 0 || kind === "warning" || kind === "error") {
      elements.logPanel.hidden = false;
    }
    appendLogLine(
      kind === "error" ? "ERROR" : kind === "warning" ? "WARNING" : "STATUS",
      `${phase}: ${message}`,
    );
    if (persist) persistLastRun();
  }

  function restoreLastRun() {
    const saved = state.lastRun;
    if (!saved) return false;

    const device = state.releaseIndex?.devices.find((item) => item.key === saved.deviceKey);
    if (!device) return false;
    elements.device.value = device.key;
    const modeInput = root.querySelector(`input[name="installer-mode"][value="${saved.mode}"]`);
    if (modeInput) modeInput.checked = true;
    elements.exactHardware.checked = false;
    elements.factoryConfirmation.checked = false;

    if (saved.busy) {
      state.flashMutationStarted = saved.mutationStarted;
      state.busy = false;
      if (saved.mutationStarted) {
        if (saved.mode === "update") {
          showActivity(
            "Update interrupted",
            `The previous update stopped at about ${Math.round(saved.progress)}%. The previously selected app slot was preserved. Restart the display to keep using it, or select the same device and reconnect to retry.`,
            { kind: "warning", progress: saved.progress },
          );
          persistLastRun({ busy: false, recoveryRequired: true });
        } else {
          showActivity(
            "Recovery required",
            `The previous factory reset stopped at about ${Math.round(saved.progress)}%. Keep the device powered, select the same device and mode, confirm the hardware again, then reconnect and retry before normal use.`,
            { kind: "error", progress: saved.progress },
          );
          persistLastRun({ busy: false, recoveryRequired: true });
        }
      } else {
        showActivity(
          "Not started",
          "The previous connection ended before any flash write or erase began. Nothing was flashed.",
          { kind: "warning", progress: 0 },
        );
        persistLastRun({ busy: false, mutationStarted: false, recoveryRequired: false });
      }
    } else {
      showActivity(saved.phase, saved.message, {
        kind: saved.kind,
        progress: saved.progress,
        persist: false,
        announce: false,
      });
    }
    updateFormState();
    return true;
  }

  function resetCompletedRun() {
    if (state.busy || elements.status.dataset.kind !== "success") return;
    state.lastRun = null;
    state.flashMutationStarted = false;
    serialActivity.clear("installer");
    resetLog();
    setLogActionStatus("");
    elements.logPanel.hidden = true;
    showActivity("Ready", "Choose a device and confirm its model.", { progress: 0 });
  }

  function formChanged() {
    resetCompletedRun();
    updateFormState();
  }

  function updateFormState() {
    const device = selectedDevice();
    const mode = selectedMode();
    const isFactory = mode === "factory";
    elements.factoryConfirmationRow.hidden = !isFactory;
    if (!isFactory) elements.factoryConfirmation.checked = false;

    if (device) {
      const pending = device.status === "validation-pending";
      elements.deviceDetails.textContent = pending
        ? "Hardware validation is still pending for this exact profile."
        : "";
      elements.deviceDetails.dataset.kind = pending ? "warning" : "ok";
      elements.deviceDetails.hidden = !pending;
      elements.exactHardwareText.textContent = device.hardwareCheck;
    } else {
      elements.deviceDetails.textContent = "";
      elements.deviceDetails.dataset.kind = "info";
      elements.deviceDetails.hidden = true;
      elements.exactHardwareText.textContent = "I checked the exact model label.";
    }

    const ready =
      !!device &&
      elements.exactHardware.checked &&
      (!isFactory || elements.factoryConfirmation.checked) &&
      !!state.releaseIndex &&
      (selectedFirmwareSource() === "published" || elements.firmwareFile.files.length === 1) &&
      !state.busy &&
      serialAccess.owner !== "installer" &&
      !serialAccess.installerPending &&
      window.isSecureContext &&
      "serial" in navigator;
    elements.flash.disabled = !ready;
    elements.serialBusy.hidden = serialAccess.owner !== "logs";
    elements.device.disabled = state.busy;
    elements.firmwareSource.disabled = state.busy || !state.releaseIndex;
    elements.firmwareFile.hidden = selectedFirmwareSource() !== "local";
    elements.firmwareFile.disabled = state.busy;
    root.querySelectorAll('input[name="installer-mode"]').forEach((input) => {
      input.disabled = state.busy;
    });
    elements.exactHardware.disabled = state.busy;
    elements.factoryConfirmation.disabled = state.busy;
    const retrying =
      state.lastRun?.recoveryRequired === true &&
      state.lastRun.deviceKey === elements.device.value &&
      state.lastRun.mode === mode;
    elements.flash.textContent = retrying ? "Reconnect and retry" : "Connect and flash";
  }

  function populateDevices(profiles = []) {
    elements.device.innerHTML = '<option value="">Choose the exact device…</option>';
    for (const profile of profiles) {
      const option = document.createElement("option");
      option.value = profile.key;
      option.textContent = profile.label;
      elements.device.append(option);
    }
  }

  function validatePublishedIndex(index) {
    if (index?.schemaVersion !== INSTALLER_SCHEMA_VERSION) {
      throw new Error("The published installer index has an unsupported format.");
    }
    if (!Array.isArray(index.devices) || index.devices.length < 1) {
      throw new Error("The published installer index has an incomplete device list.");
    }
    if (index.partial !== true && index.devices.length !== DEVICE_PROFILES.length) {
      throw new Error("The published installer index has an incomplete device list.");
    }
    if (new Set(index.devices.map((device) => device.key)).size !== index.devices.length) {
      throw new Error("The published installer index contains duplicate devices.");
    }
    for (const published of index.devices) {
      const profile = DEVICE_PROFILES.find((device) => device.key === published.key);
      if (
        !published ||
        !profile ||
        published.label !== profile.label ||
        published.buildProfile !== profile.buildProfile ||
        published.chipFamily !== profile.chipFamily ||
        published.flashSize !== profile.flashSize ||
        published.status !== profile.status ||
        published.hardwareCheck !== profile.hardwareCheck ||
        published.metadataDeviceKey !== profile.metadataDeviceKey ||
        published.siliconVariant !== profile.siliconVariant ||
        published.minimumRevision !== profile.minimumRevision ||
        published.maximumRevision !== profile.maximumRevision ||
        published.acceptsLegacyDescriptor !== profile.acceptsLegacyDescriptor
      ) {
        throw new Error(`The published installer profile for ${published?.key || "(missing)"} is inconsistent.`);
      }
      const expectedNames = releaseAssetNames(index.tag, profile.key);
      for (const mode of ["update", "factory"]) {
        const asset = published[mode];
        const validSize =
          mode === "update"
            ? Number.isSafeInteger(asset?.size) && asset.size > 0 && asset.size <= APP_SLOTS[0].size
            : asset?.size === profile.flashSize;
        if (
          !asset ||
          asset.file !== expectedNames[mode] ||
          !validSize ||
          !/^[0-9a-f]{64}$/.test(String(asset.sha256 || ""))
        ) {
          throw new Error(`The published ${mode} asset for ${profile.key} is inconsistent.`);
        }
      }
    }
  }

  async function loadReleaseIndex() {
    const indexUrl = new URL("../../firmware/latest/release.json", import.meta.url);
    if (indexUrl.origin !== window.location.origin) {
      throw new Error("The firmware index is not hosted with this documentation.");
    }
    const response = await fetch(indexUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(`Firmware index request failed: HTTP ${response.status}`);
    const index = await response.json();
    validatePublishedIndex(index);
    state.releaseIndex = index;
    state.releaseIndex.url = indexUrl;
    populateDevices(index.devices);
    elements.firmwareSource.options[0].textContent = `Published release ${index.tag}`;
    elements.firmwareSource.disabled = false;
  }

  async function sha256Hex(bytes) {
    const digest = await crypto.subtle.digest(
      "SHA-256",
      bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength),
    );
    return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, "0")).join("");
  }

  async function downloadFirmware(device, mode, chipRevision = null) {
    if (selectedFirmwareSource() === "local") {
      const file = elements.firmwareFile.files[0];
      if (!file) throw new Error("Choose a local HomeTiles .bin file first.");
      showActivity("Reading firmware", `Reading ${file.name}â€¦`, { progress: 0 });
      const bytes = new Uint8Array(await file.arrayBuffer());
      const validSize =
        mode === "update"
          ? bytes.length > 0 && bytes.length <= APP_SLOTS[0].size
          : bytes.length === device.flashSize;
      if (!validSize) {
        throw new Error(
          mode === "update"
            ? `Local Update image has ${bytes.length} bytes; the app slot allows at most ${APP_SLOTS[0].size}.`
            : `Local Factory image has ${bytes.length} bytes; ${device.label} requires ${device.flashSize}.`,
        );
      }
      showActivity(
        "Verifying firmware",
        "Checking the local firmware size and embedded device IDâ€¦",
        { progress: 0 },
      );
      validateFirmwareDescriptor(
        bytes,
        device.metadataDeviceKey || device.key,
        mode === "factory" ? 0x10000 : 0,
        {
          siliconVariant: device.siliconVariant || "",
          allowLegacySilicon: device.acceptsLegacyDescriptor === true,
          chipRevision,
        },
      );
      appendLogLine("FIRMWARE", `${file.name} SHA-256 ${(await sha256Hex(bytes)).toLowerCase()}`);
      return bytes;
    }

    const asset = device[mode];
    const url = resolveSameOriginAsset(
      state.releaseIndex.url,
      asset.file,
      window.location.origin,
    );
    showActivity("Downloading firmware", `Downloading ${asset.file}…`, { progress: 0 });
    const response = await fetch(url, { cache: "no-store" });
    if (!response.ok) throw new Error(`Firmware download failed: HTTP ${response.status}`);
    const bytes = new Uint8Array(await response.arrayBuffer());
    if (bytes.length !== asset.size) {
      throw new Error(`Firmware size mismatch: received ${bytes.length}, expected ${asset.size}.`);
    }
    showActivity("Verifying firmware", "Checking the firmware size, SHA-256 digest, and device ID…", {
      progress: 0,
    });
    if ((await sha256Hex(bytes)) !== asset.sha256) {
      throw new Error("Firmware SHA-256 verification failed. Nothing was flashed.");
    }
    validateFirmwareDescriptor(
      bytes,
      device.metadataDeviceKey || device.key,
      mode === "factory" ? 0x10000 : 0,
      {
        siliconVariant: device.siliconVariant || "",
        allowLegacySilicon: device.acceptsLegacyDescriptor === true,
        chipRevision,
      },
    );
    return bytes;
  }

  async function verifyExistingLayout(esploader) {
    showActivity("Checking device", "Checking the existing HomeTiles partition layout…", {
      progress: 0,
    });
    const raw = await esploader.readFlash(PARTITION_TABLE.offset, PARTITION_TABLE.size, () => {});
    const bytes = raw instanceof Uint8Array ? raw : new Uint8Array(raw);
    assertHomeTilesPartitionLayout(parseEspIdfPartitionTable(bytes));
  }

  async function readOtaData(esploader, phase = "Reading OTA state") {
    showActivity(phase, "Reading the current OTA boot selection without changing it.", {
      progress: state.progress,
    });
    const raw = await esploader.readFlash(OTA_DATA_LAYOUT.offset, OTA_DATA_LAYOUT.size, () => {});
    return raw instanceof Uint8Array ? raw : new Uint8Array(raw);
  }

  async function writeFlashPart(esploader, part, reportProgress) {
    await esploader.writeFlash({
      fileArray: [{ data: part.data, address: part.address }],
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: false,
      compress: true,
      reportProgress(_fileIndex, written, total) {
        reportProgress(total > 0 ? written / total : 0);
      },
    });
  }

  async function resetAndDisconnect(esploader, transport, shouldReset, device) {
    let resetSignalSent = false;
    try {
      if (esploader && shouldReset) {
        if (device?.key === GUITION_S3_DEVICE_KEY) {
          // esptool-js 0.6.1 hard_reset only releases RTS. This CH340 board
          // needs an explicit EN pulse with GPIO0 released to boot the app.
          await esploader.after("custom_reset", undefined, GUITION_S3_NORMAL_BOOT_RESET_SEQUENCE);
        } else {
          await esploader.after("hard_reset");
        }
        resetSignalSent = true;
      }
    } catch (error) {
      console.debug("[WebInstaller] Device reset failed", error);
    }
    try {
      if (transport) await transport.disconnect();
    } catch (error) {
      console.debug("[WebInstaller] Serial disconnect failed", error);
    }
    return resetSignalSent;
  }

  async function flashSelectedFirmware() {
    const device = selectedDevice();
    const mode = selectedMode();
    if (!device || state.busy || serialAccess.owner === "installer" || serialAccess.installerPending) return;

    resetLog();
    setLogActionStatus("");
    elements.logPanel.hidden = false;
    state.busy = true;
    state.flashMutationStarted = false;
    state.lastPersistedProgress = -5;
    showActivity("Waiting for USB port", "Choose the USB serial port for the selected display.", {
      progress: 0,
      persist: true,
    });
    updateFormState();

    let transport;
    let esploader;
    let completed = false;
    let flashMutationStarted = false;
    let outcome = null;
    try {
      const port = await serialAccess.requestInstallerPort(navigator.serial);
      transport = new Transport(port);
      esploader = new HomeTilesESPLoader({
        transport,
        baudrate: INSTALLER_BAUD_RATE,
        terminal,
        debugLogging: false,
      });
      showActivity("Connecting", "Connecting and detecting the ESP chip…", { progress: 0 });
      const chipName = await esploader.main();
      await esploader.flashId();
      const detectedFamily = esploader.chip?.CHIP_NAME || chipName;
      if (detectedFamily !== device.chipFamily) {
        throw new Error(
          `Wrong chip family: connected ${detectedFamily}, but ${device.label} requires ${device.chipFamily}.`,
        );
      }

      const chipRevision =
        device.siliconVariant && detectedFamily === "ESP32-P4"
          ? await esploader.chip.getChipRevision(esploader)
          : null;
      assertFirmwareRevisionCompatible(device, chipRevision);
      if (device.siliconVariant) {
        appendLogLine(
          "DEVICE",
          `Detected ESP32-P4 revision ${chipRevision}; it matches the selected ${device.label} profile.`,
        );
      }

      const detectedFlashSize = await esploader.detectFlashSize();
      const expectedFlashSize = `${device.flashSize / (1024 * 1024)}MB`;
      if (detectedFlashSize !== expectedFlashSize) {
        throw new Error(
          `Flash-size mismatch: connected device reports ${detectedFlashSize}; ${device.label} requires ${expectedFlashSize}.`,
        );
      }

      showActivity(
        "Preparing firmware",
        device.siliconVariant
          ? `Loading and verifying the explicitly selected ${device.label} firmware.`
          : "Loading and verifying the selected firmware.",
        { progress: 0 },
      );
      const firmware = await downloadFirmware(device, mode, chipRevision);

      let otaData = null;
      if (mode === "update") {
        await verifyExistingLayout(esploader);
        otaData = await readOtaData(esploader);
      }
      const updatePlan = mode === "update" ? buildSafeOtaUpdatePlan(firmware, otaData) : null;
      const factoryPlan = mode === "factory" ? buildFlashPlan(mode, firmware) : null;
      if (factoryPlan?.eraseFirst) {
        flashMutationStarted = true;
        state.flashMutationStarted = true;
        showActivity("Erasing flash", "Factory reset is erasing the complete flash. Keep the page open.", {
          kind: "warning",
          progress: 0,
        });
        await esploader.eraseFlash();
      }

      flashMutationStarted = true;
      state.flashMutationStarted = true;
      if (updatePlan) {
        const targetLabel = APP_SLOTS[updatePlan.targetSlotIndex].label;
        showActivity(
          `Writing inactive slot ${targetLabel}`,
          "Writing and verifying one inactive app slot. The currently selected slot remains untouched.",
          { progress: 0 },
        );
        await writeFlashPart(esploader, updatePlan.appWrite, (fraction) => {
          const overallPercent = fraction * 96;
          setProgress(overallPercent);
          if (overallPercent >= state.lastPersistedProgress + 5) {
            state.lastPersistedProgress = Math.floor(overallPercent / 5) * 5;
            persistLastRun();
          }
        });

        showActivity(
          "Activating verified update",
          `The verified ${targetLabel} image is complete. Updating the redundant OTA boot selection.`,
          { progress: 97 },
        );
        await writeFlashPart(esploader, updatePlan.bootSelectionWrite, (fraction) => {
          setProgress(97 + fraction * 2);
        });
        const otaReadback = await readOtaData(esploader, "Verifying boot selection");
        assertOtaBootSelectionApplied(otaReadback, updatePlan.bootSelectionWrite);
      } else {
        const factoryPart = factoryPlan.parts[0];
        showActivity(
          "Writing factory image",
          "Writing the complete factory image. Keep this page open.",
          { progress: 0 },
        );
        await writeFlashPart(esploader, factoryPart, (fraction) => {
          const overallPercent = fraction * 100;
          setProgress(overallPercent);
          if (overallPercent >= state.lastPersistedProgress + 5) {
            state.lastPersistedProgress = Math.floor(overallPercent / 5) * 5;
            persistLastRun();
          }
        });
      }
      setProgress(100);
      completed = true;
    } catch (error) {
      const cancelled = error?.name === "NotFoundError";
      const recovery = !flashMutationStarted
        ? " Nothing was flashed."
        : mode === "update"
          ? " The previously selected app slot was preserved. Restart the display to keep using it, or reconnect and retry."
          : " Keep the device powered; reconnect and run Factory reset again before normal use.";
      outcome = {
        outcomeMessage: cancelled
          ? "No serial port was selected. Nothing was flashed."
          : `${error?.message || error}${recovery}`,
        outcomeKind: cancelled ? "info" : "error",
        outcomePhase: flashMutationStarted ? "Recovery required" : "Flash not started",
      };
    } finally {
      // An Update only mutates the inactive slot before a redundant OTA-data
      // entry is committed, so the previously selected slot remains bootable.
      // An interrupted Factory reset must stay in the serial bootloader.
      const safeToReset = completed || !flashMutationStarted || mode === "update";
      await resetAndDisconnect(esploader, transport, safeToReset, device);
      state.busy = false;
      serialAccess.release("installer");
      serialActivity.set("installer", completed ? "Flash complete"
        : outcome?.outcomeKind === "info" ? "Flash cancelled" : "Flash failed",
      completed ? "success" : outcome?.outcomeKind === "info" ? "idle" : "error");
      if (completed) {
        showActivity(
          "Complete",
          mode === "update"
            ? "Update complete. Settings were preserved. Please restart the device manually."
            : "Factory reset complete. Local settings were erased. Please restart the device manually.",
          { kind: "success", progress: 100 },
        );
        persistLastRun({ busy: false, recoveryRequired: false });
      } else {
        showActivity(
          outcome?.outcomePhase ||
            (flashMutationStarted && mode === "factory" ? "Recovery required" : "Flash not completed"),
          outcome?.outcomeMessage || "The installer stopped before completion.",
          { kind: outcome?.outcomeKind || "error", progress: flashMutationStarted ? state.progress : 0 },
        );
        persistLastRun({
          busy: false,
          mutationStarted: flashMutationStarted,
          recoveryRequired: flashMutationStarted && mode === "factory",
        });
      }
      updateFormState();
    }
  }

  populateDevices();
  elements.device.addEventListener("change", () => {
    elements.exactHardware.checked = false;
    elements.factoryConfirmation.checked = false;
    formChanged();
  });
  elements.firmwareSource.addEventListener("change", formChanged);
  elements.firmwareFile.addEventListener("change", formChanged);
  root.querySelectorAll('input[name="installer-mode"]').forEach((input) => {
    input.addEventListener("change", formChanged);
  });
  elements.exactHardware.addEventListener("change", formChanged);
  elements.factoryConfirmation.addEventListener("change", formChanged);
  elements.flash.addEventListener("click", flashSelectedFirmware);
  elements.copyLog.addEventListener("click", copyFlashLog);
  elements.logOutput.addEventListener("scroll", () => {
    const distanceFromBottom =
      elements.logOutput.scrollHeight - elements.logOutput.scrollTop - elements.logOutput.clientHeight;
    logState.followTail = distanceFromBottom <= 8;
  });
  window.addEventListener("beforeunload", (event) => {
    if (!state.busy) return;
    persistLastRun({ busy: true, mutationStarted: state.flashMutationStarted });
    event.preventDefault();
    event.returnValue = "";
  });
  window.addEventListener("pagehide", () => {
    if (state.busy) persistLastRun({ busy: true, mutationStarted: state.flashMutationStarted });
  });

  if (!window.isSecureContext) {
    showActivity("Browser unsupported", "Web Serial requires this page to be opened over HTTPS.", {
      kind: "error",
      progress: 0,
    });
  } else if (!("serial" in navigator)) {
    showActivity("Browser unsupported", "Use a current desktop version of Google Chrome or Microsoft Edge.", {
      kind: "error",
      progress: 0,
    });
  } else {
    showActivity("Loading release", "Loading the latest verified HomeTiles release…", { progress: 0 });
    loadReleaseIndex()
      .then(() => {
        if (!restoreLastRun()) {
          showActivity("Ready", "Choose a device and confirm its model.", {
            kind: "info",
            progress: 0,
          });
        }
        updateFormState();
      })
      .catch((error) => {
        showActivity("Installer unavailable", `${error.message} Use the manual flashing guide for now.`, {
          kind: "error",
          progress: 0,
        });
      });
  }
  updateFormState();
  serialAccess.subscribe(updateFormState);
  return {
    pageChanged(visible) {
      // A flash completed offscreen stays available until its result is viewed.
      if (state.visible && !visible) resetCompletedRun();
      state.visible = visible;
      if (visible) scheduleLogRender();
    },
  };
}

retainPageComponent("[data-hometiles-installer]", mountInstaller);

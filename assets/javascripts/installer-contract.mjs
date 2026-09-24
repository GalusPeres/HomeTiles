import { DEVICE_PROFILES } from './device-profiles.mjs';
export { DEVICE_PROFILES };

export const INSTALLER_SCHEMA_VERSION = 2;
export const INSTALLER_BAUD_RATE = 460800;

export const ESP_ROM_CHIP_FAMILIES = Object.freeze({
  9: "ESP32-S3",
  18: "ESP32-P4",
});

export function parseEspRomChipIdentity(rawSecurityInfo) {
  const bytes =
    rawSecurityInfo instanceof Uint8Array
      ? rawSecurityInfo
      : new Uint8Array(rawSecurityInfo);
  if (bytes.byteLength < 20) {
    throw new Error("The ESP ROM returned incomplete security information.");
  }

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const chipId = view.getUint32(12, true);
  const chipFamily = ESP_ROM_CHIP_FAMILIES[chipId];
  if (!chipFamily) {
    throw new Error(`Unsupported ESP ROM chip ID ${chipId}.`);
  }
  return { chipId, chipFamily };
}

export const PARTITION_TABLE = Object.freeze({
  offset: 0x8000,
  size: 0x1000,
});

export const REQUIRED_PARTITIONS = Object.freeze([
  Object.freeze({ label: "nvs", type: 0x01, subtype: 0x02, offset: 0x9000, size: 0x5000 }),
  Object.freeze({ label: "otadata", type: 0x01, subtype: 0x00, offset: 0xe000, size: 0x2000 }),
  Object.freeze({ label: "app0", type: 0x00, subtype: 0x10, offset: 0x10000, size: 0x680000 }),
  Object.freeze({ label: "app1", type: 0x00, subtype: 0x11, offset: 0x690000, size: 0x680000 }),
  Object.freeze({ label: "spiffs", type: 0x01, subtype: 0x82, offset: 0xd10000, size: 0x2e0000 }),
  Object.freeze({ label: "coredump", type: 0x01, subtype: 0x03, offset: 0xff0000, size: 0x10000 }),
]);

export const APP_SLOTS = Object.freeze(
  REQUIRED_PARTITIONS.filter((partition) => partition.type === 0x00).map(
    ({ label, offset, size }) => Object.freeze({ label, offset, size }),
  ),
);

export const ESP_OTA_IMAGE_STATE = Object.freeze({
  NEW: 0x00000000,
  PENDING_VERIFY: 0x00000001,
  VALID: 0x00000002,
  INVALID: 0x00000003,
  ABORTED: 0x00000004,
  UNDEFINED: 0xffffffff,
});

export const OTA_DATA_LAYOUT = Object.freeze({
  offset: REQUIRED_PARTITIONS.find((partition) => partition.label === "otadata").offset,
  size: REQUIRED_PARTITIONS.find((partition) => partition.label === "otadata").size,
  sectorSize: 0x1000,
  entrySize: 32,
  entryOffsets: Object.freeze([0x0000, 0x1000]),
});

const DEVICE_KEYS = new Set(DEVICE_PROFILES.map((device) => device.key));
const RELEASE_TAG_PATTERN = /^v[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$/;
const SHA256_PATTERN = /^sha256:([0-9a-f]{64})$/i;
const ESP_IMAGE_HEADER_MAGIC = 0xe9;
const ESP_APP_DESCRIPTOR_MAGIC = 0xabcd5432;
const DEVICE_DESCRIPTOR_MAGIC = 0x44565034;
const SILICON_REVISION_DESCRIPTOR_MAGIC = 0x53525634;
const DEVICE_DESCRIPTOR_IMAGE_OFFSET = 24 + 8 + 256;
const DEVICE_KEY_OFFSET = DEVICE_DESCRIPTOR_IMAGE_OFFSET + 4 + 32;
const DEVICE_KEY_MAX_LENGTH = 32;
const DEVICE_DESCRIPTOR_LENGTH = 4 + 32 + 32 + 32;
const SILICON_REVISION_DESCRIPTOR_IMAGE_OFFSET =
  DEVICE_DESCRIPTOR_IMAGE_OFFSET + DEVICE_DESCRIPTOR_LENGTH;
const SILICON_VARIANT_MAX_LENGTH = 16;
const UINT32_MAX = 0xffffffff;

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function readNullTerminatedAscii(bytes, offset, maxLength) {
  const endLimit = Math.min(bytes.length, offset + maxLength);
  let end = offset;
  while (end < endLimit && bytes[end] !== 0) end += 1;
  return new TextDecoder("utf-8", { fatal: true }).decode(bytes.subarray(offset, end));
}

function readU32LE(bytes, offset) {
  assert(offset >= 0 && offset + 4 <= bytes.length, "Binary field is outside the image.");
  return new DataView(bytes.buffer, bytes.byteOffset + offset, 4).getUint32(0, true);
}

function assertUint32(value, label) {
  assert(
    Number.isInteger(value) && value >= 0 && value <= UINT32_MAX,
    `${label} is not an unsigned 32-bit integer.`,
  );
}

function writeU32LE(bytes, offset, value) {
  assertUint32(value, "Binary field");
  assert(offset >= 0 && offset + 4 <= bytes.length, "Binary field is outside the image.");
  new DataView(bytes.buffer, bytes.byteOffset + offset, 4).setUint32(0, value, true);
}

function crc32LittleEndian(input, initialValue) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  let crc = (initialValue ^ UINT32_MAX) >>> 0;
  for (const byte of bytes) {
    crc = (crc ^ byte) >>> 0;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = ((crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0)) >>> 0;
    }
  }
  return (crc ^ UINT32_MAX) >>> 0;
}

// ESP-IDF v5.5.2 calls esp_rom_crc32_le(UINT32_MAX, &ota_seq, 4).
export function espIdfOtaSelectCrc(otaSequence) {
  assertUint32(otaSequence, "OTA sequence");
  const sequenceBytes = new Uint8Array(4);
  writeU32LE(sequenceBytes, 0, otaSequence);
  return crc32LittleEndian(sequenceBytes, UINT32_MAX);
}

function otaSlotForSequence(otaSequence) {
  return (((otaSequence - 1) >>> 0) % APP_SLOTS.length) >>> 0;
}

function parseOtaEntry(bytes, entryIndex) {
  const entryOffset = OTA_DATA_LAYOUT.entryOffsets[entryIndex];
  const entryBytes = bytes.subarray(entryOffset, entryOffset + OTA_DATA_LAYOUT.entrySize);
  const otaSequence = readU32LE(entryBytes, 0);
  const otaState = readU32LE(entryBytes, 24);
  const crc = readU32LE(entryBytes, 28);
  const expectedCrc = espIdfOtaSelectCrc(otaSequence);
  const invalidState =
    otaState === ESP_OTA_IMAGE_STATE.INVALID || otaState === ESP_OTA_IMAGE_STATE.ABORTED;
  const valid = otaSequence !== UINT32_MAX && !invalidState && crc === expectedCrc;
  return Object.freeze({
    index: entryIndex,
    partitionOffset: entryOffset,
    flashAddress: OTA_DATA_LAYOUT.offset + entryOffset,
    otaSequence,
    otaState,
    crc,
    expectedCrc,
    crcValid: crc === expectedCrc,
    erased: entryBytes.every((byte) => byte === 0xff),
    valid,
    slotIndex: valid ? otaSlotForSequence(otaSequence) : null,
    sequenceLabelHex: Array.from(entryBytes.subarray(4, 24), (byte) =>
      byte.toString(16).padStart(2, "0"),
    ).join(""),
  });
}

export function parseEspIdfOtaData(input) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  assert(bytes.length >= OTA_DATA_LAYOUT.size, "OTA data read is too short.");
  const entries = Object.freeze(OTA_DATA_LAYOUT.entryOffsets.map((_, index) => parseOtaEntry(bytes, index)));

  let activeEntryIndex = null;
  if (entries[0].valid && entries[1].valid) {
    // This intentionally mirrors bootloader_common_select_otadata(): ties select entry zero.
    activeEntryIndex = entries[0].otaSequence >= entries[1].otaSequence ? 0 : 1;
  } else if (entries[0].valid) {
    activeEntryIndex = 0;
  } else if (entries[1].valid) {
    activeEntryIndex = 1;
  }

  // HomeTiles has no factory app partition. ESP-IDF therefore starts with ota_0
  // when both OTA data entries are erased or invalid.
  const selectedSlotIndex =
    activeEntryIndex === null ? 0 : otaSlotForSequence(entries[activeEntryIndex].otaSequence);
  return Object.freeze({
    entries,
    activeEntryIndex,
    selectedSlotIndex,
    selectionSource: activeEntryIndex === null ? "default-ota-0" : "otadata",
  });
}

function nextOtaSequence(activeSequence, targetSlotIndex) {
  if (activeSequence === null) return targetSlotIndex + 1;

  const base = (targetSlotIndex + 1) % APP_SLOTS.length;
  const increments = activeSequence > base
    ? Math.ceil((activeSequence - base) / APP_SLOTS.length)
    : 0;
  const nextSequence = base + increments * APP_SLOTS.length;
  assert(
    nextSequence <= UINT32_MAX - 1,
    "OTA sequence space is exhausted; a normal boot-selection update is not safe.",
  );
  return nextSequence;
}

export function buildOtaBootSelectionUpdate(input, targetSlotIndex) {
  assert(
    Number.isInteger(targetSlotIndex) && targetSlotIndex >= 0 && targetSlotIndex < APP_SLOTS.length,
    "Target OTA slot is invalid.",
  );
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  const parsed = parseEspIdfOtaData(bytes);
  const activeEntry = parsed.activeEntryIndex === null ? null : parsed.entries[parsed.activeEntryIndex];
  const writeEntryIndex = parsed.activeEntryIndex === null ? 0 : 1 - parsed.activeEntryIndex;
  const entryOffset = OTA_DATA_LAYOUT.entryOffsets[writeEntryIndex];
  const entryBytes = new Uint8Array(
    bytes.slice(entryOffset, entryOffset + OTA_DATA_LAYOUT.entrySize),
  );
  const otaSequence = nextOtaSequence(activeEntry?.otaSequence ?? null, targetSlotIndex);

  writeU32LE(entryBytes, 0, otaSequence);
  // Arduino-ESP32 3.3.7 enables bootloader rollback for every HomeTiles target.
  // ESP-IDF's esp_ota_set_boot_partition() therefore marks a new selection NEW.
  writeU32LE(entryBytes, 24, ESP_OTA_IMAGE_STATE.NEW);
  writeU32LE(entryBytes, 28, espIdfOtaSelectCrc(otaSequence));

  return Object.freeze({
    targetSlotIndex,
    previousActiveEntryIndex: parsed.activeEntryIndex,
    writeEntryIndex,
    otaSequence,
    otaState: ESP_OTA_IMAGE_STATE.NEW,
    address: OTA_DATA_LAYOUT.offset + entryOffset,
    sectorSize: OTA_DATA_LAYOUT.sectorSize,
    data: entryBytes,
  });
}

export function buildSafeOtaUpdatePlan(firmwareBytes, otaDataBytes) {
  const data = firmwareBytes instanceof Uint8Array ? firmwareBytes : new Uint8Array(firmwareBytes);
  assert(data.length > 0, "Update image is empty.");
  assert(data.length <= APP_SLOTS[0].size, "Update image exceeds a HomeTiles app slot.");
  const selection = parseEspIdfOtaData(otaDataBytes);
  const targetSlotIndex = 1 - selection.selectedSlotIndex;
  const targetSlot = APP_SLOTS[targetSlotIndex];
  const bootSelectionWrite = buildOtaBootSelectionUpdate(otaDataBytes, targetSlotIndex);

  // The caller must finish and verify appWrite before performing bootSelectionWrite.
  // esptool-js v0.6.1's main() guarantees that its stub is running; a separate
  // writeFlash call for the 32-byte entry erases only this aligned 4KB sector.
  return Object.freeze({
    eraseFirst: false,
    preservedSlotIndex: selection.selectedSlotIndex,
    targetSlotIndex,
    selectionSource: selection.selectionSource,
    appWrite: Object.freeze({
      kind: "app",
      address: targetSlot.offset,
      data,
    }),
    bootSelectionWrite: Object.freeze({
      kind: "otadata",
      ...bootSelectionWrite,
    }),
  });
}

export function assertOtaBootSelectionApplied(input, expectedUpdate) {
  assert(expectedUpdate && typeof expectedUpdate === "object", "Expected OTA update is missing.");
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  const parsed = parseEspIdfOtaData(bytes);
  assert(
    parsed.activeEntryIndex === expectedUpdate.writeEntryIndex,
    "The written OTA data entry did not become active.",
  );
  assert(
    parsed.selectedSlotIndex === expectedUpdate.targetSlotIndex,
    "The written OTA data entry selects the wrong app slot.",
  );
  const entryOffset = OTA_DATA_LAYOUT.entryOffsets[expectedUpdate.writeEntryIndex];
  const actual = bytes.subarray(entryOffset, entryOffset + OTA_DATA_LAYOUT.entrySize);
  assert(
    actual.length === expectedUpdate.data.length &&
      actual.every((byte, index) => byte === expectedUpdate.data[index]),
    "The written OTA data entry failed read-back verification.",
  );
  return true;
}

export function assertFirmwareRevisionCompatible(device, chipRevision) {
  if (device?.chipFamily !== "ESP32-P4") return true;
  assert(
    device.siliconVariant &&
      Number.isInteger(device.minimumRevision) &&
      Number.isInteger(device.maximumRevision),
    `The ${device.label || "selected ESP32-P4"} profile has no safe silicon revision contract.`,
  );
  assert(
    Number.isInteger(chipRevision) && chipRevision >= 0 && chipRevision <= 0xffff,
    "The ESP32-P4 silicon revision could not be detected safely.",
  );
  assert(
    chipRevision >= device.minimumRevision && chipRevision <= device.maximumRevision,
    `ESP32-P4 revision ${chipRevision} does not match the selected ${device.label} image.`,
  );
  return true;
}

export function releaseAssetNames(tag, deviceKey) {
  assert(RELEASE_TAG_PATTERN.test(tag), `Invalid release tag: ${tag}`);
  assert(DEVICE_KEYS.has(deviceKey), `Unknown release device key: ${deviceKey}`);
  const stem = `hometiles_${tag}_${deviceKey}`;
  return Object.freeze({
    update: `${stem}.bin`,
    factory: `${stem}_factory.bin`,
  });
}

export function buildReleaseIndex(release, { allowMissingProfiles = false } = {}) {
  assert(release && typeof release === "object", "GitHub release metadata is missing.");
  assert(!release.draft, "The latest release is still a draft.");
  assert(!release.prerelease, "The browser installer does not publish prerelease firmware.");
  const tag = String(release.tag_name || "");
  assert(RELEASE_TAG_PATTERN.test(tag), `Invalid release tag: ${tag || "(missing)"}`);

  const releaseUrl = new URL(String(release.html_url || ""));
  assert(
    releaseUrl.origin === "https://github.com" &&
      releaseUrl.pathname === `/GalusPeres/HomeTiles/releases/tag/${tag}`,
    "Unexpected GitHub release URL.",
  );

  const assets = new Map((release.assets || []).map((asset) => [asset.name, asset]));
  const devices = DEVICE_PROFILES.flatMap((profile) => {
    const names = releaseAssetNames(tag, profile.key);
    const updateAsset = assets.get(names.update);
    const factoryAsset = assets.get(names.factory);
    if (allowMissingProfiles && !updateAsset && !factoryAsset) return [];
    assert(updateAsset, `Release ${tag} is missing ${names.update}.`);
    assert(factoryAsset, `Release ${tag} is missing ${names.factory}.`);

    const normalizeAsset = (asset, mode) => {
      const size = Number(asset.size);
      assert(Number.isSafeInteger(size) && size > 0, `${asset.name} has an invalid size.`);
      const digestMatch = String(asset.digest || "").match(SHA256_PATTERN);
      assert(digestMatch, `${asset.name} has no valid SHA-256 digest.`);
      const downloadUrl = new URL(String(asset.browser_download_url || ""));
      assert(
        downloadUrl.origin === "https://github.com" &&
          downloadUrl.pathname ===
            `/GalusPeres/HomeTiles/releases/download/${tag}/${asset.name}`,
        `${asset.name} has an unexpected download URL.`,
      );
      if (mode === "update") {
        assert(size <= APP_SLOTS[0].size, `${asset.name} exceeds a HomeTiles OTA slot.`);
      } else {
        assert(
          size === profile.flashSize,
          `${asset.name} is ${size} bytes; expected the complete ${profile.flashSize}-byte factory image.`,
        );
      }
      return Object.freeze({
        file: asset.name,
        size,
        sha256: digestMatch[1].toLowerCase(),
        sourceUrl: downloadUrl.toString(),
      });
    };

    return [
      Object.freeze({
        ...profile,
        update: normalizeAsset(updateAsset, "update"),
        factory: normalizeAsset(factoryAsset, "factory"),
      }),
    ];
  });
  assert(devices.length > 0, `Release ${tag} has no complete HomeTiles firmware pairs.`);

  return Object.freeze({
    schemaVersion: INSTALLER_SCHEMA_VERSION,
    tag,
    releaseUrl: releaseUrl.toString(),
    generatedAt: new Date().toISOString(),
    devices,
  });
}

export function parseEspIdfPartitionTable(input) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  assert(bytes.length >= 32, "Partition table read is too short.");
  const entries = [];
  for (let offset = 0; offset + 32 <= bytes.length; offset += 32) {
    const magic = bytes[offset] | (bytes[offset + 1] << 8);
    if (magic === 0xffff) break;
    if (magic !== 0x50aa) {
      if (entries.length) break;
      throw new Error("No ESP-IDF partition table was found at 0x8000.");
    }
    entries.push({
      type: bytes[offset + 2],
      subtype: bytes[offset + 3],
      offset: readU32LE(bytes, offset + 4),
      size: readU32LE(bytes, offset + 8),
      label: readNullTerminatedAscii(bytes, offset + 12, 16),
    });
  }
  assert(entries.length > 0, "The ESP-IDF partition table is empty.");
  return entries;
}

export function assertHomeTilesPartitionLayout(entries) {
  for (const expected of REQUIRED_PARTITIONS) {
    const actual = entries.find((entry) => entry.label === expected.label);
    assert(actual, `Partition ${expected.label} is missing.`);
    for (const field of ["type", "subtype", "offset", "size"]) {
      assert(
        actual[field] === expected[field],
        `Partition ${expected.label} has an unexpected ${field}.`,
      );
    }
  }
  return true;
}

export function validateFirmwareDescriptor(
  input,
  deviceKey,
  appOffset = 0,
  { siliconVariant = "", allowLegacySilicon = false, chipRevision = null } = {},
) {
  assert(DEVICE_KEYS.has(deviceKey), `Unknown installer device key: ${deviceKey}`);
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  const descriptorBase = appOffset + DEVICE_DESCRIPTOR_IMAGE_OFFSET;
  assert(
    descriptorBase + 4 + 32 + DEVICE_KEY_MAX_LENGTH <= bytes.length,
    "Firmware image is too short for its device descriptor.",
  );
  assert(bytes[appOffset] === ESP_IMAGE_HEADER_MAGIC, "Firmware has no ESP image header.");
  assert(
    readU32LE(bytes, appOffset + 24 + 8) === ESP_APP_DESCRIPTOR_MAGIC,
    "Firmware has no ESP application descriptor.",
  );
  assert(
    readU32LE(bytes, descriptorBase) === DEVICE_DESCRIPTOR_MAGIC,
    "Firmware has no HomeTiles device descriptor.",
  );
  const embeddedDeviceKey = readNullTerminatedAscii(
    bytes,
    appOffset + DEVICE_KEY_OFFSET,
    DEVICE_KEY_MAX_LENGTH,
  );
  assert(
    embeddedDeviceKey === deviceKey,
    `Firmware is for ${embeddedDeviceKey || "an unknown device"}, not ${deviceKey}.`,
  );

  if (siliconVariant) {
    const expectedVariant = DEVICE_PROFILES.find(
      (device) =>
        (device.metadataDeviceKey || device.key) === deviceKey &&
        device.siliconVariant === siliconVariant,
    );
    assert(expectedVariant, `Unknown silicon variant ${siliconVariant} for ${deviceKey}.`);
    const siliconBase = appOffset + SILICON_REVISION_DESCRIPTOR_IMAGE_OFFSET;
    const siliconBytes = 4 + 2 + 2 + SILICON_VARIANT_MAX_LENGTH;
    const hasSiliconDescriptor =
      siliconBase + siliconBytes <= bytes.length &&
      readU32LE(bytes, siliconBase) === SILICON_REVISION_DESCRIPTOR_MAGIC;
    if (!hasSiliconDescriptor) {
      assert(
        allowLegacySilicon && expectedVariant.acceptsLegacyDescriptor,
        `Firmware has no silicon metadata for ${expectedVariant.label}.`,
      );
      if (chipRevision !== null) {
        assert(
          Number.isInteger(chipRevision) &&
            chipRevision >= expectedVariant.minimumRevision &&
            chipRevision <= expectedVariant.maximumRevision,
          `Legacy firmware is unsafe for connected ESP32-P4 revision ${chipRevision}.`,
        );
      }
      return embeddedDeviceKey;
    }

    const minimumRevision = new DataView(
      bytes.buffer,
      bytes.byteOffset + siliconBase + 4,
      4,
    ).getUint16(0, true);
    const maximumRevision = new DataView(
      bytes.buffer,
      bytes.byteOffset + siliconBase + 6,
      2,
    ).getUint16(0, true);
    const embeddedVariant = readNullTerminatedAscii(
      bytes,
      siliconBase + 8,
      SILICON_VARIANT_MAX_LENGTH,
    );
    assert(
      embeddedVariant === expectedVariant.siliconVariant,
      `Firmware silicon variant is ${embeddedVariant || "unknown"}, not ${expectedVariant.siliconVariant}.`,
    );
    assert(
      minimumRevision >= expectedVariant.minimumRevision &&
        maximumRevision <= expectedVariant.maximumRevision &&
        minimumRevision <= maximumRevision,
      `Firmware silicon range ${minimumRevision}-${maximumRevision} is unsafe for ${expectedVariant.label}.`,
    );
    if (chipRevision !== null) {
      assert(
        Number.isInteger(chipRevision) &&
          chipRevision >= minimumRevision &&
          chipRevision <= maximumRevision,
        `Firmware silicon range ${minimumRevision}-${maximumRevision} does not support connected ESP32-P4 revision ${chipRevision}.`,
      );
    }
  }
  return embeddedDeviceKey;
}

export function buildFlashPlan(mode, firmwareBytes) {
  assert(mode === "update" || mode === "factory", `Unknown flash mode: ${mode}`);
  const data = firmwareBytes instanceof Uint8Array ? firmwareBytes : new Uint8Array(firmwareBytes);
  assert(
    mode === "factory",
    "Update requires current OTA data and buildSafeOtaUpdatePlan().",
  );
  return Object.freeze({
    eraseFirst: true,
    parts: Object.freeze([Object.freeze({ address: 0, data })]),
  });
}

export function resolveSameOriginAsset(indexUrl, fileName, pageOrigin) {
  assert(/^[A-Za-z0-9._-]+\.bin$/.test(fileName), "Unsafe firmware asset name.");
  const resolved = new URL(fileName, indexUrl);
  assert(resolved.origin === pageOrigin, "Firmware assets must be served from the documentation origin.");
  return resolved;
}

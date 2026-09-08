import { releaseProfiles } from '../../device-catalog.js';
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  APP_SLOTS,
  DEVICE_PROFILES,
  PARTITION_TABLE,
  REQUIRED_PARTITIONS,
  assertFirmwareRevisionCompatible,
  assertHomeTilesPartitionLayout,
  buildFlashPlan,
  buildReleaseIndex,
  parseEspIdfPartitionTable,
  parseEspRomChipIdentity,
  releaseAssetNames,
  resolveSameOriginAsset,
  validateFirmwareDescriptor,
} from "../../../docs/assets/javascripts/installer-contract.mjs";
import { selectDevicesForPublication } from "../../../release-helper/prepare-web-installer.mjs";

const repositoryRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const read = (relativePath) => fs.readFileSync(path.join(repositoryRoot, relativePath), "utf8");

function fixtureRelease(tag = "v0.6.5") {
  const assets = [];
  for (const device of DEVICE_PROFILES) {
    const names = releaseAssetNames(tag, device.key);
    for (const [mode, name] of Object.entries(names)) {
      assets.push({
        name,
        size: mode === "factory" ? device.flashSize : APP_SLOTS[0].size - 4096,
        digest: `sha256:${"a".repeat(64)}`,
        browser_download_url:
          `https://github.com/GalusPeres/HomeTiles/releases/download/${tag}/${name}`,
      });
    }
  }
  return {
    tag_name: tag,
    html_url: `https://github.com/GalusPeres/HomeTiles/releases/tag/${tag}`,
    draft: false,
    prerelease: false,
    assets,
  };
}

function writePartitionEntry(bytes, index, partition) {
  const offset = index * 32;
  const view = new DataView(bytes.buffer);
  view.setUint16(offset, 0x50aa, true);
  bytes[offset + 2] = partition.type;
  bytes[offset + 3] = partition.subtype;
  view.setUint32(offset + 4, partition.offset, true);
  view.setUint32(offset + 8, partition.size, true);
  bytes.fill(0, offset + 12, offset + 28);
  bytes.set(new TextEncoder().encode(partition.label), offset + 12);
}

function partitionTableFixture(partitions = REQUIRED_PARTITIONS) {
  const bytes = new Uint8Array(PARTITION_TABLE.size).fill(0xff);
  partitions.forEach((partition, index) => writePartitionEntry(bytes, index, partition));
  return bytes;
}

function firmwareFixture(deviceKey, appOffset = 0, silicon = null) {
  const bytes = new Uint8Array(appOffset + 512);
  const view = new DataView(bytes.buffer);
  bytes[appOffset] = 0xe9;
  view.setUint32(appOffset + 24 + 8, 0xabcd5432, true);
  view.setUint32(appOffset + 24 + 8 + 256, 0x44565034, true);
  bytes.set(new TextEncoder().encode(deviceKey), appOffset + 24 + 8 + 256 + 4 + 32);
  if (silicon) {
    const siliconOffset = appOffset + 24 + 8 + 256 + 4 + 32 + 32 + 32;
    view.setUint32(siliconOffset, 0x53525634, true);
    view.setUint16(siliconOffset + 4, silicon.minimumRevision, true);
    view.setUint16(siliconOffset + 6, silicon.maximumRevision, true);
    bytes.set(new TextEncoder().encode(silicon.key), siliconOffset + 8);
  }
  return bytes;
}

assert.equal(DEVICE_PROFILES.length, 15, "Every explicit firmware choice needs an installer profile.");
assert.equal(new Set(DEVICE_PROFILES.map((device) => device.key)).size, 15);
assert.equal(DEVICE_PROFILES.filter((device) => device.chipFamily === "ESP32-S3").length, 3);
assert.equal(DEVICE_PROFILES.filter((device) => device.chipFamily === "ESP32-P4").length, 12);
for (const device of DEVICE_PROFILES.filter((candidate) => candidate.chipFamily === "ESP32-P4")) {
  const isRev3 = device.key === "waveshare_touch_lcd_7b_rev3_1";
  assert.equal(device.siliconVariant, isRev3 ? "rev3_1" : "pre_v3");
  assert.equal(device.minimumRevision, isRev3 ? 301 : 1);
  assert.equal(device.maximumRevision, isRev3 ? 301 : 199);
  assert.equal(device.acceptsLegacyDescriptor, !isRev3);
}
for (const device of DEVICE_PROFILES.filter((candidate) => candidate.chipFamily === "ESP32-S3")) {
  assert.equal(device.siliconVariant, undefined);
}
assert.equal(
  DEVICE_PROFILES.find((device) => device.key === "guition_esp32_4848s040").chipFamily,
  "ESP32-S3",
);
for (const [key, chipFamily, flashSize, labelPattern] of [
  ["waveshare_touch_lcd_4_3", "ESP32-P4", 32 * 1024 * 1024, /4\.3 inch/],
  ["waveshare_touch_lcd_7b", "ESP32-P4", 32 * 1024 * 1024, /before v3\.0/],
  ["waveshare_touch_lcd_7b_rev3_1", "ESP32-P4", 32 * 1024 * 1024, /v3\.1 only, experimental/],
  ["guition_jc1060p470c_v2", "ESP32-P4", 16 * 1024 * 1024, /V2 \(New Panel\)/],
  ["waveshare_s3_touch_lcd_4b", "ESP32-S3", 16 * 1024 * 1024, /ESP32-S3 Touch LCD 4B/],
]) {
  const device = DEVICE_PROFILES.find((candidate) => candidate.key === key);
  assert.ok(device, `Missing installer profile ${key}.`);
  assert.equal(device.chipFamily, chipFamily);
  assert.equal(device.flashSize, flashSize);
  assert.equal(device.status, "validation-pending");
  assert.match(device.label, labelPattern);
}

const waveshareS3Lcd4 = DEVICE_PROFILES.find(
  (device) => device.key === "waveshare_s3_touch_lcd_4",
);
assert.equal(waveshareS3Lcd4.chipFamily, "ESP32-S3");
assert.equal(waveshareS3Lcd4.flashSize, 16 * 1024 * 1024);
assert.equal(waveshareS3Lcd4.status, "supported");
assert.match(waveshareS3Lcd4.label, /Rev 4\.0/);
assert.match(waveshareS3Lcd4.hardwareCheck, /revision 4\.0.*CH32V003/);

function securityInfoFixture(chipId) {
  const bytes = new Uint8Array(20);
  new DataView(bytes.buffer).setUint32(12, chipId, true);
  return bytes;
}

assert.deepEqual(parseEspRomChipIdentity(securityInfoFixture(18)), {
  chipId: 18,
  chipFamily: "ESP32-P4",
});
assert.deepEqual(parseEspRomChipIdentity(securityInfoFixture(9)), {
  chipId: 9,
  chipFamily: "ESP32-S3",
});
assert.throws(() => parseEspRomChipIdentity(securityInfoFixture(0x46b6b8de)), /Unsupported ESP ROM chip ID/);

const sketchProfiles = read("sketch.yaml");
const releaseTargets = DEVICE_PROFILES;
assert.equal(releaseTargets.length, 15, "The two explicit 7B choices need separate release builds.");
for (const target of releaseTargets) {
  const escapedProfile = target.buildProfile.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const match = sketchProfiles.match(new RegExp(`^  ${escapedProfile}:\\r?\\n    fqbn: ([^\\r\\n]+)$`, "m"));
  assert.ok(match, `Missing sketch profile ${target.buildProfile}.`);
  const expectedBoard = target.chipFamily === "ESP32-S3" ? "esp32s3" : "esp32p4|m5stack_tab5";
  assert.match(match[1], new RegExp(`esp32:esp32:(?:${expectedBoard}):`));
  assert.match(match[1], new RegExp(`FlashSize=${target.flashSize / (1024 * 1024)}M(?:,|$)`));
}
assert.match(sketchProfiles, /^  waveshare_7b:[\s\S]*?ChipVariant=prev3$/m);
assert.match(sketchProfiles, /^  waveshare_7b_rev3_1:[\s\S]*?ChipVariant=postv3$/m);

const firmwareWorkflowKeys = [...read(".github/workflows/firmware.yml").matchAll(/^\s+key:\s+([a-z0-9_]+)\s*$/gm)]
  .map((match) => match[1]);
const releaseWorkflow = read(".github/workflows/firmware.yml");
assert.deepEqual(
  releaseTargets.map((target) => target.key).sort(),
  [...firmwareWorkflowKeys].sort(),
  "Installer device keys must match the release build matrix.",
);
for (const target of releaseTargets.filter((candidate) => candidate.chipFamily === "ESP32-P4")) {
  const profileStart = releaseWorkflow.indexOf(`profile: ${target.buildProfile}`);
  const profileEnd = releaseWorkflow.indexOf("publish: true", profileStart);
  assert.ok(profileStart >= 0 && profileEnd > profileStart, `Missing workflow entry ${target.buildProfile}.`);
  const workflowEntry = releaseWorkflow.slice(profileStart, profileEnd);
  assert.match(
    workflowEntry,
    new RegExp(`silicon_variant: ${target.siliconVariant}`),
    `${target.buildProfile} must package its exact silicon generation.`,
  );
}

const packageSource = read("release-helper/package-ci-build.js");
const packagedDeviceKeys = releaseProfiles.map((profile) => profile.key);
assert.deepEqual(
  releaseTargets.map((target) => target.key).sort(),
  packagedDeviceKeys.sort(),
  "Installer device keys must match packaged release assets.",
);
assert.match(packageSource, /const otaSlotSize = 0x680000;/);

const releaseIndex = buildReleaseIndex(fixtureRelease());
assert.equal(releaseIndex.tag, "v0.6.5");
assert.equal(releaseIndex.devices.length, 15);
for (const device of releaseIndex.devices) {
  const names = releaseAssetNames("v0.6.5", device.key);
  assert.equal(device.update.file, names.update);
  assert.equal(device.factory.file, names.factory);
  assert.equal(device.factory.size, device.flashSize);
  assert.ok(device.update.size <= APP_SLOTS[0].size);
}
const waveshare7bPre = releaseIndex.devices.find(
  (device) => device.key === "waveshare_touch_lcd_7b",
);
const waveshare7bRev3 = releaseIndex.devices.find(
  (device) => device.key === "waveshare_touch_lcd_7b_rev3_1",
);
assert.equal(waveshare7bPre.siliconVariant, "pre_v3");
assert.equal(waveshare7bRev3.siliconVariant, "rev3_1");
assert.equal(waveshare7bPre.metadataDeviceKey, "waveshare_touch_lcd_7b");
assert.equal(waveshare7bRev3.metadataDeviceKey, "waveshare_touch_lcd_7b");
assert.equal("revisionVariants" in waveshare7bPre, false);
assert.equal("revisionVariants" in waveshare7bRev3, false);
assert.equal(assertFirmwareRevisionCompatible(waveshare7bPre, 103), true);
assert.equal(assertFirmwareRevisionCompatible(waveshare7bRev3, 301), true);
const tab5Profile = releaseIndex.devices.find((device) => device.key === "m5stacks_tab5");
assert.equal(assertFirmwareRevisionCompatible(tab5Profile, 103), true);
assert.throws(
  () => assertFirmwareRevisionCompatible(tab5Profile, 301),
  /does not match the selected/,
  "A pre-v3-only P4 profile must reject later silicon before downloading firmware.",
);
assert.throws(
  () => assertFirmwareRevisionCompatible(waveshare7bPre, 301),
  /does not match the selected/,
);
assert.throws(
  () => assertFirmwareRevisionCompatible(waveshare7bRev3, 103),
  /does not match the selected/,
);
for (const unsupportedRevision of [0, 200, 300, 302, 399, 400]) {
  assert.throws(
    () => assertFirmwareRevisionCompatible(waveshare7bRev3, unsupportedRevision),
    /does not match the selected/,
  );
}

const fullPublication = selectDevicesForPublication(releaseIndex);
assert.equal(fullPublication.partial, false);
assert.equal(fullPublication.devices.length, 15);
const localPublication = selectDevicesForPublication(releaseIndex, "guition_esp32_4848s040");
assert.equal(localPublication.partial, true);
assert.deepEqual(localPublication.devices.map((device) => device.key), ["guition_esp32_4848s040"]);
assert.throws(
  () => selectDevicesForPublication(releaseIndex, "unknown_device"),
  /Unknown installer device key/,
);

const missingAssetRelease = fixtureRelease();
missingAssetRelease.assets.pop();
assert.throws(() => buildReleaseIndex(missingAssetRelease), /is missing/);
assert.throws(
  () => buildReleaseIndex(missingAssetRelease, { allowMissingProfiles: true }),
  /is missing/,
  "A release with only one file from an update/factory pair must remain invalid.",
);
const legacy7bRelease = fixtureRelease("v0.6.7");
legacy7bRelease.assets = legacy7bRelease.assets.filter(
  (asset) => !asset.name.includes("_waveshare_touch_lcd_7b_rev3_1"),
);
assert.throws(() => buildReleaseIndex(legacy7bRelease), /rev3_1/);
const legacy7bIndex = buildReleaseIndex(legacy7bRelease, { allowMissingProfiles: true });
assert.equal(legacy7bIndex.devices.length, 14);
assert.equal(
  legacy7bIndex.devices.some((device) => device.key === "waveshare_touch_lcd_7b"),
  true,
  "A legacy pre-v3 pair remains an explicit pre-v3 choice.",
);
assert.equal(
  legacy7bIndex.devices.some((device) => device.key === "waveshare_touch_lcd_7b_rev3_1"),
  false,
  "A release without the rev3.1 pair must not expose the experimental rev3.1 choice.",
);
const previousRelease = fixtureRelease("v0.6.7");
const pendingDeviceKeys = new Set([
  "waveshare_touch_lcd_4_3",
  "waveshare_touch_lcd_7b",
  "waveshare_touch_lcd_7b_rev3_1",
  "guition_jc1060p470c_v2",
  "waveshare_s3_touch_lcd_4",
  "waveshare_s3_touch_lcd_4b",
]);
previousRelease.assets = previousRelease.assets.filter(
  (asset) =>
    ![...pendingDeviceKeys].some((key) =>
      Object.values(releaseAssetNames("v0.6.7", key)).includes(asset.name),
    ),
);
assert.throws(
  () => buildReleaseIndex(previousRelease),
  /is missing/,
  "The normal release contract must still require every configured profile.",
);
const previousReleaseIndex = buildReleaseIndex(previousRelease, { allowMissingProfiles: true });
assert.equal(previousReleaseIndex.devices.length, 9);
assert.deepEqual(
  previousReleaseIndex.devices.map((device) => device.key).filter((key) => pendingDeviceKeys.has(key)),
  [],
);
const previousReleasePublication = selectDevicesForPublication(previousReleaseIndex);
assert.equal(previousReleasePublication.partial, true);
assert.equal(previousReleasePublication.devices.length, 9);
assert.throws(
  () => selectDevicesForPublication(previousReleaseIndex, "waveshare_touch_lcd_7b"),
  /has no complete firmware pair/,
  "An explicit local device request must fail when that release has no matching pair.",
);
const beforeLcd4Release = fixtureRelease("v0.6.9");
const lcd4AssetNames = Object.values(releaseAssetNames("v0.6.9", waveshareS3Lcd4.key));
beforeLcd4Release.assets = beforeLcd4Release.assets.filter(
  (asset) => !lcd4AssetNames.includes(asset.name),
);
const beforeLcd4Publication = selectDevicesForPublication(
  buildReleaseIndex(beforeLcd4Release, { allowMissingProfiles: true }),
);
assert.equal(beforeLcd4Publication.devices.length, 14);
assert.equal(beforeLcd4Publication.partial, true);
assert.equal(beforeLcd4Publication.devices.some((device) => device.key === waveshareS3Lcd4.key), false);
assert.throws(
  () => selectDevicesForPublication(beforeLcd4Publication, waveshareS3Lcd4.key),
  /has no complete firmware pair/,
  'The current release must keep working without advertising an unreleased LCD-4 image.',
);

const wrongFactorySizeRelease = fixtureRelease();
wrongFactorySizeRelease.assets.find((asset) => asset.name.endsWith("_factory.bin")).size -= 1;
assert.throws(() => buildReleaseIndex(wrongFactorySizeRelease), /complete .* factory image/);

const parsedPartitions = parseEspIdfPartitionTable(partitionTableFixture());
assert.equal(assertHomeTilesPartitionLayout(parsedPartitions), true);
const obsoleteLayout = REQUIRED_PARTITIONS.map((partition) =>
  partition.label === "app1" ? { ...partition, offset: 0x800000 } : partition,
);
assert.throws(
  () => assertHomeTilesPartitionLayout(parseEspIdfPartitionTable(partitionTableFixture(obsoleteLayout))),
  /app1 has an unexpected offset/,
);

const csvPartitions = read("partitions.csv")
  .split(/\r?\n/)
  .filter((line) => line.trim() && !line.trim().startsWith("#"))
  .map((line) => {
    const [label, typeName, subtypeName, offset, size] = line.split(",").map((field) => field.trim());
    const type = typeName === "app" ? 0x00 : 0x01;
    const subtypeMap = { nvs: 0x02, ota: 0x00, ota_0: 0x10, ota_1: 0x11, spiffs: 0x82, coredump: 0x03 };
    return { label, type, subtype: subtypeMap[subtypeName], offset: Number(offset), size: Number(size) };
  });
assert.equal(assertHomeTilesPartitionLayout(csvPartitions), true);

const updateFirmware = firmwareFixture("m5stacks_tab5");
assert.equal(validateFirmwareDescriptor(updateFirmware, "m5stacks_tab5"), "m5stacks_tab5");
assert.throws(() => validateFirmwareDescriptor(updateFirmware, "waveshare_4b"), /Firmware is for/);
const tab5PreFirmware = firmwareFixture("m5stacks_tab5", 0, {
  key: "pre_v3",
  minimumRevision: 1,
  maximumRevision: 199,
});
assert.equal(
  validateFirmwareDescriptor(tab5PreFirmware, "m5stacks_tab5", 0, {
    siliconVariant: "pre_v3",
    chipRevision: 103,
  }),
  "m5stacks_tab5",
);
const unsafeTab5Firmware = firmwareFixture("m5stacks_tab5", 0, {
  key: "default",
  minimumRevision: 0,
  maximumRevision: 0xffff,
});
assert.throws(
  () => validateFirmwareDescriptor(unsafeTab5Firmware, "m5stacks_tab5", 0, {
    siliconVariant: "pre_v3",
    chipRevision: 103,
  }),
  /silicon variant is default/,
  "A P4 release image must not publish the former unbounded default descriptor.",
);
const broadTab5Firmware = firmwareFixture("m5stacks_tab5", 0, {
  key: "pre_v3",
  minimumRevision: 1,
  maximumRevision: 399,
});
assert.throws(
  () => validateFirmwareDescriptor(broadTab5Firmware, "m5stacks_tab5", 0, {
    siliconVariant: "pre_v3",
    chipRevision: 103,
  }),
  /silicon range 1-399 is unsafe/,
  "A matching variant name must not bypass the exact silicon range contract.",
);
const factoryFirmware = firmwareFixture("guition_esp32_4848s040", 0x10000);
assert.equal(
  validateFirmwareDescriptor(factoryFirmware, "guition_esp32_4848s040", 0x10000),
  "guition_esp32_4848s040",
);
const preV3Firmware = firmwareFixture("waveshare_touch_lcd_7b", 0, {
  key: "pre_v3",
  minimumRevision: 1,
  maximumRevision: 199,
});
const rev3Firmware = firmwareFixture("waveshare_touch_lcd_7b", 0, {
  key: "rev3_1",
  minimumRevision: 301,
  maximumRevision: 301,
});
assert.equal(
  validateFirmwareDescriptor(preV3Firmware, "waveshare_touch_lcd_7b", 0, {
    siliconVariant: "pre_v3",
  }),
  "waveshare_touch_lcd_7b",
);
assert.equal(
  validateFirmwareDescriptor(rev3Firmware, "waveshare_touch_lcd_7b", 0, {
    siliconVariant: "rev3_1",
    chipRevision: 301,
  }),
  "waveshare_touch_lcd_7b",
);
const narrowRev3Firmware = firmwareFixture("waveshare_touch_lcd_7b", 0, {
  key: "rev3_1",
  minimumRevision: 302,
  maximumRevision: 302,
});
assert.throws(
  () => validateFirmwareDescriptor(narrowRev3Firmware, "waveshare_touch_lcd_7b", 0, {
    siliconVariant: "rev3_1",
    chipRevision: 301,
  }),
  /silicon range 302-302 is unsafe/,
);
assert.throws(
  () => validateFirmwareDescriptor(preV3Firmware, "waveshare_touch_lcd_7b", 0, {
    siliconVariant: "rev3_1",
  }),
  /silicon variant is pre_v3/,
);
const legacy7bFirmware = firmwareFixture("waveshare_touch_lcd_7b");
assert.equal(
  validateFirmwareDescriptor(legacy7bFirmware, "waveshare_touch_lcd_7b", 0, {
    siliconVariant: "pre_v3",
    allowLegacySilicon: true,
    chipRevision: 103,
  }),
  "waveshare_touch_lcd_7b",
);
assert.throws(
  () => validateFirmwareDescriptor(legacy7bFirmware, "waveshare_touch_lcd_7b", 0, {
    siliconVariant: "pre_v3",
    allowLegacySilicon: true,
    chipRevision: 302,
  }),
  /Legacy firmware is unsafe for connected ESP32-P4 revision 302/,
);
assert.throws(
  () => validateFirmwareDescriptor(legacy7bFirmware, "waveshare_touch_lcd_7b", 0, {
    siliconVariant: "rev3_1",
    allowLegacySilicon: true,
  }),
  /has no silicon metadata/,
);

assert.throws(
  () => buildFlashPlan("update", updateFirmware),
  /requires current OTA data and buildSafeOtaUpdatePlan/,
);
const factoryPlan = buildFlashPlan("factory", factoryFirmware);
assert.equal(factoryPlan.eraseFirst, true);
assert.deepEqual(factoryPlan.parts.map((part) => part.address), [0]);

assert.equal(
  resolveSameOriginAsset(
    new URL("https://galusperes.github.io/HomeTiles/firmware/latest/release.json"),
    "hometiles_v0.6.5_m5stacks_tab5.bin",
    "https://galusperes.github.io",
  ).origin,
  "https://galusperes.github.io",
);
assert.throws(
  () => resolveSameOriginAsset(
    new URL("https://github.com/GalusPeres/HomeTiles/releases/download/v0.6.5/release.json"),
    "hometiles_v0.6.5_m5stacks_tab5.bin",
    "https://galusperes.github.io",
  ),
  /documentation origin/,
);

const docsWorkflow = read(".github/workflows/docs.yml");
assert.match(docsWorkflow, /push:\s*\n\s+branches:\s*\[main\]/);
assert.match(docsWorkflow, /workflow_dispatch:/);
assert.doesNotMatch(
  docsWorkflow,
  /release:\s*\n\s+types:\s*\[published\]/,
  "A release created with GITHUB_TOKEN does not reliably trigger another workflow.",
);
assert.match(docsWorkflow, /node tools\/tests\/build\/test-web-installer\.mjs/);
assert.match(docsWorkflow, /mkdocs build --strict/);
assert.match(
  docsWorkflow,
  /prepare-web-installer\.mjs --output site\/firmware\/latest/,
  "Release assets must be copied into the deployed same-origin site.",
);
assert.match(docsWorkflow, /ghp-import .*--no-history.* site/);
assert.doesNotMatch(docsWorkflow, /mkdocs gh-deploy/);

const firmwareWorkflow = read(".github/workflows/firmware.yml");
const tagReleaseJob = firmwareWorkflow.split("\n  release:\n")[1];
assert.ok(tagReleaseJob, "The normal tag release job must remain present.");
assert.match(firmwareWorkflow, /node tools\/run-tests\.mjs/);
assert.match(
  firmwareWorkflow,
  /release:[\s\S]*?permissions:\s*\n\s+actions:\s*write\s*\n\s+contents:\s*write/,
  "The release job needs Actions write permission to dispatch the docs workflow.",
);
const releaseUploadIndex = tagReleaseJob.indexOf("gh release upload");
const docsDispatchIndex = tagReleaseJob.indexOf("gh workflow run docs.yml");
assert.ok(releaseUploadIndex >= 0, "The release workflow must upload firmware assets.");
assert.ok(
  docsDispatchIndex > releaseUploadIndex,
  "Docs must be dispatched only after all release assets were uploaded.",
);
assert.match(
  tagReleaseJob.slice(docsDispatchIndex),
  /--repo "\$GITHUB_REPOSITORY"[\s\\]*\n\s+--ref "\$GITHUB_REF_NAME"/,
  "The docs workflow must run for the just-published release tag.",
);

const installerSource = read("docs/assets/javascripts/installer.mjs");
const installerPageSource = read("docs/installer.md");
const installerPageVersion = read("docs/assets/javascripts/docs-session.mjs").match(/installer\.mjs\?v=([a-z0-9-]+)/)?.[1];
const installerContractVersion = installerSource.match(/installer-contract\.mjs\?v=([a-z0-9-]+)/)?.[1];
assert.ok(installerPageVersion, "The installer module needs an explicit browser-cache version.");
assert.equal(
  installerContractVersion,
  installerPageVersion,
  "The installer and its contract module must use the same browser-cache version.",
);
assert.match(installerSource, /esptool-js@0\.6\.1\/bundle\.js/);
assert.doesNotMatch(installerSource, /esptool-js@0\.6\.0\/bundle\.js/);
assert.match(installerSource, /class HomeTilesESPLoader extends ESPLoader/);
assert.match(installerSource, /await this\.connect\(mode, 7, false\)/);
assert.match(installerSource, /parseEspRomChipIdentity\(securityInfo\)/);
assert.match(installerSource, /new HomeTilesESPLoader\(/);
assert.doesNotMatch(installerSource, /github\.com\/GalusPeres\/HomeTiles\/releases\/download/);
assert.match(installerSource, /resolveSameOriginAsset/);
assert.match(installerSource, /verifyExistingLayout/);
assert.match(installerSource, /eraseFlash\(\)/);
assert.match(installerSource, /populateDevices\(index\.devices\)/);
assert.match(installerSource, /function selectedFirmwareSource\(\)/);
assert.match(installerSource, /elements\.firmwareSource\.options\[0\]\.textContent = `Published release \$\{index\.tag\}`/);
assert.match(installerSource, /elements\.firmwareFile\.files\[0\]/);
assert.match(installerSource, /Local Update image has \$\{bytes\.length\} bytes/);
assert.match(installerSource, /Local Factory image has \$\{bytes\.length\} bytes/);
assert.match(installerSource, /device\.metadataDeviceKey \|\| device\.key/);
assert.match(installerSource, /assertFirmwareRevisionCompatible\(device, chipRevision\)/);
assert.doesNotMatch(installerSource, /resolveFirmwareAssets|revisionVariants/);
assert.match(installerSource, /await esploader\.chip\.getChipRevision\(esploader\)/);
assert.match(installerSource, /elements\.exactHardware\.checked = false/);
assert.match(
  installerSource,
  /const safeToReset = completed \|\| !flashMutationStarted \|\| mode === "update";/,
  "An interrupted inactive-slot Update may reboot the preserved selected slot.",
);
assert.match(installerSource, /const GUITION_S3_DEVICE_KEY = "guition_esp32_4848s040"/);
assert.match(
  installerSource,
  /const GUITION_S3_NORMAL_BOOT_RESET_SEQUENCE = "D0\|R1\|W100\|R0\|W100\|D0"/,
);
assert.match(installerSource, /device\?\.key === GUITION_S3_DEVICE_KEY/);
assert.match(
  installerSource,
  /esploader\.after\("custom_reset", undefined, GUITION_S3_NORMAL_BOOT_RESET_SEQUENCE\)/,
);
assert.match(installerSource, /resetAndDisconnect\(esploader, transport, safeToReset, device\)/);
assert.doesNotMatch(installerSource, /display is restarting/);
assert.match(installerSource, /Update complete\. Settings were preserved\. Please restart the device manually\./);
assert.match(installerSource, /Factory reset complete\. Local settings were erased\. Please restart the device manually\./);
assert.doesNotMatch(installerSource, /reset signal was sent|Power-cycle once if/);
assert.match(installerSource, /LAST_RUN_STORAGE_KEY = "hometiles\.webInstaller\.lastRun\.v1"/);
assert.match(installerSource, /function restoreLastRun\(\)/);
assert.match(installerSource, /"Recovery required"/);
assert.match(installerSource, /The previously selected app slot was preserved/);
assert.match(installerSource, /window\.addEventListener\("beforeunload"/);
assert.match(installerSource, /event\.preventDefault\(\)/);
assert.match(installerSource, /window\.addEventListener\("pagehide"/);
assert.match(installerSource, /elements\.progressText\.textContent = `\$\{Math\.round\(state\.progress\)\}%`/);
assert.match(installerSource, /buildSafeOtaUpdatePlan\(firmware, otaData\)/);
assert.match(installerSource, /`Writing inactive slot \$\{targetLabel\}`/);
assert.match(installerSource, /baudrate: INSTALLER_BAUD_RATE/);
assert.match(installerSource, /const LOG_MAX_LINES = 300/);
assert.match(installerSource, /const LOG_MAX_CHARACTERS = 64 \* 1024/);
assert.match(installerSource, /\[\$\{source\}\]/);
assert.match(installerSource, /terminalPartial/);
assert.match(installerSource, /navigator\.clipboard\?\.writeText/);
assert.doesNotMatch(installerSource, /URL\.createObjectURL|downloadFlashLog|installer-download-log/);
assert.match(installerSource, /logState\.followTail = distanceFromBottom <= 8/);
const flashSelectedSource = installerSource.slice(installerSource.indexOf("async function flashSelectedFirmware"));
assert.ok(
  flashSelectedSource.indexOf("await esploader.chip.getChipRevision(esploader)") <
    flashSelectedSource.indexOf("await downloadFirmware(device, mode, chipRevision)"),
  "The browser must reject a wrong explicit 7B choice before downloading its image.",
);
assert.ok(
  flashSelectedSource.indexOf("await downloadFirmware(device, mode, chipRevision)") <
    flashSelectedSource.indexOf("await esploader.eraseFlash()"),
  "Firmware must download and verify before any flash erase or write.",
);

const installerDocs = read("docs/installer.md");
assert.match(installerDocs, /class="ht-installer-form"/);
assert.match(installerDocs, /id="installer-firmware-source"/);
assert.match(installerDocs, /Local HomeTiles \.bin file/);
assert.match(installerDocs, /id="installer-firmware-file"[^>]*type="file"/);
assert.match(installerDocs, /<label id="installer-device-label" class="ht-installer-section-heading" for="installer-device">2\. Device<\/label>/);
for (const label of ["1. Firmware", "3. Flash mode", "4. Confirm", "5. Connect and flash"]) {
  assert.match(installerDocs, new RegExp(label.replace(".", "\\.")));
}
assert.match(installerDocs, /<strong>Update<\/strong>/);
assert.match(installerDocs, /<strong>First install \/ factory reset<\/strong>/);
assert.match(installerDocs, /id="installer-progress-panel" class="ht-installer-progress" hidden/);
assert.match(installerDocs, /id="installer-phase"/);
assert.match(installerDocs, /id="installer-progress-text">0%/);
assert.match(installerDocs, /id="installer-log-panel" class="ht-installer-log-panel" hidden/);
assert.match(installerDocs, /aria-label="Copy flash log to clipboard"/);
assert.doesNotMatch(installerDocs, /Download log|Show flash log|installer-download-log|installer-log-details/);
assert.match(installerDocs, /role="log" aria-label="Installer flash log"/);
assert.doesNotMatch(installerDocs, /writes both application slots|Writing both app slots|cover both slots/);
assert.doesNotMatch(installerDocs, /ht-installer-(?:grid|step|step-number|heading|kicker)/);
assert.doesNotMatch(installerDocs, /<strong>Update\s+—/);
assert.doesNotMatch(installerDocs, /<strong>Factory\s*\//);
assert.doesNotMatch(installerDocs, /Lokaler|Veröffentlichung|Gerät auswählen|gültigen Werte/);
assert.match(installerDocs, /Select the exact model printed on the device or rear label/);
assert.doesNotMatch(installerDocs, /Choose the right mode|\| Mode \| Use it for \| Local data \|/);
for (const heading of ["Browser installer", "Manual flashing", "Troubleshooting"]) {
  assert.ok(installerDocs.includes(`## ${heading}`), `Missing installer section ${heading}.`);
}
assert.match(installerDocs, /Device list\]\(index\.md#device-support\)/);
assert.match(installerDocs, /write-flash --erase-all 0x0/);
assert.match(installerDocs, /chip-id/);
assert.match(installerDocs, /v3\.2 and newer are unsupported/);
assert.doesNotMatch(installerDocs, /Local test before publication|Update safety and partition details/);
assert.doesNotMatch(installerDocs, /manual flashing guide\]\(flashing\.md\)/);

const overviewDocs = read("docs/index.md");
assert.match(overviewDocs, /\[Flash the firmware\]\(installer\.md\)/);
assert.match(overviewDocs, /\[Open online flasher\]\(installer\.md#browser-installer\)/);
assert.match(overviewDocs, /### ESP32-P4/);
assert.match(overviewDocs, /### ESP32-S3/);
assert.match(read("docs/home-assistant-setup.md"), /\(installer\.md(?:#browser-installer)?\)/);
assert.doesNotMatch(read("docs/home-assistant-setup.md"), /\(flashing\.md(?:#[^)]*)?\)/);

const updatingDocs = read("docs/updating.md");
assert.match(updatingDocs, /inactive application slot/);
assert.match(updatingDocs, /previously selected application bootable/);

const installerStyles = read("docs/stylesheets/extra.css");
const installerStyleStart = installerStyles.indexOf("Browser firmware installer");
assert.ok(installerStyleStart >= 0);
const installerStylesOnly = installerStyles.slice(installerStyleStart);
assert.match(installerStylesOnly, /\.ht-installer\s*\{[\s\S]*?width:\s*100%/);
assert.match(installerStylesOnly, /grid-template-columns:\s*7\.5rem minmax\(0, 1fr\)/);
assert.match(installerStylesOnly, /\.ht-installer-section \+ \.ht-installer-section\s*\{[\s\S]*?border-top:\s*1px solid/);
assert.match(installerStylesOnly, /\.ht-installer select\s*\{[\s\S]*?width:\s*15rem/);
assert.match(installerStylesOnly, /\.ht-installer select\s*\{[\s\S]*?appearance:\s*none/);
assert.match(installerStylesOnly, /background-position:\s*right 0\.38rem center/);
assert.match(installerStylesOnly, /\.ht-installer-section-info\s*\{[\s\S]*?color:\s*var\(--md-default-fg-color--light\)/);
assert.match(installerStylesOnly, /\.ht-installer-action button\s*\{[\s\S]*?white-space:\s*nowrap/);
assert.doesNotMatch(installerStylesOnly, /radial-gradient|ht-installer-grid|ht-installer-step-number/);
assert.match(installerStylesOnly, /\.ht-installer-log-panel pre\s*\{[\s\S]*?max-height:\s*14rem/);

const docsNavigation = read("mkdocs.yml");
assert.equal(
  [...docsNavigation.matchAll(/^\s+- Flash firmware:\s*installer\.md\s*$/gm)].length,
  1,
  "The sidebar must contain one clear flashing entry.",
);
assert.doesNotMatch(docsNavigation, /Manual flashing:|Supported devices:|Browser Firmware Installer:/);
assert.match(docsNavigation, /- Release Notes: releases\/index\.md/);
assert.doesNotMatch(docsNavigation, /navigation\.expand/);
for (const match of docsNavigation.matchAll(/^\s+- (?:[^:\r\n]+):\s+([^\s#]+\.md)\s*$/gm)) {
  assert.ok(fs.existsSync(path.join(repositoryRoot, "docs", match[1])), `Missing nav target ${match[1]}.`);
}

const screensaverDocs = read("docs/screensaver.md");
assert.match(screensaverDocs, /baseline \(non-progressive\) JPEG/);
assert.match(screensaverDocs, /RGB\/sRGB/);
assert.match(screensaverDocs.replace(/\*\*/g, ""), /longest edge at 1920 pixels/);
assert.match(screensaverDocs, /index\.md#device-support/);

console.log("Browser installer contract tests passed.");

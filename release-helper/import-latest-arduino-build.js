const fs = require('fs');
const os = require('os');
const path = require('path');

const repoRoot = path.resolve(__dirname, '..');
const projectName = 'HomeTiles.ino';
const releaseDir = path.join(repoRoot, 'build', 'releases');
const deviceSelectPath = path.join(repoRoot, 'src', 'devices', 'device_select.h');
const versionFilePath = path.join(repoRoot, 'version.txt');
const factoryAppOffset = 0x10000;

const { profiles, releaseProfiles, releaseFileVersion } = require('../tools/device-catalog.js');
const { parseFirmwareMetadata } = require('./firmware-metadata.js');

const releaseTargets = new Map();
for (const profile of releaseProfiles) {
  if (releaseTargets.has(profile.define)) continue;
  const variants = releaseProfiles.filter((candidate) => candidate.define === profile.define);
  releaseTargets.set(profile.define, {
    key: profile.metadataDeviceKey,
    slug: profile.legacySlug,
    ...(variants.length > 1 ? {
      siliconVariants: new Map(variants.map((variant) => [variant.siliconVariant, {
        key: variant.key, slug: variant.legacySlug,
        minimumRevision: variant.minimumRevision, maximumRevision: variant.maximumRevision,
      }])),
    } : { expectedSiliconVariant: profile.siliconVariant }),
  });
}
const nonReleaseTargets = new Set(profiles.filter((profile) => !profile.publish).map((profile) => profile.define));

function readVersion() {
  const text = fs.readFileSync(versionFilePath, 'utf8');
  const defineMatch = text.match(/^\s*#define\s+FW_VERSION\s+"([^"]+)"/m);
  if (defineMatch) {
    return defineMatch[1].trim();
  }

  const firstLine = text
    .split(/\r?\n/)
    .map((line) => line.trim())
    .find((line) => line.length > 0);

  if (!firstLine) {
    throw new Error('version.txt is empty');
  }

  return firstLine;
}

function readManualDeviceSelection(source) {
  const lines = source.split(/\r?\n/);
  const selectedDefines = [];
  let inManualBlock = false;
  let conditionalDepth = 0;

  for (const line of lines) {
    if (!inManualBlock) {
      if (/^\s*#if\s+!defined\(HOMETILES_CI_TARGET\)\s*$/.test(line)) {
        inManualBlock = true;
        conditionalDepth = 1;
      }
      continue;
    }

    if (/^\s*#if(?:def|ndef)?\b/.test(line)) {
      conditionalDepth += 1;
      continue;
    }
    if (/^\s*#endif\b/.test(line)) {
      conditionalDepth -= 1;
      if (conditionalDepth === 0) break;
      continue;
    }
    if (conditionalDepth !== 1) continue;

    const defineMatch = line.match(/^\s*#define\s+(DEVICE_[A-Z0-9_]+)\b/);
    if (defineMatch) selectedDefines.push(defineMatch[1]);
  }

  if (!inManualBlock) {
    throw new Error('Manual device selection block is missing from src/devices/device_select.h');
  }
  if (selectedDefines.length === 0) {
    throw new Error(
      'No explicit release device is enabled in src/devices/device_select.h; refusing to assume Waveshare 4B.'
    );
  }
  if (selectedDefines.length > 1) {
    throw new Error(
      `Multiple device targets are enabled in src/devices/device_select.h: ${selectedDefines.join(', ')}`
    );
  }

  const selectedDefine = selectedDefines[0];
  if (nonReleaseTargets.has(selectedDefine)) {
    throw new Error(`${selectedDefine} is a layout-test target and cannot be packaged as release firmware.`);
  }
  const target = releaseTargets.get(selectedDefine);
  if (!target) {
    throw new Error(`Unsupported release device target in src/devices/device_select.h: ${selectedDefine}`);
  }

  return { define: selectedDefine, ...target };
}

function readDeviceInfo() {
  return readManualDeviceSelection(fs.readFileSync(deviceSelectPath, 'utf8'));
}

function resolveReleaseDevice(selection, metadata) {
  if (metadata.deviceKey !== selection.key) {
    throw new Error(
      `Latest Arduino build is for ${metadata.deviceKey || '(unknown)'}, but ${selection.define} is selected.`
    );
  }

  if (!selection.siliconVariants) {
    if (!metadata.silicon) {
      throw new Error(
        `${selection.define} firmware has no silicon-revision metadata; rebuild it before packaging.`
      );
    }
    if (metadata.silicon.variant !== selection.expectedSiliconVariant) {
      throw new Error(
        `${selection.define} firmware silicon variant mismatch: expected ${selection.expectedSiliconVariant}, got ${metadata.silicon.variant || '(empty)'}.`
      );
    }
    if (
      selection.expectedSiliconVariant === 'pre_v3' &&
      (metadata.silicon.minimumRevision !== 1 || metadata.silicon.maximumRevision !== 199)
    ) {
      throw new Error(
        `Unsafe ${selection.define} pre-v3 revision range: ${metadata.silicon.minimumRevision}-${metadata.silicon.maximumRevision}`
      );
    }
    return { key: selection.key, slug: selection.slug };
  }
  if (!metadata.silicon) {
    throw new Error(
      `${selection.define} firmware has no silicon-revision metadata; rebuild it before packaging.`
    );
  }

  const variantTarget = selection.siliconVariants.get(metadata.silicon.variant);
  if (!variantTarget) {
    throw new Error(
      `Unsupported ${selection.define} silicon variant: ${metadata.silicon.variant || '(empty)'}`
    );
  }
  if (
    metadata.silicon.variant === 'pre_v3' &&
    (metadata.silicon.minimumRevision !== 1 || metadata.silicon.maximumRevision !== 199)
  ) {
    throw new Error(
      `Unsafe Waveshare 7B pre-v3 revision range: ${metadata.silicon.minimumRevision}-${metadata.silicon.maximumRevision}`
    );
  }
  if (
    metadata.silicon.variant === 'rev3_1' &&
    (metadata.silicon.minimumRevision !== 301 || metadata.silicon.maximumRevision !== 301)
  ) {
    throw new Error(
      `Unsafe Waveshare 7B v3.1 revision range: ${metadata.silicon.minimumRevision}-${metadata.silicon.maximumRevision}`
    );
  }
  // Every catalog variant carries its exact HomeTiles revision contract.
  if (
    metadata.silicon.minimumRevision !== variantTarget.minimumRevision ||
    metadata.silicon.maximumRevision !== variantTarget.maximumRevision
  ) {
    throw new Error(
      `Unsafe ${selection.define} ${metadata.silicon.variant} revision range: ${metadata.silicon.minimumRevision}-${metadata.silicon.maximumRevision}`
    );
  }
  return { key: variantTarget.key, slug: variantTarget.slug };
}

function getArduinoSketchesPath() {
  if (process.env.ARDUINO_SKETCHES_PATH) {
    return process.env.ARDUINO_SKETCHES_PATH;
  }

  if (process.env.LOCALAPPDATA) {
    return path.join(process.env.LOCALAPPDATA, 'arduino', 'sketches');
  }

  return path.join(os.homedir(), 'AppData', 'Local', 'arduino', 'sketches');
}

function findLatestSuccessfulBuild(sketchesPath) {
  if (!fs.existsSync(sketchesPath)) {
    throw new Error(`Arduino sketches path not found: ${sketchesPath}`);
  }

  const dirs = fs
    .readdirSync(sketchesPath, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => {
      const fullPath = path.join(sketchesPath, entry.name);
      const stat = fs.statSync(fullPath);
      return { fullPath, mtimeMs: stat.mtimeMs };
    })
    .sort((a, b) => b.mtimeMs - a.mtimeMs);

  for (const dir of dirs) {
    const updatePath = path.join(dir.fullPath, `${projectName}.bin`);
    const factoryPath = path.join(dir.fullPath, `${projectName}.merged.bin`);
    if (fs.existsSync(updatePath) && fs.existsSync(factoryPath)) {
      return { buildPath: dir.fullPath, updatePath, factoryPath };
    }
  }

  throw new Error(`No successful Arduino build with ${projectName}.bin and ${projectName}.merged.bin was found.`);
}

function main() {
  const version = releaseFileVersion(readVersion());
  const selection = readDeviceInfo();
  const sketchesPath = getArduinoSketchesPath();
  const build = findLatestSuccessfulBuild(sketchesPath);
  const updateMetadata = parseFirmwareMetadata(fs.readFileSync(build.updatePath));
  const factoryMetadata = parseFirmwareMetadata(
    fs.readFileSync(build.factoryPath),
    factoryAppOffset
  );
  const device = resolveReleaseDevice(selection, updateMetadata);
  const factoryDevice = resolveReleaseDevice(selection, factoryMetadata);
  if (factoryDevice.key !== device.key) {
    throw new Error(
      `Arduino update/factory build mismatch: ${device.key} vs ${factoryDevice.key}.`
    );
  }

  fs.mkdirSync(releaseDir, { recursive: true });

  const deviceFilePatterns = [
    new RegExp(`^hometiles_.*_${device.key}(_factory)?\\.bin$`),
    new RegExp(`^esp32-p4-homeassistant-display-.*-${device.slug}-(update|factory)\\.bin$`),
  ];
  for (const entry of fs.readdirSync(releaseDir, { withFileTypes: true })) {
    if (!entry.isFile()) continue;
    if (!deviceFilePatterns.some((pattern) => pattern.test(entry.name))) continue;
    fs.unlinkSync(path.join(releaseDir, entry.name));
  }

  const updateDest = path.join(releaseDir, `hometiles_${version}_${device.key}.bin`);
  const factoryDest = path.join(releaseDir, `hometiles_${version}_${device.key}_factory.bin`);

  fs.copyFileSync(build.updatePath, updateDest);
  fs.copyFileSync(build.factoryPath, factoryDest);

  console.log(`[release-helper] Build: ${build.buildPath}`);
  console.log(`[release-helper] ${path.basename(updateDest)}`);
  console.log(`[release-helper] ${path.basename(factoryDest)}`);
}

if (require.main === module) main();

module.exports = {
  parseFirmwareMetadata,
  readManualDeviceSelection,
  resolveReleaseDevice,
};

const fs = require('fs');
const path = require('path');

const repoRoot = path.resolve(__dirname, '..');
const projectName = 'HomeTiles.ino';
const versionFilePath = path.join(repoRoot, 'version.txt');
const otaSlotSize = 0x680000;

const { releaseProfiles, releaseFileVersion } = require('../tools/device-catalog.js');
const { parseFirmwareMetadata, assertReleaseMetadata } = require('./firmware-metadata.js');
const { iconPxForDefine, findMklittlefs, addIconFileToFactory } = require('./factory-icon-file.js');
const devices = new Map(releaseProfiles.map((profile) => [profile.key, profile]));

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

function readArgs(argv) {
  const args = {};
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (!arg.startsWith('--')) {
      throw new Error(`Unexpected argument: ${arg}`);
    }
    const key = arg.slice(2);
    const value = argv[i + 1];
    if (!value || value.startsWith('--')) {
      throw new Error(`Missing value for --${key}`);
    }
    args[key] = value;
    i += 1;
  }
  return args;
}

function walkFiles(root) {
  const files = [];
  const entries = fs.readdirSync(root, { withFileTypes: true });

  for (const entry of entries) {
    const fullPath = path.join(root, entry.name);
    if (entry.isDirectory()) {
      files.push(...walkFiles(fullPath));
    } else if (entry.isFile()) {
      files.push(fullPath);
    }
  }

  return files;
}

function selectSingleFile(files, predicate, label) {
  const matches = files.filter(predicate).sort();
  if (matches.length !== 1) {
    const shown = matches.length ? `\n${matches.join('\n')}` : '';
    throw new Error(`Expected exactly one ${label} binary, found ${matches.length}.${shown}`);
  }
  return matches[0];
}

function ensureCleanDeviceOutputs(outDir, deviceKey) {
  fs.mkdirSync(outDir, { recursive: true });
  const pattern = new RegExp(`^hometiles_.*_${deviceKey}(_factory)?\\.bin$`);

  for (const entry of fs.readdirSync(outDir, { withFileTypes: true })) {
    if (!entry.isFile()) continue;
    if (!pattern.test(entry.name)) continue;
    fs.unlinkSync(path.join(outDir, entry.name));
  }
}

function main() {
  const args = readArgs(process.argv);
  const buildDirArg = args['build-dir'] || process.env.BUILD_DIR;
  const outDir = path.resolve(repoRoot, args['out-dir'] || process.env.OUT_DIR || 'build/releases');
  const deviceKey = args['device-key'] || process.env.DEVICE_KEY;
  const metadataDeviceKey = args['metadata-device-key'] || deviceKey;
  const siliconVariant = args['silicon-variant'] || process.env.SILICON_VARIANT || 'default';
  // The icon files (tools/generate-icon-files.mjs --files-only): the factory
  // image gets the device's one in its LittleFS partition.
  const iconDir = args['icon-dir'] || process.env.ICON_DIR;

  if (!buildDirArg) {
    throw new Error('Missing --build-dir');
  }
  if (!deviceKey || !devices.has(deviceKey)) {
    throw new Error(`Unknown or missing device key: ${deviceKey || '(none)'}`);
  }
  const device = devices.get(deviceKey);
  const expectedMetadataDeviceKey = device.metadataDeviceKey || device.key;
  if (metadataDeviceKey !== expectedMetadataDeviceKey) {
    throw new Error(
      `Release metadata device mismatch for ${deviceKey}: expected ${expectedMetadataDeviceKey}, got ${metadataDeviceKey}.`
    );
  }
  if (siliconVariant !== device.siliconVariant) {
    throw new Error(
      `Release silicon variant mismatch for ${deviceKey}: expected ${device.siliconVariant}, got ${siliconVariant}.`
    );
  }

  const buildDir = path.resolve(repoRoot, buildDirArg);
  if (!fs.existsSync(buildDir)) {
    throw new Error(`Build directory not found: ${buildDir}`);
  }

  const version = releaseFileVersion(readVersion());
  const files = walkFiles(buildDir);

  const updatePath = selectSingleFile(
    files,
    (file) => path.basename(file) === `${projectName}.bin`,
    `${deviceKey} OTA`
  );
  const factoryPath = selectSingleFile(
    files,
    (file) => path.basename(file) === `${projectName}.merged.bin`,
    `${deviceKey} factory`
  );

  assertReleaseMetadata(parseFirmwareMetadata(fs.readFileSync(updatePath)), device);
  assertReleaseMetadata(parseFirmwareMetadata(fs.readFileSync(factoryPath), 0x10000), device);
  const updateSize = fs.statSync(updatePath).size;
  if (updateSize > otaSlotSize) {
    throw new Error(
      `Firmware image is ${updateSize} bytes but the OTA slot is only ${otaSlotSize} bytes.`
    );
  }

  let factory = fs.readFileSync(factoryPath);
  if (iconDir) {
    const deviceSelect = fs.readFileSync(path.join(repoRoot, 'src/devices/device_select.h'), 'utf8');
    const px = iconPxForDefine(device.define, deviceSelect);
    const iconFile = path.join(path.resolve(repoRoot, iconDir), `hometiles-icons-${px}.bin`);
    const added = addIconFileToFactory(factory, { iconFile, px, mklittlefs: findMklittlefs() });
    factory = added.factory;
    console.log(`[release-helper] ${deviceKey}: /${added.name} in the factory LittleFS at 0x${added.partition.offset.toString(16)}`);
  }

  ensureCleanDeviceOutputs(outDir, deviceKey);

  const updateDest = path.join(outDir, `hometiles_${version}_${deviceKey}.bin`);
  const factoryDest = path.join(outDir, `hometiles_${version}_${deviceKey}_factory.bin`);

  fs.copyFileSync(updatePath, updateDest);
  fs.writeFileSync(factoryDest, factory);

  console.log(`[release-helper] ${path.relative(repoRoot, updateDest)}`);
  console.log(`[release-helper] ${path.relative(repoRoot, factoryDest)}`);
}

main();

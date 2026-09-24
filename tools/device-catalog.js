const fs = require('node:fs');
const path = require('node:path');

const catalogPath = path.join(__dirname, 'device-profiles.json');

function assert(condition, message) {
  if (!condition) throw new Error(`Device catalog: ${message}`);
}

function validateCatalog(catalog) {
  assert(catalog?.schemaVersion === 1, 'unsupported schema version');
  assert(Array.isArray(catalog.profiles) && catalog.profiles.length > 0, 'profiles are missing');
  const keys = new Set();
  const builds = new Set();
  const ciOrders = new Set();
  const identities = new Set();
  for (const profile of catalog.profiles) {
    for (const field of ['key', 'buildProfile', 'metadataDeviceKey']) {
      assert(typeof profile[field] === 'string' && /^[a-z][a-z0-9_]*$/.test(profile[field]),
        `invalid ${field}`);
    }
    assert(!keys.has(profile.key), `duplicate key ${profile.key}`);
    assert(!builds.has(profile.buildProfile), `duplicate build profile ${profile.buildProfile}`);
    keys.add(profile.key);
    builds.add(profile.buildProfile);
    assert(/^DEVICE_[A-Z0-9_]+$/.test(profile.define), `invalid define for ${profile.key}`);
    assert(Buffer.byteLength(profile.metadataDeviceKey) < 32, `metadata key too long for ${profile.key}`);
    assert(['ESP32-P4', 'ESP32-S3'].includes(profile.chipFamily), `invalid chip family for ${profile.key}`);
    assert([16 * 1024 * 1024, 32 * 1024 * 1024].includes(profile.flashSize), `invalid flash size for ${profile.key}`);
    assert(typeof profile.publish === 'boolean', `publish must be explicit for ${profile.key}`);
    const s3 = profile.chipFamily === 'ESP32-S3';
    const revision = profile.siliconVariant;
    assert(s3 ? revision === 'default' : ['pre_v3', 'rev3_1', 'post_v3'].includes(revision),
      `invalid silicon variant for ${profile.key}`);
    const [minimum, maximum] = s3 ? [0, 65535] : revision === 'pre_v3' ? [1, 199] : revision === 'rev3_1' ? [301, 301] : [301, 399];
    assert(profile.minimumRevision === minimum && profile.maximumRevision === maximum,
      `unsafe revision range for ${profile.key}`);
    assert(revision !== 'rev3_1' || (profile.key === 'waveshare_touch_lcd_7b_rev3_1' &&
      profile.metadataDeviceKey === 'waveshare_touch_lcd_7b' && profile.define === 'DEVICE_WAVESHARE_TOUCH_LCD_7B'),
    `unsupported exact-v3.1 target ${profile.key}`);
    assert(revision !== 'post_v3' || (profile.key === 'waveshare_touch_lcd_10_1_rev3' &&
      profile.metadataDeviceKey === 'waveshare_touch_lcd_10_1' && profile.define === 'DEVICE_WAVESHARE_TOUCH_LCD_10_1'),
    `unsupported v3.x target ${profile.key}`);
    const identity = `${profile.define}:${revision}`;
    assert(!identities.has(identity), `ambiguous define/silicon identity ${identity}`);
    identities.add(identity);
    const isGuitionV1 = profile.key === 'guition_jc8012p4a1';
    assert(profile.rxVariant === (s3 ? 'native-s3' : isGuitionV1 ? 'repo-guition-jc8012-rx-single-block' : 'repo-a8204'),
      `unsafe transport variant for ${profile.key}`);
    assert(profile.elfFlags === (isGuitionV1 ? '-Wl,--wrap=esp_hosted_get_default_sdio_config' : ''),
      `unsafe linker flags for ${profile.key}`);
    if (!profile.publish) {
      assert(!profile.installer && profile.ciOrder === undefined, `non-release profile ${profile.key} cannot be published`);
      continue;
    }
    assert(Number.isInteger(profile.ciOrder) && profile.ciOrder >= 0 && !ciOrders.has(profile.ciOrder),
      `invalid or duplicate CI order for ${profile.key}`);
    ciOrders.add(profile.ciOrder);
    assert(typeof profile.ciLabel === 'string' && /^[^\r\n:]+$/.test(profile.ciLabel), `invalid CI label for ${profile.key}`);
    assert(typeof profile.legacySlug === 'string' && /^[a-z0-9-]+$/.test(profile.legacySlug), `invalid legacy slug for ${profile.key}`);
    const installer = profile.installer;
    assert(installer && ['supported', 'validation-pending'].includes(installer.status), `invalid installer status for ${profile.key}`);
    assert(typeof installer.label === 'string' && installer.label.length > 0 &&
      typeof installer.hardwareCheck === 'string' && installer.hardwareCheck.length > 0,
    `installer text is missing for ${profile.key}`);
    assert(s3 ? installer.acceptsLegacyDescriptor === undefined : installer.acceptsLegacyDescriptor === (revision === 'pre_v3'),
      `unsafe legacy descriptor policy for ${profile.key}`);
    assert(profile.metadataDeviceKey === profile.key || installer.explicitMetadataKey === true,
      `installer metadata key is missing for ${profile.key}`);
  }
  return catalog;
}

const catalog = validateCatalog(JSON.parse(fs.readFileSync(catalogPath, 'utf8')));
const profiles = catalog.profiles;
const releaseProfiles = profiles.filter((profile) => profile.publish);

function getBuildProfile(name) {
  const profile = profiles.find((candidate) => candidate.buildProfile === name);
  assert(profile, `unknown build profile ${name}`);
  return profile;
}

function getReleaseProfile(key) {
  const profile = releaseProfiles.find((candidate) => candidate.key === key);
  assert(profile, `unknown release device ${key}`);
  return profile;
}

function installerProfiles() {
  return releaseProfiles.map((profile) => ({
    key: profile.key,
    buildProfile: profile.buildProfile,
    label: profile.installer.label,
    chipFamily: profile.chipFamily,
    flashSize: profile.flashSize,
    status: profile.installer.status,
    hardwareCheck: profile.installer.hardwareCheck,
    ...(profile.installer.explicitMetadataKey ? { metadataDeviceKey: profile.metadataDeviceKey } : {}),
    ...(profile.chipFamily === 'ESP32-P4' ? {
      siliconVariant: profile.siliconVariant,
      minimumRevision: profile.minimumRevision,
      maximumRevision: profile.maximumRevision,
      acceptsLegacyDescriptor: profile.installer.acceptsLegacyDescriptor,
    } : {}),
  }));
}

function releaseFileVersion(version) {
  assert(typeof version === 'string' && version.length > 0, 'invalid release version');
  // Preserve the established packaging names, including legacy beta suffixes.
  return version.replace(/[^A-Za-z0-9._-]/g, '-');
}

function releaseAssetNames(version) {
  version = releaseFileVersion(version);
  return releaseProfiles.flatMap(({ key }) => [
    `hometiles_${version}_${key}.bin`,
    `hometiles_${version}_${key}_factory.bin`,
  ]).sort();
}

function verifyReleaseAssets(directory, version) {
  const actual = fs.readdirSync(directory, { withFileTypes: true })
    .filter((entry) => entry.isFile() && entry.name.endsWith('.bin')).map((entry) => entry.name).sort();
  const expected = releaseAssetNames(version);
  assert(JSON.stringify(actual) === JSON.stringify(expected),
    `release asset set mismatch; missing: ${expected.filter((name) => !actual.includes(name)).join(', ') || '(none)'}; unexpected: ${actual.filter((name) => !expected.includes(name)).join(', ') || '(none)'}`);
}

if (require.main === module) {
  try {
    const args = process.argv.slice(2);
    if (args[0] === '--profile' && args.length === 2) {
      console.log(JSON.stringify(getBuildProfile(args[1])));
    } else if (args[0] === '--verify-release-assets' && args.length === 3) {
      verifyReleaseAssets(args[1], args[2]);
      console.log('Release asset set: PASS');
    } else {
      throw new Error('Usage: node tools/device-catalog.js --profile <name> | --verify-release-assets <directory> <version>');
    }
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}

module.exports = { profiles, releaseProfiles, validateCatalog, getBuildProfile, getReleaseProfile,
  installerProfiles, releaseFileVersion, releaseAssetNames, verifyReleaseAssets };

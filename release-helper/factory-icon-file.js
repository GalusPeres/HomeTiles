const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

// Puts the panel's icon file into the LittleFS partition of its factory
// image, so a freshly flashed panel shows every icon without downloading it.
// - The partition: the "spiffs" data entry of the partition table inside the
//   factory image itself (0x8000), so the image and the table never disagree.
// - The icon size: the layout defines of src/devices/device_select.h, read the
//   same way src/tiles/icons/mdi_icons.h picks its font (1024x600 40 px,
//   480x480 32 px, every other panel 48 px).
// - The image: the ESP32 core's own mklittlefs with the firmware's LittleFS
//   geometry, 4096 byte flash sectors as blocks and CONFIG_LITTLEFS_PAGE_SIZE
//   256 from the core's sdkconfig.

const kPartitionTableOffset = 0x8000;
const kPartitionEntryBytes = 32;
const kBlockSize = 4096;
const kPageSize = 256;

function storagePartition(factory) {
  for (let at = kPartitionTableOffset; at + kPartitionEntryBytes <= factory.length; at += kPartitionEntryBytes) {
    if (factory[at] !== 0xaa || factory[at + 1] !== 0x50) break;
    const type = factory[at + 2];
    const subtype = factory[at + 3];
    const label = factory.subarray(at + 12, at + 28).toString('latin1').replace(/\0.*$/, '');
    if (type === 0x01 && subtype === 0x82 && label === 'spiffs') {
      return { offset: factory.readUInt32LE(at + 4), size: factory.readUInt32LE(at + 8) };
    }
  }
  throw new Error('The factory image has no spiffs data partition');
}

function definesBefore(deviceSelect, marker) {
  const at = deviceSelect.indexOf(marker);
  if (at < 0) throw new Error(`device_select.h has no ${marker}`);
  const start = deviceSelect.lastIndexOf('#if ', at);
  return [...deviceSelect.slice(start, at).matchAll(/defined\((\w+)\)/g)].map((match) => match[1]);
}

function iconPxForDefine(define, deviceSelect) {
  if (definesBefore(deviceSelect, '#define DEVICE_LAYOUT_1024X600').includes(define)) return 40;
  if (definesBefore(deviceSelect, '#define DEVICE_LAYOUT_480X480').includes(define)) return 32;
  return 48;
}

function findMklittlefs() {
  if (process.env.MKLITTLEFS) return process.env.MKLITTLEFS;
  const roots = [path.join(os.homedir(), '.arduino15')];
  if (process.env.LOCALAPPDATA) roots.push(path.join(process.env.LOCALAPPDATA, 'Arduino15'));
  for (const root of roots) {
    const dir = path.join(root, 'packages', 'esp32', 'tools', 'mklittlefs');
    if (!fs.existsSync(dir)) continue;
    for (const version of fs.readdirSync(dir).sort().reverse()) {
      for (const name of ['mklittlefs', 'mklittlefs.exe']) {
        const candidate = path.join(dir, version, name);
        if (fs.existsSync(candidate)) return candidate;
      }
    }
  }
  return null;
}

function runTool(tool, args) {
  const result = spawnSync(tool, args.map(String), { encoding: 'utf8' });
  if (result.status !== 0) {
    throw new Error(`${path.basename(tool)} ${args.join(' ')} failed: ${result.stdout}${result.stderr}`);
  }
  return result.stdout;
}

// Returns a copy of the factory image with the icon file in its LittleFS
// partition; the rest of the image stays byte for byte.
function addIconFileToFactory(factory, { iconFile, px, mklittlefs }) {
  if (!mklittlefs) throw new Error('mklittlefs of the ESP32 core was not found (set MKLITTLEFS)');
  const partition = storagePartition(factory);
  if (factory.length < partition.offset + partition.size) {
    throw new Error(`The factory image ends before its LittleFS partition (${factory.length} bytes)`);
  }
  const name = `_mdi_icons_${px}.bin`;
  const iconBytes = fs.readFileSync(iconFile);
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'hometiles-littlefs-'));
  try {
    const root = path.join(work, 'root');
    fs.mkdirSync(root);
    fs.writeFileSync(path.join(root, name), iconBytes);
    const image = path.join(work, 'littlefs.bin');
    const geometry = ['-b', kBlockSize, '-p', kPageSize, '-s', partition.size];
    runTool(mklittlefs, ['-c', root, ...geometry, image]);
    const bytes = fs.readFileSync(image);
    if (bytes.length !== partition.size) {
      throw new Error(`The LittleFS image has ${bytes.length} bytes, the partition ${partition.size}`);
    }
    const listing = runTool(mklittlefs, ['-l', ...geometry, image]);
    if (!new RegExp(`^${iconBytes.length}\\s+/${name.replace('.', '\\.')}\\b`, 'm').test(listing)) {
      throw new Error(`The LittleFS image does not list /${name} with ${iconBytes.length} bytes:\n${listing}`);
    }
    const out = Buffer.from(factory);
    bytes.copy(out, partition.offset);
    return { factory: out, partition, name };
  } finally {
    fs.rmSync(work, { recursive: true, force: true });
  }
}

module.exports = { storagePartition, iconPxForDefine, findMklittlefs, addIconFileToFactory };

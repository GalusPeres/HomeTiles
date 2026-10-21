// The old own-icon upload (/api/upload_icon, /api/sd_icons, the /icons folder
// on LittleFS) had no caller left in the Web Admin and no tile read it. No
// profile creates or fills the folder any more, and the endpoints are gone.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {readRepoFile, repoRoot} from '../../lib/admin-source.mjs';

const devices = path.join(repoRoot, 'src', 'devices');
for (const dir of fs.readdirSync(devices, {withFileTypes: true}).filter(entry => entry.isDirectory())) {
  for (const file of fs.readdirSync(path.join(devices, dir.name)).filter(name => name.endsWith('.cpp'))) {
    const source = readRepoFile('src', 'devices', dir.name, file);
    assert.doesNotMatch(source, /"\/icons"/, `${dir.name}/${file} no longer creates or copies /icons`);
  }
}
assert.doesNotMatch(readRepoFile('src', 'tiles', 'config', 'tile_config.cpp'), /"\/icons"/);
const server = readRepoFile('src', 'web', 'server', 'web_admin.cpp');
assert.doesNotMatch(server, /\/api\/upload_icon|\/api\/sd_icons/, 'the unused icon endpoints are gone');
assert.doesNotMatch(readRepoFile('src', 'web', 'server', 'web_admin.h'), /handleUploadIcon|handleGetSdIcons/);
assert.doesNotMatch(readRepoFile('src', 'web', 'server', 'handlers', 'web_admin_files.cpp'), /"\/icons|handleUploadIcon|handleGetSdIcons/);
console.log('PASS: no profile creates the unused /icons folder; its endpoints are gone');

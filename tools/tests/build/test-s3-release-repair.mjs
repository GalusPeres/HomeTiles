import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {replacementNames, inspectReplacements, verifyRepair} from '../../repair-s3-release.mjs';
import {releaseAssetNames} from '../../device-catalog.js';

const tag='v0.6.10';
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'hometiles-s3-repair-'));
const names=replacementNames(tag);
assert.equal(names.length,6);
for(const invalid of ['../v0.6.10','v0.6.10/../../x','main','']) assert.throws(()=>replacementNames(invalid));
try {
  const content='v0.6.10 HTTPS host=%s tls=%d (%s) allocator=%s';
  for(const name of names) fs.writeFileSync(path.join(dir,name),content);
  const replacements=inspectReplacements(dir,tag);
  const before={assets:releaseAssetNames(tag).map(name=>({name,size:100,digest:'sha256:'+'a'.repeat(64)}))};
  const after={assets:before.assets.map(a=>replacements.find(r=>r.name===a.name)??a)};
  verifyRepair(before,after,replacements,tag);
  const p4=after.assets.findIndex(a=>!names.includes(a.name));
  const changed=structuredClone(after); changed.assets[p4].digest='sha256:'+'b'.repeat(64);
  assert.throws(()=>verifyRepair(before,changed,replacements,tag),/digest mismatch/);
  const missing=structuredClone(after); missing.assets.pop();
  assert.throws(()=>verifyRepair(before,missing,replacements,tag));
  fs.writeFileSync(path.join(dir,names[0]),'v0.6.10 without the fix');
  assert.throws(()=>inspectReplacements(dir,tag),/fix marker missing/);
  fs.writeFileSync(path.join(dir,names[0]),content.replace('v0.6.10','v0.6.9'));
  assert.throws(()=>inspectReplacements(dir,tag),/version missing/);
  fs.writeFileSync(path.join(dir,names[0]),content);
  fs.writeFileSync(path.join(dir,'unexpected-p4.bin'),content);
  assert.throws(()=>inspectReplacements(dir,tag),/Unexpected or missing/);
} finally { fs.rmSync(dir,{recursive:true,force:true}); }
const workflow=fs.readFileSync(new URL('../../../.github/workflows/firmware.yml',import.meta.url),'utf8');
const build=workflow.slice(workflow.indexOf('  build:'),workflow.indexOf('  repair-s3-release:'));
for(const name of ['Check out repository','Set up Arduino CLI','Compile firmware','Package firmware binaries','Upload firmware artifact']) {
  const step=build.slice(build.indexOf('- name: '+name)).split(/\n      - name:/)[0];
  assert.match(step,/if:.*!inputs\.repair_s3_release.*matrix\.rx_variant == 'native-s3'/,name);
}
console.log('S3-only replacement set, version, fix marker and unchanged P4 digests: PASS');
const repairJob=workflow.slice(workflow.indexOf('  repair-s3-release:'),workflow.indexOf('\n  release:'));
assert.ok(repairJob.indexOf('node tools/repair-s3-release.mjs') < repairJob.indexOf('gh workflow run docs.yml'));

import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {spawn, spawnSync} from 'node:child_process';

// Optional real LVGL software rendering, using the same installed library as Arduino.
export async function lvglHost(root) {
  const library = [process.env.LVGL_INCLUDE, path.join(os.homedir(), 'Documents/Arduino/libraries/lvgl')]
    .filter(Boolean).find(p => fs.existsSync(path.join(p, 'lvgl.h')));
  const cc = ['clang', 'gcc'].find(c => spawnSync(c, ['--version']).status === 0);
  const cxx = ['clang++', 'g++'].find(c => spawnSync(c, ['--version']).status === 0);
  const ar = ['llvm-ar', 'ar'].find(c => spawnSync(c, ['--version']).status === 0);
  if (!library || !cc || !cxx || !ar) return null;
  const flags = ['-DLV_CONF_SKIP', '-DLV_FONT_FMT_TXT_LARGE=1', '-DLV_USE_FONT_COMPRESSED=1', '-DLV_COLOR_DEPTH=32', '-DLV_MEM_SIZE=33554432', '-I', library, '-I', root, '-DMDI_ICONS_32=1', '-DMDI_ICONS_40=1', '-DMDI_ICONS_48=1', '-DUI_FONT_32=1', '-DUI_FONT_40=1', '-DUI_FONT_14=1', '-DUI_FONT_CYRILLIC_14=1'];
  const walk = p => fs.readdirSync(p, {withFileTypes: true}).flatMap(e => e.isDirectory() ? walk(path.join(p, e.name)) : [path.join(p, e.name)]);
  const sources = walk(path.join(library, 'src'));
  const signature = crypto.createHash('sha256').update(JSON.stringify(flags));
  for (const p of sources.filter(p => p.endsWith('.h'))) signature.update(fs.readFileSync(p));
  signature.update(spawnSync(cc, ['--version']).stdout);
  const out = path.join(root, 'build/tests/lvgl-host', signature.digest('hex').slice(0, 16));
  fs.mkdirSync(out, {recursive: true});
  const fonts = walk(path.join(root, 'src/fonts')).filter(p => /(?:ui_(font_(14|16|20|24|28|32|40|cyrillic_14|cyrillic_16|cyrillic_20|cyrillic_24)|symbols_(20|24))|mdi_icons_(32|40|48))\.c$/.test(p));
  const objects = [], pending = [];
  for (const file of [...sources.filter(p => p.endsWith('.c')), ...fonts]) {
    const digest = crypto.createHash('sha256').update(file).update(fs.readFileSync(file)).digest('hex');
    const obj = path.join(out, digest + '.o'); objects.push(obj);
    if (!fs.existsSync(obj)) pending.push({file, obj});
  }
  const run = (cmd, args) => new Promise((resolve, reject) => {
    const child = spawn(cmd, args); let output = '';
    child.stdout.on('data', b => output += b); child.stderr.on('data', b => output += b);
    child.on('error', reject); child.on('exit', code => code === 0 ? resolve() : reject(new Error(output)));
  });
  await Promise.all(Array.from({length: 8}, async () => {
    while (pending.length) { const {file, obj} = pending.pop(); await run(cc, [...flags, '-O1', '-w', '-c', file, '-o', obj]); }
  }));
  const archive = path.join(out, 'lvgl.a');
  const manifest = path.join(out, 'objects.json');
  if (!fs.existsSync(archive) || !fs.existsSync(manifest) || fs.readFileSync(manifest, 'utf8') !== JSON.stringify(objects)) {
    const response = path.join(out, 'objects.rsp'); fs.writeFileSync(response, objects.map(p => '"' + p.replaceAll('\\', '/') + '"').join('\n'));
    const next = archive + '.new';
    if (fs.existsSync(next)) fs.unlinkSync(next);
    await run(ar, ['rcs', next, '@' + response]); fs.renameSync(next, archive);
    fs.writeFileSync(manifest, JSON.stringify(objects));
  }
  return {library, flags, cxx, archive};
}

#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { gzipSync } from 'node:zlib';
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readAdminBundle } from './lib/admin-bundle.mjs';
import { formatAdminDelivery } from './lib/admin-delivery.mjs';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const checkOnly = process.argv.includes('--check');

const assets = [
  {
    key: 'Css',
    source: 'src/web/assets/admin.css',
    generated: 'src/web/generated/admin_css_gzip.inc',
    extension: 'css',
    contentType: 'text/css; charset=utf-8'
  },
  {
    key: 'Js',
    source: 'src/web/assets/admin.js',
    generated: 'src/web/generated/admin_js_gzip.inc',
    extension: 'js',
    contentType: 'application/javascript; charset=utf-8'
  },
  {
    // Login page and Web Admin password helpers (SHA-256/HMAC, CSRF header).
    // Served without a session, so it must stay free of device data.
    key: 'AuthJs',
    name: 'auth',
    source: 'src/web/assets/auth.js',
    generated: 'src/web/generated/auth_js_gzip.inc',
    extension: 'js',
    contentType: 'application/javascript; charset=utf-8'
  }
];

function normalizedSource(relativePath) {
  return readFileSync(resolve(repoRoot, relativePath), 'utf8')
    .replace(/\r\n?/g, '\n');
}

function gzipDeterministic(data) {
  const compressed = gzipSync(data, { level: 9, mtime: 0 });
  // zlib may use a platform-specific gzip OS marker. It has no semantic
  // meaning, so normalize it to "unknown" for byte-identical generation.
  compressed[9] = 255;
  return compressed;
}

function byteInclude(data) {
  const lines = [];
  for (let offset = 0; offset < data.length; offset += 12) {
    const bytes = Array.from(data.subarray(offset, offset + 12), value =>
      `0x${value.toString(16).padStart(2, '0')}`);
    lines.push(`  ${bytes.join(', ')},`);
  }
  return `${lines.join('\n')}\n`;
}

function writeOrCheck(relativePath, expected) {
  const absolutePath = resolve(repoRoot, relativePath);
  let current = null;
  try {
    current = readFileSync(absolutePath, 'utf8').replace(/\r\n?/g, '\n');
  } catch (error) {
    if (error.code !== 'ENOENT') throw error;
  }
  if (current === expected) return;
  if (checkOnly) {
    if (current === null) {
      throw new Error(`Generated file is missing: ${relativePath}`);
    }
    throw new Error(
      `Generated file is stale: ${relativePath}\n` +
      'Run: node tools/generate-web-assets.mjs');
  }
  writeFileSync(absolutePath, expected, 'utf8');
}

writeOrCheck('src/web/assets/admin.js', readAdminBundle(repoRoot).source);

const generated = await Promise.all(assets.map(async asset => {
  const source = normalizedSource(asset.source);
  const sourceBytes = Buffer.from(source, 'utf8');
  const delivery = asset.extension === 'js' ? await formatAdminDelivery(source) : source;
  const deliveryBytes = Buffer.from(delivery, 'utf8');
  // Hash the decoded HTTP response, so a formatter update cannot reuse an
  // immutable asset URL for different delivered bytes. ETags identify gzip.
  const hash = createHash('sha256').update(deliveryBytes).digest('hex');
  const gzip = gzipDeterministic(deliveryBytes);
  const gzipHash = createHash('sha256').update(gzip).digest('hex');
  writeOrCheck(asset.generated, byteInclude(gzip));
  return { ...asset, hash, gzipHash, sourceBytes, deliveryBytes, gzip };
}));

const metaLines = [
  '#ifndef WEB_ADMIN_ASSETS_META_H',
  '#define WEB_ADMIN_ASSETS_META_H',
  '',
  '#include <stddef.h>',
  '',
  'namespace web_admin_assets_generated {',
  '// SourceSize and the path hash describe decoded HTTP response bytes.',
  '// The readable Admin JS bundle is formatted only during host generation.',
  ''
];

for (const asset of generated) {
  const shortHash = asset.hash.slice(0, 12);
  metaLines.push(
    `inline constexpr char kAdmin${asset.key}Path[] =`,
    `    "/assets/${asset.name || 'admin'}.${shortHash}.${asset.extension}";`,
    `inline constexpr char kAdmin${asset.key}Etag[] =`,
    `    "\\\"${asset.gzipHash}\\\"";`,
    `inline constexpr char kAdmin${asset.key}ContentType[] =`,
    `    "${asset.contentType}";`,
    `inline constexpr size_t kAdmin${asset.key}SourceSize = ${asset.deliveryBytes.length};`,
    `inline constexpr size_t kAdmin${asset.key}GzipSize = ${asset.gzip.length};`,
    ''
  );
}

metaLines.push(
  '}  // namespace web_admin_assets_generated',
  '',
  '#endif  // WEB_ADMIN_ASSETS_META_H',
  ''
);
writeOrCheck(
  'src/web/generated/admin_assets_meta.h',
  metaLines.join('\n'));

const mode = checkOnly ? 'verified' : 'generated';
for (const asset of generated) {
  console.log(
    `${asset.source}: ${asset.sourceBytes.length} source -> ${asset.deliveryBytes.length} delivery -> ${asset.gzip.length} gzip bytes (${mode})`);
}

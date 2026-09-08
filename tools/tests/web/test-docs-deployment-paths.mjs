import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { findBrowser } from '../../lib/headless-dom.mjs';
import { startDocsBrowser } from '../../lib/docs-browser.mjs';
import { setupSerial } from './fixtures/docs-serial.mjs';

const python = process.env.PYTHON || (process.platform === 'win32' ? 'python' : 'python3');
const executable = findBrowser();
if (!executable || spawnSync(python, ['-c', 'import mkdocs, material']).status !== 0) {
  if (process.env.HOMETILES_REQUIRE_DOCS_BROWSER === '1') throw new Error('Chrome and mkdocs-material are required');
  console.log('SKIP: Deployment navigation needs Chrome and mkdocs-material.');
  process.exit(0);
}
const repository = fileURLToPath(new URL('../../../', import.meta.url));
const artifacts = path.join(repository, 'build/docs-deployment-paths');
const site = path.join(artifacts, 'site');
fs.mkdirSync(artifacts, { recursive: true });
const server = http.createServer((request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname.replace(/^\/HomeTiles(?=\/)/, '');
  let file = path.resolve(site, '.' + decodeURIComponent(pathname));
  if (!file.startsWith(site + path.sep) && file !== site) { response.writeHead(404).end(); return; }
  if (fs.existsSync(file) && fs.statSync(file).isDirectory()) file = path.join(file, 'index.html');
  if (!fs.existsSync(file)) { response.writeHead(404).end(); return; }
  const type = { '.html': 'text/html', '.mjs': 'text/javascript', '.js': 'text/javascript', '.css': 'text/css', '.xml': 'application/xml', '.json': 'application/json' }[path.extname(file)];
  if (type) response.setHeader('Content-Type', type);
  response.end(fs.readFileSync(file));
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
let browser;
try {
  // One root-canonical snapshot is hosted at both public paths in production.
  const build = spawnSync(python, ['-c', "import sys; from mkdocs.config import load_config; from mkdocs.commands.build import build; c=load_config('mkdocs.yml', site_url=sys.argv[1]+'/', site_dir=sys.argv[2], strict=True); c.extra['sitemap_aliases']=[sys.argv[1]+'/HomeTiles/']; build(c)", origin, site], { cwd: repository, encoding: 'utf8' });
  assert.equal(build.status, 0, build.stderr);
  browser = await startDocsBrowser(executable, fs.mkdtempSync(path.join(artifacts, 'browser-')));
  await browser.send('Page.addScriptToEvaluateOnNewDocument', { source: setupSerial });
  for (const prefix of ['', '/HomeTiles']) {
    const base = origin + prefix + '/';
    await browser.send('Page.navigate', { url: base + 'device-logs/' });
    await browser.until("!!document.querySelector('[data-log-connect]') && !document.querySelector('[data-log-connect]').disabled", 'Logger ready');
    await browser.evaluate("window.navigationToken = 'retained'; void document$.subscribe(() => window.finishedNavigation = location.href); document.querySelector('[data-log-connect]').click()");
    await browser.until('fixture.opened', 'Logger connected');
    for (const page of ['faq/', '', 'device-logs/']) {
      await browser.evaluate(`Array.from(document.querySelectorAll('.md-sidebar--primary a')).find(a => a.href === ${JSON.stringify(base + page)}).click()`);
      await browser.until(`window.finishedNavigation === ${JSON.stringify(base + page)}`, 'Navigation at ' + base);
      assert.equal(await browser.evaluate("window.navigationToken === 'retained' && fixture.opened && fixture.closes === 0"), true, 'USB capture was interrupted at ' + base);
    }
    await browser.evaluate("fixture.emit('After navigation at both mounts\\n')");
    await browser.until("document.querySelector('[data-log-output]').textContent.includes('After navigation at both mounts')", 'Capture still receives data');
    await browser.evaluate("document.querySelector('[data-log-disconnect]').click()");
    await browser.until('!fixture.opened', 'Explicit disconnect');
  }
  assert.deepEqual(browser.errors, []);
  console.log('Root and /HomeTiles/ deployment paths retain real logger sessions across navigation.');
} finally {
  await browser?.close();
  await new Promise(resolve => server.close(resolve));
}

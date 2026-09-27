// Runs the delivered auth.js (login page and Web Admin password helpers) with
// a stubbed browser: SHA-256/HMAC must match OpenSSL, the login must produce
// the proof the panel expects and verify the panel's server proof, and a
// protected admin page must add the CSRF header to every changing request.
import assert from 'node:assert/strict';
import {createHash, createHmac, webcrypto} from 'node:crypto';
import vm from 'node:vm';
import {gunzipSync} from 'node:zlib';

import {readRepoFile} from '../../lib/admin-source.mjs';

function deliveredAuthJs() {
  const include = readRepoFile('src/web/generated/auth_js_gzip.inc');
  const bytes = Buffer.from([...include.matchAll(/0x([0-9a-f]{2})/g)]
    .map(match => Number.parseInt(match[1], 16)));
  return gunzipSync(bytes).toString('utf8');
}
const source = deliveredAuthJs();
assert.doesNotMatch(readRepoFile('src/web/assets/auth.js'),
  /textContent\s*=\s*['"`]|alert\(['"`]/, 'auth.js has no hardcoded display text');

const ORIGIN = 'http://panel.local';

function createBrowser({csrf = '', loginForm = null, fetchImpl}) {
  const calls = [];
  const navigation = [];
  const listeners = {};
  class FakeXhr {
    constructor() { this.headers = {}; }
    open(method, url) { this.method = method; this.url = url; }
    setRequestHeader(name, value) { this.headers[name] = value; }
    send(body) { this.body = body; FakeXhr.sent.push(this); }
  }
  FakeXhr.sent = [];
  const document = {
    readyState: 'complete',
    querySelector: selector => selector === 'meta[name="hometiles-csrf"]' && csrf
      ? {getAttribute: () => csrf} : null,
    getElementById: id => (loginForm && loginForm.elements[id]) || null,
    addEventListener: (type, handler) => { listeners[type] = handler; }
  };
  const window = {
    document,
    crypto: webcrypto,
    location: {
      href: ORIGIN + '/', origin: ORIGIN,
      replace: url => navigation.push(['replace', url]),
      assign: url => navigation.push(['assign', url])
    },
    setTimeout: (fn) => { fn(); return 1; },
    fetch: async (input, init = {}) => {
      calls.push({url: String(input), init});
      return fetchImpl(String(input), init);
    }
  };
  // As in a browser, the window is the global object: a bare fetch() call
  // inside auth.js resolves to window.fetch, including its CSRF wrapper.
  Object.assign(window, {
    XMLHttpRequest: FakeXhr, Request, Response, Headers, URL, URLSearchParams,
    TextEncoder, HTMLFormElement: class {}, console
  });
  window.window = window;
  vm.createContext(window);
  vm.runInContext(source, window);
  return {window, calls, navigation, listeners, FakeXhr};
}

const json = (body, status = 200, headers = {}) =>
  new Response(JSON.stringify(body), {status, headers: {'Content-Type': 'application/json', ...headers}});

// 1. Primitive agreement with OpenSSL.
{
  const {window} = createBrowser({fetchImpl: async () => json({})});
  const auth = window.HomeTilesAuth;
  for (let length = 0; length < 200; length += 7) {
    const data = Buffer.from(Array.from({length}, (_, i) => (i * 31 + length) & 0xff));
    assert.equal(auth.toHex(auth.sha256(new Uint8Array(data))),
      createHash('sha256').update(data).digest('hex'), `SHA-256 ${length}`);
    const key = Buffer.from(Array.from({length: length % 90}, (_, i) => (i * 7 + 1) & 0xff));
    assert.equal(auth.toHex(auth.hmacSha256(new Uint8Array(key), new Uint8Array(data))),
      createHmac('sha256', key).update(data).digest('hex'), `HMAC ${length}`);
  }
  assert.equal(auth.toHex(auth.utf8('ä€')), 'c3a4e282ac');
  assert.throws(() => auth.fromHex('0g'));
}

// 2. Login: proof and server proof exactly as the firmware computes them.
const salt = Buffer.from('a1'.repeat(16), 'hex');
const nonce = Buffer.from('5c'.repeat(32), 'hex');
const password = 'Pässwort-123';
const key = createHash('sha256').update(Buffer.concat([salt, Buffer.from(password, 'utf8')])).digest();
const proof = createHmac('sha256', key).update(nonce).digest();
const serverProof = createHmac('sha256', key)
  .update(Buffer.concat([Buffer.from('HomeTiles-Web-Admin-server-v1'), nonce, proof])).digest('hex');
// Shared with HomeTiles Bridge tests/test_panel_auth.py (Python hashlib/hmac).
assert.equal(key.toString('hex'), '1a1168e2a2b908f40f849a0828d7dc2120d5e3355a33cb614f66600d2a7ca954');
assert.equal(proof.toString('hex'), 'a4b17028d639885824c1cd3e586a1203edb2648daafad81c14c4f203e92f32f9');
assert.equal(serverProof, '1ead99615ad5e76301fc20b5f87d4410bccd52b7df60f7464ea1b38160e8abe5');

function panel({loginStatus = 200, loginBody = null} = {}) {
  return async (url, init) => {
    if (url === '/api/auth/challenge') {
      return json({enabled: true, salt: salt.toString('hex'), nonce: nonce.toString('hex')});
    }
    if (url === '/api/auth/login') {
      const body = JSON.parse(init.body);
      assert.equal(body.nonce, nonce.toString('hex'));
      assert.equal(body.proof, proof.toString('hex'), 'proof = HMAC(SHA-256(salt || password), nonce)');
      return json(loginBody || {csrf: 'c'.repeat(32), server_proof: serverProof}, loginStatus);
    }
    if (url === '/api/auth/password') return json({ok: true});
    return json({});
  };
}

{
  const {window} = createBrowser({fetchImpl: panel()});
  const result = await window.HomeTilesAuth.login(password);
  assert.deepEqual({...result}, {ok: true, csrf: 'c'.repeat(32)});
}
{
  const {window} = createBrowser({fetchImpl: panel({loginBody: {csrf: 'x', server_proof: '00'.repeat(32)}})});
  const result = await window.HomeTilesAuth.login(password);
  assert.equal(result.ok, false);
  assert.equal(result.error, 'server_proof', 'a fake panel without the key is detected');
}
{
  const {window} = createBrowser({fetchImpl: panel({loginStatus: 429, loginBody: {error: 'too_many_attempts', retry_after: 8}})});
  const result = await window.HomeTilesAuth.login(password);
  assert.equal(result.status, 429);
  assert.equal(result.retryAfter, 8);
}
{
  const {window} = createBrowser({fetchImpl: async url => url === '/api/auth/challenge'
    ? json({enabled: false}) : json({})});
  assert.equal((await window.HomeTilesAuth.login(password)).disabled, true);
}

// 3. Setting a password sends only salt and SHA-256(salt || password).
{
  const {window, calls} = createBrowser({fetchImpl: panel()});
  assert.equal(await window.HomeTilesAuth.setPassword(password), true);
  const request = calls.find(call => call.url === '/api/auth/password');
  const body = JSON.parse(request.init.body);
  assert.deepEqual(Object.keys(body).sort(), ['key', 'salt']);
  assert.equal(body.salt.length, 32);
  const expected = createHash('sha256')
    .update(Buffer.concat([Buffer.from(body.salt, 'hex'), Buffer.from(password, 'utf8')])).digest('hex');
  assert.equal(body.key, expected);
  assert.doesNotMatch(request.init.body, /Pässwort/);
  assert.equal(new Headers(request.init.headers).get('X-HomeTiles-CSRF'), 'setup');
}

// 4. Protected admin page: CSRF header on changes, login page on expiry.
{
  const token = 'ab'.repeat(16);
  let expired = false;
  const {window, calls, navigation, FakeXhr, listeners} = createBrowser({
    csrf: token,
    fetchImpl: async () => expired
      ? new Response('{}', {status: 401, headers: {'X-HomeTiles-Auth': 'required'}})
      : json({ok: true})
  });
  await window.fetch('/api/tiles', {method: 'POST', body: 'x'});
  await window.fetch('/api/tiles');
  await window.fetch('https://example.com/other', {method: 'POST'});
  assert.equal(new Headers(calls[0].init.headers).get('X-HomeTiles-CSRF'), token);
  assert.equal(new Headers(calls[1].init.headers || {}).get('X-HomeTiles-CSRF'), null);
  assert.equal(new Headers(calls[2].init.headers || {}).get('X-HomeTiles-CSRF'), null);
  const xhr = new FakeXhr();
  xhr.open('POST', '/api/ota/upload/raw');
  xhr.send('data');
  assert.equal(xhr.headers['X-HomeTiles-CSRF'], token);
  expired = true;
  await window.fetch('/api/status');
  assert.deepEqual(navigation, [['replace', '/']]);
  assert.equal(typeof listeners.submit, 'function', 'plain form posts are converted');
}
{
  // Without a password nothing is wrapped: behaviour stays unchanged.
  const {window, listeners} = createBrowser({fetchImpl: async () => json({})});
  assert.equal(window.HomeTilesAuth.csrfToken(), '');
  assert.equal(listeners.submit, undefined);
}

// 5. Login form wiring and messages from the server-rendered data attributes.
async function submitLogin(fetchImpl) {
  const message = {textContent: '', classList: {toggle() {}}};
  const input = {value: password, select() {}};
  const button = {disabled: false};
  let handler;
  const form = {
    dataset: {checking: 'CHECK', invalid: 'WRONG', locked: 'WAIT %s', failed: 'FAILED'},
    addEventListener: (type, fn) => { if (type === 'submit') handler = fn; },
    elements: {}
  };
  form.elements = {ht_login_form: form, ht_login_password: input, ht_login_submit: button, ht_login_message: message};
  const browser = createBrowser({loginForm: form, fetchImpl});
  await handler({preventDefault() {}});
  return {message, navigation: browser.navigation};
}
{
  const ok = await submitLogin(panel());
  assert.deepEqual(ok.navigation, [['replace', '/']]);
  const wrong = await submitLogin(panel({loginStatus: 401, loginBody: {error: 'invalid_password'}}));
  assert.equal(wrong.message.textContent, 'WRONG');
  const locked = await submitLogin(panel({loginStatus: 401, loginBody: {error: 'invalid_password', retry_after: 4}}));
  assert.equal(locked.message.textContent, 'WRONG WAIT 4');
  const throttled = await submitLogin(panel({loginStatus: 429, loginBody: {error: 'too_many_attempts', retry_after: 9}}));
  assert.equal(throttled.message.textContent, 'WAIT 9');
}

// The admin page loads auth.js before admin.js; the login page loads it too.
const scripts = readRepoFile('src/web/server/render/web_admin_scripts.cpp');
assert.ok(scripts.indexOf('authJsAssetPath()') < scripts.indexOf('adminJsAssetPath()'));
const bundle = readRepoFile('src/web/assets/admin.js');
assert.match(bundle, /function initWebAdminPasswordSettings\(\)/);
assert.match(readRepoFile('src/web/admin/core/bootstrap.js'), /initWebAdminPasswordSettings\(\);/);
console.log('Delivered auth.js: primitives, login, server proof, password setup and CSRF passed');

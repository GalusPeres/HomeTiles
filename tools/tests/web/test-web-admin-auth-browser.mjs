// Runs the delivered auth.js (login page and Web Admin password helpers) with
// a stubbed browser: SHA-256/HMAC/PBKDF2 must match OpenSSL, the login must
// produce the proof the panel expects and verify the panel's server proof, and
// a protected admin page must add the CSRF header to every changing request.
import assert from 'node:assert/strict';
import {createHash, createHmac, pbkdf2Sync, webcrypto} from 'node:crypto';
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
  // PBKDF2: passwords around the 64-byte HMAC block size, short salts, and the
  // first iterations where the fast two-compression path takes over.
  for (const length of [0, 1, 31, 63, 64, 65, 100]) {
    for (const iterations of [1, 2, 3, 1000]) {
      for (const saltLength of [16, 5]) {
        const secret = Buffer.from(Array.from({length}, (_, i) => (i * 37 + 11) & 0xff));
        const pbkdfSalt = Buffer.from(Array.from({length: saltLength}, (_, i) => i * 3));
        assert.equal(auth.toHex(auth.pbkdf2Sha256(new Uint8Array(secret), new Uint8Array(pbkdfSalt), iterations)),
          pbkdf2Sync(secret, pbkdfSalt, iterations, 32, 'sha256').toString('hex'),
          `PBKDF2 password ${length} B, ${iterations} iterations, salt ${saltLength} B`);
      }
    }
  }
}

// 2. Login: proof and server proof exactly as the firmware computes them.
const salt = Buffer.from('a1'.repeat(16), 'hex');
const nonce = Buffer.from('5c'.repeat(32), 'hex');
const password = 'Pässwort-123';
const iterations = 100000;
const key = pbkdf2Sync(Buffer.from(password, 'utf8'), salt, iterations, 32, 'sha256');
const proof = createHmac('sha256', key).update(nonce).digest();
const serverProof = createHmac('sha256', key)
  .update(Buffer.concat([Buffer.from('HomeTiles-Web-Admin-server-v1'), nonce, proof])).digest('hex');
// Shared with HomeTiles Bridge tests/test_panel_auth.py (Python hashlib/hmac)
// and docs-dev/command-encryption.md.
assert.equal(key.toString('hex'), '1ca04c9ba257bbc2be95d76f4ee7385ad79143f23050d4c5c56ec3e3fd5fddc0');
assert.equal(proof.toString('hex'), '028c2df33691a772cb260751e39bcda9716901c5bb6650ac66970e4513fe5afe');
assert.equal(serverProof, '5f38e5560475a83b44fa1014b30cb16f703442a6aab247dcf692795a23875007');
{
  const {window} = createBrowser({fetchImpl: async () => json({})});
  const auth = window.HomeTilesAuth;
  assert.equal(auth.toHex(auth.deriveKey(new Uint8Array(salt), password, iterations)), key.toString('hex'),
    'auth.js derives the shared vector key');
}
const doc = readRepoFile('docs-dev/command-encryption.md');
for (const value of [key.toString('hex'), proof.toString('hex'), serverProof]) {
  assert.ok(doc.includes(value), `the protocol document lists the shared vector ${value.slice(0, 8)}...`);
}

function panel({loginStatus = 200, loginBody = null, iter = iterations, challengeSalt = salt} = {}) {
  let logins = 0;
  const handler = async (url, init) => {
    if (url === '/api/auth/challenge') {
      return json({enabled: true, salt: challengeSalt.toString('hex'), nonce: nonce.toString('hex'), iter});
    }
    if (url === '/api/auth/login') {
      logins++;
      const body = JSON.parse(init.body);
      assert.equal(body.nonce, nonce.toString('hex'));
      assert.equal(body.proof, proof.toString('hex'), 'proof = HMAC(PBKDF2(password, salt, iter), nonce)');
      return json(loginBody || {csrf: 'c'.repeat(32), server_proof: serverProof}, loginStatus);
    }
    if (url === '/api/auth/password') return json({ok: true});
    return json({});
  };
  handler.logins = () => logins;
  return handler;
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
// A challenge outside 10,000..1,000,000 iterations (a fake or broken panel)
// is refused before any key derivation or login request.
for (const iter of [9999, 1000001, '300000', 1e4 + 0.5, null]) {
  const fetchImpl = panel({iter});
  const {window} = createBrowser({fetchImpl});
  const result = await window.HomeTilesAuth.login(password);
  assert.equal(result.ok, false);
  assert.equal(result.error, 'iterations', `iter ${iter} is refused`);
  assert.equal(fetchImpl.logins(), 0, `no login request for iter ${iter}`);
}

// 3. Setting a password sends only salt, iterations and the PBKDF2 key.
{
  const {window, calls} = createBrowser({fetchImpl: panel()});
  assert.equal(await window.HomeTilesAuth.setPassword(password), true);
  const request = calls.find(call => call.url === '/api/auth/password');
  const body = JSON.parse(request.init.body);
  assert.deepEqual(Object.keys(body).sort(), ['iter', 'key', 'salt']);
  assert.equal(body.salt.length, 32);
  assert.equal(body.iter, 300000, 'new passwords use 300,000 iterations');
  const expected = pbkdf2Sync(Buffer.from(password, 'utf8'), Buffer.from(body.salt, 'hex'),
    body.iter, 32, 'sha256').toString('hex');
  assert.equal(body.key, expected);
  assert.doesNotMatch(request.init.body, /Pässwort/);
  assert.equal(new Headers(request.init.headers).get('X-HomeTiles-CSRF'), 'setup');
}
// The login right after setting the password reuses the derived key once; a
// later login derives it again.
{
  const source = readRepoFile('src/web/assets/auth.js');
  const login = source.slice(source.indexOf('async function login('), source.indexOf('async function setPassword('));
  assert.match(login, /const reuse = justSet;\s*justSet = null;/, 'the remembered key is used at most once');
  assert.match(login, /reuse\.salt === challenge\.salt &&\s*reuse\.iterations === iterations && reuse\.password === password\s*\?\s*reuse\.key/,
    'it is reused only for the same salt, iterations and password');
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

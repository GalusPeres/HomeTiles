// HomeTiles Web Admin password helpers.
//
// Browsers expose WebCrypto only in secure contexts, and the panel is reached
// over plain http://, so SHA-256, HMAC-SHA256 and PBKDF2 are implemented here.
// The protocol (firmware: src/web/server/auth/web_admin_auth_core.h):
//   key   = PBKDF2-HMAC-SHA256(UTF-8 password, salt, iter, 32 bytes)
//   proof = HMAC-SHA256(key, nonce)
//   server_proof = HMAC-SHA256(key, label || nonce || proof)
// The slow key derivation makes a sniffed login expensive to brute-force; the
// panel only stores salt, iter and key and never derives a key itself.
// This file is served without a session (the login page needs it) and holds
// no device data. On the admin page it also adds the X-HomeTiles-CSRF header
// to every request that changes something.
(function () {
  'use strict';

  const SERVER_PROOF_LABEL = 'HomeTiles-Web-Admin-server-v1';
  // About 0.2 s on a desktop and well under a second on a phone. The panel
  // stores the count, so it can be raised later without a protocol change.
  const DEFAULT_ITERATIONS = 300000;
  const MIN_ITERATIONS = 10000;
  const MAX_ITERATIONS = 1000000;
  const IV = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
  const K = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
  ]);

  function rotr(value, bits) {
    return (value >>> bits) | (value << (32 - bits));
  }

  // One SHA-256 compression: the block in w[0..15] updates the eight state
  // words in place; w[16..63] is the message schedule.
  function compress(state, w) {
    for (let i = 16; i < 64; i++) {
      const x = w[i - 15];
      const y = w[i - 2];
      const s0 = rotr(x, 7) ^ rotr(x, 18) ^ (x >>> 3);
      const s1 = rotr(y, 17) ^ rotr(y, 19) ^ (y >>> 10);
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
    }
    let a = state[0], b = state[1], c = state[2], d = state[3];
    let e = state[4], f = state[5], g = state[6], h = state[7];
    for (let i = 0; i < 64; i++) {
      const s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const choice = (e & f) ^ (~e & g);
      const t1 = (h + s1 + choice + K[i] + w[i]) | 0;
      const s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const majority = (a & b) ^ (a & c) ^ (b & c);
      const t2 = (s0 + majority) | 0;
      h = g;
      g = f;
      f = e;
      e = (d + t1) | 0;
      d = c;
      c = b;
      b = a;
      a = (t1 + t2) | 0;
    }
    state[0] = (state[0] + a) | 0;
    state[1] = (state[1] + b) | 0;
    state[2] = (state[2] + c) | 0;
    state[3] = (state[3] + d) | 0;
    state[4] = (state[4] + e) | 0;
    state[5] = (state[5] + f) | 0;
    state[6] = (state[6] + g) | 0;
    state[7] = (state[7] + h) | 0;
  }

  function stateBytes(state) {
    const out = new Uint8Array(32);
    const view = new DataView(out.buffer);
    for (let i = 0; i < 8; i++) view.setUint32(i * 4, state[i] >>> 0);
    return out;
  }

  function sha256(data) {
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    const length = bytes.length;
    const padded = new Uint8Array(Math.ceil((length + 9) / 64) * 64);
    padded.set(bytes);
    padded[length] = 0x80;
    const view = new DataView(padded.buffer);
    view.setUint32(padded.length - 8, Math.floor(length / 0x20000000));
    view.setUint32(padded.length - 4, (length * 8) >>> 0);

    const state = Int32Array.from(IV);
    const w = new Int32Array(64);
    for (let offset = 0; offset < padded.length; offset += 64) {
      for (let i = 0; i < 16; i++) w[i] = view.getInt32(offset + i * 4);
      compress(state, w);
    }
    return stateBytes(state);
  }

  function concat(...parts) {
    const total = parts.reduce((sum, part) => sum + part.length, 0);
    const out = new Uint8Array(total);
    let offset = 0;
    parts.forEach(part => {
      out.set(part, offset);
      offset += part.length;
    });
    return out;
  }

  function hmacSha256(key, data) {
    let block = key instanceof Uint8Array ? key : new Uint8Array(key);
    if (block.length > 64) block = sha256(block);
    const inner = new Uint8Array(64);
    const outer = new Uint8Array(64);
    for (let i = 0; i < 64; i++) {
      const byte = i < block.length ? block[i] : 0;
      inner[i] = byte ^ 0x36;
      outer[i] = byte ^ 0x5c;
    }
    return sha256(concat(outer, sha256(concat(inner, data))));
  }

  function utf8(text) {
    return new TextEncoder().encode(String(text));
  }

  function toHex(bytes) {
    return Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
  }

  function fromHex(hex) {
    const text = String(hex || '');
    if (text.length % 2 || /[^0-9a-f]/i.test(text)) throw new Error('invalid hex');
    const out = new Uint8Array(text.length / 2);
    for (let i = 0; i < out.length; i++) out[i] = parseInt(text.substr(i * 2, 2), 16);
    return out;
  }

  function randomBytes(length) {
    const cryptoApi = window.crypto || window.msCrypto;
    if (!cryptoApi || typeof cryptoApi.getRandomValues !== 'function') {
      throw new Error('no random source');
    }
    return cryptoApi.getRandomValues(new Uint8Array(length));
  }

  function validIterations(iterations) {
    return Number.isInteger(iterations) && iterations >= MIN_ITERATIONS &&
      iterations <= MAX_ITERATIONS;
  }

  // PBKDF2-HMAC-SHA256 with one 32-byte output block. The HMAC inner and
  // outer states are computed once; each further iteration then costs two
  // compressions, because U(i-1) and the inner hash each fit one padded block.
  function pbkdf2Sha256(password, salt, iterations) {
    let key = password instanceof Uint8Array ? password : new Uint8Array(password);
    if (key.length > 64) key = sha256(key);
    const w = new Int32Array(64);
    const keyWords = new Int32Array(16);
    for (let i = 0; i < key.length; i++) keyWords[i >> 2] |= key[i] << (24 - 8 * (i & 3));
    const innerStart = Int32Array.from(IV);
    const outerStart = Int32Array.from(IV);
    for (let i = 0; i < 16; i++) w[i] = keyWords[i] ^ 0x36363636;
    compress(innerStart, w);
    for (let i = 0; i < 16; i++) w[i] = keyWords[i] ^ 0x5c5c5c5c;
    compress(outerStart, w);

    const first = hmacSha256(key, concat(salt, new Uint8Array([0, 0, 0, 1])));
    const firstView = new DataView(first.buffer, first.byteOffset, first.byteLength);
    const u = new Int32Array(8);
    for (let i = 0; i < 8; i++) u[i] = firstView.getInt32(i * 4);
    const result = Int32Array.from(u);
    const inner = new Int32Array(8);
    // Padding of a 32-byte message after the 64-byte key block: 768 bits.
    w[8] = 0x80000000 | 0;
    for (let i = 9; i < 15; i++) w[i] = 0;
    w[15] = 768;
    for (let n = 1; n < iterations; n++) {
      for (let i = 0; i < 8; i++) w[i] = u[i];
      inner.set(innerStart);
      compress(inner, w);
      for (let i = 0; i < 8; i++) w[i] = inner[i];
      u.set(outerStart);
      compress(u, w);
      for (let i = 0; i < 8; i++) result[i] ^= u[i];
    }
    return stateBytes(result);
  }

  function deriveKey(salt, password, iterations) {
    return pbkdf2Sha256(utf8(password), salt, iterations);
  }

  // Setting a password is followed by a login with the same password; the key
  // derived for it is reused once instead of running PBKDF2 a second time.
  let justSet = null;

  function csrfToken() {
    const meta = document.querySelector('meta[name="hometiles-csrf"]');
    return meta ? String(meta.getAttribute('content') || '') : '';
  }

  // Resolves to {ok, csrf} or {ok: false, status, error, retryAfter}.
  async function login(password) {
    const challengeResponse = await fetch('/api/auth/challenge',
      {cache: 'no-store', credentials: 'same-origin'});
    const challenge = await challengeResponse.json();
    const reuse = justSet;
    justSet = null;
    if (!challenge || challenge.enabled !== true) return {ok: true, disabled: true};
    const iterations = challenge.iter;
    if (!validIterations(iterations)) {
      return {ok: false, status: challengeResponse.status, error: 'iterations'};
    }
    const nonce = fromHex(challenge.nonce);
    const key = reuse && reuse.salt === challenge.salt &&
      reuse.iterations === iterations && reuse.password === password
      ? reuse.key
      : deriveKey(fromHex(challenge.salt), password, iterations);
    const proof = hmacSha256(key, nonce);
    const response = await fetch('/api/auth/login', {
      method: 'POST',
      credentials: 'same-origin',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({nonce: challenge.nonce, proof: toHex(proof)})
    });
    const result = await response.json().catch(() => ({}));
    if (!response.ok) {
      return {
        ok: false,
        status: response.status,
        error: String(result.error || ''),
        retryAfter: Number(result.retry_after || response.headers.get('Retry-After') || 0)
      };
    }
    const expected = toHex(hmacSha256(key, concat(utf8(SERVER_PROOF_LABEL), nonce, proof)));
    if (expected !== String(result.server_proof || '')) {
      return {ok: false, status: response.status, error: 'server_proof'};
    }
    return {ok: true, csrf: String(result.csrf || '')};
  }

  async function setPassword(password) {
    justSet = null;
    const salt = randomBytes(16);
    const iterations = DEFAULT_ITERATIONS;
    const key = deriveKey(salt, password, iterations);
    const response = await fetch('/api/auth/password', {
      method: 'POST',
      credentials: 'same-origin',
      headers: {
        'Content-Type': 'application/json',
        // Without a password there is no session token yet; any value marks
        // the request as coming from this page (see handleAuthPassword).
        'X-HomeTiles-CSRF': csrfToken() || 'setup'
      },
      body: JSON.stringify({salt: toHex(salt), iter: iterations, key: toHex(key)})
    });
    if (response.ok) justSet = {salt: toHex(salt), iterations, password, key};
    return response.ok;
  }

  async function removePassword() {
    const response = await fetch('/api/auth/password', {
      method: 'POST',
      credentials: 'same-origin',
      headers: {'Content-Type': 'application/json', 'X-HomeTiles-CSRF': csrfToken() || 'setup'},
      body: JSON.stringify({disable: true})
    });
    return response.ok;
  }

  async function logout() {
    await fetch('/api/auth/logout', {
      method: 'POST',
      credentials: 'same-origin',
      headers: {'X-HomeTiles-CSRF': csrfToken()}
    }).catch(() => {});
  }

  window.HomeTilesAuth = {
    sha256, hmacSha256, pbkdf2Sha256, toHex, fromHex, utf8, deriveKey, csrfToken,
    login, setPassword, removePassword, logout
  };

  function sameOrigin(url) {
    try {
      return new URL(url, window.location.href).origin === window.location.origin;
    } catch (error) {
      return false;
    }
  }

  let redirectingToLogin = false;
  function redirectToLogin() {
    if (redirectingToLogin) return;
    redirectingToLogin = true;
    window.location.replace('/');
  }

  // Admin page with an active password: every changing request carries the
  // session's CSRF token, and an expired session returns to the login page.
  const token = csrfToken();
  if (token) {
    const nativeFetch = window.fetch.bind(window);
    window.fetch = function (input, init) {
      const options = init ? {...init} : {};
      const url = input instanceof Request ? input.url : String(input);
      const method = String(options.method ||
        (input instanceof Request ? input.method : 'GET')).toUpperCase();
      const local = sameOrigin(url);
      if (local && method !== 'GET' && method !== 'HEAD') {
        const headers = new Headers(options.headers ||
          (input instanceof Request ? input.headers : undefined));
        if (!headers.has('X-HomeTiles-CSRF')) headers.set('X-HomeTiles-CSRF', token);
        options.headers = headers;
      }
      return nativeFetch(input, options).then(response => {
        const reason = response.headers.get('X-HomeTiles-Auth');
        if (local && (response.status === 401 || response.status === 403) &&
            (reason === 'required' || reason === 'csrf')) {
          redirectToLogin();
        }
        return response;
      });
    };

    const nativeOpen = XMLHttpRequest.prototype.open;
    const nativeSend = XMLHttpRequest.prototype.send;
    XMLHttpRequest.prototype.open = function (method, url, ...rest) {
      this.hometilesMethod = String(method || 'GET').toUpperCase();
      this.hometilesLocal = sameOrigin(url);
      return nativeOpen.call(this, method, url, ...rest);
    };
    XMLHttpRequest.prototype.send = function (body) {
      if (this.hometilesLocal && this.hometilesMethod !== 'GET' &&
          this.hometilesMethod !== 'HEAD') {
        this.setRequestHeader('X-HomeTiles-CSRF', token);
      }
      return nativeSend.call(this, body);
    };

    // A plain form post cannot carry a header. Forms nobody else handled
    // (the restart form) are sent with fetch instead, then the page reloads
    // as it did after the former redirect.
    document.addEventListener('submit', event => {
      const form = event.target;
      if (event.defaultPrevented || !(form instanceof HTMLFormElement)) return;
      if (String(form.method || '').toLowerCase() !== 'post') return;
      const action = form.getAttribute('action') || window.location.href;
      if (!sameOrigin(action)) return;
      event.preventDefault();
      window.fetch(action, {
        method: 'POST',
        credentials: 'same-origin',
        body: new URLSearchParams(new FormData(form))
      }).catch(() => {}).finally(() => {
        window.setTimeout(() => window.location.assign('/'), 300);
      });
    });
  }

  function formatText(template, value) {
    return String(template || '').replace('%s', String(value));
  }

  // Login page (rendered by getLoginPage() when a password is set).
  function initLoginForm() {
    const form = document.getElementById('ht_login_form');
    if (!form) return;
    const input = document.getElementById('ht_login_password');
    const button = document.getElementById('ht_login_submit');
    const message = document.getElementById('ht_login_message');
    const show = (text, error) => {
      if (!message) return;
      message.textContent = text;
      message.classList.toggle('is-error', !!error);
    };
    form.addEventListener('submit', async event => {
      event.preventDefault();
      if (!input || !input.value) return;
      if (button) button.disabled = true;
      show(form.dataset.checking, false);
      try {
        const result = await login(input.value);
        if (result.ok) {
          window.location.replace('/');
          return;
        }
        if (result.status === 429) {
          show(formatText(form.dataset.locked, result.retryAfter || 1), true);
        } else if (result.error === 'invalid_password' && result.retryAfter) {
          show(form.dataset.invalid + ' ' +
            formatText(form.dataset.locked, result.retryAfter), true);
        } else if (result.error === 'invalid_password') {
          show(form.dataset.invalid, true);
        } else {
          show(form.dataset.failed, true);
        }
      } catch (error) {
        show(form.dataset.failed, true);
      } finally {
        if (button) button.disabled = false;
        input.select();
      }
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initLoginForm);
  } else {
    initLoginForm();
  }
})();

# Encrypted Bridge commands and Web Admin login

Developer reference for the optional security features shared by the HomeTiles
firmware and the HomeTiles Bridge (Home Assistant integration `tab5_lvgl`). All
of them are off by default. A panel or Bridge without them, and every older
firmware or Bridge version, keeps the unencrypted protocol unchanged.

Implementation:

| Part | Firmware | Bridge |
| --- | --- | --- |
| Primitives | `src/core/security/ht_crypto.*` (portable SHA-256, HMAC, HKDF, ChaCha20-Poly1305) | `hashlib`, `hmac`, `cryptography` |
| Web Admin login | `src/web/server/auth/`, `src/web/assets/auth.js` | `panel_auth.py` |
| Command channel | `src/network/secure/command_channel_core.h` (pure), `command_channel.*` (MQTT, NVS, UI) | `command_channel.py` |

The host tests `tools/tests/core/test-ht-crypto.mjs`,
`tools/tests/network/test-command-channel-core.mjs` and the Bridge tests
`tests/test_command_channel.py` pin the same fixed vectors, so both sides fail
when either one drifts.

## Threat model

Anyone on the MQTT broker can read and publish every HomeTiles topic; anyone on
the LAN can reach the panel's Web Admin on port 80. There are no certificates:
trust comes from the user, who confirms the same six-digit number on the
display and in Home Assistant when pairing (numeric comparison, as in
Bluetooth LE Secure Connections).

Protected:

- Commands from the panel to the Bridge (lights, switches, scenes, media,
  climate, covers, cameras, editable values) are encrypted and authenticated.
  A paired Bridge executes only commands from the paired panel, each at most
  once.
- Stream tokens: the Bridge's reply that opens a camera stream for the panel
  and the Bridge's requests to stream or photograph the panel's built-in camera
  are encrypted and authenticated, so nobody on MQTT learns a token or points
  the panel's camera at another host.
- The Web Admin, when a password is set.

Not protected, by design: entity states, weather, history and energy replies,
and camera images stay unencrypted, as do Bridge-to-panel settings such as
brightness. MQTT broker credentials remain the first line of defence.

## Web Admin password

Stored on the panel: a 16-byte random salt, the iteration count `iter` and
`key = PBKDF2-HMAC-SHA256(UTF-8 password, salt, iter, 32 bytes)` (NVS
`tab5_config/web_auth`, checksummed record version 2). The password never
reaches the panel, and the panel never derives a key: the browser runs PBKDF2
in JavaScript, because WebCrypto is unavailable on `http://`, and the Bridge
uses `hashlib.pbkdf2_hmac`. The slow derivation makes a login sniffed on the
LAN expensive to brute-force offline. New passwords use 300,000 iterations.
Clients accept 10,000 to 1,000,000 and refuse any other challenge, so a fake
panel cannot stall them. A record of the earlier single SHA-256 scheme
(version 1, never released) keeps Web Admin locked until the password is
removed on the device and set again.

The first password can be set only within two minutes after tapping
Password under System > Security on the panel; without that tap the panel
answers `403 panel_tap_required`. Changing or removing a set password needs
the Web Admin session as before.

Login:

1. `GET /api/auth/challenge` →
   `{"enabled":true,"salt":"<32 hex>","nonce":"<64 hex>","iter":<integer>}`,
   or `{"enabled":false}` without a password. Older firmware answers 404.
2. `proof = HMAC-SHA256(key, nonce)`; `POST /api/auth/login`
   `{"nonce":"…","proof":"<64 hex>"}`.
3. The panel compares in constant time and consumes the nonce whatever the
   result (single use, 60 s lifetime, at most four outstanding).
4. Success: `Set-Cookie: ht_session=<32 hex>; Path=/; Max-Age=2592000; HttpOnly; SameSite=Lax`
   and `{"csrf":"<32 hex>","server_proof":"<64 hex>"}` with
   `server_proof = HMAC-SHA256(key, "HomeTiles-Web-Admin-server-v1" || nonce || proof)`.
   A client that knows the password verifies it to recognise the real panel.
5. Failure: 401 `invalid_password`. From the third consecutive failure the login
   is locked for 1 s, doubling up to 5 minutes (429 with `Retry-After`).

Every other page and endpoint then needs the session cookie; every request
other than GET/HEAD also needs the `X-HomeTiles-CSRF` header. Missing sessions
get 401 with `X-HomeTiles-Auth: required`, a wrong CSRF token 403 with
`X-HomeTiles-Auth: csrf`. Upload chunks of an unauthenticated request are
discarded before they reach a writer. Sessions last 30 days, also across
restarts: the panel keeps SHA-256(session id), the CSRF token and the
wall-clock expiry, and writes them to NVS on login, logout and password
changes only. A new password, **Sign out** or removing the password ends them.
At most four sessions exist; a login replaces the least recently used one.

`POST /api/auth/password` with
`{"salt":"<32 hex>","iter":<integer>,"key":"<64 hex>"}` sets or changes the
password (400 for an iteration count outside 10,000 to 1,000,000),
`{"disable":true}` removes it (session and CSRF required while one is set, the
CSRF header alone otherwise). The display removes it under Settings → System →
Security. While a password is set, stored Wi-Fi/MQTT passwords and PINs are
never sent to a browser.

The Bridge's pairing dialog accepts the password once to send `POST /mqtt` and
`/restart`; it verifies `server_proof` before it sends any MQTT credential and
never stores or logs the password.

Shared test vector (browser test `tools/tests/web/test-web-admin-auth-browser.mjs`,
Bridge `tests/test_panel_auth.py`): password `Pässwort-123` (UTF-8
`50c3a47373776f72742d313233`), salt 16 bytes `a1`, `iter` 100000, nonce 32
bytes `5c`:

- key `1ca04c9ba257bbc2be95d76f4ee7385ad79143f23050d4c5c56ec3e3fd5fddc0`
- proof `028c2df33691a772cb260751e39bcda9716901c5bb6650ac66970e4513fe5afe`
- server_proof `5f38e5560475a83b44fa1014b30cb16f703442a6aab247dcf692795a23875007`

## Command channel

### Pairing (contract v2)

The user starts pairing on the display (Settings → System → Security → Pair).
Panel and Bridge run an X25519 exchange (RFC 7748; mbedTLS on the panel,
`cryptography` in the Bridge) and both show a six-digit number; the user
confirms it on both sides. A commitment keeps either side from steering the
number, so an attacker in the middle has one chance in 10^6 per attempt, and
every attempt needs a tap on the display.

Messages are plain JSON on `{base}/pair/panel` (panel → Bridge) and
`{base}/pair/bridge` (Bridge → panel), QoS 0, never retained. `v` must be 2,
bytes are lowercase hex of exact length, `id` is 16 hex (new per attempt), and
unknown fields are ignored. Each side uses a fresh key pair and fresh nonces
per attempt.

1. Panel: `{"v":2,"t":"start","id":…,"pk":<pk_p>}`
2. Bridge: `{"v":2,"t":"commit","id":…,"pk":<pk_b>,"c":<c>}` with
   `c = SHA-256("HomeTiles pairing commit v2" || pk_b || pk_p || n_b)` and
   `n_b` 16 random bytes
3. Panel: `{"v":2,"t":"nonce","id":…,"n":<n_p>}`
4. Bridge: `{"v":2,"t":"nonce","id":…,"n":<n_b>}`; the panel checks that `n_b`
   opens `c`

Then both compute:

```text
s      = X25519(own private key, peer public key)   (all-zero is refused)
T      = SHA-256("HomeTiles pairing v2" || u16be(len(base)) || base
                 || pk_p || pk_b || n_p || n_b)
number = uint32_be(SHA-256("HomeTiles pairing number v2" || T)[0..4]) mod 10^6
K      = HKDF-SHA256(salt = T, ikm = s, info = "pairing key", 32 bytes)
```

`base` is the UTF-8 base topic of `{base}/pair/panel`, so a start cannot be
answered for another panel. The number is always shown with six digits,
grouped as `061 806`.

5. After the user confirmed locally, each side sends
   `{"v":2,"t":"confirm","id":…,"m":<m>}` with
   `m = HMAC-SHA256(K, "confirm panel" || T)` (panel) or
   `"confirm bridge" || T` (Bridge) and repeats it every 2 s until the other
   side's valid `m` arrives. A side is paired only with its own confirmation
   and the other side's valid `m`; it then stores `K`. A finished side answers
   a repeated valid confirm of the same id until 120 s after the start.
6. `{"v":2,"t":"abort","id":…,"r":<reason>}` ends a running attempt on cancel,
   rejection or timeout. It is unauthenticated, only for display (`busy`,
   `rate`, `paired`, `rejected`, `timeout`, `cancel`; anything else is a
   general abort) and never undoes a finished pairing.

Rules: one attempt per panel at a time, at most 120 s. The panel takes only
the first commit of an attempt and ignores a nonce that does not open it (a
second Bridge on the broker then cannot break the attempt); the Bridge takes
only the first `n_p`. `c` and `m` are compared in constant time. The Bridge
answers at most three starts per panel in ten minutes, at least 10 s apart
(`abort` `rate`), never replaces a running attempt (`busy`) and refuses a start
while it is paired with that panel, also while removing (`paired`, no prompt
in Home Assistant). Re-pairing therefore needs Unpair on the display or Remove
pairing in Home Assistant first. The panel shows "Update the HomeTiles
Bridge" only when nothing at all arrives within 15 s, and runs the X25519
vector of RFC 7748 section 6.1 before every attempt.

### Keys

Keys use HKDF-SHA256 (RFC 5869) with salt `HomeTiles command pairing v2`, the
32-byte `K` as input key material, and these `info` labels:

| Label | Length | Use |
| --- | --- | --- |
| `panel-to-bridge` | 32 | Messages from the panel |
| `bridge-to-panel` | 32 | Messages from the Bridge |
| `announce` | 32 | HMAC key of the signed announcement |
| `key-id` | 8 | Public key identifier, lowercase hex |

Separate keys per direction make a reflected message undecryptable. The panel
stores `K` in NVS `tab5_config/cmd_pairing` (record v2); the Bridge stores it in
its config entry. Neither side logs `K`, a key, a private key, a nonce or the
number; the key id is public.

Shared pairing vector (base `hometiles/test`, private keys as 32 raw bytes
`a1`×32 and `b2`×32, `n_p` = `c3`×16, `n_b` = `d4`×16):

- `pk_p` `c306fb0ef2bf8b7f93bad98155fa37daec74db0c4cbeda6c6f1dba9d36558252`
- `pk_b` `db48257e1237976a74ad8cfedca00213408fe89ac6251f1b930245f242b5c31a`
- `c` `3262f80a0dcdf8b757e44596ed47c05afdc9a16405a915c710ab33443f8af112`
- `s` `9502af7a4b678841b839429623a09a23f6cc551836e48a52c0e4faf4b9d3b06e`
- `T` `2d4cd2f1d56b383880cc9e27ec65419c1ee1bf1df99bbe5dd115e65d3c613e9f`
- number `061 806`
- `K` `b925def556256ead767b0f1d14879e50d6bddbd0bb44dc0d2435e4af011b3e19`
- `m` panel `a2bbe3db083e9884b39df9d41eac55ed94b652e364c636157423f773bc35516b`
- `m` Bridge `ce6193e03597bf02204ee8dd3a1f05d38675088c497a6f5ae35718acf78ef456`
- `panel-to-bridge` `3ce896914535a84f25b6bcbb18bae3e2e0bbdefa5b03712b2fbaf16e51d084de`
- `bridge-to-panel` `5631cdca5a6fbae0a0fdfc738926f75254f40065c9e64322430aba78be18278d`
- `announce` `318f1b1153aed588afc39a727b1f7a56659c9104b8f4d2eac4b8ee08eb71563e`
- `key-id` `20a8108ed11215c5`
- `hello - 0 00112233445566778899aabbccddeeff\n` sealed with `panel-to-bridge`
  on `hometiles/test/secure/panel`, nonce `000102030405060708090a0b`:
  `d` = `0cadb7a053444eb47a4fe832c2666d6aabeb102ffe25b8aa6dfbeb29b0206815420dad833e0db0c4023b193d8e4881cacf43b23f7beced790a8ef3`

### Topics

| Topic | Direction | Content |
| --- | --- | --- |
| `{base}/secure/panel` | panel → Bridge | Sealed `hello`, `cmd` and `unpair` messages, QoS 0, not retained |
| `{base}/secure/bridge` | Bridge → panel | Sealed `session`, `rekey`, `data` and `unpair` messages, QoS 0, not retained |
| `{base}/stat/secure` | panel → Bridge | Retained plain status `{"v":1,"state":"active","kid":"<16 hex>"}` once `K` is stored; empty when off, also during pairing |
| `{base}/pair/panel` | panel → Bridge | Plain pairing messages `start`, `nonce`, `confirm`, `abort` |
| `{base}/pair/bridge` | Bridge → panel | Plain pairing messages `commit`, `nonce`, `confirm`, `abort` |

The status only tells the Bridge which key the panel holds; it grants nothing.

### Envelope

```json
{"v":1,"k":"<key id, 16 hex>","n":"<nonce, 24 hex>","d":"<hex of ciphertext || tag>"}
```

AEAD: ChaCha20-Poly1305 (RFC 8439), key of the sending direction, a fresh
random 96-bit nonce per message, and the exact MQTT topic (UTF-8) as additional
authenticated data, so a message cannot be moved to another panel's topic.

Plaintext:

```text
<type> <session: 32 hex | -> <seq: decimal uint32> <name | ->\n<body>
```

`name` is 1–32 characters of `[a-z0-9_]`; the body is at most 2048 bytes.

| Type | Direction | Session | Seq | Name | Body |
| --- | --- | --- | --- | --- | --- |
| `hello` | panel → Bridge | `-` | 0 | fresh 32-hex challenge | empty |
| `session` | Bridge → panel | new random session id | 0 | the challenge it answers | empty |
| `rekey` | Bridge → panel | `-` | 0 | `-` | empty |
| `cmd` | panel → Bridge | current session | 1, 2, … | `scene`, `light`, `switch`, `media`, `climate`, `cover`, `camera`, `value`, `fan`, `lock`, `alarm`, `entities`, `tiles` | the unchanged plain command payload |
| `data` | Bridge → panel | current session | 1, 2, … | `camera`, `local_camera` or `entities` | the unchanged plain payload of `{base}/stat/camera` or `{base}/cmnd/local_camera`, or a search answer part |
| `unpair` | both | current session | next number of the sender (continues `cmd`/`data`) | `-` | empty |

### Session and replay protection

1. After every MQTT connect, when it has no session, and after a `rekey`, the
   panel sends `hello` with a new random challenge (at most every 3 s; without
   an answer after 10 s, 30 s, 60 s and then every 5 minutes).
2. The Bridge answers with `session`: a new random session id bound to the
   challenge. The panel accepts it only for its current challenge, so an old
   recorded `session` message is useless. The Bridge creates at most one
   session per 2 s per panel.
3. Each direction numbers its `cmd`/`data`/`unpair` messages from 1 within the session.
   The receiver keeps the highest number and a 64-message window (reordering
   between the panel's normal and priority MQTT lanes) and accepts every number
   once. A replayed message is dropped; a message from an older session never
   matches.
4. A Bridge that receives a command for an unknown session, or must send
   `data` without a session, sends `rekey` (at most every 5 s). It also sends
   `rekey` after its own start and when the retained status shows its key id
   while it has no session, in each case only once it is subscribed to
   `{base}/secure/panel`. A `rekey` restarts the panel's hello backoff, so a
   hello lost during a Bridge restart is repeated after 10 s.

The panel holds at most one command for up to 5 s while it waits for a session
and then sends it sealed; after that it drops it.

### Activation and compatibility

- **Off** (default): nothing is published or subscribed except an empty
  retained `{base}/stat/secure` after a pairing was removed. A pairing attempt
  subscribes to `{base}/pair/bridge` only while it runs; commands stay plain.
- **Active**: once both sides confirmed, the panel stores `K`, sends `hello`
  and from then on sends every command sealed only. It also ignores plain
  `{base}/stat/camera` and `{base}/cmnd/local_camera`.
- A Bridge with a stored key ignores the plain command topics of that panel
  and sends camera replies and built-in camera requests only sealed. It keeps
  doing so if the panel later reports `off` or another key id (an attacker
  could forge that status) and asks the user, with a persistent notification,
  to remove the pairing under Configure → Security.

### Removing the pairing

Removing the pairing on one side removes it on the other side too, with an
`unpair` message in the current session. It is authenticated and numbered
like `cmd`/`data`, so it cannot be forged or replayed; the unauthenticated
status never turns anything off.

- **On the display** (Settings → System → Security → Unpair, after a
  confirmation): the panel sends `unpair` while it still has the keys, then
  deletes `K`, publishes the empty retained status and the unsigned
  announcement. On a valid `unpair` the Bridge deletes its key, reloads the
  entry unpaired and shows a notification. Without a session the panel still
  turns off and tells the user to remove the pairing in Home Assistant as well.
- **In the Bridge** (Configure → Security → Remove pairing): the entry keeps the
  key in a removing state, accepts plain commands again, runs no sealed ones,
  keeps `{base}/secure/panel` subscribed and sends `rekey`. It answers the
  next `hello` with `session` followed by `unpair` (its number 1), so an
  offline panel is turned off after its next connect. On a valid `unpair` the
  panel turns off exactly as above. The Bridge drops the key once the
  retained status is empty or shows another key id.

Shared vectors (`K` of the pairing vector, key id `20a8108ed11215c5`, nonce
`000102030405060708090a0b`, plaintext
`unpair 0123456789abcdef0123456789abcdef 1 -\n`):

- panel → Bridge on `hometiles/secure/panel`: `d` =
  `11a6abad551643a47b5deb36c6616860a1b94678af75e8ac6bfee025bc2f3e4c190eac833e0cb381557d3e5bda1967f7f79b5700e0ab459406d83fef`
- Bridge → panel on `hometiles/secure/bridge`: `d` =
  `2df48e85bda4d37632843fde4a78d178089f741c47e3291ce1d451a60cc7473dd4676251129cc80d9219d7433ce992181d00bc712a5ce6e994abb98f`

Old firmware never sends `start`, so its Bridge entry stays unpaired. Old
Bridges never answer `start`; the panel then shows that the Bridge needs an
update and keeps sending plain commands. The Bridge betas b1/b2 and the
matching firmware betas used a typed 25-symbol code (contract v1): both sides
discard a stored v1 code at start with a log line and run unencrypted until
the panel is paired again.

## Entity search

A paired panel whose Bridge announces `"entity_search": 1` in its
configuration lets the Web Admin entity picker search Home Assistant through
the Bridge. Both messages travel only sealed (`SEALED_ONLY_COMMANDS`):

- `entities` (panel → Bridge): `{"id":n,"list":"<picker list>","q":"<query>",
  "web_auth":true|false}`. The Bridge answers every entity of the list's
  domains only when the command is sealed, the pairing is not being removed
  and `web_auth` is true, the same rule as Lock and Alarm. Otherwise it
  answers only the entities released for the panel. The answer comes back as
  sealed `entities` data in at most six parts of up to 1,900 bytes
  (`{"id","p","n","full","more","r":[{v,t,i,a,d}]}`); the panel joins
  them and keeps only the newest search.
- `tiles` (panel → Bridge): `{"lists":{"<list>":["entity_id",…]},"web_auth":…}` lists
  the tiles' entities beyond the released lists. With the same rule the
  Bridge adds them to that panel's tracked entities in memory; it validates
  each domain against the list and keeps at most 200. The panel sends the
  report again on every new session and whenever tiles, the Bridge lists or
  the password change.

A plain (unpaired) panel never searches; its picker shows the released
entities.

## Announcements, discovery and history requests

The retained announcement on `tab5_lvgl/config/{id}/bridge` tells the Bridge
a panel's base topic, entity selections, local I/O and capabilities. Anyone on
the broker can publish there too, so the Bridge applies these rules:

- The `{id}` in the topic must equal the announced `device_id` (current
  firmware: 12 upper-case hex digits of the MAC); payloads over 64 KiB are
  dropped.
- An announcement for an existing entry must carry that entry's base topic;
  otherwise it cannot change the entry's entities or selections.
- While the panel is paired, it signs the announcement and republishes it
  whenever a pairing is completed or removed:

  ```text
  sig = HMAC-SHA256(announce key, topic "\n" unsigned payload)
  signed payload = unsigned payload without its final "}" + ,"sig":"<64 hex>"}
  ```

  The Bridge verifies the exact received bytes, so no JSON re-serialisation is
  involved. An entry with a stored key accepts only announcements with a
  valid signature. An old signed announcement can be replayed, but it only
  repeats what the panel itself once announced.
- A new panel never creates an entry by itself: it gets a discovery card. At
  most three cards wait at a time and at most five new panels get a card per
  ten minutes. Linking a panel to an existing entry that has no panel yet
  (manually added, or from firmware before v0.3.1) also waits for the user's
  confirmation.

History requests (`tab5_lvgl/config/{id}/history/request`) are answered only
for configured entities, never when retained, at most two at a time and 30
per minute per panel; further requests wait in line (at most 32) instead of
being dropped. The numeric graph uses the Recorder statistics for buckets of
5 minutes or more when the sensor has them; otherwise it reads state changes
in pages of 5,000 rows, newest time range first, at most 60,480 rows (one
change per 10 s for a week); beyond that the oldest buckets stay empty. State,
binary and editable histories keep their existing paged limit of 8,192
changes.

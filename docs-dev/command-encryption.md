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
trust comes from a secret the user carries from the display to Home Assistant,
as with the ESPHome API encryption key.

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

Login:

1. `GET /api/auth/challenge` →
   `{"enabled":true,"salt":"<32 hex>","nonce":"<64 hex>","iter":<integer>}`,
   or `{"enabled":false}` without a password. Older firmware answers 404.
2. `proof = HMAC-SHA256(key, nonce)`; `POST /api/auth/login`
   `{"nonce":"…","proof":"<64 hex>"}`.
3. The panel compares in constant time and consumes the nonce whatever the
   result (single use, 60 s lifetime, at most four outstanding).
4. Success: `Set-Cookie: ht_session=<32 hex>; Path=/; HttpOnly; SameSite=Strict`
   and `{"csrf":"<32 hex>","server_proof":"<64 hex>"}` with
   `server_proof = HMAC-SHA256(key, "HomeTiles-Web-Admin-server-v1" || nonce || proof)`.
   A client that knows the password verifies it to recognise the real panel.
5. Failure: 401 `invalid_password`. From the third consecutive failure the login
   is locked for 1 s, doubling up to 5 minutes (429 with `Retry-After`).

Every other page and endpoint then needs the session cookie; every request
other than GET/HEAD also needs the `X-HomeTiles-CSRF` header. Missing sessions
get 401 with `X-HomeTiles-Auth: required`, a wrong CSRF token 403 with
`X-HomeTiles-Auth: csrf`. Upload chunks of an unauthenticated request are
discarded before they reach a writer. Sessions end after 1 h idle or 12 h,
with a new password, or with a reboot (they live in PSRAM only).

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

### Pairing code and keys

The display creates the code (Settings → System → Security → Set up
encryption): 25 symbols of the Crockford Base32 alphabet
`0123456789ABCDEFGHJKMNPQRSTVWXYZ`, 125 random bits, shown as
`XXXXX-XXXXX-XXXXX-XXXXX-XXXXX`. Input is case-insensitive, ignores spaces and
dashes and maps `O`→`0`, `I`/`L`→`1`.

Keys use HKDF-SHA256 (RFC 5869) with salt `HomeTiles command pairing v1`, the
25 canonical ASCII symbols as input key material, and these `info` labels:

| Label | Length | Use |
| --- | --- | --- |
| `panel-to-bridge` | 32 | Messages from the panel |
| `bridge-to-panel` | 32 | Messages from the Bridge |
| `announce` | 32 | HMAC key of the signed announcement |
| `key-id` | 8 | Public key identifier, lowercase hex |

Separate keys per direction make a reflected message undecryptable. The panel
stores the code (to show it again) and the state in NVS `tab5_config/cmd_pairing`;
the Bridge stores the code in its config entry (`command_pairing_code`, like an
ESPHome encryption key). Neither side logs the code or a key; the key id is
public.

### Topics

| Topic | Direction | Content |
| --- | --- | --- |
| `{base}/secure/panel` | panel → Bridge | Sealed `hello`, `cmd` and `unpair` messages, QoS 0, not retained |
| `{base}/secure/bridge` | Bridge → panel | Sealed `session`, `rekey`, `data` and `unpair` messages, QoS 0, not retained |
| `{base}/stat/secure` | panel → Bridge | Retained plain status `{"v":1,"state":"pending"\|"active","kid":"<16 hex>"}`; empty when off |

The status only helps the Bridge check an entered code; it grants nothing.

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
| `cmd` | panel → Bridge | current session | 1, 2, … | `scene`, `light`, `switch`, `media`, `climate`, `cover`, `camera`, `value` | the unchanged plain command payload |
| `data` | Bridge → panel | current session | 1, 2, … | `camera` or `local_camera` | the unchanged plain payload of `{base}/stat/camera` or `{base}/cmnd/local_camera` |
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
  retained `{base}/stat/secure` after a pairing was removed.
- **Pending**: the code exists on the panel; commands stay plain; the panel
  requests a session. Entering the code in the Bridge (Configure → Security)
  checks it against the retained key id, stores it and sends `rekey`.
- **Active**: after the first valid `session` the panel stores the state and
  from then on sends every command sealed only. It also ignores plain
  `{base}/stat/camera` and `{base}/cmnd/local_camera`.
- A Bridge with a stored code ignores the plain command topics of that panel
  and sends camera replies and built-in camera requests only sealed. It keeps
  doing so if the panel later reports `off` or another key id (an attacker
  could forge that status) and asks the user, with a persistent notification,
  to remove the pairing under Configure → Security.

### Removing the pairing

Removing the pairing on one side removes it on the other side too, with an
`unpair` message in the current session. It is authenticated and numbered
like `cmd`/`data`, so it cannot be forged or replayed; the unauthenticated
status never turns anything off.

- **On the display** (Settings → System → Security → Turn off): the panel
  sends `unpair` while it still has the keys, then deletes the code, publishes
  the empty retained status and the unsigned announcement. On a valid `unpair`
  the Bridge deletes its code, reloads the entry unpaired and shows a
  notification. Without a session the panel still turns off and tells the
  user to remove the code in the Bridge as well.
- **In the Bridge** (Configure → Security → Remove pairing): the entry keeps the
  key in a removing state, accepts plain commands again, runs no sealed ones,
  keeps `{base}/secure/panel` subscribed and sends `rekey`. It answers the
  next `hello` with `session` followed by `unpair` (its number 1), so an
  offline panel is turned off after its next connect. On a valid `unpair` the
  panel turns off exactly as above. The Bridge drops the key once the
  retained status is empty or shows another key id.

Shared vectors (code `ABCDE-FGHJK-MNPQR-STVWX-YZ012`, key id
`8982fb24a78d94e1`, nonce `000102030405060708090a0b`, plaintext
`unpair 0123456789abcdef0123456789abcdef 1 -\n`):

- panel → Bridge on `hometiles/secure/panel`: `d` =
  `57bf95048efe6d21a8392023d835128bca899f957700cb026d090ecb5c811a72707dfb8b90c495763ea5ab124daac7347b8d1d772ea4ace283bbb02a`
- Bridge → panel on `hometiles/secure/bridge`: `d` =
  `f0304f27341f5bebe38670e0476e55fefa34e2b11a21a4224f5e66be62a63afaa85712b16a9618140eb3fecc7d8cb5b0b9332900c520448e4f1e7977`

Old firmware never shows a code, so its Bridge entry cannot be paired. Old
Bridges never answer `hello`, so a new panel stays pending and keeps sending
plain commands.

## Announcements, discovery and history requests

The retained announcement on `tab5_lvgl/config/{id}/bridge` tells the Bridge
a panel's base topic, entity selections, local I/O and capabilities. Anyone on
the broker can publish there too, so the Bridge applies these rules:

- The `{id}` in the topic must equal the announced `device_id` (current
  firmware: 12 upper-case hex digits of the MAC); payloads over 64 KiB are
  dropped.
- An announcement for an existing entry must carry that entry's base topic;
  otherwise it cannot change the entry's entities or selections.
- While a pairing code exists (pending or active), the panel signs the
  announcement and republishes it whenever the code is created or removed:

  ```text
  sig = HMAC-SHA256(announce key, topic "\n" unsigned payload)
  signed payload = unsigned payload without its final "}" + ,"sig":"<64 hex>"}
  ```

  The Bridge verifies the exact received bytes, so no JSON re-serialisation is
  involved. An entry with a stored code accepts only announcements with a
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

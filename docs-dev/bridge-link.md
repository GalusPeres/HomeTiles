# Direct Bridge link

Developer reference for the direct, encrypted connection between a panel and
the HomeTiles Bridge (Home Assistant integration `tab5_lvgl`). It replaces the
MQTT broker: the panel connects to the Bridge over TCP, and the Bridge acts as
the message broker for its panels. Panels set up over MQTT keep working
unchanged until they are moved to the link.

The link keeps the MQTT model on purpose: the same topic names, publish,
subscribe and retained messages. Application code on both sides (tiles,
popups, history, energy, cameras, pairing and the sealed command channel of
`command-encryption.md`) therefore stays as it is; only the transport below it
changes.

| Part | Firmware | Bridge |
| --- | --- | --- |
| Wire format, handshake | `src/network/link/bridge_link_core.h` (pure) | `link_protocol.py` (pure) |
| Client / server | `src/network/link/bridge_link_client.*` | `link_server.py` |
| Broker and MQTT shim | none (the MQTT worker drives the client) | `link_broker.py`, `link_mqtt.py` |
| Settings | `src/network/link/link_config.*` (NVS `tab5_config/bridge_link`) | config entry (`transport: link`) |

## Setup

1. The user presses **Pair** on the panel (Settings > System > Security).
   A panel without a Bridge address becomes discoverable for two minutes:
   it advertises `_hometiles._tcp` over mDNS with the TXT records `link=1`
   and `pair=1`, also when it is still set up over MQTT. Firmware with link
   support always adds `link=1`; without `pair=1` the Bridge offers nothing.
   A running advertisement only replaces its TXT set, which mDNS announces
   at once; restarting it took seconds, and a second change within them
   (Unpair, then Pair at once) never reached Home Assistant. The TXT record
   `seq` (8 hex digits, random start per boot) changes with every update:
   Home Assistant's mDNS cache keeps a replaced record for up to 10 seconds
   and ignores a record equal to one it still holds, so pair on, off and on
   again would otherwise be missed. The Bridge ignores `seq`.
2. Home Assistant shows "Panel found" for a new panel, or "Switch to the
   direct connection" for a panel that already has an entry (over MQTT). The
   user clicks **Add** or **Submit**; no password is asked.
3. The Bridge sends `POST /api/link` to the panel: `host`, `port` (the
   Bridge's link server), `base` (base topic; an existing entry keeps its
   own) and `ha_prefix`. The panel accepts it only while the two minutes
   run, which replaces the Web Admin password: whoever pressed Pair stands at
   the panel. Otherwise it answers 403. The panel stores the address, drops
   an older pairing key and the MQTT broker host, and switches its network
   worker to the link without a restart. Only another base topic or prefix
   (a name clash with another panel) restarts it, because the topics are
   built at start.
4. The panel connects in pair mode, starts pairing v2
   (`command-encryption.md`) and opens System > Security with the number.
   The pairing messages travel as publishes on `{base}/pair/panel` and
   `{base}/pair/bridge`; their content is unchanged.
5. The same Home Assistant dialog shows the six-digit number. The user
   confirms it there and on the panel. Both sides store the pairing key `K`.
   The Bridge creates the entry, or switches the existing one to the link
   (`transport: link`, new key; area, names and entities stay), and the panel
   reconnects in session mode.

Removing the pairing (on the display, or in Home Assistant, which sends an
unpair) also forgets the Bridge address: without its key the link cannot
connect. The panel leaves the link without a restart and is a new panel
again; **Pair** sets it up as above, and an existing entry is kept. When the
Bridge refuses the stored key three times in a row (`unknown`: the entry was
deleted in Home Assistant), the panel does the same by itself. A single
refusal can be the moment between pairing and the new entry, so the Bridge
also accepts the key of a setup dialog that has just paired.

Once an entry uses the link, the Bridge neither publishes nor subscribes its
topics over MQTT any more: the broker may still hold the panel's old retained
messages (for example `0` on `{base}/stat/connected` from the moment it left
MQTT), and they must not override the live state from the link.

Leftovers are cleared from both sides. A panel that switches from MQTT to the
link deletes its retained announcement (`tab5_lvgl/config/{id}/bridge`),
`{base}/stat/connected` and `{base}/stat/ip` on the broker before a clean
disconnect, and drops the broker host, user and password. A panel that left
MQTT without this (reset, reflashed, older test firmware) cannot clear
anything, so the Bridge does: a linked entry deletes the announcement on MQTT
when it is set up, and a panel that announces itself over mDNS with `link=1`
and without `pair=1` has neither MQTT nor the link, so an MQTT announcement of
it from before the start is left over. It opens no card (the mDNS cache is
checked, whichever discovery Home Assistant runs first) and is deleted. A
live announcement means the panel is on MQTT again and still opens a card.

## Wire format

Every frame is a 4-byte big-endian length `L` followed by `L` bytes. `L` is at
most 70,000 (`kMaxFrameLength`); a larger length closes the connection.

Before the session keys exist (handshake and pair mode) the body is
`type (1 byte) || payload`. In session mode the body is
`ChaCha20-Poly1305(type || payload) || tag (16 bytes)` with the key of the
sending direction, the nonce `00 00 00 00 || u64be(counter)` (each direction
counts its sealed frames from 0) and the 4 length bytes as additional data.
A frame that does not open closes the connection. TCP keeps the order, so
the counter also rejects replayed, dropped or reordered frames.

| Type | Name | Direction | Payload |
| --- | --- | --- | --- |
| `0x01` | hello | panel → Bridge | JSON, see below |
| `0x02` | welcome | Bridge → panel | JSON `{"v":1,"n":"<32 hex>"}` (session) or `{"v":1}` (pair) |
| `0x03` | refuse | Bridge → panel | JSON `{"v":1,"r":"<reason>"}`, then the Bridge closes |
| `0x04` | ready | both, sealed | empty; the first sealed frame of each side |
| `0x10` | publish | both | `flags (1) || u16be(topic length) || topic || payload`; flag bit 0 = retain |
| `0x11` | subscribe | panel → Bridge | topic |
| `0x12` | unsubscribe | panel → Bridge | topic |
| `0x13` | publish begin | panel → Bridge | `flags (1) || u16be(topic length) || topic || u32be(total length)` |
| `0x14` | publish data | panel → Bridge | the next bytes of the streamed payload |
| `0x15` | publish end | panel → Bridge | empty |
| `0x20` | ping | both | empty |
| `0x21` | pong | both | empty |

Topics are 1–255 bytes of UTF-8 without NUL, `+` or `#`. A publish payload is
at most 65,535 bytes. Payloads above that (the built-in camera still image)
use publish begin/data/end with at most 2 MiB in total; data frames carry at
most 8,192 bytes each.

## Handshake

The panel sends hello as the first frame:

```json
{"v":1,"id":"<device id>","base":"<base topic>","mode":"session","kid":"<16 hex>","n":"<32 hex>"}
```

`mode` is `session` (paired: `kid` is the key id of `K`, `n` 16 fresh random
bytes) or `pair` (no `kid`, no `n`).

Session mode: the Bridge looks up the entry with this device id whose pairing
key (or key being removed) has this key id and the same base topic. Otherwise
it refuses (`unknown`, `base`). It answers welcome with its own 16 random
bytes `n_b`, and both sides derive:

```text
salt = SHA-256("HomeTiles link v1" || u16be(len(id)) || id
               || u16be(len(base)) || base || n_p || n_b)
panel key  = HKDF-SHA256(salt, K, "HomeTiles link panel-to-bridge v1", 32)
bridge key = HKDF-SHA256(salt, K, "HomeTiles link bridge-to-panel v1", 32)
```

Each side then sends ready, sealed with its counter 0. A side that receives
a valid ready knows that the other side holds `K`; nobody without `K` can
read or write a frame. Fresh `n_p` and `n_b` give every connection new keys,
so recorded frames are useless on a later connection.

Pair mode: the Bridge accepts the connection only while a setup dialog waits
for this device id or an entry with this device id exists. It answers
welcome `{"v":1}` without keys. In pair mode the panel may only publish
`{base}/pair/panel` and subscribe `{base}/pair/bridge`; both ends drop
everything else. The panel connects in pair mode only while a pairing runs
and leaves it when the pairing ends.

A newer connection of the same panel replaces an older one (usually a
half-open socket after a Wi-Fi drop); only the newer one keeps its session.

Refuse reasons: `version`, `unknown` (no matching pairing), `base`
(another base topic), `pair` (nothing waits for a pairing), `invalid`
(malformed hello). The panel logs the reason and retries with its normal
backoff (6 to 96 seconds).

## Broker rules

- A subscribe delivers the stored retained message of the topic, flagged as
  retained, then every later publish (not flagged).
- A retained publish replaces the stored message of its topic; an empty
  retained payload deletes it. The Bridge keeps retained messages in memory
  only; after a restart it publishes its states again.
- A session-mode panel may publish under `{base}/` and
  `tab5_lvgl/config/{id}/`, and subscribe under these and `{ha_prefix}/`.
  Publishes under `homeassistant/` (old discovery clean-up) are dropped
  silently; anything else is dropped with a rate-limited log line.
- When a session ends for any reason the Bridge publishes `0` retained on
  `{base}/stat/connected`, like the MQTT last will.
- Bridge code publishes and subscribes through `link_mqtt.py`, which has the
  calls of `homeassistant.components.mqtt` it uses. It delivers to link
  panels and, while at least one entry still uses MQTT, to the MQTT broker as
  well.

## Keepalive and limits

The panel sends ping when it has sent nothing for 15 seconds; the Bridge
answers pong. Either side closes the connection after 45 seconds without any
received frame. The handshake must finish within 10 seconds.

The Bridge listens on TCP port 8140 (8141–8147 when that is taken) on all
interfaces. Home Assistant in Docker without host networking must publish
this port, as it already must for the camera ports 8124–8131.

## Firmware integration

The link client has the calls of `PubSubClient` that the MQTT worker uses
(`connect`, `loop`, `publish`, `subscribe`, `beginPublish`/`write`/
`endPublish`, ...). The worker keeps its single-owner model, outbound lanes,
DMA reserve, backoff and Wi-Fi wedge checks; it only picks the link client
instead of `PubSubClient` when a link is configured. Receive buffers live in
PSRAM. The sealed command channel keeps running inside the link.

## Open points

- A new IP address of Home Assistant needs a new setup; the panel does not
  search for the Bridge yet.
- Panels set up over MQTT move to the link in a later step.

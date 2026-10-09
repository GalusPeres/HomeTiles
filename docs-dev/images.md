# Pictures from the Bridge

Developer reference for pictures the HomeTiles Bridge renders for linked
panels: media covers and the screensaver picture (issue #69) today; picture
tiles (webcams, vacuum maps) and the rain radar later. Only panels on the
direct link (`bridge-link.md`) get them; panels on MQTT keep the cover
embedded in the media state and the screensaver on its SD slideshow.

| Part | Firmware | Bridge |
| --- | --- | --- |
| Header and topic | `src/network/bridge/bridge_images_core.h` (pure) | `images.py` |
| Store and display | `src/network/bridge/bridge_images.*`, `tile_renderer.cpp`, `image_screensaver.cpp` | `images.py`, `media_artwork.py`, `__init__.py` (sources) |
| Transport | link streams (`bridge-link.md`, `rx`) | `link_server.py` |

## Topics

`<ha_prefix>/<domain>/<object_id>/image/<w>x<h>` with the domain
`media_player`, `image` or `camera`, `w` and `h` 16 to 1,280 without leading
zeros. A panel subscribes to the picture it shows in the size it shows it; the
Bridge renders a picture only while at least one linked panel subscribes to
its topic and the Bridge serves that entity to the panel. Nobody else gets a
render. Served are a player in the panel's media players, an image in its
images, a camera in its images or cameras (released or declared). A
subscription that arrives before the entity is served is kept: the panel
subscribes a new choice at once and declares it 1.5 s later; every change of
the served lists checks the kept subscriptions again (`recheck`), and one no
longer served stops. Each topic logs one line per outcome (sent, waits,
could not load or render).

A Media tile subscribes to its player's cover in the size the Media popup
shows it (`popup_layout::scale(240)`: 240 on 1280x800 panels, 200 on
1024x600, 160 on 480x480); a tile shows at most that much of it.

The screensaver, set to "Picture from Home Assistant" in the Web Admin
(`picture_source` `"ha"`, `picture_entity` in its `config_v2.json`), subscribes
to its image or camera entity in the screen's size as shown (`1280x800`,
upright `800x1280`). An image entity stays subscribed while it is the source,
so a new picture is decoded ahead and the screensaver opens with it. A camera
is subscribed only while the screensaver shows it on an awake display, and
again after a reconnect; the panel declares the entity in its `images` list
(`entity_search`). The screensaver needs no microSD card for it.

## Message

```text
HTIMG1 <key> <w>x<h>\n<JPEG>
```

- `key`: 16 lowercase hex digits naming the picture (below).
- The JPEG is baseline, RGB with 4:2:0 subsampling, exactly `w` x `h`: the
  source is cut to fill the size, centred.
- The Bridge publishes the message retained, so a panel that subscribes gets
  the current picture at once. Above 65,535 bytes it travels as a stream and
  only to panels that announced room for it (`rx`); the Bridge lowers the
  JPEG quality (88 down to 46) until the picture fits the smallest room of the
  topic's panels, at most 512 KiB.
- An empty message clears the picture.

The panel reads the header strictly (magic, key, size, line end, a JPEG
start); anything else is dropped with a log line. It keeps the newest picture
of up to six entities in PSRAM; the screensaver also keeps its decoded copy.

## Keys and songs

The media state carries `"image_key"`:

- the key of the current artwork: the first 16 hex digits of SHA-256 of the
  artwork address (`media_image_url` before `entity_picture`) without
  Home Assistant's `token` query parameter, which changes over time;
- `""` once the player has had no artwork for three seconds (the
  `ArtworkClearGate` of `media_artwork.py`): the panel clears the cover;
- absent while the gate keeps the cover, or from an older Bridge.

A linked panel whose state carries `image_key` uses the picture with that key
and ignores the embedded copy (`entity_picture_data`). Until the new song's
picture arrives the tile keeps its cover, as with every other replacement;
when it arrives, every tile of that song and an open Media popup show it. A
state without `image_key` (older Bridge, or the panel on MQTT) keeps the
embedded cover as before.

An image entity's key comes from its picture address (`entity_picture`
without `token`) and its state, the time of its last picture. A camera has no
address to name its still: the key is the first 16 hex digits of SHA-256 of
the still's bytes, loaded every 10 s while a panel subscribes; an unchanged
still is not sent again. The screensaver shows a new key once the tiles have
settled after a touch, like a slide change.

## Rendering

The Bridge fetches the artwork through Home Assistant's HTTP client (at most
1.5 MB), reads an image entity's picture from the entity itself like Home
Assistant's image proxy (at most 12 MB; its address only as a fallback) and a
camera's still through Home Assistant's camera component. Six sources up to
8 MB in total are cached. It renders in an executor with Pillow:
EXIF orientation, transparency over black, `ImageOps.fit` to the size. A
render runs per topic and artwork at most once; a state change with the same
artwork renders nothing, and a newer artwork that arrives during a render
follows it.

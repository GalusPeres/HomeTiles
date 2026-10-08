# Pictures from the Bridge

Developer reference for pictures the HomeTiles Bridge renders for linked
panels: media covers today; the screensaver image, picture tiles (webcams,
vacuum maps) and the rain radar later. Only panels on the direct link
(`bridge-link.md`) get them; panels on MQTT keep the cover embedded in the
media state.

| Part | Firmware | Bridge |
| --- | --- | --- |
| Header and topic | `src/network/bridge/bridge_images_core.h` (pure) | `images.py` |
| Store and display | `src/network/bridge/bridge_images.*`, `tile_renderer.cpp` | `images.py`, `media_artwork.py` |
| Transport | link streams (`bridge-link.md`, `rx`) | `link_server.py` |

## Topics

`<ha_prefix>/media_player/<object_id>/image/<w>x<h>`, `w` and `h` 16 to
1,280 without leading zeros. A panel subscribes to the picture it shows in the
size it shows it; the Bridge renders a picture only while at least one linked
panel subscribes to its topic and the Bridge serves that player to the panel
(its released or declared media players). Nobody else gets a render.

A Media tile subscribes to its player's cover in the size the Media popup
shows it (`popup_layout::scale(240)`: 240 on 1280x800 panels, 200 on
1024x600, 160 on 480x480); a tile shows at most that much of it.

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
of up to six players in PSRAM.

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

## Rendering

The Bridge fetches the artwork through Home Assistant's HTTP client (at most
1.5 MB, six sources cached) and renders in an executor with Pillow:
EXIF orientation, transparency over black, `ImageOps.fit` to the size. A
render runs per topic and artwork at most once; a state change with the same
artwork renders nothing, and a newer artwork that arrives during a render
follows it.

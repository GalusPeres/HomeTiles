# Audio stream over the Bridge

Developer reference for clips the HomeTiles Bridge decodes for a panel's
speaker (firmware b502, Bridge v0.9.4b8). The panel no longer downloads and
decodes the clip itself: the Bridge's FFmpeg reads it and sends plain PCM
over the acknowledged TCP transport of the camera stream
(`src/video/tcp_ack_wire.h`), so the panel sets the pace. Code: firmware
`src/audio/audio_stream_contract.h` and `speaker.cpp` (`playBridgeStream`),
Bridge `audio_stream.py`. Camera sound and the microphone in the other
direction can dock onto the same channel later; neither is built.

## Why

Measured on a Guition V2 with firmware b500 and b501:

- The panel downloads a song much faster than it plays it. Once its 256 KB
  buffer in PSRAM is full it stops reading, and the sender keeps going until
  the TCP receive window is full. The Arduino core builds lwIP with
  `CONFIG_LWIP_TCP_WND_DEFAULT=65534` and without
  `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`, so up to 64 KB plus pbuf overhead
  waited in internal RAM for the whole song: free internal RAM 168 KB before
  a clip, about 80 KB while it played. The UI and the link slowed down.
- ESPHome has the same reader and ring buffer but builds ESP-IDF itself and
  switches `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` on for a speaker media
  player. The prebuilt Arduino core cannot change that.
- Every format and quirk landed on the microcontroller: TLS for https URLs,
  MP3 at every sample rate (own Helix build, b501), two ID3 tags in a row
  with the cover in the second (both songs that failed in the b501 test),
  and no AAC, FLAC or Ogg at all.

Home Assistant's ESPHome integration does the same in its own way: its
`ffmpeg_proxy` runs FFmpeg on the media URL inside Home Assistant and serves
the converted clip to the device.

## Decisions

- **PCM on the wire.** The panel decodes nothing; the Bridge does all the
  work. 44.1 kHz mono is 0.7 Mbit/s, a fraction of a camera stream (5-16
  Mbit/s on the same Wi-Fi path, internal DMA heap stayed at 146-157 KB free
  at 12 Mbit/s). What occupies internal RAM is the data in flight, which the
  window bounds, not the bit rate.
- **Rates.** 44.1 kHz for music (`media_player.play_media`), 22.05 kHz for
  speech: Assist answers, TTS media sources and announcements.
- **Assist answers use the stream too**, so a panel with a current Bridge
  never needs https for sound.
- **No extra encryption**, like the camera picture: the token is single use
  and the stream stays in the LAN.

## Flow

1. Home Assistant calls `media_player.play_media` (or Assist plays its
   answer). The Bridge resolves the media to an absolute, signed URL as
   before.
2. If the panel's speaker status announces `"stream":"tcp-ack-v1"`, the
   Bridge creates a session with a one-time token and sends the usual play
   command with `"url":"tcp://<ha-ip>:<camera-port>/<token>"` (sealed when
   paired).
3. The panel connects to the camera listener, sends `HTAUD/1 <token>\n` and
   gets the hello (`HTC1`, status 0, chunk 4096; status 1 = unknown token).
4. The Bridge starts FFmpeg (`-protocol_whitelist http,https,tcp,tls,crypto`,
   `-ss` for a seek, mono, the rate, `s16le` to stdout), reads the input
   length from its `Duration:` line and sends the format, then the PCM.
5. The panel keeps the PCM in its 256 KB PSRAM buffer (about 3 s at
   44.1 kHz) and plays it through the existing output (volume, amplifier,
   position, the memory line per clip).

## Wire format

Frame header `">4sB3xII"`: `"HTF1"`, type, 3 zero bytes, sequence, length.

| Type | Payload | Meaning |
| --- | --- | --- |
| 4 format | 16 bytes `">IHHII"`: rate, channels (1), bits (16), position ms, length ms (0 = unknown) | first message of every stream |
| 1 audio | PCM, little-endian, even length, 4 KB blocks | |
| 3 end | none | play the buffer out and close |

The panel acknowledges format and audio messages with `"HTA1"`, sequence,
length once they lie in its buffer. The Bridge keeps at most two messages
unacknowledged: at most 8 KB of the clip wait in the panel's internal
network memory. The panel reads without waiting (`MSG_DONTWAIT`) so the I2S
output stays fed, and reads audio only while its buffer has room.

## Control

Commands stay on `{base}/cmnd/audio`, the status on `{base}/stat/audio`
(`speaker_contract.h`), so the media player and the Assist satellite keep
their logic.

- **Pause / resume**: the panel stops its output and keeps the buffer; once
  it is full it reads nothing more. The Bridge marks the session paused and
  waits up to 10 minutes for the next acknowledgement (a playing panel that
  is silent for 45 s, the link's idle limit, is gone).
- **Seek**: the Bridge starts the same clip again from the new position (a
  new session and play command; FFmpeg seeks with `-ss`). The format names
  the position, from which the panel counts `position_ms`. Home Assistant
  offers seek once the length is known.
- **Stop / new clip**: the panel closes the connection; the Bridge ends
  FFmpeg. A new session for a panel ends its previous one.

## Compatibility

Firmware before b502 announces no `stream` and keeps the b501 path (it
downloads the http(s) URL, own Helix decoder, TLS in PSRAM). A Bridge
without the stream never sends a `tcp://` URL. Without the camera listener
the Bridge falls back to the URL.

## Measured on a Guition V2

- b502 with Bridge b8: every song played, including the two that failed with
  b501. Free internal RAM 168 KB before a song, 137-139 KB while it played
  (b501: about 80 KB): the I2S buffers (7.5 KB), the packets of two 8 KB
  blocks in flight and the socket. The UI was still slower and a camera full
  screen next to a song got 7.7 instead of 13 Mbit/s: the PSRAM buffer moved
  its whole unread tail (254 KB) for every 2 KB played, 43 times a second on
  the core of the camera stream and the network worker. b503 moves at most
  half the buffer once per half buffer played; Bridge b9 sends 4 KB blocks.
- The seek bar vanished or jumped back for a moment: the new stream reported
  "playing" from 0 without a length before its format arrived. b503 reports
  the clip once the format names its position.
- b503 with Bridge b9: free internal RAM 169 KB before a song, 148 KB while it
  played; a camera full screen next to a song ran at 30 FPS and 13-15 Mbit/s;
  the UI no longer slowed down. Home Assistant still moved the seek bar back
  until the panel reported the new position; Bridge b10 sets it at once.

## Camera sound (b504, Bridge b11)

No button: a panel whose speaker plays Bridge streams adds
`"audio":"tcp-ack-v1"` to its camera open (popup and full screen). For a
camera with a stream source the Bridge asks FFmpeg once whether the stream
has an audio track (`async_has_audio`, cached 10 minutes per source); if so
it starts a live session (22.05 kHz, RTSP read like the picture: TCP,
`nobuffer`) and sends a play command with `"live":true`.

- Switching between popup and full screen sends another open; a sound
  already running for the same stream keeps running.
- Close ends FFmpeg (`finish_device`); the end message follows and the panel
  plays out what it holds, so the clip ends as done, not as an error.
- The panel keeps 120-250 ms of a live stream and drops what arrives beyond
  that (`liveExcessBytes`), so a burst after a stall does not put the sound
  further behind the picture; its I2S buffer is 4 x 240 frames (44 ms) and
  the Bridge sends what FFmpeg wrote without waiting for a full block. b504
  with b11 kept 300-600 ms, 174 ms of I2S buffer and up to 93 ms per block,
  and its sound ran audibly behind the picture. Lip sync with timestamps is
  not attempted.
- The sound takes over the speaker like any clip: a playing song stops.
  Still-image cameras and the panels' own cameras have no sound.

## Later on the same channel

- **Microphone to the Bridge**: an intercom would send PCM the other way
  over tcp-ack, like the built-in camera upload (`local_camera_upload.cpp`).
  Talking while the speaker plays needs echo cancellation on the panel
  (Espressif ESP-SR).

The wake word stays deaf while a clip plays (one worker for both).

# Audio stream over the Bridge (draft)

Draft 2026-10-21, not implemented. Developer reference for playing sound on a
panel through the HomeTiles Bridge instead of letting the panel download the
file itself. It reuses the acknowledged TCP transport of the camera stream
(`src/video/tcp_ack_wire.h`, `camera_stream.py`), so the panel sets the pace.
Camera sound and the microphone in the other direction are meant to dock onto
the same channel later; neither is part of the first step.

## Why

Measured on a Guition V2 with firmware b500 and b501:

- The panel downloads a song much faster than it plays it. Once its 256 KB
  buffer in PSRAM is full it stops reading, and the sender keeps going until
  the TCP receive window is full. The Arduino core builds lwIP with
  `CONFIG_LWIP_TCP_WND_DEFAULT=65534` and without
  `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`, so up to 64 KB plus pbuf overhead
  wait in internal RAM for the whole song: free internal RAM 168 KB before a
  clip, about 80 KB while it plays, DMA largest about 37 KB. The UI and the
  link slow down.
- ESPHome has the same reader and ring buffer (Voice PE: `audio_http` with
  250 KB and 500 KB), but builds ESP-IDF itself and switches
  `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` on for a speaker media player, so its
  network buffers sit in PSRAM. The prebuilt Arduino core cannot change that.
- Every format and quirk lands on the microcontroller: TLS for https URLs,
  MP3 at every sample rate (own Helix build, b501), two ID3 tags in a row
  with the cover in the second (both songs that failed in the b501 test),
  and no AAC, FLAC or Ogg at all.

With the Bridge in front, Home Assistant's media stays inside Home Assistant,
FFmpeg reads every format, and the panel receives plain audio at the pace it
acknowledges.

## Overview

1. Home Assistant calls `media_player.play_media` (or an Assist answer, an
   announcement) on the panel's speaker entity.
2. The Bridge resolves the media (media source, TTS, URL) inside Home
   Assistant and starts one FFmpeg process: decode, mix down to mono,
   resample to the panel's rate, write raw PCM.
3. The Bridge sends the panel `{"v":1,"op":"stream", ...}` on the existing
   audio command topic (sealed when paired) with a one-time token, like the
   camera popup's stream URL.
4. The panel connects to the Bridge's audio TCP server, sends the token and
   receives PCM frames over tcp-ack. It acknowledges a chunk once it sits in
   its PSRAM ring; the Bridge never has more than `window` chunks in flight.
5. The panel plays from the ring through the existing output path (volume,
   limiter, amplifier, position, the memory line per clip).

## Wire format

The tcp-ack framing of `tcp_ack_wire.h`, unchanged:

| Message | Layout | Use |
| --- | --- | --- |
| hello | `"HTC1"`, status u16, chunk u16 | Bridge accepts the token, names the chunk size |
| frame | `"HTF1"`, type u8, 3 zero bytes, sequence u32, length u32 | type 1 audio, 2 flush, 3 end |
| ack | `"HTA1"`, sequence u32, cumulative bytes u32 | panel, per chunk |

- Audio frames carry signed 16-bit little-endian mono PCM at the rate the
  panel asked for in its request (proposed default 44.1 kHz; 22.05 kHz for
  speech is enough). The first frame of a stream starts with a small header
  (rate, channels, stream position in ms) so a seek or a new clip can change
  them.
- Flush (type 2): drop everything buffered and not yet played (seek, a new
  clip on the same connection).
- End (type 3): the clip is over; the panel plays out its ring, reports idle
  and closes.

Memory on the panel: internal RAM holds at most `window x chunk` in flight
(proposed 2 x 8 KB) plus the I2S DMA buffers; the ring (proposed 256 KB, about
3 s at 44.1 kHz mono) lives in PSRAM. No TLS, no decoder state.

## Control

Commands stay on `{base}/cmnd/audio`; the status stays on `{base}/stat/audio`
(`speaker_contract.h`), so the media player entity and the Assist satellite
keep their logic.

- **Pause / resume**: the panel stops or restarts its output and keeps the
  ring. The Bridge stops reading FFmpeg while no acknowledgement comes; the
  camera stream's 45 s acknowledgement limit does not apply to a paused
  audio stream.
- **Seek**: the Bridge restarts FFmpeg at the new position (`-ss`) and sends
  a flush frame; the next audio frame names the new stream position.
- **Position and length**: the Bridge knows the length (FFmpeg probe or
  Home Assistant metadata) and reports it to the entity; the panel keeps
  reporting `position_ms` from the samples it played, offset by the stream
  position of the last flush.
- **Stop / new clip**: the Bridge ends FFmpeg and sends end, or flush and the
  new clip on the same connection.

## Compatibility

A panel announces the stream in its capabilities; a Bridge that sees it uses
the stream, otherwise the current `play` command with a URL. Panels and
Bridges without the stream keep the b501 path (HTTP download, own Helix
decoder, TLS in PSRAM).

## Later on the same channel

- **Camera sound**: the camera's audio track (FFmpeg reads it from the same
  source as the picture) as a second stream type, started and stopped with
  the camera popup or full screen. Picture and sound timing still needs a
  design.
- **Microphone to the Bridge**: the panel already sends PCM to the Bridge for
  Assist. An intercom (panel microphone to a door camera's backchannel, or
  the panel's own camera with sound) would send PCM the other way over
  tcp-ack, like the built-in camera upload (`local_camera_upload.cpp`).
  Talking while the speaker plays needs echo cancellation on the panel
  (Espressif ESP-SR, as Tessera uses on the P4).

The wake word stays deaf while a clip plays (one worker for both); echo
cancellation would also be the way to listen during playback.

## Decisions before implementation

- PCM on the wire (no decoder on the panel, about 0.7 Mbit/s at 44.1 kHz
  mono) or MP3 (less traffic, decoder stays).
- Default sample rate.
- Whether Assist answers and announcements also move to the stream (no
  https on the panel at all) in the first step.

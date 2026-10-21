# HomeTiles shared project context

Last reviewed: 2026-10-21

## Sources of truth

- Version: `version.txt`; code: `git status`, recent commits/branch; finished work: git and `docs-dev/project-history.md`
- Device support and validation: `docs/index.md`; ESP32-P4/ESP-Hosted patches: `tools/esp-hosted-3.3.7-rx-fix/README.md`
- Release procedure: `RELEASING.md`. Live bug status: newest GitHub issue comments; recheck online before changing an issue status.
- Bridge publishing requires separate authorization. Docs: `docs/`, `mkdocs.yml`, `overrides/` (gh-pages).
- This file stays below 160 lines and 12 KiB (`tools/tests/build/test-project-context-size.mjs`); current facts only.

## Releases and builds

- FW + Bridge v0.9.3 released 2026-10-18. Every former feature branch (perf, direct-link, voice, layouts, settings-setup, security, icon-file, camera-view) is in it.
- Milestone v0.9.4: pairing by IP without mDNS (Bridge #16, HomeTiles #114: read-only `GET /api/link` + Bridge step).
- Test builds stay `v0.6.12bNNN` on purpose (System Update compares numbers only and offers releases); V2 beta define in `firmware_version.h`, last b495.
- Run `node tools/fast-build-check.mjs --build-path <cache>` before every build; a cache stays warm when only repo headers changed and the LVGL config fingerprint matches.
- Docs CI runs on pinned Chrome for Testing 154.0.8037.57 (the runner's .97 never answered the first Page.navigate); a tag's docs run uses the tag's workflow, so after a release dispatch docs on `main`.
- Factory images carry the icon file (`release-helper/factory-icon-file.js`); test builds need their icons on HomeTiles-staging (tag = FW_VERSION).

## Active work

- Media popup (`feature/media-popup` in both repos, Bridge v0.9.4b4 pre-release): design `build/design-mockups/media-player` (approved), plan `build/analysis/2026-10-19-analyse.md`. Two layouts per tile (`sensor_display_mode` 0 cover left, 1 cover centre), geometry `media_popup_layout.h`; controls only with their HA feature bit; sub-views source/sound mode, speakers (join/unjoin), library (PR #87); memooox3 co-author. Left: small bugs, simulator first. HW pending: both layouts V2/S3, dark/light, grouping, library, unavailable, sleep.
- Issue #116 (JC4880, camera popup ended after 18-60 min): the Bridge closed after 5 s without a chunk ACK. Bridge v0.9.4b5 waits 45 s (the link's idle limit) and logs one warning; FW logs data gaps >= 1 s and the Wi-Fi signal at stream end; test builds hold the stream with /api/debug/camera-pause (V2 b499: a 20 s stall passes). Reporter confirmation pending; why his round trip stalls is unproven (Wi-Fi suspected). A camera stream pauses the wake word (on core 0 it cut the V2 full screen to 21-23 FPS; b499 HW ok). Bridge Python work is negligible, FFmpeg decoding is ~80 % of the camera load. `build/analysis/2026-10-21-analyse.md`.
- Issue #119 (Guition V1): the core's Helix (12-bit sync) cannot see MPEG-2.5 frames; tag bytes made it overwrite its memory. Fixed in `53b03f32`; b501 (`4f0ba6b1`): own Helix `src/audio/helix_mp3`, TLS in PSRAM, position/length/pause, Bridge player bar. Speaker audio now comes from the Bridge (b502/b503, Bridge b8-b10, `docs-dev/audio-stream.md`): FFmpeg -> mono PCM (44.1 kHz music / 22.05 kHz speech) over the camera tcp-ack listener (`HTAUD/1`, two 4 KB blocks in flight); panels announce `"stream":"tcp-ack-v1"`, older ones download. b503 + b9 HW ok: every song plays, 169 -> 148 KB while playing, camera full screen 30 FPS next to a song. Seek restarts at `-ss`. Camera sound (b504-b506, Bridge b11-b13): live track of cameras with sound, picture waits for the sound (lip sync; b507 shows held frames on time, b506 dropped 3-5 FPS; HW pending). Small player, no queue; the wake word is deaf while a clip plays.
- Voice: microphone and wake word listening off on a new panel (b500, `2054a82a`); listening is its own setting, the chosen word stays (`applyWakeWordsCommand`). Bridge v0.9.4b6: Listening switch, word-only Wake word select, panel settings under Configuration, battery auto-sleep removed (no board has a battery sleep profile). HW/HA test pending. Mic button + audio settings open (user decides where, mockup first); Zeid's #91 Audio page as a fifth panel Settings category declined 2026-10-17.

## Do not retry (rejected with evidence)

- Guition S3 XIP/`-O2` (`5279456`): risk, no gain; XIP needs 5.4 of the 8 MB PSRAM.
- LVGL 2 draw threads: global compressed-font RLE state, glyph artifacts, no gain.
- S3 GT911 (fw 0x1060) steps slow drags and ignores config writes (also esp32-macropad #101).
- P4 OTA: throttling, PSRAM staging, TLS-to-flash streaming, Hosted restart, permanent SDIO buffers.
- Issue #30: a8204 markers, 20 MHz, 1-bit alone, 2.9.3 rollback. `JC-C6-slave_v2.3.2.bin` is packet mode: never flash it alone.
- Camera: `-skip_loop_filter` (drift, PSNR 18.6 dB), key-frame-only fallback (1 FPS), H.264 decoding on the panel (no HW decoder), `-flags low_delay` (one decode thread).
- Entity picker lists taken from the Bridge (b232-235 loop); b126 cover fade and b128 UI frame swap (lag); Wi-Fi power saving (P4 Hosted TX asserts, S3 link drops).

## Hardware validation

- Maintainer hardware: Tab5, Waveshare 4B/8-inch, Guition S3 and V2 (JC8012P4A1C_I_W_Y1, SKU10153002-V2; PPA/SD confirmed). Other revisions need community validation; compiling is not support.
- P4 code is shared; panel/touch init, timings, revision and images stay profile-specific. A new board never changes another board; shared files only get list entries.
- LCD-4 Rev 4.0 contributor-tested (display/touch/Wi-Fi/MQTT/OTA); older revisions and SD unsupported.
- JC4880P443 (PR #46): portrait 480x800, contributor-tested; landscape later. Open: SD DEINIT_ARG, P4 DSI groups.
- JC1060 V2 rotation pair 0/2 (b478, #108); `test-rotation-profiles` checks every profile.
- P4 v3 images (post_v3 301-399): WS 10.1 (PR #48, tested v3.2), WS 7B `_rev3` (#41), JC8012 V3 = V2 code (#44); HW pending.
- New in v0.9.3, HW pending by the reporters: GS-T3E v2 (#93, S3, relays GPIO45/46), Waveshare 5-inch (#76, HX8394, own `dsi_driver`, pre-v3 + `_rev3`).
- Voice (b477) HW ok 2026-10-16 on V2, Tab5, 8-inch, 4B; per-board `kVolumeLimit` (Guition 0.6, Tab5 0.9); S3 and the original JC1060 panel have none.

## P4 network, issues #37/#38

- Backported ESP-Hosted allocation/PSRAM fixes, synchronous RPC UID routing, Espressif `a8204f9` dropped-RX recovery; `repo-a8204` is the release-safe baseline. Wedge safeguards are recovery, not a transport fix.
- Issue #30 fix (v0.6.10): exact V1 keeps 1-bit/40 MHz and splits large RX into 512-byte CMD53 reads.
- Flash-write blue flash: cache-safe DSI/DMA/CSI objects (`tools/esp-idf-3.3.7-p4-cache-safe`).
- #38: reporter SKU10153001-V2 vs maintainer SKU10153002-V2 (labels prove no other panel). Exact-V2 fixes `e1a9297`. SD (#55): reporter card init fails, a failed V2 mount stays final until restart, init cause open. Interrupt-WDT cause unproven.
- #37: packets up to 65,535 bytes with bounded queues/ACKs; reporter confirmation pending.

## Memory, flash, performance

- LVGL PSRAM S3 2 MiB / P4 12 MiB; internal/DMA band <= 72 KiB; page caches S3 4 / P4 6. Bindings, popup history, Select options, renderer slot state, Binary Sensor queue and grids live in PSRAM.
- S3 mallocs > 32 B prefer PSRAM (b324): internal free 42 -> ~89 KB, text-heavy steps slower. Open: S3 Web Admin pages block the loop 1-6 s.
- Icon file: all 7447 MDI icons in LittleFS `/_mdi_icons_<px>.bin` (`tools/generate-icon-files.mjs`), 399 built in, one fixed PSRAM glyph cache; a missing file downloads once from its release; System page Icons card.
- `-fno-exceptions` (-300 KiB per image), unused LVGL widgets off, Admin CSS minified (`formatCssDelivery`). LVGL 9.6.0 (lvgl#10306, caches `hometiles-lvgl96-*`).
- Perf (released): nav preload (S3 max 3 grids, reserve, pressure release), shadow/circle caches, P4 CPU rotation for small areas, S3 PCLK 12 MHz.
- Pending: artwork, sleep/wake, camera soak, memory minima.

## Camera

- Stream: one Bridge FFmpeg per camera/size/fps/quality for all panels (lingers 30 s), thinned to the panel's rate before scaling; tcp-ack-v1 with 32 KB chunks x2, 8 KB x1 after a transport error or a restart during a fast stream. P4 decodes JPEG in hardware, S3 with tjpgd (~4.5 FPS popup).
- Full screen (#65): frames in the panel's framebuffer size and turn, HW JPEG straight into the framebuffer, black until the first frame; V2/Tab5 30 FPS HW ok; "(Vollbild)" View targets (`full_targets`).
- Camera tile with picture (#50, b377) HW pending. Screensaver SD/HA picture (#69) HW ok.
- Local camera: core `src/video/local_camera/`, drivers `sensors/*/`, boards `src/devices/*/local_camera_board.*`, opt-in `local_cam_en`. Window rule (b296): Bridge quarter turn = upright centred strip, no turn = whole image. `lcam_rot` (b387, decided): one value for every layout, the mirror never changes the turn. HW pending on most boards (#56). Open: TEST window `kChunkWindow = 4` (`local_camera_stream_contract.h`).

## UI decisions (do not reopen)

- One color rule (b378): color always from the icon; per tile only "Ausfuellen" (`tileFills`); own color only Clock/Text/Pixelanim; Light/Climate/Cover popups never filled. Light theme: filled tile kJewel, frame line per `frame_line`.
- Radius fixed (b378): `tile_radius::kDefault`, circle 25 %, card per theme (dark `#1A1A1A`). Colors from `tone_color.h` (OKLCH); presses without theme recolor.
- Half-grid: Sensor/Binary/Energy/Switch height 0.5, width >= 1 by 0.5; V7 size unchanged. Open: cross-grid import clamps whole tiles (HTTP 400), value fonts 32/40 clip in half tiles.
- Titles: two centred/ellipsized lines, 255 UTF-8 bytes in `/_tile_titles`.
- HW pending (evidence in `build/`): radius reboot, screensaver child-click, Energy compact slots, Clock/Text border.
- Layouts (`grid_layout.h`): classic, head bar landscape, head bar upright (prepared); NVS `layout`; places `/_tile_grids/layouts.json`; folder storage entries (`grid_slots.h`, max 64), full storage blocks Save (`layoutStorageFull`); b479/b480 HW pending.
- One look (b300): popup card margin 0, tiles one grid gap from the edge; popup header follows the head bar rule (`popup_layout.h`); card shadow width follows the radius to stay in the LVGL shadow cache (`shadow_width_for`, max 28 px).
- Upright (b294): P4 panels upright under a landscape profile draw the upright UI directly (`drawUpright`); JC1060/7B upright and JC4880 landscape (b368) compile only, HW pending.
- Light theme: `ui_theme` roles + `SurfaceScope`; popups take the opener's surface. Remembered LVGL objects clear themselves on delete (LVGL 9.6 `lv_obj_is_valid` reads freed objects).
- Settings: four pages after `build/design-mockups/settings`; first-start setup in the popup card (S3 HW pending).

## Tiles and Bridge contracts

- Editable tiles 21 Number, 22 Select, 23 Date/Time reuse Sensor persistence/popups (`PackedTileV7` unchanged); drafts coalesce 600 ms, sliders publish on release. Open: S3 froze adding Number to an active screensaver.
- Lock/Alarm/Fan (24-26, `src/types/device/`): sealed commands need pairing + Web Admin password; HW ok 2026-10-02.
- Switch tile layouts (`sensor_decimals` 0-3) and `command_pacer.h` slider pacing: reporter test pending (#11).
- Scene tile is "Button": picker searches scenes/scripts/buttons (`entity_search` 4), entity in `sensor_entity`.
- Direct link: TCP 8140, Bridge = broker, frames sealed with pairing keys (`docs-dev/bridge-link.md`). The panel declares every entity it uses; without a password only released entities and the Energy Dashboard categories reach it. Picker lists Recent/Favorites/All (b369): V2 HW pending.
- Security: optional Web Admin password (PBKDF2 300k), sessions, CSRF; command channel pairing v2 (X25519, 6-digit code), `docs-dev/command-encryption.md`.
- Commands validate targets/availability/features and ignore retained commands; stable tile IDs in reserved PackedTileV7 bytes.
- Languages en/de/fr/pl/cs/es/sk are data only (Strings + LocaleProfile incl. long date pattern and keyboard; no language codes in code); `test-i18n-panel-glyphs` rejects missing glyphs. Device text fit cs/es/sk pending.

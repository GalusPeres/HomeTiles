# HomeTiles shared project context

Last reviewed: 2026-10-26

## Sources of truth

- Version: `version.txt`; code: `git status`, recent commits/branch; finished work: git and `docs-dev/project-history.md`
- Device support and validation: `docs/index.md`; ESP32-P4/ESP-Hosted patches: `tools/esp-hosted-3.3.7-rx-fix/README.md`
- Release procedure: `RELEASING.md`. Live bug status: newest GitHub issue comments; recheck online before changing an issue status.
- Bridge publishing requires separate authorization. Docs: `docs/`, `mkdocs.yml`, `overrides/` (gh-pages).
- This file stays below 160 lines and 12 KiB (`tools/tests/build/test-project-context-size.mjs`); current facts only.

## Releases and builds

- FW v0.9.5 (hotfix) and FW + Bridge v0.9.4 released 2026-10-23. Bridge beta v0.9.6b1.
- Pairing by IP without mDNS (#114, Bridge #16): read-only `GET /api/link` + Bridge menu step; HA ok on Tab5 (b513, b16).
- Test builds stay `v0.6.12bNNN` on purpose (System Update compares numbers only and offers releases); beta define in `firmware_version.h`, last b566.
- Run `node tools/fast-build-check.mjs --build-path <cache>` before every build; a cache stays warm when only repo headers changed and the LVGL config fingerprint matches.
- Docs CI runs on pinned Chrome for Testing 154.0.8037.57; a tag's docs run uses the tag's workflow, so after a release dispatch docs on `main`.
- Factory images carry the icon file (`release-helper/factory-icon-file.js`); test builds need their icons on HomeTiles-staging (tag = FW_VERSION).

## Active work

- Power flow tile 28 ("Leistungsfluss", `src/types/energy_flow/`, picture 65): nodes in `/_tile_flow_entities`, options in key_code, node tap = Sensor popup; V2 HW ok b542.
- Media popup (v0.9.4): layouts `sensor_display_mode` 0 cover left, 1 centre (`media_popup_layout.h`); controls only with their HA feature bit; menu box (built once): speakers (group count), source, library; tile/head icon state form (`state_icon.h`).
- Issue #116 (JC4880, camera popup ended after 18-60 min): the Bridge closed after 5 s without a chunk ACK. Bridge v0.9.4b5 waits 45 s and logs one warning; FW logs data gaps >= 1 s and the Wi-Fi signal at stream end. Reporter confirmation pending. A camera stream pauses the wake word (b499 HW ok). `build/analysis/2026-10-21-analyse.md`.
- Issue #119 (Guition V1): the core's Helix missed MPEG-2.5 frames and overwrote its memory. Own Helix (b501) `src/audio/helix_mp3`, TLS in PSRAM, position/length/pause, Bridge player bar. Speaker audio comes from the Bridge (`docs-dev/audio-stream.md`): FFmpeg -> mono PCM (44.1 kHz music / 22.05 kHz speech) over the camera tcp-ack listener (`HTAUD/1`, two 4 KB blocks in flight); panels announce `"stream":"tcp-ack-v1"`, older ones download. HW ok (169 -> 148 KB playing, camera 30 FPS beside a song). Camera sound: live track of cameras with sound, picture waits for the sound (lip sync; b507 30 FPS HW ok, b508 even gaps HW pending). Small player, no queue; a pause frees the stream (b512/b15); the wake word is deaf while a clip plays.
- Voice: microphone and wake word listening off on a new panel (b500, `2054a82a`); listening is its own setting, the chosen word stays (`applyWakeWordsCommand`). Bridge v0.9.4b6: Listening switch, word-only Wake word, battery auto-sleep removed. HW/HA test pending. Mic button + audio settings open (user decides where, mockup first); #91 Audio page declined 2026-10-17.

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
- P4 v3 images (post_v3 301-399): WS 10.1 (PR #48, tested v3.2), WS 7B `_rev3` (#41; 30 s Wi-Fi join, PR #120), JC8012 V3 = V2 code (#44); HW pending.
- New in v0.9.3, HW pending by the reporters: GS-T3E v2 (#93, S3, relays GPIO45/46/44 at once), Waveshare 5-inch (#76, HX8394, own `dsi_driver`, pre-v3 + `_rev3`).
- Voice (b477) HW ok 2026-10-16 on V2, Tab5, 8-inch, 4B; per-board `kVolumeLimit` (Guition 0.6, Tab5 0.9); S3 and the original JC1060 panel have none.

## P4 network, issues #37/#38

- Backported ESP-Hosted allocation/PSRAM fixes, synchronous RPC UID routing, Espressif `a8204f9` dropped-RX recovery; `repo-a8204` is the release-safe baseline. Wedge safeguards are recovery, not a transport fix.
- Issue #30 fix (v0.6.10): exact V1 keeps 1-bit/40 MHz and splits large RX into 512-byte CMD53 reads.
- Flash-write blue flash: cache-safe DSI/DMA/CSI objects (`tools/esp-idf-3.3.7-p4-cache-safe`).
- #38: reporter SKU10153001-V2 vs maintainer SKU10153002-V2. Exact-V2 fixes `e1a9297`. SD (#55): reporter card init fails, a failed V2 mount stays final until restart, init cause open. Interrupt-WDT cause unproven.
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

- One color rule (b378): color always from the icon; per tile only "Ausfuellen" (`tileFills`); own color only Clock/Text/Pixelanim; Light/Climate/Cover popups never filled; light theme: filled tile kJewel, frame line per `frame_line`.
- Radius fixed (b378): `tile_radius::kDefault`, circle 25 %, card per theme (dark `#1A1A1A`). Colors from `tone_color.h` (OKLCH); presses without theme recolor.
- Half-grid: Sensor/Binary/Energy/Switch height 0.5, width >= 1 by 0.5; V7 size unchanged. Open: cross-grid import clamps whole tiles (HTTP 400), value fonts 32/40 clip in half tiles.
- Titles: two centred/ellipsized lines, 255 UTF-8 bytes in `/_tile_titles`.
- HW pending (evidence in `build/`): radius reboot, screensaver child-click, Energy compact slots, Clock/Text border.
- Layouts (`grid_layout.h`): classic, head bar landscape, head bar upright (prepared); NVS `layout`; places `/_tile_grids/layouts.json`; folder storage entries (`grid_slots.h`, max 64), full storage blocks Save (`layoutStorageFull`); backups hold all three, import checks classic places on the stored grid, a start keeps the choice (b509/b510); b479/b480 HW pending.
- One look (b300): popup card margin 0, tiles one grid gap from the edge; popup header follows the head bar rule (`popup_layout.h`); card shadow width follows the radius to stay in the LVGL shadow cache (`shadow_width_for`, max 28 px).
- Upright (b294): P4 panels upright under a landscape profile draw the upright UI directly (`drawUpright`); JC1060/7B upright and JC4880 landscape (b368) compile only, HW pending.
- Light theme: `ui_theme` roles + `SurfaceScope`; popups take the opener's surface. Remembered LVGL objects clear themselves on delete (LVGL 9.6 `lv_obj_is_valid` reads freed objects).
- Settings: 4 pages (`build/design-mockups/settings`), Display with a Sound group on speaker boards (#123); setup in the popup card.
- Lights (#130, b554): HA colors in both themes; Fill fills while on (white grey); Switch tiles off unfilled; Light popup = tile without Fill. HW pending.
- Units (b551): a step smaller after the number, digit tops level, like the weather forecast (`value_unit.h`, `.tile-unit`, `unitTextHtml`): tiles, state lines, popups; two units in one line keep one size.

## Tiles and Bridge contracts

- Editable tiles 21 Number, 22 Select, 23 Date/Time reuse Sensor persistence/popups (`PackedTileV7` unchanged); drafts coalesce 600 ms, sliders publish on release. Open: S3 froze adding Number to an active screensaver.
- Lock/Alarm/Fan (24-26, `src/types/device/`): sealed commands need pairing + Web Admin password; HW ok 2026-10-02.
- Calendar 27 (PR #127, `docs-dev/calendar.md`): Bridge v0.9.6b1 `events` (`entity_search` 5); V2 HW pending.
- Sensor graph (#131, mockup sensor-graph-3): pictures (`area_graph.h`), no LVGL lines; popup area circle color, fades in lower half; 1 px 8 % lines max/min/zero/time + zero label; tile only 24 h; popup 7D/24H/6H (6H numeric). HW pending.
- Switch layouts `sensor_decimals` 0-3, 4-6 upright bar (`bar_orientation.h`; Fan upright = 1; sim ok, HW pending); `command_pacer.h` pacing: reporter test pending (#11).
- Scene tile is "Button": picker searches scenes/scripts/buttons (`entity_search` 4), entity in `sensor_entity`.
- Direct link: TCP 8140, Bridge = broker, frames sealed with pairing keys (`docs-dev/bridge-link.md`). The panel declares every entity it uses; without a password only released entities and the Energy Dashboard categories reach it. Picker lists Recent/Favorites/All (b369): V2 HW pending.
- Security: optional Web Admin password (PBKDF2 300k), sessions, CSRF; command channel pairing v2 (X25519, 6-digit code), `docs-dev/command-encryption.md`.
- Commands validate targets/availability/features and ignore retained commands; stable tile IDs in reserved PackedTileV7 bytes.
- Languages en/de/fr/pl/cs/es/sk are data only (Strings + LocaleProfile incl. long date pattern and keyboard; no language codes in code); `test-i18n-panel-glyphs` rejects missing glyphs. Device text fit cs/es/sk pending.

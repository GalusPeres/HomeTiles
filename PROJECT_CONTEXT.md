# HomeTiles shared project context

Last reviewed: 2026-10-08

## Sources of truth

- Version: `version.txt`; code: `git status`, recent commits/branch
- Device support and validation: `docs/index.md` (device status notes)
- ESP32-P4/ESP-Hosted patches: `tools/esp-hosted-3.3.7-rx-fix/README.md`
- Release procedure: `RELEASING.md`
- Live bug status: newest GitHub issue comments; recheck online before changing an issue status
- Bridge publishing requires separate authorization. Docs: `docs/`, `mkdocs.yml`, `overrides/` (gh-pages).

## Firmware baseline

- v0.8.0: tag `2a7790a` (2026-10-03), Bridge v0.8.0 `dc4a750`; `main` adds docs images only.
- Guition S3 XIP/`-O2` reverted in `5279456` (risk, no measured gain); XIP now needs 5.4 MB of the 8 MB PSRAM (3.6 MB in use) - do not retry.
- `feature/perf` (both repos, unreleased, FW `b259-perf3f`): nav preload (S3 max 3 grids, 40/16 KB reserve, pressure release), shadow/circle caches, P4 CPU rotation for small areas, touch hold, 60 s energy/history reuse, S3 PCLK 12 MHz; V2/S3/8-inch HW ok. Bridge v0.9.0b16 beta (`17ca8a3`, feature/direct-link): request limits, empty energy answer, meta push + `"push": 1` (panel refresh 15 min); HA test pending.
- Rejected: LVGL 2 draw threads (global compressed-font RLE state: glyph artifacts, no gain). S3 GT911 (fw 0x1060, cfg 0xFA, filter 8 = 32 px) steps slow drags and ignores config writes (also esp32-macropad #101) - do not retry. Open: S3 Web Admin pages block the loop 1-6 s.

## Direct Bridge link (`feature/direct-link` in both repos, unreleased)

- Panel connects to the Bridge over TCP 8140 instead of MQTT; Bridge = broker (same topics/retained), frames sealed with pairing-K keys. Contract `docs-dev/bridge-link.md` (shared vectors).
- Setup: Pair on panel (2 min, TXT `pair=1`) -> HA card (new or switch MQTT entry) -> `POST /api/link` (no password) -> number, no restart.
- V2 b226 + Bridge b9 HW OK: new, switch, unpair, delete, quick Pair. Both clear MQTT leftovers. New HA IP = new setup.
- Entity picker (b227-b241, Bridge b15): panel declares all it uses (acked; `own` = own entry's releases + these); others only paired + password, 1st via panel tap. Never from Bridge lists (b232-235 loop). S3 b240 HW ok; b241 title Auto/full-height settings HW pending.

## Hardware validation

- Maintainer hardware: Tab5, Waveshare 4B/8-inch, Guition S3/V2 (JC8012P4A1C_I_W_Y1, SKU10153002-V2). V2 Tested, PPA/SD confirmed.
- v0.6.9 Binary/Text-State Sensor UI passed hardware tests on 4B, 8-inch and S3.
- Other revisions need community validation; compiling is not support.
- P4 code is shared; panel/touch init, timings, revision and images stay profile-specific.
- LCD-4 Rev 4.0: contributor-tested display/touch/Wi-Fi/MQTT/Web OTA; older revisions and SD unsupported (`docs/index.md`).
- JC4880P443 (PR #46, damianeek): portrait 480x800/4x6, contributor-tested; landscape later. Open: SD DEINIT_ARG, P4 DSI groups.
- P4 v3 images (post_v3 301-399, v3 DSI clock): WS 10.1 (PR #48, tested v3.2), WS 7B (`_rev3`, replaces exact-v3.1, #41), JC8012 V3 = V2 code (#44); HW pending.

## Issue #30 (closed by fix b6, shipped v0.6.10)

- Guition V1 SDIO cascade (CMD53 `0x109`/`0x107`, `rst:0xc`) on large reads; fix: exact V1 keeps 1-bit/40 MHz and splits large RX into 512-byte CMD53 reads. Not fixes, do not retry: a8204 markers, 20 MHz, 1-bit alone, 2.9.3 rollback.
- `JC-C6-slave_v2.3.2.bin` is packet mode: never flash it alone (original C6 and HomeTiles use streaming mode).

## ESP32-P4 network history

- Backported: ESP-Hosted allocation/PSRAM fixes, synchronous RPC UID routing, Espressif's `a8204f9` dropped-RX recovery, sparse diagnostics. Patches, variants, hashes, limits: `tools/esp-hosted-3.3.7-rx-fix/README.md`.
- `repo-a8204` is the release-safe baseline; the short-tail variant was experimental.
- Failed P4 OTA approaches: throttling, PSRAM staging, TLS-to-flash streaming, Hosted restart, permanent SDIO buffers; retry only with new evidence.
- Network wedge safeguards are recovery, not a transport fix.
- Flash-write blue flash: cache-safe DSI/DMA/CSI objects (`tools/esp-idf-3.3.7-p4-cache-safe`), V2 b137 ok.

## Issue #38

- Reporter SKU10153001-V2 (`_I_W_Y`); #18 and maintainer SKU10153002-V2 (`_I_W_Y1`). Labels alone prove no other panel.
- V2 fixes `e1a9297` (exact V2): touch bounds, internal I2C atomic state, slot-aware SD cleanup; beta define `HOMETILES_ISSUE38_BETA`.
- SD (#55): reporter card-init fails (40/20 MHz); Web Admin remounts then hit a Hosted slot-1 assert. Failed V2 mount now final until restart (boot try before Wi-Fi); init cause open. `build/issue-38/SD-LOG-ANALYSIS.md`.
- Touch rapid-tap fix confirmed. Interrupt-WDT dump: I2C atomic state was in PSRAM, `37758ef327f9` forces internal; WDT cause unproven (`build/guition-v2-crash-20260911/`).
- Bridge v0.6.47 (`43be012`): HA forecast subscriptions replace polling; restart validation pending.

## Sensor history

- Binary Sensor (20) V7-compatible; textual states timeline/Activity, numeric graphs; missing/unknown/unavailable distinct.
- Bridge v0.6.40 (`581150b`): bounded Recorder paging, categorical history, legacy compatibility.

## Editable tiles

- IDs 21 Number, 22 Select, 23 Date/Time reuse Sensor persistence/popups; `PackedTileV7` unchanged.
- Number: Media slider, Climate +/- pill or roller, graph/Activity; Select: Settings dropdown, timeline/Activity.
- Date/Time: hh/mm/ss rollers in a pill, 23/00 and 59/00 wrap; date spinboxes; HA timezone/DST validation.
- Additive `/control` keeps legacy clients; sessions/revisions/deadlines reject stale commands.
- Drafts coalesce steps/rollers 600 ms, sliders publish on release, kept until confirmation/rejection or 30 s.
- Editable surfaces: control fill, white text; Select list = card + hairline, gap, inset selection.
- Titles (approved): two centered/ellipsized lines; 255 UTF-8 bytes in `/_tile_titles`; Settings v4 size unchanged; view labels flatten CR/LF.
- S3 froze adding Number to active screensaver (Web ok, manual reboot); cause unknown.

## Shared-popup/artwork baseline

- v0.6.11 `3b534ab`: shared popup frame/header/close, cached bodies; cold content waits for first frame; close/switch/delete cancel work.
- Artwork: URL-only `state_fast` precedes MQTT; failed replacements keep covers; URL/content pairing avoids S3 redownloads.
- Memory: LVGL PSRAM S3 2 MiB/P4 12 MiB; internal/DMA band <=72 KiB; page caches S3 4/P4 6. Bindings, popup history, Select options in PSRAM (b123).
- Pending: artwork, sleep/wake, camera soak, memory minima.
- Colors: `tone_color.h` OKLCH (+0.06 L at 25 %); opaque circles/controls, veil only on see-through tiles; dark icons lifted vs. defaults (b156); tinted circle/controls = From icon circle (b153). Presses: no theme recolor/teal fade, PIN/pill a step up.
- LVGL 9.6.0 (lvgl#10306): S3 recolor 230->5 ms; caches `hometiles-lvgl96-*`.
- Reverted: b126 cover fade, b128 UI frame swap (lag). LVGL 9.6 band rounding: 4B buffers rounded up (b166 ok).

## Radius and half-grid

- NVS radius: old radius to `(cell h - gap)/4`, unset = max; tile color `#1A1A1A`; new icon tiles "From icon" 20 %. Used by tiles, Climate, popups, previews.
- Half-grid: Sensor/Binary/Energy/Switch height 0.5, width >=1 by 0.5; original 2x1. Whole layouts/V7 size unchanged; header fraction bits. Make every tile whole before 0.6.x (`docs/updating.md`).
- Device/Web: concentric icon radius, original title font, smaller default value font; explicit sizes respected; text gap 0. Masking declined.
- Reflow/drafts/rollback keep positions; stale GETs preserve edits. Empty 1x1 slots scan both axes by 0.5, no overlaps. Settings/Back: 1x0.5; Settings v4 bits 1-4 store snapshot fractions.
- Hidden Climate reset crash (fractional Sensor width): Climate-only integer guard.
- Binary Sensor shares Sensor value sizes (20/24/32/40); default preserves old layout. Stored in existing V7 field; editor, import and previews retain it.
- HW pending (evidence in `build/`): radius reboot, screensaver child-click, Energy compact slots, Clock/Text border.
- Weather icons (`tools/generate-weather-icon-fonts.mjs`): colored, toggle, icon tint; night: bridge `sun`.
- Clock/Text per-tile border: V7 display-mode byte 1=hidden; global/screensaver toggles respect it.
- Open: cross-grid import clamps whole tiles to half steps (HTTP 400), snapshot type accepts halves, value fonts 32/40 clip in half tiles.

## Switch tile and slider pacing (HW pending)

- Layout = `sensor_decimals`: 0 icon button, 1 switch, 2 dimmer, 3 automatic. Header like 1x0.5, from 1.5 rows like Sensor (bar +1/3); bar = Climate pill, touch to card edges.
- 3 s hold after drag/toggle keeps bar, icon, tint local. Light popup = tile: circle, track, buttons, off thumb, #B0B0B0 symbol (b156).
- `command_pacer.h`: HA slider timing, Light popup/dimmer (#11): tap = one command, >=500 ms apart, paced final; reporter test pending.

## Local camera

- Core `src/video/local_camera/` is device-agnostic; drivers `sensors/*/`, boards `src/devices/*/local_camera_board.*`; opt-in `local_cam_en`.
- V2 OV02C10 720p. 8-inch OV5647 544x960, Bridge `rotate` 90. Tab5 SC202CS 720p RAW8, mirror+180, BGGR under flips.
- Camera builds pass; HW pending (#56): WS 7/10.1/7B/4.3/4B OV5647, V1/JC1060 V2 OV02C10, JC4880 quarter turn.
- Advanced: `lcam_rot` (180 = flip, odd = `rotate` 90), `lcam_rbswap` (Bayer, next start).
- HW pending: b30-b35 (CSI/ISP, gain, screenshot, Wi-Fi/AP, rotation, sleep stream/indicator wake).
- Open: int WDT HW; TEST `kChunkWindow = 4`; stream AE waits for a step to show (b215).
- Popup stream: Bridge thins before scaling, shared FFmpeg per camera; P4 b134 30 FPS; #63 1024x600 552x310 (b135).

## v0.7.0 release

- S3 fix `c3e0a673`: PSRAM-first OTA TLS, all three profiles. Guition b74 OTA/boot/MQTT passed; Waveshare HW pending.
- v0.7.1 promised: PR #51 Polish port, #26 S3-4B PCLK (16 vs 10 MHz), P4 v3.2 for 7B (#41) and JC8012 V3 (#44); French.
- Tests pending #7 #11 #27 #34 #45; #55 reporter check.

## Flash and RAM (PR #62)

- `-fno-exceptions` (CI and local): -300 KiB per image, Tab5 428 KiB OTA headroom; unused LVGL widgets off: -49 KiB, IRAM -21 KiB.
- Renderer slot state, Binary Sensor queue and grids in PSRAM (`setup()`): static DRAM V2 137 -> 65 KB, S3 140 -> 80 KB; b75 passed.

## View control and telemetry

- v0.6.10: Home/folder/popup navigation (`4c9ea4e`) via existing UI, PIN and camera teardown paths.
- Visible folders are reused for their popup/descendants; new/locked paths still require access checks (S3 Home detour fix).
- Stable tile IDs use reserved PackedTileV7 bytes and durable counters; MQTT sessions/sequences/deadlines reject replay.
- Switch adds input_boolean/automation/fan/humidifier/remote/siren; Scene adds button/input_button. Aliases stay stable.
- Commands validate targets/availability/features; ignore retained commands.
- View/editable controls confirmed on 8-inch/S3. HA migration, legacy firmware and lifecycle coverage pending.
- Issue #37: packets up to 65,535 bytes (was 16 KiB) with bounded queues/ACKs; reporter confirmation pending.

## Security branch (unreleased)

- Web Admin password (optional): PBKDF2 key (300k iter) derived by browser/Bridge, panel stores it; HMAC login, 30-day NVS sessions, CSRF; device reset; hides secrets.
- Command channel (optional): pairing v2 (X25519, 6-digit code on panel/HA), sealed commands/stream tokens, replay window, two-sided unpair, signed announcement; `docs-dev/command-encryption.md`. P4 random: SAR ADC. V2/S3 tested. b125 rekey resets stale sessions.

## Lock, Alarm panel and Fan tiles (HW tested by maintainer 2026-10-02)

- IDs 24-26 share `src/types/device/` and `src/ui/popups/device/`; design/contract: `build/ha-dummy-sim/`.
- Retained `.../detail` state; views on their cards, by entity. Lock/Alarm: pairing + Web Admin password, sealed (id, 15 s deadline, `web_auth`); answers `{base}/stat/lock|alarm` (large buffer); PIN popup code; only reported states show; no screensaver.

## Settings redesign (`feature/settings-setup`, unreleased)

- Rebuilt after `build/design-mockups/settings` (HANDOFF.md): four pages, dialogs, new keyboard; old popups and `ui_keyboard` removed.
- First-start setup (b252, `setup_screen.cpp`): 4 steps in the popup card; a new panel stores its defaults at boot (network starts, no restart), step kept in NVS `setup_step`; V2 HW ok, S3 pending.
- Web Admin Settings (b257): list like the panel (WLAN, Lokalisierung, System + I/O, camera, files, diagnostics), one footer; System shows the way (direct/MQTT), MQTT folded; #73 `/api/language` poll. V2 b258 HW ok.
- New look (b260): popup head, borderless groups; square panels gear + four tabs; portrait popups = centered square card. b261: WLAN entry card, back arrow, option gap, concentric head buttons, corner circle at the pill inset. HW pending.
- Weather tile (b261) by real size, not slots: days = width / (widest measured text + 85 % gap) (4B/V2 3x2 = 5), row from two-row height, FR tile "Auj."; preview gets the measured width.

## Layouts (`feature/layouts`, unreleased)

- Head bar option: Web Admin global switch, NVS `head_bar`, read at boot (restart). `grid_layout.h` shown grid: 1280x800 6x4, 1280x720/1024x600 5x3, 800x480 4x3, square 3x3, JC4880 3x5; `home_bar.cpp` = Settings head (circle, title, time, gear/X), Settings/Back tiles hidden.
- Stored positions keep the profile grid: tiles outside the shown grid are not drawn and outlined red in Web Admin, never moved; placement/reorder stay inside it. Screensaver tab keeps the profile grid, the panel shifts its rows. Settings and camera stripe keep profile geometry.
- Emulator only so far: portrait, folder circles in the bar. HW pending.

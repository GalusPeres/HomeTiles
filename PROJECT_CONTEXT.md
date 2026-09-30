# HomeTiles shared project context

Last reviewed: 2026-09-30

## Sources of truth

- Version: `version.txt`; code: `git status`, recent commits/branch
- Device support and validation: `docs/index.md` (device status notes)
- ESP32-P4/ESP-Hosted patches: `tools/esp-hosted-3.3.7-rx-fix/README.md`
- Release procedure: `RELEASING.md`
- Live bug status: newest GitHub issue comments; recheck online before changing an issue status
- Bridge publishing requires separate authorization.

## Firmware baseline

- v0.6.12: `9605b6a`, CI `34353664113`, 15 profiles / 30 images; 102 tests pass. Guition V1/V2 PPA and Weather fixes; V2 confirmed, V1 hardware pending.
- Stabilization: display/MQTT guards, Light coalescing, incremental Weather (`e3de63c`-`33b4e06`).
- Guition S3 XIP/`-O2` reverted in `5279456` (risk, no measured gain); do not retry without evidence.

## Hardware validation

- Maintainer hardware: Tab5, Waveshare 4B/8-inch, Guition S3/V2 (JC8012P4A1C_I_W_Y1, SKU10153002-V2). V2 Tested, PPA/SD confirmed.
- v0.6.9 Binary/Text-State Sensor UI passed hardware tests on 4B, 8-inch and S3.
- Other revisions need community validation; compiling is not support.
- P4 code is shared; panel/touch init, timings, revision and images stay profile-specific.
- LCD-4 Rev 4.0: contributor-tested display/touch/Wi-Fi/MQTT/Web OTA; older revisions and SD unsupported (`docs/index.md`).
- JC4880P443 (PR #46, damianeek): portrait 480x800/4x6, contributor-tested; landscape later. Open: SD DEINIT_ARG, P4 DSI groups, tall popups.
- WS 10.1 v3 (PR #48, memooox3): separate `waveshare_10_1_rev3`, post_v3 301-399; pre-v3 image unchanged. Chip-id/CI image test pending.

## Issue #30

Issue: https://github.com/GalusPeres/HomeTiles/issues/30

- External Guition `JC8012P4A1C_I_W_Y` V1, Foscam via HA Generic Camera; OTA failed, USB worked. SDIO cascade (CMD53 `0x109`, timeout `0x107`, raw `0xcccccccc`, invalid RX length, `rst:0xc`), also without cameras; restarts leave no panic dump. First DCRC `0x80` on 11-/14-block C6-to-P4 reads. Repeated a8204 markers, 20 MHz (b3, also Issue #167), 1-bit alone (b5) and the 2.9.3 rollback are not fixes; do not retry.
- Version RPC `0x15e` also occurs on the stable 8-inch; not the cascade cause.
- SDIO schematics: V1 5.1-kohm pull-ups/no series termination; 8-inch 51-kohm; Tab5 5.1-kohm/22-ohm series/switched WLAN power. Signal margin unproven.
- Original `JC8012P4A1_C6.bin`/HomeTiles use streaming mode; `JC-C6-slave_v2.3.2.bin` is packet mode, never flash it alone. USB reaches P4 only; C6 needs CN5 and a 3.3 V UART.
- Fix b6 passed reporter tests (two cameras 15-20 FPS, Web OTA); confirmed v0.6.9b1, shipped v0.6.10. Exact V1 keeps 1-bit/40 MHz, splits large RX into 512-byte CMD53 reads. Lower camera quality/FPS only as labeled diagnostic A/B.

## ESP32-P4 network history

- Backported: ESP-Hosted allocation/PSRAM fixes, synchronous RPC UID routing, Espressif's `a8204f9` dropped-RX recovery, sparse diagnostics. Patches, variants, hashes, limits: `tools/esp-hosted-3.3.7-rx-fix/README.md`.
- `repo-a8204` is the release-safe baseline; the short-tail variant was experimental.
- Failed P4 OTA approaches: throttling, PSRAM staging, TLS-to-flash streaming, Hosted restart, permanent SDIO buffers; retry only with new evidence.
- Network wedge safeguards are recovery, not a transport fix.

## Issue #38

- Reporter: JC8012P4A1 V2, SKU10153001-V2 (2632), `_I_W_Y`; #18 tested SKU10153002-V2 (2627), `_I_W_Y1`. Maintainer received JC8012P4A1C_I_W_Y1, SKU10153002-V2. Labels alone prove no other panel.
- V2 fixes committed in `e1a9297`: touch bounds, internal I2C atomic-state allocation, slot-aware SD cleanup; exact-V2 only. Beta `HOMETILES_ISSUE38_BETA` reports v0.6.12b1; release version stays v0.6.12.
- SD: reporter card-init failure (40/20 MHz), then Hosted slot-1 assertion. V2 dropped DEINIT_ARG from default host flags like V1; maintainer SD diagnostic passes (~8 GB). Assertion and card failure causes open (#55). Evidence: `build/issue-38/SD-LOG-ANALYSIS.md`.
- Touch: maintainer confirms rapid-tap raw-bounds fix works. BIN/ELF: `build/guition-v2-touch/`.
- Interrupt-WDT dump (touch ELF): I2C atomic-state object was in PSRAM; backport `37758ef327f9` forces internal allocation. Exposure proven, WDT causality unproven. Evidence/hash: `build/guition-v2-crash-20260911/`.
- Bridge v0.6.47 (`43be012`): HA forecast subscriptions replace minute polling; 134 tests pass, restart validation pending. v0.6.12 keeps daily extrema (24 C daily vs partial hourly 11 C).

## Sensor history

- Binary Sensor (20): V7-compatible; localized icons/previews and history shipped.
- Textual states use timeline/Activity; numeric keep graphs. Missing/unknown/unavailable stay distinct.
- Bridge v0.6.40 (`581150b`): bounded Recorder paging, categorical history, legacy compatibility.

## Editable tiles

- IDs 21 Number, 22 Select, 23 Date/Time reuse Sensor persistence/popups and five fonts; preview preserves `editableValues`, `PackedTileV7` unchanged.
- Number/input_number uses the centered Media slider/value, Climate +/- pill or bounded roller, with graph/Activity. Select/input_select uses Settings dropdowns, timeline and Activity.
- Date/Time: single-row hh/mm/ss rollers, popup-colored pill, no arrows, native 23/00 and 59/00 wrap; date spinboxes without keyboard; HA timezone/DST validation.
- Additive `/control` preserves legacy clients; sessions/revisions/deadlines reject stale commands.
- Bridge v0.6.44 (`148dec4`): stale icon cache fixed, overrides kept. v0.6.10 has `84511da` plus title/color fixes.
- Controls clear wrapped titles/close area; Number/Select equal height, Time taller. Select has compact history/earlier Activity; status in header. Range changes retain data; offline closes dropdowns.
- Drafts coalesce steps/rollers for 600 ms, publish sliders on release and survive service ACKs until confirmation/rejection or 30-second timeout.
- Editable surfaces: control fill, white text; Select list = card + hairline, gap, inset selection.
- Titles (approved): two centered/ellipsized lines; 255 UTF-8 bytes in `/_tile_titles`; Settings v4 size unchanged; view labels flatten CR/LF.
- S3 froze adding Number to active screensaver (Web ok, manual reboot); cause unknown.

## Shared-popup/artwork baseline

- v0.6.11: `3b534ab` shared frame/header/close with cached bodies; matching content stays visible, cold content waits for first frame. Close/switch/delete cancel work; PIN retained. Settings forms disposable, Camera preloaded.
- Artwork: URL-only `state_fast` precedes MQTT; failed replacements retain covers. URL/content pairing prevents S3 redownloads/stale results; deferred Media resolves current descriptors.
- Memory: LVGL PSRAM S3 2 MiB/P4 12 MiB; internal/DMA band <=72 KiB; page caches S3 4/P4 6. Bindings, popup history, Select options in PSRAM (b123); no extra framebuffers.
- Popup/title fixes accepted on 8-inch, S3, 4B, Tab5. Pending: artwork, sleep/wake, camera soak, memory minima.
- Colors: `tone_color.h` OKLCH (+0.06 L at 25 %); circles/controls opaque (16-bit blending lost the step), veil only on see-through screensaver tiles; controls tint only From icon/cover; dark icons lifted. Presses: no theme recolor (tiles too) or teal fade, PIN/pill buttons a step up, circles fade with tiles (b132, HW pending).
- LVGL 9.6.0 (lvgl#10306): S3 recolor 230->5 ms; own lib, caches `hometiles-lvgl96-*`.
- Reverted: b126 cover fade (RGB565: few dark-tint steps), b128 UI frame swap (input lag). Next: measure V2 tile redraw.

## Radius and half-grid

- NVS radius: old radius to `(cell h - gap)/4`, unset = max; tile color `#1A1A1A`; new icon tiles "From icon" 20 %. Used by tiles, Climate, popups, previews.
- Half-grid: Sensor/Binary/Energy height 0.5, width >=1 by 0.5; original 2x1. Whole layouts/V7 size unchanged; header fraction bits. Make every tile whole before 0.6.x (`docs/updating.md`).
- Device/Web: concentric icon radius, original title font, smaller default value font; explicit sizes respected; text gap 0. Masking declined.
- Reflow/drafts/rollback keep positions; stale GETs preserve edits. Empty 1x1 slots scan both axes by 0.5, no overlaps. Settings/Back: 1x0.5; Settings v4 bits 1-4 store snapshot fractions.
- Hidden Climate reset crash (fractional Sensor width): Climate-only integer guard.
- Binary Sensor shares Sensor value sizes (20/24/32/40); default preserves old layout. Stored in existing V7 field; editor, import and previews retain it.
- HW pending (evidence in `build/`): radius reboot, screensaver child-click, Energy compact slots, Clock/Text border.
- Weather icons (`tools/generate-weather-icon-fonts.mjs`): colored, toggle, icon tint; night: bridge `sun`; b98 HW
- Clock/Text per-tile border: V7 display-mode byte 1=hidden; global/screensaver toggles respect it.
- Open: cross-grid import clamps whole tiles to half steps (HTTP 400), snapshot type accepts halves, value fonts 32/40 clip in half tiles.

## Local camera

- Core `src/video/local_camera/` is device-agnostic; drivers `sensors/*/`, boards `src/devices/*/local_camera_board.*`; opt-in `local_cam_en`.
- V2 OV02C10 720p. 8-inch OV5647 544x960, Bridge `rotate` 90. Tab5 SC202CS 720p RAW8, mirror+180, BGGR under flips.
- Camera builds pass; HW pending (#56): WS 7/10.1/7B/4.3/4B OV5647, V1/JC1060 V2 OV02C10, JC4880 quarter turn.
- Advanced: `lcam_rot` (180 = flip, odd = `rotate` 90), `lcam_rbswap` (Bayer, next start).
- HW pending: b30 CSI/ISP, q10; b31 gain, q10-90; b32 screenshot; b33 Wi-Fi/AP, kbd, #43; b34 rotation, boards; b35 sleep stream/indicator wake, Tab5 1% wake, S3 150Hz.
- Open: int WDT fix HW test, TEST `kChunkWindow = 2`.

## v0.7.0 release

- v0.7.0 (`609550a`): public, S3 assets from `c3e0a673`; P4 unchanged. Existing S3 v0.7.0 needs Web Admin update.
- Bridge v0.7.0 (`1c12eda`): 267 pass/1 skip. Evidence: `build/release-v0.7.0/`.
- Cache b73 frees queues. Five owned devices stable.
- S3 fix `c3e0a673`: PSRAM-first OTA TLS, all three profiles. Guition b74 OTA/boot/MQTT passed; Waveshare HW pending.
- CI `36334088502`: 17 builds, 152 tests/21 skips; six S3 BINs verified.
- v0.7.1 promised: PR #51 Polish port, #26 S3-4B PCLK (16 vs 10 MHz), P4 v3.2 for 7B (#41) and JC8012 V3 (#44) incl. installer stub crash; French.
- Tests pending #7 #11 #27 #34 #45; V2 SD restart #55.

## Maintenance

- Docs: `docs/`, `mkdocs.yml`, `overrides/`; gh-pages. Installer esptool-js 0.7.0 (#57) passed Update on five owned devices; P4 v3.x pending (#41/#44).

## Flash and RAM (PR #62)

- `-fno-exceptions` (`compiler.cpp.flags`, CI and local): about -300 KiB per image, Tab5 428 KiB OTA headroom. Unused LVGL widgets/formats off: -49 KiB, IRAM -21 KiB.
- Renderer slot state, Binary Sensor queue and active/screensaver grids live in PSRAM, allocated in `setup()` (not constructors). Static DRAM V2 136,632 -> 64,984 B, S3 139,576 -> 80,376 B. V2/S3 b75 passed.

## View control and telemetry

- v0.6.10: Home/folder/popup navigation (`4c9ea4e`) via existing UI, PIN and camera teardown paths.
- Visible folders are reused for their popup/descendants; new/locked paths still require access checks (S3 Home detour fix).
- Stable tile IDs use reserved PackedTileV7 bytes and durable counters; MQTT sessions/sequences/deadlines reject replay.
- Switch adds input_boolean/automation/fan/humidifier/remote/siren; Scene adds button/input_button. Aliases stay stable.
- Commands validate targets/availability/features; ignore retained commands. Battery is a stub; unsupported probes stay unregistered.
- View/editable controls confirmed on 8-inch/S3. HA migration, legacy firmware and lifecycle coverage pending.
- Issue #37: valid 20,033-byte packet disconnects v0.6.9 at 16 KiB; reception up to 65,535 bytes with bounded queues/draining/ACKs/logs. Reporter confirmation pending; no unplanned MQTT loss in ~8 h.

## Security branch (unreleased)

- Web Admin password (optional): PBKDF2 key (300k iter) derived by browser/Bridge, panel stores it; HMAC login, 30-day NVS sessions, CSRF; device reset; hides secrets.
- Command channel (optional): pairing v2 (X25519, 6-digit code on panel/HA), sealed commands/stream tokens, replay window, two-sided unpair, signed announcement; `docs-dev/command-encryption.md`. P4 random: SAR ADC. V2/S3 tested. b125 rekey resets a stale session; HW pending.

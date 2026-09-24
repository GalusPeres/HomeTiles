# HomeTiles shared project context

Last reviewed: 2026-09-11

## Sources of truth

- Version: `version.txt`; code: `git status`, recent commits/branch
- Device support and validation: `docs/index.md` (device status notes)
- ESP32-P4/ESP-Hosted patches: `tools/esp-hosted-3.3.7-rx-fix/README.md`
- Release procedure: `RELEASING.md`
- Live bug status: the current GitHub issue and its newest comments; recheck
  online before changing an issue status
- Bridge is a separate repository; firmware authorization does not cover Bridge publishing or vice versa.

## Firmware baseline

- v0.6.12: `9605b6a`, CI `34353664113`, 15 profiles / 30 images; 102 tests pass. Guition V1/V2 PPA and Weather fixes; V2 confirmed, V1 hardware pending.
- Stabilization: display/MQTT guards, Light coalescing, incremental Weather (`e3de63c`-`33b4e06`).
- S3 TLS fallback: 87 tests/three builds pass; Guition OTA passed, Waveshare S3 pending. Earlier watchdogs unproven: `build/s3-ota-release-v0.6.10/`.
- Guition S3 XIP/`-O2` was reverted in `5279456`: increased risk without solving measured interaction problems. Do not reintroduce without evidence.

## Hardware validation

- Maintainer hardware: Tab5, Waveshare 4B/8-inch, Guition S3/V2 (JC8012P4A1C_I_W_Y1, SKU10153002-V2). V2 Tested, PPA/SD confirmed; V1 hardware pending.
- v0.6.9 Binary/Text-State Sensor UI passed hardware tests on 4B, 8-inch and S3.
- Other revisions need community hardware validation; compilation does not establish support.
- P4 application code is shared; panel/touch controllers, initialization, timing, board revision and firmware images remain exact-profile concerns.
- LCD-4 Rev 4.0 has contributor-tested display/touch/Wi-Fi/MQTT/Web OTA;
  older revisions and SD access are unsupported. See `docs/index.md` for validation.
- JC4880P443 (PR #46, damianeek): portrait 480x800/4x6, contributor-tested; landscape later. Open: SD DEINIT_ARG, P4 DSI groups, tall popups.
- WS 10.1 v3 (PR #48, memooox3): separate `waveshare_10_1_rev3`, post_v3 301-399; pre-v3 image unchanged. Chip-id/CI image test pending.

## Issue #30

Issue: https://github.com/GalusPeres/HomeTiles/issues/30

- External Guition `JC8012P4A1C_I_W_Y` V1, Foscam via HA Generic Camera; normal OTA failed, USB worked. SDIO cascade (CMD53 `0x109`, timeout `0x107`, raw `0xcccccccc`, invalid RX length, `rst:0xc`), also without cameras; recovery restarts leave no panic dump. First DCRC `0x80` on 11-/14-block C6-to-P4 reads. Repeated a8204 markers, 20 MHz (b3, also Issue #167), 1-bit alone (b5) and the 2.9.3 rollback are not fixes; do not retry them.
- Version RPC `0x15e` also occurs on the stable 8-inch; it does not explain the cascade.
- SDIO schematics: V1 5.1-kohm pull-ups/no series termination; 8-inch 51-kohm; Tab5 5.1-kohm/22-ohm series/switched WLAN power. Signal margin unproven.
- Original `JC8012P4A1_C6.bin` and HomeTiles use streaming mode; `JC-C6-slave_v2.3.2.bin` is packet mode, never flash it alone. USB reaches P4 only; C6 flashing needs CN5 and a 3.3 V UART adapter.
- Fix b6 passed reporter tests (two cameras at 15-20 FPS, Web OTA); reporter confirmed v0.6.9b1, v0.6.10 ships it. Exact V1 keeps 1-bit/40 MHz and splits large RX into 512-byte CMD53 reads; other P4 profiles keep baseline objects, S3 unaffected. Lower camera quality/FPS only as a labeled diagnostic A/B.

## ESP32-P4 network history

- The repository already backports ESP-Hosted allocation/PSRAM fixes,
  synchronous RPC UID routing, Espressif's `a8204f9` dropped-RX recovery, and
  sparse diagnostics. Exact patches, variants, hashes, and limitations are in
  `tools/esp-hosted-3.3.7-rx-fix/README.md`; do not duplicate them here.
- The `repo-a8204` variant is the release-safe baseline. The short-tail receive
  variant was an experimental field path and is not proof of a universal fix.
- Failed P4 OTA approaches: throttling, PSRAM staging, TLS-to-flash streaming, Hosted restart, permanent SDIO buffers. Require new evidence before retrying.
- Network wedge safeguards are recovery, not
  proof that the transport defect is solved.

## Issue #38

- Reporter: JC8012P4A1 V2, SKU10153001-V2 (2632), `_I_W_Y`; #18 tested SKU10153002-V2 (2627), `_I_W_Y1`. Maintainer received JC8012P4A1C_I_W_Y1, SKU10153002-V2. Labels alone do not establish another panel variant.
- V2 fixes committed in `e1a9297`: touch bounds, internal I2C atomic-state allocation, slot-aware SD cleanup; exact-V2 only. Beta `HOMETILES_ISSUE38_BETA` reports v0.6.12b1; release version stays v0.6.12.
- SD: reporter card-init failure at 40 MHz, clock timeout on 20 MHz retry, then Hosted slot-1 assertion. V2 dropped DEINIT_ARG from default host flags; preserve defaults as V1 already does. Regression and V2 build pass; 105 tests. Maintainer beta SD diagnostic passes mkdir/write/read/remove (~8 GB). Without DEINIT_ARG, core 3.3.7 passes a pointer that deinit rejects, so slot 1 is untouched; assertion and card failure causes remain open. Evidence: `build/issue-38/SD-LOG-ANALYSIS.md`; package/hash: `build/guition-v2-v0.6.12b1/VERIFICATION.md`.
- Touch: maintainer confirms rapid-tap raw-bounds fix works. BIN/ELF: `build/guition-v2-touch/`.
- Interrupt-WDT dump matches touch ELF SHA256 `af562d1cea4dc3d8096ec17cd631e45cc6d82ab1ea6fb0037c03e8638c234a67`; IDLE1 waits for interrupt, setup uploads touch firmware. I2C atomic-state object at `0x483e96bc` is in PSRAM. Backport `37758ef327f9` ensures internal allocation (ELF caps 0x804). Exposure proven, WDT causality unproven; no wall-clock timestamp. Evidence/hash: `build/guition-v2-crash-20260911/`.
- Bridge v0.6.47 (`43be012`): HA forecast subscriptions replace minute polling; 134 tests pass, restart validation pending. Released firmware v0.6.12 preserves daily extrema (24 C daily versus partial hourly 11 C).

## Sensor history

- Binary Sensor (20): V7-compatible; localized icons/previews and history shipped.
- Textual states use timeline/Activity; numeric sensors retain graphs. Missing, unknown and unavailable remain distinct.
- Bridge v0.6.40 (`581150b`): bounded Recorder paging, categorical history, legacy compatibility.

## Editable tiles

- IDs 21 Number, 22 Select, 23 Date/Time reuse Sensor persistence/popups and five fonts; preview preserves `editableValues`, `PackedTileV7` unchanged.
- Number/input_number uses the centered Media slider/value, Climate +/- pill or bounded roller, with graph/Activity. Select/input_select uses Settings dropdowns, timeline and Activity.
- Date/Time: single-row hh/mm/ss rollers, popup-colored pill, no arrows, native 23/00 and 59/00 wrap; date spinboxes without keyboard; HA timezone/DST validation.
- Additive `/control` preserves legacy clients; sessions/revisions/deadlines reject stale commands.
- Bridge v0.6.44 (`148dec4`) on HACS: stale icon cache fixed, overrides preserved. v0.6.10 includes `84511da` plus title/color fixes.
- Controls clear wrapped titles/close area; Number/Select equal height, Time taller. Select has compact history/earlier Activity; status in header. Range changes retain data; offline closes dropdowns.
- Drafts coalesce steps/rollers for 600 ms, publish sliders on release and survive service ACKs until confirmation/rejection or 30-second timeout.
- Editable surfaces follow tile color, white text unchanged; selection white with surface-colored text; S3 arrow 20px. 4096 colors/seven layouts tested: `build/editable-colors-view/`.
- Wi-Fi idle/reconnect gaps remain unfixed; findings/probes: `build/wifi-power-audit/VERIFICATION.md`.
- Titles: two centered/ellipsized lines; 255 UTF-8 bytes in `/_tile_titles`; Settings record v4 unchanged. View labels flatten CR/LF for Bridge compatibility. Maintainer approved.
- S3 froze adding Number to active screensaver: Web answered, save persisted, user rebooted; crash log has an older ELF. Cause unproven; retained as a release validation limitation.

## Shared-popup/artwork baseline

- v0.6.11: `3b534ab` shared frame/header/close with cached bodies; matching content stays visible, cold content waits for first frame. Close/switch/delete cancel work; PIN retained. Settings forms disposable, Camera preloaded.
- Artwork: URL-only `state_fast` precedes MQTT; failed replacements retain covers. URL/content pairing prevents S3 redownloads/stale results; deferred Media resolves current descriptors.
- Memory: LVGL PSRAM S3 2 MiB/P4 12 MiB; internal/DMA band <=72 KiB; page caches S3 4/P4 6. Bindings in PSRAM, no extra framebuffers.
- Popup/title fixes accepted on 8-inch, Guition S3, 4B and Tab5; native tests cover 17 profiles. Pressed-state layout fix: `build/tile-state-layout/`; title evidence: `build/weather-title-ellipsis/`, `build/test-devices-popup-title/`.
- Reused PIN popup full title corrected (Tab5 confirmed): `build/pin-popup-title/`. Broader artwork/controls, sleep/wake, camera/Hosted soak and memory minima pending.

## Radius and half-grid

- NVS radius: existing minimum; maximum = rounded `(profile cell height - gap) / 4`. Shared cached tiles, Climate, popups/Settings, artwork and live Web previews.
- Radius/close-highlight V2 accepted; reboot pending. Evidence: `build/global-radius-guition-v2-corrections/`.
- Half-grid: Sensor/Binary/Energy height 0.5, width >=1 in half steps; original 2x1. Whole layouts/V7 size unchanged; header bits store fractions. Fractional-layout downgrade unsupported.
- Device/Web: concentric icon radius, original title font, smaller default value font; explicit sizes respected; text gap 0. Masking declined.
- Half-grid reflow restored; drafts/rollback retain positions, stale GETs preserve edits. Empty 1x1 slots now scan both axes in half steps without overlaps. Settings/Back integral.
- Hidden Climate reset crashed on fractional Sensor width; Climate-only integer guard fixes it; browser regression covers switching/autosave.
- Binary Sensor now shares Sensor value sizes (20/24/32/40); default preserves old layout. Stored in existing V7 field; editor, import and previews retain it.
- Hardware pending: screensaver child-click `build/screensaver-click-guition-v2/`, Energy compact/empty slots `build/half-grid-empty-guition-v2/`.

- Popup-overlapped buttons briefly appear square; deferred by user, no fix.

- Clock/Text per-tile border: V7 display-mode byte 1=hidden; global/screensaver toggles respect it. V2 BIN `build/clock-text-border-guition-v2/` (SHA256 `8752AF47...62C4`); hardware pending.
- Open: cross-grid import clamps whole tiles to half steps (HTTP 400), snapshot type accepts halves, value fonts 32/40 clip in half tiles.

## Local camera (branch `local-camera-beta`)

- Core `src/video/local_camera/` names no device/sensor; drivers `sensors/<name>/`, boards `src/devices/<device>/local_camera_board.*`; opt-in `local_cam_en`.
- V2 OV02C10 720p. 8-inch OV5647 544x960 + `"rotate":90` (Bridge turns). Tab5 SC202CS 720p RAW8, mirror+180; keeps BGGR itself (no window shift).
- b30: fresh CSI/ISP per start (8-inch swapped colours/no frames after restart), oversize floor q10; HW pending.
- b31: Web Admin Max. gain (0-100 %, log, default 100), Custom quality 10-90; HW pending.
- Open: int WDT fix HW test, TEST `kChunkWindow = 2`.

## Maintenance

- Docs: `docs/`, `mkdocs.yml`, `overrides/`; `HomeTiles/gh-pages` v0.6.12 at both mounts. USB installer tests are simulated.

## View control and telemetry

- v0.6.10 includes Home/folder/popup navigation from `4c9ea4e`, using existing UI, PIN and camera teardown paths.
- Visible folders are reused for their popup/descendants; new/locked paths still require access checks (S3 Home detour fix).
- Stable tile IDs use reserved PackedTileV7 bytes and durable counters; MQTT sessions/sequences/deadlines reject replay.
- Switch adds input_boolean/automation/fan/humidifier/remote/siren; Scene adds button/input_button. Aliases stay stable.
- Commands validate targets/availability/features; ignore retained commands. Battery is a stub; unsupported probes stay unregistered.
- View/editable controls confirmed on 8-inch/S3; tests/BINs: `build/editable-colors-view/VERIFICATION.md`. HA migration, legacy firmware and lifecycle coverage pending.
- Issue #37: valid 20,033-byte packet disconnects v0.6.9 at 16 KiB; local reception grows to 65,535 bytes with bounded queues/draining/ACKs/logs. Reporter confirmation pending. Maintainer log: no unplanned MQTT loss over ~8 h.

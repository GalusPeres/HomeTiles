# HomeTiles shared project context

Last reviewed: 2026-09-11

## Sources of truth

- Firmware version: `version.txt`
- Current code: `git status`, recent commits, checked-out branch
- Device support and validation: `docs/index.md` (device status notes)
- ESP32-P4/ESP-Hosted patches: `tools/esp-hosted-3.3.7-rx-fix/README.md`
- Release procedure: `RELEASING.md`
- Live bug status: the current GitHub issue and its newest comments; recheck
  online before changing an issue status
- HomeTiles Bridge is a separate repository. Firmware work never implicitly
  authorizes Bridge commits, pushes, or releases, and vice versa.

## Current firmware baseline

- v0.6.12: `9605b6a`, CI `34353664113`, 15 profiles / 30 images; 102 tests pass. Guition V1/V2 PPA and Weather fixes; V2 confirmed, V1 hardware pending.
- Stabilization: S3 display/update guards, MQTT validation, Light coalescing and incremental Weather (`e3de63c`–`33b4e06`).
- S3 TLS fallback: 87 tests/three builds pass; Guition OTA passed all 11 ranges, Waveshare S3 field tests pending. Earlier TLS error/two watchdogs unproven. Evidence: `build/s3-ota-release-v0.6.10/`.
- Guition S3 XIP/`-O2` was reverted in `5279456`: increased risk without solving measured interaction problems. Do not reintroduce without evidence.

## Hardware and validation reality

- Maintainer hardware: Tab5, Waveshare 4B/8-inch, Guition S3/V2 (JC8012P4A1C_I_W_Y1, SKU10153002-V2). V2 Tested, PPA/SD confirmed; V1 hardware pending.
- v0.6.9 Binary/Text-State Sensor UI passed hardware tests on 4B, 8-inch and S3.
- Other exact revisions depend on community testers. A successful compile does
  not promote an untested revision to supported status.
- P4 application code is shared; panel/touch controllers, initialization, timing, board revision and firmware images remain exact-profile concerns.
- LCD-4 Rev 4.0 has contributor-tested display/touch/Wi-Fi/MQTT/Web OTA;
  older revisions and SD access are unsupported. See `docs/index.md` for validation.

## Active problem: GitHub issue #30

Issue: https://github.com/GalusPeres/HomeTiles/issues/30

- External Guition `JC8012P4A1C_I_W_Y` V1; Foscam via HA Generic Camera. Normal OTA failed; USB worked.
- b1/b2: CMD53 `0x109`, timeout `0x107`, raw `0xcccccccc`, invalid RX length and `rst:0xc`, also without cameras. Recovery restarts leave no panic dump.
- b1 already contained a8204 raw-PKT_LEN/pending-drain and short-tail markers; repeating that patch is not a solution. Version RPC `0x15e` also occurs on stable Waveshare 8-inch and is insufficient to explain the fatal Guition transport cascade.
- Do not use/publish b3 (4-bit/20 MHz, no first-fault diagnostics). Issue #167 also found 20 MHz unreliable. b4 located the first DCRC `0x80` on 11-/14-block C6-to-P4 reads, before `0x109`/`0x107`. b5 1-bit/40 MHz still failed, sometimes before Camera; lane reduction alone is not a fix.
- SDIO schematics: V1 5.1-kohm pull-ups/no series termination; 8-inch 51-kohm pull-ups; Tab5 5.1-kohm pull-ups/22-ohm series resistors/switched WLAN power. Signal margin remains unproven.
- Original `JC8012P4A1_C6.bin` and HomeTiles use streaming mode. New `JC-C6-slave_v2.3.2.bin` uses packet mode: do not flash it alone. USB reaches P4 only; C6 flashing needs CN5 and a 3.3 V UART adapter.
- Do not retry the reported 2.9.3 rollback: it concerns another board's `0x102` TX/alignment failure, not this CRC path, and discards relevant safety fixes.
- b6 passed reporter hardware tests: two cameras at 15-20 FPS and Web OTA, without the transport cascade. Exact V1 retains 1-bit/40 MHz and splits large RX into individual 512-byte CMD53 reads. Reporter confirmed integrated v0.6.9b1; v0.6.10 ships it.
- Keep the single-block marker/1-bit configuration exact-V1 only; other P4 profiles retain baseline objects, S3 is unaffected. Lower camera quality/FPS/resolution is allowed only for a labeled diagnostic A/B, never a silent final fix.

## ESP32-P4 network history that remains relevant

- The repository already backports ESP-Hosted allocation/PSRAM fixes,
  synchronous RPC UID routing, Espressif's `a8204f9` dropped-RX recovery, and
  sparse diagnostics. Exact patches, variants, hashes, and limitations are in
  `tools/esp-hosted-3.3.7-rx-fix/README.md`; do not duplicate them here.
- The `repo-a8204` variant is the release-safe baseline. The short-tail receive
  variant was an experimental field path and is not proof of a universal fix.
- P4 OTA experiments showed that generic transfer throttling,
  PSRAM-only staging, direct TLS-to-flash streaming, in-place ESP-Hosted
  restart, and extra permanent SDIO buffers did not cure the underlying
  failure. Do not repeat them without new evidence and an isolated test.
- Network wedge safeguards are recovery, not
  proof that the transport defect is solved.

## Active problem: GitHub issue #38

- Reporter: JC8012P4A1 V2, SKU10153001-V2 (2632), `_I_W_Y`; #18 tested SKU10153002-V2 (2627), `_I_W_Y1`. Maintainer received JC8012P4A1C_I_W_Y1, SKU10153002-V2. Labels alone do not establish another panel variant.
- V2 fixes committed in `e1a9297`: touch bounds, internal I2C atomic-state allocation, slot-aware SD cleanup; exact-V2 only. Beta `HOMETILES_ISSUE38_BETA` reports v0.6.12b1; release version stays v0.6.12.
- SD: reporter card-init failure at 40 MHz, clock timeout on 20 MHz retry, then Hosted slot-1 assertion. V2 dropped DEINIT_ARG from default host flags; preserve defaults as V1 already does. Regression and V2 build pass; 105 tests. Maintainer beta SD diagnostic passes mkdir/write/read/remove (~8 GB). Reporter validation/initial card failure cause pending. Evidence: `build/issue-38/SD-LOG-ANALYSIS.md`; package/hash: `build/guition-v2-v0.6.12b1/VERIFICATION.md`.
- Touch: maintainer confirms rapid-tap raw-bounds fix works. BIN/ELF: `build/guition-v2-touch/`.
- Interrupt-WDT dump matches touch ELF SHA256 `af562d1cea4dc3d8096ec17cd631e45cc6d82ab1ea6fb0037c03e8638c234a67`; IDLE1 waits for interrupt, setup uploads touch firmware. I2C atomic-state object at `0x483e96bc` is in PSRAM. Backport `37758ef327f9` ensures internal allocation (ELF caps 0x804). Exposure proven, WDT causality unproven; no wall-clock timestamp. Evidence/hash: `build/guition-v2-crash-20260911/`.
- Bridge v0.6.45 aggregates twice-daily forecasts; missing retained data after HA restart recovered by reloading Bridge.
- Bridge v0.6.47 (`43be012`): HA forecast subscriptions replace minute polling; 134 tests pass, restart validation pending. Released firmware v0.6.12 preserves daily extrema (24 C daily versus partial hourly 11 C).

## Binary and textual Sensor history in v0.6.9

- Sensor type 20 reuses `sensor_entity`/`PackedTileV7`; DE/EN/FR, state-aware icons, autosave/previews and 24H/7D history shipped.
- Textual states use timeline/Activity; numeric sensors retain graphs. Missing, unknown and unavailable remain distinct.
- Bridge v0.6.40 (`581150b`) released bounded Recorder paging, categorical history and legacy-firmware compatibility.

## Editable value tiles in v0.6.10

- IDs 21 Number, 22 Select, 23 Date/Time reuse Sensor persistence/popups and five fonts; preview preserves `editableValues`, `PackedTileV7` unchanged.
- Number/input_number uses the centered Media slider/value, Climate +/- pill or bounded roller, with graph/Activity. Select/input_select uses Settings dropdowns, timeline and Activity.
- Date/Time: single-row hh/mm/ss rollers, popup-colored pill, no arrows, native 23/00 and 59/00 wrap; date spinboxes without keyboard; HA timezone/DST validation.
- Additive `/control` preserves legacy clients; sessions/revisions/deadlines reject stale commands.
- Bridge v0.6.44 (`148dec4`) is on HACS; fixes stale icon cache, preserves overrides. 119 Bridge tests pass. The v0.6.10 release includes checkpoint `84511da` and subsequent title/color fixes.
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
- Maintainer accepts popup/title fixes on 8-inch, Guition S3, 4B and Tab5. Native Weather/Sensor tests cover 17 profiles. Removed state-specific zero translations/border widths that forced descendant layout on press. Evidence: `build/tile-state-layout/`, `build/weather-title-ellipsis/`, `build/test-devices-popup-title/`.
- Reused PIN popup full title corrected (Tab5 confirmed): `build/pin-popup-title/`. Broader artwork/controls, sleep/wake, camera/Hosted soak and memory minima pending.

## Global tile radius

- NVS radius: current minimum; maximum = rounded `(profile cell height - gap) / 4`. Half-height spans not implemented.
- Shared styles cover cached tiles, Climate, popup/Settings and artwork. Web previews immediately, saves on release; wallpaper refresh coalesces 350 ms.
- First V2 build: 107 tests pass, native radius/NVS covers 17 profiles. BIN/hash: `build/global-radius-guition-v2/VERIFICATION.md`. Maintainer likes the main appearance.
- Follow-up: restore original close-highlight baseline with live updates; align Web radius control with checkboxes, suppress pointer-focus field ring. 107 tests and V2 build pass; BIN/hash: `build/global-radius-guition-v2-corrections/VERIFICATION.md`. Hardware/reboot checks pending.

## Current maintenance refactoring

- Architecture/workflows: `ARCHITECTURE.md`, `CONTRIBUTING.md`; host dependencies need `npm ci --ignore-scripts`.
- Docs: `docs/`, `mkdocs.yml`, `overrides/`; `HomeTiles/gh-pages` v0.6.12 at both mounts. Sitemap aliases fix reloads. Flash success clears on reload/selection change; recovery persists. USB tests are simulated.

## Current view control, telemetry and compatible controls

- v0.6.10 includes Home/folder/popup navigation from `4c9ea4e`, using existing UI, PIN and camera teardown paths.
- Visible folders are reused for their popup/descendants; new/locked paths still require access checks (S3 Home detour fix).
- Stable tile IDs use reserved PackedTileV7 bytes and durable counters; MQTT sessions/sequences/deadlines reject replay.
- Bridge v0.6.44 retains documented View/telemetry migration and compatible controls.
- Switch adds input_boolean/automation/fan/humidifier/remote/siren; Scene adds button/input_button. Aliases stay stable.
- Commands validate targets/availability/features; ignore retained commands. Battery is a stub; unsupported probes stay unregistered. Bridge migration preserves user entities and validates ownership/capabilities.
- View/editable controls confirmed on 8-inch/S3; tests/BINs: `build/editable-colors-view/VERIFICATION.md`. HA migration, legacy firmware and lifecycle coverage pending.
- Issue #37: valid 20,033-byte packet disconnects v0.6.9 at 16 KiB; local reception grows to 65,535 bytes with bounded queues/draining/ACKs/logs. Reporter confirmation pending. Maintainer log: no unplanned MQTT loss (~7.5 h earlier BIN, ~25 min latest; two OTA restarts).

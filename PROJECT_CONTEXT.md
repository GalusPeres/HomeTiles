# HomeTiles shared project context

Last reviewed: 2026-09-09

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

- v0.6.12 prepared: Guition V1/V2 UI PPA and Weather alignment; daily extrema preserved. 102 tests pass; release CI pending. V2 confirmed, V1 hardware pending.
- Stabilization: S3 display/update guards, MQTT validation, Light coalescing and incremental Weather (`e3de63c`–`33b4e06`).
- TLS fallback ships on all three S3 RGB profiles; 87 tests and three CI builds pass. Guition hardware OTA passed all 11 ranges first try; Waveshare S3 OTA awaits field tests. Prior TLS error/two boot watchdog resets remain unproven. Evidence: `build/s3-ota-release-v0.6.10/`.
- The experimental Guition S3 XIP/`-O2` performance path was reverted in
  `5279456`. Do not reintroduce it as an assumed optimization. It increased
  risk and did not solve the measured interaction problem.

## Hardware and validation reality

- Maintainer hardware: Tab5, Waveshare 4B/8-inch, Guition S3/V2. V2 PPA/SD confirmed; V1 port awaits CI/hardware. See `docs/releases/v0.6.12.md`.
- v0.6.9 Binary/Text-State Sensor UI passed hardware tests on 4B, 8-inch and S3.
- Other exact revisions depend on community testers. A successful compile does
  not promote an untested revision to supported status.
- P4 application code is shared; panel/touch controllers, initialization, timing, board revision and firmware images remain exact-profile concerns.
- LCD-4 Rev 4.0 has contributor-tested display/touch/Wi-Fi/MQTT/Web OTA;
  older revisions and SD access are unsupported. See `docs/index.md` for validation.

## Active problem: GitHub issue #30

Issue: https://github.com/GalusPeres/HomeTiles/issues/30

- External Guition `JC8012P4A1C_I_W_Y` V1; Foscam via HA Generic Camera. Normal OTA failed; USB worked.
- b1/b2 logs: first JPEG succeeds, then CMD53 `0x109`, timeout `0x107`, raw `0xcccccccc`, invalid RX length and `rst:0xc`. Failures also occur during MQTT startup without a camera. Recovery restarts explain the absence of a panic core dump.
- b1 already contained a8204 raw-PKT_LEN/pending-drain and short-tail markers; repeating that patch is not a solution. Version RPC `0x15e` also occurs on stable Waveshare 8-inch and is insufficient to explain the fatal Guition transport cascade.
- Do not use/publish b3 (4-bit/20 MHz, no first-fault diagnostics). Issue #167 also found 20 MHz unreliable. b4 located the first DCRC `0x80` on 11-/14-block C6-to-P4 reads, before `0x109`/`0x107`. b5 1-bit/40 MHz still failed, sometimes before Camera; lane reduction alone is not a fix.
- Schematics: Guition V1 has 5.1-kohm CMD/CLK/D0-D3 pull-ups without series termination; Waveshare 8-inch has 51-kohm pull-ups; Tab5 has 5.1-kohm pull-ups, 22-ohm series resistors and switched WLAN power. Signal/power margin is plausible, not proven by the working mitigation.
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

## Binary and textual Sensor history in v0.6.9

- Stable tile type 20 reuses `sensor_entity` without changing `PackedTileV7`; central DE/EN/FR strings, state-aware HA icons, autosave/previews and responsive 24-hour/7-day history are released.
- Textual states use timeline/Activity; numeric sensors retain graphs. Missing, unknown and unavailable remain distinct.
- Bridge v0.6.40 (`581150b`) released bounded Recorder paging, categorical history and legacy-firmware compatibility.

## Editable value tiles in v0.6.10

- IDs 21 Number, 22 Select, 23 Date/Time reuse Sensor rendering/persistence/popups; value font uses Sensor's five choices. Preview refresh preserves normalized `editableValues`; `PackedTileV7` is unchanged.
- Number/input_number uses the centered Media slider/value, Climate +/- pill or bounded roller, with graph/Activity. Select/input_select uses Settings dropdowns, timeline and Activity.
- Time/date/datetime/input_datetime: large single-row hh/mm/ss rollers in a pill matching popup color, no arrows, native 23/00 and 59/00 wrap; date spinboxes without keyboard. HA timezone/DST validation applies.
- Additive `/control` preserves legacy clients; service allow-lists, sessions, revisions and deadlines reject stale commands.
- Bridge v0.6.44 (`148dec4`) is on HACS; fixes stale icon cache, preserves overrides. 119 Bridge tests pass. The v0.6.10 release includes checkpoint `84511da` and subsequent title/color fixes.
- Control bands clear wrapped titles and the full close touch area. Number/Select share a height; Time is taller. Select keeps compact history and earlier Activity. Status shares the heading row. Range changes keep old data until reply; offline closes dropdowns.
- Drafts coalesce steps/rollers for 600 ms, publish sliders on release and survive service ACKs until confirmation/rejection or 30-second timeout.
- Editable popup surfaces derive from tile color; white text/fonts stay unchanged. Dropdown selection is white with surface-colored text; Guition S3 arrow uses 20px. Tests cover 4096 colors and seven layouts. Builds: `build/editable-colors-view/`.
- Wi-Fi audit: S3 idle (>3 s) requests MIN_MODEM/11 dBm; sleep/wake NONE/19.5. P4 blocks idle saving; boot/reconnect and failed-call caching have gaps on both. Unfixed; probes: `build/wifi-power-audit/VERIFICATION.md`.
- Titles: two centered lines with ellipsis, 255 UTF-8 bytes in `/_tile_titles`; Settings uses `set_title`, record v4 unchanged. View labels flatten CR/LF to fix Bridge `writable:false` from multiline S3 titles. Current builds approved by maintainer.
- S3 froze adding Number to active screensaver: Web answered, save persisted, user rebooted; crash log has an older ELF. Cause unproven; retained as a release validation limitation.

## Shared-popup/artwork checkpoint (2026-09-08)

- v0.6.11 includes checkpoint `3b534ab` and shared-style fixes resolving large Weather opening on 8-inch.
- Popups share one visible frame/header/close button and cached bodies; title/icon/color remain variable. Matching bodies/Sensor graphs stay visible, cold contents follow the first frame. Close/switch/deletion cancel pending work; PIN checks remain. Settings forms are disposable; Camera widgets preloaded.
- Artwork: Bridge URL-only `state_fast` precedes full MQTT. Blocked/failed replacements retain loaded covers; URL/content pairing prevents S3 redownloads/stale results. Deferred Media opening resolves current descriptors without borrowed pixels.
- Memory unchanged: PSRAM LVGL pools S3 2 MiB/P4 12 MiB; internal/DMA draw band capped at 72 KiB; page caches S3 4/P4 6. Bindings use PSRAM, no extra framebuffers. Larger covers and bounded idle Media service remain.
- Maintainer accepted fast 8-inch opening: removed state-specific zero translations/border widths that forced descendant layout. Tests use real global borders and Weather trees. BIN/hash: `build/tile-state-layout/VERIFICATION.md`.
- Weather values/preview headers match Sensor; runtime names use the shared title helper. Maintainer confirms `Viecht...` on 8-inch. 95 tests/build pass; BIN/hash: `build/weather-title-ellipsis/VERIFICATION.md`.
- Native Weather/Sensor tests cover all 17 profiles: real global styles, colors, short/long input, first-frame gating, geometry and covered drawing. Timing instrumentation is opt-in only.
- Maintainer confirms Guition S3, 4B and Tab5 builds work well; 95 tests, no popup timing. BINs/hashes: `build/test-devices-popup-title/VERIFICATION.md`.
- PIN reuse updates the full title; maintainer confirmed Tab5 correction. BIN/hash: `build/pin-popup-title/VERIFICATION.md`.
- Pending: broader controls/artwork, navigation, sleep/wake, camera/ESP-Hosted soak and memory minima. No post-fix serial timing comparison captured.

## Current maintenance refactoring

- Architecture/workflows: `ARCHITECTURE.md`, `CONTRIBUTING.md`; host dependencies need `npm ci --ignore-scripts`.
- Docs source: `docs/`, `mkdocs.yml`, `overrides/`; root hosting deploys `HomeTiles/gh-pages`.
- Docs: `/HomeTiles/` reloaded because the root-canonical sitemap omitted that mount. Sitemap aliases preserve native navigation at both mounts; saved flash results no longer overwrite current USB status. Browser regressions cover both.
- Public docs serve v0.6.11 at both mounts; navigation/USB checks use simulated ports.
- Flash success clears on reload or after viewing and leaving/changing selection; recovery persists. USB status is active-only. Logger help: paragraphs, menu restart or RESET if fitted, local-only log notice.

## Current view control, telemetry and compatible controls

- v0.6.10 includes Home/folder/popup navigation from `4c9ea4e`, using existing UI, PIN and camera teardown paths.
- Visible folders are reused for their popup/descendants; new/locked paths still require access checks (S3 Home detour fix).
- Stable tile IDs use reserved PackedTileV7 bytes and durable counters; MQTT sessions/sequences/deadlines reject replay.
- Bridge v0.6.44 (`148dec4`) retains View/telemetry migration and compatible controls. The firmware documentation now covers these features.
- Switch adds input_boolean/automation/fan/humidifier/remote/siren; Scene adds button/input_button. Aliases stay stable.
- Commands validate selected targets, availability and on/off features; retained commands are ignored.
- Firmware battery is a stub on all profiles. Unsupported battery/probes are no longer auto-registered; explicit local I/O remains.
- Bridge migration checks registry ownership/capabilities, cleans shared selections and preserves user entities.
- Pre-OTA-fix verification: 85 tests and S3/8-inch builds; hashes: `build/editable-colors-view/VERIFICATION.md`.
- Maintainer reports View and editable controls working on Waveshare 8-inch/S3; broader HA/device validation remains pending.
- HA migration/re-pairing, old firmware compatibility, PIN, stream cleanup and sleep/reconnect remain pending.
- Issue #37: valid 20,033-byte packet disconnects v0.6.9 at 16 KiB; local reception grows to 65,535 bytes with bounded queues/draining/ACKs/logs. Reporter confirmation pending. Maintainer log: no unplanned MQTT loss (~7.5 h earlier BIN, ~25 min latest; two OTA restarts).

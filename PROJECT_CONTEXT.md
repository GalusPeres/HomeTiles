# HomeTiles shared project context

Last reviewed: 2026-09-07

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

- Firmware `v0.6.9` adds Binary/categorical Sensor history, Waveshare 4.3-inch support, the Waveshare 8 thin-PPA-strip guard, B4 brightness calibration and post-v0.6.8 stabilization.
- Main includes unreleased JC8012 V1 SDIO RX and Waveshare S3 LCD-4 Rev 4.0 (PR #35), with local builds, 15 release profiles and installer support. Version remains `v0.6.9`.
- Recent stabilization covers S3 update/display guards, MQTT validation,
  Light coalescing and incremental Weather parsing (`e3de63c`–`33b4e06`).
- Both test panels serve matching Admin assets; initial use reportedly stable. Full hardware validation and runtime measurements remain pending.
- The experimental Guition S3 XIP/`-O2` performance path was reverted in
  `5279456`. Do not reintroduce it as an assumed optimization. It increased
  risk and did not solve the measured interaction problem.

## Hardware and validation reality

- The maintainer can directly test M5Stack Tab5, Waveshare 4B, Waveshare
  8-inch, and Guition ESP32-4848S040 S3 hardware.
- v0.6.9 Binary/Text-State Sensor UI passed hardware tests on 4B, 8-inch and S3.
- Other exact revisions depend on community testers. A successful compile does
  not promote an untested revision to supported status.
- Similar P4 products share application code and sometimes base-board logic,
  but panel controller, initialization table, timing, touch controller, board
  revision, and firmware image remain exact-profile concerns.
- LCD-4 Rev 4.0 has contributor-tested display/touch/Wi-Fi/MQTT/Web OTA;
  older revisions and SD access are unsupported. See `docs/index.md` for validation.

## Active problem: GitHub issue #30

Issue: https://github.com/GalusPeres/HomeTiles/issues/30

- External hardware: Guition `JC8012P4A1C_I_W_Y` V1. The reporter uses Foscam
  cameras through Home Assistant's Generic Camera integration.
- The reporter also cannot install normal OTA updates; USB installation works.
- The `v0.6.8b1` and `v0.6.8b2` logs repeatedly show the same sequence: the
  stream connects and the first JPEG decodes successfully, then
  ESP-Hosted/SDIO reports CMD53 error `0x109`, wait timeout `0x107`, raw
  `0xcccccccc`, and an out-of-range RX length, followed by
  `rst:0xc (SW_CPU_RESET)`. One b2 failure began before opening the camera.
- The same SDIO failure can occur during ordinary MQTT startup without an open
  camera. Camera traffic and OTA both exercise the P4-to-C6 network transport;
  the evidence points below JPEG decoding and LVGL, but the exact electrical,
  board-revision, or driver cause is not yet proven.
- There is no normal panic core dump because the observed path ends in a
  software restart/transport recovery rather than an application exception.
- Beta b1 already contained the a8204 raw-PKT_LEN/pending-drain recovery and
  the short-tail CMD53 marker. It still failed, so repeating that patch or
  merely changing its label is not a solution.
- The ESP-Hosted version RPC timeout `0x15e` also occurs on a stable Waveshare
  8-inch unit. It identifies an older C6 protocol but is not sufficient to
  cause the crash; the fatal Guition-only evidence is the `0x109`/`0x107`
  transport cascade.
- Do not use or publish `v0.6.8b3`; it only requested 4-bit SDIO at 20 MHz and
  did not contain the later first-fault diagnostics. Espressif issue #167 also
  reports that 20 MHz did not reliably prevent this failure sequence.
- The b4 logs locate the first failure at a large C6-to-P4 CMD53 block read:
  raw DCRC status `0x80` occurs on 11- or 14-block reads before `0x109` and the
  later `0x107` timeout. JPEG decode, display, and Home Assistant are downstream
  of the failing transport operation.
- The b5 1-bit/40-MHz build still produces the same first DCRC failure, including
  failures before opening the camera. Reducing the active SDIO data lanes is
  therefore not a fix.
- Exact schematics explain why this Guition can behave differently from other
  P4 boards despite sharing the same P4/C6 architecture: Guition V1 uses
  5.1-kohm pull-ups on CMD, CLK, and D0-D3 without series termination;
  Waveshare 8-inch uses 51-kohm pull-ups, while Tab5 combines 5.1-kohm
  pull-ups with 22-ohm series resistors and a separately switched WLAN rail.
  This makes Guition-specific SDIO signal or power margin plausible. The b6
  result proves that limiting the RX CMD53 transaction length is an effective
  mitigation, but it does not by itself prove the underlying electrical cause.
- Guition's original `JC8012P4A1_C6.bin` and the current HomeTiles host both
  use ESP-Hosted streaming mode. The newer official
  `JC-C6-slave_v2.3.2.bin` uses packet mode and is not a drop-in C6 update for
  the current host. Do not flash it alone. The onboard USB paths reach only
  the P4; C6 UART/boot access requires the internal CN5 header and an external
  3.3 V UART adapter.
- Do not use the reported ESP-Hosted 2.9.3 rollback as the next test. That
  report concerns a different `0x102` TX/alignment failure on another board;
  the normal 2.9.3-to-2.11.6 CMD53 RX path does not explain this Guition's
  `0x109` CRC failure, and rollback would discard relevant safety fixes.
- `v0.6.8b6` passed the reporter's hardware test: both cameras ran at 15-20 FPS,
  Web Admin OTA succeeded, and the previous `0x109`/`0x107` failure did not
  recur. The regular-source integration retains the proven 1-bit/40-MHz V1
  configuration and splits large P4 RX reads into individual 512-byte CMD53
  reads. It remains limited to the exact V1 profile; v0.6.9 does not contain it.
- Lowering camera FPS, resolution, or quality may be used only as a clearly
  identified diagnostic A/B test. It is not an acceptable final fix and must
  not silently reduce normal camera performance.
- Keep the release integration limited to the exact V1 profile and verify the
  single-block marker plus 1-bit configuration in its final build. Other P4
  profiles must retain their baseline ESP-Hosted object; S3 remains unaffected.

## ESP32-P4 network history that remains relevant

- The repository already backports ESP-Hosted allocation/PSRAM fixes,
  synchronous RPC UID routing, Espressif's `a8204f9` dropped-RX recovery, and
  sparse diagnostics. Exact patches, variants, hashes, and limitations are in
  `tools/esp-hosted-3.3.7-rx-fix/README.md`; do not duplicate them here.
- The `repo-a8204` variant is the release-safe baseline. The short-tail receive
  variant was an experimental field path and is not proof of a universal fix.
- Historical P4 OTA experiments showed that generic transfer throttling,
  PSRAM-only staging, direct TLS-to-flash streaming, in-place ESP-Hosted
  restart, and extra permanent SDIO buffers did not cure the underlying
  failure. Do not repeat them without new evidence and an isolated test.
- Existing safeguards against a permanent network wedge are recovery, not
  proof that the transport defect is solved.

## Binary and textual Sensor history in v0.6.9

- Stable tile type 20 reuses `sensor_entity` without changing `PackedTileV7`; central DE/EN/FR strings, state-aware HA icons, autosave/previews and responsive 24-hour/7-day history are released.
- Textual states use timeline/Activity; numeric sensors retain graphs. Missing, unknown and unavailable remain distinct.
- Bridge v0.6.40 (`581150b`) released bounded Recorder paging, categorical history and legacy-firmware compatibility.

## Editable value tiles: unreleased firmware

- IDs 21 Number, 22 Select, 23 Date/Time reuse Sensor rendering/persistence/popups; `PackedTileV7` is unchanged.
- Number/input_number uses the centered Media slider/value, Climate +/- pill or bounded roller, with graph/Activity. Select/input_select uses Settings dropdowns, timeline and Activity.
- Time/date/datetime/input_datetime: large single-row hh/mm/ss rollers in a neutral pill, no arrows, native 23/00 and 59/00 wrap; date spinboxes without keyboard. HA timezone/DST validation applies.
- Additive `/control` payloads preserve legacy states/configurations; fixed services, sessions, revisions, deadlines and reconnect generation reject stale commands.
- Bridge v0.6.44 (`148dec4`) is on HACS; fixes stale icon cache, preserves overrides. 119 Bridge tests pass. Firmware `7c37bb2` is committed locally, not pushed.
- Control bands clear wrapped titles and the full close touch area. Number/Select share a height; Time is taller. Select keeps compact history and earlier Activity. Status shares the heading row. Range changes keep old data until reply; offline closes dropdowns.
- Drafts coalesce steps/rollers for 600 ms, publish sliders on release and survive service ACKs until confirmation/rejection or 30-second timeout.
- UI: neutral dropdown, subtle press, gray border; graphs survive reuse. Activity fills to the footer; its row pool scales. All 15 layouts have native tests. Header clearance: 82 tests pass, incremental S3 build verified; hardware pending. Proof: `build/editable-header-clearance/VERIFICATION.md`.
- Wi-Fi audit: S3 idle (>3 s) requests MIN_MODEM/11 dBm; sleep/wake NONE/19.5. P4 blocks idle saving; boot/reconnect and failed-call caching have gaps on both. Unfixed; probes: `build/wifi-power-audit/VERIFICATION.md`.
- Icon parser stopped at View state IDs `[f:1]`/`[t:13]`, dropping Select/later Time names/icons; reproduced and fixed, live cache matches. Device check pending. Docs unchanged; notifications/folder colors deferred.
## Current maintenance refactoring

- Architecture/workflows: `ARCHITECTURE.md`, `CONTRIBUTING.md`; host dependencies need `npm ci --ignore-scripts`.
- Docs source: `docs/`, `mkdocs.yml`, `overrides/`; root hosting deploys `HomeTiles/gh-pages`.
- https://galusperes.github.io/ desktop/mobile pages and all 14 published flasher profiles checked 2026-09-06.
- Previous maintenance BINs and hashes: `build/maintenance-20260905/VERIFICATION.md`.

## Current view control, telemetry and compatible controls

- Main `4c9ea4e` adds unreleased Home/folder/popup navigation through existing UI, PIN and camera teardown paths; the authorized push is complete.
- Visible folders are reused for their popup/descendants; new/locked paths still require access checks (S3 Home detour fix).
- Stable tile IDs use reserved PackedTileV7 bytes and durable counters; MQTT sessions/sequences/deadlines reject replay.
- Bridge v0.6.43 (`6a02859`) retains View/telemetry migration and compatible controls. Firmware documentation changes are local drafts until firmware release.
- Switch adds input_boolean/automation/fan/humidifier/remote/siren; Scene adds button/input_button. Aliases stay stable.
- Commands validate selected targets, availability and on/off features; retained commands are ignored.
- Firmware battery is a stub on all profiles. Unsupported battery/probes are no longer auto-registered; explicit local I/O remains.
- Bridge migration checks registry ownership/capabilities, cleans shared selections and preserves user entities.
- 76 firmware host tests and 107 Bridge tests pass; both sequential incremental builds pass. BINs/hashes: `build/controls-verification/VERIFICATION.md`.
- View control is user-reported working on Waveshare 8-inch/S3; the S3 detour fix and new controls still need hardware/HA checks.
- HA migration/re-pairing, old firmware compatibility, PIN, stream cleanup and sleep/reconnect remain pending.
- Issue #37 candidate reproduced: valid 20,033-byte packet disconnects v0.6.9 at 16 KiB. Local fix grows reception to 65,535 bytes; queue bounds, oversize draining/ACKs and diagnostics remain bounded. Reporter confirmation pending. Maintainer's log: no unplanned MQTT loss, ~7.5 h on earlier test BIN, ~25 min on latest, two OTA restarts.

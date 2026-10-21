# HomeTiles shared project context

Last reviewed: 2026-10-17

## Sources of truth

- Version: `version.txt`; code: `git status`, recent commits/branch
- Device support and validation: `docs/index.md` (device status notes)
- ESP32-P4/ESP-Hosted patches: `tools/esp-hosted-3.3.7-rx-fix/README.md`
- Release procedure: `RELEASING.md`
- Live bug status: newest GitHub issue comments; recheck online before changing an issue status
- Bridge publishing requires separate authorization. Docs: `docs/`, `mkdocs.yml`, `overrides/` (gh-pages).

## Firmware baseline

- v0.9.3: 2026-10-18 (cs/es/sk, keyboard and long dates as language data, Icons card, factory images with the icon file, GS-T3E v2 + Waveshare LCD-5 pending), Bridge v0.9.3 `dc19547` (2026-10-18): HA restarts with a connected panel (Bridge #15: the link server waited for open connections since Python 3.12.1; connections close first, servers wait at most 3 s). Pairing by IP without mDNS (Bridge #16, HomeTiles #114, milestone v0.9.4): firmware read-only `GET /api/link` + Bridge step. v0.9.2: tag `8964652b` (2026-10-17); v0.9.1 2026-10-15. Docs CI browser tests run on pinned Chrome for Testing 154.0.8037.57 (`7379a3ad`): the runner's Chrome 154.0.8037.97 (image 20261012.334) never answered the first Page.navigate; a tag's own docs run uses the tag's workflow, so after a release dispatch docs on `main`. Test builds stay `v0.6.12bNNN` on purpose (System Update compares numbers only and offers releases).
- Guition S3 XIP/`-O2` reverted in `5279456` (risk, no measured gain); XIP now needs 5.4 MB of the 8 MB PSRAM (3.6 MB in use) - do not retry.
- Fast build (`ca676434`): a cache stays warm when only repo headers changed and the recorded LVGL config fingerprint matches (`hometiles-library-config.json` in the cache); run `node tools/fast-build-check.mjs --build-path <cache>` before every build.
- `feature/perf` (both repos, unreleased, FW `b259-perf3f`): nav preload (S3 max 3 grids, 40/16 KB reserve, pressure release), shadow/circle caches, P4 CPU rotation for small areas, touch hold, 60 s energy/history reuse, S3 PCLK 12 MHz; V2/S3/8-inch HW ok. Bridge v0.9.0b16 beta (`17ca8a3`, feature/direct-link): request limits, empty energy answer, meta push + `"push": 1` (panel refresh 15 min); HA test pending.
- Rejected: LVGL 2 draw threads (global compressed-font RLE state: glyph artifacts, no gain). S3 GT911 (fw 0x1060, cfg 0xFA, filter 8 = 32 px) steps slow drags and ignores config writes (also esp32-macropad #101) - do not retry. Open: S3 Web Admin pages block the loop 1-6 s. S3 b324: mallocs > 32 B prefer PSRAM (heap_caps_malloc_extmem_enable): internal free in use 42 -> ~89 KB, camera-screensaver low 14 -> 85 KB, no stripes; text-heavy steps slower (routes ~0.6 s once after connect).

## Languages (released in v0.9.3)

- Built in: en, de, fr, pl, cs (PR #110, pepe59), es, sk (#104 attachment, vik147); each costs ~22 KB flash, fonts already carry Latin-1 + Latin Extended-A. A language is data only: its Strings and LocaleProfile, including `long_date_year_pattern` ("{d} de {M} de {Y}"), `keyboard_layout` (Auto: 0 QWERTY, 1 QWERTZ, 2 AZERTY) and `keyboard_letters` (lead the accent page and a held letter's forms, `keyboard_letters.h`); no language codes in code. Tests take the languages from `kLanguages`; `test-i18n-panel-glyphs` rejects characters the panel fonts lack (Czech came with U+2212). Language packs (like the icon file) later. Device text fit for cs/es/sk pending.

## Media popup rework (`feature/media-popup` in both repos, FW b487 local, Bridge v0.9.4b1 pre-release 2026-10-19)

- Design: `build/design-mockups/media-player` (user-approved), plan and PR notes `build/analysis/2026-10-19-analyse.md`. Two layouts per tile (`sensor_display_mode` 0 "Cover links", 1 "Cover Mitte", Web Admin "Popup"); geometry `media_popup_layout.h` (every distance = left edge L; cover V2 399 / S3 218 = `coverEdge`). Controls only with their HA feature bit (old Bridge: previous/play/next/volume). Sub-views source/sound mode, speakers (join/unjoin), library (PR #87 `media_browse`, Bridge #11 rewritten). memooox3 co-author.
- Extra fields per player cached by `media_popup_note_state` (8 players). 14 icons added to the built-in set. HW pending: both layouts V2/S3, dark/light, filled/plain, grouping, library, unavailable, sleep. Next: tiles (step 4).

## Direct Bridge link (`feature/direct-link` in both repos, unreleased)

- Panel connects to the Bridge over TCP 8140 instead of MQTT; Bridge = broker (same topics/retained), frames sealed with pairing-K keys. Contract `docs-dev/bridge-link.md` (shared vectors).
- Setup: Pair on panel (2 min, TXT `pair=1`) -> HA card (new or switch MQTT entry) -> `POST /api/link` (no password) -> number, no restart. V2 b226 + Bridge b9 HW OK: new, switch, unpair, delete, quick Pair; both clear MQTT leftovers; new HA IP = new setup.
- Entity picker (b227-b241, Bridge b15): panel declares all it uses (acked; `own` = own entry's releases + these); others only paired + password, 1st via panel tap. Never from Bridge lists (b232-235 loop). S3 b240 HW ok; b241 title Auto/full-height settings HW pending. b369: open list Recent (per browser) / Favorites (own) / All (#79); a language change republishes the view catalog (#72); V2 HW pending.
- `feature/voice` (both repos, released in v0.9.2): keepalive pings when nothing was sent OR received for 15 s (b474; one-way traffic into a quiet HA hit the 45 s timeout); logs name the way (`bridgeLogName()`: Link/MQTT, never MQTT on the link, b475); Scene tile renamed Button, its picker searches scenes/scripts/buttons (`entity_search` 4, Bridge v0.9.2b2), stores the entity id in `sensor_entity` (`sceneTarget()`, old aliases kept), test walks every picker list (b476 + Bridge v0.9.2b2, 8-inch HW ok 2026-10-16).
- Voice (Zeid's PRs #89/#91, Bridge #13/#14, released in v0.9.2, early version): shared core `src/audio/`; a board only names its chips/addresses/pins in `speaker_board.h` (contract `audio_board.h`, chips ES8311 out+mic, ES7210 mic, ES8388 out; `codec_bus` = i2c_master or the board's own, Tab5 M5Unified). b477: every P4 board from its schematic/BSP (WS 4B/4.3/7/8/10.1/7B ES8311+ES7210 GPIO53; Guition JC8012 V1/V2/V3 ES8311 mic DIN 11 amp 20, JC1060 V2/JC4880 DIN 48 amp 11; Tab5 ES8388+ES7210, amp on expander P1); JC1060 original panel and S3 stay out; wake word warm-up 100 feature rows = 1 s (was 100 invocations = 3 s, deaf ~4 s after an answer); `Mic level` log. b478: per-board `kVolumeLimit` (share of the shared volume scale; Guition boards 0.6, Tab5 0.9: at the full scale V2 and Tab5 distorted and switched off; JC1060 V2/JC4880 same circuit, 0.6 by precaution). b477 HW ok 2026-10-16 on V2 (speaker, ES8311 mic, Assist), Tab5, 8-inch and 4B (speaker fitted): every maintainer P4 board. Open: mic button + audio settings (where: user decides, mockup first; Zeid's re-pushed #91 Audio page as a fifth panel Settings category was declined 2026-10-17: too much on the panel, no room on the 4B).
- Energy (released in v0.9.1, FW and Bridge): the energy list searched through the Bridge (`entity_search` 3). Without a Web Admin password only the categories ticked under Energy Dashboard (per entry) reach the panel: configuration, search and energy answers (`_panel_secured` = its sealed declaration); with one every source. Favorites = released + own Energy tiles' sources; tile icon/name by category from the answers (`energy_source_icon/_name`). V2 HW ok 2026-10-15.
- Pictures (`docs-dev/images.md`, link only): covers by `image_key` (b310); #69 screensaver SD or HA image/camera (fill/fit/original, camera 3-60 s, native-order decode) V2 b313 + Bridge b20 HW ok. Camera full screen #65 (`build/analysis/2026-10-09-analyse.md`): tap video = Bridge sends frames in the panel's size and turn, HW JPEG straight into the framebuffer, black (not a still frame) until the first frame both ways; every P4 (b322: shared presenter, heights to 16 rows; Tab5 one FB after refresh; 4B one FB, tear unchecked); stream 32 KB chunks x2 + 8 KB fallback (Bridge b24 beta); V2/Tab5 30 FPS HW ok. S3 (b325-b330): same stream, tjpgd in camera task, LVGL image; popup 4.5-4.9 FPS, full fit `inside` (Bridge b26: 480x270, no bars) ~3.8 FPS HW ok; q 5 looked same (b329), back to 11; faster = JPEGDEC (ask). OTA restart: RTCWDT reset seen twice (recovers). b326: every tile save re-declares entities (camera tile was unknown_camera). b331 + Bridge b27: View select "(Vollbild)" per camera tile (`full_targets`, V2 HW ok 2026-10-11). b372-b377 #50: Camera tile shows its camera/image picture in tile size (Sensor head, title pill or one shared pill, transparency, circle+title Auto/Dark/Light, no shadow; pictures kept per entity+size; corners follow the theme); tap opens the popup or the full screen directly; HW pending (b377). b323: Web Admin upright bounds (placeRows), Tab5 per-row cache calls fixed (HW ok).

## Hardware validation

- Maintainer hardware: Tab5, Waveshare 4B/8-inch, Guition S3/V2 (JC8012P4A1C_I_W_Y1, SKU10153002-V2). V2 Tested, PPA/SD confirmed.
- v0.6.9 Binary/Text-State Sensor UI passed hardware tests on 4B, 8-inch and S3.
- Other revisions need community validation; compiling is not support.
- P4 code is shared; panel/touch init, timings, revision and images stay profile-specific.
- LCD-4 Rev 4.0: contributor-tested display/touch/Wi-Fi/MQTT/Web OTA; older revisions and SD unsupported (`docs/index.md`).
- JC1060 V2 profile rotation pair 0/2 (b478, #108: `e8457f37` left normal = flipped = 0, so 0/180 did nothing; `test-rotation-profiles` checks every profile).
- JC4880P443 (PR #46, damianeek): portrait 480x800/4x6, contributor-tested; landscape later. Open: SD DEINIT_ARG, P4 DSI groups.
- P4 v3 images (post_v3 301-399, v3 DSI clock): WS 10.1 (PR #48, tested v3.2), WS 7B (`_rev3`, replaces exact-v3.1, #41), JC8012 V3 = V2 code (#44); HW pending.
- New boards in v0.9.3 (HW pending by the reporters): GS-T3E v2 (#93, S3 480x480, own driver derived from the Guition one, relays GPIO45/46) and Waveshare 5-inch (#76, 720x1280 HX8394, own `dsi_driver`, pre-v3 + `_rev3`, voice full volume, OV5647 camera, SD 20 MHz). Rule: a new board never changes another board; shared files only get list entries.

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

- Radius, circles, circle strength and card color fixed (b378, no settings): radius `tile_radius::kDefault` (= max), circle 25 %, card per theme (dark `#1A1A1A`). Used by tiles, Climate, popups, previews.
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
- V2 OV02C10 720p, Tab5 SC202CS 720p RAW8 (mirror+180), 8-inch OV5647. Window rule (b296, user-drawn): Bridge turns a quarter = sensor upright = centred strip (OV5647 544x960, others 400x720); no turn = whole image (1280x960 / 1280x720). Panel never turns; Web Admin 90° swaps both.
- Camera builds pass; HW pending (#56): WS 7/10.1/7B/4.3/4B OV5647, V1/JC1060 V2 OV02C10, JC4880 quarter turn.
- Advanced: `lcam_rot` (180 = flip, odd = `rotate` 90; b387: one for every layout, upright adds its own quarter turn, the user mirror never changes the turn - b298-b386 reversed it and stood the V2 upright picture upside down; `lcam_rot_up` no longer read; V2 HW ok 2026-10-11), `lcam_rbswap` (Bayer, next start).
- HW pending: b30-b35 (CSI/ISP, gain, screenshot, Wi-Fi/AP, rotation, sleep stream/indicator wake).
- Open: int WDT HW; TEST `kChunkWindow = 4`; stream AE waits for a step to show (b215).
- Popup stream: Bridge thins before scaling, shared FFmpeg per camera; P4 b134 30 FPS; #63 1024x600 552x310 (b135).

## v0.7.0 release

- S3 fix `c3e0a673`: PSRAM-first OTA TLS, all three profiles. Guition b74 OTA/boot/MQTT passed; Waveshare HW pending.
- v0.7.1 promised: PR #51 Polish port, #26 S3-4B PCLK (16 vs 10 MHz), P4 v3.2 for 7B (#41) and JC8012 V3 (#44); French.
- Tests pending #7 #11 #27 #34 #45; #55 reporter check.

## Flash and RAM (PR #62)

- Icon file (released in v0.9.1): all 7447 MDI icons in LittleFS `/_mdi_icons_<px>.bin` (`tools/generate-icon-files.mjs`, release assets), 399 core icons built in, one fixed PSRAM glyph cache (V2 166 KB, S3 114 KB; holding all broke the screensaver's 2 MB block). A panel without a valid file downloads it once from its release (betas: HomeTiles-staging) and restarts. Tab5 b468 4,830,464 B of 6,815,744 (b454: 6 KB free). V2/S3/Tab5 HW ok 2026-10-15. b482/b483 (released in v0.9.3, #112 firewall): System page Icons card (icon file there: name + all icons; missing: link to its own release, built-in / all, Update = panel downloads now without using up an automatic attempt, red last-failure line from NVS `icon_dl/err`; file row uploads by hand: raw body into PSRAM, header check at once, CRC at the end, loop installs and restarts); factory images carry the icon file (`release-helper/factory-icon-file.js`, mklittlefs into spiffs 0xD10000). HW ok 2026-10-18: V2 both ways, S3 Update, 8-inch factory. Test builds need their icons on HomeTiles-staging (tag = FW_VERSION). Test builds' file manager also shows LittleFS (use, delete).
- Admin CSS ships without comments/indentation (b454, `formatCssDelivery`). Cleanup list: `build/analysis/2026-10-14-analyse.md`.
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

## Layouts (`feature/layouts`, unreleased, b294)

- Folder list (b479, user 2026-10-16): a folder holds its classic cells plus as many storage entries (`grid_slots.h`, max 64 for the 64-bit switch/binary sensor update masks: 4B/S3 32, Tab5 56, 35-cell boards 64), so tiles in a layout's storage (and the Back tile) never take a free cell's entry (4B 3x3 head bar: 8 shown + 7 stored + Back filled all 16). Tile records and 4-tile blocks unchanged, files only longer; a file from before (exactly one entry per cell) reads with the rest empty; V6/NVS migrations keep the old size; folders off the task stacks (`makeTileGridScratch`). Layout window: a storage over its entries blocks Save (`layoutStorageFull`). b480: a full folder's free cell keeps the hand, a click says so (`folderFull`: clear out the storage in the layout window and save). HW pending.

- Three layouts (`grid_layout.h`): classic (profile grid, places in PackedTileV7), head bar landscape (1280x800 6x4, 1280x720/1024x600 5x3, 800x480 4x3, square 3x3), head bar upright (prepared: 4x6.5, 3x6, 3x5.5). NVS `layout` read at boot (OTA keeps it); a bar layout without places starts classic.
- Places: `/_tile_grids/layouts.json` by folder + view_id (`tile_layouts.cpp`); screensaver = folder 0xFFFE keyed slot+1 (`screensaver_places.*`: the layout's grid, no head, two rows at the bottom, three upright); its clock per layout in the screensaver config (`clock_layouts`, classic on top), edited in the layout window. No place = `layout_hidden` (not drawn, no room).
- Layout window (`tiles/layout-window.js`): screen top left, storage only right/below; X always deletes after a question; tiles never shrink; red blocks Speichern/Umstellen; screensaver tab without picture, with that layout's clock (saved with its places, b288). Export marks classic storage tiles.
- Popup header = head bar rule (b284, `popup_layout.h`): X one grid gap in the card corner, circle mirrored, card radius = tile radius + gap. Head time 32/20 px; without gear its right gap = top gap; gear shows the PIN lock; Settings PIN popup always global color.
- One look (b300, user 2026-10-08): popup card margin 0 (reaches the screen edge), every layout's tiles one grid gap from the edge; the classic grid = profile cols/rows with pad = gap (`device.h` kGridPad/kGridCellW/H, cells 3-4 % smaller, stored places unchanged). Card shadow width follows the radius to stay in the LVGL shadow cache (`shadow_width_for`, max 28 px; the b284 corner missed it), none on square full-screen cards. Half tiles step the value down to fit (1280x800: max 24); weather forecast row from the classic two-row height; camera stream keeps 752x424 etc. (former margin).
- Hochkant (b294, one general way, user 2026-10-08): P4 panels upright by themselves under a landscape profile (V1/V2, WS 4.3/7/8/10.1, Tab5) draw the upright UI straight (presenter `drawUpright`, 180 mirrored; Tab5 own copy with byte swap), DisplayManager turns the landscape touch once for all, screen turns after `tileConfig.load()`; grid space `GRID_SPACE_*`, half row tracks, own camera +1 quarter turn. V2 HW ok (b290-b292). b368: JC1060/7B upright and JC4880 landscape by a real quarter turn the other way round (`draw_turned_area`, shared PPA/CPU turn, `turn_reversed`), compile only, HW pending (community). Owed: camera popup upright, Web Admin global settings collapsible.
- Light theme (b339-b367, user 2026-10-10): Design Dunkel/Hell/Automatisch; `ui_theme` roles + `SurfaceScope`, `tile_surface`; unavailable one grey (#BDBDBD). Popups take the opener's surface (`g_open_surface`); remembered LVGL objects clear themselves on delete (LVGL 9.6 `lv_obj_is_valid` reads freed objects).
- One color rule (b378, user 2026-10-11, decided, do not reopen): color always from the icon; per tile only "Ausfuellen" (`tileFills`: "fill 20", old From icon/From cover tile/rule "Kachel toenen" migrate to it); rules color only the icon; own color only Clock/Text/Pixelanim (no Fill there). Light theme: a filled tile is kJewel from render on (idle grey without color), never switches surface; others keep the white card. Frame line like Tessera (`frame_line`): plain card #DDDDDD/#292929, other cards their own color 14 % darker in the light theme, 9 % lighter in the dark (by the screen, b383: lighter looked frayed). Filled card second line white. Almost white light never tints (`without_white`). Popups: Light/Climate/Cover never filled (intended). Global settings left: borders, layout, theme. b379: hidden Global/Custom row really hidden (CSS weight).
- Gesture menu, screensaver page (b380-b412) and pages in the head bar (b413-b461), with every later fix: build-by-build notes in `docs-dev/project-history.md` (moved verbatim 2026-10-21).

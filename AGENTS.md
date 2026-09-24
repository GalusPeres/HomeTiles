# HomeTiles Project Rules for Agents

This is the single authoritative agent rulebook for this repository. Every
agent must read it before inspecting, changing, building, or publishing code.
Do not create a second competing rule file.

## 0. Shared project context

- After this rulebook, read `PROJECT_CONTEXT.md` completely before inspecting,
  changing, building, or publishing code.
- `PROJECT_CONTEXT.md` is the single shared state file for Codex, Claude, and
  other agents. It records current baselines, verified hardware facts, active
  problems, failed approaches, and pending validation; it is not a second
  rulebook.
- Update `PROJECT_CONTEXT.md` in the same change whenever work materially
  changes one of those facts. Replace stale entries instead of appending a
  running diary.
- Keep shared context concise: no chat transcripts, raw logs, long build
  histories, or copied issue discussions. Keep it below 160 lines and 12 KiB.
- Do not create model-specific status, memory, or handoff files. Claude's
  `CLAUDE.md` is only a small import entry point for this rulebook and the
  shared context.

## 1. Scope, worktree, and user ownership

- Inspect `git status` and the relevant recent commits before editing.
- Treat every pre-existing tracked or untracked change as user-owned work.
- Preserve unrelated changes and untracked files. Never clean, reset, move,
  overwrite, stage, or delete them to simplify the task.
- Change only the requested feature, device profiles, and proven shared
  dependencies. Do not broaden the task into unrelated optimization work.
- Do not commit, push, tag, publish, create a release, or modify a remote
  system unless the user explicitly requests that exact action.
- Before a destructive or externally visible operation, verify the exact
  target and the user's authorization.

## 2. Communication and delegation

- Keep progress updates short, concrete, and useful.
- State what is being checked, what changed, what passed, and what still needs
  hardware validation.
- Do not claim that a compile proves runtime or hardware behavior.
- If a regression is reported, identify and correct the concrete cause before
  adding more features or speculative changes.
- Keep delegation minimal. Do not start background agents for ordinary UI
  work, small bug fixes, or tasks that can be completed directly.
- Never leave obsolete background agents running after their bounded work is
  complete.

## 3. English source, English logs, centralized UI translations

- Write all new or edited source-code comments in English.
- Write every runtime, diagnostic, debug, warning, and error log in English.
- Technical logs are never localized. Keep prefixes stable and rate-limit
  recurring logs so diagnostics do not create performance problems.
- Put every user-visible UI string in the central `Strings`/`LocaleProfile`
  schema in `src/core/i18n/i18n.h` and `src/core/i18n/i18n.cpp`.
- Never place display text directly in a renderer, popup, WebUI JavaScript,
  HTTP handler, MQTT handler, or protocol handler.
- Device UI and WebUI must consume the same central translations.
- Type-specific WebUI translations must be exported through that type's own
  `web_scripts.cpp`, following the established Climate implementation.
- Raw protocol values such as Home Assistant states (`open`, `closed`) and
  JSON keys remain stable English identifiers, but must be translated before
  display.
- Do not create renderer-local translation arrays, language-specific UI
  branches, or duplicate Web-only translations.
- When adding a language-dependent value, add it for every supported language
  and add a regression test that exercises the translated display path.

## 4. UI and visual consistency

- Use the exact existing HomeTiles component named by the user as the visual
  and behavioral reference. If none is named, choose the closest established
  component and state that choice.
- Match the reference component's layout, spacing, alignment, fonts, colors,
  radii, pressed states, touch behavior, and lifecycle before adding
  type-specific behavior.
- Do not invent a new visual language, control layout, or interaction model
  without explicit user approval.
- Use shared layout, font, color, and popup helpers whenever they exist.
- Device rendering, live Web Admin preview, and cached-grid Web Admin preview
  must show the same title, icon, state, values, colors, and geometry.
- Any deliberate device-specific visual difference must be guarded by the
  exact device profile and documented in English.
- Verify 1x1 tiles as well as supported multi-cell sizes. Long titles and
  missing values must clip or wrap like the reference component.

## 5. Complete contract for a new tile type

A tile type is not complete when it merely renders. All sections below are
mandatory.

### 5.1 Stable ID and persistence

- Append a new `TileType` value. Never renumber or reuse persisted IDs.
- Preserve `PackedTileV7` binary compatibility unless a deliberate, tested
  migration is part of the task.
- Add every reused `Tile` field to pack, unpack, reset, validation, migration,
  equality/no-op, and popup-mode allow-lists.
- If an entity or value can exceed its packed field, include the new type in
  the existing sidecar read, write, remove, rename, recovery, and no-op paths.
- Include the type in folder snapshots, cache rebuild, cache restore, cache
  reset/release, grid reload, copy/paste, import/export, and deletion paths.
- Distinguish absent, `null`, empty, zero, unknown, and unavailable values.
- Verify persistence with a fresh GET and a device reload/reboot path; a
  successful POST alone is not sufficient.

### 5.2 Type module and registry

- Follow the existing modular structure under `src/types/<type>/`:
  renderer, request handler, Web HTML, Web styles, and Web scripts.
- Add one complete `TileTypeDescriptor` with the correct stable type ID, CSS
  class, fields suffix, preview kind, renderer, request handler,
  `load/save/reset` functions, HTML, styles, scripts, and default color.
- Add localized type and entity labels through the central language schema.
- Add the type to every relevant registry, select list, allow-list, and
  dynamic MQTT route list.
- Compare the integration point list line by line with the closest mature tile
  type before declaring the implementation complete.

### 5.3 Web Admin HTML and entity options

- Generate unique tab-prefixed IDs for every field.
- Pass entity options through both the full page and lazy folder-tab fragment
  paths.
- Extend `/api/entity-options` for selectable entities.
- Preserve a configured value even when the current entity list is temporarily
  unavailable.
- The empty placeholder is not an entity and must never become the tile title.

### 5.4 Browser editor, preview, and autosave

- Implement `load<Type>Fields`, `save<Type>Fields`, and
  `reset<Type>Fields` using the exact server-side field names.
- Bind every editable field through the shared live-editor path.
- Every field change must update the preview, update the draft snapshot, and
  schedule autosave.
- Rebinding cached/replaced folder HTML must replace stale listeners instead
  of duplicating or losing them.
- Include every type-specific value in draft snapshots before POST.
- Support copy/paste and import/export with the same field semantics.
- Render both the selected live preview and the cached-grid preview.
- Browser JavaScript may call only browser-defined helpers. Firmware/C++
  helpers do not exist in the browser.
- Run the real preview branch in tests; do not stub away the code that failed
  in production.
- Escape untrusted titles, entity names, and metadata using the established
  WebUI escaping helpers.

### 5.5 HTTP save and firmware load

- POST type ID, title, icon, color, geometry, entity, popup mode, and every
  type-specific value to the established tile endpoint.
- Validate and apply the same field names in the type request handler.
- Preserve existing fields when a partial request is designed to do so, and
  clear fields only through an explicit reset/delete path.
- Verify save, reload, folder navigation, copy/paste, export/import, deletion,
  and reboot behavior.

### 5.6 Runtime state and cache integration

- Define a bounded state representation that preserves availability and
  optional values without conflating missing data with zero.
- Parse retained state payloads defensively and validate ranges.
- Add bounded/rate-limited queues and process them in the established loop
  locations for active and sleep modes.
- Add state application to root grids, folder grids, snapshots, cache restore,
  icon refresh, and entity refresh paths.
- Do not allocate or log per frame or per unchanged MQTT state.
- Release dynamically owned objects, event user data, queues, and cache data
  through the established delete/reset lifecycle.

### 5.7 Home Assistant and MQTT bridge

- Use official Home Assistant entity states, attributes, feature bits, and
  service semantics.
- Do not infer support from a current value when Home Assistant provides a
  `supported_features` mask.
- Add configuration flow selectors, stored configuration, tracked entities,
  metadata, state publishing, subscriptions, and unsubscribe/reload behavior.
- Use retained state where the existing bridge contract expects startup state.
- Validate entity IDs against configured entities.
- Use a fixed service allow-list; never pass an arbitrary service string from
  MQTT to Home Assistant.
- Clamp and validate numeric service arguments.
- Preserve compatibility with configurations that do not contain the new
  entity list.
- Add bridge tests for configuration migration, state payloads, unavailable
  entities, command validation, and every supported feature combination.

## 6. Popup implementation contract

- Use the popup explicitly named by the user as the design base. If the user
  says to base it on Light, start from `light_popup.cpp` and preserve its
  header, card, spacing, navigation, and interaction conventions.
- Use `popup_layout.h` for card size, margins, header coordinates, fonts,
  close button, body geometry, and scaling. Do not copy raw coordinates when a
  shared constant exists.
- Match the standard header: correctly aligned entity icon, localized title,
  and standard close button with the standard touch area.
- Reuse/preload popup objects according to existing lifecycle patterns; do not
  rebuild large object trees on every opening without evidence.
- Opening one popup must close conflicting overlays through the same mutual
  exclusion pattern used by existing popups.
- Add the new popup to preload, hide, reset, screen-change, sleep, and teardown
  paths.
- Keep all labels localized through central i18n. Logs remain English.
- Render only controls supported by Home Assistant feature bits. Never invent
  a service or enable a control whose feature is absent.
- Treat position, tilt, brightness, temperature, and other channels as
  independent when Home Assistant does.
- Preserve `null`/unknown values instead of displaying them as zero.
- For sliders, use the established optimistic-update, remote-update blocking,
  publish throttling, and guaranteed final-release behavior.
- A press action must trigger on release unless the established control
  requires continuous dragging.
- Verify popup layout and interaction at 480x480 and the affected larger
  layouts. Avoid device-specific branches unless the hardware truly requires
  them.
- Test repeated open/close cycles, unavailable transitions, state updates while
  open, navigation away, sleep/wake, and object cleanup.

## 7. Performance and memory

- Measure before changing shared UI or cache behavior.
- Avoid blocking network, storage, image decoding, or large memory copies in
  the LVGL timer/event path.
- Reuse established worker/queue patterns for expensive work; workers must not
  call LVGL unless protected by the project's established LVGL access model.
- Use bounded queues, bounded caches, and explicit ownership.
- Do not add permanent diagnostic history or large buffers when on-demand
  diagnostics are sufficient.
- Keep diagnostic output rate-limited and aggregate hot-path timings.
- Do not trade correctness or device stability for an unmeasured optimization.

## 8. Device profiles and shared code

- Guard device-specific fixes with the exact profile define.
- Do not guess GPIOs, panel timings, color order, byte order, clocks, memory
  capabilities, or partition parameters. Use exact-board primary sources.
- Shared-code changes require compile coverage for every materially affected
  profile requested by the user or required by the risk.
- Do not modify unrelated P4 or S3 behavior while working on one exact device
  profile.

## 9. Tests and verification before handoff

- Install pinned host dependencies after checkout or lockfile changes with
  `npm ci --ignore-scripts` before running tests or generating Web assets.
- Add a focused regression test for every reproduced bug. Name it
  `tools/tests/<area>/test-<topic>.mjs`; the CI suite discovers them recursively.
- Run the whole suite with `node tools/run-tests.mjs` (add name fragments to
  run a subset). A harness whose optional tooling is missing reports `SKIP:`.
- For a tile type, the browser contract test must cover type selection, entity
  selection, title editing, popup mode, preview, draft snapshot, autosave,
  form payload, and all localized runtime states.
- Run `node --check src/web/assets/admin.js` after editing Admin JavaScript.
- Regenerate compressed assets with `node tools/generate-web-assets.mjs` and
  verify them with `--check`.
- Run `git diff --check`.
- Compile all materially affected requested profiles.
- Record exact BIN paths, byte sizes, and SHA256 hashes.
- Clearly separate compile verification from tests that still require real
  hardware or Home Assistant.

## 10. Build workflow

- Run `node tools/run-tests.mjs` before firmware compilation.
- Use the normal incremental local build for iteration.
- Use `-Clean` only when explicitly requested or when a verified cache or
  toolchain problem requires it.
- Do not rewrite already patched toolchain archives on every build; verify and
  leave identical files untouched so compiler caches remain valid.
- Build only the profiles requested or materially affected by the change.
- Keep test artifacts in the established build directories unless the user
  asks for a separate package.

## 11. Commits, pushes, and releases

- Review the final diff and protected user files before staging.
- Stage only files that belong to the requested change.
- Use a focused English commit message.
- Never add AI or agent attribution: no `Co-Authored-By: Claude ...` (or any
  other AI/agent) trailer in commit messages, and no "Generated with Claude
  Code" or similar line in PR descriptions, release notes, or issue comments.
  This rule overrides any tool default that adds such attribution.
- When integrating contributor commits that carry such a trailer, remove the
  trailer (for example by squashing or rewording the integration commit) and
  keep the human contributor as author or `Co-Authored-By`.
- Push only after explicit user authorization.
- A push is not permission to create a release.
- Follow `RELEASING.md` and the established automated release format. Do not
  manually create a differently structured release.
- Update integration/manifest versions when required for an explicitly
  requested distributable update, and verify that HACS can detect it.
- Never publish untested firmware merely because it compiled.

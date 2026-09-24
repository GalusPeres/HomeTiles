# Web Admin source

This directory owns the shared browser editor. Type-specific browser code
lives beside its firmware module in `src/types/<type>/admin*.js`.

`bundle.json` lists 56 complete source units in execution order. The asset
generator keeps a readable assembly at `src/web/assets/admin.js`. It separately
formats a delivery copy and creates the compressed includes and content hashes
used by firmware. Edit the source units, not the assembled or compressed files.

With Node.js/npm installed, run from the repository root. Install the locked
host dependencies before generation or tests, and again after lockfile changes:

```text
npm ci --ignore-scripts
node tools/generate-web-assets.mjs
node tools/generate-web-assets.mjs --check
node tools/tests/web/test-admin-bundle.mjs
node tools/run-tests.mjs
```

The browser still loads one classic script with the existing shared scope,
function names and execution order. No module loader, extra requests or runtime
npm dependency are introduced.

Delivery formatting uses pinned Terser 5.51.2 with `compress: false` and
`mangle: false`: ordinary comments and excess whitespace are removed; selected
license/preserve comments and identifier names stay intact. Optimizer passes
remain disabled. An independent Acorn syntax-tree check permits only lexical
metadata differences and equivalent ordinary data-property shorthand.
The checked delivery output is gzipped;
its bytes differ from the readable assembly. Asset savings do not establish
the final firmware size or runtime performance.

| Directory | Responsibility |
| --- | --- |
| `core` | Localization helpers and DOM-ready bootstrap |
| `settings` | Display preferences, network fields, access controls and diagnostics actions |
| `navigation` | Top-level tab activation and responsive panel sizing |
| `folders` | Lazy folder fragments, selection and bounded session cache |
| `files` | File-manager state, selection and chunked uploads |
| `firmware` | Browser OTA upload and GitHub update controls |
| `hardware` | Local hardware I/O editor |
| `screensaver` | Screensaver layout, wallpaper and clock editing |
| `tiles/state.js` | Shared tile/editor request and entity-cache state |
| `tiles/registry.js` | Type metadata, accessibility and callback dispatch |
| `tiles/snapshots.js`, `drafts.js`, `clipboard.js` | Saved-field snapshots, local drafts and copy/paste |
| `tiles/editor.js`, `type-selection.js` | Selection, rebinding and type changes |
| `tiles/live-preview.js`, `grid-preview.js` | Selected editor and cached-grid rendering |
| `tiles/autosave.js`, `import-export.js` | Persistence requests, serialization and import |
| `tiles/layout.js`, `drag-geometry.js`, `placement.js`, `drag-resize.js` | Grid geometry and interactions |

The generator parses each source unit and the complete bundle without
executing browser code. Every source file is listed once; an unlisted file
fails validation. DOM/browser tests continue to execute production functions.
Bundle checks cover the readable assembly; delivery checks compare syntax trees
and verify the compressed firmware content and hashes.

When adding a type, use its existing registry load/save/reset callbacks and
central translations. New type code must still support both preview paths,
drafts, autosave and import/export. See [CONTRIBUTING.md](../../../CONTRIBUTING.md)
and [ARCHITECTURE.md](../../../ARCHITECTURE.md) for the full integration path
and the remaining shared-state boundaries.

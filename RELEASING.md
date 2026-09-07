# Releasing HomeTiles

Releases are built and published automatically by GitHub Actions
([`.github/workflows/firmware.yml`](.github/workflows/firmware.yml)).
You never upload binaries by hand — you only bump the version and push a tag.

## Steps

Before tagging, install the locked host dependencies and validate generated
assets and tests from the repository root:

```text
npm ci --ignore-scripts
node tools/generate-device-profiles.mjs --check
node tools/generate-web-assets.mjs --check
node tools/run-tests.mjs
```

Web asset delivery uses pinned Terser 5.51.2 with `compress: false` and
`mangle: false`, an independent Acorn syntax-tree check, and deterministic gzip.
The readable source and assembly remain available; npm packages are build tools
and are not installed on the device or loaded by its browser UI. Commit any
required regenerated assets before preparing the release. Final firmware size
and hardware checks remain separate release requirements.

```bash
# 1. Bump the version (only when the current state is actually ready to ship!)
#    Edit version.txt:  #define FW_VERSION "vX.Y.Z"

# 2. Commit, tag, push (the tag must match FW_VERSION exactly)
git add version.txt
git commit -m "Release vX.Y.Z"
git tag vX.Y.Z
git push --atomic origin main refs/tags/vX.Y.Z
```

That's it. The action then:

1. Builds 15 explicit installer/release profiles for fourteen physical device
   profiles with the pinned toolchain (ESP32 core + libraries, see workflow
   `env`). Waveshare 7B/7B-C has a build for pre-v3 revisions 1–199 and a
   separate, experimental exact-v3.1 build. The latter uses profile
   `waveshare_7b_rev3_1` and assets containing
   `waveshare_touch_lcd_7b_rev3_1`. Exact-v3.1 hardware remains unverified.
   Every other current P4 profile is a vendor P4NRW32/pre-v3 target explicitly
   guarded to revisions 1–199. ESP32-P4 v3.2 or newer is unsupported with the
   pinned Arduino-ESP32 3.3.7 / ESP-IDF 5.5.2 toolchain.
2. Verifies that the tag matches `FW_VERSION` in `version.txt` — a mismatch
   fails the build on purpose.
3. Verifies the device descriptor and exact silicon-revision contract embedded
   in each binary. The v3.1 HomeTiles contract must be 301–301 even though the
   Arduino `v3.00 or newer` ESP image header can remain 301–399.
4. Creates the GitHub release with auto-generated notes and uploads all
   30 binaries (`<device>.bin` for OTA + `<device>_factory.bin` for first flash).

After all 30 assets were uploaded successfully, the release job explicitly
dispatches the documentation workflow for the release tag. This explicit
`workflow_dispatch` is required because GitHub suppresses ordinary follow-up
workflow events created with `GITHUB_TOKEN`. The documentation workflow
validates the installer device/asset contract, downloads the same 30 published
release assets, verifies their GitHub SHA-256 digests, and places them in the
generated documentation site under `firmware/latest/`. Normal documentation
changes pushed to `main` still deploy through the workflow's filtered `push`
trigger. The browser installer must fetch firmware from that same HTTPS origin
because GitHub release downloads do not provide the cross-origin response
header required by browser flashing. The `gh-pages` deployment is an orphan
snapshot so successive full-size factory images do not accumulate in the
branch history.

The canonical documentation URL is https://galusperes.github.io/. Its hosting
repository deploys the same verified `HomeTiles/gh-pages` snapshot and checks
for changes twice per hour. Run **Publish HomeTiles documentation** in
`GalusPeres/galusperes.github.io` for immediate publication after a release,
then check the live installer version and asset downloads.

Keep the two browser operations distinct. **Factory** must erase the full chip
and write the merged image at `0x0`. **Update** must never use that merged image
or an install flow that can opt into a full erase; it verifies `partitions.csv`
and the current redundant OTA selection, writes the regular app image once to
the inactive slot, verifies it, and only then commits a new redundant OTA
selection entry. NVS and LittleFS remain untouched.
`tools/tests/build/test-installer-otadata.mjs` and `tools/tests/build/test-web-installer.mjs` guard this
contract.

The checked-in release notes are not copied into the GitHub release
automatically. After the workflow succeeds, replace the generated GitHub text
with the matching `docs/releases/vX.Y.Z.md` content and keep the asset list.

Keep every release note in this order:

1. One short sentence describing what the release changes. Do not start with
   update instructions.
2. `## Highlights` with concise user-visible changes.
3. `## Update Notes` with the required Bridge version and image guidance.
4. `## Hardware Confirmed`.
5. `## Pending Hardware Validation`.
6. Credits and the full changelog link.

The release notes must keep **hardware-confirmed** devices and devices with
**pending hardware validation** in separate sections. A successful CI compile
does not by itself prove display, touch, storage, networking, or OTA behavior.

Devices pick up the new version via their GitHub OTA check as soon as the
release is published (GitHub CDN propagation can add a few minutes).

## Rules that prevent past mistakes

- **Only the tag push triggers a release build.** Normal pushes to `main`
  build nothing (PRs do get build checks). Bumping `version.txt` alone does
  not release anything.
- **Don't bump `version.txt` while still developing.** If you flash a dev
  build that already carries the final version string, that device will later
  think it is up to date and skip the real OTA update (this happened with
  v0.3.3). Bump the version as the last step before tagging.
- **Don't pre-create a draft release for the tag.** The workflow can't see
  drafts and would create a second release. If you want custom release notes,
  edit them *after* the workflow finishes (web UI or `gh release edit`).
- **Keep target-specific networking paths separated.** Guition JC8012P4A1 V1
  uses its field-validated single-block RX variant; every other published P4
  target uses the checked-in a8204 ESP-Hosted baseline, and ESP32-S3 targets
  use native WiFi. CI verifies the expected markers before packaging.

## Preparing a candidate without releasing

Push a release-preparation branch, open a draft pull request, or dispatch the
firmware workflow manually for that branch. The workflow builds all targets and
keeps the binaries as private GitHub Actions artifacts for 14 days. Do not bump
`version.txt`, create the final tag, or create a GitHub draft release. This gives
the candidate full GitHub build coverage without making it available to OTA.

## If something goes wrong

For an explicitly requested same-version S3 correction, return the existing
release to Draft, push the tested fix, then dispatch `firmware.yml` with
`repair_s3_release=vX.Y.Z`. The version must match `version.txt`. This uses the
normal CI compiler and packager for the three S3 profiles, replaces only their
six assets, verifies all P4 asset hashes remain unchanged, and publishes only
after verification. `s3-rebuild.json` records the exact source commit and hashes;
the existing Git tag remains unchanged. Device OTA must be field-tested before
using this maintenance path. Older installed downloaders may need one Web Admin
update, and devices already reporting the same version will not auto-upgrade.

- A failed run can simply be re-run from the Actions tab — asset upload uses
  `--clobber`, so re-runs are idempotent.
- Tag pushed but wrong/missing version bump? Fix `version.txt`, then move the
  tag: `git tag -f vX.Y.Z && git push -f origin vX.Y.Z` (the re-run rebuilds
  and re-uploads).

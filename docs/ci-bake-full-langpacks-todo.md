# TODO: bake the full language-pack collection into CI-published dists

## Status
**GitHub CI: DONE.** The merge-to-main firmware workflow now publishes **fully localized**
flashable dists — the complete language-pack collection baked into the internal vfs at
`/lang-packs/<locale>/…`, so a user who flashes a CI artifact gets a cardless, localized
device. **GitLab/Forgejo: still English-only** (their dist bake is noted-but-disabled; mirror
the GitHub pattern when it's enabled — see below).

## Goal (met on GitHub)
`dist/<BOARD>/` ships with every locale's pack baked in, not just the English floor.

## How GitHub does it (`.github/workflows/build-firmware.yml`)
The pack producer is a **Docker image** (`scripts/build_packs.sh` → the pinned
`seedsigner-langpack-builder`), and the firmware `build` job runs **inside** the base-image
container, which has no Docker socket — so packs can't be built in that job. Instead:

1. **`build-packs` job (plain `ubuntu-24.04` runner, push-only).** Checks out
   `kdmukAI-bot/seedsigner-language-packs` at a **pinned SHA** (with its
   `seedsigner-translations` submodule — the on-device `.mo` source — riding along), runs
   `scripts/build_packs.sh` (pulls the public digest-pinned pack image → byte-identical
   output), and uploads the collection as the `lang-packs` workflow artifact.
2. **`build` job** `needs: build-packs`, downloads the artifact, and runs
   `SS_PACKS_DIR=<packs> make dist` so `resolve_packs()` bakes them into the vfs. A guard
   fails the build if `vfs.bin` comes out < 1 MB (an English-only regression).

PRs stay compile-only (no dist, no pack build). `workflow_dispatch` is likewise
compile-only. Only `push` to `main` produces a localized dist.

### Bumping the pinned pack source
The `build-packs` checkout `ref:` pins the exact `seedsigner-language-packs` commit, which
(via its own translations submodule) fully determines the baked bytes — a builder-repo
change is required to change dist output (reproducible). **Bump that `ref` deliberately** when
translations or locale policy change, keeping it compatible with the frozen app's translation
version (the pinned `deps/seedsigner` and its `seedsigner-translations`).

## Remaining
- **GitLab/Forgejo** publish no localized dist yet. When their dist bake is enabled, mirror
  the GitHub shape: build packs in a **non-container** stage (or fetch a prebuilt bundle),
  stage them, and export `SS_PACKS_DIR` before `ci.sh collect-dist` (which now honors a
  pre-set `SS_PACKS_DIR`).
- **(optional) Fetch-prebuilt instead of build-in-CI.** Lighter CI, but needs
  `seedsigner-language-packs` to publish a consumable release/artifact first (its `packs.yml`
  currently builds-and-discards for the determinism gate). Revisit if pack-build minutes
  matter or once signed release bundles exist.

## Notes / constraints
- **Flash budget:** the ~2 MB pack image fits comfortably on the 32 MB P4-43 (release target,
  auto-vfs ~19.7 MB) and fits but is tighter on 16 MB boards (auto-vfs ~3.7 MB). See the
  design doc's flash-budget section.
- **Builder never builds packs.** The *source of truth* for pack bytes stays
  `seedsigner-language-packs`; CI just stages them, exactly as local dev does from the
  sibling app's built `src/lang-packs`.

## Cross-refs
- BUILDER-13 (the bake mechanism this completes) — `docs/language-pack-onboard-storage-and-installer-design.md`;
  cross-repo ledger `/home/kdmukai/dev/docs/cross-repo-ledger.md`.
- Bake mechanism: `tools/build_launcher_fs.py`, `tools/mklittlefs_launcher.c`,
  `tools/_langpacks.py`, `docs/knowledge/esp32-auto-vfs-partition-and-launcher-bake.md`.
- Pack producer: `seedsigner-language-packs/scripts/build_packs.sh`.

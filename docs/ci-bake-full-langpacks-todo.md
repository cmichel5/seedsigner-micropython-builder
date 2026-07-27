# TODO: bake the full language-pack collection into CI-published dists

## Goal
The CI-published, self-booting flashable dist (`dist/<BOARD>/`) should ship **fully
localized** — the complete language-pack collection baked into the internal vfs at
`/lang-packs/<locale>/…` — so a user who flashes a CI artifact gets a cardless, localized
device. Today CI dists ship the **baked English floor only**.

## Why CI is English-only today
The vfs bake (`make dist` → `tools/build_launcher_fs.py`) copies whatever pack bytes it finds
at the app's bundled pack location and bakes them; an absent/empty source is a valid
English-only image (it logs `baked /main.py only (no language packs -> English-only)`). Two
gaps keep CI on that path:

1. **The packs aren't present in CI.** `src/lang-packs` is a **gitignored, built artifact** —
   the app is a pure *reader* of it. The bytes are produced by the separate
   **`seedsigner-language-packs`** repo (`scripts/build_packs.sh --out-dir src/lang-packs`).
   The pinned `deps/seedsigner` submodule CI checks out therefore carries **no** packs, and
   this builder repo deliberately **does not build packs** (see `tools/_devenv.py` /
   `resolve_packs()` — "points only at the app; does not know the pack repo").

2. **The active dist step isn't pointed at the packs.** GitHub CI runs `make dist` **directly**
   (`.github/workflows/build-firmware.yml`, step "Package flashable dist", ~line 183) with no
   `SS_PACKS_DIR`/`SS_APP_DIR` in its env, so `resolve_packs()` falls back to a nonexistent
   sibling path. (`scripts/ci/ci.sh`'s `collect-dist` case *is* already prepped with
   `SS_PACKS_DIR=$REPO_ROOT/deps/seedsigner/src/lang-packs`, but no CI config calls
   `collect-dist` — GitHub uses the inline `make dist`; GitLab/Forgejo have the dist bake
   noted-but-disabled.)

## What to do
1. **Make the packs available in the CI container** (pick one):
   - **Build them in CI** — check out `seedsigner-language-packs`, run its `build_packs.sh
     --out-dir "$REPO_ROOT/deps/seedsigner/src/lang-packs"` before the dist step. Verify the
     base image has its build deps (font tooling, `msgfmt`/gettext, the shaping/endonym
     pipeline); if not, add them to the base image, or
   - **Fetch prebuilt packs** — consume a released pack bundle (a `seedsigner-language-packs`
     release artifact) and unpack it to that dir. Lighter CI, but adds a versioned-artifact
     dependency to pin against the app/translations version.
2. **Point the dist step at the packs.** Set `SS_PACKS_DIR` (or `SS_APP_DIR`) on the GitHub
   "Package flashable dist" step so `make dist` bakes them; OR route packaging through
   `ci.sh collect-dist` (already wired) instead of the inline `make dist`. Do the same when
   GitLab/Forgejo enable their dist bake.
3. **Verify** the uploaded artifact: `vfs.bin` should be ~2 MB (not ~12 KB), and the bake log
   should read `baked /main.py + N pack file(s) across M locale(s)`.

## Notes / constraints
- **Flash budget:** the ~2 MB pack image fits comfortably on the 32 MB P4-43 (release target,
  auto-vfs ~19.7 MB) and fits but is tighter on 16 MB boards (auto-vfs ~3.7 MB). See the
  design doc's flash-budget section.
- Keep the builder's "does not build packs" philosophy intact: the *source of truth* for pack
  bytes stays `seedsigner-language-packs`; CI just stages them, exactly as local dev does from
  the sibling app's built `src/lang-packs`.
- Version alignment: the packs baked should match the frozen app's translation version — build
  packs from a `seedsigner-language-packs` revision compatible with the pinned
  `deps/seedsigner` (and its `seedsigner-translations`).

## Cross-refs
- BUILDER-13 (the bake mechanism this completes) — `docs/language-pack-onboard-storage-and-installer-design.md`;
  cross-repo ledger `/home/kdmukai/dev/docs/cross-repo-ledger.md`.
- Bake mechanism: `tools/build_launcher_fs.py`, `tools/mklittlefs_launcher.c`,
  `tools/_langpacks.py`, `docs/knowledge/esp32-auto-vfs-partition-and-launcher-bake.md`.

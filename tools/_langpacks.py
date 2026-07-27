"""Shared language-pack file selection: which pack files the DEVICE loads, and how
to enumerate them under a pack-source dir.

Defined ONCE here so both deployment paths stage the SAME bytes:
  * tools/sd_format_push.py   -- copies them onto the microSD  (/sd/<locale>/...)
  * tools/build_launcher_fs.py -- bakes them into the internal vfs  (/lang-packs/<locale>/...)

This module deliberately has no third-party dependency (no pyserial), so the dist
bake can import it in plain CI. The pack SOURCE is the app's bundled bytes
(`$SS_APP_DIR/src/lang-packs` via tools/_devenv.resolve_packs()); this repo only
COPIES them and never builds packs.
"""
import os


def is_runtime_file(rel):
    """True for a pack file the DEVICE loads, keyed by its path relative to the pack
    root ("<locale>/..."). Selects the subset font(s), pre-shaped runs, endonym images,
    the self-describing manifest, and the compiled catalog at its LC_MESSAGES subpath;
    skips debug artifacts (runs.json)."""
    base = rel.rsplit("/", 1)[-1]
    if rel.endswith("/LC_MESSAGES/messages.mo"):
        return True
    if base in ("manifest.json", "runs.bin"):
        return True
    if base.endswith(".ttf"):
        return True
    return base.startswith("endonym_") and base.endswith(".bin")


def collect_pack_files(packs_dir):
    """Every runtime file across all locale packs under `packs_dir`, as
    (host_path, relpath) where relpath is "<locale>/..." (LC_MESSAGES/ preserved).
    Debug artifacts (runs.json) are skipped by is_runtime_file(). An absent/empty dir
    returns [] (a valid English-only deploy), so this NEVER errors on a missing dir."""
    rels = []
    if os.path.isdir(packs_dir):
        for loc in sorted(os.listdir(packs_dir)):
            loc_dir = os.path.join(packs_dir, loc)
            if loc.startswith(".") or not os.path.isdir(loc_dir):
                continue
            for root, _dirs, fnames in os.walk(loc_dir):
                for fn in fnames:
                    full = os.path.join(root, fn)
                    rel = os.path.relpath(full, packs_dir).replace(os.sep, "/")
                    if is_runtime_file(rel):
                        rels.append((full, rel))
    rels.sort(key=lambda fr: fr[1])
    return rels

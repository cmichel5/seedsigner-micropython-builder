# D-8 — ESP32 microSD hotplug: builder-side design

Runtime microSD insert/remove detection + a safe mount lifecycle on the ESP32-P4
(MicroPython) firmware. Fast-follow to the settings-persistence pass (APP-19 /
BUILDER-11), which deliberately treated the card as present-at-boot and deferred
hotplug here.

- **Ledger:** `/home/kdmukai/dev/docs/cross-repo-ledger.md` → **BUILDER-12** (this repo:
  detection primitive + facade mount lifecycle) + **APP-25** (`⬜*`, app side: detection
  tick, state-change dispatch, fail-soft `save()`).
- **Detection mechanism (resolved 2026-07-23):** SD-peripheral **polling** — the P4-43 wires
  no card-detect line, so there is no interrupt source. See
  `docs/knowledge/esp32-p4-microsd-no-card-detect-gpio.md`.
- **App-side findings + UX** already documented in
  `docs/sd-card-settings-persistence-plan.md` → "D-8 (hotplug) app-side findings".

## Two decisions locked (2026-07-24)

1. **Facade owns the mount lifecycle; the app delegates.** Unmount breaks BUILDER-11's
   "two idempotent mounters coexist, first wins" symmetry (a stale `_sd_ready` flag or a
   stale app-side cache desync on removal). Hotplug needs exactly one authority. Layering
   only permits app → facade (the facade is a frozen dep *below* the app and is already the
   boot-time mounter), so the facade becomes the single owner and the app's `is_inserted` +
   poll tick call into it. This converges the two mounters BUILDER-11 consciously left
   coexisting.
2. **Poll from the app's main loop (Python), not a thread or a C task.** A Python
   main-loop tick is **GIL-serialized** with settings/pack I/O, which (a) eliminates the
   SDMMC bus-contention race by construction and (b) guarantees the poller never fires
   mid-file-write, so unmount is always at a quiescent point. A C FreeRTOS detector was
   rejected: it cannot own the oofatfs mount (MicroPython-side), so it would fight the MP
   layer on the bus — the exact risky case the knowledge docs flag.

## The live SD path (why no new C is needed)

Two SD paths exist; only one is in the firmware:

- **Not linked:** `ports/esp32/board_common/src/board_sdcard.c` (ESP-IDF `esp_vfs_fat`,
  `/sdcard`) is compiled but called only by the standalone `camera_capture` test app.
  Calling it in the firmware would trigger the oofatfs duplicate-symbol collision
  (`docs/knowledge/micropython-fatfs-vs-esp-idf-fatfs-collision.md`).
- **Live:** C boot powers the SD rail (`LDO_VO4`, acquired once and held for the device
  lifetime — `docs/knowledge/esp32-p4-sdcard-ldo-power.md`), then the frozen facade
  `deps/third-party/seedsigner_lvgl_screens.py` mounts `/sd` via
  `machine.SDCard(slot=0, width=4)` + `vfs.mount(VfsFat(sd), "/sd")` at import time.

Because the rail is held for the whole device lifetime, **re-insertion never needs
re-powering** — the one thing `machine.SDCard` cannot do (enable the LDO) is already done
permanently in C. Everything else the poller needs is on the existing `machine.SDCard`
block-device surface (`deps/micropython/upstream/ports/esp32/machine_sdcard.c`):

| Need | Primitive | Behavior |
|---|---|---|
| Liveness probe (card mounted) | `sd.readblocks(0, scratch)` → `bool` | Returns `False` on a pulled card (SDMMC transaction times out). Cheap, non-destructive, no re-enumeration. |
| Force re-enumeration | `sd.ioctl(2)` clears the cached init flag; next `info()`/`ioctl(1)`/`readblocks` re-runs `sdmmc_card_init` (CMD0/CMD8/ACMD41/CMD2/CMD3) | Detects a freshly-inserted card. |
| Tear down host on removal | `sd.deinit()` | Frees the SDMMC slot; a fresh `machine.SDCard(...)` re-inits on the next insertion. |

**No new C is required for detection/removal** — those are pure-Python facade helpers. **But live
reinsert-remount of a power-cycled card DID require a small C primitive** (`reinit_slot()`, see the
resolution at the bottom of this doc): re-enumeration needs the SDMMC *slot* re-inited in place, which
`machine.SDCard` had no way to do. The pure-Python-only conclusion held for removal, not reinsert.

> ⚠️ This "no new C" conclusion is reasoned from source, **not yet device-verified.** Three
> risks gate it (see Verification): (1) `readblocks` on a pulled card returns cleanly rather
> than hanging; (2) `vfs.umount` on a dead block device does not fault; (3)
> deinit → fresh `SDCard` → remount actually re-enumerates a swapped card.

## Facade API (the builder deliverable)

Extend `deps/third-party/seedsigner_lvgl_screens.py`. The facade becomes the sole owner of
the `/sd` lifecycle — it always constructs and holds the `SDCard` object (`_sd_dev`) rather
than tolerating a foreign mount.

- `_sd_dev` (module global) — the live `machine.SDCard` object, or `None` when unmounted.
- `_sd_scratch` — a preallocated 512-byte `bytearray` so the probe never allocates.
- `_sd_live() -> bool` — the honest "is `/sd` usable right now" primitive. `False` if not
  mounted; else `sd.readblocks(0, _sd_scratch)` in a `try/except` (any error → `False`).
  Backs the app's `MicroSD.is_inserted` on ESP.
- `_ensure_sd() -> bool` — unchanged role (mount if not mounted), but always constructs and
  stores `_sd_dev`; re-runnable after an umount. Still `os.stat`-guarded + fail-soft.
- `sd_poll() -> str | None` — the state-machine tick the app calls each loop:
  - mounted & probe fails → **removal**: `vfs.umount("/sd")` (swallow), `_sd_dev.deinit()`
    (swallow), `_sd_dev = None`, `_sd_ready = False` → return `"removed"`.
  - unmounted & a fresh construct+mount now succeeds → **insertion** → return `"inserted"`.
  - otherwise → return `None` (no change).
  - *Optional hardening:* require 2 consecutive failed probes before declaring removal, to
    swallow a transient bus glitch.

The app never touches `machine.SDCard` directly on ESP — it calls `sd_poll()` on its tick
and reads `_sd_live()` (via its `microsd.py` shim) for `is_inserted`.

## App ↔ facade contract (app side = APP-25, routed via the seedsigner-stack)

Per the plan doc's D-8 findings, the app must:

- Call `sd_poll()` on the Controller main-loop tick; on `"removed"`/`"inserted"` invoke
  `Settings.handle_microsd_state_change(action)` (the explainer UX already exists — forces
  Persistent Settings → Disabled + "Insert SD card to enable" help text on removal, restores
  on insertion).
- **Broaden the gate:** `handle_microsd_state_change` and the `RemoveSDCard*` toast /
  `RemoveMicroSDWarningView` paths are wrapped in `HOSTNAME == SEEDSIGNER_OS` — widen to also
  cover `IS_MICROPYTHON`.
- Point ESP `MicroSD.is_inserted` at the facade `_sd_live()` (delegation, per decision 1),
  replacing APP-19's boot-only mount check.
- **Fail-soft `save()`:** wrap the `open(SETTINGS_FILENAME, 'w')` in try/except — after a
  physical pull the mount reads stale-live for one poll cycle, so a setting-change write can
  race a gone card and raise an uncaught `OSError`. Closes the sharp edge independent of the
  full poll.

The toast UI these consume already exists (SCREENS-2 / BUILDER-5 ✅).

## Failure & edge cases

- **Removal mid-write (FAT corruption):** inherent to no-CD hardware; unavoidable in
  firmware. Mitigation is atomic open/write/close + the app's removal warning. The
  GIL-serialized poller cannot itself umount mid-write.
- **umount on a dead card:** FatFs `f_mount(0,...)` clears the FS object without flushing;
  with no open handles (guaranteed by the quiescent-point poll) it is metadata-only and safe.
  Swallow any error regardless.
- **Boot with no card:** `_ensure_sd()` already fails soft → app runs on the baked Western
  floor + English; `is_inserted` false. First insertion is picked up by `sd_poll()`.
- **Rapid insert/remove:** the optional 2-strike removal debounce + the fact that insertion
  requires a full successful init sequence (a bouncing contact fails init) keep spurious
  transitions out.

## On-device verification gates (P4-43)

**Status (2026-07-24): RESOLVED + device-validated on P4-43 (MAC …c1:ed, 32MB). Removal AND live
reinsert both work with no reboot and no crash.** The reinsert path is fixed by a C slot-reinit
primitive (`machine.SDCard.reinit_slot()`) that re-inits the SDMMC *slot* in place while keeping the
one global host + `s_request_mutex` alive — see the knowledge doc for the mechanism.

- ✅ **Boot clean**; card-present primitives healthy; fresh-mount reads work.
- ✅ **Removal (user-confirmed):** pull → `sd_live()` False → Persistent Settings auto-disables.
- ✅ **Reinsert (user-confirmed):** reinsert → `sd_poll()` calls `_sd_dev.reinit_slot()` + remounts →
  card re-enumerates in place, Persistent Settings re-enables. No reboot, no crash.
- ✅ **Rapid remove/reinsert in succession:** handled cleanly (user-confirmed).
- ✅ **Persistence round-trip:** set a Persistent Setting → full reboot → value written and retrieved.
- ✅ **REPL pre-check (present card):** `reinit_slot()` callable, returns OK (no mutex UAF), and a
  subsequent read re-enumerates (`live_after_reinit: True`, `ls /sd` lists locale dirs).

### Implemented fix — the C slot-reinit primitive
`machine.SDCard` gained a `reinit_slot()` method (patched into
`deps/micropython/upstream/ports/esp32/machine_sdcard.c` via
`deps/micropython/mods/patches/0001-esp32-integration-mods.patch`): the object now stores its
`sdmmc_slot_config_t` at construction, and `reinit_slot()` re-runs `sdmmc_host_init_slot()` (restoring
400 kHz/1-bit probing) + clears `CARD_INIT_DONE`, never calling `sdmmc_host_deinit()`. The facade's
`_ensure_sd()` calls it on the remount-of-a-reused-object path before `vfs.mount`. Exactly one
persistent `machine.SDCard` object is kept, never deinit'd. Full rationale + esp-idf source analysis:
`docs/knowledge/esp32-p4-sdcard-hotplug-no-host-deinit.md`.

> **Supersedes** the earlier "no new C is needed" conclusion in "The live SD path" section above:
> reinsert-remount of a power-cycled card *does* require the C slot re-init; the pure-Python
> `ioctl(2)`+remount path EBUSYs (stale slot) and `deinit()`+reconstruct crashes (global-host mutex UAF).

### Separate open item — endonyms don't load (pack-path mismatch, NOT D-8)
Fresh boot, card present: the Language screen doesn't show endonym images. Cause: the facade
resolves the pack root to `/sd/lang-packs/<locale>/` (its `_resolve("lang-packs")` default), but this
dev card has packs at `/sd/<locale>/` (`HAS_LANGPACKS_DIR False`, verified via REPL; a direct read of
`/sd/el/endonym_240.bin` works). Pre-existing facade/card-layout/`LOCALE_PACK_DIR` behavior, untouched
by D-8. **Open question for the user:** did endonyms load on this card before (regression vs.
card-setup)? Fix is either the card layout (put packs under `lang-packs/`) or the pack-dir config.

## Implementation & host validation (2026-07-24)

Full vertical slice implemented and host-validated (real SD hardware behavior — the 4 gates
above — still pending on device).

**Builder (this repo):** `deps/third-party/seedsigner_lvgl_screens.py` — `_sd_dev`/`_sd_scratch`
state, re-runnable `_ensure_sd()` (holds the device), and public `sd_ensure()` / `sd_live()` /
`sd_poll()` (throttled state machine).

**App (`deps/seedsigner` submodule, on `integration/lvgl-mpy` @ `1d48f549` + working changes):**
- `hardware/microsd.py` — `ensure_mounted()` and ESP `is_inserted` delegate to the facade
  (`sd_ensure`/`sd_live`); new `MicroSD.poll()` dispatches insert/remove → Settings + toast.
- `models/settings.py` — `handle_microsd_state_change` gate broadened to `IS_MICROPYTHON`;
  `save()` wrapped fail-soft (`OSError` → warn, no crash).
- `controller.py` — ESP boot state sync (absent-at-boot → `handle_microsd_state_change(REMOVED)`).
- `gui/lvgl_screen_runner.py` — `MicroSD.poll()` on the ESP pump tick (the only frequent Python
  cadence).
- `tests/test_microsd.py` — rewritten for the delegation contract + `poll()` dispatch coverage.

**Validation (host, CPython):** a standalone harness faking `machine`/`vfs`/the native module
exercised the real facade state machine + app delegation — **22/22** (mount, live-probe,
remove→umount, re-insert→remount, throttle, all delegations, poll dispatch). `pytest`
`test_microsd.py` **10/10**; the CI-style main suite (`test_flows` + `test_flows_settings` +
`test_lvgl_screen_runner` + `test_microsd` + `test_settings`) **68/68**. (The screenshot
generator needs the native `seedsigner_lvgl` `.so`, absent in the minimal env — unrelated to
these changes, which don't touch the CPython render path.)

**Deviation from the plan's D-8 findings:** that note suggested also broadening the
`controller.py:338` `RemoveSDCard*` toast / `RemoveMicroSDWarningView` boot tip to ESP. Left
Pi-only deliberately — it is the "you can remove the SD card now" tip specific to the
**boot-off-SD** model (the P4 boots from internal flash, not the removable card). The runtime
insert/remove notification ESP needs is the `SDCardStateChange` toast, wired via `MicroSD.poll()`.

## Routing

- **Builder (this repo):** the facade helpers above → a builder PR + a submodule/stack bump.
  Facade is dev-overlayable, so iterate without a full firmware rebuild.
- **App (`seedsigner`):** APP-25 changes route through the seedsigner-stack (mpy-compat layer).
  Must not land the app's delegation before the facade helpers exist.

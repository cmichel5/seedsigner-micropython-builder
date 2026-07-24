# ESP32-P4 microSD hotplug: the SDMMC host is a global singleton — remount needs an in-place slot re-init, not `deinit()`/reconstruct

## TL;DR

Making a **physically reinserted** microSD usable again (without a reboot) is the hard part of
D-8 hotplug on ESP32-P4 (esp-idf v5.5.1) + MicroPython `machine.SDCard`. Every `machine.SDCard`
object shares **one global esp-idf SDMMC host** (`s_host_ctx`, and the transaction mutex
`s_request_mutex`). Because of that:

- **`deinit()` + reconstruct → crash.** Constructing/GC-destroying `SDCard` objects init/deinit the
  *global* host. An orphaned object's `__del__` (`sd_deinit → sdmmc_host_deinit`) frees the global
  transaction mutex out from under the live object → the next read faults on a NULL `s_request_mutex`.
- **reuse the one object, no `deinit()` → safe but insufficient.** Keeping a single persistent
  `SDCard` never tears the host down (mutex stays valid → no crash), but a plain remount does **not**
  re-configure the SDMMC *slot*, so a power-cycled (reinserted) card fails to re-enumerate
  (`vfs.mount` → `OSError 16` EBUSY).

**The fix is not achievable in pure Python.** It needs a small **C primitive** that re-initializes
the *slot* (and forces a card re-enumeration) **in place, keeping the global host + mutex alive** —
never calling `sdmmc_host_deinit()`.

> **RESOLVED (2026-07-24, device-validated on P4-43).** Implemented as option (1) below —
> `machine.SDCard.reinit_slot()`, a patch to `machine_sdcard.c`. Option (1) alone was sufficient; no
> host deinit/re-init (option 2) needed. On device: removal + live reinsert + rapid remove/reinsert +
> a persistent-setting write/read across reboot all work, no reboot and no crash. See "The implemented
> fix" at the bottom.

## The hardware/software model

- One SD card, 4-bit SDMMC, slot fixed by IOMUX. VDD is a held-for-life on-chip LDO (`LDO_VO4`,
  see `esp32-p4-sdcard-ldo-power.md`) — power never drops across a physical remove/insert.
- The card is mounted on the **MicroPython** side (`machine.SDCard(slot=0, width=4)` + `vfs.mount`
  of `VfsFat`); ESP-IDF's `esp_vfs_fat` is deliberately NOT linked (oofatfs collision, see
  `micropython-fatfs-vs-esp-idf-fatfs-collision.md`).
- **Everything global:** `esp-idf/components/esp_driver_sdmmc/src/sdmmc_host.c` keeps a single
  `static host_ctx_t s_host_ctx` and `esp_driver_sdmmc/src/sdmmc_transaction.c` a single
  `static QueueHandle_t s_request_mutex`. `machine.SDCard` is a thin wrapper over these globals;
  there is no per-object host.

## Root cause of the crash (deinit + reconstruct)

Core-dump backtrace (`mp_task`, reading `endonym_*.bin` off `/sd` after a reinsert):
`machine_sdcard_readblocks → sdmmc_read_sectors → sdmmc_host_do_transaction(slot=1) →
xQueueSemaphoreTake(xQueue=0x0)` → fault. `xQueue=0x0` is `s_request_mutex` (taken at
`sdmmc_transaction.c:105`).

The two esp-idf guards that make this a use-after-free:

```c
// sdmmc_host.c
esp_err_t sdmmc_host_init(void) {
    if (s_host_ctx.intr_handle) {                 // <-- "already initialized, skipping init flow"
        return ESP_OK;                            //     SKIPS sdmmc_host_transaction_handler_init()
    }
    ... esp_intr_alloc(..., &s_host_ctx.intr_handle);
    ... sdmmc_host_transaction_handler_init();     // creates s_request_mutex (only on a full init)
}
esp_err_t sdmmc_host_deinit(void) {
    if (!s_host_ctx.intr_handle) return ESP_ERR_INVALID_STATE;
    for (slot..) sdmmc_host_deinit_slot_internal(slot);
    sdmmc_host_deinit_internal();                  // frees mutex + clears intr_handle
}
```

`s_request_mutex` is created only when `sdmmc_host_init()` runs its **full** flow (i.e. when
`intr_handle` was NULL) and freed by `sdmmc_host_deinit()`. So if a `sdmmc_host_deinit()` ever runs
while another code path still expects the host up, the mutex goes NULL and stays NULL (the next
`sdmmc_host_init()` sees `intr_handle` already set — wait, deinit cleared it — the desync comes from
**multiple SDCard objects**, below).

**The trigger is object churn, not a single deinit.** During the "card removed" window the Python
poll retries `_ensure_sd()` ~1×/s; each failed attempt that constructs a `machine.SDCard` (host
init → mutex created) and is then orphaned gets its `__del__`/`sd_deinit` (→ `sdmmc_host_deinit` →
mutex freed) run by the GC at an arbitrary later time. Interleave that GC-deinit with the live
object's read and you dereference a freed/NULL global mutex. The lesson: **never let more than one
`machine.SDCard` object exist, and never `deinit()` the one you keep.**

## What the current firmware does (safe, but reinsert needs a reboot)

`deps/third-party/seedsigner_lvgl_screens.py` (D-8 facade): construct the `SDCard` **once**, keep it
for life in `_sd_dev`, never `deinit()`. On removal: `vfs.umount("/sd")` + `sd.ioctl(2, 0)` (clears
only the cached *card*-init flag). On reinsert: remount the same object. Result:

- ✅ No crash (single object, host never torn down).
- ✅ Removal detected (`sd_live()` = `readblocks(0)` fails on a gone card → `is_inserted` False →
  Persistent Settings auto-disables). **User-confirmed on device.**
- ❌ Reinsert does **not** re-enumerate: remounting the reused object doesn't re-configure the slot
  for the freshly-powered card → `OSError 16` EBUSY. The card is unusable until reboot.

## The implemented fix (C primitive) — option (1), device-validated 2026-07-24

**What shipped:** `machine.SDCard` now stores its `sdmmc_slot_config_t` at construction and exposes
`reinit_slot()` (SDMMC-only; SPI raises). `reinit_slot()`:
1. `sdmmc_host_init_slot(self->host.slot, &self->slot_config)` — re-runs slot init on the existing
   global host. Per esp-idf v5.5.1 `sdmmc_host.c`: it guards on `s_host_ctx.intr_handle` (always alive
   here — we never deinit), allocates **no** new resources (no queue/interrupt/mutex), resets the slot
   to 400 kHz + 1-bit **probing** state, and — because `active_slot_num` already equals the slot — does
   **not** bump `num_of_init_slots` (no refcount leak). So `s_request_mutex` stays valid throughout:
   no NULL-mutex window.
2. Clears `SDCARD_CARD_FLAGS_CARD_INIT_DONE` → the next `readblocks`/`ioctl(INIT)` re-runs
   `sdmmc_card_init()` (CMD0/CMD8/ACMD41/CMD2/CMD3) against the fresh card.

**Why the old EBUSY happened (now understood):** construction is the *only* place that ran
`sdmmc_host_init_slot()`; a reused object's remount never re-ran it, so the slot stayed at the previous
card's 40 MHz/4-bit and a freshly-powered card couldn't probe. `reinit_slot()` restores probing state.

**Facade wiring:** `_ensure_sd()` calls `_sd_dev.reinit_slot()` on the remount-of-a-reused-object path
(before `vfs.mount`); the single persistent `machine.SDCard` object is never deinit'd.

**Delivery:** patch hunk for `ports/esp32/machine_sdcard.c` inside
`deps/micropython/mods/patches/0001-esp32-integration-mods.patch`; facade change in
`deps/third-party/seedsigner_lvgl_screens.py`.

## The original fix direction (preserved for context)

Re-enumerate a reinserted card **without touching the host-level transaction handler**. Keep the
single persistent `machine.SDCard`/global host (mutex intact); add a C call that re-initializes the
**slot** and forces a card re-init, then let the existing `vfs.mount` succeed.

Concretely, investigate/implement one of:

1. **A `machine.SDCard` patch or new method** (`bindings/` or a patch to
   `deps/micropython/upstream/ports/esp32/machine_sdcard.c`, delivered via
   `deps/micropython/mods/patches/0001-esp32-integration-mods.patch`) that calls
   `sdmmc_host_init_slot(slot, &slot_config)` again (re-configures the slot: clock, width, pins)
   **without** `sdmmc_host_deinit()`, then clears `CARD_INIT_DONE` so the next `readblocks`
   re-runs `sdmmc_card_init()` (CMD0/CMD8/ACMD41/CMD2/CMD3). This keeps `s_host_ctx.intr_handle`
   and `s_request_mutex` alive — no NULL-mutex window.
   - Rationale for why plain `ioctl(2)`+remount EBUSYs but this should work: the EBUSY is the card
     re-enumeration failing because the *slot* state (clock divider, bus width, controller) is stale
     after the card left; `sdmmc_host_init_slot()` resets exactly that. The host/mutex don't need to
     move.
2. If (1) is insufficient, a board-side C helper (e.g. in `ports/esp32/board_common` or
   `display_manager`, near the existing `sd_power_on`) that performs a full, **atomic**
   `sdmmc_host_deinit()` + `sdmmc_host_init()` + `sdmmc_host_init_slot()` in one call — atomic so the
   mutex is freed and recreated within one call with no GC/other-object interleaving — exposed to
   MicroPython as a single binding the facade calls on reinsert (still keeping exactly one
   `machine.SDCard` object; the helper owns the host cycle, not object construction).

### First things to read/confirm next session
- `sdmmc_host.c`: `sdmmc_host_init_slot()`, `sdmmc_host_deinit_slot_internal()`,
  `sdmmc_host_deinit_internal()`, and the `s_host_ctx.num_of_init_slots` refcount — to pick between
  (1) and (2) and get the exact call sequence.
- `machine_sdcard.c`: `machine_sdcard_make_new` (how it builds `slot_config` + calls
  `host.init()`/`sdmmc_host_init_slot`), `sd_deinit`, the `ioctl` cases, `sdcard_ensure_card_init`.

### Hardware-iteration loop (this needs the user at the bench)
Each candidate = rebuild + reflash + **user physically pulls then reinserts the card** + open
Language (or REPL-probe). There is no way to simulate a power-cycled card in software (the LDO rail
is held; you cannot drop card power from MicroPython). Use `tools/mpy_repl.py`
(`exec`/`run`/`stream`) over `/dev/ttyACM<n>` for probing; expect the card to sit at
`/sd/<locale>/` on this dev card (not `/sd/lang-packs/`, see the endonym note below).

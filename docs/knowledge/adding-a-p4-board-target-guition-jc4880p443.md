# Adding a new ESP32-P4 board target (lessons from the Guition JC4880P443 bring-up)

The Guition JC4880P443 is a near-twin of the Waveshare ESP32-P4 WiFi6 Touch LCD 4.3
(same ST7701S 480×800 MIPI-DSI panel class, same GT911 touch). Bringing it up was a
**board config + pin deltas, not a port** — the shared `board_display_st7701.c` + GT911
drivers and the P4-43 DPI timing worked unchanged. What follows is the non-obvious part:
the wiring a new board needs *outside* the board_config.h + MP board-def, and two
platform gotchas that cost a debug cycle each.

## A new board needs THREE per-board switches in `scripts/build_firmware.sh`

Registering `board_config.h` + the MicroPython board def is not enough. `build_firmware.sh`
has three independent `case "$BOARD"` switches; a new board that only wires the first one
**builds and flashes fine, boots through all hardware init, then aborts in the app**:

1. `BOARD_CONFIG_DIR` — maps the board to its `board_common/boards/<dir>`. Miss it → the
   build warns and can't find `board_config.h`.
2. `SEEDSIGNER_DISPLAY_HEIGHT` — selects which `SUPPORT_DISPLAY_HEIGHT_*` display profiles
   get **compiled into** the screens library. It must match the board's runtime *landscape*
   height (480 for a 480×800 panel → 800×480 landscape). The default is `320`. Miss it and
   the board boots cleanly through display + touch + `Board initialized`, then the app
   aborts at `gui_constants.cpp`: **`FATAL: no display profile for 800x480`** → an
   `abort()` with a huge repeating backtrace (looks like a stack overflow; it isn't).
3. The `CHIP_TYPE` flash-hint `case` (`*ESP32_P4*` glob) — cosmetic (only the printed
   "Flash with:" hint), but a board name without `ESP32_P4` in it prints `esp32s3`.

Lesson: when adding a board, grep `scripts/build_firmware.sh` for **every** `case "$BOARD"`
and add the new board to each, not just the config-dir one.

## Native USB-Serial-JTAG console (no CH343 bridge)

Unlike the Waveshare P4 boards (CH343 USB-UART bridge wired to UART0), the Guition's only
USB serial is the P4's **built-in USB-Serial-JTAG**. Consequences:

- Set it as the **primary** console in the board's `sdkconfig.defaults`:
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` + `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`
  (the Waveshare boards keep UART0 primary + JTAG secondary). This puts boot logs + the
  REPL on the JTAG port. Writes are non-blocking when no host is attached, so it never
  stalls at boot.
- **The port re-enumerates on every reset** (the chip's own USB drops + reappears), and it
  **renumbers** `/dev/ttyACM0 ↔ /dev/ttyACM1` between resets. Always drive esptool and the
  deploy tools at the stable **`/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00`**
  path, not a bare `ttyACMx`. A reader holding the port and esptool cannot share it
  (esptool fails with "device reports readiness to read but returned no data / multiple
  access on port") — don't run a serial capture concurrently with esptool.
- Flash over the **"Full-speed / USB2" Type-C** (the connector farther from the 2×13 pin
  header). The "High-speed" Type-C is the USB-OTG PHY — not a serial/JTAG console and not a
  flashing port; a second cable there does **not** give a second log channel.

## First boot after a firmware-only flash: unformatted vfs → app can't launch

`make docker-build-all` + `esptool write_flash micropython.bin` flashes only bootloader /
app / partition-table. The MicroPython **vfs** partition is *not* in the CSV — the esp32
port auto-registers it at runtime from the end of the last partition to the end of flash
(here 0xC50000..0x1000000, ~3.8 MB, littlefs2). esptool never writes that region, so it
holds whatever was there before (prior firmware garbage). On first boot MicroPython then
logs **"The filesystem appears to be corrupted … perform factory reprogramming"** and `/`
is left **unmounted** — `os.statvfs('/')` returns all-zeros. Any runtime launcher write
(`deploy_app.py` / `set_p4_boot_app.py`) then fails with **`OSError: [Errno 19] ENODEV`**
opening `/main.py`. The flash hardware is fine: `esp32.Partition('vfs')` +
`vfs.VfsLfs2.mkfs()` + mount + write all succeed manually.

Two ways to provision the launcher cleanly:

- **One-time manual format**, then deploy: `vfs.VfsLfs2.mkfs(esp32.Partition('vfs'))` over
  the REPL, then `set_p4_boot_app.py` (writes `/main.py` and resets → frozen app boots).
  The formatted vfs + `/main.py` survive later *firmware* reflashes (esptool doesn't touch
  the vfs region), so this is a one-time step per unit.
- **`make dist BAKE_LAUNCHER=1`** (the intended self-booting path): `build_launcher_fs.py`
  bakes a pre-formatted littlefs2 `vfs.bin` with `/main.py` in it and appends its offset to
  `flash_args`, so a fresh flash of the dist boots straight to the app — no first-boot
  format, no runtime deploy.

## SD language-pack provisioning: order vs the auto-booting app

`tools/sd_format_push.py` was written for a **bare-REPL** board: it hard-resets, then
`machine.SDCard(...)` + `VfsFat.mkfs` + `vfs.mount('/sd')` + pushes packs. Once `/main.py`
auto-boots the SeedSigner app, that breaks: the app's C facade **mounts `/sd` itself (and
FAT-formats the card on mount failure)** during startup, before the tool's Ctrl-C reaches
the REPL. The tool's own `vfs.mount('/sd')` then raises **`OSError: [Errno 1] EPERM`** —
which is `mp_vfs_mount`'s "already mounted" error, *not* a disk/write-protect fault (raw
`sd.readblocks`/`writeblocks` and `VfsFat.mkfs` all succeed independently). Symptom trail
that pinpoints it: `MKFS_OK` + `VFSFAT_OBJ_OK` but `vfs.mount` EPERM, and `os.listdir('/sd')`
already works with a real `statvfs`.

Two ways to provision packs:
- **Push straight to the app's existing `/sd` mount** — skip the tool's format/mount and
  just `open('/sd/<locale>/...','wb')` over the REPL (reuse `collect_pack_files` +
  `_push_file` from `sd_format_push.py`). This is what worked here (78 files / 22 locales).
- **Provision the SD *before* writing `/main.py`** (bare-REPL board), then deploy the
  launcher — the original tool's intended order.

The SD hardware itself needs no tuning: `machine.SDCard(slot=0, width=4)` inits, 4-bit
read+write work, and the same IOMUX pins (39–44) + LDO path as the P4-43 apply. SD VDD is
default-on via a P-FET here (no ch4 LDO enable needed), but enabling it is harmless.

## Flash chip

Boya (JEDEC mfr `0x68`, dev `0x4018`, 16 MB). IDF logs `Detected boya flash chip but using
generic driver` — benign here (format / mount / run all work). `CONFIG_SPI_FLASH_SUPPORT_BOYA_CHIP=y`
is available if a chip-specific driver is ever wanted.

## What "just worked" (twin parity)

ST7701S DSI init + P4-43 DPI timing (500 Mbps, 30 MHz DPI, HBP42/HSYNC12/HFP42/VBP2/VSYNC8/VFP60),
GT911 touch, PSRAM at 200 MHz, and — notably — **touch-to-display coordinate mapping in
landscape needed no per-board swap/mirror tuning** (it matches the P4-43). Deltas vs the
twin: LCD RST GPIO5, backlight GPIO23 **non-inverted**, touch RST22 / INT21, 16 MB flash,
USB-JTAG primary console. Camera (OV02C10, not the twin's OV5647) and audio are left
disabled — phase 2/3.

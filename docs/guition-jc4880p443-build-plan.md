# Build plan — GUITION_JC4880P443 board target

**Goal:** add a MicroPython firmware board target for the **Guition JC4880P443** (owned) and flash it. It is a near-twin of the release target (Waveshare ESP32-P4 WiFi6 Touch LCD 4.3): same ESP32-P4, same **ST7701S 480×800 MIPI-DSI** panel class, same **GT911** touch. So this is a **board config + pin deltas, not a port** — reuse the existing `board_display_st7701.c` and GT911 drivers.

This plan is self-contained: the full pinmap is below so it survives even if the (gitignored) `docs/board-schematics/` collection is cleaned.

---

## Orientation (read first)

- Hardware eval + rationale: [esp32-p4-hardware-evaluation.md](esp32-p4-hardware-evaluation.md) §3c
- **Fork target (the twin):** `ports/esp32/board_common/boards/waveshare_p4_lcd43/board_config.h` — a filled-in template of every field a P4 board needs. Also `deps/micropython/mods/new_files/ports/esp32/boards/WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_43/mpconfigboard.cmake`.
- Shared drivers already in-repo: `ports/esp32/board_common/src/board_display_st7701.c`, `board_init.c`.
- **Vendor package** (gitignored): full zip at repo root `JC4880P443C_I_W.zip` (~309 MB); small useful files extracted to `docs/board-schematics/guition-jc4880p443/vendor-extract/` (schematic PNGs `1_PWR`…`7_OTHER`, English spec PDF, `pins_config.h`, flash configs). ESPHome working config: `docs/board-schematics/guition-jc4880p443/esphome/`.
- **Camera sensor is OV02C10** (2 MP OmniVision) — confirmed by the `OV02C10_CSP_DS` datasheet + a board-specific IPA JSON in the package. **Not** SC2336, **not** OV5647.

## Pinmap — GUITION JC4880P443 (from vendor schematic sheet 3 + ESPHome, CONFIRMED unless noted)

| Function | Pin(s) | vs. Waveshare P4-43 | Notes |
|---|---|---|---|
| Display | ST7701S MIPI-DSI **2-lane**, 480×800 portrait | same class | dedicated P4 DSI pins |
| LCD reset | **GPIO5** | Waveshare 27 | Arduino demo uses −1; try GPIO5 |
| Backlight (LCD_PWM) | **GPIO23**, LEDC, **non-inverted** | Waveshare 26, **inverted** | |
| DSI PHY LDO | esp_ldo **ch3, 2.5 V** | same | |
| I²C (touch + camera SCCB + codec) | SDA **GPIO7**, SCL **GPIO8**, 400 kHz | same | net `ES_I2C` = `RTC_*_SDA1/SCL1` = GPIO7/8 |
| Touch (GT911) | RST **GPIO22**, INT **GPIO21** | Waveshare 23 / NC | wired even though Arduino demo left them −1 |
| microSD (SDMMC 4-bit) | CLK **43**, CMD **44**, D0 **39**, D1 **40**, D2 **41**, D3 **42** | **same as Waveshare** | |
| microSD power | esp_ldo **ch4, ~2.7 V** | — | TF_VCC via P-FET, default-on (GPIO45 gate, R10 NC) |
| Camera sensor | **OV02C10** (2 MP, up to **1920×1080** / 1288×728), I²C ~0x36 | Waveshare OV5647 | bespoke 15-pin 0.3 mm flip FPC |
| Camera SCCB | GPIO7/8 (shared I²C) | same bus | |
| Camera CSI | 2-lane, dedicated P4 pins | same | control IOs `CSI_IO0`/`CSI_IO1` + XCLK → **trace** (see phase 2) |
| C6 radio (SDIO, unused) | reset **GPIO54**; cmd 19 / clk 18 / d0–3 = 14/15/16/17 | GPIO54 reset same | drive reset low, no radio |
| Console UART0 | TXD **GPIO37**, RXD **GPIO38** | | |
| Boot mode | **GPIO35** | | |
| Audio (optional, phase 3) | ES8311+ES7210: MCLK 13 / SCLK 12 / DSDIN 9 / LRCK 10; PA_CTRL 10(?) | | LRCK vs PA_CTRL GPIO10 conflict — trace |

---

## Phase 1 — display + touch (do first, minimal)

1. **board_common config** — create `ports/esp32/board_common/boards/guition_jc4880p443/board_config.h` by copying `waveshare_p4_lcd43/board_config.h`, then apply the deltas above:
   - `BOARD_NAME "Guition JC4880P443"`
   - Backlight `BOARD_PIN_LCD_BL GPIO_NUM_23`, `BOARD_BACKLIGHT_INVERTED 0`
   - `BOARD_PIN_LCD_RST GPIO_NUM_5`
   - `BOARD_PIN_TOUCH_RST GPIO_NUM_22`, `BOARD_PIN_TOUCH_INT GPIO_NUM_21`
   - Keep: I²C SDA7/SCL8, DSI LDO ch3/2500, 2-lane DSI, 480×800, SD pins 39–44, C6 reset GPIO54
   - `BOARD_HAS_CAMERA 0`, `BOARD_HAS_AUDIO 0` for this pass
   - **Panel timing:** start with the Waveshare ST7701 DPI timing (same controller + resolution). If the panel won't sync, take the exact init/timing from the vendor MIPI init (`JC4880P443C_I_W.zip` → `1-Demo/arduino_examples/lvgl_v9_sw_rotation/src/lcd/esp_lcd_st7701_mipi.c`) or ESPHome's `model: JC4880P443` preset in `esphome/esphome` source.
2. **MicroPython board def** — create `deps/micropython/mods/new_files/ports/esp32/boards/GUITION_JC4880P443/mpconfigboard.cmake` from the Waveshare one; 16 MB flash / 32 MB PSRAM; partition table. **First confirm how `board_config.h` is selected per board** (grep the board-selection var / include path in `board_common/CMakeLists.txt` + `board_init.c`) and wire the new board to `guition_jc4880p443`.
3. **Build** (use the `esp-build` skill): `BOARD=GUITION_JC4880P443 make docker-build-all`. Iterating on MicroPython mods → add `MP_ALLOW_DIRTY=1`.
4. **Flash** — flash over the **USB2 / "Full-speed Type-C"** port = the P4 USB-Serial-JTAG (the USB-C **farther from the 2×13 pin header**; enumerates as Espressif VID `303a`, e.g. `/dev/ttyACM0` or `/dev/serial/by-id/...USB_JTAG_serial_debug_unit...`). The "High-speed Type-C" is USB-OTG and is **not** the flashing port. Ensure ≥600 mA supply. From `build/GUITION_JC4880P443/`: `python -m esptool --chip esp32p4 write_flash @flash_args`.
   - Gotchas (repo memory): run `esptool flash_id` **first** to confirm port/chip; **never** pipe `esptool write_flash | head` (SIGPIPE → partial flash); P4 console is on UART0 (GPIO37/38) and the board also enumerates USB — confirm which port is which.
5. **Validate:** boot logo / UI renders; touch responds. (No camera/SD yet.)

## Phase 2 — camera (OV02C10)

- **Confirm on-device:** `i2c` scan the GPIO7/8 bus → expect the sensor near **0x36**.
- **Driver:** the vendor zip bundles `esp_cam_sensor` with an **ov02c10** driver + P4 IPA JSON (`1-Demo/idf_examples/components/esp_cam_sensor/sensors/ov02c10/`). Check whether the upstream `esp_cam_sensor` our `board_common` camera pipeline pulls already includes OV02C10; if not, port the vendor's `ov02c10.c` + `cfg/*.json` in (self-contained sensor add — see eval doc §6).
- **Camera control pins:** SCCB on GPIO7/8; CSI 2-lane on dedicated P4 pins; the reset/pwdn (`CSI_IO0`/`CSI_IO1`) + XCLK GPIOs come from the vendor IDF camera example (`JC4880P443C_I_W.zip` → `1-Demo/idf_examples/ESP-IDF_5.5.4/esp_brookesia_phone/components/apps/camera/app_video.c` + `Camera.cpp`), or trace on schematic sheet `3_ESP32-P4.png`.
- Set `BOARD_HAS_CAMERA 1` + the camera pins; rebuild; validate QR capture.
- **Physical:** the camera faces front out of the box — reroute the flex to face rearward (already confirmed doable by opening the case and folding the stock flex).

## Phase 3 — SD + audio (as needed)

- **SD:** pins already match Waveshare (39–44). Ensure ESP_LDO **channel 4** (~2.7–3.3 V) is enabled for TF_VCC (see ESPHome sibling `SIBLING-jc1060p470-device.yaml` + schematic `6_CODEC&TFCARD.png`). Set `BOARD_HAS_SDCARD 1`.
- **Audio** (ES8311 + ES7210): optional, low priority for a signer; pins on sheets 3/6 (resolve the LRCK vs PA_CTRL GPIO10 conflict first).

## Open items to resolve during the build

- Exact ST7701S panel init/timing for JC4880P443 (try Waveshare's first; fall back to vendor init / ESPHome preset).
- Camera `CSI_IO0`/`CSI_IO1` → GPIO + XCLK (from the vendor IDF camera example).
- The per-board `board_config.h` selection mechanism in the build (confirm before wiring the new board).
- Audio GPIO10 LRCK/PA_CTRL conflict (only if doing audio).

## Key references

- Full pinmap: this doc (above). Durable board facts also in hardware-kb: `guition/jc4880p443/board.md`. Supplementary: `docs/board-schematics/guition-jc4880p443/VENDOR-DOCS-NOTE.md`.
- Vendor init/drivers/schematic: `JC4880P443C_I_W.zip` (root, gitignored) + `docs/board-schematics/guition-jc4880p443/vendor-extract/`.
- ESPHome working config (pin cross-check): `docs/board-schematics/guition-jc4880p443/esphome/`.
- In-repo twin: `ports/esp32/board_common/boards/waveshare_p4_lcd43/` + `board_display_st7701.c` + `board_init.c`.

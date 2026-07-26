# Guition JC4880P443 camera — follow-ups (post phase-2)

Phase 2 (OV02C10 camera) is **working and device-validated**: sensor detected, live
preview, animated-QR scanning, and the image-entropy still all function, with correct
orientation and full color. Two polish items remain, deferred to a later session.

## 1. Exposure: too bright + no auto-exposure adaptation

**Symptom (device):** the camera preview/scan is much too bright, and the exposure is
**fixed** — it does not adapt to scene lighting.

**Root cause:** no auto-exposure loop is running for the OV02C10.
- The OV5647 (the twin's sensor) enables **on-sensor AGC/AEC** in its register init
  (`0x3503` = `MENU_AG_AE`, `0x3A05` `step_auto_en`), so it self-adapts with no external
  help. The **OV02C10 driver leaves its AEC/AGC-control write commented out**
  (`components/ov02c10/ov02c10.c`, ~line 959: `// ov02c10_write(..., OV02C10_REG_AEC_AGC_CTL, 0xB8)`),
  so the sensor has no auto-exposure of its own.
- There is also **no IPA tuning JSON** in the build (`ESP_IPA_JSON_CONFIG_FILE_PATH` is
  unset), so `esp_video`'s ISP IPA auto-exposure loop never starts — `esp_video_init.c`
  calls `esp_ipa_pipeline_get_config(sensor_name)`, gets NULL, logs "failed to get
  configuration to initialize ISP controller", and skips the controller. (This is true for
  OV5647 too; the OV5647 gets by on its on-sensor AGC/AEC.)

**Fix options (pick one):**
- **(a) Enable OV02C10 on-sensor AGC/AEC** — mirror the OV5647 approach: set the AEC/AGC
  control register (the commented `OV02C10_REG_AEC_AGC_CTL` write) and tune. Cleanest match
  to how the twin works; keeps the no-IPA pipeline. Needs on-device AE tuning.
- **(b) Provide the vendor IPA JSON** — the vendor zip ships
  `.../sensors/ov02c10/cfg/ov02c10_default_p4_eco4.json` and `..._eco5.json` (P4 chip
  revisions). Copy into `components/ov02c10/cfg/`, wire the `ESP_IPA_JSON_CONFIG_FILE_PATH`
  build property so `esp_ipa` embeds it and `esp_video` finds it by the sensor name. Gives
  proper ISP-driven adaptive AE, but this also spins up the IPA ISP-pipeline controller that
  currently runs for no sensor — validate memory/task impact.
- **(c) Stopgap** — set `CONFIG_BOARD_CSI_AE_TARGET` (currently `0` = ISP default) to a lower
  fixed value in the board's `sdkconfig.board`. `csi_set_ae_target()` applies it as a one-time
  `V4L2_CID_EXPOSURE`. Reduces brightness but stays non-adaptive.

Recommended: (a) first (matches the twin, no IPA side-effects); fall back to (b) if on-sensor
AE proves inadequate.

## 2. Boot screen: light-blue flash instead of black

**Symptom (device):** on power-up, the Guition's screen shows a "crashed light-blue" color
during init, before the app takes over. The P4-43 twin was tuned to come up **black** instead.

**Likely cause:** the uninitialized DSI framebuffer (which reads as a color) is shown before
anything clears it to black, and the Guition's backlight (**non-inverted GPIO23**) turns on
early enough to reveal it. The P4-43 twin's DSI boot choreography (board_common; see the
`project_dsi_boot_choreography` memory / board_common PR#11 — "white-flash + backlight dip"
fixes) sequences the backlight after the panel is cleared / clears to black early.

**Fix direction:** ensure the framebuffer is cleared to black before the backlight ramps up on
this board, or apply the same choreography the twin uses. Start in
`ports/esp32/board_common/src/board_display_st7701.c`, `board_backlight.c` (the "start OFF"
logic + `BOARD_BACKLIGHT_INVERTED`), and the `board_init.c` boot sequence; compare the
Guition path (non-inverted backlight, `BOARD_PIN_LCD_BL GPIO_NUM_23`) against the Waveshare
P4-43 (inverted backlight) to find where the ordering differs.

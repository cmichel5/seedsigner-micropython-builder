# Guition JC4880P443 camera — follow-ups (post phase-2)

Phase 2 (OV02C10 camera) is **working and device-validated**: sensor detected, live
preview, animated-QR scanning, and the image-entropy still all function, with correct
orientation and full color. This doc tracks the camera follow-up items after phase 2:
exposure (§1), boot colour (§2), and the image-entropy still capture (§3).

## 1. Exposure: too bright + no auto-exposure adaptation

**Symptom (device):** the camera preview/scan is much too bright, and the exposure is
**fixed** — it does not adapt to scene lighting.

**Root cause:** no auto-exposure loop runs for the OV02C10 at all.
- The OV02C10 driver has **no on-sensor AE**: it exposes only manual exposure/gain
  setters (`ov02c10_set_exp_val` / `ov02c10_set_total_gain_val`) meant to be driven by
  an external ISP loop. (Register `0x3503` = `OV02C10_REG_AEC_AGC_CTL` is manual
  gain/exposure control, **not** an on-sensor AE-loop enable — so the earlier idea of
  "mirror the OV5647 and enable on-sensor AGC/AEC" is a dead end for this sensor. The
  OV5647 twin self-adapts only because it genuinely has an on-sensor AGC/AEC loop.)
- There is also **no IPA tuning JSON** in the build (`ESP_IPA_JSON_CONFIG_FILE_PATH` is
  unset), so `esp_video`'s ISP IPA auto-exposure loop never starts.
- With `CONFIG_BOARD_CSI_AE_TARGET=0`, `csi_start()` skips the exposure override entirely,
  so the sensor sits at its 1288x728 format default (`exp_def ≈ 894`, of a ~1149 max) with
  gain at its `gain_def=1` floor — a long integration time → over-bright, and static.

### ✅ Stopgap DONE — fixed lower exposure (non-adaptive)

Implemented (pending on-device tuning of the exact value):
- **`GUITION_JC4880P443/sdkconfig.board`**: `CONFIG_BOARD_CSI_AE_TARGET` `0 → 450`
  (≈ half the ~894 default). `csi_set_ae_target()` writes this straight to
  `V4L2_CID_EXPOSURE` → `ESP_CAM_SENSOR_EXPOSURE_VAL` → `ov02c10_set_exp_val()` at each
  camera start, shortening the integration time. **Tune on device**: lower = darker,
  higher = brighter, clamped by the sensor to `[8, 1149]`.
- **`board_common/Kconfig`**: widened `BOARD_CSI_AE_TARGET` `range 0 235 → 0 1200` (235
  couldn't reach the OV02C10's exposure scale) and rewrote the misleading "128 = neutral"
  help — the value is a raw sensor exposure register write, not a normalized AE setpoint.
- **`board_common` `board_pipeline_camera_csi.{h,c}`**: widened the `ae_target` field
  `uint8_t → uint16_t` in both the config struct and the driver ctx, so values > 255
  survive (a `uint8_t` would have truncated 450 → 194).

### Deferred proper fix — adaptive AE via the vendor IPA JSON

For real scene-adaptive exposure (the remaining open item): copy the vendor OV02C10 IPA
JSON (the vendor zip ships `.../sensors/ov02c10/cfg/ov02c10_default_p4_eco4.json` and
`..._eco5.json` for the P4 chip revisions) into `board_common/components/ov02c10/cfg/`,
wire the `ESP_IPA_JSON_CONFIG_FILE_PATH` build property so `esp_ipa` embeds it and
`esp_video` finds it by the sensor name. This spins up the ISP IPA controller (which
currently runs for no sensor on these boards) — validate its memory/task impact on device.
Once adaptive AE works, drop the fixed `CONFIG_BOARD_CSI_AE_TARGET` back to `0`.

## 2. Boot screen: light-blue flash instead of black — ⏸️ SET ASIDE (known cosmetic artifact)

**Symptom (device):** on a COLD boot the screen shows a ~3 s light-blue flash before the
boot logo. The P4-43 twin comes up black.

**Root cause (device-verified):** the light-blue is the ST7701S panel's own output during
the pre-initialisation window — after power-up but before `board_display_st7701_init()` runs
and starts streaming the (calloc'd, black) DPI framebuffers. The DPI FBs are genuinely
zeroed (`esp_lcd_panel_dpi.c` uses `heap_caps_aligned_calloc`), so this is not stale FB
data; it is the panel emitting a blue default while nothing drives the DPI. The twin's panel
happens to show black in that same window — same firmware, different panel behaviour.

**Why it's hard:** the window is BEFORE firmware controls the backlight or panel (the
backlight is already on from hardware power-up), so the backlight/black-default-screen tricks
that gave the twin a black boot cannot reach it. A `BOARD_BACKLIGHT_KEEP_ON_AT_BOOT 1 → 0`
attempt was tried and **did not change it** (confirmed in the flashed firmware). A real fix
would need to assert the panel reset (`BOARD_PIN_LCD_RST`) and/or force the backlight off
from very early boot (bootloader level), and might only shorten rather than eliminate the
flash. It is also awkward to iterate: cold-boot-only (warm/esptool resets don't reproduce
it), the USB-JTAG console drops during early boot, and there's no flash-log partition.

**Decision:** deferred as a cosmetic cold-boot artifact — the board boots and works. The
`KEEP_ON` knob is left at `1` (twin-consistent); the artifact is documented in
`board_config.h` so it isn't re-attempted. Revisit only if it becomes a priority.

## 3. Image-entropy capture hang — ✅ FIXED (device-validated)

**Symptom (device):** in the image-entropy flow, pressing capture showed "Capturing…"
forever — the live preview kept running and the confirm image never arrived.

**Root cause:** the high-res still grab (`grab_still` in the esp-camera-pipeline component)
was **not rotation-aware**. The camera PPA rotates 90° on this board (portrait-native DSI
panel; `board_pipeline` pre-rotates the camera to the landscape canvas), and a 90° rotation
swaps the output width/height. `grab_still` sized its crop against the still dims directly,
ignoring the swap, so `ppa_do_scale_rotate_mirror` failed the fit check
(`ppa_srm: scale does not fit in the out pic`) on every frame. The grab silently never set
`still_ready`, so the entropy consumer polled `lock_still()` forever and never froze the
pipeline. (The OV5647 twin escaped it because its 1280×960 frame made the 960 square a clean
1:1 grab; the OV02C10's 1288×728 forced an upscale that also tripped a quantization overshoot
of the same PPA check.)

**Fix (device-validated):**
- **esp-camera-pipeline `grab_still`** — made rotation-aware, mirroring the display pass: for
  a 90/270° rotation it computes the crop against the still dims mapped back into sensor space,
  plus a clamp so the ceil-quantised scaled block can never exceed the output pic (the
  overshoot that silently failed the old 960 square). `camera_entropy.cpp` gained
  `BOARD_ENTROPY_STILL_W/H` (defaulting to the square `DIM`) so a still can be non-square.
- **Orientation proof:** a temporary full-frame **portrait** still (544×960) captured the whole
  FOV and confirmed on-device that the sensor's long (1288) axis maps to the display's SHORT
  axis — so a widescreen-landscape still that fills the 800-wide display is **not achievable**;
  the extra field of view is vertical.
- **Final framing:** a centred **square**, `BOARD_ENTROPY_STILL_DIM 960 → 720` (fits the 728-tall
  frame, no upscale, rotation-invariant). Confirm image matches the live preview, centred +
  pillarboxed. Device-confirmed working.

## Validation status

- **Exposure (dim-now):** ✅ device-validated — `CONFIG_BOARD_CSI_AE_TARGET=450` is acceptable
  ("a little dim; the real fix is adaptive AE"). IPA-JSON adaptive AE remains the deferred
  proper fix (§1).
- **Boot-color:** ⏸️ set aside (§2).
- **Entropy capture:** ✅ device-validated — square confirm image correct (§3).

Changes span three repos: the builder (`sdkconfig.board`, `camera_entropy.cpp`), the
`board_common` submodule (`board/guition-jc4880p443`: `board_config.h`, `Kconfig`,
`board_pipeline_camera_csi.{c,h}`), and the nested `esp-camera-pipeline` submodule
(`feat/ppa-output-mirror`: `esp_cam_pipeline.c`). Committing is a re-pin chain.

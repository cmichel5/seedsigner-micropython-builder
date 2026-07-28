# Guition JC4880P443 camera — tuning record

What the OV02C10 camera on this board is configured to do and the on-device
measurements behind each value. Referenced from the `ov02c10` component's
PROVENANCE.md and from `board_config.h`, which cite the evidence here rather than
repeating it.

Not a task list — the mechanisms live in
`docs/knowledge/esp32-p4-ipa-adaptive-ae-awb-ov02c10.md`; this is the numbers that
justify the shipped settings, plus one cosmetic artifact deliberately left alone (§2).

## 1. Exposure: too bright + no auto-exposure adaptation — ✅ FIXED (adaptive AE + AWB)

**Symptom (device):** the camera preview/scan is much too bright, the exposure is
**fixed** (it does not adapt to scene lighting), and the image carries a greenish cast.

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

### Superseded stopgap — fixed lower exposure

Before the loop existed, `CONFIG_BOARD_CSI_AE_TARGET=450` pinned `V4L2_CID_EXPOSURE`
at roughly half the ~894 format default: dim enough to use, but static. It is now `0`
— an AE loop drives the same control, so a fixed value would just be overwritten (the
driver warns if a board sets both). The widened `BOARD_CSI_AE_TARGET` Kconfig range and
the `uint8_t → uint16_t` widening of the `ae_target` field stay, since a future sensor
with neither on-sensor AE nor a tuning file would still need them.

### ✅ The fix — esp_ipa closed-loop AE + AWB

`esp_ipa` runs the loop the sensor lacks, fed by the ISP's AE/AWB/histogram statistics:
exposure and gain go to the sensor, red/blue gains and CCM/gamma/denoise/sharpen to the
ISP. Auto white balance is what removes the green cast — a Bayer sensor at unity red and
blue gain is green-dominant, and nothing was correcting it before.

- **Tuning file** `board_common/components/ov02c10/cfg/ov02c10_seedsigner_p4_eco4.json`,
  derived from the vendor's `ov02c10_default_p4_eco4.json` (eco4 is correct here — the
  vendor picks eco4 below chip rev v3, and this part is v1.0, read with `esptool chip_id`).
  Registered via the component's new `project_include.cmake`. Deltas from the vendor file
  and which keys the pinned `esp_ipa` 0.2.0 actually reads are in the component's
  `PROVENANCE.md`.
- **Board opt-in** `BOARD_CAMERA_IPA_CONFIG_NAME` in `board_config.h`. Boards without it
  (the OV5647 targets) are untouched.
- **`CONFIG_BOARD_CSI_AE_TARGET` → 0.**

Device-verified: on the first camera open the AGC immediately re-drives exposure
(`set exposure 0x37e` → `0x2ba`) instead of holding a fixed value, and the pipeline still
runs at 30 fps with no change to the preview/decode path.

### Per-session AE metering — QR scan centre-weighted, image entropy flat

`agc.luma_adjust.weight` weights the ISP's 5×5 measurement grid. The two camera uses want
opposite things: QR scanning wants the middle of the frame (where the code is held) to
decide exposure, while image entropy wants the whole scene to count equally. `esp_ipa`
re-reads that table from the config pointer every frame, so the profile is switched
per session — `board_pipeline_set_ae_metering()`, called from `camera_scanner` and
`camera_entropy` before `cam_pipeline_create()`.

Making that possible is why the tuning file is keyed `OV02C10_SS` rather than `OV02C10`:
the mismatch makes `esp_video`'s own lookup miss, so `board_pipeline_camera_csi.c` can
create the ISP pipeline against a RAM copy it can keep retuning. The expected boot
signature is `esp_video_init: failed to get configuration to initialize ISP controller`
(the intended miss) immediately followed by `pipeline_cam_csi: ISP pipeline running IPA
'OV02C10_SS' …`. **The warning on its own means the takeover did not happen.**

⚠ Every metering table must sum to the same total (75), because `esp_ipa` caches that sum
once at pipeline creation and re-reads the table itself. Enforced at runtime. Full
mechanism, including the disassembly this rests on:
`docs/knowledge/esp32-p4-ipa-adaptive-ae-awb-ov02c10.md`.

### Measured on device (same scene, four firmwares)

Captured by pulling the entropy still back over the REPL and measuring channel means —
worth repeating that way rather than judging by eye, since a phone photo of the LCD
white-balances against the room and misreports the cast badly.

| build | R/G | B/G | mean | contrast σ |
|---|---|---|---|---|
| pre-IPA baseline (fixed exposure 450) | 0.849 | 0.707 | 0.089 | 0.130 |
| IPA, target 75 | 0.946 | 0.874 | 0.267 | 0.102 |
| IPA, target 45 | 1.256 | 1.903 | 0.229 | 0.021 |

- **AWB works**: B/G 0.707 → 0.874 is the green cast being pulled toward neutral.
- **Do not lower the setpoint to 45.** It collapses on a backlit scene (a ceiling lamp in
  frame, dark surroundings): the lamp dominates the 5×5 metering grid, the AE cuts
  exposure to protect it, everything else falls to the sensor noise floor, and what
  survives is pedestal — which the CCM tints purple and AWB then amplifies into nonsense
  gains. Result is a flat purple field, σ 0.021.
- **A fixed black-level pedestal was the washed-out image.** With `acc` (CCM +
  saturation), `adn` (denoise/demosaic) and `aen` (gamma/sharpen/contrast) enabled,
  every frame floored at luma **0.214** — identical across three scenes and two AE
  setpoints. Nothing reached black, so the image compressed into a ~20-level band.
  Dropping all three restored it (floor 0.036, contrast σ 0.009 → 0.091).

  ⚠ **That attribution was wrong, and the correction matters.** The pedestal was the
  SENSOR's own black level (§ below) expanded by `aen`'s 0.72 gamma — not something
  `acc` added. Once the sensor pedestal was removed at source, `acc` was restored with
  the black floors still at exactly 0.000, and it is what fixes saturation: without a
  colour matrix, raw sensor channel crosstalk renders washed out (mean HSV saturation
  0.386 → 0.551, +43%). Only `adn` and `aen` remain out — we supply our own gamma and
  do not need their denoise/sharpen.
- ⚠ These numbers come from a pathological scene (camera aimed at a ceiling light, no
  good targets). Re-validate on a normally-lit scene with a real QR code before tuning
  further — this one is dominated by the light source.

### The ISP tone curve, and why the AE setpoint is coupled to it

Symptoms: dim, low-contrast preview with blacks that never reach zero.

Two causes, one fix. **The ISP GAMMA block is bypassed unless something writes a
curve** — `esp_video` only enables it on a `V4L2_CID_USER_ESP_ISP_GAMMA` write, and
the only thing that did was the `aen` block removed above. So the panel was being
handed linear light, which reads dim and flat. Separately, **the sensor's black
offset is never subtracted**: the ESP32-P4 ISP has a black-level block but ESP-IDF
5.5.1 ships no driver for it and `esp_video` exposes no control, leaving a floor
around 9/255.

`board_pipeline_set_tone(gamma_x10, black_level)` folds both into one 16-point
hardware LUT — `y = 255·clamp((x−black)/(255−black),0,1)^(1/gamma)`. They MUST be
combined: gamma applied to an un-subtracted pedestal lifts 9/255 to roughly 60/255,
making the washed blacks materially worse. Cost is one ioctl per camera session and
no per-frame CPU (cheaper than `aen`, which recomputed gamma every frame).

⚠ **The AE setpoint is in post-gamma units, so it must move with the curve.** The
ISP meters at `ISP_AE_SAMPLE_POINT_AFTER_GAMMA`: adding a gamma lift makes the loop
read the scene as brighter and cut exposure, which *undoes* the curve and lands
worse than no curve at all. Measured, tone curve on, same scene:

| AE setpoint | 75 | 110 | 140 | 200 |
|---|---|---|---|---|
| black floor | 0.095 | 0.016 | 0.016 | 0.016 |
| peak | 0.635 | 1.000 | 1.000 | 1.000 |
| mean | 0.191 | 0.298 | 0.312 | 0.313 |

Hence `target` 75 → **115** alongside `BOARD_CAMERA_TONE_GAMMA_X10 22` (125 first, then
trimmed after the preview read slightly bright in a well-lit room). 18% mid-grey encodes
to ~117 under gamma 2.2, so this sits at textbook mid-grey and inside the measured
plateau. Changing one without the other will regress the image.

**Black point: remove the pedestal at source, do not subtract around it.** The sensor
adds a black-level pedestal of 64/1024 (~16/255) via register `0x4003`, and nothing
downstream removes it, so the white-balance gains scale it **per channel** and the
gamma expands what survives. Measured residual: **+27/255 on red, +45/255 on blue**,
with highlights still neutral (R/G 0.950, B/G 1.020) — i.e. shadows rendered washed
navy while the white balance itself was correct.

A single global black point cannot cancel a per-channel offset. Chasing it that way
does not converge: 10 floored at 0.192 (worse than no curve), 24 looked better but was
crushing **31% of the frame** to solid black, 40 crushed half. Setting the sensor
target to 0 instead drops all three channels to exactly **0.000**, after which the
black point is a contrast toe rather than a repair:

| gamma / black (pedestal removed) | mean | σ | crushed to black |
|---|---|---|---|
| linear (no curve) | 0.143 | 0.183 | 8.9% |
| 2.2 / 0 | 0.356 | 0.188 | 0.01% |
| **2.2 / 12** | 0.283 | **0.211** | 0.68% |
| 2.2 / 18 | 0.215 | 0.241 | 28.9% |
| 2.2 / 24 | 0.196 | 0.236 | 31.5% |

**12 is the shipping value**: more contrast than 0, only 0.7% crushed.

**The AE ceiling is real (previously only inferred).** In a dim room, setpoints 100,
125, 150, 175, 200 and 230 produce an identical frame (mean 0.266±0.001, σ 0.140) —
the loop is already at maximum exposure and gain, so asking for more changes nothing.
Brightness there is a light/gain limit, not a tuning one. The setpoint only bites in
scenes bright enough for the loop to have headroom, which is why this setpoint is safe: above
the ceiling it costs nothing, below it it targets correctly.

### Tunables worth revisiting on device

- `agc.luma_adjust.target` (115) — overall brightness, in POST-GAMMA units.
  `target_low`/`target_high` (74/130) are the deadband: widen to damp hunting, narrow to
  track faster. 45 is measurably far too low (above); move in small steps and re-check on
  a backlit scene.
- `BOARD_CAMERA_TONE_GAMMA_X10` (22) / `BOARD_CAMERA_TONE_BLACK_LEVEL` (12), and
  `BOARD_CAMERA_COLOR_SATURATION` / `_CONTRAST` (128 = neutral, on top of the CCM's own
  saturation of 136). All live-tunable on a debug build via
  `camera_scanner.set_tone()` / `set_color()` / `set_ae_luma_target()`.
- `agc.anti_flicker` — set to `"none"` (vendor ships `"full"`). Anti-flicker quantises
  every exposure to a mains half-period: 10 ms at 50 Hz. This format runs `vts=1164` at
  30 fps = 28.64 µs/line, so the loop's whole usable range collapses to ~10/20/30 ms —
  all long enough to smear a hand-held QR code, and device-observed overexposing a
  lamp-lit scene by settling on exactly 20 ms (`set exposure 0x2ba` = 698 lines). `"part"`
  is NOT a middle ground: it still forces the quantised exposure whenever gain has room
  to move. Cost of `"none"`: possible banding under AC lighting — revisit if it shows up.
- `awb.min_red_gain_step` / `min_blue_gain_step` (0.034) — how finely white balance
  tracks. Raise to damp colour hunting.
- The centre-weighted table in `board_pipeline_camera_csi.c` (middle 3×3 ≈ 73% of the
  total) — only change it for another table summing to 75.

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

- **Exposure + white balance:** adaptive AE/AWB is live and device-verified at the
  mechanism level (the loop starts and drives exposure; 30 fps unchanged). Still owed a
  **visual** pass: confirm the preview tracks a lighting change, the green cast is gone,
  and QR decode is no worse — then tune the values in §1 to taste (§1).
- **Boot-color:** ⏸️ set aside (§2).
- **Entropy capture:** ✅ device-validated — square confirm image correct (§3).

Changes span two repos: the builder (`sdkconfig.board`, `camera_entropy.cpp`,
`camera_scanner.cpp`) and the `board_common` submodule (`board_config.h`, `Kconfig`,
`CMakeLists.txt`, `board_pipeline.{c,h}`, `board_pipeline_camera_csi.{c,h}`, and the
`components/ov02c10` tuning file + `project_include.cmake`). Committing is a re-pin
chain. The earlier §3 work also touched the nested `esp-camera-pipeline` submodule.

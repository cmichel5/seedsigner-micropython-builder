# Adaptive auto-exposure and auto-white-balance on ESP32-P4 (esp_ipa), and how to
# re-weight metering per camera session

Context: the Guition JC4880P443's OV02C10 camera. Applies to any ESP32-P4 board
whose sensor has no auto-exposure of its own.

## The problem this solves

The OV02C10 is a RAW-Bayer sensor with **no on-sensor auto-exposure and no
auto-white-balance**. It exposes only manual setters (`ov02c10_set_exp_val` /
`ov02c10_set_total_gain_val`) meant to be driven by an external loop. Register
`0x3503` is manual gain/exposure *control*, not an AE-loop enable — "mirror the
OV5647 and turn on the sensor's AGC/AEC" is a dead end on this part, because the
OV5647 self-adapts only by virtue of genuinely having an on-chip loop.

Symptoms without a loop: the preview sits at one fixed exposure (the format
default, ~894 of a ~1149 max — far too bright) and never adapts to the scene, and
the image carries a **green cast**, because a Bayer sensor with unity red/blue
gains yields a green-dominant image and nothing is correcting it.

## The loop: esp_ipa

The exposure/white-balance loop is not in the sensor driver — it is
`espressif/esp_ipa`, driven by `esp_video`'s ISP pipeline controller:

```
ISP AE/AWB/histogram stats  →  esp_video isp_task  →  esp_ipa pipeline
                                                       │  agc → exposure + gain → sensor
                                                       │  awb → red/blue gains  → ISP
                                                       └  acc/adn/aen → CCM, denoise, gamma, sharpen
```

Two things must both be true for any of it to run:

1. `CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y`, **and**
2. `esp_ipa_pipeline_get_config(<name>)` must return a configuration.

Point 2 is the one that silently does nothing. That lookup is served by a C file
`esp_ipa` **generates at build time** from every JSON listed in the
`ESP_IPA_JSON_CONFIG_FILE_PATH` CMake build property. With no JSON registered,
the generated file is a five-line stub that always returns NULL:

```c
const void *esp_ipa_pipeline_get_config(const char *name) { return (void *)0; }
```

`esp_video_init()` then logs one WARNING ("failed to get configuration to
initialize ISP controller") and carries on with no loop at all. A build can
therefore have the pipeline controller enabled, the ISP producing perfectly good
RGB565, and still have zero auto-exposure. **To check a build, look at
`build/<BOARD>/esp-idf/espressif__esp_ipa/esp_video_ipa_config.c` — if it is the
five-line stub, no IPA is running.**

A JSON is registered from a component's `project_include.cmake` (not its
`CMakeLists.txt` — the property is consumed while `esp_ipa` itself is configured):

```cmake
idf_build_set_property(ESP_IPA_JSON_CONFIG_FILE_PATH
    "${COMPONENT_PATH}/cfg/<file>.json" APPEND)
```

Vendors ship tuning files per silicon class, selected on
`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`: below v3 → the `eco4` file, v3 and later →
`eco5` (which additionally configures lens-shading and black-level correction and
a colour-temperature-interpolated CCM). Read the part's revision with
`esptool chip_id` before picking. Note that `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`
does not exist in ESP-IDF 5.5.1 — a CMake test on it silently evaluates false, so
copying the vendor's selector verbatim onto an older IDF picks `eco5` for
everything.

## Version-matching the tuning file

The JSON schema tracks the `esp_ipa` version, and `esp_ipa`'s generator ignores
keys it does not know rather than failing. A vendor file written for a newer
`esp_ipa` therefore *builds cleanly* while quietly dropping tuning. Against
`esp_ipa` 0.2.0, the vendor OV02C10 file's `awb.model`, `awb.range`,
`awb.green_luma_*` and `agc.f_m0` are all inert — and its AWB gain steps (0.34 /
0.4) are sized for the newer algorithm, coarse enough under 0.2.0's simpler
white-patch algorithm to be worth replacing with that version's own scale (0.034).

Check the generator (`tools/config/esp_ipa_config.py`) for what the pinned
version actually consumes; it is short and readable, and it can be run standalone
against a candidate JSON to see exactly what C it produces.

Two live bugs in `esp_ipa` 0.2.0's generator, both harmless but worth knowing:
`min_blue_gain_step` is emitted from `min_red_gain_step` (so blue always equals
red), and the generated `esp_ipa_pipeline_get_config` for a no-JSON build returns
`const void *` rather than the declared type.

## Anti-flicker quantises exposure — mind it on a scanner

`agc.anti_flicker.mode = "full"` forces every exposure to a multiple of the mains
half-period: at 50 Hz that is 10 ms. On a 30 fps sensor whose maximum exposure is
~33 ms, the loop is left with exactly three usable exposures (10, 20, 30 ms), all
long enough to smear a hand-held QR code.

`"part"` reads like a middle ground — the docs say it ignores anti-flicker when
gain cannot compensate — but **it is not**, because gain almost always has room to
move, so the quantised exposure is forced anyway. Device evidence on the OV02C10
(`vts=1164` at 30 fps → 28.64 µs/line): under `"part"` the loop settled on
`set exposure 0x2ba` = 698 lines = **19.99 ms**, i.e. exactly 2 × the 50 Hz
half-period, and visibly blew out a lamp-lit scene it should have exposed at well
under 10 ms. Use `"none"` on a scanner and accept possible banding.

A related trap when reading logs: under anti-flicker the *first* exposure write
after the format default is often just the startup clamp to the flicker grid, not
a scene-driven correction — `init_config()` precomputes
`floor(max_exposure/period)*period` and `ceil(min_exposure/period)*period` at
pipeline creation. With `"none"` that write disappears, which can look like the
loop has stopped working when it has simply found the default already inside its
deadband.

`ac_freq` is regional (50 Hz EU/CN, 60 Hz US/JP) and is baked into the tuning file.

## Per-session metering: the config pointer is live, the sum is not

The ISP measures brightness over a **5×5 grid of regions** (`ISP_AE_REGIONS`), and
`esp_ipa`'s AGC combines them using `agc.luma_adjust.weight` — a 25-entry weight
table. That is the knob for centre-weighted versus whole-scene metering.

The table is compiled into flash from the JSON, so changing it per camera session
means owning a RAM copy. Disassembling `libesp_ipa.a` (it ships as a prebuilt
static library) shows this is viable:

- `esp_ipa_pipeline_t` (a **public** struct) stores `const esp_ipa_config_t *config`
  — the pointer, not a copy.
- `cal_cur_luma_avg()` re-reads the weight table straight off that pointer **every
  frame** (`memcpy` of 25 bytes into a stack buffer), along with
  `luma_low_threshold`, `luma_low_regions`, `luma_high_threshold`,
  `luma_high_regions`. `luma_target` / `luma_low` / `luma_high` are likewise read
  live, in `cal_target_gain` and the process path.
- **The one cached value is `sum(weight)`**, computed once in `init_config()` at
  pipeline creation and stored in the algorithm's private state.

So a RAM configuration can be retuned at any time, with one hard constraint:
**every weight table swapped in must sum to the same total as the one present when
the pipeline was created.** The AGC divides the weighted luma by that cached sum;
a table summing to something else rescales measured brightness and pushes the loop
off target — silently, since nothing validates it. `board_pipeline_camera_csi.c`
enforces the equal-sum rule at runtime and refuses a mismatched table.

## Getting a RAM configuration in

`esp_video_init()` creates the pipeline itself the moment
`esp_ipa_pipeline_get_config(cam_dev->name)` hits, handing over the flash-resident
`const` config. It is created **once per boot** and there is no
`esp_video_isp_pipeline_deinit()`, so there is no second chance to replace it.

The way in is to make that lookup miss: key the tuning file to a name the sensor
does not report (here `OV02C10_SS` rather than `OV02C10`). `esp_video_init()` then
logs its warning and skips, and board code creates the pipeline itself right
afterwards:

```c
const esp_ipa_config_t *base = esp_ipa_pipeline_get_config("OV02C10_SS");
s_ipa_cfg = *base;              /* RAM copies … */
s_agc_cfg = *base->agc;
s_ipa_cfg.agc = &s_agc_cfg;     /* … that we keep writing later */
esp_video_isp_pipeline_init(&(esp_video_isp_config_t){
    .cam_dev = ESP_VIDEO_MIPI_CSI_DEVICE_NAME,
    .isp_dev = ESP_VIDEO_ISP1_DEVICE_NAME,
    .ipa_config = &s_ipa_cfg,
});
```

`esp_video_isp_pipeline_init()` is a linked global whose declaration sits in
esp_video's `private_include`. Add that directory to the consuming component's
include path rather than re-declaring the prototype, so a component bump that
changes the signature fails the build instead of mismatching silently.

The board opts in by defining `BOARD_CAMERA_IPA_CONFIG_NAME`; a board without it
compiles the whole path out and keeps stock `esp_video` behaviour. The expected
boot signature for the takeover is esp_video's "failed to get configuration to
initialize ISP controller" warning immediately followed by the board's own "ISP
pipeline running IPA …" line. The warning alone means the takeover did not happen.

## A fixed exposure and an AE loop cannot coexist

`CONFIG_BOARD_CSI_AE_TARGET` writes `V4L2_CID_EXPOSURE` once at stream start. The
AGC drives the same control every few frames, so a fixed value is overwritten
almost immediately — set it to 0 on any board running an IPA and tune exposure via
the tuning file's `agc.luma_adjust.target` instead.

## How exposure and gain actually reach this sensor

Worth recording because it constrains sensor back-ports:
`config_exposure_time()` writes `V4L2_CID_EXPOSURE_ABSOLUTE` (100 µs units), which
`esp_video_sensor.c` maps to `ESP_CAM_SENSOR_EXPOSURE_VAL` after converting µs to
lines itself using the format's `fps` and `isp_info.vts`. Gain goes out as
`V4L2_CID_GAIN` → `ESP_CAM_SENSOR_GAIN`, handled as a menu control against the
driver's gain map. The loop never uses `ESP_CAM_SENSOR_GROUP_EXP_GAIN`, so the
0.9.0 back-port's dropped `exposure_val` branch in that handler does not affect it.

`cur_format->isp_info` must be non-NULL or `esp_video_init()` skips the pipeline
regardless of everything above.

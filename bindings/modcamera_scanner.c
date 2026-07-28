/*
 * camera_scanner — MicroPython binding over the builder's QR-scan composition.
 *
 * The consumer's SOLE API to the scan pipeline (contract:
 * docs/camera-pipeline-phase2-poll-contract.md). A thin skin over the plain-C
 * cam_scanner_* surface in the camera_scanner component (which owns the pipeline +
 * overlay + scan_coordinator). Module-level functions over a single session — there
 * is only ever one camera.
 *
 * Drain-then-read each consumer loop:
 *     camera_scanner.start()
 *     while not done:
 *         while (p := camera_scanner.poll_new()) is not None:   # drain NEW ring
 *             status, pct = decoder.add_data(p)
 *             camera_scanner.report(status, pct)                # -> overlay
 *         st = camera_scanner.read_status()                     # coalesced status
 *         if st.consecutive_misses >= THRESHOLD: warn(...)
 *     camera_scanner.report_complete()
 *     camera_scanner.stop()
 *
 * read_status() returns a STRUCTURED object (attrtuple), not a positional tuple, so
 * the reserved corners field can be added later without breaking call sites (§7
 * tier 2). This is an internal system-to-system contract, hence attribute access
 * (st.latest) rather than a human-facing dict.
 */
#include <stdint.h>

#include "py/obj.h"
#include "py/objtuple.h"   // mp_obj_new_attrtuple
#include "py/runtime.h"

#include "camera_scanner.h"

// start(focus_assist=False, instructions_text=None) -> None. Raises OSError with
// a short reason on bring-up failure. focus_assist=True brings up the camera
// preview with an on-screen software focus meter (quirc skipped) instead of the QR
// scan overlay; in that mode poll_new()/read_status()/report() are inert. start()
// with no args is the normal scan session (unchanged for existing call sites).
//
// instructions_text is the Pi Zero hardware-mode overlay hint line; the ESP touch
// UI shows its own persistent gutter back button and doesn't use it. We accept the
// kwarg for cross-platform contract parity (both targets share one Python call site)
// and ignore it — declaring it here is what keeps mp_arg_parse_all from raising
// TypeError on the extra keyword.
static mp_obj_t mp_camera_scanner_start(size_t n_args, const mp_obj_t *pos_args,
                                        mp_map_t *kw_args) {
    enum { ARG_focus_assist, ARG_instructions_text };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_focus_assist, MP_ARG_BOOL, { .u_bool = false } },
        { MP_QSTR_instructions_text, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args),
                     allowed_args, args);

    const char *err = cam_scanner_start(args[ARG_focus_assist].u_bool);
    if (err) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(camera_scanner_start_obj, 0, mp_camera_scanner_start);

// stop() -> None.
static mp_obj_t mp_camera_scanner_stop(void) {
    cam_scanner_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_scanner_stop_obj, mp_camera_scanner_stop);

// is_running() -> bool.
static mp_obj_t mp_camera_scanner_is_running(void) {
    return mp_obj_new_bool(cam_scanner_is_running());
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_scanner_is_running_obj, mp_camera_scanner_is_running);

// poll_new() -> bytes | None. Drains one NEW payload from the ring, copying it into
// a fresh bytes (the C buffer is valid only until the next poll). None when empty.
static mp_obj_t mp_camera_scanner_poll_new(void) {
    const uint8_t *payload = NULL;
    size_t len = 0;
    if (!cam_scanner_poll_new(&payload, &len)) {
        return mp_const_none;
    }
    return mp_obj_new_bytes(payload, len);
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_scanner_poll_new_obj, mp_camera_scanner_poll_new);

// poll_miss_frame() -> (bytes, meta) | None. DIAGNOSTIC: drains the latest sampled
// located-but-undecoded frame (only produced during a scan on a debug build). The
// bytes are a width*height grayscale crop; meta is an attrtuple describing why it
// missed (quirc_err) and how it looked (side_px / sharpness / luma). None when no
// new miss is available. The C buffer is valid only until the next call, so the
// bytes are copied out here at once.
static mp_obj_t mp_camera_scanner_poll_miss_frame(void) {
    const uint8_t *payload = NULL;
    size_t len = 0;
    cam_scanner_miss_meta_t m;
    if (!cam_scanner_poll_miss_frame(&payload, &len, &m)) {
        return mp_const_none;
    }
    static const qstr fields[] = {
        MP_QSTR_seq, MP_QSTR_timestamp_us, MP_QSTR_quirc_err,
        MP_QSTR_side_px, MP_QSTR_sharpness, MP_QSTR_luma_mean,
        MP_QSTR_width, MP_QSTR_height,
    };
    mp_obj_t meta_items[] = {
        mp_obj_new_int_from_uint(m.seq),
        mp_obj_new_int_from_ll(m.timestamp_us),
        MP_OBJ_NEW_SMALL_INT(m.quirc_err),
        mp_obj_new_float(m.side_px),
        mp_obj_new_float(m.sharpness),
        MP_OBJ_NEW_SMALL_INT(m.luma_mean),
        mp_obj_new_int_from_uint(m.width),
        mp_obj_new_int_from_uint(m.height),
    };
    mp_obj_t ret[] = {
        mp_obj_new_bytes(payload, len),
        mp_obj_new_attrtuple(fields, MP_ARRAY_SIZE(meta_items), meta_items),
    };
    return mp_obj_new_tuple(2, ret);
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_scanner_poll_miss_frame_obj, mp_camera_scanner_poll_miss_frame);

// read_status() -> attrtuple(latest, consecutive_misses, dropped_new, has_corners).
static mp_obj_t mp_camera_scanner_read_status(void) {
    cam_scanner_status_t st;
    cam_scanner_read_status(&st);

    static const qstr fields[] = {
        MP_QSTR_latest,
        MP_QSTR_consecutive_misses,
        MP_QSTR_dropped_new,
        MP_QSTR_has_corners,
    };
    mp_obj_t items[] = {
        MP_OBJ_NEW_SMALL_INT(st.latest),
        mp_obj_new_int_from_uint(st.consecutive_misses),
        mp_obj_new_int_from_uint(st.dropped_new),
        mp_obj_new_bool(st.has_corners),
    };
    return mp_obj_new_attrtuple(fields, MP_ARRAY_SIZE(items), items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_scanner_read_status_obj, mp_camera_scanner_read_status);

// report(status, percent) -> None. status is one of FRAME_*; percent 0..100.
static mp_obj_t mp_camera_scanner_report(mp_obj_t status_obj, mp_obj_t percent_obj) {
    cam_scanner_report(mp_obj_get_int(status_obj), mp_obj_get_int(percent_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(camera_scanner_report_obj, mp_camera_scanner_report);

// report_complete() -> None.
static mp_obj_t mp_camera_scanner_report_complete(void) {
    cam_scanner_report_complete();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_scanner_report_complete_obj, mp_camera_scanner_report_complete);

// begin_segments(total_segments) -> None. Announce an INDEXED animated-QR cycle
// (BBQR/Specter) of `total_segments` pieces: switches the overlay from the continuous
// fill to per-piece cells. total_segments <= 0 is a no-op. Called ONCE, when the first
// decoded frame reveals the cycle size. The screen owns the decoded set and derives the
// percent from its own lit count (see cam_scanner_begin_segments).
static mp_obj_t mp_camera_scanner_begin_segments(mp_obj_t total_obj) {
    cam_scanner_begin_segments(mp_obj_get_int(total_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_scanner_begin_segments_obj, mp_camera_scanner_begin_segments);

// segment_event(status, piece_index) -> None. One segmented-scan decode event. status
// is one of FRAME_* (forwarded raw, like report()): FRAME_NEW lights piece_index and
// advances the derived percent; FRAME_REPEAT re-marks an already-lit piece_index (the
// real re-read index, NOT -1); FRAME_MISS updates the dot only (piece_index = -1).
static mp_obj_t mp_camera_scanner_segment_event(mp_obj_t status_obj, mp_obj_t piece_obj) {
    cam_scanner_segment_event(mp_obj_get_int(status_obj), mp_obj_get_int(piece_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(camera_scanner_segment_event_obj, mp_camera_scanner_segment_event);

// set_ae_luma_target(n) -> bool. Auto-exposure setpoint on the ISP's 0-255 luma
// scale; 0 restores the board tuning file's own value. Applies to the next metered
// frame, so brightness can be swept while the preview runs. False on a board with
// no auto-exposure loop, or for a value outside 4..251.
//
// Tuning/diagnostic surface, so it is compiled only into camera-debug builds —
// mirroring the other CAM_PIPELINE_DEBUG-only instrumentation. A user-facing
// exposure control would go through this same board_pipeline setter rather than
// widening the release binding.
#if CONFIG_CAM_PIPELINE_DEBUG
static mp_obj_t mp_camera_scanner_set_ae_luma_target(mp_obj_t target_obj) {
    mp_int_t t = mp_obj_get_int(target_obj);
    if (t < 0 || t > 255) {
        mp_raise_ValueError(MP_ERROR_TEXT("ae luma target must be 0..255"));
    }
    return mp_obj_new_bool(cam_scanner_set_ae_luma_target((uint8_t)t));
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_scanner_set_ae_luma_target_obj, mp_camera_scanner_set_ae_luma_target);

// get_ae_luma_target() -> int. 0 when no auto-exposure loop is running.
static mp_obj_t mp_camera_scanner_get_ae_luma_target(void) {
    return MP_OBJ_NEW_SMALL_INT(cam_scanner_get_ae_luma_target());
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_scanner_get_ae_luma_target_obj, mp_camera_scanner_get_ae_luma_target);

// set_tone(gamma_x10, black_level) -> bool. ISP tone curve: display gamma x10
// (22 = 2.2, 0 = leave linear) and the input level mapped to true black (0..64).
static mp_obj_t mp_camera_scanner_set_tone(mp_obj_t gamma_obj, mp_obj_t black_obj) {
    mp_int_t g = mp_obj_get_int(gamma_obj);
    mp_int_t b = mp_obj_get_int(black_obj);
    if (g < 0 || g > 255 || b < 0 || b > 255) {
        mp_raise_ValueError(MP_ERROR_TEXT("tone args out of range"));
    }
    return mp_obj_new_bool(cam_scanner_set_tone((uint8_t)g, (uint8_t)b));
}
static MP_DEFINE_CONST_FUN_OBJ_2(camera_scanner_set_tone_obj, mp_camera_scanner_set_tone);

// set_color(saturation, contrast) -> bool. ISP colour trim, 128 = neutral, 0..255.
static mp_obj_t mp_camera_scanner_set_color(mp_obj_t sat_obj, mp_obj_t con_obj) {
    mp_int_t sat = mp_obj_get_int(sat_obj);
    mp_int_t con = mp_obj_get_int(con_obj);
    if (sat < 0 || sat > 255 || con < 0 || con > 255) {
        mp_raise_ValueError(MP_ERROR_TEXT("colour args must be 0..255"));
    }
    return mp_obj_new_bool(cam_scanner_set_color((uint8_t)sat, (uint8_t)con));
}
static MP_DEFINE_CONST_FUN_OBJ_2(camera_scanner_set_color_obj, mp_camera_scanner_set_color);
#endif

static const mp_rom_map_elem_t camera_scanner_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_camera_scanner) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&camera_scanner_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&camera_scanner_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_running), MP_ROM_PTR(&camera_scanner_is_running_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll_new), MP_ROM_PTR(&camera_scanner_poll_new_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll_miss_frame), MP_ROM_PTR(&camera_scanner_poll_miss_frame_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_status), MP_ROM_PTR(&camera_scanner_read_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_report), MP_ROM_PTR(&camera_scanner_report_obj) },
    { MP_ROM_QSTR(MP_QSTR_report_complete), MP_ROM_PTR(&camera_scanner_report_complete_obj) },
    { MP_ROM_QSTR(MP_QSTR_begin_segments), MP_ROM_PTR(&camera_scanner_begin_segments_obj) },
    { MP_ROM_QSTR(MP_QSTR_segment_event), MP_ROM_PTR(&camera_scanner_segment_event_obj) },
#if CONFIG_CAM_PIPELINE_DEBUG
    { MP_ROM_QSTR(MP_QSTR_set_ae_luma_target), MP_ROM_PTR(&camera_scanner_set_ae_luma_target_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_ae_luma_target), MP_ROM_PTR(&camera_scanner_get_ae_luma_target_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_tone), MP_ROM_PTR(&camera_scanner_set_tone_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_color), MP_ROM_PTR(&camera_scanner_set_color_obj) },
#endif
    // Frame-status vocabulary (mirrors scan_coordinator / Python DecodeQRStatus).
    { MP_ROM_QSTR(MP_QSTR_FRAME_NONE), MP_ROM_INT(CAM_SCAN_FRAME_NONE) },
    { MP_ROM_QSTR(MP_QSTR_FRAME_NEW), MP_ROM_INT(CAM_SCAN_FRAME_NEW) },
    { MP_ROM_QSTR(MP_QSTR_FRAME_REPEAT), MP_ROM_INT(CAM_SCAN_FRAME_REPEAT) },
    { MP_ROM_QSTR(MP_QSTR_FRAME_MISS), MP_ROM_INT(CAM_SCAN_FRAME_MISS) },
};
static MP_DEFINE_CONST_DICT(camera_scanner_module_globals, camera_scanner_module_globals_table);

const mp_obj_module_t camera_scanner_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&camera_scanner_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_camera_scanner, camera_scanner_user_cmodule);

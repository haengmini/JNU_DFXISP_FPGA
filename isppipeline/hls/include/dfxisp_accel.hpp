#pragma once

#include <cstdint>

// =============================================================================
// DFX AI-ISP HLS C-sim top-level interface.
// Architecture: shared baseline ISP core (BLC + WB + CCM) + mutually exclusive
// tone RM slot. NORMAL keeps H x W; LOW_LIGHT outputs H/2 x W/2 (Policy A).
// Path diagrams, invariants, and interface-design rationale: dfxisp_accel.md
// (same directory).
//
// Pixel format:
//   input : RAW Bayer RGGB, 12-bit values stored in uint16_t (real-sensor
//           raw_bin conversions — PASCALRAW / Sony NOD, see data/ — or the
//           synthetic csim vectors in tests/)
//   output: packed RGB888 in uint32_t, 0x00RRGGBB
//   rgb_out capacity must be >= in_width * in_height (low-light uses <= that).
// =============================================================================

enum DfxIspMode : int {
    DFXISP_MODE_NORMAL = 0,
    DFXISP_MODE_LOW_LIGHT = 1,
    DFXISP_MODE_AUTO = 2,
};

enum DfxIspSelectedRm : int {
    DFXISP_RM_NORMAL_TONE = 0,     // gain 1.25x + gamma 2.0 tone RM (normal scenes)
    DFXISP_RM_LOW_LIGHT_TONE = 1,  // 2x2 binning-demosaic + gain 2.0x + gamma 2.0 (dark scenes)
};

// Per-frame Schmitt-band flags exported for the static-region hysteresis
// block (results/pr_controller/checker_hysteresis.v). Both clear = dark
// ratio inside the (exit, enter] band. See dfxisp_accel.md §7.
enum DfxIspHystFlag : int {
    DFXISP_HYST_ABOVE_ENTER = 1 << 0,  // dark ratio > enter threshold (62%)
    DFXISP_HYST_BELOW_EXIT = 1 << 1,   // dark ratio < exit threshold (60%)
};

// Output metadata (mutually exclusive tone RM slot), as four separate scalar
// s_axilite output pointers rather than one struct pointer -- see file header.
// Any pointer may be null (metadata write is skipped for that field).
extern "C" void dfxisp_accel(
    const uint16_t* raw_bayer,
    uint32_t* rgb_out,
    int width,
    int height,
    int mode,
    uint16_t dark_pixel_threshold,
    int* out_width,      // baseline-core / RM output width
    int* out_height,     // baseline-core / RM output height
    int* selected_mode,  // DFXISP_MODE_NORMAL or DFXISP_MODE_LOW_LIGHT (resolved AUTO)
    int* selected_rm,    // DfxIspSelectedRm
    int* hyst_flags);    // DfxIspHystFlag bits (ap_vld wire out, not s_axilite)

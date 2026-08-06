#pragma once

#include <cstdint>

// =============================================================================
// File   : isppipeline/hls/include/dfxisp_accel.hpp
// Updated: 2026-07-02 (adversarial-review fixes)
// Function: DFX AI-ISP HLS C-sim top-level interface.
// Goal   : Two corrections from an adversarial review of the branch diff
//          against the 2026-07-01 architecture reset:
//   (1) Metadata output changed from a single `DfxIspResult*` struct pointer
//       over s_axilite to four separate scalar `int*` output pointers. A
//       struct pointer over s_axilite is an unusual/unproven HLS pattern
//       (s_axilite is a slave-only control interface, not a memory-writing
//       master); the review found no artifact confirming the struct fields
//       synthesize as addressable registers, and RTL-level cosim never
//       completed far enough to confirm it either (see
//       results/stage4-hw-synthesis-2026-07-02.md §6b). Individual scalar
//       output pointers over s_axilite are the well-established Vitis HLS
//       idiom for post-completion status/readback registers.
//   (2) Low-light shape description below corrected: RM_NORMAL_TONE is no
//       longer "identity bypass" (ver1 added gain+gamma to it) and
//       RM_LOW_LIGHT_TONE's tone is gamma-2.0, not gamma-4.0 (superseded
//       still further by ver1). This header had drifted out of sync with
//       src/dfxisp_accel.cpp.
// =============================================================================
//
// Architecture: shared baseline ISP core + mutually exclusive mode-specific
// tone RMs (see RESEARCH.md, SPEC.md). The tone RM slot *wraps* the shared
// baseline core:
//
//   NORMAL:
//     raw -> demosaic (RGGB) -> baseline_isp_core (BLC + WB + CCM, no gain/gamma)
//         -> RM_NORMAL_TONE (gain 1.25x + gamma 2.0)
//         -> RGB32  (H x W)
//
//   LOW_LIGHT:
//     raw -> RM_LOW_LIGHT_TONE.front (2x2 RAW binning-demosaic, fused: this IS
//            the demosaic step for the binned grid, preserving per-channel
//            R/G/B identity -- R=top-left, G=avg(top-right,bottom-left),
//            B=bottom-right; NOT a 4-sample scalar average re-demosaiced,
//            which would collapse chroma)
//         -> baseline_isp_core (BLC + WB + CCM, no gain/gamma; same function
//            as the NORMAL path, applied to the binned RGB instead of the
//            full-res demosaic output)
//         -> RM_LOW_LIGHT_TONE.back (gain 2.0x + gamma 2.0)
//         -> RGB32  (H/2 x W/2, shape-changing Policy A)
//
// Invariants proven by the C-sim golden gates (RESEARCH.md §8.2):
//   * exactly one tone RM per frame (mutually exclusive)
//   * gain/gamma live only in the tone RMs, never duplicated in the baseline core
//   * output metadata reports mode, selected RM, and output shape
//
// Pixel format:
//   input : pseudo-RAW Bayer RGGB, 12-bit values stored in uint16_t
//   output: packed RGB888 in uint32_t, 0x00RRGGBB
//   rgb_out capacity must be >= in_width * in_height (low-light uses <= that).

enum DfxIspMode : int {
    DFXISP_MODE_NORMAL = 0,
    DFXISP_MODE_LOW_LIGHT = 1,
    DFXISP_MODE_AUTO = 2,
};

enum DfxIspSelectedRm : int {
    DFXISP_RM_NORMAL_TONE = 0,     // gain 1.25x + gamma 2.0 tone RM (normal scenes)
    DFXISP_RM_LOW_LIGHT_TONE = 1,  // 2x2 binning-demosaic + gain 2.0x + gamma 2.0 (dark scenes)
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
    int* selected_rm);   // DfxIspSelectedRm

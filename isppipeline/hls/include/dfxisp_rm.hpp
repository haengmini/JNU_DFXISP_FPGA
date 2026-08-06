#pragma once

#include <cstdint>

// DFXISP RM variant C-sim entry point.
// Bit-exact mirror of tools/rm_model.py. Computes the demosaiced RGB frame and
// applies the selected low-light reconfigurable-module variant.
//
// Variants:
//   0 STATIC   : demosaic only
//   1 REG_ONLY : per-pixel gain + lift (register path)
//   2 DFX_BIN  : 2x2 binning + gain, replicated to HxW
//   3 DFX_FP   : green-guided feature-preserving (soft-knee + gated local contrast)
//
// Input  : pseudo-RAW Bayer GRBG, 12-bit in uint16_t (H*W)
// Output : packed RGB888 0x00RRGGBB in uint32_t (H*W, shape preserved)

enum DfxIspRmVariant : int {
    DFXISP_RM_STATIC = 0,
    DFXISP_RM_REG_ONLY = 1,
    DFXISP_RM_DFX_BIN = 2,
    DFXISP_RM_DFX_FP = 3,
};

extern "C" void dfxisp_accel_variant(
    const uint16_t* raw_bayer,
    uint32_t* rgb_out,
    int width,
    int height,
    int variant);

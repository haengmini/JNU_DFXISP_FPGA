#pragma once

#include <cstdint>

// =============================================================================
// File   : isppipeline/hls/include/lowlight_isp.hpp
// Added  : 2026-08-06
// Function: lowlight_ISP -- the proposal low-light arm (v2), rebuilt from the
//           principles in results/lowlight-feature-principles-2026-07-05.md and
//           the measured levers of the 07-08..07-20 real-RAW campaigns.
//
// Relationship to the existing arms:
//   * default_isp.cpp  = the standard/reference arm (Vitis Vision ordering).
//   * lowlight_isp.cpp = THIS -- shares default_isp's correction backbone
//     (Bayer-domain black level + gain, CCM) so that the two arms differ ONLY
//     in the low-light specialisation: 2x2 binning, the 2.0x exposure gain,
//     the variance-stabilising tone curve and the edge-preserving denoise.
//     That is what makes the paper's "specialised beats general in its own
//     condition" comparison controlled.
//   * RM_LOW_LIGHT_TONE in dfxisp_accel.cpp (v1) is untouched and still the
//     deployed arm; promoting lowlight_ISP is a separate decision.
//
// Pipeline (derivations in lowlight_isp.md):
//   RAW Bayer 12-bit
//     -> (1) blackLevelCorrection   [Bayer]  subtract pedestal + restore range
//     -> (2) gain                   [Bayer]  exposure 2.0x x per-site WB, folded,
//                                            applied upstream of quantisation
//     -> (3) 2x2 binning-demosaic   [fused]  R=TL, G=(TR+BL)/2, B=BR
//     -> (4) colorcorrectionmatrix  [RGB12]  same matrix as default_isp
//     -> (5) GAT/Anscombe VST tone  [12->8]  replaces gamma 2.0; linear at the
//                                            origin so the read-noise floor is
//                                            not over-amplified
//     -> (6) edge-preserving denoise[VST 8]  sigma-clipped 3x3, CONSTANT
//                                            threshold (valid because (5)
//                                            stabilised the variance)
//     -> packed RGB888 0x00RRGGBB, H/2 x W/2 (Policy A)
//
// All arithmetic is integer and bit-exact against
// tools/gen_lowlight_isp_golden.py.
// =============================================================================

// Denoise switch, so the stage can be ablated without rebuilding the pipeline
// (the literature is consistent that over-denoising costs detection accuracy,
// so this stage must earn its place by measurement).
enum LowlightIspDenoise : int {
    LOWLIGHT_ISP_DENOISE_OFF = 0,
    LOWLIGHT_ISP_DENOISE_ON = 1,
};

// Development/analysis top: exposes the denoise switch.
//   raw_bayer : RAW Bayer RGGB, 12-bit values in uint16_t (W*H)
//   rgb_out   : packed RGB888 0x00RRGGBB; capacity >= W*H (uses W/2 * H/2)
// out_width/out_height report the binned shape (Policy A).
extern "C" void lowlight_isp(
    const uint16_t* raw_bayer,
    uint32_t* rgb_out,
    int width,
    int height,
    int denoise_mode,
    int* out_width,
    int* out_height);

// DFX Reconfigurable Module candidate. Port list is IDENTICAL (type, order,
// count) to rm_normal_tone_top / rm_low_light_tone_top / rm_default_isp_top,
// so all of them are valid implementations of the same RP slot (SPEC.md §7).
// Denoise is enabled.
extern "C" void rm_lowlight_isp_top(
    const uint16_t* raw_bayer,
    uint32_t* rgb_out,
    int width,
    int height,
    int* out_width,
    int* out_height);

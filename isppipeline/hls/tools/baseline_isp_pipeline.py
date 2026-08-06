# =============================================================================
# File   : isppipeline/hls/tools/baseline_isp_pipeline.py
# Date   : 2026-07-08
# Function: NORMAL-mode SW proxy ISP pipeline (numpy), mirroring the canonical
#           HW/C-sim NORMAL path in src/dfxisp_accel.cpp bit-for-bit-in-intent.
# =============================================================================
"""baseline_isp_pipeline: bit-exact-intent SW proxy of dfxisp_accel.cpp's NORMAL path.

Pipeline: BLC -> WB -> CCM(identity) -> gain 1.25x -> gamma 2.0 (integer sqrt LUT).

This supersedes the stale, archived pipeline files that drifted from the
canonical HW/C-sim pipeline (see isppipeline/hls/src/dfxisp_accel.cpp and
isppipeline/hls/tools/gen_golden_vectors.py, the ground truth):
  - newrm_pipeline.py (archived): its "normal" arm applied ZERO gain and ZERO
    gamma (stale pre-reset architecture) -- does not match canonical at all.
  - isp_pipeline_ver1.py (archived): BLC offsets and gain factors matched
    canonical, but it used a floating-point power-law gamma-2.2 LUT for the
    normal arm, whereas canonical uses a SINGLE shared gamma-2.0 curve
    (integer sqrt: out = floor(sqrt(255*v))) for both normal and low-light,
    realized as the 256-entry GAMMA2_LUT ROM in dfxisp_accel.cpp (~lines
    107-124) / the gamma2() function in gen_golden_vectors.py.

This file is fully self-contained (numpy only, no imports from any other
pipeline file in this repo) and covers ONLY the normal/baseline path plus the
plain "none" demosaic reference. Low-light, RAW binning, and the adaptive
dark-ratio checker live in the separate, equally self-contained
low_light_isp_pipeline.py and checker.py.

Demosaic neighbor lookup uses clamp-to-edge (not circular wrap) to match
gen_golden_vectors.py/dfxisp_accel.cpp -- this was a real bug in the archived
isp_pipeline_ver1.py this file was derived from (fixed independently in both
places: here, and in the archived copy via a parallel 2026-07-08 review).
See internal_edge_smoke.py for the regression test.

Demosaic fidelity (2026-07-09 fix): `_demosaic_rggb16` now bilinear-averages
the R/B planes (2 taps at the opposite-color G position, 4-tap diagonal
average at the opposite-color native position) to match
`gen_golden_vectors.demosaic_rggb12` / `dfxisp_accel.cpp`'s `demosaic_rggb12`
exactly -- verified bit-exact against the former on random RGGB grids (see
`tools/verify_demosaic_bilinear_cross_check.py`). Previously this used a
single nearest tap for the R/B cross-color positions (G was already correct
at 4-tap average) -- a real, independently-confirmed SW-proxy/HW mismatch
that the edge-clamp fix above did NOT cover (see internal_edge_smoke.py's own
docstring, which explicitly flagged this as a separate, then-unfixed gap).
See `results/demosaic-bilinear-fix-2026-07-09.md` for the before/after
analysis and blast-radius assessment (limited to the normal/none arms; the
low-light arm's binning-demosaic in low_light_isp_pipeline.py was already
verified bit-exact and is unaffected).
"""
from __future__ import annotations

from math import isqrt

import numpy as np

# ---- parameters (must match dfxisp_accel.cpp / gen_golden_vectors.py) ------
SHIFT = 8                              # raw16 -> 8-bit domain (>>8 = /256)
BLK_RAW = 2 << SHIFT                   # black level in RAW16 domain (BLC_OFFSET12 == 2<<4 in HW's 12-bit domain; recalibrated 2026-07-20)
AWB_R, AWB_G, AWB_B = 286, 256, 307     # Q8 per-channel white balance (color)
GAIN_NORMAL_NUM, GAIN_NORMAL_DEN = 5, 4  # normal exposure gain 1.25x

# gamma 2.0 realized exactly as integer sqrt: 255*(v/255)^(1/2) = floor(sqrt(255*v)).
# Copied verbatim (byte-for-byte, all 256 entries verified against the literal
# C++ array in dfxisp_accel.cpp, including spot-check indices 0,1,15,64,128,
# 192,255) rather than re-derived independently -- computed here via the exact
# same isqrt formula gen_golden_vectors.py uses, which reproduces the literal
# HW ROM values exactly (verified, not assumed).
GAMMA2_LUT = np.array([isqrt(255 * v) for v in range(256)], dtype=np.uint8)


def _demosaic_rggb16(bayer16, w, h):
    """RGGB bilinear demosaic, kept in the RAW16 domain (no >>8). Returns int32
    planes. Matches gen_golden_vectors.demosaic_rggb12 / dfxisp_accel.cpp's
    demosaic_rggb12 tap-for-tap: G is a 4-tap average at the R/B native
    positions; R/B are 2-tap averages at the opposite-color G position and a
    4-tap diagonal average at the opposite-color native position (2026-07-09
    fix -- previously single-nearest-tap for R/B, see module docstring)."""
    b = bayer16.astype(np.int32)
    R = np.zeros((h, w), np.int32); G = np.zeros((h, w), np.int32); B = np.zeros((h, w), np.int32)

    def avg(*a):
        return sum(a) // len(a)

    def shift(a, dy, dx):
        # Clamp-to-edge (matches gen_golden_vectors.sample_clamped / src/dfxisp_accel.cpp).
        # NOT np.roll(): a circular wrap would pull the opposite border's pixels
        # into this array's border neighbors, which the HLS/golden path never does.
        ys = np.clip(np.arange(h) + dy, 0, h - 1)
        xs = np.clip(np.arange(w) + dx, 0, w - 1)
        return a[ys[:, None], xs[None, :]]

    yy, xx = np.mgrid[0:h, 0:w]
    ey = (yy % 2 == 0); ex = (xx % 2 == 0)
    left, right = shift(b, 0, -1), shift(b, 0, 1)
    up, down = shift(b, -1, 0), shift(b, 1, 0)
    ul, ur, dl, dr = shift(b, -1, -1), shift(b, -1, 1), shift(b, 1, -1), shift(b, 1, 1)

    R[ey & ex] = b[ey & ex]
    R[ey & ~ex] = avg(left, right)[ey & ~ex]
    R[~ey & ex] = avg(up, down)[~ey & ex]
    R[~ey & ~ex] = avg(ul, ur, dl, dr)[~ey & ~ex]
    B[~ey & ~ex] = b[~ey & ~ex]
    B[~ey & ex] = avg(left, right)[~ey & ex]
    B[ey & ~ex] = avg(up, down)[ey & ~ex]
    B[ey & ex] = avg(ul, ur, dl, dr)[ey & ex]
    G[(ey & ~ex) | (~ey & ex)] = b[(ey & ~ex) | (~ey & ex)]
    gmiss = (ey & ex) | (~ey & ~ex)
    G[gmiss] = avg(left, right, up, down)[gmiss]
    return np.stack([R, G, B], -1)   # int32, RAW16 domain


def demosaic_rggb(bayer16, w, h):
    """8-bit plain demosaic for the 'none' arm (reference view, pre-BLC/WB/gain/gamma)."""
    return np.clip(_demosaic_rggb16(bayer16, w, h) >> SHIFT, 0, 255).astype(np.uint8)


def _blc_wb_gain(rgb16, gnum, gden, blk_raw=BLK_RAW):
    """RAW-domain corrections (before >>8): BLC -> WB(Q8) -> exposure gain."""
    x = np.clip(rgb16 - blk_raw, 0, None)                 # BLC (subtract first)
    x[..., 0] = x[..., 0] * AWB_R // 256                  # WB per channel (Q8)
    x[..., 1] = x[..., 1] * AWB_G // 256
    x[..., 2] = x[..., 2] * AWB_B // 256
    x = x * gnum // gden                                  # exposure gain
    return (np.clip(x >> SHIFT, 0, 255)).astype(np.uint8)  # -> 8-bit LAST (precision preserved)


def run_normal(bayer16, w, h, blc_offset=None):
    """NORMAL arm: demosaic -> BLC/WB/gain(1.25x) -> CCM(identity) -> gamma 2.0. -> H x W x 3 uint8.

    blc_offset: optional override for the BLC constant, in the same units as
    the module constant BLK_RAW is derived from (i.e. an 8-bit-equivalent
    black-level value that gets shifted into the RAW16 domain via <<SHIFT).
    When None (default), the existing module constant BLK_RAW is used
    unchanged -- this is a purely additive, backward-compatible parameter
    for the BLC recalibration ablation.
    """
    blk_raw = BLK_RAW if blc_offset is None else (int(blc_offset) << SHIFT)
    rgb8 = _blc_wb_gain(_demosaic_rggb16(bayer16, w, h), GAIN_NORMAL_NUM, GAIN_NORMAL_DEN, blk_raw=blk_raw)
    return GAMMA2_LUT[rgb8]


def run_arm(bayer16, w, h, arm, blc_offset=None):
    if arm == "none":
        return demosaic_rggb(bayer16, w, h)
    if arm == "normal":
        return run_normal(bayer16, w, h, blc_offset=blc_offset)
    raise ValueError(arm)

# =============================================================================
# File   : isppipeline/hls/tools/checker.py
# Date   : 2026-07-08
# Function: Adaptive-mode dark-ratio scene checker, decoupled from both the
#           normal and low-light SW proxy pipelines.
# =============================================================================
"""checker: dark-ratio scene checker matching checker_select_mode in
src/dfxisp_accel.cpp. Deployed rule is C1 since 2026-07-20 (gate 4,
checker-status-2026-07-10.md §1/§4): dark16 ratio > 0.62, i.e. fraction of
RAW pixels strictly below 16 (8-bit terms; 16<<8 in this pseudo-RAW16 domain,
16<<4 = 256 in the HW's raw12 `dark_pixel_threshold` register) strictly
greater than DARK_RATIO_PCT=62.

C1 fidelity note (2026-07-20): `selected_mode` now thresholds RAW Bayer
pixels directly, exactly like the HLS `checker_select_mode` (raw <
dark_pixel_threshold) and exactly like the domain the C1 operating point was
calibrated in (checker_stat_sweep dark16 / analyze_adaptive_tau_*
`dark_ratio_at`). The previous C0-era implementation approximated the HW by
computing luminance < DARK_Y on a demosaiced view -- that approximation is
gone; the demosaic helpers below remain only for the cross-check scripts and
the "none" reference view.

This is the decoupled checker used by an eval harness to decide which of
baseline_isp_pipeline.run_normal / low_light_isp_pipeline.run_lowlight to
call per frame. The "adaptive" arm is not internal to any one pipeline file:
it is the composition of this checker plus both pipelines, performed by the
caller.

Fully self-contained (numpy only): carries its own tiny bilinear RGGB
demosaic helper (pre-BLC/WB/gain/gamma, the same "none"/checker view used by
canonical) rather than importing it from baseline_isp_pipeline.py, to keep
all three files fully independent per explicit instruction.

Demosaic fidelity (2026-07-09 fix): `_demosaic_rggb16` now bilinear-averages
the R/B planes (2 taps at the opposite-color G position, 4-tap diagonal
average at the opposite-color native position) to match
`gen_golden_vectors.demosaic_rggb12` / `src/dfxisp_accel.cpp`'s
`demosaic_rggb12` exactly -- verified bit-exact against the former on random
RGGB grids (see `tools/verify_demosaic_bilinear_cross_check.py`). Previously
this used a single nearest tap for the R/B cross-color positions (G was
already correct at 4-tap average); see
`results/demosaic-bilinear-fix-2026-07-09.md` for the before/after analysis
and blast-radius assessment.
"""
from __future__ import annotations

import numpy as np

# ---- parameters (must match dfxisp_accel.cpp checker_select_mode) ---------
SHIFT = 8               # raw16 -> 8-bit domain (>>8 = /256)
DARK_T8 = 16            # C1 dark-pixel threshold, 8-bit terms (HW raw12 register = 16<<4 = 256)
DARK_RAW = DARK_T8 << SHIFT   # same threshold in this pseudo-RAW16 domain
DARK_RATIO_PCT = 62     # AUTO -> LOW_LIGHT when dark pixels > 62% (DARK_RATIO_PCT in dfxisp_accel.cpp)
DARK_RATIO = DARK_RATIO_PCT / 100.0


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
    """8-bit plain demosaic, pre-BLC/WB/gain/gamma -- the checker's view of the frame."""
    return np.clip(_demosaic_rggb16(bayer16, w, h) >> SHIFT, 0, 255).astype(np.uint8)


def dark_ratio(bayer16):
    """Fraction of RAW Bayer pixels strictly below DARK_RAW -- same per-pixel
    compare as the HLS checker (raw < dark_pixel_threshold) and the dark16
    statistic the C1 threshold was calibrated on."""
    return float(np.mean(np.asarray(bayer16) < DARK_RAW))


def selected_mode(bayer16, w, h):
    """Return "lowlight" or "normal" per the deployed C1 dark-ratio checker."""
    del w, h  # kept for signature compatibility; the RAW compare needs no geometry
    return "lowlight" if dark_ratio(bayer16) > DARK_RATIO else "normal"

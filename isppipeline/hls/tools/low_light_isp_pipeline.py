# =============================================================================
# File   : isppipeline/hls/tools/low_light_isp_pipeline.py
# Date   : 2026-07-08
# Function: LOW-LIGHT-mode SW proxy ISP pipeline (numpy), the proposed structure
#           mirroring the canonical HW/C-sim low-light path in
#           src/dfxisp_accel.cpp bit-for-bit-in-intent.
# =============================================================================
"""low_light_isp_pipeline: SW proxy of dfxisp_accel.cpp's LOW_LIGHT path.

Pipeline: 2x2 RAW binning-demosaic -> BLC -> WB -> gain 2.0x -> CCM(identity)
-> gamma 2.0 (the SAME shared GAMMA2_LUT as the normal/baseline path, per
canonical -- NOT the stale gamma-2.5 / gamma-4.0 curves used by earlier,
now-archived pipeline files).

Fully self-contained (numpy only): duplicates whatever constants/helpers it
needs rather than importing from baseline_isp_pipeline.py or any archived
file, per explicit instruction to keep these pipeline files independent.

Ground truth: isppipeline/hls/src/dfxisp_accel.cpp (BLC_OFFSET12_LOWLIGHT,
GAIN_LOWLIGHT_NUM/DEN, GAMMA2_LUT) and gen_golden_vectors.py (gamma2()).
"""
from __future__ import annotations

from math import isqrt

import numpy as np

# ---- parameters (must match dfxisp_accel.cpp / gen_golden_vectors.py) ------
SHIFT = 8                              # raw16 -> 8-bit domain (>>8 = /256)

# BLC_OFFSET_LOWLIGHT: recalibrated to 2 (2026-07-20, approved; 32 in the
# HW's 12-bit domain: BLC_OFFSET12_LOWLIGHT = 2 << 4). The old warning that
# "optimal 2" came only from a wrong-gamma sweep no longer applies: the value
# was re-measured on THIS canonical gamma-2.0 pipeline with real-sensor RAW
# (results/isp-pipeline-recalibration-2026-07-08.md, SonyNOD 321;
# results/lod-pascal-isp-simulation-2026-07-15.md, LOD/PASCAL/Shuffle splits)
# and BLC 1~2 is the measured mAP peak for the low-light arm on both.
BLC_OFFSET_LOWLIGHT = 2 << SHIFT

AWB_R, AWB_G, AWB_B = 286, 256, 307     # Q8 per-channel white balance (color), same as canonical
GAIN_LOWLIGHT_NUM, GAIN_LOWLIGHT_DEN = 2, 1  # low-light exposure gain 2.0x

# gamma 2.0 realized exactly as integer sqrt: 255*(v/255)^(1/2) = floor(sqrt(255*v)).
# This is the SAME shared curve as the normal/baseline path in canonical HW
# (single GAMMA2_LUT ROM used by both modes) -- duplicated here (own copy) to
# keep this file independent, computed via the exact isqrt formula verified
# (spot-checked at 0,1,15,64,128,192,255, and exhaustively at all 256 indices)
# to reproduce the literal C++ GAMMA2_LUT array byte-for-byte.
GAMMA2_LUT = np.array([isqrt(255 * v) for v in range(256)], dtype=np.uint8)


def _bin_demosaic_rggb16(bayer16, w, h):
    """2x2 RAW binning-demosaic, one RGB per RGGB cell (chroma-preserving,
    fused version -- NOT the old buggy "average all 4 into one scalar"
    version): R = top-left, G = avg(top-right, bottom-left), B = bottom-right.
    Returns (H/2, W/2, 3) int32 in the RAW16 domain (no >>SHIFT yet).
    """
    b = bayer16.astype(np.int32)
    h2, w2 = h - (h % 2), w - (w % 2)
    b = b[:h2, :w2]
    R = b[0::2, 0::2]
    G = (b[0::2, 1::2] + b[1::2, 0::2]) // 2
    B = b[1::2, 1::2]
    return np.stack([R, G, B], -1)


def _blc_wb_gain(rgb16, gnum, gden, blk_raw=BLC_OFFSET_LOWLIGHT, wb=None):
    """RAW-domain corrections (before >>8): BLC -> WB(Q8) -> exposure gain."""
    wb_r, wb_g, wb_b = (AWB_R, AWB_G, AWB_B) if wb is None else wb
    x = np.clip(rgb16 - blk_raw, 0, None)                 # BLC (subtract first)
    x[..., 0] = x[..., 0] * wb_r // 256                   # WB per channel (Q8)
    x[..., 1] = x[..., 1] * wb_g // 256
    x[..., 2] = x[..., 2] * wb_b // 256
    x = x * gnum // gden                                  # exposure gain
    return (np.clip(x >> SHIFT, 0, 255)).astype(np.uint8)  # -> 8-bit LAST (precision preserved)


def run_lowlight(bayer16, w, h, blc_offset=None, wb=None):
    """LOW_LIGHT arm: 2x2 RAW bin-demosaic -> BLC/WB/gain(2.0x) -> CCM(identity)
    -> gamma 2.0. -> (H/2) x (W/2) x 3 uint8.

    blc_offset: optional override for the BLC constant (8-bit-equivalent
    black-level value, shifted into the RAW16 domain via <<SHIFT). When None
    (default), the existing module constant BLC_OFFSET_LOWLIGHT is used
    unchanged -- purely additive, backward-compatible parameter for the BLC
    recalibration ablation.

    wb: optional (R,G,B) Q8 white-balance gain override. When None (default)
    the shared module constants AWB_R/G/B are used, so existing callers are
    bit-identical. Added for the mode-specific WB recalibration: the deployed
    shared gains are a bright-scene calibration (measured 2026-08-03: they sit
    at 0.92x of PASCAL's gray-world optimum but only 0.50x of SonyNOD's, i.e.
    low-light gets half the blue correction it needs).
    """
    blk_raw = BLC_OFFSET_LOWLIGHT if blc_offset is None else (int(blc_offset) << SHIFT)
    rgb8 = _blc_wb_gain(_bin_demosaic_rggb16(bayer16, w, h), GAIN_LOWLIGHT_NUM, GAIN_LOWLIGHT_DEN,
                        blk_raw=blk_raw, wb=wb)
    return GAMMA2_LUT[rgb8]


def run_arm(bayer16, w, h, arm, blc_offset=None, wb=None):
    if arm == "lowlight":
        return run_lowlight(bayer16, w, h, blc_offset=blc_offset, wb=wb)
    raise ValueError(arm)

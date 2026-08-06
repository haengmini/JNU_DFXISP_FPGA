#!/usr/bin/env python3
# =============================================================================
# File   : isppipeline/hls/tools/gen_golden_vectors.py
# Updated: 2026-07-02 (adversarial-review fix)
# Function: deterministic DFXISP HLS C-sim golden vectors (bit-exact mirror of
#           src/dfxisp_accel.cpp)
# Goal   : Fix a chroma-collapse bug found by an adversarial review: the
#           low-light path used to average all 4 RGGB samples in a 2x2 cell
#           into ONE scalar, then re-demosaic that scalar grid as if it were
#           still Bayer-patterned -- since the grid no longer had real Bayer
#           structure, this just spread near-identical values around,
#           destroying chroma before AWB/gain ever ran. Because this file WAS
#           the golden model, `make verify`'s bit-exact test matched the bug
#           perfectly and could never catch it. Worse, the SW mAP evidence
#           (tools/isp_pipeline_ver1.py `_bin_demosaic_rggb16`) used a
#           different, chroma-preserving algorithm, so the reported low-light
#           mAP was never evidence for what this file/the HLS computed.
#           Fixed: `bin_demosaic_rggb12()` now performs a fused 2x2
#           binning+demosaic in one step (R=top-left, G=avg(top-right,
#           bottom-left), B=bottom-right) -- bit-exact match to
#           `_bin_demosaic_rggb16` -- then feeds the shared `apply_blc_wb12()`
#           core directly (no second Bayer-assuming demosaic pass).
# =============================================================================
"""Generate deterministic DFXISP HLS C-sim golden vectors.

Bit-exact mirror of src/dfxisp_accel.cpp: shared baseline ISP core + mutually
exclusive tone RM slot. tone RM wraps the core; gain/gamma live only in the tone
RMs (no duplication).

  NORMAL:     raw -> demosaic(RGGB) -> apply_blc_wb12 -> RM_NORMAL_TONE
                     (gain 1.25x + gamma2)                          (H x W)
  LOW_LIGHT:  raw -> 2x2 RAW binning-demosaic (fused) -> apply_blc_wb12
                     -> RM_LOW_LIGHT_TONE(gain 2.0x + gamma2)  (H/2 x W/2, Policy A)

apply_blc_wb12 = BLC + WB + CCM(identity), 12-bit, no gain/gamma -- shared between
both paths, consuming an already-demosaiced R,G,B triple either way.

Standard library only. CSV carries per-case metadata (mode, selected RM, output
shape) plus input RAW rows (kind=raw) and expected output rows (kind=rgb).
"""

from __future__ import annotations

import argparse
import csv
from math import isqrt
from pathlib import Path

DFXISP_MODE_NORMAL = 0
DFXISP_MODE_LOW_LIGHT = 1
DFXISP_MODE_AUTO = 2

DFXISP_RM_NORMAL_TONE = 0
DFXISP_RM_LOW_LIGHT_TONE = 1

# shared baseline-core params (12-bit RAW domain)
# BLC recalibration (2026-07-20, approved): both modes share black level 2 --
# real-sensor sweeps on the canonical pipeline put the mAP peak at BLC 1~2
# (results/isp-pipeline-recalibration-2026-07-08.md,
# results/lod-pascal-isp-simulation-2026-07-15.md). Must match dfxisp_accel.cpp.
BLC_OFFSET12 = 2 << 4            # black level 2 (8-bit) -> 32 (12-bit), normal mode
BLC_OFFSET12_LOWLIGHT = 2 << 4   # black level 2 (8-bit) -> 32 (12-bit)
RAW12_MAX = 4095
AWB_R, AWB_G, AWB_B = 286, 256, 307
# tone RM params
GAIN_NORMAL_NUM, GAIN_NORMAL_DEN = 5, 4        # normal 1.25x
GAIN_LOWLIGHT_NUM, GAIN_LOWLIGHT_DEN = 2, 1    # low-light 2.0x
# Recalibrated 2026-07-02 from measured dataset separation (Youden's J sweep):
# old 40% gave ExDark recall=1.00 but COCO false-trigger=0.80; 80% gives
# recall=0.90, false-trigger=0.11 (near-optimal J=0.79).
DARK_RATIO_PCT = 62                            # AUTO -> LOW_LIGHT when dark pixels > 62% (C1, deployed 2026-07-20)


def clamp(v: int, lo: int, hi: int) -> int:
    return lo if v < lo else hi if v > hi else v


def clamp_u8(v: int) -> int:
    return clamp(v, 0, 255)


def pack_rgb(r: int, g: int, b: int) -> int:
    return (r << 16) | (g << 8) | b


def gamma2(v: int) -> int:
    # gamma 2.0: out = floor(sqrt(255 * v)); isqrt is exact & matches C++ isqrt_u64
    return clamp_u8(isqrt(255 * v))


def sample_clamped(raw: list[int], w: int, h: int, x: int, y: int) -> int:
    x = 0 if x < 0 else w - 1 if x >= w else x
    y = 0 if y < 0 else h - 1 if y >= h else y
    return raw[y * w + x]


def demosaic_rggb12(raw: list[int], w: int, h: int, x: int, y: int) -> tuple[int, int, int]:
    # RGGB Bayer: (0,0)=R (0,1)=G (1,0)=G (1,1)=B ; keep 12-bit (no >>4 here)
    win = [[sample_clamped(raw, w, h, x + wx - 1, y + wy - 1) for wx in range(3)] for wy in range(3)]
    ey, ex, c = (y & 1) == 0, (x & 1) == 0, win[1][1]
    if ey and ex:                    # R
        rr = c; gg = (win[1][0] + win[1][2] + win[0][1] + win[2][1]) // 4
        bb = (win[0][0] + win[0][2] + win[2][0] + win[2][2]) // 4
    elif ey and not ex:              # G on R row (R horizontal, B vertical)
        gg = c; rr = (win[1][0] + win[1][2]) // 2; bb = (win[0][1] + win[2][1]) // 2
    elif (not ey) and ex:            # G on B row (R vertical, B horizontal)
        gg = c; rr = (win[0][1] + win[2][1]) // 2; bb = (win[1][0] + win[1][2]) // 2
    else:                            # B
        bb = c; gg = (win[1][0] + win[1][2] + win[0][1] + win[2][1]) // 4
        rr = (win[0][0] + win[0][2] + win[2][0] + win[2][2]) // 4
    return min(rr, RAW12_MAX), min(gg, RAW12_MAX), min(bb, RAW12_MAX)


def apply_blc_wb12(dr: int, dg: int, db: int, blc_offset: int = BLC_OFFSET12) -> tuple[int, int, int]:
    """Shared baseline ISP core (ver1): BLC + WB + CCM in 12-bit. No gain/gamma.
    Consumes an already-demosaiced R,G,B triple (full Bayer demosaic for normal,
    or the low-light binning-demosaic -- see run_frame LOW_LIGHT branch). WB/CCM
    are identical between modes; `blc_offset` is the one mode-specific parameter
    (BLC_OFFSET12_LOWLIGHT for low-light, see comment above)."""
    dr = clamp(dr - blc_offset, 0, RAW12_MAX)            # BLC (subtract first)
    dg = clamp(dg - blc_offset, 0, RAW12_MAX)
    db = clamp(db - blc_offset, 0, RAW12_MAX)
    r = clamp(dr * AWB_R // 256, 0, RAW12_MAX)           # WB per channel (Q8)
    g = clamp(dg * AWB_G // 256, 0, RAW12_MAX)
    b = clamp(db * AWB_B // 256, 0, RAW12_MAX)           # CCM identity
    return r, g, b


def baseline_core12(raw: list[int], w: int, h: int, x: int, y: int) -> tuple[int, int, int]:
    """Normal-path baseline core: full Bayer demosaic -> apply_blc_wb12."""
    dr, dg, db = demosaic_rggb12(raw, w, h, x, y)
    return apply_blc_wb12(dr, dg, db, BLC_OFFSET12)


def bin_demosaic_rggb12(raw: list[int], w: int, h: int, bx: int, by: int) -> tuple[int, int, int]:
    """2x2 RAW binning-demosaic, fused (RESEARCH §4.2): one RGGB cell -> one R,G,B
    triple (R=top-left, G=avg(top-right,bottom-left), B=bottom-right). This IS the
    demosaic step for the binned grid -- bit-exact match to
    tools/isp_pipeline_ver1.py's `_bin_demosaic_rggb16`. Chroma-preserving (fixes
    the earlier scalar-average-then-redemosaic bug that collapsed color)."""
    x0, x1 = 2 * bx, min(2 * bx + 1, w - 1)
    y0, y1 = 2 * by, min(2 * by + 1, h - 1)
    p00 = raw[y0 * w + x0]  # R (top-left)
    p01 = raw[y0 * w + x1]  # G (top-right)
    p10 = raw[y1 * w + x0]  # G (bottom-left)
    p11 = raw[y1 * w + x1]  # B (bottom-right)
    return p00, (p01 + p10) // 2, p11


def tone(v12: int, gnum: int, gden: int) -> int:
    """tone RM: exposure gain (12-bit) -> >>4 to 8-bit -> gamma 2.0."""
    gained = clamp(v12 * gnum // gden, 0, RAW12_MAX)
    return gamma2(clamp_u8(gained >> 4))


def bin_dim(d: int) -> int:
    return max(1, d // 2)


def checker_select_mode(raw: list[int], w: int, h: int, mode: int, dark_threshold: int) -> int:
    if mode == DFXISP_MODE_NORMAL:
        return DFXISP_MODE_NORMAL
    if mode == DFXISP_MODE_LOW_LIGHT:
        return DFXISP_MODE_LOW_LIGHT
    dark = sum(1 for v in raw if v < dark_threshold)
    return DFXISP_MODE_LOW_LIGHT if dark * 100 > DARK_RATIO_PCT * (w * h) else DFXISP_MODE_NORMAL


def run_frame(raw: list[int], w: int, h: int, mode: int, dark_threshold: int):
    """Return (selected_mode, selected_rm, out_w, out_h, packed_rgb_list)."""
    selected = checker_select_mode(raw, w, h, mode, dark_threshold)

    if selected == DFXISP_MODE_NORMAL:
        out = []
        for y in range(h):
            for x in range(w):
                r12, g12, b12 = baseline_core12(raw, w, h, x, y)     # core (no gain/gamma)
                out.append(pack_rgb(tone(r12, GAIN_NORMAL_NUM, GAIN_NORMAL_DEN),
                                    tone(g12, GAIN_NORMAL_NUM, GAIN_NORMAL_DEN),
                                    tone(b12, GAIN_NORMAL_NUM, GAIN_NORMAL_DEN)))
        return selected, DFXISP_RM_NORMAL_TONE, w, h, out

    # LOW_LIGHT: 2x2 RAW binning-demosaic (front, fused) -> baseline core (BLC+WB,
    # no second demosaic) -> gain 2.0x + gamma (back)
    bw, bh = bin_dim(w), bin_dim(h)
    out = []
    for y in range(bh):
        for x in range(bw):
            dr, dg, db = bin_demosaic_rggb12(raw, w, h, x, y)
            r12, g12, b12 = apply_blc_wb12(dr, dg, db, BLC_OFFSET12_LOWLIGHT)
            out.append(pack_rgb(tone(r12, GAIN_LOWLIGHT_NUM, GAIN_LOWLIGHT_DEN),
                                tone(g12, GAIN_LOWLIGHT_NUM, GAIN_LOWLIGHT_DEN),
                                tone(b12, GAIN_LOWLIGHT_NUM, GAIN_LOWLIGHT_DEN)))
    return selected, DFXISP_RM_LOW_LIGHT_TONE, bw, bh, out


# --------------------------------------------------------------------------
# Deterministic fixtures (RESEARCH.md §10.1 / §12 Task 3).
# --------------------------------------------------------------------------
def grid_raw(w: int, h: int, levels: list[int], cell: int = 2, texture: int = 32) -> list[int]:
    raw: list[int] = []
    cells_x = max((w + cell - 1) // cell, 1)
    for y in range(h):
        for x in range(w):
            base = levels[((y // cell) * cells_x + (x // cell)) % len(levels)]
            ripple = (x % cell) * texture + (y % cell) * (texture // 2)
            bayer_offset = 18 if ((x + y) & 1) else -10
            raw.append(max(0, min(4095, base + ripple + bayer_offset)))
    return raw


def boundary_ratio_raw(w: int, h: int, dark_count: int, dark_val: int = 200,
                       bright_val: int = 3000) -> list[int]:
    """Exactly dark_count pixels below a typical dark_pixel_threshold, rest bright.
    Used to test the DARK_RATIO_PCT boundary precisely (checker counts raw<threshold
    irrespective of position, so exact per-pixel layout does not matter here)."""
    n = w * h
    dark_count = max(0, min(n, dark_count))
    return [dark_val] * dark_count + [bright_val] * (n - dark_count)


def golden_cases():
    # bright normal x3 -> dark low-light x3 -> bright recovery x1, + threshold + odd-dim.
    return [
        ("seq1_bright_normal_grid_8x8", 8, 8, DFXISP_MODE_NORMAL, 512,
         grid_raw(8, 8, [1800, 2300, 2800, 3300, 3800, 3050, 2450, 3600])),
        ("seq2_bright_normal_grid_8x8", 8, 8, DFXISP_MODE_NORMAL, 512,
         grid_raw(8, 8, [2100, 2550, 3000, 3450, 3900, 3250, 2700, 3650], texture=28)),
        ("seq3_mixed_normal_grid_16x16", 16, 16, DFXISP_MODE_NORMAL, 1400,
         grid_raw(16, 16, [1450, 1800, 2200, 2600, 3050, 3400, 3750, 2450], cell=4, texture=36)),
        ("seq4_dark_lowlight_grid_8x8", 8, 8, DFXISP_MODE_LOW_LIGHT, 512,
         grid_raw(8, 8, [180, 260, 380, 520, 700, 920, 620, 300], texture=24)),
        ("seq5_dark_lowlight_grid_8x8", 8, 8, DFXISP_MODE_LOW_LIGHT, 512,
         grid_raw(8, 8, [240, 360, 500, 680, 860, 1040, 720, 420], texture=26)),
        ("seq6_mixed_dark_lowlight_grid_16x16", 16, 16, DFXISP_MODE_LOW_LIGHT, 1400,
         grid_raw(16, 16, [220, 420, 760, 1180, 540, 980, 1320, 360], cell=4, texture=38)),
        ("seq7_bright_recovery_auto_8x8", 8, 8, DFXISP_MODE_AUTO, 512,
         grid_raw(8, 8, [1800, 2300, 2800, 3300, 3800, 3050, 2450, 3600])),
        ("auto_dark_trigger_8x8", 8, 8, DFXISP_MODE_AUTO, 512,
         grid_raw(8, 8, [120, 180, 240, 300, 360, 300, 240, 180], texture=16)),
        ("odd_dimension_lowlight_7x5", 7, 5, DFXISP_MODE_LOW_LIGHT, 512,
         grid_raw(7, 5, [200, 320, 480, 660, 900, 620, 380, 260], texture=22)),
        # DARK_RATIO_PCT boundary regression (C1 deployment 80% -> 62%, 2026-07-20):
        # 60.9% dark must stay NORMAL, 62.5% dark must trigger LOW_LIGHT
        # (dark*100 > 62*64 needs dark >= 40).
        ("auto_boundary_ratio_61_8x8", 8, 8, DFXISP_MODE_AUTO, 512,
         boundary_ratio_raw(8, 8, dark_count=39)),   # 39/64 = 60.9% -> NOT > 62% -> NORMAL
        ("auto_boundary_ratio_62p5_8x8", 8, 8, DFXISP_MODE_AUTO, 512,
         boundary_ratio_raw(8, 8, dark_count=40)),   # 40/64 = 62.5% -> > 62% -> LOW_LIGHT
        # RAW-domain boundary regression (adversarial review, 2026-07-04): the dark16
        # threshold is 16<<4 = 256 in the HLS raw12 domain (NOT the dataset-domain
        # 16<<8 = 4096, which would mark every 12-bit pixel dark). Pixels at exactly
        # 256 are not dark (strict < compare) -> NORMAL; one LSB below -> LOW_LIGHT.
        ("auto_raw12_thr256_at_threshold_8x8", 8, 8, DFXISP_MODE_AUTO, 256,
         [256] * 64),   # raw == threshold -> 0% dark -> NORMAL
        ("auto_raw12_thr256_below_threshold_8x8", 8, 8, DFXISP_MODE_AUTO, 256,
         [255] * 64),   # raw < threshold everywhere -> 100% dark -> LOW_LIGHT
    ]


def write_csv(path: Path) -> int:
    rows = 0
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["case", "in_w", "in_h", "mode", "threshold",
                         "out_w", "out_h", "sel_mode", "sel_rm", "kind", "idx", "val"])
        for name, w, h, mode, threshold, raw in golden_cases():
            assert len(raw) == w * h, name
            sel_mode, sel_rm, ow, oh, rgb = run_frame(raw, w, h, mode, threshold)
            meta = [name, w, h, mode, threshold, ow, oh, sel_mode, sel_rm]
            for idx, v in enumerate(raw):
                writer.writerow(meta + ["raw", idx, v]); rows += 1
            for idx, v in enumerate(rgb):
                writer.writerow(meta + ["rgb", idx, f"0x{v:06x}"]); rows += 1
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="tests/golden_vectors.csv",
                        help="output CSV path (default: tests/golden_vectors.csv)")
    args = parser.parse_args()
    out = Path(args.out)
    rows = write_csv(out)
    print(f"wrote {out} ({rows + 1} rows including header; {rows} data rows; "
          f"{len(golden_cases())} cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

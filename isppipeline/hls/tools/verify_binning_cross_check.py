# =============================================================================
# File   : isppipeline/hls/tools/verify_binning_cross_check.py
# Date   : 2026-07-03 (repointed 2026-07-08 at low_light_isp_pipeline.py)
# Time   : 01:00 KST
# Function: Independent cross-check gate (improvement-strategy-2026-07-03.md
#           Phase 0.1) -- fuzzes tools/gen_golden_vectors.py's
#           bin_demosaic_rggb12 (the function whose earlier scalar-average bug
#           the adversarial review caught) against
#           tools/low_light_isp_pipeline.py's _bin_demosaic_rggb16 (an
#           independently-authored, canonical-gain/gamma-matched implementation
#           of the SAME algorithm -- this file used to cross-check against the
#           now-archived isp_pipeline_ver1.py, which had the same
#           _bin_demosaic_rggb16 function; only the import target changed, the
#           algorithm/semantics are identical) on many random RGGB grids. This
#           is the CI gate that "make verify" was missing: the original
#           color-collapse bug survived because the golden CSV generator and
#           the C++ implementation mirrored the SAME mistake -- a single
#           self-consistent pair can never catch that. A THIRD,
#           independently-written reference closes that gap.
# Goal   : exit 1 on any mismatch so this can be wired into `make verify`.
# =============================================================================
"""Usage: python3 tools/verify_binning_cross_check.py [--cases N] [--seed S]"""
from __future__ import annotations

import argparse
import random
import sys

import gen_golden_vectors as G
import low_light_isp_pipeline as P
import numpy as np


def random_grid(rng: random.Random, w: int, h: int, bits: int) -> list[int]:
    vmax = (1 << bits) - 1
    return [rng.randint(0, vmax) for _ in range(w * h)]


def check_one(rng: random.Random, w: int, h: int, bits: int) -> tuple[bool, str]:
    raw = random_grid(rng, w, h, bits)
    bayer_np = np.array(raw, dtype=np.int64).reshape(h, w)

    # low_light_isp_pipeline's _bin_demosaic_rggb16 is vectorized over the whole
    # grid at once -- compute it once per grid, not per cell.
    sw_result = P._bin_demosaic_rggb16(bayer_np, w, h)  # (bh, bw, 3) int32

    bh, bw = h // 2, w // 2
    for by in range(bh):
        for bx in range(bw):
            golden_r, golden_g, golden_b = G.bin_demosaic_rggb12(raw, w, h, bx, by)
            sw_r, sw_g, sw_b = (int(sw_result[by, bx, 0]),
                                int(sw_result[by, bx, 1]),
                                int(sw_result[by, bx, 2]))
            if (golden_r, golden_g, golden_b) != (sw_r, sw_g, sw_b):
                return False, (
                    f"MISMATCH at grid {w}x{h} cell (bx={bx},by={by}): "
                    f"golden(gen_golden_vectors.bin_demosaic_rggb12)=({golden_r},{golden_g},{golden_b}) "
                    f"!= sw(low_light_isp_pipeline._bin_demosaic_rggb16)=({sw_r},{sw_g},{sw_b})"
                )
    return True, ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cases", type=int, default=500, help="number of random grids to test")
    ap.add_argument("--seed", type=int, default=20260703, help="RNG seed (deterministic by default)")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    failures = 0
    tested_cells = 0
    # Mix of grid sizes: minimum (2x2), typical, odd dimensions (exercises the
    # w-1/h-1 edge clamp in both implementations identically).
    size_pool = [(2, 2), (4, 4), (8, 6), (16, 16), (17, 17), (33, 9), (64, 48)]

    for i in range(args.cases):
        w, h = size_pool[i % len(size_pool)]
        ok, msg = check_one(rng, w, h, bits=16)
        tested_cells += (w // 2) * (h // 2)
        if not ok:
            print(f"[verify_binning_cross_check] FAIL case {i}: {msg}")
            failures += 1
            if failures >= 10:
                print("[verify_binning_cross_check] stopping after 10 failures")
                break

    if failures:
        print(f"[verify_binning_cross_check] FAILED: {failures} mismatching grid(s)")
        return 1

    print(f"[verify_binning_cross_check] PASS: {args.cases} random grids, "
          f"{tested_cells} binned cells, gen_golden_vectors.bin_demosaic_rggb12 "
          f"== low_light_isp_pipeline._bin_demosaic_rggb16 (independent implementations, bit-exact)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

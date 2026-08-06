# =============================================================================
# File   : isppipeline/hls/tools/verify_demosaic_bilinear_cross_check.py
# Date   : 2026-07-09
# Function: Independent cross-check gate for the 2026-07-09 demosaic-bilinear
#           fix (see results/demosaic-bilinear-fix-2026-07-09.md) -- fuzzes
#           tools/gen_golden_vectors.py's demosaic_rggb12 (the bit-exact
#           golden reference, itself verified against src/dfxisp_accel.cpp
#           via `make verify`) against the vectorized _demosaic_rggb16 in
#           BOTH baseline_isp_pipeline.py and checker.py (two independent
#           copies, per those files' explicit "kept deliberately un-shared"
#           design) on many random RGGB grids.
#
#           Same rationale as verify_binning_cross_check.py: a proxy file
#           checking itself before/after a change (internal_edge_smoke.py's
#           boundary regression) can never catch a tap-count mismatch against
#           the true reference -- only a cross-check against the
#           independently-written golden model can. This is exactly the gap
#           internal_edge_smoke.py's own docstring flagged as out of scope
#           ("the proxy files use single-nearest-tap for cross-color
#           positions ... a separate, larger fidelity gap").
# Goal   : exit 1 on any mismatch so this can be wired into `make verify`.
# =============================================================================
"""Usage: python3 tools/verify_demosaic_bilinear_cross_check.py [--cases N] [--seed S]"""
from __future__ import annotations

import argparse
import random

import baseline_isp_pipeline as BP
import checker as CK
import gen_golden_vectors as G
import numpy as np


def random_grid(rng: random.Random, w: int, h: int, bits: int) -> list[int]:
    vmax = (1 << bits) - 1
    return [rng.randint(0, vmax) for _ in range(w * h)]


def check_one(rng: random.Random, w: int, h: int, bits: int = 12) -> tuple[bool, str]:
    # gen_golden_vectors.demosaic_rggb12 expects genuine 12-bit RAW (0-4095,
    # clamped at RAW12_MAX) -- feeding it 16-bit values would saturate nearly
    # every pixel at the clamp and only test the clamp, not the interpolation.
    # The SW proxy's _demosaic_rggb16 tap arithmetic is scale-agnostic (plain
    # sums/averages, no domain-specific clamp inside the function), so 12-bit
    # input exercises the identical code path it uses at its real 16-bit
    # (shift8) input range without triggering a spurious domain mismatch.
    raw = random_grid(rng, w, h, bits)
    bayer_np = np.array(raw, dtype=np.int64).reshape(h, w)

    for name, mod in (("baseline_isp_pipeline", BP), ("checker", CK)):
        sw_result = mod._demosaic_rggb16(bayer_np, w, h)  # (h, w, 3) int32
        for y in range(h):
            for x in range(w):
                gr, gg, gb = G.demosaic_rggb12(raw, w, h, x, y)
                sr, sg, sb = (int(sw_result[y, x, 0]),
                              int(sw_result[y, x, 1]),
                              int(sw_result[y, x, 2]))
                if (gr, gg, gb) != (sr, sg, sb):
                    return False, (
                        f"MISMATCH at grid {w}x{h} ({name}) pixel (x={x},y={y}): "
                        f"golden(gen_golden_vectors.demosaic_rggb12)=({gr},{gg},{gb}) "
                        f"!= sw({name}._demosaic_rggb16)=({sr},{sg},{sb})"
                    )
    return True, ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cases", type=int, default=200, help="number of random grids to test")
    ap.add_argument("--seed", type=int, default=20260709, help="RNG seed (deterministic by default)")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    failures = 0
    tested_pixels = 0
    # Mix of grid sizes: minimum, typical, odd dimensions (exercises the
    # w-1/h-1 edge clamp identically to verify_binning_cross_check.py).
    size_pool = [(2, 2), (3, 3), (4, 4), (8, 6), (16, 16), (17, 17), (33, 9)]

    for i in range(args.cases):
        w, h = size_pool[i % len(size_pool)]
        ok, msg = check_one(rng, w, h, bits=12)
        tested_pixels += w * h * 2  # 2 modules checked per grid
        if not ok:
            print(f"[verify_demosaic_bilinear_cross_check] FAIL case {i}: {msg}")
            failures += 1
            if failures >= 10:
                print("[verify_demosaic_bilinear_cross_check] stopping after 10 failures")
                break

    if failures:
        print(f"[verify_demosaic_bilinear_cross_check] FAILED: {failures} mismatching grid(s)")
        return 1

    print(f"[verify_demosaic_bilinear_cross_check] PASS: {args.cases} random grids, "
          f"{tested_pixels} pixel checks, gen_golden_vectors.demosaic_rggb12 == "
          f"baseline_isp_pipeline._demosaic_rggb16 == checker._demosaic_rggb16 "
          f"(independent implementations, bit-exact)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Generate bit-exact golden vectors for the 4 RM variants.

Cases exercise: dark-flat (noise lift), edge/gradient (feature preserve),
highlight (knee clipping), threshold boundary. Each case is emitted for all
4 variants. Output mirrors tools/rm_model.py exactly.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import rm_model as M


def _dark_flat(w, h):
    return [40 for _ in range(w * h)]


def _gradient(w, h):
    return [min(4095, (x * 4096) // w) for y in range(h) for x in range(w)]


def _edge(w, h):
    return [(60 if x < w // 2 else 3000) for y in range(h) for x in range(w)]


def _highlight(w, h):
    return [3900 for _ in range(w * h)]


def _boundary(w, h):
    # mix of dark and mid to sit around checker threshold
    return [(50 if (x + y) & 1 else 900) for y in range(h) for x in range(w)]


CASES = [
    ("dark_flat_8x8", 8, 8, _dark_flat),
    ("gradient_16x16", 16, 16, _gradient),
    ("edge_16x16", 16, 16, _edge),
    ("highlight_8x8", 8, 8, _highlight),
    ("boundary_8x8", 8, 8, _boundary),
]


def write_csv(path: Path) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = 0
    with path.open("w", newline="") as f:
        wr = csv.writer(f, lineterminator="\n")
        wr.writerow(["case", "variant", "width", "height", "index", "raw", "expected_rgb_hex"])
        for name, w, h, gen in CASES:
            raw = gen(w, h)
            for variant in (M.VAR_STATIC, M.VAR_REG, M.VAR_BIN, M.VAR_FP):
                out = M.variant_frame(raw, w, h, variant)
                for idx in range(w * h):
                    wr.writerow([name, variant, w, h, idx, raw[idx], f"0x{out[idx]:06x}"])
                    rows += 1
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="tests/rm_golden_vectors.csv")
    args = ap.parse_args()
    out = Path(args.out)
    n = write_csv(out)
    print(f"wrote {out} ({n} data rows, {len(CASES)} cases x 4 variants)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

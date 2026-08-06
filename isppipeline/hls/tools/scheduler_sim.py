#!/usr/bin/env python3
"""E3 — DFX-aware mode scheduler simulation (software, no board).

Compares four scheduler policies on a deterministic noisy luminance sequence:
  1. baseline_checker : instantaneous threshold (repo baseline behaviour)
  2. plus_hysteresis  : dual Th_hi/Th_lo
  3. plus_temporal    : hysteresis + N-frame temporal smoothing
  4. plus_min_dwell   : + minimum dwell time + PR invalid window

Metrics: mode mismatch rate, switch count / 1k frames, thrashing rate,
skipped/invalid frames. Deterministic (fixed seed). numpy only.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np

N = 1000
THR = 512            # luminance threshold (12-bit domain)
TH_HI = 560          # hysteresis high (enter normal needs > Th_hi)
TH_LO = 464          # hysteresis low  (enter low-light needs < Th_lo)
TEMPORAL_N = 5       # consecutive frames required to switch
MIN_DWELL = 30       # min frames to stay after a switch
PR_INVALID = 4       # frames invalidated per DFX reconfiguration
SEED = 7


def build_sequence():
    """Piecewise true scene (0=normal,1=low-light) + noisy measured luminance."""
    rng = np.random.default_rng(SEED)
    # segments: (true_mode, mean_luma, length). include near-threshold flicker zones.
    segs = [
        (0, 800, 150), (1, 250, 150), (0, 540, 120),   # 540 near THR -> flicker
        (1, 470, 120),                                 # 470 just below -> borderline
        (0, 900, 150), (1, 180, 160), (0, 600, 50),
    ]
    luma, truth = [], []
    for mode, mean, length in segs:
        luma.extend(rng.normal(mean, 60, length))
        truth.extend([mode] * length)
    luma = np.array(luma[:N]); truth = np.array(truth[:N])
    # pad if short
    if len(luma) < N:
        luma = np.pad(luma, (0, N - len(luma)), constant_values=800)
        truth = np.pad(truth, (0, N - len(truth)), constant_values=0)
    return np.clip(luma, 0, 4095), truth


def run_policy(luma, policy):
    """Return decided mode per frame and switch indices."""
    mode = 0            # start normal
    decided = []
    switches = []
    cnt = 0             # consecutive counter for temporal
    pending = None
    last_switch = -10 ** 9
    for t, L in enumerate(luma):
        new = mode
        if policy == "baseline_checker":
            new = 1 if L < THR else 0
        else:
            # hysteresis target
            if mode == 0 and L < TH_LO:
                target = 1
            elif mode == 1 and L > TH_HI:
                target = 0
            else:
                target = mode
            if policy == "plus_hysteresis":
                new = target
            else:
                # temporal smoothing: require TEMPORAL_N consecutive votes
                if target != mode:
                    if pending == target:
                        cnt += 1
                    else:
                        pending, cnt = target, 1
                    if cnt >= TEMPORAL_N:
                        new = target
                else:
                    pending, cnt = None, 0
                if policy == "plus_min_dwell":
                    # block switch until min dwell elapsed
                    if new != mode and (t - last_switch) < MIN_DWELL:
                        new = mode
        if new != mode:
            switches.append(t)
            last_switch = t
            mode = new
            pending, cnt = None, 0
        decided.append(mode)
    return np.array(decided), switches


def metrics(decided, truth, switches, policy):
    mismatch = float(np.mean(decided != truth))
    switch_per_1k = len(switches) * 1000.0 / len(decided)
    # thrashing: switches that reverse within 10 frames
    thr = 0
    for i in range(1, len(switches)):
        if switches[i] - switches[i - 1] <= 10:
            thr += 1
    thrashing = thr / max(1, len(switches))
    # skipped/invalid frames: DFX reconfig invalidates PR_INVALID frames per switch
    skipped = len(switches) * PR_INVALID
    return mismatch, switch_per_1k, thrashing, skipped


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="results/scheduler.csv")
    args = ap.parse_args()
    luma, truth = build_sequence()
    policies = ["baseline_checker", "plus_hysteresis", "plus_temporal", "plus_min_dwell"]
    res = {}
    for p in policies:
        decided, switches = run_policy(luma, p)
        res[p] = metrics(decided, truth, switches, p)
        print(f"{p:18s} mismatch={res[p][0]:.3f} switch/1k={res[p][1]:.1f} "
              f"thrash={res[p][2]:.3f} skipped={res[p][3]}")
    out = Path(args.out); out.parent.mkdir(parents=True, exist_ok=True)
    rows = [
        ("mode_mismatch_rate", 0, "ratio", "lower better"),
        ("switch_count_per_1k", 1, "count/1k frames", "lower better"),
        ("thrashing_rate", 2, "ratio", "reversals<=10f"),
        ("skipped_invalid_frames", 3, "count", f"PR_INVALID={PR_INVALID}/switch"),
    ]
    with out.open("w", newline="") as f:
        wr = csv.writer(f, lineterminator="\n")
        wr.writerow(["metric"] + policies + ["unit", "notes"])
        for name, j, unit, note in rows:
            wr.writerow([name] + [f"{res[p][j]:.3f}" if j < 3 else f"{res[p][j]}" for p in policies] + [unit, note])
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

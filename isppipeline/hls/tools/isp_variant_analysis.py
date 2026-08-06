#!/usr/bin/env python3
"""E2-proxy — ISP variant analysis without a detector (software, no board).

Builds a deterministic dark synthetic pseudo-RAW scene (flat region + high-freq
texture + edge + noise), runs the 4 RM variants from rm_model (bit-exact to the
C-sim), and computes no-reference proxies that motivate the mAP hypothesis:
  - mean_luma      : brightness lift (low-light visibility)
  - edge_energy    : high-frequency retention (feature preservation proxy)
  - edge_retention : edge_energy / static  (DFX-Bin loses, DFX-FP keeps)
  - flat_snr       : mean/std on a flat ROI (denoise/SNR proxy)
  - clip_rate      : fraction of clipped pixels (highlight handling)

This is a proxy, not mAP. The full detector pipeline is tools/eval_map.py.
numpy only.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np

import rm_model as M

W = H = 96
SEED = 11


def build_dark_bayer():
    """12-bit GRBG Bayer of a dark scene: flat ROI + texture + vertical edge + noise."""
    rng = np.random.default_rng(SEED)
    img = np.full((H, W), 60.0)                       # dark base
    img[8:32, 8:32] = 50.0                            # flat ROI (top-left)
    # fine high-frequency checkerboard texture (period-1 px) in a clean ROI
    # (rows 56:96, cols 48:96) — binning destroys this, full-res FP retains it.
    yy, xx = np.mgrid[0:H, 0:W]
    tex = ((((xx + yy) % 2)) * 120).astype(float)
    mask = (xx >= 48) & (yy >= 56)
    img[mask] += tex[mask]
    # macro vertical edge in a separate band (rows 40:52) — does not touch ROI
    img[40:52, W // 2:] += 220
    # sensor noise
    img += rng.normal(0, 12, (H, W))
    img = np.clip(img, 0, 4095).astype(int)
    return img.flatten().tolist()


def unpack(out):
    a = np.array(out, dtype=np.int64)
    R = (a >> 16) & 0xFF
    G = (a >> 8) & 0xFF
    B = a & 0xFF
    luma = (R + 2 * G + B) // 4
    return R.reshape(H, W), G.reshape(H, W), B.reshape(H, W), luma.reshape(H, W)


def edge_energy(luma):
    gx = np.abs(np.diff(luma, axis=1))
    gy = np.abs(np.diff(luma, axis=0))
    return float((gx.mean() + gy.mean()) / 2.0)


def texture_hf(luma):
    """High-frequency energy on the fine-texture ROI (rows 56:, cols 48:).
    Directly tests resolution loss: binning -> ~0, full-res FP -> retained."""
    roi = luma[56:, 48:].astype(float)
    gx = np.abs(np.diff(roi, axis=1))
    gy = np.abs(np.diff(roi, axis=0))
    return float((gx.mean() + gy.mean()) / 2.0)


def flat_snr(luma):
    roi = luma[8:32, 8:32].astype(float)
    return float(roi.mean() / (roi.std() + 1e-6))


def clip_rate(R, G, B):
    clipped = (R == 255) | (G == 255) | (B == 255)
    return float(clipped.mean())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="results/isp_analysis.csv")
    args = ap.parse_args()
    raw = build_dark_bayer()
    variants = [M.VAR_STATIC, M.VAR_REG, M.VAR_BIN, M.VAR_FP]
    names = [M.VARIANT_NAMES[v] for v in variants]
    res = {}
    static_edge = None
    for v in variants:
        out = M.variant_frame(raw, W, H, v)
        R, G, B, luma = unpack(out)
        e = edge_energy(luma)
        if v == M.VAR_STATIC:
            static_edge = e
        res[M.VARIANT_NAMES[v]] = {
            "mean_luma": float(luma.mean()),
            "edge_energy": e,
            "texture_hf": texture_hf(luma),
            "flat_snr": flat_snr(luma),
            "clip_rate": clip_rate(R, G, B),
        }
    static_hf = res[M.VARIANT_NAMES[M.VAR_STATIC]]["texture_hf"]
    for n in names:
        res[n]["edge_retention"] = res[n]["edge_energy"] / (static_edge + 1e-9)
        res[n]["hf_retention"] = res[n]["texture_hf"] / (static_hf + 1e-9)

    metrics = ["mean_luma", "edge_energy", "texture_hf", "hf_retention", "flat_snr", "clip_rate"]
    out = Path(args.out); out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="") as f:
        wr = csv.writer(f, lineterminator="\n")
        wr.writerow(["metric"] + names + ["notes"])
        notes = {
            "mean_luma": "brightness lift (higher=brighter)",
            "edge_energy": "global edge energy (macro-edge dominated)",
            "texture_hf": "fine-texture HF energy (resolution proxy)",
            "hf_retention": "texture_hf vs static (FP>Bin = feature preserve)",
            "flat_snr": "denoise/SNR proxy (higher=cleaner)",
            "clip_rate": "clipped fraction (lower better)",
        }
        for m in metrics:
            wr.writerow([m] + [f"{res[n][m]:.3f}" for n in names] + [notes[m]])
    # console + markdown summary
    print("variant       mean_luma texture_hf hf_ret flat_snr clip")
    for n in names:
        r = res[n]
        print(f"{n:12s} {r['mean_luma']:8.2f} {r['texture_hf']:10.3f} "
              f"{r['hf_retention']:6.3f} {r['flat_snr']:7.2f} {r['clip_rate']:5.3f}")
    md = out.with_suffix(".md")
    with md.open("w") as f:
        f.write("# ISP variant analysis (proxy, no detector)\n\n")
        f.write(f"Synthetic dark scene {W}x{H}, seed={SEED}. Proxy for mAP hypothesis.\n\n")
        f.write("| metric | " + " | ".join(names) + " |\n|" + "---|" * (len(names) + 1) + "\n")
        for m in metrics:
            f.write(f"| {m} | " + " | ".join(f"{res[n][m]:.3f}" for n in names) + " |\n")
        fp_hf, bin_hf = res['dfx_fp']['hf_retention'], res['dfx_bin']['hf_retention']
        verdict = ("FP가 Bin보다 HF 보존 우위 → feature-preserve 가설 지지"
                   if fp_hf > bin_hf else
                   "현 정수 DFX-FP는 HF 보존에서 Bin 미달 → **가설 미지지(이 프록시 기준)**. "
                   "soft-knee lift가 미세 디테일을 압축. RM 재설계(약한 tone+guided detail) 또는 실제 mAP로 판정 필요")
        f.write("\n## 해석 (정직한 평결)\n")
        f.write(f"- **feature 보존(핵심):** texture hf_retention Bin={bin_hf:.3f} vs FP={fp_hf:.3f} → {verdict}.\n")
        f.write(f"- **denoise:** flat_snr Bin={res['dfx_bin']['flat_snr']:.2f} vs FP={res['dfx_fp']['flat_snr']:.2f} "
                "— 단, flat_snr는 mean lift에 의해 부풀려지므로(밝기 교란) 단독 결론 금지.\n")
        f.write(f"- **밝기:** mean_luma static={res['static']['mean_luma']:.1f} → FP={res['dfx_fp']['mean_luma']:.1f} "
                "(저조도 가시성↑은 명확).\n")
        f.write("- 전역 edge_energy는 단일 매크로 에지 지배로 변종 구분력 약함 → texture_hf로 판정.\n")
        f.write("- **결론:** no-reference proxy로는 DFX-FP>DFX-Bin을 확증 못 함. (1) DFX-FP 재설계, "
                "(2) 실제 pseudo-RAW mAP(tools/eval_map.py)로 확정. SW단계가 보드 전 이 리스크를 조기 노출함.\n")
    print(f"wrote {out} and {md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

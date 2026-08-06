# default_ISP — the Vitis-Vision-aligned standard ISP arm (2026-08-06)

Design note for `src/default_isp.cpp` / `include/default_isp.hpp`, split out
of the source comments.

## 0. Why it exists

The existing `RM_NORMAL_TONE` (in `dfxisp_accel.cpp`) differs from the AMD
Vitis Vision L3 `isppipeline` example in **stage order and stage domain**
(verified against the actual source, 2026-08-06). Calling the normal arm a
"standard ISP baseline" in the paper therefore requires closing a
**structural** gap, not a constant gap — hence this separate arm, which
follows the Vitis Vision ordering. It is the first deliverable of the
Vitis-first refactor proposed 2026-07-03 and tracked as unstarted in the
origin repo's ROADMAP #7.

**It is additive.** `RM_NORMAL_TONE`, its golden contract, and the deployed
BLC/checker decisions are untouched. Whether default_ISP is promoted to the
deployed normal-mode RM is a separate decision (origin `STRATEGY.md` open
question #4).

## 1. Pipeline comparison (Vitis Vision vs the two arms)

| # | Vitis Vision `isppipeline` | **default_ISP (new)** | RM_NORMAL_TONE (existing) |
|---|---|---|---|
| 1 | blackLevelCorrection — **Bayer** | ✅ same (subtract + range restore) | demosaic comes first |
| 2 | gaincontrol — **Bayer**, per R/B site | ✅ same (Q8 286/307) | — |
| 3 | demosaicing | ✅ RGGB bilinear | RGGB bilinear |
| 4 | AWB — **RGB, per-frame adaptive** | ✅ gray-world adaptive (bypassable) | fixed-constant WB (RGB) |
| 5 | colorcorrectionmatrix | ✅ **real 3×3 Q8 matrix** | identity placeholder |
| 6 | quantization & dithering | △ `>>4` only (dithering omitted) | `>>4` |
| 7 | gammacorrection (LUT) | ✅ same LUT (γ2.0) | same LUT |
| 8 | rgb2yuyv (output CSC) | ✗ deliberately omitted → RGB888 | RGB888 |
| — | (gain) | none — exposure gain belongs to the tone RM | gain 1.25× (tone) |

## 2. Deliberate deviations from Vitis (3, with reasons)

1. **Output CSC (rgb2yuyv) omitted** — the consumer is a DPU/detector that
   takes RGB888 directly (32-bit packing kept for AXI alignment, SPEC §5.1).
   Switching to YUYV would change the whole evaluation path for no gain.
2. **Dithering omitted** — Vitis' quantization&dithering is an optional
   stage that reduces banding on bit-depth reduction. No effect on detection
   mAP has been established, so only the `>>4` truncation is kept (can be
   added later).
3. **AWB statistic source** — Vitis derives gains from the **previous
   frame's histogram** (1-frame lag, double-buffered). default_ISP uses the
   **current frame's Bayer-site means (gray-world)**: no frame buffer, no
   lag, at the cost of one extra read pass over raw (§4). The statistic is
   also simpler (gray-world vs histogram normalization) — read this as "an
   adaptive AWB stage in the same position with the same role", not "the
   same algorithm as Vitis".

## 3. Constants

| Stage | Constant | Value | Rationale |
|---|---|---|---|
| (1) BLC | `BLC_LEVEL12` | 32 (= 8-bit 2 << 4) | the value deployed by the 2026-07-20 real-RAW recalibration |
| (1) BLC | `BLC_MUL_Q8` | 258 | `round(256 × 4095/(4095−32))` — restores the range lost to the subtraction (the Vitis approach) |
| (2) gain | `GAIN_R_Q8` / `GAIN_B_Q8` | 286 / 307 | same values as the existing arm's WB — the two arms differ in **where** the gain is applied, not how much |
| (4) AWB | clamp | Q8 [64, 1024] | 0.25×–4× |
| (5) CCM | 3×3 Q8 | rows sum to 256 | preserves neutral gray. **Placeholder until sensor calibration** — no color-accuracy claims |
| (7) gamma | LUT | γ2.0 `isqrt(255v)` | byte-identical to `dfxisp_accel.cpp` so the arms stay comparable on the tone axis |

## 4. Implementation notes

- **BLC/gain are applied on the fly, with no buffer**: the correction runs
  at every demosaic-window read. It is pointwise, so results are identical
  and no frame buffer is needed — but the same pixel is re-corrected up to 9
  times (compute redundancy traded for zero storage).
- **AWB adds a second read pass** over raw. Combined with the checker's own
  full-frame scan, this raises the per-frame gmem0 read count — account for
  it in the bandwidth budget at board bring-up.
- **Accumulator width**: 1920×1080×4095 ≈ 8.5e9 → 64-bit accumulators.
- The CCM's negative coefficients can drive the accumulator below zero, so
  it is **floored at 0 before the shift** — this avoids C++'s
  implementation-defined right shift of negatives and guarantees
  bit-exactness with the Python golden.

## 5. Verification

| Gate | Status |
|---|---|
| Python golden ↔ C++ bit-exact (`make default-isp-verify`) | ✅ 528 px, 10 cases (flat / gradient / color cast / saturation / odd dims / 1×1) |
| BLC precedes demosaic in the Bayer domain (at-or-below pedestal → pure black) | ✅ |
| AWB is adaptive (channel imbalance shrinks on a color cast) | ✅ |
| CCM preserves neutral (rows sum to 256 → flat input, channel spread ≤ 8) | ✅ |
| Saturated input never overflows RGB8 | ✅ |
| DFX contract (`rm_default_isp_top`, 6 args, identical output to `default_isp(AWB_ON)`) | ✅ |

In this handover repo `make default-isp-csim` runs against the committed
golden CSV; `make default-isp-golden` needs the origin repo's `tools/`
Python (same situation as `make verify`).

## 6. Measured resources / timing (Vitis HLS 2024.1, xczu7ev, 5.0 ns)

`reports/csynth/rm_default_isp_top_csynth.rpt` — synthesized via the
flat-tempdir workaround.

| top | BRAM_18K | DSP | FF | LUT | Est. period |
|---|---:|---:|---:|---:|---:|
| **`rm_default_isp_top`** (default_ISP) | 4 | **28** | **8,794** | **12,659** | 3.650 ns |
| `rm_normal_tone_top` (RM_NORMAL_TONE) | 4 | 12 | 3,797 | 5,202 | 3.650 ns |

> **Updated after the 2026-08-06 ponytail review:** the AWB green gain is
> always 256 by construction (green is the reference channel), so its
> out-param, multiply and clamp were all identity — removing them and
> re-synthesising gives **FF 8,803 → 8,794 (−9)** with LUT, DSP and timing
> unchanged. The synthesiser had already folded ×256>>8 into a shift, so the
> saving was pipeline registers, not a multiplier. A case study in source
> tidiness not automatically translating into silicon gain.

**Reading:** default_ISP costs **2.43× the LUT and 2.33× the DSP** of the
existing normal arm. The increase is structural — (a) the AWB statistics
pass (full scan + 64-bit accumulators + division), (b) a real CCM (a
9-multiply matrix instead of identity), (c) the BLC range-restore multiply.
**Timing is identical** (3.650 ns = 273.97 MHz), so adopting the standard
structure costs no Fmax.

> **Caution:** these are csynth estimates. SPEC.md §10.3's arm comparison
> table is on the post-route flat axis — **do not mix them**. Putting
> default_ISP into that comparison requires a separate post-route run with
> the same tie-off wrapper (not done).

## 7. Remaining work

1. **mAP not evaluated** — default_ISP's detection performance has not been
   measured. The SW proxy (`tools/baseline_isp_pipeline.py`, origin repo)
   still mirrors RM_NORMAL_TONE, so evaluating default_ISP needs a matching
   Python proxy (or reuse of `gen_default_isp_golden.py`) wired into the mAP
   harness.
2. **Post-route measurement** — see the caution in §6.
3. **Adoption decision** — promoting default_ISP to `RM_NORMAL` is decided
   together with `STRATEGY.md` open question #4 (the RP-boundary narrative).
   Promotion would pull in golden regeneration and a checker-constant review.
4. **CCM calibration** — the current matrix is a placeholder (§3).

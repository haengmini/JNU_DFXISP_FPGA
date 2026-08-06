# lowlight_ISP — the proposal low-light arm, v2 (2026-08-06)

Design note for `src/lowlight_isp.cpp` / `include/lowlight_isp.hpp`. It turns
the principles of the origin repo's `lowlight-feature-principles-2026-07-05.md`
and the **measured levers** of the 07-08…07-20 real-RAW campaigns directly
into a pipeline.

## 0. Where it sits

| Module | Role | Status |
|---|---|---|
| `default_isp.cpp` | standard/reference arm (Vitis Vision ordering) | coexisting, not deployed |
| **`lowlight_isp.cpp`** | **proposal low-light arm (this note)** | coexisting, not deployed |
| RM_LOW_LIGHT_TONE in `dfxisp_accel.cpp` (v1) | currently deployed arm | **untouched** |

It **shares default_ISP's correction backbone** (stages ①②④), so the two arms
differ in exactly **{binning, exposure gain, tone curve, denoise}** — which is
what makes the paper's "the specialised arm wins in its own condition" claim a
controlled comparison.

## 1. Pipeline

```
RAW Bayer 12-bit
 ① blackLevelCorrection [Bayer]   subtract + range restore (Q8 258)  ┐ shared with
 ② gain [Bayer, pre-quantisation] exposure 2.0x x per-site WB, folded │ default_ISP
 ③ 2x2 binning-demosaic [fused]   R=TL, G=(TR+BL)/2, B=BR            │ ← low-light specific
 ④ colorcorrectionmatrix [RGB12]  same matrix as default_ISP          ┘
 ⑤ GAT/Anscombe VST tone [12->8]  replaces gamma 2.0                  ← low-light specific (core)
 ⑥ edge-preserving denoise [VST]  sigma-clip 3x3, constant threshold  ← low-light specific
 ⑦ pack RGB888 -> H/2 x W/2 (Policy A)
```

## 2. Key design decisions

### 2.1 Putting GAT/VST in the tone slot (⑤) — the centre of this design

```
f(z) = (2/a)·sqrt(a·z + 3a²/8 + b)    (sigma² = a·y + b, Poisson-Gaussian)
out8 = 255·(sqrt(N(z)) − sqrt(N(0))) / (sqrt(N(4095)) − sqrt(N(0)))
N(z) = 256·(a·z + 3a²/8 + b)
```

Subtracting `sqrt(N(0))` is the point. Plain sqrt has **infinite slope at the
origin**, so it amplifies the read-noise floor maximally; the GAT offset term
makes the curve **linear near zero**. Measured curve comparison (same 12-bit
input):

| input (8-bit equiv.) | 1 | 2 | 4 | 16 | 64 | 100 | 255 |
|---|---:|---:|---:|---:|---:|---:|---:|
| **GAT** | **7** | **12** | **20** | 53 | 119 | 153 | 255 |
| existing gamma 2.0 | 15 | 22 | 31 | 63 | 127 | 159 | 255 |
| ratio | 0.47 | 0.55 | 0.65 | 0.84 | 0.94 | 0.96 | 1.00 |

**Half the lift at the noise floor, near-identical above mid-grey** — exactly
the intent. With `b = 0` the curve degenerates *exactly* to the existing
gamma 2.0, so this is a strict generalisation.

> **Testable prediction:** if BLC is the dominant lever (up to 5.7× mAP)
> *because* its clipping happens to cut the noise floor, then once GAT does
> that job on principle, **the BLC sensitivity should flatten**. Verifying
> this is the core experiment for this arm (§6).

### 2.2 Denoise in the VST domain with a constant threshold (⑥)

Variance stabilisation makes the noise sigma signal-independent — **derived**:

```
sigma_VST = sigma_z · |d(out8)/dz| = 255·A/(16·2·D) = 255·256/(16·2·961) ≈ 2.1 LSB
```

So no per-signal-level threshold multiply/LUT is needed; one constant suffices.
`DENOISE_SIGMA = 5 ≈ 2.4 sigma`, deliberately **conservative** — the literature
is consistent that over-denoising removes the high-frequency features detectors
rely on. Sigma-clipping (neighbours beyond ±sigma of the centre are excluded
from the mean) preserves edges.

No inverse transform is needed either: the detector's input space is gamma-like
anyway, so **the VST output is the final tone**. One LUT does variance
stabilisation, tone mapping and floor suppression at once.

### 2.3 Gain moved upstream (②)

Gain cannot change SNR (signal and noise scale together), but in
low-light + high-gain the quantisation variance widens by the gain factor, so
it is applied in the **wide Bayer domain before the `>>4`** (principle P3
§4.3). Exposure gain (2.0×) and the per-site WB are **folded into one
multiply**.

### 2.4 No adaptive AWB (deliberate)

Three independent campaigns established WB is **not a lever** (full-range mAP
spread 0.0020 vs the BLC lever's 0.0876). The reason is structural — a
per-channel global gain is a diagonal linear transform that moves no edges or
shapes, and it sits after BLC clipping so it cannot act on the pixels already
at zero. So fixed gains only, and the **AWB statistics pass (an extra frame
read) is saved**. default_ISP keeps one for the general arm but has an
`AWB_OFF` switch, so a matched comparison is available.

## 3. Finding: our binning is not +6 dB (documentation correction)

`lowlight-feature-principles` §4.1 states "2×2 same-colour summation → SNR
+6 dB", but **the current implementation (both v1 and v2) is not same-colour
summation.** An RGGB 2×2 cell holds one R, two G and one B:

| Channel | Actual operation | SNR gain |
|---|---|---|
| R, B | single sample | **0 dB** |
| G | 2-sample average | +3 dB |

True same-colour 2×2 binning needs a 4×4 raw footprint and would output
H/4 × W/4 (4× the resolution loss). **v2 keeps the current semantics** and
leaves R/B noise reduction to the edge-aware denoise in ⑥ — a better choice
than blind averaging, and it separates the roles of binning and denoising
cleanly. But **the +6 dB claim in the principles doc does not apply to this
implementation** — cite with care.

## 4. Parameters

| Parameter | Value | Status |
|---|---|---|
| `BLC_LEVEL12` / `BLC_MUL_Q8` | 32 / 258 | inherited from the deployed value; **re-sweep needed** after GAT (§2.1 prediction) |
| `EXPOSURE_GAIN_Q8` | 512 (2.0×) | inherited from v1; not swept since moving upstream |
| `WB_R/G/B_Q8` | 286/256/307 | confirmed non-lever — fixed |
| **`A_Q8` (a)** | **256 (a = 1.0 DN)** | **ESTIMATE — no EMVA1288 calibration** |
| **`B_DN2` (b)** | **16 (sigma_read = 4 DN)** | **ESTIMATE — same** |
| `DENOISE_SIGMA` | 5 (≈2.4 sigma) | derived-value based; sweep needed |
| soft-knee | **not implemented** | measured saturation ≤ 2.13%, so no evidence justifies it; a LUT-only change if wanted later |

That `a` and `b` are estimates is this arm's largest uncertainty. Until
calibration, the exact GAT shape is not settled and sensitivity must be
reported alongside any result.

## 5. Measured resources / timing (Vitis HLS 2024.1, xczu7ev, 5.0 ns)

`reports/csynth/rm_lowlight_isp_top_csynth.rpt`

| top | BRAM_18K | DSP | FF | LUT | Est. period |
|---|---:|---:|---:|---:|---:|
| **`rm_lowlight_isp_top`** (v2 low-light) | **11** | **17** | **6,447** | **10,848** | 3.650 ns |
| `rm_default_isp_top` (v2 normal) | 4 | 28 | 8,803 | 12,659 | 3.650 ns |
| `rm_low_light_tone_top` (v1 low-light) | 8 | 9 | 3,243 | 4,204 | 3.650 ns |
| `rm_normal_tone_top` (v1 normal) | 4 | 12 | 3,797 | 5,202 | 3.650 ns |

**A failed prediction, recorded:** the design proposal estimated "+10–20% over
the current arm"; the measurement is **2.58× v1 (4,204 → 10,848 LUT)**.
Breakdown:
- **denoise (⑥) dominates** — 3 channels × 9 neighbours = 27 compare/selects
  per output pixel
- CCM introduced (identity → a real 3×3): DSP 9 → 17
- GAT LUT of 4096 entries (vs the 256-entry gamma LUT): BRAM 8 → 11
- BLC range-restore + folded gain multiply run 4× per binned pixel (once per
  2×2 sample)

**Timing is identical across all four arms** (3.650 ns = 273.97 MHz), so the
principled structure costs no Fmax.

**The DFX story survives:** on the v2 axis the low-light arm (10,848) is still
14% smaller than the normal arm (12,659), since it processes only H/2 × W/2.
The DFX saving ratio versus a both-resident Arm2 is expected to be similar to
v1, but **post-route has not been measured** (SPEC §10.3's arm table is on the
post-route flat axis — **do not mix** it with these csynth numbers).

## 6. Remaining work (suggested experiment order)

1. **GAT-alone ablation + BLC re-sweep** — verifies the §2.1 prediction. The
   highest-value experiment; either outcome is publishable.
2. **Denoise ablation** — if it cannot demonstrate a gain, **it should be
   removed** (the literature warning plus its dominance of the area budget
   mean the cost/benefit must be explicit).
3. **Gain-placement ablation.**
4. **Size-decomposed AP (small/med/large)** — measures the binning break-even,
   re-interpreted in light of the §3 finding.
5. **Calibrate a, b** — EMVA1288 or data-driven estimation, with sensitivity
   reported.
6. Wire into the mAP harness (the SW proxy currently mirrors the v1 arm only),
   post-route measurement, and the `RM_LOW_LIGHT` promotion decision.

## 7. Verification status

| Gate | Status |
|---|---|
| Python golden ↔ C++ bit-exact (`make lowlight-isp-verify`) | ✅ 131 px, 10 cases |
| BLC precedes everything (at/below pedestal → pure black; 2.0× gain and tone cannot resurrect it) | ✅ |
| GAT floor suppression (near-floor output < 40, midtone > 150) | ✅ |
| Denoise reduces noise (spread shrinks on a noisy patch) | ✅ |
| Denoise preserves edges (step contrast held within −2) | ✅ |
| Saturated input never overflows RGB8 | ✅ |
| DFX contract (`rm_lowlight_isp_top`, 6 args, identical to the dev top) | ✅ |
| **mAP** | ❌ **not evaluated** |

In this handover repo `make lowlight-isp-csim` runs against the committed
golden CSV; `make lowlight-isp-golden` needs the origin repo's
`tools/gen_lowlight_isp_golden.py`.

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
 ① binning [RAW]                  same-colour 2x2 average            ← low-light specific (SNR)
                                  R/B 4 samples +6 dB, G 8 samples +9 dB
                                  **before black level** (§2.5)
 ② blackLevelCorrection [binned]  subtract + range restore (Q8 258)  ┐ constants shared
 ③ gain [binned, pre-quantisation] exposure 2.0x x per-channel WB     │ with default_ISP
 ④ colorcorrectionmatrix [RGB12]  same matrix as default_ISP          ┘
 ⑤ GAT/Anscombe VST tone [12->8]  replaces gamma 2.0                  ← low-light specific (core)
 ⑥ edge-preserving denoise [VST]  sigma-clip 3x3, constant threshold  ← low-light specific
 ⑦ pack RGB888 -> H/2 x W/2 (Policy A)
```

> **Revised 2026-08-06:** the first implementation subsampled one pixel per
> 2x2 cell at ③ (§2.5), so binning's SNR benefit was never realised. It has
> been **replaced with real same-colour binning** and moved ahead of black
> level. The old behaviour is kept behind `LOWLIGHT_ISP_BIN_SUBSAMPLE` as
> the ablation baseline.

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

## 2.5 Binning made real, and moved ahead of black level (revised 2026-08-06)

**Finding:** v1 and the first v2 implementation took R = p00 and B = p11
straight from the 2×2 cell and averaged only the two G samples — an RGGB cell
holds a single R and a single B, so **R/B got 0 dB and only G got +3 dB**.
The module was called binning while not binning, and the +6 dB that motivated
the design choice was never realised.

**Fix:** replaced with a 2×2 average *within each colour plane* (overlapping
windows). Adjacent outputs share samples, so the **output size stays
H/2 × W/2** while every channel gains:

| Channel | Samples | SNR gain (theory) |
|---|---:|---|
| R, B | 4 | **+6 dB** |
| G | 8 | **+9 dB** |

**Measured** (synthetic Poisson-Gaussian frames, denoise OFF so binning is
isolated):

| signal level | sigma (subsample) | sigma (samecolour) | improvement |
|---:|---:|---:|---:|
| 100 | 3.19 | 1.59 | **+6.1 dB** |
| 400 | 2.72 | 1.21 | **+7.1 dB** |
| 1600 | 2.59 | 1.36 | **+5.6 dB** |

Matches theory (G's 8 samples pull the average slightly past +6 dB).

**Why it now precedes black level — noise rectification bias:** clipping each
sample at zero first gives `sum of max(0, x_i − p)`, which folds negative
noise excursions up and adds a **positive bias**. Averaging first and
subtracting the pedestal once is unbiased, and preserves signal whose
individual samples fall below the pedestal but whose average does not. Part
of the observed "52–73% of low-light pixels clip to zero" may be due to this
ordering.

**Isn't this redundant with the denoise in ⑥?** No. Binning is
**unconditional** — a guaranteed sqrt(4) reduction independent of signal —
whereas sigma-clipping is **conditional**, excluding neighbours beyond the
threshold (so it paradoxically averages less where noise is largest). That
said, once binning delivers an unconditional reduction the marginal value of
the denoise may shrink, which **raises the priority of the denoise ablation**
(§6).

> **Two corrections to the principles doc:** (a) its +6 dB applies only to
> **real same-colour summation** — it did not apply to our implementation
> before this revision. (b) its "+3 dB when shot-limited" is also wrong:
> a 4-sample average gives **+6 dB in both the read-limited and shot-limited
> regimes** (SNR is scale-invariant).

## 4. Parameters

| Parameter | Value | Status |
|---|---|---|
| `BLC_LEVEL12` / `BLC_MUL_Q8` | 32 / 258 | inherited from the deployed value; **re-sweep needed** after GAT (§2.1 prediction) |
| `EXPOSURE_GAIN_Q8` | 512 (2.0×) | inherited from v1; not swept since moving upstream |
| `WB_R/G/B_Q8` | 286/256/307 | confirmed non-lever — fixed |
| **`A_Q8` (a)** | **256 (a = 1.0 DN)** | just above the measured range 0.55–0.91 — kept, §4.1 |
| **`B_DN2` (b)** | **16 (sigma_read = 4 DN)** | 4× the measured PASCALRAW upper bound (b ≤ 3.97), but that is the wrong sensor — kept, §4.1 |
| `DENOISE_SIGMA` | 5 (≈2.4 sigma) | derived-value based; sweep needed |
| soft-knee | **not implemented** | measured saturation ≤ 2.13%, so no evidence justifies it; a LUT-only change if wanted later |

### 4.1 Calibration attempt (2026-08-06) — partial, constants unchanged

`tools/calibrate_noise_model.py` (origin repo, new) ran single-image photon
transfer over **16 PASCALRAW original NEFs** (8×8 blocks → 2nd-percentile
variance per brightness bin → χ² bias correction → linear fit):

| Quantity | Value | Confidence |
|---|---|---|
| slope `a` | **0.55 – 0.91 DN** | varies with settings, R² 0.88–0.97 |
| intercept `b` (line fit) | **negative (−32)** | ❌ **unusable** — physically impossible |
| `b` upper bound (darkest bin, direct) | **≤ 3.97 DN²** (sigma_read ≤ 2.0 DN, n=19,747) | ✅ observed, not extrapolated |

**Why the intercept fails:** in natural daylight images the "flat block"
population changes with brightness (shadows below, sky and texture above).
Texture contaminates even the low percentile at higher signal, steepening the
slope so the extrapolation to zero goes negative. Read noise is properly
measured from a **dark frame** (capped lens), and PASCALRAW has none.

**Why the constants were nonetheless left alone — it is the wrong sensor:**

1. The calibration target is **PASCALRAW (Nikon D3200, daylight, low ISO)**
   while this arm aims at **SonyNOD (RX100 VII, night, high ISO)**. Read
   noise **in DN scales with analog gain**, so applying a low-ISO daylight
   `b ≤ 4` to a high-ISO night frame errs in exactly the condition that
   matters most; the present 16 may well be closer for the night case.
2. **The Sony ARW originals are not on disk** — `data/sonynod_test/` holds
   only the shift8 `raw_bin` conversion, whose samples are all multiples of
   256 (8-bit derived), so the 12-bit read-noise scale has been quantised
   away. The target sensor cannot currently be measured at all.

**Conclusion:** `a` now has a measured range (0.55–0.91) and the current 1.0
sits just above it; `b` cannot be measured on the target sensor. Neither was
changed — moving constants on a half-calibration would buy the appearance of
measurement without the accuracy (SPEC §11.4).

**What is needed:** (a) the SonyNOD ARW originals, and (b) **dark frames** at
the night shooting ISO. With those, `b` comes straight from the dark-frame
variance and `a` from the same tool.

## 5. Measured resources / timing (Vitis HLS 2024.1, xczu7ev, 5.0 ns)

`reports/csynth/rm_lowlight_isp_top_csynth.rpt`

| top | BRAM_18K | DSP | FF | LUT | Est. period |
|---|---:|---:|---:|---:|---:|
| **`rm_lowlight_isp_top`** (v2 low-light, **real binning**) | **11** | **20** | **7,555** | **12,826** | 3.650 ns |
| (reference) same arm, subsample era | 11 | 17 | 6,447 | 10,848 | 3.650 ns |
| `rm_default_isp_top` (v2 normal) | 4 | 28 | 8,794 | 12,659 | 3.650 ns |
| `rm_low_light_tone_top` (v1 low-light) | 8 | 9 | 3,243 | 4,204 | 3.650 ns |
| `rm_normal_tone_top` (v1 normal) | 4 | 12 | 3,797 | 5,202 | 3.650 ns |

**A failed prediction, recorded:** the design proposal estimated "+10–20% over
the current arm"; the measurement is **3.05× v1 (4,204 → 12,826 LUT)**.
Breakdown:
- **denoise (⑥) dominates** — 3 channels × 9 neighbours = 27 compare/selects
  per output pixel
- **real binning (①) costs +1,978 LUT (+18%)** — raw reads per output pixel
  went from 4 to **16** (overlapping 2×2 windows), and the address arithmetic
  with them
- CCM introduced (identity → a real 3×3): DSP 9 → 20
- GAT LUT of 4096 entries (vs the 256-entry gamma LUT): BRAM 8 → 11

**Timing is identical across all arms** (3.650 ns = 273.97 MHz).

> **⚠️ One sub-claim of the DFX story has collapsed.** In the subsample era
> the low-light arm (10,848) was 14% smaller than the normal arm (12,659), so
> "low-light mode is cheaper" was sayable. With real binning it is **12,826 —
> 1.3% LARGER than the normal arm**. The claim that DFX saves against Arm2
> (both resident) still holds, but **"the low-light RM is smaller" no longer
> does** — do not use that phrasing in the paper.
>
> **Un-done optimisation:** the cause is servicing the overlapping windows
> with direct raw reads, 16 per output pixel. A 4-row line buffer would reuse
> them and bring it to one read per pixel, improving both area and bandwidth.
> These numbers are **pre-optimisation**, so addressing this first may make
> the low-light arm smaller again.

**Post-route has not been measured** (SPEC §10.3's arm table is on the
post-route flat axis — **do not mix** it with these csynth numbers).

## 6. Remaining work (suggested experiment order)

1. **GAT-alone ablation + BLC re-sweep** — verifies the §2.1 prediction. The
   highest-value experiment; either outcome is publishable.
2. **Denoise ablation** — if it cannot demonstrate a gain, **it should be
   removed** (the literature warning plus its dominance of the area budget
   mean the cost/benefit must be explicit).
3. **Gain-placement ablation.**
4. **Size-decomposed AP (small/med/large)** — measures the binning break-even.
   The `LOWLIGHT_ISP_BIN_SUBSAMPLE`/`SAMECOLOR` switch now makes binning's
   contribution **directly separable by measurement** (§2.5).
4b. **Binning line-buffer optimisation** — removes the 16-reads-per-pixel
   problem in §5 (area and bandwidth).
5. **Calibrate a, b** — EMVA1288 or data-driven estimation, with sensitivity
   reported.
6. Wire into the mAP harness (the SW proxy currently mirrors the v1 arm only),
   post-route measurement, and the `RM_LOW_LIGHT` promotion decision.

## 7. Verification status

| Gate | Status |
|---|---|
| Python golden ↔ C++ bit-exact (`make lowlight-isp-verify`) | ✅ 163 px, 12 cases (both binning modes) |
| **Binning SNR gain** (samecolour spread < subsample on the same noisy frame, denoise OFF) | ✅ **measured +5.6 to +7.1 dB** |
| Black level (at/below pedestal → pure black; averaging first cannot lift it) | ✅ |
| GAT floor suppression (near-floor output < 40, midtone > 150) | ✅ |
| Denoise reduces noise (spread shrinks on a noisy patch) | ✅ |
| Denoise preserves edges (step contrast held within −2) | ✅ |
| Saturated input never overflows RGB8 | ✅ |
| DFX contract (`rm_lowlight_isp_top`, 6 args, identical to the dev top) | ✅ |
| **mAP** | ❌ **not evaluated** |

In this handover repo `make lowlight-isp-csim` runs against the committed
golden CSV; `make lowlight-isp-golden` needs the origin repo's
`tools/gen_lowlight_isp_golden.py`.

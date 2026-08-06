---
type: spec
title: "DFXISP system specification (input datasets → output)"
project: DFXISP
version: 1.3
created: 2026-07-02
updated: 2026-08-06 — Schmitt hysteresis moved into fabric (this repo):
  `dfxisp_accel` now exports per-frame band flags (`hyst_flags`, ap_vld wire,
  11th argument; enter 62% / exit 60%) and the new static-region module
  `results/pr_controller/checker_hysteresis.v` owns the mode state and drives
  `pr_controller.trigger` directly (request/ack, no PS in the decision path) —
  implementing the 2026-07-03 adoption that had stayed unimplemented; the
  earlier "driver-side policy" wording (§3.1, §7) described that interim state
  and is superseded. End-to-end trigger chain verified in xsim
  (`checker_to_pr_tb.v`). Golden contract unchanged (csim bit-exact PASS).
  Previous update (2026-08-04): (a) per-mode WB split rejected (§4, §11.11): the diagnosis
  that the deployed shared WB is mistuned for low light was confirmed by
  measurement, but mAP does not respond, so no split
  (`results/lowlight-wb-mode-split-2026-08-03.md`). (b) RP-boundary wording
  corrected to match the actual implementation (§7, §11.12): the synthesized RP
  wraps the full per-mode pipeline, not "tone only". Previous update
  (2026-07-20): BLC/checker parameters updated to the values deployed by the
  07-13–07-20 real-RAW campaign (§3.1, §4). Canonical evidence:
  `results/blc-recalibration-deploy-2026-07-20.md`,
  `results/checker-c1-deploy-2026-07-20.md`,
  `results/checker-oracle-label-gate2-2026-07-20.md`.
target: Zynq UltraScale+ ZCU104 / XCZU7EV (xczu7ev-ffvc1156-2-e)
status: active — source-level shared baseline core + mutually exclusive tone RM
  slot; the physical RP boundary is per-mode full pipelines (§7, §11.12)
refs: "README.md · RESEARCH.md · isppipeline/hls · results/experiment-report-2026-07-02.md"
---

# DFXISP system specification

> **Note for this extracted repo (JNU_DFXISP_FPGA):** references of the form
> `results/*.md`, `tools/*`, `RESEARCH.md`, `STRATEGY.md`, `deliverables/`
> point to documents in the origin repo
> [haengmini/dfxisp](https://github.com/haengmini/dfxisp); most of them are
> not included here.

> **Architecture note (2026-07-10):** in the 2026-07-10 reset v2,
> `RESEARCH.md` widened the RM boundary from tone (gain/gamma) to the whole
> ISP datapath — only the static shell (checker / DFX controller / AXI /
> packer) remains static, and BLC/AWB/demosaic/CCM/gain/gamma are all owned
> by the two full ISP-pipeline RMs `RM_NORMAL`/`RM_LOW_LIGHT`. **The body of
> this SPEC.md still describes the current (v1) implementation, which has not
> been migrated to v2** — `isppipeline/hls/src/dfxisp_accel.cpp` still has
> the shared-baseline-core + tone-RM-slot structure. The v1 arithmetic
> (parameter values, interfaces) is itself correct and valid; this document
> will be updated together with the v2 code migration. When reading below,
> keep in mind that "baseline ISP core" is a v1-only concept.

> Defines the data formats, arithmetic, interfaces, and parameters of the
> whole chain: input (RAW datasets) → checker → tone RM slot → baseline ISP
> core → RGB32 output → detector/mAP (v1, current implementation). Canonical
> architecture: `RESEARCH.md`; implementation: `isppipeline/hls/`. All
> arithmetic is **integer (bit-exact)**.

---

## 0. Scope and the two-domain distinction

DFXISP has two execution domains. **The Bayer pattern is unified as RGGB**
(2026-07-02); the only remaining difference is the RAW bit representation:

| Domain | Purpose | Input RAW | demosaic | Canonical |
|---|---|---|---|---|
| **HW / C-sim** | hardware path, bit-exact verification | RAW **RGGB**, 12-bit in uint16 (`>>4`) | RGGB 3x3 | `src/dfxisp_accel.cpp` ↔ `tools/gen_golden_vectors.py` |
| **SW eval** | dataset-scale mAP/metrics | dataset RAW **RGGB16** (shift8, `>>8`) | RGGB nearest | `tools/newrm_pipeline.py` |

Both domains now use **the same Bayer convention (RGGB)** (unified
2026-07-02: C-sim GRBG→RGGB). The only remaining difference is the RAW bit
representation (HW 12-bit vs SW 8-bit shift8), which makes it possible in
the future to stream dataset raw directly into C-sim/HW for an end-to-end
bit comparison. Absolute bit-exactness is guaranteed inside the HW domain
(synthetic fixtures); SW mAP is judged by **relative ordering between
arms**.

---

## 1. System overview (end-to-end)

```text
[input datasets]                  [DFXISP pipeline]                          [output/eval]
 RAW Bayer          ──▶  ① Scene checker (mode decision: dark_ratio + hysteresis)
 (raw_bin / fixture)          │
 + labels (COCO-80)           ├─ NORMAL  ─▶ ② baseline core12 (demosaic+BLC+WB+CCM, 12-bit)
                              │                 └─▶ ③ RM_NORMAL_TONE (gain 1.25× + gamma2.0)
                              │                       └─▶ RGB32 (H×W)
                              │
                              └─ LOW_LIGHT ─▶ ③ RM_LOW_LIGHT_TONE.front (2x2 RAW binning-demosaic,
                                                  fused: R=TL, G=avg(TR,BL), B=BR — channel-preserving)
                                                └─▶ ② baseline core12 (BLC+WB+CCM, 12-bit, no second demosaic)
                                                     └─▶ ③ RM_LOW_LIGHT_TONE.back (gain 2.0× + gamma2.0)
                                                          └─▶ RGB32 (H/2 × W/2, Policy A)
                                                                       │
                              ④ metadata (4 scalar output pointers) ───┤
                                 out_w, out_h, selected_mode, selected_rm
                                                                       ▼
                                                        packed RGB888 0x00RRGGBB
                                                        ─▶ DPU / detector (YOLO·SSD) ─▶ mAP
```

**Invariants:** exactly one tone RM per frame (mutually exclusive);
gain/gamma exist only in the tone RMs (no duplication in the baseline core);
output metadata reports mode, RM, and shape. (Automatically verified by the
C-sim gates.)

---

## 2. Input specification

### 2.1 Dataset composition

**Canonical evaluation pair (real RAW, for the cross-superiority
demonstration of Goal 1 — RESEARCH.md §10):**

| Dataset | Illumination | Format | Role |
|---|---|---|---|
| **PASCAL RAW** | bright | real Bayer RAW | the condition where the `normal` module should win |
| **LOD RAW** | low light | **Sony `.ARW`** | the condition where the `lowlight` module should win |

- Both are real sensor RAW, carrying true Poisson-Gaussian noise, so the
  low-light module's binning (SNR recovery) justification is properly tested
  (pseudo-RAW has no noise to recover).
- Adapters: LOD is Sony `.ARW`, so the rawpy path of
  `tools/aodraw_adapter.py` applies as-is (per-file black/white level and
  Bayer phase read via rawpy). Normalized to the shift8 convention → same
  format as §2.2 below.
- **Arm comparison is limited to `normal`/`lowlight`/`adaptive`** (the
  color-uncorrected `none` is not a deployable ISP output and is excluded —
  RESEARCH.md §1.2).

**History (superseded proxies — early experiments; only qualitative
conclusions remain valid):**

| Dataset | Illumination | Path | Images | Valid (raw==jpg) |
|---|---|---|---|---|
| COCO_val | normal | `data/coco_val/` | 575 | 347 |
| ExDark_val | low light | `data/exdark_val/` | 491 | 260 |
| SonyNOD | low light (real sensor) | `data/sonynod_test/` | 321 | 321 |

Each dataset consists of `raw_bin/` (pseudo-RAW or shift8 real-RAW),
`images/` (jpg, the resolution source), `labels/` (YOLO txt). Frames whose
`raw_bin` size disagrees with the jpg resolution are skipped (only valid
frames are used).

### 2.2 raw_bin format (SW eval input)
- **Layout:** RGGB Bayer. `(0,0)=R (0,1)=G (1,0)=G (1,1)=B`.
- **Type:** headerless little-endian `uint16` array, length = `W*H`
  (row-major).
- **Scale:** values are 8-bit shifted up (`effective8bit = value >> 8`,
  `SHIFT=8`). Range 0–65280.
- **Resolution (W,H):** parsed from the SOF marker of the same-stem
  `images/<stem>.jpg`.

### 2.3 HW / C-sim input format (canonical hardware path)
- **Layout:** RGGB Bayer (unified with the SW datasets, 2026-07-02).
- **Type:** 12-bit values stored in `uint16`
  (`raw12_to_u8(v) = min(v,4095) >> 4`).
- **Fixtures:** synthetic grid frames (`gen_golden_vectors.py`), scenario
  `NORMAL×3 → LOW_LIGHT×3 → NORMAL×1` + threshold-boundary +
  bright-recovery + odd-dimension.

### 2.4 Label format
- YOLO txt: one line `class cx cy w h` (normalized 0–1, image-size
  independent).
- **Class ids:** both datasets use **COCO-80 ids**
  `{0,1,2,3,5,8,15,16,39,41,56,60}` (12 classes) → detector is
  COCO-pretrained, so **no remap needed**.

### 2.5 Input resolution distribution (measured)
| Dataset | width min/median/max | height min/median/max | Typical sizes |
|---|---|---|---|
| ExDark | 200 / 640 / 3200 | 178 / 499 / 3443 | 640×480, 640×427, 500×375 |
| COCO | 240 / 640 / 640 | 160 / 480 / 640 | 640×480, 640×427, 480×640 |

Variable resolution (not fixed). RESEARCH's 1280×720@30fps is the HW
frame-budget target, separate from the SW dataset inputs.

---

## 3. Pipeline stage specification

### 3.1 ① Scene checker / mode decision (static region)

Input mode ∈ {NORMAL(0), LOW_LIGHT(1), AUTO(2)}.

```text
NORMAL     -> selected_mode = NORMAL
LOW_LIGHT  -> selected_mode = LOW_LIGHT
AUTO       -> dark_ratio = count(dark) / (W*H)
              dark = (raw < dark_pixel_threshold)     # RAW domain, identical in HW and SW (checker.py)
              # dark_pixel_threshold: 16<<8 in raw16 (SW precompute); the HW raw12
              # register value is by convention 256 (= 16<<4) — the "dark16" statistic
              selected_mode = LOW_LIGHT  if  dark_ratio > 0.62  else NORMAL
              (integer compare: dark_count*100 > 62*(W*H))
```

- **Deployment history:** initial 0.40 (2026-07-01) → **C0** dark50>0.80
  (2026-07-02 ver2 recalibration, optimized in Youden's-J terms) → **C1**
  dark16>0.62 (**formally deployed 2026-07-20, gate 4**) — dominates C0 on
  every metric (recall 0.936 vs 0.918, false-trigger 0.089 vs 0.125,
  J 0.847 vs 0.793) with zero HW change (only the `DARK_RATIO_PCT` constant
  + the runtime `dark_pixel_threshold` register value changed). In the same
  deployment the SW mirror (`checker.py`) switched to a **direct raw-domain
  compare**, removing the old luminance<50 approximation (post-demosaic
  decision), matching the HW/calibration domain exactly. Zero mismatch
  against the manifest-precomputed dark16/verdict on 642 real RAW frames
  (SonyNOD 321 + PASCALRAW 321). Details:
  `results/checker-c1-deploy-2026-07-20.md`.
- **Oracle-label revalidation (2026-07-20, gate 2, final gate):** dual-arm
  render (normal/lowlight) + per-frame YOLOv8n detection delta redefines the
  "correct mode" by actual detection improvement rather than dataset
  provenance. Of C1's 154 residual naive-label disagreements, **89.6% are
  label artifacts** (only 10.4% genuine errors). dark16 discriminates "is
  this scene nighttime" well (J 0.847) but "does lowlight processing help
  detection on this frame" poorly (oracle J 0.008) — yet C1's threshold
  keeps its **cost-neutral property (C_miss ≈ C_FA) under the oracle
  labels**, so the conclusion is **no re-tuning needed**. Details:
  `results/checker-oracle-label-gate2-2026-07-20.md`.
- **Hysteresis (scene level) — in fabric since 2026-08-06:** the
  single-frame entry stays stateless, but it now exports two Schmitt band
  compares per frame (`hyst_flags`: above-enter 62% / below-exit 60%,
  δ=2%p) as an ap_vld wire, and the static-region module
  `results/pr_controller/checker_hysteresis.v` owns the mode flip-flop,
  min-dwell (`DWELL_FRAMES`, default 1), and the `pr_trigger` request/ack
  to the PR controller — no PS in the decision path (the PS only observes
  `selected_mode` via AXI4-Lite). This implements the 2026-07-03 adoption;
  the earlier "driver-side policy" wording described the unimplemented
  interim state. History: scheduler-level simulation
  (`tools/scheduler_sim.py`, origin repo) measured narrow band +
  temporal_N=3 → mismatch 0.015, thrashing 0.

### 3.2 ② Baseline ISP core (shared code path, mode-specific BLC) — ver1

**No gain/gamma.** Correction is performed **in the 12-bit RAW domain**, and
the final `>>4` happens in tone (ver1 core idea: precision preservation).
WB/CCM share exactly the same function (`apply_blc_wb12`) between modes, but
**the BLC offset is per-mode** (2026-07-03, from the §11.9 root-cause
result). Per pixel:

```text
1. demosaic (RGGB) -> R,G,B 12-bit (0..4095)     # HW/C-sim: no >>4 here (kept at 12-bit)
2. BLC   : v = clip(v - blc_offset, 0, 4095)     # normal: 256 (16<<4) / low-light: 128 (8<<4, relaxed)
3. WB    : R = clip(R * 286 / 256, 0, 4095)      # Q8 channel white balance (color), mode-independent
           G = clip(G * 256 / 256, 0, 4095)
           B = clip(B * 307 / 256, 0, 4095)
4. CCM   : identity                              # structure kept, no color transform
return   : R,G,B 12-bit  (>>4 and gamma happen in the tone RM)
```

> The SW eval proxy (`isp_pipeline_ver1.py`) is an approximation using the
> 8-bit domain (>>8) + float γ LUT; HW/C-sim is canonical (12-bit, integer
> γ). See §0.

### 3.3 ③ Tone RM slot (mutually exclusive, reconfigurable) — ver1

The tone RM slot **wraps** the baseline core (front/back). tone = exposure
gain (12-bit) → `>>4` → gamma.

```text
tone(v12, gnum, gden) = gamma2( clip(v12*gnum/gden, 0, 4095) >> 4 )
gamma2(v8) = floor(sqrt(255 * v8)) = isqrt(255*v8)     # γ=2.0, integer exact, bit-exact
```

**RM_NORMAL_TONE (NORMAL):** gain **1.25×** (5/4) + gamma2.0. Shape H×W.
(ver1: identity → gain+gamma)

**RM_LOW_LIGHT_TONE (LOW_LIGHT), Policy A:**
```text
front (RAW):  2x2 RAW binning-demosaic, fused — R=top-left, G=avg(top-right,
              bottom-left), B=bottom-right  -> (W/2, H/2) R,G,B triple
core       :  apply_blc_wb12(front output, blc_offset=128)   # §3.2 above, no second demosaic,
                                                             # only BLC relaxed (2026-07-03)
back (tone):  gain **2.0×** (2/1) + gamma2.0
output shape:  H/2 × W/2   (bin_dim(d) = max(1, d/2))
```

**Caution (2026-07-02 fix):** previously the 4 samples were averaged into
one scalar `(p00+p01+p10+p11)/4` and then re-demosaiced as if still Bayer —
color information was already destroyed, producing near-grayscale output
(found by the adversarial review). Fixed to the channel-preserving scheme
above (bit-exact with `_bin_demosaic_rggb16` of `tools/isp_pipeline_ver1.py`).

**Caution (2026-07-03 fix, BLC relaxation):** the root-cause ablation
(§11.6) showed ~70% of the low-light mAP loss comes from BLC (not WB) — the
low-light path's BLC offset was halved from 256 (16<<4) to **128 (8<<4)**.
ExDark mAP 0.0586→0.1043 (+78%, exceeding normal for the first time), COCO
also safe (0.2647→0.2857). HW resources/timing **completely unchanged**
(constant-only change). Details: `results/blc-fix-resynthesis-2026-07-03.md`.

### 3.4 Dataflow ordering decision (ver1)

ver1 (2026-07-02): correction (BLC/WB) is performed **right after demosaic
in 12-bit** (linear, hence equivalent to RAW-domain, preserving precision
until the final `>>4`). Low-light binning happens **on RAW before precision
loss**; gain/gamma sit in the tone RM (after the core) (RESEARCH §4.2).
De-dup invariant maintained (gain/gamma only in tone RMs).

---

## 4. Parameter table (all constants, integer)

| Stage | Parameter | Value | Notes |
|---|---|---|---|
| checker | DARK_RATIO_PCT | **62** (C1, deployed 2026-07-20, gates 4 & 2 closed) — direct dark16 raw-domain compare; the old 80 (C0, dark50, post-demosaic Y<50 approximation) is retired | AUTO→LOW_LIGHT threshold, paired with the `dark_pixel_threshold` register (HW raw12 convention value 256 = 16<<4) |
| baseline core | BLC_OFFSET12 | **32 (= 2<<4)**, normal (recalibrated 2026-07-20; formerly 256 = 16<<4) | 12-bit black level |
| baseline core | BLC_OFFSET12_LOWLIGHT | **32 (= 2<<4)**, low-light (recalibrated 2026-07-20; formerly 128 = 8<<4) — same value as normal (per-mode relaxation unnecessary; 2 is the measured peak of both arms) | 12-bit black level |
| baseline core | AWB_R / G / B | 286 / 256 / 307 | Q8 (/256) white balance — **mode-shared** (per-mode split examined and rejected 2026-08-03, §11.11) |
| baseline core | CCM | identity (256) | placeholder |
| normal tone | GAIN_NORMAL | 5/4 (1.25×) | exposure gain (added in ver1) |
| low-light tone | GAIN_LOWLIGHT | 2/1 (2.0×) | exposure gain |
| tone (shared) | GAMMA | γ=2.0 | `floor(sqrt(255·v))` = isqrt, integer exact |
| raw conversion | RAW12_MAX / `>>4` (HW) | 4095 / 12→8 bit | >>4 happens in tone |
| raw conversion | SHIFT (SW proxy) | 8 | 16→8 bit |
| shape | bin_dim | `max(1, d/2)` | Policy A |

> ver1 (2026-07-02) fully applied: correction in 12-bit RAW domain, normal
> gets gain+gamma, low-light γ4.0→2.0 (relaxed). The SW proxy
> (`isp_pipeline_ver1.py`) is a float γ2.2/2.5, 8-bit approximation (the HW
> integer γ2.0 is canonical) — that file itself was later replaced by
> `baseline_isp_pipeline.py` / `low_light_isp_pipeline.py` / `checker.py`
> (canonical, direct HW-constant mirrors) (2026-07-08,
> `results/isp-pipeline-recalibration-2026-07-08.md`). BLC/checker values
> were recalibrated and deployed by the 2026-07-20 real-RAW
> (SonyNOD+PASCALRAW) campaign — the table above is current canon.
>
> **The full BLC curve (SonyNOD 321, canonical pipeline, YOLOv8n — why 2):**
> `map_isp_sonynod_blcfix_yolov8n.csv` (07-08 recalibration) swept the whole
> range.
>
> | BLC | 0 | 1 | **2 (deployed)** | 4 | 8 | 16 |
> |---|---:|---:|---:|---:|---:|---:|
> | lowlight mAP@[.5:.95] | 0.1965 | 0.2130 | **0.2140** | 0.1759 | 0.1030 | 0.0372 |
> | normal mAP@[.5:.95] | 0.1849 | 0.1932 | 0.1900 | 0.1625 | 0.0918 | 0.0344 |
>
> The curve **peaks at 1–2 and falls off both ways** — excessive BLC (4–16)
> destroys signal, and **BLC=0 is also worse than the deployed value**
> (lowlight −8.2%). **The counterintuitive point (written up 2026-08-04):**
> even at BLC=2, 52–73% of low-light pixels clip to 0
> (`results/lowlight-wb-mode-split-2026-08-03.md` §2), yet removing that
> clipping (BLC=0) makes things *worse*. The reason is that **gamma 2.0 is a
> sqrt and strongly amplifies low values** (8-bit 1→16, 2→23). What BLC=0
> preserves is mostly the sensor black pedestal and dark-current noise,
> which gamma pulls up into visible gray that hurts detection. In other
> words, **the high clipping rate is not a defect — it is acting as noise
> removal.**
>
> **Both per-mode color-correction constants converged on "no split needed"
> (settled 2026-08-04).** BLC was deployed per-mode once (07-03, 256→128)
> but the real-RAW recalibration showed **2 is the peak for both** (07-20);
> WB was re-tuned per-mode on real RAW but rejected for **mAP
> non-response** (08-03, §11.11). The real difference between
> normal/low-light is therefore **structural, not in color-correction
> constants** (2×2 binning or not, exposure gain 1.25× vs 2.0×) — the
> baseline core's color-correction parameters are robust to illumination.

---

## 5. Output specification

### 5.1 Pixel format
- **packed RGB888**, `uint32`, `0x00RRGGBB` =
  `[31:24]=0x00, [23:16]=R, [15:8]=G, [7:0]=B`.
- 32-bit packing (power-of-two width) instead of 24-bit, for AXI DMA
  alignment.

### 5.2 Shape policy (Policy A, shape-changing)

| mode | output shape |
|---|---|
| NORMAL | H × W (same as input) |
| LOW_LIGHT | ⌊H/2⌋ × ⌊W/2⌋ (min 1) |

- `rgb_out` buffer capacity ≥ `W*H` (low-light uses at most that).
- (Policy B = restoring H×W via upsample/pad only if a fixed DPU ABI
  requires it; §11, future.)

### 5.3 Output metadata

**Four separate scalar output pointers** (2026-07-02 fix, see §6.1):
`out_width` (actual output width) · `out_height` (actual output height) ·
`selected_mode` (0=NORMAL, 1=LOW_LIGHT; AUTO resolved) · `selected_rm`
(0=RM_NORMAL_TONE, 1=RM_LOW_LIGHT_TONE). In HW each is exposed as an
AXI-Lite read-back register; the DPU front end must know the output
size/mode.

> **Previous design (struct pointer, retired by the adversarial review):** a
> single `DfxIspResult*` struct was declared `s_axilite`, but s_axilite is a
> slave-only control interface and no artifact ever verified that struct
> field write-back actually synthesizes (cosim also never completed).
> Replaced with individual scalar pointers — the well-established Vitis HLS
> pattern for post-completion read-back, hence trustworthy.

---

## 6. Interface specification

### 6.1 HLS top function

```c
extern "C" void dfxisp_accel(
    const uint16_t* raw_bayer,     // input RAW RGGB (W*H)
    uint32_t*       rgb_out,       // output RGB32 (capacity >= W*H)
    int             width,
    int             height,
    int             mode,          // DfxIspMode
    uint16_t        dark_pixel_threshold,  // AUTO checker RAW threshold
    int*            out_width,     // output metadata (individual scalar pointers)
    int*            out_height,
    int*            selected_mode,
    int*            selected_rm,
    int*            hyst_flags);   // Schmitt band flags (ap_vld wire, 2026-08-06)
```

AXI: `raw_bayer`/`rgb_out` = `m_axi` (gmem0/gmem1); the remaining scalar
arguments, the 4 metadata outputs, and `return` = `s_axilite` (control).
`hyst_flags` is the exception: an `ap_vld` fabric wire pair
(`hyst_flags[31:0]` + `hyst_flags_ap_vld`, one pulse per completed frame)
feeding `checker_hysteresis.v` directly — not an s_axilite register
(§3.1; bit 0 = above-enter 62%, bit 1 = below-exit 60%).

### 6.2 Golden vector CSV format (verification contract)

Header: `case,in_w,in_h,mode,threshold,out_w,out_h,sel_mode,sel_rm,kind,idx,val`
- Per-case metadata repeated + `kind=raw` (input RAW, val = decimal) /
  `kind=rgb` (expected output, val = `0xRRGGBB`).
- Input pixel count (in_w×in_h) and output pixel count (out_w×out_h) can
  differ, so the two row kinds are separate.

---

## 7. Hardware / DFX specification

| Item | Value |
|---|---|
| Target device | ZCU104, `xczu7ev-ffvc1156-2-e` |
| Synthesis tool | Vitis HLS 2024.1 |
| Clock target | 5.0 ns (200 MHz) |
| static region | AXI/control wrapper, checker/mode FSM, Schmitt mode arbiter (`checker_hysteresis.v`, 2026-08-06), DFX/PR controller, output/metadata packer (**the baseline ISP core is NOT static** — see the RP-boundary row below) |
| RM slot (reconfigurable) | RM_NORMAL_TONE / RM_LOW_LIGHT_TONE (mutually exclusive, identical port signature = the DFX contract) |
| **RP boundary (as-built, important)** | The synthesized RP (`rm_normal_tone_top`/`rm_low_light_tone_top`) wraps **the full per-mode pipeline demosaic→BLC→WB→tone, not just tone**. `apply_blc_wb12()` is shared **only at the source level** and is duplicated per RM in silicon. 3 partition pins. Evidence: `results/design-limitations-2026-07-03.md` §4.3, `deliverables/verilog/rm_*_tone_top/`, `results/dfx-reimplementation-2026-08-01.md`. A finer split (making the baseline core a genuinely static module) **has not been attempted** |
| Switch policy | per scene (not per frame): fabric Schmitt δ=2%p + min-dwell in `checker_hysteresis.v` → `pr_controller.trigger` request/ack; PS observes only (§3.1) |
| Reconfiguration latency | theoretical drain+ICAP+warm-up breakdown: **peak 1.72 ms / typical 6.87 ms** (spec-derived, not board-measured). Details `results/pr-latency-breakdown-2026-07-02.md`. Driver/FSM overhead is TODO (board) |

**Experiment arms:** Arm1 (static baseline + normal tone) / Arm2
(register-only adaptation, no DFX) / Arm3 (DFX swaps the tone RM slot).
Ablations: post-RGB8 gain/lift, dfx_bin, dfx_fp (`dfxisp_rm.*`).

---

## 8. Verification specification (bit-exact propagation chain)

| Lv | Target | Tool | Status |
|---|---|---|---|
| L0 | Python golden (reference) | `gen_golden_vectors.py` | ✅ |
| L1 | HLS C-sim (C++ == Python) | `make verify` | ✅ 646 px bit-exact |
| L1.5 | C-synthesis (real Vitis HLS) | `DFXISP_HLS_FLOW=csynth` | ✅ measured (§10) |
| L2 | C/RTL co-sim (synthesized RTL == C TB) | `DFXISP_HLS_FLOW=cosim` | 🟡 RTL runs pass (7/7 transactions); the automated bit-exact compare never completes due to a tool-harness SIGSEGV (`results/stage4-hw-synthesis-2026-07-02.md` §6b) |
| L3 | RTL wrapper sim (AXI-Stream) | Vivado xsim | ⬜ |
| **L4** | **DFX implementation + pr_verify (fabric-only, non-project batch flow)** | **Vivado 2024.1** | **✅ pr_verify PASS, real partial bitstreams generated** (`results/stage5-dfx-implementation-2026-07-02.md`) |
| L5 | Board HIL (real PR, PS/DDR integration, measured power & PR latency) | ZCU104 | ⬜ the only remaining stage |

**Architecture gates (all PASS, `reports/latest.md`):** baseline core
bit-exact / RM_NORMAL_TONE / RM_LOW_LIGHT_TONE / mutually exclusive RM
selection / no gain·gamma duplication / shape policy (LOW_LIGHT H/2×W/2).

---

## 9. Evaluation specification (detection accuracy)

- **Detectors:** YOLOv8n, YOLOv8s (ultralytics `val`, imgsz=640,
  mAP@[.5:.95] & @50), SSDLite-MobileNetV3 (torchvision, COCOeval) as
  cross-checks. The exact Vitis-AI `tf_ssdmobilenetv1` cannot run in this
  environment (weights unavailable, TF1.15) → reserved for the **board DPU
  end-to-end stage**.
- **Condition table A–G:** ExDark{A none, B normal, C lowlight} /
  COCO{D none, E normal, F lowlight} / G adaptive (checker per-frame
  selection).
- **Primary metric:** mAP@[.5:.95] (0–1 fraction, ×100 = %). Judged by
  **arm ordering** (guardrail).

---

## 10. Performance / resources (Stage 4 measurements, 2026-07-02 — Vitis HLS 2024.1 C-synthesis)

**Measurement complete (C-synthesis + real Vivado DFX implementation,
xczu7ev, 2024.1) — re-synthesized with the 2026-07-02 20:33 KST
adversarial-review fixes (chroma-preserving binning-demosaic + scalar
metadata pointers, commit `a2d1b6d`).** The unified top (`dfxisp_accel`,
both tone RMs resident, runtime mode select, no DFX) is **Arm2
(register-only)**. RM_NORMAL_TONE/RM_LOW_LIGHT_TONE were re-implemented as a
real Reconfigurable Partition with **pr_verify PASS** and regenerated
partial bitstreams, giving the **Arm3 (DFX) fabric-only measurement**
(PS/DDR not integrated; absolute power and PR latency (ms) are board-only).
**Arm1 (static baseline-only) was also measured on 2026-08-04** — see the
table and §10.1. Details: `results/stage4-hw-synthesis-2026-07-02.md`
(csynth), `results/stage5-dfx-implementation-2026-07-02.md` (DFX
implementation).

| Metric | **Arm1 (static, measured 2026-08-04)** | **Arm2 (register-only, measured)** | **Arm3 (DFX, measured — final with BLC fix + pblock fix, 2026-07-03)** |
|---|---|---|---|
| LUT / FF / BRAM / DSP | **5,202 / 3,797 / 4 / 12** | **8,264 / 5,536 / 9 / 24** (unchanged by the BLC fix) | config1 (static+RM_NORMAL) routed: LUT 3,972 / BRAM 1.5 tile / DSP 12; config2 (static+RM_LOW_LIGHT) routed: LUT 2,927 / BRAM 3.5 tile / DSP 8 (`results/blc-fix-resynthesis-2026-07-03.md`) |
| Fmax @5.0ns | **273.97 MHz** (critical path 3.650 ns — same as Arm2) | **273.97 MHz** (critical path 3.650 ns, identical before/after the fixes) | **200 MHz constraint met** in the old pblock (X0Y0:X1Y0) (WNS config1 +0.619 ns / config2 +1.930 ns, 2026-07-03; `results/dfx-vivado-considerations-2026-07-03.md` §6) — **timing in the new pblock (X1Y0:X2Y0) still TODO** |
| pr_verify | — | — | **✅ PASS** (static fully identical even after BLC fix + pblock fix; partition pins **3** — differs from the old floorplan's 15, cause uninvestigated) |
| full bitstream size | — | — | **19,311,211 bytes ≈ 19.3 MB** (unchanged) |
| partial bitstream size | — | — | **1,447,424 bytes ≈ 1.38 MB** (new pblock; **2.11×** the old 686,664 B — the direct price of doubling pblock capacity, `results/blc-fix-resynthesis-2026-07-03.md` §5) |
| reconfiguration latency (ms) | — | — | recomputed for the new bitstream: peak **3.618 ms** / typical **14.473 ms** (2.11× the old 1.72/6.87 ms); measured incl. driver/FSM is TODO (board) |
| normal-mode power (W) | TODO | TODO | TODO (needs board measurement) |

### 10.1 Arm1 vs Arm2 — "the cost of adaptivity" (new 2026-08-04)

**What Arm1 is:** Arm1 (static baseline + normal tone) is algorithmically
`run_normal()` itself (demosaic → BLC/WB/CCM → gain 1.25× + gamma), and its
AXI wrapper already existed as `rm_normal_tone_top`. So **Arm1 was not a new
design but an already-synthesized top; the `TODO` in the table was a
bookkeeping gap, not a measurement gap.** On 2026-08-04, Arm1 and Arm2 were
re-synthesized from the **same source (BLC 2/2) in the same tool session**
to settle this (Arm1 exactly matches the 07-03 numbers — reconfirming
§11.10's observation that constant changes don't alter csynth).

> **Side finding (repo consistency):** during this re-synthesis, the
> committed `reports/csynth/dfxisp_accel_ver1_csynth.rpt` read **LUT 11,217
> / FF 7,008 / DSP 30**, disagreeing with the 8,264 / 5,536 / 24 this table
> has been citing — the report predating the 2026-07-02 adversarial-review
> fixes (16:39) had been left in place, while the same-day 20:33
> re-synthesis result (§11.5) was reflected only in this table. Replaced
> with the 2026-08-04 measurement. **The table's numbers were canonical and
> the report was stale** (the re-synthesis reproduced the table's values
> exactly).

| Metric | Arm1 | Arm2 | Δ (cost of adaptivity) |
|---|---:|---:|---:|
| LUT | 5,202 | 8,264 | **+3,062 (+58.9%)** |
| FF | 3,797 | 5,536 | +1,739 (+45.8%) |
| BRAM | 4 | 9 | +5 (+125%) |
| DSP | 12 | 24 | +12 (+100%) |
| Fmax | 273.97 MHz | 273.97 MHz | **0 (identical)** |

**Breakdown of the +3,062 LUT** (consistent with Arm2's instance breakdown,
sum verified):

| Component | LUT | Share | Removable via DFX? |
|---|---:|---:|---|
| `run_low_light` datapath | 2,110 | 69% | **Yes** (the RM swap target) |
| checker + mode mux + extra control registers | 952 | 31% | **No** (always static) |

**Implication (Goal 2):** the price of adaptivity is **+58.9% LUT** over the
static ISP, of which **the DFX-recoverable ceiling is 2,110 LUT = 25.5% of
Arm2's total LUT**. The remaining 31% (checker/mux) cannot be removed by any
reconfiguration scheme — this number defines **the theoretical ceiling of
the net DFX gain**. Timing is identical across all three arms, confirming
adaptivity does not sacrifice Fmax.

### 10.2 Arm1/Arm2 post-route measurements (2026-08-04)

§10.1's Δ was a csynth estimate. Arm1 and Arm2 were **actually placed and
routed in Vivado**, moving to the post-route axis (fabric-only tie-off
wrapper, same technique as Arm3: only `ap_clk`/`ap_rst_n` leave the chip;
the rest of the 110 ports are tied off + XOR-observed outputs).

| Metric | Arm1 | Arm2 | Δ (cost of adaptivity) |
|---|---:|---:|---:|
| CLB LUT | **3,363** | **4,768** | **+1,405 (+41.8%)** |
| CLB Register | 4,057 | 5,369 | +1,312 (+32.3%) |
| Block RAM Tile | 1.5 | 3.5 | +2 |
| DSP | 12 | 23 | +11 |
| WNS @5.0ns | +0.682 ns | +0.893 ns | both meet the constraint |

**Versus the csynth estimate:** the cost of adaptivity was +58.9% LUT in
csynth but **+41.8%** post-route — csynth overestimates the overhead. The
direction is unchanged.

**Robustness check (measurements insensitive to flow):** the same design
implemented three ways — (a) in-context flat synthesis, (b) the same OOC
synthesis + DCP link as Arm3, (c) (b) plus config1's pblock
(`CLOCKREGION_X1Y0:X2Y0`, CONTAIN_ROUTING, EXCLUDE_PLACEMENT) — gives Arm1
3,400 / 3,363 / 3,364 LUT and Arm2 4,769 / 4,768 LUT, **within 1%**. The
numbers do not move with flow choice or floorplan constraints.

### 10.3 All three arms on one axis (2026-08-04) — canonical DFX-saving numbers

**Published config1 reproduces (confirmed).** A 2026-08-04 re-run with the
unmodified `scripts/dfx/dfx_flow.tcl` produced **LUT 2,630 / BRAM 1.5 /
DSP 12**, exactly matching the 08-01 published numbers.

> **Correction record:** the first draft of this section (an earlier commit
> the same day) claimed "published config1 does not reproduce" — **that was
> wrong.** The RTL had been moved into the DFX flow path with `cp *.v`,
> which **dropped the gamma-LUT ROM init `.dat` files**, and synthesis ran
> without them (hence BRAM 1.5→1, DSP 12→10). Re-copying with the `.dat`
> files matched immediately. **There is no reproducibility problem.**
> Lesson: HLS `syn/verilog` output includes ROM `.dat` files besides `.v` —
> move it wholesale.

**Resource comparisons are unified on the flat axis.** Partition builds
(`HD.RECONFIGURABLE`) report ~24% lower LUT than flat on the identical
netlist (RM_NORMAL: flat 3,363 vs RP 2,544). The pblock was ruled out as the
cause by the §10.2 robustness check (c). The cause appears to be a
difference in how partitioned designs are implemented/reported and is
**unresolved**, but the practical rule is clear — **never subtract partition
numbers from flat numbers.** Therefore:

| arm / mode | Resident logic | CLB LUT | vs Arm2 |
|---|---|---:|---:|
| Arm1 (static, normal-only) | RM_NORMAL | 3,363 | — |
| **Arm2 (register-only, both resident)** | RM_NORMAL + RM_LOW_LIGHT + checker | **4,768** | baseline |
| **Arm3 (DFX) — normal mode** | RM_NORMAL | **3,363** | **−1,405 (−29.5%)** |
| **Arm3 (DFX) — low-light mode** | RM_LOW_LIGHT | **2,344** | **−2,424 (−50.8%)** |

(All placed & routed with the same tie-off wrapper, same OOC+DCP flow, same
part; post-route measurements. RM_LOW_LIGHT: Reg 3,450 / BRAM 3.5 / DSP 8,
WNS +1.661 ns.)

**Goal 2 conclusion:** versus always-on (Arm2), DFX saves **29.5% LUT in
normal mode and 50.8% in low-light mode**. The low-light saving is larger
because 2×2 binning means only H/2×W/2 is processed — the datapath is
smaller to begin with. §10.1's csynth-based estimate ("DFX recovery ceiling
= 25.5% of Arm2") **was an underestimate** — the post-route measurement
exceeds it.

> **What partition builds are for:** config1/config2 (2,630/1,843 LUT) are a
> **different axis** from the table above and must not be mixed into
> resource comparisons. Cite them only for their unique outputs: **partial
> bitstream size, `pr_verify`, partition pins, reconfiguration latency**
> (§10 table).

Arm2 instance breakdown (inside the unified top; reference point for
estimating the net DFX gain, post-re-synthesis):

| instance | BRAM | DSP | FF | LUT |
|---|---|---|---|---|
| RM_NORMAL_TONE (`run_normal`) | 1 | 12 | 1,785 | 3,108 |
| RM_LOW_LIGHT_TONE (`run_low_light`) | 5 | 9 | 1,295 | 2,110 |
| AXI/control infrastructure | 3 | 3 | 2,456 | 3,046 |

**Utilization (vs xczu7ev):** BRAM 1%, DSP 1%, FF 1%, LUT 4% — very
comfortable.

Expectation (H3): in normal mode, Arm3 fabric/power < Arm2 (low-light block
not resident). The `run_low_light` instance (5 BRAM / 9 DSP / 1,295 FF /
2,110 LUT — much smaller than pre-fix) is the estimated ceiling of what DFX
can remove — confirming Arm1/Arm3 requires the separate static baseline-only
top synthesis plus the Vivado DFX floorplan (PR / pr_verify / partial
bitstreams).

**Standalone RM top measurements (Stage 5 prep, post-re-synthesis):**
`RM_NORMAL_TONE`/`RM_LOW_LIGHT_TONE` synthesized separately as tops with
their own AXI infrastructure (a more realistic estimate of DFX partial
bitstream size):

| top | BRAM | DSP | FF | LUT | Fmax |
|---|---|---|---|---|---|
| `rm_normal_tone_top` | 4 | 12 | 3,797 | 5,202 | 273.97 MHz |
| `rm_low_light_tone_top` | 8 | 9 | 3,243 | 4,204 | 273.97 MHz |

`rm_normal_tone_top` is internally unchanged by the fixes, so it matches the
earlier measurement exactly (cross-validation). `rm_low_light_tone_top`
shrank by removing the second demosaic call: LUT −41.3% / FF −31.5% /
DSP −40.0%.

> **Note (earlier optimization history):** in the first csynth, `gamma2()`
> used a runtime integer sqrt (iterated division), inflating resources >5×
> (total FF 58,655 / LUT 52,053). Replacing it with a 256-entry ROM LUT gave
> the numbers above (FF −88%, LUT −78%). Details in the Stage 4 doc.

---

## 11. Constraints, assumptions, known issues

1. **SW eval is a proxy:** pseudo-RAW is inverse-transformed from
   already-ISP'd JPEGs, RGGB nearest, n=71–113, CPU. Judged by arm ordering,
   not absolute values.
2. **Bayer unification (2026-07-02):** HW/C-sim and SW are both RGGB. The
   only remaining difference is the RAW bit representation (HW 12-bit `>>4`
   vs SW shift8 `>>8`). (The "GRBG vs RGGB" caveat in older experiment
   reports predates the unification.)
3. **Key Stage 1–3 finding:** ver0 (normal=identity,
   low-light=bin+gain+gamma-4.0) scored **below no-processing (none)** on
   all three detectors and both datasets = fails the mAP guardrail.
   **ver1 applied (2026-07-02):** (a) RM_NORMAL_TONE = gain 1.25×+gamma
   **done**, (b) low-light γ4.0→2.0 relaxation **done**, correction in
   12-bit RAW domain **done**. ver1 improved the low-light arm by ~20% but
   **none still wins** (SW proxy ceiling) → direction A stands (mAP asks
   for minimal processing; DFX/RM is justified by resources/power). Remaining
   revisions: (c) checker dark-level recalibration, (d) Policy B /
   denoise-style RM. Final verdict belongs to the board DPU + real RAW.
4. **No fabricated HW numbers:** never invent §10 / L2–L5 numbers without
   Vivado/board (TODOs stay TODO).
5. **Adversarial-review fixes (2026-07-02):**
   `/codex:adversarial-review --base 0e433f9` found and fixed two defects:
   (a) low-light binning averaged 4 samples into a scalar then
   re-demosaiced, destroying color (the golden model mirrored the same bug,
   so the bit-exact test couldn't catch it; the reported lowlight mAP
   evidence measured a different, color-preserving algorithm) — fixed by
   fusing binning-demosaic, bit-exact with `_bin_demosaic_rggb16` (§3.3).
   (b) metadata used the unproven struct-pointer s_axilite pattern —
   replaced with 4 scalar output pointers (§5.3/§6.1). **`make verify` 646
   px bit-exact maintained; new color-preservation regression test added.**
   **2026-07-02 20:33 KST: Vitis HLS csynth + Vivado DFX re-implementation
   completed with the fixed sources (pr_verify PASS, bitstream sizes
   byte-identical). §10 updated to the new numbers.**
6. **Why the low-light RM doesn't lift mAP — ablation measured
   (2026-07-02):** the earlier guess "H/2 resolution loss is the cause"
   (experiment-report §5.2) was tested with a 5-stage ablation
   (`tools/isp_pipeline_ablation.py`): **the cause depends on illumination.**
   On ExDark (low light), resolution loss contributes only −1.4% and
   **BLC/WB (the baseline core shared by both RMs) accounts for ~70% of the
   loss** (−49.6 %p) — not an RM-specific problem but the shared core's
   static WB gains distorting low-light color statistics. Conversely on COCO
   (normal light), resolution loss dominates (−11.7%; BLC/WB only −1.9%).
   Details: `results/lowlight-rm-map-rootcause-2026-07-02.md`.
7. **DFX reconfiguration latency — theoretical stage breakdown
   (2026-07-02):** decomposed into drain (measured, 74–171 cycles), ICAP
   transfer (686,532 B payload ÷ AMD UG570 ICAPE3 spec bandwidth: peak
   1.72 ms / typical 6.87 ms), and warm-up (measured). ICAP transfer is
   >99.9% of the total (drain/warm-up are µs; ICAP is ms). Driver/FSM
   overhead can't be computed with no PR controller synthesized — stays TODO
   (board). **A more precise Vivado (XSIM) simulation was attempted
   (streaming the real partial bitstream into the ICAPE3 UNISIM model), but
   an isolated testbench never produced a trigger→completion signal (SYNC
   succeeded; all 3 completion methods failed — cause/limit analysis
   included, recorded honestly)** — the payload word count (171,633) was
   verified directly from the file and incorporated. Details:
   `results/pr-latency-breakdown-2026-07-02.md`,
   `results/pr-latency-vivado-sim-2026-07-02.md`.
8. **Design-limitations synthesis + practical DFX considerations
   (2026-07-03):** limits across five layers (algorithm / SW eval / HW
   synthesis / DFX implementation / simulation) consolidated
   (`results/design-limitations-2026-07-03.md`). The same-day
   timing-constrained re-implementation obtained real WNS (§10 Fmax row),
   found that the pblock effectively covered only one clock region (one of
   the two contributed 0.06%), and confirmed the design contains no
   `ICAPE3`/`STARTUPE3` at all — i.e. no PR controller exists yet. The full
   Vivado DFX troubleshooting set (I/O pin overflow, SNAPPING_MODE,
   black-box+lock methodology, DRC workarounds) is compiled as a checklist.
   Details: `results/dfx-vivado-considerations-2026-07-03.md`. Both
   documents were restructured into an actionable step-by-step strategy:
   `results/improvement-strategy-2026-07-03.md` (the only genuine blocking
   item is the missing PR controller — solve that and the rest can proceed
   in parallel).
9. **Phase 0–2 immediate execution (same-day follow-up):** all 6 items
   executed — independent golden-model cross-check gate (folded into
   `make verify`), the pblock clock-region skew cause pinned down (X0 column
   Y0–Y3 = PS macros, 0 SLICEs), the low-light WB/BLC split ablation
   (**BLC relaxation is the real winner** — ExDark mAP 0.062→0.150, 42%
   above normal, harmless on COCO), first-pass PR controller FSM design +
   simulation (word-count-based completion sidesteps the previous day's
   PRDONE limitation; trigger→completion 1.716 ms measured —
   cross-validating the spec-derived estimate), pblock re-floorplan
   (X1Y0:X2Y0, 2× capacity: LUT 8,640→19,200). Details:
   `results/phase0-2-execution-2026-07-03.md`.
10. **BLC relaxation made canonical + full re-synthesis (same-day
    follow-up):** item 9's ablation winner (BLC relaxation) applied to the
    canonical `src/dfxisp_accel.cpp` / `gen_golden_vectors.py` /
    `isp_pipeline_ver1.py` (`apply_blc_wb12` gains a `blc_offset` parameter;
    low-light only 128, normal stays 256). Standard-sample re-measurement
    (n=71/80): ExDark lowlight 0.0586→**0.1043** (+78%, **above normal for
    the first time**), COCO 0.2647→0.2857 (+8%, harmless). All three HLS
    csynth targets resource-identical (pure constant change). Vivado DFX
    re-implemented together with item 9's pblock fix: pr_verify stays PASS
    but the **partial bitstream grows 2.11×** (686,664 B→1,447,424 B) — the
    direct price of doubling pblock capacity (reconfiguration latency also
    2.11×: peak 1.72 ms→3.62 ms). Details:
    `results/blc-fix-resynthesis-2026-07-03.md`.
11. **Per-mode low-light WB split — examined and rejected (2026-08-03,
    final real-RAW verdict):** as a sub-question of the proposal "move
    gain/gamma into the tone RM and split normal/low-light into independent
    modules", low-light WB retuning was measured. **The diagnosis was
    confirmed** — the deployed shared WB (286/256/307) is 0.92× the B gain
    PASCAL (bright) wants (nearly exact) but only **0.50×** what SonyNOD
    (low light) wants; the two conditions differ **1.84× in B** (gray-world
    measurement, n=321×2). **But detection does not respond** — the mAP
    spread across the whole range from WB removal to 2× overcorrection is
    **0.0020, 1/44 of the BLC lever (0.0876)**, and channel-split
    experiments disproved the effect (R-only −0.05% / B-only −0.19% / both
    +0.61% = a super-additive pattern with no physical mechanism = mAP
    jitter). Per-channel global gains are a diagonal linear transform that
    moves no edges/shapes, and WB sits **after** BLC clipping so it cannot
    act on the 52–73% of low-light pixels already at 0. **Decision: no
    split** (HW constants unchanged; no re-synthesis or golden regeneration
    needed). WB has now been tested three times (07-02 combined / 07-03
    split / 08-03 real RAW), all converging on "not a lever" — no further
    experiments needed. As a side product it filled the saturation-rate gap
    §1.4 had left unmeasured (max 2.13%; the feared 11.25% oversaturation
    does not reproduce at current parameters). Details:
    `results/lowlight-wb-mode-split-2026-08-03.md`.
12. **RP-boundary wording corrected (2026-08-04, doc-implementation
    mismatch resolved):** this document's earlier conceptual diagram read as
    "baseline core is static; only the tone RM is reconfigurable", but **the
    actually synthesized RP wraps the whole per-mode pipeline
    (demosaic→BLC→WB→tone)** — `apply_blc_wb12()` is shared only at the
    source level and duplicated per RM in silicon. The fact itself was
    recorded in `design-limitations-2026-07-03.md` §4.3 but never reflected
    in the canonical spec, contradicting §7 — made explicit in §7's "RP
    boundary" row on 2026-08-04. **Not a functional bug but a documentation
    debt**; `pr_verify` continues to PASS. The remaining option (making the
    baseline core genuinely static = shrinking the RP to tone only) stays
    undecided as `STRATEGY.md` open question #4.

---

## 12. Glossary

| Term | Meaning |
|---|---|
| DFX / DPR | Dynamic Function eXchange / partial reconfiguration |
| RM | Reconfigurable Module (the unit swapped by a partial bitstream) |
| tone RM slot | **(v1 only)** the mutually exclusive reconfigurable region holding gain/gamma/binning. In v2 (RESEARCH.md §0) the RM boundary widens to the whole ISP datapath and this concept is superseded |
| baseline ISP core | **(v1 only)** the shared demosaic+BLC+WB+CCM stage (12-bit, wrapped by tone), no gain/gamma. In v2 this shared static stage disappears and each RM owns the whole pipeline (RESEARCH.md §0/§3) |
| Policy A / B | shape-changing (H/2×W/2) / shape-preserving (upsample-pad) |
| guardrail | an RM is adopted only if mAP ≥ the baseline (e.g. none/register-only) |
| arm | experiment comparison group (static / register-only / DFX / ablation) |

End of document — keep in sync with `RESEARCH.md`, `src/dfxisp_accel.cpp`,
and `tools/*` when anything changes.

# dfxisp_accel.cpp — design background & history notes

Design-background documentation split out of the source comments of
`dfxisp_accel.cpp`. Nothing here changes code behavior; this is the record of
*why* the constants and structure are what they are.
(Origin: haengmini/dfxisp commit `ea6c2de` source header comments.)

## 1. File overview

DFXISP core C-sim — shared baseline ISP core + mutually exclusive tone RM
slot. Integer-only arithmetic; bit-exact mirror of the Python golden model
(`tools/gen_golden_vectors.py` in the origin repo).

Major update history:
- **2026-07-20** — BLC recalibration 16/8 → 2/2, checker C1 deployed
  (DARK_RATIO_PCT 80 → 62)
- **2026-07-03** — low-light-only BLC relaxation (superseded by the 07-20
  recalibration)
- **2026-07-02** — two adversarial-review fixes (§3)

## 2. Rationale for the BLC / checker constants

### BLC_OFFSET12 = BLC_OFFSET12_LOWLIGHT = 2<<4 (black level 2)

Recalibrated 2026-07-20 (approved and deployed). Real-sensor RAW sweeps
(SonyNOD 321 + PASCALRAW 321 frames, BLC ∈ {0,1,2,4,8,16}, canonical
gamma-2.0 pipeline) put the **mAP peak at BLC 1–2 for every arm and every
split**. The previous values 16 (normal) / 8 (low-light) cost up to 5.7× mAP
on night data. Both modes now share the measured peak value 2, superseding
the 2026-07-03 per-mode relaxation.

How we got there: the 2026-07-03 root-cause ablation split the earlier
"BLC/WB" bucket into BLC-only / WB-only / WB-skip variants and showed the
**black-level offset — not the white-balance gain — is the driver**: relaxing
BLC alone (16 → 8) recovered ExDark mAP from 0.062 to 0.150 (+42% over the
normal arm), while WB relaxation/skip did nothing (+7% / −0.5%). COCO stayed
neutral-to-positive (+5%), so the change is safe outside its target condition
too. This is why `apply_blc_wb12()` has a `blc_offset` parameter; WB/CCM
remain the literal same code path for both modes.

### DARK_RATIO_PCT = 62 (checker C1)

Deployed 2026-07-20 (gate 4). The **dark16 ratio > 0.62** rule replaces the
2026-07-02 C0 rule (dark50 > 0.80). C1 dominates C0 on every metric: recall
0.936 vs 0.918, false-trigger 0.089 vs 0.125, Youden J 0.847 vs 0.793 — with
**zero RTL change**, because the dark-pixel threshold is the runtime
`dark_pixel_threshold` AXI-lite register:

- **The driver must now write 256** (= 16<<4 in this raw12 domain; the
  equivalent in the dataset raw16 representation is 16<<8 = 4096).
- At compile time only the ratio constant (62) changes.

Real-sensor validation (gate 3): SonyNOD recall + PASCALRAW false-trigger
rate C0 92.9% → C1 41.8%. The Schmitt hysteresis band (Δ = 2 %p) of the C1
spec is driver-side policy state (one mode flip-flop); the single-frame rule
here stays a pure threshold compare.

## 3. The two adversarial-review fixes (2026-07-02)

(1) **Chroma-collapse bug (high severity):** the old low-light front end
averaged all 4 RGGB samples of a 2×2 cell into one scalar, then re-demosaiced
that grid as if it were still Bayer-patterned — chroma was structurally
destroyed before AWB/gain ever ran. Worse, the golden model mirrored the same
bug, so `make verify`'s bit-exact test could never catch it (HLS matched its
own wrong golden perfectly). Meanwhile the reported SW mAP evidence used a
*different*, chroma-preserving algorithm (R = top-left, G = avg(top-right,
bottom-left), B = bottom-right), so it was never evidence for what this file
actually computed.
**Fix:** `compute_binned_rgb_row()` performs a fused 2×2 binning+demosaic in
one step, extracting true per-channel R/G/B directly (bit-exact with the SW
implementation), then feeds the shared `apply_blc_wb12()` core — no second
(Bayer-assuming) demosaic pass. This removed the whole 3-row sliding-window
helper family (binning-demosaic only needs the 2 raw rows of its own cell).

(2) **Metadata RTL visibility (medium severity):** a `DfxIspResult*` struct
pointer was declared `s_axilite` — an interface mode meant for lightweight
slave-side register access, not memory-writing struct output — and no
artifact ever confirmed the four fields synthesize as separately addressable
registers. Replaced with four separate scalar `int*` output pointers
(out_width / out_height / selected_mode / selected_rm), the standard,
well-supported Vitis HLS idiom for post-completion status registers (same
pattern as the rm_*_top entry points).

## 4. Earlier (ver1) history

- RAW-domain-first reordering: baseline core = demosaic → BLC → WB → CCM in
  12-bit; RM_NORMAL_TONE gained gain+gamma instead of identity;
  RM_LOW_LIGHT_TONE = gain 2.0x + gamma back stage.
- `gamma2()`: moved from a runtime Newton's-method isqrt (which dominated
  csynth resources — 28.6k FF / 25.4k LUT for run_low_light) to a 256-entry
  ROM table generated once in Python. `std::array`/constexpr was tried first,
  but the gcc-8.3.0 STL bundled with Vitis HLS rejects `<array>` under
  `-std=c++17`; a plain C array avoids that.
- Bayer pattern RGGB (unified with the SW dataset convention).

**Ordering rule:** the tone RM slot wraps the shared baseline core. Low-light
binning-demosaic runs on RAW (before precision loss); gain/gamma exist only
in the tone stages and never inside the baseline core (no duplication —
de-dup rule).

## 5. The two standalone RM tops (rm_normal_tone_top / rm_low_light_tone_top)

Stage 5 prep (2026-07-02): separately synthesizable entry points so each DFX
Reconfigurable Module candidate gets its **own** resource/timing numbers
(the sub-instance breakdown inside the unified `dfxisp_accel()` top is not
enough — SPEC.md §10). Same translation unit, so they call the
anonymous-namespace helpers directly; behavior is bit-exact identical. They
are synthesis-only (csynth "synth" flow, no testbench) and are not exercised
by `make verify`.

**The two tops' port lists must match exactly** (same argument types, order,
count) — DFX requires identical port lists across the RM implementations of
one Reconfigurable Partition ("identical downstream interface contract").
out_width/out_height always equal width/height in rm_normal_tone_top (shape
preserved).

## 6. The story behind the cosim `depth=` hint

`depth=` in `#pragma HLS INTERFACE m_axi ... depth=2048` is only a memory-
model sizing hint for C/RTL cosim's m_axi bus functional model; it does not
affect synthesized RTL behavior (real depth is width*height at runtime).

- `depth=1920*1080` (full design envelope): SIGSEGV in ENTER_WRAPC (likely a
  wrapc harness stack overflow)
- `depth=1024`: got past that but SIGSEGV'd in ENTER_WRAPC_PC (post-check)
  after all 7 test transactions completed — likely too small for the
  *cumulative* address span cosim's BFM uses across all calls in one session
  (7 calls × up to 256 px ≈ 1800)
- `depth=2048`: adopted, with headroom above the current fixture set

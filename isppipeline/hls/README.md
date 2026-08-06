# DFXISP HLS C-sim scaffold

Repo-local path: `isppipeline/hls/`. Canonical architecture doc:
`RESEARCH.md` at the origin-repo root.

> **Note for this extracted repo (JNU_DFXISP_FPGA):** references to
> `tools/*.py` and `results/*.md` below refer to the origin repo
> haengmini/dfxisp. `tools/` is not included here, so `make verify` /
> `make report` and the golden-regeneration targets will not run; `make
> csim` / `make rm-csim` work as-is against the committed golden CSVs.

## Goal (reset 2026-07-01)

This scaffold is the first deterministic C-simulation target of the
**shared baseline ISP core + mutually exclusive tone RM slot** structure.
The tone RM slot **wraps** the shared baseline core:

```text
NORMAL:
  RAW Bayer RGGB uint16
    -> checker (mode decision)
    -> RM_NORMAL_TONE (gain 1.25x + gamma2.0)
    -> baseline ISP core (demosaic + BLC + WB + CCM, 12-bit, no gain/gamma)
    -> packed RGB888 uint32  (H x W)

LOW_LIGHT:
  RAW Bayer RGGB uint16
    -> checker (mode decision)
    -> RM_LOW_LIGHT_TONE.front : 2x2 RAW binning (before precision loss, RESEARCH §4.2)
    -> baseline ISP core (demosaic + BLC + WB + CCM, 12-bit, no gain/gamma)
    -> RM_LOW_LIGHT_TONE.back  : low-light gain 2.0x + gamma2.0 tone
    -> packed RGB888 uint32  (H/2 x W/2, Policy A shape change)
```

Invariants proven by the C-sim (RESEARCH.md §8.2):

- **exactly one tone RM** selected per frame (mutually exclusive)
- gain/gamma exist **only in the tone RMs**, never duplicated in the baseline core
- output metadata reports mode, selected RM, and output shape

Deliberately minimal: one small HLS top, stdlib-only C-sim, no Vitis
dependency for local smoke tests, HLS pragmas preserved for the Vitis
HLS/Vitis flow.

## Files

- `include/dfxisp_accel.hpp` — HLS top interface, mode/selected-RM enums, 4 scalar metadata output pointers
- `src/dfxisp_accel.cpp` — checker + baseline core12 (demosaic/BLC/WB/CCM, 12-bit) + RM_NORMAL_TONE (gain 1.25x + gamma2.0) + RM_LOW_LIGHT_TONE (2x2 bin + gain 2.0x + gamma2.0)
- `tests/test_dfxisp_csim.cpp` — C-sim smoke tests + golden CSV bit-compare + architecture invariant checks
- `tools/gen_golden_vectors.py` (origin repo) — stdlib-only deterministic golden generator (bit-exact mirror of `src/dfxisp_accel.cpp`)
- `tools/gen_verification_report.py` (origin repo) — stdlib-only Markdown verification/report generator
- `scripts/vitis_hls.tcl` — Vitis HLS project scaffold for `dfxisp_accel`
- `Makefile` — g++ local C-sim, golden generation, verify/report, Vitis HLS dry-run report
- `include/default_isp.hpp` · `src/default_isp.cpp` — **default_ISP** (new 2026-08-06):
  the standard ISP arm following the AMD Vitis Vision L3 `isppipeline` stage order and
  domains (Bayer-domain BLC/gain → demosaic → adaptive AWB → real CCM → gamma). It
  **coexists** with `RM_NORMAL_TONE` and does not touch the existing golden contract.
  Details: `src/default_isp.md`
- `tests/test_default_isp_csim.cpp` + `tests/default_isp_golden_vectors.csv` — its C-sim
  and committed golden (`make default-isp-csim`; regenerating the golden needs the origin
  repo's `tools/gen_default_isp_golden.py`)

> The experiment arms (§7) and ablations (§12 Task 5) live separately in
> `src/dfxisp_rm.cpp` / `tools/rm_model.py` (static / reg_only / dfx_bin /
> dfx_fp). The scaffold's former post-RGB8 gain/lift path has been migrated
> into that dfx variant set and remains only as an ablation.

## `tools/` file status (canonical / proxy / legacy — 2026-07-08 Hermes review + same-day gamma realignment)

(The `tools/` directory itself lives in the origin repo; the table is kept
here because it defines which Python file is authoritative for what.)

The golden/C-sim/cross-check path itself is solid, but the boundary between
canonical golden ↔ SW-eval proxy ↔ legacy/ver0 code was undocumented and
risked confusion. The table below makes that boundary explicit — new code
must state which category it belongs to.

> **Second same-day update (2026-07-08):** independently of the Hermes
> review (the first version of this table), the gamma curves of
> `isp_pipeline_ver1.py`/`newrm_pipeline.py` were found to differ from
> canonical (both modes share the gamma-2.0 integer-sqrt LUT) — they used
> gamma 2.2/2.5/none. Both files were moved to `tools/archive/` and replaced
> by `baseline_isp_pipeline.py` (normal) / `low_light_isp_pipeline.py`
> (low-light) / `checker.py` (dark-ratio checker, decoupled). The demosaic
> wrap-around bug Hermes fixed (`np.roll` → clamp-to-edge) was ported to the
> new files as well — the two fixes caught different real bugs
> independently and were merged at rebase. Details:
> `results/isp-pipeline-recalibration-2026-07-08.md`.

| File | Status | Purpose |
|---|---|---|
| `gen_golden_vectors.py` | **canonical golden** | bit-exact mirror of `src/dfxisp_accel.cpp` |
| `verify_binning_cross_check.py` | **verification gate** | independent fuzz cross-check of binning-demosaic (part of `make verify`); cross-checks `low_light_isp_pipeline.py`'s `_bin_demosaic_rggb16` (before 2026-07-08: `isp_pipeline_ver1.py`) |
| `baseline_isp_pipeline.py` | **SW eval proxy (canonical-matched)** | normal arm: BLC+WB+gain(1.25x)+gamma-2.0, rewritten 2026-07-08 to match `dfxisp_accel.cpp` down to gain/gamma. Replaces `isp_pipeline_ver1.py` |
| `low_light_isp_pipeline.py` | **SW eval proxy (canonical-matched)** | low-light arm: 2x2 bin-demosaic+BLC+WB+gain(2.0x)+gamma-2.0 (shares the same LUT as normal, canonical-matched). The BLC_OFFSET recalibration value is explicitly marked unsettled (see in-file comment) |
| `checker.py` | **SW eval proxy (canonical-matched)** | dark-ratio-based adaptive mode selector, independent of the two pipeline files (no mutual imports) |
| `newrm_pipeline.py` / `isp_pipeline_ver1.py` | **archived (2026-07-08)** | moved to `tools/archive/`. Gamma differed from canonical (2.2/2.5/none); replaced by the 3 files above — do not reference in new work; kept only for historical ablation lineage |
| `scheduler_sim.py` / `scheduler_sweep.py` | **policy simulation** | hysteresis/temporal/min-dwell scheduler trade-off experiments. Uses synthetic luminance sequences — not a validation of the checker implementation itself |
| `internal_edge_smoke.py` | **regression test** | 1x1–8x8 tiny/odd grid smoke + demosaic boundary-clamp regression tests (`make py-verify`). Checks the independent demosaic copies in both `baseline_isp_pipeline.py` and `checker.py` (before 2026-07-08: `isp_pipeline_ver1.py`) |

## Running the local C-sim

```bash
cd isppipeline/hls
make csim              # smoke tests
make default-isp-csim  # default_ISP (Vitis-Vision-aligned arm) golden compare
make verify    # regenerate golden + packed RGB888 bit-level compare (origin repo only)
make report    # refresh reports/latest.md (includes architecture gate table; origin repo only)
```

Expected `make verify` output:

```text
python3 tools/gen_golden_vectors.py --out tests/golden_vectors.csv
wrote tests/golden_vectors.csv (1498 rows including header; 1497 data rows; 9 cases)
./build/dfxisp_csim
DFXISP golden vector compare passed (566 pixels)
DFXISP C-sim smoke tests passed
```

## Golden vector format

The CSV carries per-case metadata (mode, threshold, output shape, selected
RM) together with input RAW rows (`kind=raw`) and expected output rows
(`kind=rgb`). Input and output pixel counts differ under Policy A (low-light
H/2×W/2), so the two row kinds are separate. Coverage:
bright/dark/mixed/threshold-boundary/bright-recovery/odd-dimension.

## Running the Vitis HLS scaffold

Defaults: ZCU104 part `xczu7ev-ffvc1156-2-e`, 5.0 ns clock. Override for a
different installation. `make hls-report` prints top/project/part/clock/
sources/expected output paths without needing a `vitis_hls` install.

```bash
cd isppipeline/hls
make hls-report                              # dry run
make hls                                     # default DFXISP_HLS_FLOW=csim
DFXISP_HLS_PART=xczu7ev-ffvc1156-2-e \
DFXISP_HLS_CLOCK=5.0 \
DFXISP_HLS_FLOW=csynth make hls              # C-sim then synthesis
```

If `vitis_hls` is not on `PATH`, `make hls` exits with guidance. For a
non-standard location set `VITIS_HLS=/path/to/vitis_hls`.

## HLS top function

```cpp
extern "C" void dfxisp_accel(
    const uint16_t* raw_bayer,
    uint32_t* rgb_out,             // capacity >= width*height
    int width,
    int height,
    int mode,                      // NORMAL / LOW_LIGHT / AUTO
    uint16_t dark_pixel_threshold, // AUTO: LOW_LIGHT when dark-pixel ratio > threshold rule
    int* out_width,                // output width of the selected RM
    int* out_height,               // output height of the selected RM
    int* selected_mode,            // resolved mode (AUTO resolution)
    int* selected_rm,              // selected tone RM
    int* hyst_flags);              // Schmitt band flags (ap_vld fabric wire, 2026-08-06)
```

Why the metadata is **four separate scalar output pointers** instead of one
struct pointer: declaring a struct pointer as `s_axilite` is an unproven
(non-standard) pattern, flagged by the adversarial review (see the bottom of
§ Hardware/DFX structure). Individual scalar pointers synthesize reliably as
post-completion read-back registers in Vitis HLS.

## Hardware / DFX structure

`src/dfxisp_accel.cpp` stays stdlib-only for local C-sim while being
partitioned along the intended static/RM boundary:

- `checker_select_mode()` — static-region scene checker. Under `AUTO`
  decides NORMAL/LOW_LIGHT from the dark-pixel ratio, and (2026-08-06)
  additionally exports the two Schmitt band compares (`hyst_flags`:
  above-enter 64% / below-exit 60%; δ=2%p around the 62% center) per frame. The scene-level hysteresis
  state itself lives in the static-region RTL module
  `results/pr_controller/checker_hysteresis.v`, which drives the PR
  controller trigger directly — this single-frame C-sim entry stays
  stateless (golden contract unchanged).
- `baseline_core12()` / `apply_blc_wb12()` — **shared static** baseline core
  (ver1). Performs BLC + WB (Q8 channel gains) + CCM (identity) **in
  12-bit** (the final >>4 happens in tone). **No gain/gamma.** The normal
  path feeds it the `demosaic_rggb12()` (RGGB 3x3 Bayer demosaic) result;
  the low-light path feeds it the binning-demosaic result (below).
- `tone()` — tone RM stage: exposure gain (12-bit) → >>4 → gamma2.0.
  Per-mode gain.
- `run_normal()` — RM_NORMAL_TONE = **gain 1.25× + gamma2.0**. Runs the
  baseline core at full resolution.
- `run_low_light()` / `compute_binned_rgb_row()` — **RM_LOW_LIGHT_TONE**
  (DFX reconfigurable-module candidate). Fused RAW 2x2 **binning-demosaic**
  (front; R=top-left, G=avg(top-right, bottom-left), B=bottom-right —
  preserves per-channel identity) → baseline core → **gain 2.0× + gamma2.0**
  (back). In the Vivado DFX implementation this tone RM slot is packaged as
  the RM-compatible block; checker, baseline core, and controller stay in
  the static region.
- `gamma2()` — realizes γ=2.0 exactly as integer sqrt `floor(sqrt(255·v))`
  (bit-exact with Python `isqrt`). Replaceable with a 256-entry LUT in HW.

### 2026-07-02 adversarial-review fixes (important)

`/codex:adversarial-review --base 0e433f9` found and fixed two defects:

1. **Chroma-loss bug (high):** the previous low-light front end merged the 4
   samples of a 2x2 RGGB cell into **one scalar average**, then
   re-demosaiced that value as if it were still Bayer — color information
   was destroyed before demosaic. The golden model
   (`gen_golden_vectors.py`) mirrored the same bug, so `make verify`'s
   bit-exact test could not catch it, and the reported lowlight mAP
   evidence (`isp_pipeline_ver1.py`) measured a **different,
   channel-preserving algorithm** — it was not evidence for the actual HW
   candidate. → Fixed by fusing binning+demosaic in one step in
   `compute_binned_rgb_row()`, bit-exact with `_bin_demosaic_rggb16`
   (SW ver1).
2. **Metadata unproven in RTL (medium):** a `DfxIspResult*` struct pointer
   declared `s_axilite` — never confirmed readable in synthesized RTL by
   any artifact (cosim also failed at the post-check stage, so unconfirmed).
   → Replaced with 4 individual scalar pointers (see HLS top function
   above).

**Update 2026-07-02 20:33 KST:** with these fixes applied,
`results/stage4-hw-synthesis-2026-07-02.md` /
`results/stage5-dfx-implementation-2026-07-02.md` were re-synthesized /
re-implemented and their numbers refreshed (pr_verify still PASS; LUT/FF/DSP
decreased — the bug's unnecessary second demosaic logic is gone). Details in
those two docs and `SPEC.md` §10.

C-sim needs no Vitis-specific headers; only HLS pragmas are present and are
ignored by the local g++ build.

## Next hardware steps

1. ~~Replace `run_low_light()`'s static scratch binning buffer with a real
   streaming line buffer.~~ **Done (2026-07-02)** — replaced with a
   `row_buf[3][MAX_BINNED_W]` 3-row sliding buffer, bit-exactness kept.
2. Promote RM_LOW_LIGHT_TONE / RM_NORMAL_TONE into the standalone DFX RM
   slot packaging flow (§8.3 gate).
3. Add Policy B (shape-preserving upsample/pad) only if the DPU requires a
   fixed H×W ABI (§4.3).
4. Arm 2 (register-only) vs Arm 3 (DFX) resource/power/PR-latency comparison
   (§7). **Arm2 measured** (unified top, C-synthesis) —
   `results/stage4-hw-synthesis-2026-07-02.md`. Arm1/Arm3 and
   power/PR-latency still TODO (needs the Vivado DFX floorplan +
   implementation).
5. **(Done in simulation, 2026-08-06)** fabric-internal mode switching:
   `checker_hysteresis.v` consumes the new `hyst_flags` ap_vld wires and
   drives `pr_controller.trigger` (request/ack) — end-to-end xsim PASS
   (`checker_to_pr_tb.v`). Remaining wiring (real `hyst_flags` RTL ports
   after re-synthesis, `drain_ready` ← RM `ap_idle`, ICAPE3) is Stage 6 —
   see `checker_hysteresis.md`.

## C-synthesis / co-sim execution notes (Vitis HLS 2024.1)

Two environment issues from earlier worklogs reproduce:

- **source-path bug:** design sources added with `add_files` from a nested
  `build/vitis_hls/...` project path go missing from csim/csynth's
  `HLS_SOURCES`, causing a link failure. **Workaround:** copy hpp/cpp/tb
  into a flat temp dir (e.g. `/tmp/hls_dfxisp/dfxisp_accel/`) and run from
  that directory.
- **exit hang:** the process does not terminate after `close_project` (the
  work itself is already done). Wrap with
  `timeout -k <grace> <sec> vitis_hls -f run.tcl` and check the completion
  marker in the log.
- **cosim requires `depth=`:** `m_axi` interfaces need `depth=` for
  co-simulation. Sized for the current C-sim fixture max (16×16 = 256 px);
  using the full design envelope (1920×1080) directly SIGSEGVs cosim's
  auto-wrapc harness (suspected internal stack-allocation overflow).
  Measured resources/timing are unaffected (synthesis ignores the hint;
  cosim-only).

<!--
=============================================================================
File   : README.md
Date   : 2026-08-06 KST
Function: JNU_DFXISP_FPGA — handover repo extracted from the research repo
          haengmini/dfxisp, containing only what the FPGA experiments need:
          HLS sources, DFX flow scripts, real-RAW sample data
          (PASCALRAW / Sony NOD), and the SPEC. The FPGA experimenter can
          clone just this repo and start the Stage 6 (board) work.
Origin : https://github.com/haengmini/dfxisp (extracted 2026-08-06 at
         commit ea6c2de)
=============================================================================
-->
# JNU DFXISP — FPGA experiment repo

Extracted from the research repo
[haengmini/dfxisp](https://github.com/haengmini/dfxisp), keeping **only what
the FPGA experiments need**: HLS sources, the Vivado DFX flow, real-RAW
sample data, and the design spec (SPEC.md). ML evaluation tooling,
intermediate reports, and the full datasets live in the origin repo.

## Target / tools

| Item | Value |
|---|---|
| Board | ZCU104 (Zynq UltraScale+ `xczu7ev-ffvc1156-2-e`) |
| Tools | Vivado / Vitis HLS **2024.1** |
| Target clock | 5.0 ns (200 MHz) |
| Current stage | Stages 4–5 done (HLS synthesis + DFX implementation, fabric-only); **Stage 6 (board) not started** |

## Directory layout

```
SPEC.md                          # canonical design spec — §6–7 structure/RP boundary,
                                 #   §10 measured resources/timing
isppipeline/hls/
  src/, include/                 # HLS C++ sources (dfxisp_accel = static+RP, dfxisp_rm = RMs)
  tests/                         # csim testbenches + golden vector CSVs (committed)
  scripts/vitis_hls.tcl          # HLS csynth/cosim flow
  scripts/dfx/                   # Vivado DFX flow (dfx_flow.tcl etc.)
  results/pr_controller/         # fabric trigger chain: Schmitt mode arbiter (checker_hysteresis.v)
                                 #   + DFXC adapter + latency probe + TBs (xsim PASS)
                                 #   (custom pr_controller: archived, results/archive/)
  results/icap_sim/              # ICAP PR-latency simulation TB
data/                            # real-RAW samples (see "Datasets" below)
```

`scripts/dfx/dfx_flow.tcl` computes the repo root as four levels above the
script location, so the directory structure is kept identical to the origin
repo — every script works verbatim.

Long design-background comments from the sources have been **split into
same-named `.md` notes next to each file** (`src/dfxisp_accel.md`,
`include/dfxisp_accel.md`, `results/pr_controller/checker_hysteresis.md`,
`results/pr_controller/dfxc_adapter.md`,
`results/icap_sim/icap_pr_latency_tb.md`; the archived
`results/archive/pr_controller/pr_controller.md`). The code keeps only a
short summary plus a pointer to the note.

**Mode switching is fabric-internal** (2026-08-06): the HLS checker exports
per-frame Schmitt band flags (`hyst_flags`, ap_vld wire),
`checker_hysteresis.v` owns the mode state, and `dfxc_trigger_adapter.v`
drives the AMD DFX Controller's hardware triggers — the PS only observes.
The full chain (with the latency probe) is simulated in
`checker_to_dfxc_tb.v` (xsim PASS). See `checker_hysteresis.md` and
`dfxc_adapter.md` for the protocol and the remaining Stage 6 wiring.

## Datasets — the input is real RAW (not pseudo-RAW)

The pipeline input is **real RAW sensor data** that never went through a
camera ISP. Two public datasets are used:

| Dataset | Camera | Illumination | Samples in this repo |
|---|---|---|---|
| **PASCALRAW** (Omid-Zohoor et al., Stanford Digital Repository) | Nikon D3200, 12-bit | 100% daylight | `data/pascalraw_test/`, 3 frames |
| **Sony NOD** (RAW-NOD, Night Object Detection) | Sony RX100 VII, RGGB | evening/night | `data/sonynod_test/`, 3 frames |

Each sample directory contains:

- `raw_bin/*.bin` — RAW Bayer frames in a 16-bit container. Scale is
  `shift8` (`round(linear*255)<<8`); Bayer phase / black / white levels are
  in `meta.json` and `frames_meta.csv` (PASCALRAW: black 0 / white 4095;
  SonyNOD: converted from black 800 / white 16380 originals — always check
  the meta).
- `images/*.jpg` — reference renders (NOT RAW; for visual inspection only).
- `labels/*.txt` — YOLO-format detection labels (not needed for board
  experiments; reference only).
- `meta.json` — canonical conversion parameters.

**The full datasets** (PASCALRAW 4,259 / SonyNOD 321 frames, hundreds of GB)
cannot be hosted here due to GitHub size limits. If you need them:

1. Download from the official distributions — PASCALRAW from the Stanford
   Digital Repository, RAW-NOD from its public repository.
2. The RAW → `raw_bin` conversion tools are in the origin repo's
   `isppipeline/hls/tools/` (use the same conversion parameters recorded in
   each `meta.json`).
3. Or ask for the already-converted copies on the desktop machine via an
   external drive / cloud storage.

## Build / run

Bitstreams, DCPs, and generated RTL (`build/`) are not in git. To
regenerate:

```bash
cd isppipeline/hls
make csim rm-csim          # C simulation — compares against golden CSVs, runs immediately
# csynth/cosim: vitis_hls -f scripts/vitis_hls.tcl
#   Caution — a known bug drops design sources added with add_files from a
#   nested project path (link error: undefined symbol: dfxisp_accel).
#   Copy hpp/cpp/tb into a flat temp dir (e.g. /tmp/hls_dfxisp/) and run
#   vitis_hls from there.
# DFX:  vivado -mode batch -source scripts/dfx/dfx_flow.tcl
```

Golden-regeneration targets such as `make verify` need the origin repo's
`tools/` Python and do not work in this repo (csim is fully covered by the
committed golden CSVs).

**Caution (real incident): when copying HLS-generated RTL elsewhere,
`cp *.v` alone silently drops the gamma-ROM `.dat` files and changes the
synthesis result** — always copy the `.dat` files along with the `.v`.
Also, **never mix resource numbers from partition builds and flat builds**
(~24% difference on the identical netlist, cause not yet identified —
SPEC.md §10). Use the flat axis for resource comparisons; use partition
builds for bitstreams / `pr_verify`.

## What to do — Stage 6 (board)

In order:

1. **Prerequisites**: the fabric-internal trigger chain now exists —
   `checker_hysteresis.v` (Schmitt mode arbiter) consumes the HLS core's
   `hyst_flags`/`ap_vld` wires; **the production reconfiguration path is
   the AMD DFX Controller IP (PG374, adopted 2026-08-06)**, bridged by
   `dfxc_trigger_adapter.v` (per-RM one-hot HW trigger + `ap_idle`
   shutdown-ack shim; chain verified against a PG374 contract model in
   `checker_to_dfxc_tb.v`, xsim PASS). Follow the integration checklist in
   `dfxc_adapter.md`: generate/configure the IP (1 VS, 2 RMs, DDR bitstream
   table, ICAP), add the DFX Decoupler, confirm generated port names, wire
   the re-synthesized `hyst_flags` ports and `rm_ap_idle`, decide the
   post-swap `ap_start` policy. Latency measurement: the IP has no built-in
   counter — `pr_latency_probe.v` counts the IP's handshake boundaries
   (drain, end-to-end swap) instead. The hand-written `pr_controller.v` is
   **archived** (`results/archive/pr_controller/`, retirement note inside).
2. PS/DDR integration (Vivado Block Design).
3. Clock/reset pin assignment for the new pblock + WNS re-verification.
4. (Optional) investigate the partition-pin count drop 15 → 3 (recorded as
   uninvestigated in SPEC.md §10).
5. (Optional) complete cosim — the automated post-check SIGSEGV under
   WSL2+XSIM is a documented pre-existing bug; the RTL simulation itself
   passed 10/10 transactions. It may not reproduce on native Linux/Windows
   Vivado.

## Reporting principles

Never write numbers that were not measured as if they were facts
(SPEC.md §11.4). If a result contradicts expectations, record it honestly —
do not delete it.

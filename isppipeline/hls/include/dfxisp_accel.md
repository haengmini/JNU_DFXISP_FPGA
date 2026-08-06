# dfxisp_accel.hpp — interface design notes

Documentation split out of the source comments of `dfxisp_accel.hpp`. The
minimum needed to use the API (pixel format, capacity rule) stays in the
header; this file holds the path diagrams and design rationale.
(Origin: haengmini/dfxisp commit `ea6c2de`.)

## Architecture — the two processing paths

Shared baseline ISP core + mutually exclusive mode-specific tone RMs. The
tone RM slot *wraps* the shared baseline core:

```
NORMAL:
  raw -> demosaic (RGGB)
      -> baseline_isp_core (BLC + WB + CCM, no gain/gamma)
      -> RM_NORMAL_TONE (gain 1.25x + gamma 2.0)
      -> RGB32  (H x W)

LOW_LIGHT:
  raw -> RM_LOW_LIGHT_TONE.front (2x2 RAW binning-demosaic, fused: this IS
         the demosaic step for the binned grid, preserving per-channel
         identity — R = top-left, G = avg(top-right, bottom-left),
         B = bottom-right. NOT a 4-sample scalar average re-demosaiced,
         which would collapse chroma)
      -> baseline_isp_core (BLC + WB + CCM — same function as the NORMAL
         path, applied to the binned RGB instead of the full-res demosaic
         output)
      -> RM_LOW_LIGHT_TONE.back (gain 2.0x + gamma 2.0)
      -> RGB32  (H/2 x W/2, shape-changing Policy A)
```

Invariants proven by the C-sim golden gates:
- exactly one tone RM per frame (mutually exclusive)
- gain/gamma live only in the tone RMs, never duplicated in the baseline core
- output metadata reports mode, selected RM, and output shape

## About the input data

The input is **12-bit RAW Bayer RGGB** (in a uint16_t container). Experiment
inputs are conversions of real-sensor RAW (PASCALRAW — Nikon D3200, Sony NOD
— RX100 VII; see the `data/` samples in this repo and their `meta.json`),
while the csim testbench uses synthetic vectors
(`tests/golden_vectors.csv`). The "pseudo-RAW" wording found in early
documents referred to the synthetic/derived RAW vectors of the initial
development phase; the current experimental setup feeds real RAW.

## Metadata interface rationale (2026-07-02 adversarial review)

The metadata output was changed from a single `DfxIspResult*` struct pointer
(s_axilite) to four separate scalar `int*` output pointers. A struct pointer
over s_axilite is an unproven HLS pattern (s_axilite is a slave-only control
interface, not a memory-writing master), and no artifact (interface report,
cosim) confirmed the struct fields synthesize as individually addressable
registers. Separate scalar output pointers are the well-established Vitis
HLS idiom for post-completion status/readback registers. Any pointer may be
null (the metadata write for that field is skipped).

The same review also fixed header drift: RM_NORMAL_TONE is not an "identity
bypass" (ver1 added gain+gamma to it), and RM_LOW_LIGHT_TONE's tone is
gamma-2.0, not gamma-4.0.

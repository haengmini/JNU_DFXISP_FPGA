# DFXISP HLS Verification Report

Generated: 2026-07-02 23:57:16 UTC
Report: `reports/latest.md`

Architecture (ver1): shared baseline core (demosaic+BLC+WB+CCM, 12-bit, no gain/gamma) + mutually exclusive tone RM slot (RM_NORMAL_TONE = gain 1.25x + gamma2.0 / RM_LOW_LIGHT_TONE = 2x2 bin + gain 2.0x + gamma2.0). See `RESEARCH.md` / `SPEC.md`.

## Status

| Check | Status | Evidence |
|---|---:|---|
| Golden vectors | PASS | `tests/golden_vectors.csv`; 1705 data rows; 11 cases |
| C-sim | PASS | `build/dfxisp_csim`; return code 0 |

## Architecture gates

| Gate | Status |
|---|---:|
| Shared baseline core (bit-exact) | PASS |
| RM_NORMAL_TONE present | PASS |
| RM_LOW_LIGHT_TONE present | PASS |
| Mutually exclusive RM selection | PASS |
| No duplicate gain/gamma (tone RM only) | PASS |
| Output shape policy: LOW_LIGHT H/2 x W/2 (Policy A), NORMAL H x W | PASS |

## Makefile state

- `CXX`: `g++`
- `PYTHON`: `python3`
- `CSIM_BIN`: `build/dfxisp_csim`
- `GOLDEN_CSV`: `tests/golden_vectors.csv`
- Targets include: `.PHONY, all, csim, golden, cross-check, verify, report, hls-report, hls, rm-golden, rm-csim, rm-verify, scheduler, analysis, sw-stage, clean`

## Golden vector cases

| Case | Mode | Sel mode | Selected RM | In | Out |
|---|---:|---:|---|---:|---:|
| seq1_bright_normal_grid_8x8 | 0 | 0 | RM_NORMAL_TONE | 8x8 | 8x8 |
| seq2_bright_normal_grid_8x8 | 0 | 0 | RM_NORMAL_TONE | 8x8 | 8x8 |
| seq3_mixed_normal_grid_16x16 | 0 | 0 | RM_NORMAL_TONE | 16x16 | 16x16 |
| seq4_dark_lowlight_grid_8x8 | 1 | 1 | RM_LOW_LIGHT_TONE | 8x8 | 4x4 |
| seq5_dark_lowlight_grid_8x8 | 1 | 1 | RM_LOW_LIGHT_TONE | 8x8 | 4x4 |
| seq6_mixed_dark_lowlight_grid_16x16 | 1 | 1 | RM_LOW_LIGHT_TONE | 16x16 | 8x8 |
| seq7_bright_recovery_auto_8x8 | 2 | 0 | RM_NORMAL_TONE | 8x8 | 8x8 |
| auto_dark_trigger_8x8 | 2 | 1 | RM_LOW_LIGHT_TONE | 8x8 | 4x4 |
| odd_dimension_lowlight_7x5 | 1 | 1 | RM_LOW_LIGHT_TONE | 7x5 | 3x2 |
| auto_boundary_ratio_75_8x8 | 2 | 0 | RM_NORMAL_TONE | 8x8 | 8x8 |
| auto_boundary_ratio_86_8x8 | 2 | 1 | RM_LOW_LIGHT_TONE | 8x8 | 4x4 |

## C-sim output

```text
DFXISP golden vector compare passed (646 pixels)
DFXISP C-sim smoke tests passed
```

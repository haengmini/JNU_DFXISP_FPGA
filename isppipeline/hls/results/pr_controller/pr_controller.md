# pr_controller.v — design background notes

Documentation split out of the source comments of `pr_controller.v`.
(Origin: haengmini/dfxisp commit `ea6c2de`.)

## What it is

First-pass DFX partial-reconfiguration controller FSM (Phase 2.1).

```
FSM: IDLE -> DRAIN_WAIT -> ICAP_ARM -> ICAP_STREAM -> DONE -> IDLE
```

## Why completion comes from its own word counter

The config1/config2 utilization reports (2026-07-03) show **zero**
ICAPE3/STARTUPE3 instances — there was no PR controller yet, which is also
why the earlier isolated ICAPE3-only simulation could never produce a
trustworthy completion signal: ICAPE3's PRDONE depends on eos_startup, which
requires a real STARTUPE3 / device-level configuration context this design
does not have.

This module sidesteps that entirely — instead of trusting ICAPE3's PRDONE,
it **completes when its own word counter reaches the known partial-bitstream
word count** (171,633 words, verified 2026-07-02 by parsing the real
`rm_lowlight_partial.bit`). That signal is fully owned by this design and
can be validated in simulation without STARTUPE3.

## Limits of the fabric-only version (= the Stage 6 prerequisites)

- The partial-bitstream source is a **BRAM** (`$readmemh` in simulation,
  BRAM init assumed for real use) — not PS/DDR yet. The PS/DDR upgrade comes
  after PS/DDR integration.
- `drain_ready` is not yet tied to the real RM `ap_idle`.
- ICAPE3/STARTUPE3 are not instantiated.

All three are the "Stage 6 prerequisites" items in this repo's README.

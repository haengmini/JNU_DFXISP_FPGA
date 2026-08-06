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

## Trigger source (added 2026-08-06)

`trigger` is no longer an unconnected input: `checker_hysteresis.v` (same
directory) generates it from the HLS checker's per-frame Schmitt-band flags
and holds it until `busy` acks — see `checker_hysteresis.md` and the
end-to-end simulation `checker_to_pr_tb.v` (xsim PASS). The same doc lists
the remaining integration considerations found by source inspection
(stale `NWORDS` default vs the new-pblock bitstream, 2-cycles-per-word
streaming, 200 MHz TB clock vs ICAPE3's rated 100 MHz).

# AMD DFX Controller adoption + dfxc_trigger_adapter.v — decision record

**Decision (2026-08-06): the production reconfiguration path uses the AMD
DFX Controller IP (PG374).** The hand-written `pr_controller.v` is retained
**for latency characterization only** (its word-count completion gives the
stage-by-stage observability the IP does not), and is no longer the
integration target.

## Why

| | custom `pr_controller.v` | AMD DFX Controller IP |
|---|---|---|
| bitstream source | 1 fixed BRAM | AXI master fetch from DDR/flash, per-RM address table |
| triggers | 1 wire | HW trigger ports + SW triggers, trigger→RM mapping |
| RP support | 1 fixed | multiple Virtual Sockets (VSM) |
| shutdown/isolation | drain_ready wire | shutdown handshake + DFX Decoupler ecosystem |
| error handling | none | status registers, error management |

Growing the custom FSM to production quality would mean re-implementing all
of the right column; the IP gives it validated. The measurement narrative
(trigger→done breakdown) keeps the custom FSM as its instrument.

## What connects where

```
checker_hysteresis ──pr_trigger/pr_busy──▶ dfxc_trigger_adapter ──▶ AMD DFX Controller
      (unchanged)                               │                        │ m_axi → DDR (bitstreams)
                                                │                        │ ICAP → configuration engine
                          rm_ap_idle ──────────▶│ shutdown-ack shim      │ decouple → DFX Decoupler(s)
```

`dfxc_trigger_adapter.v` keeps `checker_hysteresis` unchanged and provides:
- **per-RM one-hot HW trigger** (`vs_hw_triggers`: [0]=RM_NORMAL,
  [1]=RM_LOW_LIGHT), held until the IP accepts — the request-until-ack rule
  survives, so a request during a swap is never lost;
- **busy ack** synthesized from the IP's observable activity
  (`rm_shutdown_req | rm_decouple`);
- **shutdown-ack shim**: `vs_rm_shutdown_ack = req & rm_ap_idle` — the same
  drain condition the custom controller's `DRAIN_WAIT` implemented.

## Honesty note on port names

The `vsm_VS_0_*` interface follows PG374's Virtual Socket Manager contract
from documentation — **the exact generated port names/widths must be
confirmed after configuring the IP** (1 Virtual Socket, 2 RMs, DDR address
table, ICAP). `checker_to_dfxc_tb.v` validates the chain against a
**behavioral contract model** of that sequence (trigger → shutdown req/ack →
decouple → program → release; xsim PASS, drain enforced) — it is not the
real IP; re-run the chain as an IP-integrated simulation in Stage 6.

## Stage 6 integration checklist (supersedes the custom-controller wiring plan)

1. Generate/configure the DFX Controller IP: 1 VS, 2 RMs
   (RM_NORMAL/RM_LOW_LIGHT), bitstream sizes/addresses in DDR, ICAP
   interface; confirm port names against `dfxc_trigger_adapter.v`.
2. Add DFX Decoupler IP on the RP boundary, driven by the IP's decouple
   output.
3. Wire `hyst_flags`/`ap_vld` (re-synthesized `dfxisp_accel`) →
   `checker_hysteresis` → adapter → IP HW triggers; `rm_ap_idle` from the
   active RM into the adapter.
4. Post-swap RM restart policy (who issues `ap_start` after a swap: static
   logic vs PS) — still an open decision.
5. PS keeps observation only (AXI4-Lite: `selected_mode`, IP status
   registers).
6. Characterization runs (paper numbers) may still use `pr_controller.v` in
   a fabric-only harness — its known caveats (stale `NWORDS`,
   2-cycles-per-word, 100 MHz ICAP clock) then apply to the instrument, not
   the product.

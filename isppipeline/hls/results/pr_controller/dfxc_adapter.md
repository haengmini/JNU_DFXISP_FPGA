# AMD DFX Controller adoption + dfxc_trigger_adapter.v — decision record

**Decision (2026-08-06): the production reconfiguration path uses the AMD
DFX Controller IP (PG374).** The hand-written `pr_controller.v` was
initially kept as a latency instrument, then **archived later the same day**
(`../archive/pr_controller/`) once `pr_latency_probe.v` — an auxiliary
(optional) counter on the IP's handshake boundaries — took over the
measurement role (§6 below).

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
(trigger→done breakdown) is served by the auxiliary `pr_latency_probe.v`.

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

## IP generation probe (2026-08-06) — port contract CONFIRMED

The IP was actually generated on Vivado 2024.1 (`dfx_controller` v1.0,
xczu7ev; reproducible script: `scripts/dfx/gen_dfx_controller.tcl`) with
NUM_HW_TRIGGERS=2, TRIGGER0_TO_RM=RM_0, TRIGGER1_TO_RM=RM_1. Confirmed from
the generated instantiation template (49 ports total):

| Generated port | Dir/width | Adapter side |
|---|---|---|
| `vsm_VS_0_hw_triggers` | in [1:0] | `vs_hw_triggers` — **one wire per trigger, trigger i loads TRIGGERi_TO_RM** → the one-hot design matches |
| `vsm_VS_0_rm_shutdown_req` | out 1 | `vs_rm_shutdown_req` |
| `vsm_VS_0_rm_shutdown_ack` | in 1 | `vs_rm_shutdown_ack` (shim = req & ap_idle) |
| `vsm_VS_0_rm_decouple` | out 1 | `vs_rm_decouple` (→ also drives the DFX Decoupler) |
| `vsm_VS_0_rm_reset` | out 1 | not in the adapter — wire into the RM reset tree (Stage 6) |
| `vsm_VS_0_event_error`, `sw_shutdown_req`, `sw_startup_req` | out 1 | observability / SW flows |
| `clk`, `reset` + `icap_clk`, `icap_reset` | in | **separate ICAP clock domain — the IP contains the CDC (CDC_STAGES param), resolving our "ICAP 100 MHz CDC" open item: just feed 100 MHz to icap_clk** |
| `icap_o` [31:0] out / `icap_i` [31:0] in, `icap_csib`, `icap_rdwrb` | — | named from ICAPE3's perspective: IP `icap_o` → ICAPE3 `I`, IP `icap_i` ← ICAPE3 `O`; plus `icap_avail`/`icap_prdone`/`icap_prerror` inputs |
| `m_axi_mem_*` | AR/R only, 32-bit addr | read-only bitstream-fetch master (no write channels) |
| `s_axi_reg_*` | AXI4-Lite, 32-bit addr | SW trigger/status register access |

**Still open after the probe (honest):** per-RM settings
(SHUTDOWN_REQUIRED=hw — required for the shutdown handshake to engage —
RESET_REQUIRED, bitstream ADDRESS/SIZE table) could not be set through the
dotted-path Tcl API in batch mode (RM-level keys errored; VS-level keys
worked); configure them in the IP customization GUI / Block Design at
Stage 6 and re-verify. Direct `CONFIG.ALL_PARAMS` assignment fails in 2024.1
batch with a `GUI_SELECT_TRIGGER_3 = -1` propagation error — use the API.

`checker_to_dfxc_tb.v` still validates against a **behavioral contract
model** (not the generated netlist); an IP-integrated simulation remains a
Stage 6 step, but the port names/widths the TB and adapter assume are now
the generated ones.

## Stage 6 integration checklist (supersedes the custom-controller wiring plan)

1. Generate/configure the DFX Controller IP: base generation is scripted
   (`scripts/dfx/gen_dfx_controller.tcl`, port contract confirmed — see the
   probe section above); still to configure via GUI/BD: per-RM
   SHUTDOWN_REQUIRED=hw, RESET settings, and the DDR bitstream
   ADDRESS/SIZE table.
2. Add DFX Decoupler IP on the RP boundary, driven by the IP's decouple
   output.
3. Wire `hyst_flags`/`ap_vld` (re-synthesized `dfxisp_accel`) →
   `checker_hysteresis` → adapter → IP HW triggers; `rm_ap_idle` from the
   active RM into the adapter; `vsm_VS_0_rm_reset` into the RM reset tree.
   ICAP clock: feed 100 MHz to `icap_clk` — the IP contains the CDC
   (probe finding), so no external CDC design is needed.
4. Post-swap RM restart policy (who issues `ap_start` after a swap: static
   logic vs PS) — still an open decision.
5. PS keeps observation only (AXI4-Lite: `selected_mode`, IP status
   registers).
6. (Auxiliary, optional — the swap chain works without it) Latency
   measurement: the IP has **no built-in latency counter** (status/error
   registers only), but its handshake signals bound every phase —
   `pr_latency_probe.v` (same directory) counts drain (shutdown req→ack)
   and end-to-end swap (trigger→decouple release); verified inside
   `checker_to_dfxc_tb.v` (probe: drain=4, swap=38 cycles on the contract
   model). On the board, read it via ILA or latch into status registers.
   The custom `pr_controller.v` is therefore **archived**
   (`../archive/pr_controller/`) — no longer needed even as an instrument.

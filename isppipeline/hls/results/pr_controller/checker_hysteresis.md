# checker_hysteresis.v — design notes

New module (2026-08-06, this repo): moves the Schmitt mode decision into the
fabric and closes the checker → pr_controller trigger path with no PS
involvement. This implements the 2026-07-03 adoption (origin repo
`checker-improvement-theory-2026-07-03.md` §7 proposal #1: "migrate
Schmitt + min-dwell into the HW checker — adopted immediately") that had
never been carried into code; the later "driver-side policy" wording in the
C1 deployment docs described the unimplemented interim state and is
superseded by this module for the DFX arm.

## Division of labor

```
dfxisp_accel (HLS core)                 checker_hysteresis.v (this, static region)
  per-frame dark-pixel scan               mode state (1 FF) + Schmitt band
  2 band compares:                        min-dwell counter
    hyst_flags[0] = ratio > 64% (enter)   pr_trigger request/ack handshake
    hyst_flags[1] = ratio < 60% (exit)
  exports hyst_flags + ap_vld  ──────▶    consumes one flag pair per frame
```

- The HLS core stays **stateless** — its single-frame verdict
  (`selected_mode`) and the golden CSV contract are unchanged (csim remains
  bit-exact; verified after the change: 726 px golden compare PASS).
- The band edges are compile-time constants in `dfxisp_accel.cpp`
  (`HYST_ENTER_PCT` = 64, `HYST_EXIT_PCT` = 60 — delta = 2%p around the
  62% center per checker-principles principle 5; the single-frame verdict
  keeps `DARK_RATIO_PCT` = 62 independently). The dark-pixel level itself stays the runtime
  `dark_pixel_threshold` AXI-Lite register (256 = 16<<4 in raw12).
- Forced NORMAL/LOW_LIGHT modes export flags matching the override, so the
  hysteresis block tracks a forced mode instead of fighting it.

## Trigger protocol (request/ack)

- On a mode transition, `pr_trigger` goes high and **stays high until
  `pr_busy` is observed** — a pulse would be lost if the controller were
  mid-reconfiguration. `pr_controller` samples `trigger` only in `S_IDLE`,
  and clearing on busy guarantees no spurious re-trigger when the
  controller returns to IDLE after DONE.
- Decisions are frozen while a request is pending or `pr_busy` is high
  (those frames were processed by the outgoing RM anyway).
- `DWELL_FRAMES` (default 1): frames a newly entered mode is held before
  the next switch is allowed; pending/busy frames don't count. 1 frame
  covers the current PR latency estimate (14.5 ms typ < 33.3 ms frame
  budget); increase if PR latency grows past one frame time.

## Verification (xsim, Vivado 2024.1 — both PASS)

- `checker_hysteresis_tb.v` — unit: in-band frames change nothing (the
  Schmitt property the single-threshold checker lacked), trigger held until
  busy ack, dwell blocks the frame right after a switch, no spurious
  re-trigger.
- `checker_to_pr_tb.v` — integration with the real `pr_controller.v`
  (NWORDS=16 override): dark frame → trigger → drain (RM ap_idle model,
  8 cycles) → ICAP stream → done, then the reverse transition. 2
  reconfigurations, no PS in the loop.

> **Update (2026-08-06, later the same day):** the AMD DFX Controller IP
> (PG374) was adopted as the production reconfiguration path —
> `dfxc_trigger_adapter.v` bridges this block to the IP with the same
> request/ack semantics (`checker_hysteresis` itself is unchanged), and
> `checker_to_dfxc_tb.v` verifies the chain against a PG374 contract model
> (xsim PASS). See `dfxc_adapter.md`. The custom `pr_controller.v` was
> subsequently **archived** (`../archive/pr_controller/`) — latency
> measurement is handled by `pr_latency_probe.v` on the IP's handshake
> signals. Items 3–5 below are retained as history of the custom-path plan;
> the live checklist is in `dfxc_adapter.md`.

## What Stage 6 still owes on top of this

1. Wire the real HLS core's `hyst_flags[1:0]` + `hyst_flags_ap_vld` RTL
   ports to this block (ports exist after re-synthesizing `dfxisp_accel` —
   the `ap_vld` pragma is already in the source).
2. `drain_ready` ← the active RM's real `ap_idle` (the TB uses a model).
3. Replace the BRAM bitstream source with SD/DDR and instantiate
   ICAPE3/STARTUPE3 (unchanged from before).
4. Known pr_controller integration considerations (source inspection
   2026-08-06): `NWORDS=171,633` default matches the OLD floorplan
   bitstream (686,532 B payload) — the current pblock partial bitstream is
   1,447,424 B ≈ 361,856 words, so re-extract and update; the FETCH/WRITE
   FSM streams 1 word per 2 cycles (half the ICAP bandwidth — consider
   pipelining); the 1.716 ms TB measurement was taken at 200 MHz while
   ICAPE3 is rated 100 MHz (UG570) — at rated clock this FSM gives ~2×
   that, and the ICAP clock domain needs a CDC decision.
5. Bitstream selection: with two RMs the controller needs a per-target
   base address / bitstream choice — currently it streams a single
   preloaded payload.

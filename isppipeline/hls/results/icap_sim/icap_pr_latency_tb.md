# icap_pr_latency_tb.v — measurement method notes

Documentation split out of the source comments of `icap_pr_latency_tb.v`.
(Origin: haengmini/dfxisp commit `ea6c2de`.)

## What it measures

PR (partial reconfiguration) latency testbench: drives the real
`rm_lowlight_partial.bit` payload through Xilinx's ICAPE3 UNISIM behavioral
model at its rated max clock (100 MHz, AMD UG570), and measures simulated
time from a "DFX trigger" pulse (first active ICAP write) to the point where
the **real DESYNC command** (Type1 write CMD=0x0000000D, opcode 0x30008001)
appears in the streamed bitstream content.

## Why DESYNC detection instead of PRDONE

ICAPE3's own PRDONE output (via the embedded SIM_CONFIGE3 packet parser) was
tried first and never asserts in an isolated ICAPE3-only testbench — PRDONE
requires eos_startup, which depends on a full device
STARTUPE3/configuration-sequence simulation context this testbench does not
provide. That is a genuine limitation of the UNISIM model for isolated
ICAP-only simulation, not a testbench bug.

The reliable alternative is detecting the real DESYNC command directly in
the streamed content — DESYNC is the actual command that ends the
configuration sequence in the real bitstream, independently located via
post-processing at **word index 171,615 (of 171,633 total words) for BOTH
partial bitstreams** — the identical position confirms the two RMs share the
same frame-address command structure and differ only in frame data content.

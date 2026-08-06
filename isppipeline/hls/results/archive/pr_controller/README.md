# Archived: custom pr_controller (2026-08-06)

Moved here unmodified after the AMD DFX Controller IP (PG374) was adopted as
the production reconfiguration path. Its measurement role is taken over by
`../../pr_controller/pr_latency_probe.v`, which counts the IP's own
handshake boundaries (the IP has no built-in latency counter) — so this FSM
is no longer needed as an instrument either. Kept for history: its
simulations produced the 2026-07-03 trigger→done measurement (1.716 ms at
200 MHz, 171,633-word payload) cross-validating the spec-derived estimate.
Known caveats at retirement: stale NWORDS vs the new-pblock bitstream,
2-cycles-per-word streaming, TB clocked above ICAPE3's rated 100 MHz.

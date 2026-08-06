`timescale 1ns/1ps
// =============================================================================
// dfxc_trigger_adapter.v — bridges checker_hysteresis to the AMD DFX
// Controller IP (PG374), adopted 2026-08-06 as the production reconfiguration
// path (the hand-written pr_controller.v is retained for latency
// characterization only).
//
// checker_hysteresis is unchanged: it still raises pr_trigger and waits for a
// busy ack. This adapter (a) converts the request into the per-RM one-hot
// hardware trigger the IP expects, (b) synthesizes the busy ack back from the
// IP's shutdown/decouple activity, and (c) implements the RM shutdown
// handshake shim: ack = shutdown request AND the active RM reporting ap_idle
// (the same drain condition the custom controller's DRAIN_WAIT state used).
//
// Port names on the IP side follow PG374's Virtual Socket Manager interface
// (vsm_VS_0_*). CONFIRM the exact generated names after configuring the IP
// (1 Virtual Socket, 2 RMs, bitstream address table in DDR) — this file
// encodes the contract, not a verified pinout. See dfxc_adapter.md.
// =============================================================================
module dfxc_trigger_adapter (
    input  wire clk,
    input  wire rst_n,

    // checker_hysteresis side (unchanged request/ack contract)
    input  wire       mode,          // stable target mode: 0=NORMAL, 1=LOW_LIGHT
    input  wire       pr_trigger,    // request, held by checker_hysteresis until ack
    output wire       pr_busy,       // ack: reconfiguration in flight

    // AMD DFX Controller side (PG374 VSM interface, names to confirm)
    output reg  [1:0] vs_hw_triggers,    // [0] = load RM_NORMAL, [1] = load RM_LOW_LIGHT
    input  wire       vs_rm_shutdown_req, // IP asks the active RM to stop
    output wire       vs_rm_shutdown_ack, // shim: granted once the RM is idle
    input  wire       vs_rm_decouple,     // IP isolating the RP boundary

    // Active RM status (ap_ctrl_hs)
    input  wire       rm_ap_idle
);

    // Busy from the IP's observable activity: from the moment it starts the
    // shutdown handshake until it releases the boundary decouple. This is
    // what clears checker_hysteresis's pending request and freezes decisions
    // during the swap — same semantics as pr_controller's busy.
    assign pr_busy = vs_rm_shutdown_req | vs_rm_decouple;

    // Drain condition, formerly DRAIN_WAIT: only ack the shutdown once the
    // active RM has finished its frame.
    assign vs_rm_shutdown_ack = vs_rm_shutdown_req & rm_ap_idle;

    // One-hot trigger for the RM of the (already flipped) target mode, held
    // until the IP accepts (busy rises), then released — the same
    // request-until-ack rule as the custom path, so a trigger arriving while
    // the IP is mid-swap is never lost.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            vs_hw_triggers <= 2'b00;
        else if (pr_trigger && !pr_busy)
            vs_hw_triggers <= mode ? 2'b10 : 2'b01;
        else
            vs_hw_triggers <= 2'b00;
    end

endmodule

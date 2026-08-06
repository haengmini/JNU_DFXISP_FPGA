`timescale 1ns/1ps
// =============================================================================
// checker_hysteresis.v — static-region Schmitt mode arbiter + PR trigger source.
// Consumes the per-frame band flags exported by dfxisp_accel (hyst_flags value
// + hyst_flags_ap_vld pulse), owns the stable mode state, and on a mode
// transition raises pr_trigger, holding it until pr_controller accepts (busy).
// This implements the 2026-07-03 adoption (Schmitt delta = 2%p around the 62% center: enter 64 / exit 60, + min-dwell) in
// fabric — the PS only observes; it is not in the decision path.
// Flag encoding, protocol, and design notes: checker_hysteresis.md.
// =============================================================================
module checker_hysteresis #(
    // Frames a newly entered mode is held before the next switch is allowed.
    // Frames seen while a trigger is pending or a reconfiguration is busy do
    // not count. 1 covers PR latency < frame budget (14.5 ms < 33.3 ms typ).
    parameter integer DWELL_FRAMES = 1
) (
    input  wire clk,
    input  wire rst_n,

    // Per-frame flags from the HLS core
    input  wire flags_vld,      // hyst_flags_ap_vld: one pulse per completed frame
    input  wire above_enter,    // hyst_flags[0]: dark ratio > enter threshold (64%)
    input  wire below_exit,     // hyst_flags[1]: dark ratio < exit threshold (60%)

    // pr_controller handshake (results/pr_controller/pr_controller.v)
    input  wire pr_busy,        // ack: controller has started the reconfiguration
    output reg  pr_trigger,     // request: held high until pr_busy is observed

    output reg  mode            // stable mode: 0 = NORMAL, 1 = LOW_LIGHT
);

    // Counter width follows the parameter instead of a fixed 16 bits.
    localparam integer DWELL_W = (DWELL_FRAMES < 2) ? 1 : $clog2(DWELL_FRAMES + 1);
    reg [DWELL_W-1:0] dwell_cnt;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mode       <= 1'b0;   // reset into NORMAL (matches the RM_NORMAL default config)
            pr_trigger <= 1'b0;
            dwell_cnt  <= {DWELL_W{1'b0}};
        end else begin
            // Release the request as soon as the controller acknowledges by
            // going busy. pr_controller samples trigger only in S_IDLE, so
            // clearing on busy guarantees no spurious re-trigger when the
            // controller returns to IDLE after DONE.
            if (pr_trigger && pr_busy)
                pr_trigger <= 1'b0;

            // Decisions are frozen while a request is pending or a
            // reconfiguration is in flight (those frames were processed by
            // the outgoing RM anyway).
            if (flags_vld && !pr_trigger && !pr_busy) begin
                if (dwell_cnt != {DWELL_W{1'b0}}) begin
                    dwell_cnt <= dwell_cnt - 1'b1;
                end else if (!mode && above_enter) begin
                    mode       <= 1'b1;
                    pr_trigger <= 1'b1;
                    dwell_cnt  <= DWELL_FRAMES[DWELL_W-1:0];
                end else if (mode && below_exit) begin
                    mode       <= 1'b0;
                    pr_trigger <= 1'b1;
                    dwell_cnt  <= DWELL_FRAMES[DWELL_W-1:0];
                end
                // In-band frames (neither flag set) change nothing — this is
                // the Schmitt property the single-threshold checker lacked.
            end
        end
    end

endmodule

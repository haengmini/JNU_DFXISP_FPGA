`timescale 1ns/1ps
// =============================================================================
// pr_latency_probe.v — reconfiguration-latency instrument for the AMD DFX
// Controller path (2026-08-06). The IP provides status/error registers but no
// built-in latency counter; its handshake signals bound every phase, so this
// small counter block timestamps them instead:
//   drain_cycles = shutdown_req rise -> shutdown_ack rise  (RM drain time)
//   swap_cycles  = trigger observed  -> decouple release   (end-to-end swap)
// Replaces the archived pr_controller.v as the measurement instrument — that
// FSM timed its own ICAP streaming, which the IP now owns. On the board,
// read the outputs via an ILA or latch them into AXI GPIO/status registers.
// =============================================================================
module pr_latency_probe #(
    parameter integer CW = 32
) (
    input  wire clk,
    input  wire rst_n,

    input  wire trigger_seen,        // |vs_hw_triggers: swap request visible
    input  wire vs_rm_shutdown_req,
    input  wire vs_rm_shutdown_ack,
    input  wire vs_rm_decouple,

    output reg [CW-1:0] drain_cycles,
    output reg [CW-1:0] swap_cycles,
    output reg          meas_valid   // 1-cycle pulse when a swap completes
);

    reg running;
    reg decouple_q;
    reg [CW-1:0] swap_cnt, drain_cnt;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            running      <= 1'b0;
            decouple_q   <= 1'b0;
            swap_cnt     <= {CW{1'b0}};
            drain_cnt    <= {CW{1'b0}};
            drain_cycles <= {CW{1'b0}};
            swap_cycles  <= {CW{1'b0}};
            meas_valid   <= 1'b0;
        end else begin
            meas_valid <= 1'b0;
            decouple_q <= vs_rm_decouple;

            if (!running && trigger_seen) begin
                running   <= 1'b1;
                swap_cnt  <= {CW{1'b0}};
                drain_cnt <= {CW{1'b0}};
            end else if (running) begin
                swap_cnt <= swap_cnt + 1'b1;
                if (vs_rm_shutdown_req && !vs_rm_shutdown_ack)
                    drain_cnt <= drain_cnt + 1'b1;
                if (decouple_q && !vs_rm_decouple) begin
                    running      <= 1'b0;
                    swap_cycles  <= swap_cnt + 1'b1;
                    drain_cycles <= drain_cnt;
                    meas_valid   <= 1'b1;
                end
            end
        end
    end

endmodule

`timescale 1ns/1ps
// =============================================================================
// pr_controller.v -- First-pass DFX partial-reconfiguration controller FSM
// (improvement-strategy-2026-07-03.md Phase 2.1).
//
// Context: config1/config2 utilization reports (2026-07-03) show ZERO
// ICAPE3/STARTUPE3 instances in the current design -- there is no PR
// controller yet, which is also why yesterday's isolated-ICAPE3 simulation
// attempt (pr-latency-vivado-sim-2026-07-02.md) could never produce a
// trustworthy completion signal (ICAPE3's PRDONE depends on eos_startup,
// which needs a real STARTUPE3/device-level context this design doesn't
// have). This module sidesteps that entirely: instead of trusting ICAPE3's
// PRDONE, it completes based on its OWN word counter reaching the known
// partial-bitstream word count (171,633 words, verified 2026-07-02 by
// parsing the real rm_lowlight_partial.bit) -- a signal this design fully
// controls and can validate in simulation without needing STARTUPE3.
//
// Fabric-only version: partial bitstream source is a BRAM (pre-loaded via
// $readmemh in simulation / actual BRAM init in real use), NOT yet PS/DDR
// (that upgrade is Phase 3, per the strategy doc -- needs PS/DDR
// integration first).
//
// FSM: IDLE -> DRAIN_WAIT -> ICAP_ARM -> ICAP_STREAM -> DONE -> IDLE
// =============================================================================
module pr_controller #(
    parameter integer NWORDS = 171633   // real payload word count, both RMs (verified)
) (
    input  wire        clk,
    input  wire        rst_n,

    // Control interface
    input  wire        trigger,         // pulse: request a reconfiguration
    input  wire        drain_ready,     // active RM reports ap_idle (safe to reconfigure)
    output reg         busy,            // high while a reconfiguration is in flight
    output reg         done_pulse,      // one-cycle pulse when reconfiguration completes
    output reg  [17:0] word_count,      // live progress counter (debug/observability)

    // Bitstream BRAM source (fabric-only: pre-loaded, word-addressed)
    output reg  [17:0] bram_addr,
    input  wire [31:0] bram_data,
    output reg         bram_rd_en,

    // ICAPE3 interface (data path only -- completion NOT derived from PRDONE)
    output reg         icap_csib,       // active low
    output reg         icap_rdwrb,      // 0 = write
    output reg  [31:0] icap_i
);

    localparam S_IDLE        = 3'd0;
    localparam S_DRAIN_WAIT   = 3'd1;
    localparam S_ICAP_ARM     = 3'd2;
    localparam S_ICAP_FETCH   = 3'd3;  // BRAM read latency (1 cycle)
    localparam S_ICAP_WRITE   = 3'd4;
    localparam S_DONE         = 3'd5;

    reg [2:0] state;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state       <= S_IDLE;
            busy        <= 1'b0;
            done_pulse  <= 1'b0;
            word_count  <= 18'd0;
            bram_addr   <= 18'd0;
            bram_rd_en  <= 1'b0;
            icap_csib   <= 1'b1;   // inactive
            icap_rdwrb  <= 1'b0;   // write mode by default
            icap_i      <= 32'd0;
        end else begin
            done_pulse <= 1'b0;  // default: pulse only asserted explicitly below

            case (state)
                S_IDLE: begin
                    busy <= 1'b0;
                    if (trigger) begin
                        state <= S_DRAIN_WAIT;
                        busy  <= 1'b1;
                    end
                end

                // Wait for the currently-active RM to report idle before
                // touching the RP -- reconfiguring mid-frame would corrupt
                // whatever partial output is in flight.
                S_DRAIN_WAIT: begin
                    if (drain_ready) begin
                        state      <= S_ICAP_ARM;
                        word_count <= 18'd0;
                        bram_addr  <= 18'd0;
                    end
                end

                S_ICAP_ARM: begin
                    icap_csib  <= 1'b0;   // activate ICAP
                    icap_rdwrb <= 1'b0;   // write
                    bram_rd_en <= 1'b1;
                    state      <= S_ICAP_FETCH;
                end

                // BRAM synchronous read: address issued this cycle, data
                // valid next cycle.
                S_ICAP_FETCH: begin
                    state <= S_ICAP_WRITE;
                end

                S_ICAP_WRITE: begin
                    icap_i     <= bram_data;
                    word_count <= word_count + 1'b1;
                    if (word_count + 1'b1 >= NWORDS[17:0]) begin
                        state      <= S_DONE;
                        bram_rd_en <= 1'b0;
                        icap_csib  <= 1'b1;  // deactivate ICAP
                    end else begin
                        bram_addr <= bram_addr + 1'b1;
                        state     <= S_ICAP_FETCH;
                    end
                end

                S_DONE: begin
                    done_pulse <= 1'b1;
                    busy       <= 1'b0;
                    state      <= S_IDLE;
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule

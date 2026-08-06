`timescale 1ns/1ps
// PR (partial reconfiguration) latency testbench: drives the real
// rm_lowlight_partial.bit payload through Xilinx's ICAPE3 UNISIM behavioral
// model at its rated max clock (100 MHz, AMD UG570), and measures simulated
// time from a "DFX trigger" pulse (first active ICAP write) to the point
// where the REAL DESYNC command (Type1 write CMD=0x0000000D, opcode
// 0x30008001) appears in the actual generated bitstream content.
//
// Note: ICAPE3's own PRDONE output (via the embedded SIM_CONFIGE3 packet
// parser) was tried first and never asserts in an isolated ICAPE3-only
// testbench -- PRDONE requires eos_startup, which depends on a full device
// STARTUPE3/configuration-sequence simulation context this testbench does
// not provide. That is a genuine limitation of the UNISIM model for
// isolated ICAP-only simulation, not a testbench bug (see
// results/pr-latency-vivado-sim-2026-07-02.md). Detecting the real DESYNC
// command directly in the streamed content is the reliable alternative:
// it is the actual command that ends the configuration sequence in the
// real bitstream, independently located via post-processing at word index
// 171615 (of 171633 total words) for BOTH partial bitstreams -- identical
// position for both RMs, confirming they share the same frame-address
// command structure and differ only in frame data content.
module icap_pr_latency_tb;

  localparam integer NWORDS = 171633; // rm_lowlight_partial.bit payload word count (measured)
  reg [31:0] mem [0:NWORDS-1];

  reg clk = 0;
  reg csib = 1;
  reg rdwrb = 0; // 0 = write
  reg [31:0] i_data = 32'h0;

  wire avail;
  wire [31:0] o_data;
  wire prdone;
  wire prerror;

  // 100 MHz ICAP clock (AMD UG570 ICAPE3 max rate) -> 10 ns period
  always #5 clk = ~clk;

  ICAPE3 #(
    .DEVICE_ID(32'h04A63093),
    .ICAP_AUTO_SWITCH("DISABLE"),
    .SIM_CFG_FILE_NAME("NONE")
  ) dut (
    .AVAIL(avail),
    .O(o_data),
    .PRDONE(prdone),
    .PRERROR(prerror),
    .CLK(clk),
    .CSIB(csib),
    .I(i_data),
    .RDWRB(rdwrb)
  );

  integer widx;
  real t_trigger, t_desync;
  integer logf;
  reg found_desync, was_synced;
  // Probe the REAL packet parser's internal desync_flag directly (computed
  // by Xilinx's own SIM_CONFIGE3 model from genuine Type1/Type2 packet
  // framing) instead of naive byte-pattern matching, which produced a false
  // positive inside frame-data payload (word 6414) rather than the true
  // final DESYNC (word 171615, confirmed by offline python parse of the
  // actual bitstream file). desync_flag is high BY DEFAULT (reset/not-yet-
  // synced state, driven by ~rst_intl) and only goes low once the real SYNC
  // word is recognized -- so completion is the LOW->HIGH transition after
  // sync was first achieved, not the raw level (which is also high at t=0).
  wire [3:0] desync_flag_probe = dut.SIM_CONFIGE3_INST.desync_flag;

  initial begin
    $readmemh("rm_lowlight_partial.hex", mem);
    logf = $fopen("icap_pr_latency.log", "w");
    found_desync = 1'b0;
    was_synced = 1'b0;

    // Wait for the ICAPE3 model's own fixed startup sequence (PROG_B/INIT_B
    // toggle) to finish -- this is a simulation-model bring-up artifact of
    // the primitive itself, NOT part of the DFX reconfiguration event, so it
    // is deliberately excluded from the trigger-to-DESYNC measurement.
    wait (avail == 1'b1);
    @(negedge clk);

    // ---- DFX TRIGGER: first active ICAP write cycle ----
    t_trigger = $realtime;
    $display("[icap_pr_latency] TRIGGER at t=%0t ns (streaming up to %0d words of rm_lowlight_partial.bit)", $realtime, NWORDS);
    $fdisplay(logf, "TRIGGER_NS %0.3f", t_trigger);

    csib = 0;
    rdwrb = 0;
    for (widx = 0; widx < NWORDS && !found_desync; widx = widx + 1) begin
      i_data = mem[widx];
      @(negedge clk);
      if (!was_synced && desync_flag_probe == 4'b0000) begin
        was_synced = 1'b1;
        $display("[icap_pr_latency] SYNC achieved (desync_flag=0000) at word index %0d, t=%0t ns", widx, $realtime);
      end
      if (was_synced && (&desync_flag_probe)) begin
        found_desync = 1'b1;
        t_desync = $realtime;
        $display("[icap_pr_latency] Real packet-parser DESYNC (0000->1111) at word index %0d, t=%0t ns", widx, $realtime);
      end
    end
    csib = 1;

    if (found_desync) begin
      $display("[icap_pr_latency] TRIGGER_TO_DESYNC_NS %0.3f", t_desync - t_trigger);
      $fdisplay(logf, "DESYNC_NS %0.3f", t_desync);
      $fdisplay(logf, "TRIGGER_TO_DESYNC_NS %0.3f", t_desync - t_trigger);
      $fdisplay(logf, "WORD_INDEX_AT_DESYNC %0d", widx - 1);
    end else begin
      $display("[icap_pr_latency] ERROR: DESYNC command not observed after streaming all %0d words", NWORDS);
      $display("[icap_pr_latency] final desync_flag_probe = %b (was_synced=%b)", desync_flag_probe, was_synced);
      $fdisplay(logf, "ERROR_DESYNC_NOT_FOUND");
      $fdisplay(logf, "FINAL_DESYNC_FLAG %b WAS_SYNCED %b", desync_flag_probe, was_synced);
    end

    $display("[icap_pr_latency] PRDONE (ICAPE3 internal, for reference only) = %b, PRERROR = %b", prdone, prerror);
    $fdisplay(logf, "PRDONE_AT_END %b PRERROR_AT_END %b", prdone, prerror);

    $fclose(logf);
    #100;
    $finish;
  end

  initial begin
    #50_000_000; // 50 ms simulated-time safety timeout
    $display("[icap_pr_latency] TIMEOUT");
    $finish;
  end

endmodule

`timescale 1ns/1ps
// Testbench: simulates pr_controller streaming the REAL rm_lowlight_partial.bit
// payload (via a BRAM model pre-loaded with the same hex extracted 2026-07-02)
// from trigger to done_pulse. This is the trigger-to-complete measurement that
// yesterday's isolated-ICAPE3 attempt could not produce -- here, completion is
// generated entirely by logic this design owns (word counter), not by
// ICAPE3's PRDONE/eos_startup (which needs infrastructure we don't have yet).
module pr_controller_tb;

    localparam integer NWORDS = 171633;
    reg  [31:0] bitstream_mem [0:NWORDS-1];

    reg clk = 0;
    reg rst_n = 0;
    reg trigger = 0;
    reg drain_ready = 0;

    wire        busy;
    wire        done_pulse;
    wire [17:0] word_count;
    wire [17:0] bram_addr;
    wire        bram_rd_en;
    wire        icap_csib;
    wire        icap_rdwrb;
    wire [31:0] icap_i;

    // Synchronous BRAM read model (1-cycle latency, matches S_ICAP_FETCH assumption)
    reg [31:0] bram_data_r;
    always @(posedge clk) begin
        if (bram_rd_en)
            bram_data_r <= bitstream_mem[bram_addr];
    end

    // 200 MHz system clock (5 ns period) -- same target as the rest of the design
    always #2.5 clk = ~clk;

    pr_controller #(.NWORDS(NWORDS)) dut (
        .clk(clk), .rst_n(rst_n),
        .trigger(trigger), .drain_ready(drain_ready),
        .busy(busy), .done_pulse(done_pulse), .word_count(word_count),
        .bram_addr(bram_addr), .bram_data(bram_data_r), .bram_rd_en(bram_rd_en),
        .icap_csib(icap_csib), .icap_rdwrb(icap_rdwrb), .icap_i(icap_i)
    );

    real t_trigger, t_done;
    integer logf;

    initial begin
        $readmemh("rm_lowlight_partial.hex", bitstream_mem);
        logf = $fopen("pr_controller_latency.log", "w");

        rst_n = 0;
        trigger = 0;
        drain_ready = 0;
        repeat (4) @(posedge clk);
        rst_n = 1;
        repeat (4) @(posedge clk);

        // Simulate the active RM being busy for a bit, then reporting idle
        // (drain) -- exercises the DRAIN_WAIT state, not just an instant pass.
        drain_ready = 0;
        repeat (10) @(posedge clk);

        // ---- DFX TRIGGER ----
        t_trigger = $realtime;
        $display("[pr_controller] TRIGGER at t=%0t ns", $realtime);
        @(posedge clk);
        trigger = 1;
        @(posedge clk);
        trigger = 0;

        // Drain completes 20 cycles after trigger (simulated pipeline flush).
        repeat (20) @(posedge clk);
        drain_ready = 1;
        $display("[pr_controller] drain_ready asserted at t=%0t ns (word_count=%0d)", $realtime, word_count);

        wait (done_pulse == 1'b1);
        t_done = $realtime;
        @(posedge clk);
        drain_ready = 0;

        $display("[pr_controller] DONE at t=%0t ns, word_count=%0d", $realtime, word_count);
        $display("[pr_controller] TRIGGER_TO_DONE_NS %0.3f", t_done - t_trigger);
        $fdisplay(logf, "TRIGGER_NS %0.3f", t_trigger);
        $fdisplay(logf, "DONE_NS %0.3f", t_done);
        $fdisplay(logf, "TRIGGER_TO_DONE_NS %0.3f", t_done - t_trigger);
        $fdisplay(logf, "WORD_COUNT_AT_DONE %0d", word_count);
        $fclose(logf);

        #50;
        $finish;
    end

    initial begin
        #50_000_000;
        $display("[pr_controller] TIMEOUT");
        $finish;
    end

endmodule

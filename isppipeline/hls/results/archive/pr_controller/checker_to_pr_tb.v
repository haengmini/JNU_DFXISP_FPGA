`timescale 1ns/1ps
// End-to-end fabric trigger chain: checker_hysteresis -> pr_controller.
// A dark frame raises pr_trigger; the controller waits for drain (an RM
// ap_idle model), streams a short BRAM payload (NWORDS=16 override) to the
// ICAP port, and pulses done. Then a bright frame drives the reverse
// transition. Verifies the request/ack handshake end to end with no PS
// involvement — the wiring Stage 6 must reproduce with the real HLS core
// (hyst_flags/hyst_flags_ap_vld) and the real ICAPE3.
module checker_to_pr_tb;

    localparam integer TB_NWORDS = 16;

    reg clk = 0, rst_n = 0;
    reg flags_vld = 0, above_enter = 0, below_exit = 0;
    wire pr_trigger, mode, busy, done_pulse;
    wire [17:0] word_count, bram_addr;
    wire bram_rd_en;
    wire icap_csib, icap_rdwrb;
    wire [31:0] icap_i;

    reg [31:0] bram [0:TB_NWORDS-1];
    reg [31:0] bram_q;
    reg drain_ready;
    reg [3:0] drain_cnt;
    integer done_count = 0;
    integer i;

    checker_hysteresis #(.DWELL_FRAMES(0)) hyst (
        .clk(clk), .rst_n(rst_n),
        .flags_vld(flags_vld), .above_enter(above_enter), .below_exit(below_exit),
        .pr_busy(busy), .pr_trigger(pr_trigger), .mode(mode));

    pr_controller #(.NWORDS(TB_NWORDS)) ctrl (
        .clk(clk), .rst_n(rst_n),
        .trigger(pr_trigger), .drain_ready(drain_ready),
        .busy(busy), .done_pulse(done_pulse), .word_count(word_count),
        .bram_addr(bram_addr), .bram_data(bram_q), .bram_rd_en(bram_rd_en),
        .icap_csib(icap_csib), .icap_rdwrb(icap_rdwrb), .icap_i(icap_i));

    always #2.5 clk = ~clk;

    // Synchronous BRAM model (1-cycle read latency, as pr_controller expects).
    always @(posedge clk) if (bram_rd_en) bram_q <= bram[bram_addr];

    always @(posedge clk) if (done_pulse) done_count = done_count + 1;

    // RM ap_idle model: the active RM drains 8 cycles after a
    // reconfiguration goes busy, then reports idle until the PR completes.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            drain_cnt <= 4'd0; drain_ready <= 1'b0;
        end else if (!busy) begin
            drain_cnt <= 4'd0; drain_ready <= 1'b0;
        end else if (drain_cnt < 4'd8) begin
            drain_cnt <= drain_cnt + 4'd1;
        end else begin
            drain_ready <= 1'b1;
        end
    end

    task frame(input above, input below);
        begin
            @(negedge clk);
            above_enter = above; below_exit = below; flags_vld = 1;
            @(negedge clk);
            flags_vld = 0; above_enter = 0; below_exit = 0;
        end
    endtask

    initial begin
        for (i = 0; i < TB_NWORDS; i = i + 1) bram[i] = 32'hA5A50000 + i;

        repeat (3) @(negedge clk);
        rst_n = 1;
        @(negedge clk);

        // Dark scene: NORMAL -> LOW_LIGHT reconfiguration.
        frame(1, 0);
        if (pr_trigger !== 1'b1) begin $display("FAIL: no trigger after dark frame"); $fatal; end
        wait (done_pulse === 1'b1);
        repeat (4) @(negedge clk);
        if (done_count !== 1) begin $display("FAIL: expected 1 done, got %0d", done_count); $fatal; end
        if (mode !== 1'b1)    begin $display("FAIL: mode not LOW_LIGHT after PR"); $fatal; end
        if (busy !== 1'b0)    begin $display("FAIL: controller stuck busy"); $fatal; end
        if (pr_trigger !== 1'b0) begin $display("FAIL: trigger not released"); $fatal; end

        // Quiet frames: no re-trigger while nothing changes.
        frame(0, 0); frame(0, 0);
        repeat (4) @(negedge clk);
        if (done_count !== 1) begin $display("FAIL: spurious reconfiguration"); $fatal; end

        // Bright scene: LOW_LIGHT -> NORMAL reconfiguration.
        frame(0, 1);
        wait (done_pulse === 1'b1);
        repeat (4) @(negedge clk);
        if (done_count !== 2) begin $display("FAIL: expected 2 done, got %0d", done_count); $fatal; end
        if (mode !== 1'b0)    begin $display("FAIL: mode not back to NORMAL"); $fatal; end

        $display("checker_to_pr_tb PASS: 2 reconfigurations, %0d words each, no PS in the loop",
                 TB_NWORDS);
        $finish;
    end

    initial begin
        #200000;
        $display("checker_to_pr_tb TIMEOUT");
        $fatal;
    end

endmodule

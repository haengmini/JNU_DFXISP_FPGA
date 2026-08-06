`timescale 1ns/1ps
// End-to-end chain against a behavioral stand-in for the AMD DFX Controller:
//   checker_hysteresis -> dfxc_trigger_adapter -> [DFXC BFM] -> RM ap_idle model
// The BFM follows the PG374 sequence: hw trigger -> rm_shutdown_req -> wait
// rm_shutdown_ack -> decouple -> (fetch+ICAP program, modeled as PROG_CYCLES)
// -> release decouple. It is a contract model, NOT the real IP — the real IP
// must be configured (1 VS, 2 RMs, DDR address table) and this chain re-run
// as an IP-integrated simulation in Stage 6.
// Verifies: one-hot trigger targets the requested RM, shutdown is only acked
// when the RM is idle (drain), busy semantics clear the request exactly once,
// and the reverse transition works — no PS in the loop.
module checker_to_dfxc_tb;

    localparam integer PROG_CYCLES = 32;

    reg clk = 0, rst_n = 0;
    reg flags_vld = 0, above_enter = 0, below_exit = 0;
    wire pr_trigger, pr_busy, mode;
    wire [1:0] vs_hw_triggers;
    wire vs_rm_shutdown_ack;
    reg  vs_rm_shutdown_req = 0, vs_rm_decouple = 0;
    reg  rm_ap_idle = 1;
    integer swap_count = 0;
    integer i;
    reg [1:0] latched_target;

    checker_hysteresis #(.DWELL_FRAMES(0)) hyst (
        .clk(clk), .rst_n(rst_n),
        .flags_vld(flags_vld), .above_enter(above_enter), .below_exit(below_exit),
        .pr_busy(pr_busy), .pr_trigger(pr_trigger), .mode(mode));

    dfxc_trigger_adapter adapter (
        .clk(clk), .rst_n(rst_n),
        .mode(mode), .pr_trigger(pr_trigger), .pr_busy(pr_busy),
        .vs_hw_triggers(vs_hw_triggers),
        .vs_rm_shutdown_req(vs_rm_shutdown_req),
        .vs_rm_shutdown_ack(vs_rm_shutdown_ack),
        .vs_rm_decouple(vs_rm_decouple),
        .rm_ap_idle(rm_ap_idle));

    always #2.5 clk = ~clk;

    // --- DFXC behavioral model (PG374 sequence) ---
    task dfxc_swap;
        begin
            wait (vs_hw_triggers != 2'b00);
            latched_target = vs_hw_triggers;
            // RM busy with a frame when the request lands: drain takes time.
            rm_ap_idle = 0;
            @(negedge clk); vs_rm_shutdown_req = 1;
            // ack must NOT be granted while the RM is still working
            repeat (5) @(negedge clk);
            if (vs_rm_shutdown_ack !== 1'b0) begin
                $display("FAIL: shutdown acked while RM not idle"); $fatal;
            end
            rm_ap_idle = 1;   // frame done -> drain complete
            wait (vs_rm_shutdown_ack === 1'b1);
            @(negedge clk); vs_rm_shutdown_req = 0; vs_rm_decouple = 1;
            repeat (PROG_CYCLES) @(negedge clk);   // fetch + ICAP program
            vs_rm_decouple = 0;
            swap_count = swap_count + 1;
        end
    endtask

    task frame(input above, input below);
        begin
            @(negedge clk);
            above_enter = above; below_exit = below; flags_vld = 1;
            @(negedge clk);
            flags_vld = 0; above_enter = 0; below_exit = 0;
        end
    endtask

    initial begin
        repeat (3) @(negedge clk);
        rst_n = 1;
        @(negedge clk);

        // Dark scene: NORMAL -> LOW_LIGHT. Expect trigger bit [1].
        fork
            frame(1, 0);
            dfxc_swap;
        join
        if (latched_target !== 2'b10) begin
            $display("FAIL: expected trigger for RM_LOW_LIGHT, got %b", latched_target); $fatal;
        end
        repeat (4) @(negedge clk);
        if (mode !== 1'b1 || pr_trigger !== 1'b0 || pr_busy !== 1'b0) begin
            $display("FAIL: post-swap state mode=%b trig=%b busy=%b", mode, pr_trigger, pr_busy); $fatal;
        end

        // Quiet + in-band frames: no new swap.
        frame(0, 0); frame(0, 0);
        repeat (4) @(negedge clk);
        if (swap_count !== 1 || vs_hw_triggers !== 2'b00) begin
            $display("FAIL: spurious swap/trigger"); $fatal;
        end

        // Bright scene: LOW_LIGHT -> NORMAL. Expect trigger bit [0].
        fork
            frame(0, 1);
            dfxc_swap;
        join
        if (latched_target !== 2'b01) begin
            $display("FAIL: expected trigger for RM_NORMAL, got %b", latched_target); $fatal;
        end
        repeat (4) @(negedge clk);
        if (swap_count !== 2 || mode !== 1'b0) begin
            $display("FAIL: reverse swap incomplete"); $fatal;
        end

        $display("checker_to_dfxc_tb PASS: 2 swaps via DFXC contract model, drain enforced, no PS in the loop");
        $finish;
    end

    initial begin
        #200000;
        $display("checker_to_dfxc_tb TIMEOUT");
        $fatal;
    end

endmodule

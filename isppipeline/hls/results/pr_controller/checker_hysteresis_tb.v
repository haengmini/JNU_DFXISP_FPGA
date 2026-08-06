`timescale 1ns/1ps
// Self-checking testbench for checker_hysteresis: Schmitt band behavior
// (in-band frames change nothing), trigger held until busy ack, no spurious
// re-trigger, and min-dwell blocking the frame right after a switch.
module checker_hysteresis_tb;

    reg clk = 0, rst_n = 0;
    reg flags_vld = 0, above_enter = 0, below_exit = 0, pr_busy = 0;
    wire pr_trigger, mode;
    integer errors = 0;

    checker_hysteresis #(.DWELL_FRAMES(1)) dut (
        .clk(clk), .rst_n(rst_n),
        .flags_vld(flags_vld), .above_enter(above_enter), .below_exit(below_exit),
        .pr_busy(pr_busy), .pr_trigger(pr_trigger), .mode(mode));

    always #2.5 clk = ~clk;

    task frame(input above, input below);
        begin
            @(negedge clk);
            above_enter = above; below_exit = below; flags_vld = 1;
            @(negedge clk);
            flags_vld = 0; above_enter = 0; below_exit = 0;
        end
    endtask

    task check(input exp_mode, input exp_trig, input [255:0] tag);
        begin
            if (mode !== exp_mode || pr_trigger !== exp_trig) begin
                $display("FAIL %0s: mode=%b (exp %b) pr_trigger=%b (exp %b)",
                         tag, mode, exp_mode, pr_trigger, exp_trig);
                errors = errors + 1;
            end
        end
    endtask

    task accept;  // emulate pr_controller: busy pulse acknowledging the request
        begin
            @(negedge clk); pr_busy = 1;
            @(negedge clk); @(negedge clk); pr_busy = 0;
            @(negedge clk);
        end
    endtask

    initial begin
        repeat (3) @(negedge clk);
        rst_n = 1;
        @(negedge clk);
        check(0, 0, "reset-normal");

        // In-band frames from NORMAL: nothing happens (a single-threshold
        // checker flapping between 60.9% and 62.5% would toggle here).
        frame(0, 0); check(0, 0, "inband-normal");
        frame(0, 1); check(0, 0, "below-exit-while-already-normal");

        // Above-enter: switch to LOW_LIGHT, trigger raised and held.
        frame(1, 0); check(1, 1, "enter-lowlight-trigger");
        frame(0, 0); check(1, 1, "trigger-held-pending-frame-frozen");
        accept;      check(1, 0, "trigger-cleared-on-busy-ack");

        // Dwell (1 frame): the first decision frame after the switch is
        // consumed by the dwell counter even if it asks to exit.
        frame(0, 1); check(1, 0, "dwell-blocks-immediate-exit");
        // In-band: stays LOW_LIGHT (Schmitt memory).
        frame(0, 0); check(1, 0, "inband-holds-lowlight");
        // Below-exit: now allowed to return to NORMAL.
        frame(0, 1); check(0, 1, "exit-normal-trigger");
        accept;      check(0, 0, "second-trigger-cleared");

        // Quiet frames afterwards: no spurious re-trigger.
        frame(0, 0); frame(0, 1); check(0, 0, "no-spurious-retrigger");

        if (errors == 0) $display("checker_hysteresis_tb PASS");
        else begin
            $display("checker_hysteresis_tb FAIL (%0d errors)", errors);
            $fatal;
        end
        $finish;
    end

    initial begin
        #100000;
        $display("checker_hysteresis_tb TIMEOUT");
        $fatal;
    end

endmodule

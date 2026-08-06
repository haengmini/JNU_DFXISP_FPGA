# Arm1/Arm2 post-route using EXACTLY the Arm3 (dfx_flow.tcl) methodology,
# minus the DFX partition constraints. This is what makes the three arms
# comparable: same part, same tie-off static wrapper, same OOC-synth +
# read_checkpoint link. The ONLY difference vs config1/config2 is that we do
# not set HD.RECONFIGURABLE and do not create/attach a pblock -- so the delta
# isolates "cost of the DFX partition", not "cost of a different flow".
#
# Env: OOC_TOP (HLS top), OOC_RTL (dir of .v), OOC_WORK (out dir)
set part xczu7ev-ffvc1156-2-e
set top   $::env(OOC_TOP)
set rtl   $::env(OOC_RTL)
set work  $::env(OOC_WORK)
file mkdir $work

set dcp  [file join $work ${top}_ooc.dcp]
set stub [file join $work ${top}_stub.v]
set wrap [file join $work ${top}_static.v]

# 1) OOC synth of the HLS top (identical to dfx_flow.tcl's synth_rm)
read_verilog [glob [file join $rtl *.v]]
synth_design -top $top -part $part -mode out_of_context
write_checkpoint -force $dcp
write_verilog -force -mode synth_stub $stub
close_design

# 2) tie-off static wrapper — reuse the *verified* DFX generator unchanged.
#    It demands two stubs with identical signatures; passing the same stub
#    twice trivially satisfies that and yields the same wrapper shape used
#    for config1/config2 (black-box RM instantiated as u_rp).
set gen /home/mini/workspace/dfxisp/.claude/worktrees/hw-interface-prompt/isppipeline/hls/scripts/dfx/generate_static_wrapper.py
if {[catch {exec /usr/bin/env -u PYTHONHOME -u PYTHONPATH /usr/bin/python3 \
        $gen $stub $stub $wrap} res]} {
    error "wrapper generation failed: $res"
}
puts $res

# 3) synth the static wrapper (RM is a black box here)
read_verilog $wrap
synth_design -top dfx_static_top -part $part
create_clock -period 5.000 -name ap_clk [get_ports ap_clk]

# --- deliberately NOT doing (this is the Arm3-only part) ---
#   set_property HD.RECONFIGURABLE true [get_cells u_rp]
#
# OOC_PBLOCK=1: add config1's *floorplan* constraints but still no
# HD.RECONFIGURABLE. Control for "does the pblock alone explain why the DFX
# RP packs into fewer LUTs than the same module unconstrained?"
if {[info exists ::env(OOC_PBLOCK)] && $::env(OOC_PBLOCK) eq "1"} {
    set rp [get_cells u_rp]
    create_pblock pblock_rp
    resize_pblock [get_pblocks pblock_rp] -add {CLOCKREGION_X1Y0:CLOCKREGION_X2Y0}
    set_property SNAPPING_MODE OFF [get_pblocks pblock_rp]
    add_cells_to_pblock [get_pblocks pblock_rp] $rp
    set_property CONTAIN_ROUTING true [get_pblocks pblock_rp]
    set_property EXCLUDE_PLACEMENT true [get_pblocks pblock_rp]
    puts "OOC_PBLOCK: config1 floorplan constraints applied"
}

# 4) link the OOC netlist and implement
read_checkpoint -cell u_rp $dcp
opt_design
place_design
route_design
write_checkpoint -force [file join $work ${top}_ooc_routed.dcp]
report_utilization -file [file join $work ${top}_ooc.util.rpt]
report_timing_summary -file [file join $work ${top}_ooc.timing.rpt]
puts "OOC_IMPL_DONE $top"
exit

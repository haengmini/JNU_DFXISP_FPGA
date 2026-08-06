# Config1: synthesize both OOC RMs, generate the static wrapper, and implement NORMAL.
set part xczu7ev-ffvc1156-2-e
set work_root [expr {[info exists ::env(DFX_WORK_ROOT)] ? $::env(DFX_WORK_ROOT) : "/tmp/hls_dfxisp/dfx"}]
set repo_root [file normalize [file join [file dirname [info script]] ../../../..]]
set flat_root /tmp/hls_dfxisp/dfxisp_accel
set normal_rtl [file join $flat_root hls_rm_normal_tone_top solution1 impl ip hdl verilog]
set lowlight_rtl [file join $flat_root hls_rm_low_light_tone_top solution1 impl ip hdl verilog]
file mkdir $work_root

proc synth_rm {top rtl_dir dcp stub part} {
    read_verilog [glob -nocomplain [file join $rtl_dir *.v]]
    synth_design -top $top -part $part -mode out_of_context
    write_checkpoint -force $dcp
    write_verilog -force -mode synth_stub $stub
    close_design
}

set reuse_rm_synth [expr {[info exists ::env(DFX_REUSE_RM_SYNTH)] && $::env(DFX_REUSE_RM_SYNTH) eq "1"}]
if {!$reuse_rm_synth} {
    synth_rm rm_normal_tone_top $normal_rtl [file join $work_root rm_normal_synth.dcp] [file join $work_root rm_normal_stub.v] $part
    synth_rm rm_low_light_tone_top $lowlight_rtl [file join $work_root rm_lowlight_synth.dcp] [file join $work_root rm_lowlight_stub.v] $part
} else {
    puts "DFX_REUSE_RM_SYNTH=1: reusing previously verified OOC DCPs and stubs"
}

set generator [file join $repo_root isppipeline hls scripts dfx generate_static_wrapper.py]
set wrapper [file join $work_root dfx_static_top.v]
if {[catch {exec python3 $generator [file join $work_root rm_normal_stub.v] [file join $work_root rm_lowlight_stub.v] $wrapper} result]} {
    error "static wrapper generation failed: $result"
}
puts $result

read_verilog $wrapper
synth_design -top dfx_static_top -part $part
create_clock -period 5.000 -name ap_clk [get_ports ap_clk]
set rp [get_cells u_rp]
if {[llength $rp] != 1} { error "expected exactly one u_rp cell" }
set_property HD.RECONFIGURABLE true $rp
create_pblock pblock_rp_final
resize_pblock [get_pblocks pblock_rp_final] -add {CLOCKREGION_X1Y0:CLOCKREGION_X2Y0}
set_property SNAPPING_MODE OFF [get_pblocks pblock_rp_final]
add_cells_to_pblock [get_pblocks pblock_rp_final] $rp
set_property CONTAIN_ROUTING true [get_pblocks pblock_rp_final]
set_property EXCLUDE_PLACEMENT true [get_pblocks pblock_rp_final]
write_checkpoint -force [file join $work_root static_synth_final.dcp]
report_utilization -pblocks pblock_rp_final -file [file join $work_root pblock_capacity_final.rpt]

read_checkpoint -cell u_rp [file join $work_root rm_normal_synth.dcp]
opt_design
place_design
route_design
write_checkpoint -force [file join $work_root config1_normal_final.dcp]
report_utilization -file [file join $work_root config1_final.util.rpt]
report_timing_summary -file [file join $work_root config1_final.timing.rpt]
exit

# Config2: derive locked static from routed Config1, insert LOW_LIGHT, implement, verify.
set work_root [expr {[info exists ::env(DFX_WORK_ROOT)] ? $::env(DFX_WORK_ROOT) : "/tmp/hls_dfxisp/dfx"}]
open_checkpoint [file join $work_root config1_normal_final.dcp]
update_design -cell u_rp -black_box
lock_design -level routing
read_checkpoint -cell u_rp [file join $work_root rm_lowlight_synth.dcp]
opt_design
place_design
route_design
write_checkpoint -force [file join $work_root config2_lowlight_final.dcp]
report_utilization -file [file join $work_root config2_final.util.rpt]
report_timing_summary -file [file join $work_root config2_final.timing.rpt]
close_design
pr_verify -initial [file join $work_root config1_normal_final.dcp] \
    -additional [file join $work_root config2_lowlight_final.dcp] \
    -file [file join $work_root pr_verify_final.rpt]
exit

# Write one full and two partial bitstreams. Only fabric-only pin DRCs are relaxed.
set work_root [expr {[info exists ::env(DFX_WORK_ROOT)] ? $::env(DFX_WORK_ROOT) : "/tmp/hls_dfxisp/dfx"}]
set_property SEVERITY Warning [get_drc_checks NSTD-1]
set_property SEVERITY Warning [get_drc_checks UCIO-1]

open_checkpoint [file join $work_root config1_normal_final.dcp]
write_bitstream -force -file [file join $work_root config1_full_final.bit]
write_bitstream -force -cell u_rp -file [file join $work_root rm_normal_partial_final.bit]
close_design

open_checkpoint [file join $work_root config2_lowlight_final.dcp]
write_bitstream -force -cell u_rp -file [file join $work_root rm_lowlight_partial_final.bit]
close_design
exit

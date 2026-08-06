# Vitis HLS build scaffold for DFXISP.
# Defaults target ZCU104's Zynq UltraScale+ MPSoC part; override with:
#   DFXISP_HLS_PART=<part> DFXISP_HLS_CLOCK=<ns> DFXISP_HLS_FLOW=<csim|csynth|cosim|export> vitis_hls -f scripts/vitis_hls.tcl
# or Tcl args:
#   vitis_hls -f scripts/vitis_hls.tcl -- -part <part> -clock <ns> -flow <flow>

set script_dir [file dirname [file normalize [info script]]]
set hls_root [file normalize [file join $script_dir ".."]]

# Config is taken from ENV only (no argv parsing, so `vitis_hls -f` works):
#   DFXISP_HLS_TOP   top function name              (default dfxisp_accel)
#   DFXISP_HLS_SRC   source file (rel to hls_root)  (default src/dfxisp_accel.cpp)
#   DFXISP_HLS_TB    testbench file (optional)
#   DFXISP_HLS_PART / DFXISP_HLS_CLOCK / DFXISP_HLS_PROJECT / DFXISP_HLS_FLOW
#   DFXISP_HLS_FLOW in {csim, synth, csynth, cosim, export}  (synth = csynth only, no tb)
proc env_or {name def} {
    if {[info exists ::env($name)]} {
        return $::env($name)
    }
    return $def
}

set top_name     [env_or DFXISP_HLS_TOP   "dfxisp_accel"]
set src_file     [env_or DFXISP_HLS_SRC   "src/dfxisp_accel.cpp"]
set tb_file      [env_or DFXISP_HLS_TB    "tests/test_dfxisp_csim.cpp"]
set part_name    [env_or DFXISP_HLS_PART  "xczu7ev-ffvc1156-2-e"]
set clock_period [env_or DFXISP_HLS_CLOCK "5.0"]
set flow         [env_or DFXISP_HLS_FLOW  "csim"]
set project_dir  [file normalize [env_or DFXISP_HLS_PROJECT [file join $hls_root "build" "vitis_hls" $top_name]]]
set solution_name "solution1"

puts "DFXISP Vitis HLS scaffold"
puts "  top     : $top_name"
puts "  src     : $src_file"
puts "  project : $project_dir"
puts "  part    : $part_name / clock $clock_period ns / flow $flow"

# Run from hls_root so relative source paths resolve correctly (avoids HLS
# recording a wrong project-relative path that breaks csynth-only flows).
cd $hls_root
set cflags "-std=c++17 -Iinclude"
open_project -reset $project_dir
set_top $top_name
add_files $src_file -cflags $cflags
if {$flow ne "synth" && [file exists [file join $hls_root $tb_file]]} {
    add_files -tb $tb_file -cflags $cflags
}
open_solution -reset $solution_name
set_part $part_name
create_clock -period $clock_period -name default

switch -- $flow {
    csim   { csim_design }
    synth  { csynth_design }
    csynth { csim_design; csynth_design }
    cosim  { csim_design; csynth_design; cosim_design }
    export { csim_design; csynth_design; export_design -format ip_catalog }
    default { error "unsupported DFXISP_HLS_FLOW '$flow' (csim|synth|csynth|cosim|export)" }
}
close_project

# Generate + configure the AMD DFX Controller IP (xilinx.com:ip:dfx_controller:1.0)
# for DFXISP: 1 Virtual Socket, 2 HW triggers mapped trigger0->RM_0(NORMAL) /
# trigger1->RM_1(LOW_LIGHT). Verified on Vivado 2024.1 (2026-08-06) — this
# script reproduces the port-contract probe recorded in
# results/pr_controller/dfxc_adapter.md.
#
# Known 2024.1 batch-mode quirks found during the probe:
#  * Setting CONFIG.ALL_PARAMS directly fails with "GUI_SELECT_TRIGGER_3 = -1"
#    — use the documented dotted-path Tcl API (api.tcl) instead.
#  * VS-level keys (NUM_HW_TRIGGERS, TRIGGERx_TO_RM, POR_RM) set fine via the
#    API; RM-level keys (SHUTDOWN_REQUIRED/RESET_REQUIRED/BS.*) errored in
#    batch here — configure those in the IP customization GUI / Block Design
#    at Stage 6 and re-verify. SHUTDOWN_REQUIRED must become "hw" for the
#    rm_shutdown_req/ack handshake to engage.
#
# Usage: vivado -mode batch -source gen_dfx_controller.tcl
#        (expects an open/created project targeting xczu7ev-ffvc1156-2-e)

set part xczu7ev-ffvc1156-2-e
if {[catch {current_project}]} {
    create_project -force dfxc_ip ./dfxc_ip_proj -part $part
}

create_ip -name dfx_controller -vendor xilinx.com -library ip -version 1.0 \
    -module_name dfx_controller_0

source [file join [get_property REPOSITORY [get_ipdefs xilinx.com:ip:dfx_controller:1.0]] \
        xilinx dfx_controller_v1_0 tcl api.tcl]

set ip [get_ips dfx_controller_0]
dfx_controller_v1_0::set_property CONFIG.VS.VS_0.NUM_HW_TRIGGERS 2 $ip
dfx_controller_v1_0::set_property CONFIG.VS.VS_0.TRIGGER0_TO_RM RM_0 $ip
dfx_controller_v1_0::set_property CONFIG.VS.VS_0.TRIGGER1_TO_RM RM_1 $ip
dfx_controller_v1_0::set_property CONFIG.VS.VS_0.POR_RM RM_0 $ip
# TODO (Stage 6, via GUI/BD): per-RM SHUTDOWN_REQUIRED hw, RESET_REQUIRED low,
# RESET_DURATION, BS.0.ADDRESS/SIZE (partial bitstream table in DDR).

generate_target {instantiation_template synthesis} $ip
puts "ALL_PARAMS: [get_property CONFIG.ALL_PARAMS $ip]"
puts "Done — port template: <proj>.gen/sources_1/ip/dfx_controller_0/dfx_controller_0.veo"

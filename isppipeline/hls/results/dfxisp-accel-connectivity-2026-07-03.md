<!--
=============================================================================
File   : isppipeline/hls/results/dfxisp-accel-connectivity-2026-07-03.md
Date   : 2026-07-03
Function: dfxisp_accel(unified top)이 직접 instantiate하는 6개 서브모듈 인스턴스의
          포트-와이어 연결을 deliverables/verilog/dfxisp_accel/dfxisp_accel.v에서
          기계적으로 전수 추출한 연결 테이블. "signal wire를 모두 포함하는
          마이크로아키텍처가 필요해" 요청에 대한 응답 — 박스+화살표 다이어그램으로는
          434개 포트를 사람이 읽을 수 있게 그릴 수 없어(HLS 자동 생성 wire 이름이
          인스턴스당 50~100개), 완전성이 필요한 부분은 이 표로, 가독성이 필요한
          핵심 데이터 흐름은 별도 SVG 다이어그램으로 분리했다.
=============================================================================
-->
# dfxisp_accel top-level 연결 테이블 (전체 signal wire, 자동 추출)

> 소스: `deliverables/verilog/dfxisp_accel/dfxisp_accel.v`(2026-07-03 BLC-fix
> 재합성본). top이 **직접** instantiate하는 6개 인스턴스, 434개 포트 연결 전수.
> Python 정규식으로 `.port(wire)` 패턴을 기계적으로 파싱 — 수작업 전사 없음.
> 서브모듈 내부(예: `run_normal` 안의 `mul_*`/`sparsemux_*` 곱셈기·먹스)까지
> 내려가면 인스턴스당 포트가 다시 50~100개씩 늘어나 이 문서는 **top 레벨 1단계까지만**
> 다룬다 — 더 깊은 레벨이 필요하면 해당 서브모듈 파일을 같은 방식으로 재파싱하면 된다.

## 요약

| 인스턴스 | 타입 | 포트 수 | 상수 tie-off |
|---|---|---:|---:|
| `PIPE151(checker/dark-scan)` | `dfxisp_accel_dfxisp_accel_Pipeline_VITIS_LOOP_151_1` | 57 | 10 |
| `run_normal` | `dfxisp_accel_p_anonymous_namespace_run_normal` | 102 | 21 |
| `run_low_light` | `dfxisp_accel_p_anonymous_namespace_run_low_light` | 104 | 21 |
| `control_s_axi` | `dfxisp_accel_control_s_axi` | 39 | 1 |
| `gmem0_m_axi` | `dfxisp_accel_gmem0_m_axi` | 66 | 8 |
| `gmem1_m_axi` | `dfxisp_accel_gmem1_m_axi` | 66 | 5 |
| **합계** | | **434** | **66** |

## 파싱으로 확인된 핵심 구조 (표를 읽기 전 요약)

- **`control_s_axi_U`**가 AXI-Lite로 프로그램되는 진짜 레지스터(`raw_bayer`, `rgb_out`,
  `width`, `height`, `mode`, `dark_pixel_threshold`)를 출력하고, 계산 결과 메타데이터
  (`out_width`/`out_height`/`selected_mode`/`selected_rm` + 각 `_ap_vld`)를 입력받는다.
- 이 레지스터들은 top 모듈 자체의 always-block에서 `*_read_reg_*` 이름으로 재래치된 뒤
  `run_normal`/`run_low_light`에 전달된다(예: `raw_bayer` → `raw_bayer_read_reg_457`).
- **`PIPE151`**이 checker 역할: `dark_pixel_threshold_read_reg_430`과 `n_reg_486`
  (=width×height로 추정)을 받아 `dark_out`(어두운 픽셀 카운트)을 계산.
- `dark_out`은 top 모듈에서 `<<5`/`<<2`/`<<7` 시프트(=×32/×4/×128, 합쳐서 ×100 계열
  정수 곱셈 분해)를 거쳐 `dark_ratio` 퍼센트 비교로 이어짐 — RESEARCH.md §5.1의
  `dark_count*100 > threshold*(W*H)` 정수비교가 RTL에 그대로 나타난 것.
- **`gmem0_m_axi`(읽기)**는 `PIPE151`/`run_normal`/`run_low_light` 셋이 공유
  (`gmem0_ARREADY`/`RDATA`/`RVALID`/`RFIFONUM` 3-way shared) — 셋 다 raw_bayer를
  읽어야 하므로 하나의 AXI read 채널을 나눠 쓴다.
- **`gmem1_m_axi`(쓰기)**는 `run_normal`/`run_low_light`만 공유 — 이 둘은 상호배타적으로
  하나만 ap_start되므로(RESEARCH §2 mutual exclusivity) 실제 충돌은 없음.

---

## PIPE151(checker/dark-scan) (57 ports)

| port | connected wire/const |
|---|---|
| `ap_clk` | `ap_clk` |
| `ap_rst` | `ap_rst_n_inv` |
| `ap_start` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_ap_start` |
| `ap_done` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_ap_done` |
| `ap_idle` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_ap_idle` |
| `ap_ready` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_ap_ready` |
| `m_axi_gmem0_AWVALID` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWVALID` |
| `m_axi_gmem0_AWREADY` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_AWADDR` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWADDR` |
| `m_axi_gmem0_AWID` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWID` |
| `m_axi_gmem0_AWLEN` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWLEN` |
| `m_axi_gmem0_AWSIZE` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWSIZE` |
| `m_axi_gmem0_AWBURST` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWBURST` |
| `m_axi_gmem0_AWLOCK` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWLOCK` |
| `m_axi_gmem0_AWCACHE` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWCACHE` |
| `m_axi_gmem0_AWPROT` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWPROT` |
| `m_axi_gmem0_AWQOS` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWQOS` |
| `m_axi_gmem0_AWREGION` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWREGION` |
| `m_axi_gmem0_AWUSER` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_AWUSER` |
| `m_axi_gmem0_WVALID` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_WVALID` |
| `m_axi_gmem0_WREADY` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_WDATA` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_WDATA` |
| `m_axi_gmem0_WSTRB` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_WSTRB` |
| `m_axi_gmem0_WLAST` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_WLAST` |
| `m_axi_gmem0_WID` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_WID` |
| `m_axi_gmem0_WUSER` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_WUSER` |
| `m_axi_gmem0_ARVALID` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARVALID` |
| `m_axi_gmem0_ARREADY` | `gmem0_ARREADY` |
| `m_axi_gmem0_ARADDR` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARADDR` |
| `m_axi_gmem0_ARID` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARID` |
| `m_axi_gmem0_ARLEN` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARLEN` |
| `m_axi_gmem0_ARSIZE` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARSIZE` |
| `m_axi_gmem0_ARBURST` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARBURST` |
| `m_axi_gmem0_ARLOCK` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARLOCK` |
| `m_axi_gmem0_ARCACHE` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARCACHE` |
| `m_axi_gmem0_ARPROT` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARPROT` |
| `m_axi_gmem0_ARQOS` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARQOS` |
| `m_axi_gmem0_ARREGION` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARREGION` |
| `m_axi_gmem0_ARUSER` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_ARUSER` |
| `m_axi_gmem0_RVALID` | `gmem0_RVALID` |
| `m_axi_gmem0_RREADY` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_RREADY` |
| `m_axi_gmem0_RDATA` | `gmem0_RDATA` |
| `m_axi_gmem0_RLAST` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_RID` | `1'd0` *(tied-off)* |
| `m_axi_gmem0_RFIFONUM` | `gmem0_RFIFONUM` |
| `m_axi_gmem0_RUSER` | `1'd0` *(tied-off)* |
| `m_axi_gmem0_RRESP` | `2'd0` *(tied-off)* |
| `m_axi_gmem0_BVALID` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_BREADY` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_m_axi_gmem0_BREADY` |
| `m_axi_gmem0_BRESP` | `2'd0` *(tied-off)* |
| `m_axi_gmem0_BID` | `1'd0` *(tied-off)* |
| `m_axi_gmem0_BUSER` | `1'd0` *(tied-off)* |
| `n` | `n_reg_486` |
| `sext_ln151` | `trunc_ln1_reg_504` |
| `dark_pixel_threshold` | `dark_pixel_threshold_read_reg_430` |
| `dark_out` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_dark_out` |
| `dark_out_ap_vld` | `grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245_dark_out_ap_vld` |

## run_normal (102 ports)

| port | connected wire/const |
|---|---|
| `ap_clk` | `ap_clk` |
| `ap_rst` | `ap_rst_n_inv` |
| `ap_start` | `grp_p_anonymous_namespace_run_normal_fu_255_ap_start` |
| `ap_done` | `grp_p_anonymous_namespace_run_normal_fu_255_ap_done` |
| `ap_idle` | `grp_p_anonymous_namespace_run_normal_fu_255_ap_idle` |
| `ap_ready` | `grp_p_anonymous_namespace_run_normal_fu_255_ap_ready` |
| `m_axi_gmem0_AWVALID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWVALID` |
| `m_axi_gmem0_AWREADY` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_AWADDR` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWADDR` |
| `m_axi_gmem0_AWID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWID` |
| `m_axi_gmem0_AWLEN` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWLEN` |
| `m_axi_gmem0_AWSIZE` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWSIZE` |
| `m_axi_gmem0_AWBURST` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWBURST` |
| `m_axi_gmem0_AWLOCK` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWLOCK` |
| `m_axi_gmem0_AWCACHE` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWCACHE` |
| `m_axi_gmem0_AWPROT` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWPROT` |
| `m_axi_gmem0_AWQOS` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWQOS` |
| `m_axi_gmem0_AWREGION` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWREGION` |
| `m_axi_gmem0_AWUSER` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_AWUSER` |
| `m_axi_gmem0_WVALID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_WVALID` |
| `m_axi_gmem0_WREADY` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_WDATA` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_WDATA` |
| `m_axi_gmem0_WSTRB` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_WSTRB` |
| `m_axi_gmem0_WLAST` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_WLAST` |
| `m_axi_gmem0_WID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_WID` |
| `m_axi_gmem0_WUSER` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_WUSER` |
| `m_axi_gmem0_ARVALID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARVALID` |
| `m_axi_gmem0_ARREADY` | `gmem0_ARREADY` |
| `m_axi_gmem0_ARADDR` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARADDR` |
| `m_axi_gmem0_ARID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARID` |
| `m_axi_gmem0_ARLEN` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARLEN` |
| `m_axi_gmem0_ARSIZE` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARSIZE` |
| `m_axi_gmem0_ARBURST` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARBURST` |
| `m_axi_gmem0_ARLOCK` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARLOCK` |
| `m_axi_gmem0_ARCACHE` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARCACHE` |
| `m_axi_gmem0_ARPROT` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARPROT` |
| `m_axi_gmem0_ARQOS` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARQOS` |
| `m_axi_gmem0_ARREGION` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARREGION` |
| `m_axi_gmem0_ARUSER` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_ARUSER` |
| `m_axi_gmem0_RVALID` | `gmem0_RVALID` |
| `m_axi_gmem0_RREADY` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_RREADY` |
| `m_axi_gmem0_RDATA` | `gmem0_RDATA` |
| `m_axi_gmem0_RLAST` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_RID` | `1'd0` *(tied-off)* |
| `m_axi_gmem0_RFIFONUM` | `gmem0_RFIFONUM` |
| `m_axi_gmem0_RUSER` | `1'd0` *(tied-off)* |
| `m_axi_gmem0_RRESP` | `2'd0` *(tied-off)* |
| `m_axi_gmem0_BVALID` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_BREADY` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem0_BREADY` |
| `m_axi_gmem0_BRESP` | `2'd0` *(tied-off)* |
| `m_axi_gmem0_BID` | `1'd0` *(tied-off)* |
| `m_axi_gmem0_BUSER` | `1'd0` *(tied-off)* |
| `raw` | `raw_bayer_read_reg_457` |
| `m_axi_gmem1_AWVALID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWVALID` |
| `m_axi_gmem1_AWREADY` | `gmem1_AWREADY` |
| `m_axi_gmem1_AWADDR` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWADDR` |
| `m_axi_gmem1_AWID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWID` |
| `m_axi_gmem1_AWLEN` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWLEN` |
| `m_axi_gmem1_AWSIZE` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWSIZE` |
| `m_axi_gmem1_AWBURST` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWBURST` |
| `m_axi_gmem1_AWLOCK` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWLOCK` |
| `m_axi_gmem1_AWCACHE` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWCACHE` |
| `m_axi_gmem1_AWPROT` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWPROT` |
| `m_axi_gmem1_AWQOS` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWQOS` |
| `m_axi_gmem1_AWREGION` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWREGION` |
| `m_axi_gmem1_AWUSER` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_AWUSER` |
| `m_axi_gmem1_WVALID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_WVALID` |
| `m_axi_gmem1_WREADY` | `gmem1_WREADY` |
| `m_axi_gmem1_WDATA` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_WDATA` |
| `m_axi_gmem1_WSTRB` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_WSTRB` |
| `m_axi_gmem1_WLAST` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_WLAST` |
| `m_axi_gmem1_WID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_WID` |
| `m_axi_gmem1_WUSER` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_WUSER` |
| `m_axi_gmem1_ARVALID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARVALID` |
| `m_axi_gmem1_ARREADY` | `1'b0` *(tied-off)* |
| `m_axi_gmem1_ARADDR` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARADDR` |
| `m_axi_gmem1_ARID` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARID` |
| `m_axi_gmem1_ARLEN` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARLEN` |
| `m_axi_gmem1_ARSIZE` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARSIZE` |
| `m_axi_gmem1_ARBURST` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARBURST` |
| `m_axi_gmem1_ARLOCK` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARLOCK` |
| `m_axi_gmem1_ARCACHE` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARCACHE` |
| `m_axi_gmem1_ARPROT` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARPROT` |
| `m_axi_gmem1_ARQOS` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARQOS` |
| `m_axi_gmem1_ARREGION` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARREGION` |
| `m_axi_gmem1_ARUSER` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_ARUSER` |
| `m_axi_gmem1_RVALID` | `1'b0` *(tied-off)* |
| `m_axi_gmem1_RREADY` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_RREADY` |
| `m_axi_gmem1_RDATA` | `32'd0` *(tied-off)* |
| `m_axi_gmem1_RLAST` | `1'b0` *(tied-off)* |
| `m_axi_gmem1_RID` | `1'd0` *(tied-off)* |
| `m_axi_gmem1_RFIFONUM` | `9'd0` *(tied-off)* |
| `m_axi_gmem1_RUSER` | `1'd0` *(tied-off)* |
| `m_axi_gmem1_RRESP` | `2'd0` *(tied-off)* |
| `m_axi_gmem1_BVALID` | `gmem1_BVALID` |
| `m_axi_gmem1_BREADY` | `grp_p_anonymous_namespace_run_normal_fu_255_m_axi_gmem1_BREADY` |
| `m_axi_gmem1_BRESP` | `2'd0` *(tied-off)* |
| `m_axi_gmem1_BID` | `1'd0` *(tied-off)* |
| `m_axi_gmem1_BUSER` | `1'd0` *(tied-off)* |
| `rgb_out` | `rgb_out_read_reg_451` |
| `width` | `trunc_ln376_1_reg_476` |
| `height` | `trunc_ln376_reg_470` |

## run_low_light (104 ports)

| port | connected wire/const |
|---|---|
| `ap_clk` | `ap_clk` |
| `ap_rst` | `ap_rst_n_inv` |
| `ap_start` | `grp_p_anonymous_namespace_run_low_light_fu_269_ap_start` |
| `ap_done` | `grp_p_anonymous_namespace_run_low_light_fu_269_ap_done` |
| `ap_idle` | `grp_p_anonymous_namespace_run_low_light_fu_269_ap_idle` |
| `ap_ready` | `grp_p_anonymous_namespace_run_low_light_fu_269_ap_ready` |
| `m_axi_gmem0_AWVALID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWVALID` |
| `m_axi_gmem0_AWREADY` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_AWADDR` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWADDR` |
| `m_axi_gmem0_AWID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWID` |
| `m_axi_gmem0_AWLEN` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWLEN` |
| `m_axi_gmem0_AWSIZE` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWSIZE` |
| `m_axi_gmem0_AWBURST` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWBURST` |
| `m_axi_gmem0_AWLOCK` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWLOCK` |
| `m_axi_gmem0_AWCACHE` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWCACHE` |
| `m_axi_gmem0_AWPROT` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWPROT` |
| `m_axi_gmem0_AWQOS` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWQOS` |
| `m_axi_gmem0_AWREGION` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWREGION` |
| `m_axi_gmem0_AWUSER` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_AWUSER` |
| `m_axi_gmem0_WVALID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_WVALID` |
| `m_axi_gmem0_WREADY` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_WDATA` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_WDATA` |
| `m_axi_gmem0_WSTRB` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_WSTRB` |
| `m_axi_gmem0_WLAST` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_WLAST` |
| `m_axi_gmem0_WID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_WID` |
| `m_axi_gmem0_WUSER` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_WUSER` |
| `m_axi_gmem0_ARVALID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARVALID` |
| `m_axi_gmem0_ARREADY` | `gmem0_ARREADY` |
| `m_axi_gmem0_ARADDR` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARADDR` |
| `m_axi_gmem0_ARID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARID` |
| `m_axi_gmem0_ARLEN` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARLEN` |
| `m_axi_gmem0_ARSIZE` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARSIZE` |
| `m_axi_gmem0_ARBURST` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARBURST` |
| `m_axi_gmem0_ARLOCK` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARLOCK` |
| `m_axi_gmem0_ARCACHE` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARCACHE` |
| `m_axi_gmem0_ARPROT` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARPROT` |
| `m_axi_gmem0_ARQOS` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARQOS` |
| `m_axi_gmem0_ARREGION` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARREGION` |
| `m_axi_gmem0_ARUSER` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_ARUSER` |
| `m_axi_gmem0_RVALID` | `gmem0_RVALID` |
| `m_axi_gmem0_RREADY` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_RREADY` |
| `m_axi_gmem0_RDATA` | `gmem0_RDATA` |
| `m_axi_gmem0_RLAST` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_RID` | `1'd0` *(tied-off)* |
| `m_axi_gmem0_RFIFONUM` | `gmem0_RFIFONUM` |
| `m_axi_gmem0_RUSER` | `1'd0` *(tied-off)* |
| `m_axi_gmem0_RRESP` | `2'd0` *(tied-off)* |
| `m_axi_gmem0_BVALID` | `1'b0` *(tied-off)* |
| `m_axi_gmem0_BREADY` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem0_BREADY` |
| `m_axi_gmem0_BRESP` | `2'd0` *(tied-off)* |
| `m_axi_gmem0_BID` | `1'd0` *(tied-off)* |
| `m_axi_gmem0_BUSER` | `1'd0` *(tied-off)* |
| `raw` | `raw_bayer_read_reg_457` |
| `m_axi_gmem1_AWVALID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWVALID` |
| `m_axi_gmem1_AWREADY` | `gmem1_AWREADY` |
| `m_axi_gmem1_AWADDR` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWADDR` |
| `m_axi_gmem1_AWID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWID` |
| `m_axi_gmem1_AWLEN` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWLEN` |
| `m_axi_gmem1_AWSIZE` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWSIZE` |
| `m_axi_gmem1_AWBURST` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWBURST` |
| `m_axi_gmem1_AWLOCK` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWLOCK` |
| `m_axi_gmem1_AWCACHE` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWCACHE` |
| `m_axi_gmem1_AWPROT` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWPROT` |
| `m_axi_gmem1_AWQOS` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWQOS` |
| `m_axi_gmem1_AWREGION` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWREGION` |
| `m_axi_gmem1_AWUSER` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_AWUSER` |
| `m_axi_gmem1_WVALID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_WVALID` |
| `m_axi_gmem1_WREADY` | `gmem1_WREADY` |
| `m_axi_gmem1_WDATA` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_WDATA` |
| `m_axi_gmem1_WSTRB` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_WSTRB` |
| `m_axi_gmem1_WLAST` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_WLAST` |
| `m_axi_gmem1_WID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_WID` |
| `m_axi_gmem1_WUSER` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_WUSER` |
| `m_axi_gmem1_ARVALID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARVALID` |
| `m_axi_gmem1_ARREADY` | `1'b0` *(tied-off)* |
| `m_axi_gmem1_ARADDR` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARADDR` |
| `m_axi_gmem1_ARID` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARID` |
| `m_axi_gmem1_ARLEN` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARLEN` |
| `m_axi_gmem1_ARSIZE` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARSIZE` |
| `m_axi_gmem1_ARBURST` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARBURST` |
| `m_axi_gmem1_ARLOCK` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARLOCK` |
| `m_axi_gmem1_ARCACHE` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARCACHE` |
| `m_axi_gmem1_ARPROT` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARPROT` |
| `m_axi_gmem1_ARQOS` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARQOS` |
| `m_axi_gmem1_ARREGION` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARREGION` |
| `m_axi_gmem1_ARUSER` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_ARUSER` |
| `m_axi_gmem1_RVALID` | `1'b0` *(tied-off)* |
| `m_axi_gmem1_RREADY` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_RREADY` |
| `m_axi_gmem1_RDATA` | `32'd0` *(tied-off)* |
| `m_axi_gmem1_RLAST` | `1'b0` *(tied-off)* |
| `m_axi_gmem1_RID` | `1'd0` *(tied-off)* |
| `m_axi_gmem1_RFIFONUM` | `9'd0` *(tied-off)* |
| `m_axi_gmem1_RUSER` | `1'd0` *(tied-off)* |
| `m_axi_gmem1_RRESP` | `2'd0` *(tied-off)* |
| `m_axi_gmem1_BVALID` | `gmem1_BVALID` |
| `m_axi_gmem1_BREADY` | `grp_p_anonymous_namespace_run_low_light_fu_269_m_axi_gmem1_BREADY` |
| `m_axi_gmem1_BRESP` | `2'd0` *(tied-off)* |
| `m_axi_gmem1_BID` | `1'd0` *(tied-off)* |
| `m_axi_gmem1_BUSER` | `1'd0` *(tied-off)* |
| `rgb_out` | `rgb_out_read_reg_451` |
| `out_width_read` | `trunc_ln376_1_reg_476` |
| `out_height_read` | `trunc_ln376_reg_470` |
| `ap_return_0` | `grp_p_anonymous_namespace_run_low_light_fu_269_ap_return_0` |
| `ap_return_1` | `grp_p_anonymous_namespace_run_low_light_fu_269_ap_return_1` |

## control_s_axi (39 ports)

| port | connected wire/const |
|---|---|
| `AWVALID` | `s_axi_control_AWVALID` |
| `AWREADY` | `s_axi_control_AWREADY` |
| `AWADDR` | `s_axi_control_AWADDR` |
| `WVALID` | `s_axi_control_WVALID` |
| `WREADY` | `s_axi_control_WREADY` |
| `WDATA` | `s_axi_control_WDATA` |
| `WSTRB` | `s_axi_control_WSTRB` |
| `ARVALID` | `s_axi_control_ARVALID` |
| `ARREADY` | `s_axi_control_ARREADY` |
| `ARADDR` | `s_axi_control_ARADDR` |
| `RVALID` | `s_axi_control_RVALID` |
| `RREADY` | `s_axi_control_RREADY` |
| `RDATA` | `s_axi_control_RDATA` |
| `RRESP` | `s_axi_control_RRESP` |
| `BVALID` | `s_axi_control_BVALID` |
| `BREADY` | `s_axi_control_BREADY` |
| `BRESP` | `s_axi_control_BRESP` |
| `ACLK` | `ap_clk` |
| `ARESET` | `ap_rst_n_inv` |
| `ACLK_EN` | `1'b1` *(tied-off)* |
| `raw_bayer` | `raw_bayer` |
| `rgb_out` | `rgb_out` |
| `width` | `width` |
| `height` | `height` |
| `mode` | `mode` |
| `dark_pixel_threshold` | `dark_pixel_threshold` |
| `out_width` | `ap_phi_mux_storemerge_phi_fu_206_p6` |
| `out_width_ap_vld` | `out_width_ap_vld` |
| `out_height` | `ap_phi_mux_storemerge2_phi_fu_220_p6` |
| `out_height_ap_vld` | `out_height_ap_vld` |
| `selected_mode` | `zext_ln411_fu_424_p1` |
| `selected_mode_ap_vld` | `selected_mode_ap_vld` |
| `selected_rm` | `zext_ln411_fu_424_p1` |
| `selected_rm_ap_vld` | `selected_rm_ap_vld` |
| `ap_start` | `ap_start` |
| `interrupt` | `interrupt` |
| `ap_ready` | `ap_ready` |
| `ap_done` | `ap_done` |
| `ap_idle` | `ap_idle` |

## gmem0_m_axi (66 ports)

| port | connected wire/const |
|---|---|
| `AWVALID` | `m_axi_gmem0_AWVALID` |
| `AWREADY` | `m_axi_gmem0_AWREADY` |
| `AWADDR` | `m_axi_gmem0_AWADDR` |
| `AWID` | `m_axi_gmem0_AWID` |
| `AWLEN` | `m_axi_gmem0_AWLEN` |
| `AWSIZE` | `m_axi_gmem0_AWSIZE` |
| `AWBURST` | `m_axi_gmem0_AWBURST` |
| `AWLOCK` | `m_axi_gmem0_AWLOCK` |
| `AWCACHE` | `m_axi_gmem0_AWCACHE` |
| `AWPROT` | `m_axi_gmem0_AWPROT` |
| `AWQOS` | `m_axi_gmem0_AWQOS` |
| `AWREGION` | `m_axi_gmem0_AWREGION` |
| `AWUSER` | `m_axi_gmem0_AWUSER` |
| `WVALID` | `m_axi_gmem0_WVALID` |
| `WREADY` | `m_axi_gmem0_WREADY` |
| `WDATA` | `m_axi_gmem0_WDATA` |
| `WSTRB` | `m_axi_gmem0_WSTRB` |
| `WLAST` | `m_axi_gmem0_WLAST` |
| `WID` | `m_axi_gmem0_WID` |
| `WUSER` | `m_axi_gmem0_WUSER` |
| `ARVALID` | `m_axi_gmem0_ARVALID` |
| `ARREADY` | `m_axi_gmem0_ARREADY` |
| `ARADDR` | `m_axi_gmem0_ARADDR` |
| `ARID` | `m_axi_gmem0_ARID` |
| `ARLEN` | `m_axi_gmem0_ARLEN` |
| `ARSIZE` | `m_axi_gmem0_ARSIZE` |
| `ARBURST` | `m_axi_gmem0_ARBURST` |
| `ARLOCK` | `m_axi_gmem0_ARLOCK` |
| `ARCACHE` | `m_axi_gmem0_ARCACHE` |
| `ARPROT` | `m_axi_gmem0_ARPROT` |
| `ARQOS` | `m_axi_gmem0_ARQOS` |
| `ARREGION` | `m_axi_gmem0_ARREGION` |
| `ARUSER` | `m_axi_gmem0_ARUSER` |
| `RVALID` | `m_axi_gmem0_RVALID` |
| `RREADY` | `m_axi_gmem0_RREADY` |
| `RDATA` | `m_axi_gmem0_RDATA` |
| `RLAST` | `m_axi_gmem0_RLAST` |
| `RID` | `m_axi_gmem0_RID` |
| `RUSER` | `m_axi_gmem0_RUSER` |
| `RRESP` | `m_axi_gmem0_RRESP` |
| `BVALID` | `m_axi_gmem0_BVALID` |
| `BREADY` | `m_axi_gmem0_BREADY` |
| `BRESP` | `m_axi_gmem0_BRESP` |
| `BID` | `m_axi_gmem0_BID` |
| `BUSER` | `m_axi_gmem0_BUSER` |
| `ACLK` | `ap_clk` |
| `ARESET` | `ap_rst_n_inv` |
| `ACLK_EN` | `1'b1` *(tied-off)* |
| `I_CH0_ARVALID` | `gmem0_ARVALID` |
| `I_CH0_ARREADY` | `gmem0_ARREADY` |
| `I_CH0_ARADDR` | `gmem0_ARADDR` |
| `I_CH0_ARLEN` | `gmem0_ARLEN` |
| `I_CH0_RVALID` | `gmem0_RVALID` |
| `I_CH0_RREADY` | `gmem0_RREADY` |
| `I_CH0_RDATA` | `gmem0_RDATA` |
| `I_CH0_RFIFONUM` | `gmem0_RFIFONUM` |
| `I_CH0_AWVALID` | `1'b0` *(tied-off)* |
| `I_CH0_AWREADY` | `gmem0_AWREADY` |
| `I_CH0_AWADDR` | `64'd0` *(tied-off)* |
| `I_CH0_AWLEN` | `32'd0` *(tied-off)* |
| `I_CH0_WVALID` | `1'b0` *(tied-off)* |
| `I_CH0_WREADY` | `gmem0_WREADY` |
| `I_CH0_WDATA` | `16'd0` *(tied-off)* |
| `I_CH0_WSTRB` | `2'd0` *(tied-off)* |
| `I_CH0_BVALID` | `gmem0_BVALID` |
| `I_CH0_BREADY` | `1'b0` *(tied-off)* |

## gmem1_m_axi (66 ports)

| port | connected wire/const |
|---|---|
| `AWVALID` | `m_axi_gmem1_AWVALID` |
| `AWREADY` | `m_axi_gmem1_AWREADY` |
| `AWADDR` | `m_axi_gmem1_AWADDR` |
| `AWID` | `m_axi_gmem1_AWID` |
| `AWLEN` | `m_axi_gmem1_AWLEN` |
| `AWSIZE` | `m_axi_gmem1_AWSIZE` |
| `AWBURST` | `m_axi_gmem1_AWBURST` |
| `AWLOCK` | `m_axi_gmem1_AWLOCK` |
| `AWCACHE` | `m_axi_gmem1_AWCACHE` |
| `AWPROT` | `m_axi_gmem1_AWPROT` |
| `AWQOS` | `m_axi_gmem1_AWQOS` |
| `AWREGION` | `m_axi_gmem1_AWREGION` |
| `AWUSER` | `m_axi_gmem1_AWUSER` |
| `WVALID` | `m_axi_gmem1_WVALID` |
| `WREADY` | `m_axi_gmem1_WREADY` |
| `WDATA` | `m_axi_gmem1_WDATA` |
| `WSTRB` | `m_axi_gmem1_WSTRB` |
| `WLAST` | `m_axi_gmem1_WLAST` |
| `WID` | `m_axi_gmem1_WID` |
| `WUSER` | `m_axi_gmem1_WUSER` |
| `ARVALID` | `m_axi_gmem1_ARVALID` |
| `ARREADY` | `m_axi_gmem1_ARREADY` |
| `ARADDR` | `m_axi_gmem1_ARADDR` |
| `ARID` | `m_axi_gmem1_ARID` |
| `ARLEN` | `m_axi_gmem1_ARLEN` |
| `ARSIZE` | `m_axi_gmem1_ARSIZE` |
| `ARBURST` | `m_axi_gmem1_ARBURST` |
| `ARLOCK` | `m_axi_gmem1_ARLOCK` |
| `ARCACHE` | `m_axi_gmem1_ARCACHE` |
| `ARPROT` | `m_axi_gmem1_ARPROT` |
| `ARQOS` | `m_axi_gmem1_ARQOS` |
| `ARREGION` | `m_axi_gmem1_ARREGION` |
| `ARUSER` | `m_axi_gmem1_ARUSER` |
| `RVALID` | `m_axi_gmem1_RVALID` |
| `RREADY` | `m_axi_gmem1_RREADY` |
| `RDATA` | `m_axi_gmem1_RDATA` |
| `RLAST` | `m_axi_gmem1_RLAST` |
| `RID` | `m_axi_gmem1_RID` |
| `RUSER` | `m_axi_gmem1_RUSER` |
| `RRESP` | `m_axi_gmem1_RRESP` |
| `BVALID` | `m_axi_gmem1_BVALID` |
| `BREADY` | `m_axi_gmem1_BREADY` |
| `BRESP` | `m_axi_gmem1_BRESP` |
| `BID` | `m_axi_gmem1_BID` |
| `BUSER` | `m_axi_gmem1_BUSER` |
| `ACLK` | `ap_clk` |
| `ARESET` | `ap_rst_n_inv` |
| `ACLK_EN` | `1'b1` *(tied-off)* |
| `I_CH0_ARVALID` | `1'b0` *(tied-off)* |
| `I_CH0_ARREADY` | `gmem1_ARREADY` |
| `I_CH0_ARADDR` | `64'd0` *(tied-off)* |
| `I_CH0_ARLEN` | `32'd0` *(tied-off)* |
| `I_CH0_RVALID` | `gmem1_RVALID` |
| `I_CH0_RREADY` | `1'b0` *(tied-off)* |
| `I_CH0_RDATA` | `gmem1_RDATA` |
| `I_CH0_RFIFONUM` | `gmem1_RFIFONUM` |
| `I_CH0_AWVALID` | `gmem1_AWVALID` |
| `I_CH0_AWREADY` | `gmem1_AWREADY` |
| `I_CH0_AWADDR` | `gmem1_AWADDR` |
| `I_CH0_AWLEN` | `gmem1_AWLEN` |
| `I_CH0_WVALID` | `gmem1_WVALID` |
| `I_CH0_WREADY` | `gmem1_WREADY` |
| `I_CH0_WDATA` | `gmem1_WDATA` |
| `I_CH0_WSTRB` | `gmem1_WSTRB` |
| `I_CH0_BVALID` | `gmem1_BVALID` |
| `I_CH0_BREADY` | `gmem1_BREADY` |

## 재현

```bash
cd deliverables/verilog/dfxisp_accel
python3 - <<'PYEOF'
import re
with open('dfxisp_accel.v') as f:
    text = f.read()
instances = [
    ("dfxisp_accel_dfxisp_accel_Pipeline_VITIS_LOOP_151_1", "grp_dfxisp_accel_Pipeline_VITIS_LOOP_151_1_fu_245"),
    ("dfxisp_accel_p_anonymous_namespace_run_normal", "grp_p_anonymous_namespace_run_normal_fu_255"),
    ("dfxisp_accel_p_anonymous_namespace_run_low_light", "grp_p_anonymous_namespace_run_low_light_fu_269"),
    ("dfxisp_accel_control_s_axi", "control_s_axi_U"),
    ("dfxisp_accel_gmem0_m_axi", "gmem0_m_axi_U"),
    ("dfxisp_accel_gmem1_m_axi", "gmem1_m_axi_U"),
]
for typ, inst in instances:
    idx = text.find(inst + "(")
    end = text.find(");", idx)
    block = text[idx:end]
    for p, w in re.findall(r'\.(\w+)\(([^()]*(?:\([^()]*\)[^()]*)*)\)\s*,?', block, re.MULTILINE):
        print(inst, p, w.strip())
PYEOF
```

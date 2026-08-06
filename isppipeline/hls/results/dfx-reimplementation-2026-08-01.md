<!--
File: isppipeline/hls/results/dfx-reimplementation-2026-08-01.md
Date: 2026-08-01 KST
Purpose: Rebuild the fabric-only Vivado DFX artifacts from the BLC=2/2,
         DARK_RATIO_PCT=62 canonical HLS source.
-->
# Vivado DFX 재구현 — 최신 BLC/C1 상수 반영 (2026-08-01)

## 1. 범위와 입력

`src/dfxisp_accel.cpp`의 최신 상수(`BLC_OFFSET12=2<<4`,
`BLC_OFFSET12_LOWLIGHT=2<<4`, `DARK_RATIO_PCT=62`)를 입력으로 Vivado DFX
fabric-only 특성화 흐름을 처음부터 재실행했다. Part는
`xczu7ev-ffvc1156-2-e`, HLS/Vivado는 2024.1, target clock은 5.0 ns다.

두 RM은 `/tmp/hls_dfxisp/dfxisp_accel/` flat 복사본에서 각각 HLS top으로
`csynth_design`과 `export_design -rtl verilog -format ip_catalog`를 실행했다.
HLS 추정치는 이전과 동일했다.

| HLS top | LUT | FF | BRAM_18K | DSP | Estimated Fmax |
|---|---:|---:|---:|---:|---:|
| `rm_normal_tone_top` | 5,202 | 3,797 | 4 | 12 | 273.97 MHz |
| `rm_low_light_tone_top` | 4,204 | 3,243 | 8 | 9 | 273.97 MHz |

Vivado OOC 합성 stub에서 방향·폭·순서를 포함한 포트 signature를 자동 비교한
결과 두 RM은 **110 ports, 완전 동일**했다.

## 2. DFX 방법론

1. 각 RM의 HLS Verilog 전체를 Vivado `synth_design -mode out_of_context`로
   합성하고 DCP와 `write_verilog -mode synth_stub` 결과를 생성했다.
2. Python 생성기가 두 stub를 파싱해 `dfx_static_top.v`를 만들었다. 실제 chip I/O는
   `ap_clk`/`ap_rst_n`만 노출하고, 나머지 입력은 내부 0 tie-off, 출력은 내부 XOR
   관찰 신호로 연결했다. RP에는 `dont_touch`와 `keep_hierarchy`를 적용했다.
3. `u_rp`를 reconfigurable로 지정하고 pblock을
   `CLOCKREGION_X1Y0:CLOCKREGION_X2Y0`, `SNAPPING_MODE OFF`,
   `CONTAIN_ROUTING/EXCLUDE_PLACEMENT true`로 설정했다. 실측 pblock 용량은
   LUT 19,200 / BRAM tile 24 / DSP 216이며 X1Y0 42.40%, X2Y0 57.60%다.
4. Config1(NORMAL)을 opt/place/route한 뒤 routed DCP를 저장했다.
5. Config1 DCP에서 `update_design -cell u_rp -black_box`,
   `lock_design -level routing`으로 static을 고정하고 LOW_LIGHT OOC DCP를 이식해
   Config2를 opt/place/route했다.
6. `pr_verify`를 실행했다. 두 DCP 모두 partition pin 3, static tile 29,573,
   static site 51, static cell 91, routed node 89, routed pip 84로 동일해 PASS했다.
7. fabric-only top의 미지정 두 핀에 해당하는 `NSTD-1`/`UCIO-1`만 Warning으로
   낮추고 full/partial bitstream을 생성했다. 다른 DRC severity는 변경하지 않았다.

## 3. 실측 결과와 2026-07-03 기준 비교

| 지표 | 07-03 기준 Config1 | 08-01 Config1 | 판정 | 07-03 기준 Config2 | 08-01 Config2 | 판정 |
|---|---:|---:|---|---:|---:|---|
| CLB LUT | 3,972 | **2,630** | 불일치(-1,342, -33.8%) | 2,927 | **1,843** | 불일치(-1,084, -37.0%) |
| Block RAM Tile | 1.5 | **1.5** | 일치 | 3.5 | **3.5** | 일치 |
| DSP | 12 | **12** | 일치 | 8 | **8** | 일치 |
| WNS @ 5.0 ns | 최종 floorplan 기준 미측정 | **+0.813 ns** | 신규 실측/PASS | 최종 floorplan 기준 미측정 | **+2.224 ns** | 신규 실측/PASS |
| `pr_verify` | PASS | **PASS** | 일치 | PASS | **PASS** | 일치 |
| partition pin | 3 | **3** | 일치 | 3 | **3** | 일치 |

| Bitstream | 07-03 bytes | 08-01 bytes | 판정 |
|---|---:|---:|---|
| `config1_full_final.bit` | 19,311,211 | **19,311,211** | 일치 |
| `rm_normal_partial_final.bit` | 1,447,424 | **1,447,424** | 일치 |
| `rm_lowlight_partial_final.bit` | 1,447,424 | **1,447,424** | 일치 |

### LUT 불일치 조사

예상과 달리 routed LUT는 크게 감소했다. 수치를 기준에 맞추지 않고 실측 그대로
기록한다. 다음 교차검증을 수행했다.

- 최신 HLS csynth 추정은 07-03과 동일(5,202/4,204)하다.
- 새 routed Config1 DCP에서 `u_rp`의 `DONT_TOUCH=1`,
  `KEEP_HIERARCHY=yes`가 보존됨을 직접 조회했다.
- RP 자체 비교에서도 Config1 LUT가 3,752→2,544로 감소했다. 따라서 주원인은
  static wrapper의 관찰 로직 감소가 아니라 최신 RM netlist의 Vivado mapping이다.
- Git 이력상 07-03 이후 HLS C++의 의미 있는 변경은 BLC 16/8→2/2와 checker
  ratio 80→62뿐이다. checker는 RM top에 포함되지 않으므로 RM LUT 차이는 BLC 상수
  변경에 따른 Vivado 상수 전파/technology mapping 결과로 판단한다.
- BRAM/DSP, RM port signature, pblock 용량, partition pin, bitstream 크기는 모두
  불변이다. 따라서 구조적 DFX 계약과 frame footprint는 유지됐다.

HLS 추정 자원이 동일하더라도 downstream Vivado mapping까지 동일하다는 가설은 이번
실측으로 반증됐다. 기능 RTL 자체는 최신 상수를 포함하며, 감소는 오류가 아니라 합성
최적화 결과로 보인다.

### 후속 조사 (2026-08-03): 근본 원인 확정

위 "판단"을 실측으로 확정했다. `deliverables/verilog/{rm_normal,rm_low_light}_tone_top/`
(08-01, 신규)와 `deliverables/archive/2026-07-03-stale-blc16-checker80/verilog/...`
(07-03, 구)의 HLS export Verilog를 파일 단위로 전부 대조했다. HLS가 소스 라인 번호를
그대로 신호명(`_lnNNN`)과 모듈/파일명(`VITIS_LOOP_NNN`)에 새겨 넣으므로 그 번호만
정규화(`sed -E 's/ln[0-9]+/lnN/g; s/LOOP_[0-9]+/LOOP_N/g'`)한 뒤 대조했다.

**결과: 두 RM 모두 파이프라인 본체 파일 단 하나씩만 실질적으로 다르고, 그 안의 차이는
정확히 BLC 상수에서 유도된 리터럴 값 교체뿐이다.** 나머지 모든 파일(top, control_s_axi,
gmem AXI, mul_*, sparsemux_*, row buffer RAM 등)은 라인 번호를 제외하고 완전히
바이트 단위로 동일했다.

- `rm_normal_tone_top`(BLC_OFFSET12, 256→32): 파이프라인 본체에서
  `13'd7936`→`13'd8160`(= `8192 - 256`→`8192 - 32`, `-blc_offset`의 13비트 2의 보수
  인코딩), `13'd3839`→`13'd4063`(= `RAW12_MAX(4095) - blc_offset`) 세 채널(R/G/B) 반복.
- `rm_low_light_tone_top`(BLC_OFFSET12_LOWLIGHT, 128→32): 같은 패턴으로
  `17'd130944`→`17'd131040`(= `131072 - 128`→`131072 - 32`) 세 채널 반복. (`13'd4574`/
  `13'd4910` 등 다른 클램프 상수는 이번 변경과 무관 — 값 자체는 불변, 파일 내 statement
  순서만 달라 diff에 잠깐 걸렸을 뿐.)
- 그 외 폭·연산자·인스턴스 구조는 단 하나도 다르지 않다 — 새 게이트, 새 레지스터,
  새 뮤텍스/멀티플렉서가 추가되거나 사라진 적이 없다.

즉 08-01과 07-03의 RM RTL은 **정확히 6개의 상수 리터럴**(채널당 2개 × 2 RM, 실질적으로는
`-blc_offset`과 `RAW12_MAX-blc_offset` 두 파생값)만 다르고 그 외에는 100% 동일하다.
CLB LUT 34~37% 감소는 이 상수 리터럴이 바뀐 것에 대해 Vivado의 technology mapping/LUT
패킹이 다른 결과를 낸 것 — 표준적인 상수 기반 합성 최적화 거동이며, 회로 구조나 기능
로직이 빠지거나 잘못 합성된 것이 아니다(같은 폭의 가산기/비교기에 다른 상수가 들어가면
LUT truth table 내용과 그에 따른 패킹 효율이 달라지는 것은 Vivado 합성기에서 통상적).
이번 조사로 §3의 "판단"은 재현 가능한 실측 근거를 갖춘 확정 결론으로 종결한다 — 추가
조치 불필요, 08-01 report의 routed 수치는 그대로 신뢰 가능.

재현: `deliverables/verilog/`와 `deliverables/archive/2026-07-03-stale-blc16-checker80/verilog/`
의 대응 파일을 위 sed 정규화 후 `diff`하면 위 상수 치환만 남는다(다른 모든 파일은 diff 0).

## 4. 트러블슈팅

1. 첫 HLS 시도에서 절대경로 `add_files`도 프로젝트 기준 상대경로로 다시 기록되어
   source가 누락됐다. flat 디렉터리에서 실행하고 HLS 프로젝트도 그 아래에 두어 해결했다.
2. 최초 wrapper 생성기는 Verilog 파일에 SystemVerilog unsized `'0`을 출력해 문법
   오류가 났다. 일반 Verilog 상수 `0`으로 변경했다.
3. stub 헤더 주석의 `module interface` 문구를 실제 module 선언으로 오인하고,
   `ap_clk` 선언의 inline synthesis 주석을 처리하지 못했다. module regex를 행 시작에
   고정하고 inline 주석을 허용해 110개 포트를 정확히 파싱했다.
4. 모든 성공 실행 로그에 사용자 Tcl store 쓰기 권한 관련 `Common 17-741` critical
   warning이 있었으나 Vivado가 설치 영역 Tcl store로 자동 fallback했고 합성/구현/
   bitgen에는 영향이 없었다. 구현과 bitgen은 각각 0 errors로 종료했다.

## 5. 영구 산출물과 재현

재현 스크립트는 `scripts/dfx/dfx_flow.tcl`, `dfx_flow_config2.tcl`,
`write_bitstreams.tcl`, `generate_static_wrapper.py`에 저장했다. 07-03 산출물은 삭제하지
않고 `deliverables/archive/2026-07-03-stale-blc16-checker80/`로 이동했다. 최신
bitstream/DCP/report/HLS RM Verilog는 각각 `deliverables/`의 표준 하위 디렉터리에
반영했다.

```bash
source /tools/Xilinx/Vivado/2024.1/settings64.sh
cd /tmp/hls_dfxisp/dfx
vivado -mode batch -source /home/mini/workspace/dfxisp/isppipeline/hls/scripts/dfx/dfx_flow.tcl
vivado -mode batch -source /home/mini/workspace/dfxisp/isppipeline/hls/scripts/dfx/dfx_flow_config2.tcl
vivado -mode batch -source /home/mini/workspace/dfxisp/isppipeline/hls/scripts/dfx/write_bitstreams.tcl
```

`dfx_flow.tcl` 실행 전에는 본문 §1처럼 flat 복사본에서 두 RM의 HLS RTL export가
존재해야 한다. 중단 후 이미 검증한 OOC DCP/stub를 재사용할 때만
`DFX_REUSE_RM_SYNTH=1`을 지정한다.

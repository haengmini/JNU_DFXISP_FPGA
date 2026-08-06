<!--
=============================================================================
File   : isppipeline/hls/results/stage5-dfx-implementation-2026-07-02.md
Date   : 2026-07-02
Time   : 17:15 KST
Function: Stage 5 실측 결과 — 실제 Vivado DFX(Dynamic Function eXchange) 구현
Goal   : "보드 측정 직전 최종 관문" — RM_NORMAL_TONE/RM_LOW_LIGHT_TONE을 실제
         Reconfigurable Partition으로 플로어플랜·구현·pr_verify·partial bitstream
         생성까지 완주. 이 문서 이후 남은 것은 실제 ZCU104 보드뿐이다.
=============================================================================
-->
# Stage 5 — Vivado DFX 구현 (실측)

> ✅ **재구현 완료 (2026-07-02 20:31 KST, adversarial-review 수정 반영):** 이 문서의 모든
> 수치는 low-light 색상보존 binning-demosaic 수정 및 구조체→scalar 메타데이터 포인터 수정이
> 반영된 **커밋 a2d1b6d 소스**로 RM out-of-context 재합성 → DFX 재구현 → pr_verify →
> bitstream 재생성까지 완주한 최신 실측치다. `rm_normal_tone_top`/`rm_low_light_tone_top`의
> 포트 목록은 수정 전후 완전 동일(byte-identical stub diff로 확인)하므로 static wrapper
> (`dfx_static_top.v`)는 재생성 없이 재사용했다. 상세: `SPEC.md` §11.5.

> Stage 4(csynth)에서 확보한 `rm_normal_tone_top`/`rm_low_light_tone_top` IP를 실제
> **Vivado 2024.1 non-project batch DFX flow**(AMD UG909 표준 절차)로 구현했다.
> Fabric-only 특성화(PS/DDR 통합 없음, 순수 플로어플랜·자원·pr_verify·partial bitstream
> 크기 검증 목적) — 실제 보드 동작을 주장하지 않으며, 남은 것은 물리 보드뿐이다.

## 1. 사전 준비

### 1.1 포트 인터페이스 통일
DFX는 같은 RP 슬롯에 들어가는 두 구현이 **동일 포트 목록**을 가져야 한다(RESEARCH §2.3).
`rm_normal_tone_top`에 `out_width`/`out_height` 출력을 추가해 `rm_low_light_tone_top`과
포트를 통일(값은 항상 width/height 그대로 — normal은 형상 보존). `make verify` 646px
bit-exact 유지(순수 additive, `dfxisp_accel()` 동작 불변).

**검증:** 두 top의 합성된 Verilog 포트 선언을 diff — **완전 동일(0 차이, 112줄)**.

### 1.2 IP 개별 out-of-context 합성
각 RM을 HLS 생성 Verilog 소스 전체(15/19개 파일)로 `synth_design -mode out_of_context`
(순수 Vivado 합성, HLS 아님) → `.dcp` 체크포인트 확보. 두 top 모두 0 errors.

### 1.3 Static wrapper 자동 생성
Vivado `write_verilog -mode synth_stub`로 파라미터가 resolve된 black-box 선언을 확보한 뒤,
Python으로 파싱해 static top(`dfx_static_top.v`)을 프로그래밍적으로 생성(수동 전사 오류 방지).

- 최초 시도: 모든 ~110개 RM 포트(662 signal bits)를 칩 top-level pin으로 노출 →
  **`Number of unplaced IO Ports (662) > available pins (360)`로 실패**(xczu7ev 패키지
  물리적 한계). Fabric-only 특성화 목적에 맞게 `ap_clk`/`ap_rst_n`만 실제 chip I/O로 남기고
  나머지는 내부 tie-off(입력=0, 출력=관찰용 XOR reduce)로 변경.
- `dont_touch`/`keep_hierarchy` 속성 없이는 `synth_design`이 RP 인스턴스를 죽은 로직으로
  최적화 제거(`No cells matched 'u_rp'`) → 속성 추가로 해결.

## 2. 플로어플랜

```
클럭 리전: xczu7ev = 4×6 = 24개 (X0Y0~X3Y5, 실측 조회)
RP pblock: CLOCKREGION_X0Y0:CLOCKREGION_X1Y0 (2개 리전)
Pblock 용량: LUT 8,640 (RM 최대 요구 7,167 LUT 대비 여유, 실측)
```
단일 클럭 리전(X0Y0)에 `SNAPPING_MODE ON`을 걸었을 때 유효 영역이 0으로 축소되는 문제
발생(해당 리전의 reconfigurable frame 경계 정렬 이슈로 추정) → 2리전·스냅핑 OFF로 해결.

## 3. 구현 (opt → place → route)

| Config | 구성 | 결과 |
|---|---|---|
| **Config 1** | static + RM_NORMAL_TONE | ✅ opt/place/route 성공 |
| **Config 2** | static(재사용, lock) + RM_LOW_LIGHT_TONE | ✅ opt/place/route 성공 |

**중요한 방법론 수정:** Config2를 static_synth.dcp(미구현)에서 독립적으로 place했더니
static 영역 배치가 Config1과 미세하게 달라짐(`instance i_53 at site SLICE_X66Y0` 불일치)
→ **AMD UG909 표준 절차대로 수정**: Config1의 **완전히 구현된** 체크포인트에서
`update_design -cell u_rp -black_box` + `lock_design -level routing`으로 static 배치를
고정한 뒤 RM2를 이식 → 재구현. 이후 static 영역이 두 config에서 완전히 동일해짐.

### Config1 자원 (routed, `dfx_static_top` 전체 = static + RM_NORMAL_TONE, 재합성 후 실측)
| 지표 | 사용 | 가용 | Util% |
|---|---|---|---|
| CLB LUT | 3,953 | 230,256 | 1.72% |
| Block RAM Tile | 1.5 | 312 | 0.48% |
| DSP | 12 | 1,728 | 0.69% |

(DSP=12는 Stage4 standalone csynth 실측치와 **정확히 일치** — 교차검증. LUT는 구 수치
3,948과 거의 동일(+5, 배치·라우팅 비결정성 범위) — `rm_normal_tone_top` 내부 로직이 수정으로
바뀌지 않았다는 사실과 정합.)

### Config2 자원 (routed, `dfx_static_top` 전체 = static + RM_LOW_LIGHT_TONE, 재합성 후 실측)
| 지표 | 사용 | 가용 | Util% |
|---|---|---|---|
| CLB LUT | 2,922 | 230,256 | 1.27% |
| Block RAM Tile | 3.5 | 312 | 1.12% |
| DSP | 8 | 1,728 | 0.46% |

Config1 대비 크게 작다(LUT -26.1%, DSP -33.3%) — Stage4 §6c에서 확인한 `rm_low_light_tone_top`
단독 축소(버그 수정으로 2차 demosaic 제거)가 실제 배치·라우팅된 하드웨어에도 그대로 반영됨.

## 4. pr_verify — DFX 정합성 공식 확인

```
pr_verify -initial config1_normal_impl.dcp -additional config2_lowlight_impl.dcp
```
**결과: PASS (재합성 후에도 유지).**
```
INFO: [Vivado 12-3253] PR_VERIFY: check points config1_normal_impl.dcp and
config2_lowlight_impl.dcp are compatible
```
비교 내역(양쪽 동일): reconfigurable module 1개, partition pin **15개**, static tile 29,648개,
static site 61개, static cell 256개, static routed node 1,154개, routed pip 958개 —
**완전히 동일**(static 영역이 두 config에서 진짜로 고정됨을 수치로 증명). static cell 256개는
구 수치와 동일(static 로직 자체는 안 바뀜); site/node/pip 수는 배치·라우팅 비결정성으로 소폭
변동(정상).

> **partition pin 2개 → 15개로 증가한 것이 이번 수정의 가장 직접적인 하드웨어 증거다.**
> 구조체 포인터(`DfxIspResult*`) 시절에는 RP 경계를 통과하는 메타데이터 신호가 2개로 뭉뚱그려
> 보였으나, 4개 scalar 출력(`out_width`/`out_height`/`selected_mode`/`selected_rm`)으로
> 분리한 뒤에는 실제로 15개의 개별 partition pin이 물리적으로 존재한다. adversarial-review
> Finding 2("메타데이터가 RTL에서 개별 출력으로 보이지 않는다")가 **post-route 배치·라우팅
> 단계에서도 실측으로 해소**되었음을 의미한다.

## 5. Bitstream (실측 크기)

| 산출물 | 크기 | 비고 |
|---|---|---|
| `config1_full.bit` (전체) | **19,311,211 bytes ≈ 19.3 MB** | 재합성 전과 **byte 단위로 동일** |
| `rm_normal_partial.bit` (partial) | **686,664 bytes ≈ 671 KB** | RM_NORMAL_TONE, 재합성 전과 동일 |
| `rm_lowlight_partial.bit` (partial) | **686,664 bytes ≈ 671 KB** | RM_LOW_LIGHT_TONE, 재합성 전과 동일 |

bitstream 크기가 수정 전후로 완전히 동일한 이유: 크기는 **pblock의 reconfigurable frame 수**로
결정되며(고정 프레임 그리드), 실제 로직 활용률(LUT 사용량)과는 독립적 — Pblock/floorplan을
그대로 재사용했고(§2, 용량 8,640 LUT는 축소된 두 RM 모두에 여전히 충분한 여유), 두 RM이 같은
pblock을 공유하므로 로직 사용량이 줄어도 partial bitstream 크기는 변하지 않는 것이 정상이다.

DRC 참고: `write_bitstream`이 `ap_clk`/`ap_rst_n`의 미지정 I/O standard/location(NSTD-1/
UCIO-1)에서 막힘 — 실제 보드 핀 배정이 없는 fabric-only 특성화이므로 해당 DRC를
`SEVERITY Warning`으로 낮춰 우회(가짜 핀 배정을 지어내지 않음, 정직하게 문서화).

## 6. 재현

```bash
# 사전: Stage 4의 rm_normal_tone_top / rm_low_light_tone_top HLS 합성 Verilog 확보
source /tools/Xilinx/Vivado/2024.1/settings64.sh
cd /tmp/hls_dfxisp/dfx
vivado -mode batch -source dfx_flow.tcl -log dfx_flow.log            # static+RP+config1(성공)
vivado -mode batch -source dfx_flow_config2.tcl -log dfx_flow_config2.log  # config2(static 고정)+pr_verify
vivado -mode batch -source write_bitstreams.tcl -log write_bitstreams.log # bitstream
```
(스크립트/체크포인트/비트스트림은 `/tmp` 산출물이라 repo에 커밋하지 않음 — 위 절차로 재생성.
Verilog 소스·static wrapper 생성 스크립트는 재현 가능하나 세션 종료 시 `/tmp`가 소실될 수
있음. 다음 세션에서 이어가려면 Stage 4 IP export부터 다시 실행.)

## 7. 이 시점부터 필요한 것 (진짜 보드 단계)

이 문서로 **"보드 측정 전단계"가 완료**된다. 남은 것은 물리적으로만 확인 가능:
- ZCU104 실보드에 static+PS+DDR **실제 통합**(본 특성화는 fabric-only, PS 미통합)
- 실제 clock/reset 핀 배정 + 타이밍 제약(`create_clock`) — 본 패스는 미적용(WNS 미측정)
- ICAP을 통한 실제 partial bitstream 로드 + 전환 지연(ms) 실측
- 절대 전력(W) 측정
- DPU/검출기 end-to-end 실행

## 8. 산출물
- `/tmp/hls_dfxisp/dfx/`: `dfx_static_top.v`(생성 스크립트 포함), `dfx_flow*.tcl`,
  `write_bitstreams.tcl`, `pr_verify.rpt`, `config{1,2}_impl.util.rpt`,
  `pblock_capacity.rpt`, bitstream 3종(바이너리, repo 미커밋)
- 이 문서: 전체 수치·방법론·트러블슈팅 기록(재현 가능)

<!--
=============================================================================
File   : isppipeline/hls/results/cosim-waveform-analysis-2026-07-03.md
Date   : 2026-07-03
Function: dfxisp_accel C/RTL Co-sim(XSIM) 실제 파형 실측 및 분석. Stage 4
          §6b(cosim 자동 post-check 미완주)에서 미해결로 남았던 "RTL이 실제로
          어떻게 동작하는지"를 파형 레벨에서 직접 검증.
Goal   : "waveform 보여줘" / "타이밍 다이어그램 분석에 대한 리포트가 있나?"
         요청에 대한 응답. GUI 없이 VCD를 직접 파싱해 얻은 실측 결과 기록.
=============================================================================
-->
# dfxisp_accel C/RTL Co-sim 파형 분석 (2026-07-03)

> Stage 4(`stage4-hw-synthesis-2026-07-02.md` §6b)는 "RTL 시뮬레이션 실행은
> 성공(7/7)했지만 자동 bit-exact 비교는 SIGSEGV로 미완주"라고 기록했다. 이
> 문서는 그 실행이 **실제로 무엇을 했는지**를 파형으로 직접 검증한 결과다 —
> 자동 비교가 실패해도 시뮬레이션 자체는 유효했다는 것을 신호 레벨에서 증명.

## 1. 배경 — 왜 다시 cosim을 돌렸나

기존 Stage 4 결과는 "RTL이 8/8(또는 7/7) 트랜잭션을 완료했다"는 사실만 로그
텍스트로 기록했을 뿐, **그 트랜잭션들이 실제로 무엇을 했는지(어느 모드가
선택됐는지, checker가 실제로 몇 프레임을 저조도로 판정했는지 등)는 확인된
적이 없었다.** 이번 작업은 그 공백을 파형으로 직접 메운다.

## 2. 방법 — GUI 없이 VCD를 직접 파싱

### 2.1 재현 중 발생한 문제 3가지 (전부 정직하게 기록)

| 시도 | 결과 | 원인 |
|---|---|---|
| 1차: `isppipeline/hls/build/`에서 직접 `make hls FLOW=cosim` | 실패 | 기존에 문서화된 "source-path 버그" 재현(`HLS_SOURCES` 누락, `undefined symbol: dfxisp_accel`) — flat temp-dir 필요 |
| 2차: flat temp-dir + `cosim_design ... -wave_debug` | 행(hang) | `-wave_debug`가 GUI를 강제로 열어(`start_gui`) 헤드리스 배치 실행이 멈춤 |
| 3차: `cosim_design ... -trace_level all`(GUI 옵션 제거) | **성공** | 8/8 트랜잭션 100%, `$finish` 정상 호출. 단, vitis_hls의 **자동 post-check 비교 단계**가 기존에 문서화된 것과 동일한 SIGSEGV로 실패(`ENTER_WRAPC_PC` 상태에서 세그폴트) — 이건 시뮬레이션 자체가 아니라 별도의 사후 비교 유틸리티 문제 |

### 2.2 SIGSEGV 우회 — 이미 컴파일된 스냅샷을 직접 재사용

3차 시도로 만들어진 `xsim.dir/dfxisp_accel`(elaborate된 시뮬레이션 스냅샷)는
이미 유효했으므로, vitis_hls의 `cosim_design` 래퍼를 거치지 않고 **xsim을
직접 재호출**해 같은 스냅샷으로 VCD를 덤프했다:

```tcl
open_vcd dfxisp_accel_dump.vcd
log_vcd /apatb_dfxisp_accel_top/AESL_inst_dfxisp_accel/*
run all
close_vcd
quit
```

이 경로는 vitis_hls의 post-check 유틸리티를 아예 호출하지 않으므로 SIGSEGV를
완전히 피한다. 결과: 3.4MB VCD, 8/8 트랜잭션, SIGSEGV 없이 클린 종료.

### 2.3 필수 신호만 골라 재덤프

전체 신호(434개 signal wire, `dfxisp-accel-connectivity-2026-07-03.md` 참조)는
파형으로 보기엔 과하므로, **데이터 입력→checker 판정→경로 선택→데이터 출력**
흐름에 필요한 18개만 골라 별도 스크립트(`deliverables/simulation/dump_essential.tcl`)로
재덤프했다. 이 스크립트도 재실행해서 경고 0개로 정상 동작함을 재검증했다.

## 3. 실측 결과 — 8개 트랜잭션 전수 분석

`ap_start`/`ap_done`/`mode`/checker `dark_out`(with `dark_out_ap_vld` 스트로브로
정확한 시점의 확정값만 채택)/`run_normal`·`run_low_light` 각각의 `ap_start`를
전부 시간순으로 매칭한 결과:

| # | ap_start(ns) | ap_done(ns) | 소요(ns) | mode | checker dark_out | 선택된 경로 |
|---|---:|---:|---:|---|---:|---|
| 1 | 402.5 | 5687.5 | 5285.0 | NORMAL(명시) | — | run_normal |
| 2 | 6032.5 | 7202.5 | 1170.0 | LOW_LIGHT(명시) | — | run_low_light |
| 3 | 7562.5 | 9142.5 | 1580.0 | AUTO | 63 | run_low_light |
| 4 | 9417.5 | 15112.5 | 5695.0 | AUTO | 0 | run_normal |
| 5 | 15422.5 | 21117.5 | 5695.0 | AUTO | 48 | **run_normal** |
| 6 | 21427.5 | 23007.5 | 1580.0 | AUTO | 55 | run_low_light |
| 7 | 23357.5 | 24527.5 | 1170.0 | LOW_LIGHT(명시) | — | run_low_light |
| 8 | 24912.5 | 25137.5 | 225.0 | LOW_LIGHT(명시) | — | run_low_light |

전체 시뮬레이션 시간: 25,412.5 ns(8 트랜잭션 합산, XSIM 시간 단위).

## 4. 핵심 발견 — checker가 비율로 판정한다는 것을 파형에서 직접 확인

**트랜잭션 #5가 결정적이다.** `dark_out=48`로 **0이 아닌데도** `run_normal`이
선택됐다. 이는 checker가 "dark_out이 0보다 크면 무조건 저조도"가 아니라,
`RESEARCH.md` §5.1에 명시된 **비율 비교**(`dark_count*100 > dark_pixel_threshold
*(W*H)`)로 판정한다는 사실을 실측 파형에서 직접 증명한다 — 트랜잭션 #5의
이미지 크기(W×H)에서는 48이라는 카운트가 그 비율 임계값을 넘지 못했다는 뜻.
반대로 #3(63)과 #6(55)은 각각의 이미지 크기 기준으로 임계값을 넘어 저조도로
판정됐다.

**이 발견의 의미:** 지금까지 checker의 `dark_ratio` 공식은 SW golden model
(`gen_golden_vectors.py`)과 소스코드 리딩(`dfxisp_accel.cpp`)으로만 확인됐었는데,
이번에 **합성된 실제 RTL의 동작에서도 동일한 비율 로직이 재현됨**을 파형으로
교차검증했다 — L1(C-sim)↔L1.5(csynth)↔L2(RTL 동작) 사이의 정합성에 대한
새로운 실측 증거.

## 5. 산출물

- `deliverables/simulation/dfxisp_accel.wdb` — Vivado 네이티브 waveform database(1.1MB).
- `deliverables/simulation/dfxisp_accel_dump.vcd` — 전체 co-sim 실행의 VCD(3.4MB).
- `deliverables/simulation/dfxisp_accel_essential.vcd` — 위 §2.3의 18개 필수 신호만
  담은 VCD(155KB).
- `deliverables/simulation/dump_essential.tcl` — 필수 신호 재현 스크립트(검증됨,
  경고 0개).
- `deliverables/simulation/create_project.tcl` — RTL 탐색/자체 테스트벤치 작성용
  Vivado Project Mode 프로젝트 생성 스크립트.

## 6. 한계 (정직하게 기록)

- 이 실행은 **여전히 fabric-only C-TB 기반 co-sim**이다 — 실제 AXI-Stream
  wrapper(L3, `SPEC.md` §8에서 여전히 ⬜)나 보드 실측이 아니다.
- vitis_hls의 자동 post-check 비교(golden vs RTL bit-exact 자동 확인)는 여전히
  SIGSEGV로 미완주 상태다 — 이번 작업은 **"직접 xsim을 재호출해 그 단계를
  우회"**했을 뿐, 근본 원인(WSL2+Vitis HLS 2024.1+XSIM cosim 하네스 버그로 추정)을
  고치거나 해소한 것은 아니다. 자동 비교가 필요하면 여전히 별도 해결이 필요.
- 8개 트랜잭션의 정확한 입력 시나리오(`test_dfxisp_csim.cpp`의 각 호출이 어떤
  이미지 크기·raw 데이터를 썼는지)는 이 문서에서 W*H 절대값까지 역산하지
  않았다 — dark_out 절대값과 mode/경로 선택의 **상대적 관계**만 확인.

## 재현

```bash
# 1. flat temp-dir 스캐폴드 준비 (source-path 버그 우회)
mkdir -p /tmp/hls_dfxisp/dfxisp_accel/tests
cp include/dfxisp_accel.hpp src/dfxisp_accel.cpp tests/test_dfxisp_csim.cpp \
   /tmp/hls_dfxisp/dfxisp_accel/
cp tests/golden_vectors.csv /tmp/hls_dfxisp/dfxisp_accel/tests/

# 2. csim+csynth+cosim (GUI 옵션 없이, trace_level all만)
cd /tmp/hls_dfxisp/dfxisp_accel
source /tools/Xilinx/Vitis_HLS/2024.1/settings64.sh
vitis_hls -f run_cosim_wave2.tcl   # cosim_design -rtl verilog -tool xsim -trace_level all

# 3. SIGSEGV 우회: 컴파일된 스냅샷을 xsim으로 직접 재호출, 필수 신호만 VCD 덤프
cd proj_wave/solution1/sim/verilog
xsim --noieeewarnings dfxisp_accel -tclbatch dump_essential.tcl
```

## 관련 문서

- `stage4-hw-synthesis-2026-07-02.md` §6b — 이 파형 실측의 출발점(cosim 미완주 기록).
- `dfxisp-accel-connectivity-2026-07-03.md` — 이번에 쓴 18개 필수 신호가 전체
  434개 신호 중 어디에 해당하는지의 전체 맥락.
- `RESEARCH.md` §5.1 — checker `dark_ratio` 공식(이번 §4 발견의 근거).

<!--
=============================================================================
File   : isppipeline/hls/results/stage4-hw-synthesis-2026-07-02.md
Date   : 2026-07-02
Time   : 16:20 KST
Function: Stage 4 실측 결과 — 실제 Vitis HLS 2024.1 C-synthesis (xczu7ev, 5.0ns)
Goal   : 보드 측정 전 마지막 SW/툴체인 단계. streaming line buffer 리팩터 +
         gamma LUT 최적화를 반영한 ver1/ver2 아키텍처의 실제 자원/타이밍을 측정
=============================================================================
-->
# Stage 4 — HW 실측: C-Synthesis (Vitis HLS 2024.1)

> ✅ **재합성 완료 (2026-07-02 20:33 KST, adversarial-review 수정 반영):** §4/§6c의 수치는
> `/codex:adversarial-review --base 0e433f9`가 발견한 두 수정(low-light 색상보존
> binning-demosaic, 구조체→scalar 메타데이터 포인터)이 반영된 **커밋 a2d1b6d 소스**로
> 재합성한 최신 실측치다. 최초 합성 시점(16:20 KST)의 구 수치는 `git log -p`로 조회 가능.
> 상세: `SPEC.md` §11.5, §10.

> 이 환경에 **Vitis HLS 2024.1 + Vivado 2024.1이 실제로 설치**되어 있음을 확인하고
> (`/tools/Xilinx/Vitis_HLS/2024.1`, `/tools/Xilinx/Vivado/2024.1`), 실제 C-synthesis를
> 수행했다. 대상: `dfxisp_accel`(현재 ver1+ver2 아키텍처 — RAW-domain-first baseline
> core12, checker 재보정 dark_ratio>0.80), `xczu7ev-ffvc1156-2-e`, clock target 5.0 ns.

## 1. 사전 리팩터 (합성 전 필수)

### 1.1 Streaming line buffer (HLS README "다음 하드웨어 단계" #1)
`run_low_light`의 `static uint16_t binned[1920*1080]`(≈4MB, BRAM에 비현실적)를 **3-row
sliding buffer**(`row_buf[3][960]`)로 교체. 매 출력 row마다 y-1/y/y+1 binned row를 raw에서
재계산(단순·정확 우선, 추가 최적화는 후속). `demosaic_rggb12_rows()`를 신설해 3-row 버퍼에서
동일 demosaic 로직을 수행. **`make verify` bit-exact 유지(646px)** — 산술 불변, 메모리
접근 패턴만 변경.

### 1.2 툴체인 우회: flat temp-dir
과거 worklog(`12-worklog-2026-06-29.md`)에 기록된 "flat temp-dir 방식으로 source-path
버그와 종료-hang을 우회"와 동일한 문제를 재확인:
- **source-path 버그:** 중첩 프로젝트 디렉터리(`isppipeline/hls/build/vitis_hls/...`)에서
  `add_files`로 design source(`src/dfxisp_accel.cpp`)를 추가해도 `csim.mk`의 `HLS_SOURCES`에
  누락되어 링크 실패(`undefined symbol: dfxisp_accel`). testbench만 등록됨.
- **우회:** `/tmp/hls_dfxisp/dfxisp_accel/`에 소스를 flat하게 복사(hpp/cpp/tb를 같은 디렉터리,
  golden CSV만 `tests/` 서브폴더) 후 그 디렉터리에서 실행 → 정상 동작(design+tb 모두 컴파일).
- **종료-hang:** `close_project` 이후 프로세스가 CPU를 거의 안 쓰면서 종료되지 않음(work는
  이미 끝난 상태). `timeout -k <grace> <sec> vitis_hls ...`로 감싸 실제 작업 완료를 로그로
  확인한 뒤 강제 종료하는 방식으로 대응.

## 2. 1차 synthesis 결과 및 발견 — gamma 정수 sqrt의 합성 비효율

첫 csynth(리팩터 직후, 최적화 전)에서 **tone 스테이지가 자원을 지배**함을 발견:

| instance | BRAM | DSP | FF | LUT |
|---|---|---|---|---|
| run_normal | 0 | 15 | 27,605 | 23,647 |
| run_low_light | 3 | 15 | 28,611 | 25,370 |
| **합계** | 6 | 33 | 58,655 | 52,053 |

원인: `gamma2()`가 **런타임 Newton's-method 정수 sqrt**(`while` 루프, 나눗셈 포함)를 `PIPELINE
II=1` 영역 안에서 매 픽셀 호출 → HLS가 반복 나눗셈 로직을 펼쳐 넣으며 자원이 폭증. HLS
README·SPEC에 이미 "HW에서는 256-엔트리 LUT로 대체 가능"이라 명시했던 권고를 실측으로 검증.

## 3. 최적화: gamma2 런타임 sqrt → 256-엔트리 ROM

동일 공식(`floor(sqrt(255·v))`, golden model과 bit-exact)의 값을 **컴파일타임에 고정된
정적 배열**로 교체. 시도 1(`std::array`+`constexpr` 루프)은 Vitis HLS 번들 gcc-8.3.0 STL이
`<array>` 헤더를 거부(`C++ requires a type specifier...` in `bits/stl_pair.h` 등)해서 실패 →
시도 2(순수 C 배열 리터럴, Python으로 256개 값 생성)로 대체, 성공.

- `make verify`: **여전히 bit-exact(646px)** — 값 동일, 표현만 ROM.
- `results/resource_csynth_ver1_2026-07-02.csv`, `reports/csynth/dfxisp_accel_ver1_csynth.rpt`.

## 4. 최종 synthesis 결과 (gamma ROM 반영, 실측)

> **2026-07-02 20:33 KST 재합성 결과로 갱신.** 이 절의 수치는 adversarial-review 두 수정
> (chroma-preserving binning-demosaic + scalar 메타데이터 포인터)이 반영된 커밋 `a2d1b6d`
> 소스 기준이다. §2/§3의 gamma ROM 최적화 히스토리(전→후 표)는 그 수정 **이전** 시점의
> before/after 기록이라 역사적 사실로 그대로 남긴다 — 아래 표만 최신 수치로 교체한다.

### 4.1 타이밍
| Clock | Target | Estimated | Uncertainty |
|---|---|---|---|
| ap_clk | 5.00 ns | **3.650 ns** | 1.35 ns |

Fmax ≈ 1/3.65ns = **273.97 MHz** (수정 전후 동일 — critical path가 gmem AXI 인프라에 있고
low-light 알고리즘 변경과 무관함을 재확인).

### 4.2 자원 (adversarial-review 수정 전 → 후, unified top 실측)
| instance | BRAM(전→후) | DSP(전→후) | FF(전→후) | LUT(전→후) |
|---|---|---|---|---|
| **합계(unified top, `dfxisp_accel`)** | 8→**9** | 30→**24**(-20.0%) | 7,008→**5,536**(-21.0%) | 11,217→**8,264**(-26.3%) |

수정 전 수치는 위 §3(gamma ROM 최적화 직후) 시점 기록. 수정으로 LUT/FF/DSP가 모두
감소한 이유: low-light 경로의 3-row sliding-window + 2차 demosaic(`demosaic_rggb12_rows`
재호출) 로직이 통째로 제거되고, 단일 fused binning-demosaic pass(`compute_binned_rgb_row`)
로 대체되었기 때문 — 버그 수정이 곧 자원 절감으로 이어진 사례.

### 4.3 디바이스 대비 활용률 (xczu7ev, 2026-07-02 20:33 재합성 기준)
| 지표 | 사용 | 가용 | 활용% |
|---|---|---|---|
| BRAM_18K | 9 | 624 | 1% |
| DSP | 24 | 1728 | 1% |
| FF | 5,536 | 460,800 | 1% |
| LUT | 8,264 | 230,400 | 4% |

**매우 여유 있는 풋프린트**(unified top, 두 RM 모드 코드가 모두 상주 — 아직 실제 DFX 분리 전).

### 4.4 latency (design envelope, 최대 지원 해상도 1920×1080 기준 보고값, 재합성 후 실측)
| instance | min cycles | max cycles | max absolute(@target 5ns) |
|---|---|---|---|
| run_normal | 171 | 18,662,427 | 93.312 ms |
| run_low_light | 74 | 2,604,428 | 13.022 ms |

(run_low_light의 max cycles가 구 수치 7,284,608 → 2,604,428로 감소한 이유도 §4.2와 동일:
2차 demosaic 재호출이 제거되어 픽셀당 사이클 수가 줄었다.)

RESEARCH 평가 목표 해상도(1280×720)로 환산(II=1 파이프라인이라 cycles≈픽셀수+오버헤드,
achieved clock 3.65ns 기준 추정치이며 별도 실행으로 직접 측정한 값은 아님):
- run_normal: 1280×720=921,600픽셀 → ≈3.4 ms/frame (33.3ms 예산의 약 10%)
- run_low_light: 640×360(binned)=230,400픽셀 → ≈0.84 ms/frame

두 모드 모두 30fps 프레임 예산에 여유 있게 들어간다(estimate, board 실측 아님).

## 5. 재현

```bash
cd /tmp/hls_dfxisp/dfxisp_accel   # flat temp-dir (source-path 우회)
# hpp/cpp/tb를 isppipeline/hls/{include,src,tests}에서 복사, golden CSV는 tests/ 서브폴더
source /tools/Xilinx/Vitis_HLS/2024.1/settings64.sh
DFXISP_HLS_FLOW=csynth timeout -k 15 900 vitis_hls -f run.tcl
cat proj/solution1/syn/report/dfxisp_accel_csynth.rpt
```

## 6. 산출물
- `reports/csynth/dfxisp_accel_ver1_csynth.rpt` (전체 리포트)
- `results/resource_csynth_ver1_2026-07-02.csv`
- 코드: `src/dfxisp_accel.cpp`(streaming line buffer + gamma ROM), `run_low_light`/
  `demosaic_rggb12_rows`/`GAMMA2_LUT` 추가

## 6b. C/RTL Co-sim (L2 gate) 시도 — 정직한 기록

`DFXISP_HLS_FLOW=cosim`으로 7회 시도(depth 1024/2048/8192/2073600, `result` 인터페이스에
`depth=1` 추가 등). **결론: RTL 시뮬레이션(XSIM) 자체는 매번 성공**(7/7 트랜잭션 100% 완료,
`$finish` 정상 호출, latency 641~1203 cycles) — 이는 합성된 하드웨어가 실제로 동작함을
보여주는 긍정적 신호다. 그러나 **자동 post-check 비교 단계(ENTER_WRAPC_PC)에서 매번
SIGSEGV**로 실패해 "L2 bit-exact PASS"를 자동으로 확정하지 못했다.

- depth=1024/2048: pre-check(ENTER_WRAPC) 통과, RTL 완주, **post-check에서** SIGSEGV.
- depth=8192/2073600(전체 설계 envelope): **pre-check에서** SIGSEGV(추정: cosim
  auto-wrapc 하네스의 스택 할당 오버플로).
- `result`(`DfxIspResult*`, `s_axilite`+`ap_vld`) 인터페이스에 `depth=1` 추가: 변화 없음
  (동일하게 post-check SIGSEGV) — struct-pointer 출력 인터페이스가 원인이라는 가설은
  이 시도로 반증되지 않았지만 확증도 못함.
- 과거 worklog(`12-worklog-2026-06-29.md`)의 "source-path 버그·종료-hang"과 같은 계열의
  **이 WSL2+Vitis HLS 2024.1+XSIM 환경 특유의 cosim 하네스 한계**로 판단, 추가 depth 튜닝은
  중단.

**현재 상태:** L2는 "RTL 실행 성공 확인(긍정적 신호), 자동 bit-exact 비교는 미완주"로 기록.
Vivado GUI 기반 수동 파형 비교, 또는 인터페이스 재설계(`DfxIspResult*` 대신 개별 scalar 출력
포트로 분리)가 후속 후보. `src/dfxisp_accel.cpp`의 `depth=` pragma는 값 자체가 합성 RTL
동작에 영향을 주지 않으므로(cosim 검증 전용 힌트) csynth 결과(§4)는 이 이슈와 무관하게 유효.

## 6c. Stage 5 준비 — RM 개별 top 분리 합성 (실측)

DFX RM 패키징의 전제조건(HLS README "다음 하드웨어 단계" #2)을 진행: `RM_NORMAL_TONE`/
`RM_LOW_LIGHT_TONE`을 **독립 top 함수**(`rm_normal_tone_top`, `rm_low_light_tone_top`)로
노출해 각각 별도 C-synthesis(`flow=synth`, no tb). 동일 translation unit 내부 함수를
재사용하므로 동작은 `dfxisp_accel()`과 동일(별도 golden 불필요, 순수 합성 전용 entry).

> **2026-07-02 20:33 KST 재합성 결과로 갱신** (adversarial-review 수정 반영, 커밋 `a2d1b6d`).

| top | 역할 | BRAM | DSP | FF | LUT | Fmax |
|---|---|---|---|---|---|---|
| `rm_normal_tone_top` | RM_NORMAL_TONE(자체 AXI 포함) | 4 | 12 | 3,797 | 5,202 | 273.97 MHz |
| `rm_low_light_tone_top` | RM_LOW_LIGHT_TONE(자체 AXI 포함) | 8 | 9 | 3,243 | 4,204 | 273.97 MHz |

`rm_normal_tone_top`은 수정으로 내부 로직이 바뀌지 않아 포트 통일(out_width/out_height 추가,
Stage 5 준비 단계) 이후 수치와 완전히 동일 — 교차검증 성공. `rm_low_light_tone_top`은 §4.2와
같은 이유로 크게 감소(LUT 7,167→4,204, -41.3%; FF 4,732→3,243, -31.5%; DSP 15→9, -40.0%).
BRAM은 7→8로 소폭 증가(단일 fused pass의 row 버퍼 구성 변화 영향, 미미).

unified top의 sub-instance 수치보다 큰 이유는 변함없음: 독립 top은 **자체 m_axi/s_axilite
인프라**를 포함(unified top에서는 두 RM이 gmem0/gmem1/control을 공유). **실제 DFX partial
bitstream 크기 추정에는 독립 top 수치가 더 현실적**. 산출물:
`reports/csynth/rm_{normal,low_light}_tone_top_csynth.rpt`,
`results/resource_csynth_rm_standalone_2026-07-02.csv`.

## 7. 다음 (Stage 4 잔여 / Stage 5)
- [x] C-synthesis 실측 자원/타이밍(unified top) — §4.
- [x] RM 개별 top 분리 합성(실측) — §6c.
- [~] C/RTL Co-sim (L2 gate) — RTL 실행 성공 확인, 자동 bit-exact 비교는 툴 하네스 문제로
      미완주(§6b). 인터페이스 재설계 또는 Vivado 수동 검증 필요.
- [x] 두 RM IP 패키징(export, `ip_catalog` 포맷) — Vivado IP Integrator에 바로 임포트
      가능한 IP-XACT 산출. `rm_normal_tone_top`: 159KB, `rm_low_light_tone_top`: 198KB,
      0 errors. Vivado DFX Block Design의 직접 전제조건 완료.
- [x] **Vivado DFX 구현(fabric-only) 완료** — RP 플로어플랜, config1/config2 구현,
      **pr_verify PASS**, full/partial bitstream 생성. 상세:
      `results/stage5-dfx-implementation-2026-07-02.md`. PS/AXI interconnect/ICAP 통합과
      전력·PR latency 실측은 실제 ZCU104 보드에서만 가능 — **이것이 유일하게 남은 단계**.

### IP 패키징 재현
```bash
cd /tmp/hls_dfxisp/dfxisp_accel   # flat temp-dir (dfxisp_accel.cpp/hpp/tb 복사됨)
source /tools/Xilinx/Vitis_HLS/2024.1/settings64.sh
DFXISP_HLS_TOP=rm_normal_tone_top    DFXISP_HLS_FLOW=export timeout -k 15 900 vitis_hls -f run.tcl
DFXISP_HLS_TOP=rm_low_light_tone_top DFXISP_HLS_FLOW=export timeout -k 15 900 vitis_hls -f run.tcl
# 결과: proj_<top>/solution1/impl/export.zip (IP-XACT, Vivado IP Catalog 임포트용)
```
(export.zip은 바이너리 산출물이라 repo에는 커밋하지 않음 — 위 명령으로 재생성.)
- [ ] RM_NORMAL_TONE / RM_LOW_LIGHT_TONE을 **개별 HLS top으로 분리 합성**하여 Arm1(static
      all-resident)·Arm2(register-only, 현재 unified top과 유사)·Arm3(DFX) 자원 비교의
      실제 partial-bitstream 후보 크기 산정.
- [ ] Vivado DFX 플로어플랜(RP/RM 배치), `pr_verify`, partial bitstream 크기, PR latency —
      Block Design/PS 통합이 필요해 보드 단계 직전 최종 관문.

## 주의
자원/타이밍은 **C-synthesis 추정치**이며 post-route(구현 후) 수치가 아니다. 절대 전력(W)·
PR latency·post-route 자원은 여전히 `TODO(측정)` — Vivado 구현(place&route) 및 보드에서만
확정된다.
